import { Canvas, useFrame } from '@react-three/fiber'
import { useRef, useMemo } from 'react'
import * as THREE from 'three'
import { Environment, Float, ContactShadows, RoundedBox } from '@react-three/drei'
import { useLayoutStore } from '../stores/layoutStore'
import { getKeyClass } from '../KeyDefinitions'

function KeyboardModel() {
  const group = useRef<THREE.Group>(null)
  const physicalLayout = useLayoutStore(state => state.physicalLayout)
  const layers = useLayoutStore(state => state.layers)
  const activeLayer = useLayoutStore(state => state.activeLayer)
  const isConnected = useLayoutStore(state => state.isConnected)
  const fadeProgress = useRef(0)
  const prevConnected = useRef(false)

  useFrame((state, delta) => {
    if (group.current) {
      group.current.rotation.y = state.clock.elapsedTime * 0.1
      group.current.rotation.x = Math.sin(state.clock.elapsedTime * 0.2) * 0.1 + 0.2
    }
    // Animate key color fade-in when layout just loaded
    if (isConnected && !prevConnected.current) {
      fadeProgress.current = 0;
    }
    prevConnected.current = isConnected;
    if (isConnected && fadeProgress.current < 1) {
      fadeProgress.current = Math.min(1, fadeProgress.current + delta * 0.4); // ~2.5s fade
      if (group.current) {
        group.current.traverse((obj) => {
          if ((obj as THREE.Mesh).isMesh) {
            const mat = (obj as THREE.Mesh).material as THREE.MeshStandardMaterial;
            if (mat && mat.color) {
              mat.opacity = fadeProgress.current;
              mat.transparent = fadeProgress.current < 1;
            }
          }
        });
      }
    }
  })

  const { elements } = useMemo(() => {
    const unitSize = 0.92;
    const gap = 0.08;
    const totalUnit = unitSize + gap;

    if (!physicalLayout || physicalLayout.length === 0) {
      const layout = [
        [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1],
        [1.5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.5, 1],
        [1.75, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2.25, 1],
        [2.25, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.75, 1, 1],
        [1.25, 1.25, 1.25, 6.25, 1, 1, 1, 1, 1, 1]
      ];

      const elements = [];
      const startZ = -2 * totalUnit;

      layout.forEach((row, rowIndex) => {
        let currentX = -8 * totalUnit;

        row.forEach((width, colIndex) => {
          const keyWidth = (width * totalUnit) - gap;
          const keyDepth = unitSize;
          const xPos = currentX + (keyWidth / 2);
          const zPos = startZ + (rowIndex * totalUnit);

          let color = "#111111";

          const isBlue = (width > 1 && width < 6 && !(rowIndex === 1 && colIndex === 13)) ||
            (rowIndex === 0 && colIndex === 0) || // Esc
            (rowIndex === 3 && colIndex === 12) || // Up
            (rowIndex === 4 && (colIndex === 4 || colIndex === 7 || colIndex === 8 || colIndex === 9)) || // RAlt, Left, Down, Right
            (colIndex === row.length - 1 && rowIndex !== 4); // Rightmost nav column

          const isPurple = (rowIndex === 4 && ((colIndex === 5) || (colIndex === 6))); // FN

          if (isBlue) color = "#1f4576ff";
          else if (isPurple) color = "#6436b5";

          elements.push(
            <RoundedBox
              key={`key-${rowIndex}-${colIndex}`}
              args={[keyWidth, 0.4, keyDepth]}
              position={[xPos, 0.4, zPos]}
              radius={0.05}
              smoothness={4}
            >
              <meshStandardMaterial color={color} roughness={0.4} metalness={0.3} />
            </RoundedBox>
          );
          currentX += width * totalUnit;
        });
      });
      return {
        elements: [
          <group key="default-cluster" position={[0, 0, 0]}>
            <RoundedBox args={[16.4, 0.45, 5.4]} position={[0, 0, 0]} radius={0.15} smoothness={4}>
              <meshStandardMaterial color="#0a0a0a" roughness={0.6} metalness={0.5} />
            </RoundedBox>
            {elements}
          </group>
        ]
      };
    } else {
      const elements: JSX.Element[] = [];

      // 1. Detect if it's a split keyboard by checking for unused columns in the middle
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

      // Helper to compute rotated bounding boxes
      const rotatePoint = (x: number, y: number, cx: number, cy: number, deg: number): [number, number] => {
        const rad = deg * Math.PI / 180;
        const cos = Math.cos(rad), sin = Math.sin(rad);
        const dx = x - cx, dy = y - cy;
        return [cx + dx * cos - dy * sin, cy + dx * sin + dy * cos];
      };

      // 2. Global bounds
      let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
      physicalLayout.forEach(row => {
        row.forEach(pk => {
          if (pk.r && pk.rx !== undefined && pk.ry !== undefined) {
            const corners: [number, number][] = [
              [pk.x, pk.y],
              [pk.x + (pk.w || 1), pk.y],
              [pk.x, pk.y + (pk.h || 1)],
              [pk.x + (pk.w || 1), pk.y + (pk.h || 1)]
            ];
            corners.forEach(([cx, cy]) => {
              const [rx, ry] = rotatePoint(cx, cy, pk.rx!, pk.ry!, pk.r!);
              minX = Math.min(minX, rx);
              maxX = Math.max(maxX, rx);
              minY = Math.min(minY, ry);
              maxY = Math.max(maxY, ry);
            });
          } else {
            minX = Math.min(minX, pk.x);
            maxX = Math.max(maxX, pk.x + (pk.w || 1));
            minY = Math.min(minY, pk.y);
            maxY = Math.max(maxY, pk.y + (pk.h || 1));
          }
        });
      });
      if (!isFinite(minX)) { minX = 0; maxX = 16; minY = 0; maxY = 5; }

      const widthU = maxX - minX;
      const heightU = maxY - minY;
      const startX = -(widthU / 2) * totalUnit;
      const startZ = -(heightU / 2) * totalUnit;

      // Helper to compute convex hull for a custom base plate shape
      const getConvexHull = (points: { x: number, z: number }[]) => {
        if (points.length <= 3) return points;
        const sorted = [...points].sort((a, b) => a.x - b.x || a.z - b.z);
        const cross = (o: any, a: any, b: any) => (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x);

        const lower = [];
        for (let p of sorted) {
          while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], p) <= 0) {
            lower.pop();
          }
          lower.push(p);
        }

        const upper = [];
        for (let i = sorted.length - 1; i >= 0; i--) {
          let p = sorted[i];
          while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], p) <= 0) {
            upper.pop();
          }
          upper.push(p);
        }

        upper.pop();
        lower.pop();
        return lower.concat(upper);
      };

      // 3. Separate keys into clusters
      const clusters = isSplit ? [[], []] : [physicalLayout.flat()];
      if (isSplit) {
        physicalLayout.forEach(row => row.forEach(pk => {
          if (pk.col < splitThreshold) clusters[0].push(pk);
          else clusters[1].push(pk);
        }));
      }

      // 4. Generate each cluster
      clusters.forEach((cluster, idx) => {
        if (cluster.length === 0) return;

        let cMinX = Infinity, cMaxX = -Infinity, cMinY = Infinity, cMaxY = -Infinity;
        cluster.forEach((pk: any) => {
          if (pk.r && pk.rx !== undefined && pk.ry !== undefined) {
            const corners: [number, number][] = [
              [pk.x, pk.y],
              [pk.x + (pk.w || 1), pk.y],
              [pk.x, pk.y + (pk.h || 1)],
              [pk.x + (pk.w || 1), pk.y + (pk.h || 1)]
            ];
            corners.forEach(([cx, cy]) => {
              const [rx, ry] = rotatePoint(cx, cy, pk.rx!, pk.ry!, pk.r!);
              cMinX = Math.min(cMinX, rx);
              cMaxX = Math.max(cMaxX, rx);
              cMinY = Math.min(cMinY, ry);
              cMaxY = Math.max(cMaxY, ry);
            });
          } else {
            cMinX = Math.min(cMinX, pk.x);
            cMaxX = Math.max(cMaxX, pk.x + (pk.w || 1));
            cMinY = Math.min(cMinY, pk.y);
            cMaxY = Math.max(cMaxY, pk.y + (pk.h || 1));
          }
        });

        const cWidthU = cMaxX - cMinX;
        const cHeightU = cMaxY - cMinY;
        const cCenterX = cMinX + cWidthU / 2;
        const cCenterY = cMinY + cHeightU / 2;

        const worldX = startX + (cCenterX - minX) * totalUnit;
        const worldZ = startZ + (cCenterY - minY) * totalUnit;

        const clusterPoints: { x: number, z: number }[] = [];
        const margin = 0.0; // We apply padding via the Minkowski sum radius later

        const keyElements = cluster.map((pk: any, kIdx) => {
          const keyWidth = ((pk.w || 1) * totalUnit) - gap;
          const keyDepth = ((pk.h || 1) * totalUnit) - gap;

          let color = "#111111";
          if (layers[activeLayer]) {
            const rowData = layers[activeLayer]![pk.row];
            const code = rowData ? rowData[pk.col] || 0 : 0;
            const keyClass = getKeyClass(code);

            if (code >= 0x3A && code <= 0x45) color = "#2a7a3b"; // Distinct green for F1-F12
            else if (keyClass === 'key-modifier') color = "#2a61a8"; // Deeper blue for modifiers/action
            else if (keyClass === 'key-system' || keyClass === 'key-macro' || keyClass === 'key-ckey') color = "#6436b5"; // Deeper purple for system
            else if (keyClass === 'key-none' || keyClass === 'key-transparent') color = "#080b0f"; // Very dark for unassigned
            else color = "#111111"; // Standard keys
          } else {
            const isAccent = (pk.w && pk.w > 2) || (pk.row === 0 && pk.col === 0);
            color = isAccent ? "#2a61a8" : (Math.random() > 0.98 ? "#6436b5" : "#111111");
          }

          let rotY = 0;
          let pivotLocalX = 0, pivotLocalZ = 0;
          let relX = 0, relZ = 0;

          if (pk.r) {
            const pivotWorldX = startX + ((pk.rx || 0) - minX) * totalUnit;
            const pivotWorldZ = startZ + ((pk.ry || 0) - minY) * totalUnit;

            const keyCenterWorldX = startX + (pk.x - minX + (pk.w || 1) / 2) * totalUnit;
            const keyCenterWorldZ = startZ + (pk.y - minY + (pk.h || 1) / 2) * totalUnit;

            relX = keyCenterWorldX - pivotWorldX;
            relZ = keyCenterWorldZ - pivotWorldZ;

            rotY = -pk.r * Math.PI / 180;

            pivotLocalX = pivotWorldX - worldX;
            pivotLocalZ = pivotWorldZ - worldZ;
          } else {
            const keyCenterWorldX = startX + (pk.x - minX + (pk.w || 1) / 2) * totalUnit;
            const keyCenterWorldZ = startZ + (pk.y - minY + (pk.h || 1) / 2) * totalUnit;

            pivotLocalX = keyCenterWorldX - worldX;
            pivotLocalZ = keyCenterWorldZ - worldZ;
          }

          // Calculate expanded corners for the custom baseplate
          const corners = [
            [-keyWidth / 2 - margin, -keyDepth / 2 - margin],
            [keyWidth / 2 + margin, -keyDepth / 2 - margin],
            [-keyWidth / 2 - margin, keyDepth / 2 + margin],
            [keyWidth / 2 + margin, keyDepth / 2 + margin]
          ];

          corners.forEach(([cx, cz]) => {
            const pt = new THREE.Vector3(relX + cx, 0, relZ + cz);
            pt.applyAxisAngle(new THREE.Vector3(0, 1, 0), rotY);
            pt.add(new THREE.Vector3(pivotLocalX, 0, pivotLocalZ));
            clusterPoints.push({ x: pt.x, z: pt.z });
          });

          return (
            <group key={`k-${idx}-${kIdx}`} position={[pivotLocalX, 0.4, pivotLocalZ]} rotation={[0, rotY, 0]}>
              <RoundedBox args={[keyWidth, 0.4, keyDepth]} position={[relX, 0, relZ]} radius={0.05} smoothness={4}>
                <meshStandardMaterial color={color} roughness={0.4} metalness={0.3} />
              </RoundedBox>
            </group>
          );
        });

        // Create the base shape using a convex hull + Minkowski sum for perfect rounded corners
        const hull = getConvexHull(clusterPoints);
        const shape = new THREE.Shape();
        if (hull.length > 0) {
          // Ensure CCW ordering for outward normals
          let area = 0;
          for (let i = 0; i < hull.length; i++) {
            const p1 = hull[i];
            const p2 = hull[(i + 1) % hull.length];
            area += p1.x * p2.z - p2.x * p1.z;
          }
          if (area < 0) {
            hull.reverse();
          }

          const R = 0.22; // Tighter corner radius and bezel padding
          const edges: { nx: number, nz: number }[] = [];
          for (let i = 0; i < hull.length; i++) {
            const p1 = hull[i];
            const p2 = hull[(i + 1) % hull.length];
            const dx = p2.x - p1.x;
            const dz = p2.z - p1.z;
            const len = Math.hypot(dx, dz);
            // Outward normal (CCW polygon)
            edges.push({ nx: dz / len, nz: -dx / len });
          }

          shape.moveTo(hull[0].x + edges[0].nx * R, hull[0].z + edges[0].nz * R);
          for (let i = 0; i < hull.length; i++) {
            const p2 = hull[(i + 1) % hull.length];
            const e = edges[i];
            const nextE = edges[(i + 1) % hull.length];

            // Draw straight line to the end of the current offset edge
            shape.lineTo(p2.x + e.nx * R, p2.z + e.nz * R);

            // Sweep a perfectly round arc around the corner vertex
            const startAngle = Math.atan2(e.nz, e.nx);
            let endAngle = Math.atan2(nextE.nz, nextE.nx);
            while (endAngle < startAngle) endAngle += Math.PI * 2;

            if (endAngle - startAngle > 0.001) {
              shape.absarc(p2.x, p2.z, R, startAngle, endAngle, false);
            }
          }
        }

        // Ergonomic tenting and visual separation if split
        let tentZ = 0;
        let splitOffsetX = 0;
        if (isSplit) {
          tentZ = idx === 0 ? 0.08 : -0.08; // tent middle upwards
          splitOffsetX = idx === 0 ? -0.6 : 0.6; // move left half left, right half right
        }

        elements.push(
          <group key={`cluster-${idx}`} position={[worldX + splitOffsetX, 0, worldZ]} rotation={[0, 0, tentZ]}>
            {hull.length > 0 && (
              <mesh rotation={[Math.PI / 2, 0, 0]} position={[0, 0.2, 0]}>
                <extrudeGeometry args={[shape, { depth: 0.45, bevelEnabled: true, bevelSize: 0.05, bevelThickness: 0.05, bevelSegments: 4 }]} />
                <meshStandardMaterial color="#0a0a0a" roughness={0.6} metalness={0.5} />
              </mesh>
            )}
            {keyElements}
          </group>
        );
      });

      return { elements };
    }
  }, [physicalLayout, layers, activeLayer]);

  return (
    <group ref={group}>
      {elements}
    </group>
  )
}

