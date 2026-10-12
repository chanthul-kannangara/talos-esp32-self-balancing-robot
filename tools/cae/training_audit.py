import argparse
import csv
import math
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from analyse import GRAVITY, ROOT, finish_figure, leg_load, read_json, save_csv, save_json, solve_leg
from talos_env import TalosEnv
from training_session import DEFAULT_EXPERIMENT, digest, load_policy, rollout


def audit(destination):
    manifest = read_json(destination / "source_manifest.json")
    source = Path(manifest["source"])
    unchanged = all(digest(source / name) == expected for name, expected in manifest["files"].items())
    if not unchanged:
        raise AssertionError("A preserved source artifact changed")
    original_book = read_json(source / "preserved_source_hashes.json")
    original_inputs = read_json(source / "inputs.json")
    preserved_roots = {
        "archived_assessment": ROOT / "outputs" / "talos-cae-2026-10-11",
        "original_cad": Path(original_inputs["native_cad_path"]).parent,
    }
    original_counts = {}
    for category, paths in original_book.items():
        for name, expected in paths.items():
            if digest(preserved_roots[category] / name) != expected:
                raise AssertionError(f"Original source changed: {category}/{name}")
        original_counts[category] = len(paths)
    environment = TalosEnv(destination)
    environment.reset(seed=71)
    rows = []
    for height in np.linspace(0.15, 0.21, 13):
        environment.set_height(float(height))
        environment.q_target = environment.q_goal.copy()
        row = {"left_hip_height_m": float(height),
               "geometric_trim_deg_assumed_com": math.degrees(environment.balance_trim())}
        for index, side in enumerate(["left", "right"]):
            angle = float(environment.q_goal[index])
            parameters = environment.results["cad"][side]
            increment = 1e-5
            derivative = (solve_leg(parameters, angle + increment)["wheel"] -
                          solve_leg(parameters, angle - increment)["wheel"]) / (2 * increment)
            load = leg_load(parameters, angle, environment.config["total_mass_kg_example_not_measured"] * GRAVITY / 2)
            row[f"{side}_hip_deg"] = math.degrees(angle)
            row[f"{side}_hold_torque_nm_2kg_example"] = float(load["drive_torque_nm"])
            row[f"{side}_vertical_mm_per_encoder_count_ideal"] = float(abs(derivative[1]) * 1000 * 2 * math.pi / 4096)
        rows.append(row)
    save_csv(destination / "cad_transfer_sweep.csv", rows)
    linear_errors = []
    first, last = rows[0], rows[-1]
    for fraction in np.linspace(0, 1, 101):
        angle = math.radians(first["left_hip_deg"] + fraction * (last["left_hip_deg"] - first["left_hip_deg"]))
        actual = environment.config["wheel_radius_m"] - solve_leg(environment.results["cad"]["left"], angle)["wheel"][1]
        linear_errors.append(actual - (0.15 + fraction * 0.06))
    firmware = ROOT / "firmware" / "TALOS_V17_Stable_Control" / "src" / "main.cpp"
    summary = {
        "preserved_source_artifacts_unchanged": unchanged,
        "source_file_count": len(manifest["files"]),
        "original_cad_and_archived_assessment_unchanged_counts": original_counts,
        "firmware_path_reviewed_not_modified": str(firmware),
        "firmware_sha256": digest(firmware),
        "nominal_wheel_speed_limit_m_s_assumed": environment.speed_limits[2] * environment.config["wheel_radius_m"],
        "encoder_resolution_deg_nominal": 360 / 4096,
        "geometric_trim_span_deg_assumed_com": [min(row["geometric_trim_deg_assumed_com"] for row in rows), max(row["geometric_trim_deg_assumed_com"] for row in rows)],
        "max_linear_angle_height_error_mm_over_150_210mm": float(max(abs(value) for value in linear_errors) * 1000),
        "height_range_is_provisional_not_approved_hardware_travel": True,
        "torques_are_static_screens_not_continuous_motor_ratings": True,
        "encoder_resolution_is_not_physical_accuracy": True,
        "hardware_validated": False,
    }
    save_json(destination / "audit.json", summary)
    environment.close()
    figure, axes = plt.subplots(1, 3, figsize=(13, 4))
    heights = [row["left_hip_height_m"] * 1000 for row in rows]
    axes[0].plot(heights, [row["geometric_trim_deg_assumed_com"] for row in rows], color="#12665b")
    axes[0].set(ylabel="Trim (degrees)", title="Assumed CoM geometric trim")
    for side, color in [("left", "#12665b"), ("right", "#d97930")]:
        axes[1].plot(heights, [abs(row[f"{side}_hold_torque_nm_2kg_example"]) for row in rows], label=side, color=color)
        axes[2].plot(heights, [row[f"{side}_hip_deg"] for row in rows], label=side, color=color)
    axes[1].set(ylabel="Holding torque magnitude (Nm)", title="2kg static load example only")
    axes[2].set(ylabel="Hip angle (degrees)", title="Nonlinear CAD linkage mapping")
    for axis in axes:
        axis.set_xlabel("Left driven-hip height (mm)")
        axis.grid(alpha=0.25)
    axes[1].legend()
    axes[2].legend()
    figure.tight_layout()
    finish_figure(figure, destination / "figures" / "transfer_geometry.png")
    print(summary, flush=True)


