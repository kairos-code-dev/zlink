import sys
import time

import zlink

from perf_multi_common import (
    TOPIC,
    latency_ns_from_message,
    is_active_message,
    parse_client_args,
    print_result_lines,
    recv_nonblocking,
    result_metrics,
    safe_poll,
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
            print(f"CLIENT_READY,{args.msg_size}", flush=True)
            command = sys.stdin.readline().strip()
            if command != f"START,{args.msg_size}":
                raise SystemExit(f"unexpected command: {command}")

            started = time.perf_counter()
            deadline = started + args.duration
            with zlink.Poller() as poller:
                for sock in sockets:
                    poller.add_socket(sock, zlink.PollEvent.POLLIN)
                while time.perf_counter() < deadline:
                    events = safe_poll(poller, 100)
                    if not events:
                        continue
                    for event in events:
                        current_sock = event["socket"]
                        while True:
                            received = recv_nonblocking(current_sock, method="subscribe")
                            if received is None:
                                break
                            with received:
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
