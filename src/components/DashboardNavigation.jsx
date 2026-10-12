import { Activity, Gamepad2, MoveVertical, Radio, SlidersHorizontal } from "lucide-react";

const sections = [
  { id: "control", label: "Control", compactLabel: "Control", icon: Gamepad2 },
  { id: "posture", label: "Posture", compactLabel: "Posture", icon: MoveVertical },
  { id: "telemetry", label: "Telemetry", compactLabel: "Status", icon: Activity },
  { id: "connections", label: "Connections", compactLabel: "Connect", icon: Radio },
  { id: "tuning", label: "Tuning", compactLabel: "Tuning", icon: SlidersHorizontal },
];

export default function DashboardNavigation({ activeSection, onSectionChange }) {
  const activeIndex = sections.findIndex((section) => section.id === activeSection);

  function handleKeyDown(event, index) {
    const nextIndexes = {
      ArrowRight: (index + 1) % sections.length,
      ArrowLeft: (index + sections.length - 1) % sections.length,
      Home: 0,
      End: sections.length - 1,
    };
    if (!Object.prototype.hasOwnProperty.call(nextIndexes, event.key)) return;
    event.preventDefault();
    const nextIndex = nextIndexes[event.key];
    onSectionChange(sections[nextIndex].id);
    event.currentTarget.parentElement.querySelectorAll('[role="tab"]')[nextIndex].focus();
  }

  return (
    <nav className="workspace-navigation" aria-label="Robot workspace">
      <div className="workspace-tabs" role="tablist" aria-label="Workspace views" style={{ "--active-tab": activeIndex }}>
        {sections.map(({ id, label, compactLabel, icon: Icon }, index) => (
          <button
            type="button" role="tab" id={`tab-${id}`} key={id}
            aria-label={label} aria-selected={activeSection === id}
            aria-controls={`view-${id}`} tabIndex={activeSection === id ? 0 : -1}
            className={activeSection === id ? "workspace-tab is-active" : "workspace-tab"}
            onClick={() => onSectionChange(id)} onKeyDown={(event) => handleKeyDown(event, index)}
          >
            <Icon size={18} aria-hidden="true" strokeWidth={1.8} />
            <span className="tab-label">{label}</span><span className="tab-label--compact">{compactLabel}</span>
          </button>
        ))}
      </div>
      <span className="workspace-navigation__note">Your robot, your workspace</span>
    </nav>
  );
}
