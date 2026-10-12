import argparse
import hashlib
import importlib.metadata
import math
from pathlib import Path
import shutil
import time

import numpy as np

from analyse import ROOT, finish_figure, read_json, save_csv, save_json
from talos_env import TalosEnv


DEFAULT_SOURCE = ROOT / "outputs" / "talos-pivot-model-2026-10-12"
DEFAULT_EXPERIMENT = ROOT / "outputs" / "talos-training-2026-10-12"


class LegacyPostureTrainingEnv(TalosEnv):
    def step(self, action):
        observation, original_reward, terminated, truncated, info = super().step(action)
        pitch_error = math.radians(info["pitch_deg_truth"]) - self.balance_trim()
        joint_error = np.abs(self.encoder - self.q_target)
        posture_dense = 1 - float(np.mean(np.minimum(joint_error / 0.5, 1)))
        posture_precise = math.exp(-float(np.mean((joint_error / 0.12) ** 2)) / 2)
        below = np.maximum(0, math.radians(25) - self.encoder)
        above = np.maximum(0, self.encoder - math.radians(60))
        limit_cost = float(np.sum(np.minimum((below + above) / 0.03, 1)))
        upright = math.exp(-(pitch_error / 0.15) ** 2)
        stationary = math.exp(-(self.data.qvel[0] / 0.2) ** 2)
        reward = (1.2 * upright + 0.8 * posture_dense + 0.8 * posture_precise +
                  0.2 * stationary - limit_cost - 0.02 * float(np.mean(np.asarray(action) ** 2)))
        if terminated:
            reward -= 10
        return observation, reward, terminated, truncated, info


class PostureTrainingEnv(LegacyPostureTrainingEnv):
    def step(self, action):
        observation, reward, terminated, truncated, info = super().step(action)
        if not terminated:
            reward += 2.1
        return observation, reward, terminated, truncated, info


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def prepare(source, destination):
    if destination.exists():
        raise FileExistsError(f"Refusing to overwrite experiment: {destination}")
    destination.mkdir(parents=True)
    shutil.copytree(source / "simulation", destination / "simulation")
    for filename in ["inputs.json", "results.json", "hardware_inputs.json", "native-cad.json"]:
        shutil.copy2(source / filename, destination / filename)
    (destination / "training").mkdir()
    (destination / "figures").mkdir()
    files = [path for path in source.rglob("*") if path.is_file()]
    save_json(destination / "source_manifest.json", {
        "source": str(source.resolve()),
        "files": {str(path.relative_to(source)): digest(path) for path in files},
        "hardware_access": False,
        "model_calibrated": False,
        "mass_kg_example_not_measured": 2.0,
    })
    print(f"Prepared separate offline experiment: {destination}", flush=True)


