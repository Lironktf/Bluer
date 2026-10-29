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
#include <ArduinoJson.h>  // v7
#include <Preferences.h>

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

// MPU6050 wiring: SDA -> GPIO 21, SCL -> GPIO 22, VCC -> 3V3, GND -> GND, AD0 -> GND; INT unused.
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
uint16_t washerAckVersion = 0;      // config version the washer says it is following
unsigned long washerLastHeardMs = 0;
bool lastRelayedWasherRunning = false;
bool lastRelayedWasherEmpty = true;
bool lastRelayedWasherMicOk = true;
bool washerRelayedOnce = false;
portMUX_TYPE washerMux = portMUX_INITIALIZER_UNLOCKED;
unsigned long lastBleScanTime = 0;
const unsigned long bleScanInterval = 20000;  // 2 s scan out of every 20 s, outside test mode
bool continuousScan = false;                  // test mode: listen ~30% of the time to catch every washer second

NimBLEScan* pBLEScan;
NimBLEAdvertising* pAdvertising;
uint8_t pairNumber = 0;  // the digits at the end of machineId, shared with the paired washer

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
volatile unsigned long netHeartbeat = 0;
const unsigned long NET_STALL_TIMEOUT_MS = 120000UL;   // covers a 30 s connect plus slow HTTPS
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
  DYRERONTHRESHHOLD = 2.45,  //Thresholds, per-device, see Arduino/MACHINES.md
  DOORCLOSINGCHANGE = 0.55,  // per-device, see Arduino/MACHINES.md
  avg10 = 0,  //Average activity over last 15 seconds (easier way just always keep track of the average)
  sum10 = 0   //used to calucate a new average every second
  ;

// Machine identification and server configuration
//VARIES: dryer ID; its trailing number selects the BLE pair.
const char* machineId = "sj-d4";
const char* serverUrl = "https://laun-dryer.vercel.app/api/machines";
//VARIES: paired washer ID; use the same number as machineId.
const char* washerMachineId = "sj-w4";

// Timing for sending updates (send every 5 seconds)
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 300000;  // 5 min

// Snapshot of the dryer state for the network task (loop() writes, core 0 reads).
portMUX_TYPE dryerMux = portMUX_INITIALIZER_UNLOCKED;
bool pubRunning = false;
bool pubEmpty = true;
const char* pubSensorState = "ok";

// ---------------- BLE link with the paired washer ----------------
// Both sides broadcast 27 bytes of manufacturer data: FF FF 'B' <type> <pair> ...
// A legacy BLE advertisement holds at most 31 bytes, so the washer sends a rolling
// two-second window every second instead of one 15-second batch.
// Washer 'W': [5] status bits, [6-7] applied config version, [8] second counter,
//             [9-17] newest second, [18-26] the second before (water1, water2, door drop, 24-bit each).
// Dryer  'D': [5-6] config version, [7] flags, [8-10] first fill, [11-13] refill,
//             [14-17] door drop, [18-20] refill ms, [21-23] full ms, [24-26] max cycle ms.
const uint8_t LINK_LEN = 27;
const uint8_t ST_RUNNING = 0x01, ST_EMPTY = 0x02, ST_MIC_OK = 0x04, ST_TEST = 0x08,
              ST_PREV_RUNNING = 0x10, ST_PREV_EMPTY = 0x20, ST_CUR_VALID = 0x40, ST_PREV_VALID = 0x80;
const uint8_t DF_TEST = 0x01, DF_BANDS2 = 0x02, DF_HAS_CONFIG = 0x80;

// Washer seconds heard over BLE, matched to this dryer's own seconds before upload.
struct WasherSecond {
  bool valid;
  uint8_t counter;
  unsigned long t;  // this dryer's millis() when that washer second ended (first time heard)
  uint32_t w1, w2, drop;
  bool running, empty;
};
const int WASHER_RING = 32;
WasherSecond washerRing[WASHER_RING];

// ---------------- Website thresholds and test mode ----------------
struct NodeConfig {
  int version;
  float dryerOn, dryerDoor;
  double firstFill, refill, doorDrop;
  unsigned long refillMs, fullMs, maxCycleMs;
  int refillBands;
};
portMUX_TYPE cfgMux = portMUX_INITIALIZER_UNLOCKED;
NodeConfig nodeCfg;            // dryer values are used here; washer values are relayed over BLE
volatile uint32_t cfgSeq = 0;  // bumped when nodeCfg changes; loop() copies the dryer values on change
int appliedConfigVersion = 0;  // 0 = built-in values
Preferences prefs;
const char* telemetryUrl = "https://laun-dryer.vercel.app/api/diagnostics?kind=telemetry";

