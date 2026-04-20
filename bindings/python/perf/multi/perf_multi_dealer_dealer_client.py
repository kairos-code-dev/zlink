import sys
import time
from contextlib import ExitStack

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    latency_ns_from_message,
    is_active_message,
    parse_client_args,
    print_result_lines,
    result_metrics,
    wait_monitor_event,
)


def main(argv=None):
    args = parse_client_args(argv or sys.argv[1:], pattern="dealer_dealer")
    endpoints = args.endpoint.split(";")
    if len(endpoints) != args.clients:
        raise SystemExit("endpoint count must match --clients")
    latencies = []
    count = 0

    with zlink.Context() as ctx:
        sockets = [zlink.DealerSocket(ctx) for _ in range(args.clients)]
        try:
            with ExitStack() as stack:
                monitors = []
                for sock, endpoint in zip(sockets, endpoints):
                    monitor = stack.enter_context(
                        sock.monitor_open(zlink.MonitorEventMask.CONNECTION_READY)
                    )
                    apply_multi_socket_options(sock)
                    sock.connect(endpoint)
                    monitors.append(monitor)
                for monitor in monitors:
                    wait_monitor_event(monitor, zlink.MonitorEventMask.CONNECTION_READY)
                print(f"CLIENT_READY,{args.msg_size}", flush=True)
                command = sys.stdin.readline().strip()
                if command != f"START,{args.msg_size}":
                    raise SystemExit(f"unexpected command: {command}")

                started = time.perf_counter()
                deadline = started + args.duration
                while time.perf_counter() < deadline:
                    for current_sock in sockets:
                        try:
                            with current_sock.recv() as received:
                                data = received.to_bytes_list()[0]
                        except zlink.RecvError as exc:
                            if exc.result == zlink.RecvResult.NO_DATA:
                                continue
                            raise
                        if not is_active_message(
                            data,
                            expected_msg_size=args.msg_size,
                            run_id=None,
                        ):
                            continue
                        latencies.append(latency_ns_from_message(data))
                        count += 1
                metrics = result_metrics(
                    count=count,
                    msg_size=args.msg_size,
                    elapsed_s=max(args.duration, time.perf_counter() - started),
                    latencies_ns=latencies,
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
