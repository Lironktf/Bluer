// Vercel Serverless Function for machine status (handles POST and GET)
// Uses MongoDB for persistent storage with state history tracking
import { getCollection } from './lib/mongodb.js';
import { nodeConfigForReport, nodeIdForMachine, storeReadings, REPORT_EVERY_S } from './diagnostics.js';

/**
 * Collections:
 * - machines: Current state of each machine
 * - machineHistory: History of all state changes
 */

/**
 * Map machine ID prefix to room name
 * Extracts prefix from machineId (e.g., "sj" from "sj-w1") and maps to room
 * Update this mapping as you add new areas/machines
 */
function getRoomFromMachineId(machineId) {
  // Extract prefix (e.g., "sj" from "sj-w1")
  const match = machineId.match(/^([a-z]+)-/i);
  if (!match) return null;
  
  const prefix = match[1].toLowerCase();
  
  // Mapping: area prefix -> room name
  const areaToRoomMap = {
    'sj': 'SJU-Sieg/Ryan',
    'fn': 'SJU-Finn',
  };
  
  return areaToRoomMap[prefix] || null;
}

// Machine goes offline if it misses several heartbeats. Older firmware reports every 5 min,
// so 15 min; nodes on the 30 s reports store reportEveryS and go offline after 3 missed reports.
const HEARTBEAT_INTERVAL_MS = 5 * 60 * 1000;
const MISSED_HEARTBEATS_BEFORE_OFFLINE = 3;
const OFFLINE_TIMEOUT_MS = HEARTBEAT_INTERVAL_MS * MISSED_HEARTBEATS_BEFORE_OFFLINE;
const MIN_OFFLINE_TIMEOUT_MS = 2 * 60 * 1000;

function offlineAfterMs(machine) {
  return typeof machine?.reportEveryS === 'number'
    ? Math.max(machine.reportEveryS * 1000 * MISSED_HEARTBEATS_BEFORE_OFFLINE, MIN_OFFLINE_TIMEOUT_MS)
    : OFFLINE_TIMEOUT_MS;
}

// Nodes shown on /test/history. Add an id here to start recording it; remove it to stop.
const DIAGNOSTIC_MACHINE_IDS = Array.from({ length: 10 }, (_, i) => [`sj-w${i + 1}`, `sj-d${i + 1}`]).flat();

// A heartbeat with nothing new is still recorded this often, to show the node is alive.
const DIAGNOSTIC_CHECKIN_MS = 3 * 60 * 60 * 1000;

/**
 * Saves one machine's status, a history record when it changed, and a /test/history record
 * when something is worth logging. Returns whether the state changed.
 */
async function recordStatus({ machineId, room, running, empty, sensorState, resetReason, uptime, freeHeap, reportEveryS }, now) {
  const machines = await getCollection('machines');
  const history = await getCollection('machineHistory');

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
  if (typeof reportEveryS === 'number') updateData.reportEveryS = reportEveryS;

  let recordDiagnostic = false;
  if (DIAGNOSTIC_MACHINE_IDS.includes(machineId)) {
    const prev = currentMachine || {};
    const lastRecordedAt = prev.lastDiagnosticAt ? new Date(prev.lastDiagnosticAt).getTime() : 0;
    recordDiagnostic =
      stateChanged ||
      (prev.sensorState ?? null) !== (typeof sensorState === 'string' ? sensorState : null) ||
      (prev.resetReason ?? null) !== (typeof resetReason === 'number' ? resetReason : null) ||
      (prev.room ?? null) !== (roomName || null) ||
      // Uptime going backwards is a reboot, even when the reset reason repeats.
      (typeof uptime === 'number' && typeof prev.uptime === 'number' && uptime < prev.uptime) ||
      (currentMachine && now - new Date(currentMachine.lastUpdate) >= offlineAfterMs(currentMachine)) ||
      now.getTime() - lastRecordedAt >= DIAGNOSTIC_CHECKIN_MS;
    if (recordDiagnostic) updateData.lastDiagnosticAt = now;
  }

  // A washer's status carries no node diagnostics, because they belong to the dryer
  // reporting it. Clear those, and the superseded sensorOk, once a node
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
  // Back on older firmware: its 5 min heartbeat must not be judged by the 30 s timeout.
  if (typeof reportEveryS !== 'number') unset.reportEveryS = '';
  if (Object.keys(unset).length > 0) update.$unset = unset;

  await machines.updateOne({ machineId }, update, { upsert: true });

  if (recordDiagnostic) {
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
  }

  return stateChanged;
}

