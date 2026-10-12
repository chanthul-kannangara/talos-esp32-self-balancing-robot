import argparse
import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from analyse import COLORS, DEFAULT_OUTPUT, finish_figure, read_json, save_csv, save_json


def run(output):
    results = read_json(output / "results.json")
    force = results["q50_example"]["upper_pin_load_n_at_example_mass"]
    records = []
    for diameter_mm in [3.0, 4.0, 5.0]:
        for thickness_mm in [3.0, 5.0, 6.0]:
            for load_multiplier in [1.0, 2.0]:
                applied_force = force * load_multiplier
                records.append({
                    "nominal_shank_diameter_mm": diameter_mm,
                    "pla_engagement_thickness_mm_scenario": thickness_mm,
                    "load_multiplier_scenario": load_multiplier,
                    "pin_load_n": applied_force,
                    "single_shear_average_mpa": 4 * applied_force / (math.pi * diameter_mm ** 2),
                    "double_shear_average_mpa": 2 * applied_force / (math.pi * diameter_mm ** 2),
                    "projected_pla_bearing_pressure_mpa": applied_force / (diameter_mm * thickness_mm),
                    "cantilever_bending_stress_mpa_at_5mm_overhang_scenario": 32 * applied_force * 5 / (math.pi * diameter_mm ** 3),
                    "cantilever_bending_stress_mpa_at_10mm_overhang_scenario": 32 * applied_force * 10 / (math.pi * diameter_mm ** 3),
                })
    friction = []
    for coefficient in [0.15, 0.3, 0.5]:
        for preload in [50, 100, 250]:
            for faces in [1, 2]:
                friction.append({"friction_coefficient_assumed": coefficient, "clamp_preload_n_assumed": preload,
                                 "sliding_faces_assumed": faces, "effective_friction_radius_mm_assumed": 5,
                                 "face_friction_torque_nm_scenario": coefficient * preload * faces * 0.005})
    m4 = next(row for row in records if row["nominal_shank_diameter_mm"] == 4 and row["pla_engagement_thickness_mm_scenario"] == 3 and row["load_multiplier_scenario"] == 1)
    summary = {"reported_interface": "M4 for most joints; direct rotation against PLA",
               "all_joint_sizes_verified": False, "bolt_grade": None, "factor_of_safety": None,
               "example_total_mass_kg_not_measured": results["example_mass_kg_not_measured"],
               "load_case": "Reconstructed left linkage at q50, equal vertical ground reactions",
               "m4_at_3mm_engagement_scenario": m4,
               "native_knee_bore_diameter_mm": 4.95,
               "nominal_m4_diametral_gap_if_used_in_that_native_bore_mm": 0.95,
               "nominal_m4_radial_gap_if_used_in_that_native_bore_mm": 0.475,
               "face_friction_example_nm_assumed": 0.3 * 100 * 2 * 0.005,
               "limitations": ["Nominal smooth-shank screening, not thread-root strength or fatigue",
                               "Bolt grade, true load sharing, bolt overhang, printed bore and engagement thickness unknown",
                               "Face friction scenarios are not measured PLA tribology; passive joint friction needs Jacobian mapping to hip",
                               "No preload/contact/creep analysis; no approved tightening torque or safety rating"]}
    assert math.isclose(m4["single_shear_average_mpa"], force / (math.pi * 2 ** 2), rel_tol=1e-12)
    assert math.isclose(m4["double_shear_average_mpa"] * 2, m4["single_shear_average_mpa"], rel_tol=1e-12)
    assert all(row["projected_pla_bearing_pressure_mpa"] > 0 for row in records)
    save_json(output / "bolt_joint_results.json", summary)
    save_csv(output / "bolt_joint_sensitivity.csv", records)
    save_csv(output / "pivot_friction_scenarios.csv", friction)
    figure, axes = plt.subplots(1, 2, figsize=(10, 3.8))
    for diameter, color in zip([3, 4, 5], COLORS):
        subset = [row for row in records if row["nominal_shank_diameter_mm"] == diameter and row["load_multiplier_scenario"] == 1]
        axes[0].plot([row["pla_engagement_thickness_mm_scenario"] for row in subset],
                     [row["projected_pla_bearing_pressure_mpa"] for row in subset], "o-", color=color, label=f"Nominal {diameter} mm pin")
    axes[0].set(xlabel="Assumed PLA bearing engagement (mm)", ylabel="Average projected bearing pressure (MPa)", title=f"{force:.2f} N pin load; 2 kg illustrative robot")
    for coefficient, color in zip([0.15, 0.3, 0.5], COLORS):
        subset = [row for row in friction if row["friction_coefficient_assumed"] == coefficient and row["sliding_faces_assumed"] == 2]
        axes[1].plot([row["clamp_preload_n_assumed"] for row in subset],
                     [row["face_friction_torque_nm_scenario"] for row in subset], "o-", color=color, label=f"Assumed friction coefficient {coefficient}")
    axes[1].set(xlabel="Assumed axial clamp preload (N)", ylabel="Friction torque at that pivot (N m)", title="Two sliding faces, assumed 5 mm effective radius")
    for axis in axes:
        axis.grid(alpha=0.15)
        axis.legend(fontsize=7)
    figure.tight_layout()
    finish_figure(figure, output / "figures" / "bolt_pivot_screen.png")
    print(summary)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args()
    run(arguments.output)
