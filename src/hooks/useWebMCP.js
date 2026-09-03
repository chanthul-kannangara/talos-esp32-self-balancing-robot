import { useEffect, useState, useRef } from "react";
import { initWebMCP, webModelContext } from "../services/webmcp";

/**
 * useWebMCP Hook
 * Registers TALOS robot capabilities as WebMCP tools on navigator.modelContext
 * and window.__WEBMCP__, and provides reactive state for UI inspection.
 */
export function useWebMCP({
  status,
  dashboardStatus,
  currentCommand,
  speed,
  mode,
  legPosition,
  legSpeed,
  legAssist,
  balanceTuning,
  fallAngleLimitDeg,
  cameraUrl,
  handleCommand,
  handleSpeedChange,
  handleArmedChange,
  handleEmergencyStop,
  handleStopDisarm,
  handleHeightCommandApply,
  handleLegAssistChange,
  handleBalanceTuningApply,
  handleFallAngleLimitChange,
  handleModeChange,
  handleCameraSave,
}) {
  const [tools, setTools] = useState([]);
  const [executionLogs, setExecutionLogs] = useState([]);
  const [isReady, setIsReady] = useState(false);

  // Maintain latest values in refs for stable tool execution closures
  const stateRef = useRef();
  stateRef.current = {
    status,
    dashboardStatus,
    currentCommand,
    speed,
    mode,
    legPosition,
    legSpeed,
    legAssist,
    balanceTuning,
    fallAngleLimitDeg,
    cameraUrl,
    handleCommand,
    handleSpeedChange,
    handleArmedChange,
    handleEmergencyStop,
    handleStopDisarm,
    handleHeightCommandApply,
    handleLegAssistChange,
    handleBalanceTuningApply,
    handleFallAngleLimitChange,
    handleModeChange,
    handleCameraSave,
  };

  useEffect(() => {
    initWebMCP();

    // Define the 11 WebMCP tools conforming to W3C WebMCP Standard
    const robotTools = [
      {
        name: "talos_get_telemetry",
        description: "Fetch live sensor telemetry, balance orientation (pitch, roll), battery level, armed state, and subsystem health from the TALOS robot.",
        inputSchema: {
          type: "object",
          properties: {},
        },
        execute: async () => {
          const { status: currentStatus, dashboardStatus: currentDashboard, speed: currentSpd, mode: currentM, legPosition: currentLeg, legAssist: currentLegAssist } = stateRef.current;
          return {
            online: Boolean(currentStatus?.online),
            armed: Boolean(currentDashboard?.balancingEnabled),
            emergencyStopActive: Boolean(currentDashboard?.emergencyStopActive),
            orientation: {
              pitchDeg: Number(currentDashboard?.pitchDeg ?? 0),
              rollDeg: Number(currentDashboard?.rollDeg ?? 0),
              tiltAlert: Boolean(currentDashboard?.tiltAlert),
            },
            battery: {
              voltage: Number(currentStatus?.batteryVoltage ?? 11.8),
              percentage: Number(currentDashboard?.batteryPercent ?? 85),
            },
            motion: {
              command: stateRef.current.currentCommand || "stop",
              speed: Number(currentSpd ?? 50),
              mode: currentM || "manual",
            },
            posture: {
              legPositionPercent: Number(currentLeg ?? 50),
              legAssistActive: Boolean(currentLegAssist),
            },
            safety: {
              fallAngleLimitDeg: Number(stateRef.current.fallAngleLimitDeg ?? 60),
            },
            timestamp: new Date().toISOString(),
          };
        },
      },
      {
        name: "talos_motion_command",
        description: "Drive the robot with directional bias (stop, forward, backward, left, right). The deterministic 200 Hz FreeRTOS loop maintains balance while moving.",
        inputSchema: {
          type: "object",
          properties: {
            direction: {
              type: "string",
              enum: ["stop", "forward", "backward", "left", "right"],
              description: "The desired movement direction",
            },
          },
          required: ["direction"],
        },
        execute: async ({ direction }) => {
          if (!direction || !["stop", "forward", "backward", "left", "right"].includes(direction)) {
            throw new Error("Invalid direction. Must be one of: stop, forward, backward, left, right");
          }
          await stateRef.current.handleCommand(direction);
          return {
            success: true,
            command: direction,
            message: `Motion command '${direction}' executed successfully.`,
            timestamp: new Date().toISOString(),
          };
        },
      },
      {
        name: "talos_set_speed",
        description: "Set driving throttle and motion effort percentage (0 to 100%).",
        inputSchema: {
          type: "object",
          properties: {
            speed: {
              type: "number",
              minimum: 0,
              maximum: 100,
              description: "Target speed percentage between 0 and 100",
            },
          },
          required: ["speed"],
        },
        execute: async ({ speed: newSpeed }) => {
          const clamped = Math.min(100, Math.max(0, Number(newSpeed)));
          await stateRef.current.handleSpeedChange(clamped);
          return {
            success: true,
            speed: clamped,
            message: `Robot throttle updated to ${clamped}%.`,
          };
        },
      },
      {
        name: "talos_set_armed",
        description: "Arm or disarm the balancing control loop. Arming activates the 200 Hz PID wheel motors. Disarming coasts the wheels safely.",
        inputSchema: {
          type: "object",
          properties: {
            armed: {
              type: "boolean",
              description: "True to arm and balance, false to disarm into standby.",
            },
          },
          required: ["armed"],
        },
        execute: async ({ armed: shouldArm }) => {
          await stateRef.current.handleArmedChange(Boolean(shouldArm));
          return {
            success: true,
            armed: Boolean(shouldArm),
            message: shouldArm ? "Robot armed. Balance task active." : "Robot disarmed. Wheel motors stopped.",
          };
        },
      },
      {
        name: "talos_emergency_stop",
        description: "Immediate hardware emergency stop. Instantly disarms all wheel and leg actuators, cancels pending commands, and engages safety interlock.",
        inputSchema: {
          type: "object",
          properties: {},
        },
        execute: async () => {
          await stateRef.current.handleEmergencyStop();
          return {
            success: true,
            emergencyStopEngaged: true,
            message: "Emergency stop engaged. All motors cut off.",
          };
        },
      },
      {
        name: "talos_set_leg_posture",
        description: "Articulate dual MG996R leg servos to adjust chassis ride height, ground clearance, and pitch trim.",
        inputSchema: {
          type: "object",
          properties: {
            legPosition: {
              type: "integer",
              minimum: 0,
              maximum: 100,
              description: "Leg extension percentage (0 = fully bent / low clearance, 100 = fully extended / high clearance).",
            },
            legSpeed: {
              type: "integer",
              minimum: 0,
              maximum: 100,
              description: "Speed of transition (0 to 100 percent).",
            },
          },
          required: ["legPosition", "legSpeed"],
        },
        execute: async ({ legPosition: pos, legSpeed: spd }) => {
          const targetPos = Math.min(100, Math.max(0, Number(pos)));
          const targetSpd = Math.min(100, Math.max(0, Number(spd)));
          await stateRef.current.handleHeightCommandApply(targetPos, targetSpd);
          return {
            success: true,
            legPosition: targetPos,
            legSpeed: targetSpd,
            message: `Leg posture updated: extension ${targetPos}%, motion speed ${targetSpd}%.`,
          };
        },
      },
      {
        name: "talos_toggle_leg_assist",
        description: "Enable or disable active dynamic balance leg assist. When active, leg servos dynamically flex to counteract tilt disturbances.",
        inputSchema: {
          type: "object",
          properties: {
            enabled: {
              type: "boolean",
              description: "True to enable active leg stabilization assist, false to disable.",
            },
          },
          required: ["enabled"],
        },
        execute: async ({ enabled }) => {
          await stateRef.current.handleLegAssistChange(Boolean(enabled));
          return {
            success: true,
            legAssist: Boolean(enabled),
            message: enabled ? "Dynamic leg assist enabled." : "Dynamic leg assist disabled.",
          };
        },
      },
      {
        name: "talos_tune_pid",
        description: "Live-tune PID balance loop gains and wheel limits on the ESP32. Applied instantly over Wi-Fi/Firebase.",
        inputSchema: {
          type: "object",
          properties: {
            kp: { type: "number", minimum: 20, maximum: 250, description: "Proportional gain for tilt error (default: 80)." },
            ki: { type: "number", minimum: 0, maximum: 20, description: "Integral gain for steady-state offset (default: 0)." },
            kd: { type: "number", minimum: 0, maximum: 30, description: "Derivative gyro rate damping gain (default: 2.5)." },
            balanceTrimDeg: { type: "number", minimum: -5, maximum: 5, description: "Zero-angle mechanical pitch trim in degrees." },
            predictionTimeMs: { type: "number", minimum: 10, maximum: 80, description: "Filter lead prediction horizon in milliseconds." },
            maxWheelOutputUs: { type: "integer", minimum: 300, maximum: 500, description: "Max wheel servo pulse delta in microseconds." },
            minActiveWheelOutputUs: { type: "integer", minimum: 100, maximum: 450, description: "Deadband overcome pulse threshold in microseconds." },
          },
        },
        execute: async (tuningUpdates = {}) => {
          const currentTuning = stateRef.current.balanceTuning || {};
          const merged = { ...currentTuning, ...tuningUpdates };
          await stateRef.current.handleBalanceTuningApply(merged);
          return {
            success: true,
            tuning: merged,
            message: "PID tuning parameters successfully transmitted to ESP32.",
          };
        },
      },
      {
        name: "talos_set_fall_limit",
        description: "Configure maximum tilt safety limit. Exceeding this pitch angle triggers auto-shutdown to protect motors.",
        inputSchema: {
          type: "object",
          properties: {
            limitDeg: {
              type: "integer",
              enum: [30, 60, 80],
              description: "Fall detection angle cutoff in degrees (30, 60, or 80).",
            },
          },
          required: ["limitDeg"],
        },
        execute: async ({ limitDeg }) => {
          const validLimit = [30, 60, 80].includes(Number(limitDeg)) ? Number(limitDeg) : 60;
          await stateRef.current.handleFallAngleLimitChange(validLimit);
          return {
            success: true,
            fallAngleLimitDeg: validLimit,
            message: `Fall-angle safety limit set to ${validLimit} degrees.`,
          };
        },
      },
      {
        name: "talos_set_mode",
        description: "Switch operational mode of the robot.",
        inputSchema: {
          type: "object",
          properties: {
            mode: {
              type: "string",
              enum: ["manual", "balance-only", "camera-assist"],
              description: "Operational mode.",
            },
          },
          required: ["mode"],
        },
        execute: async ({ mode: newMode }) => {
          await stateRef.current.handleModeChange(newMode);
          return {
            success: true,
            mode: newMode,
            message: `Robot operational mode switched to '${newMode}'.`,
          };
        },
      },
      {
        name: "talos_get_camera_feed",
        description: "Retrieve current ESP32-CAM video stream endpoint URL, relay status, and camera connection state.",
        inputSchema: {
          type: "object",
          properties: {},
        },
        execute: async () => {
          const streamUrl = stateRef.current.cameraUrl || "";
          return {
            streamUrl,
            online: Boolean(streamUrl),
            format: "MJPEG",
            resolution: "VGA (640x480)",
            notes: "Direct AI-Thinker OV2640 HTTP stream via relay on port 4174 or direct IP.",
          };
        },
      },
    ];

    // Register all tools into the WebMCP registry
    robotTools.forEach((tool) => webModelContext.registerTool(tool));

    setTools(webModelContext.getTools());
    setIsReady(true);

    // Subscribe to tool execution events for reactive UI display
    const unsubscribe = webModelContext.subscribe((event, data) => {
      if (event === "executed") {
        setExecutionLogs((prev) => [data, ...prev].slice(0, 20));
      }
      setTools(webModelContext.getTools());
    });

    return () => {
      unsubscribe();
      robotTools.forEach((t) => webModelContext.unregisterTool(t.name));
    };
  }, []);

  return {
    isReady,
    tools,
    executionLogs,
    executeTool: (name, args) => webModelContext.executeTool(name, args),
  };
}
