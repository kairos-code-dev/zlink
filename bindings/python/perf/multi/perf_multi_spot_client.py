import os
import sys
import time

import zlink

from perf_multi_common import (
    TOPIC,
    latency_ns_from_message,
    is_active_message,
    parse_client_args,
    print_result_lines,
    result_metrics,
)


def main(argv=None):
    args = parse_client_args(argv or sys.argv[1:], pattern="spot")
    clients = []
    latencies = []
    count = 0

    with zlink.Context() as ctx:
        try:
            for index in range(args.clients):
                node = zlink.SpotNode(ctx)
                node.set_routing_id(f"SPOT-CLIENT-{index}".encode("ascii"))
                node.connect_peer(args.endpoint)
                spot = node.wrap_handle()
                spot.set_subscription(TOPIC)
                clients.append((node, spot))

            time.sleep(0.05)
            started = time.perf_counter()
            deadline = started + args.duration
            while time.perf_counter() < deadline:
                for _, spot in clients:
                    with spot.subscribe() as received:
                        data = received.to_bytes_list()[0]
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
            print_result_lines("MULTI_SPOT", "tcp", args.msg_size, metrics)
            sys.stdout.flush()
            os._exit(0)
        finally:
            for node, spot in reversed(clients):
                try:
                    spot.close()
                except Exception:
                    pass
                try:
                    node.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
