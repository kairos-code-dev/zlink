import sys
import time
from contextlib import ExitStack

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    benchmark_run_id,
    extract_metric_payload,
    is_active_message,
    latency_ns_from_message,
    new_payload,
    parse_client_args,
    perf_client_context,
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
    args = parse_client_args(argv or sys.argv[1:], pattern="dealer_router")
    run_id = benchmark_run_id()
    payloads = [new_payload(args.msg_size) for _ in range(args.clients)]
    # C perf_multi_client_helpers.hpp run_echo_window_round_robin: the echo
    # client is purely deadline-driven (no wire stop token); awaiting_reply /
    # send_pending gate inflight==1 per socket.
    waiting_reply = [False] * args.clients
    send_pending = [True] * args.clients
    received = 0
    latencies = []
    seq = 0

    with perf_client_context() as ctx:
        sockets = [zlink.DealerSocket(ctx) for _ in range(args.clients)]
        try:
            with ExitStack() as stack:
                monitors = []
                for index, sock in enumerate(sockets):
                    sock.set_routing_id(f"CLIENT-{index}".encode("ascii"))
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
                recv_storage = [zlink.Received() for _ in sockets]
                with zlink.Poller() as poller:
                    for sock in sockets:
                        poller.add_socket(
                            sock,
                            zlink.PollEventFlag.POLLIN | zlink.PollEventFlag.POLLOUT,
                        )
                    # C run_echo_window_round_robin: loop until the steady
                    # deadline (no stop token). Each iteration sends pending
                    # sockets, then polls (-1, signal-driven) only the
                    # sockets with interest and dispatches only ready ones.
                    while time.perf_counter() < active_deadline:
                        for index, current_sock in enumerate(sockets):
                            if waiting_reply[index] or not send_pending[index]:
                                continue
                            seq += 1
                            payload = stamp_payload(
                                payloads[index],
                                phase=1,
                                run_id=run_id,
                                seq=seq,
                            )
                            if send_nonblocking(current_sock, payload):
                                waiting_reply[index] = True
                                send_pending[index] = False

                        # Build poll interest per C: POLLIN when awaiting a
                        # reply, else POLLOUT when a send is pending.
                        interest = [
                            i
                            for i in range(len(sockets))
                            if waiting_reply[i] or send_pending[i]
                        ]
                        if not interest:
                            for i in range(len(sockets)):
                                if not waiting_reply[i]:
                                    send_pending[i] = True
                            continue

                        # C uses perf_socket_poll(..., -1). The Poller has all
                        # sockets registered for POLLIN|POLLOUT; -1 wait is
                        # signal-driven and the result identifies ready
                        # sources for dispatch.
                        events = safe_poll(poller, -1)
                        if not events:
                            continue
                        for event in events:
                            current_sock = event.socket
                            try:
                                index = sockets.index(current_sock)
                            except ValueError:
                                continue
                            ev = int(event.events)
                            if (
                                ev & int(zlink.PollEventFlag.POLLOUT)
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
                                if send_nonblocking(current_sock, payload):
                                    waiting_reply[index] = True
                                    send_pending[index] = False
                            if not (ev & int(zlink.PollEventFlag.POLLIN)):
                                continue
                            while True:
                                msg = recv_nonblocking(
                                    current_sock,
                                    storage=recv_storage[index],
                                )
                                if msg is None:
                                    break
                                with msg:
                                    parts = msg.to_bytes_list()
                                waiting_reply[index] = False
                                data = extract_metric_payload(parts)
                                header = latency_ns_from_message(data) if data else None
                                # C: every matched header counts (++local_recv,
                                # → recv_count); latency only added when not
                                # clock-skewed (sent_ts_ns>0 && now>=sent_ts),
                                # halved for the round trip.
                                if data and is_active_message(
                                    data,
                                    expected_msg_size=args.msg_size,
                                    run_id=run_id,
                                ):
                                    received += 1
                                    if header is not None:
                                        latencies.append(header / 2.0)
                                if time.perf_counter() < active_deadline:
                                    send_pending[index] = True
                if received == 0:
                    raise RuntimeError(
                        "multi dealer-router benchmark did not receive any active reply"
                    )
                metrics = result_metrics(
                    count=received,
                    msg_size=args.msg_size,
                    elapsed_s=args.duration,
                    latencies_ns=latencies,
                    bandwidth_multiplier=2.0,
                )
                print_result_lines(
                    "MULTI_DEALER_ROUTER", args.transport, args.msg_size, metrics
                )
        finally:
            for sock in sockets:
                try:
                    sock.close()
                except Exception as exc:
                    print(f"[perf] close failed: {exc}", file=sys.stderr)


if __name__ == "__main__":
    main()
