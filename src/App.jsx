import { lazy, Suspense, useCallback, useState } from "react";
import { ArrowUpRight, Cable, CircleHelp, MoveVertical, ShieldCheck, SlidersHorizontal } from "lucide-react";
import ActivityLog from "./components/ActivityLog";
import AttitudePanel from "./components/AttitudePanel";
import ControlPanel from "./components/ControlPanel";
import DashboardNavigation from "./components/DashboardNavigation";
import EmergencyStop from "./components/EmergencyStop";
import Header from "./components/Header";
import LoginPage from "./components/LoginPage";
import ModeSelector from "./components/ModeSelector";
import SafetyControlPanel from "./components/SafetyControlPanel";
import ServoControls from "./components/ServoControls";
import RobotPosePanel from "./components/RobotPosePanel";
import SpeedSlider from "./components/SpeedSlider";
import StatusGrid from "./components/StatusGrid";
import WifiSetupPanel from "./components/WifiSetupPanel";
import { useAuthSession } from "./hooks/useAuthSession";
import { useRobotDashboard } from "./hooks/useRobotDashboard";
import { usePoseFeed } from "./hooks/usePoseFeed";
import { useThemePreference } from "./hooks/useThemePreference";
import { useWebMCP } from "./hooks/useWebMCP";

const CameraPanel = lazy(() => import("./components/CameraPanel"));
const WebMcpConsole = lazy(() => import("./components/WebMcpConsole"));

export default function App() {
  const { handleThemeToggle, theme } = useThemePreference();
  const { authLoading, authUser, handleSignOut } = useAuthSession();
  const showDashboardPreview = import.meta.env.DEV &&
    typeof window !== "undefined" &&
    new URLSearchParams(window.location.search).has("preview-dashboard");

  if (authLoading) {
    return (
      <main className="auth-shell">
        <section className="auth-loading panel" role="status">
          <span className="cursor-blink" aria-hidden="true" />
          Opening your workspace…
        </section>
      </main>
    );
  }

  if (!authUser && !showDashboardPreview) {
    return <LoginPage theme={theme} onThemeToggle={handleThemeToggle} />;
  }

  return (
    <DashboardApp
      authUser={authUser || { email: "local preview" }}
      handleSignOut={handleSignOut}
      handleThemeToggle={handleThemeToggle}
      theme={theme}
    />
  );
}

