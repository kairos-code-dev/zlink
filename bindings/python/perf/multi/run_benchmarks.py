import argparse
import os
import queue
import signal
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

from perf_multi_common import (
    build_report_path,
    parse_result_lines,
    pin_current_process_cpu0,
    render_effective_options,
    resolve_multi_timeout_seconds,
    rows_by_case,
    status_row_text,
    table_header_lines,
    throughput_unit,
)


ROOT = Path(__file__).resolve().parent
RUNNER_LIB = ROOT.parent.parent.parent.parent / "core" / "build" / "lib" / "libzlink.so"
DEFAULT_PATTERNS = (
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "PUBSUB",
    "SPOT",
    "SPOT_REQREP",
    "STREAM",
)
DEFAULT_MSG_SIZES = ("64", "256", "1024", "65536", "131072", "262144")
DEFAULT_STREAM_MSG_SIZES = ("64", "256", "1024", "65536")
RAW_TRANSPORTS = (
    ("tcp", "tls", "ws", "wss")
    if sys.platform.startswith("win")
    else ("tcp", "tls", "ws", "wss", "ipc")
)
POLICY_TRANSPORTS = {
    "DEALER_DEALER": RAW_TRANSPORTS,
    "DEALER_ROUTER": RAW_TRANSPORTS,
    "ROUTER_ROUTER": RAW_TRANSPORTS,
    "PUBSUB": RAW_TRANSPORTS,
    "SPOT": ("tcp", "tls", "ws", "wss"),
    "SPOT_REQREP": ("tcp", "tls", "ws", "wss"),
    "STREAM": ("tcp", "tls", "ws", "wss"),
}
RUNNABLE_TRANSPORTS = POLICY_TRANSPORTS


def parse_args(argv):
    parser = argparse.ArgumentParser(prog="run_benchmarks_multi.sh")
    parser.add_argument("--pythonpath", required=True)
    parser.add_argument("--pattern", default="ALL")
    parser.add_argument("--build-dir", default="")
    parser.add_argument("--reuse-build", action="store_true")
    parser.add_argument("--clean-build", action="store_true")
    parser.add_argument(
        "--duration",
        default=os.environ.get("PERF_MULTI_DURATION_SECONDS", "5"),
    )
    parser.add_argument("--msg-sizes", default="")
    parser.add_argument("--transports", default="")
    parser.add_argument("--runs", default="1")
    parser.add_argument("--clients", default=None)
    parser.add_argument("--results-dir", default="")
    parser.add_argument("--results-tag", default="")
    parser.add_argument("--output", default="")
    parser.add_argument("--pin-cpu", action="store_true")
    parser.add_argument("--io-threads", default="")
    parser.add_argument("--server-io-threads", default="")
    parser.add_argument("--client-io-threads", default="")
    parser.add_argument("--hwm", default="")
    parser.add_argument("--send-hwm", default="")
    parser.add_argument("--recv-hwm", default="")
    parser.add_argument("--sndtimeo", "--send-timeout-ms", dest="send_timeout_ms", default="")
    parser.add_argument("--rcvtimeo", "--recv-timeout-ms", dest="recv_timeout_ms", default="")
    parser.add_argument("--connect-concurrency", default="")
    parser.add_argument("--transport-transition-ms", default="")
    parser.add_argument("--pattern-transition-ms", default="")
    parser.add_argument("--server-ready-timeout-ms", default="")
    parser.add_argument("--connect-ready-timeout-ms", default="")
    parser.add_argument("--monitor-hwm", default="")
    parser.add_argument("--server-shutdown-timeout-ms", default="")
    parser.add_argument("--server-bind-port", default="")
    parser.add_argument(
        "--msg-size", dest="msg_size_compat", default=None, help=argparse.SUPPRESS
    )
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


def _requested_msg_sizes(args):
    if args.msg_size_compat is not None:
        return [args.msg_size_compat]
    if args.msg_sizes:
        sizes = _parse_csv(args.msg_sizes)
        if not sizes:
            raise SystemExit("--msg-sizes must not be empty")
        return sizes
    return None


