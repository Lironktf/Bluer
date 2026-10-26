import { useState, useCallback } from 'react';
import { adminFetch, getAdminPassword, setAdminPassword } from '../../utils/adminApi';
import styles from './PasswordGate.module.css';

// Set true and restore NODE_ADMIN_PASSWORD in Vercel to re-enable the gate.
const PASSWORD_PROTECTION_ENABLED = false;

/** Renders children only once the server has accepted the password. */
export default function PasswordGate({ children }) {
  const [unlocked, setUnlocked] = useState(Boolean(getAdminPassword()));
  const [value, setValue] = useState('');
  const [error, setError] = useState(null);
  const [checking, setChecking] = useState(false);

  // Stable identity: children's polling effects depend on it.
  const lock = useCallback(() => {
    setAdminPassword('');
    setUnlocked(false);
  }, []);

  if (!PASSWORD_PROTECTION_ENABLED) {
    return children({ lock: () => {} });
  }

  if (unlocked && getAdminPassword()) {
    return children({ lock });
  }

  const submit = async (e) => {
    e.preventDefault();
    setChecking(true);
    setError(null);
    setAdminPassword(value.trim());
    const { ok, status, data } = await adminFetch('kind=nodes');
    setChecking(false);
    if (ok) {
      setUnlocked(true);
    } else {
      setAdminPassword('');
      setError(status === 401 ? 'Wrong password.' : data?.error || `Could not reach the server (${status}).`);
    }
  };

  return (
    <form className={styles.gate} onSubmit={submit}>
      <label htmlFor="node-admin-password">Password</label>
      <input
        id="node-admin-password"
        type="password"
        inputMode="numeric"
        autoComplete="current-password"
        value={value}
        onChange={(e) => setValue(e.target.value)}
        autoFocus
      />
      <button type="submit" disabled={checking || !value}>
        {checking ? 'Checking…' : 'Unlock'}
      </button>
      {error && <p className={styles.error}>{error}</p>}
    </form>
  );
}
