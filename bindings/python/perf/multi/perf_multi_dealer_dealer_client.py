import sys
import time
from contextlib import ExitStack

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    benchmark_run_id,
    configure_multi_tls_client,
    new_payload,
    parse_client_args,
    perf_client_context,
    resolve_multi_connect_ready_timeout_ms,
    safe_poll,
    send_nonblocking,
    stamp_payload,
    wait_monitor_event,
)


def main(argv=None):
    args = parse_client_args(argv or sys.argv[1:], pattern="dealer_dealer")
    run_id = benchmark_run_id()
    payloads = [new_payload(args.msg_size) for _ in range(args.clients)]
    seq = 0

    with perf_client_context() as ctx:
        sockets = [zlink.DealerSocket(ctx) for _ in range(args.clients)]
        try:
            with ExitStack() as stack:
                monitors = []
                for sock in sockets:
                    monitor = stack.enter_context(
                        sock.monitor_open(zlink.MonitorEventMask.CONNECTION_READY)
                    )
                    configure_multi_tls_client(sock, args.transport)
                    apply_multi_socket_options(sock)
                    sock.connect(args.endpoint)
                    monitors.append(monitor)
                for monitor in monitors:
                    wait_monitor_event(
                        monitor,
                        zlink.MonitorEventMask.CONNECTION_READY,
                        timeout_ms=resolve_multi_connect_ready_timeout_ms(),
                    )
                print(f"CLIENT_READY,{args.msg_size}", flush=True)
                command = sys.stdin.readline().strip()
                if command != f"START,{args.msg_size}":
                    raise SystemExit(f"unexpected command: {command}")

                active_deadline = time.perf_counter() + args.duration
                with zlink.Poller() as poller:
                    for sock in sockets:
                        poller.add_socket(sock, zlink.PollEvent.POLLOUT)
                    while time.perf_counter() < active_deadline:
                        progressed = False
                        for index, current_sock in enumerate(sockets):
                            if time.perf_counter() >= active_deadline:
                                break
                            seq += 1
                            progressed |= send_nonblocking(
                                current_sock,
                                stamp_payload(
                                    payloads[index],
                                    phase=1,
                                    run_id=run_id,
                                    seq=seq,
                                ),
                            )
                        if not progressed:
                            continue
                print(f"CLIENT_DONE,{args.msg_size}", flush=True)
        finally:
            for sock in sockets:
                try:
                    sock.close()
                except Exception as exc:
                    print(f"[perf] close failed: {exc}", file=sys.stderr)


if __name__ == "__main__":
    main()
