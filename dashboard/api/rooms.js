// User rooms management endpoint
import { getCollection } from './lib/mongodb.js';
import { authenticateRequest } from './lib/auth.js';
import { ObjectId } from 'mongodb';

export default async function handler(req, res) {
  // Enable CORS
  res.setHeader('Access-Control-Allow-Credentials', true);
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,POST,PUT,DELETE,OPTIONS');
  res.setHeader(
    'Access-Control-Allow-Headers',
    'X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Content-Type, Date, X-Api-Version, Authorization'
  );

  if (req.method === 'OPTIONS') {
    res.status(200).end();
    return;
  }

  try {
    // Verify authentication
    const tokenData = authenticateRequest(req);

    if (!tokenData) {
      return res.status(401).json({
        success: false,
        error: 'Authentication required. Please log in to manage rooms.'
      });
    }

    const rooms = await getCollection('rooms');
    const users = await getCollection('users');
    const userId = new ObjectId(tokenData.userId);

    // GET - Get user's room list + all available rooms for search
    if (req.method === 'GET') {
      // Get user document to find their room list
      const user = await users.findOne({ _id: userId });
      const userRoomIds = user?.roomIds || [];

      // Get rooms in user's list
      const userRoomObjectIds = userRoomIds.map(id => new ObjectId(id));
      const userRoomsList = await rooms.find({ 
        _id: { $in: userRoomObjectIds } 
      }).sort({ name: 1 }).toArray();

      // Get all public rooms (for search/adding)
      const publicRooms = await rooms.find({ isPublic: true }).sort({ name: 1 }).toArray();

      return res.status(200).json({
        success: true,
        userRooms: userRoomsList, // Rooms in user's list
        availableRooms: publicRooms // All public rooms available to add
      });
    }

    // POST - Add a room to user's list (not create a new room)
    if (req.method === 'POST') {
      const { roomId } = req.body;

      if (!roomId) {
        return res.status(400).json({
          success: false,
          error: 'Room ID is required'
        });
      }

      const roomObjectId = new ObjectId(roomId);

      // Verify room exists and is public
      const room = await rooms.findOne({ 
        _id: roomObjectId,
        isPublic: true 
      });

      if (!room) {
        return res.status(404).json({
          success: false,
          error: 'Room not found or not available'
        });
      }

      // Check if user already has this room
      const user = await users.findOne({ _id: userId });
      const userRoomIds = user?.roomIds || [];
      
      if (userRoomIds.some(id => id.toString() === roomId)) {
        return res.status(400).json({
          success: false,
          error: 'Room already in your list'
        });
      }

      // Add room ID to user's roomIds array
      await users.updateOne(
        { _id: userId },
        {
          $push: { roomIds: roomObjectId },
          $set: { updatedAt: new Date() }
        },
        { upsert: true }
      );

      console.log(`✅ Room "${room.name}" added to ${tokenData.username}'s list`);

      return res.status(200).json({
        success: true,
        message: 'Room added to your list',
        room
      });
    }

    // PUT - Not allowed for regular users (only admins can edit rooms)
    if (req.method === 'PUT') {
      return res.status(403).json({
        success: false,
        error: 'Only administrators can edit room information. Users can only add/remove rooms from their list.'
      });
    }

    // DELETE - Remove a room from user's list (not delete the room itself)
    if (req.method === 'DELETE') {
      const { roomId } = req.query;

      if (!roomId) {
        return res.status(400).json({
          success: false,
          error: 'Room ID is required'
        });
      }

      const roomObjectId = new ObjectId(roomId);

      // Verify room is in user's list
      const user = await users.findOne({ _id: userId });
      const userRoomIds = user?.roomIds || [];
      
      if (!userRoomIds.some(id => id.toString() === roomId)) {
        return res.status(404).json({
          success: false,
          error: 'Room not found in your list'
        });
      }

      // Remove room ID from user's roomIds array
      await users.updateOne(
        { _id: userId },
        {
          $pull: { roomIds: roomObjectId },
          $set: { updatedAt: new Date() }
        }
      );

      console.log(`✅ Room removed from ${tokenData.username}'s list`);

      return res.status(200).json({
        success: true,
        message: 'Room removed from your list'
      });
    }

    return res.status(405).json({ error: 'Method not allowed' });

  } catch (error) {
    console.error('❌ Rooms API error:', error);
    return res.status(500).json({
      success: false,
      error: 'Internal server error',
      message: error.message
    });
  }
}
