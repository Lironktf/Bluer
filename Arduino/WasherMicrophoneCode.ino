#include <driver/i2s.h>
#include <arduinoFFT.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>

// Custom tracking string for the washer
#define WASHER_BLE_NAME "WASHER_A1"
const char* machineId = "WASHER_NODE_A1"; 

bool empty = true; //Two status booleans sent to website
bool running = false;
bool doorclosed = false;
bool wasRunning = false;
bool wasEmpty = true;
bool lastIntakeHIGH = false;
const int WINDOW = 200; // 200 samples × 25 ms = 5 second, used to make avg HIGH activity variable
int idx = 0;
int quiettime = 0;
double HighFreqWindow [WINDOW];
double AvgHighFreq = 0.0;
double lowmid = 0.0;
double high = 0.0;
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 300000; // 5 min

#define I2S_WS 25
#define I2S_SD 33
#define I2S_SCK 26
#define I2S_PORT I2S_NUM_0
#define SAMPLES 512          
#define SAMPLE_RATE 16000  

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
  if (running && !wasRunning){
    Serial.println("▶️ Machine started running ✅");
  }
  else if (!running && wasRunning) {
    Serial.println("🛑 Machine stopped");
  }

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
  double zone_high = 0;    // 6500Hz to 8000Hz Water in

  int c_lowmid = 0, c_high = 0;

  for (int i = 2; i < (SAMPLES / 2); i++) {
    double freq = i * binResolution;
    
    if (freq >= 60 && freq < 600) { zone_lowmid += vReal[i]; c_lowmid++; }
    else if (freq >= 6500 && freq < 7800) { zone_high += vReal[i]; c_high++; }
  }

  // Calculate averages to keep the graph stable
  double prevlowmid = lowmid;
  lowmid = (c_lowmid > 0) ? (zone_lowmid / c_lowmid) : 0;
  high = (c_high > 0) ? (zone_high / c_high) : 0;
  if ((prevlowmid > (lowmid + 150000)) && !running){
    doorclosed = true;
    empty = true;
    Serial.println("Door Closed! Now Empty");
  }

  AvgHighFreq -= HighFreqWindow[idx];
  HighFreqWindow[idx] = high;
  AvgHighFreq += high;
  idx = (idx + 1) % WINDOW;

  double actualAvgWater = AvgHighFreq / WINDOW;

  if (actualAvgWater > 180000){
    lastIntakeHIGH = true;
  }

  if (actualAvgWater > 40000){
    running = true;
    empty = false;
    
    if (quiettime > 0 && actualAvgWater < 180000){
      lastIntakeHIGH = false; //If we are starting a new water in cycle, after a break where it was < 40000, then break gets reset to normal
    }

    quiettime = 0;
  }
  else {
    quiettime++;
  }

  int maximumQuietLoops = 16559; // Standard 10-11 minutes limit
  if (lastIntakeHIGH) {
    maximumQuietLoops = 21126; // Extended 14 minutes limit
  }

  if (quiettime >= maximumQuietLoops){
    running = false;
    lastIntakeHIGH = false;
    quiettime = 0;         // Clean up counter tracking
    Serial.println("🛑 Cycle ended automatically due to timeout.");
  }

  Serial.print("Door_60-600Hz:"); Serial.print(lowmid); Serial.print(",");
  Serial.print("Water_6500-8000Hz:"); Serial.print(high); Serial.print(",");
  Serial.print("AvgWaterIn5:"); Serial.println(actualAvgWater);

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
