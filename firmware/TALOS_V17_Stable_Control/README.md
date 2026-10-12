# TALOS V17 — supported commissioning firmware

Open this folder in VS Code with the PlatformIO extension. COM8 was the development computer's port; select your own ESP32 serial port. Building alone does not require a robot connection.

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run --target upload --upload-port COM8
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor --port COM8 --baud 115200
```

Close any serial monitor before uploading. Upload does not erase NVS: saved Wi-Fi profiles, endpoint references, and supported balance/cutoff settings remain. Do not use erase-flash. Recheck the reported settings after uploading.

## Hardware

| Device | ESP32 pins |
| --- | --- |
| MPU6050 and left AS5600 | SDA 21, SCL 22 |
| Right AS5600 on second I2C bus | SDA 16, SCL 17 |
| Left / right leg continuous-rotation servos | 26 / 25 |
| Left / right wheel continuous-rotation servos | 27 / 33 |

The MPU/wheel calculation target is 500 Hz; the leg feedback target is 100 Hz. All servo PWM remains 50 Hz. Faster calculation cannot make a 50 Hz servo accept 500 independent commands per second. The AS5600 sensors measure leg joints, not wheel speed or ground displacement. Physical center-of-mass location is not measured by the MPU or by body-height endpoints.

## Verified on COM8, 2026-10-11

- PlatformIO build and flash completed successfully; flash hashes verified during upload.
- Measured control rate approximately 500 Hz, MPU readable, all four outputs neutral at 1500 microseconds.
- Manual, servo-power-off bends decreased both encoder readings. The left extension-coordinate mapping was corrected to positive, matching the observed direction. Motor/encoder feedback polarity still requires a supported powered check.
- Both AS5600 sensors reported magnet detected and weak magnetic field (typically status 0x37; 0x33 also observed). Firmware deliberately blocks leg actuation when the magnetic field is outside the sensor's range. Raw angle movement does not establish reliable angular accuracy.
- Connected to a saved Wi-Fi profile, three saved profiles retained, Firebase HTTP 200 and live V17 status verified. The network credential is stored on the ESP32, not added to this source project or sent to Firebase.
- Both height endpoints remain uncalibrated. No powered angle test, free-balancing test, obstacle climb, or self-righting test has passed in this session.

## Required next steps

1. Keep the chassis securely supported, wheels clear, hands/wires away from linkages. Keep servo power OFF while adjusting magnets. Use correctly oriented diametric magnets centered over each AS5600; check air gap and mounting. Send `magnet-status`. Both sensors must show MD=1, weak=0, strong=0 consistently throughout their travel. Being physically close alone does not establish field quality.
2. With all actuators OFF, place both legs at their actual fully extended endpoint. Send `capture-extended`. Do not capture a hanging or partly bent pose as fully extended. The command samples both encoders and saves the reference only on successful sampling.
3. Enable supported leg-only hold using `bench-hold`. Wheels remain off. Request `bend 2 2 2` for 2 degrees on each side at 2 degrees/second. Start small and observe direction, error, and pulses using `pose`. Send `stop` immediately if motion is reversed or tracking worsens. A 15-second timeout and growth guard stop failed bench trajectories. Until both endpoints are calibrated, bend commands are limited to 8 degrees and bench pulses to 1500 ±120 microseconds.
4. Stop all actuators, switch servo power OFF, position both legs fully bent without forcing a stop, then capture `capture-bent`. The 16.2 cm and 29.2 cm values are the user's mechanical body-height references. The interpolation between them is an estimate, not measured linkage forward kinematics. Calibration must agree with the observed encoder extension directions and have 5–160 degrees of travel on each side.
5. Hand control to the website with `remote`. In Posture, enable leg-only hold, choose left/right bend angles and joint speed, then Apply. No move is sent by dragging a slider. Control and Posture retain all 13 original Assem1 CAD components and consume measured encoder bends, not target sliders. Invalid encoders hold reference/last valid legs; healthy IMU attitude continues independently. Offline data freeze the view. The linkage pivots and extended-angle registration are inferred from the export, not a validated mechanical simulation; check their alignment against the physical robot. Wheels are illustrative and never spin as if leg encoders measured wheel speed.
6. Only after supported joint tracking is verified, test low-height balance on a clear floor with a tether/support and power cutoff. Capture `stand` at the desired supported posture; `hold` retains it. `zero` explicitly captures a still, upright balance reference with all actuators OFF. `arm-saved` requires leg hold, healthy sensors, low gyro rates, and upright attitude. From the website, use a fresh OFF then ON request. Boot never blindly arms from a retained command.

Balance gains and motor directions still need physical tuning. Begin at a low stance. Do not assume higher body height always requires higher wheel speed; this firmware interpolates empirical gain/authority limits only when calibrated. Normal leg support tapers to neutral at target and includes velocity damping, so it does not continuously force full extension. Height trajectories pause when tracking or tilt conditions are poor.

Compact cloud pose updates are requested at a nominal 200 ms interval even while disarmed. Actual remote update cadence depends on Wi-Fi and HTTP latency; it is not 500 Hz. The onboard balance controller remains independent of website rendering.

## Fast USB 3D feedback

The website's Control and Posture views offer **Connect USB** in desktop Chrome/Edge. Close serial monitors/bridges, keep the body supported with servo power OFF, and select the ESP32 on COM8. Opening a serial port may reset the ESP32. The browser requests a view-only 25 Hz stream at 115200 baud; it performs encoder-angle conversion, CAD linkage calculations and rendering locally at up to 60 fps. Drive commands still use the existing cloud controls.

`pose-stream on` starts or renews a three-second stream lease. Repeat once per second to keep it active. `pose-stream off` stops the stream. These commands do not arm motors, change ownership or alter calibration. Frames contain `talosPose:1`, boot ID, sequence, device timestamp, pitch/roll/rates, raw 12-bit encoder readings, magnetic status and ages, extended references/signs, four pulse outputs, health/mode flags, measured control frequency and PID output. The sender skips output when the TX buffer is busy instead of waiting for telemetry space.

Verified after this upload: 200 packets at 25.02 Hz, no sequence gaps, 40.01 ms median device interval, 500 Hz reported control calculation rate, every output 1500 microseconds and all actuator modes off. This verifies the serial stream, not end-to-end browser latency or mechanical accuracy.

Both encoders still report weak fields and both height endpoints remain uncalibrated. The browser offers an explicitly untrusted manual preview: place both legs at their true extended endpoint with servo power OFF, then **Set visual extended reference**. This browser-only reference does not save controller calibration or relax any firmware gate. It expires on page reload or robot reboot. Correct the magnets before powered tests.

## Commands

`stop` / `disarm` / `legs-off`: neutralize all four servos. `local` selects serial ownership; `remote` returns ownership to the dashboard and stops first. `status` prints state/settings; `pose` returns compact JSON; `magnet-status` prints sensor field flags; `wifi-status` prints connection state without passwords.

`stand` captures the current supported leg position without redefining upright pitch. `hold` engages the captured position. `bench-hold` engages leg-only hold at the current position; `bend <left> <right> <deg/s>` requests bend relative to the extended reference. Speed must be 1–15 degrees/second. `capture-bent` / `capture-extended` require all actuators OFF. `zero` calibrates upright pitch; `imu-reset` recalibrates stationary gyro biases. `set` changes validated parameters while safe; `save` persists calibration and supported runtime balance settings.

`wifi-add {"ssid":"YOUR_2.4_GHZ_NETWORK","password":"YOUR_PASSWORD"}` adds/tests a local USB provisioning request on the network task. Existing profiles are preserved. Credentials are case-sensitive and never echoed or sent to Firebase. Do not publish credentials in code or command logs. Adding/changing networks requires every actuator OFF.

The Wi-Fi setup portal remains available in setup mode at 192.168.4.1 using the existing TALOS setup network. Five saved network profiles, dashboard cutoff options, endpoint calibration, and existing Wi-Fi controls remain supported.

## Rollback and evidence

Full-device backups and raw service captures remain private because they may contain Wi-Fi credentials and calibration. They are excluded from this repository. The published source contains no saved network password.

No autonomous lying-down recovery or obstacle-climbing routine is enabled. Those require verified joint travel, stable low-stance balance, actuator capability measurements, and suitable contact/obstacle sensing. Do not test those behaviors on this uncommissioned controller.
