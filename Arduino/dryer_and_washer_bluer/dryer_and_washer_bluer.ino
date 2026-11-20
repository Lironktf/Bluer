// Combined node: one ESP32-S3 watches a stacked pair. MPU6050 on the dryer for
// vibration, INMP441 aimed at the washer's water valve, both reported over WiFi.
// No BLE. Detection logic is unchanged from dryer_bluer.ino and washer_bluer.ino; the Wi-Fi,
// 30 s reports and recovery rules match dryer_bluer.ino.
//
// Board: "ESP32S3 Dev Module", Tools > USB CDC On Boot: Enabled.
// Wiring (no shared pins):
//   MPU6050  SDA -> GPIO 8, SCL -> GPIO 9, AD0 -> GND, VCC -> 3V3
//   INMP441  SCK -> GPIO 4, WS -> GPIO 5, SD -> GPIO 6, L/R -> GND, VDD -> 3V3
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>
#include <arduinoFFT.h>
#include <ArduinoJson.h>  // v7
#include <Preferences.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_sleep.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This sketch is for the ESP32-S3: select an ESP32-S3 board (pins differ on other chips)"
#endif
#if !ARDUINO_USB_CDC_ON_BOOT
#error "Set Tools > USB CDC On Boot > Enabled so the log and plot come out of the USB-C port"
#endif

#define DryerSerial Serial
#define WasherSerial Serial

// Leave at 0 until you have confirmed a 200 with it set to 1: if the bundle is
// unavailable every upload fails, which is worse than your original setInsecure().
#define USE_TLS_CERT_BUNDLE 0
#if USE_TLS_CERT_BUNDLE
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
#endif

// Open campus Wi-Fi, no login: IST allow-lists each board's MAC (printed at boot), renewed yearly.
// Confirmed in the SJU laundry room on Nov 18, 2026. Some buildings call it uw-wifi-no-encryption instead.
const char* const WIFI_SSIDS[] = {"UniversityOfWaterloo"};
const char* WIFI_PASSWORD = "";  // open network
const int WIFI_SSID_COUNT = sizeof(WIFI_SSIDS) / sizeof(WIFI_SSIDS[0]);
int wifiSsid = 0;

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
// Built-in defaults; thresholds saved on the website's Node settings page replace them.
unsigned long TIMER_FULL_MS = 1500000UL;    // 25 min, start of cycle
unsigned long TIMER_REFILL_MS = 600000UL;   // VARIES, per-device, see Arduino/MACHINES.md
double FIRST_FILL_HIGH2 = 68500;            // either band, starts the cycle
double REFILL_HIGH = 32000;                 // VARIES, a later water in tops the timer back up
  int REFILL_BANDS = 2;                       // VARIES, 1 = lower band, 2 = either band, 3 = upper band only
double DOOR_DROP = 750000;                  // VARIES, lowmid drop that marks the door closed
unsigned long cycleStartedAt = 0;
unsigned long MAX_CYCLE_MS = 2520000UL;  // 42 min hard cap

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

// ---------------- Website thresholds ----------------
struct NodeConfig {
  int version;
  float dryerOn, dryerDoor;
  double firstFill, refill, doorDrop;
  unsigned long refillMs, fullMs, maxCycleMs;
  int refillBands;
};
portMUX_TYPE cfgMux = portMUX_INITIALIZER_UNLOCKED;
NodeConfig pendingCfg;
volatile uint32_t cfgSeq = 0;  // bumped when pendingCfg changes; sensing tasks copy on change
int appliedConfigVersion = 0;  // 0 = built-in values
Preferences prefs;

// ---------------- Reports to the website ----------------
// One POST carries both machines' status and every 5 s reading since the last one. Sent every
// reportEveryMs, sooner when a status changes; readings stay queued until the website confirms them.
const unsigned long PERIOD_MS = 5000;
const unsigned long MIN_SEND_GAP_MS = 5000;  // a flickering status can't send faster than this
const unsigned long RETRY_MIN_MS = 5000;
const unsigned long RETRY_MAX_MS = 60000;
const int QUEUE_MAX = 120;  // 10 min of 5 s readings
// Per POST. 24 rows of each kind is ~2.7 KB of JSON at worst, so it always fits reportJson.
const int SEND_MAX = 24;
volatile unsigned long reportEveryMs = 30000;  // the server's reply can change this

