import argparse
import math
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path

import mujoco
import numpy as np
from scipy.optimize import brentq
import trimesh

from analyse import DEFAULT_OUTPUT, GRAVITY, ROOT, leg_load, read_json, rotation, save_json, solve_leg, transform

CAD_TO_SIM = np.array([[1, 0, 0], [0, 0, -1], [0, 1, 0]])


def numbers(values):
    return " ".join(f"{value:.10g}" for value in np.asarray(values).ravel())


def rotation3(angle):
    result = np.eye(3)
    result[:2, :2] = rotation(angle)
    return result


def add_visual(parent, asset, scene, components, names, reference, angle, directory, prefix):
    for index, name in enumerate(names):
        mesh = scene.geometry[scene.graph[name][1]].copy()
        component = components[name]
        values = component["transform"]
        global_vertices = mesh.vertices @ np.array(values[:9]).reshape(3, 3) + np.array(values[9:12])
        mesh.vertices = (global_vertices - reference) @ rotation3(-angle).T @ CAD_TO_SIM.T
        stem = f"{prefix}_{index}"
        mesh.export(directory / f"{stem}.obj")
        ET.SubElement(asset, "mesh", name=stem, file=f"assets/{stem}.obj")
        ET.SubElement(parent, "geom", type="mesh", mesh=stem, mass="0", contype="0", conaffinity="0", group="2", rgba="0.12 0.22 0.21 1")


