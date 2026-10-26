// Password-protected calls to /api/diagnostics for the node settings and live pages.
const API_BASE_URL = import.meta.env.VITE_API_URL || 'https://laun-dryer.vercel.app';
const STORAGE_KEY = 'bluerNodeAdminPassword';

export function getAdminPassword() {
  return sessionStorage.getItem(STORAGE_KEY) || '';
}

export function setAdminPassword(password) {
  if (password) sessionStorage.setItem(STORAGE_KEY, password);
  else sessionStorage.removeItem(STORAGE_KEY);
}

/** Returns { ok, status, data }. A 401 forgets the stored password so the gate shows again. */
export async function adminFetch(query, { method = 'GET', body } = {}) {
  let response;
  try {
    response = await fetch(`${API_BASE_URL}/api/diagnostics?${query}`, {
      method,
      headers: {
        'Content-Type': 'application/json',
        'X-Admin-Password': getAdminPassword(),
      },
      body: body ? JSON.stringify(body) : undefined,
    });
  } catch {
    return { ok: false, status: 0, data: { error: 'Network error: could not reach the server.' } };
  }
  let data = null;
  try {
    data = await response.json();
  } catch {
    data = null;
  }
  if (response.status === 401) setAdminPassword('');
  return { ok: response.ok && data?.success !== false, status: response.status, data };
}

export function formatAgo(iso) {
  if (!iso) return '--';
  const s = Math.round((Date.now() - new Date(iso).getTime()) / 1000);
  if (s < 90) return `${s}s ago`;
  const m = Math.round(s / 60);
  if (m < 90) return `${m}m ago`;
  const h = Math.round(m / 60);
  if (h < 48) return `${h}h ago`;
  return `${Math.round(h / 24)}d ago`;
}

export function formatRemaining(iso) {
  if (!iso) return '';
  const s = Math.max(0, Math.round((new Date(iso).getTime() - Date.now()) / 1000));
  const m = Math.floor(s / 60);
  return `${m}:${String(s % 60).padStart(2, '0')} left`;
}

/** "sj-3" -> "Pair 3 (Dryer 3 + Washer 3)", "TEST-1" -> "TEST Node 1". */
export function nodeLabel(nodeId) {
  const [prefix, num] = nodeId.split('-');
  if (prefix === 'TEST') return `TEST Node ${num}`;
  if (prefix === 'sj') return `Pair ${num} (Dryer ${num} + Washer ${num})`;
  return nodeId;
}