template <typename T>
struct Ring {  // callers hold the matching mutex
  T items[QUEUE_MAX];
  int head = 0, count = 0;
  void push(const T& v) {
    if (count == QUEUE_MAX) {  // backlog full: drop the oldest
      head = (head + 1) % QUEUE_MAX;
      count--;
    }
    items[(head + count) % QUEUE_MAX] = v;
    count++;
  }
  const T& at(int i) const {
    return items[(head + i) % QUEUE_MAX];
  }
  void dropThrough(uint32_t seq) {  // removes everything up to and including seq
    while (count > 0 && (int32_t)(items[head].seq - seq) <= 0) {
      head = (head + 1) % QUEUE_MAX;
      count--;
    }
  }
};

struct DryerPeriod {
  uint32_t seq;
  unsigned long t;  // millis() when the period ended
  float avgMax, doorJumpMax;
  bool running, empty;
};
struct WasherPeriod {
  uint32_t seq;
  unsigned long t;
  float water1Max, water2Max, doorDropMax;
  bool running, empty;
};
struct ReportStatus {
  bool dRunning, dEmpty;
  const char* dSensor;
  bool wRunning, wEmpty, wMicOk;
  // Only running/empty changes send at once; sensor health rides on the next regular report.
  bool sameMachineState(const ReportStatus& o) const {
    return dRunning == o.dRunning && dEmpty == o.dEmpty && wRunning == o.wRunning && wEmpty == o.wEmpty;
  }
};

portMUX_TYPE dryerQMux = portMUX_INITIALIZER_UNLOCKED;
Ring<DryerPeriod> dryerQ;    // dryer loop pushes under dryerQMux
Ring<WasherPeriod> washerQ;  // washer task pushes under washerMux
uint32_t dryerSeq = 0, washerSeq = 0;
unsigned long dryerPeriodStart = 0, washerPeriodStart = 0;
float periodAvgMax = 0, periodDoorJumpMax = 0;                  // dryer loop only
float periodWater1Max = 0, periodWater2Max = 0, periodDoorDropMax = 0;  // washer task only
uint32_t bootId = 0;   // with batchNo, lets the server replace a batch it already stored
uint32_t batchNo = 0;  // advances only on success, so a retry reuses it
DryerPeriod sendD[SEND_MAX];
WasherPeriod sendW[SEND_MAX];
char reportJson[4096];
size_t reportLen = 0;

// ---------------- Network ----------------
const unsigned long WIFI_RETRY_MIN_MS = 30000UL;  // an open-network join takes a few seconds
const unsigned long WIFI_RETRY_MAX_MS = 300000UL;
unsigned long wifiRetryMs = WIFI_RETRY_MIN_MS;
unsigned long lastWifiAttempt = 0;
volatile bool wifiReinitRequested = false;  // set after repeated failed reports, serviced by the network task

const int MAX_HTTP_FAILURES = 5;
int consecutiveHttpFailures = 0;

// ---------------- Reliability supervision ----------------
volatile unsigned long dryerHeartbeat = 0;
volatile unsigned long washerHeartbeat = 0;
volatile unsigned long netHeartbeat = 0;
const unsigned long SENSOR_STALL_TIMEOUT_MS = 60000UL;  // a sensing loop frozen this long -> reboot
const unsigned long NET_STALL_TIMEOUT_MS = 120000UL;    // covers a slow HTTPS POST
// Alive but never reaching the website (Wi-Fi stuck, or off the allow list): resets every 5 min until the
// website answers again. On the dryer nodes the first report after a reset usually got through.
volatile unsigned long lastServerReplyAt = 0;
const unsigned long NO_SERVER_RESET_MS = 5UL * 60UL * 1000UL;
uint64_t preventiveRebootUs = 0;                             // 24 h + per-node jitter, set in setup()
const uint64_t ABSOLUTE_REBOOT_US = 604800000ULL * 1000ULL;  // 7 days, regardless of state
uint32_t nodeSalt = 0;  // per-node timer jitter so the fleet never reboots in lockstep

