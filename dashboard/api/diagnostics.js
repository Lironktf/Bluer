// Vercel Serverless Function for test tooling:
//  - GET  (no kind)          raw heartbeat samples for /test/history (unchanged)
//  - GET  ?kind=auth         validate the node settings password                [password]
//  - GET  ?kind=nodes        every node's thresholds, history and test mode
//  - PUT  ?kind=config       save a node's thresholds
//  - PUT  ?kind=testmode     switch a node's live-readings test mode on or off
//  - POST ?kind=telemetry    a node uploads per-second readings while in test mode
//  - GET  ?kind=telemetry    readings for /test/live
// Kept in this one file on purpose: every file under api/ is a separate Vercel function.
import crypto from 'crypto';
import { getCollection } from './lib/mongodb.js';

const DEFAULT_LIMIT = 2000;
const MAX_LIMIT = 20000;
// Thresholds a node follows, with the range a saved value must fall in.
const THRESHOLD_LIMITS = {
  dryerOnThreshold: [0.1, 50],
  dryerDoorChange: [0.05, 10],
  washerFirstFill: [1000, 10000000],
  washerRefill: [1000, 10000000],
  washerRefillBands: [1, 3],  // 1 = lower band, 2 = either band, 3 = upper band only
  washerRefillTimerMs: [60000, 3600000],
  washerDoorDrop: [10000, 100000000],
  washerFullTimerMs: [60000, 7200000],
  washerMaxCycleMs: [60000, 7200000],
};
const INTEGER_THRESHOLDS = new Set(['washerRefillBands']);

const BASE_THRESHOLDS = {
  dryerOnThreshold: 2.45,
  dryerDoorChange: 0.65,
  washerFirstFill: 68500,
  washerRefill: 32000,
  washerRefillBands: 2,
  washerRefillTimerMs: 600000,
  washerDoorDrop: 750000,
  washerFullTimerMs: 1500000,
  washerMaxCycleMs: 2520000,
};

// Per-machine values flashed today, from Arduino/MACHINES.md.
const SJ_OVERRIDES = {
  'sj-3': { washerRefillTimerMs: 630000 },
  'sj-4': { dryerDoorChange: 0.55, washerRefillTimerMs: 630000 },
  'sj-5': { washerRefillTimerMs: 630000 },
  'sj-6': { washerRefillTimerMs: 630000 },
  'sj-7': { washerRefillTimerMs: 630000, washerDoorDrop: 550000 },
  'sj-8': { washerRefillTimerMs: 630000 },
  'sj-10': { dryerDoorChange: 0.55, washerRefillTimerMs: 630000, washerRefill: 51500, washerRefillBands: 1 },
};

const SJ_NODE_IDS = Array.from({ length: 10 }, (_, i) => `sj-${i + 1}`);

// One value per second, uploaded in batches while test mode is on.
const TELEMETRY_FIELDS = [
  'dAvg10', 'dDoorJumpMax', 'dRunning', 'dEmpty',
  'wWater1Max', 'wWater2Max', 'wDoorDropMax', 'wRunning', 'wEmpty',
];
const MAX_SAMPLES_PER_BATCH = 120;
const TELEMETRY_TTL_SECONDS = 3 * 24 * 60 * 60;
const TEST_MODE_DEFAULT_MINUTES = 60;
const TEST_MODE_MAX_MINUTES = 240;

const NODE_ID_PATTERN = /^[A-Za-z]+-\d{1,3}$/;

/** "sj-d3" / "sj-w3" -> "sj-3"; "TEST_D1" / "TEST_W1" -> "TEST-1". */
export function nodeIdForMachine(machineId) {
  let m = /^([a-z]+)-[wd](\d+)$/i.exec(machineId || '');
  if (m) return `${m[1].toLowerCase()}-${parseInt(m[2], 10)}`;
  m = /^TEST_[DW](\d+)$/.exec(machineId || '');
  if (m) return `TEST-${parseInt(m[1], 10)}`;
  return null;
}

function defaultThresholds(nodeId) {
  return { ...BASE_THRESHOLDS, ...(SJ_OVERRIDES[nodeId] || {}) };
}

// Saved configs predating a newly added threshold still get a complete set.
function effectiveThresholds(nodeId, doc) {
  return { ...defaultThresholds(nodeId), ...(doc?.thresholds || {}) };
}

function testModeOn(doc, now = new Date()) {
  return Boolean(doc?.testModeUntil && new Date(doc.testModeUntil) > now);
}

