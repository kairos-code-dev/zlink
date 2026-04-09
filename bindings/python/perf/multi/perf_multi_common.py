import argparse
import sys
import struct
from pathlib import Path

PERF_DIR = Path(__file__).resolve().parent.parent
perf_dir_text = str(PERF_DIR)
if perf_dir_text not in sys.path:
    sys.path.insert(0, perf_dir_text)

from perf_metrics import (
    benchmark_run_id,
    build_report_path,
    ensure_report_path,
    latency_ns_from_message,
    new_payload,
    is_active_message,
    parse_result_lines,
    platform_name,
    print_result_lines,
    render_effective_options,
    render_markdown_summary,
    resolve_results_dir,
    result_metrics,
    safe_poll,
    stamp_payload,
    tcp_endpoint,
    wait_monitor_event,
    wait_socket_event,
    write_report,
)


TOPIC = b"bench"
DEFAULT_READY_TIMEOUT_MS = 5000


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


def new_payload(size):
    return bytearray(size)


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
