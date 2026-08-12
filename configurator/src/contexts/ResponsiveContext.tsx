import { createContext, useContext, ReactNode } from 'react';
import { useViewport } from '../hooks/useViewport';
import { useTouchMode } from '../hooks/useTouchMode';

interface ResponsiveContextType {
  tier: 'mobile' | 'tablet' | 'desktop';
  isTouchMode: boolean;
  isPortrait: boolean;
  isMobile: boolean;
}

const ResponsiveContext = createContext<ResponsiveContextType>({
  tier: 'desktop',
  isTouchMode: false,
  isPortrait: false,
  isMobile: false,
});

export function ResponsiveProvider({ children }: { children: ReactNode }) {
  const viewport = useViewport();
  const isTouchMode = useTouchMode();
  
  return (
    <ResponsiveContext.Provider value={{
      tier: viewport.tier,
      isTouchMode,
      isPortrait: viewport.isPortrait,
      isMobile: viewport.tier === 'mobile',
    }}>
      {children}
    </ResponsiveContext.Provider>
  );
}

export const useResponsive = () => useContext(ResponsiveContext);
