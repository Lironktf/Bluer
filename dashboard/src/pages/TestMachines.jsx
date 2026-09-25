import { useState, useEffect, useCallback } from 'react';
import { Link } from 'react-router-dom';
import { machineNumberFromId, typeForNumber } from '../utils/machineLabel';
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

// Prefix descending so test IDs (s1, b1) sit above the deployed a1 machines,
// then numerically within a prefix so m10 does not land between m1 and m2.
function comparePrefixDesc(a, b) {
  const pattern = /^([a-z0-9]+)-m(\d+)$/i;
  const ma = pattern.exec(a);
  const mb = pattern.exec(b);

  if (!ma || !mb) return b.localeCompare(a);

  const prefixA = ma[1].toLowerCase();
  const prefixB = mb[1].toLowerCase();
  if (prefixA !== prefixB) return prefixB.localeCompare(prefixA);

  return parseInt(ma[2], 10) - parseInt(mb[2], 10);
}

// Sensor type follows the same odd/even convention the dashboard already uses.
function sensorLabel(machineId) {
  const number = machineNumberFromId(machineId);
  if (number === null || number === undefined) return null;
  return typeForNumber(number) === 'washer' ? 'INMP' : 'MPU';
}

// "flat" = answers but never changes, "noreply" = not on the bus at all. Both mean
// the wiring at the sensor needs attention rather than the node itself.
function sensorText(m) {
  if (!m.sensorState) return '--';
  if (m.sensorState === 'ok') return 'ok';
  if (m.sensorState === 'flat') return 'FLAT';
  if (m.sensorState === 'noreply') return 'NO REPLY';
  return m.sensorState;
}

function sensorIsBad(m) {
  return m.sensorState === 'flat' || m.sensorState === 'noreply';
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

  const ids = Object.keys(machines).sort(comparePrefixDesc);

  return (
    <div className={styles.page}>
      <h1>Sensor diagnostics</h1>
      <p className={styles.sub}>
        Every machine the API knows about, including test IDs that never appear on the
        dashboard. Refreshes every {POLL_INTERVAL_MS / 1000}s.
        {fetchedAt && ` Last fetch ${fetchedAt.toLocaleTimeString()}.`}{' '}
        <Link to="/test/history">Firmware trial log</Link>
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
                const sensorBad = sensorIsBad(m);

                return (
                  <tr key={id} className={stale ? styles.stale : undefined}>
                    <td className={styles.id}>{id}</td>
                    <td>{m.running ? 'yes' : 'no'}</td>
                    <td>{m.empty ? 'yes' : 'no'}</td>
                    <td>{formatAge(m.timeSinceUpdate)}</td>
                    <td className={sensorBad ? styles.bad : undefined}>
                      {sensorText(m)}
                      {m.sensorState && m.sensorState !== 'unknown' && sensorLabel(id) && (
                        <span className={styles.room}> ({sensorLabel(id)})</span>
                      )}
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
        Rows dim after 15 minutes without an update. Sensor FLAT or NO REPLY means the
        wiring at the sensor; a stale row with a healthy sensor means power, WiFi or the ESP32.
      </p>
    </div>
  );
}
