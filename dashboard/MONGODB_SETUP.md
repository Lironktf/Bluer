# MongoDB Setup Guide

This application uses MongoDB to store laundry machine states and history.

## Database Structure

### Collections

#### 1. `machines` Collection
Stores the current state of each machine.

**Document Structure:**
```javascript
{
  machineId: "a1-m1",           // Unique machine identifier
  running: true,                // Is machine currently running?
  empty: false,                 // Is machine empty?
  available: true,              // Is machine online (responding)?
  lastUpdate: ISODate("..."),   // Last time we received an update
  createdAt: ISODate("..."),    // When this machine was first seen
  updatedAt: ISODate("...")     // Last modification time
}
```

**Indexes:**
- `machineId` (unique)
- `lastUpdate` (for availability checking)

#### 2. `machineHistory` Collection
Tracks all state changes for analytics and history.

**Document Structure:**
```javascript
{
  machineId: "a1-m1",           // Machine identifier
  running: true,                // Running state at this time
  empty: false,                 // Empty state at this time
  available: true,              // Availability at this time
  timestamp: ISODate("..."),    // When this change occurred
  changeType: "update"          // Type: "initial", "update", "went_offline", "came_online"
}
```

**Indexes:**
- `machineId`
- `timestamp` (descending)
- Compound: `machineId` + `timestamp` (descending)

## Setup Instructions

### 1. Create MongoDB Atlas Account

1. Go to [MongoDB Atlas](https://cloud.mongodb.com)
2. Sign up for a free account
3. Create a new cluster (free tier M0 is fine)
4. Wait for cluster to be created (~5 minutes)

### 2. Configure Database Access

1. In Atlas, go to "Database Access"
2. Click "Add New Database User"
3. Create a username and strong password
4. Grant "Read and write to any database" permissions

### 3. Configure Network Access

1. In Atlas, go to "Network Access"
2. Click "Add IP Address"
3. For Vercel deployment: Click "Allow Access from Anywhere" (0.0.0.0/0)
   - This is safe because authentication is still required
4. For local development: Add your current IP

### 4. Get Connection String

1. In Atlas, go to "Database" and click "Connect"
2. Choose "Connect your application"
3. Select "Node.js" driver
4. Copy the connection string
5. Replace `<password>` with your database user password
6. Replace `<dbname>` with `laundry`

Example:
```
mongodb+srv://myuser:mypassword@cluster0.xxxxx.mongodb.net/laundry?retryWrites=true&w=majority
```

### 5. Set Environment Variables

#### For Local Development:
Create a `.env` file in the `dashboard` directory:
```bash
MONGODB_URI=mongodb+srv://...
MONGODB_DB=laundry
```

#### For Vercel Deployment:
1. Go to your Vercel project settings
2. Navigate to "Environment Variables"
3. Add:
   - `MONGODB_URI` = your connection string
   - `MONGODB_DB` = `laundry`
4. Apply to "Production", "Preview", and "Development"

### 6. Create Indexes (Recommended)

Run the setup script to create optimal indexes:

```bash
cd dashboard
node api/scripts/setupIndexes.js
```

## API Endpoints

### POST `/api/machines`
Update machine state (used by ESP32 routers)

**Request:**
```json
{
  "machineId": "a1-m1",
  "running": true,
  "empty": false
}
```

**Response:**
```json
{
  "success": true,
  "machineId": "a1-m1",
  "received": {
    "running": true,
    "empty": false
  },
  "stateChanged": true
}
```

### GET `/api/machines`
Get current state of all machines

**Response:**
```json
{
  "success": true,
  "machines": {
    "a1-m1": {
      "running": true,
      "empty": false,
      "available": true,
      "lastUpdate": "2026-01-20T...",
      "timeSinceUpdate": 5234
    }
  },
  "timestamp": "2026-01-20T..."
}
```

### GET `/api/history`
Query machine history

**Query Parameters:**
- `machineId` (optional): Filter by specific machine
- `limit` (optional, default 50): Number of records to return
- `startDate` (optional): ISO date string for range start
- `endDate` (optional): ISO date string for range end

**Example:**
```
GET /api/history?machineId=a1-m1&limit=100
```

**Response:**
```json
{
  "success": true,
  "count": 100,
  "records": [
    {
      "machineId": "a1-m1",
      "running": true,
      "empty": false,
      "available": true,
      "timestamp": "2026-01-20T...",
      "changeType": "update"
    }
  ],
  "stats": {
    "a1-m1": {
      "totalChanges": 523,
      "totalRunningChanges": 245,
      "totalEmptyChanges": 178,
      "daysActive": 15
    }
  }
}
```

## Features

### Automatic Offline Detection
- Machines that haven't sent updates in 2 minutes are marked as offline
- Offline/online transitions are recorded in history
- Frontend can display machine availability status

### State Change Tracking
- Only records history when state actually changes
- Heartbeat updates don't create history records (efficient!)
- Tracks different change types: initial, update, went_offline, came_online

### Timestamps
- `createdAt`: When machine was first registered
- `updatedAt`: Last modification time
- `lastUpdate`: Last time machine sent an update
- `timestamp`: When each history event occurred

## Monitoring

View logs in Vercel:
1. Go to your Vercel project
2. Click on "Logs"
3. Watch for:
   - `📊 [machineId] STATE CHANGE` - State transitions
   - `📊 [machineId] Heartbeat` - Regular updates
   - `📡 [machineId] ONLINE/OFFLINE` - Availability changes
   - `❌ API Error` - Problems to investigate

## Troubleshooting

### "Please define MONGODB_URI environment variable"
- Ensure MONGODB_URI is set in Vercel environment variables
- Redeploy after adding environment variables

### Connection timeout
- Check Network Access in MongoDB Atlas
- Ensure 0.0.0.0/0 is allowed (for Vercel)

### Authentication failed
- Verify database user credentials
- Check password in connection string (URL encode special characters)

### Slow queries
- Run the setupIndexes script to add indexes
- Monitor query performance in MongoDB Atlas

## Data Retention

Consider setting up TTL (Time To Live) indexes for automatic cleanup:

```javascript
// Delete history records older than 90 days
db.machineHistory.createIndex(
  { "timestamp": 1 },
  { expireAfterSeconds: 7776000 } // 90 days
)
```

## Backup

MongoDB Atlas automatically backs up your data. Configure backup settings in Atlas:
1. Go to "Backup" tab
2. Enable continuous backup for point-in-time recovery
3. Set retention policies
