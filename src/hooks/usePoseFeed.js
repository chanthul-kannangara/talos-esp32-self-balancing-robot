import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { allPoseOutputsNeutral, createVisualPose } from "../services/poseTelemetry";
import { openUsbPoseStream, usbPoseAvailable } from "../services/usbPoseStream";

export function usePoseFeed(cloudStatus) {
  const sessionRef = useRef(null);
  const mountedRef = useRef(true);
  const connectingRef = useRef(false);
  const [frame, setFrame] = useState(null);
  const [clock, setClock] = useState(performance.now());
  const [phase, setPhase] = useState("cloud");
  const [error, setError] = useState("");
  const [localReference, setLocalReference] = useState(null);
  const [diagnostic, setDiagnostic] = useState(false);
  const metricsRef = useRef({ start: 0, count: 0, rate: 0 });
  useEffect(() => {
    mountedRef.current = true;
    const timer = window.setInterval(() => setClock(performance.now()), 100);
    return () => {
      mountedRef.current = false;
      window.clearInterval(timer);
      sessionRef.current?.close();
    };
  }, []);
  const connect = useCallback(async () => {
    if (connectingRef.current || sessionRef.current) return;
    if (!window.confirm("Opening USB can reset the ESP32. Keep the robot securely supported with wheels clear and servo power OFF. Ready to select its USB port (COM8)? This connection sends telemetry commands only.")) return;
    connectingRef.current = true;
    metricsRef.current = { start: 0, count: 0, rate: 0 };
    setError("");
    setPhase("connecting");
    try {
      const session = await openUsbPoseStream({
        onFrame: (next) => {
          if (!mountedRef.current) return;
          const metric = metricsRef.current;
          if (!metric.start) metric.start = next.poseReceivedAtMs;
          metric.count++;
          if (next.poseReceivedAtMs - metric.start >= 1000) {
            metric.rate = metric.count * 1000 / (next.poseReceivedAtMs - metric.start);
            metric.count = 0;
            metric.start = next.poseReceivedAtMs;
          }
          setClock(next.poseReceivedAtMs);
          setFrame(next);
        },
        onError: (message) => { if (mountedRef.current) setError(message); },
        onClose: () => {
          sessionRef.current = null;
          if (mountedRef.current) { setPhase("cloud"); setFrame(null); }
        },
      });
      if (!mountedRef.current) { await session.close(); return; }
      if (session.closed) return;
      sessionRef.current = session;
      setPhase("usb");
    } catch (connectError) {
      if (mountedRef.current) {
        setError(connectError.name === "NotFoundError" ? "USB selection cancelled." : connectError.message);
        setPhase("cloud");
      }
    } finally { connectingRef.current = false; }
  }, []);
  const disconnect = useCallback(() => sessionRef.current?.close(), []);
  const usbSelected = phase === "usb";
  const frameAgeMs = frame ? Math.max(0, clock - frame.poseReceivedAtMs) : Infinity;
  const usbFresh = usbSelected && frameAgeMs < 250;
  const cloudFresh = Boolean(cloudStatus.online && Date.now() - Number(cloudStatus.updatedAt) < 3000);
  const status = useMemo(() => {
    const base = usbSelected ? { ...cloudStatus, ...frame, online: usbFresh, poseTransport: "usb" } :
      { ...cloudStatus, online: cloudFresh, poseTransport: "cloud" };
    return createVisualPose(base, localReference, diagnostic);
  }, [cloudStatus, frame, usbSelected, usbFresh, cloudFresh, localReference, diagnostic]);
  const referenceReady = status.online && Number.isInteger(status.poseBootId) && allPoseOutputsNeutral(status) && ["left", "right"].every(side =>
    Number.isFinite(Number(status[`${side}EncoderDeg`])) && Number(status[`${side}EncoderSampleAgeMs`]) < 100 &&
    (Number(status[`${side}MagneticStatus`]) & 0x20) !== 0);
  const captureReference = () => {
    if (!referenceReady || !window.confirm("Visual reference only: with servo power OFF and the robot securely supported, are BOTH legs at their true fully extended position? This sets a browser-only drawing reference, not controller calibration or motor commands. Weak-field angles remain untrusted.")) return;
    setLocalReference({ bootId: status.poseBootId, left: Number(status.leftEncoderDeg), right: Number(status.rightEncoderDeg) });
    setDiagnostic(true);
  };
  return { status, phase, error, usbSupported: usbPoseAvailable(), connect, disconnect, diagnostic, setDiagnostic,
    captureReference, referenceReady, hasLocalReference: Boolean(localReference && localReference.bootId === status.poseBootId),
    sampleRate: metricsRef.current.rate, frameAgeMs: Number.isFinite(frameAgeMs) ? frameAgeMs : null };
}
