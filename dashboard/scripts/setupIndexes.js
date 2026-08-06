#!/usr/bin/env node
/**
 * MongoDB Index Setup Script
 *
 * Creates optimal indexes for the laundry machine database
 * Run this script once after setting up your MongoDB database
 *
 * Usage: node scripts/setupIndexes.js
 */

import { MongoClient } from 'mongodb';
import * as dotenv from 'dotenv';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

// Load environment variables
const __dirname = dirname(fileURLToPath(import.meta.url));
dotenv.config({ path: join(__dirname, '../.env') });

const MONGODB_URI = process.env.MONGODB_URI;
const MONGODB_DB = process.env.MONGODB_DB || 'laundry';

if (!MONGODB_URI) {
  console.error('❌ Error: MONGODB_URI environment variable not set');
  console.error('Create a .env file with your MongoDB connection string');
  process.exit(1);
}

async function setupIndexes() {
  console.log('🔗 Connecting to MongoDB...');
  console.log(`   Database: ${MONGODB_DB}`);

  const client = await MongoClient.connect(MONGODB_URI);
  const db = client.db(MONGODB_DB);

  try {
    // ===== MACHINES COLLECTION =====
    console.log('\n📦 Setting up indexes for "machines" collection...');
    const machines = db.collection('machines');

    // Unique index on machineId
    await machines.createIndex(
      { machineId: 1 },
      { unique: true, name: 'machineId_unique' }
    );
    console.log('   ✓ Created unique index on machineId');

    // Index for availability checking (queries by lastUpdate)
    await machines.createIndex(
      { lastUpdate: -1 },
      { name: 'lastUpdate_desc' }
    );
    console.log('   ✓ Created index on lastUpdate');

    // Index for efficient queries by availability status
    await machines.createIndex(
      { available: 1, lastUpdate: -1 },
      { name: 'available_lastUpdate' }
    );
    console.log('   ✓ Created compound index on available + lastUpdate');

    // ===== MACHINE HISTORY COLLECTION =====
    console.log('\n📜 Setting up indexes for "machineHistory" collection...');
    const history = db.collection('machineHistory');

    // Index for queries by machineId
    await history.createIndex(
      { machineId: 1 },
      { name: 'machineId_idx' }
    );
    console.log('   ✓ Created index on machineId');

    // Index for time-based queries (most recent first)
    await history.createIndex(
      { timestamp: -1 },
      { name: 'timestamp_desc' }
    );
    console.log('   ✓ Created index on timestamp');

    // Compound index for machine-specific time queries
    await history.createIndex(
      { machineId: 1, timestamp: -1 },
      { name: 'machineId_timestamp' }
    );
    console.log('   ✓ Created compound index on machineId + timestamp');

    // Index for changeType filtering
    await history.createIndex(
      { changeType: 1, timestamp: -1 },
      { name: 'changeType_timestamp' }
    );
    console.log('   ✓ Created compound index on changeType + timestamp');

    // Optional: TTL index to auto-delete old history (uncomment if desired)
    // This will automatically delete records older than 90 days
    /*
    await history.createIndex(
      { timestamp: 1 },
      { expireAfterSeconds: 7776000, name: 'ttl_90days' }
    );
    console.log('   ✓ Created TTL index (90 days retention)');
    */

    // ===== MACHINE REPORTS COLLECTION =====
    console.log('\n🚩 Setting up indexes for "machineReports" collection...');
    const reports = db.collection('machineReports');

    // One report per device per machine per type per cycle. This is what makes
    // the 3-distinct-devices threshold meaningful.
    await reports.createIndex(
      { machineId: 1, type: 1, deviceId: 1, cycleId: 1 },
      { unique: true, name: 'report_dedupe_unique' }
    );
    console.log('   ✓ Created unique index on machineId + type + deviceId + cycleId');

    // Counting pending reports inside the rolling window
    await reports.createIndex(
      { machineId: 1, type: 1, cycleId: 1, createdAt: -1 },
      { name: 'report_lookup' }
    );
    console.log('   ✓ Created compound index for report counting');

    // IP rate limiting
    await reports.createIndex({ ipHash: 1, createdAt: -1 }, { name: 'ipHash_createdAt' });
    console.log('   ✓ Created index on ipHash + createdAt');

    // ===== MACHINE FLAGS COLLECTION =====
    console.log('\n⚠️  Setting up indexes for "machineFlags" collection...');
    const flags = db.collection('machineFlags');

    // Active-flag lookup: not cleared, not expired
    await flags.createIndex(
      { clearedAt: 1, until: -1 },
      { name: 'active_flags' }
    );
    console.log('   ✓ Created compound index on clearedAt + until');

    await flags.createIndex({ machineId: 1, until: -1 }, { name: 'machineId_until' });
    console.log('   ✓ Created compound index on machineId + until');

    // ===== VERIFY INDEXES =====
    console.log('\n🔍 Verifying indexes...');

    const machinesIndexes = await machines.indexes();
    console.log(`\n   machines collection (${machinesIndexes.length} indexes):`);
    machinesIndexes.forEach(idx => {
      console.log(`      - ${idx.name}: ${JSON.stringify(idx.key)}`);
    });

    const historyIndexes = await history.indexes();
    console.log(`\n   machineHistory collection (${historyIndexes.length} indexes):`);
    historyIndexes.forEach(idx => {
      console.log(`      - ${idx.name}: ${JSON.stringify(idx.key)}`);
    });

    console.log('\n✅ Index setup complete!');
    console.log('\n💡 Tips:');
    console.log('   - Monitor index usage in MongoDB Atlas');
    console.log('   - Consider enabling TTL index for automatic history cleanup');
    console.log('   - Run db.collection.stats() to see collection statistics\n');

  } catch (error) {
    console.error('\n❌ Error setting up indexes:', error);
    process.exit(1);
  } finally {
    await client.close();
    console.log('🔌 Disconnected from MongoDB\n');
  }
}

// Run the setup
setupIndexes().catch(error => {
  console.error('❌ Fatal error:', error);
  process.exit(1);
});

