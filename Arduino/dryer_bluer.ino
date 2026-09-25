#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_eap_client.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "secrets.h"
#include <NimBLEDevice.h>
#include <WiFiClientSecure.h>

// Leave at 0 until you have confirmed a 200 with it set to 1: if the bundle is
// unavailable every upload fails, which is worse than your original setInsecure().
#define USE_TLS_CERT_BUNDLE 0
#if USE_TLS_CERT_BUNDLE
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
#endif

// University of Waterloo eduroam configuration.
// Replace only the password before uploading.
// Do not include < or > around the password. A ! does not need escaping.
const char* EDUROAM_SSID = "eduroam";
const char* EAP_IDENTITY = "anonymous@uwaterloo.ca";
const char* EXPECTED_SERVER_NAME = "eduroam.uwaterloo.ca";

#define MPU_ADDR 0x68  // I2C address from datasheet (AD0 should be logic low, wire to GND)
// x high 3B, x low 3C, y high 3D, y low 3E, z high 3F, z low 40
#define ACCEL_REG 0x3B
#define ACCEL_SCALE_REG 0x1C
#define POWER_REG 0x6B
#define SIGNAL_PATH_RESET_REG 0x68

// Set accelerometer range to ±16g (scale = 3)
// ACCEL_SCALE = 0      1    2    3
// Range is   +- 2g     4g   8g   16g
// sens (LSB/g)= 16384  8192 4096 2048
static const int LSB_SENS_TABLE[4]{ 16384, 8192, 4096, 2048 };
#define ACCEL_SCALE 3
const float LSB_SENS = LSB_SENS_TABLE[ACCEL_SCALE];  //SAMEPLE TIME 10s, SLEEPTIME CYCLE, 10min

// Read by supervisorTask on core 0, written by loop() on core 1.
volatile bool empty = true;
volatile bool running = false;
bool wasRunning = false;  // edge detection for the serial log only

// State the server has actually acknowledged.
bool lastReportedRunning = false;
bool lastReportedEmpty = true;
bool reportedOnce = false;

// Global status tracking for the washer captured over Bluetooth
volatile bool washerRunning = false;
volatile bool washerEmpty = true;
volatile bool washerHeard = false;  // never relay defaults before the first packet
volatile bool washerMicOk = true;
volatile bool washerMicKnown = false;  // false when the washer is on pre-3-byte firmware
bool lastRelayedWasherRunning = false;
bool lastRelayedWasherEmpty = true;
bool lastRelayedWasherMicOk = true;
bool washerRelayedOnce = false;
portMUX_TYPE washerMux = portMUX_INITIALIZER_UNLOCKED;
unsigned long lastBleScanTime = 0;
const unsigned long bleScanInterval = 20000;  // 2 s scan out of every 20 s

NimBLEScan* pBLEScan;

// Wi-Fi reconnect timing
unsigned long lastWifiReconnectAttempt = 0;
unsigned long wifiBackoffMs = 15000;
const unsigned long WIFI_BACKOFF_MAX = 300000UL;
const unsigned long WIFI_HARD_RESET_MS = 180000UL;  // offline this long -> full radio re-init
bool wifiIsDown = false;
unsigned long wifiDownSince = 0;
volatile bool wifiReinitRequested = false;  // set from the HTTP path, serviced in loop()

// ---------------- Reliability supervision ----------------
// Everything below exists so the node can never end up in a state that only a physical power cycle can clear.
volatile unsigned long loopHeartbeat = 0;
const unsigned long LOOP_STALL_TIMEOUT_MS = 120000UL;  // loop frozen this long -> reboot
uint64_t preventiveRebootUs = 0;            // 24 h + per-node jitter, set in setup()
const uint64_t ABSOLUTE_REBOOT_US = 604800000ULL * 1000ULL;  // 7 days, regardless of state
const int MAX_HTTP_FAILURES = 5;
const int MAX_I2C_FAILURES = 10;

int consecutiveHttpFailures = 0;
int consecutiveI2cFailures = 0;
bool sensorHealthy = true;

// Reported to the dashboard so a sensor fault can be told apart from a node
// fault: "flat" means the sensor answers but never changes, "noreply" means it
// is not on the bus at all. Both point at the wiring, not the ESP32.
const char* SENSOR_OK = "ok";
const char* SENSOR_FLAT = "flat";
const char* SENSOR_NOREPLY = "noreply";
const char* SENSOR_UNKNOWN = "unknown";
const char* sensorState = SENSOR_OK;

