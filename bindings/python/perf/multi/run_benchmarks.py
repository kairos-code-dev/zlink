import argparse
import os
import signal
import subprocess
import sys
from pathlib import Path

from perf_multi_common import (
    build_report_path,
    parse_result_lines,
    render_effective_options,
    render_markdown_summary,
)


ROOT = Path(__file__).resolve().parent
RUNNER_LIB = ROOT.parent.parent.parent.parent / "core" / "build" / "lib" / "libzlink.so"
STREAM_CLIENT = ROOT.parent.parent.parent.parent / "core" / "build" / "bin" / "perf_stream_client"
DEFAULT_PATTERNS = (
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "PUBSUB",
    "SPOT",
    "STREAM",
)
DEFAULT_MSG_SIZES = ("64", "256", "1024", "65536", "131072", "262144")
DEFAULT_STREAM_MSG_SIZES = ("64", "256", "1024", "65536")


def _ensure_core_stream_client():
    repo_root = ROOT.parent.parent.parent.parent
    core_build = repo_root / "core" / "build"
    subprocess.run(
        [
            "cmake",
            "-S",
            str(repo_root),
            "-B",
            str(core_build),
            "-DCMAKE_BUILD_TYPE=Release",
            "-DENABLE_LTO=OFF",
            "-DBUILD_BENCHMARKS=ON",
            "-DZLINK_BUILD_TESTS=OFF",
            "-DZLINK_BUILD_BENCH_ZMQ=OFF",
            "-DZLINK_BUILD_BENCH_ZLINK=ON",
            "-DZLINK_BUILD_BENCH_BEAST=OFF",
            "-DZLINK_BUILD_BENCH_STREAMCOMPARE=OFF",
            "-DZLINK_BUILD_BENCH_ROUTER_COMPARE=OFF",
            "-DZLINK_CXX_STANDARD=17",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        ["cmake", "--build", str(core_build), "--target", "perf_stream_client"],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    if not STREAM_CLIENT.exists():
        raise SystemExit(f"shared stream client not found: {STREAM_CLIENT}")


def parse_args(argv):
    parser = argparse.ArgumentParser(prog="run_benchmarks.sh")
    parser.add_argument("--pythonpath", required=True)
    parser.add_argument("--pattern", default="ALL")
    parser.add_argument("--duration", default="5")
    parser.add_argument("--msg-sizes", default=",".join(DEFAULT_MSG_SIZES))
    parser.add_argument("--transports", default="tcp")
    parser.add_argument("--runs", default="1")
    parser.add_argument("--clients", default=None)
    parser.add_argument("--results-dir", default="")
    parser.add_argument("--results-tag", default="")
    parser.add_argument("--msg-size", dest="msg_size_compat", default=None, help=argparse.SUPPRESS)
    return parser.parse_args(argv)


def _parse_csv(value):
    return [item.strip() for item in value.split(",") if item.strip()]


def _normalize_pattern(value):
    pattern = value.strip().upper()
    if pattern.startswith("MULTI_"):
        pattern = pattern[6:]
    return pattern


def _parse_patterns(value):
    text = value.strip().upper()
    if text == "ALL":
        return list(DEFAULT_PATTERNS)
    patterns = [_normalize_pattern(item) for item in value.split(",") if item.strip()]
    if not patterns:
        raise SystemExit("unsupported pattern: ")
    unknown = [pattern for pattern in patterns if pattern not in DEFAULT_PATTERNS]
    if unknown:
        raise SystemExit(f"unsupported pattern: {unknown[0]}")
    return patterns


def _parse_msg_sizes(args, patterns):
    if args.msg_size_compat is not None:
        return [args.msg_size_compat]
    if args.msg_sizes:
        sizes = _parse_csv(args.msg_sizes)
        if not sizes:
            raise SystemExit("--msg-sizes must not be empty")
        return sizes
    if patterns == ["STREAM"]:
        return list(DEFAULT_STREAM_MSG_SIZES)
    return list(DEFAULT_MSG_SIZES)


def _parse_transports(value):
    transports = [item.lower() for item in _parse_csv(value)]
    if not transports:
        raise SystemExit("--transports must not be empty")
    if any(item != "tcp" for item in transports):
        raise SystemExit("python multi perf only supports tcp transport")
    return transports


def _default_clients(patterns):
    if patterns and all(pattern == "STREAM" for pattern in patterns):
        return "10000"
    return "100"


def _server_popen_kwargs():
    kwargs = {}
    if os.name == "posix":
        kwargs["start_new_session"] = True
    return kwargs


def _wait_for_tcp_endpoint(endpoint, timeout_s=10.0):
    import socket
    import time

    host_port = endpoint.split("://", 1)[1]
    host, port_text = host_port.rsplit(":", 1)
    deadline = time.perf_counter() + timeout_s
    while time.perf_counter() < deadline:
        with socket.socket() as sock:
            sock.settimeout(0.2)
            if sock.connect_ex((host, int(port_text))) == 0:
                return
        time.sleep(0.05)
    raise SystemExit(f"tcp endpoint did not become ready: {endpoint}")


def _normalize_stream_result_lines(output):
    normalized = []
    for line in output.splitlines():
        if line.startswith("RESULT,current,STREAM,"):
            normalized.append(line.replace(",STREAM,", ",MULTI_STREAM,", 1))
        else:
            normalized.append(line)
    return "\n".join(normalized)


def _terminate_process(proc, *, grace_seconds):
    if proc.poll() is not None:
        return
    try:
        if proc.stdin:
            proc.stdin.write("STOP\n")
            proc.stdin.flush()
            proc.stdin.close()
    except Exception:
        pass
    try:
        proc.wait(timeout=grace_seconds)
        return
    except subprocess.TimeoutExpired:
        pass

    if os.name == "posix":
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
    else:
        proc.terminate()
    try:
        proc.wait(timeout=grace_seconds)
        return
    except subprocess.TimeoutExpired:
        pass

    if os.name == "posix":
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            return
    else:
        proc.kill()
    proc.wait(timeout=grace_seconds)


def _run_pattern(args, env, pattern, msg_size, clients):
    server_path = ROOT / f"perf_multi_{pattern.lower()}_server.py"
    client_path = ROOT / f"perf_multi_{pattern.lower()}_client.py"
    if pattern == "STREAM":
        if not server_path.exists():
            raise SystemExit(f"unsupported pattern: {pattern}")
        _ensure_core_stream_client()
    elif not server_path.exists() or not client_path.exists():
        raise SystemExit(f"unsupported pattern: {pattern}")

    server = subprocess.Popen(
        [
            sys.executable,
            str(server_path),
            "--clients",
            clients,
            "--msg-size",
            msg_size,
        ],
        cwd=str(ROOT.parent.parent),
        env=env,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        **_server_popen_kwargs(),
    )
    stdout_chunks = []
    stderr_chunks = []
    try:
        ready = server.stdout.readline().strip()
        if ready:
            stdout_chunks.append(ready)
        if ready.startswith("UNSUPPORTED,") or ready.startswith("SKIP,"):
            return "\n".join(stdout_chunks)
        if not ready.startswith("READY,"):
            raise SystemExit(f"server did not become ready: {ready}")
        endpoint = ready.split(",", 1)[1]

        if pattern == "STREAM":
            _wait_for_tcp_endpoint(endpoint)
            client = subprocess.run(
                [
                    str(STREAM_CLIENT),
                    "--transport",
                    "tcp",
                    "--pattern",
                    "STREAM",
                "--sizes",
                msg_size,
                "--runs",
                "1",
                "--duration",
                str(args.duration),
                "--ccu",
                clients,
                "--io-threads",
                "4",
                "--print-perf-result",
                "1",
                "--send-stop-token",
                "0",
                "--endpoint",
                endpoint,
            ],
                cwd=str(ROOT.parent.parent.parent.parent),
                env=env,
                capture_output=True,
                text=True,
                timeout=90,
                check=True,
            )
            if client.stdout:
                stdout_chunks.append(_normalize_stream_result_lines(client.stdout.strip()))
            if client.stderr:
                stderr_chunks.append(client.stderr.strip())
            return "\n".join(chunk for chunk in stdout_chunks if chunk)

        client = subprocess.run(
            [
                sys.executable,
                str(client_path),
                "--endpoint",
                endpoint,
                "--duration",
                args.duration,
                "--msg-size",
                msg_size,
                "--clients",
                clients,
            ],
            cwd=str(ROOT.parent.parent),
            env=env,
            capture_output=True,
            text=True,
            timeout=90,
            check=True,
        )
        if client.stdout:
            stdout_chunks.append(client.stdout.strip())
        if client.stderr:
            stderr_chunks.append(client.stderr.strip())
    except subprocess.CalledProcessError as exc:
        if exc.stdout:
            stdout_chunks.append(exc.stdout.strip())
        if exc.stderr:
            stderr_chunks.append(exc.stderr.strip())
        raise SystemExit("\n".join(chunk for chunk in stderr_chunks + stdout_chunks if chunk))
    except subprocess.TimeoutExpired as exc:
        if exc.stdout:
            stdout_chunks.append(exc.stdout.strip())
        if exc.stderr:
            stderr_chunks.append(exc.stderr.strip())
        raise SystemExit(
            "\n".join(
                chunk
                for chunk in stderr_chunks + stdout_chunks + [f"client timed out for pattern {pattern}"]
                if chunk
            )
        )
    finally:
        _terminate_process(server, grace_seconds=3)
        if server.stderr:
            err = server.stderr.read().strip()
            if err:
                stderr_chunks.append(err)
    if stderr_chunks:
        raise SystemExit("\n".join(chunk for chunk in stderr_chunks if chunk))
    return "\n".join(chunk for chunk in stdout_chunks if chunk)


def _build_options(args, patterns, transports, msg_sizes, clients):
    return {
        "lang": "python",
        "suite": "multi",
        "runs": args.runs,
        "patterns": ",".join(f"MULTI_{pattern}" for pattern in patterns),
        "transports": ",".join(transports),
        "msg_sizes": ",".join(msg_sizes),
        "clients": clients,
        "duration_seconds": args.duration,
    }


def main(argv=None):
    args = parse_args(argv or sys.argv[1:])
    patterns = _parse_patterns(args.pattern)
    transports = _parse_transports(args.transports)
    msg_sizes = _parse_msg_sizes(args, patterns)
    clients = args.clients or _default_clients(patterns)
    runs = int(args.runs)
    if runs <= 0:
        raise SystemExit("--runs must be > 0")

    env = dict(os.environ)
    env["PYTHONPATH"] = args.pythonpath
    if RUNNER_LIB.exists():
        env["ZLINK_LIBRARY_PATH"] = str(RUNNER_LIB)

    options = _build_options(args, patterns, transports, msg_sizes, clients)
    sections = [render_effective_options(options), ""]
    emitted_chunks = []
    for _ in range(runs):
        for pattern in patterns:
            for _transport in transports:
                for msg_size in msg_sizes:
                    output = _run_pattern(args, env, pattern, msg_size, clients)
                    if output:
                        emitted_chunks.append(output)
                        sections.append(output)
    rows = parse_result_lines("\n".join(emitted_chunks))
    expected_result_lines = len(patterns) * len(transports) * len(msg_sizes) * runs * 5
    sections.extend(["", render_markdown_summary(rows), "", "## Effective Options (result)"])
    sections.append(f"- status: {'complete' if len(rows) == expected_result_lines else 'partial'}")
    sections.append(f"- expected_result_lines: {expected_result_lines}")
    sections.append(f"- actual_result_lines: {len(rows)}")
    final_output = "\n".join(section for section in sections if section is not None).rstrip() + "\n"
    print(final_output, end="")

    report_path = build_report_path(
        lang="python",
        suite="multi",
        results_dir=args.results_dir or None,
        tag=args.results_tag or None,
    )
    report_path.write_text(final_output, encoding="utf-8")


if __name__ == "__main__":
    main()