def _msg_sizes_for_pattern(pattern, requested_msg_sizes):
    if requested_msg_sizes is not None:
        return requested_msg_sizes
    if pattern == "STREAM":
        stream_env = os.environ.get("PERF_MULTI_STREAM_MSG_SIZES", "")
        if stream_env:
            sizes = _parse_csv(stream_env)
            if not sizes:
                raise SystemExit("PERF_MULTI_STREAM_MSG_SIZES must not be empty")
            return sizes
    common_env = os.environ.get("PERF_MSG_SIZES", "")
    if common_env:
        sizes = _parse_csv(common_env)
        if not sizes:
            raise SystemExit("PERF_MSG_SIZES must not be empty")
        return sizes
    if pattern == "STREAM":
        return list(DEFAULT_STREAM_MSG_SIZES)
    return list(DEFAULT_MSG_SIZES)


def _parse_transports(value):
    source = value or os.environ.get("PERF_TRANSPORTS", "")
    if not source:
        return None
    transports = [item.lower() for item in _parse_csv(source)]
    if not transports:
        raise SystemExit("--transports must not be empty")
    return transports


def _transports_for_pattern(pattern, transports):
    if transports is None:
        return list(POLICY_TRANSPORTS[pattern])
    return list(transports)


def _grouped_option_text(patterns, value_for_pattern, *, prefix="MULTI_"):
    groups = []
    for pattern in patterns:
        values = tuple(value_for_pattern(pattern))
        if groups and groups[-1][1] == values:
            groups[-1][0].append(pattern)
            continue
        groups.append(([pattern], values))
    if not groups:
        return ""
    if len(groups) == 1:
        return ",".join(groups[0][1])
    rendered = []
    for grouped_patterns, values in groups:
        rendered.append(
            f"{','.join(f'{prefix}{pattern}' for pattern in grouped_patterns)}={','.join(values)}"
        )
    return "; ".join(rendered)


def _selected_configs(patterns, transports, requested_msg_sizes):
    configs = []
    for pattern in patterns:
        for transport in _transports_for_pattern(pattern, transports):
            for msg_size in _msg_sizes_for_pattern(pattern, requested_msg_sizes):
                configs.append((pattern, transport, msg_size))
    if not configs:
        raise SystemExit("no runnable pattern/transport combinations selected")
    return configs


def _default_clients(patterns):
    if patterns and all(pattern == "STREAM" for pattern in patterns):
        return "10000"
    return "policy-default"


def _options_clients_display(patterns, cli_value):
    if cli_value is not None:
        return cli_value
    env_value = os.environ.get("PERF_MULTI_CLIENTS")
    if env_value:
        return env_value
    if patterns and all(pattern == "STREAM" for pattern in patterns):
        return "10000"
    if any(pattern == "STREAM" for pattern in patterns):
        return "100 (stream=10000)"
    return "100"


def _clients_for_pattern(pattern, cli_value):
    if cli_value is not None:
        return cli_value
    env_value = os.environ.get("PERF_MULTI_CLIENTS")
    if env_value:
        return env_value
    return "10000" if pattern == "STREAM" else "100"


def _server_popen_kwargs():
    kwargs = {}
    if os.name == "posix":
        kwargs["start_new_session"] = True
    return kwargs


def _coerce_text(chunk):
    if chunk is None:
        return ""
    if isinstance(chunk, bytes):
        return chunk.decode("utf-8", errors="replace")
    return str(chunk)


def _parse_status_lines(output):
    rows = []
    for line in output.splitlines():
        if line.startswith("SKIP,") or line.startswith("UNSUPPORTED,"):
            rows.append(line.strip())
    return rows


def _env_int(name, default):
    value = os.environ.get(name)
    if value in (None, ""):
        return default
    try:
        return int(value)
    except ValueError:
        return default


def _arg_or_env_int(cli_value, env_name, default):
    if cli_value not in (None, ""):
        try:
            return int(cli_value)
        except ValueError:
            raise SystemExit(f"{env_name} must be an integer")
    return _env_int(env_name, default)


def _append_line(lines, line=""):
    print(line, flush=True)
    lines.append(line)


def _failure_reason(output):
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    if not lines:
        return "no_data"
    return lines[-1]


