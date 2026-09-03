import { OctagonAlert, ShieldAlert } from "lucide-react";
import { useState } from "react";

export default function EmergencyStop({ active, onStop }) {
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

  return (
    <section
      className={`estop-panel ${active ? "is-active" : ""}`}
      aria-labelledby="estop-heading"
    >
      <span className="estop-symbol" aria-hidden="true">
        <ShieldAlert size={21} />
      </span>
      <div className="estop-copy">
        <span className="section-kicker">Immediate safety action</span>
        <h2 id="estop-heading">Emergency stop</h2>
        <p>
          Immediately neutralizes wheel and leg outputs. Keep a physical motor-power cutoff
          within reach.
        </p>
        {error ? <span className="estop-error">{error}</span> : null}
      </div>
      <div className="estop-state">
        <span>{active ? "Active" : "Ready"}</span>
        <button
          className="estop-btn"
          type="button"
          onClick={triggerStop}
          disabled={busy}
          title="Immediately stop wheel and leg outputs"
        >
          <OctagonAlert size={19} aria-hidden="true" />
          {busy ? "Sending…" : "STOP NOW"}
        </button>
      </div>
    </section>
  );
}
