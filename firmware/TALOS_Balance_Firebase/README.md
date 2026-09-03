# TALOS Balance + Firebase Firmware

## Fixed ESP32 Connections

| Device | ESP32 pin |
|---|---|
| MPU6050 SDA | GPIO21 / D21 |
| MPU6050 SCL | GPIO22 / D22 |
| Left SM-S4315R wheel signal | GPIO25 / D25 |
| Right SM-S4315R wheel signal | GPIO26 / D26 |
| Left MG996R leg signal | GPIO27 / D27 |
| Right MG996R leg signal | GPIO33 / D33 |

Connect the MPU6050 to ESP32 `3V3` and `GND`. Power the servos only from their
regulated external supplies. Join every converter `OUT-`, servo ground,
MPU6050 ground, and ESP32 ground at `GND_COMMON`.

## Arduino IDE Setup

1. Select **NodeMCU-32S** as the board.
2. Install **ESP32Servo** and **ArduinoJson** using Library Manager.
3. Open `TALOS_Balance_Firebase.ino`.
4. Replace `WIFI_SSID` and `WIFI_PASSWORD` with a 2.4 GHz Wi-Fi network.
5. Upload, then open Serial Monitor at `115200` baud.

The sketch was compiled with ESP32 core `3.3.10`, ESP32Servo `3.2.1`, and
ArduinoJson `7.4.3`.

## Safe First Test

1. Raise the robot so both wheels can rotate freely.
2. Disconnect the leg linkages, then adjust `LEG_LEFT_CENTER_US` and
   `LEG_RIGHT_CENTER_US` until the horns are at the mechanical center.
3. Find the exact stopped pulse for each continuous servo and set
   `WHEEL_LEFT_NEUTRAL_US` and `WHEEL_RIGHT_NEUTRAL_US`.
4. Keep leg assist off. Tilt the robot by hand and confirm both wheels drive
   toward the fall. Correct direction constants before ground testing.
5. Place the robot upright, allow the three-second startup calibration, then
   press **Arm balance** in the dashboard or send `A` in Serial Monitor.
6. Keep a hand near the power switch. A tilt over 35 degrees disarms the robot.

## Tuning Order

1. Wheel neutral pulses
2. Wheel direction and IMU sign constants
3. `BALANCE_ANGLE_OFFSET_DEG`
4. `KP`
5. `KD`
6. `KI` last, using a small value
7. Leg center pulses and mirrored leg direction
8. Leg-assist gain and maximum assist movement

Start with `KI = 0`. Increase `KP` until the wheels react strongly enough,
then increase `KD` to reduce oscillation. Adjust the balance offset only after
the robot consistently leans in one direction.

## Dashboard Controls

- **Forward/backward/left/right** change the balance lean or turn request.
- **Stop** cancels movement while allowing stationary balancing to continue.
- **Stop & disarm** immediately commands neutral wheel pulses and disables PID.
- **Wheel command** limits lean/turn effort. It is not measured wheel RPM.
- **Leg bend** sends a symmetric MG996R posture target.
- **Leg movement speed** controls how quickly the target pulse is approached.
- **Automatic assist** adds a small tilt-based leg correction after wheel
  balancing is working.

Exact wheel speed requires encoders; continuous-rotation servo pulses are open
loop and can vary with load, battery voltage, and individual servo calibration.

## Prototype Firebase Security

The current Realtime Database rules allow the ESP32 to read commands and write
telemetry without a device token. The firmware also uses TLS without certificate
validation to fit a classroom prototype. Do not expose this configuration as a
production or safety-critical robot controller.
