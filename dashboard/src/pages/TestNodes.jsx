import { useState, useEffect, useCallback, useRef } from 'react';
import { Link } from 'react-router-dom';
import { usePolling } from '../hooks/usePolling';
import { adminFetch, formatAgo, nodeLabel } from '../utils/adminApi';
import PasswordGate from '../components/PasswordGate/PasswordGate';
import styles from './TestAdmin.module.css';

const POLL_INTERVAL_MS = 15000;

// Timers are stored in ms on the node; shown here in minutes.
const FIELDS = [
  { key: 'dryerOnThreshold', label: 'Dryer on (avg10)', step: 0.05 },
  { key: 'dryerDoorChange', label: 'Dryer door change', step: 0.05 },
  { key: 'washerFirstFill', label: 'Washer first fill', step: 500 },
  { key: 'washerRefill', label: 'Washer refill', step: 500 },
  { key: 'washerRefillBands', label: 'Refill bands (1 = 7.9–9.8 kHz, 2 = either, 3 = 9.8–11.6 kHz)', step: 1 },
  { key: 'washerDoorDrop', label: 'Washer door drop', step: 10000 },
  { key: 'washerRefillTimerMs', label: 'Refill timer (min)', minutes: true, step: 0.5 },
  { key: 'washerFullTimerMs', label: 'First-fill timer (min)', minutes: true, step: 0.5 },
  { key: 'washerMaxCycleMs', label: 'Max cycle (min)', minutes: true, step: 0.5 },
];

const toDisplay = (field, value) => (field.minutes ? Math.round((value / 60000) * 100) / 100 : value);
const toStored = (field, value) => (field.minutes ? Math.round(Number(value) * 60000) : Number(value));

function formatValue(key, value) {
  const field = FIELDS.find((f) => f.key === key);
  if (!field) return String(value);
  return field.minutes ? `${toDisplay(field, value)} min` : String(value);
}

function NodeCard({ node, limits, onSaved, onUnauthorized }) {
  const initial = Object.fromEntries(FIELDS.map((f) => [f.key, String(toDisplay(f, node.thresholds[f.key]))]));
  const [values, setValues] = useState(initial);
  const [edited, setEdited] = useState(false);
  const [saving, setSaving] = useState(false);
  const [message, setMessage] = useState(null);

  const dirty = edited && FIELDS.some((f) => values[f.key] !== initial[f.key]);

  // Pick up server changes unless the user is mid-edit.
  const initialKey = JSON.stringify(initial);
  useEffect(() => {
    if (!edited) setValues(JSON.parse(initialKey));
  }, [initialKey, edited]);

  const save = async () => {
    setSaving(true);
    setMessage(null);
    // An untouched field keeps its exact stored value, so minute rounding can't drift it.
    const thresholds = Object.fromEntries(
      FIELDS.map((f) => [f.key, values[f.key] === initial[f.key] ? node.thresholds[f.key] : toStored(f, values[f.key])])
    );
    const { ok, status, data } = await adminFetch('kind=config', { method: 'PUT', body: { nodeId: node.nodeId, thresholds } });
    if (status === 401) return onUnauthorized();
    if (ok) {
      await onSaved();
      setEdited(false);
      setMessage(data.unchanged ? 'No changes.' : `Saved as version ${data.version}. The node picks it up on its next report.`);
    } else {
      setMessage(data?.error || 'Save failed.');
    }
    setSaving(false);
  };

  let status;
  if (!node.saved) status = <span className={styles.muted}>Built-in values (nothing saved yet)</span>;
  else if (node.appliedVersion === node.version) status = <span className={styles.ok}>v{node.version} applied {formatAgo(node.appliedAt)}</span>;
  else {
    status = (
      <span className={styles.pending}>
        v{node.version} waiting for the node{node.appliedVersion ? ` (running v${node.appliedVersion})` : ''}
      </span>
    );
  }

  return (
    <section className={styles.card}>
      <header className={styles.cardHeader}>
        <div>
          <h2>{nodeLabel(node.nodeId)}</h2>
          <p className={styles.muted}>
            <span className={styles.id}>{node.nodeId}</span> · last report {formatAgo(node.lastSeen)} · {status}
          </p>
        </div>
        <div className={styles.nodeLinks}>
          <Link to={`/test/live?node=${encodeURIComponent(node.nodeId)}`}>Live readings</Link>
        </div>
      </header>

      <div className={styles.fields}>
        {FIELDS.map((f) => (
          <label key={f.key} className={styles.field}>
            <span>{f.label}</span>
            <input
              type="number"
              step={f.step}
              min={limits?.[f.key] ? toDisplay(f, limits[f.key][0]) : undefined}
              max={limits?.[f.key] ? toDisplay(f, limits[f.key][1]) : undefined}
              value={values[f.key]}
              onChange={(e) => {
                setEdited(true);
                setValues((v) => ({ ...v, [f.key]: e.target.value }));
              }}
              className={dirty && values[f.key] !== initial[f.key] ? styles.changed : undefined}
            />
          </label>
        ))}
      </div>

      <div className={styles.actions}>
        <button type="button" className={styles.primary} onClick={save} disabled={saving || (node.saved && !dirty)}>
          {saving ? 'Saving…' : node.saved ? 'Save changes' : 'Save (node starts following these)'}
        </button>
        {edited && (
          <button type="button" onClick={() => { setEdited(false); setValues(initial); }}>Undo edits</button>
        )}
        {message && <span className={styles.muted}>{message}</span>}
      </div>

      {node.history.length > 0 && (
        <details className={styles.history}>
          <summary>Change history ({node.history.length})</summary>
          <ul>
            {node.history.map((h) => (
              <li key={h._id}>
                <strong>v{h.version}</strong> {new Date(h.changedAt).toLocaleString()}:{' '}
                {h.changes.length === 0
                  ? 'first save, values unchanged'
                  : h.changes.map((c) => `${c.key} ${formatValue(c.key, c.from)} → ${formatValue(c.key, c.to)}`).join(', ')}
              </li>
            ))}
          </ul>
        </details>
      )}
    </section>
  );
}

