import argparse
import sys


METRIC_ORDER = {
    "bandwidth": 0,
    "latency": 1,
    "latency_p95": 2,
    "latency_p99": 3,
    "throughput": 4,
}

SINGLE_TABLE_HEADER_LINES = (
    "| Size     |       Throughput |    Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |",
    "|----------|------------------|--------------|--------------|--------------|--------------|",
)


def parse_csv(value):
    return [item.strip() for item in (value or "").split(",") if item.strip()]


def single_auto_hwm_detail_lines(patterns, msg_sizes):
    yield "## Auto-HWM Detail"
    for pattern in patterns:
        yield f"- pattern: {pattern}"
        yield (
            "| Size(B) | Component | Owner | Socket | Type | Role | SNDHWM | RCVHWM | "
            "SNDBUF(KB) | RCVBUF(KB) | MsgUnit(B) | Slots |"
        )
        yield (
            "|---------|-----------|-------|--------|------|------|--------|--------|"
            "-----------|-----------|------------|-------|"
        )
        for msg_size in msg_sizes:
            yield (
                f"| {msg_size} | unavailable | binding | n/a | n/a | n/a | "
                "n/a | n/a | n/a | n/a | n/a | n/a |"
            )


def multi_auto_hwm_lines(pattern, msg_sizes):
    spot_patterns = {
        "SPOT",
        "SPOT_REQREP",
        "SPOT_SENDSEND",
        "MULTI_SPOT",
        "MULTI_SPOT_REQREP",
        "MULTI_SPOT_SENDSEND",
    }
    if pattern in spot_patterns:
        yield "    Auto-HWM spotnode:"
        for msg_size in msg_sizes:
            msg_unit = max(4096, int(msg_size))
            yield f"      - Size(B)={msg_size}, MsgUnit(B)={msg_unit}"
            yield "      | Socket      | Type | Role | SNDHWM | RCVHWM | SNDBUF | RCVBUF |"
            yield "      |-------------|------|------|--------|--------|--------|--------|"
            yield "      | unavailable | n/a  | n/a  | n/a    | n/a    | n/a    | n/a    |"
        return
    yield "    Auto-HWM detail:"
    yield (
        "      | Size(B) | Component   | Type | UnitBudget(KB) | MsgUnit(B) | "
        "SNDHWM | RCVHWM | SNDBUF(KB) | RCVBUF(KB) |"
    )
    yield (
        "      |---------|-------------|------|----------------|------------|--------|"
        "--------|------------|------------|"
    )
    for msg_size in msg_sizes:
        yield (
            f"      | {msg_size:<7} | unavailable | n/a  | n/a            | "
            f"{msg_size:<10} | n/a    | n/a    | n/a        | n/a        |"
        )


def sort_result_data_lines(lines):
    def sort_key(line):
        parts = line.strip().split(",")
        if len(parts) >= 7 and parts[0] == "RESULT":
            try:
                size = int(parts[4])
            except ValueError:
                size = -1
            return (parts[2], parts[3], size, METRIC_ORDER.get(parts[5], 99), line)
        return (
            parts[2] if len(parts) > 2 else "",
            parts[3] if len(parts) > 3 else "",
            -1,
            99,
            line,
        )

    selected = [
        line.rstrip("\n")
        for line in lines
        if line.startswith(("RESULT,", "UNSUPPORTED,", "SKIP,", "FAIL,"))
    ]
    return sorted(selected, key=sort_key)


def main(argv=None):
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    single = subparsers.add_parser("single-auto-hwm")
    single.add_argument("--patterns", required=True)
    single.add_argument("--msg-sizes", required=True)

    multi = subparsers.add_parser("multi-auto-hwm")
    multi.add_argument("--pattern", required=True)
    multi.add_argument("--msg-sizes", required=True)

    sort_data = subparsers.add_parser("sort-result-data")
    sort_data.add_argument("path")

    args = parser.parse_args(argv)
    if args.command == "single-auto-hwm":
        for line in single_auto_hwm_detail_lines(
            parse_csv(args.patterns), parse_csv(args.msg_sizes)
        ):
            print(line)
        return
    if args.command == "multi-auto-hwm":
        for line in multi_auto_hwm_lines(args.pattern, parse_csv(args.msg_sizes)):
            print(line)
        return
    with open(args.path, "r", encoding="utf-8", errors="replace") as fh:
        for line in sort_result_data_lines(fh):
            print(line)


if __name__ == "__main__":
    main(sys.argv[1:])
