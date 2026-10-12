import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.optimize import brentq, minimize_scalar

GRAVITY = 9.80665
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DEFAULT_OUTPUT = ROOT / "outputs" / "talos-cae-2026-10-11"
COLORS = ["#12665b", "#d97930", "#5673ad", "#ad4656", "#6d8052"]


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def save_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False), encoding="utf-8")


def save_csv(path, rows):
    with Path(path).open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def transform(component, point):
    values = component["transform"]
    return np.array(values[:9]).reshape(3, 3).T @ np.array(point) + values[9:12]


def cross2(first, second):
    return first[0] * second[1] - first[1] * second[0]


def rotation(angle):
    return np.array([[math.cos(angle), -math.sin(angle)], [math.sin(angle), math.cos(angle)]])


def geometry(native):
    components = {component["name"]: component for component in native["components"]}
    sides = {}
    for side, upper, lower, support in [
        ("left", "TALOS_Left_Leg_Link-1", "Left Wheel Leg-1", "Left Leg Support-1"),
        ("right", "TALOS_Right_Leg_Link-1", "Right Wheel Leg -2", "Right Leg Support-1"),
    ]:
        drive = transform(components[upper], [0, 0.1, 0])
        knee = transform(components[upper], [0, 0, 0])
        lower_knee = transform(components[lower], [0, -0.02481, 0])
        follower = transform(components[lower], [0, 0, 0])
        support_pivot = transform(components[support], [0, 0.077725948694398, 0])
        support_follower = transform(components[support], [0, -0.02578756359973451, 0])
        sides[side] = {
            "drive_world_m": drive.tolist(), "support_world_m": support_pivot.tolist(),
            "support_offset_m": (support_pivot[:2] - drive[:2]).tolist(),
            "drive_length_m": 0.1, "coupler_length_m": 0.02481,
            "support_length_m": 0.1035135122941325, "wheel_from_follower_m": 0.12408,
            "wheel_from_drive_knee_m": 0.09927,
            "drive_knee_mismatch_mm": float(np.linalg.norm(knee[:2] - lower_knee[:2]) * 1000),
            "support_knee_mismatch_mm": float(np.linalg.norm(follower[:2] - support_follower[:2]) * 1000),
            "reference_angle_deg": math.degrees(math.atan2(knee[0] - drive[0], drive[1] - knee[1])),
            "reference_hip_height_mm": float((drive[1] - transform(components[lower], [0, -0.12408, 0])[1] + 0.0425) * 1000),
            "upper_name": upper, "lower_name": lower, "support_name": support,
        }
    head = components["Head-1"]
    box = head["bbox_local_m"]
    corners = [transform(head, [horizontal, vertical, lateral]) for horizontal in [box[0], box[3]]
               for vertical in [box[1], box[4]] for lateral in [box[2], box[5]]]
    sides["head_bbox_world_m"] = [np.min(corners, axis=0).tolist(), np.max(corners, axis=0).tolist()]
    return sides


def solve_leg(parameters, angle, branch=-1):
    drive = np.zeros(2)
    support = np.array(parameters["support_offset_m"])
    knee = np.array([math.sin(angle), -math.cos(angle)]) * parameters["drive_length_m"]
    difference = support - knee
    distance = np.linalg.norm(difference)
    coupler = parameters["coupler_length_m"]
    follower_radius = parameters["support_length_m"]
    along = (coupler ** 2 - follower_radius ** 2 + distance ** 2) / (2 * distance)
    squared_height = coupler ** 2 - along ** 2
    if squared_height < 0:
        return None
    direction = difference / distance
    follower = knee + along * direction + branch * math.sqrt(squared_height) * np.array([-direction[1], direction[0]])
    wheel = follower + parameters["wheel_from_follower_m"] / coupler * (knee - follower)
    return {"drive": drive, "support": support, "knee": knee, "follower": follower, "wheel": wheel}


