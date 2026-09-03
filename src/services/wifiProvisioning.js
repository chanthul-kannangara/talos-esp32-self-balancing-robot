const WIFI_COMMAND_PREFIX = "WIFI_CONFIG ";

export function isWebSerialSupported() {
  return typeof navigator !== "undefined" && "serial" in navigator;
}

function toBase64(value) {
  const bytes = new TextEncoder().encode(value);
  let binary = "";

  for (const byte of bytes) {
    binary += String.fromCharCode(byte);
  }

  return window.btoa(binary);
}

export async function provisionRobotWifi({ ssid, password }) {
  if (!isWebSerialSupported()) {
    throw new Error("USB setup requires desktop Chrome or Edge.");
  }

  let port;
  let writer;

  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });

    writer = port.writable.getWriter();
    const payload = `${toBase64(ssid)}:${toBase64(password)}`;
    await writer.write(new TextEncoder().encode(`${WIFI_COMMAND_PREFIX}${payload}\n`));

    // Give the ESP32 time to persist the command before closing the USB port.
    await new Promise((resolve) => window.setTimeout(resolve, 500));
  } catch (error) {
    if (error?.name === "NotFoundError") {
      throw new Error("No USB device was selected.");
    }

    if (error?.name === "NetworkError") {
      throw new Error("Close Arduino Serial Monitor, then try again.");
    }

    throw error;
  } finally {
    writer?.releaseLock();

    if (port?.readable || port?.writable) {
      await port.close().catch(() => undefined);
    }
  }
}
