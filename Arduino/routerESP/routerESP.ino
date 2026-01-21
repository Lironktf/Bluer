#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <map>
#include "../credentials.h"

// Server configuration
const char* serverUrl = "https://laun-dryer.vercel.app/api/machines";

// GTS Root R1 certificate (Google Trust Services - used by Vercel)
// Retrieved from: openssl s_client -showcerts -connect laun-dryer.vercel.app:443
const char* root_ca = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIFYjCCBEqgAwIBAgIQd70NbNs2+RrqIQ/E8FjTDTANBgkqhkiG9w0BAQsFADBX\n" \
"MQswCQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UE\n" \
"CxMHUm9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTIwMDYx\n" \
"OTAwMDA0MloXDTI4MDEyODAwMDA0MlowRzELMAkGA1UEBhMCVVMxIjAgBgNVBAoT\n" \
"GUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBMTEMxFDASBgNVBAMTC0dUUyBSb290IFIx\n" \
"MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAthECix7joXebO9y/lD63\n" \
"ladAPKH9gvl9MgaCcfb2jH/76Nu8ai6Xl6OMS/kr9rH5zoQdsfnFl97vufKj6bwS\n" \
"iV6nqlKr+CMny6SxnGPb15l+8Ape62im9MZaRw1NEDPjTrETo8gYbEvs/AmQ351k\n" \
"KSUjB6G00j0uYODP0gmHu81I8E3CwnqIiru6z1kZ1q+PsAewnjHxgsHA3y6mbWwZ\n" \
"DrXYfiYaRQM9sHmklCitD38m5agI/pboPGiUU+6DOogrFZYJsuB6jC511pzrp1Zk\n" \
"j5ZPaK49l8KEj8C8QMALXL32h7M1bKwYUH+E4EzNktMg6TO8UpmvMrUpsyUqtEj5\n" \
"cuHKZPfmghCN6J3Cioj6OGaK/GP5Afl4/Xtcd/p2h/rs37EOeZVXtL0m79YB0esW\n" \
"CruOC7XFxYpVq9Os6pFLKcwZpDIlTirxZUTQAs6qzkm06p98g7BAe+dDq6dso499\n" \
"iYH6TKX/1Y7DzkvgtdizjkXPdsDtQCv9Uw+wp9U7DbGKogPeMa3Md+pvez7W35Ei\n" \
"Eua++tgy/BBjFFFy3l3WFpO9KWgz7zpm7AeKJt8T11dleCfeXkkUAKIAf5qoIbap\n" \
"sZWwpbkNFhHax2xIPEDgfg1azVY80ZcFuctL7TlLnMQ/0lUTbiSw1nH69MG6zO0b\n" \
"9f6BQdgAmD06yK56mDcYBZUCAwEAAaOCATgwggE0MA4GA1UdDwEB/wQEAwIBhjAP\n" \
"BgNVHRMBAf8EBTADAQH/MB0GA1UdDgQWBBTkrysmcRorSCeFL1JmLO/wiRNxPjAf\n" \
"BgNVHSMEGDAWgBRge2YaRQ2XyolQL30EzTSo//z9SzBgBggrBgEFBQcBAQRUMFIw\n" \
"JQYIKwYBBQUHMAGGGWh0dHA6Ly9vY3NwLnBraS5nb29nL2dzcjEwKQYIKwYBBQUH\n" \
"MAKGHWh0dHA6Ly9wa2kuZ29vZy9nc3IxL2dzcjEuY3J0MDIGA1UdHwQrMCkwJ6Al\n" \
"oCOGIWh0dHA6Ly9jcmwucGtpLmdvb2cvZ3NyMS9nc3IxLmNybDA7BgNVHSAENDAy\n" \
"MAgGBmeBDAECATAIBgZngQwBAgIwDQYLKwYBBAHWeQIFAwIwDQYLKwYBBAHWeQIF\n" \
"AwMwDQYJKoZIhvcNAQELBQADggEBADSkHrEoo9C0dhemMXoh6dFSPsjbdBZBiLg9\n" \
"NR3t5P+T4Vxfq7vqfM/b5A3Ri1fyJm9bvhdGaJQ3b2t6yMAYN/olUazsaL+yyEn9\n" \
"WprKASOshIArAoyZl+tJaox118fessmXn1hIVw41oeQa1v1vg4Fv74zPl6/AhSrw\n" \
"9U5pCZEt4Wi4wStz6dTZ/CLANx8LZh1J7QJVj2fhMtfTJr9w4z30Z209fOU0iOMy\n" \
"+qduBmpvvYuR7hZL6Dupszfnw0Skfths18dG9ZKb59UhvmaSGZRVbNQpsg3BZlvi\n" \
"d0lIKO2d1xozclOzgjXPYovJJIultzkMu34qQb9Sz/yilrbCgj8=\n" \
"-----END CERTIFICATE-----\n";

