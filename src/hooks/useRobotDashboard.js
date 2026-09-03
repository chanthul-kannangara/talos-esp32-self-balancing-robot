import { useCallback, useEffect, useMemo, useState } from "react";
import {
  applyBalanceTuning,
  listenToRobotStatus,
  requestPostureCalibration,
  requestWifiAction,
  saveCameraUrl,
  sendCommand,
  stopAndDisarm,
  triggerEmergencyStop,
  updateArmed,
  updateFallAngleLimit,
  updateLegHeight,
  updateLegAssist,
  updateMode,
  updateSpeed,
} from "../services/robotApi";

const ROBOT_HEARTBEAT_STALE_MS = 15_000;

const MAX_LOG_ENTRIES = 8;

function createLogEntry(message) {
  return {
    id: `${Date.now()}-${Math.random().toString(16).slice(2)}`,
    message,
    time: new Intl.DateTimeFormat("en", {
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    }).format(new Date()),
  };
}

function modeLabel(value) {
  const labels = {
    manual: "Manual",
    "balance-only": "Balance only",
    "camera-assist": "Camera assist",
  };

  return labels[value] || "Manual";
}

export function useRobotDashboard() {
  const initialRobotState = useMemo(() => listenToRobotStatus(), []);
  const [status, setStatus] = useState(initialRobotState.status);
  const [currentCommand, setCurrentCommand] = useState(initialRobotState.control.command);
  const [speed, setSpeed] = useState(initialRobotState.control.speed);
  const [legPosition, setLegPosition] = useState(initialRobotState.control.legPosition);
  const [legSpeed, setLegSpeed] = useState(initialRobotState.control.legSpeed);
  const [legAssist, setLegAssist] = useState(initialRobotState.control.legAssist);
  const [armed, setArmed] = useState(initialRobotState.control.armed);
  const [mode, setMode] = useState(initialRobotState.control.mode);
  const [fallAngleLimitDeg, setFallAngleLimitDeg] = useState(
    initialRobotState.control.fallAngleLimitDeg,
  );
  const [balanceTuning, setBalanceTuning] = useState(
    initialRobotState.control.tuning,
  );
  const [cameraUrl, setCameraUrl] = useState(initialRobotState.camera.streamUrl);
  const [clockMs, setClockMs] = useState(Date.now());
  const [activityEntries, setActivityEntries] = useState(() => [
    createLogEntry("dashboard ready"),
    createLogEntry("stop sent"),
  ]);

  useEffect(() => {
    return listenToRobotStatus((robotState) => {
      setStatus(robotState.status);
      setCurrentCommand(robotState.control.command);
      setSpeed(robotState.control.speed);
      setLegPosition(robotState.control.legPosition);
      setLegSpeed(robotState.control.legSpeed);
      setLegAssist(robotState.control.legAssist);
      setArmed(robotState.control.armed);
      setMode(robotState.control.mode);
      setFallAngleLimitDeg(robotState.status.fallAngleLimitDeg);
      setBalanceTuning(robotState.status.tuning);
      setCameraUrl(robotState.camera.streamUrl);
    });
  }, []);

  useEffect(() => {
    const timer = window.setInterval(() => setClockMs(Date.now()), 1000);
    return () => window.clearInterval(timer);
  }, []);

  const addLogEntry = useCallback((message) => {
    setActivityEntries((entries) => [
      createLogEntry(message),
      ...entries,
    ].slice(0, MAX_LOG_ENTRIES));
  }, []);

  const handleCommand = useCallback(
    async (command) => {
      await sendCommand(command);
      addLogEntry(`${command} sent`);
    },
    [addLogEntry],
  );

  const handleStopDisarm = useCallback(async () => {
    await stopAndDisarm();
    addLogEntry("balance off requested · all actuator outputs stop immediately");
  }, [addLogEntry]);

  const handleArmedChange = useCallback(
    async (nextArmed) => {
      await updateArmed(nextArmed);
      addLogEntry(nextArmed ? "arm requested" : "disarm requested");
    },
    [addLogEntry],
  );

  const handleFallAngleLimitChange = useCallback(
    async (nextLimit) => {
      await updateFallAngleLimit(nextLimit);
      addLogEntry(`fall cutoff requested at ${nextLimit}°`);
    },
    [addLogEntry],
  );

  const handleBalanceTuningApply = useCallback(
    async (nextTuning) => {
      await applyBalanceTuning(nextTuning);
      addLogEntry("validated balance tuning sent");
    },
    [addLogEntry],
  );

  const handleEmergencyStop = useCallback(async () => {
    await triggerEmergencyStop();
    addLogEntry("EMERGENCY STOP sent");
  }, [addLogEntry]);

  const handleWifiAction = useCallback(
    async (action, options) => {
      await requestWifiAction(action, options);
      const ssid = options?.ssid ? ` · ${options.ssid}` : "";
      addLogEntry(`Wi-Fi ${action} requested${ssid}`);
    },
    [addLogEntry],
  );

  const handleSpeedChange = useCallback(
    async (nextSpeed) => {
      setSpeed(nextSpeed);
      await updateSpeed(nextSpeed);
      addLogEntry(`speed changed to ${nextSpeed}%`);
    },
    [addLogEntry],
  );

  const handleHeightCommandApply = useCallback(
    async (nextPosition, nextSpeed) => {
      await updateLegHeight(nextPosition, nextSpeed);
      setLegPosition(nextPosition);
      setLegSpeed(nextSpeed);
      const targetHeightCm = 16.2 + (29.2 - 16.2) * (nextPosition / 100);
      addLogEntry(
        `height requested at ${targetHeightCm.toFixed(1)} cm · ${nextSpeed}% leg speed`,
      );
    },
    [addLogEntry],
  );

  const handlePostureCalibration = useCallback(
    async (action) => {
      await requestPostureCalibration(action);
      addLogEntry(
        action === "capture-bent"
          ? "16.2 cm bent endpoint capture requested"
          : "29.2 cm extended endpoint capture requested",
      );
    },
    [addLogEntry],
  );

  const handleLegAssistChange = useCallback(
    async (enabled) => {
      await updateLegAssist(enabled);
      addLogEntry(`leg assist ${enabled ? "enabled" : "disabled"}`);
    },
    [addLogEntry],
  );

  const handleModeChange = useCallback(
    async (nextMode) => {
      await updateMode(nextMode);
      addLogEntry(`${modeLabel(nextMode)} mode selected`);
    },
    [addLogEntry],
  );

  const handleCameraSave = useCallback(
    async (event) => {
      event.preventDefault();
      await saveCameraUrl(cameraUrl);
      addLogEntry("camera URL saved");
    },
    [addLogEntry, cameraUrl],
  );

  const dashboardStatus = useMemo(() => {
    const updatedAt = Number(status.updatedAt || 0);
    const heartbeatAgeMs = Math.max(0, clockMs - updatedAt);
    const heartbeatFresh =
      updatedAt > 0 && heartbeatAgeMs <= ROBOT_HEARTBEAT_STALE_MS;

    return {
      ...status,
      online: Boolean(status.online && heartbeatFresh),
      lastCommand: currentCommand,
    };
  }, [clockMs, currentCommand, status]);

  return {
    activityEntries,
    armed,
    balanceTuning,
    cameraUrl,
    currentCommand,
    dashboardStatus,
    fallAngleLimitDeg,
    handleBalanceTuningApply,
    handleCameraSave,
    handleCommand,
    handleArmedChange,
    handleEmergencyStop,
    handleFallAngleLimitChange,
    handleHeightCommandApply,
    handleLegAssistChange,
    handleModeChange,
    handleSpeedChange,
    handleStopDisarm,
    handlePostureCalibration,
    handleWifiAction,
    legAssist,
    legPosition,
    legSpeed,
    mode,
    setCameraUrl,
    speed,
    status: dashboardStatus,
  };
}
