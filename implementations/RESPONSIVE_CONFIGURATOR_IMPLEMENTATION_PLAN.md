# Responsive Configurator — Implementation Plan

> Make the [configurator](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src) fully usable on tactile (touch) and portrait-oriented devices, from phones (≥ 360 px) to tablets (≥ 768 px) to desktops (≥ 1024 px).

---

## Task Tracker

> Mark items `[/]` when in progress, `[x]` when complete.

### Cross-Cutting Infrastructure
- [x] **CI-1** — Create `src/hooks/useViewport.ts` (viewport tier, dimensions, portrait detection)
- [x] **CI-2** — Create `src/assets/css/responsive.css` (centralized `@media` overrides)
- [x] **CI-2** — Import `responsive.css` last in `src/index.css`
- [x] **CI-3** — Create `src/hooks/useTouchMode.ts` (last-input-modality detection)
- [x] **CI-3** — Create `src/contexts/ResponsiveContext.tsx` (provider + `useResponsive` hook)
- [x] **CI-3** — Wrap `<App />` with `<ResponsiveProvider>` in `src/main.tsx`

### Phase 1 — Structural Shell
- [ ] **1.1** — Modify `Background3D.tsx` — CSS gradient fallback on non-desktop tiers; 3D canvas never initializes on mobile/tablet
- [ ] **1.2** — Add `#root` mobile & tablet overrides in `responsive.css`
- [ ] **1.3** — Add `.main-header` mobile overrides (56px height, compact padding)
- [ ] **1.3** — Hide `.brand-title`, `.brand-separator`, `.brand-device-name` on mobile
- [ ] **1.3** — Create `src/components/CompactStatusBadge.tsx`; wire into `App.tsx`
- [ ] **1.4** — Create `src/components/MobileSidebar.tsx` (bottom tab bar + full-screen sheet with swipe-to-dismiss)
- [ ] **1.4** — Create `src/assets/css/mobile-sidebar.css`
- [ ] **1.4** — Modify `Sidebar.tsx` — branch to `MobileSidebar` on mobile tier
- [ ] **1.4** — Hide `.sidebar-container` on mobile via `responsive.css`
- [ ] **1.4** — Migrate `sidebar.css` breakpoint from `1100px` → `1024px`

### Phase 2 — Core Views
- [ ] **2.1** — Dynamic `UNIT` scaling based on container width on mobile
- [ ] **2.1** — Pinch-to-zoom gesture handler on keyboard grid
- [ ] **2.1** — `.keyboard-grid` / `.keyboard-key` mobile overrides; `overscroll-behavior` + `touch-action`
- [ ] **2.2** — `.layout-toolbar` mobile overrides (full-width, horizontal scroll, compact tabs)
- [ ] **2.2** — Show `.layout-tab-delete-btn` on active tab for touch (no hover required)
- [ ] **2.3** — `.accordion-modal` full-screen mobile overrides; 2×2 button grid; 48px key targets
- [ ] **2.3** — Modify `SearchableKeyModal.tsx` — bottom-aligned overlay on mobile
- [ ] **2.4** — `.key-action-popover` touch overrides (wider, larger buttons)

### Phase 3 — Sidebar Panels
- [ ] **3.1** — Macros dashboard mobile overrides (padding, card sizing, search bar)
- [ ] **3.2** — Custom Keys dashboard mobile overrides
- [ ] **3.3** — Combos dashboard mobile overrides

### Phase 4 — Secondary Views & Modals
- [ ] **4.1** — Settings modal full-width / bottom-sheet on mobile
- [ ] **4.2** — Dev Console modal full-width / bottom-sheet on mobile
- [ ] **4.3** — Connect modal mobile overrides (margin-top, larger transport buttons)
- [ ] **4.4** — Global Export / Import modals mobile overrides
- [ ] **4.5** — Macro Editor modal full-screen on mobile; larger element row tap targets
- [ ] **4.6** — Onboarding tooltip / welcome card mobile sizing
- [ ] **4.7** — Notifications positioned above mobile tab bar; full-width
- [ ] **4.8** — Disconnected overlay / content mobile overrides

### Phase 5 — Touch Interaction Adaptations
- [ ] **5.1** — Add `onTouchStart` / `onTouchEnd` to keyboard keys (tap-to-edit, long-press multi-select)
- [ ] **5.1** — Guard existing `onMouseDown` handlers with `isTouchMode` check
- [ ] **5.2** — Add `TouchSensor` (delay 250ms) to DnD Kit sensors for layer tab reorder
- [ ] **5.3** — Global `@media (hover: none)` block neutralizing all hover transforms
- [ ] **5.3** — `KeyActionPopover` — show on tap (not hover) on touch devices
- [ ] **5.4** — `overscroll-behavior-y: none` on `html`; `overscroll-behavior-x: contain` on keyboard grid

### Phase 6 — Performance & Bundle
- [ ] **6.1** — Convert `Background3D` import to `React.lazy()` in `App.tsx`; wrap with `<Suspense>`
- [ ] **6.1** — Verify Three.js chunk not downloaded on mobile (DevTools Network tab)
- [ ] **6.2** — Update `index.html` viewport meta tag (`viewport-fit=cover`)

### Verification
- [ ] `npm run build` succeeds with no errors
- [ ] **Mobile (360px)** — Full manual test pass (see Verification Matrix in plan)
- [ ] **Tablet (768px portrait)** — Full manual test pass
- [ ] **Desktop (1440px)** — No regression from existing behavior
- [ ] Touch interactions verified on a real device or emulator
- [ ] Safe area insets verified on iPhone (notch / home indicator)

---

## Table of Contents

