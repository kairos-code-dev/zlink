#!/usr/bin/env python3
from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Tuple

ROOT = Path(__file__).resolve().parents[4]
CLIENT_BUILD_SCRIPT = ROOT / "bindings" / "bench" / "stream_client" / "build.sh"
CLIENT_BIN = ROOT / "bindings" / "bench" / "stream_client" / "build" / "bench_stream_client"


def ensure_client_bin() -> Path:
    if CLIENT_BIN.exists():
        return CLIENT_BIN
    subprocess.run([str(CLIENT_BUILD_SCRIPT)], check=True, cwd=str(ROOT))
    if not CLIENT_BIN.exists():
        raise RuntimeError("bench_stream_client build failed")
    return CLIENT_BIN


def run_client(args: list[str]) -> Tuple[int, str, str]:
    client = ensure_client_bin()
    cmd = [str(client)] + args
    proc = subprocess.run(
        cmd,
        cwd=str(ROOT),
        text=True,
        capture_output=True,
        check=False,
    )
    return proc.returncode, proc.stdout or "", proc.stderr or ""


def emit_perf_lines(stdout: str) -> None:
    for raw in stdout.splitlines():
        line = raw.strip()
        if line.startswith("RESULT,current,"):
            print(line, flush=True)
