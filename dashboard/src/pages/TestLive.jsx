import { useState, useEffect, useCallback, useMemo, useRef } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { adminFetch, formatAgo, formatRemaining, nodeLabel } from '../utils/adminApi';
import styles from './TestAdmin.module.css';

const POLL_INTERVAL_MS = 5000;
const publicPageLock = () => {};
const SUMMARY_SECONDS = 15;
const FETCH_MINUTES = 15; // the table covers this; graphs show 1/10 of it
const CHART_MS = (FETCH_MINUTES * 60000) / 10;
const DRYER_Y_CAP = 15;
const WASHER_Y_CAP = 150000;
const DOOR_Y_CAP = 1000000;

// One sample per second from the node, while test mode is on:
//   dAvg10        dryer avg10 (what the dryer-on threshold is compared to)
//   dDoorJumpMax  biggest rise of activitysmall over 0.15 s in that second (the door check compares this to
//                 the door-change threshold, and only while the dryer is not running)
//   wWater1Max    highest 7875-9750 Hz water band in that second
//   wWater2Max    highest 9750-11625 Hz water band in that second
//   wDoorDropMax  biggest one-step drop of the 60-600 Hz door band in that second
//   dRunning dEmpty wRunning wEmpty  states, 0/1

const fmt = (v, digits = 2) => (typeof v === 'number' ? v.toLocaleString(undefined, { maximumFractionDigits: digits }) : '--');

// Merges consecutive running seconds into one block, so the shading has no seams.
function runSpans(samples, key) {
  const spans = [];
  let current = null;
  for (const s of samples) {
    const t = new Date(s.ts).getTime();
    if (s[key] !== 1) {
      current = null;
    } else if (current && t - current.end <= 1500) {
      current.end = t + 1000;
    } else {
      current = { start: t, end: t + 1000 };
      spans.push(current);
    }
  }
  return spans;
}

function maxOf(rows, key) {
  const values = rows.map((r) => r[key]).filter((v) => typeof v === 'number');
  return values.length ? Math.max(...values) : undefined;
}

function avgOf(rows, key) {
  const values = rows.map((r) => r[key]).filter((v) => typeof v === 'number');
  return values.length ? values.reduce((a, b) => a + b, 0) / values.length : undefined;
}

function stateOf(rows, key) {
  const values = rows.map((r) => r[key]).filter((v) => typeof v === 'number');
  if (!values.length) return '--';
  const first = values[0] ? 'on' : 'off';
  const last = values[values.length - 1] ? 'on' : 'off';
  return first === last ? last : `${first} → ${last}`;
}

function summarize(samples) {
  const buckets = new Map();
  for (const s of samples) {
    const t = new Date(s.ts).getTime();
    const key = Math.floor(t / (SUMMARY_SECONDS * 1000));
    if (!buckets.has(key)) buckets.set(key, []);
    buckets.get(key).push(s);
  }
  return [...buckets.entries()]
    .sort((a, b) => b[0] - a[0])
    .map(([key, rows]) => ({
      start: new Date(key * SUMMARY_SECONDS * 1000),
      count: rows.length,
      dAvg: avgOf(rows, 'dAvg10'),
      dMax: maxOf(rows, 'dAvg10'),
      dDoorJump: maxOf(rows, 'dDoorJumpMax'),
      dRunning: stateOf(rows, 'dRunning'),
      dEmpty: stateOf(rows, 'dEmpty'),
      w1: maxOf(rows, 'wWater1Max'),
      w2: maxOf(rows, 'wWater2Max'),
      doorDrop: maxOf(rows, 'wDoorDropMax'),
      wRunning: stateOf(rows, 'wRunning'),
      wEmpty: stateOf(rows, 'wEmpty'),
    }));
}

/** Simple time chart: lines for readings, dashed lines for thresholds, shading while running.
 *  With yCap, the axis is fixed and anything higher is drawn flat along the top. */
function niceStep(raw) {
  const pow = 10 ** Math.floor(Math.log10(raw));
  const n = raw / pow;
  return (n <= 1 ? 1 : n <= 2 ? 2 : n <= 2.5 ? 2.5 : n <= 5 ? 5 : 10) * pow;
}

const fmtTick = (v) => (v >= 1e6 ? `${fmt(v / 1e6, 2)}M` : v >= 1000 ? `${fmt(v / 1000, 1)}k` : fmt(v, 2));

