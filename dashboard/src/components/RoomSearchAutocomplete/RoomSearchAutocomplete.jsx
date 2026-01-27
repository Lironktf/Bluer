import { useState, useRef, useEffect } from 'react';
import styles from './RoomSearchAutocomplete.module.css';

export default function RoomSearchAutocomplete({ rooms, searchTerm, onSearchChange, disabled = false }) {
  const [isOpen, setIsOpen] = useState(false);
  const containerRef = useRef(null);

  // Filter rooms based on search term (same logic as RoomSelector)
  const filteredRooms = rooms.filter(room => {
    if (!searchTerm) return false; // Don't show all rooms when empty - only show when typing
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

  const handleSelect = (room) => {
    onSearchChange(room.name);
    setIsOpen(false);
  };

  const handleInputChange = (e) => {
    onSearchChange(e.target.value);
    setIsOpen(true); // Show dropdown when typing
  };

  const handleInputFocus = () => {
    if (searchTerm) {
      setIsOpen(true);
    }
  };

  return (
    <div className={styles.container} ref={containerRef}>
      <div className={styles.searchContainer}>
        <input
          type="text"
          className={styles.searchInput}
          placeholder="Search rooms by name, building, or floor..."
          value={searchTerm}
          onChange={handleInputChange}
          onFocus={handleInputFocus}
          disabled={disabled}
        />
        {searchTerm && (
          <button
            className={styles.clearButton}
            onClick={() => {
              onSearchChange('');
              setIsOpen(false);
            }}
            aria-label="Clear search"
          >
            ×
          </button>
        )}
        {isOpen && filteredRooms.length > 0 && (
          <ul className={styles.dropdown}>
            {filteredRooms.map((room) => (
              <li
                key={room._id}
                className={styles.option}
                onClick={() => handleSelect(room)}
              >
                <div>
                  <strong>{room.name}</strong>
                  {(room.building || room.floor) && (
                    <span style={{ fontSize: '0.85rem', color: '#666', marginLeft: '0.5rem' }}>
                      {[room.building, room.floor].filter(Boolean).join(' • ')}
                    </span>
                  )}
                </div>
              </li>
            ))}
          </ul>
        )}
        {isOpen && filteredRooms.length === 0 && searchTerm && (
          <ul className={styles.dropdown}>
            <li className={styles.noResults}>No rooms found matching "{searchTerm}"</li>
          </ul>
        )}
      </div>
    </div>
  );
}
