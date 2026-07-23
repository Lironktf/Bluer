#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <MPU6050.h>
#include "esp_eap_client.h"
#include "secrets.h"
#include <NimBLEDevice.h>
#include <WiFiClientSecure.h>

// University of Waterloo eduroam configuration.
// Replace only the password before uploading.
// Do not include < or > around the password. A ! does not need escaping.
const char* EDUROAM_SSID = "eduroam";
const char* EAP_IDENTITY = "anonymous@uwaterloo.ca";
const char* EXPECTED_SERVER_NAME = "eduroam.uwaterloo.ca";

#define MPU_ADDR 0x68 // I2C address from datasheet (AD0 should be logic low, wire to GND)
// x high 3B, x low 3C, y high 3D, y low 3E, z high 3F, z low 40
#define ACCEL_REG 0x3B
#define ACCEL_SCALE_REG 0x1C
#define POWER_REG 0x6B

// Set accelerometer range to ±16g (scale = 3)
// ACCEL_SCALE = 0      1    2    3
// Range is   +- 2g     4g   8g   16g
// sens (LSB/g)= 16384  8192 4096 2048
static const int LSB_SENS_TABLE[4] {16384, 8192, 4096, 2048};
#define ACCEL_SCALE 3
const float LSB_SENS = LSB_SENS_TABLE[ACCEL_SCALE];  //SAMEPLE TIME 10s, SLEEPTIME CYCLE, 10min

MPU6050 mpu;

bool empty = true; //Two status booleans sent to website
bool running = false;
bool dooropened = false; //Two door latch booleans to help determine if clothes have been taken out
bool doorclosed = false;
bool wasRunning = false;
bool wasEmpty = true;

// Global status tracking for the washer captured over Bluetooth
volatile bool washerRunning = false;
volatile bool washerEmpty = true;
bool lastSentWasherRunning = false;
bool lastSentWasherEmpty = true;
volatile bool washerChanged = false; // "volatile" ensures safe multi-threaded core operations
unsigned long lastBleScanTime = 0;
const unsigned long bleScanInterval = 20000; // 10 seconds total cycle time

NimBLEScan* pBLEScan;

// Wi-Fi reconnect timing
unsigned long lastWifiReconnectAttempt = 0;
const unsigned long wifiReconnectInterval = 15000;
const int WINDOW = 20;   // 20 samples × 50 ms = 1 second, used to make activity variable
const int WINDOWTWO = 10;   // 10 samples of activity variable = 10 seconds of data
const int WINDOWTHREE = 4;   // 4 samples × 50 ms = 0.2, used to make activity small variable, shorter time frame here to detect quick things like door latch
int idx = 0; //Index variables to store array values
int idxtwo = 0;
int idxthree = 0;
int activitysmallIdx = 0;
int quiettime = 0; //Time between door opening and door opening, used to detect each event properly (seperatly)
int doorCooldown = 0; //Used to stop door opening from becoming true due to the closing vibration in the case where the user is closing the door right after opening it

float
  mpu_a_x,
  mpu_a_y,
  mpu_a_z,
  mpu_a_mag,
  prev_mag = 0.0,
  activitysmall = 0,
  activity = 0,
  deltas[WINDOW], //Arrays that hold the values which make up the activity variables
  activities[WINDOWTWO], //Used to caluclates avg of last 15 seconds of activity
  deltastwo[WINDOWTHREE],
  activitysmallHistory[6], //Holds history of last six small acitivties to capture the small activity 0.3s ago (6 x 50ms)
  DYRERONTHRESHHOLD = 4, //Thresholds
  DOOROPENINGCHANGE = 0.45,
  DOORCLOSINGCHANGE = 0.9,
  avg10 = 0, //Average activity over last 15 seconds (easier way just always keep track of the average)
  sum10 = 0 //used to calucate a new average every second
;

// Machine identification and server configuration
const char* machineId = "a1-m2"; // VARIES
const char* serverUrl = "https://laun-dryer.vercel.app/api/machines";
const char* washerMachineId = "a1-m1"; //VARIES

// Timing for sending updates (send every 5 seconds)
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 300000; // 5 min

