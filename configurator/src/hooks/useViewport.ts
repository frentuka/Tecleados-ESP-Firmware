import { useState, useEffect } from 'react';

type ViewportTier = 'mobile' | 'tablet' | 'desktop';

interface ViewportInfo {
  tier: ViewportTier;
  isTouchDevice: boolean;
  width: number;
  height: number;
  isPortrait: boolean;
}

export function useViewport(): ViewportInfo {
  const getTier = (w: number): ViewportTier =>
    w < 768 ? 'mobile' : w < 1024 ? 'tablet' : 'desktop';

  const isTouchDevice = () =>
    'ontouchstart' in window || navigator.maxTouchPoints > 0;

  const getInfo = (): ViewportInfo => ({
    tier: getTier(window.innerWidth),
    isTouchDevice: isTouchDevice(),
    width: window.innerWidth,
    height: window.innerHeight,
    isPortrait: window.innerHeight > window.innerWidth,
  });

  const [info, setInfo] = useState(getInfo);

  useEffect(() => {
    let rafId: number;
    const onResize = () => {
      cancelAnimationFrame(rafId);
      rafId = requestAnimationFrame(() => setInfo(getInfo()));
    };
    window.addEventListener('resize', onResize, { passive: true });
    return () => {
      window.removeEventListener('resize', onResize);
      cancelAnimationFrame(rafId);
    };
  }, []);

  return info;
}
