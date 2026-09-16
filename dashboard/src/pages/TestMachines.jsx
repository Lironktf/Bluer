import { useState, useEffect, useCallback } from 'react';
import styles from './TestMachines.module.css';

const API_BASE_URL = import.meta.env.VITE_API_URL || 'https://laun-dryer.vercel.app';
const POLL_INTERVAL_MS = 5000;

// esp_reset_reason_t. 9 means the supply dipped; 3 means our own watchdog fired.
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

async function fetchJson(url) {
  try {
    const response = await fetch(url);
    if (!response.ok) return null;
    const contentType = response.headers.get('content-type') || '';
    if (!contentType.includes('application/json')) return null;
    return await response.json();
  } catch (error) {
    console.error(`Request to ${url} failed:`, error);
    return null;
  }
}

function formatAge(ms) {
  if (ms === null || ms === undefined) return '--';
  const s = Math.round(ms / 1000);
  if (s < 90) return `${s}s`;
  const m = Math.round(s / 60);
  if (m < 90) return `${m}m`;
  const h = Math.round(m / 60);
  if (h < 48) return `${h}h`;
  return `${Math.round(h / 24)}d`;
}

function formatUptime(seconds) {
  if (seconds === null || seconds === undefined) return '--';
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  if (d > 0) return `${d}d ${h}h`;
  if (h > 0) return `${h}h ${m}m`;
  return `${m}m`;
}

function formatHeap(bytes) {
  if (bytes === null || bytes === undefined) return '--';
  return `${Math.round(bytes / 1024)} KB`;
}

export default function TestMachines() {
  const [machines, setMachines] = useState({});
  const [fetchedAt, setFetchedAt] = useState(null);

  const refresh = useCallback(async () => {
    const data = await fetchJson(`${API_BASE_URL}/api/machines`);
    if (data?.success) {
      setMachines(data.machines || {});
      setFetchedAt(new Date());
    }
  }, []);

  useEffect(() => {
    refresh();
    const interval = setInterval(refresh, POLL_INTERVAL_MS);
    return () => clearInterval(interval);
  }, [refresh]);

  const ids = Object.keys(machines).sort();

  return (
    <div className={styles.page}>
      <h1>Sensor diagnostics</h1>
      <p className={styles.sub}>
        Every machine the API knows about, including test IDs that never appear on the
        dashboard. Refreshes every {POLL_INTERVAL_MS / 1000}s.
        {fetchedAt && ` Last fetch ${fetchedAt.toLocaleTimeString()}.`}
      </p>

      {ids.length === 0 && <p className={styles.empty}>No machines reporting.</p>}

      {ids.length > 0 && (
        <div className={styles.tableWrap}>
          <table className={styles.table}>
            <thead>
              <tr>
                <th>Machine</th>
                <th>Running</th>
                <th>Empty</th>
                <th>Last seen</th>
                <th>Sensor</th>
                <th>Last reset</th>
                <th>Uptime</th>
                <th>Free heap</th>
                <th>Room</th>
              </tr>
            </thead>
            <tbody>
              {ids.map((id) => {
                const m = machines[id];
                const stale = m.timeSinceUpdate > 15 * 60 * 1000;
                const brownout = m.resetReason === 9;
                const sensorBad = m.sensorOk === false;

                return (
                  <tr key={id} className={stale ? styles.stale : undefined}>
                    <td className={styles.id}>{id}</td>
                    <td>{m.running ? 'yes' : 'no'}</td>
                    <td>{m.empty ? 'yes' : 'no'}</td>
                    <td>{formatAge(m.timeSinceUpdate)}</td>
                    <td className={sensorBad ? styles.bad : undefined}>
                      {m.sensorOk === null || m.sensorOk === undefined
                        ? '--'
                        : m.sensorOk
                          ? 'ok'
                          : 'FAULT'}
                    </td>
                    <td className={brownout ? styles.bad : undefined}>
                      {m.resetReason === null || m.resetReason === undefined
                        ? '--'
                        : `${RESET_REASONS[m.resetReason] || m.resetReason} (${m.resetReason})`}
                    </td>
                    <td>{formatUptime(m.uptime)}</td>
                    <td>{formatHeap(m.freeHeap)}</td>
                    <td className={styles.room}>{m.room || '--'}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      )}

      <p className={styles.note}>
        Dashes mean the node is on firmware that does not report diagnostics yet.
        Rows dim after 15 minutes without an update.
      </p>
    </div>
  );
}
