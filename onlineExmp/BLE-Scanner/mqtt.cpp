/*
  BLE-Scanner - Laundry Machine Monitor

  MQTT client for publishing machine status

  This file is part of BLE-Scanner.

  BLE-Scanner is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

*/

#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"
#include "mqtt.h"
#include "util.h"
#include "state.h"

// WiFi client for MQTT (non-secure for lightweight operation)
static WiFiClient _mqttWifiClient;

// MQTT client
static PubSubClient _mqttClient(_mqttWifiClient);

// Connection state
static bool _connected = false;
static unsigned long _lastConnectAttempt = 0;
static unsigned long _lastPublish = 0;

// Reconnect interval (ms)
#define MQTT_RECONNECT_INTERVAL 5000

// Topic prefix
#define MQTT_TOPIC_PREFIX "laundry/machines/"

/*
   MQTT callback (not used for this publish-only client)
*/
static void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  // We don't subscribe to anything, but callback is required
  LogMsg("MQTT: Received message on topic %s", topic);
}

/*
   Connect to MQTT broker
*/
static bool mqttConnect()
{
  if (_mqttClient.connected()) {
    return true;
  }

  unsigned long now = millis();
  if (now - _lastConnectAttempt < MQTT_RECONNECT_INTERVAL) {
    return false;
  }
  _lastConnectAttempt = now;

  LogMsg("MQTT: Connecting to %s:%d...", MQTT_BROKER, MQTT_PORT);

  // Generate client ID from device name and chip ID
  String clientId = String(DEVICE_NAME) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  bool success = false;
  
#if MQTT_USE_AUTH
  LogMsg("MQTT: Using authentication (user: %s)", MQTT_USER);
  success = _mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD);
#else
  LogMsg("MQTT: Connecting without authentication");
  success = _mqttClient.connect(clientId.c_str());
#endif

  if (success) {
    _connected = true;
    LogMsg("MQTT: Connected successfully as %s", clientId.c_str());
    return true;
  } else {
    _connected = false;
    int state = _mqttClient.state();
    LogMsg("MQTT: Connection failed, state=%d", state);
    return false;
  }
}

/*
   Initialize the MQTT client
*/
void MqttSetup(void)
{
  LogMsg("MQTT: Setting up MQTT client");
  LogMsg("MQTT: Broker: %s:%d", MQTT_BROKER, MQTT_PORT);
  LogMsg("MQTT: Topic prefix: %s", MQTT_TOPIC_PREFIX);

  _mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  _mqttClient.setCallback(mqttCallback);
  _mqttClient.setKeepAlive(60);
  _mqttClient.setSocketTimeout(10);

  // Try initial connection
  mqttConnect();
}

/*
   Cyclic update - maintains connection
*/
void MqttUpdate(void)
{
  if (StateCheck(STATE_CONFIGURING))
    return;

  // Maintain connection
  if (!_mqttClient.connected()) {
    _connected = false;
    mqttConnect();
  }

  // Process any incoming messages (even though we don't subscribe)
  _mqttClient.loop();
}

/*
   Publish machine status to MQTT broker
*/
bool MqttPublishMachineStatus(const char* machineId, const char* roomName, bool running, bool empty)
{
  if (!_mqttClient.connected()) {
    LogMsg("MQTT: Not connected, cannot publish");
    if (!mqttConnect()) {
      return false;
    }
  }

  // Build topic: laundry/machines/{machineId}/status
  String topic = String(MQTT_TOPIC_PREFIX) + machineId + "/status";

  // Build JSON payload
  String payload = "{";
  payload += "\"machineId\":\"";
  payload += machineId;
  if (roomName && strlen(roomName) > 0) {
    payload += "\",\"room\":\"";
    payload += roomName;
  }
  payload += "\",\"running\":";
  payload += running ? "true" : "false";
  payload += ",\"empty\":";
  payload += empty ? "true" : "false";
  payload += ",\"timestamp\":";
  payload += String(millis());
  payload += "}";

  LogMsg("MQTT: Publishing to %s", topic.c_str());
  LogMsg("MQTT: Payload: %s", payload.c_str());

  bool success = _mqttClient.publish(topic.c_str(), payload.c_str(), false);

  if (success) {
    _lastPublish = millis();
    LogMsg("MQTT: Published successfully");
  } else {
    LogMsg("MQTT: Publish failed");
  }

  return success;
}

/*
   Check if MQTT is connected
*/
bool MqttIsConnected(void)
{
  return _mqttClient.connected();
}

/*
   Get MQTT connection status as string
*/
const char* MqttGetStatusString(void)
{
  if (_mqttClient.connected()) {
    return "Connected";
  }
  
  switch (_mqttClient.state()) {
    case MQTT_CONNECTION_TIMEOUT:
      return "Connection Timeout";
    case MQTT_CONNECTION_LOST:
      return "Connection Lost";
    case MQTT_CONNECT_FAILED:
      return "Connect Failed";
    case MQTT_DISCONNECTED:
      return "Disconnected";
    case MQTT_CONNECTED:
      return "Connected";
    case MQTT_CONNECT_BAD_PROTOCOL:
      return "Bad Protocol";
    case MQTT_CONNECT_BAD_CLIENT_ID:
      return "Bad Client ID";
    case MQTT_CONNECT_UNAVAILABLE:
      return "Server Unavailable";
    case MQTT_CONNECT_BAD_CREDENTIALS:
      return "Bad Credentials";
    case MQTT_CONNECT_UNAUTHORIZED:
      return "Unauthorized";
    default:
      return "Unknown";
  }
}
