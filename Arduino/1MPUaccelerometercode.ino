#include <Wire.h>
#include <Wifi.h>
#include <HTTPClient.h>

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

bool empty = true;
bool running = false;
const int WINDOW = 20;   // 20 samples × 50 ms = 1 second
const int WINDOWTWO = 30;   // 20 samples × 50 ms = 1 second
int idx = 0;
int idxtwo = 0;

float
  mpu_a_x,
  mpu_a_y,
  mpu_a_z,
  mpu_a_mag,
  prev_mag = 0.0,
  deltas[WINDOW],
  activities[WINDOWTWO],
  activity = 0,
  avg30 = 0, //easier way just always keep track of the average
  sum30 = 0 //so we can calucate a new average every second
;

//wifi credentials OLIVER ADD:
const char* ssid = "ENTER YOUR WIFI NAME";
const char* password = "ENTER YOUR PASSWORD";


void setup() {
  for (int i = 0; i < WINDOW; i++) {
    deltas[i] = 0.0;
  }
  for (int i = 0; i < WINDOWTWO; i++) {
  activities[i] = 0.0;
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
  deltas[idx] = delta; //record the delta now to delete in 20 samples from now
  activity += delta;

  idx = (idx + 1) % WINDOW;

  if (idx == 0){ //Once every second, do this:
    sum30 -= activities[idxtwo];
    activities[idxtwo] = activity;
    sum30 += activities[idxtwo];
    idxtwo = (idxtwo + 1) % WINDOWTWO;

    avg30 = sum30 / WINDOWTWO;
    if (avg30 > 14){
      running = true;
      empty = false;
    }
    else {
      running = false;
    }
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
