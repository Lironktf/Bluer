// Machine type glyphs shown in the top-right corner of each card. They sit
// alongside the text label ("Washer 1"), so they reinforce type rather than
// carry it on their own.

export function WasherIcon({ className }) {
  return (
    <svg
      className={className}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      focusable="false"
    >
      <rect x="4" y="2" width="16" height="20" rx="2.5" />
      <line x1="4" y1="7" x2="20" y2="7" />
      <circle cx="7.5" cy="4.5" r="0.75" fill="currentColor" stroke="none" />
      <circle cx="12" cy="14.5" r="4.75" />
      {/* water line */}
      <path d="M7.6 14.2c.9-.85 1.8-.85 2.7 0s1.8.85 2.7 0 1.8-.85 2.7 0" />
    </svg>
  );
}

export function DryerIcon({ className }) {
  return (
    <svg
      className={className}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      focusable="false"
    >
      <rect x="4" y="2" width="16" height="20" rx="2.5" />
      <line x1="4" y1="7" x2="20" y2="7" />
      <circle cx="7.5" cy="4.5" r="0.75" fill="currentColor" stroke="none" />
      <circle cx="12" cy="14.5" r="4.75" />
      {/* tumble swirl */}
      <path d="M10 12.6a3 3 0 1 1-.9 2.4" />
      <path d="M9.1 12.4v1.9h1.9" />
    </svg>
  );
}

export function WrenchIcon({ className }) {
  return (
    <svg
      className={className}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      focusable="false"
    >
      <path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z" />
    </svg>
  );
}

export function CheckIcon({ className }) {
  return (
    <svg
      className={className}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2.5"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      focusable="false"
    >
      <path d="M20 6L9 17l-5-5" />
    </svg>
  );
}
