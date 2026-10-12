# TALOS CAD and telemetry views

`talos-assembly.glb` is a read-only SOLIDWORKS export of the user's `Assem1.SLDASM` assembly. The original assembly and part files were hash-checked and left unchanged.

The export contains 13 structural components. Tire and rim geometry is not present in that assembly, so `src/components/RobotModel.jsx` adds illustrative wheels for the website preview and applies a matte display palette. These additions do not modify the CAD source and are not a mechanical simulation.

The login model's decorative motion supports pause, reduced-motion preferences, background/offscreen suspension, and an SVG fallback when WebGL is unavailable.

The Control balance monitor and Posture page lazy-load the same `RobotPoseModel.jsx`. Both retain every one of the original 13 CAD meshes, their geometry and original reference transforms. Neither substitutes box legs or deletes support links. A presentation wrapper recenters the assembly without changing the source export. A matte palette and two illustrative wheels are display-only additions.

`cadLegRig.js` articulates the five original components on each leg independently. Drive/support pivots are inferred from the exported part holes; a planar circle-intersection linkage preserves the reference component transforms. The native assembly's exported holes are not perfectly coincident (approximately 5–7 mm at the lower coupling), and glTF does not contain the SOLIDWORKS mates. Consequently, this is an inferred display rig, not a validated SOLIDWORKS constraint solution. Fully extended registration is estimated from maximum vertical wheel reach. Its angular offset, axis alignment, range and linkage motion must be checked against the physical robot during commissioning; matching geometry does not prove calibrated forward kinematics.

Trusted leg animation uses fresh, healthy raw encoders and the controller's captured extended reference. The browser computes each bend as `encoderSign * wrapDegrees(extendedReference - rawAngle)` independently; slider targets never drive the drawing. Missing calibration or bad magnetic fields hold the reference/previous legs by default. A separately enabled diagnostic preview uses a browser-only extended reference to show manual encoder changes, with an explicit untrusted label. It never unlocks powered controls. Browser references expire on reload or a changed robot boot identifier.

IMU pitch/roll update independently while the latest sample and connection are healthy. The CAD pitch rotation is negative MPU pitch, matching the user's observed mounting; the existing roll sign is retained. Historical IMU error counts do not freeze a recovered fresh sample. Offline/IMU-fault data hold the body. Unsolvable linkage poses retain the last feasible drawing and show a registration warning. No wheel spin or absolute yaw is invented: the AS5600 sensors are leg encoders and the MPU6050 has no absolute heading reference.

Interpolation runs on the viewing device, capped at 60 frames/second with a 25 ms smoothing time constant, and stops at convergence or while hidden/offscreen. Reduced-motion mode updates directly. CAD/WebGL failures show an explicit unavailable message. Direct USB Web Serial telemetry runs at 25 Hz; stale frames stop updating the drawing after approximately 250–350 ms. Firmware also publishes cloud pose updates while disarmed with a nominal 200 ms request interval; actual cloud cadence depends on HTTP/Wi-Fi latency. Onboard calculations remain independent at 500 Hz.

`node tools/validate-cad-rig.mjs` verifies all 13 structural meshes remain, original reference transforms/geometry are preserved, legs move independently, and restoration is exact. This validation is not a physical joint-tracking or balance test.

`tools/pose-preview.html` is a development-only synthetic fixture, not part of the production Vite build. It tests independent leg interpolation and offline freezing without sending any commands to the robot.
