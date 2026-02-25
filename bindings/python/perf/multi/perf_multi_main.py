#!/usr/bin/env python3
"""Split-process multi benchmark runner for Python bindings.

Server:
  perf_multi_main.py --role server --pattern MULTI_STREAM --transport tcp [--size 1024]

Client:
  perf_multi_main.py --role client --pattern MULTI_STREAM --transport tcp --size 1024 --endpoint tcp://127.0.0.1:15557
"""

from __future__ import annotations

import argparse
import os
import resource
import socket
import sys
import time
from pathlib import Path
from typing import Callable, Optional

ROOT = Path(__file__).resolve().parents[4]
SINGLE_DIR = ROOT / "bindings" / "python" / "perf" / "single"
PY_SRC_DIR = ROOT / "bindings" / "python" / "src"
if str(SINGLE_DIR) not in sys.path:
    sys.path.insert(0, str(SINGLE_DIR))
if str(PY_SRC_DIR) not in sys.path:
    sys.path.insert(0, str(PY_SRC_DIR))

from perf_common import (  # type: ignore
    StreamCallbackEcho,
    StreamLen32BeStash,
    int_sockopt,
    parse_env,
    zlink,
)
from perf_stream_client_common import emit_perf_lines, run_client  # type: ignore
from zlink._ffi import lib as zlink_lib  # type: ignore

STOP_TOKEN = b"__zlink_perf_stop__"

ECHO_PATTERNS = {
    "MULTI_DEALER_ROUTER",
    "MULTI_ROUTER_ROUTER",
    "MULTI_STREAM",
    "MULTI_STREAM_CALLBACK",
    "MULTI_STREAM_LEN32BE",
}

def get_port() -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = int(sock.getsockname()[1])
    sock.close()
    return port


def make_endpoint(transport: str, bind_port: int) -> str:
    port = bind_port if bind_port > 0 else get_port()
    return f"{transport}://127.0.0.1:{port}"


def open_monitor(socket_obj: object, events: int) -> object:
    handle = zlink_lib().zlink_socket_monitor_open(socket_obj._handle, int(events))
    if not handle:
        raise RuntimeError("zlink_socket_monitor_open failed")
    return zlink.MonitorSocket(handle)


def wait_monitor_ready(monitor: object, timeout_ms: int, accept_fallback: bool) -> bool:
    poller = zlink.Poller()
    poller.add_socket(monitor, int(zlink.PollEvent.POLLIN))

    deadline = time.monotonic() + (max(1, timeout_ms) / 1000.0)
    while time.monotonic() < deadline:
        remain = max(1, int((deadline - time.monotonic()) * 1000))
        ready = poller.poll(remain)
        if not ready:
            continue

        evt = monitor.recv(int(zlink.ReceiveFlag.NONE))
        event_id = int(evt.get("event", 0))
        if event_id == int(zlink.MonitorEvent.CONNECTION_READY):
            return True
        if accept_fallback and event_id in (
            int(zlink.MonitorEvent.ACCEPTED),
            int(zlink.MonitorEvent.CONNECTED),
        ):
            return True
    return False


def is_echo(pattern: str) -> bool:
    return pattern in ECHO_PATTERNS


def is_stream_pattern(pattern: str) -> bool:
    return pattern in {
        "MULTI_STREAM",
        "MULTI_STREAM_CALLBACK",
        "MULTI_STREAM_LEN32BE",
    }


def bandwidth_mbps(pattern: str, throughput: float, size: int) -> float:
    multiplier = 2.0 if is_echo(pattern) else 1.0
    return (throughput * float(size) * multiplier) / 1_000_000.0


def print_client_results(
    pattern: str, transport: str, size: int, throughput: float, latency_us: float
) -> None:
    bandwidth = bandwidth_mbps(pattern, throughput, size)
    print(f"RESULT,current,{pattern},{transport},{size},throughput,{throughput}")
    print(f"RESULT,current,{pattern},{transport},{size},bandwidth,{bandwidth}")
    print(f"RESULT,current,{pattern},{transport},{size},latency,{latency_us}")


