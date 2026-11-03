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

// Global status tracking for the washer captured over Bluetooth
volatile bool washerRunning = false;
volatile bool washerEmpty = true;
volatile bool washerHeard = false;  // never report defaults before the first packet
volatile bool washerMicOk = true;
uint16_t washerAckVersion = 0;      // config version the washer says it is following
unsigned long washerLastHeardMs = 0;
portMUX_TYPE washerMux = portMUX_INITIALIZER_UNLOCKED;

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
volatile bool wifiReinitRequested = false;  // set after repeated failed reports, serviced by the network task
volatile bool wifiEverConnected = false;
volatile uint8_t loginRejects = 0;  // login refusals since the last successful join

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

// Snapshot of the dryer state for the network task (loop() writes, core 0 reads).
portMUX_TYPE dryerMux = portMUX_INITIALIZER_UNLOCKED;
bool pubRunning = false;
bool pubEmpty = true;
const char* pubSensorState = "ok";

// ---------------- BLE link with the paired washer ----------------
// Both sides broadcast 27 bytes of manufacturer data: FF FF 'B' <type> <pair> ...
// A legacy BLE advertisement holds at most 31 bytes, so the washer sends its last two
// 5 s rounds every round, and the dryer can miss several seconds without losing one.
// Washer 'W': [5] status bits, [6-7] applied config version, [8] round counter,
//             [9-17] newest round, [18-26] the round before (water1, water2, door drop, 24-bit each).
// Dryer  'D': [5-6] config version, [7] flags, [8-10] first fill, [11-13] refill,
//             [14-17] door drop, [18-20] refill ms, [21-23] full ms, [24-26] max cycle ms.
const uint8_t LINK_LEN = 27;
const uint8_t ST_RUNNING = 0x01, ST_EMPTY = 0x02, ST_MIC_OK = 0x04,  // 0x08 was test mode
              ST_PREV_RUNNING = 0x10, ST_PREV_EMPTY = 0x20, ST_CUR_VALID = 0x40, ST_PREV_VALID = 0x80;
const uint8_t DF_BANDS2 = 0x02, DF_BANDS3 = 0x04, DF_HAS_CONFIG = 0x80;  // 0x01 was test mode

// ---------------- Website thresholds ----------------
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

// ---------------- Reports to the website ----------------
// One POST carries everything: both machines' status for the website and the 5 s readings
// for the live graphs. Sent every reportEveryMs, sooner when a status changes; readings stay
// queued until the server confirms them, so a failed send is simply retried.
const unsigned long PERIOD_MS = 5000;
const unsigned long MIN_SEND_GAP_MS = 5000;  // a flickering status can't send faster than this
const unsigned long RETRY_MIN_MS = 5000;
const unsigned long RETRY_MAX_MS = 60000;
const unsigned long WASHER_RECENT_MS = 60000;  // the washer's status is reported only while it is heard
const int QUEUE_MAX = 120;  // 10 min of 5 s readings
const int SEND_MAX = 60;    // per POST, so the JSON always fits reportJson
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
struct WasherRound {
  uint32_t seq;
  unsigned long t;  // this dryer's millis() when the round ended, as near as it can tell
  uint32_t w1, w2, drop;
  bool running, empty;
};
struct ReportStatus {
  bool dRunning, dEmpty;
  const char* dSensor;
  bool wKnown, wRunning, wEmpty, wMicOk;
  // Only running/empty changes send at once; sensor health rides on the next regular report.
  bool sameMachineState(const ReportStatus& o) const {
    return dRunning == o.dRunning && dEmpty == o.dEmpty
           && (!wKnown || !o.wKnown || (wRunning == o.wRunning && wEmpty == o.wEmpty));
  }
};

portMUX_TYPE dryerQMux = portMUX_INITIALIZER_UNLOCKED;
Ring<DryerPeriod> dryerQ;   // loop() pushes under dryerQMux
Ring<WasherRound> washerQ;  // the BLE callback pushes under washerMux
uint32_t dryerSeq = 0, washerSeq = 0;
uint8_t lastWasherRound = 0;
unsigned long lastWasherRoundAt = 0;
bool washerRoundSeen = false;
unsigned long periodStart = 0;
float periodAvgMax = 0, periodDoorJumpMax = 0;  // loop() only, closed by closeDryerPeriod()
uint32_t bootId = 0;   // with batchNo, lets the server replace a batch it already stored
uint32_t batchNo = 0;  // advances only on success, so a retry reuses it
DryerPeriod sendD[SEND_MAX];
WasherRound sendW[SEND_MAX];
char reportJson[8192];
size_t reportLen = 0;

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

