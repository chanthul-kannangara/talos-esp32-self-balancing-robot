import { useEffect, useRef, useState } from "react";
import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import RobotDiagram from "./RobotDiagram";

export function disposeModel(model) {
  const geometries = new Set();
  const materials = new Set();
  model.traverse((object) => {
    if (object.isInstancedMesh) object.dispose();
    if (object.geometry) geometries.add(object.geometry);
    const objectMaterials = Array.isArray(object.material) ? object.material : [object.material];
    objectMaterials.filter(Boolean).forEach((material) => materials.add(material));
  });
  geometries.forEach((geometry) => geometry.dispose());
  materials.forEach((material) => {
    Object.values(material).forEach((value) => {
      if (value?.isTexture) value.dispose();
    });
    material.dispose();
  });
}

export function createDisplayWheel() {
  const wheel = new THREE.Group();
  const rubber = new THREE.MeshStandardMaterial({ color: 0x252b28, roughness: 0.9 });
  const metal = new THREE.MeshStandardMaterial({ color: 0xa8b6af, metalness: 0.55, roughness: 0.35 });
  const hubMaterial = new THREE.MeshStandardMaterial({ color: 0x343e38, metalness: 0.25, roughness: 0.5 });
  const tire = new THREE.Mesh(new THREE.CylinderGeometry(0.048, 0.048, 0.033, 48), rubber);
  tire.rotation.x = Math.PI / 2;
  wheel.add(tire);

  for (const side of [-1, 1]) {
    const rim = new THREE.Mesh(new THREE.TorusGeometry(0.031, 0.003, 8, 40), metal);
    rim.position.z = side * 0.017;
    wheel.add(rim);
    const hub = new THREE.Mesh(new THREE.CylinderGeometry(0.011, 0.011, 0.005, 24), hubMaterial);
    hub.rotation.x = Math.PI / 2;
    hub.position.z = side * 0.019;
    wheel.add(hub);
    const spokes = new THREE.InstancedMesh(new THREE.BoxGeometry(0.005, 0.024, 0.004), metal, 6);
    const spokeTransform = new THREE.Object3D();
    for (let spokeIndex = 0; spokeIndex < 6; spokeIndex += 1) {
      const angle = spokeIndex * Math.PI / 3;
      spokeTransform.position.set(Math.sin(angle) * 0.019, Math.cos(angle) * 0.019, side * 0.017);
      spokeTransform.rotation.z = -angle;
      spokeTransform.updateMatrix();
      spokes.setMatrixAt(spokeIndex, spokeTransform.matrix);
    }
    spokes.instanceMatrix.needsUpdate = true;
    wheel.add(spokes);
  }

  const treads = new THREE.InstancedMesh(new THREE.BoxGeometry(0.008, 0.007, 0.036), rubber, 32);
  const treadTransform = new THREE.Object3D();
  for (let treadIndex = 0; treadIndex < 32; treadIndex += 1) {
    const angle = treadIndex * Math.PI / 16;
    treadTransform.position.set(Math.sin(angle) * 0.047, Math.cos(angle) * 0.047, 0);
    treadTransform.rotation.z = -angle;
    treadTransform.updateMatrix();
    treads.setMatrixAt(treadIndex, treadTransform.matrix);
  }
  treads.instanceMatrix.needsUpdate = true;
  wheel.add(treads);
  return wheel;
}