// Thresholds and the report interval ride on the reply, so nodes never need a second request.
async function replyFor(machineId, configVersion) {
  try {
    return await nodeConfigForReport(machineId, configVersion);
  } catch (error) {
    console.error(`⚠️ [${machineId}] Node config lookup failed:`, error.message);
    return {};
  }
}

/**
 * The dryer's single report (v: 2): both machines' status for the website, plus the 5 s readings
 * for /test/live. The washer is only included while the dryer can hear it, so a silent washer
 * goes offline on its own.
 */
async function handleReport(body, res) {
  const { machineId, washerId, dryer, washer, resetReason, uptime, freeHeap, configVersion } = body;
  if (typeof machineId !== 'string' || !machineId || typeof dryer?.running !== 'boolean' || typeof dryer?.empty !== 'boolean') {
    return res.status(400).json({ success: false, error: 'Missing machineId or dryer status' });
  }

  const now = new Date();
  const updates = [
    recordStatus({
      machineId,
      running: dryer.running,
      empty: dryer.empty,
      sensorState: typeof dryer.sensorState === 'string' ? dryer.sensorState : undefined,
      resetReason,
      uptime,
      freeHeap,
      reportEveryS: REPORT_EVERY_S
    }, now)
  ];
  const nodeId = nodeIdForMachine(machineId);
  // A washer id from another pair means a mis-set VARIES line; never overwrite that machine's status.
  if (nodeId && nodeIdForMachine(washerId) === nodeId && typeof washer?.running === 'boolean' && typeof washer?.empty === 'boolean') {
    updates.push(recordStatus({
      machineId: washerId,
      running: washer.running,
      empty: washer.empty,
      sensorState: washer.micOk === false ? 'flat' : 'ok',
      reportEveryS: REPORT_EVERY_S
    }, now));
  }
  // Readings are for the graphs only: a failure there must not fail the status report.
  if (nodeId) {
    updates.push(storeReadings(nodeId, body, now.getTime()).catch((error) => {
      console.error(`⚠️ [${machineId}] Storing readings failed:`, error.message);
    }));
  }
  const [reply] = await Promise.all([replyFor(machineId, configVersion), ...updates]);

  return res.status(200).json({ success: true, ...reply });
}

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
      if (req.body?.v === 2) return await handleReport(req.body, res);

      // Older firmware: one machine per POST.
      const { machineId, room, running, empty, sensorState, resetReason, uptime, freeHeap, configVersion } = req.body;

      // Validate required fields
      // A non-string machineId (e.g. {"$ne": null}) would act as a database query operator.
      if (typeof machineId !== 'string' || !machineId || typeof running !== 'boolean' || typeof empty !== 'boolean') {
        return res.status(400).json({
          success: false,
          error: 'Missing required fields: machineId, running, empty'
        });
      }

      const stateChanged = await recordStatus(
        { machineId, room, running, empty, sensorState, resetReason, uptime, freeHeap },
        new Date()
      );

      return res.status(200).json({
        success: true,
        machineId,
        received: { running, empty },
        stateChanged,
        ...(await replyFor(machineId, configVersion))
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
        const isAvailable = timeSinceUpdate < offlineAfterMs(machine);

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

      // Every visitor sees the same list, so Vercel's edge cache answers repeat polls
      // without running this function (and its database reads) each time.
      res.setHeader('Cache-Control', 'public, s-maxage=5, stale-while-revalidate=5');
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
