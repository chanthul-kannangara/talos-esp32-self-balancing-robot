import argparse
import hashlib
import math
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path

import mujoco
import numpy as np
import trimesh
from PIL import Image, ImageDraw, ImageFont

from analyse import DEFAULT_OUTPUT, HERE, ROOT, read_json, save_json
from simulation import closure_errors, generate, numbers

DEFAULT_HARDWARE_OUTPUT = ROOT / "outputs" / "talos-pivot-model-2026-10-12"
PHOTOS = [
    "codex-clipboard-c580773c-b78f-44c8-afe8-8b09c261e9b5.png",
    "codex-clipboard-2c270c17-6281-44b0-a2e3-bba7c61ff04e.png",
    "codex-clipboard-db172a08-f9ad-4776-8967-0924f73b9640.png",
    "codex-clipboard-2f4b2630-234c-4c7e-a19f-bea92b45874d.png",
    "codex-clipboard-3c1d9cb9-5a59-4822-bfb1-ba05cc3ad4de.png",
    "codex-clipboard-89981600-96f3-4794-befb-44a652e6c444.png",
    "codex-clipboard-3e39f6aa-4a6b-4dc7-b66d-43942e192dc6.png",
    "codex-clipboard-fd77c97e-0c83-4b0b-8472-2d845081a97a.png",
    "codex-clipboard-20f7a84b-1099-4f14-b2ee-1b57a9981469.png",
]


