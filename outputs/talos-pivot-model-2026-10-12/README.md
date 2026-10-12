# TALOS: photo-informed M4/M3 pivot model

Date: 2026-10-12. Separate extension of the 2026-10-11 preliminary CAE model. The original CAD, report, assessment models, and assessment ZIP remain unchanged. This extension does not flash firmware, connect to the robot, move hardware, or train a controller.

## What the photographs establish

Both legs use the same connection pattern. The user identifies the large linkage pins as M4, the small support-retainer pin as M3, and confirms that both two-hole retaining clips stay aligned with their long links during bending. The latest wheel-removed photograph establishes that the wheel-servo case stays on the lower leg and its output shaft is at the wheel hub.

The leg-servo axis is the centre of the circular headphone housing. It is not the silver mid-arm M4. The wheel-servo axis is the wheel hub, not one of the linkage pins. Wheel-servo mounting screws retain the case and are not additional rotating mechanism joints.

## CAD-derived locations, on each side

Coordinates below are SolidWorks **part-local** millimetres, not measurements from photographs. Axes pass through the part thickness.

| Connection | CAD part-local axis centre (x, y) mm | Simulation connection |
|---|---:|---|
| Leg servo output | Upper link (0, 100) | Driven hip hinge |
| M4 knee | Upper link (0, 0); lower leg (0, -24.81) | Passive knee hinge |
| M4 follower | Support (0, -25.787564); lower leg (0, 0) | Existing closed-loop point connection |
| M4 passive support hip | Support (0, 77.725949) | Passive support hinge |
| M4 upper retaining clip | Upper link (0, 51.332348) | Pin/clip follows upper link |
| M3 support retaining clip | Support (0, -7.994051) | Pin/clip follows support link |
| Wheel servo output | Lower leg (0, -124.08) | Wheel hinge, coaxial output shaft |

This adds **8 M4 and 2 M3 annotated linkage pins** across both sides. It is not a complete robot fastener bill of materials. Each visual pin has a nominal-diameter shaft, head and schematic nut. Hidden head direction, nut count and exact grip length are not fully established by the photos.

## Corrected clip attachment

The older generator exported both retaining clips with the lower-leg body. The derived model instead registers the long clip to the upper link and the short clip to the support link, using their native hole pairs. Original clip component names/transforms do not reliably identify the correct assembly side, so the new registration uses native part geometry directly. No original CAD component is modified or stretched.

The upper clip's CAD pitch is 51.330000 mm versus 51.332348 mm between its upper-link holes. The support clip's CAD pitch is 17.790000 mm versus 17.793512 mm between its support-link holes. The maximum retained in-plane discrepancy is approximately **0.003513 mm**. This is a numerical CAD registration check, **not** claimed physical robot accuracy.

The actual four-bar joints retain relative rotation. Because the user confirmed that the two-hole clips stay aligned with their links, their retaining pins do not introduce extra independent mechanism degrees of freedom. The model still has four actuators: two leg drives and two wheel drives. The follower uses the existing MuJoCo point-closure constraint; no redundant extra hinge is inserted there.

## Wheel-servo representation

The case is fixed to the lower link; a shaft rotates with the wheel about the same axis. A separate wheel-removed render exposes this relationship. The case envelope uses the supplier's 20.5/39.5/42 mm dimensions, assigned to axes from the photographs. The eccentric output location, spline/adapter diameter, axial spacing and precise housing shape remain schematic. The existing 85 mm wheel diameter and illustrative lumped masses are retained.

Supplier: [SM-S4315R listing](https://tronic.lk/product/servo-motor-metal-wheel-360c-15kg-sm-s4315r). The listing contains mixed model text; its envelope is not a verified manufacturer mechanical drawing or proof of the installed unit's dimensions.

## Run and inspect

From the project root in PowerShell:

```powershell
.\.venv-mujoco\Scripts\python.exe tools\cae\hardware_simulation.py
.\.venv-mujoco\Scripts\python.exe tools\cae\talos_env.py --output outputs\talos-pivot-model-2026-10-12
& "$env:LOCALAPPDATA\Programs\MuJoCo\3.15.0\bin\simulate.exe" "$PWD\outputs\talos-pivot-model-2026-10-12\simulation\talos.xml"
```

The native simulator may start unpaused. Space toggles pause; Backspace resets. There is **no balance controller** in the native viewer: playing the default scene is an uncontrolled fall, not a successful balance test. Offline files `talos-h150.xml`, `talos-h180.xml` and `talos-h210.xml` provide three initial driven-hip heights. They are provisional simulation poses, not approved physical travel commands.

Editable assumptions are in `tools/cae/hardware_inputs.json`; a copy is saved beside this README. Regenerate after changes. Exact bolt lengths need an under-head measurement; the supplied photographs have no calibrated scale for reliable length inference.

## Verification and limits

`verification.json` records checks on both sides at 150, 180 and 210 mm initial left driven-hip heights: pin/axis collinearity, clip ownership and hole registration, wheel-output coaxiality, loop closure, unchanged actuator/DOF counts and unchanged illustrative mass/inertia. A 60-degree wheel-rotation check confirms that the case stays fixed to the lower link while the output shaft rotates with the wheel. A 1,000-step smoke test compares the extended model to the baseline dynamics; identical motion means the visual extension has not silently changed the assumed plant.

All added fasteners, clips and servo cases are **zero-mass, non-colliding visuals**, covered only by the previous illustrative lumped masses. They are not separate identified steel/servo masses. Pivot dynamics remain ideal hinges/point closure with the previous assumed damping. Bolt/PLA contact, clearance, preload, stiction, wear, structural stresses, supply sag and actual mass/CoM are **not** solved by this addition. Native assembly closure corrections and collapsed axial joint planes remain provisional; the illustrated head/nut/clip axial stack is not a contact-ready assembly.

`preserved_source_hashes.json` records the untouched original CAD and archived assessment files. `simulation/hardware_mapping.json` records the source components, CAD bore centres, local attachment frames and supplied photo filenames. `figures/pivot_axis_map.png` identifies every added pin and both servo axes. No physical motion or sensor readback is claimed.

The delivered model was compiled by the native MuJoCo 3.15.0 compiler, opened in the native viewer, visually inspected, paused and reset to its initial pose. The TALOS desktop/Start Menu shortcuts now select this separate model. The Gymnasium checker passes with the existing 21 observations and four actions; its infinite observation-range warnings remain nonfatal. All five untuned diagnostic controller episodes still fall after 0.58-0.64 seconds. This extension therefore makes no successful balance or training claim.

MuJoCo references: [kinematic tree and frames](https://mujoco.readthedocs.io/en/stable/modeling.html#kinematic-tree), [point-closure constraint](https://mujoco.readthedocs.io/en/stable/XMLreference.html#equality-connect), [geometry mass/inertia semantics](https://mujoco.readthedocs.io/en/stable/XMLreference.html#body-geom-mass).
