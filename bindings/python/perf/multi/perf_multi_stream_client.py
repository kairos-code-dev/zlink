import socket
import sys
import threading
import time
from urllib.parse import urlparse

from perf_multi_common import (
    encode_len32be,
    latency_us_from_message,
    new_payload,
    parse_client_args,
    print_result_lines,
    recv_len32be,
    result_metrics,
    stamp_payload,
)


def _parse_tcp(endpoint):
    parsed = urlparse(endpoint)
    return parsed.hostname, parsed.port


def main(argv=None):
    args = parse_client_args(
        argv or sys.argv[1:], pattern="stream", allowed_recv={"recv", "callback"}
    )
    payload = new_payload(args.msg_size)
    count = 0
    latencies = []
    lock = threading.Lock()
    host, port = _parse_tcp(args.endpoint)

    def worker():
        nonlocal count
        with socket.create_connection((host, port), timeout=5.0) as client:
            warmup_deadline = time.perf_counter() + args.warmup
            while time.perf_counter() < warmup_deadline:
                client.sendall(encode_len32be(stamp_payload(payload)))
                recv_len32be(client)

            deadline = time.perf_counter() + args.duration
            while time.perf_counter() < deadline:
                client.sendall(encode_len32be(stamp_payload(payload)))
                reply = recv_len32be(client)
                sample = latency_us_from_message(reply)
                with lock:
                    count += 1
                    latencies.append(sample)

    threads = [threading.Thread(target=worker) for _ in range(args.clients)]
    started = time.perf_counter()
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    elapsed = time.perf_counter() - started
    metrics = result_metrics(
        count=count,
        msg_size=args.msg_size,
        elapsed_s=max(args.duration, elapsed),
        latencies_us=latencies,
    )
    print_result_lines("MULTI_STREAM", "tcp", args.msg_size, metrics)


if __name__ == "__main__":
    main()
