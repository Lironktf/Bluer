import { useState } from 'react';
import styles from './Navigation.module.css';

export default function Navigation() {
  const [isOpen, setIsOpen] = useState(false);

  const toggleMenu = () => {
    setIsOpen(!isOpen);
  };

  return (
    <nav className={styles.nav}>
      {/* Hamburger button */}
      <button
        className={styles.hamburger}
        onClick={toggleMenu}
        aria-label="Toggle menu"
      >
        <span className={styles.line}></span>
        <span className={styles.line}></span>
        <span className={styles.line}></span>
      </button>

      {/* Menu buttons that appear when open */}
      {isOpen && (
        <div className={styles.menuButtons}>
          <a
            href="/about/"
            className={styles.menuButton}
          >
            About
          </a>
          <a
            href="/"
            className={styles.menuButton}
            onClick={(e) => {
              e.preventDefault();
              setIsOpen(false);
            }}
          >
            Rooms
          </a>
        </div>
      )}
    </nav>
  );
}
