import argparse
import os
import sys
import time
from pathlib import Path

PERF_DIR = Path(__file__).resolve().parent.parent
perf_dir_text = str(PERF_DIR)
if perf_dir_text not in sys.path:
    sys.path.insert(0, perf_dir_text)

from perf_stop_token import (
    STOP_TOKEN,
    is_stop_token,
    is_stop_token_in_parts,
)

from perf_metrics import (
    HEADER_MAGIC,
    HEADER_SIZE,
    benchmark_run_id,
    build_report_path,
    configure_tls_client,
    configure_tls_server,
    decode_header,
    extract_metric_payload,
    latency_ns_from_message,
    new_payload,
    parse_result_lines,
    print_result_lines,
    is_active_message,
    payload_phase,
    pin_current_process_cpu0,
    render_effective_options,
    render_markdown_summary,
    result_metrics,
    resolve_single_connect_ready_timeout_ms,
    resolve_single_timeout_seconds,
    rows_by_case,
    safe_poll,
    stamp_payload,
    status_row_text,
    table_header_lines,
    throughput_unit,
    transport_endpoint,
    wait_monitor_event,
    _require_zlink,
)


def parse_single_args(argv, *, pattern):
    parser = argparse.ArgumentParser(prog=f"perf_{pattern.lower()}.py")
    parser.add_argument("--transport", default="tcp")
    parser.add_argument("--duration", type=float, default=5.0)
    parser.add_argument("--msg-size", type=int, default=256)
    args = parser.parse_args(argv)
    if args.duration <= 0:
        raise SystemExit("--duration must be > 0")
    if args.msg_size < HEADER_SIZE:
        raise SystemExit(f"--msg-size must be >= {HEADER_SIZE}")
    args.transport = args.transport.lower()
    return args


def _env_int(name, default):
    value = os.environ.get(name)
    if value in (None, ""):
        return default
    try:
        return int(value)
    except ValueError:
        return default


def resolve_single_send_hwm():
    return _env_int("PERF_SINGLE_SNDHWM", _env_int("PERF_SINGLE_HWM", 1000))


def resolve_single_recv_hwm():
    return _env_int("PERF_SINGLE_RCVHWM", _env_int("PERF_SINGLE_HWM", 1000))


def resolve_single_send_timeout_ms():
    return _env_int("PERF_SINGLE_SNDTIMEO_MS", -1)


def resolve_single_recv_timeout_ms():
    return _env_int("PERF_SINGLE_RCVTIMEO_MS", 200)


def resolve_single_pubsub_recv_timeout_ms():
    return _env_int(
        "PERF_SINGLE_PUBSUB_RCVTIMEO_MS",
        resolve_single_recv_timeout_ms(),
    )


def resolve_single_pubsub_ready_settle_s():
    return _env_int("PERF_SINGLE_PUBSUB_READY_SETTLE_MS", 1000) / 1000.0


def resolve_single_spot_ready_settle_s():
    return _env_int("PERF_SINGLE_SPOT_READY_SETTLE_MS", 1000) / 1000.0


def resolve_single_endpoint(transport, prefix):
    return transport_endpoint(transport, prefix)


def configure_single_tls_server(target, transport):
    configure_tls_server(target, transport)


def configure_single_tls_client(target, transport):
    configure_tls_client(target, transport)


def apply_single_socket_options(*sockets, receive_timeout_ms=None):
    send_hwm = resolve_single_send_hwm()
    recv_hwm = resolve_single_recv_hwm()
    send_timeout_ms = resolve_single_send_timeout_ms()
    recv_timeout_ms = (
        resolve_single_recv_timeout_ms()
        if receive_timeout_ms is None
        else receive_timeout_ms
    )
    for sock in sockets:
        sock.options.linger_ms = 0
        sock.options.send_high_water_mark = send_hwm
        sock.options.receive_high_water_mark = recv_hwm
        if send_timeout_ms >= 0:
            sock.options.send_timeout_ms = send_timeout_ms
        sock.options.receive_timeout_ms = recv_timeout_ms


def apply_single_spot_node_admission(*nodes):
    send_hwm = resolve_single_send_hwm()
    recv_hwm = resolve_single_recv_hwm()
    for node in nodes:
        node.set_pubsub_hwm(send_hwm)
        node.set_router_hwm(recv_hwm)


def recv_nonblocking(sock, *, method="recv"):
    zlink_mod = _require_zlink()
    recv_method = getattr(sock, method)
    try:
        return recv_method(flags=zlink_mod.RecvFlags.DONT_WAIT)
    except zlink_mod.RecvError as exc:
        if exc.result == zlink_mod.RecvResult.NO_DATA:
            return None
        raise
