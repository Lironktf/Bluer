// Combined node: one ESP32-S3 watches a stacked pair. MPU6050 on the dryer for
// vibration, INMP441 aimed at the washer's water valve, both reported over WiFi.
// No BLE. Detection logic is unchanged from dryer_bluer.ino and washer_bluer.ino.
//
// Board: "ESP32S3 Dev Module", Tools > USB CDC On Boot: Enabled.
// Wiring (no shared pins):
//   MPU6050  SDA -> GPIO 8, SCL -> GPIO 9, AD0 -> GND, VCC -> 3V3
//   INMP441  SCK -> GPIO 4, WS -> GPIO 5, SD -> GPIO 6, L/R -> GND, VDD -> 3V3
// Serial plotters, one per USB-C port:
//   "USB"  port (Serial)  : dryer graph  Avg10, ActivitySmall  (+ node/WiFi log)
//   "UART" port (Serial0) : washer graph Door, Avg_7875_9750, Avg_9750_11625
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>
#include <arduinoFFT.h>
#include "esp_eap_client.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "secrets.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This sketch is for the ESP32-S3 (pins and dual serial ports differ on other chips)"
#endif
#if !ARDUINO_USB_CDC_ON_BOOT
#error "Set Tools > USB CDC On Boot > Enabled so the dryer and washer get separate serial ports"
#endif

#define DryerSerial Serial    // native "USB" port
#define WasherSerial Serial0  // "UART" port

// Leave at 0 until you have confirmed a 200 with it set to 1: if the bundle is
// unavailable every upload fails, which is worse than your original setInsecure().
#define USE_TLS_CERT_BUNDLE 0
#if USE_TLS_CERT_BUNDLE
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
#endif

// University of Waterloo eduroam. Username and password come from secrets.h.
const char* EDUROAM_SSID = "eduroam";
const char* EAP_IDENTITY = "anonymous@uwaterloo.ca";
const char* EXPECTED_SERVER_NAME = "eduroam.uwaterloo.ca";

// ---------------- Pins ----------------
#define MPU_SDA 8
#define MPU_SCL 9
#define I2S_SCK 4
#define I2S_WS 5
#define I2S_SD 6
#define I2S_PORT I2S_NUM_0

// ---------------- Machine identification ----------------
const char* dryerMachineId = "sj-d1";   // VARIES
const char* washerMachineId = "sj-w1";  // VARIES
const char* serverUrl = "https://laun-dryer.vercel.app/api/machines";
const unsigned long sendInterval = 300000;  // 5 min heartbeat per machine

// ---------------- Dryer (MPU6050) ----------------
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
const float LSB_SENS = LSB_SENS_TABLE[ACCEL_SCALE];

bool dryerEmpty = true;
bool dryerRunning = false;
bool dryerWasRunning = false;  // edge detection for the serial log only

const int WINDOW = 20;      // 20 samples × 50 ms = 1 second, used to make activity variable
const int WINDOWTWO = 10;   // 10 samples of activity variable = 10 seconds of data
const int WINDOWTHREE = 4;  // 4 samples × 50 ms = 0.2, used to make activity small variable, shorter time frame here to detect quick things like door latch
int idx = 0;                //Index variables to store array values
int idxtwo = 0;
int idxthree = 0;
int activitysmallIdx = 0;

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
  activitysmallHistory[3],   //Holds history of last three small acitivties to capture the small activity 0.15s ago (3 x 50ms)
  DYRERONTHRESHHOLD = 2.45,  // VARIES, per-device, see Arduino/MACHINES.md
  DOORCLOSINGCHANGE = 0.65,  // VARIES, per-device, see Arduino/MACHINES.md
  avg10 = 0,                 //Average activity over last 15 seconds (easier way just always keep track of the average)
  sum10 = 0                  //used to calucate a new average every second
  ;

const int MAX_I2C_FAILURES = 10;
int consecutiveI2cFailures = 0;
bool sensorHealthy = true;

// "flat" means the sensor answers but never changes, "noreply" means it is not
// on the bus at all. Both point at the wiring, not the ESP32.
const char* SENSOR_OK = "ok";
const char* SENSOR_FLAT = "flat";
const char* SENSOR_NOREPLY = "noreply";
const char* sensorState = SENSOR_OK;

