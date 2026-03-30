import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = [
    "pair_recv.py",
    "pair_callback.py",
    "pubsub_recv.py",
    "pubsub_callback.py",
    "dealer_router_recv.py",
    "dealer_router_callback.py",
    "spot_recv.py",
    "spot_callback.py",
    "stream_recv.py",
    "stream_callback.py",
]


def main():
    env = dict(os.environ)
    env["PYTHONPATH"] = str(ROOT / "src")
    for name in EXAMPLES:
        result = subprocess.run(
            [sys.executable, str(ROOT / "examples" / name)],
            cwd=str(ROOT),
            env=env,
            capture_output=True,
            text=True,
            timeout=30,
            check=True,
        )
        if result.stdout:
            print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr)


if __name__ == "__main__":
    main()
