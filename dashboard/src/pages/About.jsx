import { Link } from 'react-router-dom';
import styles from './About.module.css';

export default function About() {
  return (
    <div className={styles.container}>
      <div className={styles.content}>
        <h1>About LaunDryer</h1>

        <section className={styles.section}>
          <h2>What is LaunDryer?</h2>
          <p>
            LaunDryer is a real-time laundry machine monitoring system that helps you track
            the status of washing machines and dryers. Using ESP32 microcontrollers and
            accelerometer sensors, we detect when machines are running and when they're empty
            and ready to use.
          </p>
        </section>

        <section className={styles.section}>
          <h2>How It Works</h2>
          <ul>
            <li>
              <strong>Sensor Nodes:</strong> ESP32 devices with MPU6050 accelerometers monitor
              vibrations to detect machine activity
            </li>
            <li>
              <strong>BLE Communication:</strong> Sensor nodes broadcast status via Bluetooth Low
              Energy for minimal power consumption
            </li>
            <li>
              <strong>Gateway:</strong> A router ESP32 receives BLE signals and forwards data
              to the cloud via WiFi
            </li>
            <li>
              <strong>Live Dashboard:</strong> This web interface displays real-time machine status
            </li>
          </ul>
        </section>

        <section className={styles.section}>
          <h2>Technology Stack</h2>
          <div className={styles.techGrid}>
            <div className={styles.techItem}>
              <h3>Hardware</h3>
              <p>ESP32, MPU6050 Accelerometer</p>
            </div>
            <div className={styles.techItem}>
              <h3>Firmware</h3>
              <p>Arduino C++, BLE Protocol</p>
            </div>
            <div className={styles.techItem}>
              <h3>Backend</h3>
              <p>Vercel Serverless Functions</p>
            </div>
            <div className={styles.techItem}>
              <h3>Frontend</h3>
              <p>React, Vite</p>
            </div>
          </div>
        </section>

        <Link to="/" className={styles.backButton}>
          ← Back to Dashboard
        </Link>
      </div>
    </div>
  );
}
