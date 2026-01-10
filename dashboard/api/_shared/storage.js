// Simple in-memory storage for machine statuses
// WARNING: This is NOT persistent across serverless function cold starts
// For production, upgrade to Vercel KV: https://vercel.com/docs/storage/vercel-kv

// Global storage (persists only while the serverless instance is warm)
const machineStatuses = new Map();

export async function setMachineStatus(machineId, status) {
  machineStatuses.set(machineId, {
    ...status,
    lastUpdate: new Date().toISOString()
  });
  return true;
}

export async function getMachineStatus(machineId) {
  return machineStatuses.get(machineId) || null;
}

export async function getMachineStatuses() {
  const statuses = {};
  for (const [machineId, status] of machineStatuses.entries()) {
    statuses[machineId] = status;
  }
  return statuses;
}

export async function deleteMachineStatus(machineId) {
  return machineStatuses.delete(machineId);
}
