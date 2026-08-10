import styles from './Footer.module.css';

export default function Footer() {
  return (
    <footer className={styles.footer}>
      <p className={styles.credit}>Made by Liron Katsif &amp; Oliver Simser</p>
      <p className={styles.copyright}>&copy; {new Date().getFullYear()} Bluer</p>
    </footer>
  );
}
