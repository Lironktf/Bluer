import { useEffect } from 'react';

// Calls fn now and every ms while the tab is visible. A hidden tab makes no requests,
// and catches up the moment it is shown again.
export function usePolling(fn, ms) {
  useEffect(() => {
    let interval = null;
    const start = () => {
      if (interval || document.hidden) return;
      fn();
      interval = setInterval(fn, ms);
    };
    const stop = () => {
      clearInterval(interval);
      interval = null;
    };
    const onVisibility = () => (document.hidden ? stop() : start());

    start();
    document.addEventListener('visibilitychange', onVisibility);
    return () => {
      stop();
      document.removeEventListener('visibilitychange', onVisibility);
    };
  }, [fn, ms]);
}
