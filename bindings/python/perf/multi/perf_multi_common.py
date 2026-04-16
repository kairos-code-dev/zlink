import argparse
import sys
import struct
import time
from pathlib import Path

PERF_DIR = Path(__file__).resolve().parent.parent
perf_dir_text = str(PERF_DIR)
if perf_dir_text not in sys.path:
    sys.path.insert(0, perf_dir_text)

from perf_metrics import (
    build_report_path,
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
    tcp_endpoint,
    wait_monitor_event,
    _require_zlink,
)


TOPIC = b"bench"


def parse_client_args(argv, *, pattern):
    parser = argparse.ArgumentParser(prog=f"perf_multi_{pattern.lower()}_client.py")
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--duration", type=float, default=2.0)
    parser.add_argument("--msg-size", type=int, default=256)
    parser.add_argument("--clients", type=int, default=4)
    args = parser.parse_args(argv)
    if args.duration <= 0 or args.msg_size < 16 or args.clients <= 0:
        raise SystemExit("invalid perf arguments")
    return args


def parse_server_args(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--clients", type=int, default=4)
    parser.add_argument("--msg-size", type=int, default=256)
    return parser.parse_args(argv)


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
    zlink_mod = _require_zlink()
    pub_sock = zlink_mod.PubSocket(ctx)
    sub_sock = zlink_mod.SubSocket(ctx)
    endpoint = tcp_endpoint()
    pub_sock.bind(endpoint)
    sub_sock.connect(endpoint)
    sub_sock.set_subscription(b"")
    node.attach_pubsub(service_name, pub_sock, sub_sock)
    return pub_sock, sub_sock
