import sys
import threading
from collections import deque

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    benchmark_endpoint,
    parse_server_args,
    perf_server_context,
    recv_nonblocking,
    safe_poll,
    send_nonblocking,
)


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    endpoint = benchmark_endpoint(args.transport, "multi-dealer-router")
    stop = threading.Event()
    pending = deque()

    def wait_stop():
        for line in sys.stdin:
            if line.strip().upper() in {"STOP", "QUIT"}:
                stop.set()
                return
        # stdin EOF (parent closed pipe) is also a STOP signal.
        stop.set()

    threading.Thread(target=wait_stop, daemon=True).start()

    with perf_server_context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            apply_multi_socket_options(router)
            router.bind(endpoint)
            print(f"READY,{endpoint}", flush=True)
            with zlink.Poller() as poller:
                poller.add_socket(
                    router,
                    zlink.PollEventFlag.POLLIN | zlink.PollEventFlag.POLLOUT,
                )
                recv_storage = zlink.Received()
                # PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven wait. The
                # echo server has no in-band phase end of its own; the
                # runner shuts it down via stdin STOP/EOF and SIGTERM.
                # We wake immediately on POLLIN/POLLOUT so we never sit on
                # a short timer cadence for I/O readiness.
                while not stop.is_set():
                    while pending:
                        routing_id, payload = pending[0]
                        if not send_nonblocking(router, payload, routing_id=routing_id):
                            break
                        pending.popleft()
                    while True:
                        received = recv_nonblocking(router, storage=recv_storage)
                        if received is None:
                            break
                        with received:
                            payload = received.to_bytes_list()[0]
                            routing_id = bytes(received.routing_id)
                            sent = False
                            if not pending:
                                sent = (
                                    received.send()
                                    .message(payload)
                                    .flags(zlink.SendFlags.DONT_WAIT)
                                    .submit()
                                )
                        if not sent:
                            pending.append((routing_id, payload))
                    safe_poll(poller, -1)


if __name__ == "__main__":
    main()
