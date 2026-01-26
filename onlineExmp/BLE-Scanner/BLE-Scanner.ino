/*
  BLE-Scanner - Laundry Machine Monitor

  Scans for BLE advertisements from LaundryMachine devices and
  posts their status to a REST API.

  Based on BLE-Scanner (c) 2020 Christian.Lorenz@gromeck.de

  This file is part of BLE-Scanner.

  BLE-Scanner is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

*/

#include "config.h"
#include "led.h"
#include "wifiHandler.h"
#include "ntp.h"
#include "http.h"
#include "httpApi.h"
#include "state.h"
#include "util.h"
#include "bluetooth.h"
#include "scandev.h"
#include "watchdog.h"
#if defined(ESP32)
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#endif

void setup()
{
#if defined(ESP32)
  /*
     disable the brownout detector (BOD)
  */
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif

  /*
     setup serial communication
  */
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  LogMsg("**********************************************");
  LogMsg("*  Laundry Machine Scanner                   *");
  LogMsg("*  Version " GIT_VERSION "                          *");
  LogMsg("**********************************************");
  LogMsg("Target devices: %s with manufacturer ID 0x%04X", 
         TARGET_DEVICE_NAME, TARGET_MANUFACTURER_ID);
  LogMsg("API Endpoint: %s", API_ENDPOINT);

#if UNIT_TEST
  WatchdogUnitTest();
  LogMsg("End of UnitTest -- restarting");
  ESP.restart();
#endif

  /*
     initialize the basic sub-systems
  */
  WatchdogSetup(0);
  LedSetup(LED_MODE_ON);
  StateSetup(STATE_SCANNING);

  // Initialize config with hardcoded values
  ConfigSetup();

  // Connect to WiFi
  if (!WifiSetup()) {
    LogMsg("SETUP: WiFi connection failed!");
    LogMsg("SETUP: Check WIFI_SSID and WIFI_PASSWORD in config.h");
    LedSetup(LED_MODE_BLINK_FAST);
    // Keep trying to connect
    while (!WifiSetup()) {
      delay(5000);
      WatchdogUpdate();
    }
  }

  /*
     setup the other sub-systems
  */
  HttpSetup();
  ScanDevSetup();
  NtpSetup();
  HttpApiSetup();
  BluetoothSetup();
  WatchdogSetup(_config.bluetooth.scan_time);
  
  LogMsg("SETUP: All systems ready - starting BLE scanning");
}

void loop()
{
  /*
     do the cyclic updates of the sub systems
  */
  WatchdogUpdate();
  ConfigUpdate();
  LedUpdate();
  WifiUpdate();
  
  if (StateCheck(STATE_SCANNING) || StateCheck(STATE_PAUSING)) {
    /*
       in normal operation we work on all sub systems
    */
    HttpUpdate();
    NtpUpdate();
    HttpApiUpdate();
    BluetoothUpdate();
    ScanDevUpdate();
  }

  /*
     what to do?
  */
  switch (StateUpdate()) {
    case STATE_SCANNING:
      /*
         start the scanner
      */
      LogMsg("SCANNER: Starting BLE scan for %d seconds...", _config.bluetooth.scan_time);
      LedSetup(LED_MODE_BLINK_SLOW);
      BluetoothScanStart();
      break;
    case STATE_PAUSING:
      /*
         we are now pausing
      */
      LogMsg("SCANNER: Pausing for %d seconds (machines tracked: %d)", 
             _config.bluetooth.pause_time, ScanDevGetCount());
      LedSetup(LED_MODE_ON);
      BluetoothScanStop();
      break;
    case STATE_REBOOT:
      /*
         time to boot
      */
      LogMsg("SCANNER: Restarting the device");
      LedSetup(LED_MODE_OFF);
      ESP.restart();
      break;
  }
}