// A live accelerometer always jitters in its low bits. Bit-identical readings
// mean it has frozen while still answering on the bus.
int16_t lastRawX = 0, lastRawY = 0, lastRawZ = 0;
int identicalReadCount = 0;
const int MAX_IDENTICAL_READS = 100;  // 5 s at the 50 ms sample rate

// ---------------- Washer (INMP441) ----------------
#define SAMPLES 512
#define SAMPLE_RATE 44100

bool washerEmpty = true;
bool washerRunning = false;
bool washerDoorClosed = false;
const int WASHER_WINDOW = 200;  // 200 samples × 25 ms = 5 second, considering other delays, more like 10s of averaging
int washerIdx = 0;
double HighFreqWindow[WASHER_WINDOW];
double AvgHighFreq = 0.0;
double HighFreqWindow2[WASHER_WINDOW];
double AvgHighFreq2 = 0.0;
double lowmid = 0.0;
double high = 0.0;
double high2 = 0.0;
unsigned long cycleEndsAt = 0;
const unsigned long TIMER_FULL_MS = 1500000UL;    // 25 min, start of cycle
const unsigned long TIMER_REFILL_MS = 600000UL;   // VARIES, per-device, see Arduino/MACHINES.md
const double FIRST_FILL_HIGH2 = 68500;            // either band, starts the cycle
const double REFILL_HIGH = 32000;                 // VARIES, either band, a later water in tops the timer back up
const double DOOR_DROP = 750000;                  // VARIES, lowmid drop that marks the door closed
unsigned long cycleStartedAt = 0;
const unsigned long MAX_CYCLE_MS = 2520000UL;  // 42 min hard cap

const int MAX_I2S_FAILURES = 20;
int consecutiveI2sFailures = 0;

// A live INMP441 always has a noise floor, so bit-identical samples mean there is
// no signal path. i2s_read still returns ESP_OK with a zero-filled buffer when the
// mic is unpowered or unwired, so this is the only way that fault becomes visible.
const int MAX_FLAT_READS = 200;  // ~5 s at the 25 ms loop
int flatReadCount = 0;
bool micHealthy = true;

double vReal[SAMPLES];
double vImag[SAMPLES];
int32_t i2s_buffer[SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLE_RATE);

// ---------------- Shared between tasks ----------------
// Each sensing task publishes a consistent snapshot here; the network task and
// the supervisor only ever read these.
portMUX_TYPE dryerMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE washerMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool pubDryerRunning = false;
volatile bool pubDryerEmpty = true;
const char* volatile pubDryerSensorState = "ok";
volatile bool pubWasherRunning = false;
volatile bool pubWasherEmpty = true;
volatile bool pubWasherMicOk = true;

// ---------------- Network ----------------
unsigned long lastWifiReconnectAttempt = 0;
unsigned long wifiBackoffMs = 15000;
const unsigned long WIFI_BACKOFF_MAX = 300000UL;
const unsigned long WIFI_HARD_RESET_MS = 180000UL;  // offline this long -> full radio re-init
bool wifiIsDown = false;
unsigned long wifiDownSince = 0;
bool wifiReinitRequested = false;  // set from the HTTP path, serviced in maintainWiFiConnection()

const int MAX_HTTP_FAILURES = 5;
int consecutiveHttpFailures = 0;

// Per-target upload pacing: backoff applies only after a failure, so a genuine
// state change is never delayed, and neither machine can starve the other.
struct UploadSlot {
  unsigned long nextAttempt = 0;
  unsigned long backoff = 0;
};
UploadSlot dryerSlot;
UploadSlot washerSlot;
const unsigned long UPLOAD_BACKOFF_MAX = 120000UL;

bool dryerReportedOnce = false;
bool lastReportedDryerRunning = false;
bool lastReportedDryerEmpty = true;
unsigned long lastDryerSendTime = 0;

bool washerReportedOnce = false;
bool lastReportedWasherRunning = false;
bool lastReportedWasherEmpty = true;
bool lastReportedWasherMicOk = true;
unsigned long lastWasherSendTime = 0;