uint32_t nodeSalt = 0;  // per-node timer jitter so the fleet never reboots in lockstep

// A live accelerometer always jitters in its low bits. Bit-identical readings
// mean it has frozen while still answering on the bus - the failure that lets a
// machine go undetected for hours while the node still looks perfectly healthy.
int16_t lastRawX = 0, lastRawY = 0, lastRawZ = 0;
int identicalReadCount = 0;
const int MAX_IDENTICAL_READS = 100;  // 5 s at the 50 ms sample rate

// Per-target upload pacing: backoff applies only after a failure, so a genuine
// state change is never delayed, and neither machine can starve the other.
struct UploadSlot {
  unsigned long nextAttempt = 0;
  unsigned long backoff = 0;
};
UploadSlot dryerSlot;
UploadSlot washerSlot;
const unsigned long UPLOAD_BACKOFF_MAX = 120000UL;

bool sendCustomStatusUpdate(const char* targetMachineId, bool isRunning, bool isEmpty,
                            const char* stateStr, bool isRelay);
bool record_mpu_accel();
bool resetMpu();
bool setup_mpu();
bool write_to(const byte to_register, const byte write_value);
bool read_from(const byte from_register, const int num_bytes, byte read_data[]);
void connectToEduroam();
void hardRestart(const char* why);
const int WINDOW = 20;      // 20 samples × 50 ms = 1 second, used to make activity variable
const int WINDOWTWO = 10;   // 10 samples of activity variable = 10 seconds of data
const int WINDOWTHREE = 4;  // 4 samples × 50 ms = 0.2, used to make activity small variable, shorter time frame here to detect quick things like door latch
int idx = 0;                //Index variables to store array values
int idxtwo = 0;
int idxthree = 0;
int activitysmallIdx = 0;
//int quiettime = 0;     //Time between door opening and door opening, used to detect each event properly (seperatly)
//int doorCooldown = 0;  //Used to stop door opening from becoming true due to the closing vibration in the case where the user is closing the door right after opening it

float
  mpu_a_x,
  mpu_a_y,
  mpu_a_z,
  mpu_a_mag,
  prev_mag = 0.0,
  activitysmall = 0,
  activity = 0,
  deltas[WINDOW],         //Arrays that hold the values which make up the activity variables
  activities[WINDOWTWO],  //Used to caluclates avg of last 15 seconds of activity
  deltastwo[WINDOWTHREE],
  activitysmallHistory[3],  //Holds history of last three small acitivties to capture the small activity 0.15s ago (3 x 50ms)
  DYRERONTHRESHHOLD = 2.45,  //Thresholds
  DOORCLOSINGCHANGE = 0.65,
  avg10 = 0,  //Average activity over last 15 seconds (easier way just always keep track of the average)
  sum10 = 0   //used to calucate a new average every second
  ;

// Machine identification and server configuration
const char* machineId = "a1-m20";  // VARIES
const char* serverUrl = "https://laun-dryer.vercel.app/api/machines";
const char* washerMachineId = "a1-m19";  //VARIES

// Timing for sending updates (send every 5 seconds)
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 300000;  // 5 min

class MyAdvertisedDeviceCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    if (advertisedDevice->getName() != "WASHER_M19") {  //VARIES: must equal WASHER_BLE_NAME in the paired washer sketch
      return;
    }

    std::string data = advertisedDevice->getManufacturerData();
    if (data.length() < 2) {
      return;
    }

    portENTER_CRITICAL(&washerMux);
    washerRunning = (data[0] == '1');
    washerEmpty = (data[1] == '1');
    if (data.length() >= 3) {
      washerMicOk = (data[2] == '1');
      washerMicKnown = true;
    }
    washerHeard = true;
    portEXIT_CRITICAL(&washerMux);

    Serial.printf("[BLE RX] washer packet rssi=%d running=%c empty=%c mic=%c\n",
                  advertisedDevice->getRSSI(), data[0], data[1],
                  data.length() >= 3 ? data[2] : '?');
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

void hardRestart(const char* why) {
  Serial.printf("[SUPERVISOR] Restart: %s\n", why);
  Serial.flush();
  delay(50);
  esp_restart();
  // If esp_restart() ever wedges in a shutdown handler, the task watchdog fires.
}