def _result_metrics_for_case(output, pattern, transport, msg_size):
    grouped = rows_by_case(parse_result_lines(output, warn=_warn_runner), warn=_warn_runner)
    return grouped.get((f"MULTI_{pattern}", transport, str(msg_size)), {})


def _metric_row(pattern, msg_size, metrics, *, indent="      "):
    return (
        f"{indent}| {int(msg_size):>7}B | "
        f"{float(metrics.get('throughput', 0.0)) / 1000.0:>16.2f} {throughput_unit(f'MULTI_{pattern}')} | "
        f"{float(metrics.get('bandwidth', 0.0)):>10.2f} MB/s | "
        f"{float(metrics.get('latency', 0.0)):>12.6f} ms | "
        f"{float(metrics.get('latency_p95', 0.0)):>12.6f} ms | "
        f"{float(metrics.get('latency_p99', 0.0)):>12.6f} ms |"
    )


def _status_row(msg_size, status, *, indent="      "):
    return indent + status_row_text(int(msg_size), status)


def _median_metrics(outputs, pattern, transport, msg_size):
    metrics_list = [
        _result_metrics_for_case(output, pattern, transport, msg_size)
        for output in outputs
    ]
    metrics_list = [metrics for metrics in metrics_list if metrics]
    if not metrics_list:
        return {}
    medians = {}
    for metric in ("throughput", "bandwidth", "latency", "latency_p95", "latency_p99"):
        values = [float(metrics[metric]) for metrics in metrics_list if metric in metrics]
        if values:
            medians[metric] = statistics.median(values)
    return medians


def _is_unsupported_output(text):
    lowered = text.lower()
    markers = (
        "protocol not supported",
        "unsupported,current,",
    )
    return any(marker in lowered for marker in markers)


def _warn_runner(message):
    print(f"warning: {message}", file=sys.stderr, flush=True)


def _status_kind(output):
    if parse_result_lines(output):
        return "ok"
    for line in _parse_status_lines(output):
        if line.startswith("UNSUPPORTED,"):
            return "unsupported"
        if line.startswith("SKIP,"):
            return "skip"
    return "fail"


def _stdout_queue(proc):
    existing = getattr(proc, "_zlink_stdout_queue", None)
    if existing is not None:
        return existing
    lines = queue.Queue()

    def read_stdout():
        try:
            for line in proc.stdout:
                lines.put(line)
        finally:
            lines.put(None)

    threading.Thread(target=read_stdout, daemon=True).start()
    proc._zlink_stdout_queue = lines
    return lines


def _wait_for_control_line(proc, prefixes, *, timeout_s, stdout_chunks):
    deadline = time.perf_counter() + timeout_s
    prefix_tuple = tuple(prefixes)
    lines = _stdout_queue(proc)
    while time.perf_counter() < deadline:
        remaining = max(0.0, deadline - time.perf_counter())
        try:
            line = lines.get(timeout=remaining)
        except queue.Empty:
            break
        if not line:
            break
        text = line.strip()
        if not text:
            continue
        stdout_chunks.append(text)
        if text.startswith(prefix_tuple):
            return text
    wanted = ", ".join(prefixes)
    raise SystemExit(f"missing control line: {wanted}")


def _drain_stdout_queue(proc, stdout_chunks):
    lines = getattr(proc, "_zlink_stdout_queue", None)
    if lines is None:
        return
    while True:
        try:
            line = lines.get_nowait()
        except queue.Empty:
            return
        if not line:
            return
        text = line.strip()
        if text:
            stdout_chunks.append(text)


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


