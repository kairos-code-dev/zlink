import argparse
import math
import socket
import statistics
import struct
import time


TOPIC = b"bench"


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
