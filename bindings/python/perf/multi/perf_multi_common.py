import argparse
import os
import sys
import time
from pathlib import Path

PERF_DIR = Path(__file__).resolve().parent.parent
perf_dir_text = str(PERF_DIR)
if perf_dir_text not in sys.path:
    sys.path.insert(0, perf_dir_text)

from perf_metrics import (
    HEADER_SIZE,
    benchmark_run_id,
    build_report_path,
    configure_tls_client,
    configure_tls_server,
    decode_header,
    extract_metric_payload,
    latency_ns_from_message,
    new_payload,
    is_active_message,
    payload_phase,
    parse_result_lines,
    pin_current_process_cpu0,
    print_result_lines,
    resolve_multi_connect_ready_timeout_ms,
    render_effective_options,
    render_markdown_summary,
    result_metrics,
    rows_by_case,
    safe_poll,
    stamp_payload,
    status_row_text,
    table_header_lines,
    throughput_unit,
    tcp_endpoint,
    transport_endpoint,
    wait_monitor_event,
    _require_zlink,
)


TOPIC = b"bench"


def parse_client_args(argv, *, pattern):
    parser = argparse.ArgumentParser(prog=f"perf_multi_{pattern.lower()}_client.py")
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--transport", default="tcp")
    parser.add_argument("--duration", type=float, default=5.0)
    parser.add_argument("--msg-size", type=int, default=64)
    parser.add_argument("--clients", type=int, default=100)
    args = parser.parse_args(argv)
    if args.duration <= 0 or args.msg_size < HEADER_SIZE or args.clients <= 0:
        raise SystemExit("invalid perf arguments")
    args.transport = args.transport.lower()
    return args


