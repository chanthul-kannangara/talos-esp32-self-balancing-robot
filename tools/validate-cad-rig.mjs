import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { createCadLegRig } from "../src/components/cadLegRig.js";

const bytes = readFileSync(new URL("../public/models/talos-assembly.glb", import.meta.url));
const model = (await new GLTFLoader().parseAsync(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength), "")).scene;
const originals = [];
model.traverse((part) => {
  if (part.isMesh) originals.push({ part, position: part.position.clone(), quaternion: part.quaternion.clone(), geometry: part.geometry });
});
assert.equal(originals.length, 13);
const rigs = ["left", "right"].map((side) => createCadLegRig(model, side, () => new THREE.Group()));
for (const rig of rigs) assert.equal(rig.applyReference(), true);
for (const { part, position, quaternion, geometry } of originals) {
  assert.ok(part.position.distanceTo(position) < 1e-7, `${part.name} reference position changed`);
  assert.ok(part.quaternion.toArray().every((value, index) => Math.abs(value - quaternion.toArray()[index]) < 1e-7), `${part.name} reference orientation changed`);
  assert.equal(part.geometry, geometry);
}
const results = [];
for (const [index, rig] of rigs.entries()) {
  const opposite = originals.filter(({ part }) => rigs[1 - index].names.includes(part.name));
  const before = opposite.map(({ part }) => ({ position: part.position.clone(), quaternion: part.quaternion.clone() }));
  const supportedAngles = [];
  for (let angle = 0; angle <= 160; angle += 1) {
    if (rig.setBend(angle)) supportedAngles.push(angle);
    for (const [otherIndex, { part }] of opposite.entries()) {
      assert.ok(part.position.distanceTo(before[otherIndex].position) < 1e-10);
      assert.deepEqual(part.quaternion.toArray(), before[otherIndex].quaternion.toArray());
    }
  }
  rig.restoreReference();
  results.push({ side: index === 0 ? "left" : "right", referenceAngle: rig.referenceAngleDeg,
    estimatedExtendedAngle: rig.estimatedExtendedAngleDeg, supportedRange: [supportedAngles[0], supportedAngles.at(-1)], feasibleCount: supportedAngles.length });
}
for (const { part, position, quaternion } of originals) {
  assert.deepEqual(part.position.toArray(), position.toArray());
  assert.deepEqual(part.quaternion.toArray(), quaternion.toArray());
}
console.log(JSON.stringify({ structuralParts: originals.length, referencePreserved: true, independentLegs: true, results }, null, 2));
