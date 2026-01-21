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

    // GET - Get all rooms for the current user
    if (req.method === 'GET') {
      const userRooms = await rooms.find({ userId }).sort({ createdAt: -1 }).toArray();

      return res.status(200).json({
        success: true,
        rooms: userRooms
      });
    }

    // POST - Create a new room
    if (req.method === 'POST') {
      const { name, building, floor, machineIds } = req.body;

      // Validate input
      if (!name) {
        return res.status(400).json({
          success: false,
          error: 'Room name is required'
        });
      }

      // Create room document
      const newRoom = {
        userId,
        name,
        building: building || '',
        floor: floor || '',
        machineIds: machineIds || [], // Array of machine IDs in this room
        createdAt: new Date(),
        updatedAt: new Date()
      };

      const result = await rooms.insertOne(newRoom);

      // Add room ID to user's rooms array
      await users.updateOne(
        { _id: userId },
        {
          $push: { rooms: result.insertedId },
          $set: { updatedAt: new Date() }
        }
      );

      console.log(`✅ Room created: "${name}" by ${tokenData.username}`);

      return res.status(201).json({
        success: true,
        room: {
          _id: result.insertedId,
          ...newRoom
        }
      });
    }

    // PUT - Update a room
    if (req.method === 'PUT') {
      const { roomId, name, building, floor, machineIds } = req.body;

      if (!roomId) {
        return res.status(400).json({
          success: false,
          error: 'Room ID is required'
        });
      }

      // Verify room belongs to user
      const room = await rooms.findOne({
        _id: new ObjectId(roomId),
        userId
      });

      if (!room) {
        return res.status(404).json({
          success: false,
          error: 'Room not found or access denied'
        });
      }

      // Update room
      const updates = {
        updatedAt: new Date()
      };

      if (name !== undefined) updates.name = name;
      if (building !== undefined) updates.building = building;
      if (floor !== undefined) updates.floor = floor;
      if (machineIds !== undefined) updates.machineIds = machineIds;

      await rooms.updateOne(
        { _id: new ObjectId(roomId) },
        { $set: updates }
      );

      console.log(`✅ Room updated: "${name}" by ${tokenData.username}`);

      return res.status(200).json({
        success: true,
        room: {
          ...room,
          ...updates
        }
      });
    }

    // DELETE - Delete a room
    if (req.method === 'DELETE') {
      const { roomId } = req.query;

      if (!roomId) {
        return res.status(400).json({
          success: false,
          error: 'Room ID is required'
        });
      }

      const roomObjectId = new ObjectId(roomId);

      // Verify room belongs to user
      const room = await rooms.findOne({
        _id: roomObjectId,
        userId
      });

      if (!room) {
        return res.status(404).json({
          success: false,
          error: 'Room not found or access denied'
        });
      }

      // Delete room
      await rooms.deleteOne({ _id: roomObjectId });

      // Remove room ID from user's rooms array
      await users.updateOne(
        { _id: userId },
        {
          $pull: { rooms: roomObjectId },
          $set: { updatedAt: new Date() }
        }
      );

      console.log(`✅ Room deleted: "${room.name}" by ${tokenData.username}`);

      return res.status(200).json({
        success: true,
        message: 'Room deleted successfully'
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
