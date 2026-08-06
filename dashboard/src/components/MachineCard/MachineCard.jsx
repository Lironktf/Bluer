import { useState } from 'react';
import ConfirmDialog from '../ConfirmDialog/ConfirmDialog';
import { WasherIcon, DryerIcon, WrenchIcon, CheckIcon } from '../Icons/MachineIcons';
import { WASHER, formatAge } from '../../utils/machineLabel';
import styles from './MachineCard.module.css';

function formatReturnDate(date) {
  if (!date) return null;
  return date.toLocaleDateString(undefined, { month: 'short', day: 'numeric' });
}

export default function MachineCard({ machine, onReportBroken, onReportFixed, isPending }) {
  const [confirming, setConfirming] = useState(null); // 'broken' | 'fixed' | null

  const TypeIcon = machine.type === WASHER ? WasherIcon : DryerIcon;
  const { hasSensor, isFresh, isRunning, isEmpty, flagged } = machine;

  // No sensor has ever reported for this slot. Nothing to show, so it collapses
  // to a single quiet line rather than occupying a full card.
  if (!hasSensor) {
    return (
      <div className={styles.placeholder}>
        <span className={styles.placeholderName}>{machine.label}</span>
        <span className={styles.placeholderNote}>No sensor</span>
      </div>
    );
  }

  const handleConfirm = () => {
    if (confirming === 'broken') onReportBroken(machine.id);
    if (confirming === 'fixed') onReportFixed(machine.id);
    setConfirming(null);
  };

  return (
    <>
      {confirming && (
        <ConfirmDialog
          message={
            confirming === 'broken'
              ? `Report ${machine.label} as broken?`
              : `Report ${machine.label} as working again?`
          }
          onConfirm={handleConfirm}
          onCancel={() => setConfirming(null)}
        />
      )}

      <div
        className={`${styles.card} ${isRunning ? styles.running : styles.idle} ${
          flagged ? styles.flagged : ''
        }`}
        aria-label={`${machine.label}, ${isRunning ? 'running' : 'not running'}, ${
          isEmpty ? 'empty' : 'full'
        }`}
      >
        <div className={styles.head}>
          <span className={styles.name}>{machine.label}</span>
          <TypeIcon className={styles.typeIcon} />
        </div>

        <div className={styles.status}>{isRunning ? 'Running' : 'Not running'}</div>

        <div className={styles.fill}>
          {isEmpty ? 'Empty' : 'Full'}
          {/* Older than the fresh window: still the machine's real last known
              state, just flagged with its age so nobody over-trusts it. */}
          {!isFresh && <span className={styles.age}>{formatAge(machine.ageMs)} ago</span>}
        </div>

        {flagged ? (
          <div className={styles.flagBlock}>
            <p className={styles.flagNote}>
              Reported broken
              {machine.flaggedUntil && ` · until ${formatReturnDate(machine.flaggedUntil)}`}
            </p>
            <button
              type="button"
              className={styles.fixButton}
              onClick={() => setConfirming('fixed')}
              disabled={isPending}
            >
              <CheckIcon className={styles.actionIcon} />
              Works again ({machine.fixedCount}/3)
            </button>
          </div>
        ) : (
          <button
            type="button"
            className={styles.reportButton}
            onClick={() => setConfirming('broken')}
            disabled={isPending}
          >
            <WrenchIcon className={styles.actionIcon} />
            {machine.brokenCount > 0 ? `Broken (${machine.brokenCount}/3)` : 'Report broken'}
          </button>
        )}
      </div>
    </>
  );
}
