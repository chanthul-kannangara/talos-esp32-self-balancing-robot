import {
  Camera,
  ExternalLink,
  RefreshCw,
  Save,
  ShieldAlert,
} from "lucide-react";
import { useEffect, useMemo, useRef, useState } from "react";
import { normalizeCameraStreamUrl } from "../services/robotApi";

const CONNECT_TIMEOUT_MS = 12000;

function appendReconnectToken(url, token) {
  if (!url || /^wss?:\/\//i.test(url)) return url;

  try {
    const parsed = new URL(url);
    parsed.searchParams.set("_talos", String(token));
    return parsed.toString();
  } catch {
    return url;
  }
}

function getCameraCaptureUrl(streamUrl, reconnectToken, frameToken) {
  try {
    const parsed = new URL(streamUrl);
    parsed.port = "";
    parsed.pathname = "/capture";
    parsed.search = "";
    parsed.hash = "";
    parsed.searchParams.set(
      "_talos",
      `${reconnectToken}-${frameToken}`,
    );
    return parsed.toString();
  } catch {
    return streamUrl;
  }
}

function getCameraHomeUrl(streamUrl) {
  try {
    const parsed = new URL(streamUrl);
    parsed.port = "";
    parsed.pathname = "/";
    parsed.search = "";
    parsed.hash = "";
    return parsed.toString();
  } catch {
    return streamUrl;
  }
}

export default function CameraPanel({ cameraUrl, onCameraUrlChange, onSave }) {
  const normalizedCameraUrl = normalizeCameraStreamUrl(cameraUrl);
  const hasCameraUrl = Boolean(normalizedCameraUrl);
  const isWebSocketStream = /^wss?:\/\//i.test(normalizedCameraUrl);
  const isSecurePage =
    typeof window !== "undefined" && window.location.protocol === "https:";
  const isLocalHttpStream =
    isSecurePage && normalizedCameraUrl.startsWith("http://");

  const [allowLocalStream, setAllowLocalStream] = useState(
    () => window.sessionStorage.getItem("talos-allow-local-camera") === "true",
  );
  const [previewReady, setPreviewReady] = useState(false);
  const [streamState, setStreamState] = useState(
    hasCameraUrl ? "connecting" : "idle",
  );
  const [reconnectToken, setReconnectToken] = useState(0);
  const [snapshotToken, setSnapshotToken] = useState(0);
  const imageRef = useRef(null);
  const snapshotTimerRef = useRef(null);

  const canRenderStream =
    hasCameraUrl && (!isLocalHttpStream || allowLocalStream);
  const useLowLatencySnapshots =
    canRenderStream && isLocalHttpStream && !isWebSocketStream;
  const imageStreamUrl = useMemo(
    () =>
      useLowLatencySnapshots
        ? getCameraCaptureUrl(
            normalizedCameraUrl,
            reconnectToken,
            snapshotToken,
          )
        : appendReconnectToken(normalizedCameraUrl, reconnectToken),
    [
      normalizedCameraUrl,
      reconnectToken,
      snapshotToken,
      useLowLatencySnapshots,
    ],
  );
  const cameraHomeUrl = getCameraHomeUrl(normalizedCameraUrl);

  useEffect(() => {
    setPreviewReady(false);

    if (!hasCameraUrl) {
      setStreamState("idle");
    } else if (isLocalHttpStream && !allowLocalStream) {
      setStreamState("blocked");
    } else {
      setStreamState("connecting");
    }
  }, [
    allowLocalStream,
    hasCameraUrl,
    isLocalHttpStream,
    normalizedCameraUrl,
    reconnectToken,
  ]);

  useEffect(() => {
    if (!canRenderStream || isWebSocketStream || previewReady) return undefined;

    const timeout = window.setTimeout(() => {
      setStreamState("offline");
    }, CONNECT_TIMEOUT_MS);

    return () => window.clearTimeout(timeout);
  }, [canRenderStream, isWebSocketStream, previewReady, reconnectToken]);

  useEffect(() => {
    if (!canRenderStream || !isWebSocketStream) return undefined;

    let currentFrameUrl = null;
    let socket = null;
    let retryTimer = null;
    let retryCount = 0;
    let disposed = false;

    const connect = () => {
      if (disposed) return;

      setStreamState(retryCount > 0 ? "reconnecting" : "connecting");
      socket = new WebSocket(normalizedCameraUrl);
      socket.binaryType = "arraybuffer";

      socket.onmessage = (event) => {
        const image = imageRef.current;
        if (!image) return;

        const nextFrameUrl = URL.createObjectURL(
          new Blob([event.data], { type: "image/jpeg" }),
        );
        image.src = nextFrameUrl;
        setPreviewReady(true);
        setStreamState("live");
        retryCount = 0;

        if (currentFrameUrl) URL.revokeObjectURL(currentFrameUrl);
        currentFrameUrl = nextFrameUrl;
      };

      socket.onerror = () => socket?.close();
      socket.onclose = () => {
        if (disposed) return;

        setPreviewReady(false);
        setStreamState("reconnecting");
        retryCount += 1;
        retryTimer = window.setTimeout(
          connect,
          Math.min(1000 * 2 ** retryCount, 10000),
        );
      };
    };

    connect();

    return () => {
      disposed = true;
      window.clearTimeout(retryTimer);
      socket?.close();
      if (currentFrameUrl) URL.revokeObjectURL(currentFrameUrl);
    };
  }, [
    canRenderStream,
    isWebSocketStream,
    normalizedCameraUrl,
    reconnectToken,
  ]);

  useEffect(
    () => () => window.clearTimeout(snapshotTimerRef.current),
    [],
  );

  const requestNextSnapshot = (delay = 0) => {
    window.clearTimeout(snapshotTimerRef.current);
    snapshotTimerRef.current = window.setTimeout(() => {
      setSnapshotToken((current) => current + 1);
    }, delay);
  };

  const reconnect = () => {
    window.clearTimeout(snapshotTimerRef.current);
    setPreviewReady(false);
    setStreamState("connecting");
    setReconnectToken((current) => current + 1);
  };

  const tryLocalStream = () => {
    window.sessionStorage.setItem("talos-allow-local-camera", "true");
    setAllowLocalStream(true);
    reconnect();
  };

  const statusLabel = {
    blocked: "Browser blocked",
    connecting: "Connecting",
    idle: "Offline",
    live: "Live",
    offline: "Unavailable",
    reconnecting: "Reconnecting",
  }[streamState];

  return (
    <section className="panel camera-panel" aria-labelledby="camera-heading">
      <div className="panel-title-row">
        <div>
          <span className="section-kicker">Vision</span>
          <h2 id="camera-heading">Camera</h2>
          <p className="panel-sub">
            {hasCameraUrl ? "OV3660 live preview" : "Camera not configured"}
          </p>
        </div>
        <span className={`camera-stream-state camera-stream-state--${streamState}`}>
          <span aria-hidden="true" />
          {statusLabel}
        </span>
      </div>

      <form className="camera-form" onSubmit={onSave}>
        <div className="camera-form__row">
          <label htmlFor="camera-url">Stream address</label>
          <input
            id="camera-url"
            type="url"
            value={cameraUrl}
            onChange={(event) => onCameraUrlChange(event.target.value)}
            placeholder="http://192.168.4.1:81/stream"
          />
        </div>
        <div className="camera-form__actions">
          <button type="submit" title="Save camera URL">
            <Save size={18} aria-hidden="true" />
            <span>Apply stream</span>
          </button>
          <button
            type="button"
            title="Reconnect camera stream"
            onClick={reconnect}
            disabled={!hasCameraUrl}
          >
            <RefreshCw size={18} aria-hidden="true" />
            <span>Reconnect</span>
          </button>
          <a
            className="camera-link-button"
            href={cameraHomeUrl}
            target="_blank"
            rel="noreferrer"
            aria-disabled={!hasCameraUrl}
          >
            <ExternalLink size={18} aria-hidden="true" />
            <span>Open camera</span>
          </a>
        </div>
      </form>

      <div className="camera-preview">
        {canRenderStream ? (
          <>
            <img
              ref={imageRef}
              className={
                previewReady
                  ? "camera-preview__image is-visible"
                  : "camera-preview__image"
              }
              src={isWebSocketStream ? undefined : imageStreamUrl}
              alt="TALOS OV3660 stream preview"
              onLoad={() => {
                setPreviewReady(true);
                setStreamState("live");
                if (useLowLatencySnapshots) requestNextSnapshot();
              }}
              onError={() => {
                setPreviewReady(false);
                setStreamState("offline");
                if (useLowLatencySnapshots) requestNextSnapshot(1000);
              }}
            />
            <div
              className={
                previewReady
                  ? "camera-preview__placeholder is-hidden"
                  : "camera-preview__placeholder"
              }
            >
              <Camera size={44} aria-hidden="true" />
              <strong>
                {streamState === "offline"
                  ? "Camera unavailable"
                  : "Connecting to camera"}
              </strong>
              <span>
                {streamState === "offline"
                  ? "Check the camera Wi-Fi, then reconnect."
                  : "Waiting for the first frame."}
              </span>
            </div>
          </>
        ) : isLocalHttpStream ? (
          <div className="camera-preview__placeholder camera-preview__blocked">
            <ShieldAlert size={42} aria-hidden="true" />
            <strong>Local stream permission required</strong>
            <span>
              Connect this device to TALOS-CAMERA-DIRECT, then allow the local
              preview or open the camera directly.
            </span>
            <div className="camera-preview__actions">
              <button type="button" onClick={tryLocalStream}>
                Try embedded view
              </button>
              <a href={cameraHomeUrl} target="_blank" rel="noreferrer">
                Open direct view
              </a>
            </div>
          </div>
        ) : (
          <div className="camera-preview__placeholder">
            <Camera size={44} aria-hidden="true" />
            <strong>No camera URL saved</strong>
          </div>
        )}
      </div>
    </section>
  );
}
