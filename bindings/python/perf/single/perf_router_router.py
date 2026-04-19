import sys
import threading
import time

import zlink

from perf_common import (
    benchmark_run_id,
    latency_ns_from_message,
    is_active_message,
    payload_phase,
    new_payload,
    parse_single_args,
    print_result_lines,
    result_metrics,
    stamp_payload,
    tcp_endpoint,
)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="router_router")
    run_id = benchmark_run_id()
    latencies = []

    def send_loop(router):
        warmup_end = time.perf_counter() + args.warmup
        while time.perf_counter() < warmup_end:
            router.send(
                b"SERVER",
                stamp_payload(
                    new_payload(args.msg_size), phase=0, run_id=run_id
                ),
            )

        active_end = time.perf_counter() + args.duration
        while time.perf_counter() < active_end:
            router.send(
                b"SERVER",
                stamp_payload(
                    new_payload(args.msg_size), phase=1, run_id=run_id
                ),
            )
        router.send(
            b"SERVER",
            stamp_payload(new_payload(args.msg_size), phase=2, run_id=run_id),
        )

    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as server:
            with zlink.RouterSocket(ctx) as client:
                server.set_routing_id(b"SERVER")
                client.set_routing_id(b"CLIENT")
                client.router_options.connect_routing_id = b"SERVER"
                endpoint = tcp_endpoint("router-router")
                server.bind(endpoint)
                client.connect(endpoint)
                time.sleep(0.05)

                sender = threading.Thread(
                    target=send_loop, args=(client,), daemon=True
                )
                sender.start()

                while True:
                    with server.recv() as received:
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
                        "router-router benchmark did not receive any active message"
                    )
                metrics = result_metrics(
                    count=len(latencies),
                    msg_size=args.msg_size,
                    elapsed_s=args.duration,
                    latencies_ns=latencies,
                )
                print_result_lines("ROUTER_ROUTER", "tcp", args.msg_size, metrics)


if __name__ == "__main__":
    main()
