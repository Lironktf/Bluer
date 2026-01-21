#!/usr/bin/env node
/**
 * Script to add a room to the database
 * Usage: node api/scripts/addRoom.js
 */

import { MongoClient } from 'mongodb';
import * as dotenv from 'dotenv';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

// Load environment variables
const __dirname = dirname(fileURLToPath(import.meta.url));
dotenv.config({ path: join(__dirname, '../../.env') });

const MONGODB_URI = process.env.MONGODB_URI;
const MONGODB_DB = process.env.MONGODB_DB || 'laundry';

if (!MONGODB_URI) {
  console.error('❌ Error: MONGODB_URI environment variable not set');
  process.exit(1);
}

async function addRoom() {
  console.log('🔗 Connecting to MongoDB...');
  const client = await MongoClient.connect(MONGODB_URI);
  const db = client.db(MONGODB_DB);

  try {
    const rooms = db.collection('rooms');

    // Room data
    const roomData = {
      name: 'STJ',
      building: '',
      floor: '',
      machineIds: [], // Add machine IDs here if needed
      isPublic: true, // Public room, not tied to a specific user
      userId: null, // No specific owner
      createdAt: new Date(),
      updatedAt: new Date()
    };

    // Check if room already exists
    const existing = await rooms.findOne({ name: 'STJ' });
    if (existing) {
      console.log('⚠️  Room "STJ" already exists');
      console.log('   Room ID:', existing._id);
      await client.close();
      return;
    }

    // Insert room
    const result = await rooms.insertOne(roomData);
    console.log('✅ Room "STJ" created successfully!');
    console.log('   Room ID:', result.insertedId);

  } catch (error) {
    console.error('❌ Error adding room:', error);
    process.exit(1);
  } finally {
    await client.close();
    console.log('🔌 Disconnected from MongoDB\n');
  }
}

addRoom().catch(error => {
  console.error('❌ Fatal error:', error);
  process.exit(1);
});