// ---------------- Reliability supervision ----------------
volatile unsigned long dryerHeartbeat = 0;
volatile unsigned long washerHeartbeat = 0;
volatile unsigned long netHeartbeat = 0;
const unsigned long SENSOR_STALL_TIMEOUT_MS = 60000UL;  // a sensing loop frozen this long -> reboot
const unsigned long NET_STALL_TIMEOUT_MS = 120000UL;    // covers a 30 s connect plus slow HTTPS
uint64_t preventiveRebootUs = 0;                             // 24 h + per-node jitter, set in setup()
const uint64_t ABSOLUTE_REBOOT_US = 604800000ULL * 1000ULL;  // 7 days, regardless of state
uint32_t nodeSalt = 0;  // per-node timer jitter so the fleet never reboots in lockstep

bool sendCustomStatusUpdate(const char* targetMachineId, bool isRunning, bool isEmpty, const char* stateStr);
bool record_mpu_accel();
bool resetMpu();
bool setup_mpu();
bool write_to(const byte to_register, const byte write_value);
bool read_from(const byte from_register, const int num_bytes, byte read_data[]);
void connectToEduroam();
void hardRestart(const char* why);
esp_err_t setup_i2s();

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      DryerSerial.println("[WIFI] Associated with an eduroam access point");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      DryerSerial.printf("[WIFI] Connected to eduroam. IP %s, signal %d dBm\n",
                         WiFi.localIP().toString().c_str(), WiFi.RSSI());
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      DryerSerial.printf("[WIFI] Disconnected. Reason code: %d\n", info.wifi_sta_disconnected.reason);
      break;

    default:
      break;
  }
}

void hardRestart(const char* why) {
  DryerSerial.printf("[SUPERVISOR] Restart: %s\n", why);
  WasherSerial.printf("[SUPERVISOR] Restart: %s\n", why);
  WasherSerial.flush();
  delay(50);
  esp_restart();
  // If esp_restart() ever wedges in a shutdown handler, the task watchdog fires.
}

void connectToEduroam() {
  DryerSerial.println("[WIFI] Configuring secure Waterloo eduroam connection...");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_11dBm);  // halves peak TX current on a shared supply
  // WiFi.onEvent() is registered once in setup(): it has no dedup and would leak here.

  esp_eap_client_clear_ca_cert();
  esp_err_t bundleResult = esp_eap_client_use_default_cert_bundle(true);
  DryerSerial.printf("[WIFI] Default CA bundle setup: %s\n", esp_err_to_name(bundleResult));

  // Require the authentication server certificate to match Waterloo's server.
  esp_err_t domainResult = esp_eap_client_set_domain_name(EXPECTED_SERVER_NAME);
  DryerSerial.printf("[WIFI] Server-name validation setup: %s\n", esp_err_to_name(domainResult));

  DryerSerial.println("[WIFI] Connecting to eduroam...");
  WiFi.begin(EDUROAM_SSID, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_USERNAME, EAP_PASSWORD);

  const unsigned long connectionTimeout = 30000;
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < connectionTimeout) {
    delay(500);
    netHeartbeat = millis();  // this wait is expected; it must not trip the stall guard
  }

  if (WiFi.status() != WL_CONNECTED) {
    DryerSerial.println("[WIFI] Initial eduroam connection timed out. Will keep retrying.");
  }
}