volatile bool testMode = false;
const unsigned long TELEMETRY_UPLOAD_MS = 15000;
const unsigned long WASHER_MATCH_DELAY_MS = 3000;  // a washer second can arrive up to ~2 s after it ends
const long WASHER_MATCH_WINDOW_MS = 550;
const int TELEMETRY_MAX = 120;
const int TELEMETRY_BATCH = 30;
struct TelemetrySample {
  unsigned long t;
  float dAvg10, dDoorJumpMax;
  bool dRunning, dEmpty;
};
portMUX_TYPE telemetryMux = portMUX_INITIALIZER_UNLOCKED;
TelemetrySample telemetryBuf[TELEMETRY_MAX];
int telemetryHead = 0, telemetryCount = 0;
unsigned long lastTelemetryUpload = 0;
char telemetryJson[8192];
float secDoorJumpMax = 0;  // loop() only, closed by recordTelemetrySecond()

static void putU16(uint8_t* p, uint32_t v) {
  p[0] = v;
  p[1] = v >> 8;
}
static void putU24(uint8_t* p, uint32_t v) {
  if (v > 0xFFFFFF) v = 0xFFFFFF;
  p[0] = v;
  p[1] = v >> 8;
  p[2] = v >> 16;
}
static void putU32(uint8_t* p, uint32_t v) {
  p[0] = v;
  p[1] = v >> 8;
  p[2] = v >> 16;
  p[3] = v >> 24;
}
static uint32_t getU16(const uint8_t* p) {
  return p[0] | (p[1] << 8);
}
static uint32_t getU24(const uint8_t* p) {
  return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16);
}
static uint32_t toU24(double v) {
  if (!(v > 0)) return 0;
  return v >= 16777215.0 ? 16777215UL : (uint32_t)(v + 0.5);
}
static uint32_t toU32(double v) {
  if (!(v > 0)) return 0;
  return v >= 4294967295.0 ? 4294967295UL : (uint32_t)(v + 0.5);
}

// Called with washerMux held. Keeps the earliest arrival time, the closest to when the second ended.
void storeWasherSecond(uint8_t counter, unsigned long t, const uint8_t* p, bool isRunning, bool isEmpty) {
  WasherSecond& s = washerRing[counter % WASHER_RING];
  if (s.valid && s.counter == counter && (long)(t - s.t) < 20000) return;
  s = { true, counter, t, getU24(p), getU24(p + 3), getU24(p + 6), isRunning, isEmpty };
}

class MyAdvertisedDeviceCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    std::string data = advertisedDevice->getManufacturerData();
    if (data.size() < LINK_LEN) {
      return;
    }
    const uint8_t* b = (const uint8_t*)data.data();
    if (b[0] != 0xFF || b[1] != 0xFF || b[2] != 'B' || b[3] != 'W' || b[4] != pairNumber) {
      return;
    }

    uint8_t st = b[5];
    unsigned long now = millis();
    bool changed;
    portENTER_CRITICAL(&washerMux);
    changed = !washerHeard || washerRunning != bool(st & ST_RUNNING) || washerEmpty != bool(st & ST_EMPTY)
              || washerMicOk != bool(st & ST_MIC_OK);
    washerRunning = st & ST_RUNNING;
    washerEmpty = st & ST_EMPTY;
    washerMicOk = st & ST_MIC_OK;
    washerAckVersion = getU16(b + 6);
    washerHeard = true;
    washerLastHeardMs = now;
    if (st & ST_CUR_VALID) storeWasherSecond(b[8], now, b + 9, st & ST_RUNNING, st & ST_EMPTY);
    if (st & ST_PREV_VALID) storeWasherSecond((uint8_t)(b[8] - 1), now - 1000, b + 18, st & ST_PREV_RUNNING, st & ST_PREV_EMPTY);
    portEXIT_CRITICAL(&washerMux);

    if (changed) {
      Serial.printf("[BLE RX] washer rssi=%d running=%d empty=%d mic=%d config=v%u test=%d\n",
                    advertisedDevice->getRSSI(), bool(st & ST_RUNNING), bool(st & ST_EMPTY),
                    bool(st & ST_MIC_OK), (unsigned)getU16(b + 6), bool(st & ST_TEST));
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
    netHeartbeat = millis();  // this wait is expected; it must not trip the stall guard
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

    // Signed: a heartbeat written just after millis() was read must not look 49 days old.
    if ((long)(millis() - loopHeartbeat) > (long)LOOP_STALL_TIMEOUT_MS) {
      hardRestart("main loop stalled");
    }
    if ((long)(millis() - netHeartbeat) > (long)NET_STALL_TIMEOUT_MS) {
      hardRestart("network task stalled");
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

  pairNumber = pairFromMachineId(machineId);
  Serial.printf("[BLE] Pair %u: listening for washer %s\n", pairNumber, washerMachineId);

  // Staggered boot so four nodes on one supply never draw inrush together.
  delay(nodeSalt % 12000);

  loopHeartbeat = millis();
  netHeartbeat = millis();

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

  // Initialize accelerometer (mpu.initialize() would set ±2g then be overwritten)
  setup_mpu();
  sensorHealthy = verifyMpu();
  Serial.printf("[MPU] WHO_AM_I check: %s\n", sensorHealthy ? "ok" : "FAILED");

  loadSavedConfig();

  // Bluetooth: broadcast thresholds and test mode to the washer, and listen for its packets.
  NimBLEDevice::init("");
  pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setConnectableMode(BLE_GAP_CONN_MODE_NON);
  pAdvertising->setMinInterval(320);  // 200 ms, in 0.625 ms units
  pAdvertising->setMaxInterval(480);  // 300 ms
  updateDryerAdvert();
  pAdvertising->start();

  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(new MyAdvertisedDeviceCallbacks(), true);  // every packet: test mode needs each second
  pBLEScan->setMaxResults(0);  // callback only; never cache results next to eduroam's TLS buffers
  pBLEScan->setActiveScan(false);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->start(2000, false);  // returns at once; listens for 2 s in the background
  lastBleScanTime = millis();

  // WiFi and uploads on core 0, so a slow HTTPS POST or a 30 s eduroam join never pauses the MPU.
  xTaskCreatePinnedToCore(networkTask, "network", 12288, NULL, 1, NULL, 0);
}

// "sj-d4" -> 4. The paired washer is flashed with the same PAIR_NUMBER.
uint8_t pairFromMachineId(const char* id) {
  const char* p = id + strlen(id);
  while (p > id && isdigit((unsigned char)p[-1])) p--;
  return (uint8_t)atoi(p);
}

void publishDryer() {
  portENTER_CRITICAL(&dryerMux);
  pubRunning = running;
  pubEmpty = empty;
  pubSensorState = sensorState;
  portEXIT_CRITICAL(&dryerMux);
}

// ---------------- Website thresholds and test-mode readings ----------------
void applyDryerConfig() {
  static uint32_t seen = 0;
  if (seen == cfgSeq) return;
  portENTER_CRITICAL(&cfgMux);
  DYRERONTHRESHHOLD = nodeCfg.dryerOn;
  DOORCLOSINGCHANGE = nodeCfg.dryerDoor;
  seen = cfgSeq;
  portEXIT_CRITICAL(&cfgMux);
}

void loadSavedConfig() {
  prefs.begin("bluer", false);
  // Never reuse thresholds saved under a different machine id after a reflash.
  if (prefs.getString("node", "") != machineId) return;
  NodeConfig saved;
  if (prefs.getBytes("cfg", &saved, sizeof(saved)) != sizeof(saved) || saved.version <= 0) return;
  nodeCfg = saved;
  cfgSeq = cfgSeq + 1;
  appliedConfigVersion = saved.version;
  Serial.printf("[CONFIG] Using saved website thresholds v%d\n", saved.version);
}

// Reply to every status report and telemetry upload: test mode, plus saved thresholds if any.
void applyServerReply(const String& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  if (doc["testMode"].is<bool>()) {
    bool on = doc["testMode"].as<bool>();
    if (on != testMode) Serial.printf("[TEST] Test mode %s\n", on ? "ON" : "off");
    testMode = on;
  }

  JsonObject c = doc["config"].as<JsonObject>();
  if (c.isNull()) return;  // nothing saved on the website: keep current values
  NodeConfig n;
  n.version = c["version"].as<int>();
  if (n.version <= 0 || n.version == appliedConfigVersion) return;
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
      || n.refillMs == 0 || n.fullMs == 0 || n.maxCycleMs < n.fullMs || (n.refillBands != 1 && n.refillBands != 2)) {
    Serial.printf("[CONFIG] Ignored invalid website thresholds v%d\n", n.version);
    return;
  }

  portENTER_CRITICAL(&cfgMux);
  nodeCfg = n;
  cfgSeq = cfgSeq + 1;
  portEXIT_CRITICAL(&cfgMux);
  appliedConfigVersion = n.version;
  prefs.putString("node", machineId);
  prefs.putBytes("cfg", &n, sizeof(n));
  Serial.printf("[CONFIG] Now following website thresholds v%d (relaying the washer's)\n", n.version);
}

