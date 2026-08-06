// Anonymous per-browser identity used to deduplicate machine reports.
//
// There are no user accounts, so this is the only identity we have. It is
// trivially resettable (clearing site data or opening a private window gets a
// new one), which is why a report threshold only ever *flags* a machine with a
// warning rather than hiding it. See api/reports.js.

const STORAGE_KEY = 'bluer_device_id';

function randomId() {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
    return crypto.randomUUID();
  }
  // Older Safari / non-secure contexts.
  return `d-${Math.random().toString(36).slice(2)}${Date.now().toString(36)}`;
}

export function getDeviceId() {
  try {
    const existing = window.localStorage.getItem(STORAGE_KEY);
    if (existing) return existing;

    const created = randomId();
    window.localStorage.setItem(STORAGE_KEY, created);
    return created;
  } catch {
    // Storage blocked (private mode, cookies disabled). Fall back to a
    // per-session id so reporting still works, just without dedupe.
    return randomId();
  }
}
