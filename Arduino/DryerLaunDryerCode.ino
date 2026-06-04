#include <Wire.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <MPU6050.h>

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

#define SAMPLE_TIME   200     // stay awake for 10 seconds (200x50ms)
#define SLEEP_TIME_CYCLE   15       // sleep for 10 min

MPU6050 mpu;
const int intPin = 15;
volatile bool motionDetected = false;

RTC_DATA_ATTR bool empty = true; //Two status booleans sent to website
RTC_DATA_ATTR bool running = false;
RTC_DATA_ATTR bool dooropened = false; //Two door latch booleans to help determine if clothes have been taken out
RTC_DATA_ATTR bool doorclosed = false;
RTC_DATA_ATTR bool wasRunning = false;
RTC_DATA_ATTR bool wasEmpty = true;
RTC_DATA_ATTR bool monitoringContinuously = false;
RTC_DATA_ATTR bool waitingForDoorClose = false;
const int WINDOW = 20;   // 20 samples × 50 ms = 1 second, used to make activity variable
const int WINDOWTWO = 10;   // 10 samples of activity variable = 10 seconds of data
const int fiveCycle = 5;
RTC_DATA_ATTR int idx = 0; //Index variables to store array values
RTC_DATA_ATTR int idxtwo = 0;
RTC_DATA_ATTR int cycleCounter = 0;
RTC_DATA_ATTR int quiettime = 0; //Time between door opening and door opening, used to detect each event properly (seperatly)
RTC_DATA_ATTR int wakeStart = 0;
int timeWake = 0;
RTC_DATA_ATTR esp_sleep_wakeup_cause_t lastWakeReason;

float
  mpu_a_x,
  mpu_a_y,
  mpu_a_z,
  mpu_a_mag,
  RTC_DATA_ATTR prev_mag = 0.0,
  RTC_DATA_ATTR activity = 0,
  RTC_DATA_ATTR deltas[WINDOW], //Arrays that hold the values which make up the activity variables
  RTC_DATA_ATTR activities[WINDOWTWO], //Used to caluclates avg of last 15 seconds of activity
  DYRERONTHRESHHOLD = 4, //Thresholds
  avg10 = 0, //Average activity over last 15 seconds (easier way just always keep track of the average)
  RTC_DATA_ATTR sum10 = 0 //used to calucate a new average every second
;

// Machine identification
const char* machineId = "a1-m1"; // e.g., "a1-m1", "a2-m5", "b1-m3"

// BLE Advertising object
BLEAdvertising *pAdvertising;

void IRAM_ATTR motionISR(); 
void sleepWaitForPin();

void setup() {
  Serial.begin(38400);
  delay(500);

  Serial.println("\n\n========================================");
  Serial.println("   Wake Laundry Machine Monitor");
  Serial.printf("   Machine ID: %s\n", machineId);
  Serial.println("========================================\n");

  lastWakeReason = esp_sleep_get_wakeup_cause();

  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  bool coldBoot = (wakeReason == ESP_SLEEP_WAKEUP_UNDEFINED);

  if (coldBoot){
    cycleCounter = 0;
    // Initialize arrays
    for (int i = 0; i < WINDOW; i++) {
      deltas[i] = 0.0;
    }
    for (int i = 0; i < WINDOWTWO; i++) {
      activities[i] = 0.0;
    }
    prev_mag = 0;
    idx = 0;
    running = false;
  }

  timeWake = 0;
  motionDetected = false;

  // Initialize I2C for accelerometer
  Wire.begin(21, 22);

  mpu.initialize();
  // Configure INT pin: Active High, Push-Pull, Latch until cleared
  mpu.setInterruptMode(0);  // Active high
  mpu.setInterruptDrive(0); // Push-pull
  mpu.setInterruptLatch(1); // Latch until cleared

  // Motion detection
  mpu.setMotionDetectionThreshold(11);
  mpu.setMotionDetectionDuration(5);  
  mpu.setIntMotionEnabled(true);       // Enable motion interrupt

  // Setup ESP32 GPIO interrupt
  pinMode(intPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(intPin), motionISR, RISING); // Trigger when INT goes HIGH

  // Initialize accelerometer
  setup_mpu();

  // Initialize BLE
  Serial.println("[BLE] Initializing BLE advertising...");
  BLEDevice::init("LaundryMachine");
  pAdvertising = BLEDevice::getAdvertising();

  // Set advertising parameters
  pAdvertising->setMinInterval(0x20);
  pAdvertising->setMaxInterval(0x40);

  // Update advertisement with initial status
  updateAdvertisement(); //COULD GET RID OF IN FUTURE IF USES TOO MUCH BATTERY

  // Start advertising
  BLEDevice::startAdvertising();
  Serial.println("[BLE] Advertising started!");
}

