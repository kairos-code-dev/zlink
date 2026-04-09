import sys
import threading
import time

import zlink

from perf_common import (
    benchmark_run_id,
    latency_us_from_message,
    is_active_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    result_metrics,
    safe_poll,
    stamp_payload,
    unique_endpoint,
    wait_monitor_event,
)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="dealer_router")
    payload = new_payload(args.msg_size)
    run_id = benchmark_run_id()
    lock = threading.Lock()
    state = {"active_sent": 0, "active_count": 0, "latencies": []}

    def send_loop(dealer):
        warmup_end = time.perf_counter() + args.warmup
        while time.perf_counter() < warmup_end:
            dealer.send(stamp_payload(payload, phase=0))

        active_end = time.perf_counter() + args.duration
        while time.perf_counter() < active_end:
            dealer.send(stamp_payload(payload, phase=1))
            with lock:
                state["active_sent"] += 1
    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            with zlink.DealerSocket(ctx) as dealer:
                endpoint = unique_endpoint("dealer-router")
                with router.monitor_open(zlink.MonitorEvent.CONNECTION_READY) as router_monitor:
                    with dealer.monitor_open(
                        zlink.MonitorEvent.CONNECTION_READY
                    ) as dealer_monitor:
                        router.bind(endpoint)
                        dealer.connect(endpoint)
                        wait_monitor_event(
                            router_monitor, zlink.MonitorEvent.CONNECTION_READY
                        )
                        wait_monitor_event(
                            dealer_monitor, zlink.MonitorEvent.CONNECTION_READY
                        )

                        sender = threading.Thread(
                            target=send_loop, args=(dealer,), daemon=True
                        )
                        sender.start()
                        drain_deadline = time.perf_counter() + args.duration + 1.0

                        with zlink.Poller() as poller:
                            poller.add_socket(router, zlink.PollEvent.POLLIN)
                            while True:
                                safe_poll(poller, 50)
                                while True:
                                    received = router.try_recv()
                                    if received is None:
                                        break
                                    with received:
                                        data = received.to_bytes_list()[0]
                                        if not is_active_message(
                                            data,
                                            expected_msg_size=args.msg_size,
                                            run_id=run_id,
                                        ):
                                            continue
                                        with lock:
                                            state["active_count"] += 1
                                        state["latencies"].append(
                                            latency_us_from_message(data)
                                        )
                                if time.perf_counter() >= drain_deadline:
                                    break

                        sender.join()
                if state["active_count"] == 0:
                    raise RuntimeError(
                        "dealer-router benchmark did not receive any active message"
                    )
                metrics = result_metrics(
                    count=state["active_count"],
                    msg_size=args.msg_size,
                    elapsed_s=args.duration,
                    latencies_us=state["latencies"],
                )
                print_result_lines("DEALER_ROUTER", "inproc", args.msg_size, metrics)


if __name__ == "__main__":
    main()
