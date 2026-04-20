import argparse
import os
import subprocess
import sys
from pathlib import Path

from perf_common import (
    build_report_path,
    parse_result_lines,
    render_effective_options,
    render_markdown_summary,
)


ROOT = Path(__file__).resolve().parent
DEFAULT_PATTERNS = (
    "PAIR",
    "PUBSUB",
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "SPOT",
)
DEFAULT_MSG_SIZES = ("64", "256", "1024", "65536", "131072", "262144")
RAW_TRANSPORTS = ("tcp", "tls", "ws", "wss", "inproc", "ipc")
SPOT_TRANSPORTS = ("tcp", "tls", "ws", "wss")
POLICY_TRANSPORTS = {
    "PAIR": RAW_TRANSPORTS,
    "PUBSUB": RAW_TRANSPORTS,
    "DEALER_DEALER": RAW_TRANSPORTS,
    "DEALER_ROUTER": RAW_TRANSPORTS,
    "ROUTER_ROUTER": RAW_TRANSPORTS,
    "SPOT": SPOT_TRANSPORTS,
}
RUNNABLE_TRANSPORTS = {
    "PAIR": ("tcp", "inproc", "ipc"),
    "PUBSUB": ("tcp", "inproc", "ipc"),
    "DEALER_DEALER": ("tcp", "inproc", "ipc"),
    "DEALER_ROUTER": ("tcp",),
    "ROUTER_ROUTER": ("tcp",),
    "SPOT": (),
}


def parse_args(argv):
    parser = argparse.ArgumentParser(prog="run_benchmarks.sh")
    parser.add_argument("--pythonpath", required=True)
    parser.add_argument("--pattern", default="ALL")
    parser.add_argument("--duration", default="5")
    parser.add_argument("--warmup", default="0", help=argparse.SUPPRESS)
    parser.add_argument("--msg-sizes", default=",".join(DEFAULT_MSG_SIZES))
    parser.add_argument("--transports", default=",".join(RAW_TRANSPORTS))
    parser.add_argument("--runs", default="1")
    parser.add_argument("--results-dir", default="")
    parser.add_argument("--results-tag", default="")
    parser.add_argument("--msg-size", dest="msg_size_compat", default=None, help=argparse.SUPPRESS)
    return parser.parse_args(argv)


def _parse_csv(value):
    return [item.strip() for item in value.split(",") if item.strip()]


def _parse_patterns(value):
    text = value.strip().upper()
    if text == "ALL":
        return list(DEFAULT_PATTERNS)
    patterns = [item.strip().upper() for item in text.split(",") if item.strip()]
    if not patterns:
        raise SystemExit("unsupported pattern: ")
    unknown = [pattern for pattern in patterns if pattern not in POLICY_TRANSPORTS]
    if unknown:
        raise SystemExit(f"unsupported pattern: {unknown[0]}")
    return patterns


def _parse_msg_sizes(args):
    if args.msg_size_compat is not None:
        return [args.msg_size_compat]
    sizes = _parse_csv(args.msg_sizes)
    if not sizes:
        raise SystemExit("--msg-sizes must not be empty")
    return sizes


def _parse_transports(value):
    transports = [item.lower() for item in _parse_csv(value)]
    if not transports:
        raise SystemExit("--transports must not be empty")
    return transports


def _selected_configs(patterns, transports, msg_sizes):
    configs = []
    for pattern in patterns:
        matched = [transport for transport in transports if transport in POLICY_TRANSPORTS[pattern]]
        if not matched:
            continue
        for transport in matched:
            for msg_size in msg_sizes:
                configs.append((pattern, transport, msg_size))
    if not configs:
        raise SystemExit("no runnable pattern/transport combinations selected")
    return configs


def _is_unsupported_output(text):
    lowered = text.lower()
    markers = (
        "protocol not supported",
        "unsupported transport",
        "operation not permitted",
        "permission denied",
        "listen eperm",
        "permissionerror: [errno 1]",
        "binderror(code=0, internal_errno=1)",
        "binderror(code=502, internal_errno=98)",
        "internal_errno=98",
        "unsupported,current,",
    )
    return any(marker in lowered for marker in markers)


