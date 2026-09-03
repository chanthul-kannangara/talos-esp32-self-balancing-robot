import { useState } from "react";
import ActivityLog from "./components/ActivityLog";
import AttitudePanel from "./components/AttitudePanel";
import CameraPanel from "./components/CameraPanel";
import ControlPanel from "./components/ControlPanel";
import EmergencyStop from "./components/EmergencyStop";
import Header from "./components/Header";
import LoginPage from "./components/LoginPage";
import ModeSelector from "./components/ModeSelector";
import SafetyControlPanel from "./components/SafetyControlPanel";
import ServoControls from "./components/ServoControls";
import SpeedSlider from "./components/SpeedSlider";
import StatusGrid from "./components/StatusGrid";
import WebMcpConsole from "./components/WebMcpConsole";
import WifiSetupPanel from "./components/WifiSetupPanel";
import { useAuthSession } from "./hooks/useAuthSession";
import { useRobotDashboard } from "./hooks/useRobotDashboard";
import { useThemePreference } from "./hooks/useThemePreference";
import { useWebMCP } from "./hooks/useWebMCP";

export default function App() {
  const { handleThemeToggle, theme } = useThemePreference();
  const { authLoading, authUser, handleSignOut } = useAuthSession();
  const showDashboardPreview =
    import.meta.env.DEV &&
    typeof window !== "undefined" &&
    new URLSearchParams(window.location.search).has("preview-dashboard");

  if (authLoading) {
    return (
      <main className="auth-shell">
        <section className="auth-loading panel">
          <span className="cursor-blink" aria-hidden="true" />
          Checking secure session...
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
  const [isWebMcpOpen, setIsWebMcpOpen] = useState(false);
  const {
    activityEntries,
    balanceTuning,
    cameraUrl,
    currentCommand,
    dashboardStatus,
    fallAngleLimitDeg,
    handleBalanceTuningApply,
    handleCameraSave,
    handleCommand,
    handleArmedChange,
    handleEmergencyStop,
    handleFallAngleLimitChange,
    handleHeightCommandApply,
    handleLegAssistChange,
    handleModeChange,
    handleSpeedChange,
    handleStopDisarm,
    handlePostureCalibration,
    handleWifiAction,
    legAssist,
    legPosition,
    legSpeed,
    mode,
    setCameraUrl,
    speed,
    status,
  } = useRobotDashboard();

  const webmcp = useWebMCP({
    status,
    dashboardStatus,
    currentCommand,
    speed,
    mode,
    legPosition,
    legSpeed,
    legAssist,
    balanceTuning,
    fallAngleLimitDeg,
    cameraUrl,
    handleCommand,
    handleSpeedChange,
    handleArmedChange,
    handleEmergencyStop,
    handleStopDisarm,
    handleHeightCommandApply,
    handleLegAssistChange,
    handleBalanceTuningApply,
    handleFallAngleLimitChange,
    handleModeChange,
    handleCameraSave,
  });

  return (
    <div className="app">
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
        <EmergencyStop
          active={dashboardStatus.emergencyStopActive}
          onStop={handleEmergencyStop}
        />

        <div className="command-console">
          <section className="console-column console-column--state" aria-labelledby="robot-state-column">
            <header className="console-column__header">
              <div>
                <span className="console-column__index">01</span>
                <div>
                  <p>Robot state</p>
                  <h2 id="robot-state-column">Balance instrument</h2>
                </div>
              </div>
              <span className={`console-column__signal ${status.online ? "is-live" : ""}`}>
                {status.online ? "Live" : "No signal"}
              </span>
            </header>

            <AttitudePanel armed={dashboardStatus.balancingEnabled} status={dashboardStatus} />
            <ActivityLog entries={activityEntries} />
          </section>

          <section className="console-column console-column--controls" aria-labelledby="primary-controls-column">
            <header className="console-column__header">
              <div>
                <span className="console-column__index">02</span>
                <div>
                  <p>Primary controls</p>
                  <h2 id="primary-controls-column">Drive & response</h2>
                </div>
              </div>
              <span className={`console-column__signal ${dashboardStatus.balancingEnabled ? "is-armed" : ""}`}>
                {dashboardStatus.balancingEnabled ? "Armed" : "Standby"}
              </span>
            </header>

            <ControlPanel
              armed={dashboardStatus.balancingEnabled}
              currentCommand={currentCommand}
              emergencyStopActive={dashboardStatus.emergencyStopActive}
              onArmedChange={handleArmedChange}
              onCommand={handleCommand}
              onStopDisarm={handleStopDisarm}
            />

            <section className="panel speed-mode-panel" aria-labelledby="response-heading">
              <div className="panel-heading">
                <div>
                  <span className="section-kicker">Response</span>
                  <h2 id="response-heading">Motion settings</h2>
                </div>
              </div>
              <SpeedSlider speed={speed} onSpeedChange={handleSpeedChange} />
              <ModeSelector mode={mode} onModeChange={handleModeChange} />
              <ServoControls
                legAssist={legAssist}
                legPosition={legPosition}
                legSpeed={legSpeed}
                status={dashboardStatus}
                onHeightCommandApply={handleHeightCommandApply}
                onLegAssistChange={handleLegAssistChange}
                onPostureCalibration={handlePostureCalibration}
              />
            </section>

            <SafetyControlPanel
              balanceTuning={balanceTuning}
              fallAngleLimitDeg={fallAngleLimitDeg}
              isBalancing={dashboardStatus.balancingEnabled}
              onApplyBalanceTuning={handleBalanceTuningApply}
              onFallAngleLimitChange={handleFallAngleLimitChange}
              status={dashboardStatus}
            />
          </section>

          <aside className="console-column console-column--health" aria-labelledby="system-health-column">
            <header className="console-column__header">
              <div>
                <span className="console-column__index">03</span>
                <div>
                  <p>System health</p>
                  <h2 id="system-health-column">Telemetry & links</h2>
                </div>
              </div>
              <span className={`console-column__signal ${status.online ? "is-healthy" : "is-warning"}`}>
                {status.online ? "Nominal" : "Offline"}
              </span>
            </header>

            <StatusGrid status={dashboardStatus} currentCommand={currentCommand} speed={speed} />
            <WifiSetupPanel onWifiAction={handleWifiAction} status={dashboardStatus} />
            <CameraPanel
              cameraUrl={cameraUrl}
              onCameraUrlChange={setCameraUrl}
              onSave={handleCameraSave}
            />
          </aside>
        </div>
      </main>

      <WebMcpConsole
        isOpen={isWebMcpOpen}
        onClose={() => setIsWebMcpOpen(false)}
        tools={webmcp.tools}
        executionLogs={webmcp.executionLogs}
        executeTool={webmcp.executeTool}
      />
    </div>
  );
}
