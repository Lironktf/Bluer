import { useState, useEffect, useMemo, useCallback } from 'react';
import { useLocation, useNavigate } from 'react-router-dom';
import { track } from '@vercel/analytics';
import { useLocalStorage } from '../hooks/useLocalStorage';
import MachineGrid from '../components/MachineGrid/MachineGrid';
import SummaryBar from '../components/SummaryBar/SummaryBar';
import RoomSearchAutocomplete from '../components/RoomSearchAutocomplete/RoomSearchAutocomplete';
import { buildRoomSlots, roomPrefix } from '../utils/machineLabel';
import { getDeviceId } from '../utils/deviceId';
import styles from './Dashboard.module.css';

const API_BASE_URL = import.meta.env.VITE_API_URL || 'https://laun-dryer.vercel.app';

// Used the first time someone visits, before they have picked a room.
const DEFAULT_ROOM_NAME = 'SJU-Sieg/Ryan';

const POLL_INTERVAL_MS = 5000;

/**
 * Fetch JSON, returning null instead of throwing on anything unexpected.
 *
 * The SPA rewrite in vercel.json serves index.html for unmatched paths, so an
 * endpoint that is not deployed yet answers 200 with HTML rather than a 404.
 * Parsing that blindly throws on every poll.
 */
async function fetchJson(url) {
  try {
    const response = await fetch(url);
    if (!response.ok) return null;

    const contentType = response.headers.get('content-type') || '';
    if (!contentType.includes('application/json')) return null;

    return await response.json();
  } catch (error) {
    console.error(`Request to ${url} failed:`, error);
    return null;
  }
}

function matchesRoom(room, searchLower) {
  return (
    room.name.toLowerCase().includes(searchLower) ||
    (room.building && room.building.toLowerCase().includes(searchLower)) ||
    (room.floor && room.floor.toLowerCase().includes(searchLower))
  );
}

