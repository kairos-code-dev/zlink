import argparse
import threading
import time

import zlink

from perf_multi_common import (
    latency_us_from_message,
    new_payload,
    parse_client_args,
    print_result_lines,
    result_metrics,
    stamp_payload,
)


def main(argv=None):
    args = parse_client_args(
        argv or [], pattern="dealer_router", allowed_recv={"recv"}
    )
    payload = new_payload(args.msg_size)
    count = 0
    latencies = []
    lock = threading.Lock()
    stop = threading.Event()

    with zlink.Context() as ctx:
        sockets = [zlink.DealerSocket(ctx) for _ in range(args.clients)]
        try:
            for index, sock in enumerate(sockets):
                sock.set_routing_id(f"CLIENT-{index}".encode("ascii"))
                sock.connect(args.endpoint)

            def worker(sock):
                nonlocal count
                warmup_deadline = time.perf_counter() + args.warmup
                while time.perf_counter() < warmup_deadline:
                    sock.send(stamp_payload(payload))
                    with sock.recv():
                        pass

                started = time.perf_counter()
                deadline = started + args.duration
                while time.perf_counter() < deadline and not stop.is_set():
                    sock.send(stamp_payload(payload))
                    with sock.recv() as received:
                        sample = latency_us_from_message(received.to_bytes_list()[0])
                    with lock:
                        count += 1
                        latencies.append(sample)

            threads = [threading.Thread(target=worker, args=(sock,)) for sock in sockets]
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
            print_result_lines("MULTI_DEALER_ROUTER", "tcp", args.msg_size, metrics)
        finally:
            stop.set()
            for sock in sockets:
                try:
                    sock.close()
                except Exception:
                    pass


if __name__ == "__main__":
    import sys

    main(sys.argv[1:])