bool record_mpu_accel();
bool resetMpu();
bool setup_mpu();
bool write_to(const byte to_register, const byte write_value);
bool read_from(const byte from_register, const int num_bytes, byte read_data[]);
void connectToWiFi();
void hardRestart(const char* why);
esp_err_t setup_i2s();

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      DryerSerial.printf("[WIFI] Associated with %s\n", WIFI_SSIDS[wifiSsid]);
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      DryerSerial.printf("[WIFI] Connected to %s. IP %s, signal %d dBm\n", WIFI_SSIDS[wifiSsid],
                         WiFi.localIP().toString().c_str(), WiFi.RSSI());
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      DryerSerial.printf("[WIFI] Disconnected. Reason code: %u (heap=%u maxAlloc=%u)\n",
                         info.wifi_sta_disconnected.reason, (unsigned)ESP.getFreeHeap(),
                         (unsigned)ESP.getMaxAllocHeap());
      break;

    default:
      break;
  }
}

void hardRestart(const char* why) {
  DryerSerial.printf("[SUPERVISOR] Restart: %s\n", why);
  DryerSerial.flush();
  delay(50);
  esp_restart();
  // If esp_restart() ever wedges in a shutdown handler, the task watchdog fires.
}

// A 1 s deep sleep powers down the radio and most of the chip: much closer to unplugging it than esp_restart().
void fullReset(const char* why) {
  DryerSerial.printf("[SUPERVISOR] Full reset: %s\n", why);
  DryerSerial.flush();
  esp_sleep_enable_timer_wakeup(1000000ULL);
  esp_deep_sleep_start();
}

// Starts a join and returns; maintainWiFiConnection() checks on it.
void connectToWiFi() {
  lastWifiAttempt = millis();
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // full power: at about -77 dBm, uploads timed out at 11 dBm
  // WiFi.onEvent() is registered once in setup(): it has no dedup and would leak here.
  DryerSerial.printf("[WIFI] Connecting to %s, MAC %s\n", WIFI_SSIDS[wifiSsid], WiFi.macAddress().c_str());
  WiFi.begin(WIFI_SSIDS[wifiSsid], WIFI_PASSWORD);
}

void restartWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(200);
  connectToWiFi();
}

// The library retries a dropped connection straight away. If that hasn't worked, this restarts Wi-Fi from
// scratch at 30 s, then doubling up to every 5 min, for as long as it takes.
void maintainWiFiConnection() {
  // Serviced here, never inside the HTTP path.
  if (wifiReinitRequested) {
    wifiReinitRequested = false;
    DryerSerial.println("[WIFI] Upload failures requested a clean re-join.");
    restartWiFi();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    lastWifiAttempt = millis();  // so a drop first gets the library's own quick retry
    wifiRetryMs = WIFI_RETRY_MIN_MS;
    return;
  }
  if (millis() - lastWifiAttempt < wifiRetryMs) return;

  wifiSsid = (wifiSsid + 1) % WIFI_SSID_COUNT;
  DryerSerial.println("[WIFI] Not connected. Restarting Wi-Fi...");
  restartWiFi();
  // Jitter, so nodes on one supply never retry in lockstep.
  wifiRetryMs = min(wifiRetryMs * 2, WIFI_RETRY_MAX_MS) + random(0, 5000);
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
    .mck_io_num = I2S_PIN_NO_CHANGE,  // left at 0 it would drive MCLK onto GPIO 0 (BOOT)
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

  if (actualAvgWater > periodWater1Max) periodWater1Max = actualAvgWater;
  if (actualAvgWater2 > periodWater2Max) periodWater2Max = actualAvgWater2;
  if (prevlowmid - lowmid > periodDoorDropMax) periodDoorDropMax = prevlowmid - lowmid;

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
      bool refillDetected = (REFILL_BANDS == 1 && actualAvgWater > REFILL_HIGH)
                || (REFILL_BANDS == 2 && (actualAvgWater > REFILL_HIGH || actualAvgWater2 > REFILL_HIGH))
                || (REFILL_BANDS == 3 && actualAvgWater2 > REFILL_HIGH);
      if (refillDetected && (cycleEndsAt - millis() < TIMER_REFILL_MS)) {
        cycleEndsAt = millis() + TIMER_REFILL_MS;
        WasherSerial.println("💧 Refill detected -> timer topped up");
      }

      if ((long)(millis() - cycleEndsAt) >= 0) {
        washerRunning = false;
        WasherSerial.println("🛑 Cycle ended (timer expired)");
      }
    }
  }
}

