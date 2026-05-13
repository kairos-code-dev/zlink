import sys
import threading
import time

import zlink

from perf_common import (
    STOP_TOKEN,
    apply_single_socket_options,
    benchmark_run_id,
    configure_single_tls_client,
    configure_single_tls_server,
    extract_metric_payload,
    is_stop_token_in_parts,
    latency_ns_from_message,
    is_active_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    recv_nonblocking,
    result_metrics,
    resolve_single_endpoint,
    resolve_single_connect_ready_timeout_ms,
    safe_poll,
    stamp_payload,
    wait_monitor_event,
)


def _send_stop_token(sock):
    """Send wire-level stop token once with bounded backpressure retry.

    PERF_SINGLE_TEST_POLICY § 1.4: phase end is signalled on the wire by
    sending ``__zlink_perf_stop__`` once. The send is allowed to block
    through transient backpressure (no deadline) so that the receiver
    always observes the terminator after every in-flight payload.
    """

    for _ in range(100):
        try:
            sock.send().message(STOP_TOKEN).submit()
            return
        except zlink.SubmitError as exc:
            if exc.result != zlink.SubmitResult.BACKPRESSURED:
                raise
            time.sleep(0.001)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="dealer_dealer")
    run_id = benchmark_run_id()
    latencies = []
    payload = new_payload(args.msg_size)

    def send_loop(dealer):
        active_end = time.perf_counter() + args.duration
        while time.perf_counter() < active_end:
            dealer.send().message(stamp_payload(payload, phase=1, run_id=run_id)).submit()
        # PERF_SINGLE_TEST_POLICY § 1.4: signal phase end on the wire.
        _send_stop_token(dealer)

    with zlink.Context() as ctx:
        with zlink.DealerSocket(ctx) as server:
            with zlink.DealerSocket(ctx) as client:
                endpoint = resolve_single_endpoint(args.transport, "dealer-dealer")
                apply_single_socket_options(server, client)
                configure_single_tls_server(server, args.transport)
                configure_single_tls_client(client, args.transport)
                with client.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as monitor:
                    server.bind(endpoint)
                    client.connect(endpoint)
                    wait_monitor_event(
                        monitor,
                        zlink.MonitorEventMask.CONNECTION_READY,
                        timeout_ms=resolve_single_connect_ready_timeout_ms(),
                    )

                sender = threading.Thread(
                    target=send_loop, args=(client,), daemon=True
                )
                sender.start()
                with zlink.Poller() as poller:
                    poller.add_socket(server, zlink.PollEvent.POLLIN)
                    stop_received = False
                    # PERF_SINGLE_TEST_POLICY § 1.4: signal-driven wait.
                    while not stop_received:
                        events = safe_poll(poller, -1)
                        if not events:
                            continue
                        for _event in events:
                            while True:
                                received = recv_nonblocking(server)
                                if received is None:
                                    break
                                with received:
                                    parts = received.to_bytes_list()
                                if is_stop_token_in_parts(parts):
                                    stop_received = True
                                    break
                                data = extract_metric_payload(parts)
                                if not is_active_message(
                                    data,
                                    expected_msg_size=args.msg_size,
                                    run_id=run_id,
                                ):
                                    continue
                                latencies.append(latency_ns_from_message(data))
                            if stop_received:
                                break

                sender.join()
                if not latencies:
                    raise RuntimeError(
                        "dealer-dealer benchmark did not receive any active message"
                    )
                metrics = result_metrics(
                    count=len(latencies),
                    msg_size=args.msg_size,
                    elapsed_s=args.duration,
                    latencies_ns=latencies,
                )
                print_result_lines("DEALER_DEALER", args.transport, args.msg_size, metrics)


if __name__ == "__main__":
    main()
