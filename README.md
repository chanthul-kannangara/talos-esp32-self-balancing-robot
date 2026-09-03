# TALOS: Two-Wheeled Self-Balancing Robot with Active Articulation

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/)
[![React: 18](https://img.shields.io/badge/Frontend-React_18-61dafb.svg)](https://react.dev/)
[![Vite: 5](https://img.shields.io/badge/Bundler-Vite_5-646CFF.svg)](https://vitejs.dev/)
[![Firebase: RTDB](https://img.shields.io/badge/Cloud-Firebase_RTDB-FFA611.svg)](https://firebase.google.com/)

**TALOS** is an open-source, two-wheeled self-balancing robot featuring active dual-servo leg articulation, deterministic real-time balancing via ESP32 FreeRTOS, and a responsive web teleoperation dashboard with low-latency live camera streaming.

Developed by **Chanthul Kannangara** at NSBM Green University (Mechatronics Engineering).

---

## Key Features

- **Deterministic 200 Hz Balance Loop**: Runs on **Core 1** of the ESP32 using FreeRTOS tasks to guarantee hard real-time balance calculations without network jitter.
- **Sensor Fusion & PID Stabilization**: Integrates an **MPU-6050** 6-axis IMU with a complementary filter for drift-free pitch estimation and high-frequency rate-damping PID control.
- **Active Leg Articulation**: Dual **MG996R** high-torque servos adjust ride height, lean trim, and dynamic posture compensation for stability across transitions.
- **Dual Continuous Servos**: SM-S4315R continuous-rotation servos deliver smooth differential drive balancing.
- **Live Video Streaming**: Dedicated **ESP32-CAM** (OV2640) streaming MJPEG video with low latency directly into the control UI via Node.js relay bridges.
- **Modern Web Teleoperation Dashboard**: Built with **React 18**, **Vite**, and **Tailwind CSS**, connecting via **Firebase Realtime Database** for live telemetry streaming, PID gain tuning, and virtual joystick control.

---

## System Architecture

```text
+-----------------------------------------------------------------------+
|                            USER INTERFACE                             |
|         React 18 + Vite Web Dashboard (https://talosone.web.app)       |
|  - Live Pitch/Roll/Status Telemetry       - Real-time PID Gain Tuner  |
|  - Virtual Joystick Control               - Embedded MJPEG Camera     |
+------------------------------------+----------------------------------+
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
├── LICENSE                                # MIT Open Source License
├── README.md                              # Main documentation & quickstart
├── package.json                           # React dashboard dependencies & scripts
├── vite.config.js                         # Vite build configuration
├── index.html                             # Web dashboard entry point
├── firebase.json                          # Firebase Hosting & RTDB rules configuration
├── database.rules.json                    # RTDB security rules
├── .env.example                           # Example environment configuration
├── src/                                   # Teleoperation Web Dashboard
│   ├── App.jsx                            # Dashboard root component
│   ├── components/                        # Telemetry gauges, PID panel, joystick, camera
│   ├── hooks/                             # Real-time state hooks
│   └── services/                          # Firebase real-time database interface
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

### 1. Flashing the Robot Controller
1. Open `firmware/TALOS_Balance_Firebase/TALOS_Balance_Firebase.ino` in Arduino IDE (or PlatformIO).
2. Install required libraries:
   - `ESP32Servo` by Kevin Harrington & John K. Bennett
   - `ArduinoJson` (v6 or v7) by Benoit Blanchon
3. Update Wi-Fi credentials in lines 77-78:
   ```cpp
   const char *WIFI_SSID = "YOUR_WIFI_SSID";
   const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   ```
4. Select board **ESP32 Dev Module** and upload via USB.

### 2. Flashing the Camera Streamer
1. Open `firmware/ESP32_CAM_WebServer/CameraWebServer_working.ino`.
2. Select board **AI Thinker ESP32-CAM**.
3. Configure your local Wi-Fi credentials and upload.
4. Note the IP address printed to Serial Monitor at boot.

### 3. Running the Teleoperation Dashboard
```bash
# Clone the repository
git clone https://github.com/Chanthul/talos-esp32-self-balancing-robot.git
cd talos-esp32-self-balancing-robot

# Install dependencies
npm install

# Start local development server
npm run dev
```
Open `http://localhost:5173` in your browser.

---

## PID Tuning & Balancing Sequence

1. **Bench Calibration**: Place the robot on a stand with wheels clear of the floor. Ensure leg assist is off.
2. **Servo Neutral Finding**: Adjust `WHEEL_LEFT_NEUTRAL_US` and `WHEEL_RIGHT_NEUTRAL_US` until servos are completely stationary when disarmed.
3. **Angle Zero Offset**: Level the chassis to the physical balance point and calibrate `BALANCE_ANGLE_OFFSET_DEG`.
4. **Proportional Gain ($K_p$)**: Increment $K_p$ until the robot responds briskly to tilt without oscillating violently.
5. **Derivative Gain ($K_d$)**: Increase $K_d$ to dampen overshoot using gyro angular velocity feedback.
6. **Integral Gain ($K_i$)**: Apply a small $K_i$ only to eliminate steady-state drift once $K_p$ and $K_d$ maintain upright balance.

---

## Author & License

Developed by **Chanthul Kannangara**  
NSBM Green University — Mechatronics Engineering  
[LinkedIn Profile](https://www.linkedin.com/in/chanthul-kannangara-4554a22b3/)

Released under the **[MIT License](LICENSE)**.
