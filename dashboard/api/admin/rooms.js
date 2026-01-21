// Admin API endpoint to manage public rooms
// This endpoint is for admin use only - add authentication if deploying to production

import { getCollection } from '../lib/mongodb.js';
import { ObjectId } from 'mongodb';

// Admin secret key - set this in environment variables
const ADMIN_SECRET = process.env.ADMIN_SECRET || 'your-admin-secret-change-this';

export default async function handler(req, res) {
  // Enable CORS
  res.setHeader('Access-Control-Allow-Credentials', true);
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,POST,PUT,DELETE,OPTIONS');
  res.setHeader(
    'Access-Control-Allow-Headers',
    'X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Content-Type, Date, X-Api-Version, X-Admin-Secret'
  );

  if (req.method === 'OPTIONS') {
    res.status(200).end();
    return;
  }

  try {
    // Check admin authorization
    const adminSecret = req.headers['x-admin-secret'];
    if (adminSecret !== ADMIN_SECRET) {
      return res.status(401).json({
        success: false,
        error: 'Unauthorized - Invalid admin secret'
      });
    }

    const rooms = await getCollection('rooms');

    // GET - Get all public rooms
    if (req.method === 'GET') {
      const publicRooms = await rooms.find({ isPublic: true }).sort({ name: 1 }).toArray();

      return res.status(200).json({
        success: true,
        rooms: publicRooms
      });
    }

    // POST - Create a new public room
    if (req.method === 'POST') {
      const { name, building, floor, machineIds } = req.body;

      if (!name) {
        return res.status(400).json({
          success: false,
          error: 'Room name is required'
        });
      }

      // Check if room already exists
      const existing = await rooms.findOne({ name, isPublic: true });
      if (existing) {
        return res.status(409).json({
          success: false,
          error: 'Room with this name already exists'
        });
      }

      const newRoom = {
        name,
        building: building || '',
        floor: floor || '',
        machineIds: machineIds || [],
        isPublic: true,
        userId: null,
        createdAt: new Date(),
        updatedAt: new Date()
      };

      const result = await rooms.insertOne(newRoom);

      console.log(`✅ Admin created public room: "${name}"`);

      return res.status(201).json({
        success: true,
        room: {
          _id: result.insertedId,
          ...newRoom
        }
      });
    }

    // PUT - Update a public room
    if (req.method === 'PUT') {
      const { roomId, name, building, floor, machineIds } = req.body;

      if (!roomId) {
        return res.status(400).json({
          success: false,
          error: 'Room ID is required'
        });
      }

      const updates = {
        updatedAt: new Date()
      };

      if (name !== undefined) updates.name = name;
      if (building !== undefined) updates.building = building;
      if (floor !== undefined) updates.floor = floor;
      if (machineIds !== undefined) updates.machineIds = machineIds;

      await rooms.updateOne(
        { _id: new ObjectId(roomId), isPublic: true },
        { $set: updates }
      );

      console.log(`✅ Admin updated public room: ${roomId}`);

      return res.status(200).json({
        success: true,
        message: 'Room updated successfully'
      });
    }

    // DELETE - Delete a public room
    if (req.method === 'DELETE') {
      const { roomId } = req.query;

      if (!roomId) {
        return res.status(400).json({
          success: false,
          error: 'Room ID is required'
        });
      }

      const result = await rooms.deleteOne({
        _id: new ObjectId(roomId),
        isPublic: true
      });

      if (result.deletedCount === 0) {
        return res.status(404).json({
          success: false,
          error: 'Room not found'
        });
      }

      console.log(`✅ Admin deleted public room: ${roomId}`);

      return res.status(200).json({
        success: true,
        message: 'Room deleted successfully'
      });
    }

    return res.status(405).json({ error: 'Method not allowed' });

  } catch (error) {
    console.error('❌ Admin rooms API error:', error);
    return res.status(500).json({
      success: false,
      error: 'Internal server error',
      message: error.message
    });
  }
}