function NodeList({ lock }) {
  const [nodes, setNodes] = useState([]);
  const [limits, setLimits] = useState(null);
  const [reportEveryS, setReportEveryS] = useState(30);
  const [error, setError] = useState(null);
  const [fetchedAt, setFetchedAt] = useState(null);
  const seqRef = useRef(0);

  const refresh = useCallback(async () => {
    // Only the newest request may update the page, so a slow poll can't undo a save.
    const seq = ++seqRef.current;
    const { ok, status, data } = await adminFetch('kind=nodes');
    if (seq !== seqRef.current) return;
    if (status === 401) return lock();
    if (ok) {
      setNodes(data.nodes || []);
      setLimits(data.limits || null);
      if (typeof data.reportEveryS === 'number') setReportEveryS(data.reportEveryS);
      setFetchedAt(new Date());
      setError(null);
    } else {
      setError(data?.error || `HTTP ${status}`);
    }
  }, [lock]);

  usePolling(refresh, POLL_INTERVAL_MS);

  return (
    <>
      <p className={styles.sub}>
        These are the exact thresholds each node follows. A node picks up saved changes in the reply to its next report,
        within about {reportEveryS} seconds.
        {fetchedAt && ` Last fetch ${fetchedAt.toLocaleTimeString()}.`}
      </p>
      {error && <p className={styles.error}>Could not load: {error}</p>}
      {nodes.map((node) => (
        <NodeCard key={node.nodeId} node={node} limits={limits} onSaved={refresh} onUnauthorized={lock} />
      ))}
    </>
  );
}

export default function TestNodes() {
  return (
    <div className={styles.page}>
      <h1>Node settings</h1>
      <p className={styles.sub}>
        <Link to="/test">Sensor diagnostics</Link> · <Link to="/test/live">Live readings</Link>
      </p>
      <PasswordGate>{({ lock }) => <NodeList lock={lock} />}</PasswordGate>
    </div>
  );
}