// BLE scan configuration
#define SCAN_TIME 1 // Scan for 1 second at a time
BLEScan* pBLEScan;

// Structure to track each machine's state
struct MachineState {
  String machineId;
  bool running;
  bool empty;
  unsigned long lastSeen;
  unsigned long lastSent;
  bool stateChanged;
};

// Map to store all discovered machines (key: MAC address)
std::map<String, MachineState> machines;

// Heartbeat interval - send update even if no state change (30 seconds)
const unsigned long HEARTBEAT_INTERVAL = 30000;

// Timeout for considering a machine offline (60 seconds)
const unsigned long OFFLINE_TIMEOUT = 60000;

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
    newMachine.lastSent = 0; // Never sent
    newMachine.stateChanged = true; // Mark as changed to send initial state
    machines[macAddr] = newMachine;

    Serial.print("<� New machine discovered: ");
    Serial.println(machineId);
  } else {
    // Existing machine - check if state changed
    MachineState& machine = machines[macAddr];

    if (machine.running != running || machine.empty != empty) {
      // State changed!
      Serial.print("=State change detected for ");
      Serial.println(machineId);
      machine.stateChanged = true;
    }

    // Update state
    machine.running = running;
    machine.empty = empty;
    machine.lastSeen = now;
  }
}

// Callback class for BLE scan results
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Check if device has manufacturer data
    if (advertisedDevice.haveManufacturerData()) {
      String manufData = advertisedDevice.getManufacturerData();

      // Check if this is our laundry machine format (10 bytes, Company ID 0xFFFF)
      if (manufData.length() == 10 &&
          (uint8_t)manufData[0] == 0xFF &&
          (uint8_t)manufData[1] == 0xFF) {

        // Extract MAC address (bytes 2-7) as device identifier
        String macAddr = "";
        for(int i = 2; i < 8; i++) {
          char hex[3];
          sprintf(hex, "%02X", (uint8_t)manufData[i]);
          macAddr += hex;
          if(i < 7) macAddr += ":";
        }

        // Extract machine ID character (byte 8)
        char machineNum = manufData[8];

        // Extract status byte (byte 9)
        uint8_t status = (uint8_t)manufData[9];
        bool running = (status & 0x01) != 0;
        bool empty = (status & 0x02) != 0;

        // Build full machine ID (format: "a1-m1", "a1-m2", etc.)
        String machineId = "a1-m" + String(machineNum);

        // Update or create machine state
        updateMachineState(macAddr, machineId, running, empty);

        // Debug output
        Serial.print("=� BLE Device: ");
        Serial.print(macAddr);
        Serial.print(" | Machine: ");
        Serial.print(machineId);
        Serial.print(" | Running: ");
        Serial.print(running ? "YES" : "NO");
        Serial.print(" | Empty: ");
        Serial.println(empty ? "YES" : "NO");
      }
    }
  }
};

bool sendStatusUpdate(String machineId, bool running, bool empty) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected");
    return false;
  }

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  String jsonPayload = "{\"machineId\":\"" + machineId +
                       "\",\"running\":" + (running ? "true" : "false") +
                       ",\"empty\":" + (empty ? "true" : "false") + "}";

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0) {
    Serial.print("✅ Status sent: ");
    Serial.println(httpResponseCode);
    http.end();
    return true;
  } else {
    Serial.print("❌ HTTP failed: ");
    Serial.println(httpResponseCode);
    http.end();
    return false;
  }
}