function Chart({ title, samples: allSamples, series, thresholds: allThresholds, runningKey, start, end, yCap }) {
  const W = 1000;
  const H = 200;
  const TOP = 4; // keeps a line capped at the top edge visible
  const span = Math.max(end - start, 1);
  const thresholds = allThresholds.filter((t) => typeof t.value === 'number');
  const samples = allSamples.filter((s) => new Date(s.ts).getTime() >= start - 1000);

  const values = samples.flatMap((s) => series.map((d) => s[d.key])).filter((v) => typeof v === 'number');
  const yMax = yCap ?? Math.max(1e-6, ...values, ...thresholds.map((t) => t.value)) * 1.15;

  const x = (t) => ((t - start) / span) * W;
  const y = (v) => H - (Math.min(v, yMax) / yMax) * (H - TOP);
  const step = yCap ? yCap / 10 : niceStep(yMax / 5);
  const ticks = Array.from({ length: Math.floor(yMax / step + 1e-9) + 1 }, (_, i) => i * step);

  return (
    <figure className={styles.chart}>
      <figcaption>
        {title}
        <span className={styles.legend}>
          {series.map((d) => (
            <span key={d.key}><i style={{ background: d.color }} />{d.label}</span>
          ))}
          {thresholds.map((t) => (
            <span key={t.label}><i className={styles.dashKey} style={{ borderColor: t.color }} />{t.label} {fmt(t.value)}</span>
          ))}
          <span><i className={styles.runKey} />running</span>
        </span>
      </figcaption>
      <div className={styles.plot}>
        <div className={styles.yAxis} aria-hidden="true">
          {ticks.map((v) => (
            <span key={v} style={{ top: `${(y(v) / H) * 100}%` }}>{fmtTick(v)}</span>
          ))}
        </div>
        <svg viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none" role="img" aria-label={title}>
          {ticks.map((v) => (
            <line key={v} x1={0} x2={W} y1={y(v)} y2={y(v)} className={styles.gridLine} vectorEffect="non-scaling-stroke" />
          ))}
          {runSpans(samples, runningKey).map((r) => {
            const x0 = x(Math.max(r.start, start));
            return <rect key={r.start} x={x0} y={0} width={Math.max(x(Math.min(r.end, end)) - x0, 1)} height={H} className={styles.runShade} />;
          })}
          {thresholds.map((t) => (
            <line key={t.label} x1={0} x2={W} y1={y(t.value)} y2={y(t.value)} stroke={t.color} strokeDasharray="8 6" strokeWidth={1.5} vectorEffect="non-scaling-stroke" />
          ))}
          {series.map((d) => (
            <polyline
              key={d.key}
              fill="none"
              stroke={d.color}
              strokeWidth={1.5}
              vectorEffect="non-scaling-stroke"
              points={samples
                .filter((s) => typeof s[d.key] === 'number')
                .map((s) => `${x(new Date(s.ts).getTime())},${y(s[d.key])}`)
                .join(' ')}
            />
          ))}
        </svg>
      </div>
      <div className={styles.axis}>
        <span>{new Date(start).toLocaleTimeString()}</span>
        <span>{yCap ? `top ${fmt(yCap, 0)} (higher values drawn flat at the top)` : `max shown ${fmt(yMax, 0)}`}</span>
        <span>{new Date(end).toLocaleTimeString()}</span>
      </div>
    </figure>
  );
}