// Called with washerMux held. Each packet repeats the washer's last two rounds, so each round
// is queued once, the first time it is heard; a lower counter after silence is a washer reboot.
void queueWasherRound(uint8_t counter, unsigned long t, const uint8_t* p, bool isRunning, bool isEmpty) {
  int8_t ahead = (int8_t)(counter - lastWasherRound);
  bool rebooted = ahead < -2 && t - lastWasherRoundAt > 15000;
  if (washerRoundSeen && ahead <= 0 && !rebooted) return;
  washerQ.push({ ++washerSeq, t, getU24(p), getU24(p + 3), getU24(p + 6), isRunning, isEmpty });
  lastWasherRound = counter;
  lastWasherRoundAt = t;
  washerRoundSeen = true;
}

// Every nearby advert passes through here, so read the raw packet rather than
// getManufacturerData(), which heap-allocates a copy per packet and fragments memory.
static const uint8_t* findLinkData(const std::vector<uint8_t>& p) {
  for (size_t i = 0; i + 1 < p.size();) {
    uint8_t len = p[i];
    if (len == 0 || i + 1 + len > p.size()) return nullptr;
    if (p[i + 1] == 0xFF && len - 1 >= LINK_LEN) return &p[i + 2];
    i += 1 + len;
  }
  return nullptr;
}

class MyAdvertisedDeviceCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    const uint8_t* b = findLinkData(advertisedDevice->getPayload());
    if (!b) {
      return;
    }
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
    if (st & ST_PREV_VALID) queueWasherRound((uint8_t)(b[8] - 1), now - PERIOD_MS, b + 18, st & ST_PREV_RUNNING, st & ST_PREV_EMPTY);
    if (st & ST_CUR_VALID) queueWasherRound(b[8], now, b + 9, st & ST_RUNNING, st & ST_EMPTY);
    portEXIT_CRITICAL(&washerMux);

    if (changed) {
      Serial.printf("[BLE RX] washer rssi=%d running=%d empty=%d mic=%d config=v%u\n",
                    advertisedDevice->getRSSI(), bool(st & ST_RUNNING), bool(st & ST_EMPTY),
                    bool(st & ST_MIC_OK), (unsigned)getU16(b + 6));
    }
  }
};

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WIFI] Associated with an eduroam access point");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiEverConnected = true;
      loginRejects = 0;
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
      // The EAP login needs a large contiguous block; maxAlloc shows if memory is why it fails.
      Serial.printf("[WIFI] Disconnected. Reason code: %u (heap=%u maxAlloc=%u)\n",
                    info.wifi_sta_disconnected.reason, (unsigned)ESP.getFreeHeap(),
                    (unsigned)ESP.getMaxAllocHeap());
      if (info.wifi_sta_disconnected.reason == WIFI_REASON_802_1X_AUTH_FAILED
          || info.wifi_sta_disconnected.reason == WIFI_REASON_AUTH_FAIL) {
        loginRejects++;
      }
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
  pBLEScan->stop();

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

  // The same credentials already worked this boot, so repeated refusals mean the login ran short
  // of memory; a reboot clears it. Never before the first join, so a bad password can't loop.
  if (wifiEverConnected && loginRejects >= 3) {
    hardRestart("WiFi login keeps being refused after a working session");
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
  bootId = esp_random();
  preventiveRebootUs = (uint64_t)(86400000UL + (nodeSalt % 21600000UL)) * 1000ULL;  // 24 h + up to 6 h

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

  // Bluetooth: broadcast thresholds to the washer, and listen for its packets.
  NimBLEDevice::init("");
  pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setConnectableMode(BLE_GAP_CONN_MODE_NON);
  pAdvertising->setMinInterval(320);  // 200 ms, in 0.625 ms units
  pAdvertising->setMaxInterval(480);  // 300 ms
  updateDryerAdvert();
  pAdvertising->start();

  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(new MyAdvertisedDeviceCallbacks(), true);  // every packet, not just new devices
  pBLEScan->setMaxResults(0);  // callback only; never cache results next to eduroam's TLS buffers
  pBLEScan->setActiveScan(false);
  pBLEScan->setInterval(160);  // 100 ms
  pBLEScan->setWindow(48);     // 30 ms of it listening

  periodStart = millis();

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

// ---------------- Website thresholds and readings ----------------
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

// The reply to every report: how often to report, plus saved thresholds if any.
void applyServerReply(const String& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  if (doc["reportEveryS"].is<int>()) {
    reportEveryMs = constrain(doc["reportEveryS"].as<int>(), 15, 90) * 1000UL;
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
      || n.refillMs == 0 || n.fullMs == 0 || n.maxCycleMs < n.fullMs || n.refillBands < 1 || n.refillBands > 3) {
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

// What the washer listens for: its thresholds once the website has saved some.
void updateDryerAdvert() {
  uint8_t b[LINK_LEN] = { 0xFF, 0xFF, 'B', 'D', pairNumber };
  uint8_t flags = 0;
  if (appliedConfigVersion > 0) {
    NodeConfig c;
    portENTER_CRITICAL(&cfgMux);
    c = nodeCfg;
    portEXIT_CRITICAL(&cfgMux);
    flags |= DF_HAS_CONFIG;
    if (c.refillBands == 2) flags |= DF_BANDS2;
    if (c.refillBands == 3) flags |= DF_BANDS3;
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
  if (sentVersion == appliedConfigVersion) return;
  sentVersion = appliedConfigVersion;
  updateDryerAdvert();
}

// Listening ~30% of the time: each washer packet carries two 5 s rounds, so every
// round arrives while most of the airtime is left to eduroam. Paused while joining
// so the EAP login gets the shared radio.
void serviceBleScan() {
  if (WiFi.status() != WL_CONNECTED) {
    if (pBLEScan->isScanning()) pBLEScan->stop();
    return;
  }
  if (!pBLEScan->isScanning()) {
    pBLEScan->start(0, false, true);  // 0 = until stopped
  }
}

// loop(): every 5 s, queues the highest readings of the period and the state at its end.
void closeDryerPeriod() {
  if (millis() - periodStart < PERIOD_MS) return;
  periodStart = millis();

  DryerPeriod p = { 0, periodStart, periodAvgMax, periodDoorJumpMax, running, empty };
  periodAvgMax = periodDoorJumpMax = 0;
  portENTER_CRITICAL(&dryerQMux);
  p.seq = ++dryerSeq;
  dryerQ.push(p);
  portEXIT_CRITICAL(&dryerQMux);
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
  if (code < 0) {
    char tlsErr[100] = "";
    client.lastError(tlsErr, sizeof(tlsErr));
    Serial.printf("[HTTP] %s; tls: %s; rssi=%d dBm; maxAlloc=%u\n", HTTPClient::errorToString(code).c_str(),
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
  s.dRunning = pubRunning;
  s.dEmpty = pubEmpty;
  s.dSensor = pubSensorState;
  portEXIT_CRITICAL(&dryerMux);
  unsigned long now = millis();
  portENTER_CRITICAL(&washerMux);
  // Signed: a packet heard just after `now` was read must not look 49 days old.
  s.wKnown = washerHeard && (long)(now - washerLastHeardMs) < (long)WASHER_RECENT_MS;
  s.wRunning = washerRunning;
  s.wEmpty = washerEmpty;
  s.wMicOk = washerMicOk;
  portEXIT_CRITICAL(&washerMux);
  return s;
}

// One POST with both machines' status and the queued 5 s readings. Readings leave the queue
// only once the server confirms them; a retry reuses batchNo, so the server replaces, never duplicates.
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
                         "\"dryer\":{\"running\":%s,\"empty\":%s,\"sensorState\":\"%s\"},\"washer\":",
                         machineId, washerMachineId, (unsigned long)bootId, (unsigned long)batchNo,
                         reportedConfigVersion(), (unsigned long)(esp_timer_get_time() / 1000000),
                         (int)esp_reset_reason(), (unsigned)ESP.getFreeHeap(),
                         st.dRunning ? "true" : "false", st.dEmpty ? "true" : "false", st.dSensor);
  if (st.wKnown) {
    fits = fits && appendJson("{\"running\":%s,\"empty\":%s,\"micOk\":%s}", st.wRunning ? "true" : "false",
                              st.wEmpty ? "true" : "false", st.wMicOk ? "true" : "false");
  } else {
    fits = fits && appendJson("null");
  }
  // [age ms, highest avg10, highest door jump, running, empty]
  fits = fits && appendJson(",\"d\":[");
  for (int i = 0; fits && i < nd; i++) {
    const DryerPeriod& p = sendD[i];
    // A nan would make the whole report invalid JSON.
    fits = appendJson("%s[%lu,%.3f,%.3f,%d,%d]", i ? "," : "", now - p.t, isfinite(p.avgMax) ? p.avgMax : 0.0f,
                      isfinite(p.doorJumpMax) ? p.doorJumpMax : 0.0f, p.running, p.empty);
  }
  // [age ms, highest water 1, highest water 2, highest door drop, running, empty]
  fits = fits && appendJson("],\"w\":[");
  for (int i = 0; fits && i < nw; i++) {
    const WasherRound& r = sendW[i];
    fits = appendJson("%s[%lu,%lu,%lu,%lu,%d,%d]", i ? "," : "", now - r.t, (unsigned long)r.w1,
                      (unsigned long)r.w2, (unsigned long)r.drop, r.running, r.empty);
  }
  fits = fits && appendJson("]}");
  if (!fits) {
    Serial.println("❌ Report did not fit its buffer");
    return false;
  }

  String reply;
  unsigned long postStart = millis();
  int code = httpPostJson(serverUrl, reportJson, &reply);
  netHeartbeat = millis();
  // A 4xx is the server refusing this report, so resending it can't help and would block every
  // later status. Its readings are dropped; the status goes out again with the next report.
  bool rejected = code >= 400 && code < 500 && code != 408 && code != 429;
  if ((code < 200 || code >= 300) && !rejected) {
    Serial.printf("❌ Report failed: %d (%s) after %lu ms, rssi=%d dBm, maxAlloc=%u, queued d=%d w=%d\n", code,
                  HTTPClient::errorToString(code).c_str(), millis() - postStart, (int)WiFi.RSSI(),
                  (unsigned)ESP.getMaxAllocHeap(), nd, nw);
    if (++consecutiveHttpFailures >= MAX_HTTP_FAILURES) {
      Serial.println("[WIFI] Repeated report failures. Requesting a clean re-join.");
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
  if (rejected) {
    Serial.printf("❌ Report rejected by the server: %d, dropped d=%d w=%d\n", code, nd, nw);
    return false;
  }
  applyServerReply(reply);
  Serial.printf("✅ Report %d: d=%d w=%d dryer run=%d empty=%d, washer %s, heap=%u\n", code, nd, nw,
                st.dRunning, st.dEmpty, st.wKnown ? (st.wRunning ? "running" : "idle") : "not heard",
                (unsigned)ESP.getFreeHeap());
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

void networkTask(void* parameter) {
  netHeartbeat = millis();
  connectToEduroam();
  for (;;) {
    netHeartbeat = millis();
    maintainWiFiConnection();
    serviceBleScan();
    serviceDryerAdvert();
    serviceReports();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void loop() {
  loopHeartbeat = millis();
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
      running = true;
      empty = false;
    } else {
      running = false;
    }
  }

  float activitysmall3ago = activitysmallHistory[activitysmallIdx];  //This stores value of small acitivty change from 0.15 seconds ago to compare it to current small activity change to see if door opened/closed
  //As idxthree was incremented before this, it now points to the item of the array that was recorded last, 3 cycles ago (0.15s)
  if (activitysmall - activitysmall3ago > periodDoorJumpMax) periodDoorJumpMax = activitysmall - activitysmall3ago;

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

void print_accels() {
  Serial.print("Avg10:");
  Serial.print(avg10);
  Serial.print(",");
  Serial.print("ActivitySmall:");
  Serial.println(activitysmall);
}
