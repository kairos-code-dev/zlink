import sys
import threading
import time

import zlink

from perf_common import (
    benchmark_run_id,
    latency_ns_from_message,
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
    args = parse_single_args(argv or sys.argv[1:], pattern="router_router")
    payload = new_payload(args.msg_size)
    run_id = benchmark_run_id()
    latencies = []

    def send_loop(router):
        warmup_end = time.perf_counter() + args.warmup
        while time.perf_counter() < warmup_end:
            router.send(stamp_payload(payload, phase=0), routing_id=b"SERVER")

        active_end = time.perf_counter() + args.duration
        while time.perf_counter() < active_end:
            router.send(stamp_payload(payload, phase=1), routing_id=b"SERVER")
    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as server:
            with zlink.RouterSocket(ctx) as client:
                server.set_routing_id(b"SERVER")
                client.set_routing_id(b"CLIENT")
                client.router_options.connect_routing_id = b"SERVER"
                endpoint = unique_endpoint("router-router")
                with server.monitor_open(zlink.MonitorEvent.CONNECTION_READY) as server_monitor:
                    with client.monitor_open(
                        zlink.MonitorEvent.CONNECTION_READY
                    ) as client_monitor:
                        server.bind(endpoint)
                        client.connect(endpoint)
                        wait_monitor_event(
                            server_monitor, zlink.MonitorEvent.CONNECTION_READY
                        )
                        wait_monitor_event(
                            client_monitor, zlink.MonitorEvent.CONNECTION_READY
                        )

                        sender = threading.Thread(
                            target=send_loop, args=(client,), daemon=True
                        )
                        sender.start()
                        drain_deadline = time.perf_counter() + args.duration + 1.0

                        with zlink.Poller() as poller:
                            poller.add_socket(server, zlink.PollEvent.POLLIN)
                            while True:
                                safe_poll(poller, 50)
                                while True:
                                    received = server.try_recv()
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
                                        latencies.append(latency_ns_from_message(data))
                                if time.perf_counter() >= drain_deadline:
                                    break

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
                print_result_lines("ROUTER_ROUTER", "inproc", args.msg_size, metrics)


if __name__ == "__main__":
    main()
