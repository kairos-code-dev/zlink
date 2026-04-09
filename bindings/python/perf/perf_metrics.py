import importlib
import math
import os
import socket
import statistics
import struct
import sys
import time
import uuid
from datetime import datetime
from pathlib import Path


DEFAULT_READY_TIMEOUT_MS = 5000
HEADER_MAGIC = 0x5A4C4E4B
HEADER_FORMAT = "<IIBIQq"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
_zlink = None
_run_id = None
_seq = 0


def _current_run_id():
    global _run_id
    if _run_id is None:
        _run_id = uuid.uuid4().int & 0xFFFFFFFF
    return _run_id


def benchmark_run_id():
    return _current_run_id()


def _next_seq():
    global _seq
    seq = _seq
    _seq += 1
    return seq


def decode_header(data):
    if len(data) < HEADER_SIZE:
        return None
    magic, run_id, phase, msg_size, seq, sent_ts_us = struct.unpack_from(
        HEADER_FORMAT, data, 0
    )
    return {
        "magic": magic,
        "run_id": run_id,
        "phase": phase,
        "msg_size": msg_size,
        "seq": seq,
        "sent_ts_us": sent_ts_us,
    }


def latency_us_from_message(data):
    header = decode_header(data)
    if header is None or header["magic"] != HEADER_MAGIC:
        raise RuntimeError("invalid perf message header")
    now_us = time.time_ns() // 1000
    return float(now_us - header["sent_ts_us"])


def is_active_message(data, *, expected_msg_size=None, run_id=None):
    header = decode_header(data)
    if header is None:
        return False
    if header["magic"] != HEADER_MAGIC:
        return False
    if header["phase"] != 1:
        return False
    if expected_msg_size is not None and header["msg_size"] != expected_msg_size:
        return False
    if run_id is not None and header["run_id"] != run_id:
        return False
    return True


def payload_phase(data):
    header = decode_header(data)
    if header is None:
        return 0
    return header["phase"]


def stamp_payload(payload, phase=0, *, run_id=None, seq=None):
    header_run_id = _current_run_id() if run_id is None else (run_id & 0xFFFFFFFF)
    header_seq = _next_seq() if seq is None else seq
    struct.pack_into(
        HEADER_FORMAT,
        payload,
        0,
        HEADER_MAGIC,
        header_run_id,
        int(phase),
        len(payload),
        int(header_seq),
        int(time.time_ns() // 1000),
    )
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
    with socket_obj.monitor_open(event_mask) as monitor:
        return wait_monitor_event(monitor, event_mask, timeout_ms=timeout_ms)


def wait_monitor_event(monitor, event_mask, *, timeout_ms=DEFAULT_READY_TIMEOUT_MS):
    zlink_mod = _require_zlink()
    deadline = time.perf_counter() + (timeout_ms / 1000.0)
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
            if not (int(event.event) & int(event_mask)):
                continue
            return event


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


def build_report_path(*, lang, suite, results_dir=None, tag=None):
    report_dir = resolve_results_dir(results_dir, suite)
    report_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    suffix = f'_{tag}' if tag else ''
    return report_dir / f'perf_{lang}_{suite}_{platform_name()}_{timestamp}{suffix}.txt'


def ensure_report_path(lang, suite, tag=None):
    report_dir = Path(__file__).resolve().parent / 'results' / suite / 'report'
    report_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    suffix = f'_{tag}' if tag else ''
    return report_dir / f'perf_{lang}_{suite}_{platform_name()}_{timestamp}{suffix}.txt'


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
