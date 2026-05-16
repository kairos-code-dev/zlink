import sys
import threading
import time

import zlink

from perf_common import (
    STOP_TOKEN,
    apply_single_auto_hwm_msg_unit,
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
    perf_context,
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
    """PERF_SINGLE_TEST_POLICY § 1.4 wire-level shutdown signal."""

    for _ in range(100):
        try:
            sock.send().message(STOP_TOKEN).submit()
            return
        except zlink.SubmitError as exc:
            if exc.result != zlink.SubmitResult.BACKPRESSURED:
                raise
            time.sleep(0.001)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="pair")
    payload = new_payload(args.msg_size)
    run_id = benchmark_run_id()
    latencies = []
    received = 0
    sender_errors = []

    def send_loop(client, active_end):
        try:
            while time.perf_counter() < active_end:
                client.send().message(stamp_payload(payload, phase=1, run_id=run_id)).submit()
            # PERF_SINGLE_TEST_POLICY § 1.4: wire stop token instead of
            # threading.Event coordination.
            _send_stop_token(client)
        except BaseException as exc:  # pragma: no cover - surfaced on main thread
            sender_errors.append(exc)

    with perf_context() as ctx:
        with zlink.PairSocket(ctx) as server:
            with zlink.PairSocket(ctx) as client:
                endpoint = resolve_single_endpoint(args.transport, "pair")
                # C perf_pair.cpp: apply_single_auto_hwm_msg_unit on both raw
                # sockets before setup, then apply_single_hwm/options.
                apply_single_auto_hwm_msg_unit(
                    server, client, msg_size=args.msg_size
                )
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

                active_end = time.perf_counter() + args.duration
                sender = threading.Thread(target=send_loop, args=(client, active_end), daemon=True)
                sender.start()
                with zlink.Poller() as poller:
                    poller.add_socket(server, zlink.PollEventFlag.POLLIN)
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
                                if time.perf_counter() >= active_end:
                                    continue
                                # C perf_single_one_way.hpp run_active_phase:
                                # every matched header increments received and
                                # adds a latency sample (single_latency_ns
                                # clamps clock-skew to 0.0; the message still
                                # counts toward throughput).
                                received += 1
                                latency = latency_ns_from_message(data)
                                latencies.append(
                                    latency if latency is not None else 0.0
                                )
                            if stop_received:
                                break

                sender.join()
                if sender_errors:
                    raise sender_errors[0]
                if received == 0:
                    raise RuntimeError("pair benchmark did not receive any active message")
                metrics = result_metrics(
                    count=received,
                    msg_size=args.msg_size,
                    elapsed_s=args.duration,
                    latencies_ns=latencies,
                )
                print_result_lines("PAIR", args.transport, args.msg_size, metrics)


if __name__ == "__main__":
    main()
