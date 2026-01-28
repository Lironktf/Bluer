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

// Machine goes offline if not updated in 2 minutes
const OFFLINE_TIMEOUT_MS = 2 * 60 * 1000;

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
      const { machineId, room, running, empty } = req.body;

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
      
      await machines.updateOne(
        { machineId },
        {
          $set: updateData,
          $setOnInsert: { createdAt: now }
        },
        { upsert: true }
      );

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
          timeSinceUpdate: timeSinceUpdate
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
