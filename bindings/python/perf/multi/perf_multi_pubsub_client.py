import sys

import zlink

from perf_multi_common import (
    TOPIC,
    apply_multi_socket_options,
    benchmark_run_id,
    is_stop_token_in_parts,
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

            stopped = [False] * len(sockets)
            with zlink.Poller() as poller:
                for sock in sockets:
                    poller.add_socket(sock, zlink.PollEvent.POLLIN)
                # PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven wait. Each
                # subscriber exits on the wire-level stop token.
                while not all(stopped):
                    events = safe_poll(poller, -1)
                    if not events:
                        continue
                    for event in events:
                        current_sock = event.socket
                        try:
                            index = sockets.index(current_sock)
                        except ValueError:
                            continue
                        if stopped[index]:
                            continue
                        while True:
                            received = recv_nonblocking(current_sock, method="subscribe")
                            if received is None:
                                break
                            with received:
                                parts = received.to_bytes_list()
                            if is_stop_token_in_parts(parts):
                                stopped[index] = True
                                break
                            if not parts:
                                continue
                            data = parts[0]
                            if not is_active_message(
                                data,
                                expected_msg_size=args.msg_size,
                                run_id=run_id,
                            ):
                                continue
                            latencies.append(latency_ns_from_message(data))
                            count += 1

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
