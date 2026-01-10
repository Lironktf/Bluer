// Vercel Serverless Function for ESP32 machine status updates
import { setMachineStatus } from '../_shared/storage.js';

export default async function handler(req, res) {
  // Enable CORS
  res.setHeader('Access-Control-Allow-Credentials', true);
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,OPTIONS,PATCH,DELETE,POST,PUT');
  res.setHeader(
    'Access-Control-Allow-Headers',
    'X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Content-Type, Date, X-Api-Version'
  );

  // Handle OPTIONS request for CORS preflight
  if (req.method === 'OPTIONS') {
    res.status(200).end();
    return;
  }

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
    await setMachineStatus(machineId, { running, empty });

    console.log(`📊 [${machineId}] Running: ${running}, Empty: ${empty}`);

    return res.status(200).json({
      success: true,
      machineId,
      received: { running, empty }
    });
  }

  return res.status(405).json({ error: 'Method not allowed' });
}