// The website shows a version as applied only once the washer follows it too.
int reportedConfigVersion() {
  bool heard;
  uint16_t ack;
  portENTER_CRITICAL(&washerMux);
  heard = washerHeard;
  ack = washerAckVersion;
  portEXIT_CRITICAL(&washerMux);
  return heard ? min(appliedConfigVersion, (int)ack) : appliedConfigVersion;
}

// What the washer listens for: test mode, plus its thresholds once the website has saved some.
void updateDryerAdvert() {
  uint8_t b[LINK_LEN] = { 0xFF, 0xFF, 'B', 'D', pairNumber };
  uint8_t flags = testMode ? DF_TEST : 0;
  if (appliedConfigVersion > 0) {
    NodeConfig c;
    portENTER_CRITICAL(&cfgMux);
    c = nodeCfg;
    portEXIT_CRITICAL(&cfgMux);
    flags |= DF_HAS_CONFIG;
    if (c.refillBands == 2) flags |= DF_BANDS2;
    putU16(b + 5, c.version);
    putU24(b + 8, toU24(c.firstFill));
    putU24(b + 11, toU24(c.refill));
    putU32(b + 14, toU32(c.doorDrop));
    putU24(b + 18, c.refillMs);
    putU24(b + 21, c.fullMs);
    putU24(b + 24, c.maxCycleMs);
  }
  b[7] = flags;

  NimBLEAdvertisementData data;
  data.setManufacturerData(b, LINK_LEN);
  pAdvertising->setAdvertisementData(data);
}

void serviceDryerAdvert() {
  static int sentVersion = -1;
  static int sentTest = -1;
  if (sentVersion == appliedConfigVersion && sentTest == (int)testMode) return;
  sentVersion = appliedConfigVersion;
  sentTest = testMode;
  updateDryerAdvert();
}

// Outside test mode: 2 s out of every 20 s. In test mode: ~30% of the time, so every
// washer second (each packet carries two) arrives while leaving most airtime to eduroam.
void serviceBleScan() {
  if (testMode) {
    if (!continuousScan || !pBLEScan->isScanning()) {
      continuousScan = true;
      pBLEScan->stop();
      pBLEScan->setInterval(160);
      pBLEScan->setWindow(48);
      pBLEScan->start(0, false, true);  // 0 = until stopped
      Serial.println("[BLE] Test mode: listening for every washer second");
    }
    return;
  }
  if (continuousScan) {
    continuousScan = false;
    pBLEScan->stop();
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    lastBleScanTime = millis();
  }
  if (millis() - lastBleScanTime >= bleScanInterval) {
    lastBleScanTime = millis();
    Serial.printf("[BLE] Sniffing the air for washer %s updates...\n", washerMachineId);
    pBLEScan->start(2000, false, true);  // returns at once; listens for 2 s in the background
  }
}

// Dryer loop: closes the current one-second window and queues it while test mode is on.
void recordTelemetrySecond() {
  static unsigned long lastSecond = 0;
  if (millis() - lastSecond < 1000) return;
  lastSecond = millis();

  TelemetrySample s = { lastSecond, avg10, secDoorJumpMax, running, empty };
  secDoorJumpMax = 0;

  if (!testMode) return;
  portENTER_CRITICAL(&telemetryMux);
  if (telemetryCount == TELEMETRY_MAX) {  // upload backlog: drop the oldest second
    telemetryHead = (telemetryHead + 1) % TELEMETRY_MAX;
    telemetryCount--;
  }
  telemetryBuf[(telemetryHead + telemetryCount) % TELEMETRY_MAX] = s;
  telemetryCount++;
  portEXIT_CRITICAL(&telemetryMux);
}