async function configReply(nodeId, configVersion) {
  const configs = await getCollection('nodeConfigs');
  const doc = await configs.findOne({ nodeId });

  if (doc?.thresholds && typeof configVersion === 'number' && configVersion !== doc.appliedVersion) {
    await configs.updateOne({ nodeId }, { $set: { appliedVersion: configVersion, appliedAt: new Date() } });
  }

  const reply = { nodeId, testMode: testModeOn(doc) };
  if (doc?.thresholds) reply.config = { version: doc.version, ...effectiveThresholds(nodeId, doc) };
  return reply;
}

/**
 * Called by machines.js on every node report. Returns what the node should follow:
 * its saved thresholds (only once someone has saved them) and whether test mode is on.
 */
export async function nodeConfigForReport(machineId, configVersion) {
  const nodeId = nodeIdForMachine(machineId);
  if (!nodeId) return {};
  return configReply(nodeId, configVersion);
}

const MAX_FAILED_LOGINS = 10;  // per IP, per FAILED_LOGIN_WINDOW_S
const FAILED_LOGIN_WINDOW_S = 15 * 60;
const sha256 = (value) => crypto.createHash('sha256').update(String(value)).digest();

let authIndexes = null;
async function failedLogins() {
  const col = await getCollection('adminAuthFailures');
  if (!authIndexes) {
    authIndexes = col
      .createIndex({ at: 1 }, { expireAfterSeconds: FAILED_LOGIN_WINDOW_S, name: 'at_ttl' })
      .catch((error) => {
        authIndexes = null;
        throw error;
      });
  }
  await authIndexes;
  return col;
}

async function checkAdmin(req, res) {
  const expected = process.env.NODE_ADMIN_PASSWORD;
  if (!expected) {
    res.status(503).json({ success: false, error: 'NODE_ADMIN_PASSWORD is not set on the server' });
    return false;
  }

  const ip = String(req.headers['x-forwarded-for'] || req.socket?.remoteAddress || 'unknown').split(',')[0].trim();
  const failures = await failedLogins();
  const since = new Date(Date.now() - FAILED_LOGIN_WINDOW_S * 1000);
  if ((await failures.countDocuments({ ip, at: { $gte: since } })) >= MAX_FAILED_LOGINS) {
    res.status(429).json({ success: false, error: 'Too many wrong passwords. Try again in 15 minutes.' });
    return false;
  }

  // Comparing digests keeps the check constant-time and hides the password length.
  if (!crypto.timingSafeEqual(sha256(req.headers['x-admin-password'] || ''), sha256(expected))) {
    await failures.insertOne({ ip, at: new Date() });
    res.status(401).json({ success: false, error: 'Wrong password' });
    return false;
  }
  return true;
}

function validateThresholds(input) {
  const thresholds = {};
  for (const [key, [min, max]] of Object.entries(THRESHOLD_LIMITS)) {
    const value = Number(input?.[key]);
    if (!Number.isFinite(value) || value < min || value > max) {
      return { error: `${key} must be a number between ${min} and ${max}` };
    }
    if (INTEGER_THRESHOLDS.has(key) && !Number.isInteger(value)) {
      return { error: `${key} must be a whole number` };
    }
    thresholds[key] = value;
  }
  if (thresholds.washerMaxCycleMs < thresholds.washerFullTimerMs) {
    return { error: 'Max cycle must be at least as long as the first-fill timer' };
  }
  return { thresholds };
}

function nodeSortKey(nodeId) {
  const [prefix, num] = nodeId.split('-');
  return `${prefix === 'TEST' ? '0' : '1'}${prefix}-${String(num).padStart(4, '0')}`;
}

