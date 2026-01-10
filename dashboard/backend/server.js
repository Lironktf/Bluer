const express = require('express');
const cors = require('cors');

const app = express();
const PORT = 3001;

// Middleware
app.use(cors()); // Allow React app to connect
app.use(express.json()); // Parse JSON bodies

// In-memory storage for machine statuses
// Format: { machineId: { running: boolean, empty: boolean, lastUpdate: timestamp } }
const machineStatuses = {};

// ESP32 endpoint - receives status updates from ESP32
app.post('/api/machine/status', (req, res) => {
  const { machineId, running, empty } = req.body;

  // Validate required fields
  if (!machineId || typeof running !== 'boolean' || typeof empty !== 'boolean') {
    return res.status(400).json({
      success: false,
      error: 'Missing required fields: machineId, running, empty'
    });
  }

  // Store the status with timestamp
  machineStatuses[machineId] = {
    running,
    empty,
    lastUpdate: new Date().toISOString()
  };

  console.log(`📊 [${machineId}] Running: ${running}, Empty: ${empty}`);

  res.json({ success: true, machineId, received: { running, empty } });
});

// Frontend endpoint - get all machine statuses
app.get('/api/machines', (req, res) => {
  res.json({
    success: true,
    machines: machineStatuses,
    timestamp: new Date().toISOString()
  });
});

// Frontend endpoint - get specific machine status
app.get('/api/machine/:machineId', (req, res) => {
  const { machineId } = req.params;
  const status = machineStatuses[machineId];

  if (!status) {
    return res.status(404).json({
      success: false,
      error: 'Machine not found'
    });
  }

  res.json({
    success: true,
    machineId,
    ...status
  });
});

// Legacy endpoint (keeping for backwards compatibility)
app.post('/api/motion', (req, res) => {
  const data = req.body;
  console.log('📊 Received from ESP32 (legacy):', data);
  res.json({ success: true, received: data });
});

app.listen(PORT, () => {
  console.log(`✅ Backend running on http://localhost:${PORT}`);
});