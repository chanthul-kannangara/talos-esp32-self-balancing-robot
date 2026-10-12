#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>

#include "board_config.h"

// Copy network_config.example.h to network_config.h before deployment.
// The optional local header is ignored by Git.
#if __has_include("network_config.h")
#include "network_config.h"
#else
#define TALOS_CAMERA_AP_SSID "TALOS-CAM-SETUP"
#define TALOS_CAMERA_AP_PASSWORD "configure-me-first"
#define TALOS_CAMERA_ROUTER_SSID ""
#define TALOS_CAMERA_ROUTER_PASSWORD ""
#endif
const char *directApSsid = TALOS_CAMERA_AP_SSID;
const char *directApPassword = TALOS_CAMERA_AP_PASSWORD;
const char *fallbackRouterSsid = TALOS_CAMERA_ROUTER_SSID;
const char *fallbackRouterPassword = TALOS_CAMERA_ROUTER_PASSWORD;

IPAddress directApIp(192, 168, 4, 1);
IPAddress directApMask(255, 255, 255, 0);

Preferences wifiPreferences;
WebServer setupServer(82);
String activeSsid;
String activePassword;
bool naptEnabled = false;
bool restartRequested = false;
uint32_t restartAt = 0;

void startCameraServer();
void setupLedFlash();

bool connectToRouter(const String &ssid, const String &password,
                     uint32_t timeoutMs) {
  if (ssid.isEmpty()) {
    return false;
  }

  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.printf("Connecting to %s", ssid.c_str());

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected to %s\n", ssid.c_str());
    return true;
  }

  Serial.printf("Could not connect to %s\n", ssid.c_str());
  return false;
}

String htmlEscape(const String &value) {
  String escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  escaped.replace("'", "&#39;");
  return escaped;
}

void startSetupServer() {
  setupServer.on("/", HTTP_GET, []() {
    const bool connected = WiFi.status() == WL_CONNECTED;
    const String statusText =
        connected
            ? "Connected to " + htmlEscape(activeSsid) + " at " +
                  WiFi.localIP().toString()
            : activeSsid.isEmpty()
                  ? "No router Wi-Fi has been saved yet."
                  : "Could not connect to " + htmlEscape(activeSsid) + ".";

    String page =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>TALOS Camera Wi-Fi</title><style>"
        "body{margin:0;background:#0d1110;color:#eef5f1;font-family:Arial,sans-serif}"
        "main{max-width:460px;margin:40px auto;padding:24px}"
        "section{background:#171d1b;border:1px solid #303936;border-radius:12px;padding:24px}"
        "h1{font-size:24px;margin:0 0 8px}p{color:#aebbb5;line-height:1.5}"
        "label{display:block;margin:18px 0 7px;font-weight:700}"
        "input{box-sizing:border-box;width:100%;padding:13px;border-radius:8px;"
        "border:1px solid #43504b;background:#0e1311;color:#fff;font-size:16px}"
        "button{width:100%;margin-top:22px;padding:13px;border:0;border-radius:8px;"
        "background:#34d6a0;color:#07130f;font-size:16px;font-weight:800}"
        "small{display:block;margin-top:16px;color:#7f8d87}</style></head><body>"
        "<main><section><h1>TALOS Camera Wi-Fi</h1><p>" +
        statusText +
        "</p><form method='post' action='/save'>"
        "<label for='ssid'>Wi-Fi name (SSID)</label>"
        "<input id='ssid' name='ssid' required autocomplete='off' value='" +
        htmlEscape(activeSsid) +
        "'><label for='password'>Wi-Fi password</label>"
        "<input id='password' name='password' type='password' "
        "autocomplete='new-password' placeholder='Enter password'>"
        "<button type='submit'>Save and connect</button></form>"
        "<small>Use a 2.4 GHz network. The camera restarts after saving.</small>"
        "</section></main></body></html>";

    setupServer.send(200, "text/html", page);
  });

  setupServer.on("/save", HTTP_POST, []() {
    const String newSsid = setupServer.arg("ssid");
    const String newPassword = setupServer.arg("password");

    if (newSsid.isEmpty()) {
      setupServer.send(400, "text/plain", "Wi-Fi name is required.");
      return;
    }

    wifiPreferences.begin("talos-wifi", false);
    wifiPreferences.putString("ssid", newSsid);
    wifiPreferences.putString("password", newPassword);
    wifiPreferences.end();

    setupServer.send(
        200,
        "text/html",
        "<!doctype html><meta name='viewport' content='width=device-width'>"
        "<body style='background:#0d1110;color:#eef5f1;font-family:Arial;"
        "padding:32px'><h2>Wi-Fi saved</h2>"
        "<p>The camera is restarting. Reconnect to the selected Wi-Fi, then "
        "use the Router UI address shown in Serial Monitor.</p></body>");

    restartRequested = true;
    restartAt = millis() + 1800;
  });

  setupServer.begin();
  Serial.printf("Wi-Fi setup page: http://%s:82/\n",
                WiFi.softAPIP().toString().c_str());
}

