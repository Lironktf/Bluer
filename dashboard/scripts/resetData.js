#!/usr/bin/env node
// One-time reset of logged data. Thresholds, users, rooms and current machine status are not touched.
// Usage: node scripts/resetData.js        (shows counts only)
//        node scripts/resetData.js --yes  (deletes)

import { MongoClient } from 'mongodb';
import * as dotenv from 'dotenv';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));
dotenv.config({ path: join(__dirname, '../.env') });

const MONGODB_URI = process.env.MONGODB_URI;
const MONGODB_DB = process.env.MONGODB_DB || 'laundry';
const COLLECTIONS = ['machineHistory', 'machineDiagnostics', 'nodeTelemetry'];
const confirmed = process.argv.includes('--yes');

if (!MONGODB_URI) {
  console.error('MONGODB_URI is not set. Create dashboard/.env (see .env.example).');
  process.exit(1);
}

const client = await MongoClient.connect(MONGODB_URI);
try {
  const db = client.db(MONGODB_DB);
  for (const name of COLLECTIONS) {
    const col = db.collection(name);
    if (confirmed) {
      const { deletedCount } = await col.deleteMany({});
      console.log(`${name}: deleted ${deletedCount}`);
    } else {
      console.log(`${name}: ${await col.countDocuments()} records (would be deleted)`);
    }
  }
  if (!confirmed) console.log('\nNothing deleted. Run again with --yes to delete.');
} finally {
  await client.close();
}