void maintainWiFiConnection() {
  unsigned long currentTime = millis();

  if (wifiReinitRequested) {
    wifiReinitRequested = false;
    DryerSerial.println("[WIFI] Upload failures requested a clean re-join.");
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

  if (!wifiIsDown) {
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
    DryerSerial.println("[WIFI] Soft reconnect is not recovering. Re-initialising the EAP session...");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(200);
    connectToEduroam();
  } else {
    DryerSerial.println("[WIFI] Attempting to reconnect to eduroam...");
    WiFi.reconnect();
  }

  // Exponential backoff plus per-node and per-attempt jitter, so a campus-wide
  // outage cannot make every node on one supply retry and reboot in lockstep.
  wifiBackoffMs = min(wifiBackoffMs * 2, WIFI_BACKOFF_MAX);
  wifiBackoffMs += (nodeSalt % 5000) + random(0, 5000);
}

// ---------------- Dryer sensor ----------------
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
  DryerSerial.println("[I2C] Bus appears wedged. Running clock-pulse recovery...");
  Wire.end();

  // OUTPUT_OPEN_DRAIN alone does not enable the internal pull-up, so a released
  // line would float and the recovery would silently do nothing.
  pinMode(MPU_SCL, OUTPUT_OPEN_DRAIN | PULLUP);
  pinMode(MPU_SDA, OUTPUT_OPEN_DRAIN | PULLUP);  // still readable while open-drain
  digitalWrite(MPU_SDA, HIGH);
  digitalWrite(MPU_SCL, HIGH);
  delayMicroseconds(10);

  bool freed = false;
  for (int i = 0; i < 9 && !freed; i++) {
    digitalWrite(MPU_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(MPU_SCL, HIGH);
    delayMicroseconds(5);
    if (digitalRead(MPU_SDA) == HIGH) {
      freed = true;
    }
  }

  // Proper STOP: SCL low -> SDA low -> SCL high -> SDA high.
  digitalWrite(MPU_SCL, LOW);
  delayMicroseconds(5);
  digitalWrite(MPU_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(MPU_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(MPU_SDA, HIGH);
  delayMicroseconds(5);

  DryerSerial.printf("[I2C] SDA %s after clocking.\n", freed ? "released" : "STILL STUCK");

  Wire.begin(MPU_SDA, MPU_SCL);
  Wire.setTimeOut(50);
  setup_mpu();

  // Re-baseline the freeze detector against whatever comes back next.
  identicalReadCount = 0;
  lastRawX = 0x7FFF;
  lastRawY = 0x7FFF;
  lastRawZ = 0x7FFF;

  if (verifyMpu()) {
    DryerSerial.println("[I2C] Recovery verified (WHO_AM_I responded).");
    return true;
  }

  // No reboot here on purpose: setup_mpu() already issues a full device reset,
  // so restarting the ESP32 would do nothing extra for the sensor.
  DryerSerial.println("[I2C] Recovery FAILED: sensor did not respond.");
  sensorHealthy = false;
  sensorState = SENSOR_NOREPLY;
  return false;
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
  bool ok = write_to(POWER_REG, 0x00);                // Wake from sleep
  ok = write_to(ACCEL_SCALE_REG, ACCEL_SCALE) && ok;  // Set accelerometer scale
  return ok;
}

bool read_from(const byte from_register, const int num_bytes, byte read_data[]) {
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
  Wire.write(to_register);
  Wire.write(write_value);
  return Wire.endTransmission(true) == 0;
}

bool record_mpu_accel() {
  byte buffer[6];
  if (!read_from(ACCEL_REG, 6, buffer)) {
    sensorState = SENSOR_NOREPLY;
    return false;
  }
  int16_t a_x_raw = (buffer[0] << 8) | buffer[1];
  int16_t a_y_raw = (buffer[2] << 8) | buffer[3];
  int16_t a_z_raw = (buffer[4] << 8) | buffer[5];
  // Combine high byte and low byte into 16-bit accel value, then divide by LSB sensitivity to get accel in g-forces
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
      DryerSerial.println("[MPU] FROZEN: identical readings, sensor answering but not sampling.");
    }
    sensorHealthy = false;
    sensorState = SENSOR_FLAT;
    return false;
  }

  // Gravity guarantees a reading well clear of zero. No upper bound: a door slam
  // legitimately spikes high, and the door detector needs those samples.
  float mag = sqrtf(mpu_a_x * mpu_a_x + mpu_a_y * mpu_a_y + mpu_a_z * mpu_a_z);
  if (mag < 0.2f) {
    sensorState = SENSOR_FLAT;
    return false;
  }
  return true;
}

void publishDryer() {
  portENTER_CRITICAL(&dryerMux);
  pubDryerRunning = dryerRunning;
  pubDryerEmpty = dryerEmpty;
  pubDryerSensorState = sensorState;
  portEXIT_CRITICAL(&dryerMux);
}

// ---------------- Washer sensor ----------------
esp_err_t setup_i2s() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  esp_err_t r = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (r != ESP_OK) {
    WasherSerial.printf("[I2S] driver_install failed: %d\n", r);
    return r;
  }
  r = i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
  if (r != ESP_OK) {
    WasherSerial.printf("[I2S] set_clk failed: %d\n", r);
    return r;
  }
  r = i2s_set_pin(I2S_PORT, &pin_config);
  if (r != ESP_OK) {
    WasherSerial.printf("[I2S] set_pin failed: %d\n", r);
    return r;
  }
  i2s_zero_dma_buffer(I2S_PORT);
  return ESP_OK;
}

