import { parseUsbPose } from "./poseTelemetry.js";

export function usbPoseAvailable() {
  return typeof navigator !== "undefined" && "serial" in navigator && window.isSecureContext;
}

export async function openUsbPoseStream({ onFrame, onError, onClose }) {
  if (!usbPoseAvailable()) throw new Error("Direct USB requires a Web Serial compatible desktop browser such as Chrome or Edge.");
  const port = await navigator.serial.requestPort();
  let reader;
  let writer;
  let heartbeat;
  let stopped = false;
  let closing;
  let readLoop;
  let writeInProgress = false;
  const encoder = new TextEncoder();
  const writeCommand = async (command) => {
    if (stopped || !writer || writeInProgress) return;
    writeInProgress = true;
    let timeout;
    try {
      await Promise.race([
        writer.write(encoder.encode(`${command}\n`)),
        new Promise((resolve, reject) => { timeout = window.setTimeout(() => reject(new Error("USB write timed out. Reconnect the cable.")), 1000); }),
      ]);
    } finally {
      window.clearTimeout(timeout);
      writeInProgress = false;
    }
  };
  const close = () => {
    if (closing) return closing;
    closing = (async () => {
      window.clearInterval(heartbeat);
      try { await writeCommand("pose-stream off"); } catch {}
      stopped = true;
      try { await reader?.cancel(); } catch {}
      try { await readLoop; } catch {}
      try { reader?.releaseLock(); } catch {}
      try { writer?.releaseLock(); } catch {}
      try { await port.close(); } catch {}
      navigator.serial.removeEventListener("disconnect", disconnected);
      onClose();
    })();
    return closing;
  };
  const disconnected = (event) => { if (event.target === port) close(); };
  try {
    await port.open({ baudRate: 115200, bufferSize: 4096 });
    await port.setSignals({ dataTerminalReady: false, requestToSend: false });
    reader = port.readable.getReader();
    writer = port.writable.getWriter();
    navigator.serial.addEventListener("disconnect", disconnected);
    const decoder = new TextDecoder();
    let pending = "";
    let previousBoot;
    let previousSequence;
    readLoop = (async () => {
      try {
        while (!stopped) {
          const { value, done } = await reader.read();
          if (done) break;
          pending += decoder.decode(value, { stream: true });
          const lines = pending.split("\n");
          pending = lines.pop();
          if (pending.length > 2048) pending = "";
          let latest;
          for (const line of lines) {
            if (line.length > 1024) continue;
            const frame = parseUsbPose(line);
            if (!frame) continue;
            if (frame.poseBootId === previousBoot &&
                ((frame.poseSequence - previousSequence) >>> 0) >= 0x80000000) continue;
            if (frame.poseBootId === previousBoot && frame.poseSequence === previousSequence) continue;
            previousBoot = frame.poseBootId;
            previousSequence = frame.poseSequence;
            latest = frame;
          }
          if (latest) onFrame(latest);
        }
      } catch (error) {
        if (!stopped) onError(error.message || "USB telemetry interrupted.");
      }
      if (!stopped) queueMicrotask(close);
    })();
    await writeCommand("pose-stream on");
    if (!closing) heartbeat = window.setInterval(() => { writeCommand("pose-stream on").catch(error => { onError(error.message); close(); }); }, 1000);
    return { close, get closed() { return stopped || Boolean(closing); } };
  } catch (error) {
    await close();
    if (error.name === "NetworkError") throw new Error("Cannot open USB. Close the COM8 serial monitor/bridge, then try again.");
    throw error;
  }
}
