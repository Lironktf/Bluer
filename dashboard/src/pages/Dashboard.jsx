import { useState, useEffect } from 'react';
import { useLocation, useNavigate } from 'react-router-dom';
import { useLocalStorage } from '../hooks/useLocalStorage';
import Navigation from '../components/Navigation/Navigation';
import MachineGrid from '../components/MachineGrid/MachineGrid';
import styles from './Dashboard.module.css';
import Cookies from 'js-cookie';

const BACKEND_URL = 'https://laun-dryer.vercel.app';

export default function Dashboard() {
  const location = useLocation();
  const navigate = useNavigate();

  // State for broken machines (persisted in localStorage)
  const [brokenMachines, setBrokenMachines] = useLocalStorage('brokenMachines', {});

  // State for machine statuses from backend
  const [machineStatuses, setMachineStatuses] = useState({});

  // State for rooms
  const [rooms, setRooms] = useState([]);
  const [roomsLoading, setRoomsLoading] = useState(true);

  // State for last update time
  const [lastUpdate, setLastUpdate] = useState(null);

  // State for loading indicator
  const [isRefreshing, setIsRefreshing] = useState(false);

  // State for search/filter (now searches rooms)
  // Initialize from URL params or location state
  const [searchTerm, setSearchTerm] = useState(() => {
    const params = new URLSearchParams(location.search);
    return params.get('room') || location.state?.roomName || '';
  });

  // Update search term when location state changes (from MyRooms navigation)
  useEffect(() => {
    if (location.state?.roomName && location.state.roomName !== searchTerm) {
      setSearchTerm(location.state.roomName);
      // Clear the state to avoid re-triggering
      navigate(location.pathname, { replace: true, state: {} });
    }
  }, [location.state]);

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

  // Fetch rooms for search - need ALL available rooms (public rooms)
  useEffect(() => {
    const fetchRooms = async () => {
      setRoomsLoading(true);
      try {
        const token = Cookies.get('auth_token');
        if (token) {
          // Fetch all available rooms (user's rooms + public rooms)
          const response = await fetch(`${BACKEND_URL}/api/rooms`, {
            headers: {
              'Authorization': `Bearer ${token}`
            }
          });
          if (response.ok) {
            const data = await response.json();
            // Combine user rooms and available rooms for search
            const allRooms = [
              ...(data.userRooms || []),
              ...(data.availableRooms || [])
            ];
            // Remove duplicates
            const uniqueRooms = Array.from(
              new Map(allRooms.map(room => [room._id.toString(), room])).values()
            );
            setRooms(uniqueRooms);
            console.log('📦 Loaded', uniqueRooms.length, 'rooms for search');
          } else {
            console.error('❌ Failed to fetch rooms:', response.status);
          }
        } else {
          // If not logged in, we can't fetch rooms
          console.log('⚠️ Not logged in - room search unavailable');
        }
      } catch (error) {
        console.error('❌ Error fetching rooms:', error);
      } finally {
        setRoomsLoading(false);
      }
    };

    fetchRooms();
  }, []);

  // Convert backend data to machine array (only show machines that have reported)
  const allMachines = Object.keys(machineStatuses).map(machineId => {
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

  // Update URL when search term changes
  useEffect(() => {
    const params = new URLSearchParams(location.search);
    if (searchTerm) {
      params.set('room', searchTerm);
    } else {
      params.delete('room');
    }
    const newSearch = params.toString();
    const newUrl = newSearch ? `${location.pathname}?${newSearch}` : location.pathname;
    if (newUrl !== location.pathname + location.search) {
      navigate(newUrl, { replace: true });
    }
  }, [searchTerm, location.pathname, navigate]);

  // Filter machines based on room search (same logic as RoomSelector)
  let machines = allMachines;
  if (searchTerm && rooms.length > 0) {
    const searchLower = searchTerm.toLowerCase();
    
    // Find rooms that match the search term (same filtering as RoomSelector)
    const matchingRooms = rooms.filter(room => {
      return (
        room.name.toLowerCase().includes(searchLower) ||
        (room.building && room.building.toLowerCase().includes(searchLower)) ||
        (room.floor && room.floor.toLowerCase().includes(searchLower))
      );
    });

    console.log('🔍 Search term:', searchTerm);
    console.log('📦 Total rooms available:', rooms.length);
    console.log('🏠 Matching rooms:', matchingRooms.map(r => ({ name: r.name, machineIds: r.machineIds })));

    // Get machine IDs from matching rooms
    const matchingMachineIds = new Set();
    matchingRooms.forEach(room => {
      if (room.machineIds && Array.isArray(room.machineIds)) {
        room.machineIds.forEach(id => matchingMachineIds.add(id));
      }
    });

    console.log('🔧 Matching machine IDs:', Array.from(matchingMachineIds));
    console.log('🔧 Total machines available:', allMachines.length);

    // Filter machines that belong to matching rooms
    if (matchingMachineIds.size > 0) {
      machines = allMachines.filter(machine => matchingMachineIds.has(machine.id));
      console.log('✅ Filtered to', machines.length, 'machines');
    } else {
      // No matching rooms found
      machines = [];
      console.log('❌ No machines found for matching rooms');
    }
  } else if (searchTerm && rooms.length === 0) {
    // Search term but no rooms loaded yet
    console.log('⚠️ Search term entered but rooms not loaded yet');
    machines = []; // Show "no results" while loading
  }

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
        <h1 className={styles.title}>Bluer</h1>
        <p className={styles.subtitle}>
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: '8px' }}>
            {isRefreshing && <span style={{ fontSize: '12px' }}>🔄</span>}
            Rooms
            {lastUpdate && (
              <span style={{ fontSize: '12px', opacity: 0.7 }}>
                • Updated {lastUpdate.toLocaleTimeString()}
              </span>
            )}
          </span>
        </p>
      </div>

      <div className={styles.searchContainer}>
        <input
          type="text"
          className={styles.searchInput}
          placeholder={roomsLoading ? "Loading rooms..." : "Search rooms by name, building, or floor..."}
          value={searchTerm}
          onChange={(e) => setSearchTerm(e.target.value)}
          disabled={roomsLoading}
        />
        {searchTerm && (
          <button
            className={styles.clearButton}
            onClick={() => setSearchTerm('')}
            aria-label="Clear search"
          >
            ×
          </button>
        )}
      </div>

      {allMachines.length === 0 ? (
        <div style={{ textAlign: 'center', padding: '40px', color: '#666' }}>
          <h2>No machines connected yet</h2>
          <p>Waiting for machines to send data...</p>
          <p style={{ fontSize: '14px', marginTop: '10px' }}>
            {isRefreshing ? 'Checking for devices...' : 'Auto-refreshing every 5 seconds'}
          </p>
        </div>
      ) : machines.length === 0 && searchTerm ? (
        <div style={{ textAlign: 'center', padding: '40px', color: '#666' }}>
          <h2>No machines found</h2>
          {roomsLoading ? (
            <p>Loading rooms...</p>
          ) : rooms.length === 0 ? (
            <p>No rooms available. Please log in to search rooms.</p>
          ) : (
            <>
              <p>No machines found for rooms matching "{searchTerm}"</p>
              <p style={{ fontSize: '0.9rem', marginTop: '0.5rem', color: '#999' }}>
                Found {rooms.length} room{rooms.length !== 1 ? 's' : ''} total
              </p>
            </>
          )}
          <button
            onClick={() => setSearchTerm('')}
            style={{
              marginTop: '1rem',
              padding: '0.75rem 1.5rem',
              backgroundColor: 'white',
              color: 'black',
              border: '1px solid black',
              borderRadius: '8px',
              cursor: 'pointer',
              fontSize: '1rem'
            }}
          >
            Clear search
          </button>
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
