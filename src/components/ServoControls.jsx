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
  onJointCommandApply,
  onStopDisarm,
}) {
  const [draftPosition, setDraftPosition] = useState(Number(legPosition));
  const [draftSpeed, setDraftSpeed] = useState(Number(legSpeed));
  const [dirty, setDirty] = useState(false);
  const [busyAction, setBusyAction] = useState("");
  const [feedback, setFeedback] = useState("");
  const [error, setError] = useState("");
  const [leftBend, setLeftBend] = useState(0);
  const [rightBend, setRightBend] = useState(0);
  const [jointSpeed, setJointSpeed] = useState(2);
  const [linked, setLinked] = useState(true);

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
      !status.benchMode &&
      !status.emergencyStopActive,
  );
  const jointReady = Boolean(status.online && status.jointSupported &&
    status.extendedHeightCaptured && status.encodersHealthy && status.benchMode &&
    status.controlOwner === "dashboard" && !status.balancingEnabled && !status.emergencyStopActive);
  const leftLimit = Math.max(0, Number(status.leftBendLimitDeg || 0));
  const rightLimit = Math.max(0, Number(status.rightBendLimitDeg || 0));
  const jointDraftValid = leftBend >= 0 && leftBend <= leftLimit &&
    rightBend >= 0 && rightBend <= rightLimit && jointSpeed >= 1 && jointSpeed <= 15;
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

  function enableBenchHold() {
    if (!window.confirm("Support the chassis securely with both wheels clear of the ground. Keep hands and wires clear of the linkages. Enable leg-only hold? Wheels stay OFF.")) return;
    runAction("bench-hold", () => onPostureCalibration("bench-hold"),
      "Leg-only hold requested. Wait for the ESP32 acknowledgement before applying angles.");
  }

  function changeBend(side, value) {
    const next = Number(value);
    if (linked) {
      setLeftBend(Math.min(next, leftLimit, rightLimit));
      setRightBend(Math.min(next, leftLimit, rightLimit));
    } else if (side === "left") setLeftBend(next);
    else setRightBend(next);
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
          {status.online ? "Live estimate" : "Last reported height"}
        </span>
        <strong>{status.heightCalibrationValid && Number.isFinite(liveHeightCm) && liveHeightCm > 0 ? `${liveHeightCm.toFixed(1)} cm` : "Uncalibrated"}</strong>
        <small>
          Holding {status.heightCalibrationValid && Number.isFinite(reportedTargetHeightCm) && reportedTargetHeightCm > 0
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
        <small>{status.heightCalibrationValid ? "Height-scheduled controller limits" : "Base limits · height calibration missing"}</small>
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
        {!status.online
          ? "Connect TALOS to receive height feedback and control its posture."
          : status.benchMode ? "Leg-only testing active. Stop hold before using balanced height control."
          : status.heightCalibrationValid
          ? status.standingEnabled
            ? `${formatCommandState(status.heightCommandState)} · ${status.heightCommandMessage || "Ready for a fresh height request."}`
            : "Turn Balance ON and let leg hold engage before moving height."
          : "Calibrate both mechanical endpoints before using height control."}
      </p>

      <section className="joint-controls" aria-labelledby="joint-heading">
        <div className="joint-controls__heading">
          <div><p className="eyebrow">Supported commissioning</p><h3 id="joint-heading">Precise leg angles</h3></div>
          <span className="joint-mode-badge">Wheels OFF</span>
        </div>
        <p>0° is the captured fully extended position. Commands move only after Apply; the 3D view follows measured encoders, not these sliders.</p>
        <div className="joint-actions">
          <button type="button" onClick={enableBenchHold} disabled={calibrationLocked || !status.jointSupported || !status.extendedHeightCaptured || Boolean(busyAction)}>Enable leg-only hold</button>
          <button type="button" onClick={() => runAction("stop-hold", onStopDisarm, "Stop requested for all four servos.")} disabled={!status.online || Boolean(busyAction)}>Stop hold</button>
        </div>
        <label className="joint-link"><input type="checkbox" checked={linked} onChange={(event) => {
          setLinked(event.target.checked);
          if (event.target.checked) {
            const common = Math.min(leftBend, leftLimit, rightLimit);
            setLeftBend(common);
            setRightBend(common);
          }
        }} />Move both legs together</label>
        <div className="joint-fields">
          {[["left", "Left", leftBend, leftLimit], ["right", "Right", rightBend, rightLimit]].map(([side, label, bend, limit]) => (
            <div key={side} className="joint-field">
              <label htmlFor={`${side}-bend`}>{label} bend <span>Max {limit.toFixed(1)}°</span></label>
              <div><input id={`${side}-bend`} type="number" min="0" max={limit} step="0.5" value={bend} onChange={(event) => changeBend(side, event.target.value)} /><span>°</span></div>
              <input aria-label={`${label} bend slider`} type="range" min="0" max={limit || 8} step="0.5" value={bend} disabled={!status.extendedHeightCaptured} onChange={(event) => changeBend(side, event.target.value)} />
              <small>Measured {status.extendedHeightCaptured ? `${Number(status[`${side}BendDeg`]).toFixed(1)}°` : "—"}</small>
            </div>
          ))}
        </div>
        <label className="joint-speed" htmlFor="joint-speed">Joint speed <span><input id="joint-speed" type="number" min="1" max="15" step="1" value={jointSpeed} onChange={(event) => setJointSpeed(Number(event.target.value))} />°/s</span></label>
        <button className="height-apply-button" type="button" disabled={!jointReady || !jointDraftValid || Boolean(busyAction)} onClick={() => runAction("joint", () => onJointCommandApply(leftBend, rightBend, jointSpeed), "Angle request sent. Check measured angles and device acknowledgement.")}>Apply leg angles</button>
        <p className="joint-feedback" role="status">{!status.online ? "Connect TALOS to use joint control." : !status.jointSupported ? "Upload V17 firmware to enable angle controls." : status.controlOwner === "serial" ? "Serial owns control. Send remote on COM8 to hand control to the website." : !status.extendedHeightCaptured ? "Capture the fully extended reference first." : `${formatCommandState(status.jointCommandState)} · ${status.jointCommandMessage}`}</p>
        {status.online && status.encoderMagnetWarning ? <p className="height-feedback is-error" role="alert">{status.encoderHealthMessage} Leg movement remains blocked.</p> : null}
        {!status.heightCalibrationValid ? <small>Range limited to 8° until both mechanical endpoints are calibrated. Do not force a linkage against its stop.</small> : null}
      </section>

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
          disabled={!status.online || !status.heightCalibrationValid || status.benchMode}
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