def generate(output, total_mass=2.0, step_mm=0, height_m=None):
    if total_mass <= 0.6:
        raise ValueError("The illustrative mass distribution requires total mass >0.6kg")
    inputs = read_json(output / "inputs.json")
    results = read_json(output / "results.json")
    shapes = results["cad"]
    native = read_json(output / "native-cad.json")
    components = {component["name"]: component for component in native["components"]}
    directory = output / "simulation"
    assets = directory / "assets"
    assets.mkdir(parents=True, exist_ok=True)
    source_glb = Path(inputs["cad_glb_path"])
    if not source_glb.is_absolute():
        source_glb = ROOT / source_glb
    if not source_glb.exists():
        source_glb = directory / "source-assembly.glb"
    scene = trimesh.load(source_glb, force="scene")
    if source_glb.resolve() != (directory / "source-assembly.glb").resolve():
        shutil.copy2(source_glb, directory / "source-assembly.glb")
    origin = np.mean([shapes[side]["drive_world_m"] for side in ["left", "right"]], axis=0)
    radius = inputs["wheel_diameter_m"] / 2
    reference_angles = [math.radians(50)]
    if height_m is not None:
        if not 0.145 <= height_m <= 0.21:
            raise ValueError("Provisional height command must be between 0.145 and 0.210m; not a certified hardware range")
        reference_angles[0] = brentq(lambda angle: radius - solve_leg(shapes["left"], angle)["wheel"][1] - height_m,
                                    math.radians(25), math.radians(60))
    left_pose = solve_leg(shapes["left"], reference_angles[0])
    left_vertical = shapes["left"]["drive_world_m"][1] - origin[1] + left_pose["wheel"][1]
    right_angle = brentq(lambda angle: shapes["right"]["drive_world_m"][1] - origin[1] + solve_leg(shapes["right"], angle)["wheel"][1] - left_vertical,
                        math.radians(25), math.radians(60))
    reference_angles.append(right_angle)
    initial_height = radius - left_vertical
    document = ET.Element("mujoco", model="TALOS_provisional_CAD_fourbar")
    ET.SubElement(document, "compiler", angle="radian", autolimits="true", inertiafromgeom="auto")
    ET.SubElement(document, "option", timestep="0.002", gravity="0 0 -9.80665", integrator="implicitfast", iterations="80", tolerance="1e-9")
    defaults = ET.SubElement(document, "default")
    ET.SubElement(defaults, "joint", damping="0.003", armature="0.00005")
    ET.SubElement(defaults, "geom", contype="1", conaffinity="2", friction="0.8 0.003 0.0001", solref="0.006 1", solimp="0.95 0.99 0.001")
    visual = ET.SubElement(document, "visual")
    ET.SubElement(visual, "global", offwidth="1200", offheight="800")
    ET.SubElement(visual, "headlight", ambient="0.5 0.5 0.5", diffuse="0.7 0.7 0.7")
    asset = ET.SubElement(document, "asset")
    ET.SubElement(asset, "texture", name="sky", type="skybox", builtin="gradient", rgb1="0.87 0.93 0.92", rgb2="0.97 0.98 0.96", width="512", height="3072")
    world = ET.SubElement(document, "worldbody")
    ET.SubElement(world, "light", pos="0 -2 3", dir="0 0 -1", directional="true")
    ET.SubElement(world, "geom", name="floor", type="plane", size="3 3 0.1", rgba="0.91 0.92 0.9 1", contype="2", conaffinity="1")
    if step_mm > 0:
        ET.SubElement(world, "geom", name="obstacle", type="box", pos=numbers([0.45, 0, step_mm / 2000]), size=numbers([0.05, 0.8, step_mm / 2000]), rgba="0.6 0.5 0.3 1", contype="2", conaffinity="1")
    body = ET.SubElement(world, "body", name="torso", pos=numbers([0, 0, initial_height]))
    ET.SubElement(body, "freejoint", name="base")
    torso_mass = total_mass - 2 * (0.025 + 0.015 + 0.095 + 0.06)
    torso_inertia = torso_mass / 12 * np.array([0.18 ** 2 + 0.1 ** 2, 0.1 ** 2 + 0.1 ** 2, 0.1 ** 2 + 0.18 ** 2])
    ET.SubElement(body, "inertial", mass=str(torso_mass), pos="0 0 0.03", diaginertia=numbers(torso_inertia))
    head_minimum, head_maximum = np.array(shapes["head_bbox_world_m"])
    head_center = CAD_TO_SIM @ ((head_minimum + head_maximum) / 2 - origin)
    head_half_size = np.abs(CAD_TO_SIM) @ ((head_maximum - head_minimum) / 2)
    ET.SubElement(body, "geom", name="body_collision", type="box", pos=numbers(head_center), size=numbers(head_half_size), mass="0", group="3", rgba="0.1 0.2 0.2 0")
    add_visual(body, asset, scene, components, ["Head-1", "Face-1", "left-encoder-cover-1"], origin, 0, assets, "body")
    ET.SubElement(body, "site", name="imu", pos="0 0 0.03", size="0.003", rgba="0.9 0.5 0.2 1")
    equality = ET.SubElement(document, "equality")
    actuators = ET.SubElement(document, "actuator")
    config = {"status": "Offline provisional model. Not calibrated, not a trained hardware policy.",
              "total_mass_kg_example_not_measured": total_mass, "leg_q_reference_rad": reference_angles,
              "body_com_above_driven_hip_m_assumed": 0.03, "physics_dt_s": 0.002, "servo_frame_dt_s": 0.02,
              "wheel_radius_m": radius, "wheel_speed_limit_rad_s_assumed": 2 * math.pi * 50 / 60,
              "leg_speed_limit_rad_s_assumed": 2 * math.pi * 72 / 60,
              "servo_lag_s_assumed": [0.08, 0.08, 0.06, 0.06], "servo_command_delay_frames_assumed": 1,
              "deadband_fraction_assumed": 0.03, "initial_root_height_m": initial_height,
              "initial_left_driven_hip_height_m": radius - left_pose["wheel"][1],
              "limitations": ["Masses, inertia, CoM, friction, motor curve, lag and deadband are illustrative",
                              "Four-bar planar closure repaired from mismated native CAD",
                              "Joint axial offsets collapsed to a common plane; no modeled bolt clearance or backlash",
                              "M4 directly against PLA reported; clamp-dependent friction, stiction and wear are not identified or modeled",
                              "CAD meshes are visual only; primitive ground collisions, no robot self-collision",
                              "Tires are 85mm primitive cylinders, not an exported rim/tread CAD",
                              "No rail sag, BMS/current trip, thermal derating or contact sensor model",
                              "Only IMU and leg encoders in actor observations; wheel speed is not directly measured"]}
    for index, side in enumerate(["left", "right"]):
        parameters = shapes[side]
        angle = reference_angles[index]
        pose = solve_leg(parameters, angle)
        hip_position = CAD_TO_SIM @ (np.array(parameters["drive_world_m"]) - origin)
        upper = ET.SubElement(body, "body", name=f"{side}_upper", pos=numbers(hip_position), euler=numbers([0, -angle, 0]))
        ET.SubElement(upper, "joint", name=f"{side}_hip", type="hinge", axis="0 -1 0", ref=str(angle), range=numbers(np.radians([25, 60])))
        ET.SubElement(upper, "geom", type="capsule", fromto="0 0 0 0 0 -0.1", size="0.006", mass="0.025", group="3", rgba="0.2 0.5 0.4 0")
        add_visual(upper, asset, scene, components, [parameters["upper_name"]], np.array(parameters["drive_world_m"]),
                   math.radians(parameters["reference_angle_deg"]), assets, f"{side}_upper")
        lower_angle = math.atan2(*(pose["knee"] - pose["follower"]) * [1, -1])
        lower = ET.SubElement(upper, "body", name=f"{side}_lower", pos="0 0 -0.1", euler=numbers([0, -(lower_angle - angle), 0]))
        ET.SubElement(lower, "joint", name=f"{side}_knee_passive", type="hinge", axis="0 -1 0")
        ET.SubElement(lower, "geom", type="capsule", fromto="0 0 0.02481 0 0 -0.09927", size="0.007", mass="0.095", group="3", rgba="0.3 0.3 0.3 0")
        ET.SubElement(lower, "site", name=f"{side}_lower_follower", pos="0 0 0.02481", size="0.001")
        original_lower = components[parameters["lower_name"]]
        knee_reference = transform(original_lower, [0, -0.02481, 0])
        original_vector = knee_reference - transform(original_lower, [0, 0, 0])
        original_angle = math.atan2(original_vector[0], -original_vector[1])
        lower_names = [parameters["lower_name"], "Left Wheel Clip-2", "support clips - Left-1"] if side == "left" else [parameters["lower_name"], "Right Wheel Clip -2", "support clips - Right-1"]
        add_visual(lower, asset, scene, components, lower_names, knee_reference, original_angle, assets, f"{side}_lower")
        wheel = ET.SubElement(lower, "body", name=f"{side}_wheel", pos=numbers([0, 0.03 if side == "left" else -0.03, -0.09927]))
        ET.SubElement(wheel, "joint", name=f"{side}_wheel_joint", type="hinge", axis="0 1 0", damping="0.001")
        ET.SubElement(wheel, "geom", name=f"{side}_tire", type="cylinder", size=numbers([radius, 0.015]), euler=numbers([math.pi / 2, 0, 0]), mass="0.06", rgba="0.07 0.08 0.08 1")
        ET.SubElement(wheel, "geom", type="cylinder", size=numbers([radius * 0.62, 0.0152]), euler=numbers([math.pi / 2, 0, 0]), mass="0", contype="0", conaffinity="0", rgba="0.65 0.72 0.7 1")
        support_vector = pose["follower"] - pose["support"]
        support_angle = math.atan2(support_vector[0], -support_vector[1])
        support_pos = hip_position + CAD_TO_SIM @ np.array([*parameters["support_offset_m"], 0.0])
        support = ET.SubElement(body, "body", name=f"{side}_support", pos=numbers(support_pos), euler=numbers([0, -support_angle, 0]))
        ET.SubElement(support, "joint", name=f"{side}_support_passive", type="hinge", axis="0 -1 0")
        length = parameters["support_length_m"]
        ET.SubElement(support, "geom", type="capsule", fromto=numbers([0, 0, 0, 0, 0, -length]), size="0.004", mass="0.015", group="3", rgba="0.3 0.3 0.3 0")
        ET.SubElement(support, "site", name=f"{side}_support_tip", pos=numbers([0, 0, -length]), size="0.001")
        original_support = components[parameters["support_name"]]
        support_reference = np.array(parameters["support_world_m"])
        original_vector = transform(original_support, [0, -0.02578756359973451, 0]) - support_reference
        add_visual(support, asset, scene, components, [parameters["support_name"]], support_reference,
                   math.atan2(original_vector[0], -original_vector[1]), assets, f"{side}_support")
        ET.SubElement(equality, "connect", name=f"{side}_fourbar", site1=f"{side}_lower_follower", site2=f"{side}_support_tip", solref="0.004 1", solimp="0.999 0.9999 0.00001")
        leg_speed = config["leg_speed_limit_rad_s_assumed"]
        torque = results["leg_torque_6v_interpolated_nm_not_continuous"]
        ET.SubElement(actuators, "velocity", name=f"{side}_leg_servo", joint=f"{side}_hip", kv=str(torque / leg_speed), ctrlrange=numbers([-leg_speed, leg_speed]), forcerange=numbers([-torque, torque]))
    for side in ["left", "right"]:
        wheel_speed = config["wheel_speed_limit_rad_s_assumed"]
        torque = results["wheel_torque_reference_nm_not_continuous"]
        ET.SubElement(actuators, "velocity", name=f"{side}_wheel_servo", joint=f"{side}_wheel_joint", kv=str(torque / wheel_speed), ctrlrange=numbers([-wheel_speed, wheel_speed]), forcerange=numbers([-torque, torque]))
    sensors = ET.SubElement(document, "sensor")
    ET.SubElement(sensors, "gyro", name="imu_gyro", site="imu")
    ET.SubElement(sensors, "accelerometer", name="imu_accel", site="imu")
    for side in ["left", "right"]:
        ET.SubElement(sensors, "jointpos", name=f"{side}_encoder", joint=f"{side}_hip")
    ET.indent(document)
    ET.ElementTree(document).write(directory / "talos.xml", encoding="utf-8", xml_declaration=True)
    save_json(directory / "model_assumptions.json", config)
    model = mujoco.MjModel.from_xml_path(str(directory / "talos.xml"))
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    errors = closure_errors(model, data)
    assert max(errors) < 1e-8, errors
    assert math.isclose(float(sum(model.body_mass)), total_mass, abs_tol=1e-9)
    return model, data, config


