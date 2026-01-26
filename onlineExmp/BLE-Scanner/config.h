/*
  BLE-Scanner

  (c) 2020 Christian.Lorenz@gromeck.de

  module to handle the configuration


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

#ifndef __CONFIG_H__
#define __CONFIG_H__ 1

#if __has_include("git-version.h")
#include "git-version.h"
#else
#define GIT_VERSION		"unknown"
#endif

#define __TITLE__   "BLE-Scanner"

/*
   control the debugging messages
*/
#define DBG               0
#define DBG_BT            (DBG && 1)
#define DBG_CFG           (DBG && 0)
#define DBG_HTTP          (DBG && 0)
#define DBG_LED           (DBG && 0)
#define DBG_NTP           (DBG && 0)
#define DBG_SCANDEV       (DBG && 0)
#define DBG_STATE         (DBG && 0)
#define DBG_UTIL          (DBG && 0)
#define DBG_WIFI          (DBG && 0)
#define DBG_MQTT          (DBG && 1)


/*
  tags to mark the configuration in the EEPROM
*/
#define CONFIG_MAGIC      __TITLE__ "-CONFIG"
#define CONFIG_VERSION    5

/*
   ============================================
   HARDCODED CONFIGURATION - EDIT THESE VALUES
   ============================================
*/

// WiFi credentials - Load from credentials.h (not committed to git)
#if __has_include("credentials.h")
  #include "credentials.h"
#else
  #error "credentials.h not found! Copy credentials.h.example to credentials.h and fill in your WiFi credentials."
#endif

// MQTT Broker settings
// Option 1: HiveMQ Cloud (free tier) - requires TLS
// Option 2: test.mosquitto.org (public broker, no auth, no TLS)
// Option 3: Your own Mosquitto broker

// === EDIT THESE MQTT SETTINGS ===
#define MQTT_BROKER        "test.mosquitto.org"  // Change to your broker
#define MQTT_PORT          1883                   // 1883 for non-TLS, 8883 for TLS
#define MQTT_USE_AUTH      0                      // Set to 1 if using authentication
#define MQTT_USER          ""                     // MQTT username (if auth enabled)
#define MQTT_PASSWORD      ""                     // MQTT password (if auth enabled)

// Device name
#define DEVICE_NAME        "LaundryScanner"

// Bluetooth scanning settings
#define BT_SCAN_TIME       10    // seconds - duration of BLE scan
#define BT_PAUSE_TIME      20    // seconds - pause between scans
#define BT_ABSENCE_CYCLES  3     // cycles before marking device absent

// Target device settings
#define TARGET_DEVICE_NAME    "LaundryMachine"
#define TARGET_MANUFACTURER_ID 0xFFFF
#define MACHINE_ID_MAX_LEN    16

/*
   sub-systems config structs (simplified)
*/
typedef struct _config_wifi {
  char ssid[64];
  char psk[64];
} CONFIG_WIFI_T;

typedef struct _config_device {
  char name[64];
  char password[64];
  char reserved[64];
} CONFIG_DEVICE_T;

typedef struct _config_ntp {
  char server[64];
  int timezone;
} CONFIG_NTP_T;

typedef struct _config_bluetooth {
  unsigned long scan_time;          // duration of the BLE scan in seconds
  unsigned long pause_time;         // pause time after scans before restarting the scans
  unsigned long activescan_timeout; // don't report a device too often
  int absence_cycles;               // number of complete cycles before a device is set absent
  unsigned long battcheck_timeout;  // don't check the device battery too often
  char reserved[70];
} CONFIG_BT_T;

/*
   the configuration layout
*/
typedef struct _config {
  char magic[sizeof(CONFIG_MAGIC) + 1];
  int version;
  CONFIG_WIFI_T wifi;
  CONFIG_DEVICE_T device;
  CONFIG_NTP_T ntp;
  CONFIG_BT_T bluetooth;
} CONFIG_T;

/*
   the config is global
*/
extern CONFIG_T _config;

/*
    setup the configuration
*/
bool ConfigSetup(void);

/*
   cyclic update of the configuration
*/
void ConfigUpdate(void);

/*
   functions to get the configuration for a subsystem
*/
#define CONFIG_GET(type,name,cfg)  ConfigGet(offsetof(CONFIG_T,name),sizeof(CONFIG_ ## type ## _T),(void *) (cfg))
void ConfigGet(int offset, int size, void *cfg);

/*
   functions to set the configuration for a subsystem -- will be written to the EEPROM
*/
#define CONFIG_SET(type,name,cfg)  ConfigSet(offsetof(CONFIG_T,name),sizeof(type),(void *) (cfg))
void ConfigSet(int offset, int size, void *cfg);

/*
   Initialize config with hardcoded values
*/
void ConfigInitHardcoded(void);

#endif
