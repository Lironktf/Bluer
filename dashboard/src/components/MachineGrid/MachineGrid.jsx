import MachineCard from '../MachineCard/MachineCard';
import { WASHER } from '../../utils/machineLabel';
import styles from './MachineGrid.module.css';

/**
 * Two columns: washers on the left, dryers on the right.
 *
 * Reading across then down gives Washer 1, Dryer 1, Washer 2, Dryer 2 ... while
 * the columns let you scan just the type you care about.
 *
 * Placement is explicit rather than relying on source order, so Washer N always
 * sits beside Dryer N even if a room has an uneven number of each. Sharing a
 * grid row also means a row of two offline machines collapses to the short
 * height instead of being padded out to match a tall one elsewhere.
 */
export default function MachineGrid({ machines, onReportBroken, onReportFixed, pendingIds }) {
  return (
    <div className={styles.grid}>
      <h2 className={`${styles.heading} ${styles.headingWashers}`}>Washers</h2>
      <h2 className={`${styles.heading} ${styles.headingDryers}`}>Dryers</h2>

      {machines.map((machine) => (
        <div
          key={machine.id}
          className={styles.cell}
          style={{
            gridColumn: machine.type === WASHER ? 1 : 2,
            // +1 leaves room 1 for the sticky column headings.
            gridRow: machine.typeIndex + 1,
          }}
        >
          <MachineCard
            machine={machine}
            onReportBroken={onReportBroken}
            onReportFixed={onReportFixed}
            isPending={pendingIds.has(machine.id)}
          />
        </div>
      ))}
    </div>
  );
}
