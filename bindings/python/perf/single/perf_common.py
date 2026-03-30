import argparse
import math
import os
import socket
import statistics
import struct
import time
import uuid


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


def unique_endpoint(prefix):
    return f"inproc://py-perf-{prefix}-{os.getpid()}-{uuid.uuid4().hex}"


def tcp_endpoint(prefix):
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
        value = metrics[name]
        print(f"RESULT,current,{pattern},{transport},{msg_size},{name},{value:.2f}")
