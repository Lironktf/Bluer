import StatusBadge from '../StatusBadge/StatusBadge';
import styles from './MachineCard.module.css';

export default function MachineCard({ machine, isBroken, onReport }) {
  return (
    <div className={`${styles.card} ${isBroken ? styles.broken : ''}`}>
      {/* Report button in top-right corner */}
      <button
        className={styles.reportButton}
        onClick={() => onReport(machine.id)}
        aria-label="Report machine as broken"
      >
        <svg
          width="18"
          height="18"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          strokeWidth="2"
          strokeLinecap="round"
          strokeLinejoin="round"
        >
          <path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z"></path>
        </svg>
      </button>

      {/* Machine number */}
      <div className={styles.machineNumber}>Machine {machine.number}</div>

      {/* Status display */}
      <div className={styles.statusContainer}>
        <div className={styles.statusText}>
          Status: <span className={styles.statusValue}>{machine.isRunning ? 'Running' : 'Not Running'}</span>
        </div>
        <StatusBadge label={machine.isEmpty ? "Empty" : "Full"} isActive={machine.isEmpty} />
      </div>
    </div>
  );
}
