import sys
import threading
import time

import zlink

from perf_common import (
    apply_single_socket_options,
    benchmark_run_id,
    latency_ns_from_message,
    is_active_message,
    payload_phase,
    new_payload,
    parse_single_args,
    print_result_lines,
    result_metrics,
    resolve_single_endpoint,
    stamp_payload,
    wait_monitor_event,
)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="dealer_router")
    run_id = benchmark_run_id()
    latencies = []

    def send_loop(dealer):
        active_end = time.perf_counter() + args.duration
        while time.perf_counter() < active_end:
            dealer.send(
                stamp_payload(
                    new_payload(args.msg_size), phase=1, run_id=run_id
                )
            )
        dealer.send(
            stamp_payload(new_payload(args.msg_size), phase=2, run_id=run_id)
        )

    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            with zlink.DealerSocket(ctx) as dealer:
                endpoint = resolve_single_endpoint(args.transport, "dealer-router")
                apply_single_socket_options(router, dealer)
                with dealer.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as monitor:
                    router.bind(endpoint)
                    dealer.connect(endpoint)
                    wait_monitor_event(monitor, zlink.MonitorEventMask.CONNECTION_READY)

                sender = threading.Thread(
                    target=send_loop, args=(dealer,), daemon=True
                )
                sender.start()

                while True:
                    with router.recv() as received:
                        data = received.to_bytes_list()[0]
                    phase = payload_phase(data)
                    if phase == 2:
                        break
                    if phase != 1:
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
                        "dealer-router benchmark did not receive any active message"
                    )
                metrics = result_metrics(
                    count=len(latencies),
                    msg_size=args.msg_size,
                    elapsed_s=args.duration,
                    latencies_ns=latencies,
                )
                print_result_lines("DEALER_ROUTER", args.transport, args.msg_size, metrics)


if __name__ == "__main__":
    main()
