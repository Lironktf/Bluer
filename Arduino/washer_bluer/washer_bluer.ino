#include <driver/i2s.h>
#include <arduinoFFT.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>

// Must equal the number at the end of the paired dryer's machineId (sj-d6 -> 6).
// Flash the dryer and washer of a pair together: older firmware uses a different BLE format.
//VARIES: must match the paired dryer's trailing ID number.
const uint8_t PAIR_NUMBER = 1;
//VARIES: serial-log label only; it doesn't affect BLE pairing or website IDs.
const char* machineId = "WASHER_NODE_W1";

// Read by supervisorTask on core 0, written by loop() on core 1.
volatile bool empty = true;
volatile bool running = false;
bool doorclosed = false;
bool wasRunning = false;
bool wasEmpty = true;
const int WINDOW = 200;  // 200 samples × 25 ms = 5 second, considering other delays, more like 10s of averaging, used to make avg HIGH activity variable
int idx = 0;
double HighFreqWindow[WINDOW];
double AvgHighFreq = 0.0;
double HighFreqWindow2[WINDOW];
double AvgHighFreq2 = 0.0;
double lowmid = 0.0;
double high = 0.0;
double high2 = 0.0;
unsigned long cycleEndsAt = 0;                   // loops remaining in current cycle
// Built-in values. Thresholds saved on the website reach this washer through its dryer,
// replace these, and are kept in flash across reboots.
unsigned long TIMER_FULL_MS = 1500000UL;   // 25 min, start of cycle, WallClock, consistant timing
unsigned long TIMER_REFILL_MS = 630000UL;  // 10 min 30 s, per-device, see Arduino/MACHINES.md. Very accurate timing, perfect for at least the rightmost washer in tests
double FIRST_FILL_HIGH2 = 68500;          // either band, starts the cycle
double REFILL_HIGH = 32000;                // per-device, see Arduino/MACHINES.md. this is a later water in, tops the timer back up
int REFILL_BANDS = 2;                      // per-device, 1 = lower band, 2 = either band, 3 = upper band only
double DOOR_DROP = 750000;                 // per-device, lowmid drop that marks the door closed
//For above, M1 max from machine next is 45000, test if all the water ins are above this day of installation
unsigned long cycleStartedAt = 0;              // when the current cycle began
unsigned long MAX_CYCLE_MS = 2520000UL;  // 42 min hard cap
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 300000;  // 5 min

// ---------------- Restart, Reliability supervision ----------------
volatile unsigned long loopHeartbeat = 0;
const unsigned long LOOP_STALL_TIMEOUT_MS = 60000UL;  // loop frozen this long -> reboot
uint64_t preventiveRebootUs = 0;                             // 24 h + per-node jitter
const uint64_t ABSOLUTE_REBOOT_US = 604800000ULL * 1000ULL;  // 7 days regardless of state
const int MAX_I2S_FAILURES = 20;
int consecutiveI2sFailures = 0;
uint32_t nodeSalt = 0;

// A live INMP441 always has a noise floor, so bit-identical samples mean there is
// no signal path. i2s_read still returns ESP_OK with a zero-filled buffer when the
// mic is unpowered or unwired, so this is the only way that fault becomes visible.
const int MAX_FLAT_READS = 200;  // ~5 s at the 25 ms loop
int flatReadCount = 0;
bool micHealthy = true;

void hardRestart(const char* why);
esp_err_t setup_i2s();

// INMP441 wiring (classic ESP32): WS -> GPIO 25, SD -> GPIO 33, SCK -> GPIO 26, VDD -> 3V3, GND and L/R -> GND.
#define I2S_WS 25
#define I2S_SD 33
#define I2S_SCK 26
#define I2S_PORT I2S_NUM_0
#define SAMPLES 512
#define SAMPLE_RATE 44100

double vReal[SAMPLES];
double vImag[SAMPLES];
int32_t i2s_buffer[SAMPLES];

ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLE_RATE);

// ---------------- BLE link with the paired dryer ----------------
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
const uint8_t DF_TEST = 0x01, DF_BANDS2 = 0x02, DF_BANDS3 = 0x04, DF_HAS_CONFIG = 0x80;

