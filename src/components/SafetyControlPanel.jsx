import {
  AlertTriangle,
  CheckCircle2,
  Gauge,
  SlidersHorizontal,
} from "lucide-react";
import { useEffect, useMemo, useState } from "react";

const CUTOFF_OPTIONS = [30, 60, 80];

const TUNING_FIELDS = [
  { key: "kp", label: "Kp", min: 20, max: 250, step: 1, unit: "" },
  { key: "ki", label: "Ki", min: 0, max: 20, step: 0.01, unit: "" },
  { key: "kd", label: "Kd", min: 0, max: 30, step: 0.1, unit: "" },
  {
    key: "balanceTrimDeg",
    label: "Balance trim",
    min: -5,
    max: 5,
    step: 0.05,
    unit: "°",
  },
  {
    key: "predictionTimeMs",
    label: "Prediction",
    min: 10,
    max: 80,
    step: 1,
    unit: "ms",
  },
  {
    key: "maxWheelOutputUs",
    label: "Wheel authority",
    min: 300,
    max: 500,
    step: 5,
    unit: "µs",
  },
  {
    key: "minActiveWheelOutputUs",
    label: "Minimum active",
    min: 100,
    max: 450,
    step: 5,
    unit: "µs",
  },
];

function formatState(value) {
  const normalized = String(value || "idle").replaceAll("_", "-");
  return normalized
    .split("-")
    .filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(" ");
}

function validateTuning(draft) {
  const next = {};
  for (const field of TUNING_FIELDS) {
    const value = Number(draft[field.key]);
    if (!Number.isFinite(value) || value < field.min || value > field.max) {
      throw new RangeError(
        `${field.label} must be between ${field.min} and ${field.max}${field.unit ? ` ${field.unit}` : ""}.`,
      );
    }
    next[field.key] = value;
  }
  next.maxWheelOutputUs = Math.round(next.maxWheelOutputUs);
  next.minActiveWheelOutputUs = Math.round(next.minActiveWheelOutputUs);
  if (next.minActiveWheelOutputUs >= next.maxWheelOutputUs) {
    throw new RangeError("Minimum active output must be lower than wheel authority.");
  }
  return next;
}

