import sys
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
        argv or sys.argv[1:], pattern="dealer_dealer", allowed_recv={"recv"}
    )
    endpoints = args.endpoint.split(";")
    if len(endpoints) != args.clients:
        raise SystemExit("endpoint count must match --clients")
    payload = new_payload(args.msg_size)
    count = 0
    latencies = []
    lock = threading.Lock()

    with zlink.Context() as ctx:
        sockets = [zlink.DealerSocket(ctx) for _ in range(args.clients)]
        try:
            for sock, endpoint in zip(sockets, endpoints):
                sock.connect(endpoint)

            def worker(sock):
                nonlocal count
                warmup_deadline = time.perf_counter() + args.warmup
                while time.perf_counter() < warmup_deadline:
                    sock.send(stamp_payload(payload))
                    with sock.recv():
                        pass

                deadline = time.perf_counter() + args.duration
                while time.perf_counter() < deadline:
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
            print_result_lines("MULTI_DEALER_DEALER", "tcp", args.msg_size, metrics)
        finally:
            for sock in sockets:
                try:
                    sock.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
