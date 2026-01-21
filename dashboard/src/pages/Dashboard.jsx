import { useState, useEffect } from 'react';
import { useLocalStorage } from '../hooks/useLocalStorage';
import Navigation from '../components/Navigation/Navigation';
import MachineGrid from '../components/MachineGrid/MachineGrid';
import styles from './Dashboard.module.css';

const BACKEND_URL = 'https://laun-dryer.vercel.app';

export default function Dashboard() {
  // State for broken machines (persisted in localStorage)
  const [brokenMachines, setBrokenMachines] = useLocalStorage('brokenMachines', {});

  // State for machine statuses from backend
  const [machineStatuses, setMachineStatuses] = useState({});

  // State for last update time
  const [lastUpdate, setLastUpdate] = useState(null);

  // State for loading indicator
  const [isRefreshing, setIsRefreshing] = useState(false);

  // Fetch machine statuses from backend
  useEffect(() => {
    const fetchStatuses = async () => {
      try {
        setIsRefreshing(true);
        console.log('🔍 Fetching machine statuses from:', `${BACKEND_URL}/api/machines`);
        const response = await fetch(`${BACKEND_URL}/api/machines`);
        const data = await response.json();

        console.log('📦 Received data:', data);

        if (data.success) {
          console.log('✅ Machine statuses:', data.machines);
          setMachineStatuses(data.machines);
          setLastUpdate(new Date());
        }
      } catch (error) {
        console.error('❌ Error fetching machine statuses:', error);
      } finally {
        setIsRefreshing(false);
      }
    };

    // Fetch immediately
    fetchStatuses();

    // Then fetch every 5 seconds
    const interval = setInterval(fetchStatuses, 5000);

    return () => clearInterval(interval);
  }, []);

  // Convert backend data to machine array (only show machines that have reported)
  const machines = Object.keys(machineStatuses).map(machineId => {
    const status = machineStatuses[machineId];

    // Extract machine number from ID (e.g., "a1-m3" -> number: 3)
    const numberMatch = machineId.match(/m(\d+)$/);
    const number = numberMatch ? parseInt(numberMatch[1]) : 0;

    return {
      id: machineId,
      number: number,
      isRunning: status.running,
      isEmpty: status.empty
    };
  });

  console.log('🔧 Displaying machines:', machines);

  // Handle report machine (permanent - adds to broken list)
  const handleReportMachine = (machineId) => {
    setBrokenMachines(prev => ({
      ...prev,
      [machineId]: true
    }));
  };

  return (
    <div className={styles.app}>
      <Navigation />

      <div className={styles.header}>
        <h1 className={styles.title}>LaunDryer</h1>
        <p className={styles.subtitle}>
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
            {isRefreshing && <span style={{ fontSize: '12px' }}>🔄</span>}
            Live Machine Data
            {lastUpdate && (
              <span style={{ fontSize: '12px', opacity: 0.7 }}>
                • Updated {lastUpdate.toLocaleTimeString()}
              </span>
            )}
          </span>
        </p>
      </div>

      {machines.length === 0 ? (
        <div style={{ textAlign: 'center', padding: '40px', color: '#666' }}>
          <h2>No machines connected yet</h2>
          <p>Waiting for machines to send data...</p>
          <p style={{ fontSize: '14px', marginTop: '10px' }}>
            {isRefreshing ? 'Checking for devices...' : 'Auto-refreshing every 5 seconds'}
          </p>\
        </div>
      ) : (
        <MachineGrid
          machines={machines}
          brokenMachines={brokenMachines}
          onReportMachine={handleReportMachine}
        />
      )}
    </div>
  );
}
