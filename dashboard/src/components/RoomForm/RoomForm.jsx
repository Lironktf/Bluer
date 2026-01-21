import { useState, useEffect } from 'react';
import './RoomForm.css';

export default function RoomForm({ editingRoom, onSave, onCancel }) {
  const [roomName, setRoomName] = useState('');
  const [building, setBuilding] = useState('');
  const [floor, setFloor] = useState('');
  const [machineIds, setMachineIds] = useState('');

  useEffect(() => {
    if (editingRoom) {
      setRoomName(editingRoom.name);
      setBuilding(editingRoom.building || '');
      setFloor(editingRoom.floor || '');
      setMachineIds(editingRoom.machineIds.join(', '));
    } else {
      setRoomName('');
      setBuilding('');
      setFloor('');
      setMachineIds('');
    }
  }, [editingRoom]);

  function handleSubmit(e) {
    e.preventDefault();
    const roomData = {
      name: roomName,
      building,
      floor,
      machineIds: machineIds.split(',').map(id => id.trim()).filter(Boolean)
    };
    onSave(roomData);
  }

  return (
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
          <button type="button" onClick={onCancel} className="cancel-btn">
            Cancel
          </button>
          <button type="submit" className="save-btn">
            {editingRoom ? 'Update Room' : 'Add Room'}
          </button>
        </div>
      </form>
    </div>
  );
}