void connectToEduroam() {
  Serial.println("[WIFI] Configuring secure Waterloo eduroam connection...");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_11dBm);  // halves peak TX current on a shared supply
  // WiFi.onEvent() is registered once in setup(): it has no dedup and would leak here.

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
    EAP_PASSWORD);

  const unsigned long connectionTimeout = 30000;
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < connectionTimeout) {
    delay(500);
    loopHeartbeat = millis();  // this wait is expected; it must not trip the stall guard
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Initial eduroam connection timed out.");
    Serial.println("[WIFI] The monitor will keep running and retry automatically.");
  }
}

void maintainWiFiConnection() {
  unsigned long currentTime = millis();

  // Serviced here, never inside the HTTP path, so a POST can never nest a 30 s connect.
  if (wifiReinitRequested) {
    wifiReinitRequested = false;
    Serial.println("[WIFI] Upload failures requested a clean re-join.");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(200);
    connectToEduroam();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiIsDown = false;
    wifiBackoffMs = 15000;
    return;
  }

  if (!wifiIsDown) {  // a flag, not a millis()==0 sentinel
    wifiIsDown = true;
    wifiDownSince = currentTime;
  }

  unsigned long downFor = currentTime - wifiDownSince;

  if (currentTime - lastWifiReconnectAttempt < wifiBackoffMs) {
    return;
  }
  lastWifiReconnectAttempt = currentTime;

  // WPA2-Enterprise sessions expire. WiFi.reconnect() alone cannot always redo
  // the EAP handshake, so escalate to a full stack teardown.
  if (downFor >= WIFI_HARD_RESET_MS) {
    Serial.println("[WIFI] Soft reconnect is not recovering. Re-initialising the EAP session...");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(200);
    connectToEduroam();
  } else {
    Serial.println("[WIFI] Attempting to reconnect to eduroam...");
    WiFi.reconnect();
  }

  // Exponential backoff plus per-node and per-attempt jitter, so a campus-wide
  // outage cannot make every node on one supply retry and reboot in lockstep.
  wifiBackoffMs = min(wifiBackoffMs * 2, WIFI_BACKOFF_MAX);
  wifiBackoffMs += (nodeSalt % 5000) + random(0, 5000);
}

bool verifyMpu() {
  byte who = 0;
  if (!read_from(0x75, 1, &who)) {
    return false;
  }
  return (who == 0x68 || who == 0x70 || who == 0x98);
}

// Clears a slave that is holding SDA low after a glitch. A CPU reset cannot fix
// that on its own, which is why only unplugging the board used to help.
bool recoverI2cBus() {
  Serial.println("[I2C] Bus appears wedged. Running clock-pulse recovery...");
  Wire.end();

  // OUTPUT_OPEN_DRAIN alone does not enable the internal pull-up, so a released
  // line would float and the recovery would silently do nothing.
  pinMode(22, OUTPUT_OPEN_DRAIN | PULLUP);  // SCL
  pinMode(21, OUTPUT_OPEN_DRAIN | PULLUP);  // SDA, still readable while open-drain
  digitalWrite(21, HIGH);
  digitalWrite(22, HIGH);
  delayMicroseconds(10);

  bool freed = false;
  for (int i = 0; i < 9 && !freed; i++) {
    digitalWrite(22, LOW);
    delayMicroseconds(5);
    digitalWrite(22, HIGH);
    delayMicroseconds(5);
    if (digitalRead(21) == HIGH) {
      freed = true;
    }
  }

  // Proper STOP: SCL low -> SDA low -> SCL high -> SDA high.
  digitalWrite(22, LOW);
  delayMicroseconds(5);
  digitalWrite(21, LOW);
  delayMicroseconds(5);
  digitalWrite(22, HIGH);
  delayMicroseconds(5);
  digitalWrite(21, HIGH);
  delayMicroseconds(5);

  Serial.printf("[I2C] SDA %s after clocking.\n", freed ? "released" : "STILL STUCK");

  Wire.begin(21, 22);
  Wire.setTimeOut(50);
  setup_mpu();

  // Re-baseline the freeze detector against whatever comes back next.
  identicalReadCount = 0;
  lastRawX = 0x7FFF;
  lastRawY = 0x7FFF;
  lastRawZ = 0x7FFF;

  if (verifyMpu()) {
    Serial.println("[I2C] Recovery verified (WHO_AM_I responded).");
    return true;
  }

  // No reboot here on purpose: setup_mpu() already issues a full device reset,
  // so restarting the ESP32 would do nothing extra for the sensor.
  Serial.println("[I2C] Recovery FAILED: sensor did not respond.");
  sensorHealthy = false;
  sensorState = SENSOR_NOREPLY;
  return false;
}

