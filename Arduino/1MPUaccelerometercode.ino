#include <Wire.h>
#include <Wifi.h>
#include <HTTPClient.h>
#include <credentials.h>

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
const float LSB_SENS = LSB_SENS_TABLE[ACCEL_SCALE];

bool empty = true; //Two status booleans sent to website
bool running = false;
bool dooropened = false; //Two door latch booleans to help determine if clothes have been taken out
bool doorclosed = false;
const int WINDOW = 20;   // 20 samples × 50 ms = 1 second, used to make activity variable
const int WINDOWTWO = 15;   // 15 samples of activity variable = 15 seconds of data
const int WINDOWTHREE = 4;   // 4 samples × 50 ms = 0.2, used to make activity small variable, shorter time frame here to detect quick things like door latch
int idx = 0; //Index variables to store array values
int idxtwo = 0;
int idxthree = 0;
int activitysmallIdx = 0;
int quiettime = 0; //Time between door opening and door opening, used to detect each event properly (seperatly) and to catch niche cases
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
  deltastwo[WINDOWTHREE],
  activities[WINDOWTWO], //Used to caluclates avg of last 15 seconds of activity
  DYRERONTHRESHHOLD = 3, //Thresholds
  DOOROPENINGCHANGE = 0.45,
  DOORCLOSINGCHANGE = 0.9,
  activitysmallHistory[6], //Holds history of last six small acitivties to capture the small activity 0.3s ago (6 x 50ms)
  activity = 0,
  avg15 = 0, //Average activity over last 15 seconds (easier way just always keep track of the average)
  sum15 = 0 //used to calucate a new average every second
;

int thresholdForOn = 14;

// Machine identification and server configuration
const char* machineId = "a1-m1"; // e.g., "a1-m1", "a2-m5", "b1-m3"
const char* serverUrl = "https://laun-dryer.vercel.app/api/machines";

// Timing for sending updates (send every 5 seconds)
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 5000; // 5 seconds in milliseconds

void setup() {
  for (int i = 0; i < WINDOW; i++) { //Initializing arrays
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
  
  Serial.begin(38400);
  Wire.begin(21, 22);  // SDA, SCL

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  setup_mpu();
}

void loop() {

  record_mpu_accel();
  mpu_a_mag = sqrt(mpu_a_x * mpu_a_x + mpu_a_y * mpu_a_y + mpu_a_z * mpu_a_z);

  float delta = abs(mpu_a_mag - prev_mag);
  prev_mag = mpu_a_mag;

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
    sum15 -= activities[idxtwo];
    activities[idxtwo] = activity;
    sum15 += activities[idxtwo]; //sum15 holds sum of last 15 activities recorded
    idxtwo = (idxtwo + 1) % WINDOWTWO;

    avg15 = sum15 / WINDOWTWO;
    if (avg15 > DYRERONTHRESHHOLD){
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

  //Detect door opening when machine is stopped and not empty (with cooldown**)
  if (running == false && empty == false && dooropened == false && doorCooldown == 0){
      if (activitysmall > (activitysmall6ago + DOOROPENINGCHANGE)) dooropened = true;
      quiettime = 0; // reset timer after opening event
  }
  // Detect door closing after it has been opened
  if (running == false && empty == false && dooropened == true && doorclosed == false){
      if (activitysmall > (activitysmall6ago + DOORCLOSINGCHANGE) && quiettime > 40) doorclosed = true; //quiettime > 40 so that looks for door closing only after door opened has fully finished
      quiettime++; // count time between open and close
  }
  // Decide if machine is empty based on open–close duration
  if (dooropened == true && doorclosed == true){
      if (quiettime > 80) empty = true;
      else{ //Niche case, door was closed too fast, user is just checking if clothes are inside, not actually taking them out
        dooropened = false; doorclosed = false;
        doorCooldown = 20; //**so that it doesn't go back to first if statement right away so that condition won't be incorrectly satisfied
      } 
  }

  //20 -> 0, one second cooldown for niche case
  if (doorCooldown > 0) {
  doorCooldown--;
  }

  // Send status update to server every 5 seconds
  unsigned long currentTime = millis();
  if (currentTime - lastSendTime >= sendInterval) {
    sendStatusUpdate();
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

void sendStatusUpdate() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    // Create JSON payload
    String jsonPayload = "{\"machineId\":\"" + String(machineId) +
                        "\",\"running\":" + (running ? "true" : "false") +
                        ",\"empty\":" + (empty ? "true" : "false") + "}";

    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0) {
      Serial.print("✅ Status sent: ");
      Serial.println(httpResponseCode);
      String response = http.getString();
      Serial.println(response);
    } else {
      Serial.print("❌ Error sending status: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  } else {
    Serial.println("❌ WiFi not connected");
  }
}

 void print_accels() {
    /*Serial.print("mpu_a_x:");
    Serial.print(mpu_a_x);
    Serial.print(' ');
    Serial.print("mpu_a_y:");
    Serial.print(mpu_a_y);
    Serial.print(' ');
    Serial.print("mpu_a_z:");
    Serial.print(mpu_a_z);
    Serial.print(' '); */
    Serial.print("change over last second:");
    Serial.print(activity);
    Serial.print(' ');
    Serial.print("mpu_a_mag:");
    Serial.print(mpu_a_mag);
    Serial.println();
    Serial.print("Running:");
    Serial.println(running ? 1 : 0);
    Serial.println();
  } 
