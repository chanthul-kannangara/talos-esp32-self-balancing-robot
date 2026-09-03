import { LogOut, Shield, ShieldAlert, Wifi } from "lucide-react";
import ThemeToggle from "./ThemeToggle";

export default function Header({
  armed,
  emergencyStopActive,
  onSignOut,
  onThemeToggle,
  online,
  theme,
  user,
}) {
  const safetyClass = emergencyStopActive
    ? "safety-pill is-emergency"
    : armed
      ? "safety-pill is-armed"
      : "safety-pill";

  return (
    <header className="topbar">
      <div className="brand">
        <div className="brand__mark" aria-hidden="true">
          T
        </div>
        <div className="brand__text">
          <h1>TALOS ONE</h1>
          <p>Robot control</p>
        </div>
      </div>

      <div className="topbar__actions">
        <div className={safetyClass}>
          {emergencyStopActive ? (
            <ShieldAlert size={14} aria-hidden="true" strokeWidth={2} />
          ) : (
            <Shield size={14} aria-hidden="true" strokeWidth={2} />
          )}
          <span>
            {emergencyStopActive
              ? "E-stop"
              : armed
                ? "Armed"
                : "Safe"}
          </span>
        </div>
        <div className={online ? "connection-pill is-online" : "connection-pill"}>
          <Wifi size={14} aria-hidden="true" strokeWidth={2} />
          <span>{online ? "Online" : "Offline"}</span>
        </div>
        {user ? (
          <div className="user-pill" title={user.email || "Signed in"}>
            <span>{user.email || "Signed in"}</span>
            <button
              className="logout-button"
              type="button"
              onClick={onSignOut}
              title="Sign out"
              aria-label="Sign out"
            >
              <LogOut size={14} aria-hidden="true" strokeWidth={2} />
            </button>
          </div>
        ) : null}
        <ThemeToggle theme={theme} onToggle={onThemeToggle} />
      </div>
    </header>
  );
}
