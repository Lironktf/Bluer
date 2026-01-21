// Vercel Serverless Function for machine history
// Query past state changes and usage patterns
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

  // Handle OPTIONS request for CORS preflight
  if (req.method === 'OPTIONS') {
    res.status(200).end();
    return;
  }

  if (req.method !== 'GET') {
    return res.status(405).json({ error: 'Method not allowed' });
  }

  try {
    const { machineId, limit = '50', startDate, endDate } = req.query;
    const history = await getCollection('machineHistory');

    // Build query filter
    const filter = {};

    if (machineId) {
      filter.machineId = machineId;
    }

    if (startDate || endDate) {
      filter.timestamp = {};
      if (startDate) {
        filter.timestamp.$gte = new Date(startDate);
      }
      if (endDate) {
        filter.timestamp.$lte = new Date(endDate);
      }
    }

    // Query history with limit
    const records = await history
      .find(filter)
      .sort({ timestamp: -1 })
      .limit(parseInt(limit))
      .toArray();

    // Get summary statistics
    const stats = await calculateStats(history, machineId);

    return res.status(200).json({
      success: true,
      count: records.length,
      records,
      stats,
      query: { machineId, limit, startDate, endDate }
    });

  } catch (error) {
    console.error('❌ History API Error:', error);
    return res.status(500).json({
      success: false,
      error: 'Internal server error',
      message: error.message
    });
  }
}

/**
 * Calculate usage statistics
 */
async function calculateStats(historyCollection, machineId = null) {
  const matchStage = machineId ? { machineId } : {};

  const pipeline = [
    { $match: matchStage },
    {
      $group: {
        _id: {
          machineId: '$machineId',
          date: {
            $dateToString: {
              format: '%Y-%m-%d',
              date: '$timestamp'
            }
          }
        },
        stateChanges: { $sum: 1 },
        runningChanges: {
          $sum: { $cond: ['$running', 1, 0] }
        },
        emptyChanges: {
          $sum: { $cond: ['$empty', 1, 0] }
        }
      }
    },
    {
      $group: {
        _id: '$_id.machineId',
        totalChanges: { $sum: '$stateChanges' },
        totalRunningChanges: { $sum: '$runningChanges' },
        totalEmptyChanges: { $sum: '$emptyChanges' },
        daysActive: { $sum: 1 }
      }
    }
  ];

  const stats = await historyCollection.aggregate(pipeline).toArray();

  return stats.reduce((acc, stat) => {
    acc[stat._id] = {
      totalChanges: stat.totalChanges,
      totalRunningChanges: stat.totalRunningChanges,
      totalEmptyChanges: stat.totalEmptyChanges,
      daysActive: stat.daysActive
    };
    return acc;
  }, {});
}
