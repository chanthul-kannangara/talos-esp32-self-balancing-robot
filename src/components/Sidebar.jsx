import {
  Activity,
  Camera,
  Clock3,
  LayoutGrid,
  Settings,
  ShieldCheck,
} from "lucide-react";

const navItems = [
  { label: "Dashboard", icon: LayoutGrid, active: true },
  { label: "Telemetry", icon: Activity },
  { label: "Camera", icon: Camera },
  { label: "Safety", icon: ShieldCheck },
  { label: "History", icon: Clock3 },
];

export default function Sidebar() {
  return (
    <aside className="sidebar" aria-label="Dashboard sections">
      <nav className="sidebar__nav">
        {navItems.map(({ label, icon: Icon, active }) => (
          <button
            className={active ? "sidebar__button is-active" : "sidebar__button"}
            type="button"
            key={label}
            aria-label={label}
            aria-current={active ? "page" : undefined}
            title={label}
          >
            <Icon size={22} aria-hidden="true" />
          </button>
        ))}
      </nav>

      <button className="sidebar__button sidebar__settings" type="button" aria-label="Settings" title="Settings">
        <Settings size={22} aria-hidden="true" />
      </button>
    </aside>
  );
}