async function listNodes(res) {
  const [configs, history, machines] = await Promise.all([
    getCollection('nodeConfigs'),
    getCollection('nodeConfigHistory'),
    getCollection('machines'),
  ]);

  const [configDocs, historyDocs, machineDocs] = await Promise.all([
    configs.find({}).toArray(),
    history.find({}, { projection: { thresholds: 0 } }).sort({ changedAt: -1 }).limit(500).toArray(),
    machines
      .find({ machineId: { $regex: '^(sj-|TEST_)' } }, { projection: { machineId: 1, lastUpdate: 1 } })
      .toArray(),
  ]);

  const lastSeen = {};
  for (const m of machineDocs) {
    const nodeId = nodeIdForMachine(m.machineId);
    if (!nodeId || !m.lastUpdate) continue;
    if (!lastSeen[nodeId] || new Date(m.lastUpdate) > new Date(lastSeen[nodeId])) lastSeen[nodeId] = m.lastUpdate;
  }

  const byId = Object.fromEntries(configDocs.map((d) => [d.nodeId, d]));
  const ids = new Set([...SJ_NODE_IDS, ...Object.keys(byId), ...Object.keys(lastSeen)]);
  const now = new Date();

  const nodes = [...ids].sort((a, b) => nodeSortKey(a).localeCompare(nodeSortKey(b))).map((nodeId) => {
    const doc = byId[nodeId];
    return {
      nodeId,
      saved: Boolean(doc?.thresholds),
      version: doc?.version ?? 0,
      thresholds: effectiveThresholds(nodeId, doc),
      appliedVersion: doc?.appliedVersion ?? null,
      appliedAt: doc?.appliedAt ?? null,
      testMode: testModeOn(doc, now),
      testModeUntil: testModeOn(doc, now) ? doc.testModeUntil : null,
      lastSeen: lastSeen[nodeId] ?? null,
      history: historyDocs.filter((h) => h.nodeId === nodeId).slice(0, 10),
    };
  });

  return res.status(200).json({ success: true, limits: THRESHOLD_LIMITS, nodes });
}

async function saveConfig(req, res) {
  const { nodeId } = req.body || {};
  if (typeof nodeId !== 'string' || !NODE_ID_PATTERN.test(nodeId)) {
    return res.status(400).json({ success: false, error: 'Invalid nodeId' });
  }
  const { thresholds, error } = validateThresholds(req.body.thresholds);
  if (error) return res.status(400).json({ success: false, error });

  const configs = await getCollection('nodeConfigs');
  const history = await getCollection('nodeConfigHistory');
  const existing = await configs.findOne({ nodeId });
  const previous = effectiveThresholds(nodeId, existing);

  const changes = Object.keys(THRESHOLD_LIMITS)
    .filter((key) => previous[key] !== thresholds[key])
    .map((key) => ({ key, from: previous[key], to: thresholds[key] }));
  if (existing?.thresholds && changes.length === 0) {
    return res.status(200).json({ success: true, unchanged: true, version: existing.version });
  }

  const now = new Date();
  // $inc so two saves at once can never get the same version number.
  const before = await configs.findOneAndUpdate(
    { nodeId },
    { $set: { nodeId, thresholds, updatedAt: now }, $inc: { version: 1 }, $setOnInsert: { createdAt: now } },
    { upsert: true, returnDocument: 'before' }
  );
  const version = (before?.version ?? 0) + 1;
  await history.insertOne({ nodeId, version, thresholds, changes, changedAt: now });

  return res.status(200).json({ success: true, version, changes });
}

async function setTestMode(req, res) {
  const { nodeId, on, minutes } = req.body || {};
  if (typeof nodeId !== 'string' || !NODE_ID_PATTERN.test(nodeId)) {
    return res.status(400).json({ success: false, error: 'Invalid nodeId' });
  }
  if (typeof on !== 'boolean') {
    return res.status(400).json({ success: false, error: 'on must be true or false' });
  }
  const length = Math.min(Math.max(Number(minutes) || TEST_MODE_DEFAULT_MINUTES, 1), TEST_MODE_MAX_MINUTES);
  const testModeUntil = on ? new Date(Date.now() + length * 60000) : null;

  const configs = await getCollection('nodeConfigs');
  const now = new Date();
  await configs.updateOne(
    { nodeId },
    { $set: { nodeId, testModeUntil, updatedAt: now }, $setOnInsert: { createdAt: now } },
    { upsert: true }
  );
  return res.status(200).json({ success: true, nodeId, testMode: on, testModeUntil });
}

let telemetryIndexes = null;
function ensureTelemetryIndexes(col) {
  if (!telemetryIndexes) {
    telemetryIndexes = Promise.all([
      col.createIndex({ ts: 1 }, { expireAfterSeconds: TELEMETRY_TTL_SECONDS, name: 'ts_ttl' }),
      col.createIndex({ nodeId: 1, ts: 1 }, { name: 'nodeId_ts' }),
    ]).catch((error) => {
      telemetryIndexes = null;
      throw error;
    });
  }
  return telemetryIndexes;
}

