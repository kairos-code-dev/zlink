import sys
import threading
import time

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    benchmark_endpoint,
    benchmark_run_id,
    configure_multi_tls_server,
    extract_metric_payload,
    is_active_message,
    is_stop_token_in_parts,
    latency_ns_from_message,
    print_result_lines,
    recv_nonblocking,
    parse_server_args,
    perf_server_context,
    result_metrics,
    safe_poll,
)


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    run_id = benchmark_run_id()
    endpoint = benchmark_endpoint(args.transport, "multi-dealer-dealer")
    active_duration_s = max(1.0, float(args.duration))
    start_event = threading.Event()
    stop_event = threading.Event()

    def read_commands():
        for line in sys.stdin:
            text = line.strip().upper()
            if text == f"START,{args.msg_size}":
                start_event.set()
            elif text in {"STOP", "QUIT"}:
                stop_event.set()
                start_event.set()
                return

    threading.Thread(target=read_commands, daemon=True).start()

    with perf_server_context() as ctx:
        with zlink.DealerSocket(ctx) as dealer:
            configure_multi_tls_server(dealer, args.transport)
            apply_multi_socket_options(dealer)
            with dealer.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as monitor:
                dealer.bind(endpoint)
                print(f"READY,{endpoint}", flush=True)
                with zlink.Poller() as poller:
                    poller.add_socket(dealer, zlink.PollEventFlag.POLLIN)
                    start_event.wait()
                    if stop_event.is_set():
                        return
                    active_deadline = time.perf_counter() + active_duration_s
                    latencies = []
                    count = 0
                    recv_storage = zlink.Received()

                    def drain_ready():
                        # C receive_one_message + drain_non_blocking_messages:
                        # every matched header counts; latency excludes
                        # clock-skew (latency_ns_from_message returns None).
                        nonlocal count
                        while True:
                            msg = recv_nonblocking(dealer, storage=recv_storage)
                            if msg is None:
                                return
                            with msg:
                                parts = msg.to_bytes_list()
                            if is_stop_token_in_parts(parts):
                                continue
                            data = extract_metric_payload(parts)
                            if not is_active_message(
                                data,
                                expected_msg_size=args.msg_size,
                                run_id=run_id,
                            ):
                                continue
                            count += 1
                            latency = latency_ns_from_message(data)
                            if latency is not None:
                                latencies.append(latency)

                    # C run_receive_window: poll(-1) POLLIN, count until the
                    # measure deadline, then break.
                    while not stop_event.is_set():
                        events = safe_poll(poller, -1)
                        if events:
                            for event in events:
                                if int(event.events) & int(
                                    zlink.PollEventFlag.POLLIN
                                ):
                                    drain_ready()
                        if time.perf_counter() >= active_deadline:
                            break

                    # C drain_phase_until_idle: bounded uncounted tail drain
                    # so late in-flight messages do not skew the next case.
                    idle_deadline = time.perf_counter() + 0.05
                    tail_deadline = time.perf_counter() + active_duration_s
                    while (
                        not stop_event.is_set()
                        and time.perf_counter() < tail_deadline
                    ):
                        msg = recv_nonblocking(dealer)
                        if msg is not None:
                            with msg:
                                pass
                            idle_deadline = time.perf_counter() + 0.05
                            continue
                        if time.perf_counter() >= idle_deadline:
                            break
                        events = safe_poll(poller, 50)

                    if count <= 0:
                        raise RuntimeError(
                            "multi dealer-dealer server did not receive any active message"
                        )
                    metrics = result_metrics(
                        count=count,
                        msg_size=args.msg_size,
                        elapsed_s=args.duration,
                        latencies_ns=latencies,
                    )
                    print_result_lines(
                        "MULTI_DEALER_DEALER",
                        args.transport,
                        args.msg_size,
                        metrics,
                    )


if __name__ == "__main__":
    main()
