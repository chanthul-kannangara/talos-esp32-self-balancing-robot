# TALOS: preliminary CAE and offline simulation

Assessment date: 2026-10-11. This package contains real native-CAD measurements, a solved screening solid FEA, engineering calculations, and an executable MuJoCo/Gymnasium starter. It is **not** a validated digital twin, trained policy, complete joint/contact FEA, or uploadable balance firmware. No robot connection, motion, firmware upload, or website deployment is performed by these scripts.

## What changed after your clarifications

- Most joint bolts are M4 socket-head bolts, rotating directly against PLA. This is not a bearing specification or a bolt strength grade. Actual bolt fit, thread location, clamp preload, friction and wear need measurement.
- Your earlier firmware's `stand` command holds the leg posture without a support. A separate command attempts balance, which still fails. Preserve that useful leg-hold baseline; standing/holding is not evidence that dynamic balancing works.
- The two AS5600 sensors are **hip/leg encoders**, not wheel encoders. A controller cannot assume measured wheel speed from them.
- Servo supplies are 6 V; PLA is 50% infill, 0.16 mm layers. Total robot mass, wall count, print orientation and whole-robot CoM are still unknown. Numerical 2 kg results are examples, not measurements.

## Read first

Results, plots, extracted STEP geometry and models are in `outputs/talos-cae-2026-10-11/`. The original standalone report and complete assessment archive are preserved locally; this public source subset omits the redundant ZIP and intermediate FEA meshes. See `docs/PUBLICATION.md` for included artifacts and preservation-audit limits. Run new experiments in a copied output folder so the dated evidence stays intact.

Priority checks before further unsupported powered tests:

1. Support the body securely, isolate servo power and keep hands out of pinch points. Never force/back-drive a resistant servo gearbox.
2. Measure the assembled mass, actual ground-to-driven-hip height, printed pivot bores, bolt shanks, bearing engagement, and lateral play. The native knee bore is about 4.95 mm; **if** M4 is used there without a sleeve, the nominal diameter difference is 0.95 mm before print tolerances.
3. Use a properly dimensioned smooth shoulder/pin and sleeve/bushing or bearing, with a spacer/locking arrangement that retains the joint without squeezing the rotating PLA faces. Do not simply leave structural bolts loose or prescribe steel-joint tightening torque to PLA. Check available wall/edge distances before changing any bore.
4. Correct weak-field AS5600 status across the full travel, then capture each side's real angle/height references. Do not bypass sensor fault gates or burn OTP calibration.
5. Record servo voltage **at the servo connector**, converter input voltage, current, PWM and stop reason together. Stop for heating, binding, collapsing rails or resets. Never intentionally stall a servo for a long test.

## Reproduce offline, Windows / PowerShell

