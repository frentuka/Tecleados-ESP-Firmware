import { useState, useEffect } from 'react';

export function useTouchMode(): boolean {
  const [isTouchMode, setIsTouchMode] = useState(false);

  useEffect(() => {
    const onTouch = () => setIsTouchMode(true);
    const onMouse = () => setIsTouchMode(false);

    window.addEventListener('touchstart', onTouch, { passive: true, capture: true });
    window.addEventListener('mousemove', onMouse, { passive: true, capture: true });

    return () => {
      window.removeEventListener('touchstart', onTouch, true);
      window.removeEventListener('mousemove', onMouse, true);
    };
  }, []);

  return isTouchMode;
}
