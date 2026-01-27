import { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../context/AuthContext';
import Navigation from '../components/Navigation/Navigation';
import AuthModal from '../components/Auth/AuthModal';
import RoomSelector from '../components/RoomSelector/RoomSelector';
import Cookies from 'js-cookie';
import './MyRooms.css';

const API_BASE_URL = import.meta.env.VITE_API_URL || 'https://laun-dryer.vercel.app';

export default function MyRooms() {
  const { user, isAuthenticated, loading: authLoading } = useAuth();
  const navigate = useNavigate();

  const [userRooms, setUserRooms] = useState([]); // Rooms in user's list
  const [availableRooms, setAvailableRooms] = useState([]); // All available rooms to add
  const [loading, setLoading] = useState(true);
  const [showAuthModal, setShowAuthModal] = useState(false);

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
      const response = await fetch(`${API_BASE_URL}/api/rooms`, {
        headers: {
          'Authorization': `Bearer ${token}`
        }
      });

      if (response.ok) {
        const data = await response.json();
        setUserRooms(data.userRooms || []);
        setAvailableRooms(data.availableRooms || []);
        console.log('📦 User rooms:', data.userRooms?.length || 0);
        console.log('📦 Available rooms:', data.availableRooms?.length || 0);
      } else {
        console.error('❌ Failed to fetch rooms:', response.status);
      }
    } catch (error) {
      console.error('Failed to fetch rooms:', error);
    } finally {
      setLoading(false);
    }
  }

  async function handleAddRoom(roomId) {
    const token = Cookies.get('auth_token');
    if (!token) return;

    try {
      const response = await fetch(`${API_BASE_URL}/api/rooms`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${token}`
        },
        body: JSON.stringify({ roomId })
      });

      if (response.ok) {
        await fetchRooms(); // Refresh the list
      } else {
        const errorData = await response.json();
        alert(errorData.error || 'Failed to add room');
      }
    } catch (error) {
      console.error('Failed to add room:', error);
      alert('Failed to add room');
    }
  }

  async function handleRemoveRoom(roomId) {
    if (!confirm('Remove this room from your list?')) return;

    const token = Cookies.get('auth_token');
    if (!token) return;

    try {
      const response = await fetch(`${API_BASE_URL}/api/rooms?roomId=${roomId}`, {
        method: 'DELETE',
        headers: {
          'Authorization': `Bearer ${token}`
        }
      });

      if (response.ok) {
        await fetchRooms(); // Refresh the list
      } else {
        const errorData = await response.json();
        alert(errorData.error || 'Failed to remove room');
      }
    } catch (error) {
      console.error('Failed to remove room:', error);
      alert('Failed to remove room');
    }
  }

  function handleRoomClick(room) {
    // Navigate to Dashboard with room name in search
    navigate('/', { 
      state: { roomName: room.name },
      replace: false
    });
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

  // Filter available rooms to exclude ones already in user's list
  const userRoomIds = new Set(userRooms.map(r => r._id.toString()));
  const roomsToAdd = availableRooms.filter(room => !userRoomIds.has(room._id.toString()));

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

        {/* Search to add more rooms */}
        <div className="add-room-section">
          <h2>Add Rooms</h2>
          <p className="section-description">Search for rooms to add to your list</p>
          <RoomSelector
            rooms={roomsToAdd}
            selectedRoom={null}
            onRoomChange={(roomId) => {
              if (roomId) {
                handleAddRoom(roomId);
              }
            }}
            onAddRoom={handleAddRoom}
            showAddButton={true}
          />
        </div>

        {/* User's room cards */}
        <div className="my-rooms-section">
          <h2>Your Rooms ({userRooms.length})</h2>
          {userRooms.length > 0 ? (
            <div className="rooms-grid">
              {userRooms.map((room) => (
                <div key={room._id} className="room-card">
                  <div className="room-card-content" onClick={() => handleRoomClick(room)}>
                    <h3>{room.name}</h3>
                    {(room.building || room.floor) && (
                      <p className="room-location">
                        {[room.building, room.floor].filter(Boolean).join(' • ')}
                      </p>
                    )}
                    {room.machineIds && room.machineIds.length > 0 && (
                      <div className="machine-ids">
                        <strong>{room.machineIds.length}</strong> machine{room.machineIds.length !== 1 ? 's' : ''}
                      </div>
                    )}
                  </div>
                  <button
                    onClick={(e) => {
                      e.stopPropagation();
                      handleRemoveRoom(room._id);
                    }}
                    className="remove-btn"
                    aria-label="Remove room"
                  >
                    Remove
                  </button>
                </div>
              ))}
            </div>
          ) : (
            <div className="empty-state">
              <p>You haven't added any rooms yet.</p>
              <p>Use the search above to add rooms to your list.</p>
            </div>
          )}
        </div>
      </div>
      <AuthModal isOpen={showAuthModal} onClose={() => setShowAuthModal(false)} />
    </>
  );
}
