# TALOS: Two-Wheeled Self-Balancing Robot with Active Articulation & WebMCP

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![WebMCP: 1.0](https://img.shields.io/badge/WebMCP-1.0_Standard-10b981.svg)](https://talosone.web.app/webmcp.json)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/)
[![React: 18](https://img.shields.io/badge/Frontend-React_18-61dafb.svg)](https://react.dev/)
[![Vite: 5](https://img.shields.io/badge/Bundler-Vite_5-646CFF.svg)](https://vitejs.dev/)
[![Firebase: RTDB](https://img.shields.io/badge/Cloud-Firebase_RTDB-FFA611.svg)](https://firebase.google.com/)

**TALOS** is an open-source, two-wheeled self-balancing robot featuring active dual-servo leg articulation, deterministic real-time balancing via ESP32 FreeRTOS, a responsive web teleoperation dashboard with low-latency live camera streaming, and native **WebMCP (Web Model Context Protocol)** support for autonomous AI agent control.

- **Live Dashboard**: [https://talosone.web.app/](https://talosone.web.app/)
- **WebMCP Tool Manifest**: [https://talosone.web.app/webmcp.json](https://talosone.web.app/webmcp.json)

Developed by **Chanthul Kannangara** at NSBM Green University (Mechatronics Engineering).

---

## Key Features

- **Deterministic 200 Hz Balance Loop**: Runs on **Core 1** of the ESP32 using FreeRTOS tasks to guarantee hard real-time balance calculations without network jitter.
- **Sensor Fusion & PID Stabilization**: Integrates an **MPU-6050** 6-axis IMU with a complementary filter for drift-free pitch estimation and high-frequency rate-damping PID control.
- **Active Leg Articulation**: Dual **MG996R** high-torque servos adjust ride height, lean trim, and dynamic posture compensation for stability across transitions.
- **Dual Continuous Servos**: SM-S4315R continuous-rotation servos deliver smooth differential drive balancing.
- **Live Video Streaming**: Dedicated **ESP32-CAM** (OV2640) streaming MJPEG video with low latency directly into the control UI via Node.js relay bridges.
- **Native WebMCP 1.0 Agent Support**: Full compliance with the **W3C Web Model Context Protocol** specification (`navigator.modelContext`), exposing 11 high-level structured tools for browser AI agents to query telemetry, command motion, tune PID, and articulate legs.
- **Modern Web Teleoperation Dashboard**: Built with **React 18**, **Vite**, and **Tailwind CSS**, connecting via **Firebase Realtime Database** for live telemetry streaming, PID gain tuning, and virtual joystick control.

---

## WebMCP (Web Model Context Protocol) Autonomous Agent Interface

TALOS natively implements the **W3C Web Machine Learning CG WebMCP standard** directly in the web dashboard at [talosone.web.app](https://talosone.web.app/).

Autonomous AI agents (e.g. Chrome Model Context Extensions, OpenAI Operator, autonomous browser agents) can inspect and call the robot tools directly via `navigator.modelContext`:

```javascript
// Check available tools
const tools = navigator.modelContext.getTools();

// 1. Fetch live telemetry
const telemetry = await navigator.modelContext.executeTool("talos_get_telemetry");
console.log(`Current pitch: ${telemetry.orientation.pitchDeg}°, Battery: ${telemetry.battery.percentage}%`);

// 2. Command motion
await navigator.modelContext.executeTool("talos_motion_command", { direction: "forward" });

// 3. Articulate leg posture (lower ride height to 25% for stability)
await navigator.modelContext.executeTool("talos_set_leg_posture", { legPosition: 25, legSpeed: 80 });

// 4. Live PID tuning
await navigator.modelContext.executeTool("talos_tune_pid", { kp: 90, kd: 3.0 });

// 5. Emergency Stop
await navigator.modelContext.executeTool("talos_emergency_stop");
```

### Registered WebMCP Tools Catalog

| Tool Name | Parameters | Description |
|:---|:---|:---|
| `talos_get_telemetry` | *(none)* | Returns real-time pitch, roll, armed state, speed, battery, fall limit, and leg extension. |
| `talos_motion_command` | `direction`: `"stop" \| "forward" \| "backward" \| "left" \| "right"` | Commands directional drive while onboard FreeRTOS loop maintains balance. |
| `talos_set_speed` | `speed`: `0 - 100` | Adjusts drive throttle and motion effort percentage. |
| `talos_set_armed` | `armed`: `true \| false` | Arms or disarms the 200 Hz balancing control loop. |
| `talos_emergency_stop` | *(none)* | Instant hardware safety interlock: disarms all motors and stops all motion. |
| `talos_set_leg_posture` | `legPosition`: `0 - 100`, `legSpeed`: `0 - 100` | Articulates dual MG996R servos to adjust chassis ground clearance and ride height. |
| `talos_toggle_leg_assist` | `enabled`: `true \| false` | Toggles dynamic leg posture balancing compensation for large tilt disturbances. |
| `talos_tune_pid` | `kp`, `ki`, `kd`, `balanceTrimDeg`, `predictionTimeMs`, ... | Live-tunes PID parameters and wheel limits on ESP32 over Wi-Fi/Firebase. |
| `talos_set_fall_limit` | `limitDeg`: `30 \| 60 \| 80` | Sets pitch angle cutoff threshold for motor protection shutdown. |
| `talos_set_mode` | `mode`: `"manual" \| "balance-only" \| "camera-assist"` | Switches operational mode. |
| `talos_get_camera_feed` | *(none)* | Returns live ESP32-CAM MJPEG video stream URL and status. |

---

## System Architecture

```text
+-----------------------------------------------------------------------------------------+
|                                    USER & AGENT LAYER                                   |
|   Human User (React 18 Dashboard UI)  <======>  AI Agents (WebMCP: navigator.modelContext)|
+--------------------------------------------+--------------------------------------------+
                                             |
                                  Firebase RTDB / WebSocket
                                             |
                       +---------------------+---------------------+
                       |                                           |
        +--------------v---------------+           +---------------v---------------+
        |     ESP32 ROBOT CONTROLLER   |           |    ESP32-CAM STREAMER         |
        |  [Core 0]: Wi-Fi & Firebase  |           |  - AI-Thinker OV2640 Module   |
        |            Command Polling   |           |  - MJPEG Stream over HTTP     |
        |  [Core 1]: 200 Hz FreeRTOS   |           +---------------+---------------+
        |            Complementary +   |                           |
        |            PID Balance Task  |                           v
        +--------------+---------------+           +---------------+---------------+
                       |                           |    LOCAL CAMERA RELAY         |
          +------------+------------+              |  - Node.js (camera-relay.mjs) |
          |                         |              |  - Port 4174 WebSocket Proxy  |
          v                         v              +-------------------------------+
        [MPU-6050 IMU]      [Servos / Actuators]
        - I2C (GPIO 21/22)  - Left Wheel (GPIO 25)
                            - Right Wheel (GPIO 26)
                            - Left Leg (GPIO 27)
                            - Right Leg (GPIO 33)
```

---

## Hardware Pinout & Wiring

| Component | Pin / Signal | ESP32 GPIO | Description |
|:---|:---|:---|:---|
| **MPU-6050 IMU** | SDA | GPIO 21 | Primary I2C Data |
| **MPU-6050 IMU** | SCL | GPIO 22 | Primary I2C Clock |
| **MPU-6050 IMU** | VCC / GND | 3V3 / GND | Common 3.3V Logic Supply |
| **Left Wheel Servo** | PWM Signal | GPIO 25 | SM-S4315R Continuous Rotation |
| **Right Wheel Servo** | PWM Signal | GPIO 26 | SM-S4315R Continuous Rotation |
| **Left Leg Servo** | PWM Signal | GPIO 27 | MG996R High-Torque Positional |
| **Right Leg Servo** | PWM Signal | GPIO 33 | MG996R High-Torque Positional |
| **ESP32-CAM** | Camera Stream | Wi-Fi / IP | AI-Thinker OV2640 Streamer |

> **Power Notice**: Continuous and high-torque servos draw peak currents up to 2.5A. Never power servos directly from the ESP32 board. Always use dedicated external buck converters (e.g., LM2596) with a common ground (`GND_COMMON`).

---

## Repository Structure

```text
talos-esp32-self-balancing-robot/
├── LICENSE                                # MIT Open Source License (visible in About)
├── README.md                              # Main documentation & WebMCP specification
├── package.json                           # React dashboard dependencies & scripts
├── vite.config.js                         # Vite build configuration
├── index.html                             # Web dashboard entry point with WebMCP tags
├── firebase.json                          # Firebase Hosting & RTDB rules configuration
├── database.rules.json                    # RTDB security rules
├── .env.example                           # Example environment configuration
├── public/                                # Public Static Assets
│   └── webmcp.json                        # W3C WebMCP tool manifest for AI agent crawlers
├── src/                                   # Teleoperation Web Dashboard
│   ├── App.jsx                            # Dashboard root component & WebMCP integration
│   ├── components/
│   │   ├── WebMcpConsole.jsx              # Interactive WebMCP tool inspector & agent log
│   │   ├── Header.jsx                     # Topbar with WebMCP 1.0 status pill
│   │   ├── AttitudePanel.jsx              # Live pitch/roll artificial horizon
│   │   ├── ControlPanel.jsx               # Manual drive buttons & emergency stop
│   │   ├── ServoControls.jsx              # Active leg height articulation sliders
│   │   ├── SafetyControlPanel.jsx         # Live PID tuner & fall-angle selector
│   │   └── CameraPanel.jsx                # Low-latency MJPEG video player
│   ├── hooks/
│   │   ├── useWebMCP.js                   # WebMCP tool registration hook
│   │   └── useRobotDashboard.js           # Firebase state synchronization hook
│   └── services/
│       ├── webmcp.js                      # WebMCP W3C standard protocol polyfill
│       └── robotApi.js                    # Firebase RTDB command & telemetry interface
├── firmware/                              # Embedded Microcontroller Firmware
│   ├── TALOS_Balance_Firebase/            # Main ESP32 balancing firmware
│   │   ├── TALOS_Balance_Firebase.ino     # FreeRTOS 200Hz loop, MPU6050 filter, PID
│   │   └── README.md                      # Calibration and tuning instructions
│   └── ESP32_CAM_WebServer/               # ESP32-CAM Video Streamer
│       ├── CameraWebServer_working.ino    # Video stream server
│       ├── app_httpd.cpp                  # Stream handler
│       ├── camera_pins.h                  # Pin configuration for AI-Thinker OV2640
│       └── board_config.h                 # Clock & resolution configuration
└── tools/                                 # Bridge and Relay Utilities
    ├── camera-relay.mjs                   # Node.js MJPEG-to-WebSocket relay
    ├── firebase-status-writer.mjs         # Telemetry synchronization helper
    ├── robot-serial-bridge.ps1            # High-speed COM port bridge
    └── start-camera-stream.ps1            # Automated camera launcher
```

---

## Quickstart Guide

### 1. Running the WebMCP Teleoperation Dashboard
```bash
# Clone the repository
git clone https://github.com/stone35567/talos-esp32-self-balancing-robot.git
cd talos-esp32-self-balancing-robot

# Install dependencies
npm install

# Start local development server
npm run dev
```
Open `http://localhost:5173` in your browser. Open browser console and test:
```javascript
window.__WEBMCP__.getTools();
```

### 2. Flashing the Robot Controller
1. Open `firmware/TALOS_Balance_Firebase/TALOS_Balance_Firebase.ino` in Arduino IDE or PlatformIO.
2. Install required libraries (`ESP32Servo`, `ArduinoJson`).
3. Set your 2.4 GHz Wi-Fi credentials in lines 77-78.
4. Flash to ESP32.

---

## Author & License

Developed by **Chanthul Kannangara**  
NSBM Green University — Mechatronics Engineering  
[LinkedIn Profile](https://www.linkedin.com/in/chanthul-kannangara-4554a22b3/)

Released under the **[MIT License](LICENSE)**.
