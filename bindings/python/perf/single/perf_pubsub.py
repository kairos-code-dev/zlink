import sys
import threading
import time

import zlink

from perf_common import (
    apply_single_socket_options,
    benchmark_run_id,
    configure_single_tls_client,
    configure_single_tls_server,
    latency_ns_from_message,
    is_active_message,
    new_payload,
    parse_single_args,
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


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="pubsub")
    payload = new_payload(args.msg_size)
    run_id = benchmark_run_id()
    latencies = []

    def send_loop(publisher):
        active_end = time.perf_counter() + args.duration
        while time.perf_counter() < active_end:
            publisher.publish(TOPIC, stamp_payload(payload, phase=1, run_id=run_id))
        publisher.publish(TOPIC, stamp_payload(payload, phase=2, run_id=run_id))
    with zlink.Context() as ctx:
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

                sender = threading.Thread(
                    target=send_loop, args=(publisher,), daemon=True
                )
                sender.start()
                active_deadline = time.perf_counter() + args.duration
                idle_drain_deadline = active_deadline + (
                    resolve_single_pubsub_recv_timeout_ms() / 1000.0
                )
                with zlink.Poller() as poller:
                    poller.add_socket(subscriber, zlink.PollEvent.POLLIN)
                    while time.perf_counter() < idle_drain_deadline:
                        timeout_ms = max(
                            1,
                            int((idle_drain_deadline - time.perf_counter()) * 1000),
                        )
                        events = safe_poll(poller, timeout_ms)
                        if not events:
                            break
                        for event in events:
                            current_sock = event["socket"]
                            while True:
                                received = recv_nonblocking(current_sock, method="subscribe")
                                if received is None:
                                    break
                                with received:
                                    data = received.to_bytes_list()[0]
                                if time.perf_counter() > active_deadline:
                                    continue
                                if not is_active_message(
                                    data,
                                    expected_msg_size=args.msg_size,
                                    run_id=run_id,
                                ):
                                    continue
                                latencies.append(latency_ns_from_message(data))

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
