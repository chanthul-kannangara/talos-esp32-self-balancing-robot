import argparse
import math
from pathlib import Path

import gmsh
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.linalg import spsolve

from analyse import DEFAULT_OUTPUT, finish_figure, read_json, save_csv, save_json


def mesh_part(path, size, destination):
    gmsh.initialize()
    try:
        gmsh.option.setNumber("General.Terminal", 0)
        gmsh.option.setString("Geometry.OCCTargetUnit", "M")
        gmsh.model.occ.importShapes(str(path))
        gmsh.model.occ.synchronize()
        gmsh.option.setNumber("Mesh.MeshSizeMin", size * 0.45)
        gmsh.option.setNumber("Mesh.MeshSizeMax", size)
        gmsh.option.setNumber("Mesh.MeshSizeFromCurvature", 20)
        gmsh.option.setNumber("Mesh.ElementOrder", 1)
        gmsh.option.setNumber("Mesh.Optimize", 1)
        gmsh.model.mesh.generate(3)
        tags, coordinates, _ = gmsh.model.mesh.getNodes()
        coordinates = np.array(coordinates).reshape(-1, 3)
        node_lookup = np.full(int(max(tags)) + 1, -1, dtype=int)
        node_lookup[tags] = np.arange(len(tags))
        _, tet_tags = gmsh.model.mesh.getElementsByType(4)
        _, triangle_tags = gmsh.model.mesh.getElementsByType(2)
        tets = node_lookup[np.array(tet_tags).reshape(-1, 4)]
        triangles = node_lookup[np.array(triangle_tags).reshape(-1, 3)]
        gmsh.write(str(destination))
        return coordinates, tets, triangles
    finally:
        gmsh.finalize()


def elasticity(young, poisson):
    lame = young * poisson / ((1 + poisson) * (1 - 2 * poisson))
    shear = young / (2 * (1 + poisson))
    material = np.zeros((6, 6))
    material[:3, :3] = lame
    material[np.arange(3), np.arange(3)] += 2 * shear
    material[3:, 3:] = np.eye(3) * shear
    return material


def element_matrices(coordinates, tets):
    positions = coordinates[tets]
    interpolator = np.concatenate([np.ones((*positions.shape[:2], 1)), positions], axis=2)
    determinants = np.linalg.det(interpolator)
    volumes = np.abs(determinants) / 6
    assert min(volumes) > 1e-16
    gradients = np.linalg.inv(interpolator)[:, 1:, :].transpose(0, 2, 1)
    strain_matrix = np.zeros((len(tets), 6, 12))
    strain_matrix[:, 0, 0::3] = gradients[:, :, 0]
    strain_matrix[:, 1, 1::3] = gradients[:, :, 1]
    strain_matrix[:, 2, 2::3] = gradients[:, :, 2]
    strain_matrix[:, 3, 0::3] = gradients[:, :, 1]
    strain_matrix[:, 3, 1::3] = gradients[:, :, 0]
    strain_matrix[:, 4, 1::3] = gradients[:, :, 2]
    strain_matrix[:, 4, 2::3] = gradients[:, :, 1]
    strain_matrix[:, 5, 0::3] = gradients[:, :, 2]
    strain_matrix[:, 5, 2::3] = gradients[:, :, 0]
    degrees = (tets[:, :, None] * 3 + np.arange(3)).reshape(-1, 12)
    return volumes, strain_matrix, degrees


def weighted_percentile(values, weights, fraction):
    order = np.argsort(values)
    cumulative = np.cumsum(weights[order])
    return float(np.interp(fraction * cumulative[-1], cumulative, values[order]))


def assemble_load(coordinates, triangles, resultant):
    radial = np.linalg.norm(coordinates[:, :2], axis=1)
    is_knee = np.isclose(radial, 0.002475, atol=2e-7)
    selected = triangles[np.all(is_knee[triangles], axis=1)]
    assert len(selected) > 8
    positions = coordinates[selected]
    areas = np.linalg.norm(np.cross(positions[:, 1] - positions[:, 0], positions[:, 2] - positions[:, 0]), axis=1) / 2
    centers = np.mean(positions, axis=1)
    radial_direction = centers[:, :2] / np.linalg.norm(centers[:, :2], axis=1)[:, None]
    loading_direction = resultant[:2] / np.linalg.norm(resultant[:2])
    weights = areas * np.maximum(0, radial_direction @ loading_direction)
    nodal_weights = np.zeros(len(coordinates))
    for corner in range(3):
        np.add.at(nodal_weights, selected[:, corner], weights / 3)
    nodal_weights /= nodal_weights.sum()
    return nodal_weights[:, None] * resultant, selected


