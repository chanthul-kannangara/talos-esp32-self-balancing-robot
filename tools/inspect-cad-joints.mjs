import { readFileSync } from "node:fs";
import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";

const bytes = readFileSync(new URL("../public/models/talos-assembly.glb", import.meta.url));
const model = await new GLTFLoader().parseAsync(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength), "");
model.scene.updateMatrixWorld(true);
model.scene.traverse((object) => {
  if (!object.isMesh || /Head|Face|encoder-cover/.test(object.name)) return;
  const geometry = object.geometry.index ? object.geometry.toNonIndexed() : object.geometry;
  const position = geometry.getAttribute("position");
  const planes = new Map();
  const keyFor = (point) => `${Math.round(point.x * 1e6)},${Math.round(point.y * 1e6)}`;
  for (let index = 0; index < position.count; index += 3) {
    const points = [0, 1, 2].map((offset) => new THREE.Vector3().fromBufferAttribute(position, index + offset));
    if (Math.abs(points[0].z - points[1].z) > 1e-7 || Math.abs(points[0].z - points[2].z) > 1e-7) continue;
    const planeKey = Math.round(points[0].z * 1e6);
    if (!planes.has(planeKey)) planes.set(planeKey, { edges: new Map(), points: new Map() });
    const plane = planes.get(planeKey);
    points.forEach((point) => plane.points.set(keyFor(point), point));
    for (let edgeIndex = 0; edgeIndex < 3; edgeIndex++) {
      const keys = [keyFor(points[edgeIndex]), keyFor(points[(edgeIndex + 1) % 3])].sort();
      const edgeKey = keys.join("|");
      plane.edges.set(edgeKey, (plane.edges.get(edgeKey) || 0) + 1);
    }
  }
  const circles = [];
  for (const [planeKey, plane] of planes) {
    const neighbors = new Map();
    for (const [edge, count] of plane.edges) {
      if (count !== 1) continue;
      const [first, second] = edge.split("|");
      if (!neighbors.has(first)) neighbors.set(first, []);
      if (!neighbors.has(second)) neighbors.set(second, []);
      neighbors.get(first).push(second);
      neighbors.get(second).push(first);
    }
    const visited = new Set();
    for (const start of neighbors.keys()) {
      if (visited.has(start)) continue;
      const pending = [start];
      const group = [];
      while (pending.length) {
        const current = pending.pop();
        if (visited.has(current)) continue;
        visited.add(current);
        group.push(plane.points.get(current));
        pending.push(...neighbors.get(current).filter((neighbor) => !visited.has(neighbor)));
      }
      if (group.length < 10) continue;
      const bounds = new THREE.Box3().setFromPoints(group);
      const center = bounds.getCenter(new THREE.Vector3());
      center.z = planeKey / 1e6;
      const radii = group.map((point) => Math.hypot(point.x - center.x, point.y - center.y));
      const radius = radii.reduce((total, value) => total + value, 0) / radii.length;
      const error = Math.max(...radii.map((value) => Math.abs(value - radius)));
      if (error < 0.0002) circles.push({center:center.toArray(),radius,error,world:object.localToWorld(center.clone()).toArray()});
    }
  }
  console.log(JSON.stringify({name:object.name,origin:object.getWorldPosition(new THREE.Vector3()).toArray(),circles}));
});
