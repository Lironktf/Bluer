#include <driver/i2s.h>
#include <arduinoFFT.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_eap_client.h"
#include "secrets.h"

#include "esp_wifi.h" 

// University of Waterloo eduroam configuration.
// Replace only the password before uploading.
// Do not include < or > around the password. A ! does not need escaping.
const char* EDUROAM_SSID = "eduroam";
const char* EAP_IDENTITY = "anonymous@uwaterloo.ca";
const char* EXPECTED_SERVER_NAME = "eduroam.uwaterloo.ca";

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
unsigned long lastWifiReconnectAttempt = 0;
const unsigned long wifiReconnectInterval = 15000;

// Machine identification and server configuration
const char* machineId = "a1-m1"; // e.g., "a1-m1", "a2-m5", "b1-m3"
const char* serverUrl = "https://laun-dryer.vercel.app/api/machines";

// Timing for sending updates (send every 5 seconds)
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 300000; // 5 min

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

void connectToEduroam() {
  Serial.println("[WIFI] Configuring secure Waterloo eduroam connection...");

  WiFi.mode(WIFI_STA);

  WiFi.setSleep(false); // Disables modem sleep to prevent beacon timeout dropouts
  esp_wifi_set_max_tx_power(78); // Forces the antenna to its maximum transmission power (19.5 dBm)
  //
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWiFiEvent);

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
    EAP_PASSWORD
  );

  const unsigned long connectionTimeout = 30000;
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < connectionTimeout) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Initial eduroam connection timed out.");
    Serial.println("[WIFI] The monitor will keep running and retry automatically.");
  }
}

void maintainWiFiConnection() {
  // If connected, keep running normally
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  // If it's just temporarily shifting channels, let it breathe
  if (WiFi.status() == WL_IDLE_STATUS) {
    return; 
  }

  unsigned long currentTime = millis();
  if (currentTime - lastWifiReconnectAttempt >= wifiReconnectInterval) {
    lastWifiReconnectAttempt = currentTime;
    Serial.println("⚠️ Wi-Fi link fully broken. Performing clean system reboot to reconnect to eduroam...");
    delay(500);
    
    // Forces a clean system reset. This clears memory and guarantees a successful startup handshake!
    ESP.restart(); 
  }
}


#define I2S_WS 25
#define I2S_SD 33
#define I2S_SCK 26
#define I2S_PORT I2S_NUM_0

#define SAMPLES 512          
#define SAMPLE_RATE 44100   

#define WATER_THRESHOLD 1
#define WATER_DURATION 1

double vReal[SAMPLES];
double vImag[SAMPLES];
int32_t i2s_buffer[SAMPLES];

ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLE_RATE);

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
    // CHANGE 1: Turn off APLL to prevent Wi-Fi channel radio conflicts
    .use_apll = false 
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  
  // CHANGE 2: Manually clamp the clock source to the main internal system clock
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

  connectToEduroam();
  setup_i2s();
  delay(1000);
}

void loop() {
  maintainWiFiConnection();

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

  double binResolution = (double)SAMPLE_RATE / SAMPLES; // ~86.13 Hz per bin
  
  // Variables to hold energy for different frequency blocks
  double zone_lowmid = 0;  // 60Hz to 600Hz Door closed
  double zone_high = 0;    // 6500Hz to 8000Hz Water in

  int c_lowmid = 0, c_high = 0;

  for (int i = 2; i < (SAMPLES / 2); i++) {
    double freq = i * binResolution;
    
    if (freq >= 60 && freq < 600) { zone_lowmid += vReal[i]; c_lowmid++; }
    else if (freq >= 6500 && freq < 8000) { zone_high += vReal[i]; c_high++; }
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

  // Print formatting specifically for Arduino IDE Serial Plotter
  Serial.print("Door_60-600Hz:"); Serial.print(lowmid); Serial.print(",");
  Serial.print("Water_6500-8000Hz:"); Serial.print(high); Serial.print(",");
  Serial.print("AvgWaterIn5:"); Serial.println(actualAvgWater);

  // Send status update to server every 5 min
  unsigned long currentTime = millis();
  if (empty != wasEmpty || running != wasRunning) {
    sendStatusUpdate();
    lastSendTime = millis();
  }

  if (currentTime - lastSendTime >= sendInterval) {
    sendStatusUpdate();
    lastSendTime = currentTime;
  }
  
  delay(25); // Small delay to keep the Serial Plotter readable
}

void sendStatusUpdate() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    // Create JSON payload
    String jsonPayload = "{\"machineId\":\"" + String(machineId) +
                        "\",\"running\":" + (running ? "true" : "false") +
                        ",\"empty\":" + (empty ? "true" : "false") + "}";

    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0) {
      Serial.print("✅ Status sent: ");
      Serial.println(httpResponseCode);
      String response = http.getString();
      Serial.println(response);
    } else {
      Serial.print("❌ Error sending status: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  } else {
    Serial.println("❌ WiFi not connected");
  }
}