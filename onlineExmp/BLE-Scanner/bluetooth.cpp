/*
  BLE-Scanner

  (c) 2020 Christian.Lorenz@gromeck.de

  module to handle the BLE scan


  This file is part of BLE-Scanner.

  BLE-Scanner is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  BLE-Scanner is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with BLE-Scanner.  If not, see <https://www.gnu.org/licenses/>.

*/

#include "config.h"
#include "state.h"
#include "bluetooth.h"
#include "scandev.h"
#include "util.h"

static NimBLEScan *_scan = NULL;
static time_t _last_scan = 0;
static time_t _last_activescan = 0;

class BLEScannerScanCallbacks : public NimBLEScanCallbacks
{
    void onResult(const BLEAdvertisedDevice* advertisedDevice)
    {
      // Get device name
      std::string deviceName = advertisedDevice->getName();
      
#if DBG_BT
      DbgMsg("BLE: found device: %s name: '%s' address type: 0x%02x", 
             advertisedDevice->getAddress().toString().c_str(), 
             deviceName.c_str(),
             advertisedDevice->getAddressType());
#endif

      // Filter 1: Check if device name matches TARGET_DEVICE_NAME
      if (deviceName != TARGET_DEVICE_NAME) {
#if DBG_BT
        DbgMsg("BLE: Skipping - name doesn't match '%s'", TARGET_DEVICE_NAME);
#endif
        return;
      }

      // Filter 2: Check for manufacturer data
      if (!advertisedDevice->haveManufacturerData()) {
#if DBG_BT
        DbgMsg("BLE: Skipping - no manufacturer data");
#endif
        return;
      }

      // Get manufacturer data
      std::string manufData = advertisedDevice->getManufacturerData();
      
      // Need at least: 2 (company ID) + 1 (machineId) + 1 (status) = 4 bytes minimum
      // Format: 2 + MACHINE_ID_MAX_LEN + 1 = 19 bytes
      if (manufData.length() < 4) {
#if DBG_BT
        DbgMsg("BLE: Skipping - manufacturer data too short (%d bytes)", manufData.length());
#endif
        return;
      }

      // Filter 3: Check manufacturer ID (0xFFFF)
      uint16_t manufacturer_id = (uint8_t)manufData[0] | ((uint8_t)manufData[1] << 8);
      if (manufacturer_id != TARGET_MANUFACTURER_ID) {
#if DBG_BT
        DbgMsg("BLE: Skipping - manufacturer ID 0x%04X doesn't match 0x%04X", 
               manufacturer_id, TARGET_MANUFACTURER_ID);
#endif
        return;
      }

      // Parse machine ID (starts at byte 2, up to MACHINE_ID_MAX_LEN bytes, null-terminated)
      char machineId[MACHINE_ID_MAX_LEN + 1];
      memset(machineId, 0, sizeof(machineId));
      
      int idLen = manufData.length() - 3; // -2 for company ID, -1 for status byte
      if (idLen > MACHINE_ID_MAX_LEN) idLen = MACHINE_ID_MAX_LEN;
      
      for (int i = 0; i < idLen && manufData[2 + i] != '\0'; i++) {
        machineId[i] = manufData[2 + i];
      }

      // Parse status byte (last byte)
      uint8_t statusByte = (uint8_t)manufData[manufData.length() - 1];
      bool running = (statusByte & 0x01) != 0;
      bool empty = (statusByte & 0x02) != 0;

      LogMsg("BLE: Found LaundryMachine! ID: %s, Running: %s, Empty: %s, RSSI: %d",
             machineId,
             running ? "YES" : "NO",
             empty ? "YES" : "NO",
             advertisedDevice->getRSSI());

      // Add to device list for tracking and API posting
      // Room mapping is done on backend based on machineId prefix
      ScanDevAddMachine(advertisedDevice->getAddress(),
                        machineId,
                        NULL, // No room in BLE - backend will map it
                        running,
                        empty,
                        advertisedDevice->getRSSI());
    }
};