def train(destination, steps, seed, label, resume=None, vary_height=False, posture_objective=False):
    import torch
    from stable_baselines3 import PPO
    from stable_baselines3.common.callbacks import CheckpointCallback, EvalCallback
    from stable_baselines3.common.logger import configure
    from stable_baselines3.common.monitor import Monitor
    from stable_baselines3.common.vec_env import DummyVecEnv, VecNormalize

    torch.set_num_threads(1)
    torch.set_num_interop_threads(1)
    folder = destination / "training" / label
    folder.mkdir()
    snapshot = folder / "source_snapshot"
    snapshot.mkdir()
    source_hashes = {}
    for filename in ["training_session.py", "talos_env.py", "analyse.py", "simulation.py"]:
        path = Path(__file__).parent / filename
        shutil.copy2(path, snapshot / filename)
        source_hashes[filename] = digest(path)
    environment_class = PostureTrainingEnv if posture_objective else TalosEnv
    environment = DummyVecEnv([lambda: Monitor(environment_class(destination, randomize=True,
                                                       vary_height=vary_height))])
    evaluation = DummyVecEnv([lambda: Monitor(environment_class(destination, seconds=10,
                                                               vary_height=vary_height))])
    if resume:
        environment = VecNormalize.load(str(resume / "normalization.pkl"), environment)
        policy = PPO.load(str(resume / "final.zip"), env=environment, device="cpu")
    else:
        environment = VecNormalize(environment, norm_obs=True, norm_reward=False,
                                   clip_obs=10.0)
        policy = PPO("MlpPolicy", environment, policy_kwargs={"net_arch": [64, 64],
                     "log_std_init": -1.0}, n_steps=2048, batch_size=128,
                     learning_rate=3e-4, seed=seed, device="cpu", verbose=1)
    evaluation = VecNormalize(evaluation, norm_obs=True, norm_reward=False,
                              training=False)
    evaluation.seed(800)
    callbacks = [
        CheckpointCallback(save_freq=32768, save_path=str(folder / "checkpoints"),
                           name_prefix="offline", save_vecnormalize=True),
        EvalCallback(evaluation, best_model_save_path=str(folder / "best"),
                     log_path=str(folder / "evaluation"), eval_freq=16384,
                     n_eval_episodes=5, deterministic=True),
    ]
    policy.set_logger(configure(str(folder), ["stdout", "csv"]))
    started = time.perf_counter()
    policy.learn(total_timesteps=steps, callback=callbacks, log_interval=8,
                 reset_num_timesteps=resume is None)
    policy.save(folder / "final")
    environment.save(str(folder / "normalization.pkl"))
    save_json(folder / "run.json", {
        "algorithm": "PPO", "label": label, "requested_steps": steps,
        "total_steps_in_policy": policy.num_timesteps, "seed": seed,
        "wall_seconds": time.perf_counter() - started,
        "height_curriculum": vary_height, "resume": str(resume) if resume else None,
        "reward_variant": "dense_posture_v2_nonnegative_alive_reward" if posture_objective else "original",
        "source_snapshot_sha256": source_hashes,
        "observations": 21, "actions": 4, "physics_hz": 500, "commands_hz": 50,
        "model_sha256": digest(destination / "simulation" / "talos.xml"),
        "versions": {name: importlib.metadata.version(name) for name in
                     ["torch", "stable-baselines3", "mujoco", "gymnasium", "numpy"]},
        "actor_has_wheel_encoder_or_base_velocity": False,
        "privileged_truth_used_for_reward_and_evaluation_only": True,
        "hardware_validated": False,
    })
    environment.close()
    evaluation.close()
    print(f"Saved offline policy and normalization in {folder}", flush=True)


def load_policy(folder, environment):
    import torch
    from stable_baselines3 import PPO
    from stable_baselines3.common.vec_env import DummyVecEnv, VecNormalize

    torch.set_num_threads(1)
    normalization = VecNormalize.load(str(folder / "normalization.pkl"),
                                       DummyVecEnv([lambda: environment]))
    normalization.training = False
    normalization.norm_reward = False
    policy = PPO.load(str(folder / "final.zip"), device="cpu")

    def predict(observation):
        normalized = normalization.normalize_obs(observation.copy())
        action, state = policy.predict(normalized, deterministic=True)
        return action

    return predict


def scenarios():
    cases = []
    for index, pitch in enumerate([-5, -3, -1, 0, 1, 3, 5]):
        cases.append({"name": f"nominal_pitch_{pitch:+d}", "group": "nominal",
                      "seed": 1000 + index, "pitch_deg": pitch})
    for index in range(10):
        cases.append({"name": f"randomized_{index}", "group": "randomized",
                      "seed": 2000 + index, "randomize": True})
    for index, height in enumerate([0.15, 0.18, 0.21]):
        cases.append({"name": f"height_{height:.2f}", "group": "height",
                      "seed": 3000 + index, "height_m": height})
    for index, force in enumerate([-1.0, 1.0]):
        cases.append({"name": f"push_{force:+.0f}N", "group": "push",
                      "seed": 4000 + index, "push_n": force})
    for index, factor in enumerate([1.5, 2.0]):
        cases.append({"name": f"lag_{factor:.1f}x", "group": "lag",
                      "seed": 5000 + index, "lag_factor": factor})
    return cases


