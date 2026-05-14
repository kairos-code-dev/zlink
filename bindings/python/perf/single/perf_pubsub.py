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
    is_stop_token_in_parts,
    latency_ns_from_message,
    is_active_message,
    new_payload,
    parse_single_args,
    perf_context,
    print_result_lines,
    resolve_single_endpoint,
    resolve_single_connect_ready_timeout_ms,
    resolve_single_pubsub_ready_settle_s,
    resolve_single_pubsub_recv_timeout_ms,
    result_metrics,
    recv_nonblocking,
    safe_poll,
    stamp_payload,
    wait_monitor_event,
)


TOPIC = b"bench"


def _publish_stop_token(publisher):
    """PERF_SINGLE_TEST_POLICY § 1.4 wire-level shutdown signal."""

    for _ in range(100):
        try:
            publisher.publish(TOPIC).message(STOP_TOKEN).submit()
            return
        except zlink.SubmitError as exc:
            if exc.result != zlink.SubmitResult.BACKPRESSURED:
                raise
            time.sleep(0.001)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="pubsub")
    payload = new_payload(args.msg_size)
    run_id = benchmark_run_id()
    latencies = []

    def send_loop(publisher, active_end):
        while time.perf_counter() < active_end:
            publisher.publish(TOPIC).message(stamp_payload(payload, phase=1, run_id=run_id)).submit()
        _publish_stop_token(publisher)
    with perf_context() as ctx:
        with zlink.PubSocket(ctx) as publisher:
            with zlink.SubSocket(ctx) as subscriber:
                endpoint = resolve_single_endpoint(args.transport, "pubsub")
                apply_single_socket_options(
                    publisher,
                    subscriber,
                    receive_timeout_ms=resolve_single_pubsub_recv_timeout_ms(),
                )
                configure_single_tls_server(publisher, args.transport)
                configure_single_tls_client(subscriber, args.transport)
                subscriber.set_subscription(TOPIC)
                with subscriber.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as monitor:
                    publisher.bind(endpoint)
                    subscriber.connect(endpoint)
                    wait_monitor_event(
                        monitor,
                        zlink.MonitorEventMask.CONNECTION_READY,
                        timeout_ms=resolve_single_connect_ready_timeout_ms(),
                    )
                wait_seconds = resolve_single_pubsub_ready_settle_s()
                if wait_seconds > 0:
                    time.sleep(wait_seconds)

                active_end = time.perf_counter() + args.duration
                sender = threading.Thread(
                    target=send_loop, args=(publisher, active_end), daemon=True
                )
                sender.start()
                with zlink.Poller() as poller:
                    poller.add_socket(subscriber, zlink.PollEvent.POLLIN)
                    stop_received = False
                    # PERF_SINGLE_TEST_POLICY § 1.4: signal-driven wait.
                    while not stop_received:
                        events = safe_poll(poller, -1)
                        if not events:
                            continue
                        for event in events:
                            current_sock = event.socket
                            while True:
                                received = recv_nonblocking(current_sock, method="subscribe")
                                if received is None:
                                    break
                                with received:
                                    parts = received.to_bytes_list()
                                if is_stop_token_in_parts(parts):
                                    stop_received = True
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
                                if time.perf_counter() >= active_end:
                                    continue
                                latency = latency_ns_from_message(data)
                                if latency is not None:
                                    latencies.append(latency)
                            if stop_received:
                                break

                sender.join()
                if not latencies:
                    raise RuntimeError("pubsub benchmark did not receive any active message")
                metrics = result_metrics(
                    count=len(latencies),
                    msg_size=args.msg_size,
                    elapsed_s=args.duration,
                    latencies_ns=latencies,
                )
                print_result_lines("PUBSUB", args.transport, args.msg_size, metrics)


if __name__ == "__main__":
    main()