void startNetworks() {
  wifiPreferences.begin("talos-wifi", true);
  activeSsid = wifiPreferences.getString("ssid", "");
  activePassword = wifiPreferences.getString("password", "");
  wifiPreferences.end();

  // Join the router first. Starting the AP before this connection can leave
  // clients associated on a stale channel when the STA changes channels.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setHostname("talos-camera");

  bool routerConnected = false;
  if (!activeSsid.isEmpty()) {
    Serial.println("Trying saved Wi-Fi");
    routerConnected =
        connectToRouter(activeSsid, activePassword, 15000);
  }

  const String fallbackSsid(fallbackRouterSsid);
  const String fallbackPassword(fallbackRouterPassword);
  const bool fallbackDiffers =
      activeSsid != fallbackSsid || activePassword != fallbackPassword;

  if (!routerConnected && fallbackDiffers) {
    Serial.println("Trying S21 hotspot fallback");
    activeSsid = fallbackSsid;
    activePassword = fallbackPassword;
    routerConnected =
        connectToRouter(activeSsid, activePassword, 15000);
  }

  if (!routerConnected) {
    Serial.println(
        "No router connection; use the TALOS Wi-Fi setup page");
  }

  // The direct AP bypasses routers that isolate Wi-Fi clients. It is created
  // only after the STA channel is final, so clients do not keep stale leases.
  const uint8_t apChannel =
      WiFi.status() == WL_CONNECTED ? WiFi.channel() : 1;
  WiFi.mode(WIFI_AP_STA);
  const bool apConfigured =
      WiFi.softAPConfig(directApIp, directApIp, directApMask);
  const bool apStarted =
      WiFi.softAP(directApSsid, directApPassword, apChannel, false, 4);

  if (apConfigured && apStarted) {
    Serial.printf("Direct camera Wi-Fi: %s (channel %u)\n",
                  directApSsid, apChannel);
    Serial.printf("Direct camera URL: http://%s/\n",
                  WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("WARNING: direct camera Wi-Fi failed to start");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Router Wi-Fi connected: %s\n",
                  WiFi.localIP().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());

    if (MDNS.begin("talos-camera")) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("Camera hostname: http://talos-camera.local/");
    } else {
      Serial.println("WARNING: mDNS hostname could not be started");
    }

    // A client connected to TALOS-CAMERA-DIRECT can retain internet access.
    naptEnabled = WiFi.AP.enableNAPT(true);
    Serial.printf("Direct AP internet sharing: %s\n",
                  naptEnabled ? "enabled" : "unavailable");
  } else {
    Serial.println("Router unavailable; direct camera Wi-Fi remains active");
  }

  startSetupServer();
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  delay(400);

  Serial.println();
  Serial.println("TALOS low-latency OV3660 camera");
  Serial.printf("Setup core: %d\n", xPortGetCoreID());

  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  // Stable low-latency defaults. The web UI can still select a larger frame.
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 30;
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  if (!psramFound()) {
    Serial.println("WARNING: PSRAM not detected; using one DRAM frame buffer");
    config.fb_count = 1;
    config.jpeg_quality = 24;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_DRAM;
  } else {
    Serial.printf("PSRAM detected: %u bytes free\n", ESP.getFreePsram());
  }

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor == nullptr) {
    Serial.println("Camera sensor handle is unavailable");
    return;
  }

  if (sensor->id.PID == OV3660_PID) {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 0);
    sensor->set_saturation(sensor, -1);
  }

  sensor->set_framesize(sensor, FRAMESIZE_QVGA);
  sensor->set_quality(sensor, 30);
  sensor->set_whitebal(sensor, 1);
  sensor->set_awb_gain(sensor, 1);
  sensor->set_exposure_ctrl(sensor, 1);
  sensor->set_gain_ctrl(sensor, 1);
  sensor->set_lenc(sensor, 1);

#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  startNetworks();
  startCameraServer();

  Serial.println();
  Serial.printf("Direct UI: http://%s/\n",
                WiFi.softAPIP().toString().c_str());
  Serial.printf("Direct stream: http://%s:81/stream\n",
                WiFi.softAPIP().toString().c_str());

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Router UI: http://%s/\n",
                  WiFi.localIP().toString().c_str());
    Serial.printf("Router stream: http://%s:81/stream\n",
                  WiFi.localIP().toString().c_str());
  }
}

void loop() {
  static uint32_t lastStatusAt = 0;
  static uint32_t lastReconnectAt = 0;

  if (WiFi.status() == WL_CONNECTED && !naptEnabled) {
    naptEnabled = WiFi.AP.enableNAPT(true);
  }

  if (WiFi.status() != WL_CONNECTED && naptEnabled) {
    WiFi.AP.enableNAPT(false);
    naptEnabled = false;
  }

  if (WiFi.status() != WL_CONNECTED &&
      !activeSsid.isEmpty() &&
      millis() - lastReconnectAt >= 15000) {
    lastReconnectAt = millis();
    WiFi.disconnect();
    WiFi.begin(activeSsid.c_str(), activePassword.c_str());
  }

  setupServer.handleClient();

  if (restartRequested &&
      static_cast<int32_t>(millis() - restartAt) >= 0) {
    ESP.restart();
  }

  if (millis() - lastStatusAt >= 10000) {
    lastStatusAt = millis();
    Serial.printf(
        "STA %s | IP %s | RSSI %d dBm | AP clients %u | Heap %u | PSRAM %u\n",
        WiFi.status() == WL_CONNECTED ? "online" : "offline",
        WiFi.status() == WL_CONNECTED
            ? WiFi.localIP().toString().c_str()
            : "0.0.0.0",
        WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -127,
        WiFi.softAPgetStationNum(),
        ESP.getFreeHeap(),
        ESP.getFreePsram());
  }

  delay(100);
}