def rollout(environment, controller, case, seconds=10, collect_frames=False):
    observation, info = environment.reset(seed=case["seed"],
        options={"pitch_deg": case["pitch_deg"]} if "pitch_deg" in case else {})
    if "height_m" in case:
        environment.set_height(case["height_m"])
    environment.lag *= case.get("lag_factor", 1.0)
    rows, frames, states = [], [], []
    total_reward = 0.0
    terminated = False
    push_frames = 0
    for frame in range(round(seconds / 0.02)):
        environment.data.xfrc_applied[:] = 0
        if 2.0 <= environment.data.time < 2.1:
            environment.data.xfrc_applied[environment.torso_id, 0] = case.get("push_n", 0)
            if case.get("push_n", 0):
                push_frames += 1
        action = controller(observation)
        observation, reward, terminated, truncated, info = environment.step(action)
        total_reward += reward
        pitch_estimate = math.degrees(math.atan2(environment.gravity_estimate[0],
                                                -environment.gravity_estimate[2]))
        rows.append({"case": case["name"], "time_s": info["time_s"],
            "pitch_deg": info["pitch_deg_truth"], "pitch_estimate_deg": pitch_estimate,
            "trim_deg": math.degrees(environment.balance_trim()),
            "roll_deg": info["roll_deg_truth"],
            "left_error_deg": info["joint_error_deg"][0],
            "right_error_deg": info["joint_error_deg"][1],
            "left_hip_deg": math.degrees(environment.encoder[0]),
            "right_hip_deg": math.degrees(environment.encoder[1]),
            "base_x_m": float(environment.data.qpos[0]),
            "base_height_m": info["base_height_m_truth"],
            "base_speed_m_s_truth": float(environment.data.qvel[0]),
            "closure_m": info["closure_max_m"],
            "left_leg_action": float(action[0]), "right_leg_action": float(action[1]),
            "left_wheel_action": float(action[2]), "right_wheel_action": float(action[3]),
            "reward": reward})
        if collect_frames:
            states.append(environment.data.qpos.copy())
            if frame % 2 == 0:
                frames.append(environment.render().copy())
        if terminated or truncated:
            break
    tail = rows[len(rows) // 2:]
    pitch_rms = float(np.sqrt(np.mean([(row["pitch_deg"] - row["trim_deg"]) ** 2 for row in tail])))
    joint_rms = float(np.sqrt(np.mean([row[key] ** 2 for row in tail
                                     for key in ["left_error_deg", "right_error_deg"]])))
    speed_rms = float(np.sqrt(np.mean([row["base_speed_m_s_truth"] ** 2 for row in tail])))
    survived = not terminated and info["time_s"] >= seconds - 0.021
    summary = {**case, "duration_s": info["time_s"], "fell": terminated,
        "survived": survived, "return": total_reward, "pitch_rms_deg": pitch_rms,
        "joint_rms_deg": joint_rms, "speed_rms_m_s_truth": speed_rms,
        "max_closure_m": max(row["closure_m"] for row in rows),
        "max_abs_pitch_deg": max(abs(row["pitch_deg"]) for row in rows),
        "max_abs_roll_deg": max(abs(row["roll_deg"]) for row in rows),
        "max_hip_limit_violation_deg": float(max(max(0, row[key] - 60, 25 - row[key])
            for row in rows for key in ["left_hip_deg", "right_hip_deg"])),
        "drift_m": rows[-1]["base_x_m"] - rows[0]["base_x_m"],
        "push_applied_seconds": push_frames * 0.02,
        "final_target_to_goal_error_deg": np.degrees(environment.q_target - environment.q_goal).tolist(),
        "wheel_saturation_fraction": float(np.mean([max(abs(row["left_wheel_action"]),
            abs(row["right_wheel_action"])) >= 0.98 for row in rows])),
        "pass": survived and pitch_rms <= 5 and joint_rms <= 3 and speed_rms <= 0.1}
    return summary, rows, frames, states


def evaluate(destination, label, seconds, seed_offset=0):
    folder = destination / "training" / label
    all_results, traces = [], []
    for mode in ["baseline", "ppo"]:
        for case in scenarios():
            case = {**case, "seed": case["seed"] + seed_offset}
            environment = TalosEnv(destination, randomize=case.get("randomize", False),
                                   seconds=seconds)
            controller = load_policy(folder, environment) if mode == "ppo" else lambda observation: environment.baseline()
            summary, rows, frames, states = rollout(environment, controller, case, seconds)
            summary["controller"] = mode
            all_results.append(summary)
            for row in rows:
                traces.append({"controller": mode, **row})
            print(f"{mode} {case['name']}: {summary['duration_s']:.2f}s pass={summary['pass']} pitchRMS={summary['pitch_rms_deg']:.2f} jointRMS={summary['joint_rms_deg']:.2f}", flush=True)
            environment.close()
    save_csv(folder / "heldout_traces.csv", traces)
    save_json(folder / "heldout.json", {
        "seconds_per_case": seconds,
        "criteria": {"survive_without_fall": True, "tail_pitch_rms_deg_max": 5,
                     "tail_joint_rms_deg_max": 3, "tail_speed_rms_m_s_max": 0.1},
        "tail_definition": "last half of each actual rollout, not after settling if it falls",
        "evaluation_seeds_used_for_training": False,
        "seed_offset": seed_offset,
        "diagnostic_objective_design_used_earlier_zero_offset_evaluations": seed_offset != 0,
        "policy_sha256": digest(folder / "final.zip"),
        "normalization_sha256": digest(folder / "normalization.pkl"),
        "episodes": all_results, "hardware_validated": False,
    })
    plot_evaluation(folder, traces, all_results)
    return all_results


def plot_evaluation(folder, traces, results):
    import matplotlib.pyplot as plt

    figure, axes = plt.subplots(2, 2, figsize=(12, 7))
    for mode, color in [("baseline", "#ad4656"), ("ppo", "#12665b")]:
        rows = [row for row in traces if row["controller"] == mode and row["case"] == "nominal_pitch_+0"]
        axes[0, 0].plot([row["time_s"] for row in rows], [row["pitch_deg"] for row in rows], label=mode, color=color)
        axes[0, 1].plot([row["time_s"] for row in rows], [row["left_error_deg"] for row in rows], label=mode, color=color)
        episodes = [episode for episode in results if episode["controller"] == mode]
        axes[1, 0].plot(range(len(episodes)), [episode["duration_s"] for episode in episodes], 'o-', label=mode, color=color)
        axes[1, 1].plot(range(len(episodes)), [episode["joint_rms_deg"] for episode in episodes], 'o-', label=mode, color=color)
    for axis, title, xlabel, ylabel in zip(axes.flat,
        ["Nominal initial pitch 0 degrees", "Nominal left encoder error", "Held-out survival", "Held-out joint accuracy"],
        ["Time (s)", "Time (s)", "Case index", "Case index"],
        ["Pitch (deg)", "Target error (deg)", "Duration (s)", "Tail RMS error (deg)"]):
        axis.set(title=title, xlabel=xlabel, ylabel=ylabel)
        axis.grid(alpha=0.25)
        axis.legend()
    figure.suptitle("TALOS provisional CAD model - offline only, not hardware validation")
    figure.tight_layout()
    finish_figure(figure, folder / "heldout.png")


def replay(destination, label, case_name, seconds, live=False):
    folder = destination / "training" / label
    case = next(case for case in scenarios() if case["name"] == case_name)
    environment = TalosEnv(destination, randomize=case.get("randomize", False), seconds=seconds)
    controller = load_policy(folder, environment)
    summary, rows, frames, states = rollout(environment, controller, case, seconds, collect_frames=True)
    np.savez_compressed(folder / "replay.npz", qpos=states, dt=0.02)
    save_json(folder / "replay.json", summary)
    import imageio.v2 as imageio
    from PIL import Image, ImageDraw

    annotated = []
    for index, frame in enumerate(frames):
        canvas = Image.fromarray(frame)
        draw = ImageDraw.Draw(canvas)
        draw.rectangle((0, 0, 640, 48), fill="#152b2b")
        draw.text((12, 8), f"TALOS OFFLINE PPO | {case_name} | t={index * 0.04:.2f}s", fill="white")
        draw.text((12, 26), "Provisional masses and servo dynamics - NOT hardware validated", fill="white")
        annotated.append(np.asarray(canvas))
    imageio.mimsave(str(folder / "replay.mp4"), annotated, fps=25)
    Image.fromarray(annotated[len(annotated) // 2]).save(folder / "replay.png")
    environment.close()
    if live:
        import mujoco
        import mujoco.viewer

        model = mujoco.MjModel.from_xml_path(str(destination / "simulation" / "talos.xml"))
        data = mujoco.MjData(model)
        with mujoco.viewer.launch_passive(model, data) as viewer:
            viewer.cam.distance = 0.8
            viewer.cam.azimuth = 135
            viewer.cam.elevation = -18
            viewer.opt.sitegroup[:] = 0
            while viewer.is_running():
                for frame_index, position in enumerate(states):
                    if not viewer.is_running():
                        break
                    started = time.perf_counter()
                    data.qpos[:] = position
                    data.time = frame_index * 0.02
                    mujoco.mj_forward(model, data)
                    viewer.cam.lookat[:] = data.xpos[model.body("torso").id] + [0, 0, -0.03]
                    viewer.set_texts((None, mujoco.mjtGridPos.mjGRID_TOPRIGHT,
                        "TALOS OFFLINE RECORDED REPLAY\n" + label + "\n" +
                        f"t={data.time:.2f}s | Full accuracy test: {'PASS' if summary['pass'] else 'FAIL'}\n" +
                        "Uncalibrated model - not approved for ESP32", ""))
                    viewer.sync()
                    time.sleep(max(0, 0.02 - (time.perf_counter() - started)))
                time.sleep(1.0)


def main():
    parser = argparse.ArgumentParser(description="Offline-only TALOS experiments. No serial or robot interface.")
    parser.add_argument("mode", choices=["prepare", "train", "evaluate", "replay"])
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--destination", type=Path, default=DEFAULT_EXPERIMENT)
    parser.add_argument("--steps", type=int, default=65536)
    parser.add_argument("--seed", type=int, default=41)
    parser.add_argument("--label", default="ppo-fixed")
    parser.add_argument("--resume", type=Path)
    parser.add_argument("--height-curriculum", action="store_true")
    parser.add_argument("--posture-objective", action="store_true")
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--seed-offset", type=int, default=0)
    parser.add_argument("--case", default="nominal_pitch_+0")
    parser.add_argument("--live", action="store_true")
    arguments = parser.parse_args()
    if arguments.mode == "prepare":
        prepare(arguments.source, arguments.destination)
    elif arguments.mode == "train":
        train(arguments.destination, arguments.steps, arguments.seed, arguments.label,
              arguments.resume, arguments.height_curriculum, arguments.posture_objective)
    elif arguments.mode == "evaluate":
        evaluate(arguments.destination, arguments.label, arguments.seconds, arguments.seed_offset)
    else:
        replay(arguments.destination, arguments.label, arguments.case, arguments.seconds,
               arguments.live)


if __name__ == "__main__":
    main()