void publishWasher() {
  portENTER_CRITICAL(&washerMux);
  pubWasherRunning = washerRunning;
  pubWasherEmpty = washerEmpty;
  pubWasherMicOk = micHealthy;
  portEXIT_CRITICAL(&washerMux);
}

// One pass of the original washer loop().
void washerStep() {
  size_t bytes_read = 0;
  // Never block forever here: a wedged I2S peripheral used to freeze the node
  // until it was unplugged.
  esp_err_t i2sResult = i2s_read(I2S_PORT, &i2s_buffer, sizeof(i2s_buffer), &bytes_read, pdMS_TO_TICKS(500));

  // A short read is a failure too: the driver returns ESP_OK with a partial
  // count on timeout, and zero-padding that into the FFT fakes a water-fill.
  if (i2sResult != ESP_OK || bytes_read != sizeof(i2s_buffer)) {
    if (++consecutiveI2sFailures >= MAX_I2S_FAILURES) {
      WasherSerial.printf("[I2S] Stream dead (err=%d, got %u/%u). Reinstalling...\n",
                          i2sResult, (unsigned)bytes_read, (unsigned)sizeof(i2s_buffer));
      i2s_driver_uninstall(I2S_PORT);
      delay(100);
      setup_i2s();  // logs its own failure; a dead mic cannot be fixed in software
      consecutiveI2sFailures = 0;
    }
    return;
  }
  consecutiveI2sFailures = 0;

  bool flat = true;
  for (int i = 1; i < SAMPLES; i++) {
    if (i2s_buffer[i] != i2s_buffer[0]) {
      flat = false;
      break;
    }
  }
  if (flat) {
    if (flatReadCount < MAX_FLAT_READS) flatReadCount++;
  } else {
    flatReadCount = 0;
  }
  if ((flatReadCount >= MAX_FLAT_READS) == micHealthy) {  // report the edge only
    micHealthy = !micHealthy;
    WasherSerial.printf("[MIC] %s\n", micHealthy ? "signal restored" : "NO SIGNAL: samples flat, mic dead or unwired");
  }

  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = (double)(i2s_buffer[i] >> 14);
    vImag[i] = 0.0;
  }

  FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(FFT_FORWARD);
  FFT.complexToMagnitude();

  double binResolution = (double)SAMPLE_RATE / SAMPLES;

  double zone_lowmid = 0;  // 60Hz to 600Hz Door closed
  double zone_high = 0;    // 7875 to 9750
  double zone_high2 = 0;   // 9750 to 11625

  int c_lowmid = 0, c_high = 0, c_high2 = 0;

  for (int i = 2; i < (SAMPLES / 2); i++) {
    double freq = i * binResolution;

    if (freq >= 60 && freq < 600) {
      zone_lowmid += vReal[i];
      c_lowmid++;
    } else if (freq >= 7875 && freq < 9750) {
      zone_high += vReal[i];
      c_high++;
    } else if (freq >= 9750 && freq < 11625) {
      zone_high2 += vReal[i];
      c_high2++;
    }
  }

  // Calculate averages to keep the graph stable
  double prevlowmid = lowmid;
  lowmid = (c_lowmid > 0) ? (zone_lowmid / c_lowmid) : 0;
  high = (c_high > 0) ? (zone_high / c_high) : 0;
  high2 = (c_high2 > 0) ? (zone_high2 / c_high2) : 0;

  if ((prevlowmid > (lowmid + DOOR_DROP)) && !washerRunning && !washerDoorClosed) {
    washerDoorClosed = true;
    washerEmpty = true;
    WasherSerial.println("Door Closed! Now Empty");
  }

  AvgHighFreq -= HighFreqWindow[washerIdx];
  HighFreqWindow[washerIdx] = high;
  AvgHighFreq += high;

  AvgHighFreq2 -= HighFreqWindow2[washerIdx];
  HighFreqWindow2[washerIdx] = high2;
  AvgHighFreq2 += high2;

  washerIdx = (washerIdx + 1) % WASHER_WINDOW;

  double actualAvgWater = AvgHighFreq / WASHER_WINDOW;
  double actualAvgWater2 = AvgHighFreq2 / WASHER_WINDOW;

  if (!washerRunning && (actualAvgWater2 > FIRST_FILL_HIGH2 || actualAvgWater > FIRST_FILL_HIGH2)) {
    washerRunning = true;
    washerEmpty = false;
    washerDoorClosed = false;
    cycleStartedAt = millis();
    cycleEndsAt = millis() + TIMER_FULL_MS;  //Cycle will be done at current time + 25min
    WasherSerial.println("▶️ Cycle started (first fill detected)");
  }

  if (washerRunning) {
    // Hard ceiling: no cycle runs longer than 42 min, no matter how many refills fired
    if ((long)(millis() - cycleStartedAt) >= (long)MAX_CYCLE_MS) {
      washerRunning = false;
      WasherSerial.println("🛑 Cycle force-ended (42 min hard cap reached)");
    } else {
      // Unsigned on purpose: once the timer has passed, this is false and adds no time.
      if ((actualAvgWater > REFILL_HIGH || actualAvgWater2 > REFILL_HIGH) && (cycleEndsAt - millis() < TIMER_REFILL_MS)) {
        cycleEndsAt = millis() + TIMER_REFILL_MS;
        WasherSerial.println("💧 Refill detected -> timer topped up");
      }

      if ((long)(millis() - cycleEndsAt) >= 0) {
        washerRunning = false;
        WasherSerial.println("🛑 Cycle ended (timer expired)");
      }
    }
  }

  WasherSerial.printf("Door_60-600Hz:%.2f,Avg_7875_9750:%.2f,Avg_9750_11625:%.2f\n",
                      lowmid, actualAvgWater, actualAvgWater2);
}

