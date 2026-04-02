import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main():
    env = dict(os.environ)
    env["PYTHONPATH"] = (
        str(ROOT / "src")
        + os.pathsep
        + str(ROOT / "samples")
        + os.pathsep
        + str(ROOT / "examples")
    )
    result = subprocess.run(
        [sys.executable, str(ROOT / "samples" / "run_samples.py")],
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
        raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
