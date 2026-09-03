import { Activity, Camera, Gamepad2 } from "lucide-react";

const modes = [
  { value: "manual", label: "Manual", icon: Gamepad2 },
  { value: "balance-only", label: "Balance only", icon: Activity },
  { value: "camera-assist", label: "Camera assist", icon: Camera },
];

export default function ModeSelector({ mode, onModeChange }) {
  return (
    <div className="mode-block" aria-labelledby="mode-heading">
      <p className="eyebrow" id="mode-heading">
        <span className="eyebrow-dot eyebrow-dot--amber" />
        Control mode
      </p>

      <div className="mode-control" role="radiogroup" aria-label="Control mode">
        {modes.map(({ value, label, icon: Icon }) => (
          <button
            className={mode === value ? "seg-btn active" : "seg-btn"}
            type="button"
            role="radio"
            aria-checked={mode === value}
            key={value}
            onClick={() => onModeChange(value)}
            title={label}
          >
            <Icon size={15} aria-hidden="true" strokeWidth={2} />
            <span>{label}</span>
          </button>
        ))}
      </div>
    </div>
  );
}