class MyAdvertisedDeviceCallbacks: public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
      if (advertisedDevice->getName() == "WASHER_A1") { //VARIES
        std::string data = advertisedDevice->getManufacturerData();

        if (data.length() >= 2) {
          bool currentWasherRunning = (data[0] == '1');
          bool currentWasherEmpty  = (data[1] == '1');

          if (currentWasherRunning != lastSentWasherRunning || currentWasherEmpty != lastSentWasherEmpty) {
            washerRunning = currentWasherRunning;
            washerEmpty = currentWasherEmpty;
            lastSentWasherRunning = currentWasherRunning;
            lastSentWasherEmpty = currentWasherEmpty;
            Serial.println("\n➔ [BLE Event] Washer state shifted! Routing directly to Vercel...");
            washerChanged = true;
          }
        }
      }
    }
};

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WIFI] Associated with an eduroam access point");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println();
      Serial.println("========================================");
      Serial.println("[WIFI] WiFi connected to eduroam!");
      Serial.print("[WIFI] IP address: ");
      Serial.println(WiFi.localIP());
      Serial.print("[WIFI] Signal strength: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      Serial.println("========================================");
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.print("[WIFI] Disconnected. Reason code: ");
      Serial.println(info.wifi_sta_disconnected.reason);
      break;

    default:
      break;
  }
}

void connectToEduroam() {
  Serial.println("[WIFI] Configuring secure Waterloo eduroam connection...");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWiFiEvent);

  // Use Espressif's trusted CA bundle to validate the authentication server.
  esp_eap_client_clear_ca_cert();

  esp_err_t bundleResult = esp_eap_client_use_default_cert_bundle(true);
  Serial.print("[WIFI] Default CA bundle setup: ");
  Serial.println(esp_err_to_name(bundleResult));

  // Require the authentication server certificate to match Waterloo's server.
  esp_err_t domainResult = esp_eap_client_set_domain_name(EXPECTED_SERVER_NAME);
  Serial.print("[WIFI] Server-name validation setup: ");
  Serial.println(esp_err_to_name(domainResult));

  Serial.println("[WIFI] Connecting to eduroam...");

  WiFi.begin(
    EDUROAM_SSID,
    WPA2_AUTH_PEAP,
    EAP_IDENTITY,
    EAP_USERNAME,
    EAP_PASSWORD
  );

  const unsigned long connectionTimeout = 30000;
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < connectionTimeout) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Initial eduroam connection timed out.");
    Serial.println("[WIFI] The monitor will keep running and retry automatically.");
  }
}

void maintainWiFiConnection() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  unsigned long currentTime = millis();

  if (currentTime - lastWifiReconnectAttempt >= wifiReconnectInterval) {
    lastWifiReconnectAttempt = currentTime;
    Serial.println("[WIFI] Attempting to reconnect to eduroam...");
    WiFi.reconnect();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n\n========================================");
  Serial.println("   Dryer Monitor");
  Serial.printf("   Machine ID: %s\n", machineId);
  Serial.println("========================================\n");

    // Initialize arrays
    for (int i = 0; i < WINDOW; i++) {
      deltas[i] = 0.0;
    }
    for (int i = 0; i < WINDOWTWO; i++) {
      activities[i] = 0.0;
    }
    for (int i = 0; i < WINDOWTHREE; i++) {
    deltastwo[i] = 0.0;
    }
    for (int i = 0; i < 6; i++) {
    activitysmallHistory[i] = 0.0;
    }
    prev_mag = 0;
    idx = 0;
    running = false;

  // Initialize I2C for accelerometer
  Wire.begin(21, 22);

  // Connect securely to University of Waterloo eduroam
  connectToEduroam();

  mpu.initialize();

  // Initialize accelerometer
  setup_mpu();

  // Initialize background Bluetooth Sniffer engine
  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(false);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->start(2000, false);   // 2000 ms = 2 seconds
  Serial.println("[BLE] Initial 2-second startup scan complete.");
}

