import argparse
import importlib
import math
import socket
import sys
import statistics
import struct
import threading
import time
from datetime import datetime
from pathlib import Path

try:
    import zlink
except ModuleNotFoundError:  # pragma: no cover - help/argparse path
    zlink = None


TOPIC = b"bench"
DEFAULT_READY_TIMEOUT_MS = 5000


class CallbackMetrics:
    def __init__(self):
        self._lock = threading.Lock()
        self._ready = threading.Event()
        self._active = threading.Event()
        self.count = 0
        self.latencies = []

    def activate(self):
        self._active.set()

    def deactivate(self):
        self._active.clear()

    def on_payload(self, payload):
        self._ready.set()
        if not self._active.is_set():
            return
        sample = latency_us_from_message(payload)
        with self._lock:
            self.count += 1
            self.latencies.append(sample)

    def wait_ready(self, timeout_s):
        return self._ready.wait(timeout_s)


def parse_client_args(argv, *, pattern, allowed_recv):
    parser = argparse.ArgumentParser(prog=f"perf_multi_{pattern.lower()}_client.py")
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--duration", type=float, default=2.0)
    parser.add_argument("--warmup", type=float, default=1.0)
    parser.add_argument("--msg-size", type=int, default=256)
    parser.add_argument("--clients", type=int, default=4)
    parser.add_argument("--recv", default="recv")
    args = parser.parse_args(argv)
    if args.recv not in allowed_recv:
        raise SystemExit(f"--recv must be one of: {', '.join(sorted(allowed_recv))}")
    if args.duration <= 0 or args.warmup < 0 or args.msg_size < 16 or args.clients <= 0:
        raise SystemExit("invalid perf arguments")
    return args


def parse_server_args(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--clients", type=int, default=4)
    parser.add_argument("--recv", default="recv")
    parser.add_argument("--msg-size", type=int, default=256)
    return parser.parse_args(argv)


def tcp_endpoint():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return f"tcp://127.0.0.1:{port}"


def new_payload(size):
    return bytearray(size)


def stamp_payload(payload):
    struct.pack_into("!Q", payload, 0, time.perf_counter_ns())
    return payload


def latency_us_from_message(data):
    sent_ns = struct.unpack_from("!Q", data, 0)[0]
    return (time.perf_counter_ns() - sent_ns) / 1000.0


def percentile(values, ratio):
    if not values:
        return 0.0
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, math.ceil(len(ordered) * ratio) - 1))
    return float(ordered[index])


def result_metrics(*, count, msg_size, elapsed_s, latencies_us):
    throughput = (count / elapsed_s) if elapsed_s > 0 else 0.0
    bandwidth = ((count * msg_size) / elapsed_s / 1_000_000.0) if elapsed_s > 0 else 0.0
    median = statistics.median(latencies_us) if latencies_us else 0.0
    return {
        "throughput": throughput,
        "bandwidth": bandwidth,
        "latency": float(median),
        "latency_p95": percentile(latencies_us, 0.95),
        "latency_p99": percentile(latencies_us, 0.99),
    }


def print_result_lines(pattern, transport, msg_size, metrics):
    for name in ("throughput", "bandwidth", "latency", "latency_p95", "latency_p99"):
        print(f"RESULT,current,{pattern},{transport},{msg_size},{name},{metrics[name]:.2f}")


def wait_socket_event(socket_obj, event_mask, *, timeout_ms=DEFAULT_READY_TIMEOUT_MS):
    zlink_mod = _require_zlink()
    deadline = time.perf_counter() + (timeout_ms / 1000.0)
    with socket_obj.open_monitor(event_mask) as monitor:
        with zlink_mod.Poller() as poller:
            poller.add_socket(monitor, zlink_mod.PollEvent.POLLIN)
            while True:
                remaining_ms = max(0, int((deadline - time.perf_counter()) * 1000))
                if remaining_ms == 0:
                    raise RuntimeError(f"timed out waiting for socket monitor event {int(event_mask)}")
                events = safe_poll(poller, remaining_ms)
                if not events:
                    if time.perf_counter() >= deadline:
                        raise RuntimeError(
                            f"timed out waiting for socket monitor event {int(event_mask)}"
                        )
                    continue
                event = monitor.recv()
                if int(event.event) != int(event_mask):
                    continue
                if getattr(event, "value", 1) > 0:
                    return event


def wait_pubsub_ready(publisher, subscriber, *, timeout_ms=DEFAULT_READY_TIMEOUT_MS):
    _wait_for_poll_event(publisher, _require_zlink().PollEvent.POLLOUT, timeout_ms=timeout_ms)


