import sys
import threading
import time

import zlink

from perf_common import (
    apply_single_socket_options,
    benchmark_run_id,
    configure_single_tls_client,
    configure_single_tls_server,
    extract_metric_payload,
    latency_ns_from_message,
    is_active_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    recv_nonblocking,
    result_metrics,
    resolve_single_endpoint,
    resolve_single_connect_ready_timeout_ms,
    resolve_single_recv_timeout_ms,
    safe_poll,
    stamp_payload,
    wait_monitor_event,
)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="pair")
    payload = new_payload(args.msg_size)
    run_id = benchmark_run_id()
    latencies = []
    sender_errors = []

    def send_loop(client):
        try:
            active_end = time.perf_counter() + args.duration
            while time.perf_counter() < active_end:
                client.send(stamp_payload(payload, phase=1, run_id=run_id))
            client.send(stamp_payload(payload, phase=2, run_id=run_id))
        except BaseException as exc:  # pragma: no cover - surfaced on main thread
            sender_errors.append(exc)

    with zlink.Context() as ctx:
        with zlink.PairSocket(ctx) as server:
            with zlink.PairSocket(ctx) as client:
                endpoint = resolve_single_endpoint(args.transport, "pair")
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

                sender = threading.Thread(target=send_loop, args=(client,), daemon=True)
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
                            if sender.is_alive():
                                continue
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

                sender.join(timeout=max(1.0, resolve_single_recv_timeout_ms() / 1000.0))
                if sender.is_alive():
                    raise RuntimeError("pair sender thread did not finish")
                if sender_errors:
                    raise sender_errors[0]
                if not latencies:
                    raise RuntimeError("pair benchmark did not receive any active message")
                metrics = result_metrics(
                    count=len(latencies),
                    msg_size=args.msg_size,
                    elapsed_s=args.duration,
                    latencies_ns=latencies,
                )
                print_result_lines("PAIR", args.transport, args.msg_size, metrics)


if __name__ == "__main__":
    main()
