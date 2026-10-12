import { Activity, Cpu, Gauge, ShieldCheck, Radio } from "lucide-react";
import { lazy, Suspense } from "react";
import PoseFeedControls from "./PoseFeedControls";

const RobotPoseModel = lazy(() => import("./RobotPoseModel"));

export default function AttitudePanel({ status: controllerStatus, poseFeed, active = true }) {
  const status = poseFeed?.status || controllerStatus;
  const angle = Number(status.tilt || 0);
  const loopTimeUs = Number(status.loopTimeUs || 0);
  const controlRate = Number(status.measuredControlHz) > 0 ? Math.round(Number(status.measuredControlHz)) :
    loopTimeUs > 0 ? Math.round(1000000 / loopTimeUs) : 0;
  const imuHealthy = typeof status.imuSampleHealthy === "boolean" ? status.imuSampleHealthy : Number(status.imuErrors || 0) === 0;
  const isHealthy = status.online && Math.abs(angle) < 6 && imuHealthy;
  const controllerActive = status.online && status.balancingEnabled;

  return (
    <section className="panel attitude-panel" aria-labelledby="attitude-heading">
      <div className="panel-heading attitude-panel__header">
        <div><span className="section-kicker">Orientation</span><h2 id="attitude-heading">Balance monitor</h2></div>
        <span className={`system-state system-state--${isHealthy ? "nominal" : status.online ? "warning" : "offline"}`}>
          {isHealthy ? <ShieldCheck size={14} aria-hidden="true" /> : <Radio size={14} aria-hidden="true" />}
          {status.online ? status.poseTransport === "usb" ? status.balancingEnabled ? "Armed" : "Disarmed" : String(status.state || "Connected").replaceAll("_", " ").replaceAll("-", " ") : "No signal"}
        </span>
      </div>
      {poseFeed ? <PoseFeedControls feed={poseFeed} /> : null}

      <div className="balance-visual" aria-label={status.online ? `Robot pitch ${angle.toFixed(1)} degrees` : "Robot orientation unavailable while offline"}>
        <div className="visual-coordinate" aria-hidden="true"><span />IMU / Pitch</div>
        <div className="visual-pitch"><span>Body tilt</span><strong>{status.online ? angle.toFixed(1) : "—"}<small>°</small></strong></div>
        {active ? <Suspense fallback={<div className="pose-model pose-model--compact" role="status">Loading CAD view…</div>}><RobotPoseModel status={status} compact /></Suspense> : null}
        <div className="visual-caption"><span>Assem1 · 13 CAD parts</span><span>{status.online && status.heightCalibrationValid && Number(status.heightCm) > 0 ? `${Number(status.heightCm).toFixed(1)} cm height` : "Height uncalibrated"}</span></div>
      </div>

      <div className="attitude-metrics">
        <Metric icon={Gauge} label="Gyro rate" value={status.online ? `${Number(status.gyroY || 0).toFixed(1)} °/s` : "—"} />
        <Metric icon={Activity} label="PID output" value={status.online ? `${Number(status.pidOutput || 0).toFixed(0)} µs` : "—"} />
        <Metric icon={Cpu} label="Control loop" value={status.online && controlRate ? `${controlRate} Hz` : "—"} />
      </div>
      <div className={`balance-status-line ${controllerActive ? "is-armed" : ""}`}><span aria-hidden="true" />{!status.online ? "Controller state unavailable" : controllerActive ? "Balance controller active" : "Balance controller off"}<small>{status.online ? "Live feedback" : "Not receiving live data"}</small></div>
    </section>
  );
}

function Metric({ icon: Icon, label, value }) {
  return <div className="attitude-metric"><Icon size={16} aria-hidden="true" strokeWidth={1.8} /><span><small>{label}</small><strong>{value}</strong></span></div>;
}
