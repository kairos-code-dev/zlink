import argparse
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
RUNNER_LIB = ROOT.parent.parent.parent.parent / "core" / "build" / "lib" / "libzlink.so"
DEFAULT_PATTERNS = (
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "PUBSUB",
    "SPOT",
    "STREAM",
)


def parse_args(argv):
    parser = argparse.ArgumentParser(prog="run_benchmarks.sh")
    parser.add_argument("--pythonpath", required=True)
    parser.add_argument("--pattern", default="PUBSUB")
    parser.add_argument("--duration", default="2")
    parser.add_argument("--warmup", default="1")
    parser.add_argument("--msg-size", default="256")
    parser.add_argument("--clients", default="4")
    parser.add_argument("--recv", default="recv")
    return parser.parse_args(argv)


def _parse_patterns(value):
    pattern_text = value.strip().upper()
    if pattern_text == "ALL":
        return list(DEFAULT_PATTERNS)
    patterns = [item.strip().upper() for item in pattern_text.split(",") if item.strip()]
    if not patterns:
        raise SystemExit("unsupported pattern: ")
    return patterns


def _run_pattern(args, env, pattern):
    server_path = ROOT / f"perf_multi_{pattern.lower()}_server.py"
    client_path = ROOT / f"perf_multi_{pattern.lower()}_client.py"
    if not server_path.exists() or not client_path.exists():
        raise SystemExit(f"unsupported pattern: {pattern}")

    server = subprocess.Popen(
        [
            sys.executable,
            str(server_path),
            "--clients",
            args.clients,
            "--recv",
            args.recv,
            "--msg-size",
            args.msg_size,
        ],
        cwd=str(ROOT.parent.parent),
        env=env,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        ready = server.stdout.readline().strip()
        if ready.startswith("UNSUPPORTED,") or ready.startswith("SKIP,"):
            print(ready)
            return
        if not ready.startswith("READY,"):
            raise SystemExit(f"server did not become ready: {ready}")
        endpoint = ready.split(",", 1)[1]

        client = subprocess.run(
            [
                sys.executable,
                str(client_path),
                "--endpoint",
                endpoint,
                "--duration",
                args.duration,
                "--warmup",
                args.warmup,
                "--msg-size",
                args.msg_size,
                "--clients",
                args.clients,
                "--recv",
                args.recv,
            ],
            cwd=str(ROOT.parent.parent),
            env=env,
            capture_output=True,
            text=True,
            timeout=30,
            check=True,
        )
        if client.stdout:
            print(client.stdout, end="")
        if client.stderr:
            print(client.stderr, end="", file=sys.stderr)
    finally:
        if server.stdin:
            try:
                server.stdin.write("STOP\n")
                server.stdin.flush()
                server.stdin.close()
            except Exception:
                pass
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=5)
        if server.stderr:
            err = server.stderr.read()
            if err:
                print(err, end="", file=sys.stderr)


def main(argv=None):
    args = parse_args(argv or sys.argv[1:])
    env = dict(os.environ)
    env["PYTHONPATH"] = args.pythonpath
    if RUNNER_LIB.exists():
        env["ZLINK_LIBRARY_PATH"] = str(RUNNER_LIB)
    for pattern in _parse_patterns(args.pattern):
        _run_pattern(args, env, pattern)


if __name__ == "__main__":
    main()
