import argparse
from pathlib import Path

from analyse import DEFAULT_OUTPUT
from talos_env import TalosEnv


def main():
    parser = argparse.ArgumentParser(description="OFFLINE ONLY: train a provisional TALOS policy. No hardware, serial or network interface.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--steps", type=int, default=500000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--height-curriculum", action="store_true")
    arguments = parser.parse_args()
    try:
        from stable_baselines3 import PPO
        from stable_baselines3.common.monitor import Monitor
    except ImportError as error:
        raise SystemExit("Optional training dependencies are not installed. In a dedicated venv: python -m pip install stable-baselines3 torch") from error
    destination = arguments.output / "simulation" / "training"
    destination.mkdir(exist_ok=True)
    environment = Monitor(TalosEnv(arguments.output, randomize=True, vary_height=arguments.height_curriculum), str(destination / "episodes"))
    policy = PPO("MlpPolicy", environment, policy_kwargs={"net_arch": [64, 64]}, n_steps=1024,
                 batch_size=128, learning_rate=3e-4, seed=arguments.seed, device="cpu", verbose=1)
    policy.learn(total_timesteps=arguments.steps)
    policy.save(destination / "TALOS_UNVALIDATED_OFFLINE_ONLY")
    environment.close()


if __name__ == "__main__":
    main()
