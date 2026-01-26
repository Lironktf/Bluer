/*
  BLE-Scanner - Laundry Machine Monitor

  Module to track laundry machine status and post to API

  This file is part of BLE-Scanner.

  BLE-Scanner is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

*/

#ifndef __SCANDEV_H__
#define __SCANDEV_H__ 1

#include "config.h"
#include "bluetooth.h"

/*
   Maximum number of machines to track
*/
#define SCANDEV_MAX_MACHINES    50

/*
   Struct to hold a laundry machine's status
*/
typedef struct _scandev_machine {
  // Identification
  BLEAddress addr;
  char machineId[MACHINE_ID_MAX_LEN + 1];
  
  // Current state
  bool running;
  bool empty;
  int rssi;
  
  // Tracking
  time_t last_seen;
  bool present;
  
  // API posting
  bool state_changed;       // True if state changed since last post
  time_t last_posted;       // Last time status was posted to API
  bool post_pending;        // True if we need to post
  
  // Previous state (to detect changes)
  bool prev_running;
  bool prev_empty;
  
  // List management
  bool in_use;
} SCANDEV_MACHINE_T;

/*
   Add or update a laundry machine in the list
*/
bool ScanDevAddMachine(const BLEAddress addr, const char* machineId, 
                       bool running, bool empty, int rssi);

/*
   Return the machine list as HTML for web interface
*/
void ScanDevListHTML(void (*callback)(const String& content));

/*
   Setup the scan device tracking
*/
void ScanDevSetup(void);

/*
   Cyclic update - posts status to API, marks absent machines
*/
void ScanDevUpdate(void);

/*
   Get count of tracked machines
*/
int ScanDevGetCount(void);

#endif

