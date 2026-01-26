# Laundry MQTT Bridge

This bridge subscribes to MQTT messages from the BLE-Scanner and forwards machine status updates to your Vercel API.

## Architecture

```
┌─────────────┐      BLE      ┌─────────────┐      MQTT      ┌─────────────┐     HTTPS     ┌─────────────┐
│ machineESP  │ ───────────▶ │ BLE-Scanner │ ───────────▶  │ MQTT Bridge │ ───────────▶  │ Vercel API  │
│ (laundry)   │              │   (ESP32)   │               │  (Node.js)  │               │ (Dashboard) │
└─────────────┘              └─────────────┘               └─────────────┘               └─────────────┘
```

## Quick Start (Using Public Test Broker)

For testing, you can use the public Mosquitto test broker (no account needed):

### 1. Install Dependencies

```bash
cd mqtt-bridge
npm install
```

### 2. Run the Bridge

```bash
npm start
```

That's it! The bridge will connect to `test.mosquitto.org` and wait for messages.

### 3. Configure BLE-Scanner

In `onlineExmp/BLE-Scanner/config.h`, the default settings already point to the test broker:

```cpp
#define MQTT_BROKER        "test.mosquitto.org"
#define MQTT_PORT          1883
#define MQTT_USE_AUTH      0
```

---

## Production Setup (HiveMQ Cloud - Free Tier)

For a more reliable setup, use HiveMQ Cloud's free tier:

### Step 1: Create HiveMQ Cloud Account

1. Go to [https://www.hivemq.com/cloud/](https://www.hivemq.com/cloud/)
2. Click **"Get Started Free"**
3. Create an account (email + password)

### Step 2: Create a Free Cluster

1. After login, click **"Create Cluster"**
2. Select **"Serverless (Free)"** tier
3. Choose a cloud provider (AWS) and region close to you
4. Click **"Create Cluster"**
5. Wait for the cluster to be created (1-2 minutes)

### Step 3: Create Credentials

1. Go to your cluster's **"Access Management"** tab
2. Click **"Add Credentials"**
3. Create a username and password (save these!)
4. Set permissions to **"Publish and Subscribe"**

### Step 4: Get Connection Details

On your cluster overview, you'll see:
- **Cluster URL**: `xxxxxxxx.s2.eu.hivemq.cloud` (copy this)
- **Port**: `8883` (TLS)

### Step 5: Configure the Bridge

Create a `.env` file (copy from `.env.example`):

```bash
cp .env.example .env
```

Edit `.env`:

```env
MQTT_BROKER=mqtts://YOUR_CLUSTER.s2.eu.hivemq.cloud:8883
MQTT_USERNAME=your_username
MQTT_PASSWORD=your_password
API_ENDPOINT=https://laun-dryer.vercel.app/api/machines
MQTT_TOPIC=laundry/machines/+/status
```

### Step 6: Configure BLE-Scanner for HiveMQ

Edit `onlineExmp/BLE-Scanner/config.h`:

```cpp
#define MQTT_BROKER        "YOUR_CLUSTER.s2.eu.hivemq.cloud"
#define MQTT_PORT          8883
#define MQTT_USE_AUTH      1
#define MQTT_USER          "your_username"
#define MQTT_PASSWORD      "your_password"
```

**Important**: For HiveMQ Cloud (TLS), you need to modify `mqtt.cpp` to use `WiFiClientSecure` instead of `WiFiClient`. See the "TLS Support" section below.

---

## Running the Bridge 24/7

The bridge needs to run continuously. Options:

### Option A: Your Computer

Just keep a terminal open running `npm start`. Works for testing.

### Option B: Free Cloud Hosting (Recommended)

**Railway.app** (free tier):
1. Push your code to GitHub
2. Connect Railway to your repo
3. Set environment variables in Railway dashboard
4. Deploy!

**Render.com** (free tier):
1. Create a new "Background Worker"
2. Connect to your GitHub repo
3. Set build command: `npm install`
4. Set start command: `npm start`
5. Add environment variables

**Fly.io** (free tier):
1. Install flyctl CLI
2. Run `fly launch`
3. Set secrets: `fly secrets set MQTT_BROKER=... MQTT_PASSWORD=...`
4. Deploy: `fly deploy`

---

## TLS Support for ESP32 (HiveMQ Cloud)

If using HiveMQ Cloud, the ESP32 needs TLS. Modify `mqtt.cpp`:

Replace:
```cpp
static WiFiClient _wifiClient;
```

With:
```cpp
#include <WiFiClientSecure.h>
static WiFiClientSecure _wifiClient;

// In MqttSetup(), add:
_wifiClient.setInsecure(); // Skip cert verification (simpler, less secure)
```

**Note**: Using TLS on ESP32 adds ~50-100KB to sketch size, but is still much smaller than HTTPS because the connection is persistent (only one handshake).

---

## MQTT Topics

The system uses this topic structure:

| Topic | Description |
|-------|-------------|
| `laundry/machines/{machineId}/status` | Machine status updates |

**Message format** (JSON):
```json
{
  "machineId": "a1-m1",
  "running": true,
  "empty": false,
  "timestamp": 123456789
}
```

---

## Testing

### Test with MQTT Explorer

1. Download [MQTT Explorer](http://mqtt-explorer.com/)
2. Connect to `test.mosquitto.org:1883` (or your HiveMQ cluster)
3. Subscribe to `laundry/machines/#`
4. Watch for messages from your BLE-Scanner

### Send a Test Message

Using `mosquitto_pub`:
```bash
mosquitto_pub -h test.mosquitto.org -t "laundry/machines/test-1/status" \
  -m '{"machineId":"test-1","running":true,"empty":false}'
```

The bridge should receive it and POST to your Vercel API.

---

## Troubleshooting

### Bridge won't connect
- Check your broker URL and port
- For HiveMQ Cloud, ensure you're using `mqtts://` (TLS) on port 8883
- Verify username/password

### No messages received
- Check that BLE-Scanner is running and connected to WiFi
- Verify MQTT broker settings match between ESP32 and bridge
- Use MQTT Explorer to see if messages are being published

### API errors
- Verify your API endpoint is correct
- Check Vercel logs for errors
- Ensure the API accepts the JSON format

---

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `MQTT_BROKER` | `mqtt://test.mosquitto.org:1883` | MQTT broker URL |
| `MQTT_USERNAME` | (empty) | MQTT username (optional) |
| `MQTT_PASSWORD` | (empty) | MQTT password (optional) |
| `API_ENDPOINT` | `https://laun-dryer.vercel.app/api/machines` | Vercel API URL |
| `MQTT_TOPIC` | `laundry/machines/+/status` | Topic pattern |
