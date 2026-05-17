import { Canvas, useFrame, useThree } from '@react-three/fiber'
import { useRef, useMemo, useState, useEffect } from 'react'
import * as THREE from 'three'
import { Environment, Float, ContactShadows } from '@react-three/drei'
import { useLayoutStore } from '../stores/layoutStore'
import { getKeyColor, getCategoryFromCode, KEY_BASE_COLORS } from '../utils/keyColors'
import { getKeycapGeometry } from '../utils/keycapGeometry'

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

const KEY_MATERIAL = new THREE.MeshStandardMaterial({
  roughness: 0.75,
  metalness: 0.05,
  normalMap: pbtNormalMap || undefined,
  normalScale: new THREE.Vector2(0.6, 0.6),
  transparent: true,
});

const CASE_MATERIAL = new THREE.MeshStandardMaterial({
  color: "#121214",
  roughness: 0.35,
  metalness: 0.85,
  normalMap: aluNormalMap || undefined,
  normalScale: new THREE.Vector2(0.5, 0.5),
  transparent: true,
});

function InstancedKeyboard({ physicalLayout, opacity }: { physicalLayout: any[] | null, opacity: number }) {
  const instancedMeshesRef = useRef<Map<string, THREE.InstancedMesh>>(new Map());
  const keyInstancesRef = useRef<{
    size: string,
    idx: number,
    row: number,
    col: number,
    pivot: THREE.Vector3,
    rotY: number,
    relX: number,
    relZ: number,
    currentY: number,
    currentColor: THREE.Color,
    targetColor: THREE.Color
  }[]>([]);

  const unitSize = 0.92;
  const gap = 0.08;
  const totalUnit = unitSize + gap;

  const { instancedData, clusterData } = useMemo(() => {
    if (!physicalLayout || physicalLayout.length === 0) return { instancedData: new Map(), clusterData: [] };

    // 1. Detect split and bounds
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

    const rotatePoint = (x: number, y: number, cx: number, cy: number, deg: number): [number, number] => {
      const rad = deg * Math.PI / 180;
      const cos = Math.cos(rad), sin = Math.sin(rad);
      const dx = x - cx, dy = y - cy;
      return [cx + dx * cos - dy * sin, cy + dx * sin + dy * cos];
    };

    let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
    physicalLayout.forEach(row => {
      row.forEach(pk => {
        if (pk.r && pk.rx !== undefined && pk.ry !== undefined) {
          [ [pk.x, pk.y], [pk.x + (pk.w || 1), pk.y], [pk.x, pk.y + (pk.h || 1)], [pk.x + (pk.w || 1), pk.y + (pk.h || 1)] ].forEach(([cx, cy]) => {
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
    
    const widthU = maxX - minX, heightU = maxY - minY;
    const startX = -(widthU / 2) * totalUnit, startZ = -(heightU / 2) * totalUnit;

    const clusters = isSplit ? [[], []] : [physicalLayout.flat()];
    if (isSplit) {
      physicalLayout.forEach(row => row.forEach(pk => {
        if (pk.col < splitThreshold) (clusters[0] as any[]).push(pk);
        else (clusters[1] as any[]).push(pk);
      }));
    }

    const sizesMaps: Map<string, any[]>[] = [];
    const clusterGeos: any[] = [];

    clusters.forEach((cluster, idx) => {
      if (cluster.length === 0) return;
      let cMinX = Infinity, cMaxX = -Infinity, cMinY = Infinity, cMaxY = -Infinity;
      cluster.forEach((pk: any) => {
        if (pk.r && pk.rx !== undefined && pk.ry !== undefined) {
          [ [pk.x, pk.y], [pk.x + (pk.w || 1), pk.y], [pk.x, pk.y + (pk.h || 1)], [pk.x + (pk.w || 1), pk.y + (pk.h || 1)] ].forEach(([cx, cy]) => {
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
      const currentSizeMap = new Map<string, any[]>();

      cluster.forEach((pk: any) => {
        const w = (pk.w || 1), h = (pk.h || 1);
        const keyWidth = (w * totalUnit) - gap;
        const keyDepth = (h * totalUnit) - gap;
        const sizeKey = `${keyWidth.toFixed(3)}x${keyDepth.toFixed(3)}`;

        let rotY = 0, relX = 0, relZ = 0, pivotWorldX = 0, pivotWorldZ = 0;
        if (pk.r) {
          pivotWorldX = startX + ((pk.rx || 0) - minX) * totalUnit;
          pivotWorldZ = startZ + ((pk.ry || 0) - minY) * totalUnit;
          const keyCenterWorldX = startX + (pk.x - minX + w / 2) * totalUnit;
          const keyCenterWorldZ = startZ + (pk.y - minY + h / 2) * totalUnit;
          relX = keyCenterWorldX - pivotWorldX; relZ = keyCenterWorldZ - pivotWorldZ;
          rotY = -pk.r * Math.PI / 180;
        } else {
          pivotWorldX = startX + (pk.x - minX + w / 2) * totalUnit;
          pivotWorldZ = startZ + (pk.y - minY + h / 2) * totalUnit;
        }

        const pivotLocalX = pivotWorldX - worldX;
        const pivotLocalZ = pivotWorldZ - worldZ;

        [[-keyWidth/2, -keyDepth/2], [keyWidth/2, -keyDepth/2], [-keyWidth/2, keyDepth/2], [keyWidth/2, keyDepth/2]].forEach(([cx, cz]) => {
          const pt = new THREE.Vector3(relX + cx, 0, relZ + cz);
          pt.applyAxisAngle(new THREE.Vector3(0, 1, 0), rotY);
          pt.add(new THREE.Vector3(pivotLocalX, 0, pivotLocalZ));
          clusterPoints.push({ x: pt.x, z: pt.z });
        });

        if (!currentSizeMap.has(sizeKey)) currentSizeMap.set(sizeKey, []);
        currentSizeMap.get(sizeKey)!.push({
          row: pk.row, col: pk.col,
          pivot: new THREE.Vector3(pivotLocalX, 0.4, pivotLocalZ),
          rotY, relX, relZ, width: keyWidth, depth: keyDepth
        });
      });

      const hull = ((points: any[]) => {
        if (points.length <= 3) return points;
        const sorted = [...points].sort((a, b) => a.x - b.x || a.z - b.z);
        const cross = (o: any, a: any, b: any) => (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x);
        const lower = []; for (let p of sorted) { while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], p) <= 0) lower.pop(); lower.push(p); }
        const upper = []; for (let i = sorted.length - 1; i >= 0; i--) { let p = sorted[i]; while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], p) <= 0) upper.pop(); upper.push(p); }
        upper.pop(); lower.pop(); return lower.concat(upper);
      })(clusterPoints);

      const shape = new THREE.Shape();
      if (hull.length > 0) {
        let area = 0; for (let i = 0; i < hull.length; i++) { const p1 = hull[i], p2 = hull[(i + 1) % hull.length]; area += p1.x * p2.z - p2.x * p1.z; }
        if (area < 0) hull.reverse();
        const R = 0.22, edges: any[] = [];
        for (let i = 0; i < hull.length; i++) { const p1 = hull[i], p2 = hull[(i + 1) % hull.length]; const dx = p2.x - p1.x, dz = p2.z - p1.z, len = Math.hypot(dx, dz); edges.push({ nx: dz / len, nz: -dx / len }); }
        shape.moveTo(hull[0].x + edges[0].nx * R, hull[0].z + edges[0].nz * R);
        for (let i = 0; i < hull.length; i++) {
          const p2 = hull[(i + 1) % hull.length], e = edges[i], nextE = edges[(i + 1) % hull.length];
          shape.lineTo(p2.x + e.nx * R, p2.z + e.nz * R);
          const startAngle = Math.atan2(e.nz, e.nx); let endAngle = Math.atan2(nextE.nz, nextE.nx);
          while (endAngle < startAngle) endAngle += Math.PI * 2;
          if (endAngle - startAngle > 0.001) shape.absarc(p2.x, p2.z, R, startAngle, endAngle, false);
        }
      }

      clusterGeos.push({
        id: `cluster-${idx}`,
        idx,
        position: [worldX + (isSplit ? (idx === 0 ? -0.6 : 0.6) : 0), 0, worldZ],
        rotation: [0, 0, isSplit ? (idx === 0 ? 0.08 : -0.08) : 0],
        shape,
        sizesMap: currentSizeMap
      });
      sizesMaps.push(currentSizeMap);
    });

    return { instancedData: sizesMaps, clusterData: clusterGeos };
  }, [physicalLayout]);

  useEffect(() => {
    const instances: any[] = [];
    clusterData.forEach((cluster) => {
      cluster.sizesMap.forEach((keys: any[], sizeKey: string) => {
        keys.forEach((k, i) => {
          instances.push({
            clusterIdx: cluster.idx,
            size: sizeKey, idx: i, row: k.row, col: k.col,
            pivot: k.pivot, rotY: k.rotY, relX: k.relX, relZ: k.relZ,
            currentY: 0, currentColor: new THREE.Color(), targetColor: new THREE.Color()
          });
        });
      });
    });
    keyInstancesRef.current = instances;
  }, [clusterData]);

  useFrame((_, delta) => {
    const store = useLayoutStore.getState();
    const { pressedCodes, heldTestKeys, layers, activeLayer } = store;
    const tempMatrix = new THREE.Matrix4();
    const tempPos = new THREE.Vector3();
    const tempQuat = new THREE.Quaternion();
    const tempScale = new THREE.Vector3(1, 1, 1);

    const meshUpdates = new Set<string>();

    keyInstancesRef.current.forEach(info => {
      let isPressed = false;
      let code = 0;
      if (physicalLayout && layers && layers[activeLayer]) {
        const rowData = layers[activeLayer][info.row];
        code = rowData ? rowData[info.col] || 0 : 0;
        isPressed = pressedCodes.has(code) || heldTestKeys.has(`${info.row}-${info.col}`);
      }

      const category = getCategoryFromCode(code);
      const baseL = KEY_BASE_COLORS[category]?.l || 20;
      const lBoost = baseL > 50 ? 12 : 20;
      const finalLMult = isPressed ? Math.min(100, (baseL * 0.5) + lBoost) / baseL : 0.5;
      const hex = getKeyColor(code, finalLMult, isPressed ? 1.0 : 0.8);
      
      info.targetColor.set(hex);
      info.currentColor.lerp(info.targetColor, delta * 12);
      
      const targetY = isPressed ? -0.15 : 0;
      info.currentY = THREE.MathUtils.lerp(info.currentY, targetY, delta * 20);

      const meshKey = `${info.clusterIdx}-${info.size}`;
      const mesh = instancedMeshesRef.current.get(meshKey);
      if (mesh) {
        tempPos.copy(info.pivot);
        tempPos.y += info.currentY;
        tempQuat.setFromEuler(new THREE.Euler(0, info.rotY, 0));
        
        const rel = new THREE.Vector3(info.relX, 0, info.relZ);
        rel.applyQuaternion(tempQuat);
        tempPos.add(rel);

        tempMatrix.compose(tempPos, tempQuat, tempScale);
        mesh.setMatrixAt(info.idx, tempMatrix);
        mesh.setColorAt(info.idx, info.currentColor);
        meshUpdates.add(meshKey);
      }
    });

    meshUpdates.forEach(mKey => {
      const mesh = instancedMeshesRef.current.get(mKey);
      if (mesh) {
        mesh.instanceMatrix.needsUpdate = true;
        if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
      }
    });

    CASE_MATERIAL.opacity = opacity;
    KEY_MATERIAL.opacity = opacity;
  });

  return (
    <>
      {clusterData.map(c => (
        <group key={c.id} position={c.position} rotation={c.rotation}>
          <mesh rotation={[Math.PI / 2, 0, 0]} position={[0, 0.2, 0]} castShadow receiveShadow material={CASE_MATERIAL}>
            <extrudeGeometry args={[c.shape, { depth: 0.45, bevelEnabled: true, bevelSize: 0.05, bevelThickness: 0.05, bevelSegments: 4 }]} />
          </mesh>
          {Array.from(c.sizesMap.entries()).map(([sizeKey, keys]) => (
            <instancedMesh
              key={`${c.id}-${sizeKey}`}
              ref={el => { if (el) instancedMeshesRef.current.set(`${c.idx}-${sizeKey}`, el); }}
              args={[getKeycapGeometry((keys[0] as any).width, (keys[0] as any).depth), KEY_MATERIAL, keys.length]}
              castShadow receiveShadow
            />
          ))}
        </group>
      ))}
    </>
  );
}

function KeyboardModel({ isAutoRotating, setIsAutoRotating }: { isAutoRotating: boolean, setIsAutoRotating: (v: boolean) => void }) {
  const group = useRef<THREE.Group>(null)
  const physicalLayout = useLayoutStore(state => state.physicalLayout)
  const layers = useLayoutStore(state => state.layers)
  const isConnected = useLayoutStore(state => state.isConnected)

  const [opacity, setOpacity] = useState(0);
  const targetFade = useRef(0)
  const targetRotation = useRef({ x: 0.2, y: 0 })
  const isDragging = useRef(false)
  const pointerDownPos = useRef({ x: 0, y: 0 })
  const hasMoved = useRef(false)
  const autoRotateStrength = useRef(0)

  useEffect(() => {
    // Mouse/Pointer events for rotation
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

    window.addEventListener('pointermove', onPointerMove)
    window.addEventListener('pointerup', onPointerUp)
    return () => {
      window.removeEventListener('pointermove', onPointerMove)
      window.removeEventListener('pointerup', onPointerUp)
    }
  }, [isConnected, setIsAutoRotating])

  useFrame((state, delta) => {
    if (group.current) {
      const targetStrength = isAutoRotating ? 1 : 0;
      const lerpSpeed = targetStrength > autoRotateStrength.current ? 0.1 : 8.0;
      autoRotateStrength.current = THREE.MathUtils.lerp(autoRotateStrength.current, targetStrength, delta * lerpSpeed);

      if (autoRotateStrength.current > 0.001) {
        targetRotation.current.y += delta * 0.1 * autoRotateStrength.current;
        const idleX = Math.sin(state.clock.elapsedTime * 0.2) * 0.1 + 0.2;
        targetRotation.current.x = THREE.MathUtils.lerp(targetRotation.current.x, idleX, delta * 0.2 * autoRotateStrength.current);
      }
      
      const lerpFactor = hasMoved.current && isDragging.current ? 0.25 : 0.08
      group.current.rotation.x = THREE.MathUtils.lerp(group.current.rotation.x, targetRotation.current.x, lerpFactor)
      group.current.rotation.y = THREE.MathUtils.lerp(group.current.rotation.y, targetRotation.current.y, lerpFactor)

      const t = state.clock.elapsedTime;
      const fStrength = autoRotateStrength.current;
      group.current.position.y = -3.5 + Math.sin(t * 0.5) * 0.3 * fStrength;
      group.current.rotation.z = Math.sin(t * 0.4) * 0.02 * fStrength;
      group.current.rotation.x += Math.cos(t * 0.3) * 0.01 * fStrength;
    }

    const hasData = physicalLayout?.length && layers && Object.keys(layers).length > 0;
    targetFade.current = (isConnected && hasData) ? Math.min(1, targetFade.current + delta * 0.4) : 0;
    setOpacity(THREE.MathUtils.lerp(opacity, targetFade.current, delta * 3));
  })

  return (
    <group 
      ref={group}
      onPointerDown={(e) => {
        if (!isConnected || e.button !== 0) return;
        // Check if we hit the keyboard model
        e.stopPropagation();
        isDragging.current = true;
        pointerDownPos.current = { x: e.clientX, y: e.clientY };
        hasMoved.current = false;
      }}
    >
      <InstancedKeyboard physicalLayout={physicalLayout} opacity={opacity} />
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
