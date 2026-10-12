import { lazy, Suspense } from "react";
import { ShieldCheck } from "lucide-react";
import PoseFeedControls from "./PoseFeedControls";

const RobotPoseModel = lazy(() => import("./RobotPoseModel"));

export default function RobotPosePanel({ status, active, poseFeed }) {
  const calibrated = status.jointSupported && status.extendedHeightCaptured;
  const available = status.online && status.visualLegPoseAvailable;
  const diagnostic = available && status.visualLegPoseDiagnostic;
  return <section className="panel pose-panel" aria-labelledby="pose-heading">
    <div className="panel-heading"><div><span className="section-kicker">Encoder / 3D pose</span><h2 id="pose-heading">Your robot. In motion.</h2><p className="panel-sub">Independent encoder feedback, calculated on this device.</p></div></div>
    {poseFeed ? <PoseFeedControls feed={poseFeed} /> : null}
    {active ? <Suspense fallback={<div className="pose-model" role="status">Loading 3D pose…</div>}><RobotPoseModel status={status} /></Suspense> : null}
    <div className="pose-readouts">
      {["left", "right"].map((side) => <div key={side}><span>{side === "left" ? "Left" : "Right"} {diagnostic ? "preview (untrusted)" : "measured bend"}</span><strong>{available ? `${Number(status[`${side}BendDeg`]).toFixed(1)}°` : "—"}</strong><small>{status.poseTransport === "usb" ? "Cloud target" : "Target"} {calibrated && Number.isFinite(Number(status[`${side}BendTargetDeg`])) ? `${Number(status[`${side}BendTargetDeg`]).toFixed(1)}°` : "—"}</small><small>Raw {status.online && Number.isFinite(Number(status[`${side}EncoderDeg`])) ? `${Number(status[`${side}EncoderDeg`]).toFixed(1)}°` : "—"}</small></div>)}
    </div>
    <p className="pose-source-note">{!status.online ? "Stale / offline: reference or held pose. Reconnect to receive new measurements." : diagnostic ? "Untrusted preview only: raw encoder changes move each leg independently. Weak magnetic fields or missing controller references prevent accuracy claims and powered joint tests." : !status.encodersHealthy ? "Encoder warning: powered joint control remains blocked. Use the optional diagnostic preview to inspect manual changes with servo power OFF." : !calibrated ? "Live IMU attitude. Set a visual extended reference for manual preview, or commission the controller reference after correcting the magnets." : "Measured left/right hip bend and IMU attitude. Display interpolation never commands the robot."}</p>
    <p className="pose-geometry-note">All 13 original Assem1 CAD parts retained. Linkage pivots and extended-angle registration are inferred from the export and need physical alignment checks. Tires are illustrative; wheel spin is not measured.</p>
    <div className="guide-footnote"><ShieldCheck size={16} aria-hidden="true" /><span>Keep the chassis securely supported during angle tests.</span></div>
  </section>;
}