def leg_load(parameters, angle, normal_force):
    pose = solve_leg(parameters, angle)
    if pose is None:
        return None
    support_direction = (pose["support"] - pose["follower"]) / parameters["support_length_m"]
    denominator = cross2(pose["follower"] - pose["knee"], support_direction)
    if abs(denominator) < 1e-8:
        return None
    ground_force = np.array([0.0, normal_force])
    support_force = -cross2(pose["wheel"] - pose["knee"], ground_force) / denominator
    upper_force = ground_force + support_force * support_direction
    torque = -cross2(pose["knee"], upper_force)
    return {"upper_force_n": upper_force, "support_force_n": support_force, "drive_torque_nm": torque}


def finish_figure(figure, destination):
    figure.savefig(destination, dpi=180, facecolor="white", bbox_inches="tight")
    plt.close(figure)


def run(output, inputs):
    output.mkdir(parents=True, exist_ok=True)
    figures = output / "figures"
    figures.mkdir(exist_ok=True)
    native = read_json(output / "native-cad.json")
    shapes = geometry(native)
    radius = inputs["wheel_diameter_m"] / 2
    mass = inputs["total_mass_kg"] or inputs["illustrative_mass_kg"]
    torque_scale = GRAVITY * 0.01
    wheel_torque = inputs["wheel_torque_reference_kgf_cm"] * torque_scale
    fraction = (inputs["servo_voltage_v"] - 5) / 1.8
    leg_torque = (21 + fraction * 4) * torque_scale
    current_reference = np.array(inputs["leg_stall_current_reference_6_8v_a"])
    current_estimate = current_reference * inputs["servo_voltage_v"] / 6.8
    records = []
    volume_total = 0.0
    for component in native["components"]:
        volume = component["mass"]["volume_m3"]
        volume_total += volume
        solid_mass = volume * inputs["print"]["density_scenario_kg_m3"]
        records.append({"component": component["name"], "volume_cm3": volume * 1e6,
                        "solid_pla_mass_g_scenario": solid_mass * 1000,
                        "half_volume_mass_g_lower_screen": solid_mass * 500,
                        "native_assigned_mass_g_not_robot_mass": component["mass"]["cad_assigned_mass_kg"] * 1000})
    save_csv(output / "cad_components.csv", records)
    kinematics = []
    for side in ["left", "right"]:
        parameters = shapes[side]
        for degrees in np.arange(20, 70.01, 0.25):
            angle = math.radians(degrees)
            pose = solve_leg(parameters, angle)
            loading = leg_load(parameters, angle, mass * GRAVITY / 2)
            if pose is None or loading is None:
                continue
            delta = 1e-7
            previous = solve_leg(parameters, angle - delta)
            following = solve_leg(parameters, angle + delta)
            height_jacobian = -(following["wheel"][1] - previous["wheel"][1]) / (2 * delta)
            virtual_work_torque = mass * GRAVITY / 2 * height_jacobian
            assert previous is not None and following is not None, (side, degrees)
            assert math.isclose(loading["drive_torque_nm"], virtual_work_torque, rel_tol=2e-6, abs_tol=1e-7), (side, degrees)
            closure_error = max(abs(np.linalg.norm(pose["follower"] - pose["knee"]) - 0.02481),
                                abs(np.linalg.norm(pose["follower"] - pose["support"]) - parameters["support_length_m"]))
            assert closure_error < 1e-9
            kinematics.append({"side": side, "drive_angle_from_down_deg": float(degrees),
                               "ground_to_driven_hip_mm": (radius - pose["wheel"][1]) * 1000,
                               "wheel_x_relative_hip_mm": pose["wheel"][0] * 1000,
                               "height_jacobian_m_per_rad": float(height_jacobian),
                               "torque_nm_at_example_mass": abs(virtual_work_torque),
                               "upper_pin_load_n_at_example_mass": float(np.linalg.norm(loading["upper_force_n"])),
                               "support_pin_load_n_at_example_mass": abs(float(loading["support_force_n"])),
                               "closure_error_m": closure_error})
        candidates = [row for row in kinematics if row["side"] == side]
        optimum = minimize_scalar(lambda angle: solve_leg(parameters, angle)["wheel"][1],
                                  bounds=(math.radians(candidates[0]["drive_angle_from_down_deg"]), math.radians(35)), method="bounded")
        parameters["reconstructed_max_hip_height_mm"] = (radius - solve_leg(parameters, optimum.x)["wheel"][1]) * 1000
        parameters["max_height_drive_angle_deg"] = math.degrees(optimum.x)
        parameters["max_height_search_window_deg"] = [candidates[0]["drive_angle_from_down_deg"], 35]
        parameters["head_top_above_drive_mm"] = (shapes["head_bbox_world_m"][1][1] - parameters["drive_world_m"][1]) * 1000
    save_csv(output / "leg_kinematics.csv", kinematics)
    balance = []
    for height in [0.162, 0.227, 0.292]:
        for offset in inputs["com_above_hip_scenarios_m"]:
            com_height = height + offset - radius
            for inertia_ratio in inputs["body_inertia_ratio_scenarios"]:
                effective_length = com_height * (1 + inertia_ratio)
                for speed in [inputs["wheel_rpm_baseline"], inputs["wheel_rpm_alternate"]]:
                    speed_limit = 2 * math.pi * radius * speed / 60
                    balance.append({"reported_hip_height_m": height, "assumed_com_above_hip_m": offset,
                                    "com_above_axle_m": com_height, "assumed_inertia_ratio": inertia_ratio,
                                    "effective_pendulum_length_m": effective_length, "wheel_rpm": speed,
                                    "wheel_speed_m_s": speed_limit, "unstable_efold_s": math.sqrt(effective_length / GRAVITY),
                                    "zero_rate_capture_angle_deg_screen": math.degrees(speed_limit / math.sqrt(GRAVITY * effective_length)),
                                    "capture_speed_at_10deg_m_s_screen": math.sqrt(GRAVITY * effective_length) * math.radians(10),
                                    "capture_speed_at_5deg_40dps_m_s_screen": math.sqrt(GRAVITY * effective_length) * math.radians(5) + effective_length * math.radians(40)})
    save_csv(output / "balance_envelope.csv", balance)
    obstacles = [{"mass_kg": candidate_mass, "step_mm": step * 1000,
                  "torque_per_wheel_nm_ideal_shared_load": candidate_mass * GRAVITY / 2 * math.sqrt(2 * radius * step - step ** 2),
                  "fraction_of_wheel_torque_reference": candidate_mass * GRAVITY / 2 * math.sqrt(2 * radius * step - step ** 2) / wheel_torque}
                 for candidate_mass in inputs["mass_sweep_kg"] for step in [0.005, 0.01, 0.02, 0.03, 0.04]]
    save_csv(output / "obstacle_screen.csv", obstacles)
    power_scenarios = []
    for name, current in [("6V low stall extrapolation", current_estimate[0]), ("6V high stall extrapolation", current_estimate[1]),
                          ("6.8V published high used as conservative reserve", current_reference[1])]:
        power = 2 * inputs["servo_voltage_v"] * (current + inputs["wheel_current_peak_scenario_a"]) + inputs["logic_power_scenario_w"]
        power_scenarios.append({"scenario": name, "per_leg_current_a": float(current), "total_output_power_w": float(power),
                                "battery_current_at_10_8v_a": power / (inputs["converter_efficiency_scenario"] * 10.8),
                                "battery_current_at_12_6v_a": power / (inputs["converter_efficiency_scenario"] * 12.6)})
    save_csv(output / "power_scenarios.csv", power_scenarios)
    representative = next(row for row in kinematics if row["side"] == "left" and row["drive_angle_from_down_deg"] == 50)
    case_load = leg_load(shapes["left"], math.radians(50), mass * GRAVITY / 2)
    local_force = rotation(math.radians(-50)) @ case_load["upper_force_n"]
    mass_scaling = []
    for candidate_mass in inputs["mass_sweep_kg"]:
        mass_scaling.append({"mass_kg": candidate_mass,
                             "two_leg_static_torque_nm_each_at_q50": representative["torque_nm_at_example_mass"] * candidate_mass / mass,
                             "one_leg_load_case_torque_nm_at_q50": representative["torque_nm_at_example_mass"] * candidate_mass / mass * 2,
                             "dynamic_2x_two_leg_torque_nm_each_at_q50": representative["torque_nm_at_example_mass"] * candidate_mass / mass * 2})
    save_csv(output / "mass_sensitivity.csv", mass_scaling)
    results = {"status": "CAD-informed preliminary CAE; not a measured or validated digital twin",
               "example_mass_kg_not_measured": mass, "cad": shapes,
               "structural_component_count": len(records), "native_mate_count": len(native["mates"]),
               "solid_cad_volume_cm3": volume_total * 1e6,
               "printed_mass_screen_kg": [volume_total * 1240 * 0.5, volume_total * 1240],
               "wheel_speed_baseline_m_s": 2 * math.pi * radius * inputs["wheel_rpm_baseline"] / 60,
               "wheel_torque_reference_nm_not_continuous": wheel_torque,
               "leg_torque_6v_interpolated_nm_not_continuous": leg_torque,
               "leg_stall_current_6v_extrapolated_a_not_measured": current_estimate.tolist(),
               "power_scenarios": power_scenarios,
               "q50_example": representative, "fea_upper_force_local_n": [*local_force.tolist(), 0.0],
               "q50_static_mass_at_interpolated_stall_kg_not_safe_rating": mass * leg_torque / representative["torque_nm_at_example_mass"],
               "q50_static_mass_at_40percent_stall_scenario_kg": mass * 0.4 * leg_torque / representative["torque_nm_at_example_mass"],
               "cad_reach_bound_driven_pivot_m": 0.1 + 0.09927 + radius,
               "cad_reach_bound_alternate_lower_hole_m": 0.1 + 0.12408 + radius,
               "source_glb_sha256": hashlib.sha256((Path(inputs["cad_glb_path"]) if Path(inputs["cad_glb_path"]).exists() else output / "simulation" / "source-assembly.glb").read_bytes()).hexdigest(),
               "validation": {"fourbar_closure_max_m": max(row["closure_error_m"] for row in kinematics),
                              "virtual_work_vs_pin_equilibrium": "passed all sampled configurations"}}
    save_json(output / "results.json", results)
    save_json(output / "inputs.json", inputs)
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10, "axes.spines.top": False,
                         "axes.spines.right": False, "axes.labelcolor": "#253a37", "text.color": "#253a37"})
    figure, axes = plt.subplots(1, 2, figsize=(10, 4.1))
    for side, color in zip(["left", "right"], COLORS):
        rows = [row for row in kinematics if row["side"] == side and row["drive_angle_from_down_deg"] <= 60]
        axes[0].plot([row["drive_angle_from_down_deg"] for row in rows], [row["ground_to_driven_hip_mm"] for row in rows], color=color, label=side.title())
        axes[1].plot([row["ground_to_driven_hip_mm"] for row in rows], [row["torque_nm_at_example_mass"] for row in rows], color=color, label=side.title())
    axes[0].axhline(292, color=COLORS[3], linestyle="--", label="User high endpoint: check reference")
    axes[0].set(xlabel="Driven-link angle from vertical down (deg)", ylabel="Ground to driven hip (mm)", title="Provisional closed linkage, 85 mm wheels")
    axes[1].axhline(leg_torque, color=COLORS[3], linestyle="--", label="6 V interpolated stall torque")
    axes[1].axhline(0.4 * leg_torque, color=COLORS[2], linestyle=":", label="40% stall scenario, NOT a rating")
    axes[1].set(xlabel="Ground to driven hip (mm)", ylabel="Static torque per leg (N m)", title=f"Gravity support: {mass:g} kg EXAMPLE, two legs")
    for axis in axes:
        axis.grid(alpha=0.15)
        axis.legend(fontsize=7, loc="best")
    figure.tight_layout()
    finish_figure(figure, figures / "leg_height_torque.png")
    figure, axis = plt.subplots(figsize=(9.5, 3.5))
    angles = np.linspace(0, 18, 100)
    for height, color in zip([0.162, 0.227, 0.292], COLORS):
        length = height + 0.03 - radius
        axis.plot(angles, math.sqrt(GRAVITY * length) * np.radians(angles), color=color, label=f"Hip {height*100:.1f} cm; CoM hip +3 cm")
    axis.axhline(results["wheel_speed_baseline_m_s"], color=COLORS[3], linestyle="--", label="50 rpm reference = 0.223 m/s, before loading")
    axis.set(xlabel="Forward tilt from the correct height-specific balance pose (deg)", ylabel="Instant velocity-capture screen (m/s)", title="Point-mass, zero initial tilt rate; not a recovery guarantee")
    axis.grid(alpha=0.15)
    axis.legend(fontsize=8)
    finish_figure(figure, figures / "capture_envelope.png")
    figure, axes = plt.subplots(1, 2, figsize=(10, 3.8))
    axes[0].bar(["LM2596\ncontinuous", "6V leg stall\nlow estimate", "6V leg stall\nhigh estimate", "6.8V leg\npublished high"], [2, *current_estimate.tolist(), 3.4], color=[COLORS[0], COLORS[1], COLORS[3], COLORS[3]])
    axes[0].set(ylabel="Per-leg output current (A)", title="6 V estimates assume motor resistance unchanged")
    axes[1].bar(["6V low\nscenario", "6V high\nscenario", "Reserve\nscenario"], [row["battery_current_at_10_8v_a"] for row in power_scenarios], color=COLORS[:3])
    axes[1].set(ylabel="Battery current at 10.8 V (A)", title="85% efficiency; wheel peaks assumed 2 A each")
    for axis in axes:
        axis.grid(axis="y", alpha=0.15)
        axis.tick_params(axis="x", labelsize=8)
    figure.tight_layout()
    finish_figure(figure, figures / "power_budget.png")
    figure, axes = plt.subplots(1, 2, figsize=(10, 4.2))
    for axis, degrees in zip(axes, [30, 50]):
        pose = solve_leg(shapes["left"], math.radians(degrees))
        for first, second, color in [("drive", "knee", COLORS[0]), ("support", "follower", COLORS[2]), ("follower", "wheel", COLORS[1]), ("drive", "support", "#6d7471")]:
            pair = np.array([pose[first], pose[second]]) * 1000
            axis.plot(pair[:, 0], pair[:, 1], color=color, linewidth=4, marker="o", markersize=5)
        wheel = pose["wheel"] * 1000
        axis.add_patch(plt.Circle(wheel, radius * 1000, fill=False, linewidth=2, color="#253a37"))
        axis.axhline(wheel[1] - radius * 1000, color="#999999", linewidth=1)
        for label, key, offset in [("A: driven hip", "drive", (-65, 10)), ("D: passive hip", "support", (8, -13)), ("B: driven knee", "knee", (8, -4)), ("C: follower", "follower", (10, 6)), ("W: wheel axle", "wheel", (46, -5))]:
            point = pose[key] * 1000
            axis.annotate(label, point, xytext=offset, textcoords="offset points", fontsize=7)
        axis.set(aspect="equal", xlabel="Fore-aft from driven hip (mm)", ylabel="Vertical from driven hip (mm)", title=f"q = {degrees} deg; H = {(radius-pose['wheel'][1])*1000:.1f} mm")
        axis.set_xlim(-80, 110)
        axis.grid(alpha=0.12)
    figure.tight_layout()
    finish_figure(figure, figures / "cad_linkage.png")
    print(json.dumps({key: value for key, value in results.items() if key not in ["cad", "power_scenarios"]}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--inputs", type=Path, default=HERE / "inputs.json")
    arguments = parser.parse_args()
    run(arguments.output, read_json(arguments.inputs))
