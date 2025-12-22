import styles from './StatusBadge.module.css';

export default function StatusBadge({ label, isActive }) {
  return (
    <div className={`${styles.badge} ${isActive ? styles.active : styles.inactive}`}>
      {label}
    </div>
  );
}
