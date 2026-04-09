import sys
import time
from contextlib import ExitStack

import zlink

from perf_multi_common import (
    TOPIC,
    latency_ns_from_message,
    is_active_message,
    parse_client_args,
    print_result_lines,
    result_metrics,
    safe_poll,
    wait_monitor_event,
)


def _drain_ready(poller, latencies, *, expected_msg_size, deadline=None):
    count = 0
    events = safe_poll(poller, 50)
    for event in events:
        sock = event["socket"]
        while True:
            if deadline is not None and time.perf_counter() >= deadline:
                return count
            received = sock.try_subscribe()
            if received is None:
                break
            with received:
                data = received.to_bytes_list()[0]
                if not is_active_message(
                    data,
                    expected_msg_size=expected_msg_size,
                    run_id=None,
                ):
                    continue
                latencies.append(latency_ns_from_message(data))
                count += 1
    return count


def main(argv=None):
    args = parse_client_args(
        argv or sys.argv[1:], pattern="pubsub"
    )
    latencies = []
    count = 0

    with zlink.Context() as ctx:
        sockets = [zlink.SubSocket(ctx) for _ in range(args.clients)]
        try:
            with ExitStack() as stack:
                monitors = []
                for sock in sockets:
                    sock.options.linger_ms = 0
                    monitor = stack.enter_context(
                        sock.monitor_open(zlink.MonitorEvent.CONNECTION_READY)
                    )
                    sock.connect(args.endpoint)
                    sock.set_subscription(TOPIC)
                    monitors.append(monitor)
                for monitor in monitors:
                    wait_monitor_event(monitor, zlink.MonitorEvent.CONNECTION_READY)

                with zlink.Poller() as poller:
                    for sock in sockets:
                        poller.add_socket(sock, zlink.PollEvent.POLLIN)

                    started = time.perf_counter()
                    deadline = started + args.duration
                    while time.perf_counter() < deadline:
                        count += _drain_ready(
                            poller,
                            latencies,
                            expected_msg_size=args.msg_size,
                            deadline=deadline,
                        )

                elapsed = time.perf_counter() - started
                metrics = result_metrics(
                    count=count,
                    msg_size=args.msg_size,
                    elapsed_s=max(args.duration, elapsed),
                    latencies_ns=latencies,
                )
                print_result_lines("MULTI_PUBSUB", "tcp", args.msg_size, metrics)
        finally:
            for sock in sockets:
                try:
                    sock.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
