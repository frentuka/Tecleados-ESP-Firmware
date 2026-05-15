import { Canvas, useFrame, useThree } from '@react-three/fiber'
import { useRef, useMemo, useState, useEffect } from 'react'
import * as THREE from 'three'
import { Environment, Float, ContactShadows, RoundedBox } from '@react-three/drei'
import { Geometry, Base, Subtraction } from '@react-three/csg'
import { useLayoutStore } from '../stores/layoutStore'
import { getKeyClass } from '../KeyDefinitions'
import { getKeyColor } from '../utils/keyColors'

const createNoiseNormalMap = (size = 256, intensity = 30, repeat = 1) => {
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext('2d');
  if (!ctx) return null;
  
  ctx.fillStyle = '#8080ff';
  ctx.fillRect(0, 0, size, size);
  
  const imageData = ctx.getImageData(0, 0, size, size);
  const data = imageData.data;
  
  for (let i = 0; i < data.length; i += 4) {
    const noiseX = (Math.random() - 0.5) * intensity;
    const noiseY = (Math.random() - 0.5) * intensity;
    data[i] = 128 + noiseX;
    data[i+1] = 128 + noiseY;
  }
  
  ctx.putImageData(imageData, 0, 0);
  const texture = new THREE.CanvasTexture(canvas);
  texture.wrapS = THREE.RepeatWrapping;
  texture.wrapT = THREE.RepeatWrapping;
  texture.repeat.set(repeat, repeat);
  return texture;
};

const pbtNormalMap = createNoiseNormalMap(256, 50, 2);
const aluNormalMap = createNoiseNormalMap(512, 25, 4);

function MOAKeycap({ width, depth, position, color, userData }: { width: number, depth: number, position: [number, number, number], color: string, userData?: any }) {
  const bevelSize = 0.12;
  const shape = useMemo(() => {
    const s = new THREE.Shape();
    const w = width - 2 * bevelSize;
    const d = depth - 2 * bevelSize;
    const targetR = Math.min(0.25, Math.min(width, depth) / 2 - 0.01);
    const r = Math.max(0.01, targetR - bevelSize);

    s.moveTo(-w / 2 + r, -d / 2);
    s.lineTo(w / 2 - r, -d / 2);
    s.absarc(w / 2 - r, -d / 2 + r, r, -Math.PI / 2, 0, false);
    s.lineTo(w / 2, d / 2 - r);
    s.absarc(w / 2 - r, d / 2 - r, r, 0, Math.PI / 2, false);
    s.lineTo(-w / 2 + r, d / 2);
    s.absarc(-w / 2 + r, d / 2 - r, r, Math.PI / 2, Math.PI, false);
    s.lineTo(-w / 2, -d / 2 + r);
    s.absarc(-w / 2 + r, -d / 2 + r, r, Math.PI, Math.PI * 1.5, false);
    return s;
  }, [width, depth]);

  const extrudeSettings = useMemo(() => ({
    depth: 0.05,
    bevelEnabled: true,
    bevelSegments: 12,
    bevelSize: bevelSize,
    bevelThickness: 0.35
  }), [bevelSize]);

  const [x, y, z] = position;
  const dishDepth = 0.04;
  const capsuleR = 2.0;
  const topZ = extrudeSettings.depth + extrudeSettings.bevelThickness;
  const capsuleZ = topZ + capsuleR - dishDepth;
  
  const isWide = width >= depth;
  const capsuleLength = Math.abs(width - depth);
  const capsuleRot: [number, number, number] = isWide ? [0, 0, Math.PI / 2] : [0, 0, 0];

  return (
    <group position={[x, y - 0.2, z]}>
      <mesh rotation={[-Math.PI / 2, 0, 0]} castShadow receiveShadow userData={userData}>
        <Geometry>
          <Base>
            <extrudeGeometry args={[shape, extrudeSettings]} />
          </Base>
          <Subtraction position={[0, 0, capsuleZ]} rotation={capsuleRot}>
            <capsuleGeometry args={[capsuleR, capsuleLength, 16, 32]} />
          </Subtraction>
        </Geometry>
        <meshPhysicalMaterial
          color={color}
          roughness={0.75}
          metalness={0.02}
          clearcoat={0.15}
          clearcoatRoughness={0.6}
          normalMap={pbtNormalMap || undefined}
          normalScale={new THREE.Vector2(0.6, 0.6)}
          transparent
        />
      </mesh>
    </group>
  );
}