void washerTask(void* parameter) {
  for (;;) {
    washerHeartbeat = millis();
    washerStep();
    publishWasher();
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

// ---------------- Uploads ----------------
bool sendCustomStatusUpdate(const char* targetMachineId, bool isRunning, bool isEmpty, const char* stateStr) {
  if (WiFi.status() != WL_CONNECTED) {
    DryerSerial.printf("❌ Network dropped: Deferred update payload for %s\n", targetMachineId);
    return false;
  }

  WiFiClientSecure client;
#if USE_TLS_CERT_BUNDLE
  // The full bundle, not a single pinned root: a CA rotation by the host must
  // never be able to take the whole fleet offline at once.
  client.setCACertBundle(rootca_crt_bundle_start);
#else
  client.setInsecure();
#endif

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(client, serverUrl)) {
    DryerSerial.println("❌ Could not start the HTTPS session");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
#ifdef DEVICE_AUTH_TOKEN
  // Define DEVICE_AUTH_TOKEN in secrets.h once the API checks it.
  http.addHeader("Authorization", "Bearer " DEVICE_AUTH_TOKEN);
#endif

  // Both machines share this node, so its uptime, heap and reset reason are true
  // for either one and are sent with both.
  char jsonPayload[224];
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

  int httpResponseCode = http.POST((uint8_t*)jsonPayload, strlen(jsonPayload));
  http.end();
  netHeartbeat = millis();

  bool ok = (httpResponseCode >= 200 && httpResponseCode < 300);

  if (ok) {
    DryerSerial.printf("✅ Server Status [%s]: %d (running=%d empty=%d heap=%u)\n",
                       targetMachineId, httpResponseCode, isRunning, isEmpty,
                       (unsigned)ESP.getFreeHeap());
    consecutiveHttpFailures = 0;
    return true;
  }

  DryerSerial.printf("❌ Transmission Error [%s]: %d (freeHeap=%u maxAlloc=%u)\n",
                     targetMachineId, httpResponseCode,
                     (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  if (++consecutiveHttpFailures >= MAX_HTTP_FAILURES) {
    DryerSerial.println("[WIFI] Repeated upload failures. Requesting a clean re-join.");
    consecutiveHttpFailures = 0;
    wifiReinitRequested = true;  // serviced in maintainWiFiConnection(); never nest a 30 s connect here
  }

  return false;
}

bool tryUpload(UploadSlot& slot, const char* id, bool isRunning, bool isEmpty, const char* stateStr) {
  if ((long)(millis() - slot.nextAttempt) < 0) {  // rollover-safe
    return false;
  }
  if (sendCustomStatusUpdate(id, isRunning, isEmpty, stateStr)) {
    slot.backoff = 0;
    slot.nextAttempt = millis();
    return true;
  }
  slot.backoff = slot.backoff ? min(slot.backoff * 2, UPLOAD_BACKOFF_MAX) : 5000UL;
  slot.nextAttempt = millis() + slot.backoff + random(0, 2000);
  return false;
}

// Each machine is sent on any change and otherwise every sendInterval.
void serviceUploads() {
  bool dr, de, wr, we, micOk;
  const char* ds;
  portENTER_CRITICAL(&dryerMux);
  dr = pubDryerRunning;
  de = pubDryerEmpty;
  ds = pubDryerSensorState;
  portEXIT_CRITICAL(&dryerMux);
  portENTER_CRITICAL(&washerMux);
  wr = pubWasherRunning;
  we = pubWasherEmpty;
  micOk = pubWasherMicOk;
  portEXIT_CRITICAL(&washerMux);

  if (!dryerReportedOnce || dr != lastReportedDryerRunning || de != lastReportedDryerEmpty
      || millis() - lastDryerSendTime >= sendInterval) {
    if (tryUpload(dryerSlot, dryerMachineId, dr, de, ds)) {
      lastReportedDryerRunning = dr;
      lastReportedDryerEmpty = de;
      dryerReportedOnce = true;
      lastDryerSendTime = millis();  // only on success, or the server TTL flaps
    }
  }

  if (!washerReportedOnce || wr != lastReportedWasherRunning || we != lastReportedWasherEmpty
      || micOk != lastReportedWasherMicOk || millis() - lastWasherSendTime >= sendInterval) {
    if (tryUpload(washerSlot, washerMachineId, wr, we, micOk ? SENSOR_OK : SENSOR_FLAT)) {
      lastReportedWasherRunning = wr;
      lastReportedWasherEmpty = we;
      lastReportedWasherMicOk = micOk;
      washerReportedOnce = true;
      lastWasherSendTime = millis();
    }
  }
}

// Core 0, so a slow HTTPS POST or a 30 s eduroam join never pauses either sensor.
void networkTask(void* parameter) {
  netHeartbeat = millis();
  connectToEduroam();
  for (;;) {
    netHeartbeat = millis();
    maintainWiFiConnection();
    serviceUploads();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ---------------- Supervisor ----------------
bool heartbeatStale(volatile unsigned long& heartbeat, unsigned long timeout) {
  // Signed: a heartbeat written just after millis() was read must not look 49 days old.
  return (long)(millis() - heartbeat) > (long)timeout;
}

// Runs on core 0 so it keeps ticking even when another task is blocked inside a
// driver call that never returns.
void supervisorTask(void* parameter) {
  esp_task_wdt_add(NULL);  // hardware backstop in case the supervisor itself stalls

  for (;;) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5000));

    if (heartbeatStale(dryerHeartbeat, SENSOR_STALL_TIMEOUT_MS)) {
      hardRestart("dryer loop stalled");
    }
    if (heartbeatStale(washerHeartbeat, SENSOR_STALL_TIMEOUT_MS)) {
      hardRestart("washer task stalled");
    }
    if (heartbeatStale(netHeartbeat, NET_STALL_TIMEOUT_MS)) {
      hardRestart("network task stalled");
    }

    uint64_t upUs = (uint64_t)esp_timer_get_time();  // 64-bit: never rolls over

    // Only while both machines are idle, so a restart never splits a cycle.
    if (upUs >= preventiveRebootUs && !pubDryerRunning && !pubWasherRunning) {
      hardRestart("preventive restart while idle");
    } else if (upUs >= ABSOLUTE_REBOOT_US) {
      hardRestart("7-day absolute cap");
    }
  }
}

// ---------------- Setup / dryer loop ----------------
void setup() {
#if ARDUINO_USB_MODE
  DryerSerial.setTxBufferSize(2048);
#endif
  DryerSerial.begin(115200);
#if ARDUINO_USB_MODE
  DryerSerial.setTxTimeoutMs(0);  // never stall sampling when no laptop is plugged in
#endif
  WasherSerial.setTxBufferSize(2048);
  WasherSerial.begin(115200);
  delay(500);

  nodeSalt = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
  randomSeed(nodeSalt ^ esp_random());
  preventiveRebootUs = (uint64_t)(86400000UL + (nodeSalt % 21600000UL)) * 1000ULL;  // 24 h + up to 6 h

  DryerSerial.printf("\n=== Dryer + Washer Monitor === dryer %s, washer %s, reset reason %d\n",
                     dryerMachineId, washerMachineId, (int)esp_reset_reason());
  WasherSerial.printf("\n=== Dryer + Washer Monitor (washer port) === washer %s, reset reason %d\n",
                      washerMachineId, (int)esp_reset_reason());

  // Staggered boot so several nodes on one supply never draw inrush together.
  delay(nodeSalt % 12000);

  dryerHeartbeat = washerHeartbeat = netHeartbeat = millis();

  esp_task_wdt_config_t wdtCfg = {
    .timeout_ms = 30000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&wdtCfg);

  xTaskCreatePinnedToCore(supervisorTask, "supervisor", 4096, NULL, 1, NULL, 0);

  for (int i = 0; i < WINDOW; i++) deltas[i] = 0.0;
  for (int i = 0; i < WINDOWTWO; i++) activities[i] = 0.0;
  for (int i = 0; i < WINDOWTHREE; i++) deltastwo[i] = 0.0;
  for (int i = 0; i < 3; i++) activitysmallHistory[i] = 0.0;
  for (int i = 0; i < WASHER_WINDOW; i++) {
    HighFreqWindow[i] = 0.0;
    HighFreqWindow2[i] = 0.0;
  }

  Wire.begin(MPU_SDA, MPU_SCL);
  Wire.setTimeOut(50);  // never block the loop forever on a stuck slave
  setup_mpu();
  sensorHealthy = verifyMpu();
  DryerSerial.printf("[MPU] WHO_AM_I check: %s\n", sensorHealthy ? "ok" : "FAILED");

  if (setup_i2s() != ESP_OK) {
    hardRestart("I2S would not start at boot");
  }

  // Registered once and only once: WiFi.onEvent() has no deduplication.
  WiFi.persistent(false);
  WiFi.onEvent(onWiFiEvent);

  // Washer shares core 1 with this dryer loop; WiFi and uploads live on core 0.
  xTaskCreatePinnedToCore(washerTask, "washer", 8192, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(networkTask, "network", 12288, NULL, 1, NULL, 0);
}

void loop() {
  dryerHeartbeat = millis();

  if (!record_mpu_accel()) {
    if (++consecutiveI2cFailures >= MAX_I2C_FAILURES) {
      consecutiveI2cFailures = 0;
      recoverI2cBus();
    }
    publishDryer();  // so the fault is still reported
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
      dryerRunning = true;
      dryerEmpty = false;
    } else {
      dryerRunning = false;
    }
  }

  float activitysmall3ago = activitysmallHistory[activitysmallIdx];  //small activity from 0.15 s ago, to compare against now for the door event

  if (!dryerRunning && !dryerEmpty) {
    if (activitysmall > (activitysmall3ago + DOORCLOSINGCHANGE)) {
      dryerEmpty = true;
      DryerSerial.println("[STATE] Door event detected -> marking EMPTY");
    }
  }

  if (dryerRunning && !dryerWasRunning) {
    DryerSerial.println("▶️ Dryer started running");
  }
  if (!dryerRunning && dryerWasRunning) {
    DryerSerial.println("🛑 Dryer stopped");
  }
  dryerWasRunning = dryerRunning;

  publishDryer();

  DryerSerial.printf("Avg10:%.2f,ActivitySmall:%.2f\n", avg10, activitysmall);
  delay(50);
}
