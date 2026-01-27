import { useState, useRef, useEffect } from 'react';
import styles from './RoomSelector.module.css';

export default function RoomSelector({ rooms, selectedRoom, onRoomChange, onAddRoom, showAddButton = false }) {
  const [searchTerm, setSearchTerm] = useState('');
  const [isOpen, setIsOpen] = useState(false);
  const containerRef = useRef(null);

  // Get the selected room name
  const selectedRoomName = rooms.find(room => room._id === selectedRoom)?.name || '';

  // Filter rooms based on search term (search name, building, and floor)
  const filteredRooms = rooms.filter(room => {
    if (!searchTerm) return true; // Show all rooms when search is empty
    const searchLower = searchTerm.toLowerCase();
    return (
      room.name.toLowerCase().includes(searchLower) ||
      (room.building && room.building.toLowerCase().includes(searchLower)) ||
      (room.floor && room.floor.toLowerCase().includes(searchLower))
    );
  });

  // Close dropdown when clicking outside
  useEffect(() => {
    function handleClickOutside(event) {
      if (containerRef.current && !containerRef.current.contains(event.target)) {
        setIsOpen(false);
      }
    }
    document.addEventListener('mousedown', handleClickOutside);
    return () => document.removeEventListener('mousedown', handleClickOutside);
  }, []);

  const handleSelect = (roomId) => {
    if (showAddButton && onAddRoom) {
      // If in "add" mode, call onAddRoom instead
      onAddRoom(roomId);
      setIsOpen(false);
      setSearchTerm('');
    } else {
      // Normal selection mode
      onRoomChange(roomId);
      setIsOpen(false);
      setSearchTerm('');
    }
  };

  return (
    <div className={styles.container} ref={containerRef}>
      <label htmlFor="room-search" className={styles.label}>
        {showAddButton ? 'Search for rooms to add' : 'Select Room'}
      </label>
      <div className={styles.searchContainer}>
        <input
          id="room-search"
          type="text"
          className={styles.searchInput}
          placeholder={selectedRoomName || "Search rooms..."}
          value={searchTerm}
          onChange={(e) => setSearchTerm(e.target.value)}
          onFocus={() => setIsOpen(true)}
        />
        {isOpen && (
          <ul className={styles.dropdown}>
            {filteredRooms.length > 0 ? (
              filteredRooms.map((room) => (
                <li
                  key={room._id}
                  className={`${styles.option} ${room._id === selectedRoom ? styles.selected : ''}`}
                  onClick={() => handleSelect(room._id)}
                  style={showAddButton ? { display: 'flex', justifyContent: 'space-between', alignItems: 'center' } : {}}
                >
                  <div>
                    <strong>{room.name}</strong>
                    {(room.building || room.floor) && (
                      <span style={{ fontSize: '0.85rem', color: '#666', marginLeft: '0.5rem' }}>
                        {[room.building, room.floor].filter(Boolean).join(' • ')}
                      </span>
                    )}
                  </div>
                  {showAddButton && (
                    <span style={{ fontSize: '0.85rem', color: '#3b82f6', fontWeight: '600', marginLeft: '1rem' }}>
                      + Add
                    </span>
                  )}
                </li>
              ))
            ) : searchTerm ? (
              <li className={styles.noResults}>No rooms found matching "{searchTerm}"</li>
            ) : (
              <li className={styles.noResults}>No rooms available</li>
            )}
          </ul>
        )}
      </div>
    </div>
  );
}
