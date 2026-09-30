import styles from './Footer.module.css';

export default function Footer() {
  return (
    <footer className={styles.footer}>
      <p className={styles.credit}>Made by Oliver Simser &amp; Liron Katsif</p>
      <p className={styles.copyright}>&copy; {new Date().getFullYear()} Bluer</p>
    </footer>
  );
}