void loop() {
  // Clear MPU interrupt latch after waking
  mpu.getIntStatus(); 

  record_mpu_accel();
  mpu_a_mag = sqrt(mpu_a_x * mpu_a_x + mpu_a_y * mpu_a_y + mpu_a_z * mpu_a_z);

  float delta = abs(mpu_a_mag - prev_mag);
  prev_mag = mpu_a_mag;

  if (running && !wasRunning){
    Serial.println("▶️ Machine started running");
    monitoringContinuously = false;
  }

  if (running && timeWake > SAMPLE_TIME && !monitoringContinuously){
    int sleepSeconds = 0;
    if (cycleCounter == 0) {
        sleepSeconds = 12;   // 2 minutes
        Serial.println("⏳ Stage 0 → Sleeping 2 minutes");
      }
    else if (cycleCounter < 5) {
       sleepSeconds = SLEEP_TIME_CYCLE;   // 10 minutes
       Serial.println("⏳ Stage 1–4 → Sleeping 10 minutes");
    }
    else {
       Serial.println("👀 Entering continuous monitoring mode");
       monitoringContinuously = true; //Now that this is true it wont sleep anymore and will search until machine stops
       return;   // stay awake
     }
    cycleCounter++;
    timeWake = 0;

    // Configure timer wakeup
    mpu.getIntStatus();
    delay(5);
    esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);
    delay(100);
    esp_deep_sleep_start();
  }

  wasRunning = running;
  wasEmpty = empty;

  activity -= deltas[idx]; //Minusing the delta 20 samples ago to keep the activity limited to just the most recent 20 samples
  deltas[idx] = delta; //record the delta now (to delete in 20 samples from now)
  activity += delta; //add it to variable
  idx = (idx + 1) % WINDOW; //increment index

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

  //Go to sleep when stopped running and wait for door open triggered by int pin
  if (!running && wasRunning) {
    Serial.println("🛑 Machine stopped");
    timeWake = 0;
    sleepWaitForPin();
  }

  //Detect door opening when machine is stopped int pin goes high
  if (!running && !empty && !dooropened && lastWakeReason == ESP_SLEEP_WAKEUP_EXT0){
      dooropened = true;
      Serial.println("[DOOR] Door opened!");
      quiettime = 0; // reset timer after opening event
      // stay awake for SAMPLE_TIME to check for running
  }

  // Detect door closing after it has been opened
  //In the last ten seconds
  if (!running && !empty && dooropened && !doorclosed && !waitingForDoorClose){ //last condition so that this code doesnt just repeat forever as same coniditons are sill met
      if (motionDetected) {
        motionDetected = false;
        doorclosed = true;
        waitingForDoorClose = false;
        Serial.printf("[DOOR] Door closed");
      }
      quiettime++; // count time between open and close
      if (quiettime > 200){
        waitingForDoorClose = true;
        Serial.println("😴 Sleeping waiting for door to be closed");
        mpu.getIntStatus();
        delay(5);
        esp_sleep_enable_timer_wakeup(30ULL * 1000000ULL); //then it will reach condition below in 30s
        esp_sleep_enable_ext0_wakeup((gpio_num_t)intPin, 1);  // wake when INT goes HIGH
        esp_deep_sleep_start();
      }
  }

  //Between or after the 30 s that we just went to sleep for after door opened
  if (waitingForDoorClose) {
    // If we slept, door was open long enough
    empty = true;
    Serial.println("[STATE] Machine is now EMPTY (slept while door open)");
    if (empty != wasEmpty || running != wasRunning) { //Update before sleeping
      updateAdvertisement();
      Serial.println("[BLE] Advertisement updated due to state change");
      Serial.print("Running:");
      Serial.println(running ? 1 : 0);
      Serial.println();
      Serial.print("Empty:");
      Serial.println(empty ? 1 : 0);
      Serial.println();
    }
    sleepWaitForPin();
  }

  // Decide if machine is empty based on open–close duration
  if (dooropened && doorclosed && !empty){
      if (quiettime > 85) {
        empty = true;
        Serial.println("[STATE] Machine is now EMPTY (clothes removed)");
        if (empty != wasEmpty || running != wasRunning) { //Update before sleeping
          updateAdvertisement();
          Serial.println("[BLE] Advertisement updated due to state change");
          Serial.print("Running:");
          Serial.println(running ? 1 : 0);
          Serial.println();
          Serial.print("Empty:");
          Serial.println(empty ? 1 : 0);
          Serial.println();
        }
        sleepWaitForPin();
      }
      else{ //Niche case, door was closed too fast, user is just checking if clothes are inside, not actually taking them out
        Serial.println("[DOOR] Quick open/close detected - user just checking");
        dooropened = false; doorclosed = false;
        sleepWaitForPin();
      }
  }

  if (!running && timeWake > SAMPLE_TIME && empty){
    sleepWaitForPin();
  }

  if (!running) {
    cycleCounter = 0;  // reset stages when machine not running
  }

  // Update BLE advertisement when somehting has changed
  if (empty != wasEmpty || running != wasRunning) {
    updateAdvertisement();
    Serial.println("[BLE] Advertisement updated due to state change");
    Serial.print("Running:");
    Serial.println(running ? 1 : 0);
    Serial.println();
    Serial.print("Empty:");
    Serial.println(empty ? 1 : 0);
    Serial.println();
  }

  timeWake++;
  motionDetected = false;

  print_accels();
  delay(50);
}

