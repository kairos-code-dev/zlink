import argparse
import sys
from pathlib import Path

PERF_DIR = Path(__file__).resolve().parent.parent
perf_dir_text = str(PERF_DIR)
if perf_dir_text not in sys.path:
    sys.path.insert(0, perf_dir_text)

from perf_metrics import (
    benchmark_run_id,
    build_report_path,
    latency_ns_from_message,
    new_payload,
    parse_result_lines,
    print_result_lines,
    is_active_message,
    payload_phase,
    render_effective_options,
    render_markdown_summary,
    result_metrics,
    safe_poll,
    stamp_payload,
    tcp_endpoint,
    unique_endpoint,
    wait_monitor_event,
    _require_zlink,
)


def parse_single_args(argv, *, pattern):
    parser = argparse.ArgumentParser(prog=f"perf_{pattern.lower()}.py")
    parser.add_argument("--duration", type=float, default=2.0)
    parser.add_argument("--warmup", type=float, default=1.0)
    parser.add_argument("--msg-size", type=int, default=256)
    args = parser.parse_args(argv)
    if args.duration <= 0:
        raise SystemExit("--duration must be > 0")
    if args.warmup < 0:
        raise SystemExit("--warmup must be >= 0")
    if args.msg_size < 16:
        raise SystemExit("--msg-size must be >= 16")
    return args


def recv_nonblocking(sock, *, method="recv"):
    zlink_mod = _require_zlink()
    recv_method = getattr(sock, method)
    try:
        return recv_method(flags=zlink_mod.RecvFlags.DONT_WAIT)
    except zlink_mod.RecvError as exc:
        if exc.result == zlink_mod.RecvResult.NO_DATA:
            return None
        raise
