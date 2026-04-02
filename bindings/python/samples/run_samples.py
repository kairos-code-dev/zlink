import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SAMPLES_DIR = ROOT / "samples"
SAMPLES = [
    "spot_recv_sample.py",
    "spot_callback_sample.py",
    "pair_recv_sample.py",
    "pair_callback_sample.py",
    "dealer_router_recv_sample.py",
    "dealer_router_callback_sample.py",
    "pubsub_recv_sample.py",
    "pubsub_callback_sample.py",
    "stream_recv_sample.py",
    "stream_callback_sample.py",
    "monitor_recv_sample.py",
]


def main():
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(
        [
            str(ROOT / "src"),
            str(SAMPLES_DIR),
            env.get("PYTHONPATH", ""),
        ]
    ).rstrip(os.pathsep)

    passed = 0
    for name in SAMPLES:
        sample_path = SAMPLES_DIR / name
        result = subprocess.run(
            [sys.executable, str(sample_path)],
            cwd=str(ROOT),
            env=env,
            capture_output=True,
            text=True,
            timeout=120,
        )
        if result.stdout:
            print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr)
        if result.returncode != 0:
            raise SystemExit(f"sample failed: {name}")
        passed += 1

    print(f"Summary: {passed}/{len(SAMPLES)} samples passed")


if __name__ == "__main__":
    main()