export default function Dashboard() {
  const location = useLocation();
  const navigate = useNavigate();

  const [machineStatuses, setMachineStatuses] = useState({});
  const [reportState, setReportState] = useState({});
  const [rooms, setRooms] = useState([]);
  const [roomsLoading, setRoomsLoading] = useState(true);
  const [lastUpdate, setLastUpdate] = useState(null);
  const [pendingIds, setPendingIds] = useState(() => new Set());

  // Remember the room across visits so the site opens on the one you use.
  const [lastRoomName, setLastRoomName] = useLocalStorage('lastRoomName', '');

  const [searchTerm, setSearchTerm] = useState(() => {
    const params = new URLSearchParams(location.search);
    return params.get('room') || location.state?.roomName || '';
  });

  // Navigation from another page (e.g. a room card) carries the room in state.
  useEffect(() => {
    if (location.state?.roomName && location.state.roomName !== searchTerm) {
      setSearchTerm(location.state.roomName);
      navigate(location.pathname, { replace: true, state: {} });
    }
  }, [location.state]);

  const refresh = useCallback(async () => {
    const [machineData, reportData] = await Promise.all([
      fetchJson(`${API_BASE_URL}/api/machines`),
      fetchJson(`${API_BASE_URL}/api/reports`),
    ]);

    if (machineData?.success) {
      setMachineStatuses(machineData.machines);
      setLastUpdate(new Date());
    }

    // Reports are additive: if the endpoint is unavailable the dashboard still
    // shows live machine status, just without any broken-report annotations.
    if (reportData?.success) {
      setReportState(reportData.reports || {});
    }
  }, []);

  useEffect(() => {
    refresh();
    const interval = setInterval(refresh, POLL_INTERVAL_MS);
    return () => clearInterval(interval);
  }, [refresh]);

  // Room list is public -- search works with or without an account.
  useEffect(() => {
    const fetchRooms = async () => {
      setRoomsLoading(true);
      try {
        const data = await fetchJson(`${API_BASE_URL}/api/publicRooms`);
        const loaded = data?.rooms || [];
        setRooms(loaded);

        const urlRoom = new URLSearchParams(location.search).get('room');
        if (loaded.length > 0 && !searchTerm && !urlRoom && !location.state?.roomName) {
          const preferred =
            loaded.find((r) => r.name === lastRoomName) ||
            loaded.find((r) => r.name === DEFAULT_ROOM_NAME) ||
            loaded[0];
          setSearchTerm(preferred.name);
        }
      } catch (error) {
        console.error('Failed to load rooms:', error);
      } finally {
        setRoomsLoading(false);
      }
    };

    fetchRooms();
  }, []);

  // Keep ?room= in sync so the view is linkable.
  useEffect(() => {
    const params = new URLSearchParams(location.search);
    if (searchTerm) {
      params.set('room', searchTerm);
    } else {
      params.delete('room');
    }
    const query = params.toString();
    const nextUrl = query ? `${location.pathname}?${query}` : location.pathname;
    if (nextUrl !== location.pathname + location.search) {
      navigate(nextUrl, { replace: true });
    }
  }, [searchTerm, location.pathname, location.search, navigate]);

  // Resolve the search box down to a single room. An exact name match wins
  // (that is what selecting from the dropdown produces); otherwise a search
  // that narrows to exactly one room counts as a selection.
  const selectedRoom = useMemo(() => {
    if (!searchTerm || rooms.length === 0) return null;

    const searchLower = searchTerm.trim().toLowerCase();
    const exact = rooms.find((room) => room.name.toLowerCase() === searchLower);
    if (exact) return exact;

    const matches = rooms.filter((room) => matchesRoom(room, searchLower));
    return matches.length === 1 ? matches[0] : null;
  }, [searchTerm, rooms]);

  useEffect(() => {
    if (selectedRoom && selectedRoom.name !== lastRoomName) {
      setLastRoomName(selectedRoom.name);
    }
  }, [selectedRoom, lastRoomName, setLastRoomName]);

  // Which rooms actually get traffic. Page views alone cannot answer this:
  // the room lives in a ?room= query param, which analytics strips from paths.
  useEffect(() => {
    if (selectedRoom?.name) {
      track('room_viewed', { room: selectedRoom.name });
    }
  }, [selectedRoom?.name]);

  // Every slot the room is configured to have, whether or not a sensor exists.
  const machines = useMemo(() => {
    if (!selectedRoom) return [];
    return buildRoomSlots(selectedRoom.name, machineStatuses, reportState);
  }, [selectedRoom, machineStatuses, reportState]);

  const sendReport = useCallback(
    async (machineId, type) => {
      setPendingIds((prev) => new Set(prev).add(machineId));

      try {
        const response = await fetch(`${API_BASE_URL}/api/reports`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ machineId, type, deviceId: getDeviceId() }),
        });

        const contentType = response.headers.get('content-type') || '';
        const data = contentType.includes('application/json') ? await response.json() : null;

        if (!response.ok || !data?.success) {
          throw new Error(data?.error || `Report failed (${response.status})`);
        }

        track('machine_reported', { type, machine: machineId, flagged: Boolean(data.flagged) });

        await refresh();
      } catch (error) {
        console.error('Failed to submit report:', error);
      } finally {
        setPendingIds((prev) => {
          const next = new Set(prev);
          next.delete(machineId);
          return next;
        });
      }
    },
    [refresh]
  );

  const handleReportBroken = useCallback((id) => sendReport(id, 'broken'), [sendReport]);
  const handleReportFixed = useCallback((id) => sendReport(id, 'fixed'), [sendReport]);

  const renderBody = () => {
    if (roomsLoading) {
      return <p className={styles.emptyText}>Loading rooms…</p>;
    }

    if (rooms.length === 0) {
      return (
        <div className={styles.empty}>
          <h2 className={styles.emptyTitle}>No rooms available</h2>
          <p className={styles.emptyText}>Could not load the room list. Try again shortly.</p>
        </div>
      );
    }

    if (!selectedRoom) {
      return (
        <div className={styles.empty}>
          <h2 className={styles.emptyTitle}>
            {searchTerm ? 'Pick a room' : 'Choose a room'}
          </h2>
          <p className={styles.emptyText}>
            {searchTerm
              ? `More than one room matches “${searchTerm}”. Select one from the list.`
              : 'Search above to see live machine status.'}
          </p>
          {searchTerm && (
            <button type="button" className={styles.textButton} onClick={() => setSearchTerm('')}>
              Clear search
            </button>
          )}
        </div>
      );
    }

    if (!roomPrefix(selectedRoom.name)) {
      return (
        <div className={styles.empty}>
          <h2 className={styles.emptyTitle}>{selectedRoom.name} is not set up yet</h2>
          <p className={styles.emptyText}>No machines have been configured for this room.</p>
        </div>
      );
    }

    return (
      <>
        <SummaryBar machines={machines} />
        <MachineGrid
          machines={machines}
          onReportBroken={handleReportBroken}
          onReportFixed={handleReportFixed}
          pendingIds={pendingIds}
        />
      </>
    );
  };

  return (
    <div className={styles.page}>
      <header className={styles.header}>
        <h1 className={styles.title}>{selectedRoom ? selectedRoom.name : 'Bluer'}</h1>
        <p className={styles.subtitle}>
          {selectedRoom
            ? [selectedRoom.building, selectedRoom.floor].filter(Boolean).join(' · ') ||
              'Live machine status'
            : 'Live laundry room status'}
          {lastUpdate && (
            <span className={styles.timestamp}>
              Updated {lastUpdate.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' })}
            </span>
          )}
        </p>
      </header>

      <div className={styles.searchContainer}>
        <RoomSearchAutocomplete
          rooms={rooms}
          searchTerm={searchTerm}
          onSearchChange={setSearchTerm}
          disabled={roomsLoading}
        />
      </div>

      {renderBody()}
    </div>
  );
}
