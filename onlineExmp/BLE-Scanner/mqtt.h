/*
  BLE-Scanner - Laundry Machine Monitor

  MQTT client for publishing machine status

  This file is part of BLE-Scanner.

  BLE-Scanner is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

*/

#ifndef __MQTT_H__
#define __MQTT_H__ 1

#include "config.h"

/*
   Initialize the MQTT client
*/
void MqttSetup(void);

/*
   Cyclic update - maintains connection
*/
void MqttUpdate(void);

/*
   Publish machine status to MQTT broker
   Returns true if successful
*/
bool MqttPublishMachineStatus(const char* machineId, const char* roomName, bool running, bool empty);

/*
   Check if MQTT is connected
*/
bool MqttIsConnected(void);

/*
   Get MQTT connection status as string
*/
const char* MqttGetStatusString(void);

#endif
