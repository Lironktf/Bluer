// Machine fault reporting.
//
// There are no user accounts, so reporters are identified only by an anonymous
// device id generated in the browser. That is resettable, so reaching the
// threshold deliberately does NOT hide a machine -- it flags it with a warning
// while still showing live sensor status. Worst case for abuse is a scary
// label on a working machine, not a machine nobody can find for a week.
//
// Lifecycle:
//   * Anyone can report a machine broken. Reports are deduped per device.
//   * 3 distinct devices within BROKEN_WINDOW_MS  -> machine is flagged for
//     FLAG_DURATION_MS (one week).
//   * While flagged, anyone can report it fixed. 3 distinct devices clears the
//     flag immediately, at any point during the week.
//   * A flag that is neither cleared nor renewed simply expires.
//
// Collections:
//   machineReports { machineId, type, deviceId, cycleId, ipHash, createdAt }
//   machineFlags   { machineId, cycleId, flaggedAt, until, clearedAt }

import { createHash } from 'crypto';
import { ObjectId } from 'mongodb';
import { getCollection } from './lib/mongodb.js';

const BROKEN_THRESHOLD = 3;
const FIXED_THRESHOLD = 3;

const FLAG_DURATION_MS = 7 * 24 * 60 * 60 * 1000; // one week
const BROKEN_WINDOW_MS = 7 * 24 * 60 * 60 * 1000; // stale reports stop counting

// Coarse abuse guard. Dorm wifi puts everyone behind one NAT address, so an IP
// can't be used as an identity -- only to stop a single machine hammering the
// endpoint.
const IP_RATE_LIMIT = 20;
const IP_RATE_WINDOW_MS = 60 * 60 * 1000;

const VALID_TYPES = ['broken', 'fixed'];

function hashIp(req) {
  const forwarded = req.headers['x-forwarded-for'] || '';
  const ip = forwarded.split(',')[0].trim() || req.socket?.remoteAddress || 'unknown';
  return createHash('sha256').update(ip).digest('hex').slice(0, 32);
}

function setCors(res) {
  res.setHeader('Access-Control-Allow-Credentials', true);
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,POST,OPTIONS');
  res.setHeader(
    'Access-Control-Allow-Headers',
    'X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Content-Type, Date, X-Api-Version'
  );
}

/**
 * Active flags, keyed by machineId. A flag is active when it has not been
 * cleared by fix reports and has not expired.
 */
async function getActiveFlags(flags, now) {
  const active = await flags
    .find({ clearedAt: null, until: { $gt: now } })
    .toArray();

  return active.reduce((acc, flag) => {
    acc[flag.machineId] = flag;
    return acc;
  }, {});
}

/** Distinct devices that filed `type` for a machine, within a cycle or window. */
async function countDistinctDevices(reports, machineId, type, { cycleId, since }) {
  const filter = { machineId, type };

  if (cycleId) {
    filter.cycleId = cycleId;
  } else {
    filter.cycleId = null;
    filter.createdAt = { $gte: since };
  }

  const devices = await reports.distinct('deviceId', filter);
  return devices.length;
}