void loop() {
  maintainWiFiConnection();

  record_mpu_accel();
  mpu_a_mag = sqrt(mpu_a_x * mpu_a_x + mpu_a_y * mpu_a_y + mpu_a_z * mpu_a_z);

  float delta = abs(mpu_a_mag - prev_mag);
  prev_mag = mpu_a_mag;

  if (running && !wasRunning){
    Serial.println("▶️ Machine started running");
  }

  wasRunning = running;
  wasEmpty = empty;

  activity -= deltas[idx]; //Minusing the delta 20 samples ago to keep the activity limited to just the most recent 20 samples
  deltas[idx] = delta; //record the delta now (to delete in 20 samples from now)
  activity += delta; //add it to variable
  idx = (idx + 1) % WINDOW; //increment index

  activitysmall -= deltastwo[idxthree]; 
  deltastwo[idxthree] = delta; 
  activitysmall += delta;
  idxthree = (idxthree + 1) % WINDOWTHREE;
  
  activitysmallHistory[activitysmallIdx] = activitysmall; //Store the value of small acitivty so later we can compre the old one (0.3s ago) to the current
  activitysmallIdx = (activitysmallIdx + 1) % 6; //increment index

  if (idx == 0){ //Once every second, do this:
    sum10 -= activities[idxtwo];
    activities[idxtwo] = activity;
    sum10 += activities[idxtwo]; //sum10 holds sum of last 10 activities recorded
    idxtwo = (idxtwo + 1) % WINDOWTWO;

    avg10 = sum10 / WINDOWTWO;

    if (avg10 > DYRERONTHRESHHOLD){
      running = true;
      empty = false;
      dooropened = false;
      doorclosed = false;
    }
    else {
      running = false;
    }
  }

  float activitysmall6ago = activitysmallHistory[activitysmallIdx]; //This stores value of small acitivty change from 0.3 seconds ago to compare it to current small activity change to see if door opened/closed
  //As idxthree was incremented before this, it now points to the item of the array that was recorded last, 6 cycles ago (0.3s)

  if (!running && wasRunning) {
    Serial.println("🛑 Machine stopped");
  }

  //Detect door opening when machine is stopped and not empty (with cooldown**)
  if (running == false && empty == false && dooropened == false && doorCooldown == 0){
      if (activitysmall > (activitysmall6ago + DOOROPENINGCHANGE)){ 
        dooropened = true; 
        quiettime = 0; // reset timer after opening event
        Serial.println("[DOOR] Door opened!");
      }
  }

  // Detect door closing after it has been opened
  //In the last ten seconds
  if (!running && !empty && dooropened && !doorclosed){
      if (activitysmall > (activitysmall6ago + DOORCLOSINGCHANGE) && quiettime > 40){
         doorclosed = true; //quiettime > 40 so that looks for door closing only after door opened has fully finished
         Serial.println("[DOOR] Door closed!");
      }
      quiettime++; // count time between open and close
      if (quiettime > 1800){
        empty = true; //Machine is empty because door has been left open for > 2 minutes
        dooropened = false;
        doorclosed = false;
        quiettime = 0;
        Serial.println("[STATE] Machine is now EMPTY (door left open)");
      }
  }

  // Decide if machine is empty based on open–close duration
  if (dooropened && doorclosed && !empty){
      if (quiettime > 85) {
        empty = true;
        Serial.println("[STATE] Machine is now EMPTY (clothes removed)");
        dooropened = false;
        doorclosed = false;
        quiettime = 0;
      }
      else{ //Niche case, door was closed too fast, user is just checking if clothes are inside, not actually taking them out
        Serial.println("[DOOR] Quick open/close detected - user just checking");
        dooropened = false; doorclosed = false;
        doorCooldown = 20;
      }
  }

  //20 -> 0, one second cooldown for niche case
  if (doorCooldown > 0) {
  doorCooldown--;
  }

  // Send status update to server every 5 min
  unsigned long currentTime = millis();

  // Checks the air for 2 seconds, then completely turns off BLE for 8 seconds
  if (currentTime - lastBleScanTime >= bleScanInterval) {
    lastBleScanTime = currentTime;
    
    Serial.println("[BLE] Sniffing the air for washer updates...");
    
    // Scans for exactly 2 seconds, runs your callback if a packet is found, then stops
    pBLEScan->start(2000, false);   // 2000 ms = 2 seconds
    
    // Crucial: Clear RAM immediately so the BLE cache doesn't crash eduroam
    pBLEScan->clearResults(); 
    
    Serial.println("[BLE] Scan complete. Radio handed back to eduroam.");
  }

    // WASHER NETWORK ROUTER: Handles the washer network transmission safely on main thread time
  if (washerChanged) {
    washerChanged = false; // Reset flag instantly to prevent duplicate triggers
    Serial.println("\n➔ [Dryer Router] Handling deferred Washer payload trigger safely on loop timeline...");
    sendCustomStatusUpdate(washerMachineId, washerRunning, washerEmpty);
  }

  if (empty != wasEmpty || running != wasRunning) {
    // FIX: Only attempt transmission and reset the state tracking variables 
    // IF the eduroam handshake is officially complete!
    if (WiFi.status() == WL_CONNECTED) {
      sendCustomStatusUpdate(machineId, running, empty);
      wasRunning = running; // Only update these mirrors when successfully processed
      wasEmpty = empty;
      lastSendTime = millis();
    } else {//BEFORE JUST HAD THE TWO SIMPLE LINES
      Serial.println("➔ [Dryer Loop] State shift detected, but eduroam IP handshake is not ready yet. Deferring upload...");
    }
  }

  if (currentTime - lastSendTime >= sendInterval) {
    sendCustomStatusUpdate(machineId, running, empty);
    lastSendTime = currentTime;
  }

  print_accels();
  delay(50);
}

