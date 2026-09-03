import {
  Accessibility,
  Activity,
  Gauge,
  MoveVertical,
  Ruler,
} from "lucide-react";
import { useEffect, useMemo, useState } from "react";

const MIN_HEIGHT_CM = 16.2;
const MAX_HEIGHT_CM = 29.2;

function heightForExtension(extensionPercent) {
  return MIN_HEIGHT_CM +
    (MAX_HEIGHT_CM - MIN_HEIGHT_CM) * (extensionPercent / 100);
}

function formatCommandState(value) {
  return String(value || "idle")
    .replaceAll("_", "-")
    .split("-")
    .filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(" ");
}

export default function ServoControls({
  legAssist,
  legPosition,
  legSpeed,
  status,
  onHeightCommandApply,
  onLegAssistChange,
  onPostureCalibration,
}) {
  const [draftPosition, setDraftPosition] = useState(Number(legPosition));
  const [draftSpeed, setDraftSpeed] = useState(Number(legSpeed));
  const [dirty, setDirty] = useState(false);
  const [busyAction, setBusyAction] = useState("");
  const [feedback, setFeedback] = useState("");
  const [error, setError] = useState("");

  useEffect(() => {
    if (!dirty) {
      setDraftPosition(Number(legPosition));
      setDraftSpeed(Number(legSpeed));
    }
  }, [dirty, legPosition, legSpeed]);

  const targetHeightCm = useMemo(
    () => heightForExtension(draftPosition),
    [draftPosition],
  );
  const liveHeightCm = Number(status.heightCm);
  const reportedTargetHeightCm = Number(status.targetHeightCm);
  const heightControlReady = Boolean(
    status.online &&
      status.heightCalibrationValid &&
      status.standingEnabled &&
      !status.emergencyStopActive,
  );
  const calibrationLocked = Boolean(
    !status.online ||
      status.balancingEnabled ||
      status.standingEnabled ||
      status.armRequested,
  );

  async function runAction(name, action, successMessage) {
    setBusyAction(name);
    setError("");
    setFeedback("");
    try {
      await action();
      setFeedback(successMessage);
      return true;
    } catch (actionError) {
      setError(actionError?.message || "The request could not be sent.");
      return false;
    } finally {
      setBusyAction("");
    }
  }

  function applyHeight() {
    runAction(
      "height",
      () => onHeightCommandApply(draftPosition, draftSpeed),
      `Height request sent: ${targetHeightCm.toFixed(1)} cm at ${draftSpeed}% movement speed.`,
    ).then((succeeded) => {
      if (succeeded) setDirty(false);
    });
  }

  function captureEndpoint(action, heightCm) {
    const endpointName = action === "capture-bent" ? "fully bent" : "fully extended";
    const confirmed = window.confirm(
      `Balance must be OFF. Support TALOS at exactly ${heightCm.toFixed(1)} cm (${endpointName}) and keep it still. Capture now?`,
    );
    if (!confirmed) return;

    runAction(
      action,
      () => onPostureCalibration(action),
      `${heightCm.toFixed(1)} cm endpoint capture queued. Keep TALOS supported until the ESP32 reports Captured.`,
    );
  }

  return (
    <div className="servo-controls" aria-labelledby="posture-heading">
      <div className="servo-controls__heading">
        <div>
          <p className="eyebrow" id="posture-heading">
            <span className="eyebrow-dot eyebrow-dot--amber" />
            Coordinated posture
          </p>
          <p className="panel-sub">AS5600 closed-loop height and four-servo assist</p>
        </div>
        <Accessibility size={20} aria-hidden="true" />
      </div>

      <div className="height-readout" aria-label="Robot height feedback">
        <span>
          <Ruler size={15} aria-hidden="true" />
          Live estimate
        </span>
        <strong>{Number.isFinite(liveHeightCm) ? `${liveHeightCm.toFixed(1)} cm` : "--"}</strong>
        <small>
          Holding {Number.isFinite(reportedTargetHeightCm)
            ? `${reportedTargetHeightCm.toFixed(1)} cm`
            : "--"}
        </small>
      </div>

      <div className="height-readout">
        <span>
          <Gauge size={15} aria-hidden="true" />
          Wheel authority
        </span>
        <strong>{Number(status.wheelAuthorityPercent || 0).toFixed(0)}%</strong>
        <small>Automatically rises with body height</small>
      </div>

      <label className="range-control range-control--wide" htmlFor="leg-position">
        <span>Target body height</span>
        <strong>{targetHeightCm.toFixed(1)} cm · {draftPosition}% extended</strong>
      </label>
      <input
        className="native-range native-range--wide"
        id="leg-position"
        type="range"
        min="0"
        max="100"
        step="1"
        value={draftPosition}
        onChange={(event) => {
          setDraftPosition(Number(event.target.value));
          setDirty(true);
        }}
        style={{ "--range-percent": `${draftPosition}%` }}
      />
      <div className="height-endpoints" aria-hidden="true">
        <span>Bent · 16.2 cm</span>
        <span>Extended · 29.2 cm</span>
      </div>

      <label className="range-control range-control--wide" htmlFor="leg-speed">
        <span>Height movement speed</span>
        <strong>{draftSpeed}%</strong>
      </label>
      <input
        className="native-range native-range--wide"
        id="leg-speed"
        type="range"
        min="0"
        max="100"
        step="1"
        value={draftSpeed}
        onChange={(event) => {
          setDraftSpeed(Number(event.target.value));
          setDirty(true);
        }}
        style={{ "--range-percent": `${draftSpeed}%` }}
      />

      <button
        className="height-apply-button"
        type="button"
        onClick={applyHeight}
        disabled={!heightControlReady || Boolean(busyAction)}
      >
        <MoveVertical size={17} aria-hidden="true" />
        {busyAction === "height" ? "Sending height…" : "Apply coordinated height"}
      </button>

      <p className={`height-control-state ${heightControlReady ? "is-ready" : ""}`}>
        {status.heightCalibrationValid
          ? status.standingEnabled
            ? `${formatCommandState(status.heightCommandState)} · ${status.heightCommandMessage || "Ready for a fresh height request."}`
            : "Turn Balance ON and let leg hold engage before moving height."
          : "Calibrate both mechanical endpoints before using height control."}
      </p>

      <label className="assist-toggle" htmlFor="leg-assist">
        <span className="assist-toggle__copy">
          <Activity size={17} aria-hidden="true" />
          <span>
            <strong>Dynamic four-servo assist</strong>
            <small>Bounded recovery crouch plus roll-rate leveling</small>
          </span>
        </span>
        <input
          id="leg-assist"
          type="checkbox"
          checked={legAssist}
          onChange={(event) => onLegAssistChange(event.target.checked)}
        />
        <span className="toggle-track" aria-hidden="true"><span /></span>
      </label>

      <details className="height-calibration">
        <summary>Endpoint calibration</summary>
        <p>
          Use only with Balance OFF and the body physically supported. The encoders map
          16.2–29.2 cm for height feedback and wheel gain scheduling.
        </p>
        <div className="height-calibration__actions">
          <button
            type="button"
            onClick={() => captureEndpoint("capture-bent", MIN_HEIGHT_CM)}
            disabled={calibrationLocked || Boolean(busyAction)}
          >
            Capture bent · 16.2 cm
          </button>
          <button
            type="button"
            onClick={() => captureEndpoint("capture-extended", MAX_HEIGHT_CM)}
            disabled={calibrationLocked || Boolean(busyAction)}
          >
            Capture extended · 29.2 cm
          </button>
        </div>
        <small>
          ESP32: {formatCommandState(status.postureCommandState)} · {status.postureCommandMessage || "No capture requested."}
        </small>
      </details>

      {error ? <p className="height-feedback is-error">{error}</p> : null}
      {feedback ? <p className="height-feedback is-success">{feedback}</p> : null}
    </div>
  );
}