// The washer second that ended closest to t, if any ended within WASHER_MATCH_WINDOW_MS of it.
bool findWasherSecond(unsigned long t, WasherSecond& out) {
  long best = WASHER_MATCH_WINDOW_MS;
  bool found = false;
  portENTER_CRITICAL(&washerMux);
  for (int i = 0; i < WASHER_RING; i++) {
    if (!washerRing[i].valid) continue;
    long d = labs((long)(washerRing[i].t - t));
    if (d < best) {
      best = d;
      out = washerRing[i];
      found = true;
    }
  }
  portEXIT_CRITICAL(&washerMux);
  return found;
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
  http.setTimeout(8000);
  if (!http.begin(client, url)) {
    Serial.println("❌ Could not start the HTTPS session");
    return -1;
  }

  http.addHeader("Content-Type", "application/json");
#ifdef DEVICE_AUTH_TOKEN
  // Define DEVICE_AUTH_TOKEN in secrets.h once the API checks it. Without this
  // anyone who sees one request can set any machine to any state from anywhere.
  http.addHeader("Authorization", "Bearer " DEVICE_AUTH_TOKEN);
#endif

  int code = http.POST((uint8_t*)payload, strlen(payload));
  if (reply && code >= 200 && code < 300) *reply = http.getString();
  http.end();
  return code;
}

