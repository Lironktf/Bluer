# MongoDB Implementation Summary

## What Was Built

Your laundry machine tracking system now has a complete MongoDB backend with the following features:

### 1. **Persistent Storage**
- All machine states are stored in MongoDB (no more in-memory storage that resets)
- Data persists across server restarts and deployments
- Scalable to handle many machines

### 2. **State Tracking**
- **Current State**: `machines` collection stores the latest state of each machine
  - Running status (true/false)
  - Empty status (true/false)
  - Availability (online/offline based on last update)
  - Timestamps: `createdAt`, `updatedAt`, `lastUpdate`

- **State History**: `machineHistory` collection records every state change
  - When did the machine start running?
  - When did it stop?
  - When did someone empty it?
  - When did it go offline/come back online?

### 3. **Automatic Offline Detection**
- Machines that haven't sent updates in 2 minutes are automatically marked as offline
- Offline/online transitions are recorded in history
- Frontend can show machine availability status

### 4. **Efficient History Tracking**
- Only records changes when state actually changes
- Regular heartbeat updates don't create unnecessary history records
- Optimized with database indexes for fast queries

## Files Created/Modified

### API Files
1. **`api/lib/mongodb.js`** - Database connection utility with connection caching
2. **`api/machines.js`** - Updated main API endpoint with MongoDB integration
3. **`api/history.js`** - NEW: Query machine history and usage statistics
4. **`api/scripts/setupIndexes.js`** - Database index setup script

### Documentation
5. **`MONGODB_SETUP.md`** - Complete setup guide
6. **`.env.example`** - Environment variable template

### Configuration
7. **`package.json`** - Added `mongodb` and `dotenv` dependencies, new `setup-db` script

## How to Set It Up

### Step 1: Create MongoDB Atlas Account
1. Go to https://cloud.mongodb.com
2. Create a free account and cluster (M0 tier is free)
3. Create database user with read/write permissions
4. Allow access from anywhere (0.0.0.0/0) for Vercel

### Step 2: Get Connection String
1. In Atlas, click "Connect" → "Connect your application"
2. Copy the connection string
3. Replace `<password>` with your database password

### Step 3: Configure Vercel
1. Go to your Vercel project settings
2. Add environment variables:
   - `MONGODB_URI` = your connection string
   - `MONGODB_DB` = `laundry`
3. Redeploy your application

### Step 4: Set Up Indexes (Optional but Recommended)
Create a `.env` file locally:
```bash
MONGODB_URI=mongodb+srv://...
MONGODB_DB=laundry
```

Then run:
```bash
npm run setup-db
```

## API Endpoints

### 1. POST `/api/machines`
**Used by**: ESP32 routers to update machine status

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
  "received": { "running": true, "empty": false },
  "stateChanged": true
}
```

### 2. GET `/api/machines`
**Used by**: Frontend to display current machine states

**Response:**
```json
{
  "success": true,
  "machines": {
    "a1-m1": {
      "running": true,
      "empty": false,
      "available": true,
      "lastUpdate": "2026-01-20T12:00:00Z",
      "timeSinceUpdate": 5234
    }
  },
  "timestamp": "2026-01-20T12:00:05Z"
}
```

### 3. GET `/api/history` (NEW!)
**Used by**: Analytics, debugging, usage tracking

**Query Parameters:**
- `machineId` - Filter by specific machine
- `limit` - Number of records (default 50)
- `startDate` - Start of date range
- `endDate` - End of date range

**Examples:**
```bash
# Get last 100 changes for machine a1-m1
GET /api/history?machineId=a1-m1&limit=100

# Get all changes today
GET /api/history?startDate=2026-01-20T00:00:00Z

