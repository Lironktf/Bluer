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

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "eepromHandler.h"

CONFIG_T _config;

/*
   Initialize config with hardcoded values
*/
void ConfigInitHardcoded(void)
{
  memset(&_config, 0, sizeof(CONFIG_T));
  
  strcpy(_config.magic, CONFIG_MAGIC);
  _config.version = CONFIG_VERSION;
  
  // WiFi - hardcoded
  strncpy(_config.wifi.ssid, WIFI_SSID, sizeof(_config.wifi.ssid) - 1);
  strncpy(_config.wifi.psk, WIFI_PASSWORD, sizeof(_config.wifi.psk) - 1);
  
  // Device
  strncpy(_config.device.name, DEVICE_NAME, sizeof(_config.device.name) - 1);
  
  // NTP (optional, for logging)
  strncpy(_config.ntp.server, "pool.ntp.org", sizeof(_config.ntp.server) - 1);
  _config.ntp.timezone = 0;
  
  // Bluetooth
  _config.bluetooth.scan_time = BT_SCAN_TIME;
  _config.bluetooth.pause_time = BT_PAUSE_TIME;
  _config.bluetooth.activescan_timeout = 300;
  _config.bluetooth.absence_cycles = BT_ABSENCE_CYCLES;
  _config.bluetooth.battcheck_timeout = 3600;
  
  LogMsg("CFG: Initialized with hardcoded values");
  LogMsg("CFG: WiFi SSID: %s", _config.wifi.ssid);
}

/*
    setup the configuration
*/
bool ConfigSetup(void)
{
  // Use hardcoded configuration instead of EEPROM
  ConfigInitHardcoded();

#if DBG_CFG
  dump("CFG:", &_config, sizeof(CONFIG_T));
#endif
  return true;
}

/*
   cyclic update of the configuration
*/
void ConfigUpdate(void)
{
  // nothin to do so far
}


/*
   functions to get the configuration for a subsystem
*/
void ConfigGet(int offset, int size, void *cfg)
{
#if DBG_CFG
  DbgMsg("CFG: getting config: offset:%d  size:%d  cfg:%p", offset, size, cfg);
#endif

  memcpy(cfg, (byte *) &_config + offset, size);

#if DBG_CFG
  dump("CFG:", cfg, size);
#endif
}

/*
   functions to set the configuration for a subsystem -- will be written to the EEPROM
*/
void ConfigSet(int offset, int size, void *cfg)
{
#if DBG_CFG
  DbgMsg("CFG: setting config: offset:%d  size:%d  cfg:%p", offset, size, cfg);
#endif

  memcpy((byte *) &_config + offset, cfg, size);

#if DBG_CFG
  dump("CFG:", cfg, size);
#endif

  EepromWrite(offset, size, (byte *) &_config + offset);
}/**/
