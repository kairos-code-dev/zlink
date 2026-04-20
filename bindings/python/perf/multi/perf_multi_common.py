import argparse
import os
import sys
import struct
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
    extract_metric_payload,
    latency_ns_from_message,
    new_payload,
    is_active_message,
    payload_phase,
    parse_result_lines,
    print_result_lines,
    render_effective_options,
    render_markdown_summary,
    result_metrics,
    safe_poll,
    stamp_payload,
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
    parser.add_argument("--transport", default="tcp")
    parser.add_argument("--clients", type=int, default=100)
    parser.add_argument("--msg-size", type=int, default=64)
    args = parser.parse_args(argv)
    args.transport = args.transport.lower()
    if args.msg_size < HEADER_SIZE or args.clients <= 0:
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


def encode_len32be(payload):
    return struct.pack("!I", len(payload)) + payload


def recv_exact(sock, size):
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise RuntimeError("connection closed")
        chunks.extend(chunk)
    return bytes(chunks)


def recv_len32be(sock):
    header = recv_exact(sock, 4)
    size = struct.unpack("!I", header)[0]
    payload = recv_exact(sock, size)
    return payload


def parse_len32be_frames(buffer):
    frames = []
    offset = 0
    while len(buffer) - offset >= 4:
        size = struct.unpack_from("!I", buffer, offset)[0]
        frame_end = offset + 4 + size
        if len(buffer) < frame_end:
            break
        frames.append(bytes(buffer[offset:frame_end]))
        offset = frame_end
    if offset:
        del buffer[:offset]
    return frames


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
            send_method(payload, flags=zlink_mod.SendFlags.DONT_WAIT)
        else:
            send_method(routing_id, payload, flags=zlink_mod.SendFlags.DONT_WAIT)
        return True
    except zlink_mod.SubmitError as exc:
        if exc.result in (
            zlink_mod.SubmitResult.BACKPRESSURED,
            zlink_mod.SubmitResult.NOT_CONNECTED,
            zlink_mod.SubmitResult.NOT_ADMITTED,
        ):
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


def attach_spot_service_pair(ctx, node, service_name):
    dealer = _require_zlink().DealerSocket(ctx)
    node.attach_channel_dealer_manual(service_name, dealer)
    return dealer, None


def benchmark_endpoint(transport, prefix):
    return transport_endpoint(transport, prefix)
