import sys
import threading
import time
import os

import zlink

from perf_multi_common import (
    STOP_TOKEN,
    apply_multi_socket_options,
    benchmark_endpoint,
    benchmark_run_id,
    configure_multi_tls_server,
    recv_nonblocking,
    new_payload,
    parse_server_args,
    resolve_multi_connect_ready_timeout_ms,
    safe_poll,
    send_nonblocking,
    stamp_payload,
    wait_monitor_event,
)


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    run_id = benchmark_run_id()
    endpoint = benchmark_endpoint(args.transport, "multi-dealer-dealer")
    active_duration_s = max(
        1.0,
        float(os.environ.get("PERF_MULTI_DURATION_SECONDS", "5")),
    )
    start_event = threading.Event()
    stop_event = threading.Event()
    payload = new_payload(args.msg_size)
    primed_peers = 0

    def read_commands():
        for line in sys.stdin:
            text = line.strip().upper()
            if text == f"START,{args.msg_size}":
                start_event.set()
            elif text in {"STOP", "QUIT"}:
                stop_event.set()
                return

    threading.Thread(target=read_commands, daemon=True).start()

    with zlink.Context() as ctx:
        with zlink.DealerSocket(ctx) as dealer:
            configure_multi_tls_server(dealer, args.transport)
            apply_multi_socket_options(dealer)
            with dealer.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as monitor:
                dealer.bind(endpoint)
                print(f"READY,{endpoint}", flush=True)
                with zlink.Poller() as poller:
                    poller.add_socket(
                        dealer,
                        zlink.PollEvent.POLLIN | zlink.PollEvent.POLLOUT,
                    )
                    # PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven wait.
                    while not start_event.is_set() and not stop_event.is_set():
                        events = safe_poll(poller, -1)
                        if not events:
                            continue
                        for event in events:
                            if not (int(event.events) & int(zlink.PollEvent.POLLIN)):
                                continue
                            while primed_peers < args.clients:
                                received = recv_nonblocking(dealer)
                                if received is None:
                                    break
                                primed_peers += 1
                                received.close()
                    if stop_event.is_set():
                        return
                    for _ in range(args.clients):
                        wait_monitor_event(
                            monitor,
                            zlink.MonitorEventMask.CONNECTION_READY,
                            timeout_ms=resolve_multi_connect_ready_timeout_ms(),
                        )
                    active_deadline = time.perf_counter() + active_duration_s
                    # Stop token must be delivered to every load-balanced
                    # client peer. Dealer round-robin can over-/under-visit
                    # individual peers when the stream is short, so we emit
                    # several tokens per peer. Once a peer has exited, its
                    # send queue stays full (HWM) and round-robin skips it,
                    # so the remaining tokens converge onto live peers.
                    stop_tokens_remaining = args.clients * 32
                    send_pending = True
                    while primed_peers < args.clients and not stop_event.is_set():
                        events = safe_poll(poller, -1)
                        if not events:
                            continue
                        for event in events:
                            if not (int(event.events) & int(zlink.PollEvent.POLLIN)):
                                continue
                            while primed_peers < args.clients:
                                received = recv_nonblocking(dealer)
                                if received is None:
                                    break
                                primed_peers += 1
                                received.close()
                    if stop_event.is_set():
                        return
                    while not stop_event.is_set():
                        now = time.perf_counter()
                        if now >= active_deadline and stop_tokens_remaining == 0:
                            # PERF_MULTI_TEST_POLICY § 1.3.1: stay alive
                            # until the runner sends STOP so the wire-level
                            # stop tokens have time to drain to every peer
                            # (linger_ms == 0 would otherwise drop them).
                            stop_event.wait()
                            break
                        if send_pending:
                            if now < active_deadline:
                                ok = send_nonblocking(
                                    dealer,
                                    stamp_payload(payload, phase=1, run_id=run_id),
                                )
                            else:
                                # PERF_MULTI_TEST_POLICY § 1.3.1: signal phase
                                # end on the wire with the stop token. We send
                                # one token per loop pass and then wait for
                                # POLLOUT before the next, so the dealer's
                                # round-robin advances to a different peer
                                # each time and every client eventually
                                # observes a stop token.
                                ok = send_nonblocking(dealer, STOP_TOKEN)
                                if ok:
                                    stop_tokens_remaining -= 1
                            if ok:
                                continue
                        # Wait for POLLOUT readiness (signal-driven).
                        events = safe_poll(poller, -1)
                        if not events:
                            continue
                        for event in events:
                            ev = int(event.events)
                            if ev & int(zlink.PollEvent.POLLIN):
                                while True:
                                    received = recv_nonblocking(dealer)
                                    if received is None:
                                        break
                                    received.close()
                            if ev & int(zlink.PollEvent.POLLOUT):
                                send_pending = True


if __name__ == "__main__":
    main()
