import { WASHER, DRYER, countNotRunning } from '../../utils/machineLabel';
import styles from './SummaryBar.module.css';

/**
 * Headline count, so the answer is visible without scrolling -- the whole
 * reason someone opens this on their phone.
 */
export default function SummaryBar({ machines }) {
  const washers = machines.filter((m) => m.type === WASHER);
  const dryers = machines.filter((m) => m.type === DRYER);

  const anySensors = machines.some((m) => m.hasSensor);

  if (!anySensors) {
    return (
      <div className={`${styles.bar} ${styles.muted}`}>
        <span>No sensors reporting in this room yet</span>
      </div>
    );
  }

  const counts = [
    { key: 'washers', value: countNotRunning(washers), noun: 'washer' },
    { key: 'dryers', value: countNotRunning(dryers), noun: 'dryer' },
  ];

  return (
    <div className={styles.bar}>
      {counts.map(({ key, value, noun }) => (
        <span key={key} className={styles.item}>
          <strong className={value > 0 ? styles.available : styles.none}>{value}</strong>
          <span className={styles.label}>
            {noun}
            {value === 1 ? '' : 's'} not running
          </span>
        </span>
      ))}
    </div>
  );
}
