// Vercel Serverless Function for machine status (handles POST and GET)
// Uses MongoDB for persistent storage with state history tracking
import { getCollection } from './lib/mongodb.js';

/**
 * Collections:
 * - machines: Current state of each machine
 * - machineHistory: History of all state changes
 */

/**
 * Map machine ID prefix to room name
 * Extracts prefix from machineId (e.g., "a1" from "a1-m1") and maps to room
 * Update this mapping as you add new areas/machines
 */
function getRoomFromMachineId(machineId) {
  // Extract prefix (e.g., "a1" from "a1-m1")
  const match = machineId.match(/^([a-z0-9]+)-/i);
  if (!match) return null;
  
  const prefix = match[1].toLowerCase();
  
  // Mapping: area prefix -> room name
  const areaToRoomMap = {
    'a1': 'SJU-Sieg/Ryan',
    'a2': 'SJU-Finn',
  };
  
  return areaToRoomMap[prefix] || null;
}

// Machine goes offline if it misses several heartbeats.
//
// This MUST stay comfortably above the firmware's sendInterval (300000 ms, see
// Arduino/dryer_bluer.ino). It was previously 2 minutes,
// which is shorter than the 5 minute heartbeat -- healthy machines were marked
// offline for 3 of every 5 minutes, and each GET wrote a spurious
// "went_offline" history record (~288 per machine per day).
const HEARTBEAT_INTERVAL_MS = 5 * 60 * 1000;
const MISSED_HEARTBEATS_BEFORE_OFFLINE = 3;
const OFFLINE_TIMEOUT_MS = HEARTBEAT_INTERVAL_MS * MISSED_HEARTBEATS_BEFORE_OFFLINE;

// Nodes running the hardened firmware trial. Add an id here to start capturing
// every heartbeat for it; remove it to stop. Empty list disables the capture.
const DIAGNOSTIC_MACHINE_IDS = ['a1-m20', 'a1-m19', 'a1-m18', 'a1-m17'];

