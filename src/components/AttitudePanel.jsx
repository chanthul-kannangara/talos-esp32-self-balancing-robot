import { Activity, Cpu, Gauge, ShieldCheck, TriangleAlert } from "lucide-react";

export default function AttitudePanel({ armed, status }) {
  const angle = Number(status.tilt || 0);
  const gyro = Number(status.gyroY || 0);
  const pidOutput = Number(status.pidOutput || 0);
  const loopTimeUs = Number(status.loopTimeUs || 0);
  const controlRate = loopTimeUs > 0 ? Math.round(1000000 / loopTimeUs) : 0;
  const clampedAngle = Math.max(-25, Math.min(25, angle));
  const stateLabel = status.online ? formatState(status.state) : "Offline";
  const isHealthy = status.online && Math.abs(angle) < 6 && Number(status.imuErrors || 0) === 0;

  return (
    <section className="panel attitude-panel" aria-labelledby="attitude-heading">
      <div className="panel-heading attitude-panel__header">
        <div>
          <span className="section-kicker">Attitude</span>
          <h2 id="attitude-heading">Balance state</h2>
        </div>
        <span className={`system-state system-state--${isHealthy ? "nominal" : status.online ? "warning" : "offline"}`}>
          {isHealthy ? <ShieldCheck size={15} aria-hidden="true" /> : <TriangleAlert size={15} aria-hidden="true" />}
          {stateLabel}
        </span>
      </div>

      <div className="attitude-panel__body">
        <div className="attitude-instrument" aria-label={`Robot pitch ${angle.toFixed(1)} degrees`}>
          <div
            className="attitude-horizon"
            style={{
              "--tilt-angle": `${clampedAngle}deg`,
              "--pitch-shift": `${clampedAngle * 1.6}px`,
            }}
            aria-hidden="true"
          >
            <div className="attitude-horizon__field">
              <span className="attitude-horizon__line" />
            </div>
          </div>
          <span className="attitude-crosshair" aria-hidden="true" />
          <div className="pitch-readout" aria-live="polite">
            <span>Pitch</span>
            <strong>{angle.toFixed(1)} deg</strong>
          </div>
        </div>

        <div className="attitude-metrics">
          <Metric icon={Gauge} label="Gyro rate" value={`${gyro.toFixed(1)} deg/s`} />
          <Metric icon={Activity} label="PID output" value={`${pidOutput.toFixed(0)} us`} />
          <Metric icon={Cpu} label="Control loop" value={controlRate ? `${controlRate} Hz` : "--"} />
          <div className={`balance-lock ${armed ? "is-armed" : ""}`}>
            <span className="balance-lock__indicator" aria-hidden="true" />
            <span>
              <small>Balance lock</small>
              <strong>{armed ? "Armed" : "Safe"}</strong>
            </span>
          </div>
        </div>
      </div>
    </section>
  );
}

function Metric({ icon: Icon, label, value }) {
  return (
    <div className="attitude-metric">
      <Icon size={16} aria-hidden="true" strokeWidth={2} />
      <span>
        <small>{label}</small>
        <strong>{value}</strong>
      </span>
    </div>
  );
}

function formatState(value = "offline") {
  return value
    .replaceAll("_", " ")
    .replaceAll("-", " ")
    .replace(/\b\w/g, (letter) => letter.toUpperCase());
}