def parse_server_args(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint")
    parser.add_argument("--transport", default="tcp")
    parser.add_argument("--duration", type=float, default=5.0)
    parser.add_argument("--clients", type=int, default=100)
    parser.add_argument("--msg-size", type=int, default=64)
    args = parser.parse_args(argv)
    args.transport = args.transport.lower()
    if args.duration <= 0 or args.msg_size < HEADER_SIZE or args.clients <= 0:
        raise SystemExit("invalid perf arguments")
    return args


def _env_int(name, default):
    value = os.environ.get(name)
    if value in (None, ""):
        return default
    try:
        return int(value)
    except ValueError:
        return default


def resolve_multi_send_hwm():
    return _env_int("PERF_MULTI_SNDHWM", _env_int("PERF_MULTI_HWM", 1000))


def resolve_multi_recv_hwm():
    return _env_int("PERF_MULTI_RCVHWM", _env_int("PERF_MULTI_HWM", 1000))


def resolve_multi_send_timeout_ms():
    return _env_int("PERF_MULTI_SNDTIMEO_MS", 200)


def resolve_multi_recv_timeout_ms():
    return _env_int("PERF_MULTI_RCVTIMEO_MS", 200)


def resolve_multi_spot_ready_settle_s():
    return _env_int("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000) / 1000.0


def resolve_multi_spot_control_settle_s():
    return _env_int("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25) / 1000.0


def resolve_multi_server_ready_timeout_ms():
    return _env_int("PERF_MULTI_SERVER_READY_TIMEOUT_MS", 10000)


def resolve_multi_server_shutdown_timeout_ms():
    return _env_int("PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS", 5000)


def resolve_multi_timeout_seconds(duration_seconds, pattern, transport, msg_size):
    override = _env_int("PERF_MULTI_TIMEOUT_SECONDS", 0)
    if override > 0:
        return override
    size = max(int(msg_size), 64)
    duration = max(float(duration_seconds), 1.0)
    if pattern == "STREAM":
        return max(45, int(duration * 3.0) + 20)
    if pattern == "SPOT":
        return max(90, int(duration * 6.0) + 30)
    if transport in {"tls", "wss"} and size >= 131072:
        return max(90, int(duration * 6.0) + 30)
    return max(45, int(duration * 3.0) + 20)


def configure_multi_tls_server(target, transport):
    configure_tls_server(target, transport)


def configure_multi_tls_client(target, transport):
    configure_tls_client(target, transport)


def apply_multi_socket_options(*sockets, receive_timeout_ms=None):
    send_hwm = resolve_multi_send_hwm()
    recv_hwm = resolve_multi_recv_hwm()
    send_timeout_ms = resolve_multi_send_timeout_ms()
    recv_timeout_ms = (
        resolve_multi_recv_timeout_ms()
        if receive_timeout_ms is None
        else receive_timeout_ms
    )
    for sock in sockets:
        sock.options.linger_ms = 0
        sock.options.send_high_water_mark = send_hwm
        sock.options.receive_high_water_mark = recv_hwm
        sock.options.send_timeout_ms = send_timeout_ms
        sock.options.receive_timeout_ms = recv_timeout_ms


def apply_multi_spot_node_admission(*nodes):
    send_hwm = resolve_multi_send_hwm()
    recv_hwm = resolve_multi_recv_hwm()
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


def send_nonblocking(sock, payload, *, method="send", routing_id=None):
    zlink_mod = _require_zlink()
    send_method = getattr(sock, method)
    try:
        if routing_id is None:
            return bool(send_method(payload, flags=zlink_mod.SendFlags.DONT_WAIT))
        else:
            return bool(
                send_method(routing_id, payload, flags=zlink_mod.SendFlags.DONT_WAIT)
            )
    except zlink_mod.SubmitError as exc:
        if exc.result == zlink_mod.SubmitResult.BACKPRESSURED:
            return False
        raise


def send_to_spot_nonblocking(sock, dest_node_rid, dest_spot_rid, payload):
    zlink_mod = _require_zlink()
    try:
        op = sock.send_to_spot(dest_node_rid, dest_spot_rid).flags(zlink_mod.SendFlags.DONT_WAIT)
        if isinstance(payload, (list, tuple)):
            op.messages(*payload)
        else:
            op.message(payload)
        return bool(op.submit())
    except zlink_mod.SubmitError as exc:
        if exc.result == zlink_mod.SubmitResult.BACKPRESSURED:
            return False
        raise


def publish_nonblocking(sock, topic, payload):
    zlink_mod = _require_zlink()
    try:
        return bool(sock.publish(topic, payload, flags=zlink_mod.SendFlags.DONT_WAIT))
    except zlink_mod.SubmitError as exc:
        if exc.result == zlink_mod.SubmitResult.BACKPRESSURED:
            return False
        raise


def spot_publish_nonblocking(spot, channel_name, topic, payload):
    zlink_mod = _require_zlink()
    try:
        op = spot.publish(topic).flags(zlink_mod.SendFlags.DONT_WAIT)
        if isinstance(payload, (list, tuple)):
            op.messages(*payload)
        else:
            op.message(payload)
        return bool(op.submit())
    except zlink_mod.SubmitError as exc:
        if exc.result == zlink_mod.SubmitResult.BACKPRESSURED:
            return False
        raise


def wait_for_command_line(stream, *, deadline):
    while True:
        remaining = deadline - time.perf_counter()
        if remaining <= 0:
            return None
        line = stream.readline()
        if not line:
            return None
        text = line.strip()
        if text:
            return text


def attach_spot_service_pair(ctx, node, channel_name):
    dealer = _require_zlink().DealerSocket(ctx)
    node.attach_channel_dealer_manual(channel_name, dealer)
    return dealer, None


def benchmark_endpoint(transport, prefix):
    endpoint = transport_endpoint(transport, prefix)
    bind_port = _env_int("PERF_MULTI_SERVER_BIND_PORT", 0)
    if bind_port <= 0:
        return endpoint
    scheme = transport.lower()
    if scheme not in {"tcp", "tls", "ws", "wss"}:
        return endpoint
    return f"{scheme}://127.0.0.1:{bind_port}"
