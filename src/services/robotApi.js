import {
  DEFAULT_CAMERA_URL,
  initialCameraState,
  initialControlState,
  initialRobotStatus,
} from "../data/mockStatus";
import { database } from "./firebaseClient";
import { onValue, ref, serverTimestamp, set, update } from "firebase/database";

const ROBOT_ROOT_PATH = "robot";
const CONTROL_PATH = `${ROBOT_ROOT_PATH}/control`;
const CAMERA_PATH = `${ROBOT_ROOT_PATH}/camera`;
const ALLOWED_FALL_LIMITS = new Set([30, 60, 80]);
const ALLOWED_WIFI_ACTIONS = new Set([
  "setup",
  "scan",
  "reconnect",
  "remove",
  "reorder",
]);
const ALLOWED_POSTURE_ACTIONS = new Set([
  "capture-bent",
  "capture-extended",
]);

function createRequestId(prefix) {
  const id =
    typeof crypto !== "undefined" && typeof crypto.randomUUID === "function"
      ? crypto.randomUUID()
      : `${Date.now()}-${Math.random().toString(16).slice(2)}`;
  return `${prefix}-${id}`;
}

function finiteNumber(value, name) {
  const number = Number(value);
  if (!Number.isFinite(number)) {
    throw new TypeError(`${name} must be a finite number.`);
  }
  return number;
}

function normalizeBalanceTuning(tuning = {}) {
  const normalized = {
    kp: finiteNumber(tuning.kp, "Kp"),
    ki: finiteNumber(tuning.ki, "Ki"),
    kd: finiteNumber(tuning.kd, "Kd"),
    balanceTrimDeg: finiteNumber(tuning.balanceTrimDeg, "Balance trim"),
    predictionTimeMs: finiteNumber(tuning.predictionTimeMs, "Prediction time"),
    maxWheelOutputUs: Math.round(
      finiteNumber(tuning.maxWheelOutputUs, "Maximum wheel output"),
    ),
    minActiveWheelOutputUs: Math.round(
      finiteNumber(tuning.minActiveWheelOutputUs, "Minimum active wheel output"),
    ),
  };

  const rules = [
    ["Kp", normalized.kp, 20, 250],
    ["Ki", normalized.ki, 0, 20],
    ["Kd", normalized.kd, 0, 30],
    ["Balance trim", normalized.balanceTrimDeg, -5, 5],
    ["Prediction time", normalized.predictionTimeMs, 10, 80],
    ["Maximum wheel output", normalized.maxWheelOutputUs, 300, 500],
    ["Minimum active wheel output", normalized.minActiveWheelOutputUs, 100, 450],
  ];

  for (const [label, value, minimum, maximum] of rules) {
    if (value < minimum || value > maximum) {
      throw new RangeError(`${label} must be between ${minimum} and ${maximum}.`);
    }
  }

  if (normalized.minActiveWheelOutputUs >= normalized.maxWheelOutputUs) {
    throw new RangeError("Minimum active wheel output must be lower than maximum output.");
  }

  return normalized;
}

const defaultRobotState = {
  control: { ...initialControlState },
  status: { ...initialRobotStatus },
  camera: { ...initialCameraState },
};

