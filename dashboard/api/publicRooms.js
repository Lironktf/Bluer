// Public rooms endpoint - no authentication required
import { getCollection } from './lib/mongodb.js';

export default async function handler(req, res) {
  // Enable CORS
  res.setHeader('Access-Control-Allow-Credentials', true);
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,OPTIONS');
  res.setHeader(
    'Access-Control-Allow-Headers',
    'X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Content-Type, Date, X-Api-Version'
  );

  if (req.method === 'OPTIONS') {
    res.status(200).end();
    return;
  }

  if (req.method !== 'GET') {
    return res.status(405).json({ error: 'Method not allowed' });
  }

  try {
    const rooms = await getCollection('rooms');

    // Get all public rooms, sorted by name
    const publicRooms = await rooms
      .find({ isPublic: true })
      .sort({ name: 1 })
      .toArray();

    return res.status(200).json({
      success: true,
      rooms: publicRooms,
    });
  } catch (error) {
    console.error('❌ Public rooms API error:', error);
    return res.status(500).json({
      success: false,
      error: 'Internal server error',
      message: error.message,
    });
  }
}