NimBLEAdvertising* pAdvertising;
NimBLEScan* pScan;
const unsigned long DRYER_LISTEN_EVERY_MS = 20000;
const uint32_t DRYER_LISTEN_MS = 1500;
const unsigned long DRYER_SILENT_TEST_OFF_MS = 300000UL;  // dryer unheard this long -> stop test mode
unsigned long lastListenStart = 0;

struct DryerMessage {
  uint16_t version;
  uint8_t flags;
  uint32_t firstFill, refill, doorDrop, refillMs, fullMs, maxCycleMs;
};
portMUX_TYPE dryerMsgMux = portMUX_INITIALIZER_UNLOCKED;
DryerMessage pendingDryerMsg;
volatile uint32_t dryerMsgSeq = 0;  // bumped by the BLE callback, applied by loop()
unsigned long lastDryerHeardMs = 0;

struct SavedConfig {
  uint16_t version;
  uint8_t refillBands;
  uint32_t firstFill, refill, doorDrop, refillMs, fullMs, maxCycleMs;
};
Preferences prefs;
uint16_t appliedConfigVersion = 0;  // 0 = built-in values
uint16_t rejectedConfigVersion = 0;

// Test mode: the same per-second peaks the combined node uploads.
bool testMode = false;
double secWater1Max = 0, secWater2Max = 0, secDoorDropMax = 0;
int secReads = 0;
struct SecondSample {
  uint32_t w1, w2, drop;
  bool running, empty, valid;
};
SecondSample curSecond = {}, prevSecond = {};
uint8_t secondCounter = 0;

void hardRestart(const char* why) {
  Serial.printf("[SUPERVISOR] Restart: %s\n", why);
  Serial.flush();
  delay(50);
  esp_restart();
}

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
static uint32_t getU16(const uint8_t* p) {
  return p[0] | (p[1] << 8);
}
static uint32_t getU24(const uint8_t* p) {
  return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16);
}
static uint32_t getU32(const uint8_t* p) {
  return getU24(p) | ((uint32_t)p[3] << 24);
}
static uint32_t toU24(double v) {
  if (!(v > 0)) return 0;
  return v >= 16777215.0 ? 16777215UL : (uint32_t)(v + 0.5);
}

// The washer's status, plus two seconds of test-mode readings while test mode is on.
void updateAdvert() {
  uint8_t b[LINK_LEN] = { 0xFF, 0xFF, 'B', 'W', PAIR_NUMBER };
  uint8_t st = 0;
  if (running) st |= ST_RUNNING;
  if (empty) st |= ST_EMPTY;
  if (micHealthy) st |= ST_MIC_OK;
  if (testMode) {
    st |= ST_TEST;
    if (prevSecond.running) st |= ST_PREV_RUNNING;
    if (prevSecond.empty) st |= ST_PREV_EMPTY;
    if (curSecond.valid) st |= ST_CUR_VALID;
    if (prevSecond.valid) st |= ST_PREV_VALID;
    b[8] = secondCounter;
    putU24(b + 9, curSecond.w1);
    putU24(b + 12, curSecond.w2);
    putU24(b + 15, curSecond.drop);
    putU24(b + 18, prevSecond.w1);
    putU24(b + 21, prevSecond.w2);
    putU24(b + 24, prevSecond.drop);
  }
  b[5] = st;
  putU16(b + 6, appliedConfigVersion);

  NimBLEAdvertisementData data;
  data.setManufacturerData(b, LINK_LEN);
  pAdvertising->setAdvertisementData(data);
}

class DryerScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    std::string d = advertisedDevice->getManufacturerData();
    if (d.size() < LINK_LEN) return;
    const uint8_t* b = (const uint8_t*)d.data();
    if (b[0] != 0xFF || b[1] != 0xFF || b[2] != 'B' || b[3] != 'D' || b[4] != PAIR_NUMBER) return;

    DryerMessage m;
    m.version = getU16(b + 5);
    m.flags = b[7];
    m.firstFill = getU24(b + 8);
    m.refill = getU24(b + 11);
    m.doorDrop = getU32(b + 14);
    m.refillMs = getU24(b + 18);
    m.fullMs = getU24(b + 21);
    m.maxCycleMs = getU24(b + 24);
    portENTER_CRITICAL(&dryerMsgMux);
    pendingDryerMsg = m;
    dryerMsgSeq = dryerMsgSeq + 1;
    portEXIT_CRITICAL(&dryerMsgMux);
  }
};