/*
   setup
*/
void BluetoothSetup(void)
{
  /*
     check and correct the config
  */
  FIX_RANGE(_config.bluetooth.scan_time, BLUETOOTH_SCAN_TIME_MIN, BLUETOOTH_SCAN_TIME_MAX);
  FIX_RANGE(_config.bluetooth.pause_time, BLUETOOTH_PAUSE_TIME_MIN, BLUETOOTH_PAUSE_TIME_MAX);
  FIX_RANGE(_config.bluetooth.activescan_timeout, BLUETOOTH_ACTIVESCAN_TIMEOUT_MIN, BLUETOOTH_ACTIVESCAN_TIMEOUT_MAX);
  FIX_RANGE(_config.bluetooth.absence_cycles, BLUETOOTH_ABSENCE_CYCLES_MIN, BLUETOOTH_ABSENCE_CYCLES_MAX);

  /*
     set the timeout values in the status table
  */
  LogMsg("BLE: setting up timout values in the status table");
  StateModifyTimeout(STATE_SCANNING, (_config.bluetooth.scan_time + 5) * 1000);
  StateModifyTimeout(STATE_PAUSING, _config.bluetooth.pause_time * 1000);

#if DBG_BT
  DbgMsg("BLE: init ...");
#endif

  NimBLEDevice::setScanFilterMode(CONFIG_BTDM_SCAN_DUPL_TYPE_DEVICE);
  NimBLEDevice::setScanDuplicateCacheSize(200);

  /*
     init the device
  */
  if (!NimBLEDevice::isInitialized())
    NimBLEDevice::init(__TITLE__);

  /*
     create a scan
  */
#if DBG_BT
  DbgMsg("BLE: create a scan ...");
#endif
  if (!_scan && !(_scan = NimBLEDevice::getScan())) {
    LogMsg("BLE: NimBLEDevice::getScan() failed");
  }
  _scan->setScanCallbacks(new BLEScannerScanCallbacks(), false);
}

/*
   cyclic call
*/
void BluetoothUpdate(void)
{
}

/*
   init the scanning
*/
bool BluetoothScanStart(void)
{
#if DBG_BT
  DbgMsg("BLE: BluetoothScanStart");
#endif

  /*
    active/passive scan
  */
  bool active = false;

  if (now() - _last_activescan > _config.bluetooth.activescan_timeout) {
    active = true;
    _last_activescan = now();
  }
  _scan->setActiveScan(active);

  int scan_interval = 3000; // ms
  _scan->setInterval(scan_interval);
  _scan->setWindow(scan_interval - 1);

  /*
     start the scan
  */
#if DBG_BT
  DbgMsg("BLE: start %s scan for %d seconds ...", (active) ? "active" : "passive", _config.bluetooth.scan_time);
#endif
  _scan->start(_config.bluetooth.scan_time * 1000, false);
  _last_scan = now();

  return true;
}

/*
   stop the current scan
*/
bool BluetoothScanStop(void)
{
#if DBG_BT
  DbgMsg("BLE: BluetoothScanStop");
#endif
  _scan->stop();
  _scan->clearResults();

  return true;
}

/*
   callback class to connect to the device
*/
class BLEScannerClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient *client) {
    }
    void onDisconnect(NimBLEClient* pclient, int reason) {
    }
};

/*
   get the charactersitics from the battery service
*/
bool BluetoothBatteryCheck(BLEAddress device, uint8_t *battery_level)
{
#if DBG_BT
  DbgMsg("BLE: create a client ...");
#endif
  NimBLEClient *client = NimBLEDevice::createClient();

#if DBG_BT
  DbgMsg("BLE: connect device %s ...", device.toString().c_str());
#endif
  client->setClientCallbacks(new BLEScannerClientCallbacks());
  if (!client->connect(device)) {
    LogMsg("BLE: couldn't connect to client device %s to read battery level", device.toString().c_str());
    return false;
  }

  /*
     select the service
  */
#if DBG_BT
  DbgMsg("BLE: create remote service for battery service ...");
#endif
  NimBLERemoteService *service = client->getService(BLEBatteryService);

  if (!service) {
    LogMsg("BLE: couldn't create service for client device %s to read battery level", device.toString().c_str());
    client->disconnect();
    return false;
  }

  /*
     get the characteristic
  */
#if DBG_BT
  DbgMsg("BLE: get characteristics ...");
#endif
  NimBLERemoteCharacteristic *characteristic = service->getCharacteristic(BLEBatteryCharacteristics);

  if (!characteristic) {
    LogMsg("BLE: couldn't create characteristics for client device %s to read battery level", device.toString().c_str());
    client->disconnect();
    return false;
  }
  if (characteristic->canRead() && battery_level) {
#if DBG_BT
    DbgMsg("BLE: reading characteristic");
#endif
    *battery_level = characteristic->readValue<uint8_t>();
#if DBG_BT
    DbgMsg("BLE: characteristic value=%u", *battery_level);
#endif
  }

#if DBG_BT
  DbgMsg("BLE: disconnecting device");
#endif
  client->disconnect();

  return true;
}/**/