export default function SafetyControlPanel({
  balanceTuning,
  fallAngleLimitDeg,
  isBalancing,
  onApplyBalanceTuning,
  onFallAngleLimitChange,
  status,
}) {
  const [tuningDraft, setTuningDraft] = useState(balanceTuning);
  const [tuningDirty, setTuningDirty] = useState(false);
  const [busyAction, setBusyAction] = useState("");
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");

  useEffect(() => {
    if (!tuningDirty) setTuningDraft(balanceTuning);
  }, [balanceTuning, tuningDirty]);

  const appliedLimit = Number(status.fallAngleLimitDeg || fallAngleLimitDeg);
  const controlsLocked = Boolean(
    isBalancing ||
      status.standingEnabled ||
      status.armRequested,
  );
  const tuningSummary = useMemo(() => {
    if (!status.settingsReported) return "Awaiting ESP32 settings echo";
    const applied = status.tuning || {};
    return `Applied Kp ${Number(applied.kp || 0).toFixed(0)} · Kd ${Number(
      applied.kd || 0,
    ).toFixed(1)} · ${Number(applied.maxWheelOutputUs || 0).toFixed(0)} µs`;
  }, [status.settingsReported, status.tuning]);

  async function runAction(name, action, successMessage) {
    setBusyAction(name);
    setError("");
    setMessage("");
    try {
      await action();
      setMessage(successMessage);
      return true;
    } catch (actionError) {
      setError(actionError?.message || "The request could not be sent.");
      return false;
    } finally {
      setBusyAction("");
    }
  }

  function selectCutoff(limit) {
    runAction(
      "cutoff",
      () => onFallAngleLimitChange(limit),
      `${limit}° cutoff requested. The applied value is echoed by the ESP32.`,
    );
  }

  function submitTuning(event) {
    event.preventDefault();
    let normalized;
    try {
      normalized = validateTuning(tuningDraft);
    } catch (validationError) {
      setError(validationError.message);
      setMessage("");
      return;
    }
    runAction(
      "tuning",
      () => onApplyBalanceTuning(normalized),
      "Balance tuning request sent.",
    ).then((succeeded) => {
      if (succeeded) setTuningDirty(false);
    });
  }

  return (
    <section className="panel safety-control-panel" aria-labelledby="safety-control-heading">
      <div className="panel-heading">
        <div>
          <span className="section-kicker">Safety & response</span>
          <h2 id="safety-control-heading">Balance configuration</h2>
        </div>
        <span className={`config-lock-state ${controlsLocked ? "is-locked" : ""}`}>
          {controlsLocked ? "Locked while active" : "Ready to configure"}
        </span>
      </div>

      <div className="safety-config-grid safety-config-grid--single">
        <section className="config-section" aria-labelledby="cutoff-heading">
          <div className="config-section__heading">
            <span className="config-icon">
              <Gauge size={17} aria-hidden="true" />
            </span>
            <div>
              <h3 id="cutoff-heading">Fall cutoff</h3>
              <p>Measured from the captured balance zero.</p>
            </div>
          </div>

          <div className="cutoff-options" role="group" aria-label="Fall-angle cutoff">
            {CUTOFF_OPTIONS.map((limit) => (
              <button
                className={Number(fallAngleLimitDeg) === limit ? "is-selected" : ""}
                type="button"
                key={limit}
                onClick={() => selectCutoff(limit)}
                disabled={controlsLocked || Boolean(busyAction)}
                aria-pressed={Number(fallAngleLimitDeg) === limit}
              >
                {limit}°
              </button>
            ))}
          </div>

          <p className="applied-value">
            <CheckCircle2 size={14} aria-hidden="true" />
            ESP32 reported: <strong>{Number.isFinite(appliedLimit) ? appliedLimit : "--"}°</strong>
          </p>

          {Number(fallAngleLimitDeg) === 80 ? (
            <p className="config-warning" role="alert">
              <AlertTriangle size={15} aria-hidden="true" />
              80° keeps the wheels active when TALOS is almost down. Use a support stand and
              keep clear of the wheels.
            </p>
          ) : null}
        </section>

      </div>

      <details className="advanced-tuning">
        <summary>
          <span>
            <SlidersHorizontal size={17} aria-hidden="true" />
            Advanced balance tuning
          </span>
          <small>{tuningSummary}</small>
        </summary>

        <form className="tuning-form" onSubmit={submitTuning} noValidate>
          <p className="tuning-note">
            Changes are range-checked and should only be applied while TALOS is supported and
            balance is off.
          </p>
          <div className="tuning-field-grid">
            {TUNING_FIELDS.map((field) => (
              <label key={field.key}>
                <span>{field.label}</span>
                <span className="number-input">
                  <input
                    type="number"
                    min={field.min}
                    max={field.max}
                    step={field.step}
                    value={tuningDraft[field.key]}
                    onChange={(event) => {
                      setTuningDirty(true);
                      setTuningDraft((current) => ({
                        ...current,
                        [field.key]: event.target.value,
                      }));
                    }}
                    disabled={controlsLocked}
                  />
                  {field.unit ? <small>{field.unit}</small> : null}
                </span>
              </label>
            ))}
          </div>
          <button
            className="primary-apply-button"
            type="submit"
            disabled={controlsLocked || Boolean(busyAction) || !tuningDirty}
          >
            {busyAction === "tuning" ? "Applying…" : "Validate and apply tuning"}
          </button>
        </form>
      </details>

      {error ? <p className="config-feedback is-error">{error}</p> : null}
      {message ? <p className="config-feedback is-success">{message}</p> : null}
      {status.settingsAck &&
      !["none", "idle"].includes(String(status.settingsAck).toLowerCase()) ? (
        <p className="settings-ack">
          ESP32 settings: {formatState(status.settingsAck)} · revision{" "}
          {status.settingsAckRevision || "--"}
        </p>
      ) : null}
    </section>
  );
}