bool configValid(const SavedConfig& c) {
  return c.version > 0 && c.firstFill > 0 && c.refill > 0 && c.doorDrop > 0 && c.refillMs > 0 && c.fullMs > 0
         && c.maxCycleMs >= c.fullMs && c.refillBands >= 1 && c.refillBands <= 3;
}

void useConfig(const SavedConfig& c) {
  FIRST_FILL_HIGH2 = c.firstFill;
  REFILL_HIGH = c.refill;
  REFILL_BANDS = c.refillBands;
  DOOR_DROP = c.doorDrop;
  TIMER_REFILL_MS = c.refillMs;
  TIMER_FULL_MS = c.fullMs;
  MAX_CYCLE_MS = c.maxCycleMs;
  appliedConfigVersion = c.version;
}

void loadSavedConfig() {
  prefs.begin("bluer", false);
  // Never reuse thresholds saved for a different pair after a reflash.
  if (prefs.getUChar("pair", 0) != PAIR_NUMBER) return;
  SavedConfig c;
  if (prefs.getBytes("cfg", &c, sizeof(c)) != sizeof(c) || !configValid(c)) return;
  useConfig(c);
  Serial.printf("[CONFIG] Using saved website thresholds v%u\n", c.version);
}

// loop(): follows test mode and thresholds relayed by the dryer.
void applyDryerMessage() {
  static uint32_t seen = 0;
  if (seen == dryerMsgSeq) {
    if (testMode && millis() - lastDryerHeardMs > DRYER_SILENT_TEST_OFF_MS) {
      testMode = false;
      Serial.println("[TEST] Dryer not heard for 5 min, test mode off");
      updateAdvert();
    }
    return;
  }
  DryerMessage m;
  portENTER_CRITICAL(&dryerMsgMux);
  m = pendingDryerMsg;
  seen = dryerMsgSeq;
  portEXIT_CRITICAL(&dryerMsgMux);
  lastDryerHeardMs = millis();

  bool changed = false;
  bool wantTest = m.flags & DF_TEST;
  if (wantTest != testMode) {
    testMode = wantTest;
    curSecond = prevSecond = SecondSample{};
    Serial.printf("[TEST] Test mode %s\n", testMode ? "ON" : "off");
    changed = true;
  }

  if ((m.flags & DF_HAS_CONFIG) && m.version != appliedConfigVersion && m.version != rejectedConfigVersion) {
    uint8_t refillBands = (m.flags & DF_BANDS3) ? 3 : ((m.flags & DF_BANDS2) ? 2 : 1);
    SavedConfig c = { m.version, refillBands, m.firstFill, m.refill, m.doorDrop,
                      m.refillMs, m.fullMs, m.maxCycleMs };
    if (configValid(c)) {
      useConfig(c);
      prefs.putUChar("pair", PAIR_NUMBER);
      prefs.putBytes("cfg", &c, sizeof(c));
      Serial.printf("[CONFIG] Now following website thresholds v%u\n", c.version);
      changed = true;
    } else {
      rejectedConfigVersion = m.version;
      Serial.printf("[CONFIG] Ignored invalid thresholds v%u from the dryer\n", m.version);
    }
  }

  if (changed) updateAdvert();
}

// Listens for the dryer briefly every 20 s. start() returns at once; results arrive in DryerScanCallbacks.
void listenForDryer() {
  if (millis() - lastListenStart < DRYER_LISTEN_EVERY_MS) return;
  lastListenStart = millis();
  pScan->start(DRYER_LISTEN_MS, false, true);
}

// Closes the current one-second window of peaks and broadcasts it while test mode is on.
void closeWasherSecond() {
  static unsigned long lastSecond = 0;
  if (millis() - lastSecond < 1000) return;
  lastSecond = millis();

  if (testMode) {
    prevSecond = curSecond;
    curSecond.valid = secReads > 0;
    curSecond.w1 = toU24(secWater1Max);
    curSecond.w2 = toU24(secWater2Max);
    curSecond.drop = toU24(secDoorDropMax);
    curSecond.running = running;
    curSecond.empty = empty;
    secondCounter++;
    updateAdvert();
  }
  secWater1Max = secWater2Max = secDoorDropMax = 0;
  secReads = 0;
}