void setup_mpu() {
  // Wake from sleep
  write_to(POWER_REG, 0x00);
  // Set accelerometer scale
  write_to(ACCEL_SCALE_REG, ACCEL_SCALE);
}

void read_from(const byte from_register, const int num_bytes, byte read_data[]) {
  // First send address of register from which to read
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(from_register);
  // Keep control of bus to immediately read data
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, num_bytes, true); // Releases bus after
  for (int i = 0; Wire.available(); ++i) {
    read_data[i] = Wire.read();
  }
  return;
}

void write_to(const byte to_register, const byte write_value) {
  Wire.beginTransmission(MPU_ADDR);
  // First send address of register to which to write
  Wire.write(to_register);
  Wire.write(write_value);
  Wire.endTransmission(true);
  return;
}

void record_mpu_accel() {
  // x high byte 3B, x low 3C, y high 3D, y low 3E, z high 3F, z low 40
  // ACCEL_REG is 3B
  byte buffer[6];
  read_from(ACCEL_REG, 6, buffer);
  int16_t a_x_raw = (buffer[0] << 8) | buffer[1];
  int16_t a_y_raw = (buffer[2] << 8) | buffer[3];
  int16_t a_z_raw = (buffer[4] << 8) | buffer[5];
  // Combine high byte and low byte into 16-bit accel value, then divide by LSB sensitivity to get accel in g-forces
  // Also factor in offsets
  mpu_a_x = a_x_raw / LSB_SENS;
  mpu_a_y = a_y_raw / LSB_SENS;
  mpu_a_z = a_z_raw / LSB_SENS;
}

void sendCustomStatusUpdate(const char* targetMachineId, bool isRunning, bool isEmpty) {
  if (WiFi.status() == WL_CONNECTED) {
    // 1. Create a secure network transport client layer
    WiFiClientSecure client;
    
    // FIX: Apply the insecure flag directly to the SECURE CLIENT layer!
    // This tells the radio chip to skip loading the massive cert validation bundle, freeing up RAM.
    client.setInsecure(); 

    // 2. Initialize your HTTP layer and pass the secure client into it
    HTTPClient http;
    http.begin(client, serverUrl); // Passes your client object and destination url together
    
    http.addHeader("Content-Type", "application/json");

    // Properly escaped JSON string builder using the dynamic target parameters
    String jsonPayload = "{\"machineId\":\"" + String(targetMachineId) + "\","
                         "\"running\":" + (isRunning ? "true" : "false") + ","
                         "\"empty\":" + (isEmpty ? "true" : "false") + "}";

    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0) {
      Serial.print("✅ Server Status [");
      Serial.print(targetMachineId);
      Serial.print("]: ");
      Serial.println(httpResponseCode);
    } else {
      //Serial.printf("freeHeap=%u  maxAlloc=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap()); //Was helpfull for testing, not enough power for bursts of ble or wifi, so we used Nim ble for less power
      Serial.print("❌ Transmission Error [");
      Serial.print(targetMachineId);
      Serial.print("]: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  } else {
    Serial.print("❌ Network dropped: Deferred update payload for ");
    Serial.println(targetMachineId);
  }
}

 void print_accels() {
  Serial.print(avg10);                 // Average activity (10 s)
  Serial.print('\t');
  Serial.print(activitysmall);         // Short-term activity
  Serial.print('\t');
  Serial.print(mpu_a_mag);             // Raw acceleration magnitude
  Serial.print('\t');
  Serial.print(running ? 1 : 0);       // Running
  Serial.print('\t');
  Serial.println(empty ? 1 : 0);       // Empty + end of line
}
