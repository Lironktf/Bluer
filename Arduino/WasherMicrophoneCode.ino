#include <driver/i2s.h>
#include <arduinoFFT.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>

// Custom tracking string for the washer
#define WASHER_BLE_NAME "WASHER_A1" //VARIES
const char* machineId = "WASHER_NODE_A1"; //Doesnt  matter

bool empty = true; //Two status booleans sent to website
bool running = false;
bool doorclosed = false;
bool wasRunning = false;
bool wasEmpty = true;
const int WINDOW = 200; // 200 samples × 25 ms = 5 second, considering other delays, more like 10s of averaging, used to make avg HIGH activity variable
int idx = 0;
double HighFreqWindow [WINDOW];
double AvgHighFreq = 0.0;
double HighFreqWindow2 [WINDOW];
double AvgHighFreq2 = 0.0;
double lowmid = 0.0;
double high = 0.0;
double high2 = 0.0;
unsigned long cycleEndsAt = 0;            // loops remaining in current cycle
const unsigned long TIMER_FULL_MS   = 1500000UL;  // 25 min, start of cycle, WallClock, consistant timing
const unsigned long TIMER_REFILL_MS = 630000UL;   // 10 min 30 s
const double FIRST_FILL_HIGH2 = 200000;  // 9750–11625 band, starts the cycle
const double REFILL_HIGH      = 58000;   // 7875–9750 band, this is a later water in, tops the timer back up
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 300000; // 5 min

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
BLEAdvertising *pAdvertising;

void update_ble_data() {
  String strData = "";
  strData += (running ? "1" : "0");
  strData += (empty ? "1" : "0");
  
  pAdvertising->stop();
  BLEAdvertisementData oAdvertisementData;
  oAdvertisementData.setName(WASHER_BLE_NAME);
  oAdvertisementData.setManufacturerData(strData.c_str());
  pAdvertising->setAdvertisementData(oAdvertisementData);
  pAdvertising->start();
  
  Serial.printf("[BLE Broadcast] Updated -> Running: %d, Empty: %d\n", running, empty);
}

void setup_i2s() { 
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

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
  i2s_set_pin(I2S_PORT, &pin_config);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  for (int i = 0; i < WINDOW; i++) {
      HighFreqWindow[i] = 0.0;
      HighFreqWindow2[i] = 0.0;
    }

  Serial.println("\n\n========================================");
  Serial.println("   Washing Machine Monitor");
  Serial.printf("   Machine ID: %s\n", machineId);
  Serial.println("========================================\n");

  BLEDevice::init(WASHER_BLE_NAME);
  pAdvertising = BLEDevice::getAdvertising();
  update_ble_data(); // Initialize the first broadcast data packet

  setup_i2s();
  delay(1000);
}

void loop() {

  wasRunning = running;
  wasEmpty = empty;
  
  size_t bytes_read;
  i2s_read(I2S_PORT, &i2s_buffer, sizeof(i2s_buffer), &bytes_read, portMAX_DELAY);
  int samples_read = bytes_read / sizeof(int32_t);

  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = (i < samples_read) ? (double)(i2s_buffer[i] >> 14) : 0;
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
    
    if (freq >= 60 && freq < 600) { zone_lowmid += vReal[i]; c_lowmid++; }
    else if (freq >= 7875 && freq < 9750)  { zone_high  += vReal[i]; c_high++; }
    else if (freq >= 9750 && freq < 11625) { zone_high2 += vReal[i]; c_high2++; }
  }

  // Calculate averages to keep the graph stable
  double prevlowmid = lowmid;
  lowmid = (c_lowmid > 0) ? (zone_lowmid / c_lowmid) : 0;
  high = (c_high > 0) ? (zone_high / c_high) : 0;
  high2 = (c_high2 > 0) ? (zone_high2 / c_high2) : 0;

  if ((prevlowmid > (lowmid + 750000)) && !running && !doorclosed){
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

  if (!running && actualAvgWater2 > FIRST_FILL_HIGH2){
    running = true;
    empty = false;
    doorclosed = false;
    cycleEndsAt = millis() + TIMER_FULL_MS; //Cycle will be done at current time + 25min
    Serial.println("▶️ Cycle started (first fill detected)");
  }

  if (running){
    if (actualAvgWater > REFILL_HIGH && (cycleEndsAt - millis() < TIMER_REFILL_MS)){ //If when its supposed to end minus time now is < 10 minutes, even if timer had already passed here, its unsigned, so would be false and wouldnt incorrectly add more time to timer
      cycleEndsAt = millis() + TIMER_REFILL_MS; //Update end time
      Serial.println("💧 Refill detected -> timer reset to 10 min 30 s");
    }

    if ((long)(millis() - cycleEndsAt) >= 0){
      running = false;
      Serial.println("🛑 Cycle ended (timer expired)");
    }
  }

  Serial.print("Door_60-600Hz:"); Serial.print(lowmid); Serial.print(",");
  Serial.print("Avg_7875_9750:");   Serial.print(actualAvgWater);  Serial.print(",");
  Serial.print("Avg_9750_11625:"); Serial.println(actualAvgWater2);

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