function LayoutElements({ physicalLayout }: { physicalLayout: any[] | null }) {
  const renderedElements = useMemo(() => {
    const unitSize = 0.92;
    const gap = 0.08;
    const totalUnit = unitSize + gap;

    if (!physicalLayout || physicalLayout.length === 0) {
      return null;
    }

  // Helper to compute rotated bounding boxes
  const rotatePoint = (x: number, y: number, cx: number, cy: number, deg: number): [number, number] => {
    const rad = deg * Math.PI / 180;
    const cos = Math.cos(rad), sin = Math.sin(rad);
    const dx = x - cx, dy = y - cy;
    return [cx + dx * cos - dy * sin, cy + dx * sin + dy * cos];
  };

  // 1. Detect split
  const usedCols = new Set<number>();
  physicalLayout.forEach(row => row.forEach(pk => usedCols.add(pk.col)));
  const cols = Array.from(usedCols).sort((a, b) => a - b);
  let maxGap = 0;
  let splitThreshold = 100;
  for (let i = 0; i < cols.length - 1; i++) {
    if (cols[i + 1] - cols[i] > maxGap) {
      maxGap = cols[i + 1] - cols[i];
      splitThreshold = cols[i] + (maxGap / 2);
    }
  }
  const isSplit = maxGap >= 2;

  // 2. Global bounds
  let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
  physicalLayout.forEach(row => {
    row.forEach(pk => {
      if (pk.r && pk.rx !== undefined && pk.ry !== undefined) {
        const corners: [number, number][] = [
          [pk.x, pk.y], [pk.x + (pk.w || 1), pk.y],
          [pk.x, pk.y + (pk.h || 1)], [pk.x + (pk.w || 1), pk.y + (pk.h || 1)]
        ];
        corners.forEach(([cx, cy]) => {
          const [rx, ry] = rotatePoint(cx, cy, pk.rx!, pk.ry!, pk.r!);
          minX = Math.min(minX, rx); maxX = Math.max(maxX, rx);
          minY = Math.min(minY, ry); maxY = Math.max(maxY, ry);
        });
      } else {
        minX = Math.min(minX, pk.x); maxX = Math.max(maxX, pk.x + (pk.w || 1));
        minY = Math.min(minY, pk.y); maxY = Math.max(maxY, pk.y + (pk.h || 1));
      }
    });
  });
  if (!isFinite(minX)) { minX = 0; maxX = 16; minY = 0; maxY = 5; }
  const widthU = maxX - minX, heightU = maxY - minY;
  const startX = -(widthU / 2) * totalUnit, startZ = -(heightU / 2) * totalUnit;

  // 3. Separate clusters
  const clusters = isSplit ? [[], []] : [physicalLayout.flat()];
  if (isSplit) {
    physicalLayout.forEach(row => row.forEach(pk => {
      if (pk.col < splitThreshold) clusters[0].push(pk);
      else clusters[1].push(pk);
    }));
  }

  const getConvexHull = (points: { x: number, z: number }[]) => {
    if (points.length <= 3) return points;
    const sorted = [...points].sort((a, b) => a.x - b.x || a.z - b.z);
    const cross = (o: any, a: any, b: any) => (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x);
    const lower = [];
    for (let p of sorted) {
      while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], p) <= 0) lower.pop();
      lower.push(p);
    }
    const upper = [];
    for (let i = sorted.length - 1; i >= 0; i--) {
      let p = sorted[i];
      while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], p) <= 0) upper.pop();
      upper.push(p);
    }
    upper.pop(); lower.pop();
    return lower.concat(upper);
  };

  return (
    <>
      {clusters.map((cluster, idx) => {
        if (cluster.length === 0) return null;
        let cMinX = Infinity, cMaxX = -Infinity, cMinY = Infinity, cMaxY = -Infinity;
        cluster.forEach((pk: any) => {
          if (pk.r && pk.rx !== undefined && pk.ry !== undefined) {
            const corners: [number, number][] = [
              [pk.x, pk.y], [pk.x + (pk.w || 1), pk.y],
              [pk.x, pk.y + (pk.h || 1)], [pk.x + (pk.w || 1), pk.y + (pk.h || 1)]
            ];
            corners.forEach(([cx, cy]) => {
              const [rx, ry] = rotatePoint(cx, cy, pk.rx!, pk.ry!, pk.r!);
              cMinX = Math.min(cMinX, rx); cMaxX = Math.max(cMaxX, rx);
              cMinY = Math.min(cMinY, ry); cMaxY = Math.max(cMaxY, ry);
            });
          } else {
            cMinX = Math.min(cMinX, pk.x); cMaxX = Math.max(cMaxX, pk.x + (pk.w || 1));
            cMinY = Math.min(cMinY, pk.y); cMaxY = Math.max(cMaxY, pk.y + (pk.h || 1));
          }
        });

        const cCenterX = cMinX + (cMaxX - cMinX) / 2;
        const cCenterY = cMinY + (cMaxY - cMinY) / 2;
        const worldX = startX + (cCenterX - minX) * totalUnit;
        const worldZ = startZ + (cCenterY - minY) * totalUnit;
        const clusterPoints: { x: number, z: number }[] = [];

        const keyElements = cluster.map((pk: any, kIdx: number) => {
          const keyWidth = ((pk.w || 1) * totalUnit) - gap;
          const keyDepth = ((pk.h || 1) * totalUnit) - gap;
          const isAccent = (pk.w && pk.w > 2) || (pk.row === 0 && pk.col === 0);
          const defaultColor = isAccent ? "#2a61a8" : "#111111";

          let rotY = 0, pivotLocalX = 0, pivotLocalZ = 0, relX = 0, relZ = 0;
          if (pk.r) {
            const pivotWorldX = startX + ((pk.rx || 0) - minX) * totalUnit;
            const pivotWorldZ = startZ + ((pk.ry || 0) - minY) * totalUnit;
            const keyCenterWorldX = startX + (pk.x - minX + (pk.w || 1) / 2) * totalUnit;
            const keyCenterWorldZ = startZ + (pk.y - minY + (pk.h || 1) / 2) * totalUnit;
            relX = keyCenterWorldX - pivotWorldX; relZ = keyCenterWorldZ - pivotWorldZ;
            rotY = -pk.r * Math.PI / 180;
            pivotLocalX = pivotWorldX - worldX; pivotLocalZ = pivotWorldZ - worldZ;
          } else {
            const keyCenterWorldX = startX + (pk.x - minX + (pk.w || 1) / 2) * totalUnit;
            const keyCenterWorldZ = startZ + (pk.y - minY + (pk.h || 1) / 2) * totalUnit;
            pivotLocalX = keyCenterWorldX - worldX; pivotLocalZ = keyCenterWorldZ - worldZ;
          }

          [[-keyWidth/2, -keyDepth/2], [keyWidth/2, -keyDepth/2], [-keyWidth/2, keyDepth/2], [keyWidth/2, keyDepth/2]].forEach(([cx, cz]) => {
            const pt = new THREE.Vector3(relX + cx, 0, relZ + cz);
            pt.applyAxisAngle(new THREE.Vector3(0, 1, 0), rotY);
            pt.add(new THREE.Vector3(pivotLocalX, 0, pivotLocalZ));
            clusterPoints.push({ x: pt.x, z: pt.z });
          });

          return (
            <group key={`k-${idx}-${kIdx}`} position={[pivotLocalX, 0.4, pivotLocalZ]} rotation={[0, rotY, 0]}>
              <MOAKeycap width={keyWidth} depth={keyDepth} position={[relX, 0, relZ]} color={defaultColor} userData={{ isKey: true, row: pk.row, col: pk.col, defaultColor }} />
            </group>
          );
        });

        const hull = getConvexHull(clusterPoints);
        const shape = new THREE.Shape();
        if (hull.length > 0) {
          let area = 0;
          for (let i = 0; i < hull.length; i++) {
            const p1 = hull[i], p2 = hull[(i + 1) % hull.length];
            area += p1.x * p2.z - p2.x * p1.z;
          }
          if (area < 0) hull.reverse();
          const R = 0.22, edges: { nx: number, nz: number }[] = [];
          for (let i = 0; i < hull.length; i++) {
            const p1 = hull[i], p2 = hull[(i + 1) % hull.length];
            const dx = p2.x - p1.x, dz = p2.z - p1.z, len = Math.hypot(dx, dz);
            edges.push({ nx: dz / len, nz: -dx / len });
          }
          shape.moveTo(hull[0].x + edges[0].nx * R, hull[0].z + edges[0].nz * R);
          for (let i = 0; i < hull.length; i++) {
            const p2 = hull[(i + 1) % hull.length], e = edges[i], nextE = edges[(i + 1) % hull.length];
            shape.lineTo(p2.x + e.nx * R, p2.z + e.nz * R);
            const startAngle = Math.atan2(e.nz, e.nx);
            let endAngle = Math.atan2(nextE.nz, nextE.nx);
            while (endAngle < startAngle) endAngle += Math.PI * 2;
            if (endAngle - startAngle > 0.001) shape.absarc(p2.x, p2.z, R, startAngle, endAngle, false);
          }
        }

        let tentZ = 0, splitOffsetX = 0;
        if (isSplit) {
          tentZ = idx === 0 ? 0.08 : -0.08;
          splitOffsetX = idx === 0 ? -0.6 : 0.6;
        }

        return (
          <group key={`cluster-${idx}`} position={[worldX + splitOffsetX, 0, worldZ]} rotation={[0, 0, tentZ]}>
            {hull.length > 0 && (
              <mesh rotation={[Math.PI / 2, 0, 0]} position={[0, 0.2, 0]} castShadow receiveShadow>
                <extrudeGeometry args={[shape, { depth: 0.45, bevelEnabled: true, bevelSize: 0.05, bevelThickness: 0.05, bevelSegments: 4 }]} />
                <meshStandardMaterial 
                  color="#121214" roughness={0.4} metalness={0.85} 
                  normalMap={aluNormalMap || undefined} normalScale={new THREE.Vector2(0.5, 0.5)}
                  transparent
                />
              </mesh>
            )}
            {keyElements}
          </group>
        );
      })}
    </>
  );
  }, [physicalLayout]);

  return <>{renderedElements}</>;
}

