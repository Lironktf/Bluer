#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <HTTPClient.h>
//#include <credentials.h>
#include <map>

const char* ssid = "Jeff 5G Home Network";
const char* password = "Brodie1998";

// Server configuration
const char* serverUrl = "https://laun-dryer.vercel.app/api/machines";

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

// Callback class for BLE scan results
void updateMachineState(String macAddr, String machineId, bool running, bool empty);
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
        Serial.print("=ñ BLE Device: ");
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

    Serial.print("<• New machine discovered: ");
    Serial.println(machineId);
  } else {
    // Existing machine - check if state changed
    MachineState& machine = machines[macAddr];

    if (machine.running != running || machine.empty != empty) {
      // State changed!
      Serial.print("= State change detected for ");
      Serial.println(machineId);
      machine.stateChanged = true;
    }

    // Update state
    machine.running = running;
    machine.empty = empty;
    machine.lastSeen = now;
  }
}

void sendStatusUpdate(String machineId, bool running, bool empty) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    // Create JSON payload (same format as original WiFi version)
    /*String jsonPayload = "{\"machineId\":\"" + machineId +
                        "\",\"running\":" + (running ? "true" : "false") +
                        ",\"empty\":" + (empty ? "true" : "false") + "}";*/
    char jsonPayload[100];
sprintf(jsonPayload, "{\"machineId\":\"%s\",\"running\":%s,\"empty\":%s}",
        machineId, running ? "true" : "false", empty ? "true" : "false");

    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0) {
      Serial.print(" Status sent for ");
      Serial.print(machineId);
      Serial.print(": ");
      Serial.println(httpResponseCode);
      String response = http.getString();
      Serial.println(response);
    } else {
      Serial.print("L Error sending status for ");
      Serial.print(machineId);
      Serial.print(": ");
      Serial.println(httpResponseCode);
    }

    http.end();
  } else {
    Serial.println("L WiFi not connected");
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
      sendStatusUpdate(machine.machineId, machine.running, machine.empty);
      machine.lastSent = now;
      machine.stateChanged = false;
    }

    // Warn if machine went offline
    if (!isOnline && timeSinceLastSeen < (OFFLINE_TIMEOUT + 5000)) {
      // Only print once when it first goes offline
      Serial.print("  Machine ");
      Serial.print(machine.machineId);
      Serial.println(" appears offline (no BLE signal)");
    }
  }
}

void setup() {
  Serial.begin(38400);

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n WiFi Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nL WiFi Connection Failed!");
  }

  // Initialize BLE
  Serial.println("Initializing BLE Scanner...");
  BLEDevice::init("LaundryGateway");

  // Create BLE scanner
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); // Active scan uses more power but gets more data
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // Less or equal to setInterval value

  Serial.println(" BLE Scanner ready!");
  Serial.println("= Scanning for laundry machines...");
}

void loop() {
  // Start BLE scan
  pBLEScan->start(SCAN_TIME, false);

  // Clear results to free memory
  pBLEScan->clearResults();

  // Process any pending updates (state changes or heartbeats)
  processMachineUpdates();

  // Check WiFi connection and reconnect if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(" WiFi disconnected, attempting reconnect...");
    WiFi.reconnect();
  }

  // Small delay before next scan
  delay(100);
}
