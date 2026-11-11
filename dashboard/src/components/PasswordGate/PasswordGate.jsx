import { useState, useCallback } from 'react';
import { clearAdminToken, getAdminToken, logIn } from '../../utils/adminApi';
import styles from './PasswordGate.module.css';

/** Renders children once the server has accepted the password; stays unlocked for 7 days. */
export default function PasswordGate({ children }) {
  const [unlocked, setUnlocked] = useState(() => Boolean(getAdminToken()));
  const [value, setValue] = useState('');
  const [error, setError] = useState(null);
  const [checking, setChecking] = useState(false);

  // Stable identity: children's polling effects depend on it.
  const lock = useCallback(() => {
    clearAdminToken();
    setUnlocked(false);
  }, []);

  if (unlocked) {
    return children({ lock });
  }

  const submit = async (e) => {
    e.preventDefault();
    setChecking(true);
    setError(null);
    const { ok, status, data } = await logIn(value.trim());
    setChecking(false);
    if (ok) {
      setValue('');
      setUnlocked(true);
    } else {
      setError(status === 401 ? 'Wrong password.' : data?.error || `Could not reach the server (${status}).`);
    }
  };

  return (
    <form className={styles.gate} onSubmit={submit}>
      <label htmlFor="node-admin-password">Password</label>
      <input
        id="node-admin-password"
        type="password"
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