# Get changes for all machines
GET /api/history
```

**Response:**
```json
{
  "success": true,
  "count": 50,
  "records": [
    {
      "machineId": "a1-m1",
      "running": true,
      "empty": false,
      "available": true,
      "timestamp": "2026-01-20T12:00:00Z",
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

## Database Schema

### machines Collection
```javascript
{
  _id: ObjectId,
  machineId: "a1-m1",
  running: Boolean,
  empty: Boolean,
  available: Boolean,
  lastUpdate: Date,
  createdAt: Date,
  updatedAt: Date
}
```

**Indexes:**
- `machineId` (unique)
- `lastUpdate` (descending)
- `available + lastUpdate` (compound)

### machineHistory Collection
```javascript
{
  _id: ObjectId,
  machineId: "a1-m1",
  running: Boolean,
  empty: Boolean,
  available: Boolean,
  timestamp: Date,
  changeType: String  // "initial", "update", "went_offline", "came_online"
}
```

**Indexes:**
- `machineId`
- `timestamp` (descending)
- `machineId + timestamp` (compound)
- `changeType + timestamp` (compound)

## Features & Benefits

### 1. **Data Persistence**
✅ No data loss on server restart
✅ Historical data for analytics
✅ Audit trail of all changes

### 2. **Automatic Offline Detection**
✅ Know when machines stop responding
✅ Track machine uptime
✅ Alert users to connectivity issues

### 3. **Efficient Storage**
✅ Only stores actual state changes
✅ Heartbeat updates don't waste storage
✅ Optimized with indexes for fast queries

### 4. **Analytics Ready**
✅ Track machine usage patterns
✅ Calculate uptime statistics
✅ Generate usage reports

### 5. **Scalable**
✅ MongoDB Atlas handles scaling automatically
✅ Can handle thousands of machines
✅ Connection pooling for performance

## Testing

### Test the Setup

1. **Check if MongoDB is connected:**
```bash
# Deploy to Vercel or run locally
# Check Vercel logs for any MongoDB connection errors
```

2. **Send test data from Arduino:**
```bash
# Your router2.ino will automatically send data
# Check Vercel logs for "STATE CHANGE" messages
```

3. **Query the history:**
```bash
curl https://your-app.vercel.app/api/history?limit=10
```

## Monitoring

**In Vercel Logs, look for:**
- `📊 [machineId] STATE CHANGE` - State transitions
- `📊 [machineId] Heartbeat` - Regular updates (no state change)
- `📡 [machineId] ONLINE` - Machine came back online
- `📡 [machineId] OFFLINE` - Machine went offline
- `📤 Sending N machine statuses` - GET requests
- `❌ API Error` - Something went wrong

**In MongoDB Atlas:**
- Monitor query performance
- Check storage usage
- View real-time operations
- Set up alerts for issues

## Next Steps

1. **Set up MongoDB Atlas** (follow MONGODB_SETUP.md)
2. **Configure Vercel environment variables**
3. **Deploy and test**
4. **Run `npm run setup-db`** to create indexes
5. **Update frontend** to show availability status
6. **Add analytics dashboard** using `/api/history`

## Troubleshooting

**"Please define MONGODB_URI environment variable"**
→ Add MONGODB_URI to Vercel environment variables and redeploy

**Connection timeout**
→ Check MongoDB Atlas Network Access, ensure 0.0.0.0/0 is allowed

**Authentication failed**
→ Verify database user password in connection string

**Slow queries**
→ Run `npm run setup-db` to create indexes

## Cost

**MongoDB Atlas Free Tier (M0):**
- ✅ 512 MB storage (plenty for this use case)
- ✅ Shared RAM
- ✅ No credit card required
- ✅ Perfect for testing and small deployments

**Estimated storage usage:**
- Current states: ~1 KB per machine
- History: ~200 bytes per state change
- 1000 state changes = ~200 KB
- Free tier handles ~2.5 million state changes

## Security

✅ Connection string is encrypted
✅ Authentication required
✅ Environment variables not in code
✅ CORS enabled for frontend access
✅ MongoDB Atlas has built-in security

---

**Questions?** Check MONGODB_SETUP.md for detailed instructions!
