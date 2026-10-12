from pathlib import Path

import numpy as np

from analyse import ROOT, read_json, save_json
from training_session import DEFAULT_EXPERIMENT, digest


def main():
    destination = DEFAULT_EXPERIMENT
    labels = ["ppo-fixed", "ppo-fixed-continued", "ppo-posture-height", "ppo-posture-v2"]
    runs = [read_json(destination / "training" / label / "run.json") for label in labels]
    evaluations = {label: read_json(destination / "training" / label / "heldout.json")
                   for label in labels[1:]}
    audit = read_json(destination / "audit.json")
    stages = []
    for label, run in zip(labels, runs):
        stages.append({"label": label, "additional_steps": run["requested_steps"],
                       "policy_total_steps": run["total_steps_in_policy"],
                       "seconds": run["wall_seconds"],
                       "reward_variant": run.get("reward_variant", "original"),
                       "height_curriculum": run["height_curriculum"]})
    rows = []
    for label, evaluation in evaluations.items():
        for mode in ["baseline", "ppo"]:
            if mode == "baseline" and label != labels[1]:
                continue
            episodes = [episode for episode in evaluation["episodes"] if episode["controller"] == mode]
            nominal = next(episode for episode in episodes if episode["name"] == "nominal_pitch_+0")
            rows.append({"controller": "diagnostic PD baseline" if mode == "baseline" else label,
                "cases": len(episodes), "survived_10s": sum(episode["survived"] for episode in episodes),
                "accuracy_passes": sum(episode["pass"] for episode in episodes),
                "median_survival_s": float(np.median([episode["duration_s"] for episode in episodes])),
                "nominal_survival_s": nominal["duration_s"],
                "nominal_pitch_rms_deg": nominal["pitch_rms_deg"],
                "nominal_leg_rms_deg": nominal["joint_rms_deg"],
                "nominal_drift_m": nominal["drift_m"],
                "nominal_hip_limit_violation_deg": nominal["max_hip_limit_violation_deg"]})
    final = rows[-1]
    gate = {"hardware_export_allowed": False, "firmware_changed": False,
        "hardware_tested": False, "robot_commands_sent": False,
        "reasons": ["No candidate approved by the complete balance and posture criteria",
                    "2 kg is a user estimate, not a scale measurement",
                    "CoM, inertia, servo PWM mapping, lag and supply behavior uncalibrated",
                    "Bolt/PLA friction, stiction, backlash and fastener contact not identified"],
        "firmware_sha256_unchanged": digest(Path(audit["firmware_path_reviewed_not_modified"])) == audit["firmware_sha256"]}
    if not gate["firmware_sha256_unchanged"]:
        raise AssertionError("Reviewed firmware changed during offline experiment")
    if any(row["accuracy_passes"] for row in rows):
        gate["reasons"][0] = "Passing some finite simulated cases is insufficient for hardware deployment"
    summary = {"status": "Completed bounded offline training study; no hardware-approved policy",
        "total_training_transitions_across_branches": sum(run["requested_steps"] for run in runs),
        "total_physics_steps_during_training": 10 * sum(run["requested_steps"] for run in runs),
        "training_wall_seconds": sum(run["wall_seconds"] for run in runs),
        "final_branch_policy_steps": runs[-1]["total_steps_in_policy"],
        "training_seed_count": 1, "stages": stages, "final_evaluation": rows,
        "gate": gate}
    save_json(destination / "training_summary.json", summary)
    save_json(destination / "hardware_transfer_gate.json", gate)
    stage_table = "\n".join(f"| {stage['label']} | {stage['additional_steps']:,} | {stage['policy_total_steps']:,} | {stage['seconds'] / 60:.2f} |" for stage in stages)
    results_table = "\n".join(f"| {row['controller']} | {row['survived_10s']}/{row['cases']} | {row['accuracy_passes']}/{row['cases']} | {row['median_survival_s']:.2f} | {row['nominal_pitch_rms_deg']:.2f} | {row['nominal_leg_rms_deg']:.2f} |" for row in rows)
    readme = f"""# TALOS offline simulation training - 12 October 2026

## Verdict

**Actual local MuJoCo/PPO training and closed-loop evaluations were performed. No policy is approved for the physical robot.**

{summary['total_training_transitions_across_branches']:,} training transitions ({summary['total_physics_steps_during_training']:,} physics steps) were executed across four stages/branches on this laptop, using CPU PyTorch. Training took {summary['training_wall_seconds'] / 60:.1f} minutes, excluding installation, diagnosis, evaluation and rendering. The last branch itself experienced {summary['final_branch_policy_steps']:,} transitions; the sum across branches is not the last policy's training count.

The original diagnostic PD baseline is **not** a faithful port of the current ESP32 firmware, so comparisons do not establish that PPO beats the real robot's controller.

The fixed-height policy learned to survive in many 10-second simulated episodes, but it sagged against the model's soft hip limits rather than holding the commanded leg posture. Its nominal leg RMS error was about 14.4 degrees. Survival alone would have falsely looked successful. The posture/height objectives did not yield an approved controller within these bounded runs. This is a rejected training result, not a successful sim-to-real transfer and not proof that the robot cannot balance.

## Runs executed

| Stage | Added transitions | Transitions experienced by that policy | Training minutes |
|---|---:|---:|---:|
{stage_table}

- `ppo-fixed`: original reward, fixed leg target, randomized dynamics.
- `ppo-fixed-continued`: resumes the first stage, same task/reward.
- `ppo-posture-height`: resumes the fixed policy, adds changing height goals, denser posture reward and a soft-limit penalty. Its negative per-step returns encouraged early termination; retained as a failed experiment.
- `ppo-posture-v2`: separately resumes the fixed policy, uses the posture/height objective plus a nonnegative surviving-step reward to remove that early-termination incentive. It is not a continuation of the failed posture branch.

The first stages were diagnostic and informed the reward changes. No further tuning was performed after the final fresh-seed evaluations. One training seed (41) is a pilot, not a multi-seed reliability study. The final v2 run includes exact source snapshots and hashes; earlier scripts evolved during this study and their runtime variants are recorded in `run.json`.

## Final fresh-seed evaluations

Each candidate used the same 24 scenario definitions, now with a 10,000 seed offset not used for objective design: seven initial pitch cases (-5 to +5 degrees), ten randomized-dynamics cases, three height commands (150/180/210 mm), two 1 N/100 ms fore-aft pushes at t=2 s, and two increased-lag cases (1.5x/2x). Training starts within +/-1 degree; larger pitch cases are stress tests. A push case that fails before t=2 s does **not** test disturbance recovery; inspect `push_applied_seconds`. Height commands are slewed and are not evidence of achieved height when the episode fails early.

The predefined full criterion was 10 seconds without falling, plus last-half RMS pitch-to-trim error <=5 degrees, leg-to-target error <=3 degrees and ground-truth forward speed RMS <=0.1 m/s. These finite tests are only preliminary gates. Failure cutoff: absolute pitch >35 degrees, roll >25 degrees, or root height <70 mm. Soft-limit violation is also reported and must not be interpreted as physical approved travel.

| Controller | Survived 10 s | Full accuracy passes | Median duration (s) | Nominal pitch RMS (deg) | Nominal leg RMS (deg) |
|---|---:|---:|---:|---:|---:|
{results_table}

Last-branch nominal episode: {final['nominal_survival_s']:.2f} s, drift {final['nominal_drift_m']:.3f} m. See the full traces rather than treating a mean return as accuracy. All final evaluation returns use the original environment reward so they remain comparable; training-time reward curves for different objectives are not directly comparable.

## What transfers to the balance-code design

1. **Prove encoder leg holding before wheel balancing.** Reject sagging into end stops even if the body stays upright. Log signed target error, encoder velocity, PWM, saturation and tracking faults for each leg. Do not replace current safe, measured support settings with these simulator actions.
2. **Use a calibrated nonlinear encoder-to-linkage mapping, not just linear height interpolation.** In this reconstructed CAD geometry, interpolating hip angle linearly between 150 and 210 mm introduces up to {audit['max_linear_angle_height_error_mm_over_150_210mm']:.2f} mm height error. This is a geometry calculation, not measured physical accuracy. The existing firmware functions `calculateRobotHeightCm`, `legAngleForExtension` and `updateHeightTrajectory` are the review points. The physical 162-292 mm claim still needs its measurement reference/geometry reconciled; no model travel limit here authorizes hardware travel.
3. **Balance trim changes with linkage posture.** With the assumed torso CoM 30 mm above the hip, the CAD geometric trim varies from {audit['geometric_trim_span_deg_assumed_com'][0]:.2f} to {audit['geometric_trim_span_deg_assumed_com'][1]:.2f} degrees over the provisional 150-210 mm sweep. A measured CoM, encoder zero/direction and supported height calibration must replace this assumed table before adding height-dependent trim to `updateWheelControl`. Do not copy these angles into the robot.
4. **Respect wheel speed and actuator delay.** 85 mm wheels at the assumed 50 RPM give only {audit['nominal_wheel_speed_limit_m_s_assumed']:.3f} m/s no-load rim speed. Torque and recovery are not guaranteed at that speed. The model includes one 20 ms command-frame delay, 60 ms wheel/80 ms leg lag and deadband, but these need actual identification. The firmware computes balance at 500 Hz while its servo PWM is 50 Hz; those rates are not equivalent actuator bandwidth.
5. **Keep the fast safety/balance loop on ESP32.** Laptop/browser calculation is appropriate for rendering, telemetry analysis and offline training, not a Wi-Fi-dependent 2 ms stabilizing loop. Current firmware already keeps local control separate from Wi-Fi. Preserve that architecture and the encoder/IMU stale-data and magnetic-field gates.
6. **Do not invent wheel feedback.** Actor inputs were only IMU-derived gravity, gyro/accelerometer, quantized hip encoders and rates, previous actions, targets and zero motion commands (21 numbers). Four actions requested normalized left/right leg and wheel velocities. Simulator wheel/base velocity was used only for reward/scoring. AS5600 units at the hips do not provide wheel speed; physical velocity control/odometry would need an identified estimator or additional sensors.

`cad_transfer_sweep.csv` contains the geometry/torque/ideal-resolution calculations. The 12-bit nominal encoder step is {audit['encoder_resolution_deg_nominal']:.5f} degrees, not absolute sensor accuracy. Static holding torque examples are not continuous servo torque or PLA stress validation.

## Model assumptions and missing identification

Your approximately 2 kg mass estimate matches the nominal model; it is not a scale measurement. Training randomized common mass/inertia scale 0.9-1.1 (1.8-2.2 kg), ground friction 0.6-1.0, independent actuator strengths 0.8-1.0 and actuator lag factors 0.7-1.3. These chosen ranges are not confidence intervals or assurance the real robot falls inside them. The nominal model includes 6 V servo assumptions, PLA, 85 mm wheels and repaired planar four-bar closure.

M4/M3 pins, retaining clips and wheel-servo housings are present at the earlier verified CAD axes. Fasteners/cases remain zero-mass non-colliding visuals under the model's existing lumped masses. **No bolt/PLA friction, stiction, preload, backlash, wear, detailed tire compliance, rail droop, BMS trip or thermal derating was identified by training.** Material infill/layer data do not turn this rigid-body study into verified structural FEA.

The PPO Gaussian-policy action distribution and normalization are included in the saved artifacts. A `.zip` policy alone is not a drop-in ESP32 balance algorithm; observation normalization and the actuator calibration/mapping would also be required. There is no robot, serial or network-control API in the training/replay code.

## Outputs and reproducibility

- `training_summary.json`, `hardware_transfer_gate.json`: machine-readable results and explicit no-deployment gate.
- `training/*/final.zip`, `normalization.pkl`, `run.json`, `progress.csv`, `checkpoints/`: weights, normalization, configuration, timings and logs.
- `training/*/heldout.json`, `heldout_traces.csv`, `heldout.png`: per-case pass/fail, actual applied events and raw traces.
- `training/ppo-fixed-continued/replay.mp4`: illustrative nominal fixed-policy survival **with failed leg accuracy**, not an approved balance demonstration.
- `training/ppo-posture-v2/replay.mp4`: illustrative final branch, including any failure; no successful trajectory is substituted for its result.
- `figures/learning_curve.png`, `figures/transfer_geometry.png`: training diagnostics and CAD transfer calculations.
- `audit.json`, `source_manifest.json`, `requirements-installed.txt`, `user_measurements.json`: preservation, dependency and assumption evidence.

From the repository root, reproduce evaluation or show the recorded replay:

```powershell
& .\\.venv-mujoco\\Scripts\\python.exe tools\\cae\\training_session.py evaluate --label ppo-posture-v2 --seed-offset 10000
& .\\.venv-mujoco\\Scripts\\python.exe tools\\cae\\training_session.py replay --label ppo-fixed-continued --live
```

`--live` displays an explicitly labelled **recorded MuJoCo replay**, not a live hardware stream or interactive closed-loop test. The saved per-step evaluations were closed-loop physics runs. For a new training experiment, use a fresh destination and label; `prepare` refuses to overwrite an existing experiment. See `--help` for bounded training/resume options. The v2 run's `source_snapshot` preserves the relevant source version.

## Safety and preservation

No firmware was edited, compiled, uploaded or flashed for this study. No physical motor command was sent. The reviewed source was `{audit['firmware_path_reviewed_not_modified']}`; its hash is unchanged. Hash checks also preserve {audit['source_file_count']} source-extension files, the earlier CAD files and archived assessment/report package. Current magnetic health and real mechanical behavior were not re-tested.

Before any later hardware transfer: reconcile encoder zero/direction and travel, maintain valid magnetic status, establish supported leg hold at multiple postures, measure supply behavior, identify motor neutral/deadband/speed/lag, refine CoM/inertia, rerun final unseen tests with measured parameters, then compile and stage-test with support, wheels clear and immediate power cutoff. No auto-arm or auto-flash path is added.

## Method references

PPO is used as implemented in [Stable-Baselines3](https://stable-baselines3.readthedocs.io/en/master/modules/ppo.html), with monitored independent evaluation following its [evaluation guidance](https://stable-baselines3.readthedocs.io/en/master/guide/rl_tips.html). CPU PyTorch was installed from the [official PyTorch package source](https://pytorch.org/get-started/locally/). These references explain the tools; the result numbers come from this laptop's saved runs, not the documentation.
"""
    (destination / "README.md").write_text(readme, encoding="utf-8")
    print(summary, flush=True)


if __name__ == "__main__":
    main()