// Runs on core 0 so it keeps ticking even when loop() is blocked inside a
// driver call that never returns.
void supervisorTask(void* parameter) {
  esp_task_wdt_add(NULL);  // hardware backstop in case the supervisor itself stalls

  for (;;) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5000));

    if (millis() - loopHeartbeat > LOOP_STALL_TIMEOUT_MS) {
      hardRestart("main loop stalled");
    }

    uint64_t upUs = (uint64_t)esp_timer_get_time();  // 64-bit: never rolls over

    // Not-running only: a door sensor that never reports empty must not be able
    // to keep a node running forever.
    if (upUs >= preventiveRebootUs && !running) {
      hardRestart("preventive restart while idle");
    } else if (upUs >= ABSOLUTE_REBOOT_US) {
      hardRestart("7-day absolute cap");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  nodeSalt = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
  randomSeed(nodeSalt ^ esp_random());
  preventiveRebootUs = (uint64_t)(86400000UL + (nodeSalt % 21600000UL)) * 1000ULL;  // 24 h + up to ~4.7 h

  Serial.println("\n\n========================================");
  Serial.println("   Dryer Monitor");
  Serial.printf("   Machine ID: %s\n", machineId);
  Serial.printf("   Reset reason: %d\n", esp_reset_reason());
  Serial.println("========================================\n");

  // Staggered boot so four nodes on one supply never draw inrush together.
  delay(nodeSalt % 12000);

  loopHeartbeat = millis();

  esp_task_wdt_config_t wdtCfg = {
    .timeout_ms = 30000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&wdtCfg);

  xTaskCreatePinnedToCore(supervisorTask, "supervisor", 4096, NULL, 1, NULL, 0);

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
  for (int i = 0; i < 3; i++) {
    activitysmallHistory[i] = 0.0;
  }
  prev_mag = 0;
  idx = 0;

  // Initialize I2C for accelerometer
  Wire.begin(21, 22);
  Wire.setTimeOut(50);  // never block the loop forever on a stuck slave

  // Registered once and only once: WiFi.onEvent() has no deduplication.
  WiFi.persistent(false);  // stop rewriting the same credentials to flash on every retry
  WiFi.onEvent(onWiFiEvent);

  // Connect securely to University of Waterloo eduroam
  connectToEduroam();

  // Initialize accelerometer (mpu.initialize() would set ±2g then be overwritten)
  setup_mpu();
  sensorHealthy = verifyMpu();
  Serial.printf("[MPU] WHO_AM_I check: %s\n", sensorHealthy ? "ok" : "FAILED");

  // Initialize background Bluetooth Sniffer engine
  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(false);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->start(2000, false);  // 2000 ms = 2 seconds
  loopHeartbeat = millis();
  Serial.println("[BLE] Initial 2-second startup scan complete.");
}

// Backoff applies only after a failure, and each target has its own slot so the
// dryer and the relayed washer can never starve one another.
bool tryUpload(UploadSlot& slot, const char* id, bool isRunning, bool isEmpty,
               const char* stateStr, bool isRelay) {
  if ((long)(millis() - slot.nextAttempt) < 0) {  // rollover-safe
    return false;
  }
  if (sendCustomStatusUpdate(id, isRunning, isEmpty, stateStr, isRelay)) {
    slot.backoff = 0;
    slot.nextAttempt = millis();
    return true;
  }
  slot.backoff = slot.backoff ? min(slot.backoff * 2, UPLOAD_BACKOFF_MAX) : 5000UL;
  slot.nextAttempt = millis() + slot.backoff + random(0, 2000);
  return false;
}