// Runs on core 0 so it keeps ticking even when loop() is blocked inside a driver call.
void supervisorTask(void* parameter) {
  esp_task_wdt_add(NULL);  // hardware backstop in case the supervisor itself stalls

  for (;;) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5000));

    if ((long)(millis() - loopHeartbeat) > (long)LOOP_STALL_TIMEOUT_MS) {
      hardRestart("main loop stalled");
    }

    uint64_t upUs = (uint64_t)esp_timer_get_time();  // 64-bit: never rolls over

    // Not-running only: the acoustic door heuristic is not reliable enough to be
    // allowed to block a restart indefinitely.
    if (upUs >= preventiveRebootUs && !running) {
      hardRestart("preventive restart while idle");
    } else if (upUs >= ABSOLUTE_REBOOT_US) {
      hardRestart("7-day absolute cap");
    }
  }
}

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
    Serial.printf("[I2S] driver_install failed: %d\n", r);
    return r;
  }
  r = i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
  if (r != ESP_OK) {
    Serial.printf("[I2S] set_clk failed: %d\n", r);
    return r;
  }
  r = i2s_set_pin(I2S_PORT, &pin_config);
  if (r != ESP_OK) {
    Serial.printf("[I2S] set_pin failed: %d\n", r);
    return r;
  }
  i2s_zero_dma_buffer(I2S_PORT);
  return ESP_OK;
}

void setup() {
  Serial.begin(115200);

  delay(1000);
  Serial.printf("\n[BOOT] reset reason = %d\n", esp_reset_reason());

  nodeSalt = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
  randomSeed(nodeSalt ^ esp_random());
  preventiveRebootUs = (uint64_t)(86400000UL + (nodeSalt % 21600000UL)) * 1000ULL;  // 24 h + up to 6 h

  // Staggered boot so several nodes on one supply never draw inrush together.
  delay(nodeSalt % 12000);

  for (int i = 0; i < WINDOW; i++) {
    HighFreqWindow[i] = 0.0;
    HighFreqWindow2[i] = 0.0;
  }

  Serial.println("\n\n========================================");
  Serial.println("   Washing Machine Monitor");
  Serial.printf("   Machine ID: %s\n", machineId);
  Serial.println("========================================\n");

  loopHeartbeat = millis();

  esp_task_wdt_config_t wdtCfg = {
    .timeout_ms = 30000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&wdtCfg);

  xTaskCreatePinnedToCore(supervisorTask, "supervisor", 4096, NULL, 1, NULL, 0);

  loadSavedConfig();

  NimBLEDevice::init("");
  pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setConnectableMode(BLE_GAP_CONN_MODE_NON);
  pAdvertising->setMinInterval(160);  // 100 ms, in 0.625 ms units
  pAdvertising->setMaxInterval(160);
  updateAdvert();  // Initialize the first broadcast data packet
  pAdvertising->start();

  pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(new DryerScanCallbacks());
  pScan->setMaxResults(0);  // callback only; never cache results
  pScan->setActiveScan(false);
  pScan->setInterval(100);
  pScan->setWindow(60);
  lastListenStart = millis() - DRYER_LISTEN_EVERY_MS;  // listen right away

  if (setup_i2s() != ESP_OK) {
    hardRestart("I2S would not start at boot");
  }
  delay(1000);
}

