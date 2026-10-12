import { readFileSync } from "node:fs";
import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";

const bytes = readFileSync(new URL("../public/models/talos-assembly.glb", import.meta.url));
const data = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
const model = await new GLTFLoader().parseAsync(data, "");
model.scene.updateMatrixWorld(true);
model.scene.traverse((object) => {
  if (!object.isMesh) return;
  object.geometry.computeBoundingBox();
  const world = new THREE.Box3().setFromObject(object);
  console.log(JSON.stringify({
    name: object.name,
    origin: object.getWorldPosition(new THREE.Vector3()).toArray(),
    bounds: [world.min.toArray(), world.max.toArray()],
    localBounds: [object.geometry.boundingBox.min.toArray(), object.geometry.boundingBox.max.toArray()],
  }));
});
