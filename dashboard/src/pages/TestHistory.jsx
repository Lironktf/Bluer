import { useState, useEffect, useCallback } from 'react';
import { Link } from 'react-router-dom';
import styles from './TestHistory.module.css';

const API_BASE_URL = import.meta.env.VITE_API_URL || 'https://laun-dryer.vercel.app';
const POLL_INTERVAL_MS = 60000;

const RESET_REASONS = {
  0: 'unknown',
  1: 'power-on',
  2: 'external',
  3: 'software',
  4: 'panic',
  5: 'int watchdog',
  6: 'task watchdog',
  7: 'other watchdog',
  8: 'deep sleep',
  9: 'BROWNOUT',
  10: 'SDIO',
};

function formatUptime(seconds) {
  if (seconds === null || seconds === undefined) return '--';
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  if (d > 0) return `${d}d ${h}h ${m}m`;
  if (h > 0) return `${h}h ${m}m`;
  return `${m}m`;
}

function formatHeap(bytes) {
  if (bytes === null || bytes === undefined) return '--';
  return Math.round(bytes / 1024);
}

function formatStamp(iso) {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return '--';
  return d.toLocaleString(undefined, {
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
  });
}

export default function TestHistory() {
  const [records, setRecords] = useState([]);
  const [machineFilter, setMachineFilter] = useState('');
  const [fetchedAt, setFetchedAt] = useState(null);
  const [error, setError] = useState(null);

  const refresh = useCallback(async () => {
    try {
      const url = new URL(`${API_BASE_URL}/api/diagnostics`);
      url.searchParams.set('limit', '20000');
      if (machineFilter) url.searchParams.set('machineId', machineFilter);

      const response = await fetch(url);
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const data = await response.json();
      if (!data.success) throw new Error(data.error || 'Request failed');

      setRecords(data.records || []);
      setFetchedAt(new Date());
      setError(null);
    } catch (e) {
      setError(e.message);
    }
  }, [machineFilter]);

  useEffect(() => {
    refresh();
    const interval = setInterval(refresh, POLL_INTERVAL_MS);
    return () => clearInterval(interval);
  }, [refresh]);

  const machineIds = [...new Set(records.map((r) => r.machineId))].sort();

  // Oldest first so the table reads top-to-bottom as the trial progressed.
  const rows = [...records].reverse();

  // A node rebooted between two samples if its uptime went backwards.
  const rebootRowIds = new Set();
  const lastUptime = {};
  for (const r of rows) {
    const prev = lastUptime[r.machineId];
    if (typeof r.uptime === 'number') {
      if (typeof prev === 'number' && r.uptime < prev) {
        rebootRowIds.add(r._id);
      }
      lastUptime[r.machineId] = r.uptime;
    }
  }

  return (
    <div className={styles.page}>
      <h1>Firmware trial log</h1>
      <p className={styles.sub}>
        Every heartbeat received from the nodes under trial, oldest first. Refreshes every{' '}
        {POLL_INTERVAL_MS / 1000}s.
        {fetchedAt && ` Last fetch ${fetchedAt.toLocaleTimeString()}.`}{' '}
        <Link to="/test">Back to live view</Link>
      </p>

      <div className={styles.controls}>
        <label>
          Machine:{' '}
          <select value={machineFilter} onChange={(e) => setMachineFilter(e.target.value)}>
            <option value="">all</option>
            {machineIds.map((id) => (
              <option key={id} value={id}>
                {id}
              </option>
            ))}
          </select>
        </label>
        <span className={styles.count}>{rows.length} samples</span>
      </div>

      {error && <p className={styles.error}>Could not load: {error}</p>}
      {!error && rows.length === 0 && (
        <p className={styles.empty}>
          No samples yet. Nodes must be listed in DIAGNOSTIC_MACHINE_IDS in api/machines.js.
        </p>
      )}

      {rows.length > 0 && (
        <div className={styles.tableWrap}>
          <table className={styles.table}>
            <thead>
              <tr>
                <th>Time</th>
                <th>Machine</th>
                <th>Running</th>
                <th>Empty</th>
                <th>Sensor</th>
                <th>Last reset</th>
                <th>Uptime</th>
                <th>Heap KB</th>
                <th>Room</th>
              </tr>
            </thead>
            <tbody>
              {rows.map((r) => {
                const brownout = r.resetReason === 9;
                const sensorBad = r.sensorOk === false;
                const rebooted = rebootRowIds.has(r._id);

                return (
                  <tr key={r._id} className={rebooted ? styles.reboot : undefined}>
                    <td>{formatStamp(r.timestamp)}</td>
                    <td className={styles.id}>{r.machineId}</td>
                    <td>{r.running ? 'yes' : 'no'}</td>
                    <td>{r.empty ? 'yes' : 'no'}</td>
                    <td className={sensorBad ? styles.bad : undefined}>
                      {r.sensorOk === null || r.sensorOk === undefined
                        ? '--'
                        : r.sensorOk
                          ? 'ok'
                          : 'FAULT'}
                    </td>
                    <td className={brownout ? styles.bad : undefined}>
                      {r.resetReason === null || r.resetReason === undefined
                        ? '--'
                        : `${RESET_REASONS[r.resetReason] || r.resetReason} (${r.resetReason})`}
                    </td>
                    <td>{formatUptime(r.uptime)}</td>
                    <td>{formatHeap(r.freeHeap)}</td>
                    <td className={styles.room}>{r.room || '--'}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      )}

      <p className={styles.note}>
        Highlighted rows are samples where uptime went backwards, meaning the node rebooted
        since the previous heartbeat. Select all and copy to export.
      </p>
    </div>
  );
}