def wait_connected_pair(left, right, *, timeout_ms=DEFAULT_READY_TIMEOUT_MS):
    _wait_for_poll_event(right, _require_zlink().PollEvent.POLLOUT, timeout_ms=timeout_ms)


def _wait_for_poll_event(socket_obj, events, *, timeout_ms):
    zlink_mod = _require_zlink()
    with zlink_mod.Poller() as poller:
        poller.add_socket(socket_obj, events)
        ready = safe_poll(poller, timeout_ms)
    if not ready:
        raise RuntimeError(f"timed out waiting for poll event {int(events)}")


def safe_poll(poller, timeout_ms):
    zlink_mod = _require_zlink()
    try:
        return poller.poll(timeout_ms)
    except zlink_mod.ZlinkError as exc:
        if exc.errno == 11:
            return []
        raise


def _require_zlink():
    global zlink
    if zlink is None:
        zlink = importlib.import_module("zlink")
    return zlink


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


def drain_len32be_frames(buffer):
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


def ensure_report_path(suite, recv_mode, tag=None):
    report_dir = Path(__file__).resolve().parents[1] / 'results' / suite / 'report'
    report_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    suffix = f'_{tag}' if tag else ''
    return report_dir / f'perf_{platform_name()}_{recv_mode}_{timestamp}{suffix}.txt'


def write_report(path, *, options, output, status, expected_result_lines, actual_result_lines):
    lines = ['## Effective Options (start)']
    for key, value in options.items():
        lines.append(f'- {key}: {value}')
    lines.append('')
    lines.append(output.rstrip())
    lines.append('')
    lines.append('## Effective Options (result)')
    for key, value in options.items():
        lines.append(f'- {key}: {value}')
    lines.append('')
    lines.append(f'- status: {status}')
    lines.append(f'- expected_result_lines: {expected_result_lines}')
    lines.append(f'- actual_result_lines: {actual_result_lines}')
    path.write_text("\n".join(lines).rstrip() + "\n", encoding='utf-8')


def platform_name():
    if sys.platform.startswith('linux'):
        return 'linux'
    if sys.platform == 'darwin':
        return 'macos'
    if sys.platform.startswith(('win32', 'cygwin', 'msys')):
        return 'windows'
    return sys.platform


def resolve_results_dir(base_dir, suite):
    if base_dir is None:
        return Path(__file__).resolve().parents[1] / 'results' / suite / 'report'
    return Path(base_dir)


def build_report_path(*, suite, recv_mode, results_dir=None, tag=None):
    report_dir = resolve_results_dir(results_dir, suite)
    report_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    suffix = f'_{tag}' if tag else ''
    return report_dir / f'perf_{platform_name()}_{recv_mode}_{timestamp}{suffix}.txt'


def render_effective_options(options):
    lines = ['## Effective Options (start)']
    for key, value in options.items():
        lines.append(f'- {key}: {value}')
    return "\n".join(lines)


def parse_result_lines(output):
    rows = []
    for line in output.splitlines():
        if not line.startswith('RESULT,'):
            continue
        parts = line.split(',')
        if len(parts) != 7:
            continue
        _, lib, pattern, transport, size, metric, value = parts
        rows.append(
            {
                'lib': lib,
                'pattern': pattern,
                'transport': transport,
                'size': size,
                'metric': metric,
                'value': value,
            }
        )
    return rows


def render_markdown_summary(rows):
    if not rows:
        return ''
    by_key = {}
    for row in rows:
        key = (row['pattern'], row['transport'], row['size'])
        metrics = by_key.setdefault(key, {})
        metrics[row['metric']] = row['value']
    lines = [
        '| Pattern | Transport | Size | Throughput | Bandwidth | Latency | Latency P95 | Latency P99 |',
        '|---|---|---:|---:|---:|---:|---:|---:|',
    ]
    for key in sorted(by_key):
        pattern, transport, size = key
        metrics = by_key[key]
        lines.append(
            '| {pattern} | {transport} | {size} | {throughput} | {bandwidth} | {latency} | {latency_p95} | {latency_p99} |'.format(
                pattern=pattern,
                transport=transport,
                size=size,
                throughput=metrics.get('throughput', 'NA'),
                bandwidth=metrics.get('bandwidth', 'NA'),
                latency=metrics.get('latency', 'NA'),
                latency_p95=metrics.get('latency_p95', 'NA'),
                latency_p99=metrics.get('latency_p99', 'NA'),
            )
        )
    return "\n".join(lines)