// BLE scan, washer relay and dryer reporting. Split out of loop() so it still runs
// while the accelerometer is failing: a dead MPU must not silence the washer relay
// or stop this node reporting the fault it is in.
void serviceRadios() {
  unsigned long currentTime = millis();

  // Checks the air for 2 seconds, then completely turns off BLE for 18 seconds
  if (currentTime - lastBleScanTime >= bleScanInterval) {
    lastBleScanTime = currentTime;

    Serial.printf("[BLE] Sniffing the air for washer %s updates...\n", washerMachineId);

    // Scans for exactly 2 seconds, runs your callback if a packet is found, then stops
    pBLEScan->start(2000, false);  // 2000 ms = 2 seconds

    // Crucial: Clear RAM immediately so the BLE cache doesn't crash eduroam
    pBLEScan->clearResults();
    loopHeartbeat = millis();

    Serial.println("[BLE] Scan complete. Radio handed back to eduroam.");
  }

  // WASHER NETWORK ROUTER: snapshot the pair atomically so we can never relay a
  // torn combination, and compare-and-clear so a change during a POST is not lost.
  bool wr, we, heard, micOk, micKnown;
  portENTER_CRITICAL(&washerMux);
  wr = washerRunning;
  we = washerEmpty;
  heard = washerHeard;
  micOk = washerMicOk;
  micKnown = washerMicKnown;
  portEXIT_CRITICAL(&washerMux);

  if (heard && (!washerRelayedOnce || wr != lastRelayedWasherRunning || we != lastRelayedWasherEmpty
                || micOk != lastRelayedWasherMicOk)) {
    const char* washerState = !micKnown ? SENSOR_UNKNOWN : (micOk ? SENSOR_OK : SENSOR_FLAT);
    if (tryUpload(washerSlot, washerMachineId, wr, we, washerState, true)) {
      lastRelayedWasherRunning = wr;
      lastRelayedWasherEmpty = we;
      lastRelayedWasherMicOk = micOk;
      washerRelayedOnce = true;
    }
  }

  if (!reportedOnce || running != lastReportedRunning || empty != lastReportedEmpty) {
    if (tryUpload(dryerSlot, machineId, running, empty, sensorState, false)) {
      lastReportedRunning = running;
      lastReportedEmpty = empty;
      reportedOnce = true;
      lastSendTime = currentTime;
    }
  }

  if (currentTime - lastSendTime >= sendInterval) {
    if (tryUpload(dryerSlot, machineId, running, empty, sensorState, false)) {
      lastReportedRunning = running;
      lastReportedEmpty = empty;
      reportedOnce = true;
      lastSendTime = currentTime;  // only on success, or the server TTL flaps
    }
  }
}

void loop() {
  loopHeartbeat = millis();

  maintainWiFiConnection();

  if (!record_mpu_accel()) {
    if (++consecutiveI2cFailures >= MAX_I2C_FAILURES) {
      consecutiveI2cFailures = 0;
      recoverI2cBus();
    }
    serviceRadios();  // keep relaying the washer and reporting our own sensor fault
    delay(50);
    return;  // keep stale samples out of the activity window
  }
  consecutiveI2cFailures = 0;
  sensorHealthy = true;
  sensorState = SENSOR_OK;

  mpu_a_mag = sqrt(mpu_a_x * mpu_a_x + mpu_a_y * mpu_a_y + mpu_a_z * mpu_a_z);

  float delta = abs(mpu_a_mag - prev_mag);
  prev_mag = mpu_a_mag;

  activity -= deltas[idx];   //Minusing the delta 20 samples ago to keep the activity limited to just the most recent 20 samples
  deltas[idx] = delta;       //record the delta now (to delete in 20 samples from now)
  activity += delta;         //add it to variable
  idx = (idx + 1) % WINDOW;  //increment index

  activitysmall -= deltastwo[idxthree];
  deltastwo[idxthree] = delta;
  activitysmall += delta;
  idxthree = (idxthree + 1) % WINDOWTHREE;

  activitysmallHistory[activitysmallIdx] = activitysmall;  //Store the value of small acitivty so later we can compre the old one (0.3s ago) to the current
  activitysmallIdx = (activitysmallIdx + 1) % 3;           //increment index

  if (idx == 0) {  //Once every second, do this:
    sum10 -= activities[idxtwo];
    activities[idxtwo] = activity;
    sum10 += activities[idxtwo];  //sum10 holds sum of last 10 activities recorded
    idxtwo = (idxtwo + 1) % WINDOWTWO;

    avg10 = sum10 / WINDOWTWO;

    if (avg10 > DYRERONTHRESHHOLD) {
      running = true;
      empty = false;
    } else {
      running = false;
    }
  }

  float activitysmall3ago = activitysmallHistory[activitysmallIdx];  //This stores value of small acitivty change from 0.15 seconds ago to compare it to current small activity change to see if door opened/closed
  //As idxthree was incremented before this, it now points to the item of the array that was recorded last, 3 cycles ago (0.15s)

  if (!running && !empty) {
    if (activitysmall > (activitysmall3ago + DOORCLOSINGCHANGE)) {
      empty = true;
      Serial.println("[STATE] Door event detected -> marking EMPTY");
    }
  }

  if (running && !wasRunning) {
    Serial.println("▶️ Machine started running");
  }
  if (!running && wasRunning) {
    Serial.println("🛑 Machine stopped");
  }
  wasRunning = running;

  serviceRadios(); //So that we can call function twice even if our mpu is failing, to still upload washers data

  print_accels();
  delay(50);
}