function Live({ lock }) {
  const [searchParams, setSearchParams] = useSearchParams();
  const nodeId = searchParams.get('node') || '';
  const [nodes, setNodes] = useState([]);
  const [data, setData] = useState(null);
  const [error, setError] = useState(null);
  const [busy, setBusy] = useState(false);
  const currentQuery = useRef('');

  const loadNodes = useCallback(async () => {
    const { ok, status, data: d } = await adminFetch('kind=nodes');
    if (status === 401) return lock();
    if (ok) setNodes(d.nodes || []);
  }, [lock]);

  const loadTelemetry = useCallback(async () => {
    if (!nodeId) return;
    const query = `kind=telemetry&nodeId=${encodeURIComponent(nodeId)}&minutes=${FETCH_MINUTES}`;
    currentQuery.current = query;
    const { ok, status, data: d } = await adminFetch(query);
    // A reply for a node or window the user has since switched away from is dropped.
    if (currentQuery.current !== query) return;
    if (status === 401) return lock();
    if (ok) {
      setData(d);
      setError(null);
    } else {
      setError(d?.error || `HTTP ${status}`);
    }
  }, [nodeId, lock]);

  useEffect(() => {
    loadNodes();
  }, [loadNodes]);

  useEffect(() => {
    setData(null);
    loadTelemetry();
    const interval = setInterval(loadTelemetry, POLL_INTERVAL_MS);
    return () => clearInterval(interval);
  }, [loadTelemetry]);

  const samples = useMemo(() => data?.samples || [], [data]);
  const summaries = useMemo(() => summarize(samples), [samples]);
  const t = data?.thresholds || {};
  const latest = samples[samples.length - 1];
  // Fills left to right from the first reading, then scrolls with the newest one.
  const firstTs = samples.length ? new Date(samples[0].ts).getTime() : Date.now();
  const lastTs = latest ? new Date(latest.ts).getTime() + 1000 : Date.now();
  const end = Math.max(firstTs + CHART_MS, lastTs);
  const start = end - CHART_MS;

  const setTestMode = async (on) => {
    setBusy(true);
    const { ok, status, data: d } = await adminFetch('kind=testmode', { method: 'PUT', body: { nodeId, on, minutes: 60 } });
    if (status === 401) return lock();
    if (!ok) setError(d?.error || 'Could not change test mode.');
    await Promise.all([loadNodes(), loadTelemetry()]);
    setBusy(false);
  };

  const over = (value, limit) => (typeof value === 'number' && typeof limit === 'number' && value > limit ? styles.over : undefined);
  // First fill counts on either band; refill only on band 2 when the node is set to 2 bands.
  const waterClass = (value, band) => {
    const refillCounts = band === 1 || t.washerRefillBands !== 1;
    return over(value, t.washerFirstFill)
      || (refillCounts && typeof value === 'number' && value > t.washerRefill ? styles.warn : undefined);
  };

  return (
    <>
      <div className={styles.controls}>
        <label>
          Node:{' '}
          <select value={nodeId} onChange={(e) => setSearchParams(e.target.value ? { node: e.target.value } : {})}>
            <option value="">choose…</option>
            {nodes.map((n) => (
              <option key={n.nodeId} value={n.nodeId}>
                {nodeLabel(n.nodeId)}{n.testMode ? ' (test mode on)' : ''}
              </option>
            ))}
          </select>
        </label>
      </div>

      {!nodeId && <p className={styles.muted}>Choose a node. Start test mode for it here or on Node settings.</p>}
      {error && <p className={styles.error}>Could not load: {error}</p>}

      {nodeId && data && (
        <>
          <div className={styles.statusRow}>
            {data.testMode ? (
              <>
                <span className={styles.liveBadge}>TEST MODE ON · {formatRemaining(data.testModeUntil)}</span>
                <button type="button" onClick={() => setTestMode(false)} disabled={busy}>Stop test mode</button>
              </>
            ) : (
              <>
                <span className={styles.muted}>Test mode is off, so the node is not sending readings.</span>
                <button type="button" onClick={() => setTestMode(true)} disabled={busy}>Start test mode (60 min)</button>
              </>
            )}
            <span className={styles.muted}>
              {latest ? `Last reading ${formatAgo(latest.ts)}` : 'No readings in this window yet.'} The node starts
              sending within about 5 minutes of test mode turning on, then every ~15 s.
            </span>
          </div>

          {latest && (
            <div className={styles.latest}>
              <div><span>Dryer avg10</span><strong className={over(latest.dAvg10, t.dryerOnThreshold)}>{fmt(latest.dAvg10)}</strong></div>
              <div><span>Dryer door jump (max)</span><strong className={over(latest.dDoorJumpMax, t.dryerDoorChange)}>{fmt(latest.dDoorJumpMax)}</strong></div>
              <div><span>Dryer</span><strong>{latest.dRunning ? 'running' : 'idle'} · {latest.dEmpty ? 'empty' : 'full'}</strong></div>
              <div><span>Water 7.9–9.8 kHz</span><strong className={waterClass(latest.wWater1Max, 1)}>{fmt(latest.wWater1Max, 0)}</strong></div>
              <div><span>Water 9.8–11.6 kHz</span><strong className={waterClass(latest.wWater2Max, 2)}>{fmt(latest.wWater2Max, 0)}</strong></div>
              <div><span>Door drop (max)</span><strong className={over(latest.wDoorDropMax, t.washerDoorDrop)}>{fmt(latest.wDoorDropMax, 0)}</strong></div>
              <div><span>Washer</span><strong>{latest.wRunning ? 'running' : 'idle'} · {latest.wEmpty ? 'empty' : 'full'}</strong></div>
            </div>
          )}

          <Chart
            title="Dryer"
            samples={samples}
            start={start}
            end={end}
            runningKey="dRunning"
            yCap={DRYER_Y_CAP}
            series={[
              { key: 'dAvg10', label: 'avg10', color: '#2563eb' },
              { key: 'dDoorJumpMax', label: 'door jump', color: '#7c3aed' },
            ]}
            thresholds={[
              { label: 'dryer on', value: t.dryerOnThreshold, color: '#dc2626' },
              { label: 'door change', value: t.dryerDoorChange, color: '#7c3aed' },
            ]}
          />
          <Chart
            title="Washer"
            samples={samples}
            start={start}
            end={end}
            runningKey="wRunning"
            yCap={WASHER_Y_CAP}
            series={[
              { key: 'wWater1Max', label: 'water 7.9–9.8 kHz', color: '#2563eb' },
              { key: 'wWater2Max', label: 'water 9.8–11.6 kHz', color: '#0d9488' },
            ]}
            thresholds={[
              { label: 'first fill', value: t.washerFirstFill, color: '#dc2626' },
              { label: 'refill', value: t.washerRefill, color: '#d97706' },
            ]}
          />
          <Chart
            title="Washer door"
            samples={samples}
            start={start}
            end={end}
            runningKey="wRunning"
            yCap={DOOR_Y_CAP}
            series={[
              { key: 'wDoorDropMax', label: '60–600 Hz drop (max per second)', color: '#7c3aed' },
            ]}
            thresholds={[
              { label: 'door drop (marks empty)', value: t.washerDoorDrop, color: '#dc2626' },
            ]}
          />

          <h2 className={styles.section}>Every {SUMMARY_SECONDS} seconds (newest first)</h2>
          <div className={styles.tableWrap}>
            <table className={styles.table}>
              <thead>
                <tr>
                  <th>Time</th>
                  <th>Dryer avg10 (avg / max)</th>
                  <th>Dryer door jump max</th>
                  <th>Dryer running</th>
                  <th>Dryer empty</th>
                  <th>Water 7.9–9.8k max</th>
                  <th>Water 9.8–11.6k max</th>
                  <th>Door drop max</th>
                  <th>Washer running</th>
                  <th>Washer empty</th>
                  <th>Samples</th>
                </tr>
              </thead>
              <tbody>
                {summaries.map((s) => (
                  <tr key={s.start.getTime()}>
                    <td>{s.start.toLocaleTimeString()}</td>
                    <td className={over(s.dMax, t.dryerOnThreshold)}>{fmt(s.dAvg)} / {fmt(s.dMax)}</td>
                    <td className={over(s.dDoorJump, t.dryerDoorChange)}>{fmt(s.dDoorJump)}</td>
                    <td>{s.dRunning}</td>
                    <td>{s.dEmpty}</td>
                    <td className={waterClass(s.w1, 1)}>{fmt(s.w1, 0)}</td>
                    <td className={waterClass(s.w2, 2)}>{fmt(s.w2, 0)}</td>
                    <td className={over(s.doorDrop, t.washerDoorDrop)}>{fmt(s.doorDrop, 0)}</td>
                    <td>{s.wRunning}</td>
                    <td>{s.wEmpty}</td>
                    <td className={styles.muted}>{s.count}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <p className={styles.note}>
            Red: above the threshold that triggers (dryer on, dryer door change, washer first fill, washer door drop). A
            dryer door jump only marks it empty while the dryer is not running. Amber: above the
            washer refill threshold. Thresholds shown are the node&apos;s saved values from Node settings.
          </p>
        </>
      )}
    </>
  );
}

export default function TestLive() {
  return (
    <div className={styles.page}>
      <h1>Live readings</h1>
      <p className={styles.sub}>
        Per-second readings from a node in test mode, summarized every {SUMMARY_SECONDS} s. Refreshes every{' '}
        {POLL_INTERVAL_MS / 1000}s. <Link to="/test/nodes">Node settings</Link> · <Link to="/test">Sensor diagnostics</Link>
      </p>
      <Live lock={publicPageLock} />
    </div>
  );
}
