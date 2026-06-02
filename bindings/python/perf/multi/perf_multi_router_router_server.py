import sys
import threading
from collections import deque

import zlink
from zlink._native import bridge as _native_bridge

from perf_multi_common import (
    apply_multi_auto_hwm_msg_unit,
    apply_multi_socket_options,
    benchmark_endpoint,
    configure_multi_tls_server,
    parse_server_args,
    perf_server_context,
    recv_nonblocking,
    safe_poll,
    send_nonblocking,
)


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    endpoint = benchmark_endpoint(args.transport, "multi-router-router")
    stop = threading.Event()
    stop_flag = bytearray(1)
    pending = deque()

    def wait_stop():
        for line in sys.stdin:
            if line.strip().upper() in {"STOP", "QUIT"}:
                stop_flag[0] = 1
                stop.set()
                return
        stop_flag[0] = 1
        stop.set()

    threading.Thread(target=wait_stop, daemon=True).start()

    with perf_server_context() as ctx:
        apply_multi_auto_hwm_msg_unit(ctx, args.msg_size)
        with zlink.create_router_socket(ctx) as router:
            router.set_routing_id(b"SERVER")
            configure_multi_tls_server(router, args.transport)
            apply_multi_socket_options(router)
            router.bind(endpoint)
            print(f"READY,{endpoint}", flush=True)
            if _native_bridge.available():
                received, sent, pending_count, err = (
                    _native_bridge.router_echo_server_loop(
                        router._handle,
                        stop_flag,
                    )
                )
                if err:
                    raise OSError(err, "native router echo server loop failed")
                _ = (received, sent, pending_count)
                return
            with zlink.create_poller() as poller:
                poller.add_socket(
                    router,
                    zlink.PollEventFlag.POLLIN | zlink.PollEventFlag.POLLOUT,
                    0,
                )
                poll_events = zlink.create_poll_events(1)
                recv_storage = zlink.create_received()
                # PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven wait.
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
                            payload = received.parts[0].data
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
                                payload = bytes(payload)
                        if not sent:
                            pending.append((routing_id, payload))
                    safe_poll(poller, poll_events, -1)


if __name__ == "__main__":
    main()