// Full device reset. Clock pulses only free a bus the sensor is holding; this
// clears internal state a glitched or browned-out sensor is stuck in.
bool resetMpu() {
  if (!write_to(POWER_REG, 0x80)) {  // DEVICE_RESET, self-clears when finished
    return false;
  }
  delay(100);

  bool cleared = false;
  for (int i = 0; i < 10 && !cleared; i++) {
    byte pwr = 0xFF;
    if (read_from(POWER_REG, 1, &pwr) && (pwr & 0x80) == 0) {
      cleared = true;
    } else {
      delay(10);
    }
  }

  write_to(SIGNAL_PATH_RESET_REG, 0x07);  // gyro + accel + temp signal paths
  delay(100);
  return cleared;
}

bool setup_mpu() {
  resetMpu();  // always start from a known state, not whatever it was stuck in
  // Wake from sleep
  bool ok = write_to(POWER_REG, 0x00);
  // Set accelerometer scale
  ok = write_to(ACCEL_SCALE_REG, ACCEL_SCALE) && ok;
  return ok;
}

bool read_from(const byte from_register, const int num_bytes, byte read_data[]) {
  // First send address of register from which to read
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(from_register);
  // Keep control of bus to immediately read data
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if ((int)Wire.requestFrom(MPU_ADDR, num_bytes, true) != num_bytes) {  // Releases bus after
    return false;
  }
  for (int i = 0; i < num_bytes && Wire.available(); ++i) {
    read_data[i] = Wire.read();
  }
  return true;
}

bool write_to(const byte to_register, const byte write_value) {
  Wire.beginTransmission(MPU_ADDR);
  // First send address of register to which to write
  Wire.write(to_register);
  Wire.write(write_value);
  return Wire.endTransmission(true) == 0;
}

bool record_mpu_accel() {
  // x high byte 3B, x low 3C, y high 3D, y low 3E, z high 3F, z low 40
  // ACCEL_REG is 3B
  byte buffer[6];
  if (!read_from(ACCEL_REG, 6, buffer)) {
    sensorState = SENSOR_NOREPLY;
    return false;
  }
  int16_t a_x_raw = (buffer[0] << 8) | buffer[1];
  int16_t a_y_raw = (buffer[2] << 8) | buffer[3];
  int16_t a_z_raw = (buffer[4] << 8) | buffer[5];
  // Combine high byte and low byte into 16-bit accel value, then divide by LSB sensitivity to get accel in g-forces
  // Also factor in offsets
  mpu_a_x = a_x_raw / LSB_SENS;
  mpu_a_y = a_y_raw / LSB_SENS;
  mpu_a_z = a_z_raw / LSB_SENS;

  // Sensor lost its configuration, or the bus is returning padding / all-ones.
  if (a_x_raw == 0 && a_y_raw == 0 && a_z_raw == 0) {
    sensorState = SENSOR_NOREPLY;
    return false;
  }
  if (a_x_raw == -1 && a_y_raw == -1 && a_z_raw == -1) {
    sensorState = SENSOR_NOREPLY;
    return false;
  }

  if (a_x_raw == lastRawX && a_y_raw == lastRawY && a_z_raw == lastRawZ) {
    if (identicalReadCount < MAX_IDENTICAL_READS) {
      identicalReadCount++;
    }
  } else {
    identicalReadCount = 0;
    lastRawX = a_x_raw;
    lastRawY = a_y_raw;
    lastRawZ = a_z_raw;
  }
  if (identicalReadCount >= MAX_IDENTICAL_READS) {
    if (sensorHealthy) {  // log the transition only, not every 50 ms after it
      Serial.println("[MPU] FROZEN: identical readings, sensor answering but not sampling.");
    }
    sensorHealthy = false;
    sensorState = SENSOR_FLAT;
    return false;
  }

  // Gravity guarantees a reading well clear of zero. No upper bound: full scale
  // across three axes is 27.7 g and a door slam legitimately spikes high, so a
  // ceiling here would reject the very samples the door detector needs.
  float mag = sqrtf(mpu_a_x * mpu_a_x + mpu_a_y * mpu_a_y + mpu_a_z * mpu_a_z);
  if (mag < 0.2f) {
    sensorState = SENSOR_FLAT;
    return false;
  }
  return true;
}

