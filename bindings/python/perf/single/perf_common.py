import argparse
import sys
from pathlib import Path

PERF_DIR = Path(__file__).resolve().parent.parent
perf_dir_text = str(PERF_DIR)
if perf_dir_text not in sys.path:
    sys.path.insert(0, perf_dir_text)

from perf_metrics import (
    CallbackMetrics,
    build_report_path,
    latency_us_from_message,
    new_payload,
    parse_result_lines,
    payload_phase,
    platform_name,
    print_result_lines,
    render_effective_options,
    render_markdown_summary,
    resolve_results_dir,
    result_metrics,
    stamp_payload,
    tcp_endpoint,
    unique_endpoint,
    wait_send_ready,
    write_report,
)


def parse_single_args(argv, *, pattern):
    parser = argparse.ArgumentParser(prog=f"perf_{pattern.lower()}.py")
    parser.add_argument("--duration", type=float, default=2.0)
    parser.add_argument("--warmup", type=float, default=1.0)
    parser.add_argument("--msg-size", type=int, default=256)
    parser.add_argument("--recv", default="callback")
    args = parser.parse_args(argv)
    if args.recv != "callback":
        raise SystemExit("--recv must be callback for python single perf")
    if args.duration <= 0:
        raise SystemExit("--duration must be > 0")
    if args.warmup < 0:
        raise SystemExit("--warmup must be >= 0")
    if args.msg_size < 16:
        raise SystemExit("--msg-size must be >= 16")
    return args
