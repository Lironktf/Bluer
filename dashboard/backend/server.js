const express = require('express');
const cors = require('cors');

const app = express();
const PORT = 3001;

// Middleware
app.use(cors()); // Allow React app to connect
app.use(express.json()); // Parse JSON bodies

// API endpoint
app.post('/api/motion', (req, res) => {
  const data = req.body;
  console.log('📊 Received from ESP32:', data);
  
  // TODO: Store in database
  
  res.json({ success: true, received: data });
});

app.get('/api/motion', (req, res) => {
  // TODO: Fetch from database
  res.json({ message: 'Motion data endpoint' });
});

app.listen(PORT, () => {
  console.log(`✅ Backend running on http://localhost:${PORT}`);
});