function LayoutBuffer({ physicalLayout, layers, activeLayer, opacityRef, targetFadeRef }: { physicalLayout: any[] | null, layers: any, activeLayer: number, opacityRef: React.MutableRefObject<number>, targetFadeRef: React.MutableRefObject<number> }) {
  const groupRef = useRef<THREE.Group>(null);
  const materialsRef = useRef<THREE.MeshStandardMaterial[]>([]);
  const targetColorsRef = useRef<Map<THREE.Material, THREE.Color>>(new Map());
  const keyInfoRef = useRef<{mat: THREE.Material, row: number, col: number, defaultColor: string}[]>([]);

  useEffect(() => {
    if (groupRef.current) {
      const mats: THREE.MeshStandardMaterial[] = [];
      const keys: typeof keyInfoRef.current = [];
      
      groupRef.current.traverse((child) => {
        if ((child as THREE.Mesh).isMesh) {
          const mesh = child as THREE.Mesh;
          const mat = mesh.material as THREE.MeshStandardMaterial;
          if (mat) {
            mat.transparent = true;
            mats.push(mat);
            
            if (mesh.userData?.isKey) {
               keys.push({ mat, row: mesh.userData.row, col: mesh.userData.col, defaultColor: mesh.userData.defaultColor });
            }
          }
        }
      });
      materialsRef.current = mats;
      keyInfoRef.current = keys;
    }
  }, [physicalLayout]);

  useEffect(() => {
    keyInfoRef.current.forEach(({mat, row, col, defaultColor}) => {
       let hex = defaultColor;
       if (physicalLayout && physicalLayout.length > 0 && layers && layers[activeLayer]) {
         const rowData = layers[activeLayer][row];
         const code = rowData ? rowData[col] || 0 : 0;
         // Use the unified color scheme but slightly darker for 3D model
         hex = getKeyColor(code, 0.5, 0.8);
       }
       
       let target = targetColorsRef.current.get(mat);
       if (!target) {
          target = new THREE.Color(hex);
          targetColorsRef.current.set(mat, target);
       } else {
          target.set(hex);
       }
    });
  }, [physicalLayout, layers, activeLayer]);

  useFrame((state, delta) => {
    if (groupRef.current) {
      const opacity = opacityRef.current * targetFadeRef.current;
      groupRef.current.visible = opacity > 0.001;
      if (groupRef.current.visible) {
        for (let i = 0; i < materialsRef.current.length; i++) {
          materialsRef.current[i].opacity = opacity;
        }
        targetColorsRef.current.forEach((targetColor, mat) => {
           (mat as THREE.MeshStandardMaterial).color.lerp(targetColor, delta * 12);
        });
      }
    }
  });

  return (
    <group ref={groupRef}>
      <LayoutElements physicalLayout={physicalLayout} />
    </group>
  );
}

