/*
 * router2.ino - BLE Scanner for Laundry Machines
 *
 * This router scans for BLE advertisements from laundry machines and reports
 * their status to the cloud API. Uses NimBLE library for better WiFi/BLE coexistence.
 *
 * Based on: https://github.com/gromeck/BLE-Scanner
 * Hardware: ESP32
 * Libraries required: NimBLE-Arduino, WiFi, HTTPClient
 */

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <map>
#include "../credentials.h"

// ========== CONFIGURATION ==========
const char* WIFI_SSID = ssid;
const char* WIFI_PASSWORD = password;
const char* API_ENDPOINT = "https://laun-dryer.vercel.app/api/machines";

// BLE scan configuration
#define SCAN_INTERVAL_MS 5000  // Scan every 5 seconds
#define SCAN_DURATION_SEC 3    // Scan for 3 seconds at a time

// Machine tracking
#define HEARTBEAT_INTERVAL_MS 30000  // Send update every 30s even if no change
#define OFFLINE_TIMEOUT_MS 60000     // Consider machine offline after 60s

// ========== DATA STRUCTURES ==========
struct MachineState {
  String machineId;
  bool running;
  bool empty;
  unsigned long lastSeen;
  unsigned long lastSent;
  bool stateChanged;
  bool offline;
};

// Map to store all discovered machines (key: MAC address)
std::map<String, MachineState> machines;

// ========== BLE SCANNER ==========
NimBLEScan* pBLEScan;

// ========== MACHINE STATE MANAGEMENT ==========
void updateMachineState(String macAddr, String machineId, bool running, bool empty) {
  unsigned long now = millis();

  // Check if this machine exists in our map
  if (machines.find(macAddr) == machines.end()) {
    // New machine discovered
    MachineState newMachine;
    newMachine.machineId = machineId;
    newMachine.running = running;
    newMachine.empty = empty;
    newMachine.lastSeen = now;
    newMachine.lastSent = 0;
    newMachine.stateChanged = true;
    newMachine.offline = false;
    machines[macAddr] = newMachine;

    Serial.printf("[NEW] Machine discovered: %s (%s)\n", machineId.c_str(), macAddr.c_str());
  } else {
    // Existing machine - check if state changed
    MachineState& machine = machines[macAddr];

    // Check for state changes
    if (machine.running != running || machine.empty != empty) {
      Serial.printf("[STATE] Change detected for %s: Running %d->%d, Empty %d->%d\n",
                   machineId.c_str(), machine.running, running, machine.empty, empty);
      machine.stateChanged = true;
    }

    // If machine was offline and now seen, mark as online
    if (machine.offline) {
      Serial.printf("[ONLINE] Machine %s is back online\n", machineId.c_str());
      machine.offline = false;
      machine.stateChanged = true;
    }

    // Update state
    machine.running = running;
    machine.empty = empty;
    machine.lastSeen = now;
  }
}

// ========== HTTP API COMMUNICATION ==========
bool sendStatusUpdate(String machineId, bool running, bool empty) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] ERROR: WiFi not connected");
    return false;
  }

  HTTPClient http;
  http.begin(API_ENDPOINT);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000); // 5 second timeout

  // Build JSON payload
  String jsonPayload = "{\"machineId\":\"" + machineId +
                       "\",\"running\":" + (running ? "true" : "false") +
                       ",\"empty\":" + (empty ? "true" : "false") + "}";

  Serial.printf("[HTTP] POST to %s\n", API_ENDPOINT);
  Serial.printf("[HTTP] Payload: %s\n", jsonPayload.c_str());

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("[HTTP] SUCCESS: Response code %d\n", httpResponseCode);
    Serial.printf("[HTTP] Response: %s\n", response.c_str());
    http.end();
    return true;
  } else {
    Serial.printf("[HTTP] FAILED: Error code %d (%s)\n",
                 httpResponseCode,
                 http.errorToString(httpResponseCode).c_str());
    http.end();
    return false;
  }
}

void processMachineUpdates() {
  unsigned long now = millis();

  // Iterate through all tracked machines
  for (auto& pair : machines) {
    String macAddr = pair.first.c_str();
    MachineState& machine = pair.second;

    // Check if machine went offline
    unsigned long timeSinceLastSeen = now - machine.lastSeen;
    if (!machine.offline && timeSinceLastSeen > OFFLINE_TIMEOUT_MS) {
      machine.offline = true;
      Serial.printf("[OFFLINE] Machine %s not seen for %lu ms\n",
                   machine.machineId.c_str(),
                   timeSinceLastSeen);
      // Could optionally send an offline notification to API here
      continue; // Don't send updates for offline machines
    }

    // Calculate time since last send
    unsigned long timeSinceLastSend = (machine.lastSent == 0)
                                      ? HEARTBEAT_INTERVAL_MS
                                      : (now - machine.lastSent);

    // Send if: state changed OR heartbeat interval reached (and machine is online)
    bool shouldSend = machine.stateChanged || (timeSinceLastSend >= HEARTBEAT_INTERVAL_MS);

    if (shouldSend && !machine.offline) {
      bool success = sendStatusUpdate(machine.machineId, machine.running, machine.empty);

      if (success) {
        machine.lastSent = now;
        machine.stateChanged = false;
      } else {
        Serial.printf("[ERROR] Failed to send update for %s\n", machine.machineId.c_str());
      }
    }
  }
}