function DashboardApp({ authUser, handleSignOut, handleThemeToggle, theme }) {
  const [activeSection, setActiveSection] = useState("control");
  const [isWebMcpOpen, setIsWebMcpOpen] = useState(false);
  const closeWebMcp = useCallback(() => setIsWebMcpOpen(false), []);

  function openSection(section) {
    setActiveSection(section);
    window.requestAnimationFrame(() => document.getElementById(`tab-${section}`)?.focus());
  }
  const {
    activityEntries, balanceTuning, cameraUrl, currentCommand, dashboardStatus,
    fallAngleLimitDeg, handleBalanceTuningApply, handleCameraSave, handleCommand,
    handleArmedChange, handleEmergencyStop, handleFallAngleLimitChange,
    handleHeightCommandApply, handleJointCommandApply, handleLegAssistChange, handleModeChange,
    handleSpeedChange, handleStopDisarm, handlePostureCalibration, handleWifiAction,
    legAssist, legPosition, legSpeed, mode, setCameraUrl, speed, status,
  } = useRobotDashboard();
  const poseFeed = usePoseFeed(dashboardStatus);

  const webmcp = useWebMCP({
    status, dashboardStatus, currentCommand, speed, mode, legPosition, legSpeed,
    legAssist, balanceTuning, fallAngleLimitDeg, cameraUrl, handleCommand,
    handleSpeedChange, handleArmedChange, handleEmergencyStop, handleStopDisarm,
    handleHeightCommandApply, handleLegAssistChange, handleBalanceTuningApply,
    handleFallAngleLimitChange, handleModeChange, handleCameraSave,
  });

  return (
    <div className="app workspace-app">
      <a className="skip-link" href="#workspace-content">Skip to controls</a>
      <Header
        armed={dashboardStatus.balancingEnabled}
        emergencyStopActive={dashboardStatus.emergencyStopActive}
        theme={theme}
        onThemeToggle={handleThemeToggle}
        online={status.online}
        user={authUser}
        onSignOut={handleSignOut}
        onOpenWebMcp={() => setIsWebMcpOpen(true)}
        toolCount={webmcp.tools.length}
      />

      <main className="dashboard-shell">
        <EmergencyStop active={dashboardStatus.emergencyStopActive} onStop={handleEmergencyStop} onDisarm={handleStopDisarm} />

        <div className="workspace-intro">
          <div>
            <span className="section-kicker">TALOS / Workspace</span>
            <h2>Every movement. In view.</h2>
            <p>Drive, adjust, and understand your robot. All in one place.</p>
          </div>
          <div className={`workspace-connection ${status.online ? "is-online" : ""}`}>
            <span className="connection-dot" aria-hidden="true" />
            <span>
              <strong>{status.online ? "Robot connected" : "Waiting for TALOS"}</strong>
              <small>{status.online ? "Receiving live feedback" : "Offline · readings may be out of date"}</small>
            </span>
          </div>
        </div>

        <DashboardNavigation activeSection={activeSection} onSectionChange={setActiveSection} />

        <div id="workspace-content" className="workspace-content" tabIndex={-1}>
          <section className="workspace-view" id="view-control" role="tabpanel" aria-labelledby="tab-control" hidden={activeSection !== "control"}>
            <div className="control-layout">
              {activeSection === "control" ? (
                <div className="control-camera">
                  <Suspense fallback={<div className="panel loading-panel" role="status">Opening camera view…</div>}>
                    <CameraPanel compact cameraUrl={cameraUrl} onCameraUrlChange={setCameraUrl} onSave={handleCameraSave} />
                  </Suspense>
                </div>
              ) : null}
              <AttitudePanel armed={dashboardStatus.balancingEnabled} status={dashboardStatus} poseFeed={poseFeed} active={activeSection === "control"} />
              <ControlPanel
                armed={dashboardStatus.balancingEnabled}
                currentCommand={currentCommand}
                emergencyStopActive={dashboardStatus.emergencyStopActive}
                onArmedChange={handleArmedChange}
                onCommand={handleCommand}
                onStopDisarm={handleStopDisarm}
              />
              <section className="panel response-panel" aria-label="Motion settings">
                <SpeedSlider speed={speed} onSpeedChange={handleSpeedChange} />
                <ModeSelector mode={mode} onModeChange={handleModeChange} />
              </section>
            </div>
            <button className="workspace-shortcut" type="button" onClick={() => openSection("posture")}>
              <span className="shortcut-icon"><MoveVertical size={19} aria-hidden="true" /></span>
              <span><strong>Find your stance</strong><small>Body height, leg assistance, and endpoint calibration</small></span>
              <ArrowUpRight size={19} aria-hidden="true" />
            </button>
          </section>

          <section className="workspace-view" id="view-posture" role="tabpanel" aria-labelledby="tab-posture" hidden={activeSection !== "posture"}>
            <div className="workspace-two-column">
              <section className="panel posture-panel" aria-label="Body height and leg assistance">
                <div className="panel-heading"><div><span className="section-kicker">Posture</span><h2>Find your stance</h2></div><MoveVertical size={20} aria-hidden="true" /></div>
                <ServoControls
                  legAssist={legAssist} legPosition={legPosition} legSpeed={legSpeed}
                  status={dashboardStatus} onHeightCommandApply={handleHeightCommandApply}
                  onLegAssistChange={handleLegAssistChange} onPostureCalibration={handlePostureCalibration}
                  onJointCommandApply={handleJointCommandApply} onStopDisarm={handleStopDisarm}
                />
              </section>
              <RobotPosePanel status={poseFeed.status} poseFeed={poseFeed} active={activeSection === "posture"} />
            </div>
          </section>

          <section className="workspace-view" id="view-telemetry" role="tabpanel" aria-labelledby="tab-telemetry" hidden={activeSection !== "telemetry"}>
            <div className="workspace-two-column telemetry-layout">
              <StatusGrid status={dashboardStatus} currentCommand={currentCommand} speed={speed} />
              <ActivityLog entries={activityEntries} />
            </div>
          </section>

          <section className="workspace-view" id="view-connections" role="tabpanel" aria-labelledby="tab-connections" hidden={activeSection !== "connections"}>
            <div className="workspace-two-column connections-layout">
              <div className="camera-workspace">
                {activeSection === "connections" ? (
                  <Suspense fallback={<div className="panel loading-panel" role="status">Opening camera controls…</div>}>
                    <CameraPanel cameraUrl={cameraUrl} onCameraUrlChange={setCameraUrl} onSave={handleCameraSave} />
                  </Suspense>
                ) : null}
                <p className="workspace-caption"><Cable size={15} aria-hidden="true" />Also available in Control. Streams pause in other views.</p>
              </div>
              <WifiSetupPanel onWifiAction={handleWifiAction} status={dashboardStatus} />
            </div>
          </section>

          <section className="workspace-view" id="view-tuning" role="tabpanel" aria-labelledby="tab-tuning" hidden={activeSection !== "tuning"}>
            <div className="workspace-two-column">
              <SafetyControlPanel
                balanceTuning={balanceTuning} fallAngleLimitDeg={fallAngleLimitDeg}
                isBalancing={dashboardStatus.balancingEnabled}
                onApplyBalanceTuning={handleBalanceTuningApply}
                onFallAngleLimitChange={handleFallAngleLimitChange} status={dashboardStatus}
              />
              <section className="panel workspace-guide" aria-labelledby="tuning-guide-heading">
                <span className="guide-icon"><SlidersHorizontal size={24} aria-hidden="true" /></span>
                <span className="section-kicker">Tune with intention</span>
                <h2 id="tuning-guide-heading">One adjustment.<br />Then observe.</h2>
                <p>Make configuration changes with balance off and the robot supported. Applied settings are confirmed by the ESP32, not just by this dashboard.</p>
                <div className="guide-footnote"><CircleHelp size={18} aria-hidden="true" /><span>A higher fall cutoff is not a balance fix. It allows wheel motion at a larger lean angle.</span></div>
                <button className="guide-link" type="button" onClick={() => openSection("telemetry")}>View telemetry<ArrowUpRight size={17} aria-hidden="true" /></button>
              </section>
            </div>
          </section>
        </div>

        <footer className="workspace-footer"><span>TALOS ONE<span className="footer-divider">/</span>Robot control</span><span>Stay in control. Test with support.</span></footer>
      </main>

      {isWebMcpOpen ? (
        <Suspense fallback={<div className="console-loading" role="status">Opening agent console…</div>}>
          <WebMcpConsole isOpen onClose={closeWebMcp} tools={webmcp.tools} executionLogs={webmcp.executionLogs} executeTool={webmcp.executeTool} />
        </Suspense>
      ) : null}
    </div>
  );
}