def _run_pattern(args, env, pattern, transport, msg_size, clients):
    if transport == "ipc" and sys.platform.startswith("win"):
        return f"SKIP,current,MULTI_{pattern},{transport},windows_ipc_unsupported"
    if transport not in RUNNABLE_TRANSPORTS.get(pattern, ()):
        return f"UNSUPPORTED,current,MULTI_{pattern},{transport}"
    server_path = ROOT / f"perf_multi_{pattern.lower()}_server.py"
    client_path = ROOT / f"perf_multi_{pattern.lower()}_client.py"
    if not server_path.exists() or not client_path.exists():
        raise SystemExit(f"unsupported pattern: {pattern}")

    server = subprocess.Popen(
        [
            sys.executable,
            str(server_path),
            "--transport",
            transport,
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
    client = None
    ready_timeout_s = _arg_or_env_int(
        args.server_ready_timeout_ms,
        "PERF_MULTI_SERVER_READY_TIMEOUT_MS",
        10000,
    ) / 1000.0
    shutdown_grace_s = _arg_or_env_int(
        args.server_shutdown_timeout_ms,
        "PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS",
        5000,
    ) / 1000.0
    timeout_override = _arg_or_env_int(
        "",
        "PERF_MULTI_TIMEOUT_SECONDS",
        0,
    )
    client_timeout_s = (
        timeout_override
        if timeout_override > 0
        else resolve_multi_timeout_seconds(
            args.duration,
            pattern,
            transport,
            msg_size,
        )
    )
    try:
        ready = _wait_for_control_line(
            server,
            ("READY,", "UNSUPPORTED,", "SKIP,"),
            timeout_s=ready_timeout_s,
            stdout_chunks=stdout_chunks,
        )
        if ready.startswith("UNSUPPORTED,") or ready.startswith("SKIP,"):
            return "\n".join(chunk for chunk in stdout_chunks if chunk)
        if not ready.startswith("READY,"):
            server_stderr = server.stderr.read().strip() if server.stderr else ""
            combined = "\n".join(chunk for chunk in [server_stderr, ready] if chunk)
            if _is_unsupported_output(combined):
                return f"UNSUPPORTED,current,MULTI_{pattern},{transport}"
            raise SystemExit(f"server did not become ready: {combined}")
        endpoint = ready.split(",", 1)[1]
        control_endpoint = ""

        if pattern in {"SPOT", "SPOT_REQREP"}:
            control_ready = _wait_for_control_line(
                server,
                ("CONTROL_READY,",),
                timeout_s=ready_timeout_s,
                stdout_chunks=stdout_chunks,
            )
            control_endpoint = control_ready.split(",", 1)[1]
            client_cmd = [
                sys.executable,
                str(client_path),
                "--endpoint",
                f"{endpoint},{control_endpoint}",
                "--duration",
                args.duration,
                "--msg-size",
                msg_size,
                "--clients",
                clients,
                "--transport",
                transport,
            ]
            client = subprocess.Popen(
                client_cmd,
                cwd=str(ROOT.parent.parent),
                env=env,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                **_server_popen_kwargs(),
            )
            client_control = _wait_for_control_line(
                client,
                ("CLIENT_CONTROL_ENDPOINT,",),
                timeout_s=ready_timeout_s,
                stdout_chunks=stdout_chunks,
            )
            client_control_endpoint = client_control.split(",", 1)[1]
            _wait_for_control_line(
                client,
                ("CONTROL_CONNECTED,",),
                timeout_s=ready_timeout_s,
                stdout_chunks=stdout_chunks,
            )
            server.stdin.write(f"CONNECT_CONTROL,{client_control_endpoint}\n")
            server.stdin.flush()
            client_ready = _wait_for_control_line(
                client,
                ("CLIENT_READY,", "UNSUPPORTED,", "SKIP,"),
                timeout_s=client_timeout_s,
                stdout_chunks=stdout_chunks,
            )
            if client_ready.startswith(("UNSUPPORTED,", "SKIP,")):
                return "\n".join(chunk for chunk in stdout_chunks if chunk)
            if client_ready != f"CLIENT_READY,{msg_size}":
                raise SystemExit(f"client did not become ready: {client_ready}")
            server.stdin.write(f"START,{msg_size}\n")
            server.stdin.flush()
            client.stdin.write(f"START,{msg_size}\n")
            client.stdin.flush()
            try:
                client.wait(timeout=client_timeout_s)
            except subprocess.TimeoutExpired as exc:
                _drain_stdout_queue(client, stdout_chunks)
                raise exc
            _drain_stdout_queue(client, stdout_chunks)
            client_stderr = client.stderr.read() if client.stderr else ""
            if client_stderr:
                stderr_chunks.append(client_stderr.strip())
            if client.returncode != 0:
                raise subprocess.CalledProcessError(
                    client.returncode,
                    client.args,
                    output="\n".join(stdout_chunks),
                    stderr=client_stderr,
                )
            return "\n".join(chunk for chunk in stdout_chunks if chunk)
        one_way_pattern = pattern in {"DEALER_DEALER", "PUBSUB"}
        if one_way_pattern:
            client = subprocess.Popen(
                [
                    sys.executable,
                    str(client_path),
                    "--endpoint",
                    endpoint,
                    "--transport",
                    transport,
                    "--duration",
                    args.duration,
                    "--msg-size",
                    msg_size,
                    "--clients",
                    clients,
                ],
                cwd=str(ROOT.parent.parent),
                env=env,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                    **_server_popen_kwargs(),
                )
            client_ready = _wait_for_control_line(
                client,
                ("CLIENT_READY,", "UNSUPPORTED,", "SKIP,"),
                timeout_s=client_timeout_s,
                stdout_chunks=stdout_chunks,
            )
            if client_ready.startswith(("UNSUPPORTED,", "SKIP,")):
                return "\n".join(chunk for chunk in stdout_chunks if chunk)
            if client_ready != f"CLIENT_READY,{msg_size}":
                raise SystemExit(f"client did not become ready: {client_ready}")
            server.stdin.write(f"START,{msg_size}\n")
            server.stdin.flush()
            client.stdin.write(f"START,{msg_size}\n")
            if pattern == "PUBSUB":
                client.stdin.write(f"PHASE_ACTIVE,{msg_size}\n")
            client.stdin.flush()
            try:
                client.wait(timeout=client_timeout_s)
            except subprocess.TimeoutExpired as exc:
                _drain_stdout_queue(client, stdout_chunks)
                raise exc
            _drain_stdout_queue(client, stdout_chunks)
            client_stderr = client.stderr.read() if client.stderr else ""
            if client_stderr:
                stderr_chunks.append(client_stderr.strip())
            if client.returncode != 0:
                raise subprocess.CalledProcessError(
                    client.returncode,
                    client.args,
                    output="\n".join(stdout_chunks),
                    stderr=client_stderr,
                )
        else:
            client_run = subprocess.run(
                [
                    sys.executable,
                    str(client_path),
                    "--endpoint",
                    endpoint,
                    "--transport",
                    transport,
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
                timeout=client_timeout_s,
                check=True,
            )
            if client_run.stdout:
                stdout_chunks.append(client_run.stdout.strip())
            if client_run.stderr:
                stderr_chunks.append(client_run.stderr.strip())
    except subprocess.CalledProcessError as exc:
        if exc.stdout:
            stdout_chunks.append(_coerce_text(exc.stdout).strip())
        if exc.stderr:
            stderr_chunks.append(_coerce_text(exc.stderr).strip())
        output = "\n".join(chunk for chunk in stderr_chunks + stdout_chunks if chunk)
        if _is_unsupported_output(output):
            return f"UNSUPPORTED,current,MULTI_{pattern},{transport}"
        raise SystemExit(output)
    except subprocess.TimeoutExpired as exc:
        if exc.stdout:
            stdout_chunks.append(_coerce_text(exc.stdout).strip())
        if exc.stderr:
            stderr_chunks.append(_coerce_text(exc.stderr).strip())
        output = "\n".join(
            chunk
            for chunk in stderr_chunks + stdout_chunks + [f"client timed out for pattern {pattern}"]
            if chunk
        )
        if _is_unsupported_output(output):
            return f"UNSUPPORTED,current,MULTI_{pattern},{transport}"
        raise SystemExit(output)
    finally:
        if client is not None and hasattr(client, "poll") and client.poll() is None:
            _terminate_process(client, grace_seconds=shutdown_grace_s)
        _terminate_process(server, grace_seconds=shutdown_grace_s)
        if server.stderr:
            err = server.stderr.read().strip()
            if err:
                stderr_chunks.append(err)
    if stderr_chunks:
        stderr_text = "\n".join(chunk for chunk in stderr_chunks if chunk)
        if _is_unsupported_output(stderr_text):
            return f"UNSUPPORTED,current,MULTI_{pattern},{transport}"
    return "\n".join(chunk for chunk in stdout_chunks if chunk)


def _build_options(args, patterns, transports, requested_msg_sizes, clients):
    return {
        "lang": "python",
        "suite": "multi",
        "runs": args.runs,
        "patterns": ",".join(f"MULTI_{pattern}" for pattern in patterns),
        "transports": _grouped_option_text(
            patterns,
            lambda pattern: _transports_for_pattern(pattern, transports),
        ),
        "msg_sizes": _grouped_option_text(
            patterns,
            lambda pattern: _msg_sizes_for_pattern(pattern, requested_msg_sizes),
        ),
        "clients": clients,
        "pin_cpu": "on" if args.pin_cpu else "off",
        "duration_seconds": args.duration,
    }


def main(argv=None):
    args = parse_args(argv or sys.argv[1:])
    if args.pin_cpu and not pin_current_process_cpu0():
        print("warning: cpu pinning requested but could not pin to cpu 0", file=sys.stderr)
    patterns = _parse_patterns(args.pattern)
    transports = _parse_transports(args.transports)
    requested_msg_sizes = _requested_msg_sizes(args)
    clients = _options_clients_display(patterns, args.clients)
    runs = int(args.runs)
    if runs <= 0:
        raise SystemExit("--runs must be > 0")
    configs = _selected_configs(patterns, transports, requested_msg_sizes)

    env = dict(os.environ)
    env["PYTHONPATH"] = args.pythonpath
    env["PERF_MULTI_DURATION_SECONDS"] = str(args.duration)
    if args.io_threads:
        env["PERF_MULTI_SERVER_IO_THREADS"] = args.io_threads
        env["PERF_MULTI_CLIENT_IO_THREADS"] = args.io_threads
    if args.server_io_threads:
        env["PERF_MULTI_SERVER_IO_THREADS"] = args.server_io_threads
    if args.client_io_threads:
        env["PERF_MULTI_CLIENT_IO_THREADS"] = args.client_io_threads
    if args.hwm:
        env["PERF_MULTI_HWM"] = args.hwm
    if args.send_hwm:
        env["PERF_MULTI_SNDHWM"] = args.send_hwm
    if args.recv_hwm:
        env["PERF_MULTI_RCVHWM"] = args.recv_hwm
    if args.send_timeout_ms:
        env["PERF_MULTI_SNDTIMEO_MS"] = args.send_timeout_ms
    if args.recv_timeout_ms:
        env["PERF_MULTI_RCVTIMEO_MS"] = args.recv_timeout_ms
    if args.connect_concurrency:
        env["PERF_MULTI_CONNECT_CONCURRENCY"] = args.connect_concurrency
    if args.transport_transition_ms:
        env["PERF_MULTI_TRANSPORT_TRANSITION_MS"] = args.transport_transition_ms
    if args.pattern_transition_ms:
        env["PERF_MULTI_PATTERN_TRANSITION_MS"] = args.pattern_transition_ms
    if args.server_ready_timeout_ms:
        env["PERF_MULTI_SERVER_READY_TIMEOUT_MS"] = args.server_ready_timeout_ms
    if args.connect_ready_timeout_ms:
        env["PERF_MULTI_CONNECT_READY_TIMEOUT_MS"] = args.connect_ready_timeout_ms
    if args.monitor_hwm:
        env["PERF_MULTI_MONITOR_HWM"] = args.monitor_hwm
    if args.server_shutdown_timeout_ms:
        env["PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS"] = args.server_shutdown_timeout_ms
    if args.server_bind_port:
        env["PERF_MULTI_SERVER_BIND_PORT"] = args.server_bind_port
    if RUNNER_LIB.exists():
        env["ZLINK_LIBRARY_PATH"] = str(RUNNER_LIB)
        lib_dir = str(RUNNER_LIB.parent)
        env["LD_LIBRARY_PATH"] = (
            f"{lib_dir}:{env['LD_LIBRARY_PATH']}"
            if env.get("LD_LIBRARY_PATH")
            else lib_dir
        )

    run_cooldown_ms = _env_int("PERF_MULTI_RUN_COOLDOWN_MS", 3000)
    transport_transition_ms = _env_int("PERF_MULTI_TRANSPORT_TRANSITION_MS", 3000)
    pattern_transition_ms = _env_int("PERF_MULTI_PATTERN_TRANSITION_MS", 3000)

    options = _build_options(args, patterns, transports, requested_msg_sizes, clients)
    sections = []
    emitted_chunks = []
    status_lines = []
    failures = []
    fail_count = 0
    case_ordinal = 1
    _append_line(sections, render_effective_options(options))
    _append_line(sections)
    for pattern_index, pattern in enumerate(patterns):
        pattern_clients = _clients_for_pattern(pattern, args.clients)
        if pattern_index > 0:
            _append_line(sections, "===============================================================================")
            _append_line(sections)
        _append_line(sections, f"  > Benchmarking current for MULTI_{pattern}...")
        pattern_transports = _transports_for_pattern(pattern, transports)
        pattern_msg_sizes = _msg_sizes_for_pattern(pattern, requested_msg_sizes)
        size_label = ",".join(f"{msg_size}B" for msg_size in pattern_msg_sizes)
        for transport_index, transport in enumerate(pattern_transports):
            _append_line(sections, f"    Testing {transport} | {size_label}:")
            transport_failures = 0
            transport_all_unsupported = True
            run_outputs = {msg_size: [] for msg_size in pattern_msg_sizes}
            if runs == 1:
                for header_line in table_header_lines():
                    _append_line(sections, f"      {header_line}")
                for msg_size in pattern_msg_sizes:
                    case_env = dict(env)
                    case_env["PERF_RUN_ID"] = str(case_ordinal)
                    if (
                        "PERF_MULTI_HWM" not in case_env
                        and "PERF_MULTI_SNDHWM" not in case_env
                        and "PERF_MULTI_RCVHWM" not in case_env
                    ):
                        case_env["PERF_MULTI_HWM"] = "10" if pattern == "STREAM" else "100"
                    case_ordinal += 1
                    try:
                        output = _run_pattern(
                            args, case_env, pattern, transport, msg_size, pattern_clients
                        )
                    except SystemExit as exc:
                        output = str(exc).strip()
                    status_kind = _status_kind(output)
                    if status_kind == "fail":
                        fail_count += 1
                        transport_failures += 1
                    if output:
                        emitted_chunks.append(output)
                        status_lines.extend(_parse_status_lines(output))
                        run_outputs[msg_size].append(output)
                    metrics = _result_metrics_for_case(output, pattern, transport, msg_size)
                    if metrics:
                        transport_all_unsupported = False
                        _append_line(sections, _metric_row(pattern, msg_size, metrics))
                    elif status_kind == "unsupported":
                        _append_line(sections, _status_row(msg_size, "N/A"))
                    else:
                        transport_all_unsupported = False
                        if status_kind == "fail":
                            failures.append(
                                f"- MULTI_{pattern} current {transport} {msg_size}B: {_failure_reason(output)}"
                            )
                        _append_line(sections, _status_row(msg_size, "FAIL"))
            else:
                for run_index in range(runs):
                    _append_line(sections, f"      run {run_index + 1}/{runs}:")
                    for header_line in table_header_lines():
                        _append_line(sections, f"        {header_line}")
                    for msg_size in pattern_msg_sizes:
                        case_env = dict(env)
                        case_env["PERF_RUN_ID"] = str(case_ordinal)
                        if (
                            "PERF_MULTI_HWM" not in case_env
                            and "PERF_MULTI_SNDHWM" not in case_env
                            and "PERF_MULTI_RCVHWM" not in case_env
                        ):
                            case_env["PERF_MULTI_HWM"] = "10" if pattern == "STREAM" else "100"
                        case_ordinal += 1
                        try:
                            output = _run_pattern(
                                args,
                                case_env,
                                pattern,
                                transport,
                                msg_size,
                                pattern_clients,
                            )
                        except SystemExit as exc:
                            output = str(exc).strip()
                        status_kind = _status_kind(output)
                        if status_kind == "fail":
                            fail_count += 1
                            transport_failures += 1
                        if output:
                            emitted_chunks.append(output)
                            status_lines.extend(_parse_status_lines(output))
                            run_outputs[msg_size].append(output)
                        metrics = _result_metrics_for_case(output, pattern, transport, msg_size)
                        if metrics:
                            transport_all_unsupported = False
                            _append_line(
                                sections,
                                _metric_row(pattern, msg_size, metrics, indent="        "),
                            )
                        elif status_kind == "unsupported":
                            _append_line(
                                sections,
                                _status_row(msg_size, "N/A", indent="        "),
                            )
                        else:
                            transport_all_unsupported = False
                            if status_kind == "fail":
                                failures.append(
                                    f"- MULTI_{pattern} current {transport} {msg_size}B: {_failure_reason(output)}"
                                )
                            _append_line(
                                sections,
                                _status_row(msg_size, "FAIL", indent="        "),
                            )
                    if run_index + 1 < runs:
                        _append_line(sections, f"      [cooldown {run_cooldown_ms}ms]")
                        time.sleep(run_cooldown_ms / 1000.0)
                _append_line(sections, "      median:")
                for header_line in table_header_lines():
                    _append_line(sections, f"        {header_line}")
                for msg_size in pattern_msg_sizes:
                    metrics = _median_metrics(
                        run_outputs[msg_size], pattern, transport, msg_size
                    )
                    if metrics:
                        transport_all_unsupported = False
                        _append_line(
                            sections,
                            _metric_row(pattern, msg_size, metrics, indent="        "),
                        )
                    elif any(_status_kind(output) == "unsupported" for output in run_outputs[msg_size]):
                        _append_line(
                            sections,
                            _status_row(msg_size, "N/A", indent="        "),
                        )
                    else:
                        transport_all_unsupported = False
                        _append_line(
                            sections,
                            _status_row(msg_size, "FAIL", indent="        "),
                        )
            if transport_all_unsupported:
                suffix = "unsupported Done"
            elif transport_failures:
                suffix = f"(failures={transport_failures}) Done"
            else:
                suffix = "Done"
            _append_line(sections, f"    Testing {transport}: {suffix}")
            if transport_index + 1 < len(pattern_transports):
                _append_line(
                    sections,
                    f"    [transport cooldown {transport_transition_ms}ms]",
                )
                time.sleep(transport_transition_ms / 1000.0)
        if pattern_index + 1 < len(patterns):
            time.sleep(pattern_transition_ms / 1000.0)
        _append_line(sections)
    rows = parse_result_lines("\n".join(emitted_chunks), warn=_warn_runner)
    skipped_cases = 0
    unsupported_cases = 0
    for line in status_lines:
        if line.startswith("SKIP,"):
            skipped_cases += 1
        elif line.startswith("UNSUPPORTED,"):
            unsupported_cases += 1
    total_cases = len(configs) * runs
    expected_cases = max(0, total_cases - skipped_cases - unsupported_cases)
    expected_result_lines = expected_cases * 5
    status = "complete" if len(rows) == expected_result_lines else "partial"
    if failures:
        _append_line(sections)
        _append_line(sections, "## Failures")
        for line in failures:
            _append_line(sections, line)
        _append_line(sections)
    _append_line(sections, render_effective_options(options, section="result"))
    _append_line(sections, "## Completion")
    _append_line(sections, f"- status: {status}")
    _append_line(sections, f"- expected_result_lines: {expected_result_lines}")
    _append_line(sections, f"- actual_result_lines: {len(rows)}")
    _append_line(sections, f"- skip_cases: {skipped_cases}")
    _append_line(sections, f"- unsupported_cases: {unsupported_cases}")
    _append_line(sections, f"- fail: {fail_count}")
    final_output = "\n".join(sections).rstrip() + "\n"

    report_path = build_report_path(
        lang="python",
        suite="multi",
        results_dir=args.results_dir or None,
        tag=args.results_tag or None,
    )
    report_path.write_text(final_output, encoding="utf-8")
    if args.output:
        Path(args.output).write_text(final_output, encoding="utf-8")
    if status != "complete":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
