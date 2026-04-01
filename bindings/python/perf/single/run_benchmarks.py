import argparse
import contextlib
import importlib.util
import io
import os
import sys
from pathlib import Path

from perf_common import (
    build_report_path,
    parse_result_lines,
    render_effective_options,
    render_markdown_summary,
)


ROOT = Path(__file__).resolve().parent
DEFAULT_PATTERNS = ("PAIR", "PUBSUB", "DEALER_ROUTER", "SPOT")
DEFAULT_MSG_SIZES = ("64", "256", "1024", "65536", "131072", "262144")
SUPPORTED_TRANSPORTS = {
    "PAIR": ("tcp",),
    "PUBSUB": ("inproc",),
    "DEALER_ROUTER": ("inproc",),
    "SPOT": ("inproc",),
}


def parse_args(argv):
    parser = argparse.ArgumentParser(prog="run_benchmarks.sh")
    parser.add_argument("--pythonpath", required=True)
    parser.add_argument("--pattern", default="ALL")
    parser.add_argument("--recv", default="callback")
    parser.add_argument("--duration", default="5")
    parser.add_argument("--warmup", default="2")
    parser.add_argument("--msg-sizes", default=",".join(DEFAULT_MSG_SIZES))
    parser.add_argument("--transports", default="tcp,inproc")
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
    unknown = [pattern for pattern in patterns if pattern not in SUPPORTED_TRANSPORTS]
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
        matched = [transport for transport in transports if transport in SUPPORTED_TRANSPORTS[pattern]]
        if not matched:
            continue
        for transport in matched:
            for msg_size in msg_sizes:
                configs.append((pattern, transport, msg_size))
    if not configs:
        raise SystemExit("no runnable pattern/transport combinations selected")
    return configs


def _run_pattern(args, env, pattern, msg_size):
    entry = ROOT / f"perf_{pattern.lower()}.py"
    if not entry.exists():
        raise SystemExit(f"unsupported pattern: {pattern}")
    old_pythonpath = os.environ.get("PYTHONPATH")
    inserted_path = env["PYTHONPATH"]
    inserted = False
    os.environ["PYTHONPATH"] = env["PYTHONPATH"]
    if env.get("ZLINK_LIBRARY_PATH"):
        os.environ["ZLINK_LIBRARY_PATH"] = env["ZLINK_LIBRARY_PATH"]
    if inserted_path not in sys.path:
        sys.path.insert(0, inserted_path)
        inserted = True
    capture = io.StringIO()
    module_name = f"_python_perf_single_{pattern.lower()}"
    spec = importlib.util.spec_from_file_location(module_name, entry)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    try:
        with contextlib.redirect_stdout(capture):
            module.main(
                [
                    "--duration",
                    args.duration,
                    "--warmup",
                    args.warmup,
                    "--msg-size",
                    msg_size,
                    "--recv",
                    args.recv,
                ]
            )
    finally:
        if inserted:
            try:
                sys.path.remove(inserted_path)
            except ValueError:
                pass
        if old_pythonpath is None:
            os.environ.pop("PYTHONPATH", None)
        else:
            os.environ["PYTHONPATH"] = old_pythonpath
    return capture.getvalue().strip()


def _build_options(args, patterns, transports, msg_sizes):
    return {
        "runs": args.runs,
        "recv_mode": args.recv,
        "duration_seconds": args.duration,
        "warmup_seconds": args.warmup,
        "patterns": ",".join(patterns),
        "transports": ",".join(transports),
        "msg_sizes": ",".join(msg_sizes),
    }


def main(argv=None):
    args = parse_args(argv or sys.argv[1:])
    if args.recv != "callback":
        raise SystemExit("--recv must be callback for python single perf")
    patterns = _parse_patterns(args.pattern)
    transports = _parse_transports(args.transports)
    msg_sizes = _parse_msg_sizes(args)
    runs = int(args.runs)
    if runs <= 0:
        raise SystemExit("--runs must be > 0")
    configs = _selected_configs(patterns, transports, msg_sizes)

    env = dict(os.environ)
    env["PYTHONPATH"] = args.pythonpath

    options = _build_options(args, patterns, transports, msg_sizes)
    sections = [render_effective_options(options), ""]
    emitted_chunks = []
    for _ in range(runs):
        for pattern, _transport, msg_size in configs:
            output = _run_pattern(args, env, pattern, msg_size)
            if output:
                emitted_chunks.append(output)
                sections.append(output)
    rows = parse_result_lines("\n".join(emitted_chunks))
    sections.extend(["", render_markdown_summary(rows), "", "## Effective Options (result)"])
    sections.append(f"- recv_mode: {args.recv}")
    sections.append(f"- result_lines: {len(rows)}")
    sections.append(
        f"- status: {'complete' if len(rows) == len(configs) * runs * 5 else 'partial'}"
    )
    final_output = "\n".join(section for section in sections if section is not None).rstrip() + "\n"
    print(final_output, end="")

    report_path = build_report_path(
        suite="single",
        recv_mode=args.recv,
        results_dir=args.results_dir or None,
        tag=args.results_tag or None,
    )
    report_path.write_text(final_output, encoding="utf-8")


if __name__ == "__main__":
    main()
