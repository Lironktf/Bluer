#include <driver/i2s.h>
#include <arduinoFFT.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>

// Custom tracking string for the washer
#define WASHER_BLE_NAME "WASHER_M19"        //VARIES: must equal the name the paired dryer filters on
const char* machineId = "WASHER_NODE_M19";  //Doesnt  matter

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
const unsigned long TIMER_FULL_MS = 1500000UL;   // 25 min, start of cycle, WallClock, consistant timing
const unsigned long TIMER_REFILL_MS = 600000UL;  // 10 min 30 s Very accurate timing, perfect for at least the rightmost washer in tests
const double FIRST_FILL_HIGH2 = 68500;          // either band, starts the cycle
const double REFILL_HIGH = 32000;                // 7875–9750 band, this is a later water in, tops the timer back up
//For above, M1 max from machine next is 45000, test if all the water ins are above this day of installation
unsigned long cycleStartedAt = 0;              // when the current cycle began
const unsigned long MAX_CYCLE_MS = 2520000UL;  // 42 min hard cap
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
BLEAdvertising* pAdvertising;

void hardRestart(const char* why) {
  Serial.printf("[SUPERVISOR] Restart: %s\n", why);
  Serial.flush();
  delay(50);
  esp_restart();
}

void update_ble_data() {
  String strData = "";
  strData += (running ? "1" : "0");
  strData += (empty ? "1" : "0");
  strData += (micHealthy ? "1" : "0");  // third byte: older dryers read only [0] and [1]

  pAdvertising->stop();
  BLEAdvertisementData oAdvertisementData;
  oAdvertisementData.setName(WASHER_BLE_NAME);
  oAdvertisementData.setManufacturerData(strData.c_str());
  pAdvertising->setAdvertisementData(oAdvertisementData);
  pAdvertising->start();

  Serial.printf("[BLE Broadcast] Updated -> Running: %d, Empty: %d, mic=%s, heap=%u\n",
                running, empty, micHealthy ? "ok" : "FLAT", (unsigned)ESP.getFreeHeap());
}

// Runs on core 0 so it keeps ticking even when loop() is blocked inside a driver call.
void supervisorTask(void* parameter) {
  esp_task_wdt_add(NULL);  // hardware backstop in case the supervisor itself stalls

  for (;;) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5000));

    if (millis() - loopHeartbeat > LOOP_STALL_TIMEOUT_MS) {
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
  preventiveRebootUs = (uint64_t)(86400000UL + (nodeSalt % 21600000UL)) * 1000ULL;  // 24 h + up to ~4.7 h

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

  BLEDevice::init(WASHER_BLE_NAME);
  pAdvertising = BLEDevice::getAdvertising();
  update_ble_data();  // Initialize the first broadcast data packet

  if (setup_i2s() != ESP_OK) {
    hardRestart("I2S would not start at boot");
  }
  delay(1000);
}

void loop() {
  loopHeartbeat = millis();

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
    update_ble_data();
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

  if ((prevlowmid > (lowmid + 750000)) && !running && !doorclosed) {
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
      if ((actualAvgWater > REFILL_HIGH || actualAvgWater2 > REFILL_HIGH) && (cycleEndsAt - millis() < TIMER_REFILL_MS)) {
      //if (actualAvgWater > REFILL_HIGH && (cycleEndsAt - millis() < TIMER_REFILL_MS)) {  //If when its supposed to end minus time now is < 10 minutes, even if timer had already passed here, its unsigned, so would be false and wouldnt incorrectly add more time to timer
        cycleEndsAt = millis() + TIMER_REFILL_MS;                                        //Update end time
        Serial.println("💧 Refill detected -> timer reset to 10 min 30 s");
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
    update_ble_data();
    lastSendTime = currentTime;
  }

  if (currentTime - lastSendTime >= sendInterval) {
    update_ble_data();
    lastSendTime = currentTime;
  }

  delay(25);
}