- [Goals & Non-Goals](#goals--non-goals)
- [Breakpoint Strategy](#breakpoint-strategy)
- [Cross-Cutting Infrastructure](#cross-cutting-infrastructure)
  - [CI-1 — `useViewport` Hook](#ci-1--useviewport-hook)
  - [CI-2 — CSS Custom Properties & Breakpoint Tokens](#ci-2--css-custom-properties--breakpoint-tokens)
  - [CI-3 — Touch Detection & Input-Mode Adapter](#ci-3--touch-detection--input-mode-adapter)
- [Phase 1 — Structural Shell](#phase-1--structural-shell)
  - [1.1 Background3D: Conditional Rendering](#11-background3d-conditional-rendering)
  - [1.2 Root & Body Layout](#12-root--body-layout)
  - [1.3 Main Header](#13-main-header)
  - [1.4 Sidebar → Mobile Bottom-Sheet / Tab-Bar](#14-sidebar--mobile-bottom-sheet--tab-bar)
- [Phase 2 — Core Views](#phase-2--core-views)
  - [2.1 Keyboard Layout Editor](#21-keyboard-layout-editor)
  - [2.2 Layout Toolbar](#22-layout-toolbar)
  - [2.3 Key Selection Modal (SearchableKeyModal)](#23-key-selection-modal-searchablekeymodal)
  - [2.4 Key Action Popover](#24-key-action-popover)
- [Phase 3 — Sidebar Panels (Macros, CKeys, Combos)](#phase-3--sidebar-panels-macros-ckeys-combos)
  - [3.1 Macros Dashboard](#31-macros-dashboard)
  - [3.2 Custom Keys Dashboard](#32-custom-keys-dashboard)
  - [3.3 Combos Dashboard](#33-combos-dashboard)
- [Phase 4 — Secondary Views & Modals](#phase-4--secondary-views--modals)
  - [4.1 Settings Modal](#41-settings-modal)
  - [4.2 Dev Console Modal](#42-dev-console-modal)
  - [4.3 Connect Modal](#43-connect-modal)
  - [4.4 Global Export / Import Modals](#44-global-export--import-modals)
  - [4.5 Macro Editor Modal](#45-macro-editor-modal)
  - [4.6 Onboarding Wizard](#46-onboarding-wizard)
  - [4.7 Notifications & Toasts](#47-notifications--toasts)
  - [4.8 Disconnected Overlay](#48-disconnected-overlay)
- [Phase 5 — Touch Interaction Adaptations](#phase-5--touch-interaction-adaptations)
  - [5.1 Key Selection: Touch vs Mouse](#51-key-selection-touch-vs-mouse)
  - [5.2 Drag-and-Drop Layer Reorder](#52-drag-and-drop-layer-reorder)
  - [5.3 Hover States & Popovers](#53-hover-states--popovers)
  - [5.4 Scroll & Gesture Isolation](#54-scroll--gesture-isolation)
- [Phase 6 — Performance & Bundle](#phase-6--performance--bundle)
  - [6.1 Three.js Lazy-Loading](#61-threejs-lazy-loading)
  - [6.2 Viewport Meta Tag](#62-viewport-meta-tag)
- [File Change Manifest](#file-change-manifest)
- [Verification Plan](#verification-plan)
- [Risk Matrix](#risk-matrix)

---

## Goals & Non-Goals

### Goals

| # | Goal |
|---|------|
| G1 | The configurator must be **fully functional** on portrait phones (≥ 360 px width), tablets (≥ 768 px), and desktops (≥ 1024 px). |
| G2 | All interactive flows — connecting, editing keys, editing macros / ckeys / combos, changing layers — must be **usable via touch only** (no hover, no right-click, no keyboard shortcuts required). |
| G3 | The 3D background should be **conditionally hidden** on small / touch devices to conserve GPU, memory, and avoid rendering issues. |
| G4 | Existing desktop experience should **not regress** — no layout shifts, no interaction changes for landscape desktop users. |
| G5 | The CSS approach should use **mobile-first progressive enhancement** with well-defined breakpoints, avoiding ad-hoc `@media` queries sprinkled across files. |

### Non-Goals

| # | Non-Goal |
|---|----------|
| NG1 | Implementing a fully native mobile app or PWA (service workers, offline mode). |
| NG2 | Redesigning the UI from scratch — the current design language is preserved. |
| NG3 | Supporting screens narrower than 360 px. |
| NG4 | Implementing landscape-tablet-specific layouts (tablet landscape = desktop layout). |

---

## Breakpoint Strategy

The configurator will use **three primary breakpoints**, referenced throughout this document:

| Token | Min Width | Target Devices | Short Name |
|-------|-----------|----------------|------------|
| `--bp-mobile` | 0 – 767 px | Phones (portrait & landscape) | **Mobile** |
| `--bp-tablet` | 768 – 1023 px | Tablets in portrait, small laptops | **Tablet** |
| `--bp-desktop` | ≥ 1024 px | Laptops, desktops, tablets in landscape | **Desktop** |

> [!NOTE]
> The existing `1100px` breakpoint in [sidebar.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/sidebar.css#L482-L535) will be migrated to `1024px` for consistency with the above token set.

**CSS structure:** A new file [responsive.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/responsive.css) will be created and imported **last** in `index.css`. It will contain **only** `@media` overrides, organized by breakpoint, then by component. This keeps all responsive rules centralized and avoids scattering `@media` blocks across 15 CSS files.

---

## Cross-Cutting Infrastructure

### CI-1 — `useViewport` Hook

#### [NEW] `src/hooks/useViewport.ts`

A lightweight React hook that exposes the current viewport tier and touch capability.

```ts
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
```

**Rationale:** Debounced via `requestAnimationFrame` to avoid excessive re-renders during resize. The `isTouchDevice` flag allows components to branch on interaction model (hover vs tap) independent of viewport size.

---

### CI-2 — CSS Custom Properties & Breakpoint Tokens

#### [NEW] `src/assets/css/responsive.css`

This file will contain all responsive overrides, organized as:

```
/* ═══════════════════════════════════════════════
   RESPONSIVE OVERRIDES
   Import this file LAST in index.css
   ═══════════════════════════════════════════════ */

/* ── Mobile (up to 767px) ── */
@media (max-width: 767px) {
  /* ... all mobile overrides grouped by component ... */
}

/* ── Tablet (768px – 1023px) ── */
@media (min-width: 768px) and (max-width: 1023px) {
  /* ... all tablet overrides grouped by component ... */
}

/* ── Touch device overrides (interaction model) ── */
@media (hover: none) and (pointer: coarse) {
  /* ... touch-specific overrides (tap target sizes, hover removal) ... */
}
```

#### [MODIFY] `src/index.css`

Add import at the end:

```css
@import './assets/css/responsive.css';
```

---

### CI-3 — Touch Detection & Input-Mode Adapter

#### [NEW] `src/hooks/useTouchMode.ts`

A hook that tracks whether the user is currently in "touch interaction mode" vs "pointer mode." This is distinct from `isTouchDevice` because laptops with touchscreens use both.

```ts
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
```

**Rationale:** This "last input modality" approach is the industry standard (used by macOS and Android internally). It lets hybrid devices (e.g. Surface) dynamically switch between touch-optimized and mouse-optimized layouts.

#### [MODIFY] `src/App.tsx`

Pass `isTouchMode` and `viewport.tier` as context or props to children that need them. A lightweight React context (`ResponsiveContext`) will be created to avoid prop-drilling:

```tsx
// src/contexts/ResponsiveContext.tsx
import { createContext, useContext } from 'react';
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

export function ResponsiveProvider({ children }: { children: React.ReactNode }) {
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
```

---

## Phase 1 — Structural Shell

### 1.1 Background3D: Conditional Rendering

#### [MODIFY] [Background3D.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/components/Background3D.tsx)

**Problem:** The 3D background uses Three.js + react-three-fiber, loading heavy GPU resources. On mobile/tablet devices, it provides no value (too small to appreciate), introduces scroll/touch-event interference, and wastes GPU/battery.

**Solution:** Conditionally render the 3D `<Canvas>` only on desktop-tier devices. Replace with a lightweight **CSS radial-gradient background** on mobile/tablet, preserving the visual warmth.

```tsx
// Inside Background3D default export:
export default function Background3D() {
  const viewport = useViewport();
  const isConnected = useLayoutStore(state => state.isConnected);
  const render3DModel = useUiStore(state => state.render3DModel);

  // ① On sub-desktop viewports, render only the CSS gradient
  if (viewport.tier !== 'desktop') {
    return (
      <div style={{
        position: 'fixed',
        top: 0, left: 0, width: '100vw', height: '100vh',
        zIndex: -10,
        background: 'radial-gradient(circle at 50% 35%, #2d1304 0%, #0c0501 55%, #050201 100%)',
        pointerEvents: 'none',
      }} />
    );
  }

  // ② Desktop path: existing 3D canvas (unchanged)
  // ... existing code ...
}
```

**Impact:** On mobile/tablet, Three.js never initializes. The `three` chunk (~500 KB gzipped) won't even be parsed.

> [!IMPORTANT]
> The `pointerEvents: 'auto'` on the 3D background container (for mouse-drag rotation on desktop) is changed to `'none'` on the CSS-only fallback. This prevents the gradient from swallowing touch events on mobile.

---

### 1.2 Root & Body Layout

#### [MODIFY] [index.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/index.css) — `#root` rules

**Current state:**
```css
#root {
  padding: 0 2rem;
  padding-right: 92px; /* 76px sidebar rail + 16px gap */
  max-width: 1500px;
  margin: 80px auto 0 auto;
}
```

The fixed `padding-right: 92px` and `margin-top: 80px` assume a permanent right-side sidebar rail and a 70px header. Both of these must become responsive.

**Changes in `responsive.css`:**

```css
/* ── Mobile ── */
@media (max-width: 767px) {
  #root {
    padding: 0 0.75rem;
    padding-right: 0.75rem;       /* No sidebar rail on mobile */
    margin-top: 56px;             /* Compact header */
    padding-bottom: 72px;         /* Space for bottom tab bar */
    max-width: 100%;
  }
}

/* ── Tablet ── */
@media (min-width: 768px) and (max-width: 1023px) {
  #root {
    padding: 0 1.25rem;
    padding-right: 1.25rem;       /* Sidebar overlays on tablet */
    margin-top: 64px;
    max-width: 100%;
  }
}
```

---

### 1.3 Main Header

#### [MODIFY] [index.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/index.css) — `.main-header` and descendants

**Current state:** The header is a fixed 70px bar with three flex sections: `.header-left` (brand), `.header-center` (connect button), `.header-right` (status widget). This works on landscape but becomes cramped on mobile.

**Mobile adaptation (`responsive.css`):**

| Element | Mobile Behavior |
|---------|-----------------|
| `.main-header` | Height reduced to **56px**. Padding reduced. |
| `.header-left .brand-title` | Hidden (only logo icon shows). |
| `.header-left .brand-separator` | Hidden. |
| `.header-left .brand-device-name` | Hidden (device name is visible in the sidebar or status). |
| `.header-center` | Remains centered. Connection button stays. |
| `.header-right .status-wrapper` | Collapsed to an icon-only display. The full `StatusWidget` opens as a popover on tap. |

```css
@media (max-width: 767px) {
  .main-header {
    height: 56px;
    padding: 0 0.75rem;
  }

  .brand-title,
  .brand-separator,
  .brand-device-name {
    display: none;
  }

  .header-left {
    gap: 0;
    flex: 0 0 auto;
  }

  .header-right {
    flex: 0 0 auto;
  }
}
```

#### [MODIFY] [App.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/App.tsx) — StatusWidget mobile rendering

On mobile, the `StatusWidget` should condense to a small icon/badge. Tapping it opens the full widget in a bottom-sheet or popover.

```tsx
// Conditional render in the header-right section:
{viewport.isMobile ? (
  <CompactStatusBadge onClick={() => setStatusSheetOpen(true)} ... />
) : (
  <StatusWidget ... />  // existing full widget
)}
```

A new `CompactStatusBadge` component renders just the colored dot and transport icon, acting as a tap target.

---

### 1.4 Sidebar → Mobile Bottom-Sheet / Tab-Bar

This is the **most architecturally significant change** in the entire plan.

#### Current Architecture

The sidebar is a fixed-right panel with:
- An **icon rail** (76px wide) always visible
- An **expandable panel** (420px wide) that slides left when a tab is active
- Content: Macros, CKeys, Combos dashboards rendered as children

This layout is fundamentally desktop-only. On mobile, a right-side rail is both unreachable (in one-handed use) and wastes horizontal space.

#### Mobile Strategy: Bottom Tab Bar + Full-Screen Sheets

On **mobile** (`< 768px`):

1. The sidebar **icon rail** transforms into a **fixed bottom tab bar** (56px high), containing the same icons: Macros, CKeys, Combos, Settings.
2. When a tab is tapped, the corresponding dashboard opens as a **full-screen bottom sheet** that slides up from below, covering the keyboard editor entirely.
3. A swipe-down gesture or close button dismisses the sheet.

On **tablet** (`768px – 1023px`):

1. The existing overlay-mode sidebar is preserved (the `@media (max-width: 1100px)` behavior in sidebar.css already handles this).
2. The breakpoint is adjusted from `1100px` to `1024px`.
3. The icon rail remains on the right side but at a slimmer **60px** width.

#### [MODIFY] [Sidebar.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/Sidebar.tsx)

```tsx
export default function Sidebar({ ... }: SidebarProps) {
  const { isMobile } = useResponsive();

  if (isMobile) {
    return <MobileSidebar {...props} />;
  }

  return <DesktopSidebar {...props} />;
}
```

The `DesktopSidebar` is the existing component, unchanged.

#### [NEW] `src/components/MobileSidebar.tsx`

```
┌──────────────────────────────────────────┐
│             (Main Content)               │
│                                          │
│                                          │
├──────────────────────────────────────────┤ ← bottom sheet slides up from here
│  [Full-screen dashboard content]         │
│  (Macros / CKeys / Combos / Settings)    │
│                                          │
│  [Close / swipe-down to dismiss]         │
│                                          │
├──────────────────────────────────────────┤
│ 🔧  📦  🎯  ⚙️                          │ ← 56px bottom tab bar
└──────────────────────────────────────────┘
```

**Implementation details:**

- The bottom tab bar uses `position: fixed; bottom: 0` with a frosted-glass background matching the existing glassmorphism.
- Active tab indicator: a small accent-colored dot or line above the active icon.
- Sheet animation: `transform: translateY(100%)` → `translateY(0)` with the standard cubic-bezier easing from the existing codebase.
- Swipe-to-dismiss: a simple `touchstart`/`touchmove`/`touchend` handler on the sheet header that tracks vertical movement and dismisses if the delta exceeds a threshold.

#### [NEW] `src/assets/css/mobile-sidebar.css`

All mobile sidebar styling. Key rules:

```css
.mobile-tab-bar {
  position: fixed;
  bottom: 0;
  left: 0;
  right: 0;
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: space-around;
  background: rgba(13, 17, 23, 0.85);
  backdrop-filter: blur(20px);
  -webkit-backdrop-filter: blur(20px);
  border-top: 1px solid rgba(255, 255, 255, 0.08);
  z-index: 1000;
  padding-bottom: env(safe-area-inset-bottom); /* iPhone notch */
}

.mobile-sheet-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  z-index: 900;
}

.mobile-sheet {
  position: fixed;
  bottom: 0;
  left: 0;
  right: 0;
  top: 56px;    /* Below header */
  background: rgba(13, 17, 23, 0.95);
  backdrop-filter: blur(28px);
  border-radius: 16px 16px 0 0;
  z-index: 950;
  transform: translateY(100%);
  transition: transform 0.4s cubic-bezier(0.16, 1, 0.3, 1);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.mobile-sheet.open {
  transform: translateY(0);
}

.mobile-sheet-handle {
  width: 36px;
  height: 4px;
  background: rgba(255, 255, 255, 0.2);
  border-radius: 2px;
  margin: 8px auto;
  flex-shrink: 0;
}
```

> [!TIP]
> The `env(safe-area-inset-bottom)` CSS function ensures the bottom tab bar doesn't overlap the iPhone home indicator or Android gesture bar.

---

#### [MODIFY] [sidebar.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/sidebar.css)

Migrate the existing `@media (max-width: 1100px)` rules to use the new `1024px` breakpoint. Additionally, hide the desktop sidebar entirely on mobile:

```css
@media (max-width: 767px) {
  .sidebar-container {
    display: none; /* Replaced by MobileSidebar */
  }
}
```

---

## Phase 2 — Core Views

### 2.1 Keyboard Layout Editor

#### [MODIFY] [KeyboardLayoutEditor.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/KeyboardLayoutEditor.tsx)

The keyboard grid is the most challenging element to make responsive. It's a dynamic CSS grid of keys with varying widths based on `PhysKey` metadata.

#### Problem Analysis

The current rendering uses absolute positioning via computed `x`, `y`, `w`, `h` values in unit-space, multiplied by a `UNIT` constant (currently computed from the container width). The container has `overflowX: auto`, which already allows horizontal scrolling. However, on mobile:

1. The keys are **too small to tap** reliably.
2. Horizontal scrolling on a portrait phone is unintuitive for a keyboard layout.
3. The `onMouseDown` / `onMouseEnter` / `onMouseUp` event model doesn't work with touch.

#### Solution: Horizontal-Scroll + Zoom Controls

**Rendering approach (kept):** The absolute-positioned grid is preserved — it's the only approach that handles arbitrary physical layouts (rotated keys, split keyboards). Rewriting it as a pure CSS grid would lose rotation and fractional-unit support.

**Mobile adaptations:**

1. **Dynamic `UNIT` scaling:** Compute `UNIT` to fit the keyboard width within the viewport, with a minimum key height of **40px** (Apple's minimum tap target). If this means the keyboard exceeds the viewport width, allow horizontal scroll.

2. **Pinch-to-zoom:** On touch devices, add a pinch gesture handler that adjusts the `UNIT` scale factor within `[0.5, 1.5]` of the computed optimal size. Store this in a `ref` to avoid re-renders during the gesture.

3. **Mobile key padding:** The `.keyboard-grid` padding is reduced from `2.25rem` to `0.75rem` on mobile.

4. **Key font size:** Scale down from `0.7rem` to `0.6rem` on mobile to fit within smaller keys.

**Changes in `responsive.css`:**

```css
@media (max-width: 767px) {
  .keyboard-grid {
    padding: 0.75rem;
    border-radius: 8px;
  }

  .keyboard-key {
    font-size: 0.6rem;
    border-radius: 4px;
  }

  .key-label {
    font-size: 0.6rem;
  }

  .key-pos {
    display: none;   /* Hide position labels on mobile */
  }
}
```

#### [MODIFY] [KeyboardLayoutEditor.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/KeyboardLayoutEditor.tsx) — `UNIT` calculation

Inside the rendering IIFE that computes the keyboard grid:

```tsx
// Current: UNIT is a fixed constant or derived from physical layout
// New: derive UNIT from container width on mobile

const containerRef = useRef<HTMLDivElement>(null);
const { isMobile } = useResponsive();

// In the render IIFE:
const containerWidth = containerRef.current?.clientWidth ?? window.innerWidth;
const naturalWidth = (maxKeyX - minKeyX) * UNIT;
const scaleFactor = isMobile
  ? Math.min(1, (containerWidth - 24) / naturalWidth)  // 24px = 2 * 12px padding
  : 1;
const effectiveUnit = UNIT * scaleFactor;
```

---

### 2.2 Layout Toolbar

#### [MODIFY] [keyboard-layout.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/keyboard-layout.css) — `.layout-toolbar`

**Problem:** The toolbar is `inline-flex` with all layer tabs side by side. On mobile with 4+ layers, it overflows.

**Solution (Mobile):**

1. The toolbar becomes **full-width** and scrollable horizontally, with the layer tabs in a horizontal scroll container.
2. The layer tab pill sizes are reduced.
3. The dropdown menu button remains at the end.

```css
@media (max-width: 767px) {
  .layout-toolbar {
    display: flex;
    width: 100%;
    margin: 0 0 1rem;
    padding: 0.35rem 0.5rem;
    border-radius: 10px;
    gap: 0.4rem;
    overflow-x: auto;
    -webkit-overflow-scrolling: touch;
  }

  .layout-tabs-group {
    overflow-x: auto;
    flex-shrink: 1;
    min-width: 0;
  }

  .layout-tab-pill {
    padding: 0.35rem 0.75rem;
    font-size: 0.8rem;
    flex-shrink: 0;
  }

  .layout-tab-delete-btn {
    /* On touch: don't rely on hover, show on active/focus */
    opacity: 0;
  }

  /* On touch devices, show delete via long-press or always */
  @media (hover: none) {
    .layout-tab-pill.layout-tab-pill-active .layout-tab-delete-btn {
      opacity: 0.6;
      pointer-events: auto;
      transform: translateX(-50%) scale(0.9);
    }
  }
}
```

---

### 2.3 Key Selection Modal (SearchableKeyModal)

#### [MODIFY] [searchable-key-modal.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/searchable-key-modal.css)

**Current:** The accordion modal is `max-width: 500px` with category buttons and a key grid.

**Mobile adaptation:**
- The modal becomes **full-screen** on mobile (no rounded corners, no horizontal margins).
- The accordion buttons stack as a 2×2 grid instead of 4-across.
- The key option grid items have larger tap targets (min `48px` height).

```css
@media (max-width: 767px) {
  .accordion-modal {
    max-width: 100%;
    width: 100%;
    height: 100%;
    border-radius: 0;
    display: flex;
    flex-direction: column;
  }

  .accordion-menu-row {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0.5rem;
  }

  .accordion-btn {
    padding: 0.6rem;
  }

  .key-option-grid {
    grid-template-columns: repeat(auto-fill, minmax(80px, 1fr));
    gap: 8px;
  }

  .key-option {
    min-height: 48px;
    padding: 0.6rem 0.4rem;
  }

  .accordion-smooth-content {
    flex: 1;
    overflow-y: auto;
    padding: 0 1rem 1rem;
  }
}
```

#### [MODIFY] [SearchableKeyModal.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/components/SearchableKeyModal.tsx)

On mobile, the modal overlay should use `align-items: flex-end` (bottom sheet) or `stretch` (full-screen) instead of `center`.

---

### 2.4 Key Action Popover

#### [MODIFY] [key-action-popover.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/key-action-popover.css)

**Problem:** The popover appears on hover, which doesn't exist on touch devices. On mobile, it should appear on **tap** and be larger.

**Solution:**
- On touch devices, tapping a key opens the popover (see Phase 5 for the interaction change).
- The popover size increases for touch targets.

```css
@media (hover: none) and (pointer: coarse) {
  .key-action-popover {
    min-width: 260px;
    max-width: 320px;
    padding: 1.25rem;
  }

  .key-action-popover-btn {
    min-height: 44px;
    font-size: 0.9rem;
  }
}
```

---

## Phase 3 — Sidebar Panels (Macros, CKeys, Combos)

### 3.1 Macros Dashboard

#### [MODIFY] [macros-dashboard.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/macros-dashboard.css)

**Current:** The macros dashboard is designed for the 420px-wide sidebar panel. The macro cards are vertical-stack with `min-height: 80px`.

**Mobile adaptation:** When rendered inside the `MobileSidebar` full-screen sheet, the dashboard should:
- Use the full viewport width minus safe area margins.
- The search bar and card list should stretch to fill.
- No changes to the card layout itself — it's already a vertical stack.

```css
@media (max-width: 767px) {
  .macros-dashboard,
  .ckey-dashboard {
    padding: 0.75rem 0 0 0;
  }

  .list-scroll-area {
    padding: 0 0.75rem 1rem;
  }

  .macro-card {
    padding: 0.85rem 1rem !important;
    min-height: 70px;
  }

  .sidebar-search-container {
    padding: 0 0.75rem;
  }
}
```

---

### 3.2 Custom Keys Dashboard

#### [MODIFY] [custom-keys-dashboard.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/custom-keys-dashboard.css)

Similar mobile adaptations as macros. The `ckey-card` has `max-height: 170px` which is fine for mobile.

```css
@media (max-width: 767px) {
  .ckey-card {
    min-height: 150px;
    max-height: 150px;
  }

  .ckey-dashboard-header {
    padding: 0 0.75rem;
  }
}
```

---

### 3.3 Combos Dashboard

#### [MODIFY] [CombosDashboard.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/CombosDashboard.tsx)

The combos dashboard has similar layout patterns to macros. The same mobile overrides apply.

---

## Phase 4 — Secondary Views & Modals

### 4.1 Settings Modal

#### [MODIFY] [sidebar.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/sidebar.css) — `.settings-modal-content`

On mobile, modals should be full-screen or near-full-screen:

```css
@media (max-width: 767px) {
  .settings-modal-content {
    width: 100%;
    max-width: 100%;
    max-height: 100vh;
    border-radius: 16px 16px 0 0;
    margin-top: auto;           /* Push to bottom for bottom-sheet feel */
  }

  .settings-modal-overlay {
    align-items: flex-end;
  }
}
```

---

### 4.2 Dev Console Modal

#### [MODIFY] [sidebar.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/sidebar.css) — `.devconsole-modal-content`

Same full-screen treatment as Settings on mobile.

---

### 4.3 Connect Modal

#### [MODIFY] [connectModal.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/connectModal.css)

The connect modal positions itself just below the header. On mobile, the smaller header height (56px) means adjusting the `margin-top`:

```css
@media (max-width: 767px) {
  .connect-modal {
    margin-top: 60px;
  }

  .connect-modal .btn-transport {
    width: 52px;
    height: 52px;   /* Larger tap targets on touch */
  }
}
```

---

### 4.4 Global Export / Import Modals

#### [MODIFY] [import-export.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/import-export.css)

These modals contain checkboxes and lists. On mobile:

```css
@media (max-width: 767px) {
  /* Make modal full-width, scrollable */
  .global-export-modal,
  .global-import-modal {
    width: 100%;
    max-width: 100%;
    max-height: 90vh;
    border-radius: 16px 16px 0 0;
  }
}
```

---

### 4.5 Macro Editor Modal

#### [MODIFY] [macros-dashboard.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/macros-dashboard.css)

The macro editor modal is the most complex modal. It contains:
- A timeline of macro elements
- Drag-and-drop reordering
- Inline editing of delays, key presses, etc.

On mobile:

```css
@media (max-width: 767px) {
  /* Full-screen editor */
  .macro-editor-modal {
    width: 100%;
    max-width: 100%;
    max-height: 100vh;
    height: 100vh;
    border-radius: 0;
  }

  .macro-element-row {
    padding: 0.75rem;
    min-height: 48px;   /* Touch-friendly row height */
  }
}
```

---

### 4.6 Onboarding Wizard

#### [MODIFY] [onboarding.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/assets/css/onboarding.css)

The tooltip is already `max-width: calc(100vw - 2rem)`, which is good. The onboarding welcome card may need centering adjustments on mobile:

```css
@media (max-width: 767px) {
  .onboarding-tooltip {
    width: calc(100vw - 2rem);
    padding: 1.25rem;
  }

  .onboarding-welcome-card {
    width: calc(100vw - 2rem);
    max-width: 100%;
  }
}
```

---

### 4.7 Notifications & Toasts

#### [MODIFY] [index.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/index.css) — `.notification-toast`

```css
@media (max-width: 767px) {
  .notification-toast {
    width: calc(100vw - 2rem);
    max-width: calc(100vw - 2rem);
    left: 1rem;
    transform: translateX(0) translateY(20px);
    bottom: 72px;          /* Above the mobile tab bar */
  }

  .notification-toast.visible {
    transform: translateX(0) translateY(0);
  }

  .permissions-help {
    width: calc(100vw - 2rem);
    max-width: calc(100vw - 2rem);
    left: 1rem;
    transform: translateX(0) translateY(20px);
    bottom: 72px;
  }

  .permissions-help.visible {
    transform: translateX(0) translateY(0);
  }
}
```

---

### 4.8 Disconnected Overlay

#### [MODIFY] [index.css](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/index.css) — `.disconnected-overlay`

```css
@media (max-width: 767px) {
  .disconnected-overlay {
    padding: 1rem;
    margin-top: 0;
  }

  .disconnected-content h2 {
    font-size: 1.3rem;
    white-space: normal;
    width: auto;
  }

  .disconnected-content p {
    font-size: 0.9rem;
  }

  .disconnected-icon {
    width: 80px;
    height: 80px;
  }

  .pulse-ring {
    width: 140px;
    height: 140px;
  }
}
```

---

## Phase 5 — Touch Interaction Adaptations

This phase is **critical** — it addresses the fundamental interaction model differences between mouse and touch.

### 5.1 Key Selection: Touch vs Mouse

#### Current Mouse Interaction

| Gesture | Action |
|---------|--------|
| Click | Select key, open edit modal on second click |
| Ctrl+Click | Toggle key in multi-selection |
| Shift+Click | Range select |
| Drag (mouse-down + move) | Drag-select multiple keys |
| Hover | Show key action popover |
| Right-click (context menu) | N/A (blocked) |

#### Proposed Touch Interaction

| Gesture | Action |
|---------|--------|
| **Tap** | Select key → immediately open edit modal (single key edit) |
| **Long-press** (300ms) | Enter multi-select mode; first key is selected. Subsequent taps toggle keys. A floating "Edit N keys" button appears. |
| **Long-press + drag** | Drag-select (touch equivalent of mouse-drag-select) |
| **Tap on key** (when popover-worthy, e.g. macro/ckey) | Show key action popover as a bottom-attached tooltip |
| **Pinch** | Zoom keyboard (see 2.1) |

#### [MODIFY] [KeyboardLayoutEditor.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/KeyboardLayoutEditor.tsx) — Key interaction handlers

The existing `onMouseDown` / `onMouseEnter` / `onMouseUp` handlers need touch equivalents:

```tsx
// Add to the keyboard-key button:
onTouchStart={(e) => {
  if (!isTouchMode) return;
  e.preventDefault();
  touchStartRef.current = {
    keyId: physKeyId,
    timestamp: Date.now(),
    x: e.touches[0].clientX,
    y: e.touches[0].clientY,
  };
  longPressTimerRef.current = setTimeout(() => {
    // Enter multi-select mode
    setIsMultiSelectMode(true);
    setSelectedKeys(new Set([physKeyId]));
  }, 300);
}}
onTouchEnd={(e) => {
  if (!isTouchMode) return;
  clearTimeout(longPressTimerRef.current);
  const touch = touchStartRef.current;
  if (!touch) return;

  const elapsed = Date.now() - touch.timestamp;
  if (elapsed < 300 && !isMultiSelectMode) {
    // Short tap: select + open modal
    setSelectedKeys(new Set([physKeyId]));
    setIsModalOpen(true);
  } else if (isMultiSelectMode) {
    // Tap in multi-select mode: toggle
    setSelectedKeys(prev => {
      const next = new Set(prev);
      if (next.has(physKeyId)) next.delete(physKeyId);
      else next.add(physKeyId);
      return next;
    });
  }
  touchStartRef.current = null;
}}
```

> [!IMPORTANT]
> The existing `onMouseDown` handlers must be wrapped with `if (isTouchMode) return;` guards to prevent both touch and mouse events from firing on hybrid devices.

---

### 5.2 Drag-and-Drop Layer Reorder

#### [MODIFY] [KeyboardLayoutEditor.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/KeyboardLayoutEditor.tsx) — DnD Kit sensors

The `@dnd-kit` library used for layer tab reordering needs touch sensor configuration:

```tsx
const sensors = useSensors(
  useSensor(PointerSensor, {
    activationConstraint: { distance: 8 },
  }),
  useSensor(KeyboardSensor, {
    coordinateGetter: sortableKeyboardCoordinates,
  }),
  // Add touch sensor
  useSensor(TouchSensor, {
    activationConstraint: { delay: 250, tolerance: 5 },
  }),
);
```

> [!NOTE]
> `@dnd-kit/core` already exports `TouchSensor`. We just need to import and configure it with an activation delay to distinguish taps from drags.

---

### 5.3 Hover States & Popovers

#### Global Touch Override (`responsive.css`)

All `:hover` pseudo-class effects that cause visual changes (transforms, shadows, color shifts) should be gated behind `@media (hover: hover)` to prevent "sticky hover" on touch devices:

```css
@media (hover: none) {
  .keyboard-key:hover {
    transform: none;
    box-shadow: none;
    border-color: rgba(255, 255, 255, 0.12);
  }

  .glass-panel:hover {
    transform: none;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
  }

  .macro-card:hover {
    transform: none;
  }

  .btn:hover {
    transform: none;
  }

  .btn-new-action:hover:not(:disabled) {
    transform: none;
  }
}
```

#### Popover Behavior on Touch

The `KeyActionPopover` currently appears on `onMouseEnter`. On touch:

```tsx
// In KeyboardLayoutEditor.tsx, replace hover-based popover with:
if (isTouchMode) {
  // Popover shows on tap, positioned above/below the key
  // Dismissed by tapping elsewhere
} else {
  // Existing hover behavior
}
```

---

### 5.4 Scroll & Gesture Isolation

**Problem:** On mobile, horizontal scroll on the keyboard grid can conflict with page-level vertical scroll and with the browser's back/forward swipe gestures.

**Solution:**

```css
/* Prevent horizontal overscroll from triggering browser navigation */
.keyboard-grid {
  overscroll-behavior-x: contain;
  -webkit-overflow-scrolling: touch;
}

/* Prevent pull-to-refresh on the main content area */
html {
  overscroll-behavior-y: none;
}
```

Additionally, the keyboard grid container should use `touch-action: pan-x pan-y` (not `manipulation`) to allow both scroll directions while preventing unintended zoom.

---

## Phase 6 — Performance & Bundle

### 6.1 Three.js Lazy-Loading

#### [MODIFY] [App.tsx](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/src/App.tsx)

Currently, `Background3D` is imported statically at the top of `App.tsx`:

```tsx
import Background3D from './components/Background3D';
```

This causes the Three.js chunk (~500 KB gzipped) to be loaded on every device, even if it's never rendered. Change to:

```tsx
const Background3D = lazy(() => import('./components/Background3D'));

// In render:
{viewport.tier === 'desktop' && (
  <Suspense fallback={<div style={{ ... gradient background ... }} />}>
    <Background3D />
  </Suspense>
)}
```

This ensures mobile/tablet users never download the Three.js bundle.

---

### 6.2 Viewport Meta Tag

#### [MODIFY] [index.html](file:///home/srleg/Projects/Tecleados-ESP-Firmware/configurator/index.html)

Ensure the viewport meta tag is correct:

```html
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
```

- `viewport-fit=cover` enables `env(safe-area-inset-*)` for notch-aware layouts.
- `user-scalable=no` and `maximum-scale=1.0` prevent accidental pinch-zoom on the page (we handle zoom ourselves on the keyboard grid only).

---

## File Change Manifest

| File | Action | Phase |
|------|--------|-------|
| `src/hooks/useViewport.ts` | **NEW** | CI-1 |
| `src/hooks/useTouchMode.ts` | **NEW** | CI-3 |
| `src/contexts/ResponsiveContext.tsx` | **NEW** | CI-3 |
| `src/assets/css/responsive.css` | **NEW** | CI-2 |
| `src/assets/css/mobile-sidebar.css` | **NEW** | 1.4 |
| `src/components/MobileSidebar.tsx` | **NEW** | 1.4 |
| `src/components/CompactStatusBadge.tsx` | **NEW** | 1.3 |
| `index.html` | MODIFY | 6.2 |
| `src/index.css` | MODIFY | 1.2, CI-2 |
| `src/App.tsx` | MODIFY | CI-3, 1.3, 6.1 |
| `src/components/Background3D.tsx` | MODIFY | 1.1 |
| `src/Sidebar.tsx` | MODIFY | 1.4 |
| `src/KeyboardLayoutEditor.tsx` | MODIFY | 2.1, 5.1, 5.2 |
| `src/components/SearchableKeyModal.tsx` | MODIFY | 2.3 |
| `src/components/KeyActionPopover.tsx` | MODIFY | 2.4, 5.3 |
| `src/assets/css/sidebar.css` | MODIFY | 1.4 |
| `src/assets/css/keyboard-layout.css` | MODIFY | 2.2 |
| `src/assets/css/searchable-key-modal.css` | MODIFY | 2.3 |
| `src/assets/css/key-action-popover.css` | MODIFY | 2.4 |
| `src/assets/css/macros-dashboard.css` | MODIFY | 3.1, 4.5 |
| `src/assets/css/custom-keys-dashboard.css` | MODIFY | 3.2 |
| `src/assets/css/connectModal.css` | MODIFY | 4.3 |
| `src/assets/css/import-export.css` | MODIFY | 4.4 |
| `src/assets/css/onboarding.css` | MODIFY | 4.6 |
| `src/assets/css/device-dashboard.css` | MODIFY | Tablet adjustments |
| `src/assets/css/split-dashboard.css` | MODIFY | Tablet adjustments |
| `src/StatusWidget.tsx` | MODIFY | 1.3 |

**Total: 8 new files, ~20 modified files**

---

## Verification Plan

### Automated Testing

Since this is a CSS/layout-heavy change, automated visual regression testing is recommended:

```bash
# Build for production to verify no build errors
cd configurator && npm run build
```

### Manual Verification Matrix

Each of these scenarios must be tested across three device categories:

| # | Scenario | Mobile (360px) | Tablet (768px) | Desktop (1440px) |
|---|----------|:--------------:|:--------------:|:----------------:|
| V1 | App loads, gradient background visible | ☐ | ☐ | ☐ |
| V2 | 3D background renders | N/A | N/A | ☐ |
| V3 | Header is visible, brand shows correctly | ☐ | ☐ | ☐ |
| V4 | Connect button works | ☐ | ☐ | ☐ |
| V5 | Keyboard grid renders, all keys visible | ☐ | ☐ | ☐ |
| V6 | Keyboard grid scrolls horizontally (if wider than viewport) | ☐ | ☐ | N/A |
| V7 | Key tap opens edit modal | ☐ | ☐ | ☐ |
| V8 | Layer tabs are accessible and functional | ☐ | ☐ | ☐ |
| V9 | Sidebar / bottom-sheet opens (Macros tab) | ☐ | ☐ | ☐ |
| V10 | Macros list scrolls, cards are tappable | ☐ | ☐ | ☐ |
| V11 | Macro editor modal opens full-screen (mobile) / centered (desktop) | ☐ | ☐ | ☐ |
| V12 | CKeys tab works identically | ☐ | ☐ | ☐ |
| V13 | Combos tab works identically | ☐ | ☐ | ☐ |
| V14 | Settings modal opens and closes | ☐ | ☐ | ☐ |
| V15 | Onboarding wizard is legible and functional | ☐ | ☐ | ☐ |
| V16 | Notifications appear above tab bar (mobile) / bottom-center (desktop) | ☐ | ☐ | ☐ |
| V17 | Disconnected state displays correctly | ☐ | ☐ | ☐ |
| V18 | No horizontal page-level scroll on any view | ☐ | ☐ | ☐ |
| V19 | Multi-key selection works via long-press (mobile) / click (desktop) | ☐ | ☐ | ☐ |
| V20 | Pinch-to-zoom on keyboard grid works (mobile only) | ☐ | N/A | N/A |

### Browser / Device Targets

| Device | Browser | Priority |
|--------|---------|----------|
| iPhone 14 / 15 (Safari) | Safari iOS 17+ | High |
| Pixel 7 / 8 (Chrome) | Chrome Android 120+ | High |
| iPad Air (Safari, portrait) | Safari iPadOS 17+ | Medium |
| Chrome DevTools (responsive mode) | Chrome Desktop | For development |
| Desktop (Chrome, Firefox) | Chrome 120+, Firefox 120+ | High (regression) |

---

## Risk Matrix

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Keyboard grid too small on 360px phones | Medium | High | Implement pinch-to-zoom and auto-fit UNIT scaling. Consider a "landscape lock" prompt for phones during key editing. |
| Touch events interfere with keyboard grid scroll | Medium | High | Use `touch-action: pan-x pan-y` and `overscroll-behavior-x: contain`. |
| Sticky hover states on touch devices | High | Low | Global `@media (hover: none)` override block neutralizes all hover transforms. |
| Bottom tab bar overlaps with iOS home indicator | Low | Medium | `env(safe-area-inset-bottom)` padding on the tab bar. |
| DnD kit touch sensor conflicts with scroll | Medium | Medium | Use `delay: 250` activation constraint. Test on real devices. |
| Three.js lazy-load causes flash of unstyled content on desktop | Low | Low | The `Suspense` fallback renders the same CSS gradient as the non-desktop path. |
| 3D background interactions (drag-to-rotate) collide with scroll on large tablets in landscape | Low | Medium | The 3D background is only rendered on `desktop` tier (≥ 1024px). Tablets in landscape at 1024px+ will get it, but the gradient fallback is acceptable at the boundary. |
| Macro editor modal timeline is hard to use on small screens | Medium | Medium | Full-screen modal + larger touch targets on element rows. Drag-reorder delay prevents accidental reorders. |
