// Vercel Serverless Function for firmware-trial telemetry
// Returns the raw heartbeat samples captured for nodes listed in
// DIAGNOSTIC_MACHINE_IDS (see machines.js).
import { getCollection } from './lib/mongodb.js';

const DEFAULT_LIMIT = 2000;
const MAX_LIMIT = 20000;

export default async function handler(req, res) {
  res.setHeader('Access-Control-Allow-Credentials', true);
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,OPTIONS');
  res.setHeader(
    'Access-Control-Allow-Headers',
    'X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Content-Type, Date, X-Api-Version'
  );

  if (req.method === 'OPTIONS') {
    res.status(200).end();
    return;
  }

  if (req.method !== 'GET') {
    return res.status(405).json({ success: false, error: 'Method not allowed' });
  }

  try {
    const { machineId, since, limit } = req.query;

    const parsedLimit = Math.min(parseInt(limit, 10) || DEFAULT_LIMIT, MAX_LIMIT);

    const filter = {};
    if (machineId) {
      filter.machineId = machineId;
    }
    if (since) {
      const sinceDate = new Date(since);
      if (!Number.isNaN(sinceDate.getTime())) {
        filter.timestamp = { $gte: sinceDate };
      }
    }

    const diagnostics = await getCollection('machineDiagnostics');
    const records = await diagnostics
      .find(filter)
      .sort({ timestamp: -1 })
      .limit(parsedLimit)
      .toArray();

    return res.status(200).json({
      success: true,
      count: records.length,
      limit: parsedLimit,
      records
    });
  } catch (error) {
    console.error('❌ Diagnostics API Error:', error);
    return res.status(500).json({
      success: false,
      error: 'Internal server error',
      message: error.message
    });
  }
}