function KeyboardModel({ isAutoRotating, setIsAutoRotating }: { isAutoRotating: boolean, setIsAutoRotating: (v: boolean) => void }) {
  const group = useRef<THREE.Group>(null)
  const physicalLayout = useLayoutStore(state => state.physicalLayout)
  const layers = useLayoutStore(state => state.layers)
  const activeLayer = useLayoutStore(state => state.activeLayer)
  const isConnected = useLayoutStore(state => state.isConnected)

  const [bufferA, setBufferA] = useState<{ pl: any[] | null } | null>(null);
  const [bufferB, setBufferB] = useState<{ pl: any[] | null } | null>(null);
  const [activeBuffer, setActiveBuffer] = useState<'A' | 'B'>('A');
  const opacityA = useRef(0);
  const opacityB = useRef(0);
  const targetFade = useRef(0);
  
  const targetRotation = useRef({ x: 0.2, y: 0 })
  const isDragging = useRef(false)
  const pointerDownPos = useRef({ x: 0, y: 0 })
  const hasMoved = useRef(false)
  const autoRotateStrength = useRef(0)

  useEffect(() => {
    const onPointerDown = (e: PointerEvent) => {
      if (!isConnected || e.button !== 0) return
      const target = e.target as HTMLElement
      if (target.closest('button, input, select, textarea, a, .key, .editor-ui')) return
      isDragging.current = true
      pointerDownPos.current = { x: e.clientX, y: e.clientY }
      hasMoved.current = false
    }

    const onPointerMove = (e: PointerEvent) => {
      if (!isDragging.current) return
      const dx = e.clientX - pointerDownPos.current.x
      const dy = e.clientY - pointerDownPos.current.y
      if (!hasMoved.current && (Math.abs(dx) > 3 || Math.abs(dy) > 3)) {
        hasMoved.current = true
        setIsAutoRotating(false)
      }
      if (hasMoved.current) {
        const sensitivity = 0.006
        targetRotation.current.y += dx * sensitivity
        targetRotation.current.x += dy * sensitivity
        targetRotation.current.x = Math.max(-0.4, Math.min(1.4, targetRotation.current.x))
        pointerDownPos.current = { x: e.clientX, y: e.clientY }
      }
    }

    const onPointerUp = () => {
      isDragging.current = false
      if (!hasMoved.current) setIsAutoRotating(true)
    }

    window.addEventListener('pointerdown', onPointerDown)
    window.addEventListener('pointermove', onPointerMove)
    window.addEventListener('pointerup', onPointerUp)
    return () => {
      window.removeEventListener('pointerdown', onPointerDown)
      window.removeEventListener('pointermove', onPointerMove)
      window.removeEventListener('pointerup', onPointerUp)
    }
  }, [isConnected, setIsAutoRotating])

  useEffect(() => {
    const timer = setTimeout(() => {
      const layoutData = { pl: physicalLayout };
      if (activeBuffer === 'A') {
        setBufferB(layoutData);
        setActiveBuffer('B');
      } else {
        setBufferA(layoutData);
        setActiveBuffer('A');
      }
    }, 20);
    return () => clearTimeout(timer);
  }, [physicalLayout]);

  useEffect(() => {
    if (!bufferA && !bufferB) {
      setBufferA({ pl: physicalLayout });
    }
  }, []);

  useFrame((state, delta) => {
    if (group.current) {
      // Transition strength for auto-rotation (extremely slow ease-in, fast stop)
      const targetStrength = isAutoRotating ? 1 : 0;
      const lerpSpeed = targetStrength > autoRotateStrength.current ? 0.1 : 8.0;
      autoRotateStrength.current = THREE.MathUtils.lerp(autoRotateStrength.current, targetStrength, delta * lerpSpeed);

      if (autoRotateStrength.current > 0.001) {
        // Y Axis: Purely additive rotation. No snaps possible.
        // Rotation speed eases in slowly.
        targetRotation.current.y += delta * 0.1 * autoRotateStrength.current;
        
        // X Axis: Smoothly transition from manual position to idle oscillation
        const idleX = Math.sin(state.clock.elapsedTime * 0.2) * 0.1 + 0.2;
        // The lerp speed for X also eases in with the strength
        targetRotation.current.x = THREE.MathUtils.lerp(targetRotation.current.x, idleX, delta * 0.2 * autoRotateStrength.current);
      }
      
      // Standard smoothing for the group following the target
      const lerpFactor = hasMoved.current && isDragging.current ? 0.25 : 0.08
      group.current.rotation.x = THREE.MathUtils.lerp(group.current.rotation.x, targetRotation.current.x, lerpFactor)
      group.current.rotation.y = THREE.MathUtils.lerp(group.current.rotation.y, targetRotation.current.y, lerpFactor)

      // Manual smoothed "Float" effect to replace the snappy component
      const t = state.clock.elapsedTime;
      const fStrength = autoRotateStrength.current;
      group.current.position.y = -3.5 + Math.sin(t * 0.5) * 0.3 * fStrength;
      group.current.rotation.z = Math.sin(t * 0.4) * 0.02 * fStrength;
      // Add subtle extra tilting
      group.current.rotation.x += Math.cos(t * 0.3) * 0.01 * fStrength;
    }

    const hasLayoutData = physicalLayout && physicalLayout.length > 0;
    const hasLayerData = layers && Object.keys(layers).length > 0;

    if (isConnected && hasLayoutData && hasLayerData) {
      targetFade.current = Math.min(1, targetFade.current + delta * 0.4);
    } else {
      targetFade.current = 0;
    }

    const crossFadeSpeed = 3.0;
    if (activeBuffer === 'A') {
      opacityA.current = THREE.MathUtils.lerp(opacityA.current, 1, delta * crossFadeSpeed);
      opacityB.current = THREE.MathUtils.lerp(opacityB.current, 0, delta * crossFadeSpeed);
    } else {
      opacityA.current = THREE.MathUtils.lerp(opacityA.current, 0, delta * crossFadeSpeed);
      opacityB.current = THREE.MathUtils.lerp(opacityB.current, 1, delta * crossFadeSpeed);
    }
  })

  return (
    <group ref={group}>
      {bufferA && (
        <LayoutBuffer 
          physicalLayout={bufferA.pl} 
          layers={layers} 
          activeLayer={activeLayer} 
          opacityRef={opacityA}
          targetFadeRef={targetFade}
        />
      )}
      {bufferB && (
        <LayoutBuffer 
          physicalLayout={bufferB.pl} 
          layers={layers} 
          activeLayer={activeLayer} 
          opacityRef={opacityB}
          targetFadeRef={targetFade}
        />
      )}
    </group>
  )
}

