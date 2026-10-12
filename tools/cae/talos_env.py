import argparse
import hashlib
import math
from pathlib import Path

import gymnasium as gym
from gymnasium import spaces
import mujoco
import numpy as np
from scipy.optimize import brentq

from analyse import DEFAULT_OUTPUT, GRAVITY, leg_load, read_json, save_csv, save_json, solve_leg
from simulation import closure_errors, render


class TalosEnv(gym.Env):
    metadata = {"render_modes": ["rgb_array"], "render_fps": 50}

    def __init__(self, output=DEFAULT_OUTPUT, randomize=False, seconds=10, vary_height=False):
        self.output = Path(output)
        self.model = mujoco.MjModel.from_xml_path(str(self.output / "simulation" / "talos.xml"))
        self.data = mujoco.MjData(self.model)
        self.config = read_json(self.output / "simulation" / "model_assumptions.json")
        self.results = read_json(self.output / "results.json")
        self.action_space = spaces.Box(-1, 1, shape=(4,), dtype=np.float32)
        self.observation_space = spaces.Box(-np.inf, np.inf, shape=(21,), dtype=np.float32)
        self.randomize = randomize
        self.vary_height = vary_height
        self.max_frames = round(seconds / 0.02)
        self.hip_position = [int(self.model.jnt_qposadr[self.model.joint(f"{side}_hip").id]) for side in ["left", "right"]]
        self.torso_id = self.model.body("torso").id
        self.speed_limits = np.array([self.config["leg_speed_limit_rad_s_assumed"]] * 2 + [self.config["wheel_speed_limit_rad_s_assumed"]] * 2)
        self.nominal = {attribute: getattr(self.model, attribute).copy() for attribute in
                        ["body_mass", "body_inertia", "geom_friction", "actuator_gainprm", "actuator_biasprm", "actuator_forcerange"]}
        self.renderer = None

    def reset(self, seed=None, options=None):
        super().reset(seed=seed)
        options = options or {}
        for attribute, values in self.nominal.items():
            getattr(self.model, attribute)[:] = values
        self.lag = np.array(self.config["servo_lag_s_assumed"])
        if self.randomize:
            mass_scale = self.np_random.uniform(0.9, 1.1)
            self.model.body_mass[:] *= mass_scale
            self.model.body_inertia[:] *= mass_scale
            self.model.geom_friction[:, 0] = self.np_random.uniform(0.6, 1.0)
            strength = self.np_random.uniform(0.8, 1.0, size=4)
            self.model.actuator_gainprm[:] *= strength[:, None]
            self.model.actuator_biasprm[:] *= strength[:, None]
            self.model.actuator_forcerange[:] *= strength[:, None]
            self.lag *= self.np_random.uniform(0.7, 1.3, size=4)
        mujoco.mj_setConst(self.model, self.data)
        mujoco.mj_resetData(self.model, self.data)
        pitch = math.radians(options.get("pitch_deg", self.np_random.uniform(-1, 1)))
        self.data.qpos[3:7] = [math.cos(pitch / 2), 0, math.sin(pitch / 2), 0]
        mujoco.mj_forward(self.model, self.data)
        self.filtered_action = np.zeros(4)
        self.previous_action = np.zeros(4)
        self.delayed_action = np.zeros(4)
        self.q_target = np.array(self.config["leg_q_reference_rad"])
        self.q_goal = self.q_target.copy()
        if self.vary_height:
            self.set_height(self.np_random.uniform(0.15, 0.21))
        self.command = np.zeros(2)
        self.gravity_estimate = -self.data.xmat[self.torso_id].reshape(3, 3)[2].copy()
        self.encoder = self.read_encoder()
        self.encoder_rate = np.zeros(2)
        self.frame = 0
        return self.observation(), self.information()

    def sensor(self, name):
        return self.data.sensor(name).data.copy()

    def read_encoder(self):
        values = self.data.qpos[self.hip_position]
        resolution = 2 * math.pi / 4096
        return np.round(values / resolution) * resolution

    def observation(self):
        gyro = self.sensor("imu_gyro") + self.np_random.normal(0, 0.005, 3)
        accel = self.sensor("imu_accel") / GRAVITY + self.np_random.normal(0, 0.005, 3)
        return np.concatenate([self.gravity_estimate, gyro, accel, self.encoder, self.encoder_rate,
                               self.previous_action, self.q_target, self.command]).astype(np.float32)

    def information(self):
        gravity = -self.data.xmat[self.torso_id].reshape(3, 3)[2]
        pitch = math.atan2(gravity[0], -gravity[2])
        roll = math.atan2(-gravity[1], -gravity[2])
        return {"time_s": float(self.data.time), "pitch_deg_truth": math.degrees(pitch),
                "roll_deg_truth": math.degrees(roll), "base_height_m_truth": float(self.data.qpos[2]),
                "joint_error_deg": np.degrees(self.encoder - self.q_target).tolist(),
                "closure_max_m": max(closure_errors(self.model, self.data)),
                "wheel_speed_rad_s_truth_not_actor_observation": [float(self.data.joint(f"{side}_wheel_joint").qvel[0]) for side in ["left", "right"]]}

    def step(self, action):
        requested = np.clip(np.array(action, dtype=float), -1, 1)
        if requested.shape != (4,) or not np.all(np.isfinite(requested)):
            raise ValueError("Four finite normalized continuous-servo speed commands required")
        self.q_target += np.clip(self.q_goal - self.q_target, -0.2 * 0.02, 0.2 * 0.02)
        deadband = self.config["deadband_fraction_assumed"]
        applied = np.sign(self.delayed_action) * np.maximum(0, (np.abs(self.delayed_action) - deadband) / (1 - deadband))
        self.delayed_action = requested.copy()
        smoothing = 1 - np.exp(-self.model.opt.timestep / self.lag)
        for substep in range(10):
            self.filtered_action += smoothing * (applied - self.filtered_action)
            self.data.ctrl[:] = self.filtered_action * self.speed_limits
            mujoco.mj_step(self.model, self.data)
            gyro = self.sensor("imu_gyro")
            accel = self.sensor("imu_accel")
            self.gravity_estimate -= np.cross(gyro, self.gravity_estimate) * self.model.opt.timestep
            magnitude = np.linalg.norm(accel)
            if 0.85 * GRAVITY < magnitude < 1.15 * GRAVITY:
                alpha = self.model.opt.timestep / (0.25 + self.model.opt.timestep)
                self.gravity_estimate = (1 - alpha) * self.gravity_estimate - alpha * accel / magnitude
            self.gravity_estimate /= max(np.linalg.norm(self.gravity_estimate), 1e-9)
        previous_encoder = self.encoder.copy()
        self.encoder = self.read_encoder()
        self.encoder_rate = (self.encoder - previous_encoder) / 0.02
        action_change = requested - self.previous_action
        self.previous_action = requested.copy()
        self.frame += 1
        if self.vary_height and self.frame % 150 == 0:
            self.set_height(self.np_random.uniform(0.15, 0.21))
        info = self.information()
        pitch_error = math.radians(info["pitch_deg_truth"]) - self.balance_trim()
        upright = math.exp(-(pitch_error / 0.12) ** 2)
        posture = math.exp(-float(np.sum((self.encoder - self.q_target) ** 2)) / 0.015)
        speed = math.exp(-((self.data.qvel[0] - self.command[0]) / 0.2) ** 2)
        reward = 0.6 * upright + 0.25 * posture + 0.15 * speed - 0.01 * float(np.mean(requested ** 2)) - 0.02 * float(np.mean(action_change ** 2))
        terminated = abs(info["pitch_deg_truth"]) > 35 or abs(info["roll_deg_truth"]) > 25 or info["base_height_m_truth"] < 0.07
        truncated = self.frame >= self.max_frames
        if terminated:
            reward -= 1
        if not np.all(np.isfinite(self.data.qpos)):
            raise FloatingPointError("Simulation became non-finite")
        return self.observation(), float(reward), bool(terminated), bool(truncated), info

    def set_height(self, left_hip_height_m):
        if not 0.145 <= left_hip_height_m <= 0.21:
            raise ValueError("Offline provisional height target outside 0.145-0.210m")
        heights = [left_hip_height_m, left_hip_height_m + self.results["cad"]["right"]["drive_world_m"][1] - self.results["cad"]["left"]["drive_world_m"][1]]
        self.q_goal = np.array([brentq(lambda angle: self.config["wheel_radius_m"] - solve_leg(self.results["cad"][side], angle)["wheel"][1] - heights[index],
                                      math.radians(25), math.radians(60)) for index, side in enumerate(["left", "right"])])

    def balance_trim(self):
        wheel_positions = []
        for index, side in enumerate(["left", "right"]):
            pose = solve_leg(self.results["cad"][side], float(self.q_target[index]))
            hip = self.model.body(f"{side}_upper").pos[[0, 2]]
            wheel_positions.append(hip + pose["wheel"])
        wheel = np.mean(wheel_positions, axis=0)
        return math.atan2(wheel[0], 0.03 - wheel[1])

    def baseline(self):
        mass = self.config["total_mass_kg_example_not_measured"]
        leg_commands = []
        for index, side in enumerate(["left", "right"]):
            loading = leg_load(self.results["cad"][side], float(self.encoder[index]), mass * GRAVITY / 2)
            feedforward = loading["drive_torque_nm"] / self.model.actuator_gainprm[index, 0]
            command = feedforward + 12 * (self.q_target[index] - self.encoder[index]) - 0.15 * self.encoder_rate[index]
            leg_commands.append(command / self.speed_limits[index])
        pitch = math.atan2(self.gravity_estimate[0], -self.gravity_estimate[2])
        pitch_rate = self.sensor("imu_gyro")[1]
        wheel_velocity_m_s = 2 * (pitch - self.balance_trim()) + 0.25 * pitch_rate
        wheel_command = wheel_velocity_m_s / (self.speed_limits[2] * self.config["wheel_radius_m"])
        return np.clip([*leg_commands, wheel_command, wheel_command], -1, 1).astype(np.float32)

    def render(self):
        if self.renderer is None:
            self.renderer = mujoco.Renderer(self.model, height=480, width=640)
        camera = mujoco.MjvCamera()
        camera.lookat[:] = self.data.xpos[self.torso_id] + [0, 0, -0.03]
        camera.distance = 0.75
        camera.azimuth = 135
        camera.elevation = -18
        self.renderer.update_scene(self.data, camera=camera)
        return self.renderer.render()

    def close(self):
        if self.renderer is not None:
            self.renderer.close()


