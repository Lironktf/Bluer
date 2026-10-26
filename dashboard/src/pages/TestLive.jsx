import { useState, useEffect, useCallback, useMemo, useRef } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import PasswordGate from '../components/PasswordGate/PasswordGate';
import { adminFetch, formatAgo, formatRemaining, nodeLabel } from '../utils/adminApi';
import styles from './TestAdmin.module.css';

const POLL_INTERVAL_MS = 5000;
const SUMMARY_SECONDS = 15;
const WINDOWS = [5, 15, 60];

// One sample per second from the node, while test mode is on:
//   dAvg10        dryer avg10 (what the dryer-on threshold is compared to)
//   dDoorJumpMax  biggest rise of activitysmall over 0.15 s in that second (the door check compares this to
//                 the door-change threshold, and only while the dryer is not running)
//   wWater1Max    highest 7875-9750 Hz water band in that second
//   wWater2Max    highest 9750-11625 Hz water band in that second
//   wDoorDropMax  biggest one-step drop of the 60-600 Hz door band in that second
//   dRunning dEmpty wRunning wEmpty  states, 0/1

const fmt = (v, digits = 2) => (typeof v === 'number' ? v.toLocaleString(undefined, { maximumFractionDigits: digits }) : '--');

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

/** Simple time chart: lines for readings, dashed lines for thresholds, shading while running. */
function Chart({ title, samples, series, thresholds: allThresholds, runningKey, start, end }) {
  const W = 1000;
  const H = 200;
  const span = Math.max(end - start, 1);
  const thresholds = allThresholds.filter((t) => typeof t.value === 'number');

  const values = samples.flatMap((s) => series.map((d) => s[d.key])).filter((v) => typeof v === 'number');
  const yMax = Math.max(1e-6, ...values, ...thresholds.map((t) => t.value)) * 1.15;

  const x = (t) => ((t - start) / span) * W;
  const y = (v) => H - (Math.min(v, yMax) / yMax) * H;
  const secondWidth = Math.max((1000 / span) * W, 1);

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
      <svg viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none" role="img" aria-label={title}>
        {samples.filter((s) => s[runningKey] === 1).map((s) => (
          <rect key={s._id} x={x(new Date(s.ts).getTime())} y={0} width={secondWidth} height={H} className={styles.runShade} />
        ))}
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
      <div className={styles.axis}>
        <span>{new Date(start).toLocaleTimeString()}</span>
        <span>max shown {fmt(yMax, 0)}</span>
        <span>{new Date(end).toLocaleTimeString()}</span>
      </div>
    </figure>
  );
}

function Live({ lock }) {
  const [searchParams, setSearchParams] = useSearchParams();
  const nodeId = searchParams.get('node') || '';
  const [minutes, setMinutes] = useState(15);
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
    const query = `kind=telemetry&nodeId=${encodeURIComponent(nodeId)}&minutes=${minutes}`;
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
  }, [nodeId, minutes, lock]);

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
  const end = Date.now();
  const start = end - minutes * 60000;

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
        <label>
          Show last:{' '}
          <select value={minutes} onChange={(e) => setMinutes(Number(e.target.value))}>
            {WINDOWS.map((m) => (
              <option key={m} value={m}>{m} min</option>
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
            series={[
              { key: 'wWater1Max', label: 'water 7.9–9.8 kHz', color: '#2563eb' },
              { key: 'wWater2Max', label: 'water 9.8–11.6 kHz', color: '#0d9488' },
            ]}
            thresholds={[
              { label: 'first fill', value: t.washerFirstFill, color: '#dc2626' },
              { label: 'refill', value: t.washerRefill, color: '#d97706' },
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
      <PasswordGate>{({ lock }) => <Live lock={lock} />}</PasswordGate>
    </div>
  );
}