From the **extracted package root**, use a dedicated Python environment and an OpenGL-capable driver for MuJoCo rendering:

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools\cae\requirements.txt
.\.venv\Scripts\python.exe tools\cae\analyse.py
.\.venv\Scripts\python.exe tools\cae\joints.py
.\.venv\Scripts\python.exe tools\cae\fea.py
.\.venv\Scripts\python.exe tools\cae\simulation.py --height-series
.\.venv\Scripts\python.exe tools\cae\talos_env.py
.\.venv\Scripts\python.exe tools\cae\report.py
```

Pinned versions are those actually used for this assessment. Availability on other Python/platform combinations is not guaranteed. FEA takes longer than the other scripts and needs memory for about 96,000 tetrahedra. Report typography falls back to Helvetica if Windows Segoe UI fonts are absent. Poppler is needed only for PDF raster QA, not for generating the report.

The extracted JSON, two STEP files and bundled `simulation/source-assembly.glb` allow reruns without SolidWorks or the original CAD folder. Re-extraction is optional and needs installed SolidWorks, the original assembly and interop DLL. `extract-native-cad.ps1` opens documents read-only and checks before/after source hashes. Its default paths are the original laptop's; use `-CadRoot` and `-OutputRoot` on another machine. Read its source before running; it opens/closes its own SolidWorks session. Original CAD files were not edited.

## Offline training study (12 October 2026)

`outputs/talos-training-2026-10-12/README.md` records actual local PPO training, final fresh-seed tests, saved policies/normalization, traces and labelled MuJoCo replays. Across four stages/branches, 393,216 training transitions were executed. The final candidate survived 22 of 24 ten-second cases but passed zero full balance/posture accuracy tests: it relied on the model's soft hip limits and did not hold its leg target. No weights or simulator gains were transferred to ESP32, and no firmware or hardware was changed. Use `training_session.py --help` and `training_audit.py` for bounded, offline-only reproduction; the source CAD and dated assessment remain preserved. Survival is not successful height control or hardware validation.

## Interactive model

### Photo-informed M4/M3 extension, 2026-10-12

`hardware_simulation.py` creates a separate model in `outputs/talos-pivot-model-2026-10-12/`, leaving this dated assessment and its report/ZIP unchanged. It places the annotated M4/M3 pins on native CAD bore axes, fixes retaining-clip ownership, and aligns the wheel-servo output with the wheel hub. See that output's README and `hardware_inputs.json` for schematic dimensions and explicit physics limits. The pins/cases are zero-mass, non-colliding visuals; this is not a bolt-contact model or hardware-validated digital twin.

```powershell
.\.venv-mujoco\Scripts\python.exe tools\cae\hardware_simulation.py
```

```powershell
.\.venv\Scripts\python.exe tools\cae\simulation.py --height-m 0.18 --view
```

This regenerates the active model with an initial **left driven-hip height** of 180 mm and opens an **uncontrolled** physics viewer. It is not a successful balance demonstration; it will fall. The visual head and link meshes come from the user's CAD, but the tires/collisions are primitives. The native assembly has mismatched/unconstrained pivots, so the planar four-bar closure has been reconstructed, not certified. The 145-210 mm command window and 25-60 degree joint limits are provisional simulation choices, not approved physical travel limits.

`--mass 2.3` changes the assumed total model mass; it does not weigh or identify the real robot. `--step-mm 10` adds an obstacle for offline investigation; it does not establish climbing ability. Regenerate with default parameters and rerun verification/report before interpreting the delivered default results.

The dated report narrative is specific to the default 2 kg, 6 V, 85 mm, q50 assessment, not an automatic report for arbitrary redesigned robots. Its generator refuses different main assumptions or a model changed since validation; update the narrative and re-verify calculations if preparing a different assessment.

## Optional training dependencies

The 11 October assessment was untrained. The separate 12 October study used CPU PyTorch 2.14.1 and Stable-Baselines3 2.9.0. After installing `requirements.txt`, install those optional dependencies for policy evaluation or new offline training:

```powershell
.\.venv-mujoco\Scripts\python.exe -m pip install torch==2.14.1 --index-url https://download.pytorch.org/whl/cpu
.\.venv-mujoco\Scripts\python.exe -m pip install stable-baselines3==2.9.0 imageio==2.38.1 imageio-ffmpeg==0.6.0
.\.venv-mujoco\Scripts\python.exe tools\verify-publication.py
```

Training exposes four normalized **continuous-servo speed** actions. The 21 observations contain estimated gravity, gyro, accelerometer, two quantized hip angles/rates, prior actions, target hip angles and motion commands. Wheel speed and simulator base velocity are not supplied to the actor; rewards/termination may use simulator truth. Randomization is deliberately limited and does not cover all actual joint friction, backlash, tire contact, supply sag, BMS trips or thermal behavior.

The delivered untuned baseline fell in all five diagnostic episodes after 0.58-0.64 s. The later PPO study executed real training but none of its candidates passed the full balance/posture gate. First identify the real actuator/neutral/lag behavior and mass/CoM, fit the model, then evaluate held-out heights, loads, delays and pushes. Offline training alone does not authorize exporting to ESP32. There is no serial, Wi-Fi, real robot actuator interface or auto-start in this package.

## Scope of evidence

- Native CAD: 13 structural components, 5 recorded mates, unchanged source hashes. Hardware, battery and actual tires are absent from the structural CAD; native assigned density/mass is not the assembled robot's mass.
- FEA: three mesh levels, force/moment/energy/patch checks; assumed isotropic 1.5 GPa material. The 4.89 MPa boundary peak is mesh-sensitive and is not a strength certification for 50% infill PLA.
- Saved firmware evidence identifies V17 uploads on today's saved records. It does not identify the exact earlier working source from a verbal description. A pre-upload COM8 flash backup exists in the original service folder; it is deliberately not restored or distributed in this package because it may include credentials/calibration. No live ESP readback occurred during this CAE task.
- Video titles/author project pages and repositories were checked; complete videos/transcripts were not independently reviewed. The supplied pasted summaries are contextual references, not independently verified experimental data.
- `sources.json` contains the component and primary project references, while the report separates source specifications, user statements and calculated scenarios.

Do not solve a wheel-stop fault by raising the fall cutoff, removing encoder interlocks, increasing servo voltage beyond its specification or enabling unattended lying-down recovery. First prove safe low-height balance under a catch frame, then slow calibrated height transitions, then contact/obstacle behavior.
