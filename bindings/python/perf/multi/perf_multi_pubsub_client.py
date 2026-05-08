import sys
import time

import zlink

from perf_multi_common import (
    TOPIC,
    apply_multi_socket_options,
    benchmark_run_id,
    latency_ns_from_message,
    is_active_message,
    parse_client_args,
    print_result_lines,
    recv_nonblocking,
    result_metrics,
)


def main(argv=None):
    args = parse_client_args(
        argv or sys.argv[1:], pattern="pubsub"
    )
    run_id = benchmark_run_id()
    latencies = []
    count = 0

    with zlink.Context() as ctx:
        sockets = [zlink.SubSocket(ctx) for _ in range(args.clients)]
        try:
            for sock in sockets:
                apply_multi_socket_options(sock)
                sock.connect(args.endpoint)
                sock.set_subscription(TOPIC)
            print(f"CLIENT_READY,{args.msg_size}", flush=True)
            saw_start = False
            saw_phase_active = False
            while not (saw_start and saw_phase_active):
                command = sys.stdin.readline().strip()
                if command == f"START,{args.msg_size}":
                    saw_start = True
                    continue
                if command == f"PHASE_ACTIVE,{args.msg_size}":
                    saw_phase_active = True
                    continue
                raise SystemExit(f"unexpected command: {command}")

            started = time.perf_counter()
            deadline = started + args.duration
            while time.perf_counter() < deadline:
                progressed = False
                for current_sock in sockets:
                    while True:
                        received = recv_nonblocking(current_sock, method="subscribe")
                        if received is None:
                            break
                        with received:
                            data = received.to_bytes_list()[0]
                        progressed = True
                        if not is_active_message(
                            data,
                            expected_msg_size=args.msg_size,
                            run_id=run_id,
                        ):
                            continue
                        latencies.append(latency_ns_from_message(data))
                        count += 1

            elapsed = time.perf_counter() - started
            if count <= 0:
                raise RuntimeError(
                    "multi pubsub benchmark did not receive any active message"
                )
            metrics = result_metrics(
                count=count,
                msg_size=args.msg_size,
                elapsed_s=args.duration,
                latencies_ns=latencies,
            )
            print_result_lines("MULTI_PUBSUB", args.transport, args.msg_size, metrics)
        finally:
            for sock in sockets:
                try:
                    sock.close()
                except Exception as exc:
                    print(f"[perf] close failed: {exc}", file=sys.stderr)


if __name__ == "__main__":
    main()