def solve(coordinates, tets, triangles, force, young, poisson, size):
    volumes, strain_matrix, degrees = element_matrices(coordinates, tets)
    material = elasticity(young, poisson)
    element_stiffness = np.einsum("nsi,st,ntj,n->nij", strain_matrix, material, strain_matrix, volumes, optimize=True)
    row_indices = np.broadcast_to(degrees[:, :, None], element_stiffness.shape).ravel()
    column_indices = np.broadcast_to(degrees[:, None, :], element_stiffness.shape).ravel()
    stiffness = coo_matrix((element_stiffness.ravel(), (row_indices, column_indices)), shape=(len(coordinates) * 3,) * 2).tocsr()
    nodal_force, loaded_faces = assemble_load(coordinates, triangles, np.array(force))
    hip_distance = np.linalg.norm(coordinates[:, :2] - [0, 0.1], axis=1)
    fixed_nodes = np.flatnonzero(np.isclose(hip_distance, 0.003, atol=2e-7) | np.isclose(hip_distance, 0.0040215036, atol=2e-7))
    assert len(fixed_nodes) >= 15
    fixed_degrees = (fixed_nodes[:, None] * 3 + np.arange(3)).ravel()
    free = np.setdiff1d(np.arange(len(coordinates) * 3), fixed_degrees)
    displacement = np.zeros(len(coordinates) * 3)
    displacement[free] = spsolve(stiffness[free][:, free], nodal_force.ravel()[free])
    assert np.all(np.isfinite(displacement))
    residual = stiffness @ displacement - nodal_force.ravel()
    reactions = residual.reshape(-1, 3)
    nodal_displacement = displacement.reshape(-1, 3)
    strains = np.einsum("nsi,ni->ns", strain_matrix, displacement[degrees])
    stress = strains @ material.T
    normal = stress[:, :3]
    von_mises = np.sqrt(((normal[:, 0] - normal[:, 1]) ** 2 + (normal[:, 1] - normal[:, 2]) ** 2 + (normal[:, 2] - normal[:, 0]) ** 2) / 2 + 3 * np.sum(stress[:, 3:] ** 2, axis=1))
    force_error = np.linalg.norm(np.sum(reactions[fixed_nodes], axis=0) + np.sum(nodal_force, axis=0))
    moment_error = np.linalg.norm(np.sum(np.cross(coordinates[fixed_nodes], reactions[fixed_nodes]), axis=0) + np.sum(np.cross(coordinates, nodal_force), axis=0))
    relative_free_residual = np.linalg.norm(residual[free]) / np.linalg.norm(nodal_force)
    applied_work = float(np.dot(displacement, nodal_force.ravel()))
    internal_work = float(displacement @ (stiffness @ displacement))
    affine_gradient = np.array([[0.001, 0.0007, -0.0002], [0.0001, -0.0003, 0.0004], [0.0008, -0.0005, 0.0002]])
    affine = coordinates @ affine_gradient.T
    expected = np.array([affine_gradient[0, 0], affine_gradient[1, 1], affine_gradient[2, 2],
                         affine_gradient[0, 1] + affine_gradient[1, 0], affine_gradient[1, 2] + affine_gradient[2, 1],
                         affine_gradient[0, 2] + affine_gradient[2, 0]])
    patch_error = float(np.max(np.abs(np.einsum("nsi,ni->ns", strain_matrix, affine.ravel()[degrees]) - expected)))
    assert patch_error < 1e-10
    assert relative_free_residual < 1e-7 and force_error < 1e-5 and moment_error < 1e-6
    assert math.isclose(applied_work, internal_work, rel_tol=1e-8)
    centers = np.mean(coordinates[tets], axis=1)
    bulk = np.linalg.norm(centers[:, :2] - [0, 0.1], axis=1) > 0.015
    peak = int(np.argmax(von_mises))
    summary = {
        "mesh_size_mm": size * 1000, "nodes": len(coordinates), "tetrahedra": len(tets),
        "young_modulus_pa_assumed": young, "poisson_ratio_assumed": poisson,
        "force_n": force, "mesh_volume_cm3": float(sum(volumes) * 1e6),
        "fixed_hip_nodes": len(fixed_nodes), "loaded_bore_faces": len(loaded_faces),
        "max_displacement_mm": float(np.max(np.linalg.norm(nodal_displacement, axis=1)) * 1000),
        "load_direction_compliance_mm": float(applied_work / np.linalg.norm(force) * 1000),
        "von_mises_max_mpa_including_bc_peak": float(max(von_mises) / 1e6),
        "von_mises_volume_p95_mpa": weighted_percentile(von_mises, volumes, 0.95) / 1e6,
        "von_mises_volume_p99_mpa": weighted_percentile(von_mises, volumes, 0.99) / 1e6,
        "von_mises_bulk_p95_mpa_15mm_outside_clamp": weighted_percentile(von_mises[bulk], volumes[bulk], 0.95) / 1e6,
        "peak_location_local_mm": (centers[peak] * 1000).tolist(),
        "reaction_force_sum_n": np.sum(reactions[fixed_nodes], axis=0).tolist(),
        "force_balance_residual_n": float(force_error), "moment_balance_residual_nm": float(moment_error),
        "free_equation_relative_residual": float(relative_free_residual),
        "affine_strain_patch_test_max_error": patch_error, "strain_energy_j": applied_work / 2,
    }
    return summary, nodal_displacement, von_mises, fixed_nodes, nodal_force


