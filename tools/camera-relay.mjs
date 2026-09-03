import http from "node:http";
import https from "node:https";

const port = Number(process.env.PORT || 8787);
const streamUrl = process.env.CAMERA_STREAM_URL || "http://192.168.1.50:81/stream";
const captureUrl = process.env.CAMERA_CAPTURE_URL || streamUrl.replace(/\/stream(?:\?.*)?$/, "/capture");
const relayToken = process.env.CAMERA_RELAY_TOKEN || "";

function sendJson(response, status, body) {
  response.writeHead(status, {
    "Access-Control-Allow-Origin": "*",
    "Cache-Control": "no-store",
    "Content-Type": "application/json; charset=utf-8",
  });
  response.end(JSON.stringify(body));
}

function authorized(request) {
  if (!relayToken) return true;
  const requestUrl = new URL(request.url, `http://${request.headers.host}`);
  return requestUrl.searchParams.get("token") === relayToken;
}

function proxyCamera(request, response, targetUrl) {
  let target;
  try {
    target = new URL(targetUrl);
  } catch {
    sendJson(response, 500, { error: "Invalid CAMERA_STREAM_URL configuration." });
    return;
  }

  const client = target.protocol === "https:" ? https : http;
  const upstream = client.get(target, { headers: { Accept: "multipart/x-mixed-replace,image/jpeg,*/*" } }, (cameraResponse) => {
    response.writeHead(cameraResponse.statusCode || 502, {
      "Access-Control-Allow-Origin": "*",
      "Cache-Control": "no-store, no-transform",
      "Content-Type": cameraResponse.headers["content-type"] || "application/octet-stream",
      "X-Content-Type-Options": "nosniff",
    });
    cameraResponse.pipe(response);
  });

  upstream.setTimeout(15000, () => upstream.destroy(new Error("Camera connection timed out.")));
  upstream.on("error", (error) => {
    if (!response.headersSent) {
      sendJson(response, 502, { error: "ESP32-CAM is unavailable.", detail: error.message });
    } else {
      response.destroy(error);
    }
  });

  request.on("close", () => upstream.destroy());
}

const server = http.createServer((request, response) => {
  const requestUrl = new URL(request.url, `http://${request.headers.host}`);

  if (request.method !== "GET") {
    sendJson(response, 405, { error: "Only GET is supported." });
    return;
  }

  if (requestUrl.pathname === "/health") {
    sendJson(response, 200, { online: true });
    return;
  }

  if (!authorized(request)) {
    sendJson(response, 401, { error: "Invalid relay token." });
    return;
  }

  if (requestUrl.pathname === "/stream") {
    proxyCamera(request, response, streamUrl);
    return;
  }

  if (requestUrl.pathname === "/capture") {
    proxyCamera(request, response, captureUrl);
    return;
  }

  sendJson(response, 404, { error: "Use /stream, /capture, or /health." });
});

server.listen(port, "127.0.0.1", () => {
  console.log(`TALOS camera relay listening on http://127.0.0.1:${port}`);
  console.log(`ESP32-CAM source: ${streamUrl}`);
  console.log("Expose this relay through an HTTPS tunnel, then save the public /stream URL in the dashboard.");
});