// ========== WIFI MANAGEMENT ==========
void setupWiFi() {
  Serial.println("\n========== WiFi Setup ==========");
  Serial.printf("Connecting to: %s\n", WIFI_SSID);

  // Set WiFi mode
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  const int maxAttempts = 40; // 20 seconds

  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempts++;

    if (attempts % 10 == 0) {
      Serial.printf("\n[WiFi] Attempt %d/%d - Status: %d\n",
                   attempts, maxAttempts, WiFi.status());
    }
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] CONNECTED!");
    Serial.printf("[WiFi] IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] Signal: %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("[WiFi] FAILED to connect!");
    Serial.printf("[WiFi] Status code: %d\n", WiFi.status());
    Serial.println("[WiFi] Continuing anyway - updates will fail until connected");
  }
}

// ========== ARDUINO SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n");
  Serial.println("========================================");
  Serial.println("   Laundry Machine BLE Router v2.0");
  Serial.println("   Using NimBLE for Better Performance");
  Serial.println("========================================\n");

  // Initialize WiFi first
  setupWiFi();

  // Initialize NimBLE
  Serial.println("\n========== BLE Setup ==========");
  Serial.println("[BLE] Initializing NimBLE...");
  NimBLEDevice::init("LaundryRouter");

  // Create scanner
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setActiveScan(true);  // Active scanning to detect all devices
  pBLEScan->setInterval(1349);    // Match old router settings (in 0.625ms units)
  pBLEScan->setWindow(449);       // Match old router settings
  pBLEScan->setDuplicateFilter(false); // Don't filter duplicates - we want all advertisements

  Serial.println("[BLE] Scanner initialized!");
  Serial.println("[BLE] Looking for laundry machines...\n");

  Serial.println("========================================");
  Serial.println("System ready! Starting main loop...");
  Serial.println("========================================\n");
}

// ========== ARDUINO MAIN LOOP ==========
unsigned long lastScanTime = 0;

void loop() {
  unsigned long currentTime = millis();

  // Perform BLE scan every SCAN_INTERVAL_MS
  if (currentTime - lastScanTime >= SCAN_INTERVAL_MS) {
    lastScanTime = currentTime;

    Serial.printf("\n[SCAN] Starting BLE scan (duration: %ds)...\n", SCAN_DURATION_SEC);

    // Scan for devices (blocking call) - returns true on success
    if (pBLEScan->start(SCAN_DURATION_SEC, false)) {
      // Get the scan results
      NimBLEScanResults results = pBLEScan->getResults();
      int count = results.getCount();

      Serial.printf("[SCAN] Scan complete. Found %d total BLE devices\n", count);

      // Process each discovered device
      for (int i = 0; i < count; i++) {
        const NimBLEAdvertisedDevice* device = results.getDevice(i);

        // Debug: Print ALL devices found
        Serial.printf("[DEBUG] Device %d: Name='%s', Address=%s, RSSI=%d, HasManufData=%d\n",
                     i,
                     device->getName().c_str(),
                     device->getAddress().toString().c_str(),
                     device->getRSSI(),
                     device->haveManufacturerData());

        // Check if device has manufacturer data
        if (device->haveManufacturerData()) {
          std::string manufData = device->getManufacturerData();

          // Debug: Print manufacturer data length and first few bytes
          Serial.printf("[DEBUG] ManufData length=%d, bytes: ", manufData.length());
          for (size_t j = 0; j < manufData.length() && j < 10; j++) {
            Serial.printf("%02X ", (uint8_t)manufData[j]);
          }
          Serial.println();

          // Check if this is our laundry machine format (10 bytes, Company ID 0xFFFF)
          if (manufData.length() == 10 &&
              (uint8_t)manufData[0] == 0xFF &&
              (uint8_t)manufData[1] == 0xFF) {

            // Extract MAC address (bytes 2-7)
            String macAddr = "";
            for(int j = 2; j < 8; j++) {
              char hex[3];
              sprintf(hex, "%02X", (uint8_t)manufData[j]);
              macAddr += hex;
              if(j < 7) macAddr += ":";
            }

            // Extract machine ID character (byte 8)
            char machineNum = manufData[8];

            // Extract status byte (byte 9)
            uint8_t status = (uint8_t)manufData[9];
            bool running = (status & 0x01) != 0;
            bool empty = (status & 0x02) != 0;

            // Build full machine ID (format: "a1-m1", "a1-m2", etc.)
            String machineId = "a1-m" + String(machineNum);

            // Update machine state
            updateMachineState(macAddr, machineId, running, empty);

            // Debug output
            Serial.printf("[BLE] Device: %s | Machine: %s | Running: %s | Empty: %s | RSSI: %d\n",
                         macAddr.c_str(),
                         machineId.c_str(),
                         running ? "YES" : "NO",
                         empty ? "YES" : "NO",
                         device->getRSSI());
          }
        }
      }

      // Clear results to free memory
      pBLEScan->clearResults();
    } else {
      Serial.println("[SCAN] Scan failed!");
    }
  }

  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected! Attempting reconnect...");
    WiFi.reconnect();
    delay(1000);
  }

  // Process machine updates and send to API
  processMachineUpdates();

  // Small delay to prevent tight loop
  delay(100);
}
