import { useState, useRef, useEffect } from 'react';
import { Link } from 'react-router-dom';
import { useAuth } from '../../context/AuthContext';
import AuthModal from '../Auth/AuthModal';
import styles from './Navigation.module.css';

export default function Navigation() {
  const [isOpen, setIsOpen] = useState(false);
  const [showAuthModal, setShowAuthModal] = useState(false);
  const navRef = useRef(null);
  const { user, isAuthenticated, logout } = useAuth();

  const toggleMenu = () => {
    setIsOpen(!isOpen);
  };

  const handleLogout = () => {
    logout();
    setIsOpen(false);
  };

  const handleLoginClick = () => {
    setShowAuthModal(true);
    setIsOpen(false);
  };

  // Close menu when clicking outside
  useEffect(() => {
    function handleClickOutside(event) {
      if (navRef.current && !navRef.current.contains(event.target)) {
        setIsOpen(false);
      }
    }

    if (isOpen) {
      document.addEventListener('mousedown', handleClickOutside);
    }

    return () => {
      document.removeEventListener('mousedown', handleClickOutside);
    };
  }, [isOpen]);

  return (
    <>
      <nav className={styles.nav} ref={navRef}>
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
            <Link
              to="/about"
              className={styles.menuButton}
              onClick={() => setIsOpen(false)}
            >
              About
            </Link>
            <Link
              to="/"
              className={styles.menuButton}
              onClick={() => setIsOpen(false)}
            >
              Rooms
            </Link>

            {/* User section */}
            <div className={styles.userSection}>
              {isAuthenticated ? (
                <>
                  <Link
                    to="/my-rooms"
                    className={styles.menuButton}
                    onClick={() => setIsOpen(false)}
                  >
                    My Rooms
                  </Link>
                  <div className={styles.userInfo}>
                    <span className={styles.username}>{user.displayName}</span>
                    <button
                      onClick={handleLogout}
                      className={styles.logoutButton}
                    >
                      Logout
                    </button>
                  </div>
                </>
              ) : (
                <button
                  onClick={handleLoginClick}
                  className={styles.loginButton}
                >
                  Login / Sign Up
                </button>
              )}
            </div>
          </div>
        )}
      </nav>

      <AuthModal isOpen={showAuthModal} onClose={() => setShowAuthModal(false)} />
    </>
  );
}