void processMachineUpdates() {
  unsigned long now = millis();

  // Iterate through all tracked machines
  for (auto& pair : machines) {
    MachineState& machine = pair.second;

    // Calculate time since last send
    unsigned long timeSinceLastSend = (machine.lastSent == 0) ? HEARTBEAT_INTERVAL : (now - machine.lastSent);

    // Send if: state changed OR heartbeat interval reached
    bool shouldSend = machine.stateChanged || (timeSinceLastSend >= HEARTBEAT_INTERVAL);

    // Also check if machine is still online (seen recently)
    unsigned long timeSinceLastSeen = now - machine.lastSeen;
    bool isOnline = timeSinceLastSeen < OFFLINE_TIMEOUT;

    if (shouldSend && isOnline) {
      bool success = sendStatusUpdate(machine.machineId, machine.running, machine.empty);

      if (success) {
        machine.lastSent = now;
        machine.stateChanged = false;
      }
    }

    // warn if machine went offline
    if (!isOnline && timeSinceLastSeen < (OFFLINE_TIMEOUT + 5000)) {
      // only print once when it frst goes offline
      Serial.print("� Machine ");
      Serial.print(machine.machineId);
      Serial.println(" appears offline (no BLE signal)");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial time to initialize

  // Print WiFi diagnostics
  Serial.println("\n\n=== WiFi Diagnostics ===");
  Serial.print("Connecting to SSID: ");
  Serial.println(ssid);

  // Scan for available networks first
  Serial.println("Scanning for networks...");
  int n = WiFi.scanNetworks();
  Serial.print("Found ");
  Serial.print(n);
  Serial.println(" networks:");
  for (int i = 0; i < n; i++) {
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (");
    Serial.print(WiFi.RSSI(i));
    Serial.println(" dBm)");
  }
  Serial.println();

  // Set WiFi mode explicitly
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Connect to WiFi with timeout
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  int attempts = 0;
  const int maxAttempts = 40; // 20 seconds timeout (40 * 500ms)

  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempts++;

    // Print status every 10 attempts
    if (attempts % 10 == 0) {
      Serial.print("\nAttempt ");
      Serial.print(attempts);
      Serial.print("/");
      Serial.print(maxAttempts);
      Serial.print(" - Status: ");
      Serial.println(WiFi.status());
    }
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("❌ WiFi Connection Failed!");
    Serial.print("Final Status Code: ");
    Serial.println(WiFi.status());
    Serial.println("\nStatus codes:");
    Serial.println("0 = WL_IDLE_STATUS");
    Serial.println("1 = WL_NO_SSID_AVAIL (Network not found)");
    Serial.println("2 = WL_SCAN_COMPLETED");
    Serial.println("3 = WL_CONNECTED");
    Serial.println("4 = WL_CONNECT_FAILED (Wrong password)");
    Serial.println("5 = WL_CONNECTION_LOST");
    Serial.println("6 = WL_DISCONNECTED");
    Serial.println("\n⚠️  Continuing without WiFi - updates will not be sent");
  }

  // Initialize BLE
  Serial.println("\nInitializing BLE Scanner...");
  BLEDevice::init("LaundryGateway");

  // Create BLE scanner
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(false); 
  pBLEScan->setInterval(1349); // Less aggressive to allow WiFi time
  pBLEScan->setWindow(449);  // Less or equal to setInterval value

  Serial.println("BLE Scanner ready!");
  Serial.println("=\n Scanning for laundry machines...");
}

void loop() {
  // === BLE Phase: Scan for devices ===
  pBLEScan->start(SCAN_TIME, false);
  pBLEScan->clearResults();

  // === Transition: Stop BLE completely before WiFi ===
  pBLEScan->stop();

  // Critical: Give the radio enough time to fully switch from BLE to WiFi mode
  // The ESP32 has a single 2.4GHz radio that must be time-shared
  delay(2000);

  // Verify WiFi is still connected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️  WiFi disconnected, attempting reconnect...");
    WiFi.reconnect();
    delay(2000); // Wait for reconnection attempt
  }

  // === WiFi Phase: Send HTTP updates ===
  // Now WiFi has full access to the radio
  processMachineUpdates();

  // Longer delay before next BLE scan to give WiFi operations time to complete
  delay(7000);
}