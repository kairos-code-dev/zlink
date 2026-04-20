import sys
import time
from contextlib import ExitStack

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    benchmark_run_id,
    is_active_message,
    latency_ns_from_message,
    new_payload,
    parse_client_args,
    print_result_lines,
    recv_nonblocking,
    resolve_multi_connect_ready_timeout_ms,
    result_metrics,
    safe_poll,
    send_nonblocking,
    stamp_payload,
    wait_monitor_event,
)


def main(argv=None):
    args = parse_client_args(argv or sys.argv[1:], pattern="router_router")
    run_id = benchmark_run_id()
    payloads = [new_payload(args.msg_size) for _ in range(args.clients)]
    waiting_reply = [False] * args.clients
    send_pending = [True] * args.clients
    latencies = []
    seq = 0

    with zlink.Context() as ctx:
        sockets = [zlink.RouterSocket(ctx) for _ in range(args.clients)]
        try:
            with ExitStack() as stack:
                monitors = []
                for index, sock in enumerate(sockets):
                    sock.set_routing_id(f"CLIENT-{index}".encode("ascii"))
                    sock.router_options.connect_routing_id = b"SERVER"
                    monitor = stack.enter_context(
                        sock.monitor_open(zlink.MonitorEventMask.CONNECTION_READY)
                    )
                    apply_multi_socket_options(sock)
                    sock.connect(args.endpoint)
                    monitors.append(monitor)
                for monitor in monitors:
                    wait_monitor_event(
                        monitor,
                        zlink.MonitorEventMask.CONNECTION_READY,
                        timeout_ms=resolve_multi_connect_ready_timeout_ms(),
                    )

                active_deadline = time.perf_counter() + args.duration
                with zlink.Poller() as poller:
                    for index, sock in enumerate(sockets):
                        poller.add_socket(
                            sock,
                            zlink.PollEvent.POLLIN | zlink.PollEvent.POLLOUT,
                            tag=index,
                        )
                    while time.perf_counter() < active_deadline:
                        events = safe_poll(poller, 100)
                        if not events:
                            continue
                        for event in events:
                            index = event["tag"]
                            current_sock = event["socket"]
                            event_mask = int(event["events"])
                            if (
                                event_mask & int(zlink.PollEvent.POLLOUT)
                                and not waiting_reply[index]
                                and send_pending[index]
                            ):
                                seq += 1
                                payload = stamp_payload(
                                    payloads[index],
                                    phase=1,
                                    run_id=run_id,
                                    seq=seq,
                                )
                                if send_nonblocking(
                                    current_sock,
                                    payload,
                                    routing_id=b"SERVER",
                                ):
                                    waiting_reply[index] = True
                                    send_pending[index] = False
                            if not (event_mask & int(zlink.PollEvent.POLLIN)):
                                continue
                            while True:
                                received = recv_nonblocking(current_sock)
                                if received is None:
                                    break
                                with received:
                                    data = received.to_bytes_list()[0]
                                waiting_reply[index] = False
                                if time.perf_counter() < active_deadline:
                                    send_pending[index] = True
                                if not is_active_message(
                                    data,
                                    expected_msg_size=args.msg_size,
                                    run_id=run_id,
                                ):
                                    continue
                                latencies.append(latency_ns_from_message(data) / 2.0)
                if not latencies:
                    raise RuntimeError(
                        "multi router-router benchmark did not receive any active reply"
                    )
                metrics = result_metrics(
                    count=len(latencies),
                    msg_size=args.msg_size,
                    elapsed_s=args.duration,
                    latencies_ns=latencies,
                    bandwidth_multiplier=2.0,
                )
                print_result_lines(
                    "MULTI_ROUTER_ROUTER", args.transport, args.msg_size, metrics
                )
        finally:
            for sock in sockets:
                try:
                    sock.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
