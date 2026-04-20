import sys
import threading
import time

import zlink

from perf_common import (
    apply_single_socket_options,
    benchmark_run_id,
    extract_metric_payload,
    latency_ns_from_message,
    is_active_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    recv_nonblocking,
    result_metrics,
    resolve_single_endpoint,
    resolve_single_recv_timeout_ms,
    safe_poll,
    stamp_payload,
    wait_monitor_event,
)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="router_router")
    run_id = benchmark_run_id()
    latencies = []
    payload = new_payload(args.msg_size)

    def send_loop(router):
        active_end = time.perf_counter() + args.duration
        while time.perf_counter() < active_end:
            router.send(
                b"SERVER",
                stamp_payload(payload, phase=1, run_id=run_id),
            )

    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as server:
            with zlink.RouterSocket(ctx) as client:
                server.set_routing_id(b"SERVER")
                client.set_routing_id(b"CLIENT")
                client.router_options.connect_routing_id = b"SERVER"
                endpoint = resolve_single_endpoint(args.transport, "router-router")
                apply_single_socket_options(server, client)
                with client.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as monitor:
                    server.bind(endpoint)
                    client.connect(endpoint)
                    wait_monitor_event(monitor, zlink.MonitorEventMask.CONNECTION_READY)

                sender = threading.Thread(
                    target=send_loop, args=(client,), daemon=True
                )
                sender.start()
                active_deadline = time.perf_counter() + args.duration
                idle_drain_deadline = active_deadline + (
                    resolve_single_recv_timeout_ms() / 1000.0
                )
                with zlink.Poller() as poller:
                    poller.add_socket(server, zlink.PollEvent.POLLIN)
                    while time.perf_counter() < idle_drain_deadline:
                        timeout_ms = max(
                            1,
                            int((idle_drain_deadline - time.perf_counter()) * 1000),
                        )
                        events = safe_poll(poller, timeout_ms)
                        if not events:
                            break
                        for _event in events:
                            while True:
                                received = recv_nonblocking(server)
                                if received is None:
                                    break
                                with received:
                                    data = extract_metric_payload(received.to_bytes_list())
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
                    raise RuntimeError(
                        "router-router benchmark did not receive any active message"
                    )
                metrics = result_metrics(
                    count=len(latencies),
                    msg_size=args.msg_size,
                    elapsed_s=args.duration,
                    latencies_ns=latencies,
                )
                print_result_lines("ROUTER_ROUTER", args.transport, args.msg_size, metrics)


if __name__ == "__main__":
    main()
