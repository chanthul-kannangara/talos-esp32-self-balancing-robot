# TALOS Camera Relay

The ESP32 camera serves an MJPEG stream on the local network. A page hosted at
`https://talosone.web.app` cannot embed that private HTTP stream directly. This
relay fetches the local stream and is designed to be exposed through an HTTPS
tunnel running on the same computer as the camera network.

## 1. Upload and test the ESP32-S3 OV3660 camera locally

After upload, copy the IP address shown in the Serial Monitor. The local camera
stream must work first at:

`http://CAMERA_IP:81/stream`

For the GOOOUUU ESP32-S3-CAM V1.5 board, use Arduino's
`CameraWebServer` example with `CAMERA_MODEL_ESP32S3_EYE`. This selects the
board's OV3660 wiring: XCLK 15, SIOD 4, SIOC 5, D0-D7 on
11/9/8/10/12/18/17/16, VSYNC 6, HREF 7, and PCLK 13.

## 2. Start the complete dashboard relay

Run:

```text
tools\start-camera-stream.cmd
```

Enter the IP address printed by the camera. The helper starts the local relay,
opens a protected Cloudflare HTTPS tunnel, and writes the generated HTTPS
stream URL to Firebase Realtime Database.

The camera should then appear in the Camera panel at:

`https://talosone.web.app`

Firebase stores the URL and dashboard state; it does not carry the video bytes.
The computer running the relay and tunnel must remain on while the camera is
streaming.
