// Password-protected calls to /api/diagnostics for the node settings and live pages.
// The password is sent once; the server answers with a 7-day token, kept so open tabs stay unlocked.
const API_BASE_URL = import.meta.env.VITE_API_URL || 'https://laun-dryer.vercel.app';
const TOKEN_KEY = 'bluerNodeAdminToken';

export function getAdminToken() {
  try {
    const saved = JSON.parse(localStorage.getItem(TOKEN_KEY) || 'null');
    if (saved?.token && new Date(saved.expiresAt).getTime() > Date.now()) return saved.token;
  } catch {
    // unreadable entry: treat as logged out
  }
  return '';
}

export function clearAdminToken() {
  localStorage.removeItem(TOKEN_KEY);
}

function diagnosticsUrl(query) {
  return `${API_BASE_URL}/api/diagnostics?${query}`;
}

async function request(query, { method = 'GET', body, headers }) {
  let response;
  try {
    response = await fetch(diagnosticsUrl(query), {
      method,
      headers: { 'Content-Type': 'application/json', ...headers },
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
  return { ok: response.ok && data?.success !== false, status: response.status, data };
}

/** Checks the password and keeps the token the server returns. Returns { ok, status, data }. */
export async function logIn(password) {
  const result = await request('kind=auth', { headers: { 'X-Admin-Password': password } });
  if (result.ok) {
    localStorage.setItem(TOKEN_KEY, JSON.stringify({ token: result.data.token, expiresAt: result.data.expiresAt }));
  }
  return result;
}

/** Returns { ok, status, data }. A 401 forgets the token so the gate shows again. */
export async function adminFetch(query, { method = 'GET', body } = {}) {
  const result = await request(query, { method, body, headers: { 'X-Admin-Token': getAdminToken() } });
  if (result.status === 401) clearAdminToken();
  return result;
}

/** Downloads a file from the API with the token; a plain link can't send it. Returns an error message or null. */
export async function downloadFile(query, fallbackName) {
  let response;
  try {
    response = await fetch(diagnosticsUrl(query), { headers: { 'X-Admin-Token': getAdminToken() } });
  } catch {
    return 'Network error: could not reach the server.';
  }
  if (!response.ok) {
    if (response.status === 401) clearAdminToken();
    const data = await response.json().catch(() => null);
    return data?.error || `HTTP ${response.status}`;
  }
  const name = /filename="([^"]+)"/.exec(response.headers.get('Content-Disposition') || '')?.[1] || fallbackName;
  const url = URL.createObjectURL(await response.blob());
  const link = document.createElement('a');
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
  return null;
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

/** "sj-3" -> "Pair 3 (Dryer 3 + Washer 3)", "TEST-1" -> "TEST Node 1". */
export function nodeLabel(nodeId) {
  const [prefix, num] = nodeId.split('-');
  if (prefix === 'TEST') return `TEST Node ${num}`;
  if (prefix === 'sj') return `Pair ${num} (Dryer ${num} + Washer ${num})`;
  return nodeId;
}
