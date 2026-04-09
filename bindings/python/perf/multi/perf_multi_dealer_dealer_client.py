import sys
import threading
import time
from contextlib import ExitStack

import zlink

from perf_multi_common import (
    latency_us_from_message,
    is_active_message,
    new_payload,
    parse_client_args,
    print_result_lines,
    result_metrics,
    stamp_payload,
    wait_monitor_event,
)


def main(argv=None):
    args = parse_client_args(argv or sys.argv[1:], pattern="dealer_dealer")
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
            with ExitStack() as stack:
                monitors = []
                for sock, endpoint in zip(sockets, endpoints):
                    monitor = stack.enter_context(
                        sock.monitor_open(zlink.MonitorEvent.CONNECTION_READY)
                    )
                    sock.connect(endpoint)
                    monitors.append(monitor)
                for monitor in monitors:
                    wait_monitor_event(monitor, zlink.MonitorEvent.CONNECTION_READY)

                def worker(sock):
                    nonlocal count
                    deadline = time.perf_counter() + args.duration
                    while time.perf_counter() < deadline:
                        sock.send(stamp_payload(payload, phase=1))
                        with sock.recv() as received:
                            data = received.to_bytes_list()[0]
                            if not is_active_message(
                                data,
                                expected_msg_size=args.msg_size,
                                run_id=None,
                            ):
                                continue
                            sample = latency_us_from_message(data)
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
