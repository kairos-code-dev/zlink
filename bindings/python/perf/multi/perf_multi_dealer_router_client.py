import sys
import threading
import time
from contextlib import ExitStack

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    benchmark_run_id,
    latency_ns_from_message,
    is_active_message,
    new_payload,
    parse_client_args,
    print_result_lines,
    resolve_multi_connect_ready_timeout_ms,
    result_metrics,
    stamp_payload,
    wait_monitor_event,
)


def main(argv=None):
    args = parse_client_args(argv or sys.argv[1:], pattern="dealer_router")
    run_id = benchmark_run_id()
    payload = new_payload(args.msg_size)
    results = [None] * args.clients

    with zlink.Context() as ctx:
        sockets = [zlink.DealerSocket(ctx) for _ in range(args.clients)]
        try:
            with ExitStack() as stack:
                monitors = []
                for index, sock in enumerate(sockets):
                    sock.set_routing_id(f"CLIENT-{index}".encode("ascii"))
                    monitor = stack.enter_context(
                        sock.monitor_open(zlink.MonitorEventMask.CONNECTION_READY)
                    )
                    apply_multi_socket_options(sock)
                    sock.connect(args.endpoint)
                    monitors.append(monitor)
                for monitor in monitors:
                    wait_monitor_event(
                        monitor,
                        zlink.MonitorEventMask.CONNECTION_READY,
                        timeout_ms=resolve_multi_connect_ready_timeout_ms(),
                    )

                def worker(sock, slot):
                    local_latencies = []
                    deadline = time.perf_counter() + args.duration
                    while time.perf_counter() < deadline:
                        sock.send(stamp_payload(payload, phase=1))
                        with sock.recv() as received:
                            data = received.to_bytes_list()[0]
                            if not is_active_message(
                                data,
                                expected_msg_size=args.msg_size,
                                run_id=run_id,
                            ):
                                continue
                            local_latencies.append(latency_ns_from_message(data))
                    results[slot] = local_latencies

                threads = [
                    threading.Thread(target=worker, args=(sock, index))
                    for index, sock in enumerate(sockets)
                ]
                started = time.perf_counter()
                for thread in threads:
                    thread.start()
                for thread in threads:
                    thread.join()
                latencies = []
                for bucket in results:
                    latencies.extend(bucket or [])
                if not latencies:
                    raise RuntimeError(
                        "multi dealer-router benchmark did not receive any active reply"
                    )
                metrics = result_metrics(
                    count=len(latencies),
                    msg_size=args.msg_size,
                    elapsed_s=max(args.duration, time.perf_counter() - started),
                    latencies_ns=latencies,
                )
                print_result_lines(
                    "MULTI_DEALER_ROUTER", args.transport, args.msg_size, metrics
                )
        finally:
            for sock in sockets:
                try:
                    sock.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
