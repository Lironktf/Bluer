import MachineCard from '../MachineCard/MachineCard';
import styles from './MachineGrid.module.css';

export default function MachineGrid({ machines, brokenMachines, onReportMachine }) {
  return (
    <div className={styles.grid}>
      {machines.map((machine) => (
        <MachineCard
          key={machine.id}
          machine={machine}
          isBroken={!!brokenMachines[machine.id]}
          onReport={onReportMachine}
        />
      ))}
    </div>
  );
}