def print_server_metrics(pattern: str, transport: str, size: int, cpu_t0: float, wall_t0: float) -> None:
    wall = max(1e-9, time.perf_counter() - wall_t0)
    cpu_used = max(0.0, time.process_time() - cpu_t0)
    ncpu = max(1.0, float(os.cpu_count() or 1))
    cpu_pct = (cpu_used / (wall * ncpu)) * 100.0
    rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if sys.platform == "darwin":
        mem_mb = float(rss) / (1024.0 * 1024.0)
    else:
        mem_mb = float(rss) / 1024.0
    print(f"RESULT,current,{pattern},{transport},{size},server_cpu_pct,{cpu_pct}")
    print(f"RESULT,current,{pattern},{transport},{size},server_mem_mb,{mem_mb}")


def run_server_router(pattern: str, transport: str, endpoint: str, size: int) -> int:
    cpu_t0 = time.process_time()
    wall_t0 = time.perf_counter()
    ctx = zlink.Context()
    server = zlink.Socket(ctx, int(zlink.SocketType.ROUTER))
    recv_none = int(zlink.ReceiveFlag.NONE)
    send_none = int(zlink.SendFlag.NONE)
    send_more = int(zlink.SendFlag.SNDMORE)
    rid_buf = bytearray(256)
    payload_buf = bytearray(max(size, len(STOP_TOKEN), 256))
    rid_view = memoryview(rid_buf)
    payload_view = memoryview(payload_buf)
    rcv_timeout_ms = parse_env("PERF_MULTI_RCVTIMEO_MS", 5000)
    ready_timeout_ms = parse_env("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000)
    monitor: Optional[object] = None

    try:
        server.setsockopt(int(zlink.SocketOption.RCVTIMEO), int_sockopt(rcv_timeout_ms))
        monitor = open_monitor(
            server,
            int(zlink.MonitorEvent.CONNECTION_READY)
            | int(zlink.MonitorEvent.ACCEPTED)
            | int(zlink.MonitorEvent.CONNECTED),
        )
        server.bind(endpoint)
        print(f"READY,{endpoint}", flush=True)
        if not wait_monitor_ready(monitor, ready_timeout_ms, True):
            return 2
        while True:
            rid_len = server.recv_into(rid_buf, recv_none)
            payload_len = server.recv_into(payload_buf, recv_none)
            payload_len = max(0, min(payload_len, len(payload_buf)))
            if payload_buf[:payload_len] == STOP_TOKEN:
                break
            server.send(rid_view[:rid_len], send_more)
            server.send(payload_view[:payload_len], send_none)
        print_server_metrics(pattern, transport, size, cpu_t0, wall_t0)
        return 0
    except Exception:
        return 2
    finally:
        try:
            if monitor is not None:
                monitor.close()
            server.close()
            ctx.close()
        except Exception:
            pass


def run_server_dealer(pattern: str, transport: str, endpoint: str, size: int) -> int:
    cpu_t0 = time.process_time()
    wall_t0 = time.perf_counter()
    ctx = zlink.Context()
    server = zlink.Socket(ctx, int(zlink.SocketType.DEALER))
    recv_none = int(zlink.ReceiveFlag.NONE)
    send_none = int(zlink.SendFlag.NONE)
    payload_buf = bytearray(max(size, len(STOP_TOKEN), 256))
    payload_view = memoryview(payload_buf)
    rcv_timeout_ms = parse_env("PERF_MULTI_RCVTIMEO_MS", 5000)
    ready_timeout_ms = parse_env("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000)
    monitor: Optional[object] = None

    try:
        server.setsockopt(int(zlink.SocketOption.RCVTIMEO), int_sockopt(rcv_timeout_ms))
        monitor = open_monitor(
            server,
            int(zlink.MonitorEvent.CONNECTION_READY)
            | int(zlink.MonitorEvent.ACCEPTED)
            | int(zlink.MonitorEvent.CONNECTED),
        )
        server.bind(endpoint)
        print(f"READY,{endpoint}", flush=True)
        if not wait_monitor_ready(monitor, ready_timeout_ms, True):
            return 2
        while True:
            payload_len = server.recv_into(payload_buf, recv_none)
            payload_len = max(0, min(payload_len, len(payload_buf)))
            if payload_buf[:payload_len] == STOP_TOKEN:
                break
            server.send(payload_view[:payload_len], send_none)
        print_server_metrics(pattern, transport, size, cpu_t0, wall_t0)
        return 0
    except Exception:
        return 2
    finally:
        try:
            if monitor is not None:
                monitor.close()
            server.close()
            ctx.close()
        except Exception:
            pass


def run_server_stream(pattern: str, transport: str, endpoint: str, size: int) -> int:
    cpu_t0 = time.process_time()
    wall_t0 = time.perf_counter()
    ctx = zlink.Context()
    server = zlink.Socket(ctx, int(zlink.SocketType.STREAM))
    rcv_timeout_ms = parse_env("PERF_MULTI_RCVTIMEO_MS", 5000)
    stop_flag = {"value": False}
    len32be = pattern == "MULTI_STREAM_LEN32BE"
    echo = StreamCallbackEcho(server, len32be, echo=True)
    raw_stash = StreamLen32BeStash()

    def stream_handler(rid_view: memoryview, payload_view: memoryview):
        payload = bytes(payload_view)
        if len32be:
            if payload == STOP_TOKEN:
                stop_flag["value"] = True
                return 0
        else:
            for frame in raw_stash.extract_raw_frames(payload):
                if len(frame) >= 4 and frame[4:] == STOP_TOKEN:
                    stop_flag["value"] = True
                    return 0
        return echo._handler(rid_view, payload_view)

    try:
        server.setsockopt(int(zlink.SocketOption.RCVTIMEO), int_sockopt(rcv_timeout_ms))
        mode = (
            int(zlink.StreamDispatchMode.LEN32BE)
            if len32be
            else int(zlink.StreamDispatchMode.NONE)
        )
        server.stream_attach(stream_handler, mode)
        server.bind(endpoint)
        print(f"READY,{endpoint}", flush=True)
        while not stop_flag["value"]:
            time.sleep(0.01)
        print_server_metrics(pattern, transport, size, cpu_t0, wall_t0)
        return 0
    except Exception:
        return 2
    finally:
        try:
            server.stream_detach()
            server.close()
            ctx.close()
        except Exception:
            pass


def choose_server_runner(pattern: str) -> Callable[[str, str, str, int], int]:
    if is_stream_pattern(pattern):
        return run_server_stream
    if pattern in {"MULTI_DEALER_ROUTER", "MULTI_ROUTER_ROUTER"}:
        return run_server_router
    return run_server_dealer


def run_client_echo_router(pattern: str, transport: str, endpoint: str, size: int) -> int:
    _ = pattern
    ctx = zlink.Context()
    client = zlink.Socket(ctx, int(zlink.SocketType.DEALER))
    send_none = int(zlink.SendFlag.NONE)
    recv_none = int(zlink.ReceiveFlag.NONE)
    payload = b"a" * size
    recv_buf = bytearray(max(size, len(STOP_TOKEN), 256))
    snd_timeout_ms = parse_env("PERF_MULTI_SNDTIMEO_MS", 5000)
    rcv_timeout_ms = parse_env("PERF_MULTI_RCVTIMEO_MS", 5000)
    ready_timeout_ms = parse_env("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000)
    warmup_s = max(0, parse_env("PERF_MULTI_WARMUP_SECONDS", 3))
    duration_s = max(1, parse_env("PERF_MULTI_DURATION_SECONDS", 5))
    lat_count = max(1, parse_env("PERF_LAT_COUNT", 200))
    monitor: Optional[object] = None

    try:
        client.setsockopt(int(zlink.SocketOption.SNDTIMEO), int_sockopt(snd_timeout_ms))
        client.setsockopt(int(zlink.SocketOption.RCVTIMEO), int_sockopt(rcv_timeout_ms))
        monitor = open_monitor(
            client,
            int(zlink.MonitorEvent.CONNECTION_READY)
            | int(zlink.MonitorEvent.CONNECTED),
        )
        client.connect(endpoint)
        if not wait_monitor_ready(monitor, ready_timeout_ms, False):
            return 2

        warmup_end = time.monotonic() + float(warmup_s)
        while time.monotonic() < warmup_end:
            client.send_const(payload, send_none)
            client.recv_into(recv_buf, recv_none)

        lat_t0 = time.perf_counter()
        for _ in range(lat_count):
            client.send_const(payload, send_none)
            client.recv_into(recv_buf, recv_none)
        lat_us = ((time.perf_counter() - lat_t0) * 1_000_000.0) / (lat_count * 2)

        ops = 0
        t0 = time.perf_counter()
        deadline = time.monotonic() + float(duration_s)
        while time.monotonic() < deadline:
            client.send_const(payload, send_none)
            client.recv_into(recv_buf, recv_none)
            ops += 1
        elapsed = max(1e-9, time.perf_counter() - t0)
        throughput = float(ops) / elapsed

        client.send_const(STOP_TOKEN, send_none)
        return_code = 0
        print_client_results(pattern, transport, size, throughput, lat_us)
    except Exception:
        return_code = 2
    finally:
        try:
            if monitor is not None:
                monitor.close()
            client.close()
            ctx.close()
        except Exception:
            pass
    return return_code


def run_client_stream_common(pattern: str, transport: str, endpoint: str, size: int) -> int:
    warmup_s = max(0, parse_env("PERF_MULTI_WARMUP_SECONDS", 3))
    duration_s = max(1, parse_env("PERF_MULTI_DURATION_SECONDS", 5))
    lat_count = max(1, parse_env("PERF_LAT_COUNT", 200))
    clients = max(1, parse_env("PERF_MULTI_CLIENTS", 1000))
    io_threads = max(1, parse_env("PERF_IO_THREADS", 4))
    latency_sample_rate = max(1, parse_env("PERF_MULTI_LATENCY_SAMPLE_RATE", 1))

    rc, stdout_text, stderr_text = run_client(
        [
            "--pattern",
            pattern,
            "--transport",
            transport,
            "--endpoint",
            endpoint,
            "--sizes",
            str(size),
            "--runs",
            "1",
            "--ccu",
            str(clients),
            "--warmup",
            str(warmup_s),
            "--duration",
            str(duration_s),
            "--lat-count",
            str(lat_count),
            "--io-threads",
            str(io_threads),
            "--latency-sample-rate",
            str(latency_sample_rate),
            "--print-perf-result",
            "2",
            "--send-stop-token",
            "1",
            "--stop-token",
            STOP_TOKEN.decode("utf-8"),
        ]
    )
    emit_perf_lines(stdout_text)
    has_perf_result = any(
        line.startswith("RESULT,current,") for line in stdout_text.splitlines()
    )
    if rc != 0 or not has_perf_result:
        if stderr_text:
            print(stderr_text.strip(), file=sys.stderr, flush=True)
        return 2
    return 0


def choose_client_runner(pattern: str) -> Callable[[str, str, str, int], int]:
    if is_stream_pattern(pattern):
        return run_client_stream_common
    return run_client_echo_router


def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="python split multi perf runner")
    ap.add_argument("--role", choices=("server", "client"), required=True)
    ap.add_argument("--pattern", required=True)
    ap.add_argument("--transport", required=True)
    ap.add_argument("--size", type=int, default=64)
    ap.add_argument("--endpoint", default="")
    return ap.parse_args(argv)


def main_from_args(argv: Optional[list[str]] = None) -> int:
    args = parse_args(argv)
    pattern = str(args.pattern).upper()
    transport = str(args.transport).lower()
    size = int(args.size)
    if size <= 0:
        return 1

    if args.role == "server":
        bind_port = parse_env("PERF_MULTI_SERVER_BIND_PORT", 0)
        endpoint = make_endpoint(transport, bind_port)
        runner = choose_server_runner(pattern)
        return runner(pattern, transport, endpoint, size)

    endpoint = str(args.endpoint).strip()
    if not endpoint:
        return 1
    runner = choose_client_runner(pattern)
    return runner(pattern, transport, endpoint, size)


def main() -> int:
    return main_from_args(sys.argv[1:])


if __name__ == "__main__":
    raise SystemExit(main())
