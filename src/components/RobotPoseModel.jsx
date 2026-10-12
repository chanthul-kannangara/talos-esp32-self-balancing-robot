import { useEffect, useRef, useState } from "react";
import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { createDisplayWheel, disposeModel } from "./RobotModel";
import { createCadLegRig } from "./cadLegRig";

export default function RobotPoseModel({ status, compact = false }) {
  const canvasRef = useRef(null);
  const drawRef = useRef(null);
  const lastPoseRef = useRef({ left: null, right: null, pitch: 0, roll: 0 });
  const [renderState, setRenderState] = useState("loading");
  const [geometryWarning, setGeometryWarning] = useState(false);
  const imuHealthy = typeof status.imuSampleHealthy === "boolean" ? status.imuSampleHealthy : Number(status.imuErrors || 0) === 0;
  const imuValid = Boolean(status.online && imuHealthy &&
    Number.isFinite(Number(status.tilt)) && Number.isFinite(Number(status.roll)));
  const poseValid = Boolean(status.online && status.visualLegPoseAvailable &&
    Number.isFinite(Number(status.leftBendDeg)) && Number.isFinite(Number(status.rightBendDeg)));
  const diagnostic = poseValid && status.visualLegPoseDiagnostic;
  const source = diagnostic ? "diagnostic-encoder-preview" : poseValid ? "measured" : imuValid ? "live-imu-reference-legs" : "reference-or-held";

  useEffect(() => {
    if (imuValid) {
      lastPoseRef.current.pitch = Number(status.tilt);
      lastPoseRef.current.roll = Number(status.roll);
    }
    if (poseValid) {
      lastPoseRef.current.left = Number(status.leftBendDeg);
      lastPoseRef.current.right = Number(status.rightBendDeg);
    }
    drawRef.current?.();
  }, [imuValid, poseValid, status.leftBendDeg, status.rightBendDeg, status.tilt, status.roll]);

  useEffect(() => {
    const container = canvasRef.current;
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
    renderer.toneMappingExposure = 1.4;
    renderer.domElement.setAttribute("aria-hidden", "true");
    container.appendChild(renderer.domElement);
    const scene = new THREE.Scene();
    scene.add(new THREE.HemisphereLight(0xf3fff7, 0x6c786e, 3.5));
    const light = new THREE.DirectionalLight(0xffffff, 5);
    light.position.set(-3, 4, 4);
    scene.add(light);
    const edge = new THREE.DirectionalLight(0xcaf5d2, 3);
    edge.position.set(3, 2, -4);
    scene.add(edge);
    const camera = new THREE.PerspectiveCamera(32, 1, 0.01, 5);
    const robot = new THREE.Group();
    scene.add(robot);
    const display = { left: null, right: null, pitch: 0, roll: 0 };
    const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
    const abort = new AbortController();
    const target = new THREE.Vector3(0, -0.075, 0);
    let modelSize = new THREE.Vector3(0.2, 0.36, 0.32);
    let rigs;
    let disposed = false;
    let inView = true;
    let previousFrame = 0;
    let warned = false;

    const render = (timestamp = performance.now()) => {
      if (disposed || !inView || document.hidden) return;
      const elapsed = previousFrame ? Math.min((timestamp - previousFrame) / 1000, 0.1) : 1 / 60;
      if (previousFrame && elapsed < 1 / 60 - 0.001) return;
      previousFrame = timestamp;
      const blend = reducedMotion.matches ? 1 : 1 - Math.exp(-elapsed / 0.025);
      const desired = lastPoseRef.current;
      let remaining = 0;
      let outsideRig = false;
      for (const key of ["pitch", "roll"]) {
        display[key] += (desired[key] - display[key]) * blend;
        remaining = Math.max(remaining, Math.abs(desired[key] - display[key]));
      }
      if (rigs) {
        for (const side of ["left", "right"]) {
          if (desired[side] === null) continue;
          const rig = rigs[side];
          if (!rig.canBend(desired[side])) {
            outsideRig = true;
            continue;
          }
          const origin = display[side] ?? rig.referenceAngleDeg - rig.estimatedExtendedAngleDeg;
          const next = origin + (desired[side] - origin) * blend;
          if (rig.setBend(next)) {
            display[side] = next;
            remaining = Math.max(remaining, Math.abs(desired[side] - next));
          } else outsideRig = true;
        }
      }
      if (warned !== outsideRig) {
        warned = outsideRig;
        setGeometryWarning(outsideRig);
      }
      robot.rotation.z = -THREE.MathUtils.degToRad(display.pitch);
      robot.rotation.x = THREE.MathUtils.degToRad(display.roll);
      container.dataset.leftBend = display.left === null ? "reference" : display.left.toFixed(2);
      container.dataset.rightBend = display.right === null ? "reference" : display.right.toFixed(2);
      container.dataset.pitch = display.pitch.toFixed(2);
      container.dataset.cadPitch = (-display.pitch).toFixed(2);
      container.dataset.roll = display.roll.toFixed(2);
      renderer.render(scene, camera);
      if (remaining < 0.01) renderer.setAnimationLoop(null);
    };
    const invalidate = () => {
      if (disposed) return;
      previousFrame = 0;
      renderer.setAnimationLoop(inView && !document.hidden && !reducedMotion.matches ? render : null);
      render();
    };
    drawRef.current = invalidate;
    const resize = () => {
      const width = container.clientWidth;
      const height = container.clientHeight;
      if (!width || !height || disposed) return;
      renderer.setSize(width, height, false);
      camera.aspect = width / height;
      const vertical = THREE.MathUtils.degToRad(camera.fov);
      const horizontal = 2 * Math.atan(Math.tan(vertical / 2) * camera.aspect);
      const distance = Math.max(modelSize.y / (2 * Math.tan(vertical / 2)),
        Math.hypot(modelSize.x, modelSize.z) / (2 * Math.tan(horizontal / 2))) * 1.3;
      camera.position.copy(new THREE.Vector3(-1, 0.32, 1.2).normalize().multiplyScalar(distance).add(target));
      camera.lookAt(target);
      camera.updateProjectionMatrix();
      invalidate();
    };
    const observer = new ResizeObserver(resize);
    observer.observe(container);
    const visibility = new IntersectionObserver(([entry]) => {
      inView = entry.isIntersecting;
      if (!inView) renderer.setAnimationLoop(null);
      else invalidate();
    });
    visibility.observe(container);
    document.addEventListener("visibilitychange", invalidate);
    reducedMotion.addEventListener("change", invalidate);
    const onContextLost = (event) => {
      event.preventDefault();
      renderer.setAnimationLoop(null);
      setRenderState("fallback");
    };
    renderer.domElement.addEventListener("webglcontextlost", onContextLost);
    resize();

    async function loadCad() {
      let loaded;
      try {
        const response = await fetch("/models/talos-assembly.glb", { signal: abort.signal });
        if (!response.ok) throw new Error("CAD model unavailable");
        loaded = (await new GLTFLoader().parseAsync(await response.arrayBuffer(), "/models/")).scene;
        if (disposed) { disposeModel(loaded); return; }
        const structuralParts = [];
        loaded.traverse((part) => { if (part.isMesh) structuralParts.push(part); });
        if (structuralParts.length !== 13) throw new Error("Unexpected CAD assembly");
        const materials = new Set();
        for (const part of structuralParts) {
          (Array.isArray(part.material) ? part.material : [part.material]).forEach((material) => materials.add(material));
          part.material = new THREE.MeshStandardMaterial({ color: /Head|Face/.test(part.name) ? 0x252d28 : 0x48564c,
            roughness: 0.55, metalness: 0.18 });
        }
        materials.forEach((material) => material.dispose());
        rigs = { left: createCadLegRig(loaded, "left", createDisplayWheel),
          right: createCadLegRig(loaded, "right", createDisplayWheel) };
        const bounds = new THREE.Box3().setFromObject(loaded);
        for (const bend of [0, 60]) {
          rigs.left.setBend(bend);
          rigs.right.setBend(bend);
          bounds.union(new THREE.Box3().setFromObject(loaded));
        }
        rigs.left.restoreReference();
        rigs.right.restoreReference();
        const body = loaded.getObjectByName("Head-1");
        const pivot = new THREE.Box3().setFromObject(body).getCenter(new THREE.Vector3());
        target.copy(bounds.getCenter(new THREE.Vector3()).sub(pivot));
        modelSize = bounds.getSize(new THREE.Vector3());
        loaded.position.sub(pivot);
        robot.add(loaded);
        container.dataset.cadParts = String(structuralParts.length);
        setRenderState("ready");
        resize();
      } catch (error) {
        if (loaded && !loaded.parent) disposeModel(loaded);
        if (!disposed && error.name !== "AbortError") setRenderState("fallback");
      }
    }
    loadCad();
    return () => {
      disposed = true;
      abort.abort();
      drawRef.current = null;
      observer.disconnect();
      visibility.disconnect();
      document.removeEventListener("visibilitychange", invalidate);
      reducedMotion.removeEventListener("change", invalidate);
      renderer.domElement.removeEventListener("webglcontextlost", onContextLost);
      renderer.setAnimationLoop(null);
      disposeModel(scene);
      renderer.dispose();
      renderer.domElement.remove();
    };
  }, []);

  const badge = geometryWarning ? "CAD registration needs checking" : diagnostic ? "Untrusted encoder preview · not calibrated" : poseValid ? imuValid ? "Live IMU + encoder pose" : "Live encoders · IMU held" :
    imuValid ? "Live IMU · reference / held legs" : "CAD reference / held pose";
  return <div className={`pose-model${compact ? " pose-model--compact" : ""}`} data-render-state={renderState}
    data-pose-source={source} role="img" aria-label={`${badge}. Actual 13-part CAD assembly with illustrative tires.`}>
    <div className="pose-model__canvas" ref={canvasRef} />
    <span className={`pose-model__badge ${diagnostic ? "is-diagnostic" : imuValid && !geometryWarning ? "is-live" : ""}`}>{badge}</span>
    {renderState === "loading" ? <p className="pose-model__fallback">Loading your CAD assembly…</p> : null}
    {renderState === "fallback" ? <p className="pose-model__fallback">CAD view unavailable. Use the measured readouts; no substitute model is shown.</p> : null}
  </div>;
}
