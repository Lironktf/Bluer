/*
  BLE-Scanner - Laundry Machine Monitor

  Module to track laundry machine status and post to API

  This file is part of BLE-Scanner.

  BLE-Scanner is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

*/

#include "config.h"
#include "state.h"
#include "bluetooth.h"
#include "mqtt.h"
#include "util.h"
#include "scandev.h"

// Array of tracked machines
static SCANDEV_MACHINE_T _machines[SCANDEV_MAX_MACHINES];
static int _machine_count = 0;

// Minimum time between API posts for same machine (seconds)
#define MIN_POST_INTERVAL 5

// Time after which a machine is considered absent (seconds)
#define ABSENCE_TIMEOUT(cfg) ((cfg).bluetooth.absence_cycles * ((cfg).bluetooth.scan_time + (cfg).bluetooth.pause_time))

/*
   Find a machine by address, or return NULL if not found
*/
static SCANDEV_MACHINE_T* findMachineByAddr(const BLEAddress& addr)
{
  for (int i = 0; i < SCANDEV_MAX_MACHINES; i++) {
    if (_machines[i].in_use && _machines[i].addr == addr) {
      return &_machines[i];
    }
  }
  return NULL;
}

/*
   Find a machine by machineId, or return NULL if not found
*/
static SCANDEV_MACHINE_T* findMachineById(const char* machineId)
{
  for (int i = 0; i < SCANDEV_MAX_MACHINES; i++) {
    if (_machines[i].in_use && strcmp(_machines[i].machineId, machineId) == 0) {
      return &_machines[i];
    }
  }
  return NULL;
}

/*
   Find an empty slot, or return NULL if full
*/
static SCANDEV_MACHINE_T* findEmptySlot()
{
  for (int i = 0; i < SCANDEV_MAX_MACHINES; i++) {
    if (!_machines[i].in_use) {
      return &_machines[i];
    }
  }
  return NULL;
}

/*
   Add or update a laundry machine
*/
bool ScanDevAddMachine(const BLEAddress addr, const char* machineId, 
                       bool running, bool empty, int rssi)
{
  SCANDEV_MACHINE_T* machine = findMachineById(machineId);
  
  if (!machine) {
    // New machine - find empty slot
    machine = findEmptySlot();
    if (!machine) {
      LogMsg("SCANDEV: No empty slots for new machine %s", machineId);
      return false;
    }
    
    // Initialize new machine
    memset(machine, 0, sizeof(SCANDEV_MACHINE_T));
    machine->in_use = true;
    machine->addr = addr;
    strncpy(machine->machineId, machineId, MACHINE_ID_MAX_LEN);
    machine->prev_running = !running; // Force initial post
    machine->prev_empty = !empty;
    _machine_count++;
    
    LogMsg("SCANDEV: New machine added: %s (total: %d)", machineId, _machine_count);
  }
  
  // Check if state changed
  bool stateChanged = (machine->running != running) || (machine->empty != empty);
  
  // Update machine data
  machine->addr = addr;
  machine->running = running;
  machine->empty = empty;
  machine->rssi = rssi;
  machine->last_seen = now();
  machine->present = true;
  
  // Mark for posting if state changed
  if (stateChanged) {
    machine->state_changed = true;
    machine->post_pending = true;
    LogMsg("SCANDEV: Machine %s state changed - Running: %d->%d, Empty: %d->%d",
           machineId, machine->prev_running, running, machine->prev_empty, empty);
    machine->prev_running = running;
    machine->prev_empty = empty;
  }
  
  return true;
}

/*
   Setup
*/
void ScanDevSetup(void)
{
  memset(_machines, 0, sizeof(_machines));
  _machine_count = 0;
  LogMsg("SCANDEV: Initialized machine tracking (max %d machines)", SCANDEV_MAX_MACHINES);
}

/*
   Cyclic update - posts to API and marks absent machines
*/
void ScanDevUpdate(void)
{
  time_t currentTime = now();
  int absence_timeout = ABSENCE_TIMEOUT(_config);
  
  for (int i = 0; i < SCANDEV_MAX_MACHINES; i++) {
    if (!_machines[i].in_use) continue;
    
    SCANDEV_MACHINE_T* machine = &_machines[i];
    
    // Check for absence
    if (machine->present && (currentTime - machine->last_seen > absence_timeout)) {
      LogMsg("SCANDEV: Machine %s went absent (not seen for %ld seconds)",
             machine->machineId, currentTime - machine->last_seen);
      machine->present = false;
      // Don't post absence - the API will detect offline via lastUpdate timeout
    }
    
    // Publish to MQTT if pending and enough time has passed
    if (machine->post_pending && machine->present) {
      if (currentTime - machine->last_posted >= MIN_POST_INTERVAL) {
        LogMsg("SCANDEV: Publishing status for %s to MQTT", machine->machineId);
        
        if (MqttPublishMachineStatus(machine->machineId, machine->running, machine->empty)) {
          machine->post_pending = false;
          machine->state_changed = false;
          machine->last_posted = currentTime;
          LogMsg("SCANDEV: Successfully published status for %s", machine->machineId);
        } else {
          LogMsg("SCANDEV: Failed to publish status for %s - will retry", machine->machineId);
        }
      }
    }
  }
}

/*
   Get count of tracked machines
*/
int ScanDevGetCount(void)
{
  return _machine_count;
}

/*
   Return machine list as HTML
*/
void ScanDevListHTML(void (*callback)(const String& content))
{
  (*callback)("<p>Tracked Laundry Machines: " + String(_machine_count) +
              " @ " + String(TimeToString(now())) + "</p>" +
              "<table class='btscanlist'>"
              "<tr>"
              "<th>Machine ID</th>"
              "<th>Running</th>"
              "<th>Empty</th>"
              "<th>Present</th>"
              "<th>RSSI [dBm]</th>"
              "<th>Last Seen</th>"
              "<th>Last Posted</th>"
              "</tr>");

  bool foundAny = false;
  for (int i = 0; i < SCANDEV_MAX_MACHINES; i++) {
    if (!_machines[i].in_use) continue;
    foundAny = true;
    
    SCANDEV_MACHINE_T* m = &_machines[i];
    (*callback)("<tr>"
                "<td>" + String(m->machineId) + "</td>"
                "<td>" + String(m->running ? "YES" : "NO") + "</td>"
                "<td>" + String(m->empty ? "YES" : "NO") + "</td>"
                "<td>" + String(m->present ? "✅" : "❌") + "</td>"
                "<td>" + String(m->rssi) + "</td>"
                "<td>" + String(TimeToString(m->last_seen)) + "</td>"
                "<td>" + String(m->last_posted ? TimeToString(m->last_posted) : "-") + "</td>"
                "</tr>");
  }
  
  if (!foundAny) {
    (*callback)("<tr>"
                "<td colspan=7>No machines detected yet</td>"
                "</tr>");
  }
  (*callback)("</table>");
}
