// Vercel Serverless Function for machine status (handles both POST and GET)
// Module-level storage (persists while this specific function instance is warm)
const machineStatuses = new Map();

export default async function handler(req, res) {
  // Enable CORS
  res.setHeader('Access-Control-Allow-Credentials', true);
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,POST,OPTIONS');
  res.setHeader(
    'Access-Control-Allow-Headers',
    'X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Content-Type, Date, X-Api-Version'
  );

  // Handle OPTIONS request for CORS preflight
  if (req.method === 'OPTIONS') {
    res.status(200).end();
    return;
  }

  // Handle POST - ESP32 sending status update
  if (req.method === 'POST') {
    const { machineId, running, empty } = req.body;

    // Validate required fields
    if (!machineId || typeof running !== 'boolean' || typeof empty !== 'boolean') {
      return res.status(400).json({
        success: false,
        error: 'Missing required fields: machineId, running, empty'
      });
    }

    // Store the status
    machineStatuses.set(machineId, {
      running,
      empty,
      lastUpdate: new Date().toISOString()
    });

    console.log(`📊 [${machineId}] Running: ${running}, Empty: ${empty}`);
    console.log(`📦 Current storage:`, Object.fromEntries(machineStatuses));

    return res.status(200).json({
      success: true,
      machineId,
      received: { running, empty }
    });
  }

  // Handle GET - Frontend fetching all statuses
  if (req.method === 'GET') {
    const statuses = {};
    for (const [machineId, status] of machineStatuses.entries()) {
      statuses[machineId] = status;
    }

    console.log(`📤 Sending statuses:`, statuses);

    return res.status(200).json({
      success: true,
      machines: statuses,
      timestamp: new Date().toISOString()
    });
  }

  return res.status(405).json({ error: 'Method not allowed' });
}
