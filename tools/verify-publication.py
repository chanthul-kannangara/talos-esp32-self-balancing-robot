"""Read-only offline checks for the published TALOS model and saved policy.

Requires the scientific and optional training dependencies. No robot interface,
rendering, training, or writes to dated evidence are performed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import mujoco
import numpy as np
from gymnasium.utils.env_checker import check_env

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'cae'))
from talos_env import TalosEnv
from training_session import load_policy, rollout


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify():
    folders = [ROOT / 'outputs' / name for name in (
        'talos-cae-2026-10-11', 'talos-pivot-model-2026-10-12',
        'talos-training-2026-10-12')]
    evidence = {path: digest(path) for folder in folders for path in folder.rglob('*')
                if path.is_file() and '__pycache__' not in path.parts}
    checked = []
    for folder in folders:
        for path in sorted((folder / 'simulation').glob('talos*.xml')):
            model = mujoco.MjModel.from_xml_path(str(path))
            data = mujoco.MjData(model)
            assert model.nu == 4, path
            assert model.opt.timestep == 0.002, path
            for _ in range(100):
                mujoco.mj_step(model, data)
            assert np.isfinite(data.qpos).all() and np.isfinite(data.qvel).all(), path
            checked.append(str(path.relative_to(ROOT)).replace('\\', '/'))
    assert len(checked) == 12
    environment = TalosEnv(folders[1], seconds=1)
    check_env(environment, skip_render_check=True)
    environment.close()

    traces = []
    durations = []
    candidate = folders[2] / 'training' / 'ppo-posture-v2'
    for _ in range(2):
        environment = TalosEnv(folders[2], seconds=2)
        controller = load_policy(candidate, environment)
        summary, rows, _, _ = rollout(environment, controller,
            {'name': 'publication_reproducibility', 'seed': 9876, 'pitch_deg': 0.3}, seconds=2)
        assert rows and all(np.isfinite(value) for row in rows for value in row.values()
                            if isinstance(value, (int, float)))
        traces.append(rows)
        durations.append(summary['duration_s'])
        environment.close()
    assert traces[0] == traces[1], 'Loaded policy differs for identical seed and inputs'
    assert all(digest(path) == expected for path, expected in evidence.items()), 'Dated evidence changed'
    return {
        'mujoco_version': mujoco.__version__, 'compiled_model_count': len(checked),
        'models': checked, 'finite_100_step_smoke_tests': True,
        'gymnasium_checker': 'passed', 'observations': 21, 'actions': 4,
        'policy_same_seed_reproducibility': True, 'policy_rollout_duration_s': durations,
        'policy_sha256': digest(candidate / 'final.zip'),
        'dated_evidence_unchanged': True, 'checked_evidence_files': len(evidence),
        'physical_robot_tested': False, 'hardware_export_approved': False,
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, help='Optional new JSON report path')
    args = parser.parse_args()
    result = verify()
    output = json.dumps(result, indent=2, allow_nan=False)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(output + '\n', encoding='utf-8')
    print(output)
