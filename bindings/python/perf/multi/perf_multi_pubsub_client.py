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
    args = parse_client_args(
        argv or sys.argv[1:], pattern="pubsub"
    )
    latencies = []
    count = 0

    with zlink.Context() as ctx:
        sockets = [zlink.SubSocket(ctx) for _ in range(args.clients)]
        try:
            for sock in sockets:
                sock.options.linger_ms = 0
                sock.connect(args.endpoint)
                sock.set_subscription(TOPIC)
            time.sleep(0.05)

            started = time.perf_counter()
            deadline = started + args.duration
            while time.perf_counter() < deadline:
                for sock in sockets:
                    with sock.subscribe() as received:
                        data = received.to_bytes_list()[0]
                    if not is_active_message(
                        data,
                        expected_msg_size=args.msg_size,
                        run_id=None,
                    ):
                        continue
                    latencies.append(latency_ns_from_message(data))
                    count += 1

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
