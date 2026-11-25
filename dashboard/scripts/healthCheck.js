#!/usr/bin/env node
/**
 * Laundry health check, run once a day by .github/workflows/health-check.yml.
 *
 * 1. Does nothing unless the newest summary in health/HEALTH_SUMMARIES.md is 4+ days old
 *    (or the file is empty/missing, or FORCE=1).
 * 2. Reads the last 2 days from MongoDB and works out the numbers for every pair.
 * 3. Fixed rules (RULES) decide what counts as a problem. Only they can trigger an alert;
 *    the AI cannot add or remove problems.
 * 4. Google's Gemini (free tier, GEMINI_API_KEY) writes the summary.
 *    If it fails, a plain summary is written instead.
 * 5. Appends the summary to health/HEALTH_SUMMARIES.md (the workflow commits it) and, only if there
 *    are problems, opens a GitHub issue that @mentions you, which GitHub emails to you.
 *
 * Local test (MONGODB_URI in dashboard/.env):
 *   FORCE=1 DRY_RUN=1 node scripts/healthCheck.js   prints everything, writes and sends nothing
 */
// Only Node built-ins up here: the daily "is it due?" step runs before npm install.
import { appendFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'fs';
import { execSync } from 'child_process';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = join(__dirname, '..', '..');
const SUMMARY_FILE = join(REPO_ROOT, 'health', 'HEALTH_SUMMARIES.md');
const CHECKLIST_FILE = join(REPO_ROOT, 'Arduino', 'INSTALL_CHECKLIST.md');

const MIN = 60 * 1000;
const HOUR = 60 * MIN;
const DAY = 24 * HOUR;
const RUN_EVERY_DAYS = 4;
const WINDOW_MS = 2 * DAY;            // the summary covers the last 2 days
const CYCLE_LOOKBACK_MS = 7 * DAY;    // cycle-pattern rules need ~10 cycles; a washer may do fewer in 2 days
const NO_CYCLES_LOOKBACK_MS = 3 * DAY;
// Pairs checked by every rule. To monitor all boards: Array.from({ length: 10 }, (_, i) => i + 1)
const PAIRS = [1, 2];
const TZ = 'America/Toronto';
// The -latest aliases follow Google's current Flash models, so they survive model retirements.
const AI_MODELS = (process.env.AI_MODEL || 'gemini-flash-latest,gemini-3.8-flash,gemini-flash-lite-latest,gemini-3.5-flash-lite')
  .split(',').map((m) => m.trim());
const GEMINI_API = 'https://generativelanguage.googleapis.com/v1beta';

// Everything that can trigger an alert. Change a number here to make a rule stricter or looser.
const RULES = {
  offlineMin: 30,             // dryer board silent this long at check time
  washerUnheardMin: 60,       // washer status this old while its dryer is reporting
  minSendRate: 0.5,           // share of expected 30 s reports that arrived
  maxUnplannedRestarts: 8,    // more than this in 2 days (not counting the daily planned restart)
  maxWebsiteResets: 8,        // more than this many 5-min "website unreachable" resets in 2 days
  sensorBadMin: 60,           // sensor not "ok" for this long in 2 days
  lowHeapBytes: 40000,        // free memory below this
  timerEndToleranceS: 60,     // a washer cycle "ended on a timer" if within this of 25:00 / 42:00
  timerEndOutOf: 10,          // look at the last 10 finished washer cycles...
  timerEndMin: 5,             // ...alert if this many ended on the same timer
  mergeGapMin: 5,             // running pieces closer than this are one cycle
  flapPieces: 4,              // a cycle made of this many running pieces is "flapping"
  flapCycles: 2,              // alert if this many flapping cycles in 2 days...
  flapPiecesSingle: 6,        // ...or one cycle with this many pieces
  washerShortMin: 20,         // washer cycles can't end before 25:00 unless the washer board restarted
  washerShortCount: 3,
  dryerStuckMin: 150,
  washerStuckExtraMin: 15,    // washer running longer than its max cycle + this
  copyWindowS: 10,            // two machines starting within this of each other (one person loading two takes longer)...
  copyCount: 3,               // ...this many times, and for half or more of the quieter one's cycles
  noCyclesRoomMin: 10,        // "no cycles in 3 days" only counts if the monitored machines had this many...
  nearThresholdShare: 0.85,   // ...and its readings reached 85-100% of its start threshold
  nearThresholdReports: 10,   // ...in at least this many 30 s reports (a machine nobody uses stays far below)
  configPendingHours: 24,
  allowListWarnDays: 350,     // IST allow list lasts 1 year
  inactivityWarnDays: 50,     // GitHub pauses scheduled workflows after 60 days without repo activity
};

const RESET_REASONS = {
  1: 'power-on',
  3: 'software restart',
  4: 'crash',
  5: 'watchdog',
  6: 'watchdog',
  7: 'watchdog',
  8: 'website-unreachable reset',
  9: 'brownout',
};

const SUMMARY_HEADING = /^## Health summary (\d{4}-\d{2}-\d{2})/gm;
const STATS_MARKER = /<!-- health-stats (\{.*?\}) -->/g;

const dryerId = (n) => `sj-d${n}`;
const washerId = (n) => `sj-w${n}`;
const nodeIdOf = (n) => `sj-${n}`;
const ms = (d) => new Date(d).getTime();
const round1 = (x) => Math.round(x * 10) / 10;
const utcDay = (d) => new Date(d).toISOString().slice(0, 10);

const localFmt = new Intl.DateTimeFormat('en-US', {
  timeZone: TZ, weekday: 'short', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit', hourCycle: 'h23',
});
// "Sat 11-29 08:05" in Waterloo time
function local(t) {
  if (t == null) return null;
  const p = Object.fromEntries(localFmt.formatToParts(new Date(t)).map((x) => [x.type, x.value]));
  return `${p.weekday} ${p.month}-${p.day} ${p.hour}:${p.minute}`;
}
const localHour = (t) => Number(Object.fromEntries(localFmt.formatToParts(new Date(t)).map((x) => [x.type, x.value])).hour) % 24;

// ---------- Is it due? ----------

function readSummaryFile() {
  return existsSync(SUMMARY_FILE) ? readFileSync(SUMMARY_FILE, 'utf8') : '';
}

function lastSummaryDay(text) {
  let last = null;
  for (const m of text.matchAll(SUMMARY_HEADING)) if (!last || m[1] > last) last = m[1];
  return last;
}

function isDue(now) {
  if (['1', 'true'].includes(process.env.FORCE)) return true;
  const last = lastSummaryDay(readSummaryFile());
  return !last || (Date.parse(utcDay(now)) - Date.parse(last)) / DAY >= RUN_EVERY_DAYS;
}

// ---------- Repo facts ----------

function allowListDates() {
  if (!existsSync(CHECKLIST_FILE)) return [];
  return [...readFileSync(CHECKLIST_FILE, 'utf8').matchAll(/Allow-listed:\s*(\d{4}-\d{2}-\d{2})/g)].map((m) => m[1]);
}

function daysSinceLastCommit(now) {
  try {
    const t = Number(execSync('git log -1 --format=%ct', { cwd: REPO_ROOT, stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim()) * 1000;
    return Number.isFinite(t) && t > 0 ? Math.floor((now - t) / DAY) : null;
  } catch {
    return null;
  }
}

function previousStats(text) {
  let last = null;
  for (const m of text.matchAll(STATS_MARKER)) last = m[1];
  try {
    return last ? JSON.parse(last) : null;
  } catch {
    return null;
  }
}

// ---------- Loading ----------

async function loadData(db, now) {
  const since = new Date(now - WINDOW_MS);
  const ids = PAIRS.flatMap((n) => [dryerId(n), washerId(n)]);
  const nodeIds = PAIRS.map(nodeIdOf);

  const [machines, diagnostics, history, configs, activeFlags, brokenReported, telemetry] = await Promise.all([
    db.collection('machines').find({ machineId: { $in: ids } }).toArray(),
    // From a day earlier, so a restart right at the window start is still seen as one.
    db.collection('machineDiagnostics')
      .find({ machineId: { $in: ids }, timestamp: { $gte: new Date(now - WINDOW_MS - DAY) } })
      .sort({ timestamp: 1 }).toArray(),
    db.collection('machineHistory')
      .find({ machineId: { $in: ids }, timestamp: { $gte: new Date(now - CYCLE_LOOKBACK_MS) } })
      .project({ machineId: 1, running: 1, empty: 1, timestamp: 1 })
      .sort({ timestamp: 1 }).toArray(),
    db.collection('nodeConfigs').find({ nodeId: { $in: nodeIds } }).toArray(),
    db.collection('machineFlags').distinct('machineId', { clearedAt: null, until: { $gt: now } }),
    db.collection('machineReports').distinct('machineId', { type: 'broken', createdAt: { $gte: since } }),
    // One document per report that reached the website: counts, plus each report's highest
    // dryer vibration (d[1]) and washer water level (w[1], w[2]) for the near-threshold check.
    db.collection('nodeTelemetry').aggregate([
      { $match: { nodeId: { $in: nodeIds }, ts: { $gte: since } } },
      { $project: {
        _id: 0,
        nodeId: 1,
        ts: 1,
        nd: { $size: { $ifNull: ['$d', []] } },
        nw: { $size: { $ifNull: ['$w', []] } },
        dMax: { $max: { $map: { input: { $ifNull: ['$d', []] }, as: 'r', in: { $arrayElemAt: ['$$r', 1] } } } },
        wMax: { $max: { $map: { input: { $ifNull: ['$w', []] }, as: 'r', in: { $max: [{ $arrayElemAt: ['$$r', 1] }, { $arrayElemAt: ['$$r', 2] }] } } } },
      } },
    ]).toArray(),
  ]);

  // A node with no report before the window was set up during it; judge it from its first report.
  const reportedBefore = {};
  for (const nodeId of nodeIds) {
    reportedBefore[nodeId] = Boolean(
      await db.collection('nodeTelemetry').findOne({ nodeId, ts: { $lt: since } }, { projection: { _id: 1 } })
    );
  }

  return { machines, diagnostics, history, configs, activeFlags, brokenReported, telemetry, reportedBefore };
}

// ---------- Analysis helpers ----------

function sendStats(times, intervalS, from, to) {
  const interval = intervalS * 1000;
  const points = [from, ...times.filter((t) => t > from && t <= to).sort((a, b) => a - b), to];
  let missed = 0;
  let gaps2 = 0;
  let longest = 0;
  let longestAt = null;
  const byHour = new Array(24).fill(0);
  for (let i = 1; i < points.length; i++) {
    const gap = points[i] - points[i - 1];
    missed += Math.max(0, Math.round(gap / interval) - 1);
    if (gap >= 2 * MIN) {
      gaps2++;
      byHour[localHour(points[i - 1])]++;
    }
    if (gap > longest) {
      longest = gap;
      longestAt = points[i - 1];
    }
  }
  const expected = Math.max(1, Math.round((to - from) / interval));
  const worstHours = byHour
    .map((count, hour) => ({ hour, count }))
    .filter((h) => h.count > 0)
    .sort((a, b) => b.count - a.count)
    .slice(0, 3)
    .map((h) => `${String(h.hour).padStart(2, '0')}:00 (${h.count})`)
    .join(', ');
  return {
    deliveredPct: round1(Math.max(0, 1 - missed / expected) * 100),
    reportsReceived: points.length - 2,
    reportsExpected: expected,
    gaps2min: gaps2,
    longestGapMin: round1(longest / MIN),
    longestGapStart: local(longestAt),
    worstHours: worstHours || 'none',
  };
}

// Running pieces -> cycles. Pieces closer than mergeGapMin are one cycle that "flapped".
function cyclesOf(records, now) {
  const pieces = [];
  let onSince = null;
  for (const r of records) {
    const t = ms(r.timestamp);
    if (r.running && onSince == null) onSince = t;
    else if (!r.running && onSince != null) {
      pieces.push({ start: onSince, end: t });
      onSince = null;
    }
  }
  if (onSince != null) pieces.push({ start: onSince, end: null });

  const cycles = [];
  for (const p of pieces) {
    const last = cycles[cycles.length - 1];
    if (last && last.end != null && p.start - last.end < RULES.mergeGapMin * MIN) {
      last.end = p.end;
      last.pieces++;
    } else {
      cycles.push({ start: p.start, end: p.end, pieces: 1 });
    }
  }
  for (const c of cycles) c.minutes = ((c.end ?? now) - c.start) / MIN;
  return cycles;
}

function median(values) {
  if (values.length === 0) return null;
  const s = [...values].sort((a, b) => a - b);
  const mid = Math.floor(s.length / 2);
  return s.length % 2 ? s[mid] : (s[mid - 1] + s[mid]) / 2;
}

function restartsOf(rows, since) {
  const boot = (r) => ms(r.timestamp) - r.uptime * 1000;
  const out = { planned: 0, unplanned: [], websiteResets: 0 };
  const withUptime = rows.filter((r) => typeof r.uptime === 'number');
  for (let i = 1; i < withUptime.length; i++) {
    const prev = withUptime[i - 1];
    const cur = withUptime[i];
    if (boot(cur) - boot(prev) < 2 * MIN || ms(cur.timestamp) < since) continue;  // same boot
    const ranHours = round1((boot(cur) - boot(prev)) / HOUR);
    const reason = cur.resetReason;
    if (reason === 3 && ranHours >= 23.5) out.planned++;
    else if (reason === 8) out.websiteResets++;
    else out.unplanned.push(`${local(cur.timestamp)} ${RESET_REASONS[reason] || `reason ${reason}`} (had run ${ranHours} h)`);
  }
  return out;
}

function sensorBadMinutes(rows, since, now) {
  let total = 0;
  let state = null;
  for (let i = 0; i < rows.length; i++) {
    const s = rows[i].sensorState;
    if (!s || s === 'ok') continue;
    const start = Math.max(ms(rows[i].timestamp), since);
    const end = i + 1 < rows.length ? ms(rows[i + 1].timestamp) : now;
    if (end > start) {
      total += end - start;
      state = s;
    }
  }
  return { minutes: Math.round(total / MIN), state };
}

// ---------- Analysis ----------

export function analyse(data, now, { thresholdsFor, allowDates }) {
  const since = now - WINDOW_MS;
  const findings = [];
  const add = (type, machine, detail) => findings.push({ type, machine, detail });
  const byId = (list, key) => list.reduce((acc, x) => ((acc[x[key]] ||= []).push(x), acc), {});

  const machines = Object.fromEntries(data.machines.map((m) => [m.machineId, m]));
  const diagBy = byId(data.diagnostics, 'machineId');
  const historyBy = byId(data.history, 'machineId');
  const telemetryBy = byId(data.telemetry, 'nodeId');
  const configBy = Object.fromEntries(data.configs.map((c) => [c.nodeId, c]));
  const flagged = new Set(data.activeFlags);

  const oldestAllow = allowDates.length ? allowDates.reduce((a, b) => (a < b ? a : b)) : null;
  const allowAgeDays = oldestAllow ? Math.floor((now - Date.parse(oldestAllow)) / DAY) : null;
  const allowNote = allowAgeDays != null && allowAgeDays >= RULES.allowListWarnDays
    ? ` The Wi-Fi allow list batch from ${oldestAllow} is ${allowAgeDays} days old (IST allows 1 year max), so the likely cause is the allow list expiring: renew it with IST before checking the board.`
    : '';

  const cycles = {};
  for (const n of PAIRS) {
    for (const id of [dryerId(n), washerId(n)]) cycles[id] = cyclesOf(historyBy[id] || [], now);
  }

  const pairs = [];
  const offline = new Set();
  const near = {};  // per machine: readings close to, but below, its start threshold

  for (const n of PAIRS) {
    const dId = dryerId(n);
    const wId = washerId(n);
    const nodeId = nodeIdOf(n);
    const dryer = machines[dId];
    const washer = machines[wId];
    const thresholds = thresholdsFor(nodeId, configBy[nodeId]);
    const pair = { pair: n };

    // Offline now
    const dryerSilentMin = dryer ? Math.round((now - ms(dryer.lastUpdate)) / MIN) : null;
    if (!dryer) {
      add('offline', dId, `No report from this pair's dryer board has ever been stored.${allowNote}`);
      offline.add(n);
    } else if (dryerSilentMin >= RULES.offlineMin) {
      add('offline', dId, `Dryer board silent for ${dryerSilentMin} min (last report ${local(dryer.lastUpdate)}).${allowNote}`);
      offline.add(n);
    }

    const washerSilentMin = washer ? Math.round((now - ms(washer.lastUpdate)) / MIN) : null;
    if (!offline.has(n) && (washerSilentMin == null || washerSilentMin >= RULES.washerUnheardMin)) {
      add('washer_unheard', wId, washer
        ? `Dryer board is reporting but hasn't heard its washer for ${washerSilentMin} min (last ${local(washer.lastUpdate)}).`
        : 'Dryer board is reporting but its washer has never been heard.');
    }

    // Sends
    const docs = telemetryBy[nodeId] || [];
    const times = docs.map((d) => ms(d.ts));
    const from = data.reportedBefore[nodeId] || times.length === 0 ? since : Math.min(...times);
    pair.sends = sendStats(times, dryer?.reportEveryS ?? 30, from, now);
    const expectedReadings = Math.max(1, (now - from) / 5000);
    pair.readings = {
      dryerPct: round1(Math.min(100, (docs.reduce((s, d) => s + d.nd, 0) / expectedReadings) * 100)),
      washerHeardPct: round1(Math.min(100, (docs.reduce((s, d) => s + d.nw, 0) / expectedReadings) * 100)),
    };
    if (!offline.has(n) && pair.sends.deliveredPct < RULES.minSendRate * 100) {
      add('low_send_rate', dId, `Only ${pair.sends.deliveredPct}% of expected reports arrived (${pair.sends.gaps2min} gaps of 2+ min, longest ${pair.sends.longestGapMin} min from ${pair.sends.longestGapStart}; worst hours ${pair.sends.worstHours}).`);
    }

    // Highest readings vs the threshold that starts a cycle
    for (const [id, key, threshold] of [[dId, 'dMax', thresholds.dryerOnThreshold], [wId, 'wMax', thresholds.washerFirstFill]]) {
      const values = docs.map((d) => d[key]).filter((v) => typeof v === 'number');
      near[id] = {
        threshold,
        highest: values.length ? round1(Math.max(...values)) : null,
        reportsNear: values.filter((v) => v >= RULES.nearThresholdShare * threshold && v < threshold).length,
      };
    }

    // Board health (dryer rows carry the board's uptime, reset reason and memory)
    const dRows = diagBy[dId] || [];
    pair.restarts = restartsOf(dRows, since);
    if (pair.restarts.unplanned.length > RULES.maxUnplannedRestarts) {
      add('unplanned_restarts', dId, `${pair.restarts.unplanned.length} unplanned restarts in 2 days: ${pair.restarts.unplanned.join('; ')}.`);
    }
    if (pair.restarts.websiteResets > RULES.maxWebsiteResets) {
      add('website_resets', dId, `${pair.restarts.websiteResets} resets because no report reached the website for 5 min.`);
    }
    const heaps = dRows.filter((r) => ms(r.timestamp) >= since && typeof r.freeHeap === 'number').map((r) => r.freeHeap);
    if (typeof dryer?.freeHeap === 'number') heaps.push(dryer.freeHeap);
    pair.freeHeapMinKB = heaps.length ? Math.round(Math.min(...heaps) / 1024) : null;
    if (heaps.length && Math.min(...heaps) < RULES.lowHeapBytes) {
      add('low_memory', dId, `Free memory fell to ${pair.freeHeapMinKB} KB.`);
    }

    // Per machine: sensor, cycles
    for (const [id, kind] of [[dId, 'dryer'], [wId, 'washer']]) {
      const m = machines[id];
      const bad = sensorBadMinutes(diagBy[id] || [], since, now);
      if (bad.minutes >= RULES.sensorBadMin) {
        add('sensor', id, `Sensor reported "${bad.state}" for ${bad.minutes} min in 2 days.`);
      }

      const all = cycles[id];
      const inWindow = all.filter((c) => c.start >= since);
      const done = inWindow.filter((c) => c.end != null);
      const lengths = done.map((c) => c.minutes);
      const flapping = inWindow.filter((c) => c.pieces >= RULES.flapPieces);
      const info = {
        lastReport: m ? local(m.lastUpdate) : null,
        sensor: m?.sensorState ?? null,
        running: m?.running ?? null,
        cycles: inWindow.length,
        medianMin: lengths.length ? Math.round(median(lengths)) : null,
        shortestMin: lengths.length ? Math.round(Math.min(...lengths)) : null,
        longestMin: lengths.length ? Math.round(Math.max(...lengths)) : null,
        flappingCycles: flapping.length,
        mostPiecesInOneCycle: inWindow.reduce((mx, c) => Math.max(mx, c.pieces), 0),
      };

      if (flapping.length >= RULES.flapCycles || info.mostPiecesInOneCycle >= RULES.flapPiecesSingle) {
        add('flapping', id, `${flapping.length} cycles switched off and back on ${RULES.flapPieces - 1}+ times (worst cycle in ${info.mostPiecesInOneCycle} pieces).`);
      }

      const ongoing = all.find((c) => c.end == null);
      if (kind === 'dryer') {
        const longOnes = done.filter((c) => c.minutes > RULES.dryerStuckMin);
        if (!offline.has(n) && ongoing && ongoing.minutes > RULES.dryerStuckMin) {
          add('stuck_running', id, `Running for ${Math.round(ongoing.minutes)} min since ${local(ongoing.start)}.`);
        } else if (longOnes.length) {
          add('stuck_running', id, `${longOnes.length} cycles longer than ${RULES.dryerStuckMin} min (longest ${info.longestMin} min).`);
        }
      } else {
        const fullMin = thresholds.washerFullTimerMs / MIN;
        const capMin = thresholds.washerMaxCycleMs / MIN;
        const tol = RULES.timerEndToleranceS / 60;
        const lastDone = all.filter((c) => c.end != null).slice(-RULES.timerEndOutOf);
        info.lastFinishedCycles = lastDone.length;
        info.endedAtFullTimer = lastDone.filter((c) => Math.abs(c.minutes - fullMin) <= tol).length;
        info.endedAtMaxCap = lastDone.filter((c) => Math.abs(c.minutes - capMin) <= tol).length;
        if (info.endedAtFullTimer >= RULES.timerEndMin) {
          add('washer_timer_25', id, `${info.endedAtFullTimer} of the last ${lastDone.length} cycles ended at exactly ${round1(fullMin)} min (the first-fill timer).`);
        }
        if (info.endedAtMaxCap >= RULES.timerEndMin) {
          add('washer_timer_cap', id, `${info.endedAtMaxCap} of the last ${lastDone.length} cycles ended at exactly ${round1(capMin)} min (the hard cap).`);
        }
        info.shortCycles = done.filter((c) => c.minutes < RULES.washerShortMin).length;
        if (info.shortCycles >= RULES.washerShortCount) {
          add('washer_short', id, `${info.shortCycles} cycles shorter than ${RULES.washerShortMin} min in 2 days.`);
        }
        if (!offline.has(n) && ongoing && ongoing.minutes > capMin + RULES.washerStuckExtraMin) {
          add('stuck_running', id, `Running for ${Math.round(ongoing.minutes)} min since ${local(ongoing.start)}.`);
        }
      }

      pair[kind] = { ...info, highestReading: near[id].highest, startThreshold: near[id].threshold, reportsNearThreshold: near[id].reportsNear };
    }

    // Saved thresholds not picked up
    const cfg = configBy[nodeId];
    if (!offline.has(n) && cfg?.thresholds && cfg.version && cfg.appliedVersion !== cfg.version
        && now - ms(cfg.updatedAt) > RULES.configPendingHours * HOUR) {
      add('config_pending', nodeId, `Thresholds saved ${local(cfg.updatedAt)} (version ${cfg.version}) not applied yet; the board is on version ${cfg.appliedVersion ?? 'none'}.`);
    }

    pairs.push(pair);
  }

  // No cycles for 3 days while the others are busy, and readings kept getting close to the start
  // threshold: missed cycles, not a machine that's simply out of use.
  const recentStarts = (id) => cycles[id].filter((c) => c.start >= now - NO_CYCLES_LOOKBACK_MS).length;
  const roomStarts = PAIRS.reduce((s, n) => s + recentStarts(dryerId(n)) + recentStarts(washerId(n)), 0);
  for (const n of PAIRS) {
    for (const id of [dryerId(n), washerId(n)]) {
      if (offline.has(n) || flagged.has(id) || !machines[id]) continue;
      if (roomStarts >= RULES.noCyclesRoomMin && recentStarts(id) === 0 && near[id].reportsNear >= RULES.nearThresholdReports) {
        add('no_cycles', id, `No cycles in 3 days while the other machines had ${roomStarts}, but in the last 2 days ${near[id].reportsNear} reports had readings at ${Math.round(RULES.nearThresholdShare * 100)}-100% of the start threshold ${near[id].threshold} (highest ${near[id].highest}).`);
      }
    }
  }

  // Two machines starting together again and again
  const ids = PAIRS.flatMap((n) => [dryerId(n), washerId(n)]);
  const startsIn = Object.fromEntries(ids.map((id) => [id, cycles[id].filter((c) => c.start >= since).map((c) => c.start)]));
  for (let i = 0; i < ids.length; i++) {
    for (let j = i + 1; j < ids.length; j++) {
      const a = startsIn[ids[i]];
      const b = startsIn[ids[j]];
      if (!a.length || !b.length) continue;
      const together = a.filter((t) => b.some((u) => Math.abs(t - u) <= RULES.copyWindowS * 1000)).length;
      if (together >= RULES.copyCount && together >= 0.5 * Math.min(a.length, b.length)) {
        add('copying', `${ids[i]} + ${ids[j]}`, `Started within ${RULES.copyWindowS} s of each other ${together} times in 2 days (${a.length} and ${b.length} cycles).`);
      }
    }
  }

  // Prank: every monitored machine reported broken at once
  const reportedBroken = ids.filter((id) => data.activeFlags.includes(id) || data.brokenReported.includes(id));
  if (reportedBroken.length === ids.length) {
    add('prank', 'room', `All ${ids.length} monitored machines were reported or flagged broken in 2 days.`);
  }

  return { pairs, findings, window: { from: local(since), to: local(now) } };
}

// ---------- AI summary ----------

const AI_INSTRUCTIONS = `You write the health summary for a laundry-room monitoring system. Follow every instruction exactly.

THE SYSTEM
- St. Jerome's University laundry room. Pair N = dryer sj-dN + washer sj-wN. Only the pairs in the input are monitored; say nothing about other pairs.
- One ESP32 board sits on each dryer. It reads the dryer's vibration sensor, hears its washer's board over Bluetooth, and uploads both machines over Wi-Fi every 30 s.
- Dryer "running" = vibration average above dryerOnThreshold. Dryer "empty" = a door jolt bigger than dryerDoorChange.
- Washer "running" starts at the first water fill (washerFirstFill). The cycle then ends on timers: the first-fill timer (washerFullTimerMs, normally 25:00 after the start), pushed out to about 10:00-10:30 after each refill detected (washerRefill), and never past the hard cap (washerMaxCycleMs, normally 42:00). A normal full washer cycle is about 34 min. A washer cycle cannot end before 25:00 unless the washer board restarted.
- All thresholds can be changed on the website page /test/nodes without reflashing.
- Each board restarts itself once every 24-30 h while idle ("planned"). Reset reasons: power-on = power was lost or replugged; brownout = power supply too weak; crash, watchdog, software restart = firmware problem or a stall; website-unreachable reset = no report reached the website for 5 min, so the board reset itself.
- Gaps of 2+ min in the evening (about 22:00-01:30) are normal on the busy residence Wi-Fi and are not a problem by themselves.

THE INPUT (JSON)
- window: the 2 days covered, local Waterloo time.
- pairs: numbers for each pair. sends.deliveredPct = share of expected reports that arrived. gaps2min = silences of 2+ min. readings = share of 5 s readings stored. restarts.unplanned lists each unplanned restart. For each machine, highestReading is its highest reading in 2 days, startThreshold the value that starts a cycle (dryerOnThreshold or washerFirstFill), and reportsNearThreshold how many 30 s reports came within 85-100% of it without crossing.
- findings: the problems found by fixed rules. Each has type, machine, detail.
- previous: the same key numbers from the previous summary, or null.

RULES FOR YOU
1. The findings list is the complete and only list of problems. Never add, drop, merge away or soften a finding. If findings is empty, there are no problems.
2. Use only numbers and times that appear in the input. Never invent or estimate a number, time or cause. If something is unknown, write "unknown".
3. Times are already local Waterloo time. Copy them as given.
4. Plain, short sentences. No emojis. No text before the first heading. Use exactly these headings, in this order:

### Verdict
One sentence: "No problems found." or "<N> problem(s) need action: <machines>."

### Problems and what to do
One bullet per finding: "**<machine>**: <what is wrong, with its numbers>. Likely cause: <cause>. Do: <action>." Take cause and action from the CAUSES list for its type. If there are no findings, write "None."

### Connection
2-4 bullets: the range of deliveredPct across the pairs, the worst pair, the hours with the most 2+ min gaps, and any pair that is silent.

### Cycles and detection
2-4 bullets: busiest and quietest machines, typical cycle lengths, how close idle readings came to the start thresholds, and anything odd that is not a finding (call it "worth watching", not a problem).

### Board health
1-3 bullets: planned vs unplanned restarts, sensors, lowest free memory.

### Trend since last summary
Compare deliveredPct, gaps2min, unplanned restarts and cycle counts with "previous": better, worse or the same, with both numbers. If previous is null, write "First summary, no trend yet."

CAUSES (cause; action) BY FINDING TYPE
- offline: board unpowered, unplugged, Wi-Fi problem or failed board; check the board's power supply and cables, replug it, and if it stays silent reflash or swap it. If the detail mentions the allow list age, the likely cause is the expired allow list instead; renew it with IST first.
- low_send_rate: weak Wi-Fi or busy network; make sure the board isn't behind the metal cabinet and has its own power supply, then compare with the other pairs.
- washer_unheard: washer board unpowered, loose cable, or out of Bluetooth range; check the washer board's power first.
- washer_timer_25: refills are not being detected (or the washer is falsely starting from neighbour noise); on /test/nodes lower washerRefill a little (or change washerRefillBands); if the cycles are false starts, raise washerFirstFill instead.
- washer_timer_cap: refills are detected that aren't real, so the cycle runs to the cap; raise washerRefill a little on /test/nodes.
- washer_short: the washer board restarted mid-cycle; check its power supply and cable.
- flapping: the reading dips below the threshold mid-cycle; for a dryer lower dryerOnThreshold a little, for a washer check its board's power and mounting.
- stuck_running: the reading stays above the threshold after the cycle (often a neighbour's vibration) or the sensor is stuck; raise the threshold a little and check the sensor mounting.
- copying: one machine's vibration or sound sets off the other; raise the threshold of the machine that follows the other, or add foam.
- no_cycles: the machine is being used but its readings stop just short of the start threshold; lower dryerOnThreshold (dryer) or washerFirstFill (washer) a little on /test/nodes, and check the sensor mounting.
- config_pending: the board hasn't picked up thresholds saved on /test/nodes; check that it is online, then save again.
- unplanned_restarts: power-on or brownout means a power problem (check the supply, splitter and cables); crash, watchdog or software restart means a firmware problem (note it for the developer).
- website_resets: the board couldn't reach the website for 5+ min several times; if many pairs show it, the Wi-Fi, allow list or website was down.
- sensor: sensor wiring loose or sensor failed; check the dryer's sensor cable or the washer's microphone.
- low_memory: possible firmware memory leak; note it for the developer (the daily restart clears it).
- prank: many machines reported broken at once, probably a prank; check the flags on the website.`;

async function geminiCall(path, key, body) {
  const res = await fetch(`${GEMINI_API}/${path}`, {
    method: body ? 'POST' : 'GET',
    headers: { 'x-goog-api-key': key, 'Content-Type': 'application/json' },
    body: body ? JSON.stringify(body) : undefined,
  });
  const text = await res.text();
  let json = null;
  try {
    json = JSON.parse(text);
  } catch {
    // handled below
  }
  if (!res.ok || !json) throw Object.assign(new Error(`${res.status} ${text.slice(0, 300)}`), { status: res.status, body: text });
  return json;
}

const sleep = (msDelay) => new Promise((resolve) => setTimeout(resolve, msDelay));

// Standard "gemini-<version>-flash" / "-flash-lite" models only, newest first, full Flash before Lite.
async function currentFlashModels(key) {
  const { models = [] } = await geminiCall('models?pageSize=200', key);
  const rank = (name) => {
    const m = /^gemini-(\d+(?:\.\d+)?)-flash(-lite)?$/.exec(name);
    return m ? Number(m[1]) * 10 - (m[2] ? 1 : 0) : -1;
  };
  return models
    .filter((m) => m.supportedGenerationMethods?.includes('generateContent'))
    .map((m) => m.name.replace(/^models\//, ''))
    .filter((name) => rank(name) >= 0)
    .sort((a, b) => rank(b) - rank(a))
    .slice(0, 3);
}

async function aiSummary(payload) {
  const key = process.env.GEMINI_API_KEY;
  if (!key) {
    console.error('GEMINI_API_KEY is not set; writing the plain summary.');
    return null;
  }
  const request = {
    systemInstruction: { parts: [{ text: AI_INSTRUCTIONS }] },
    contents: [{ role: 'user', parts: [{ text: JSON.stringify(payload) }] }],
    generationConfig: { temperature: 0.2, maxOutputTokens: 8192 },
  };

  const tried = new Set();
  const tryModels = async (models) => {
    const queue = [...models];
    while (queue.length) {
      const model = queue.shift();
      if (tried.has(model)) continue;
      tried.add(model);
      // Busy (503) or rate-limited (429): wait and retry the same model twice before moving on.
      for (let attempt = 0; attempt < 3; attempt++) {
        try {
          const json = await geminiCall(`models/${model}:generateContent`, key, request);
          const text = (json.candidates?.[0]?.content?.parts || []).map((p) => p.text || '').join('').trim()
            .replace(/^```(?:markdown)?\s*|\s*```$/g, '');
          if (text.startsWith('### Verdict')) {
            console.log(`AI summary written by ${model}.`);
            return text;
          }
          console.error(`AI model ${model} returned an unexpected format (finish reason ${json.candidates?.[0]?.finishReason ?? 'unknown'}).`);
          break;
        } catch (error) {
          console.error(`AI model ${model} failed: ${error.message}`);
          // A retired model's error names its replacement ("use models/gemini-..."): try that next.
          const replacement = error.status === 404 && /use (?:models\/)?(gemini-[\w.-]*\w)/i.exec(error.body || '');
          if (replacement && !tried.has(replacement[1])) queue.unshift(replacement[1]);
          if (![429, 503].includes(error.status) || attempt === 2) break;
          await sleep((attempt + 1) * 15000);
        }
      }
    }
    return null;
  };

  const text = await tryModels(AI_MODELS);
  if (text) return text;
  try {
    return await tryModels(await currentFlashModels(key));
  } catch (error) {
    console.error(`Listing Gemini models failed: ${error.message}`);
    return null;
  }
}

// ---------- Output ----------

function plainSummary(result) {
  const lines = ['### Verdict', result.findings.length
    ? `${result.findings.length} problem(s) need action: ${[...new Set(result.findings.map((f) => f.machine))].join(', ')}.`
    : 'No problems found.', '', '### Problems and what to do'];
  if (result.findings.length === 0) lines.push('None.');
  for (const f of result.findings) lines.push(`- **${f.machine}** (${f.type}): ${f.detail}`);
  lines.push('', '_(AI summary unavailable this time; plain summary written instead.)_');
  return lines.join('\n');
}

function numbersTable(pairs) {
  const cell = (v) => (v == null ? '-' : v);
  const rows = [
    '| Pair | Reports arrived | Gaps 2+ min | Longest gap | Unplanned restarts | Website resets | Dryer cycles | Washer cycles | Washer 25:00 / 42:00 ends |',
    '|---|---|---|---|---|---|---|---|---|',
  ];
  for (const p of pairs) {
    rows.push(`| ${p.pair} | ${p.sends.deliveredPct}% | ${p.sends.gaps2min} | ${p.sends.longestGapMin} min | ${p.restarts.unplanned.length} | ${p.restarts.websiteResets} | ${cell(p.dryer?.cycles)} | ${cell(p.washer?.cycles)} | ${p.washer ? `${p.washer.endedAtFullTimer} / ${p.washer.endedAtMaxCap} of ${p.washer.lastFinishedCycles}` : '-'} |`);
  }
  return rows.join('\n');
}

function keyStats(pairs) {
  return Object.fromEntries(pairs.map((p) => [p.pair, {
    deliveredPct: p.sends?.deliveredPct ?? null,
    gaps2min: p.sends?.gaps2min ?? null,
    unplannedRestarts: p.restarts.unplanned.length,
    dryerCycles: p.dryer?.cycles ?? null,
    washerCycles: p.washer?.cycles ?? null,
  }]));
}

async function createIssue(title, body) {
  const repo = process.env.GITHUB_REPOSITORY;
  const res = await fetch(`https://api.github.com/repos/${repo}/issues`, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${process.env.GITHUB_TOKEN}`,
      Accept: 'application/vnd.github+json',
      'X-GitHub-Api-Version': '2022-11-28',
    },
    body: JSON.stringify({ title, body: body.slice(0, 60000) }),
  });
  if (!res.ok) throw new Error(`Creating the alert issue failed: ${res.status} ${(await res.text()).slice(0, 300)}`);
  console.log(`Alert issue: ${(await res.json()).html_url}`);
}

// ---------- Main ----------

async function main() {
  const now = new Date();
  const due = isDue(now);

  if (process.argv.includes('--check-due')) {
    console.log(due ? 'Due: running the health check.' : `Not due: last summary is under ${RUN_EVERY_DAYS} days old.`);
    if (process.env.GITHUB_OUTPUT) appendFileSync(process.env.GITHUB_OUTPUT, `due=${due}\n`);
    return;
  }
  if (!due) {
    console.log(`Not due: last summary is under ${RUN_EVERY_DAYS} days old.`);
    return;
  }

  const dotenv = await import('dotenv');
  dotenv.config({ path: join(__dirname, '..', '.env'), quiet: true });
  const { connectToDatabase } = await import('../api/lib/mongodb.js');
  const { effectiveThresholds } = await import('../api/diagnostics.js');
  const dryRun = ['1', 'true'].includes(process.env.DRY_RUN);

  const { client, db } = await connectToDatabase();
  let data;
  try {
    data = await loadData(db, now);
  } finally {
    await client.close();
  }
  // None of the monitored machines at all means the wrong database, not every board offline.
  if (data.machines.length === 0) {
    throw new Error(`No monitored machines found in database "${db.databaseName}". Set MONGODB_DB in the workflow to the database the website uses.`);
  }

  const summaryText = readSummaryFile();
  const result = analyse(data, now.getTime(), { thresholdsFor: effectiveThresholds, allowDates: allowListDates() });
  const previous = previousStats(summaryText);
  const ai = await aiSummary({ window: result.window, pairs: result.pairs, findings: result.findings, previous });
  const summary = ai || plainSummary(result);
  const alert = result.findings.length > 0;

  const entry = [
    `## Health summary ${utcDay(now)}`,
    `Data: ${result.window.from} to ${result.window.to} (Waterloo time). Problems: ${result.findings.length}${alert ? ' (alert sent as a GitHub issue)' : ' (no alert)'}.`,
    '',
    summary,
    '',
    '<details><summary>Numbers</summary>',
    '',
    numbersTable(result.pairs),
    '',
    '</details>',
    '',
    `<!-- health-stats ${JSON.stringify(keyStats(result.pairs))} -->`,
  ].join('\n');

  let issueBody = null;
  if (alert) {
    const mention = process.env.NOTIFY_USER ? `@${process.env.NOTIFY_USER.replace(/^@/, '')} ` : '';
    const commitDays = daysSinceLastCommit(now);
    const inactivity = commitDays != null && commitDays >= RULES.inactivityWarnDays
      ? `\n> **Reminder:** the repo has had no commits for ${commitDays} days. GitHub pauses scheduled workflows after 60 days without activity. Open Actions > Laundry health check > Run workflow, tick "force", and run it: its summary commit counts as activity.\n`
      : '';
    issueBody = [
      `${mention}the laundry health check found **${result.findings.length} problem(s)** in the data from ${result.window.from} to ${result.window.to} (Waterloo time).`,
      inactivity,
      '**Problems (from the fixed rules):**',
      ...result.findings.map((f) => `- **${f.machine}** (${f.type}): ${f.detail}`),
      '',
      '---',
      '',
      summary,
      '',
      `Full history: health/HEALTH_SUMMARIES.md. Close this issue once handled.`,
    ].join('\n');
  }

  if (dryRun) {
    console.log(entry);
    if (issueBody) console.log(`\n===== ALERT =====\n${issueBody}`);
    return;
  }

  mkdirSync(dirname(SUMMARY_FILE), { recursive: true });
  const existing = summaryText.trimEnd();
  writeFileSync(SUMMARY_FILE, `${existing ? `${existing}\n\n` : ''}${entry}\n`);
  console.log(`Summary written (${result.findings.length} problem(s), AI ${ai ? 'used' : 'unavailable'}).`);

  if (issueBody) {
    await createIssue(`Laundry health check: ${result.findings.length} problem(s) (${utcDay(now)})`, issueBody);
  }
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  main().catch((error) => {
    console.error(error);
    process.exit(1);
  });
}
