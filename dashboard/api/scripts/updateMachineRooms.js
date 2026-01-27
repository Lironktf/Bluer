#!/usr/bin/env node
/**
 * Script to update existing machines with room information
 * Usage: node api/scripts/updateMachineRooms.js
 * 
 * IMPORTANT: This script should ONLY be run manually, not automatically!
 */

import { MongoClient } from 'mongodb';
import * as dotenv from 'dotenv';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

// Load environment variables
const __dirname = dirname(fileURLToPath(import.meta.url));
dotenv.config({ path: join(__dirname, '../../.env') });

const MONGODB_URI = process.env.MONGODB_URI;
const MONGODB_DB = process.env.MONGODB_DB || 'LaunDryer';

if (!MONGODB_URI) {
  console.error('❌ Error: MONGODB_URI environment variable not set');
  process.exit(1);
}

/**
 * Map machine IDs to their room names
 * Update this mapping based on your actual machine-to-room assignments
 */
const MACHINE_ROOM_MAP = {
  'a1-m1': 'STJ-Sieg/Ryan',  // Example: machine "a1-m1" belongs to room "STJ-Sieg/Ryan"
  // Add more mappings as needed:
  // 'a1-m2': 'STJ-Sieg/Ryan',
  // 'a2-m1': 'Another Room',
};

async function updateMachineRooms() {
  console.log('🔗 Connecting to MongoDB...');
  console.log(`   Database: ${MONGODB_DB}`);
  
  const client = await MongoClient.connect(MONGODB_URI);
  const db = client.db(MONGODB_DB);

  try {
    const machines = db.collection('machines');
    const rooms = db.collection('rooms');

    console.log('\n📋 Machine-to-Room Mapping:');
    Object.entries(MACHINE_ROOM_MAP).forEach(([machineId, roomName]) => {
      console.log(`   ${machineId} → ${roomName}`);
    });

    let updatedCount = 0;
    let skippedCount = 0;
    let notFoundCount = 0;

    for (const [machineId, roomName] of Object.entries(MACHINE_ROOM_MAP)) {
      // Verify room exists in database
      const room = await rooms.findOne({ name: roomName });
      if (!room) {
        console.log(`\n⚠️  Room "${roomName}" not found in database for machine "${machineId}"`);
        console.log('   Skipping - please create the room first using addRoom.js');
        notFoundCount++;
        continue;
      }

      // Find machine
      const machine = await machines.findOne({ machineId });
      if (!machine) {
        console.log(`\n⚠️  Machine "${machineId}" not found in database`);
        notFoundCount++;
        continue;
      }

      // Check if room is already set and matches
      if (machine.room === roomName) {
        console.log(`\n✓ Machine "${machineId}" already has room "${roomName}" - skipping`);
        skippedCount++;
        continue;
      }

      // Update machine with room
      const result = await machines.updateOne(
        { machineId },
        {
          $set: {
            room: roomName,
            updatedAt: new Date()
          }
        }
      );

      if (result.modifiedCount > 0) {
        console.log(`\n✅ Updated machine "${machineId}" with room "${roomName}"`);
        updatedCount++;
      } else {
        console.log(`\n⚠️  Machine "${machineId}" not updated (no changes)`);
        skippedCount++;
      }
    }

    console.log('\n' + '='.repeat(50));
    console.log('📊 Summary:');
    console.log(`   ✅ Updated: ${updatedCount}`);
    console.log(`   ⏭️  Skipped: ${skippedCount}`);
    console.log(`   ❌ Not Found: ${notFoundCount}`);
    console.log('='.repeat(50));

  } catch (error) {
    console.error('❌ Error updating machines:', error);
    process.exit(1);
  } finally {
    await client.close();
    console.log('\n🔌 Disconnected from MongoDB\n');
  }
}

// IMPORTANT: This script should ONLY be run manually, not automatically!
// If you're seeing machines updated on every git push, check:
// 1. Vercel Dashboard → Project Settings → Build & Development Settings
//    - Make sure "Build Command" doesn't include this script
// 2. GitHub Actions (if you have .github/workflows/)
// 3. Any CI/CD pipelines

// Run the update
updateMachineRooms().catch(error => {
  console.error('❌ Fatal error:', error);
  process.exit(1);
});
