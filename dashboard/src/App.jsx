import { useState } from 'react';
import { laundryData } from './data/mockData';
import { useLocalStorage } from './hooks/useLocalStorage';
import Navigation from './components/Navigation/Navigation';
import RoomSelector from './components/RoomSelector/RoomSelector';
import MachineGrid from './components/MachineGrid/MachineGrid';
import styles from './App.module.css';

function App() {
  // State for selected room (default to first room)
  const [selectedRoom, setSelectedRoom] = useState(laundryData.rooms[0].id);

  // State for broken machines (persisted in localStorage)
  const [brokenMachines, setBrokenMachines] = useLocalStorage('brokenMachines', {});

  // Find the current room's machines
  const currentRoom = laundryData.rooms.find(room => room.id === selectedRoom);
  const machines = currentRoom ? currentRoom.machines : [];

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
