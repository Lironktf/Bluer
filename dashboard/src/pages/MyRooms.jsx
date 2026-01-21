import { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../context/AuthContext';
import Navigation from '../components/Navigation/Navigation';
import AuthModal from '../components/Auth/AuthModal';
import Cookies from 'js-cookie';
import './MyRooms.css';

const API_BASE_URL = import.meta.env.VITE_API_URL || 'https://laun-dryer.vercel.app';

export default function MyRooms() {
  const { user, isAuthenticated, loading: authLoading } = useAuth();
  const navigate = useNavigate();

  const [rooms, setRooms] = useState([]);
  const [loading, setLoading] = useState(true);
  const [showAuthModal, setShowAuthModal] = useState(false);
  const [showAddRoom, setShowAddRoom] = useState(false);
  const [editingRoom, setEditingRoom] = useState(null);

  // Form state
  const [roomName, setRoomName] = useState('');
  const [building, setBuilding] = useState('');
  const [floor, setFloor] = useState('');
  const [machineIds, setMachineIds] = useState('');

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
    if (!token) return;

    try {
      const response = await fetch(`${API_BASE_URL}/api/rooms`, {
        headers: {
          'Authorization': `Bearer ${token}`
        }
      });

      if (response.ok) {
        const data = await response.json();
        setRooms(data.rooms);
      }
    } catch (error) {
      console.error('Failed to fetch rooms:', error);
    } finally {
      setLoading(false);
    }
  }

  async function handleSubmit(e) {
    e.preventDefault();
    const token = Cookies.get('auth_token');

    const roomData = {
      name: roomName,
      building,
      floor,
      machineIds: machineIds.split(',').map(id => id.trim()).filter(Boolean)
    };

    try {
      let response;
      if (editingRoom) {
        response = await fetch(`${API_BASE_URL}/api/rooms`, {
          method: 'PUT',
          headers: {
            'Content-Type': 'application/json',
            'Authorization': `Bearer ${token}`
          },
          body: JSON.stringify({ ...roomData, roomId: editingRoom._id })
        });
      } else {
        response = await fetch(`${API_BASE_URL}/api/rooms`, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            'Authorization': `Bearer ${token}`
          },
          body: JSON.stringify(roomData)
        });
      }

      if (response.ok) {
        await fetchRooms();
        resetForm();
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
      }
    } catch (error) {
      console.error('Failed to delete room:', error);
    }
  }

  function startEdit(room) {
    setEditingRoom(room);
    setRoomName(room.name);
    setBuilding(room.building || '');
    setFloor(room.floor || '');
    setMachineIds(room.machineIds.join(', '));
    setShowAddRoom(true);
  }

  function resetForm() {
    setRoomName('');
    setBuilding('');
    setFloor('');
    setMachineIds('');
    setEditingRoom(null);
    setShowAddRoom(false);
  }

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
          <button onClick={() => setShowAddRoom(true)} className="add-room-btn">
            + Add Room
          </button>
        </div>

        {showAddRoom && (
          <div className="room-form-container">
            <h3>{editingRoom ? 'Edit Room' : 'Add New Room'}</h3>
            <form onSubmit={handleSubmit} className="room-form">
              <div className="form-row">
                <div className="form-group">
                  <label>Room Name *</label>
                  <input
                    type="text"
                    value={roomName}
                    onChange={(e) => setRoomName(e.target.value)}
                    placeholder="e.g., Basement Laundry"
                    required
                  />
                </div>
                <div className="form-group">
                  <label>Building</label>
                  <input
                    type="text"
                    value={building}
                    onChange={(e) => setBuilding(e.target.value)}
                    placeholder="e.g., Building A"
                  />
                </div>
              </div>

              <div className="form-row">
                <div className="form-group">
                  <label>Floor</label>
                  <input
                    type="text"
                    value={floor}
                    onChange={(e) => setFloor(e.target.value)}
                    placeholder="e.g., 1st Floor"
                  />
                </div>
                <div className="form-group">
                  <label>Machine IDs</label>
                  <input
                    type="text"
                    value={machineIds}
                    onChange={(e) => setMachineIds(e.target.value)}
                    placeholder="e.g., a1-m1, a1-m2, a1-m3"
                  />
                  <small>Comma-separated list of machine IDs</small>
                </div>
              </div>

              <div className="form-actions">
                <button type="button" onClick={resetForm} className="cancel-btn">
                  Cancel
                </button>
                <button type="submit" className="save-btn">
                  {editingRoom ? 'Update Room' : 'Add Room'}
                </button>
              </div>
            </form>
          </div>
        )}

        <div className="rooms-grid">
          {rooms.length === 0 ? (
            <div className="empty-state">
              <p>You haven't added any rooms yet.</p>
              <p>Click "Add Room" to get started!</p>
            </div>
          ) : (
            rooms.map((room) => (
              <div key={room._id} className="room-card">
                <div className="room-card-header">
                  <h3>{room.name}</h3>
                  <div className="room-actions">
                    <button onClick={() => startEdit(room)} className="edit-btn">
                      Edit
                    </button>
                    <button onClick={() => handleDelete(room._id)} className="delete-btn">
                      Delete
                    </button>
                  </div>
                </div>
                {(room.building || room.floor) && (
                  <p className="room-location">
                    {[room.building, room.floor].filter(Boolean).join(' • ')}
                  </p>
                )}
                {room.machineIds && room.machineIds.length > 0 && (
                  <div className="machine-ids">
                    <strong>Machines:</strong> {room.machineIds.join(', ')}
                  </div>
                )}
                <button
                  onClick={() => navigate('/', { state: { filterMachines: room.machineIds } })}
                  className="view-machines-btn"
                >
                  View Machines
                </button>
              </div>
            ))
          )}
        </div>
      </div>
    </>
  );
}
