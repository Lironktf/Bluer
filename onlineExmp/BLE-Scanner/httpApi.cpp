/*
  BLE-Scanner - Laundry Machine Monitor

  HTTP API client for posting machine status to REST endpoint

  This file is part of BLE-Scanner.

  BLE-Scanner is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

*/

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "httpApi.h"
#include "util.h"
#include "state.h"

static WiFiClientSecure _secureClient;
static unsigned long _lastPost = 0;

/*
   Initialize the HTTP API client
*/
void HttpApiSetup(void)
{
  if (StateCheck(STATE_CONFIGURING))
    return;

  LogMsg("HTTP_API: Setting up HTTP API client");
  LogMsg("HTTP_API: Endpoint: %s", API_ENDPOINT);
  
  // Skip certificate verification for simplicity
  // In production, you should use proper certificate validation
  _secureClient.setInsecure();
  
  LogMsg("HTTP_API: Client ready");
}

/*
   Cyclic update
*/
void HttpApiUpdate(void)
{
  // Nothing needed for HTTP (no persistent connection to maintain)
}

/*
   Post machine status to the API
*/
bool HttpApiPostMachineStatus(const char* machineId, bool running, bool empty)
{
  if (StateCheck(STATE_CONFIGURING))
    return false;

  HTTPClient http;
  
  LogMsg("HTTP_API: Posting status for machine %s (running=%d, empty=%d)", 
         machineId, running, empty);
  
  // Begin HTTPS connection
  if (!http.begin(_secureClient, API_ENDPOINT)) {
    LogMsg("HTTP_API: Failed to begin HTTP connection");
    return false;
  }
  
  // Set headers
  http.addHeader("Content-Type", "application/json");
  
  // Build JSON payload
  String payload = "{";
  payload += "\"machineId\":\"";
  payload += machineId;
  payload += "\",\"running\":";
  payload += running ? "true" : "false";
  payload += ",\"empty\":";
  payload += empty ? "true" : "false";
  payload += "}";
  
  LogMsg("HTTP_API: Payload: %s", payload.c_str());
  
  // Send POST request
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    LogMsg("HTTP_API: Response code: %d", httpCode);
    
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
      String response = http.getString();
      LogMsg("HTTP_API: Success - %s", response.c_str());
      http.end();
      return true;
    } else {
      String response = http.getString();
      LogMsg("HTTP_API: Server error - %s", response.c_str());
    }
  } else {
    LogMsg("HTTP_API: Connection failed, error: %s", http.errorToString(httpCode).c_str());
  }
  
  http.end();
  return false;
}