def verify_policy(destination, label):
    folder = destination / "training" / label
    case = {"name": "serialization_reproducibility", "seed": 9876, "pitch_deg": 0.3}
    results, traces = [], []
    for repetition in range(2):
        environment = TalosEnv(destination, seconds=2)
        predict = load_policy(folder, environment)
        summary, rows, frames, states = rollout(environment, predict, case, seconds=2)
        results.append(summary)
        traces.append(rows)
        environment.close()
    if traces[0] != traces[1]:
        raise AssertionError("Loaded policy is not reproducible with the same seed and inputs")
    save_json(folder / "verification.json", {
        "reloaded_policy_same_seed_reproducibility": True,
        "finite_rollout_and_actions": True,
        "survival_is_not_required_for_this_serialization_check": True,
        "duration_s": results[0]["duration_s"],
        "policy_sha256": digest(folder / "final.zip"),
        "normalization_sha256": digest(folder / "normalization.pkl"),
        "hardware_validated": False,
    })
    print(f"Policy serialization and seeded reproducibility passed: {label}", flush=True)


def plot_learning(destination):
    figure, axes = plt.subplots(1, 2, figsize=(11, 4))
    for folder in sorted((destination / "training").iterdir()):
        path = folder / "progress.csv"
        if not path.exists():
            continue
        with path.open(encoding="utf-8") as stream:
            rows = [row for row in csv.DictReader(stream) if row.get("eval/mean_ep_length")]
        steps = [float(row["time/total_timesteps"]) for row in rows]
        axes[0].plot(steps, [float(row["eval/mean_ep_length"]) * 0.02 for row in rows], "o-", label=folder.name)
        axes[1].plot(steps, [float(row["eval/mean_reward"]) for row in rows], "o-", label=folder.name)
    for axis, title, ylabel in zip(axes, ["Training-time evaluation survival", "Training-time evaluation return"], ["Mean duration (s)", "Episode return"]):
        axis.set(title=title, xlabel="Cumulative PPO transitions", ylabel=ylabel)
        axis.grid(alpha=0.25)
        axis.legend()
    figure.suptitle("Training-time evaluation, one seed; phase-specific rewards are not comparable")
    figure.tight_layout()
    finish_figure(figure, destination / "figures" / "learning_curve.png")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--destination", type=Path, default=DEFAULT_EXPERIMENT)
    parser.add_argument("--label")
    arguments = parser.parse_args()
    audit(arguments.destination)
    plot_learning(arguments.destination)
    if arguments.label:
        verify_policy(arguments.destination, arguments.label)


if __name__ == "__main__":
    main()