void IRAM_ATTR motionISR() {
  motionDetected = true;
}

void sleepWaitForPin() {
  Serial.println("😴 Machine idle — sleeping waiting for door interrupt");
  mpu.getIntStatus();
  delay(5);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)intPin, 1);  // wake when INT goes HIGH
  esp_deep_sleep_start();
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
  // Format: Company ID (2 bytes) + MAC address (6 bytes) + Machine ID char (1 byte) + Status (1 byte)
  uint8_t manufData[10];
  // Company ID (0xFFFF for custom/testing)
  manufData[0] = 0xFF;
  manufData[1] = 0xFF;
  // MAC address (device identification)
  uint64_t mac = ESP.getEfuseMac();
  for(int i = 0; i < 6; i++) {
    manufData[2 + i] = (mac >> (i * 8)) & 0xFF;
  }

  // Machine ID (extract last character from machineId, e.g., '1' from "a1-m1")
  const char* idPtr = machineId;
  char machineChar = '0';
  while (*idPtr != '\0') {
    if (*idPtr >= '0' && *idPtr <= '9') {
      machineChar = *idPtr;
    }
    idPtr++;
  }
  manufData[8] = (uint8_t)machineChar;

  // Status byte (bit 0: running, bit 1: empty)
  manufData[9] = (running ? 0x01 : 0x00) | (empty ? 0x02 : 0x00);

  // Set manufacturer data in advertisement
  BLEAdvertisementData advData;
  //advData.setManufacturerData(std::string((char*)manufData, 10));
  String mfg;
  for (int i = 0; i < 10; i++) {
    mfg += (char)manufData[i];
  }
  advData.setManufacturerData(mfg);
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
    Serial.print(' '); 
    Serial.print("change over last second:");
    Serial.print(activity);
    Serial.print(' ');
    Serial.print("mpu_a_mag:");
    Serial.print(mpu_a_mag);
    Serial.println();*/
  }