def closure_errors(model, data):
    errors = []
    for side in ["left", "right"]:
        first = model.site(f"{side}_lower_follower").id
        second = model.site(f"{side}_support_tip").id
        errors.append(float(np.linalg.norm(data.site_xpos[first] - data.site_xpos[second])))
    return errors


def render(model, data, destination, elevation=-18, azimuth=135):
    from PIL import Image
    camera = mujoco.MjvCamera()
    camera.lookat[:] = data.xpos[model.body("torso").id] + [0, 0, -0.03]
    camera.distance = 0.75
    camera.azimuth = azimuth
    camera.elevation = elevation
    renderer = mujoco.Renderer(model, height=680, width=1000)
    try:
        renderer.update_scene(data, camera=camera)
        Image.fromarray(renderer.render()).save(destination)
    finally:
        renderer.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--mass", type=float, default=2.0)
    parser.add_argument("--step-mm", type=float, default=0)
    parser.add_argument("--height-m", type=float)
    parser.add_argument("--height-series", action="store_true")
    parser.add_argument("--view", action="store_true")
    arguments = parser.parse_args()
    if arguments.height_series:
        checks = []
        for height in [0.15, 0.18, 0.21]:
            model, data, config = generate(arguments.output, arguments.mass, arguments.step_mm, height)
            label = f"{height * 1000:.0f}"
            render(model, data, arguments.output / "figures" / f"simulation_h{label}.png")
            shutil.copy2(arguments.output / "simulation" / "talos.xml", arguments.output / "simulation" / f"talos-h{label}.xml")
            save_json(arguments.output / "simulation" / f"talos-h{label}-assumptions.json", config)
            checks.append({"left_hip_height_m": height, "hip_angles_deg": np.degrees(config["leg_q_reference_rad"]).tolist(),
                           "wheel_center_heights_m": [float(data.xpos[model.body(f"{side}_wheel").id, 2]) for side in ["left", "right"]],
                           "closure_errors_m": closure_errors(model, data)})
        save_json(arguments.output / "simulation" / "height_pose_checks.json", checks)
    model, data, config = generate(arguments.output, arguments.mass, arguments.step_mm, arguments.height_m)
    render(model, data, arguments.output / "figures" / "simulation_cad.png")
    print({"actuators": model.nu, "mass_kg": sum(model.body_mass), "closure_errors_m": closure_errors(model, data), "initial_hip_angles_deg": np.degrees(config["leg_q_reference_rad"]).tolist()})
    if arguments.view:
        import mujoco.viewer
        import time
        with mujoco.viewer.launch_passive(model, data) as viewer:
            while viewer.is_running():
                start = time.monotonic()
                mujoco.mj_step(model, data)
                viewer.sync()
                time.sleep(max(0, model.opt.timestep - (time.monotonic() - start)))
