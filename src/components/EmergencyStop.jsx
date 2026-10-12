import { OctagonAlert, Power, ShieldAlert } from "lucide-react";
import { useState } from "react";

export default function EmergencyStop({ active, onStop, onDisarm }) {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  async function triggerStop() {
    setBusy(true);
    setError("");
    try {
      await onStop();
    } catch (stopError) {
      setError(stopError?.message || "Emergency stop request failed.");
    } finally {
      setBusy(false);
    }
  }

  async function disarm() {
    setError("");
    try {
      await onDisarm();
    } catch (stopError) {
      setError(stopError?.message || "Balance-off request failed.");
    }
  }

  return (
    <section
      className={`estop-panel ${active ? "is-active" : ""}`}
      aria-labelledby="estop-heading"
    >
      <span className="estop-symbol" aria-hidden="true">
        <ShieldAlert size={21} />
      </span>
      <div className="estop-copy">
        <h2 id="estop-heading">Emergency stop</h2>
        <p>
          Stops wheel and leg outputs. Keep your physical power cutoff nearby.
        </p>
        {error ? <span className="estop-error">{error}</span> : null}
      </div>
      <div className="estop-state">
        {onDisarm ? <button className="quick-disarm-button" type="button" onClick={disarm} disabled={active}><Power size={16} aria-hidden="true" /><span>Balance off</span></button> : null}
        <button
          className="estop-btn"
          type="button"
          onClick={triggerStop}
          disabled={busy}
          title="Immediately stop wheel and leg outputs"
        >
          <OctagonAlert size={19} aria-hidden="true" />
          {busy ? "Sending…" : active ? "STOP ACTIVE" : "STOP NOW"}
        </button>
      </div>
    </section>
  );
}