export default async function handler(req, res) {
  setCors(res);

  if (req.method === 'OPTIONS') {
    res.status(200).end();
    return;
  }

  try {
    const reports = await getCollection('machineReports');
    const flags = await getCollection('machineFlags');
    const now = new Date();

    // GET - report state for every machine, for the dashboard to render.
    if (req.method === 'GET') {
      const activeFlags = await getActiveFlags(flags, now);
      const brokenSince = new Date(now.getTime() - BROKEN_WINDOW_MS);

      // Pending broken reports (not yet part of a flagged cycle).
      const pending = await reports
        .aggregate([
          { $match: { type: 'broken', cycleId: null, createdAt: { $gte: brokenSince } } },
          { $group: { _id: { machineId: '$machineId', deviceId: '$deviceId' } } },
          { $group: { _id: '$_id.machineId', count: { $sum: 1 } } },
        ])
        .toArray();

      const state = {};

      for (const row of pending) {
        state[row._id] = {
          flagged: false,
          brokenCount: row.count,
          fixedCount: 0,
          threshold: BROKEN_THRESHOLD,
        };
      }

      // Flagged machines, plus how far along their fix reports are.
      for (const [machineId, flag] of Object.entries(activeFlags)) {
        const fixedCount = await countDistinctDevices(reports, machineId, 'fixed', {
          cycleId: flag.cycleId,
        });

        state[machineId] = {
          flagged: true,
          brokenCount: BROKEN_THRESHOLD,
          fixedCount,
          threshold: FIXED_THRESHOLD,
          since: flag.flaggedAt,
          until: flag.until,
        };
      }

      return res.status(200).json({ success: true, reports: state, timestamp: now.toISOString() });
    }

    // POST - file a report.
    if (req.method === 'POST') {
      const { machineId, type, deviceId } = req.body || {};

      if (!machineId || typeof machineId !== 'string') {
        return res.status(400).json({ success: false, error: 'machineId is required' });
      }
      if (!VALID_TYPES.includes(type)) {
        return res.status(400).json({ success: false, error: "type must be 'broken' or 'fixed'" });
      }
      if (!deviceId || typeof deviceId !== 'string') {
        return res.status(400).json({ success: false, error: 'deviceId is required' });
      }

      const ipHash = hashIp(req);
      const rateSince = new Date(now.getTime() - IP_RATE_WINDOW_MS);
      const recentFromIp = await reports.countDocuments({ ipHash, createdAt: { $gte: rateSince } });

      if (recentFromIp >= IP_RATE_LIMIT) {
        return res.status(429).json({ success: false, error: 'Too many reports. Try again later.' });
      }

      const activeFlags = await getActiveFlags(flags, now);
      const activeFlag = activeFlags[machineId] || null;

      if (type === 'fixed') {
        // Only meaningful while the machine is actually flagged.
        if (!activeFlag) {
          return res.status(409).json({
            success: false,
            error: 'This machine is not currently reported broken.',
          });
        }

        await reports.updateOne(
          { machineId, type: 'fixed', deviceId, cycleId: activeFlag.cycleId },
          { $setOnInsert: { machineId, type: 'fixed', deviceId, cycleId: activeFlag.cycleId, ipHash, createdAt: now } },
          { upsert: true }
        );

        const fixedCount = await countDistinctDevices(reports, machineId, 'fixed', {
          cycleId: activeFlag.cycleId,
        });

        if (fixedCount >= FIXED_THRESHOLD) {
          await flags.updateOne({ _id: activeFlag._id }, { $set: { clearedAt: now } });
          console.log(`✅ [${machineId}] Flag cleared by ${fixedCount} fix reports`);

          return res.status(200).json({
            success: true,
            machineId,
            flagged: false,
            fixedCount,
            threshold: FIXED_THRESHOLD,
            cleared: true,
          });
        }

        return res.status(200).json({
          success: true,
          machineId,
          flagged: true,
          fixedCount,
          threshold: FIXED_THRESHOLD,
          cleared: false,
        });
      }

      // type === 'broken'
      if (activeFlag) {
        // Already flagged; nothing further to count.
        return res.status(200).json({
          success: true,
          machineId,
          flagged: true,
          brokenCount: BROKEN_THRESHOLD,
          threshold: BROKEN_THRESHOLD,
          alreadyFlagged: true,
        });
      }

      await reports.updateOne(
        { machineId, type: 'broken', deviceId, cycleId: null },
        { $setOnInsert: { machineId, type: 'broken', deviceId, cycleId: null, ipHash, createdAt: now } },
        { upsert: true }
      );

      const brokenSince = new Date(now.getTime() - BROKEN_WINDOW_MS);
      const brokenCount = await countDistinctDevices(reports, machineId, 'broken', {
        since: brokenSince,
      });

      if (brokenCount >= BROKEN_THRESHOLD) {
        const cycleId = new ObjectId();
        const until = new Date(now.getTime() + FLAG_DURATION_MS);

        await flags.insertOne({
          machineId,
          cycleId,
          flaggedAt: now,
          until,
          clearedAt: null,
        });

        // Attach the reports that triggered this flag to the cycle, so they
        // stop counting toward any future one.
        await reports.updateMany(
          { machineId, type: 'broken', cycleId: null },
          { $set: { cycleId } }
        );

        console.log(`⚠️ [${machineId}] Flagged broken by ${brokenCount} devices until ${until.toISOString()}`);

        return res.status(200).json({
          success: true,
          machineId,
          flagged: true,
          brokenCount,
          threshold: BROKEN_THRESHOLD,
          until,
        });
      }

      return res.status(200).json({
        success: true,
        machineId,
        flagged: false,
        brokenCount,
        threshold: BROKEN_THRESHOLD,
      });
    }

    return res.status(405).json({ error: 'Method not allowed' });
  } catch (error) {
    console.error('❌ Reports API error:', error);
    return res.status(500).json({
      success: false,
      error: 'Internal server error',
      message: error.message,
    });
  }
}
