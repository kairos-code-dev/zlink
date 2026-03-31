import sys
import time

import zlink

from perf_multi_common import (
    TOPIC,
    latency_us_from_message,
    parse_client_args,
    print_result_lines,
    result_metrics,
)


def _drain_ready(poller, active, latencies, deadline=None):
    count = 0
    events = poller.poll(50)
    for event in events:
        sock = event["socket"]
        while True:
            if deadline is not None and time.perf_counter() >= deadline:
                return count
            received = sock.try_recv()
            if received is None:
                break
            with received:
                if active:
                    latencies.append(
                        latency_us_from_message(received.to_bytes_list()[0])
                    )
                    count += 1
    return count


def main(argv=None):
    args = parse_client_args(
        argv or sys.argv[1:], pattern="pubsub", allowed_recv={"recv"}
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

            with zlink.Poller() as poller:
                for sock in sockets:
                    poller.add_socket(sock, zlink.PollEvent.POLLIN)

                warmup_deadline = time.perf_counter() + args.warmup
                while time.perf_counter() < warmup_deadline:
                    _drain_ready(poller, False, latencies, warmup_deadline)

                started = time.perf_counter()
                deadline = started + args.duration
                while time.perf_counter() < deadline:
                    count += _drain_ready(poller, True, latencies, deadline)

            elapsed = time.perf_counter() - started
            metrics = result_metrics(
                count=count,
                msg_size=args.msg_size,
                elapsed_s=max(args.duration, elapsed),
                latencies_us=latencies,
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
