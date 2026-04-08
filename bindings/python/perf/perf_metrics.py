import importlib
import math
import os
import queue
import socket
import statistics
import struct
import sys
import threading
import time
import uuid
from datetime import datetime
from pathlib import Path


DEFAULT_READY_TIMEOUT_MS = 5000
DEFAULT_QUEUE_CAPACITY = 65536
_zlink = None


class CallbackMetrics:
    def __init__(self, *, capacity=DEFAULT_QUEUE_CAPACITY, phase_filter=None):
        self._queue = queue.Queue(maxsize=capacity)
        self._active = threading.Event()
        self._stop = threading.Event()
        self._ready = threading.Event()
        self._count = 0
        self._latencies = []
        self._error = None
        self._phase_filter = phase_filter
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        while not self._stop.is_set() or not self._queue.empty():
            try:
                sample = self._queue.get(timeout=0.1)
            except queue.Empty:
                continue
            self._count += 1
            self._latencies.append(sample)
            self._queue.task_done()

    @property
    def count(self):
        return self._count

    @property
    def latencies(self):
        return list(self._latencies)

    def on_payload(self, payload, phase=None):
        self._ready.set()
        if self._phase_filter is not None and phase != self._phase_filter:
            return
        if not self._active.is_set() or self._error is not None:
            return
        try:
            self._queue.put_nowait(latency_us_from_message(payload))
        except queue.Full:
            self._error = RuntimeError("callback metric queue overflow")

    def activate(self):
        self._active.set()

    def deactivate(self):
        self._active.clear()

    def wait_ready(self, timeout_s):
        return self._ready.wait(timeout_s)

    def finish(self):
        self._stop.set()
        self._queue.join()
        self._thread.join(timeout=1.0)
        if self._error is not None:
            raise self._error
        return self.count, self.latencies


def latency_us_from_message(data):
    sent_ns = struct.unpack_from("!Q", data, 0)[0]
    return (time.perf_counter_ns() - sent_ns) / 1000.0


def payload_phase(data):
    if len(data) < 12:
        return 0
    return struct.unpack_from("!I", data, 8)[0]


def stamp_payload(payload, phase=0):
    struct.pack_into("!Q", payload, 0, time.perf_counter_ns())
    if len(payload) >= 12:
        struct.pack_into("!I", payload, 8, int(phase))
    return payload


def new_payload(size):
    return bytearray(size)


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


def unique_endpoint(prefix):
    return f"inproc://py-perf-{prefix}-{os.getpid()}-{uuid.uuid4().hex}"


def tcp_endpoint(prefix="perf"):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return f"tcp://127.0.0.1:{port}"


def wait_socket_event(socket_obj, event_mask, *, timeout_ms=DEFAULT_READY_TIMEOUT_MS):
    zlink_mod = _require_zlink()
    deadline = time.perf_counter() + (timeout_ms / 1000.0)
    with socket_obj.monitor_open(event_mask) as monitor:
        with zlink_mod.Poller() as poller:
            poller.add_socket(monitor, zlink_mod.PollEvent.POLLIN)
            while True:
                remaining_ms = max(0, int((deadline - time.perf_counter()) * 1000))
                if remaining_ms == 0:
                    raise RuntimeError(
                        f"timed out waiting for socket monitor event {int(event_mask)}"
                    )
                try:
                    events = poller.poll(remaining_ms)
                except zlink_mod.ZlinkError as exc:
                    if exc.errno == 11:
                        events = []
                    else:
                        raise
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


def wait_send_ready(socket_obj, *, timeout_s=DEFAULT_READY_TIMEOUT_MS / 1000.0):
    ready = threading.Event()

    def _mark_ready(_):
        ready.set()

    socket_obj.on_send_ready(_mark_ready)
    if not ready.wait(timeout_s):
        raise RuntimeError("timed out waiting for send-ready")
    return ready


def _require_zlink():
    global _zlink
    if _zlink is None:
        _zlink = importlib.import_module("zlink")
    return _zlink


def safe_poll(poller, timeout_ms):
    zlink_mod = _require_zlink()
    try:
        return poller.poll(timeout_ms)
    except zlink_mod.ZlinkError as exc:
        if exc.errno == 11:
            return []
        raise


def _wait_for_poll_event(socket_obj, events, *, timeout_ms):
    zlink_mod = _require_zlink()
    with zlink_mod.Poller() as poller:
        poller.add_socket(socket_obj, events)
        ready = safe_poll(poller, timeout_ms)
    if not ready:
        raise RuntimeError(f"timed out waiting for poll event {int(events)}")


def wait_pubsub_ready(publisher, subscriber, *, timeout_ms=DEFAULT_READY_TIMEOUT_MS):
    _wait_for_poll_event(publisher, _require_zlink().PollEvent.POLLOUT, timeout_ms=timeout_ms)


def wait_connected_pair(left, right, *, timeout_ms=DEFAULT_READY_TIMEOUT_MS):
    _wait_for_poll_event(right, _require_zlink().PollEvent.POLLOUT, timeout_ms=timeout_ms)


# ---------------------------------------------------------------------------
# Reporting helpers (shared by single & multi runners)
# ---------------------------------------------------------------------------


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
        return Path(__file__).resolve().parent / 'results' / suite / 'report'
    return Path(base_dir)


def build_report_path(*, suite, recv_mode, results_dir=None, tag=None):
    report_dir = resolve_results_dir(results_dir, suite)
    report_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    suffix = f'_{tag}' if tag else ''
    return report_dir / f'perf_{platform_name()}_{recv_mode}_{timestamp}{suffix}.txt'


def ensure_report_path(suite, recv_mode, tag=None):
    report_dir = Path(__file__).resolve().parent / 'results' / suite / 'report'
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
