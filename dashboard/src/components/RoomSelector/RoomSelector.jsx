import { useState, useRef, useEffect } from 'react';
import styles from './RoomSelector.module.css';

export default function RoomSelector({ rooms, selectedRoom, onRoomChange }) {
  const [searchTerm, setSearchTerm] = useState('');
  const [isOpen, setIsOpen] = useState(false);
  const containerRef = useRef(null);

  // Get the selected room name
  const selectedRoomName = rooms.find(room => room.id === selectedRoom)?.name || '';

  // Filter rooms based on search term
  const filteredRooms = rooms.filter(room =>
    room.name.toLowerCase().includes(searchTerm.toLowerCase())
  );

  // Close dropdown when clicking outside
  useEffect(() => {
    function handleClickOutside(event) {
      if (containerRef.current && !containerRef.current.contains(event.target)) {
        setIsOpen(false);
        setSearchTerm('');
      }
    }
    document.addEventListener('mousedown', handleClickOutside);
    return () => document.removeEventListener('mousedown', handleClickOutside);
  }, []);

  const handleSelect = (roomId) => {
    onRoomChange(roomId);
    setIsOpen(false);
    setSearchTerm('');
  };

  return (
    <div className={styles.container} ref={containerRef}>
      <label htmlFor="room-search" className={styles.label}>
        Select Room
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
        {isOpen && filteredRooms.length > 0 && (
          <ul className={styles.dropdown}>
            {filteredRooms.map((room) => (
              <li
                key={room.id}
                className={`${styles.option} ${room.id === selectedRoom ? styles.selected : ''}`}
                onClick={() => handleSelect(room.id)}
              >
                {room.name}
              </li>
            ))}
          </ul>
        )}
        {isOpen && filteredRooms.length === 0 && searchTerm && (
          <ul className={styles.dropdown}>
            <li className={styles.noResults}>No rooms found</li>
          </ul>
        )}
      </div>
    </div>
  );
}