export default function RobotModel({ paused = false }) {
  const containerRef = useRef(null);
  const pausedRef = useRef(paused);
  const updatePlaybackRef = useRef(null);
  const [renderState, setRenderState] = useState("loading");

  useEffect(() => {
    pausedRef.current = paused;
    updatePlaybackRef.current?.();
  }, [paused]);

  useEffect(() => {
    const container = containerRef.current;
    let renderer;
    try {
      renderer = new THREE.WebGLRenderer({ alpha: true, antialias: true, powerPreference: "low-power" });
    } catch {
      setRenderState("fallback");
      return undefined;
    }

    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.45;
    renderer.domElement.setAttribute("aria-hidden", "true");
    container.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(30, 1, 0.1, 30);
    const assembly = new THREE.Group();
    scene.add(assembly);
    scene.add(new THREE.HemisphereLight(0xf2fff6, 0x66746a, 3.5));
    const keyLight = new THREE.DirectionalLight(0xffffff, 5);
    keyLight.position.set(-4, 5, 4);
    scene.add(keyLight);
    const edgeLight = new THREE.DirectionalLight(0xd1ffca, 3);
    edgeLight.position.set(3, 2, -4);
    scene.add(edgeLight);
    const fillLight = new THREE.DirectionalLight(0xffffff, 2);
    fillLight.position.set(-3, 0, -4);
    scene.add(fillLight);

    const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
    const abortController = new AbortController();
    const displayWheels = [];
    let disposed = false;
    let failed = false;
    let loaded = false;
    let inView = true;
    let modelSize = new THREE.Vector3(2.4, 2, 1.5);
    let previousFrame = 0;
    let animationTime = 0;

    const render = () => {
      if (!disposed && !failed) renderer.render(scene, camera);
    };

    const resize = () => {
      const width = container.clientWidth;
      const height = container.clientHeight;
      if (!width || !height || disposed || failed) return;
      renderer.setSize(width, height, false);
      camera.aspect = width / height;
      const verticalFov = THREE.MathUtils.degToRad(camera.fov);
      const horizontalFov = 2 * Math.atan(Math.tan(verticalFov / 2) * camera.aspect);
      const distance = Math.max(modelSize.y / (2 * Math.tan(verticalFov / 2)), Math.max(modelSize.x, modelSize.z) / (2 * Math.tan(horizontalFov / 2))) * 1.42;
      camera.position.copy(new THREE.Vector3(-1, 0.42, 0.85).normalize().multiplyScalar(distance));
      camera.lookAt(0, 0.04, 0);
      camera.updateProjectionMatrix();
      render();
    };

    const animate = (timestamp) => {
      if (timestamp - previousFrame < 1000 / 30) return;
      const elapsed = previousFrame ? Math.min((timestamp - previousFrame) / 1000, 0.07) : 0;
      previousFrame = timestamp;
      animationTime += elapsed;
      assembly.rotation.y = Math.sin(animationTime * 0.38) * 0.18;
      assembly.rotation.z = Math.sin(animationTime * 0.8) * 0.025;
      displayWheels.forEach((wheel) => {
        wheel.rotation.z = Math.sin(animationTime * 0.8) * 0.14;
      });
      render();
    };

    const updatePlayback = () => {
      if (disposed || failed) return;
      previousFrame = 0;
      const canAnimate = loaded && inView && !document.hidden && !pausedRef.current && !reducedMotion.matches;
      container.dataset.playback = canAnimate ? "playing" : reducedMotion.matches ? "reduced-motion" : pausedRef.current ? "paused" : "idle";
      renderer.setAnimationLoop(canAnimate ? animate : null);
      if (inView && !document.hidden) render();
    };
    updatePlaybackRef.current = updatePlayback;

    const onContextLost = (event) => {
      event.preventDefault();
      failed = true;
      renderer.setAnimationLoop(null);
      if (!disposed) setRenderState("fallback");
    };
    renderer.domElement.addEventListener("webglcontextlost", onContextLost);
    document.addEventListener("visibilitychange", updatePlayback);
    reducedMotion.addEventListener("change", updatePlayback);
    const resizeObserver = new ResizeObserver(resize);
    resizeObserver.observe(container);
    const intersectionObserver = new IntersectionObserver(([entry]) => {
      inView = entry.isIntersecting;
      updatePlayback();
    });
    intersectionObserver.observe(container);
    resize();

    async function loadAssembly() {
      try {
        const response = await fetch("/models/talos-assembly.glb", { signal: abortController.signal });
        if (!response.ok) throw new Error("CAD preview unavailable");
        const gltf = await new GLTFLoader().parseAsync(await response.arrayBuffer(), "/models/");
        if (disposed || failed) {
          disposeModel(gltf.scene);
          return;
        }
        gltf.scene.updateMatrixWorld(true);
        const originalMaterials = new Set();
        gltf.scene.traverse((object) => {
          if (!object.isMesh) return;
          const oldMaterials = Array.isArray(object.material) ? object.material : [object.material];
          oldMaterials.forEach((material) => originalMaterials.add(material));
          object.material = new THREE.MeshStandardMaterial({
            color: /Head|Face/.test(object.name) ? 0x252b28 : 0x38433c,
            metalness: 0.15,
            roughness: 0.55,
          });
        });
        originalMaterials.forEach((material) => material.dispose());
        assembly.add(gltf.scene);

        for (const [name, side] of [["Left_Wheel_Leg-1", -1], ["Right_Wheel_Leg_-2", 1]]) {
          const leg = gltf.scene.getObjectByName(name);
          if (!leg) continue;
          const wheel = createDisplayWheel();
          wheel.position.copy(leg.localToWorld(new THREE.Vector3(0, -0.122, 0)));
          wheel.position.z += side * 0.025;
          assembly.add(wheel);
          displayWheels.push(wheel);
        }

        const bounds = new THREE.Box3().setFromObject(assembly);
        const center = bounds.getCenter(new THREE.Vector3());
        const scale = 2 / bounds.getSize(new THREE.Vector3()).y;
        assembly.children.forEach((child) => child.position.sub(center));
        assembly.scale.setScalar(scale);
        modelSize = bounds.getSize(new THREE.Vector3()).multiplyScalar(scale);
        loaded = true;
        setRenderState("ready");
        resize();
        updatePlayback();
      } catch (error) {
        if (disposed || error.name === "AbortError") return;
        failed = true;
        renderer.setAnimationLoop(null);
        setRenderState("fallback");
      }
    }
    loadAssembly();

    return () => {
      disposed = true;
      abortController.abort();
      updatePlaybackRef.current = null;
      resizeObserver.disconnect();
      intersectionObserver.disconnect();
      document.removeEventListener("visibilitychange", updatePlayback);
      reducedMotion.removeEventListener("change", updatePlayback);
      renderer.domElement.removeEventListener("webglcontextlost", onContextLost);
      renderer.setAnimationLoop(null);
      disposeModel(assembly);
      renderer.dispose();
      renderer.domElement.remove();
    };
  }, []);

  return (
    <div className={`robot-model robot-model--${renderState}`} role="img" aria-label={renderState === "ready" ? "TALOS 3D CAD preview with illustrative wheels" : "TALOS robot illustration"} data-render-state={renderState}>
      <div className="robot-model__canvas" ref={containerRef} />
      {renderState !== "ready" ? <RobotDiagram animated={!paused} extension={40} /> : null}
    </div>
  );
}
