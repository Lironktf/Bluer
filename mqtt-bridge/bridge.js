/**
 * MQTT to Vercel API Bridge
 * 
 * This script subscribes to MQTT topics from the laundry scanner
 * and forwards machine status updates to the Vercel API.
 * 
 * Run with: node bridge.js
 * 
 * Environment variables (or edit defaults below):
 *   MQTT_BROKER    - MQTT broker URL (e.g., mqtt://test.mosquitto.org:1883)
 *   MQTT_USERNAME  - MQTT username (optional, for authenticated brokers)
 *   MQTT_PASSWORD  - MQTT password (optional, for authenticated brokers)
 *   API_ENDPOINT   - Vercel API URL
 *   MQTT_TOPIC     - Topic pattern to subscribe to
 */

const mqtt = require('mqtt');

// ============================================
// CONFIGURATION - Edit these or use env vars
// ============================================

const config = {
  // MQTT Broker settings
  // For public test broker (no auth):
  mqttBroker: process.env.MQTT_BROKER || 'mqtt://test.mosquitto.org:1883',
  mqttUsername: process.env.MQTT_USERNAME || '',
  mqttPassword: process.env.MQTT_PASSWORD || '',
  
  // Vercel API endpoint
  apiEndpoint: process.env.API_ENDPOINT || 'https://laun-dryer.vercel.app/api/machines',
  
  // MQTT topic pattern (+ is single-level wildcard)
  mqttTopic: process.env.MQTT_TOPIC || 'laundry/machines/+/status',
  
  // Retry settings
  retryAttempts: 3,
  retryDelay: 1000, // ms
};

// ============================================
// BRIDGE LOGIC
// ============================================

let mqttClient = null;
let messageCount = 0;
let errorCount = 0;

/**
 * Post machine status to Vercel API
 */
async function postToApi(machineId, running, empty) {
  const payload = {
    machineId,
    running,
    empty,
  };

  console.log(`[API] Posting: ${JSON.stringify(payload)}`);

  for (let attempt = 1; attempt <= config.retryAttempts; attempt++) {
    try {
      const response = await fetch(config.apiEndpoint, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(payload),
      });

      if (response.ok) {
        const data = await response.json();
        console.log(`[API] Success: ${JSON.stringify(data)}`);
        return true;
      } else {
        const errorText = await response.text();
        console.error(`[API] Error ${response.status}: ${errorText}`);
      }
    } catch (error) {
      console.error(`[API] Request failed (attempt ${attempt}/${config.retryAttempts}): ${error.message}`);
    }

    if (attempt < config.retryAttempts) {
      await new Promise(resolve => setTimeout(resolve, config.retryDelay));
    }
  }

  errorCount++;
  return false;
}

/**
 * Handle incoming MQTT message
 */
async function handleMessage(topic, message) {
  messageCount++;
  console.log(`\n[MQTT] #${messageCount} Message on topic: ${topic}`);
  
  try {
    const payload = JSON.parse(message.toString());
    console.log(`[MQTT] Payload: ${JSON.stringify(payload)}`);

    // Extract machine ID from topic or payload
    // Topic format: laundry/machines/{machineId}/status
    const topicParts = topic.split('/');
    const machineIdFromTopic = topicParts.length >= 3 ? topicParts[2] : null;
    
    // Prefer machineId from payload, fall back to topic
    const machineId = payload.machineId || machineIdFromTopic;
    
    if (!machineId) {
      console.error('[MQTT] No machineId found in message or topic');
      return;
    }

    const running = Boolean(payload.running);
    const empty = Boolean(payload.empty);

    // Forward to Vercel API
    await postToApi(machineId, running, empty);
    
  } catch (error) {
    console.error(`[MQTT] Failed to parse message: ${error.message}`);
    console.error(`[MQTT] Raw message: ${message.toString()}`);
  }
}

/**
 * Connect to MQTT broker
 */
function connectMqtt() {
  console.log(`[MQTT] Connecting to ${config.mqttBroker}...`);

  const options = {
    clientId: `laundry-bridge-${Date.now()}`,
    clean: true,
    connectTimeout: 10000,
    reconnectPeriod: 5000,
  };

  // Add authentication if provided
  if (config.mqttUsername) {
    options.username = config.mqttUsername;
    options.password = config.mqttPassword;
    console.log(`[MQTT] Using authentication (user: ${config.mqttUsername})`);
  }

  mqttClient = mqtt.connect(config.mqttBroker, options);

  mqttClient.on('connect', () => {
    console.log('[MQTT] Connected successfully!');
    console.log(`[MQTT] Subscribing to: ${config.mqttTopic}`);
    
    mqttClient.subscribe(config.mqttTopic, { qos: 1 }, (err, granted) => {
      if (err) {
        console.error(`[MQTT] Subscribe error: ${err.message}`);
      } else {
        console.log(`[MQTT] Subscribed to: ${granted.map(g => g.topic).join(', ')}`);
        console.log('\n[BRIDGE] Ready! Waiting for machine status updates...\n');
      }
    });
  });

  mqttClient.on('message', handleMessage);

  mqttClient.on('error', (error) => {
    console.error(`[MQTT] Error: ${error.message}`);
  });

  mqttClient.on('close', () => {
    console.log('[MQTT] Connection closed');
  });

  mqttClient.on('reconnect', () => {
    console.log('[MQTT] Reconnecting...');
  });

  mqttClient.on('offline', () => {
    console.log('[MQTT] Offline');
  });
}

/**
 * Print status
 */
function printStatus() {
  console.log(`\n[STATUS] Messages received: ${messageCount}, Errors: ${errorCount}`);
}

/**
 * Main entry point
 */
function main() {
  console.log('=========================================');
  console.log('  Laundry MQTT Bridge');
  console.log('=========================================');
  console.log(`MQTT Broker:  ${config.mqttBroker}`);
  console.log(`API Endpoint: ${config.apiEndpoint}`);
  console.log(`Topic:        ${config.mqttTopic}`);
  console.log('=========================================\n');

  connectMqtt();

  // Print status every 60 seconds
  setInterval(printStatus, 60000);

  // Handle graceful shutdown
  process.on('SIGINT', () => {
    console.log('\n[BRIDGE] Shutting down...');
    if (mqttClient) {
      mqttClient.end();
    }
    process.exit(0);
  });
}

main();