export default function Background3D() {
  const isConnected = useLayoutStore(state => state.isConnected);
  const [isAutoRotating, setIsAutoRotating] = useState(true);

  return (
    <div style={{
      position: 'fixed',
      top: 0, left: 0, width: '100vw', height: '100vh',
      zIndex: -10,
      background: '#0d1117',
      pointerEvents: 'auto',
      userSelect: 'none'
    }}>
      <div style={{
        width: '100%', height: '100%',
        opacity: isConnected ? 1 : 0,
        transform: isConnected ? 'translateY(0)' : 'translateY(40px)',
        transition: 'opacity 2.5s cubic-bezier(0.16, 1, 0.3, 1), transform 2.5s cubic-bezier(0.16, 1, 0.3, 1)',
        pointerEvents: isConnected ? 'auto' : 'none'
      }}>
        <Canvas shadows camera={{ position: [0, 12, 22], fov: 45 }}>
          <color attach="background" args={['#0d1117']} />
          <fog attach="fog" args={['#0d1117', 15, 40]} />

          <ambientLight intensity={0.4} />
          <spotLight position={[-15, 15, 15]} angle={0.25} penumbra={1} intensity={1.8} castShadow />
          <spotLight position={[15, 8, -15]} angle={0.3} penumbra={1} intensity={3.5} color="#e0eaff" />
          <pointLight position={[-10, -10, -10]} intensity={0.8} color="#2a61a8" />
          <pointLight position={[10, -10, 10]} intensity={0.8} color="#6436b5" />

          <group position={[0, -3.5, 0]}>
            <KeyboardModel isAutoRotating={isAutoRotating} setIsAutoRotating={setIsAutoRotating} />
          </group>

          <ContactShadows position={[0, -8, 0]} opacity={0.6} scale={20} blur={2.5} far={4} color="#000000" />
          <Environment preset="city" />
        </Canvas>
      </div>
    </div>
  )
}