export default async function handler(req, res) {
  // Enable CORS
  res.setHeader('Access-Control-Allow-Credentials', true);
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,POST,OPTIONS');
  res.setHeader(
    'Access-Control-Allow-Headers',
    'X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Content-Type, Date, X-Api-Version'
  );

  // Handle OPTIONS request for CORS preflight
  if (req.method === 'OPTIONS') {
    res.status(200).end();
    return;
  }

  try {
    // Handle POST - ESP32 sending status update
    if (req.method === 'POST') {
      const { machineId, room, running, empty, sensorState, resetReason, uptime, freeHeap } = req.body;

      // Validate required fields
      if (!machineId || typeof running !== 'boolean' || typeof empty !== 'boolean') {
        return res.status(400).json({
          success: false,
          error: 'Missing required fields: machineId, running, empty'
        });
      }

      const machines = await getCollection('machines');
      const history = await getCollection('machineHistory');
      const now = new Date();

      // Get current state to detect changes
      const currentMachine = await machines.findOne({ machineId });

      // Determine if state changed
      const stateChanged = !currentMachine ||
                          currentMachine.running !== running ||
                          currentMachine.empty !== empty;

      // Map machine ID prefix to room (if not provided in request)
      let roomName = room;
      if (!roomName || typeof roomName !== 'string' || roomName.trim() === '') {
        roomName = getRoomFromMachineId(machineId);
        if (roomName) {
          console.log(`📍 Mapped ${machineId} (prefix: ${machineId.match(/^([a-z0-9]+)-/i)?.[1]}) -> ${roomName}`);
        }
      } else {
        roomName = roomName.trim();
      }

      // Update or create machine document
      const updateData = {
        machineId,
        running,
        empty,
        available: true,
        lastUpdate: now,
        updatedAt: now
      };
      
      // Add room if we have one (from request or mapping)
      if (roomName) {
        updateData.room = roomName;
      }

      // Optional firmware diagnostics. resetReason 9 is a brownout, 3 is a
      // watchdog restart; nodes on older firmware simply omit these.
      // "ok" | "flat" | "noreply" | "unknown": flat and noreply both mean the wiring
      // at the sensor, not the node, needs attention.
      if (typeof sensorState === 'string') updateData.sensorState = sensorState;
      if (typeof resetReason === 'number') updateData.resetReason = resetReason;
      if (typeof uptime === 'number') updateData.uptime = uptime;
      if (typeof freeHeap === 'number') updateData.freeHeap = freeHeap;
      
      // A relay POST carries no node diagnostics, because they belong to the dryer
      // doing the relaying. Clear those, and the superseded sensorOk, once a node
      // starts reporting sensorState.
      const reportsSensorState = typeof sensorState === 'string';
      const isRelay = reportsSensorState && typeof uptime !== 'number';
      const update = {
        $set: updateData,
        $setOnInsert: { createdAt: now }
      };
      const unset = {};
      if (reportsSensorState) unset.sensorOk = '';
      if (isRelay) {
        unset.resetReason = '';
        unset.uptime = '';
        unset.freeHeap = '';
      }
      if (Object.keys(unset).length > 0) {
        update.$unset = unset;
      }

      await machines.updateOne({ machineId }, update, { upsert: true });

      // Units under firmware trial: keep every heartbeat, not just state changes,
      // so reboots and heap drift stay visible after the fact.
      if (DIAGNOSTIC_MACHINE_IDS.includes(machineId)) {
        const diagnostics = await getCollection('machineDiagnostics');
        await diagnostics.insertOne({
          machineId,
          running,
          empty,
          sensorState: typeof sensorState === 'string' ? sensorState : null,
          resetReason: typeof resetReason === 'number' ? resetReason : null,
          uptime: typeof uptime === 'number' ? uptime : null,
          freeHeap: typeof freeHeap === 'number' ? freeHeap : null,
          room: roomName || null,
          stateChanged,
          timestamp: now
        });
      }

      // Record state change in history if state actually changed
      if (stateChanged) {
        await history.insertOne({
          machineId,
          running,
          empty,
          available: true,
          timestamp: now,
          changeType: currentMachine ? 'update' : 'initial'
        });

        console.log(`📊 [${machineId}] STATE CHANGE - Running: ${running}, Empty: ${empty}`);
      } else {
        console.log(`📊 [${machineId}] Heartbeat - No state change`);
      }

      return res.status(200).json({
        success: true,
        machineId,
        received: { running, empty },
        stateChanged
      });
    }

    // Handle GET - Frontend fetching all statuses
    if (req.method === 'GET') {
      const machines = await getCollection('machines');
      const history = await getCollection('machineHistory');
      const now = new Date();

      // Get all machines
      const allMachines = await machines.find({}).toArray();

      // Check for offline machines and update availability
      const statuses = {};
      for (const machine of allMachines) {
        const timeSinceUpdate = now - new Date(machine.lastUpdate);
        const isAvailable = timeSinceUpdate < OFFLINE_TIMEOUT_MS;

        // Update availability if it changed
        if (machine.available !== isAvailable) {
          await machines.updateOne(
            { machineId: machine.machineId },
            {
              $set: {
                available: isAvailable,
                updatedAt: now
              }
            }
          );

          // Record availability change in history
          await history.insertOne({
            machineId: machine.machineId,
            running: machine.running,
            empty: machine.empty,
            available: isAvailable,
            timestamp: now,
            changeType: isAvailable ? 'came_online' : 'went_offline'
          });

          console.log(`📡 [${machine.machineId}] ${isAvailable ? 'ONLINE' : 'OFFLINE'}`);
        }

        statuses[machine.machineId] = {
          running: machine.running,
          empty: machine.empty,
          available: isAvailable,
          room: machine.room || null, // Room name from machine
          lastUpdate: machine.lastUpdate,
          timeSinceUpdate: timeSinceUpdate,
          sensorState: machine.sensorState ?? null,
          resetReason: machine.resetReason ?? null,
          uptime: machine.uptime ?? null,
          freeHeap: machine.freeHeap ?? null
        };
      }

      console.log(`📤 Sending ${Object.keys(statuses).length} machine statuses`);

      return res.status(200).json({
        success: true,
        machines: statuses,
        timestamp: now.toISOString()
      });
    }

    return res.status(405).json({ error: 'Method not allowed' });

  } catch (error) {
    console.error('❌ API Error:', error);
    return res.status(500).json({
      success: false,
      error: 'Internal server error',
      message: error.message
    });
  }
}
