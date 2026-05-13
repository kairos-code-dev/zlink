import sys
from contextlib import ExitStack

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    benchmark_run_id,
    configure_multi_tls_client,
    is_stop_token_in_parts,
    latency_ns_from_message,
    is_active_message,
    new_payload,
    parse_client_args,
    print_result_lines,
    recv_nonblocking,
    resolve_multi_connect_ready_timeout_ms,
    result_metrics,
    safe_poll,
    stamp_payload,
    wait_monitor_event,
)


def main(argv=None):
    args = parse_client_args(argv or sys.argv[1:], pattern="dealer_dealer")
    run_id = benchmark_run_id()
    latencies = []
    count = 0
    warmup = new_payload(args.msg_size)

    with zlink.Context() as ctx:
        sockets = [zlink.DealerSocket(ctx) for _ in range(args.clients)]
        try:
            with ExitStack() as stack:
                monitors = []
                for sock in sockets:
                    monitor = stack.enter_context(
                        sock.monitor_open(zlink.MonitorEventMask.CONNECTION_READY)
                    )
                    configure_multi_tls_client(sock, args.transport)
                    apply_multi_socket_options(sock)
                    sock.connect(args.endpoint)
                    monitors.append(monitor)
                for monitor in monitors:
                    wait_monitor_event(
                        monitor,
                        zlink.MonitorEventMask.CONNECTION_READY,
                        timeout_ms=resolve_multi_connect_ready_timeout_ms(),
                    )
                warmup_parts = [bytes(stamp_payload(warmup, phase=0, run_id=run_id))]
                for sock in sockets:
                    sock.send().messages(*warmup_parts).submit()
                print(f"CLIENT_READY,{args.msg_size}", flush=True)
                command = sys.stdin.readline().strip()
                if command != f"START,{args.msg_size}":
                    raise SystemExit(f"unexpected command: {command}")

                # PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven receive loop.
                # Each socket exits when its peer-side stop token arrives.
                stopped = [False] * len(sockets)
                with zlink.Poller() as poller:
                    for sock in sockets:
                        poller.add_socket(sock, zlink.PollEvent.POLLIN)
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
                            # Drain even after stop so a stopped socket is
                            # removed from the poller's POLLIN set; this also
                            # prevents the wakeup spinning when the peer keeps
                            # sending more bytes after our stop token.
                            if stopped[index]:
                                # Remove from poller so we stop being woken.
                                try:
                                    poller.remove_socket(current_sock)
                                except Exception:
                                    pass
                                continue
                            saw_stop = False
                            while True:
                                received = recv_nonblocking(current_sock)
                                if received is None:
                                    break
                                with received:
                                    parts = received.to_bytes_list()
                                if is_stop_token_in_parts(parts):
                                    saw_stop = True
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
                            if saw_stop:
                                stopped[index] = True
                                try:
                                    poller.remove_socket(current_sock)
                                except Exception:
                                    pass
                if count <= 0:
                    raise RuntimeError(
                        "multi dealer-dealer benchmark did not receive any active message"
                    )
                metrics = result_metrics(
                    count=count,
                    msg_size=args.msg_size,
                    elapsed_s=args.duration,
                    latencies_ns=latencies,
                )
                print_result_lines(
                    "MULTI_DEALER_DEALER", args.transport, args.msg_size, metrics
                )
        finally:
            for sock in sockets:
                try:
                    sock.close()
                except Exception as exc:
                    print(f"[perf] close failed: {exc}", file=sys.stderr)


if __name__ == "__main__":
    main()
