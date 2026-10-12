import { Cable, Radio } from "lucide-react";

export default function PoseFeedControls({ feed }) {
  const usb = feed.phase === "usb";
  return <div className="pose-feed-controls" aria-label="3D feedback connection">
    <div className="pose-feed-controls__connection">
      <span>{usb ? <Cable size={15} aria-hidden="true" /> : <Radio size={15} aria-hidden="true" />}
        <strong>{usb ? "Direct USB view" : "Cloud view"}</strong>
        <small>{usb ? `${feed.sampleRate ? feed.sampleRate.toFixed(0) : "—"} Hz · ${feed.status.online ? "fresh" : "waiting / stale"}` : "USB avoids cloud round trips"}</small></span>
      <button type="button" onClick={usb ? feed.disconnect : feed.connect} disabled={!feed.usbSupported || feed.phase === "connecting"}>
        {usb ? "Disconnect USB" : feed.phase === "connecting" ? "Connecting…" : "Connect USB"}
      </button>
    </div>
    <p>View-only. CAD calculations and rendering run on this device; balance and safety stay on the ESP32.</p>
    {!feed.usbSupported ? <p>Direct USB needs desktop Chrome/Edge. Cloud feedback remains available.</p> : null}
    {feed.error ? <p role="status" className="pose-feed-error">{feed.error}</p> : null}
    {!feed.status.extendedHeightCaptured || !feed.status.encodersHealthy ? <details className="pose-diagnostics">
      <summary>Leg preview &amp; missing calibration</summary>
      <p>Without an extended reference, raw encoder angles cannot identify absolute bend. A browser-only reference can show manual changes, but weak magnetic fields make this diagnostic—not accurate control feedback.</p>
      <button type="button" onClick={feed.captureReference} disabled={!feed.referenceReady}>Set visual extended reference</button>
      <label><input type="checkbox" checked={feed.diagnostic} onChange={event => feed.setDiagnostic(event.target.checked)} disabled={!feed.hasLocalReference && !feed.status.extendedHeightCaptured} />Show untrusted encoder preview</label>
      <small>Never enables motors or bypasses controller calibration. Clears on robot reboot; source references remain unchanged.</small>
    </details> : null}
  </div>;
}