// Network task: while test mode is on, uploads the queued seconds every ~15 s, each paired
// with the washer second that ended at the same time.
void serviceTelemetry() {
  if (!testMode) {
    portENTER_CRITICAL(&telemetryMux);
    telemetryCount = 0;
    portEXIT_CRITICAL(&telemetryMux);
    return;
  }
  if (WiFi.status() != WL_CONNECTED || millis() - lastTelemetryUpload < TELEMETRY_UPLOAD_MS) return;
  lastTelemetryUpload = millis();

  // Only seconds old enough that the washer's matching second has had time to arrive.
  TelemetrySample batch[TELEMETRY_BATCH];
  int n = 0;
  unsigned long cutoff = millis() - WASHER_MATCH_DELAY_MS;
  portENTER_CRITICAL(&telemetryMux);
  while (n < telemetryCount && n < TELEMETRY_BATCH) {
    const TelemetrySample& s = telemetryBuf[(telemetryHead + n) % TELEMETRY_MAX];
    if ((long)(s.t - cutoff) > 0) break;
    batch[n++] = s;
  }
  portEXIT_CRITICAL(&telemetryMux);
  if (n == 0) return;

  bool heard, wr, we;
  unsigned long lastHeard;
  portENTER_CRITICAL(&washerMux);
  heard = washerHeard;
  wr = washerRunning;
  we = washerEmpty;
  lastHeard = washerLastHeardMs;
  portEXIT_CRITICAL(&washerMux);
  bool washerRecent = heard && millis() - lastHeard < 60000;

  size_t len = snprintf(telemetryJson, sizeof(telemetryJson),
                        "{\"machineId\":\"%s\",\"configVersion\":%d,\"samples\":[",
                        machineId, reportedConfigVersion());
  for (int i = 0; i < n; i++) {
    const TelemetrySample& s = batch[i];
    len += snprintf(telemetryJson + len, sizeof(telemetryJson) - len,
                    "%s{\"dAvg10\":%.3f,\"dDoorJumpMax\":%.3f,\"dRunning\":%d,\"dEmpty\":%d",
                    i ? "," : "", s.dAvg10, s.dDoorJumpMax, s.dRunning, s.dEmpty);
    WasherSecond w;
    if (findWasherSecond(s.t, w)) {
      len += snprintf(telemetryJson + len, sizeof(telemetryJson) - len,
                      ",\"wWater1Max\":%lu,\"wWater2Max\":%lu,\"wDoorDropMax\":%lu,\"wRunning\":%d,\"wEmpty\":%d",
                      (unsigned long)w.w1, (unsigned long)w.w2, (unsigned long)w.drop, w.running, w.empty);
    } else if (washerRecent) {
      len += snprintf(telemetryJson + len, sizeof(telemetryJson) - len,
                      ",\"wRunning\":%d,\"wEmpty\":%d", wr, we);
    }
    len += snprintf(telemetryJson + len, sizeof(telemetryJson) - len, "}");
  }
  snprintf(telemetryJson + len, sizeof(telemetryJson) - len, "]}");

  String reply;
  int code = httpPostJson(telemetryUrl, telemetryJson, &reply);
  netHeartbeat = millis();
  if (code < 200 || code >= 300) {
    Serial.printf("❌ Telemetry upload failed: %d\n", code);
    return;
  }
  portENTER_CRITICAL(&telemetryMux);
  int sent = min(n, telemetryCount);
  telemetryHead = (telemetryHead + sent) % TELEMETRY_MAX;
  telemetryCount -= sent;
  portEXIT_CRITICAL(&telemetryMux);
  applyServerReply(reply);
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

// Washer relay and dryer reporting, on the network task so the MPU never waits on HTTPS.
void serviceUploads() {
  unsigned long currentTime = millis();

  // WASHER NETWORK ROUTER: snapshot the pair atomically so we can never relay a
  // torn combination, and compare-and-clear so a change during a POST is not lost.
  bool wr, we, heard, micOk;
  portENTER_CRITICAL(&washerMux);
  wr = washerRunning;
  we = washerEmpty;
  heard = washerHeard;
  micOk = washerMicOk;
  portEXIT_CRITICAL(&washerMux);

  if (heard && (!washerRelayedOnce || wr != lastRelayedWasherRunning || we != lastRelayedWasherEmpty
                || micOk != lastRelayedWasherMicOk)) {
    if (tryUpload(washerSlot, washerMachineId, wr, we, micOk ? SENSOR_OK : SENSOR_FLAT, true)) {
      lastRelayedWasherRunning = wr;
      lastRelayedWasherEmpty = we;
      lastRelayedWasherMicOk = micOk;
      washerRelayedOnce = true;
    }
  }

  bool dr, de;
  const char* ds;
  portENTER_CRITICAL(&dryerMux);
  dr = pubRunning;
  de = pubEmpty;
  ds = pubSensorState;
  portEXIT_CRITICAL(&dryerMux);

  if (!reportedOnce || dr != lastReportedRunning || de != lastReportedEmpty) {
    if (tryUpload(dryerSlot, machineId, dr, de, ds, false)) {
      lastReportedRunning = dr;
      lastReportedEmpty = de;
      reportedOnce = true;
      lastSendTime = currentTime;
    }
  }

  if (currentTime - lastSendTime >= sendInterval) {
    if (tryUpload(dryerSlot, machineId, dr, de, ds, false)) {
      lastReportedRunning = dr;
      lastReportedEmpty = de;
      reportedOnce = true;
      lastSendTime = currentTime;  // only on success, or the server TTL flaps
    }
  }
}

void networkTask(void* parameter) {
  netHeartbeat = millis();
  connectToEduroam();
  for (;;) {
    netHeartbeat = millis();
    maintainWiFiConnection();
    serviceBleScan();
    serviceDryerAdvert();
    serviceUploads();
    serviceTelemetry();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void loop() {
  loopHeartbeat = millis();
  applyDryerConfig();
  recordTelemetrySecond();

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
      running = true;
      empty = false;
    } else {
      running = false;
    }
  }

  float activitysmall3ago = activitysmallHistory[activitysmallIdx];  //This stores value of small acitivty change from 0.15 seconds ago to compare it to current small activity change to see if door opened/closed
  //As idxthree was incremented before this, it now points to the item of the array that was recorded last, 3 cycles ago (0.15s)
  if (activitysmall - activitysmall3ago > secDoorJumpMax) secDoorJumpMax = activitysmall - activitysmall3ago;

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

  publishDryer();

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

  // Fixed buffer instead of String concatenation: no transient small allocations
  // dropped into freed TLS regions, and no silent empty payload on alloc failure.
  char jsonPayload[256];
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
             "\"resetReason\":%d,\"uptime\":%lu,\"freeHeap\":%u,\"configVersion\":%d}",
             targetMachineId,
             isRunning ? "true" : "false",
             isEmpty ? "true" : "false",
             stateStr,
             (int)esp_reset_reason(),
             (unsigned long)(esp_timer_get_time() / 1000000),
             (unsigned)ESP.getFreeHeap(),
             reportedConfigVersion());
  }

  String reply;
  int httpResponseCode = httpPostJson(serverUrl, jsonPayload, &reply);
  netHeartbeat = millis();

  bool ok = (httpResponseCode >= 200 && httpResponseCode < 300);

  if (ok) {
    applyServerReply(reply);
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
