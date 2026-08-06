import { useState } from 'react';
import styles from './About.module.css';

export default function About() {
  const [activeTab, setActiveTab] = useState('attach');

  return (
    <div className={styles.aboutPage}>
      <div className={styles.container}>
        {/* Hero Section with Washing Machine */}
        <section className={styles.hero}>
          <div className={styles.washingMachine}>
            <svg viewBox="0 0 200 240" xmlns="http://www.w3.org/2000/svg">
              {/* Washing Machine Body */}
              <rect x="20" y="20" width="160" height="200" rx="8" fill="none" stroke="currentColor" strokeWidth="2"/>

              {/* Control Panel */}
              <rect x="20" y="20" width="160" height="40" rx="8" fill="none" stroke="currentColor" strokeWidth="2"/>
              <line x1="20" y1="60" x2="180" y2="60" stroke="currentColor" strokeWidth="2"/>

              {/* Power Button */}
              <circle cx="50" cy="40" r="6" fill="none" stroke="currentColor" strokeWidth="1.5"/>

              {/* Detergent Dispenser */}
              <rect x="70" y="32" width="30" height="16" rx="2" fill="none" stroke="currentColor" strokeWidth="1.5"/>

              {/* Settings Dial */}
              <circle cx="150" cy="40" r="12" fill="none" stroke="currentColor" strokeWidth="1.5"/>
              <line x1="150" y1="40" x2="150" y2="32" stroke="currentColor" strokeWidth="1.5"/>

              {/* Door/Window */}
              <circle cx="100" cy="140" r="50" fill="none" stroke="currentColor" strokeWidth="2"/>
              <circle cx="100" cy="140" r="42" fill="none" stroke="currentColor" strokeWidth="1.5" opacity="0.3"/>
            </svg>
          </div>

          <h1>Bluer</h1>
          <p className={styles.tagline}>Be more informed</p>
        </section>

        {/* What Is This Section */}
        <section className={styles.whatIsThis}>
          <h2>What is this?</h2>
          <p>Bluer is a smart sensor that attaches to the back of any washing machine. Using advanced vibration and sound detection, it monitors machine activity in real-time, giving you instant access to laundry room occupancy from your phone or web browser.</p>
          <p>Perfect for university dorms, apartment buildings, and shared laundry spaces.</p>
        </section>

        {/* How It Works Section */}
        <section className={styles.howItWorks}>
          <h2>How it works</h2>

          <div className={styles.tabs}>
            <button
              className={`${styles.tabButton} ${activeTab === 'attach' ? styles.active : ''}`}
              onClick={() => setActiveTab('attach')}
            >
              Attach
            </button>
            <button
              className={`${styles.tabButton} ${activeTab === 'configure' ? styles.active : ''}`}
              onClick={() => setActiveTab('configure')}
            >
              Configure
            </button>
            <button
              className={`${styles.tabButton} ${activeTab === 'monitor' ? styles.active : ''}`}
              onClick={() => setActiveTab('monitor')}
            >
              Monitor
            </button>
            <button
              className={`${styles.tabButton} ${activeTab === 'availability' ? styles.active : ''}`}
              onClick={() => setActiveTab('availability')}
            >
              Check Availability
            </button>
          </div>

          <div className={styles.tabContent}>
            <div className={`${styles.tabPanel} ${activeTab === 'attach' ? styles.active : ''}`}>
              <div className={styles.stepNumber}>01</div>
              <h3>Attach</h3>
              <p>Simply attach the sensor to the back of your washing machine. No tools required.</p>
            </div>

            <div className={`${styles.tabPanel} ${activeTab === 'configure' ? styles.active : ''}`}>
              <div className={styles.stepNumber}>02</div>
              <h3>Configure</h3>
              <p>Run a quick training cycle to teach the sensor your machine's unique signature. It learns the vibration patterns and sounds specific to your washer.</p>
            </div>

            <div className={`${styles.tabPanel} ${activeTab === 'monitor' ? styles.active : ''}`}>
              <div className={styles.stepNumber}>03</div>
              <h3>Monitor</h3>
              <p>The sensor continuously detects when the machine is running, idle, or available. Check real-time status from our app or website.</p>
            </div>

            <div className={`${styles.tabPanel} ${activeTab === 'availability' ? styles.active : ''}`}>
              <div className={styles.stepNumber}>04</div>
              <h3>Check Availability</h3>
              <p>View occupancy status of all machines in your building. Get notified when a machine becomes available.</p>
            </div>
          </div>
        </section>

        {/* Technology Section */}
        <section className={styles.technology}>
          <h2>The technology</h2>
          <div className={styles.techFeatures}>
            {/* All three icons are drawn to the same 4..20 bounding box inside
                the 24x24 viewBox, with matching stroke weight, so they carry
                equal optical weight and sit on a common centre line. */}
            <div className={styles.feature}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" xmlns="http://www.w3.org/2000/svg">
                <path d="M12 3.5 4.5 6.75v5.5c0 4.15 3 7.9 7.5 8.75 4.5-.85 7.5-4.6 7.5-8.75v-5.5L12 3.5Z"/>
                <path d="m9.25 12.25 2 2 3.5-3.5"/>
              </svg>
              <h4>Vibration Detection</h4>
              <p>High-precision accelerometer tracks machine movement patterns</p>
            </div>

            <div className={styles.feature}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" xmlns="http://www.w3.org/2000/svg">
                <rect x="9.25" y="3.5" width="5.5" height="10" rx="2.75"/>
                <path d="M17.5 11.25a5.5 5.5 0 0 1-11 0"/>
                <line x1="12" y1="16.75" x2="12" y2="20.5"/>
                <line x1="8.25" y1="20.5" x2="15.75" y2="20.5"/>
              </svg>
              <h4>Sound Analysis</h4>
              <p>Machine learning algorithms identify wash cycle acoustics</p>
            </div>

            <div className={styles.feature}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" xmlns="http://www.w3.org/2000/svg">
                <circle cx="12" cy="12" r="3.25"/>
                <path d="M12 4v2.5M12 17.5V20M4 12h2.5M17.5 12H20"/>
                <path d="m6.4 6.4 1.75 1.75M15.85 15.85 17.6 17.6M17.6 6.4l-1.75 1.75M8.15 15.85 6.4 17.6"/>
              </svg>
              <h4>Adaptive Learning</h4>
              <p>Continuously improves accuracy by learning your machine's behavior</p>
            </div>
          </div>
        </section>

        {/* Footer */}
        <footer className={styles.footer}>
          <p>&copy; 2025 Bluer. Be Informed.</p>
        </footer>
      </div>
    </div>
  );
}