async function receiveTelemetry(req, res) {
  const body = req.body || {};
  const nodeId = typeof body.nodeId === 'string' && NODE_ID_PATTERN.test(body.nodeId)
    ? body.nodeId
    : nodeIdForMachine(body.machineId);
  if (!nodeId) return res.status(400).json({ success: false, error: 'Missing nodeId or machineId' });
  if (!Array.isArray(body.samples) || body.samples.length === 0) {
    return res.status(400).json({ success: false, error: 'samples must be a non-empty array' });
  }

  const reply = await configReply(nodeId, body.configVersion);

  if (!reply.testMode) {
    return res.status(200).json({ success: true, stored: 0, ...reply });
  }

  // Samples are one second apart, oldest first, ending now.
  const samples = body.samples.slice(-MAX_SAMPLES_PER_BATCH);
  const end = Date.now();
  const docs = samples.map((s, i) => {
    const row = { nodeId, ts: new Date(end - (samples.length - 1 - i) * 1000) };
    for (const field of TELEMETRY_FIELDS) {
      const value = s?.[field];
      // Only real numbers/booleans: Number(null) would store a fake 0.
      if (typeof value === 'boolean') row[field] = value ? 1 : 0;
      else if (typeof value === 'number' && Number.isFinite(value)) row[field] = value;
    }
    return row;
  });

  const telemetry = await getCollection('nodeTelemetry');
  await ensureTelemetryIndexes(telemetry);
  await telemetry.insertMany(docs);

  return res.status(200).json({ success: true, stored: docs.length, ...reply });
}

async function readTelemetry(req, res) {
  const { nodeId, minutes } = req.query;
  if (typeof nodeId !== 'string' || !NODE_ID_PATTERN.test(nodeId)) {
    return res.status(400).json({ success: false, error: 'Invalid nodeId' });
  }
  const windowMinutes = Math.min(Math.max(Number(minutes) || 15, 1), 180);
  const since = new Date(Date.now() - windowMinutes * 60000);

  const [telemetry, configs] = await Promise.all([getCollection('nodeTelemetry'), getCollection('nodeConfigs')]);
  const [samples, doc] = await Promise.all([
    telemetry
      .find({ nodeId, ts: { $gte: since } }, { projection: { nodeId: 0 } })
      .sort({ ts: -1 })
      .limit(20000)
      .toArray(),
    configs.findOne({ nodeId }),
  ]);
  samples.reverse();

  return res.status(200).json({
    success: true,
    nodeId,
    testMode: testModeOn(doc),
    testModeUntil: testModeOn(doc) ? doc.testModeUntil : null,
    thresholds: effectiveThresholds(nodeId, doc),
    samples,
  });
}

async function readHeartbeats(req, res) {
  const { machineId, prefix, since, limit } = req.query;

  const parsedLimit = Math.min(parseInt(limit, 10) || DEFAULT_LIMIT, MAX_LIMIT);

  const filter = {};
  if (machineId) {
    filter.machineId = machineId;
  } else if (typeof prefix === 'string' && /^[a-z]+$/i.test(prefix)) {
    // Room prefix, e.g. "sj", so retired ids never use up the limit.
    filter.machineId = { $regex: `^${prefix}-` };
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
}

export default async function handler(req, res) {
  res.setHeader('Access-Control-Allow-Credentials', true);
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,POST,PUT,OPTIONS');
  res.setHeader(
    'Access-Control-Allow-Headers',
    'X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Content-Type, Date, X-Api-Version, X-Admin-Password'
  );

  if (req.method === 'OPTIONS') {
    res.status(200).end();
    return;
  }

  const { kind } = req.query;

  try {
    if (req.method === 'GET' && !kind) return await readHeartbeats(req, res);
    if (req.method === 'POST' && kind === 'telemetry') return await receiveTelemetry(req, res);

    if (kind === 'auth') {
      if (!(await checkAdmin(req, res))) return;
    }
    if (req.method === 'GET' && kind === 'auth') return res.status(200).json({ success: true });
    if (req.method === 'GET' && kind === 'nodes') return await listNodes(res);
    if (req.method === 'PUT' && kind === 'config') return await saveConfig(req, res);
    if (req.method === 'PUT' && kind === 'testmode') return await setTestMode(req, res);
    if (req.method === 'GET' && kind === 'telemetry') return await readTelemetry(req, res);

    return res.status(405).json({ success: false, error: 'Method not allowed' });
  } catch (error) {
    console.error('❌ Diagnostics API Error:', error);
    return res.status(500).json({
      success: false,
      error: 'Internal server error',
      message: error.message
    });
  }
}