def draw(coordinates, tets, triangles, displacement, von_mises, fixed, nodal_force, path):
    nodal_stress = np.zeros(len(coordinates))
    counts = np.zeros(len(coordinates))
    for corner in range(4):
        np.add.at(nodal_stress, tets[:, corner], von_mises / 1e6)
        np.add.at(counts, tets[:, corner], 1)
    nodal_stress /= np.maximum(counts, 1)
    positions = coordinates[triangles]
    normals = np.cross(positions[:, 1] - positions[:, 0], positions[:, 2] - positions[:, 0])
    visible = np.abs(normals[:, 2]) / np.maximum(np.linalg.norm(normals, axis=1), 1e-20) > 0.9
    visible_faces = triangles[visible]
    face_stress = np.mean(nodal_stress[visible_faces], axis=1)
    figure, axes = plt.subplots(1, 2, figsize=(9, 5.4))
    palette_limit = float(np.percentile(face_stress, 99))
    for axis, scale in zip(axes, [0, 25]):
        plotted = (coordinates + scale * displacement) * 1000
        collection = PolyCollection(plotted[visible_faces, :2], array=face_stress, cmap="viridis", edgecolors="none")
        collection.set_clim(0, palette_limit)
        axis.add_collection(collection)
        axis.scatter(plotted[fixed, 0], plotted[fixed, 1], s=6, color="#da624c", label="Clamped hip bore")
        axis.set(xlim=(-35, 40), ylim=(-20, 130), aspect="equal", xlabel="Local X (mm)", ylabel="Local Y (mm)")
        axis.set_title("Native STEP solid / stress screen" if scale == 0 else "Displacement amplified 25x")
        axis.annotate("Pin bearing\nload", xy=(0, 0), xytext=(12, 25), fontsize=7,
                      arrowprops={"arrowstyle": "->", "color": "#da624c"})
        axis.legend(fontsize=7, loc="upper left")
    figure.colorbar(collection, ax=axes, label="Nodal-averaged von Mises (MPa), color clipped at surface p99", shrink=0.7)
    finish_figure(figure, path)


def run(output, sizes):
    results = read_json(output / "results.json")
    inputs = read_json(output / "inputs.json")
    force = results["fea_upper_force_local_n"]
    summaries = []
    for size in sizes:
        label = f"{size * 1000:.1f}mm"
        coordinates, tets, triangles = mesh_part(output / "TALOS_Left_Leg_Link-1.step", size, output / f"upper-link-{label}.msh")
        summary, displacement, von_mises, fixed, nodal_force = solve(coordinates, tets, triangles, force, 1.5e9, inputs["print"]["poisson_scenario"], size)
        summary["cad_volume_relative_error"] = abs(summary["mesh_volume_cm3"] / 29.330659451131436 - 1)
        assert summary["cad_volume_relative_error"] < 0.005
        summaries.append(summary)
        print(f"{label}: {summary['nodes']} nodes, {summary['tetrahedra']} tets, u={summary['max_displacement_mm']:.6f}mm, p95={summary['von_mises_volume_p95_mpa']:.4f}MPa", flush=True)
    fine, coarse = summaries[-1], summaries[-2]
    convergence = {metric: abs(fine[metric] / coarse[metric] - 1) for metric in ["max_displacement_mm", "von_mises_volume_p95_mpa", "von_mises_max_mpa_including_bc_peak"]}
    bundle = {"status": "Screening linear solid FEA, not a 50% infill or joint-contact validation",
              "boundary_conditions": "All translations fixed on 3mm and 4.0215mm hip bores; cosine-distributed knee bore traction",
              "load_case": f"Left linkage q50, {results['example_mass_kg_not_measured']:g} kg illustrative total mass, equal vertical support",
              "material": "Homogeneous isotropic E=1.5GPa, nu=0.35; sensitivity only, not measured PLA properties",
              "meshes": summaries, "last_two_mesh_relative_changes": convergence,
              "stiffness_sensitivity": [{"young_gpa": young, "max_displacement_mm": fine["max_displacement_mm"] * 1.5 / young}
                                        for young in [0.8, 1.5, 2.6]],
              "strength_factor_of_safety": None,
              "limitations": ["No infill cells, layer anisotropy, creep, impact, fatigue, bolts, or contact stress",
                              "Clamped hip is deliberately stiff and may create mesh-sensitive peak stress",
                              "No full robot or encoder housing failure analysis; no physical coupon validation"]}
    save_json(output / "fea_results.json", bundle)
    save_csv(output / "fea_mesh_check.csv", summaries)
    np.savez_compressed(output / "upper-link-fea.npz", coordinates=coordinates, tetrahedra=tets, triangles=triangles,
                        displacement=displacement, von_mises_pa=von_mises, fixed_nodes=fixed, nodal_force=nodal_force)
    draw(coordinates, tets, triangles, displacement, von_mises, fixed, nodal_force, output / "figures" / "upper_link_fea.png")
    print({"last_two_mesh_relative_changes": convergence})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--sizes-mm", type=float, nargs="+", default=[2.4, 1.7, 1.3])
    arguments = parser.parse_args()
    run(arguments.output, [value / 1000 for value in arguments.sizes_mm])
