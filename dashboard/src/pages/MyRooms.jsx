import { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../context/AuthContext';
import Navigation from '../components/Navigation/Navigation';
import AuthModal from '../components/Auth/AuthModal';
import RoomSelector from '../components/RoomSelector/RoomSelector';
import RoomForm from '../components/RoomForm/RoomForm';
import Cookies from 'js-cookie';
import './MyRooms.css';

const API_BASE_URL = import.meta.env.VITE_API_URL || 'https://laun-dryer.vercel.app';

export default function MyRooms() {
  const { user, isAuthenticated, loading: authLoading } = useAuth();
  const navigate = useNavigate();

  const [rooms, setRooms] = useState([]);
  const [allAvailableRooms, setAllAvailableRooms] = useState([]); // All rooms for search
  const [loading, setLoading] = useState(true);
  const [showAuthModal, setShowAuthModal] = useState(false);
  const [selectedRoomId, setSelectedRoomId] = useState(null);
  const [editingRoom, setEditingRoom] = useState(null);
  const [showRoomForm, setShowRoomForm] = useState(false);

  useEffect(() => {
    if (!authLoading) {
      if (!isAuthenticated) {
        setLoading(false);
      } else {
        fetchRooms();
      }
    }
  }, [isAuthenticated, authLoading]);

  async function fetchRooms() {
    const token = Cookies.get('auth_token');
    if (!token) {
      setLoading(false);
      return;
    }

    try {
      // Fetch user's rooms + all public rooms
      const response = await fetch(`${API_BASE_URL}/api/rooms`, {
        headers: {
          'Authorization': `Bearer ${token}`
        }
      });

      if (response.ok) {
        const data = await response.json();
        const allRooms = data.rooms || [];
        console.log('📦 Fetched rooms:', allRooms.length, 'rooms');
        setRooms(allRooms);
        setAllAvailableRooms(allRooms); // All available rooms for search
      } else {
        console.error('❌ Failed to fetch rooms:', response.status, response.statusText);
        const errorData = await response.json().catch(() => ({}));
        console.error('Error details:', errorData);
      }
    } catch (error) {
      console.error('Failed to fetch rooms:', error);
    } finally {
      setLoading(false);
    }
  }

  async function handleSaveRoom(roomData) {
    const token = Cookies.get('auth_token');
    if (!editingRoom) return; // Should not happen

    try {
      const response = await fetch(`${API_BASE_URL}/api/rooms`, {
        method: 'PUT',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${token}`
        },
        body: JSON.stringify({ ...roomData, roomId: editingRoom._id })
      });

      if (response.ok) {
        await fetchRooms();
        setShowRoomForm(false);
        setEditingRoom(null);
      }
    } catch (error) {
      console.error('Failed to save room:', error);
    }
  }

  async function handleDelete(roomId) {
    if (!confirm('Are you sure you want to delete this room?')) return;

    const token = Cookies.get('auth_token');

    try {
      const response = await fetch(`${API_BASE_URL}/api/rooms?roomId=${roomId}`, {
        method: 'DELETE',
        headers: {
          'Authorization': `Bearer ${token}`
        }
      });

      if (response.ok) {
        await fetchRooms();
        setSelectedRoomId(null);
      }
    } catch (error) {
      console.error('Failed to delete room:', error);
    }
  }

  const selectedRoom = rooms.find(room => room._id === selectedRoomId);

  if (authLoading || loading) {
    return (
      <>
        <Navigation />
        <div className="my-rooms-page">
          <div className="loading">Loading...</div>
        </div>
      </>
    );
  }

  if (!isAuthenticated) {
    return (
      <>
        <Navigation />
        <div className="my-rooms-page">
          <div className="not-logged-in">
            <h2>My Rooms</h2>
            <p>You need to be logged in to manage your laundry rooms.</p>
            <button onClick={() => setShowAuthModal(true)} className="login-btn">
              Log In / Sign Up
            </button>
          </div>
        </div>
        <AuthModal isOpen={showAuthModal} onClose={() => setShowAuthModal(false)} />
      </>
    );
  }

  return (
    <>
      <Navigation />
      <div className="my-rooms-page">
        <div className="my-rooms-header">
          <div>
            <h1>My Laundry Rooms</h1>
            <p className="welcome-text">Welcome back, {user.displayName}!</p>
          </div>
        </div>

        <RoomSelector
          rooms={allAvailableRooms}
          selectedRoom={selectedRoomId}
          onRoomChange={setSelectedRoomId}
        />

        {showRoomForm && (
          <RoomForm
            editingRoom={editingRoom}
            onSave={handleSaveRoom}
            onCancel={() => {
              setShowRoomForm(false);
              setEditingRoom(null);
            }}
          />
        )}

        {selectedRoom && !showRoomForm && (
          <div className="room-card">
            <div className="room-card-header">
              <h3>{selectedRoom.name}</h3>
              <div className="room-actions">
                <button onClick={() => { setEditingRoom(selectedRoom); setShowRoomForm(true); }} className="edit-btn">
                  Edit
                </button>
                <button onClick={() => handleDelete(selectedRoom._id)} className="delete-btn">
                  Delete
                </button>
              </div>
            </div>
            {(selectedRoom.building || selectedRoom.floor) && (
              <p className="room-location">
                {[selectedRoom.building, selectedRoom.floor].filter(Boolean).join(' • ')}
              </p>
            )}
            {selectedRoom.machineIds && selectedRoom.machineIds.length > 0 && (
              <div className="machine-ids">
                <strong>Machines:</strong> {selectedRoom.machineIds.join(', ')}
              </div>
            )}
            <button
              onClick={() => navigate('/', { state: { filterMachines: selectedRoom.machineIds } })}
              className="view-machines-btn"
            >
              View Machines
            </button>
          </div>
        )}

        {!selectedRoom && !showRoomForm && rooms.length > 0 && (
          <div className="empty-state">
            <p>Select a room to view its details.</p>
          </div>
        )}

        {rooms.length === 0 && !showRoomForm && (
          <div className="empty-state">
            <p>Add rooms by searching from them!</p>
          </div>
        )}
      </div>
    </>
  );
}
