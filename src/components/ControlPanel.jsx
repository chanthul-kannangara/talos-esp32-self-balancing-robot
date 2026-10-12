import {
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  ArrowUp,
  CircleStop,
  Power,
  ShieldCheck,
} from "lucide-react";

const movementControls = [
  { command: "forward", label: "Forward", icon: ArrowUp, className: "control-button--forward" },
  { command: "left", label: "Left", icon: ArrowLeft, className: "control-button--left" },
  { command: "stop", label: "Stop", icon: CircleStop, className: "control-button--stop" },
  { command: "right", label: "Right", icon: ArrowRight, className: "control-button--right" },
  { command: "backward", label: "Backward", icon: ArrowDown, className: "control-button--backward" },
];

export default function ControlPanel({
  armed,
  currentCommand,
  emergencyStopActive,
  onArmedChange,
  onCommand,
  onStopDisarm,
}) {
  return (
    <section className="panel control-panel" aria-labelledby="movement-heading">
      <div className="section-title-row">
        <div>
          <p className="eyebrow" id="movement-heading">
            <span className="eyebrow-dot" />
            Movement
          </p>
          <h2>Drive controls</h2>
        </div>
        <span className={`command-badge command-badge--${currentCommand}`}>
          {formatCommand(currentCommand)}
        </span>
      </div>

      <div className="dpad-wrap">
        <div className="movement-grid" aria-label="Robot movement controls">
          {movementControls.map(({ command, label, icon: Icon, className }) => (
            <button
              className={`control-button ${className}`}
              type="button"
              key={command}
              aria-pressed={currentCommand === command}
              onClick={() => onCommand(command)}
              disabled={emergencyStopActive}
              title={`${label} command`}
            >
              <Icon size={18} aria-hidden="true" strokeWidth={2} />
              <span>{label}</span>
            </button>
          ))}
        </div>
      </div>

      <div className="arm-controls" aria-label="Balance controller state">
        <button
          className={armed ? "arm-button is-armed" : "arm-button"}
          type="button"
          onClick={() => onArmedChange(true)}
          disabled={armed || emergencyStopActive}
          title="Arm balance controller while the robot is upright"
        >
          <ShieldCheck size={16} aria-hidden="true" strokeWidth={2} />
          <span>{armed ? "Balance on" : "Turn balance on"}</span>
        </button>
        <button
          className="disarm-button"
          type="button"
          onClick={onStopDisarm}
          disabled={emergencyStopActive}
          title="Immediately stop wheel and leg outputs"
        >
          <Power size={16} aria-hidden="true" strokeWidth={2} />
          <span>Turn balance off</span>
        </button>
      </div>
      <p className="instant-stop-note">
        Balance off immediately neutralizes wheel and leg outputs.
      </p>
    </section>
  );
}

function formatCommand(command) {
  return command.charAt(0).toUpperCase() + command.slice(1);
}