export default function Background3D() {
  const isConnected = useLayoutStore(state => state.isConnected);

  return (
    <div style={{
      position: 'fixed',
      top: 0, left: 0, width: '100vw', height: '100vh',
      zIndex: -10,
      background: '#0d1117',
      pointerEvents: 'none'
    }}>
      <div style={{
        width: '100%', height: '100%',
        opacity: isConnected ? 1 : 0,
        transform: isConnected ? 'translateY(0)' : 'translateY(40px)',
        transition: 'opacity 2.5s cubic-bezier(0.16, 1, 0.3, 1), transform 2.5s cubic-bezier(0.16, 1, 0.3, 1)'
      }}>
      <Canvas camera={{ position: [0, 12, 22], fov: 45 }}>
        <color attach="background" args={['#0d1117']} />
        <fog attach="fog" args={['#0d1117', 15, 40]} />

        <ambientLight intensity={0.4} />
        <spotLight position={[10, 10, 10]} angle={0.15} penumbra={1} intensity={1.5} castShadow />
        <pointLight position={[-10, -10, -10]} intensity={0.8} color="#2a61a8" />
        <pointLight position={[10, -10, 10]} intensity={0.8} color="#6436b5" />

        <Float speed={2} rotationIntensity={0.5} floatIntensity={1} position={[0, -5, 0]}>
          <KeyboardModel />
        </Float>

        <ContactShadows position={[0, -8, 0]} opacity={0.6} scale={20} blur={2.5} far={4} color="#000000" />
        <Environment preset="city" />
      </Canvas>
      </div>
    </div>
  )
}
