import {
  ArrowDown,
  ArrowUp,
  ExternalLink,
  LockKeyhole,
  Plus,
  Radio,
  RefreshCcw,
  Router,
  ScanSearch,
  Trash2,
  Wifi,
  WifiOff,
} from "lucide-react";
import { useMemo, useState } from "react";

const SETUP_PORTAL_URL = "http://192.168.4.1";

function normalizeSavedNetwork(entry, index, currentSsid) {
  const network =
    typeof entry === "string"
      ? { ssid: entry }
      : entry && typeof entry === "object"
        ? entry
        : {};
  const ssid = String(network.ssid || "").trim();
  if (!ssid) return null;
  return {
    ssid,
    priority: Number(network.priority || index + 1),
    rssi: Number(network.rssi),
    active: Boolean(network.active || ssid === currentSsid),
  };
}

function normalizeScanResult(entry) {
  const network =
    typeof entry === "string"
      ? { ssid: entry }
      : entry && typeof entry === "object"
        ? entry
        : {};
  const ssid = String(network.ssid || "").trim();
  if (!ssid) return null;
  return {
    ssid,
    rssi: Number(network.rssi),
  };
}

export default function WifiSetupPanel({ onWifiAction, status }) {
  const online = Boolean(status.online);
  const [showPortalGuide, setShowPortalGuide] = useState(false);
  const [busyAction, setBusyAction] = useState("");
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");

  const currentSsid = String(status.wifiSsid || "").trim();
  const setupSsid =
    String(status.wifiSetupSsid || "").trim() || "TALOS-SETUP-<MAC4>";
  const wifiMutationAllowed =
    online &&
    !status.balancingEnabled &&
    !status.standingEnabled &&
    !status.armRequested;
  const scanAllowed = wifiMutationAllowed;
  const setupAllowed = !online || wifiMutationAllowed;
  const savedNetworks = useMemo(() => {
    const source =
      [status.wifiProfiles, status.savedWifiNetworks, status.wifiNetworks].find(
        (candidate) => Array.isArray(candidate) && candidate.length > 0,
      ) || [];
    return source
      .map((entry, index) => normalizeSavedNetwork(entry, index, currentSsid))
      .filter(Boolean)
      .sort((a, b) => a.priority - b.priority);
  }, [currentSsid, status.savedWifiNetworks, status.wifiNetworks, status.wifiProfiles]);
  const scanResults = useMemo(
    () =>
      (Array.isArray(status.wifiScanResults) ? status.wifiScanResults : [])
        .map(normalizeScanResult)
        .filter(Boolean)
        .sort((a, b) => {
          if (!Number.isFinite(a.rssi)) return 1;
          if (!Number.isFinite(b.rssi)) return -1;
          return b.rssi - a.rssi;
        })
        .slice(0, 8),
    [status.wifiScanResults],
  );

  async function runAction(action, options, successMessage) {
    const actionKey = `${action}-${options?.ssid || "setup"}`;
    setBusyAction(actionKey);
    setError("");
    setMessage("");
    try {
      await onWifiAction(action, options);
      setMessage(successMessage);
      return true;
    } catch (actionError) {
      setError(actionError?.message || "Wi-Fi request failed.");
      return false;
    } finally {
      setBusyAction("");
    }
  }

  async function startSetup() {
    setShowPortalGuide(true);
    if (!online) {
      setMessage(
        "ESP32 is offline. Its automatic fallback access point is available when saved networks cannot connect; follow the portal steps.",
      );
      return;
    }
    if (!wifiMutationAllowed) {
      setError("Wi-Fi setup is accepted only after balance and leg support are safely off.");
      setMessage("");
      return;
    }
    await runAction(
      "setup",
      undefined,
      `Setup mode requested. Connect your phone to ${setupSsid} when it appears.`,
    );
  }

  function scanNetworks() {
    if (!scanAllowed) {
      setError("Wi-Fi scanning is accepted only while balance is safely off.");
      setMessage("");
      return;
    }
    runAction(
      "scan",
      undefined,
      "Nearby-network scan requested. Results will appear when the ESP32 reports them.",
    );
  }

  function removeNetwork(ssid) {
    const confirmed = window.confirm(
      `Remove “${ssid}” from the ESP32 saved Wi-Fi list?`,
    );
    if (!confirmed) return;
    runAction("remove", { ssid }, `Remove request sent for ${ssid}.`);
  }

  return (
    <section className="panel wifi-setup-panel" aria-labelledby="wifi-setup-heading">
      <div className="wifi-setup-header">
        <div className="wifi-setup-title">
          <span className="wifi-setup-icon" aria-hidden="true">
            <Router size={18} />
          </span>
          <div>
            <p className="eyebrow" id="wifi-setup-heading">
              <span className="eyebrow-dot" />
              Robot networks
            </p>
            <p className="panel-sub">Saved locally on the ESP32</p>
          </div>
        </div>

        <span className={`system-state system-state--${online ? "nominal" : "offline"}`}>
          {online ? <Wifi size={15} aria-hidden="true" /> : <WifiOff size={15} aria-hidden="true" />}
          {online ? "Connected" : "Offline"}
        </span>
      </div>

      <div className="wifi-current-card">
        <span className="wifi-current-icon" aria-hidden="true">
          <Radio size={17} />
        </span>
        <div>
          <small>Current network</small>
          <strong>{currentSsid || (online ? "Connected network" : "No connection")}</strong>
          <span>
            {online
              ? `${status.ipAddress || "IP pending"} · ${status.wifiRssi} dBm`
              : "Use setup mode to add a 2.4 GHz network."}
          </span>
        </div>
      </div>

      <div className="saved-network-heading">
        <div>
          <span className="setting-label">Connection priority</span>
          <span className="setting-note">
            The ESP32 tries saved networks from top to bottom.
          </span>
        </div>
        <span>{savedNetworks.length} saved</span>
      </div>

      <div className="saved-network-list">
        {savedNetworks.length ? (
          savedNetworks.map((network, index) => (
            <article className={`saved-network ${network.active ? "is-active" : ""}`} key={network.ssid}>
              <span className="saved-network__priority">{index + 1}</span>
              <div className="saved-network__copy">
                <strong>{network.ssid}</strong>
                <span>
                  {network.active
                    ? "Connected"
                    : Number.isFinite(network.rssi)
                      ? `${network.rssi} dBm`
                      : "Saved on robot"}
                </span>
              </div>
              <div className="saved-network__actions">
                <button
                  type="button"
                  onClick={() =>
                    runAction("reorder", { ssid: network.ssid, direction: -1 }, `${network.ssid} moved up.`)
                  }
                  disabled={
                    !wifiMutationAllowed || index === 0 || Boolean(busyAction)
                  }
                  title={`Move ${network.ssid} up`}
                  aria-label={`Move ${network.ssid} up`}
                >
                  <ArrowUp size={14} aria-hidden="true" />
                </button>
                <button
                  type="button"
                  onClick={() =>
                    runAction("reorder", { ssid: network.ssid, direction: 1 }, `${network.ssid} moved down.`)
                  }
                  disabled={
                    !wifiMutationAllowed ||
                    index === savedNetworks.length - 1 ||
                    Boolean(busyAction)
                  }
                  title={`Move ${network.ssid} down`}
                  aria-label={`Move ${network.ssid} down`}
                >
                  <ArrowDown size={14} aria-hidden="true" />
                </button>
                <button
                  type="button"
                  onClick={() =>
                    runAction("reconnect", { ssid: network.ssid }, `Reconnect requested for ${network.ssid}.`)
                  }
                  disabled={!wifiMutationAllowed || Boolean(busyAction)}
                  title={`Connect to ${network.ssid}`}
                  aria-label={`Connect to ${network.ssid}`}
                >
                  <RefreshCcw size={14} aria-hidden="true" />
                </button>
                <button
                  className="is-danger"
                  type="button"
                  onClick={() => removeNetwork(network.ssid)}
                  disabled={!wifiMutationAllowed || Boolean(busyAction)}
                  title={`Remove ${network.ssid}`}
                  aria-label={`Remove ${network.ssid}`}
                >
                  <Trash2 size={14} aria-hidden="true" />
                </button>
              </div>
            </article>
          ))
        ) : (
          <p className="saved-network-empty">
            The installed firmware has not reported its saved-network list yet.
          </p>
        )}
      </div>

      <div className="wifi-action-row">
        <button
          className="wifi-add-button"
          type="button"
          onClick={startSetup}
          disabled={!setupAllowed || busyAction === "setup-setup"}
        >
          <Plus size={16} aria-hidden="true" />
          {busyAction === "setup-setup" ? "Requesting setup…" : "Add Wi-Fi network"}
        </button>
        <button
          className="wifi-scan-button"
          type="button"
          onClick={scanNetworks}
          disabled={!scanAllowed || Boolean(busyAction)}
          title={
            scanAllowed
              ? "Scan for nearby 2.4 GHz networks"
              : "Balance and leg support must be off before scanning"
          }
        >
          <ScanSearch size={16} aria-hidden="true" />
          {busyAction === "scan-setup" ? "Scanning…" : "Scan networks"}
        </button>
      </div>
      <p className="wifi-scan-note">
        Network scans are accepted only while balance and leg support are off.
      </p>

      {scanResults.length ? (
        <div className="wifi-scan-results" aria-label="Nearby Wi-Fi scan results">
          {scanResults.map((network) => (
            <span className="wifi-scan-result" key={`${network.ssid}-${network.rssi}`}>
              <strong>{network.ssid}</strong>
              <small>{Number.isFinite(network.rssi) ? `${network.rssi} dBm` : "Nearby"}</small>
            </span>
          ))}
        </div>
      ) : null}

      {showPortalGuide ? (
        <div className="wifi-portal-guide">
          <div className="wifi-portal-guide__heading">
            <LockKeyhole size={16} aria-hidden="true" />
            <strong>Password-safe local setup</strong>
          </div>
          <ol>
            <li>
              Wait for the ESP32 network named <strong>{setupSsid}</strong>.
            </li>
            <li>
              Connect this phone using password <strong>talossetup</strong>.
            </li>
            <li>Open the local portal and save the new 2.4 GHz Wi-Fi network.</li>
          </ol>
          <a href={SETUP_PORTAL_URL} target="_blank" rel="noreferrer">
            Open TALOS setup portal
            <ExternalLink size={14} aria-hidden="true" />
          </a>
          <p>
            Wi-Fi passwords stay between this device and the ESP32 portal. They are never
            written to Firebase.
          </p>
        </div>
      ) : null}

      {status.wifiCommandState && status.wifiCommandState !== "idle" ? (
        <p className="wifi-command-state">
          ESP32 command state: <strong>{status.wifiCommandState}</strong>
          {status.wifiCommandMessage ? ` · ${status.wifiCommandMessage}` : ""}
        </p>
      ) : null}
      {error ? <p className="wifi-setup-status wifi-setup-status--error">{error}</p> : null}
      {message ? <p className="wifi-setup-status wifi-setup-status--success">{message}</p> : null}
    </section>
  );
}
