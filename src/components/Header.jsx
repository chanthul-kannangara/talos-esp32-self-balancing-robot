import { Bot, ChevronDown, LogOut, Shield, ShieldAlert, UserRound, Wifi, WifiOff } from "lucide-react";
import { useEffect, useRef } from "react";
import ThemeToggle from "./ThemeToggle";

export default function Header({ armed, emergencyStopActive, onOpenWebMcp, onSignOut, onThemeToggle, online, theme, toolCount, user }) {
  const accountRef = useRef(null);

  useEffect(() => {
    function closeOutside(event) {
      if (accountRef.current && !accountRef.current.contains(event.target)) accountRef.current.open = false;
    }
    function closeOnEscape(event) {
      if (event.key === "Escape" && accountRef.current?.open) {
        accountRef.current.open = false;
        accountRef.current.querySelector("summary").focus();
      }
    }
    document.addEventListener("pointerdown", closeOutside);
    document.addEventListener("keydown", closeOnEscape);
    return () => {
      document.removeEventListener("pointerdown", closeOutside);
      document.removeEventListener("keydown", closeOnEscape);
    };
  }, []);

  return (
    <header className="topbar">
      <div className="brand"><div className="brand__mark" aria-hidden="true">T<span /></div><div className="brand__text"><h1>TALOS ONE</h1><p>Robot workspace</p></div></div>
      <div className="topbar__actions">
        <div className={`safety-pill ${emergencyStopActive ? "is-emergency" : armed ? "is-armed" : ""}`}>
          {emergencyStopActive ? <ShieldAlert size={14} aria-hidden="true" /> : <Shield size={14} aria-hidden="true" />}
          <span>{emergencyStopActive ? "E-stop active" : armed ? "Balance on" : "Disarmed"}</span>
        </div>
        <div className={online ? "connection-pill is-online" : "connection-pill"} role="status" aria-label={online ? "Robot connected" : "Robot offline"} title={online ? "Robot connected" : "Robot offline"}>{online ? <Wifi size={15} aria-hidden="true" /> : <WifiOff size={15} aria-hidden="true" />}<span>{online ? "Connected" : "Offline"}</span></div>
        <span className="topbar-divider" aria-hidden="true" />
        <ThemeToggle theme={theme} onToggle={onThemeToggle} />
        <details className="account-menu" ref={accountRef}>
          <summary aria-label="Account and agent tools"><UserRound size={17} aria-hidden="true" /><ChevronDown size={12} aria-hidden="true" /></summary>
          <div className="account-popover">
            <div className="account-popover__identity"><span>Signed in as</span><strong>{user?.email || "TALOS operator"}</strong></div>
            <button type="button" onClick={() => { accountRef.current.open = false; accountRef.current.querySelector("summary").focus(); onOpenWebMcp(); }}><Bot size={17} aria-hidden="true" /><span>Agent console<small>WebMCP · {toolCount || 11} tools</small></span><span className="account-key">AI</span></button>
            <button type="button" onClick={onSignOut}><LogOut size={17} aria-hidden="true" /><span>Sign out</span></button>
          </div>
        </details>
      </div>
    </header>
  );
}
