import { Link, useLocation } from 'react-router-dom';
import styles from './Navigation.module.css';

// Accounts are not surfaced in the UI right now. The auth backend, AuthContext,
// AuthModal and the /my-rooms page all still exist -- adding a link back here is
// all it takes to bring them back.
const LINKS = [
  { to: '/', label: 'Rooms' },
  { to: '/about', label: 'About' },
];

export default function Navigation() {
  const { pathname } = useLocation();

  return (
    <div className={styles.wrap}>
      <header className={styles.bar}>
        <Link to="/" className={styles.brand}>
          <img src="/favicon.png" alt="" className={styles.mark} width="28" height="28" />
          <span className={styles.wordmark}>Bluer</span>
        </Link>

        <nav className={styles.links} aria-label="Main">
          {LINKS.map((link) => (
            <Link
              key={link.to}
              to={link.to}
              className={`${styles.link} ${pathname === link.to ? styles.active : ''}`}
              aria-current={pathname === link.to ? 'page' : undefined}
            >
              {link.label}
            </Link>
          ))}
        </nav>
      </header>
    </div>
  );
}
