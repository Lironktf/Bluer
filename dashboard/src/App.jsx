import { useState, useEffect } from 'react';
import { laundryData } from './data/mockData';
import { useLocalStorage } from './hooks/useLocalStorage';
import Navigation from './components/Navigation/Navigation';
import RoomSelector from './components/RoomSelector/RoomSelector';
import MachineGrid from './components/MachineGrid/MachineGrid';
import styles from './App.module.css';

const BACKEND_URL = 'https://laun-dryer.vercel.app';

function App() {
  // State for selected room (default to first room)
  const [selectedRoom, setSelectedRoom] = useState(laundryData.rooms[0].id);

  // State for broken machines (persisted in localStorage)
  const [brokenMachines, setBrokenMachines] = useLocalStorage('brokenMachines', {});

  // State for machine statuses from backend
  const [machineStatuses, setMachineStatuses] = useState({});

  // Fetch machine statuses from backend
  useEffect(() => {
    const fetchStatuses = async () => {
      try {
        const response = await fetch(`${BACKEND_URL}/api/machines`);
        const data = await response.json();

        if (data.success) {
          setMachineStatuses(data.machines);
        }
      } catch (error) {
        console.error('Error fetching machine statuses:', error);
      }
    };

    // Fetch immediately
    fetchStatuses();

    // Then fetch every 5 seconds
    const interval = setInterval(fetchStatuses, 5000);

    return () => clearInterval(interval);
  }, []);

  // Find the current room's machines
  const currentRoom = laundryData.rooms.find(room => room.id === selectedRoom);
  let machines = currentRoom ? currentRoom.machines : [];

  // Merge backend data with mock data
  machines = machines.map(machine => {
    const backendStatus = machineStatuses[machine.id];
    if (backendStatus) {
      return {
        ...machine,
        isRunning: backendStatus.running,
        isEmpty: backendStatus.empty
      };
    }
    return machine;
  });

  // Handle room selection change
  const handleRoomChange = (roomId) => {
    setSelectedRoom(roomId);
  };

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
        <p className={styles.subtitle}>Be Informed</p>
      </div>

      <RoomSelector
        rooms={laundryData.rooms}
        selectedRoom={selectedRoom}
        onRoomChange={handleRoomChange}
      />

      <MachineGrid
        machines={machines}
        brokenMachines={brokenMachines}
        onReportMachine={handleReportMachine}
      />
    </div>
  );
}

export default App;