def file_hash(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def directory_hashes(directory):
    return {str(path.relative_to(directory)): file_hash(path)
            for path in sorted(directory.rglob("*")) if path.is_file()}


def prepare(source, output):
    source = source.resolve()
    output = output.resolve()
    if output == source or output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError("Use a separate output directory, outside the archived assessment")
    for subdirectory in [output, output / "simulation", output / "figures"]:
        subdirectory.mkdir(parents=True, exist_ok=True)
    for name in ["native-cad.json", "results.json"]:
        shutil.copy2(source / name, output / name)
    shutil.copy2(source / "simulation" / "source-assembly.glb", output / "simulation" / "source-assembly.glb")
    inputs = read_json(source / "inputs.json")
    inputs["cad_glb_path"] = str(output / "simulation" / "source-assembly.glb")
    save_json(output / "inputs.json", inputs)


def visual_geom(parent, **attributes):
    return ET.SubElement(parent, "geom", mass="0", contype="0", conaffinity="0", group="2", **attributes)


def add_mesh(parent, asset, directory, name, mesh, material):
    mesh.export(directory / f"{name}.obj")
    ET.SubElement(asset, "mesh", name=name, file=f"assets/{name}.obj")
    return visual_geom(parent, name=name, type="mesh", mesh=name, material=material)


def cylinder_bore(component, diameter, minimum_y=-math.inf, maximum_y=math.inf):
    matches = [cylinder["params_local_m"] for cylinder in component["cylinders"]
               if math.isclose(2 * cylinder["params_local_m"][6], diameter, abs_tol=1e-8)
               and minimum_y < cylinder["params_local_m"][1] < maximum_y]
    centers = np.unique(np.round(np.array(matches)[:, :2], 12), axis=0) if matches else []
    if len(centers) != 1:
        raise ValueError(f"Expected one bore in {component['name']}: {diameter}, {minimum_y}, {maximum_y}")
    return np.array([centers[0][0], centers[0][1], 0.0])


def register_clip(parent, asset, scene, directory, component_name, name, matrix, origin, source_holes, target_holes):
    mesh = scene.geometry[scene.graph[component_name][1]].copy()
    mesh.vertices = mesh.vertices @ matrix.T + origin
    add_mesh(parent, asset, directory, name, mesh, "printed_clip")
    mapped_holes = np.asarray(source_holes) @ matrix.T + origin
    residuals = np.linalg.norm((mapped_holes - target_holes)[:, [0, 2]], axis=1)
    if max(residuals) > 1e-5:
        raise ValueError(f"Retaining clip hole registration failed: {name}, {residuals}")
    return {"name": name, "owner": parent.get("name"), "source_component": component_name,
            "source_holes_part_local_m": np.asarray(source_holes).tolist(),
            "mapped_holes_owner_local_m": mapped_holes.tolist(),
            "target_holes_owner_local_m": np.asarray(target_holes).tolist(),
            "in_plane_registration_residual_m": residuals.tolist(),
            "axial_placement": "schematic; no measured grip, washer, spacer or preload specification"}


def hex_mesh(across_flats, height):
    mesh = trimesh.creation.cylinder(radius=across_flats / math.sqrt(3), height=height, sections=6)
    mesh.apply_transform(trimesh.transformations.rotation_matrix(math.pi / 2, [1, 0, 0]))
    return mesh


def add_pin(parent, asset, directory, settings, side, role, size, position, source_component, cad_point, head_style):
    sign = 1 if side == "left" else -1
    prefix = f"{side}_{role}"
    dimensions = settings["schematic_head_dimensions_m"][size]
    length = settings["under_head_length_m_assumed"][role]
    head_face = settings["outboard_head_face_m_assumed"][role]
    tip = head_face - length
    nut_height = dimensions["nut_height"]
    if tip + nut_height + 0.0012 >= 0:
        raise ValueError(f"Schematic pin too short for illustrated axial stack: {prefix}")
    start_position = np.array(position, dtype=float)
    end_position = start_position.copy()
    start_position[1] = sign * head_face
    end_position[1] = sign * tip
    visual_geom(parent, name=f"{prefix}_shaft", type="cylinder",
                fromto=numbers([*start_position, *end_position]),
                size=str(settings["nominal_shaft_diameter_m"][size] / 2), material="steel")
    head_position = start_position.copy()
    if head_style == "hex":
        head_height = dimensions["hex_height"]
        head_position[1] += sign * head_height / 2
        head = add_mesh(parent, asset, directory, f"{prefix}_head",
                        hex_mesh(dimensions["hex_across_flats"], head_height), "gold")
        head.set("pos", numbers(head_position))
    else:
        head_height = dimensions["socket_height"]
        head_position[1] += sign * head_height / 2
        visual_geom(parent, name=f"{prefix}_head", type="cylinder", pos=numbers(head_position),
                    zaxis="0 1 0", size=numbers([dimensions["socket_diameter"] / 2, head_height / 2]), material="steel")
        socket_position = start_position.copy()
        socket_position[1] += sign * (head_height + 0.00002)
        recess = add_mesh(parent, asset, directory, f"{prefix}_socket",
                          hex_mesh(settings["nominal_shaft_diameter_m"][size] * 0.65, 0.00004), "socket_dark")
        recess.set("pos", numbers(socket_position))
    nut_position = np.array(position, dtype=float)
    nut_position[1] = sign * (tip + 0.0012 + nut_height / 2)
    nut = add_mesh(parent, asset, directory, f"{prefix}_nut",
                   hex_mesh(dimensions["hex_across_flats"], nut_height), "gold")
    nut.set("pos", numbers(nut_position))
    ET.SubElement(parent, "site", name=f"{prefix}_axis", pos=numbers(position), size="0.0006", rgba="0.9 0.6 0.1 1", group="4")
    return {"name": prefix, "owner": parent.get("name"), "size_user_reported": size,
            "axis_site": f"{prefix}_axis", "point_owner_local_m": list(position),
            "cad_component": source_component, "cad_point_part_local_m": cad_point.tolist(),
            "under_head_length_m_assumed": length, "head_style_photo_inferred": head_style,
            "physics": "Ideal existing link pivot/retainer; visual pin adds no mass, contact or independent degree of freedom"}


def add_wheel_servo(lower, wheel, settings, side):
    sign = 1 if side == "left" else -1
    envelope = np.array(settings["wheel_servo"]["case_envelope_m_supplier"])
    center = np.array(settings["wheel_servo"]["case_offset_from_wheel_axis_m_assumed"])
    center[1] *= sign
    center[2] -= 0.09927
    visual_geom(lower, name=f"{side}_wheel_servo_case", type="box", pos=numbers(center),
                size=numbers(envelope / 2), material="servo_black")
    visual_geom(wheel, name=f"{side}_wheel_output_shaft", type="cylinder",
                fromto=numbers([0, -sign * 0.04, 0, 0, 0, 0]),
                size=str(settings["wheel_servo"]["shaft_diameter_m_assumed"] / 2), material="gold")
    ET.SubElement(lower, "site", name=f"{side}_wheel_mount_axis", pos="0 0 -0.09927", size="0.001", group="4")
    ET.SubElement(wheel, "site", name=f"{side}_wheel_output_axis", pos="0 0 0", size="0.001", group="4")


def apply_hardware(output, settings):
    directory = output / "simulation"
    assets_directory = directory / "assets"
    document = ET.parse(directory / "talos.xml")
    root = document.getroot()
    root.set("model", "TALOS_CAD_M4_M3_pivots_provisional")
    asset = root.find("asset")
    for name, color, shine in [("steel", "0.72 0.74 0.75 1", "0.8"),
                               ("gold", "0.65 0.48 0.23 1", "0.7"),
                               ("printed_clip", "0.07 0.1 0.1 1", "0.15"),
                               ("servo_black", "0.035 0.04 0.04 1", "0.3"),
                               ("socket_dark", "0.015 0.018 0.018 1", "0.05")]:
        ET.SubElement(asset, "material", name=name, rgba=color, specular=shine, shininess="0.5")
    components = {component["name"]: component for component in read_json(output / "native-cad.json")["components"]}
    scene = trimesh.load(directory / "source-assembly.glb", force="scene")
    results = read_json(output / "results.json")
    records = {"pins": [], "clips": [], "wheel_servos": [], "photo_filenames": PHOTOS,
               "clip_alignment_user_confirmed": settings["clips_stay_aligned_with_links_user_confirmed"]}
    for side in ["left", "right"]:
        sign = 1 if side == "left" else -1
        shapes = results["cad"][side]
        upper = root.find(f".//body[@name='{side}_upper']")
        lower = root.find(f".//body[@name='{side}_lower']")
        support = root.find(f".//body[@name='{side}_support']")
        wheel = root.find(f".//body[@name='{side}_wheel']")
        upper_component = components[shapes["upper_name"]]
        support_component = components[shapes["support_name"]]
        upper_mid = cylinder_bore(upper_component, 0.00495, 0.01, 0.09)
        support_mid = cylinder_bore(support_component, 0.004)
        support_hip = cylinder_bore(support_component, 0.005, 0.05)
        support_follower = cylinder_bore(support_component, 0.005, maximum_y=-0.01)
        upper_point = [0, 0, upper_mid[1] - 0.1]
        support_point = [0, 0, support_mid[1] - support_hip[1]]
        follower_point = [0, 0, -shapes["support_length_m"]]
        for index in [1, 2]:
            mesh_name = f"{side}_lower_{index}"
            for geom in list(lower.findall("geom")):
                if geom.get("mesh") == mesh_name:
                    lower.remove(geom)
            mesh_element = asset.find(f"mesh[@name='{mesh_name}']")
            if mesh_element is not None:
                asset.remove(mesh_element)
        upper_clip_name = "Left Wheel Clip-2" if side == "left" else "Right Wheel Clip -2"
        support_clip_name = "support clips - Left-1" if side == "left" else "support clips - Right-1"
        upper_clip = components[upper_clip_name]
        support_clip = components[support_clip_name]
        upper_holes = [cylinder_bore(upper_clip, 0.00495, -0.01), cylinder_bore(upper_clip, 0.00495, maximum_y=-0.01)]
        support_holes = [cylinder_bore(support_clip, 0.005), cylinder_bore(support_clip, 0.004)]
        upper_matrix = np.array([[-sign, 0, 0], [0, 0, -sign], [0, -1, 0]])
        support_matrix = np.array([[sign, 0, 0], [0, 0, -sign], [0, 1, 0]])
        upper_origin = np.array([0, sign * settings["clip_outer_face_m_assumed"]["upper"], -0.1])
        support_origin = np.array([0, sign * settings["clip_outer_face_m_assumed"]["support"],
                                   -shapes["support_length_m"] - support_holes[0][1]])
        records["clips"].append(register_clip(upper, asset, scene, assets_directory, upper_clip_name,
                                              f"{side}_upper_retaining_clip", upper_matrix, upper_origin,
                                              upper_holes, np.array([[0, 0, -0.1], upper_point])))
        records["clips"].append(register_clip(support, asset, scene, assets_directory, support_clip_name,
                                              f"{side}_support_retaining_clip", support_matrix, support_origin,
                                              support_holes, np.array([follower_point, support_point])))
        pin_specs = [
            (support, "support_hip", "M4", [0, 0, 0], support_component, support_hip, "hex"),
            (upper, "knee", "M4", [0, 0, -0.1], upper_component, np.zeros(3), "hex"),
            (support, "follower", "M4", follower_point, support_component, support_follower, "hex"),
            (upper, "upper_retainer", "M4", upper_point, upper_component, upper_mid, "socket"),
            (support, "support_retainer", "M3", support_point, support_component, support_mid, "socket"),
        ]
        for parent, role, size, point, component, cad_point, head_style in pin_specs:
            records["pins"].append(add_pin(parent, asset, assets_directory, settings, side, role, size,
                                           point, component["name"], cad_point, head_style))
        ET.SubElement(upper, "site", name=f"{side}_driven_hip_axis", pos="0 0 0", size="0.001", group="4")
        add_wheel_servo(lower, wheel, settings, side)
        records["wheel_servos"].append({"side": side, "case_owner": f"{side}_lower",
                                         "output_owner": f"{side}_wheel", "joint": f"{side}_wheel_joint",
                                         "wheel_axis_lower_part_local_m": [0, -0.12408, 0],
                                         "wheel_axis_lower_body_local_m": [0, 0, -0.09927],
                                         "mounting_screws": "Not modeled as rotating linkage pivots"})
    ET.indent(root)
    document.write(directory / "talos.xml", encoding="utf-8", xml_declaration=True)
    config = read_json(directory / "model_assumptions.json")
    config["hardware_extension"] = {"settings": settings, "mapping_file": "hardware_mapping.json",
                                     "counts_annotated_pins_only": {"M4": 8, "M3": 2},
                                     "additional_actuators": 0, "additional_joint_degrees_of_freedom": 0,
                                     "clip_ownership_corrected": True}
    config["limitations"].extend([
        "M4/M3 nominal diameters and clip alignment are user supplied; lengths, heads, nuts and axial stack are schematic",
        "Clip bores registered from native part coordinates, not the mismated original clip assembly transforms",
        "All added hardware and servo cases are non-colliding zero-mass visuals; existing lumped dynamics are unchanged",
        "Retaining clips stay aligned with their links; their two pins do not introduce independent mechanism joints",
        "Wheel servo case is fixed to lower link; its output is coaxial with wheel joint; spline/adapter dimensions unmeasured",
        "No bolt/PLA contact, preload, clearance, friction, wear, stress or fastener mass identified by this extension",
    ])
    save_json(directory / "model_assumptions.json", config)
    save_json(directory / "hardware_mapping.json", records)
    return records, config


def verify_alignment(model, data, records):
    checks = []
    for pin in records["pins"]:
        site = model.site(pin["axis_site"]).id
        shaft = model.geom(f"{pin['name']}_shaft").id
        owner = model.body(pin["owner"]).id
        axis = data.xmat[owner].reshape(3, 3)[:, 1]
        shaft_axis = data.geom_xmat[shaft].reshape(3, 3)[:, 2]
        offset = data.site_xpos[site] - data.geom_xpos[shaft]
        residual = float(np.linalg.norm(np.cross(offset, axis)))
        assert int(model.geom_bodyid[shaft]) == owner
        assert residual < 1e-9
        assert abs(float(np.dot(axis, shaft_axis))) > 1 - 1e-10
        assert model.geom_contype[shaft] == model.geom_conaffinity[shaft] == 0
        checks.append({"pin": pin["name"], "shaft_to_pivot_axis_residual_m": residual})
    for clip in records["clips"]:
        assert int(model.geom_bodyid[model.geom(clip["name"]).id]) == model.body(clip["owner"]).id
    wheel_checks = []
    for side in ["left", "right"]:
        mount = data.site_xpos[model.site(f"{side}_wheel_mount_axis").id]
        output = data.site_xpos[model.site(f"{side}_wheel_output_axis").id]
        lower_id = model.body(f"{side}_lower").id
        axis = data.xmat[lower_id].reshape(3, 3)[:, 1]
        residual = float(np.linalg.norm(np.cross(output - mount, axis)))
        assert residual < 1e-9
        assert model.geom_bodyid[model.geom(f"{side}_wheel_servo_case").id] == lower_id
        assert model.geom_bodyid[model.geom(f"{side}_wheel_output_shaft").id] == model.body(f"{side}_wheel").id
        case_id = model.geom(f"{side}_wheel_servo_case").id
        shaft_id = model.geom(f"{side}_wheel_output_shaft").id
        case_matrix_before = data.geom_xmat[case_id].copy()
        shaft_matrix_before = data.geom_xmat[shaft_id].copy()
        positions_before = data.qpos.copy()
        wheel_address = model.jnt_qposadr[model.joint(f"{side}_wheel_joint").id]
        data.qpos[wheel_address] += math.pi / 3
        mujoco.mj_forward(model, data)
        np.testing.assert_allclose(data.geom_xmat[case_id], case_matrix_before, atol=1e-12, rtol=0)
        assert np.linalg.norm(data.geom_xmat[shaft_id] - shaft_matrix_before) > 0.1
        data.qpos[:] = positions_before
        mujoco.mj_forward(model, data)
        wheel_checks.append({"side": side, "wheel_output_coaxial_residual_m": residual,
                             "wheel_60_degree_rotation_keeps_servo_case_fixed": True,
                             "output_shaft_rotates_with_wheel": True})
    assert model.nu == 4
    assert max(closure_errors(model, data)) < 1e-8
    return {"pins": checks, "wheel_servos": wheel_checks, "fourbar_closure_errors_m": closure_errors(model, data)}


def generate_hardware(output, settings, total_mass=2.0, height=None):
    baseline, baseline_data, config = generate(output, total_mass=total_mass, height_m=height)
    records, config = apply_hardware(output, settings)
    model = mujoco.MjModel.from_xml_path(str(output / "simulation" / "talos.xml"))
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    assert (model.nq, model.nv, model.nu, model.neq) == (baseline.nq, baseline.nv, baseline.nu, baseline.neq)
    np.testing.assert_allclose(model.body_mass, baseline.body_mass, atol=1e-14, rtol=0)
    np.testing.assert_allclose(model.body_inertia, baseline.body_inertia, atol=1e-14, rtol=0)
    proof = verify_alignment(model, data, records)
    proof.update({"hip_angles_deg": np.degrees(config["leg_q_reference_rad"]).tolist(),
                  "total_mass_kg_illustrative": float(sum(model.body_mass)),
                  "actuators": model.nu, "positions": model.nq, "velocities": model.nv,
                  "equalities": model.neq, "mass_and_inertia_match_baseline": True})
    return model, data, config, records, proof, baseline


def render_image(model, data, camera, width=1100, height=800, hide_far_leg=None, hide_wheels=False):
    option = mujoco.MjvOption()
    option.sitegroup[:] = 0
    renderer = mujoco.Renderer(model, height=height, width=width)
    try:
        renderer.update_scene(data, camera=camera, scene_option=option)
        hidden_bodies = {model.body(f"{hide_far_leg}_{role}").id for role in ["upper", "lower", "support", "wheel"]} if hide_far_leg else set()
        wheel_bodies = {model.body(f"{side}_wheel").id for side in ["left", "right"]}
        for geom in renderer.scene.geoms[:renderer.scene.ngeom]:
            if geom.objtype == mujoco.mjtObj.mjOBJ_GEOM and geom.objid >= 0:
                body_id = int(model.geom_bodyid[geom.objid])
                name = model.geom(geom.objid).name or ""
                if body_id in hidden_bodies or (hide_wheels and body_id in wheel_bodies and not name.endswith("wheel_output_shaft")):
                    geom.rgba[3] = 0
        return Image.fromarray(renderer.render())
    finally:
        renderer.close()


def render_views(model, data, output):
    torso = data.xpos[model.body("torso").id]
    camera = mujoco.MjvCamera()
    camera.lookat[:] = torso + [0.025, 0, -0.035]
    camera.distance = 0.53
    camera.azimuth = 135
    camera.elevation = -18
    render_image(model, data, camera).save(output / "figures" / "hardware_overview.png")
    for side, azimuth, far in [("left", 270, "right"), ("right", 90, "left")]:
        hip = data.xpos[model.body(f"{side}_upper").id]
        knee = data.xpos[model.body(f"{side}_lower").id]
        camera.lookat[:] = (hip + knee) / 2 + [0.015, 0, -0.012]
        camera.distance = 0.29
        camera.azimuth = azimuth
        camera.elevation = -4
        render_image(model, data, camera, hide_far_leg=far).save(output / "figures" / f"{side}_pivot_closeup.png")
        wheel = data.xpos[model.body(f"{side}_wheel").id]
        camera.lookat[:] = wheel + [0, 0, 0.012]
        camera.distance = 0.20
        camera.azimuth = azimuth - 30
        camera.elevation = -15
        render_image(model, data, camera, hide_far_leg=far, hide_wheels=True).save(output / "figures" / f"{side}_wheel_removed.png")


def compare_dynamics(baseline, model, steps=1000):
    baseline_data = mujoco.MjData(baseline)
    hardware_data = mujoco.MjData(model)
    maximum_error = 0.0
    for unused_step in range(steps):
        mujoco.mj_step(baseline, baseline_data)
        mujoco.mj_step(model, hardware_data)
        if not np.isfinite(hardware_data.qpos).all() or not np.isfinite(hardware_data.qvel).all():
            raise ValueError("Non-finite simulation state")
        maximum_error = max(maximum_error, float(np.max(np.abs(baseline_data.qpos - hardware_data.qpos))))
    assert maximum_error < 1e-10, maximum_error
    return {"steps": steps, "simulated_seconds": steps * model.opt.timestep,
            "maximum_qpos_difference_from_baseline": maximum_error,
            "finite_state": True, "scope": "Uncontrolled smoke test and visual-only regression, not a balancing result"}


def pivot_diagram(model, data, records, output):
    import matplotlib.pyplot as plt
    figure, axes = plt.subplots(1, 2, figsize=(12, 5.5), constrained_layout=True)
    colors = {"support_hip": "#99701e", "knee": "#99701e", "follower": "#99701e",
              "upper_retainer": "#386c96", "support_retainer": "#9859a3"}
    for diagram, side in zip(axes, ["left", "right"]):
        hip = data.site_xpos[model.site(f"{side}_driven_hip_axis").id][[0, 2]]
        points = {pin["name"].removeprefix(f"{side}_"): data.site_xpos[model.site(pin["axis_site"]).id][[0, 2]]
                  for pin in records["pins"] if pin["name"].startswith(side)}
        wheel = data.site_xpos[model.site(f"{side}_wheel_output_axis").id][[0, 2]]
        for chain, color in [([hip, points["knee"]], "#184b43"),
                             ([points["support_hip"], points["follower"]], "#6a867d"),
                             ([points["follower"], points["knee"], wheel], "#2d3333")]:
            coordinates = (np.array(chain) - hip) * 1000
            diagram.plot(coordinates[:, 0], coordinates[:, 1], color=color, linewidth=5, zorder=1)
        for role, point in points.items():
            location = (point - hip) * 1000
            label = role.replace("_", " ") + (" M3" if role == "support_retainer" else " M4")
            offset = (-85, 10) if role in ["upper_retainer", "knee"] else (12, 12)
            diagram.scatter(*location, s=75, color=colors[role], edgecolors="white", zorder=3)
            diagram.annotate(label, location, xytext=offset, textcoords="offset points", fontsize=9,
                             arrowprops={"arrowstyle": "-", "color": colors[role]})
        diagram.scatter(0, 0, marker="o", s=100, facecolors="none", edgecolors="#cc4d29", linewidths=2)
        diagram.annotate("Leg servo output", (0, 0), xytext=(-55, 20), textcoords="offset points", fontsize=9)
        wheel_point = (wheel - hip) * 1000
        diagram.scatter(*wheel_point, marker="+", s=100, color="#cc4d29", linewidths=2)
        diagram.annotate("Wheel servo / hub", wheel_point, xytext=(10, -12), textcoords="offset points", fontsize=9)
        diagram.set_title(f"{side.title()} leg: CAD-derived pivot centres", loc="left", fontsize=12)
        diagram.set_xlabel("Sagittal x relative to driven hip [mm]")
        diagram.set_ylabel("Vertical z relative to driven hip [mm]")
        diagram.set_aspect("equal")
        diagram.margins(x=0.6, y=0.2)
        diagram.grid(alpha=0.15)
    figure.suptitle("Pins follow their owning links; only actual linkage pivots introduce motion", fontsize=13)
    figure.savefig(output / "figures" / "pivot_axis_map.png", dpi=170)
    plt.close(figure)


def pose_sheet(images, output):
    sheet = Image.new("RGB", (1500, 535), "#f3f6f5")
    drawing = ImageDraw.Draw(sheet)
    font_path = Path("C:/Windows/Fonts/segoeui.ttf")
    font = ImageFont.truetype(str(font_path), 21) if font_path.exists() else ImageFont.load_default()
    drawing.text((24, 14), "TALOS - photo-informed M4/M3 pivot model", fill="#193e36", font=font)
    for index, (height, image) in enumerate(images):
        image = image.resize((500, 364))
        sheet.paste(image, (500 * index, 55))
        drawing.text((500 * index + 24, 430), f"Driven hip height: {height * 1000:.0f} mm", fill="#193e36", font=font)
    drawing.text((24, 486), "Offline pose checks only. Fastener lengths, axial stack and masses are not measured.", fill="#4c5c58", font=font)
    sheet.save(output / "figures" / "hardware_pose_checks.png")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_HARDWARE_OUTPUT)
    parser.add_argument("--hardware-inputs", type=Path, default=HERE / "hardware_inputs.json")
    parser.add_argument("--mass", type=float, default=2.0)
    arguments = parser.parse_args()
    settings = read_json(arguments.hardware_inputs)
    source_hashes_before = directory_hashes(arguments.source_output)
    native_source = Path(read_json(arguments.source_output / "inputs.json")["native_cad_path"]).parent
    cad_hashes_before = directory_hashes(native_source) if native_source.exists() else None
    prepare(arguments.source_output, arguments.output)
    checks = []
    pose_images = []
    for height in [0.15, 0.18, 0.21]:
        model, data, config, records, proof, baseline = generate_hardware(arguments.output, settings, arguments.mass, height)
        label = f"{height * 1000:.0f}"
        shutil.copy2(arguments.output / "simulation" / "talos.xml", arguments.output / "simulation" / f"talos-h{label}.xml")
        save_json(arguments.output / "simulation" / f"talos-h{label}-assumptions.json", config)
        proof["left_driven_hip_height_m"] = height
        proof["wheel_center_heights_m"] = [float(data.xpos[model.body(f"{side}_wheel").id, 2]) for side in ["left", "right"]]
        checks.append(proof)
        camera = mujoco.MjvCamera()
        camera.lookat[:] = data.xpos[model.body("torso").id] + [0.02, 0, -0.035]
        camera.distance = 0.57
        camera.azimuth = 135
        camera.elevation = -18
        pose_image = render_image(model, data, camera)
        pose_image.save(arguments.output / "figures" / f"hardware_h{label}.png")
        pose_images.append((height, pose_image))
    model, data, config, records, proof, baseline = generate_hardware(arguments.output, settings, arguments.mass)
    proof["pose_checks"] = checks
    proof["dynamics_regression"] = compare_dynamics(baseline, model)
    render_views(model, data, arguments.output)
    pivot_diagram(model, data, records, arguments.output)
    pose_sheet(pose_images, arguments.output)
    source_hashes_after = directory_hashes(arguments.source_output)
    cad_hashes_after = directory_hashes(native_source) if native_source.exists() else None
    assert source_hashes_before == source_hashes_after, "Archived CAE files changed"
    assert cad_hashes_before == cad_hashes_after, "Original CAD files changed"
    proof["archived_assessment_unchanged"] = True
    proof["original_cad_hashes_unchanged"] = cad_hashes_before == cad_hashes_after if cad_hashes_before is not None else None
    proof["source_cad_file_count"] = len(cad_hashes_before) if cad_hashes_before else 0
    proof["model_sha256"] = file_hash(arguments.output / "simulation" / "talos.xml")
    proof["hardware_mapping_sha256"] = file_hash(arguments.output / "simulation" / "hardware_mapping.json")
    proof["all_meshes_sha256"] = {mesh.get("file"): file_hash(arguments.output / "simulation" / mesh.get("file"))
                                 for mesh in ET.parse(arguments.output / "simulation" / "talos.xml").findall("asset/mesh")}
    proof["trained"] = False
    proof["hardware_motion_performed"] = False
    save_json(arguments.output / "verification.json", proof)
    save_json(arguments.output / "preserved_source_hashes.json", {"archived_assessment": source_hashes_before, "original_cad": cad_hashes_before})
    shutil.copy2(arguments.hardware_inputs, arguments.output / "hardware_inputs.json")
    print({"output": str(arguments.output), "M4": 8, "M3": 2, "pose_checks": len(checks),
           "clip_registration_max_mm": max(max(clip["in_plane_registration_residual_m"]) for clip in records["clips"]) * 1000,
           "native_CAD_unchanged": proof["original_cad_hashes_unchanged"],
           "dynamics_regression": proof["dynamics_regression"]})


if __name__ == "__main__":
    main()
