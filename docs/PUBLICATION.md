# Public source snapshot — 12 October 2026

This update collects the current React/CAD/USB dashboard, V17 commissioning firmware, and dated offline engineering experiments in one repository. V17 controller and Wi-Fi-manager source files were copied without changing their control behavior. Repository preparation did not connect to a robot, upload firmware, or transfer trained weights to hardware.

## What is included

- Dashboard source, original 13-component GLB and CAD-rig check.
- V17 PlatformIO inputs and source; a public commissioning README with private network identifiers removed.
- Offline calculation, FEA, MuJoCo, Gymnasium and PPO scripts.
- Three dated evidence folders: extracted native-CAD JSON, two STEP parts, model XML/meshes, input assumptions, numerical results, plots and evaluation traces.
- Each training stage's `final.zip` and `normalization.pkl`, run configuration, progress and held-out results, plus the final branch's source snapshot. These are rejected experimental policies, not robot firmware.

The numerical records and model assets were retained as produced. Some dated records contain the original development machine's local source paths. Those paths are provenance, not required installation locations. New-analysis defaults use `public/models/talos-assembly.glb`; model generation also falls back to each output's bundled `simulation/source-assembly.glb` when the original path is absent.

## What is excluded

Private `.env` files, saved Wi-Fi credentials, full-device flash backups, raw service/serial captures, virtual environments, dependency folders, PlatformIO outputs and build products are excluded. Legacy camera/router credentials were replaced by an ignored local header and a public example; the older controller sketch uses placeholders. Choose a private camera AP password before deploying that separate sketch.

The redundant assessment ZIP, intermediate FEA `.msh` meshes/NPZ, native compiled MuJoCo `.mjb`, training checkpoints/best-model duplicates and empty viewer logs are omitted. The original local assessment, CAD files, flash backup and reports remain preserved separately. Regenerate omitted FEA intermediates from the supplied STEP geometry if needed.

Historical manifests and validation files describe the **original complete experiment**, including omitted artifacts. `training_audit.py` additionally checks original SolidWorks files and that full archive; it cannot establish their preservation from this public subset alone. Its policy serialization check can be called independently. Do not interpret dated validation flags as fresh verification of an arbitrary edited model or a complete copy of the original private workspace.

## Reproduce without altering the saved evidence

Read the existing JSON/CSV results directly. For new calculations, model generation, evaluation or training, copy the relevant evidence folder to a new output directory and supply the scripts' `--output` or `--destination` option. Several scripts regenerate files in their output directory. `training_session.py prepare` refuses to overwrite an existing experiment.

Example, from the repository root after installing the scientific and optional training dependencies:

```powershell
Copy-Item outputs/talos-training-2026-10-12 outputs/local-evaluation -Recurse
.\.venv-mujoco\Scripts\python.exe tools/cae/training_session.py evaluate --destination outputs/local-evaluation --label ppo-posture-v2 --seed-offset 10000
```

The `.pkl` normalization and policy archives should be loaded only from a trusted source; they are Python serialization artifacts. The saved dependency list records the Windows CPU environment used for the study, and is not a cross-platform lockfile. See `tools/cae/README.md` for installation and the dated study README for acceptance criteria.

## Verification for this publication

The publication checks compile V17 without uploading it, build the React bundle, validate all 13 CAD meshes and independent leg motion, load all twelve saved MuJoCo XML models, run the Gymnasium checker, and reload the final policy twice with the same seed to check reproducibility. Results are recorded in `docs/verification-2026-10-12.json` after completion. These checks do not commission the physical robot.
