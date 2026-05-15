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
                    poller.add_socket(
                        dealer,
                        zlink.PollEventFlag.POLLIN | zlink.PollEventFlag.POLLOUT,
                    )
                    start_event.wait()
                    if stop_event.is_set():
                        return
                    active_deadline = time.perf_counter() + active_duration_s
                    latencies = []
                    count = 0
                    while time.perf_counter() < active_deadline and not stop_event.is_set():
                        remaining_ms = max(
                            1,
                            int((active_deadline - time.perf_counter()) * 1000),
                        )
                        events = safe_poll(poller, remaining_ms)
                        if not events:
                            continue
                        for event in events:
                            if int(event.events) & int(zlink.PollEventFlag.POLLIN):
                                while True:
                                    received = recv_nonblocking(dealer)
                                    if received is None:
                                        break
                                    with received:
                                        parts = received.to_bytes_list()
                                    data = extract_metric_payload(parts)
                                    if not is_active_message(
                                        data,
                                        expected_msg_size=args.msg_size,
                                        run_id=run_id,
                                    ):
                                        continue
                                    if time.perf_counter() >= active_deadline:
                                        continue
                                    latency = latency_ns_from_message(data)
                                    if latency is not None:
                                        latencies.append(latency)
                                    count += 1
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