void washerTask(void* parameter) {
  washerPeriodStart = millis();
  for (;;) {
    washerHeartbeat = millis();
    applyWasherConfig();
    washerStep();
    publishWasher();
    closeWasherPeriod();
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

// ---------------- Website thresholds ----------------
void applyDryerConfig() {
  static uint32_t seen = 0;
  if (seen == cfgSeq) return;
  portENTER_CRITICAL(&cfgMux);
  DYRERONTHRESHHOLD = pendingCfg.dryerOn;
  DOORCLOSINGCHANGE = pendingCfg.dryerDoor;
  seen = cfgSeq;
  portEXIT_CRITICAL(&cfgMux);
}

void applyWasherConfig() {
  static uint32_t seen = 0;
  if (seen == cfgSeq) return;
  portENTER_CRITICAL(&cfgMux);
  FIRST_FILL_HIGH2 = pendingCfg.firstFill;
  REFILL_HIGH = pendingCfg.refill;
  REFILL_BANDS = pendingCfg.refillBands;
  DOOR_DROP = pendingCfg.doorDrop;
  TIMER_REFILL_MS = pendingCfg.refillMs;
  TIMER_FULL_MS = pendingCfg.fullMs;
  MAX_CYCLE_MS = pendingCfg.maxCycleMs;
  seen = cfgSeq;
  portEXIT_CRITICAL(&cfgMux);
}

void loadSavedConfig() {
  prefs.begin("bluer", false);
  // Never reuse thresholds saved under a different machine id after a reflash.
  if (prefs.getString("node", "") != dryerMachineId) return;
  NodeConfig saved;
  if (prefs.getBytes("cfg", &saved, sizeof(saved)) != sizeof(saved) || saved.version <= 0) return;
  pendingCfg = saved;
  cfgSeq++;
  appliedConfigVersion = saved.version;
  DryerSerial.printf("[CONFIG] Using saved website thresholds v%d\n", saved.version);
}

// The reply to every report: how often to report, plus saved thresholds if any.
// False if the reply isn't the website's own, e.g. a Wi-Fi sign-in page.
bool applyServerReply(const String& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc["success"].as<bool>()) return false;
  if (doc["reportEveryS"].is<int>()) {
    reportEveryMs = constrain(doc["reportEveryS"].as<int>(), 15, 90) * 1000UL;
  }

  JsonObject c = doc["config"].as<JsonObject>();
  if (c.isNull()) return true;  // nothing saved on the website: keep current values
  NodeConfig n;
  n.version = c["version"].as<int>();
  if (n.version <= 0 || n.version == appliedConfigVersion) return true;
  n.dryerOn = c["dryerOnThreshold"].as<float>();
  n.dryerDoor = c["dryerDoorChange"].as<float>();
  n.firstFill = c["washerFirstFill"].as<double>();
  n.refill = c["washerRefill"].as<double>();
  n.refillBands = c["washerRefillBands"].as<int>();
  n.doorDrop = c["washerDoorDrop"].as<double>();
  n.refillMs = c["washerRefillTimerMs"].as<unsigned long>();
  n.fullMs = c["washerFullTimerMs"].as<unsigned long>();
  n.maxCycleMs = c["washerMaxCycleMs"].as<unsigned long>();
  if (n.dryerOn <= 0 || n.dryerDoor <= 0 || n.firstFill <= 0 || n.refill <= 0 || n.doorDrop <= 0
      || n.refillMs == 0 || n.fullMs == 0 || n.maxCycleMs < n.fullMs || n.refillBands < 1 || n.refillBands > 3) {
    DryerSerial.printf("[CONFIG] Ignored invalid website thresholds v%d\n", n.version);
    return true;
  }

  portENTER_CRITICAL(&cfgMux);
  pendingCfg = n;
  cfgSeq++;
  portEXIT_CRITICAL(&cfgMux);
  appliedConfigVersion = n.version;
  prefs.putString("node", dryerMachineId);
  prefs.putBytes("cfg", &n, sizeof(n));
  DryerSerial.printf("[CONFIG] Now following website thresholds v%d\n", n.version);
  return true;
}

// Dryer loop: every 5 s, queues the highest readings of the period and the state at its end.
void closeDryerPeriod() {
  if (millis() - dryerPeriodStart < PERIOD_MS) return;
  dryerPeriodStart = millis();

  DryerPeriod p = { 0, dryerPeriodStart, periodAvgMax, periodDoorJumpMax, dryerRunning, dryerEmpty };
  periodAvgMax = periodDoorJumpMax = 0;
  portENTER_CRITICAL(&dryerQMux);
  p.seq = ++dryerSeq;
  dryerQ.push(p);
  portEXIT_CRITICAL(&dryerQMux);
}

// Washer task: the same every 5 s for the washer.
void closeWasherPeriod() {
  if (millis() - washerPeriodStart < PERIOD_MS) return;
  washerPeriodStart = millis();

  WasherPeriod p = { 0, washerPeriodStart, periodWater1Max, periodWater2Max, periodDoorDropMax, washerRunning,
                     washerEmpty };
  periodWater1Max = periodWater2Max = periodDoorDropMax = 0;
  portENTER_CRITICAL(&washerMux);
  p.seq = ++washerSeq;
  washerQ.push(p);
  portEXIT_CRITICAL(&washerMux);
}

// Returns the HTTP code; on 2xx, reply receives the response body.
int httpPostJson(const char* url, const char* payload, String* reply) {
  WiFiClientSecure client;
#if USE_TLS_CERT_BUNDLE
  // The full bundle, not a single pinned root: a CA rotation by the host must
  // never be able to take the whole fleet offline at once.
  client.setCACertBundle(rootca_crt_bundle_start);
#else
  client.setInsecure();
#endif
  client.setHandshakeTimeout(15);  // seconds; the 120 s default could trip the network stall guard

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(20000);  // a slow cold-start reply (-11) is not a lost upload
  if (!http.begin(client, url)) {
    DryerSerial.println("❌ Could not start the HTTPS session");
    return -1;
  }

  http.addHeader("Content-Type", "application/json");
#ifdef DEVICE_AUTH_TOKEN
  // Define DEVICE_AUTH_TOKEN near the top once the API checks it.
  http.addHeader("Authorization", "Bearer " DEVICE_AUTH_TOKEN);
#endif

  int code = http.POST((uint8_t*)payload, strlen(payload));
  if (reply && code >= 200 && code < 300) *reply = http.getString();
  if (code < 0) {
    char tlsErr[100] = "";
    client.lastError(tlsErr, sizeof(tlsErr));
    DryerSerial.printf("[HTTP] %s; tls: %s; rssi=%d dBm; maxAlloc=%u\n", HTTPClient::errorToString(code).c_str(),
                       tlsErr[0] ? tlsErr : "none", WiFi.RSSI(), (unsigned)ESP.getMaxAllocHeap());
  }
  http.end();
  return code;
}

// Appends to reportJson; false if it would not fit, so a cut-off report is never sent.
bool appendJson(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(reportJson + reportLen, sizeof(reportJson) - reportLen, fmt, args);
  va_end(args);
  if (n < 0 || reportLen + n >= sizeof(reportJson)) return false;
  reportLen += n;
  return true;
}

ReportStatus readReportStatus() {
  ReportStatus s;
  portENTER_CRITICAL(&dryerMux);
  s.dRunning = pubDryerRunning;
  s.dEmpty = pubDryerEmpty;
  s.dSensor = pubDryerSensorState;
  portEXIT_CRITICAL(&dryerMux);
  portENTER_CRITICAL(&washerMux);
  s.wRunning = pubWasherRunning;
  s.wEmpty = pubWasherEmpty;
  s.wMicOk = pubWasherMicOk;
  portEXIT_CRITICAL(&washerMux);
  return s;
}

static float finiteOr0(float v) {
  return isfinite(v) ? v : 0.0f;  // a nan would make the whole report invalid JSON
}

// One POST with both machines' status and the queued 5 s readings. Readings leave the queue
// only once the website confirms them; a retry reuses batchNo, so the server replaces, never duplicates.
bool sendReport(const ReportStatus& st) {
  int nd = 0, nw = 0;
  portENTER_CRITICAL(&dryerQMux);
  for (; nd < dryerQ.count && nd < SEND_MAX; nd++) sendD[nd] = dryerQ.at(nd);
  portEXIT_CRITICAL(&dryerQMux);
  portENTER_CRITICAL(&washerMux);
  for (; nw < washerQ.count && nw < SEND_MAX; nw++) sendW[nw] = washerQ.at(nw);
  portEXIT_CRITICAL(&washerMux);

  unsigned long now = millis();
  reportLen = 0;
  bool fits = appendJson("{\"v\":2,\"machineId\":\"%s\",\"washerId\":\"%s\",\"boot\":%lu,\"batch\":%lu,"
                         "\"configVersion\":%d,\"uptime\":%lu,\"resetReason\":%d,\"freeHeap\":%u,"
                         "\"dryer\":{\"running\":%s,\"empty\":%s,\"sensorState\":\"%s\"},"
                         "\"washer\":{\"running\":%s,\"empty\":%s,\"micOk\":%s}",
                         dryerMachineId, washerMachineId, (unsigned long)bootId, (unsigned long)batchNo,
                         appliedConfigVersion, (unsigned long)(esp_timer_get_time() / 1000000),
                         (int)esp_reset_reason(), (unsigned)ESP.getFreeHeap(),
                         st.dRunning ? "true" : "false", st.dEmpty ? "true" : "false", st.dSensor,
                         st.wRunning ? "true" : "false", st.wEmpty ? "true" : "false", st.wMicOk ? "true" : "false");
  // [age ms, highest avg10, highest door jump, running, empty]
  fits = fits && appendJson(",\"d\":[");
  for (int i = 0; fits && i < nd; i++) {
    const DryerPeriod& p = sendD[i];
    fits = appendJson("%s[%lu,%.3f,%.3f,%d,%d]", i ? "," : "", now - p.t, finiteOr0(p.avgMax),
                      finiteOr0(p.doorJumpMax), p.running, p.empty);
  }
  // [age ms, highest water 1, highest water 2, highest door drop, running, empty]
  fits = fits && appendJson("],\"w\":[");
  for (int i = 0; fits && i < nw; i++) {
    const WasherPeriod& r = sendW[i];
    fits = appendJson("%s[%lu,%.0f,%.0f,%.0f,%d,%d]", i ? "," : "", now - r.t, finiteOr0(r.water1Max),
                      finiteOr0(r.water2Max), finiteOr0(r.doorDropMax), r.running, r.empty);
  }
  fits = fits && appendJson("]}");
  if (!fits) {
    DryerSerial.println("❌ Report did not fit its buffer");
    return false;
  }

  String reply;
  unsigned long postStart = millis();
  int code = httpPostJson(serverUrl, reportJson, &reply);
  netHeartbeat = millis();
  // A sign-in page on an open network can answer 200 too; only the website's own reply counts.
  bool delivered = code >= 200 && code < 300 && applyServerReply(reply);
  // Readings stay queued on any failure: on the open campus network a 4xx can come from the network, not
  // the website. A report the website truly refuses is cleared by the 5 min reset.
  if (!delivered) {
    DryerSerial.printf("❌ Report failed: %d (%s) after %lu ms, rssi=%d dBm, maxAlloc=%u, queued d=%d w=%d\n", code,
                       HTTPClient::errorToString(code).c_str(), millis() - postStart, (int)WiFi.RSSI(),
                       (unsigned)ESP.getMaxAllocHeap(), nd, nw);
    if (++consecutiveHttpFailures >= MAX_HTTP_FAILURES) {
      DryerSerial.println("[WIFI] Repeated report failures. Requesting a clean re-join.");
      consecutiveHttpFailures = 0;
      wifiReinitRequested = true;  // serviced by maintainWiFiConnection(), never nested in a POST
    }
    return false;
  }

  consecutiveHttpFailures = 0;
  batchNo++;
  portENTER_CRITICAL(&dryerQMux);
  if (nd > 0) dryerQ.dropThrough(sendD[nd - 1].seq);
  portEXIT_CRITICAL(&dryerQMux);
  portENTER_CRITICAL(&washerMux);
  if (nw > 0) washerQ.dropThrough(sendW[nw - 1].seq);
  portEXIT_CRITICAL(&washerMux);
  lastServerReplyAt = millis();
  DryerSerial.printf("✅ Report %d: d=%d w=%d dryer run=%d empty=%d, washer run=%d empty=%d, heap=%u\n", code, nd, nw,
                     st.dRunning, st.dEmpty, st.wRunning, st.wEmpty, (unsigned)ESP.getFreeHeap());
  return true;
}

// Network task: a report every reportEveryMs, sooner when a status changes or readings back up.
void serviceReports() {
  static bool sentOnce = false;
  static ReportStatus lastSent;
  static unsigned long lastSentAt = 0, lastTryAt = 0, retryDelay = 0;
  if (WiFi.status() != WL_CONNECTED) return;

  unsigned long now = millis();
  if (retryDelay && now - lastTryAt < retryDelay) return;

  ReportStatus st = readReportStatus();
  bool backlog;
  portENTER_CRITICAL(&dryerQMux);
  backlog = dryerQ.count > SEND_MAX;
  portEXIT_CRITICAL(&dryerQMux);
  portENTER_CRITICAL(&washerMux);
  backlog = backlog || washerQ.count > SEND_MAX;
  portEXIT_CRITICAL(&washerMux);

  bool due = !sentOnce || now - lastSentAt >= reportEveryMs
             || ((!st.sameMachineState(lastSent) || backlog) && now - lastSentAt >= MIN_SEND_GAP_MS);
  if (!due) return;

  lastTryAt = now;
  if (sendReport(st)) {
    sentOnce = true;
    lastSent = st;
    lastSentAt = millis();
    retryDelay = 0;
  } else {
    retryDelay = min(retryDelay ? retryDelay * 2 : RETRY_MIN_MS, RETRY_MAX_MS) + random(0, 2000);
  }
}

// Core 0, so a slow HTTPS POST or a Wi-Fi restart never pauses either sensor.
void networkTask(void* parameter) {
  netHeartbeat = millis();
  connectToWiFi();
  for (;;) {
    netHeartbeat = millis();
    maintainWiFiConnection();
    serviceReports();
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
    if (heartbeatStale(lastServerReplyAt, NO_SERVER_RESET_MS)) {
      fullReset("no report reached the website for 5 min");
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
  delay(500);

  nodeSalt = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
  randomSeed(nodeSalt ^ esp_random());
  bootId = esp_random();
  preventiveRebootUs = (uint64_t)(86400000UL + (nodeSalt % 21600000UL)) * 1000ULL;  // 24 h + up to 6 h

  DryerSerial.printf("\n=== Dryer + Washer Monitor === dryer %s, washer %s, reset reason %d\n",
                     dryerMachineId, washerMachineId, (int)esp_reset_reason());

  // Staggered boot so several nodes on one supply never draw inrush together.
  delay(nodeSalt % 12000);

  dryerHeartbeat = washerHeartbeat = netHeartbeat = millis();
  lastServerReplyAt = millis();

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

  // No restart: that would take the dryer down too. washerStep() keeps retrying.
  if (setup_i2s() != ESP_OK) {
    micHealthy = false;
  }
  publishWasher();

  loadSavedConfig();

  // Registered once and only once: WiFi.onEvent() has no deduplication.
  WiFi.persistent(false);
  WiFi.onEvent(onWiFiEvent);

  dryerPeriodStart = millis();

  // Washer shares core 1 with this dryer loop; WiFi and uploads live on core 0.
  xTaskCreatePinnedToCore(washerTask, "washer", 8192, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(networkTask, "network", 12288, NULL, 1, NULL, 0);
}

void loop() {
  dryerHeartbeat = millis();
  applyDryerConfig();
  closeDryerPeriod();

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
    if (avg10 > periodAvgMax) periodAvgMax = avg10;

    if (avg10 > DYRERONTHRESHHOLD) {
      dryerRunning = true;
      dryerEmpty = false;
    } else {
      dryerRunning = false;
    }
  }

  float activitysmall3ago = activitysmallHistory[activitysmallIdx];  //small activity from 0.15 s ago, to compare against now for the door event
  if (activitysmall - activitysmall3ago > periodDoorJumpMax) periodDoorJumpMax = activitysmall - activitysmall3ago;

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
  delay(50);
}
