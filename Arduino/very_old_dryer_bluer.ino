#include <Wire.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>

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
  avg15 = 0, //Average activity over last 15 seconds (easier way just always keep track of the average)
  sum15 = 0 //used to calucate a new average every second
;

// Machine identification
const char* machineId = "a1-m1"; // e.g., "a1-m1", "a2-m5", "b1-m3"
#define MACHINE_ID_MAX_LEN 16    // Max length for machineId in BLE advertisement

// BLE advertising intervals (in milliseconds)
const unsigned long advIntervalRunning = 10000; // 10 seconds when running
const unsigned long advIntervalIdle = 30000;    // 30 seconds when idle

// Timing for updating BLE advertisement
unsigned long lastAdvUpdate = 0;

// BLE Advertising object
BLEAdvertising *pAdvertising;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n\n========================================");
  Serial.println("   Laundry Machine Monitor");
  Serial.printf("   Machine ID: %s\n", machineId);
  Serial.println("========================================\n");

  // Check free heap
  Serial.printf("[DEBUG] Free heap: %d bytes\n", ESP.getFreeHeap());

  // Initialize arrays
  Serial.println("[INIT] Initializing sensor arrays...");
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

  // Initialize I2C for accelerometer
  Serial.println("[INIT] Setting up I2C (SDA=21, SCL=22)...");
  Wire.begin(21, 22);

  // Initialize accelerometer
  Serial.println("[INIT] Configuring MPU-6050 accelerometer...");
  setup_mpu();

  // Initialize BLE
  Serial.printf("[DEBUG] Free heap before BLE init: %d bytes\n", ESP.getFreeHeap());
  Serial.println("[BLE] Initializing BLE advertising...");
  BLEDevice::init("LaundryMachine");
  Serial.printf("[DEBUG] Free heap after BLE init: %d bytes\n", ESP.getFreeHeap());
  pAdvertising = BLEDevice::getAdvertising();

  // Set advertising parameters
  pAdvertising->setMinInterval(0x20);
  pAdvertising->setMaxInterval(0x40);

  // Update advertisement with initial status
  Serial.println("[BLE] Creating advertisement...");
  updateAdvertisement();
  Serial.println("[BLE] Advertisement created successfully");

  // Start advertising
  Serial.println("[BLE] Starting advertising...");
  BLEDevice::startAdvertising();
  Serial.println("[BLE] Advertising started!");

  Serial.println("\n========================================");
  Serial.println("System ready! Starting monitoring...");
  Serial.println("========================================\n");
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
    bool wasRunning = running;

    if (avg15 > DYRERONTHRESHHOLD){
      if (!wasRunning) {
        Serial.println("[STATE] Machine started running!");
      }
      running = true;
      empty = false;
      dooropened = false;
      doorclosed = false;
    }
    else {
      if (wasRunning) {
        Serial.println("[STATE] Machine stopped");
      }
      running = false;
    }
  }

  float activitysmall6ago = activitysmallHistory[activitysmallIdx]; //This stores value of small acitivty change from 0.3 seconds ago to compare it to current small activity change to see if door opened/closed
  //As idxthree was incremented before this, it now points to the item of the array that was recorded last, 6 cycles ago (0.3s)

  //Detect door opening when machine is stopped and not empty (with cooldown**)
  if (running == false && empty == false && dooropened == false && doorCooldown == 0){
      if (activitysmall > (activitysmall6ago + DOOROPENINGCHANGE)) {
        dooropened = true;
        Serial.println("[DOOR] Door opened!");
      }
      quiettime = 0; // reset timer after opening event
  }
  // Detect door closing after it has been opened
  if (running == false && empty == false && dooropened == true && doorclosed == false){
      if (activitysmall > (activitysmall6ago + DOORCLOSINGCHANGE) && quiettime > 40) {
        doorclosed = true;
        Serial.printf("[DOOR] Door closed (quiet time: %d cycles)\n", quiettime);
      }
      quiettime++; // count time between open and close
      if (quiettime > 2400) empty = true; //Machine is empty because door has been left open for > 2 minutes
  }
  // Decide if machine is empty based on open–close duration
  if (dooropened == true && doorclosed == true){
      if (quiettime > 80) {
        empty = true;
        Serial.println("[STATE] Machine is now EMPTY (clothes removed)");
      }
      else{ //Niche case, door was closed too fast, user is just checking if clothes are inside, not actually taking them out
        Serial.println("[DOOR] Quick open/close detected - user just checking");
        dooropened = false; doorclosed = false;
        doorCooldown = 20; //**so that it doesn't go back to first if statement right away so that condition won't be incorrectly satisfied
      }
  }

  //20 -> 0, one second cooldown for niche case
  if (doorCooldown > 0) {
  doorCooldown--;
  }

  // Update BLE advertisement periodically based on running state
  unsigned long currentTime = millis();
  unsigned long advInterval = running ? advIntervalRunning : advIntervalIdle;

  if (currentTime - lastAdvUpdate >= advInterval) {
    updateAdvertisement();
    lastAdvUpdate = currentTime;
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

void updateAdvertisement() {
  // Create manufacturer data packet
  // Format: Company ID (2 bytes) + Machine ID (16 bytes, null-padded) + Status (1 byte)
  // Total: 19 bytes (room mapping is done on backend based on machineId prefix)
  const int MANUF_DATA_LEN = 2 + MACHINE_ID_MAX_LEN + 1;
  uint8_t manufData[MANUF_DATA_LEN];
  memset(manufData, 0, MANUF_DATA_LEN);

  // Company ID (0xFFFF for custom/testing)
  manufData[0] = 0xFF;
  manufData[1] = 0xFF;

  // Machine ID (full string, null-padded to MACHINE_ID_MAX_LEN bytes)
  int idLen = strlen(machineId);
  if (idLen > MACHINE_ID_MAX_LEN) idLen = MACHINE_ID_MAX_LEN;
  memcpy(&manufData[2], machineId, idLen);

  // Status byte (bit 0: running, bit 1: empty)
  manufData[2 + MACHINE_ID_MAX_LEN] = (running ? 0x01 : 0x00) | (empty ? 0x02 : 0x00);

  BLEAdvertisementData advData;
  String mfgData;
  mfgData.reserve(MANUF_DATA_LEN); 
  for (int i = 0; i < MANUF_DATA_LEN; i++) {
    mfgData += (char)manufData[i];
  }
  advData.setManufacturerData(mfgData);
  pAdvertising->setAdvertisementData(advData);
  
  Serial.printf("[BLE] Advertisement updated - Machine: %s | Running: %s | Empty: %s\n",
               machineId,
               running ? "YES" : "NO",
               empty ? "YES" : "NO");
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
