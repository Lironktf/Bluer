import { useState, useEffect, useRef } from 'react';

export function useLocalStorage(key, initialValue) {
  // Lazy initialization: read from localStorage on mount
  const [storedValue, setStoredValue] = useState(() => {
    try {
      const item = window.localStorage.getItem(key);
      return item ? JSON.parse(item) : initialValue;
    } catch (error) {
      console.error('Error reading from localStorage:', error);
      return initialValue;
    }
  });

  // Persist on change rather than inside the setter, so that passing an updater
  // function works the same way it does with useState. Writing in the setter
  // used to hand the function itself to JSON.stringify, which serialises to
  // undefined -- localStorage got the string "undefined" and JSON.parse threw
  // on the next load, silently discarding the value.
  const isFirstRun = useRef(true);

  useEffect(() => {
    if (isFirstRun.current) {
      isFirstRun.current = false;
      return;
    }

    try {
      window.localStorage.setItem(key, JSON.stringify(storedValue));
    } catch (error) {
      console.error('Error writing to localStorage:', error);
    }
  }, [key, storedValue]);

  return [storedValue, setStoredValue];
}