void loop() {
  loopHeartbeat = millis();
  applyDryerMessage();
  listenForDryer();
  closeWasherSecond();

  wasRunning = running;
  wasEmpty = empty;

  size_t bytes_read = 0;
  // Never block forever here: a wedged I2S peripheral used to freeze the node
  // until it was unplugged.
  esp_err_t i2sResult = i2s_read(I2S_PORT, &i2s_buffer, sizeof(i2s_buffer), &bytes_read, pdMS_TO_TICKS(500));

  // A short read is a failure too: the driver returns ESP_OK with a partial
  // count on timeout, and zero-padding that into the FFT fakes a water-fill.
  if (i2sResult != ESP_OK || bytes_read != sizeof(i2s_buffer)) {
    if (++consecutiveI2sFailures >= MAX_I2S_FAILURES) {
      Serial.printf("[I2S] Stream dead (err=%d, got %u/%u). Reinstalling...\n",
                    i2sResult, (unsigned)bytes_read, (unsigned)sizeof(i2s_buffer));
      i2s_driver_uninstall(I2S_PORT);
      delay(100);
      setup_i2s();  // logs its own failure; a dead mic cannot be fixed in software
      consecutiveI2sFailures = 0;
    }
    delay(25);
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
    Serial.printf("[MIC] %s\n", micHealthy ? "signal restored" : "NO SIGNAL: samples flat, mic dead or unwired");
    updateAdvert();
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

  if ((prevlowmid > (lowmid + DOOR_DROP)) && !running && !doorclosed) {
    doorclosed = true;
    empty = true;
    Serial.println("Door Closed! Now Empty");
  }

  AvgHighFreq -= HighFreqWindow[idx];
  HighFreqWindow[idx] = high;
  AvgHighFreq += high;

  AvgHighFreq2 -= HighFreqWindow2[idx];
  HighFreqWindow2[idx] = high2;
  AvgHighFreq2 += high2;

  idx = (idx + 1) % WINDOW;

  double actualAvgWater = AvgHighFreq / WINDOW;
  double actualAvgWater2 = AvgHighFreq2 / WINDOW;

  if (actualAvgWater > secWater1Max) secWater1Max = actualAvgWater;
  if (actualAvgWater2 > secWater2Max) secWater2Max = actualAvgWater2;
  if (prevlowmid - lowmid > secDoorDropMax) secDoorDropMax = prevlowmid - lowmid;
  secReads++;

  if (!running && (actualAvgWater2 > FIRST_FILL_HIGH2 || actualAvgWater > FIRST_FILL_HIGH2)) {
    running = true;
    empty = false;
    doorclosed = false;
    cycleStartedAt = millis();
    cycleEndsAt = millis() + TIMER_FULL_MS;  //Cycle will be done at current time + 25min
    Serial.println("▶️ Cycle started (first fill detected)");
  }

  if (running) {
    // Hard ceiling: no cycle runs longer than 42 min, no matter how many refills fired
    if ((long)(millis() - cycleStartedAt) >= (long)MAX_CYCLE_MS) {
      running = false;
      Serial.println("🛑 Cycle force-ended (42 min hard cap reached)");
    } else {
      //If when its supposed to end minus time now is < the refill timer, even if timer had already passed here, its unsigned, so would be false and wouldnt incorrectly add more time to timer
      bool refillDetected = (REFILL_BANDS == 1 && actualAvgWater > REFILL_HIGH)
                || (REFILL_BANDS == 2 && (actualAvgWater > REFILL_HIGH || actualAvgWater2 > REFILL_HIGH))
                || (REFILL_BANDS == 3 && actualAvgWater2 > REFILL_HIGH);
      if (refillDetected && (cycleEndsAt - millis() < TIMER_REFILL_MS)) {
        cycleEndsAt = millis() + TIMER_REFILL_MS;  //Update end time
        Serial.println("💧 Refill detected -> timer topped up");
      }

      if ((long)(millis() - cycleEndsAt) >= 0) {
        running = false;
        Serial.println("🛑 Cycle ended (timer expired)");
      }
    }
  }

  Serial.print("Door_60-600Hz:");
  Serial.print(lowmid);
  Serial.print(",");
  Serial.print("Avg_7875_9750:");
  Serial.print(actualAvgWater);
  Serial.print(",");
  Serial.print("Avg_9750_11625:");
  Serial.println(actualAvgWater2);

  // Send status update to server every 5 min
  unsigned long currentTime = millis();

  if (empty != wasEmpty || running != wasRunning) {
    updateAdvert();
    Serial.printf("[BLE Broadcast] Updated -> Running: %d, Empty: %d, mic=%s, heap=%u\n",
                  running, empty, micHealthy ? "ok" : "FLAT", (unsigned)ESP.getFreeHeap());
    lastSendTime = currentTime;
  }

  if (currentTime - lastSendTime >= sendInterval) {
    updateAdvert();
    lastSendTime = currentTime;
  }

  delay(25);
}
