import { WASHER, DRYER, countFree } from '../../utils/machineLabel';
import styles from './SummaryBar.module.css';

/**
 * Answers "is anything free right now?" without scrolling -- the whole reason
 * someone opens this on their phone.
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
    { key: 'washers', free: countFree(washers), noun: 'washer' },
    { key: 'dryers', free: countFree(dryers), noun: 'dryer' },
  ];

  return (
    <div className={styles.bar}>
      {counts.map(({ key, free, noun }) => (
        <span key={key} className={styles.item}>
          <strong className={free > 0 ? styles.available : styles.none}>{free}</strong>
          <span className={styles.label}>
            {noun}
            {free === 1 ? '' : 's'} free
          </span>
        </span>
      ))}
    </div>
  );
}