def _parse_status_lines(output):
    rows = []
    for line in output.splitlines():
        if line.startswith("SKIP,") or line.startswith("UNSUPPORTED,"):
            rows.append(line.strip())
    return rows


def _run_pattern(args, env, pattern, transport, msg_size):
    if transport not in RUNNABLE_TRANSPORTS.get(pattern, ()):
        return f"UNSUPPORTED,current,{pattern},{transport}"
    entry = ROOT / f"perf_{pattern.lower()}.py"
    if not entry.exists():
        raise SystemExit(f"unsupported pattern: {pattern}")
    cmd = [
        sys.executable,
        str(entry),
        "--transport",
        transport,
        "--duration",
        args.duration,
        "--msg-size",
        msg_size,
    ]
    result = subprocess.run(
        cmd,
        cwd=str(ROOT.parent.parent),
        env=env,
        capture_output=True,
        text=True,
        timeout=90,
    )
    chunks = []
    if result.stdout:
        chunks.append(result.stdout.strip())
    if result.stderr:
        chunks.append(result.stderr.strip())
    output = "\n".join(chunk for chunk in chunks if chunk)
    if result.returncode == 0:
        return output
    if _is_unsupported_output(output):
        return f"UNSUPPORTED,current,{pattern},{transport}"
    raise SystemExit(output or f"{pattern} {transport} failed with exit code {result.returncode}")


def _build_options(args, patterns, transports, msg_sizes):
    return {
        "lang": "python",
        "suite": "single",
        "runs": args.runs,
        "patterns": ",".join(patterns),
        "transports": ",".join(transports),
        "msg_sizes": ",".join(msg_sizes),
        "duration_seconds": args.duration,
    }


def main(argv=None):
    args = parse_args(argv or sys.argv[1:])
    patterns = _parse_patterns(args.pattern)
    transports = _parse_transports(args.transports)
    msg_sizes = _parse_msg_sizes(args)
    runs = int(args.runs)
    if runs <= 0:
        raise SystemExit("--runs must be > 0")
    configs = _selected_configs(patterns, transports, msg_sizes)

    env = dict(os.environ)
    env["PYTHONPATH"] = str(Path(args.pythonpath).resolve())

    options = _build_options(args, patterns, transports, msg_sizes)
    sections = [render_effective_options(options), ""]
    emitted_chunks = []
    status_lines = []
    fail_count = 0
    case_ordinal = 1
    for _ in range(runs):
        for pattern, transport, msg_size in configs:
            case_env = dict(env)
            case_env["PERF_RUN_ID"] = str(case_ordinal)
            case_ordinal += 1
            try:
                output = _run_pattern(args, case_env, pattern, transport, msg_size)
            except SystemExit as exc:
                fail_count += 1
                output = str(exc).strip()
            if output:
                emitted_chunks.append(output)
                status_lines.extend(_parse_status_lines(output))
                sections.append(output)
    rows = parse_result_lines("\n".join(emitted_chunks))
    skipped_cases = 0
    unsupported_cases = 0
    for line in status_lines:
        if line.startswith("SKIP,"):
            skipped_cases += 1
        elif line.startswith("UNSUPPORTED,"):
            unsupported_cases += 1
    expected_cases = max(0, len(configs) * runs - skipped_cases - unsupported_cases - fail_count)
    expected_result_lines = expected_cases * 5
    sections.extend(["", render_markdown_summary(rows), "", "## Effective Options (result)"])
    status = "complete" if fail_count == 0 and len(rows) == expected_result_lines else "partial"
    sections.append(f"- status: {status}")
    sections.append(f"- expected_result_lines: {expected_result_lines}")
    sections.append(f"- actual_result_lines: {len(rows)}")
    sections.append(f"- skip_cases: {skipped_cases}")
    sections.append(f"- unsupported_cases: {unsupported_cases}")
    sections.append(f"- fail: {fail_count}")
    final_output = "\n".join(section for section in sections if section is not None).rstrip() + "\n"
    print(final_output, end="")

    report_path = build_report_path(
        lang="python",
        suite="single",
        results_dir=args.results_dir or None,
        tag=args.results_tag or None,
    )
    report_path.write_text(final_output, encoding="utf-8")
    if fail_count > 0:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