bool sendCustomStatusUpdate(const char* targetMachineId, bool isRunning, bool isEmpty,
                            const char* stateStr, bool isRelay) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("❌ Network dropped: Deferred update payload for ");
    Serial.println(targetMachineId);
    return false;
  }

  // 1. Create a secure network transport client layer
  WiFiClientSecure client;

#if USE_TLS_CERT_BUNDLE
  // The full bundle, not a single pinned root: a CA rotation by the host must
  // never be able to take the whole fleet offline at once.
  client.setCACertBundle(rootca_crt_bundle_start);
#else
  client.setInsecure();
#endif

  // 2. Initialize your HTTP layer and pass the secure client into it
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(client, serverUrl)) {  // Passes your client object and destination url together
    Serial.println("❌ Could not start the HTTPS session");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
#ifdef DEVICE_AUTH_TOKEN
  // Define DEVICE_AUTH_TOKEN in secrets.h once the API checks it. Without this
  // anyone who sees one request can set any machine to any state from anywhere.
  http.addHeader("Authorization", "Bearer " DEVICE_AUTH_TOKEN);
#endif

  // Fixed buffer instead of String concatenation: no transient small allocations
  // dropped into freed TLS regions, and no silent empty payload on alloc failure.
  char jsonPayload[224];
  if (isRelay) {
    // Uptime, heap and reset reason belong to this node, not to the washer we are
    // speaking for, so they are left out rather than reported as the washer's.
    snprintf(jsonPayload, sizeof(jsonPayload),
             "{\"machineId\":\"%s\",\"running\":%s,\"empty\":%s,"
             "\"sensorState\":\"%s\"}",
             targetMachineId,
             isRunning ? "true" : "false",
             isEmpty ? "true" : "false",
             stateStr);
  } else {
    snprintf(jsonPayload, sizeof(jsonPayload),
             "{\"machineId\":\"%s\",\"running\":%s,\"empty\":%s,"
             "\"sensorState\":\"%s\","
             "\"resetReason\":%d,\"uptime\":%lu,\"freeHeap\":%u}",
             targetMachineId,
             isRunning ? "true" : "false",
             isEmpty ? "true" : "false",
             stateStr,
             (int)esp_reset_reason(),
             (unsigned long)(esp_timer_get_time() / 1000000),
             (unsigned)ESP.getFreeHeap());
  }

  int httpResponseCode = http.POST((uint8_t*)jsonPayload, strlen(jsonPayload));
  http.end();
  loopHeartbeat = millis();

  bool ok = (httpResponseCode >= 200 && httpResponseCode < 300);

  if (ok) {
    Serial.printf("✅ Server Status [%s]: %d (running=%d empty=%d heap=%u)\n",
                  targetMachineId, httpResponseCode, isRunning, isEmpty,
                  (unsigned)ESP.getFreeHeap());
    consecutiveHttpFailures = 0;
    return true;
  }

  Serial.printf("❌ Transmission Error [%s]: %d (freeHeap=%u maxAlloc=%u)\n",
                targetMachineId, httpResponseCode,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  if (++consecutiveHttpFailures >= MAX_HTTP_FAILURES) {
    Serial.println("[WIFI] Repeated upload failures. Requesting a clean re-join.");
    consecutiveHttpFailures = 0;
    wifiReinitRequested = true;  // serviced in loop(); never nest a 30 s connect here
  }

  return false;
}

void print_accels() {
  Serial.print("Avg10:");
  Serial.print(avg10);
  Serial.print(",");
  Serial.print("ActivitySmall:");
  Serial.println(activitysmall);
}