export function normalizeCameraStreamUrl(value) {
  const url = String(value || "").trim();
  if (!url || /^wss?:\/\//i.test(url)) return url;

  try {
    const parsed = new URL(url);
    if (parsed.pathname === "/" || !parsed.pathname) {
      parsed.pathname = "/stream";
    }

    // The standard ESP32-CAM sketch hosts its MJPEG endpoint on port 81.
    // HTTPS relay URLs keep their standard port and only need /stream.
    if (parsed.protocol === "http:" && !parsed.port) {
      parsed.port = "81";
    }

    return parsed.toString();
  } catch {
    return url;
  }
}

function createRobotState(firebaseValue = {}) {
  const firebaseControl = firebaseValue.control || {};
  const firebaseStatus = firebaseValue.status || {};
  const requestedTuning = {
    ...defaultRobotState.control.tuning,
    ...(firebaseControl.tuning || {}),
  };
  const reportedTuning = {
    ...(firebaseStatus.tuning || {}),
  };

  for (const key of [
    "kp",
    "ki",
    "kd",
    "balanceTrimDeg",
    "predictionTimeMs",
    "maxWheelOutputUs",
    "minActiveWheelOutputUs",
  ]) {
    if (firebaseStatus[key] !== undefined) reportedTuning[key] = firebaseStatus[key];
  }
  const tuningReported = Object.keys(reportedTuning).length > 0;
  const reportedFallLimit = Number(firebaseStatus.fallAngleLimitDeg);
  const requestedFallLimit = Number(firebaseControl.fallAngleLimitDeg);
  const effectiveFallLimit = ALLOWED_FALL_LIMITS.has(reportedFallLimit)
    ? reportedFallLimit
    : ALLOWED_FALL_LIMITS.has(requestedFallLimit)
      ? requestedFallLimit
      : defaultRobotState.control.fallAngleLimitDeg;
  const reportedTargetExtension = Number(firebaseStatus.targetExtensionPercent);
  const reportedLegSpeed = Number(firebaseStatus.legMotionSpeedPercent);
  const effectiveLegPosition = Number.isFinite(reportedTargetExtension)
    ? Math.min(100, Math.max(0, Math.round(reportedTargetExtension)))
    : Number(firebaseControl.legPosition ?? defaultRobotState.control.legPosition);
  const effectiveLegSpeed = Number.isFinite(reportedLegSpeed)
    ? Math.min(100, Math.max(0, Math.round(reportedLegSpeed)))
    : Number(firebaseControl.legSpeed ?? defaultRobotState.control.legSpeed);

  return {
    control: {
      ...defaultRobotState.control,
      ...firebaseControl,
      legPosition: effectiveLegPosition,
      legSpeed: effectiveLegSpeed,
      tuning: requestedTuning,
    },
    status: {
      ...defaultRobotState.status,
      ...firebaseStatus,
      fallAngleLimitDeg: effectiveFallLimit,
      settingsReported: tuningReported,
      tuning: {
        ...requestedTuning,
        ...(tuningReported ? reportedTuning : {}),
      },
    },
    camera: {
      ...defaultRobotState.camera,
      ...firebaseValue.camera,
      streamUrl: normalizeCameraStreamUrl(firebaseValue.camera?.streamUrl || DEFAULT_CAMERA_URL),
    },
  };
}

export async function sendCommand(command) {
  // Firebase Realtime Database path:
  // robot/control/command = "stop" | "forward" | "backward" | "left" | "right"
  // The ESP32 reads this high-level command and runs balancing locally.
  return update(ref(database, CONTROL_PATH), {
    command,
    motionRevision: Date.now(),
    updatedAt: serverTimestamp(),
  });
}

export async function updateSpeed(speed) {
  const normalizedSpeed = Number(speed);

  return update(ref(database, CONTROL_PATH), {
    speed: normalizedSpeed,
    updatedAt: serverTimestamp(),
  });
}

export async function updateLegHeight(legPosition, legSpeed) {
  const normalizedPosition = Math.round(finiteNumber(legPosition, "Leg extension"));
  const normalizedSpeed = Math.round(finiteNumber(legSpeed, "Leg movement speed"));
  if (normalizedPosition < 0 || normalizedPosition > 100) {
    throw new RangeError("Leg extension must be between 0 and 100 percent.");
  }
  if (normalizedSpeed < 0 || normalizedSpeed > 100) {
    throw new RangeError("Leg movement speed must be between 0 and 100 percent.");
  }

  return update(ref(database, CONTROL_PATH), {
    legPosition: normalizedPosition,
    legSpeed: normalizedSpeed,
    heightCommandRevision: Date.now(),
    updatedAt: serverTimestamp(),
  });
}

export async function updateLegAssist(legAssist) {
  return update(ref(database, CONTROL_PATH), {
    legAssist: Boolean(legAssist),
    updatedAt: serverTimestamp(),
  });
}

export async function updateArmed(armed) {
  return update(ref(database, CONTROL_PATH), {
    armed: Boolean(armed),
    command: "stop",
    motionRevision: Date.now(),
    updatedAt: serverTimestamp(),
  });
}

export async function updateFallAngleLimit(fallAngleLimitDeg) {
  const limit = Math.round(finiteNumber(fallAngleLimitDeg, "Fall-angle limit"));
  if (!ALLOWED_FALL_LIMITS.has(limit)) {
    throw new RangeError("Fall-angle limit must be 30, 60, or 80 degrees.");
  }

  return update(ref(database, CONTROL_PATH), {
    fallAngleLimitDeg: limit,
    settingsRevision: Date.now(),
    updatedAt: serverTimestamp(),
  });
}

export async function applyBalanceTuning(tuning) {
  const normalized = normalizeBalanceTuning(tuning);

  return update(ref(database, CONTROL_PATH), {
    tuning: normalized,
    settingsRevision: Date.now(),
    updatedAt: serverTimestamp(),
  });
}

export async function stopAndDisarm() {
  return update(ref(database, CONTROL_PATH), {
    armed: false,
    command: "stop",
    motionRevision: Date.now(),
    updatedAt: serverTimestamp(),
  });
}

export async function triggerEmergencyStop() {
  return update(ref(database, CONTROL_PATH), {
    armed: false,
    command: "stop",
    motionRevision: Date.now(),
    emergencyStopNonce: Date.now(),
    updatedAt: serverTimestamp(),
  });
}

export async function requestWifiAction(action, options = {}) {
  if (!ALLOWED_WIFI_ACTIONS.has(action)) {
    throw new RangeError("Unsupported Wi-Fi action.");
  }

  const ssid = String(options.ssid || "").trim();
  if (!["setup", "scan"].includes(action) && !ssid) {
    throw new TypeError("A saved network name is required.");
  }

  const wifiCommand = {
    id: createRequestId("wifi"),
    action,
  };
  if (ssid) wifiCommand.ssid = ssid.slice(0, 32);
  if (action === "reorder") {
    const direction = Number(options.direction);
    if (direction !== -1 && direction !== 1) {
      throw new RangeError("Wi-Fi reorder direction must be -1 or 1.");
    }
    wifiCommand.direction = direction;
  }

  return update(ref(database, CONTROL_PATH), {
    wifiCommand,
    updatedAt: serverTimestamp(),
  });
}

export async function requestPostureCalibration(action) {
  if (!ALLOWED_POSTURE_ACTIONS.has(action)) {
    throw new RangeError("Unsupported posture calibration action.");
  }

  return update(ref(database, CONTROL_PATH), {
    postureCommand: {
      id: createRequestId("posture"),
      action,
    },
    updatedAt: serverTimestamp(),
  });
}

export async function updateMode(mode) {
  return update(ref(database, CONTROL_PATH), {
    mode,
    updatedAt: serverTimestamp(),
  });
}

export function listenToRobotStatus(callback) {
  if (typeof callback !== "function") {
    return createRobotState();
  }

  return onValue(ref(database, ROBOT_ROOT_PATH), (snapshot) => {
    callback(createRobotState(snapshot.val() || {}));
  });
}

export async function saveCameraUrl(url) {
  const streamUrl = normalizeCameraStreamUrl(url) || DEFAULT_CAMERA_URL;

  return update(ref(database, CAMERA_PATH), {
    streamUrl,
    online: Boolean(streamUrl),
  });
}

export async function seedRobotDefaults() {
  return set(ref(database, ROBOT_ROOT_PATH), createRobotState());
}
