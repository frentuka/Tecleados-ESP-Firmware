import * as THREE from 'three'
import { Evaluator, Operation, Brush } from 'three-bvh-csg'

const geometryCache = new Map<string, THREE.BufferGeometry>();
const evaluator = new Evaluator();

export function getKeycapGeometry(width: number, depth: number) {
  const key = `${width.toFixed(3)}x${depth.toFixed(3)}`;
  if (geometryCache.has(key)) {
    return geometryCache.get(key)!;
  }

  const bevelSize = 0.12;
  const w = width - 2 * bevelSize;
  const d = depth - 2 * bevelSize;
  const targetR = Math.min(0.25, Math.min(width, depth) / 2 - 0.01);
  const r = Math.max(0.01, targetR - bevelSize);

  const shape = new THREE.Shape();
  shape.moveTo(-w / 2 + r, -d / 2);
  shape.lineTo(w / 2 - r, -d / 2);
  shape.absarc(w / 2 - r, -d / 2 + r, r, -Math.PI / 2, 0, false);
  shape.lineTo(w / 2, d / 2 - r);
  shape.absarc(w / 2 - r, d / 2 - r, r, 0, Math.PI / 2, false);
  shape.lineTo(-w / 2 + r, d / 2);
  shape.absarc(-w / 2 + r, d / 2 - r, r, Math.PI / 2, Math.PI, false);
  shape.lineTo(-w / 2, -d / 2 + r);
  shape.absarc(-w / 2 + r, -d / 2 + r, r, Math.PI, Math.PI * 1.5, false);

  const extrudeSettings = {
    depth: 0.05,
    bevelEnabled: true,
    bevelSegments: 12,
    bevelSize: bevelSize,
    bevelThickness: 0.35
  };

  const baseGeo = new THREE.ExtrudeGeometry(shape, extrudeSettings);
  const baseBrush = new Brush(baseGeo);
  baseBrush.updateMatrixWorld();

  const dishDepth = 0.04;
  const capsuleR = 2.0;
  const topZ = extrudeSettings.depth + extrudeSettings.bevelThickness;
  const capsuleZ = topZ + capsuleR - dishDepth;
  
  const isWide = width >= depth;
  const capsuleLength = Math.abs(width - depth);
  
  const capsuleGeo = new THREE.CapsuleGeometry(capsuleR, capsuleLength, 16, 32);
  const capsuleBrush = new Brush(capsuleGeo);
  
  capsuleBrush.position.set(0, 0, capsuleZ);
  if (isWide) {
    capsuleBrush.rotation.set(0, 0, Math.PI / 2);
  }
  capsuleBrush.updateMatrixWorld();

  const resultBrush = evaluator.evaluate(baseBrush, capsuleBrush, 1); // 1 is SUBTRACTION
  const finalGeometry = resultBrush.geometry;
  
  // Rotate to match the previous orientation (it was rotated in the mesh)
  finalGeometry.rotateX(-Math.PI / 2);
  
  geometryCache.set(key, finalGeometry);
  return finalGeometry;
}