def verify(output):
    from gymnasium.utils.env_checker import check_env
    environment = TalosEnv(output, seconds=5)
    check_env(environment, skip_render_check=True)
    episodes = []
    traces = []
    for seed in [0, 1, 2, 3, 4]:
        observation, info = environment.reset(seed=seed)
        largest_closure = info["closure_max_m"]
        for frame in range(250):
            action = environment.baseline()
            observation, reward, terminated, truncated, info = environment.step(action)
            largest_closure = max(largest_closure, info["closure_max_m"])
            assert observation.shape == (21,) and np.all(np.isfinite(observation))
            traces.append({"seed": seed, "time_s": info["time_s"], "pitch_deg": info["pitch_deg_truth"],
                           "roll_deg": info["roll_deg_truth"], "hip_left_deg": math.degrees(environment.encoder[0]),
                           "hip_right_deg": math.degrees(environment.encoder[1]), "closure_m": info["closure_max_m"],
                           "wheel_action": float(action[2]), "reward": reward})
            if terminated or truncated:
                break
        episodes.append({"seed": seed, "duration_s": info["time_s"], "fell": terminated,
                         "max_closure_m": largest_closure, "end_pitch_deg": info["pitch_deg_truth"]})
    for seed in [11, 22, 33]:
        randomized = TalosEnv(output, randomize=True, seconds=1)
        randomized.reset(seed=seed)
        for frame in range(50):
            observation, reward, terminated, truncated, info = randomized.step(randomized.baseline())
            assert np.all(np.isfinite(observation))
            if terminated or truncated:
                break
        randomized.close()
    curriculum = TalosEnv(output, randomize=True, vary_height=True, seconds=1)
    curriculum.reset(seed=123)
    for height in [0.15, 0.18, 0.21]:
        curriculum.set_height(height)
        goal = curriculum.q_goal.copy()
        target_before = curriculum.q_target.copy()
        observation, reward, terminated, truncated, info = curriculum.step(curriculum.baseline())
        assert np.all(np.isfinite(goal)) and np.all(np.isfinite(observation))
        assert np.max(np.abs(curriculum.q_target - target_before)) <= 0.004000001
    curriculum.close()
    environment.close()
    save_csv(output / "simulation" / "baseline_trace.csv", traces)
    validation = {"environment_checker": "passed", "observations": 21, "actions": 4,
                  "model_xml_sha256": hashlib.sha256((output / "simulation" / "talos.xml").read_bytes()).hexdigest(),
                  "model_assumptions_sha256": hashlib.sha256((output / "simulation" / "model_assumptions.json").read_bytes()).hexdigest(),
                  "physics_hz": 500, "servo_frames_hz": 50, "baseline_episodes": episodes,
                  "randomized_rollout_smoke_tests": 3, "trained_policy": False,
                  "height_command_and_target_slew_checks": "passed at 0.15, 0.18 and 0.21m",
                  "hardware_validated": False, "note": "Baseline is a diagnostic heuristic, not a tuned or trained balancing controller"}
    save_json(output / "simulation" / "validation.json", validation)
    print(validation)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args()
    verify(arguments.output)
