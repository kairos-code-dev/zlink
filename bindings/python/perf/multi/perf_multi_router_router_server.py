import sys
import threading
from collections import deque

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    benchmark_endpoint,
    parse_server_args,
    recv_nonblocking,
    safe_poll,
    send_nonblocking,
)


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    endpoint = benchmark_endpoint(args.transport, "multi-router-router")
    stop = threading.Event()
    pending = deque()

    def wait_stop():
        for line in sys.stdin:
            if line.strip().upper() in {"STOP", "QUIT"}:
                stop.set()
                return

    threading.Thread(target=wait_stop, daemon=True).start()

    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            router.set_routing_id(b"SERVER")
            apply_multi_socket_options(router)
            router.bind(endpoint)
            print(f"READY,{endpoint}", flush=True)
            with zlink.Poller() as poller:
                poller.add_socket(
                    router,
                    zlink.PollEvent.POLLIN | zlink.PollEvent.POLLOUT,
                )
                while not stop.is_set():
                    events = safe_poll(poller, 100)
                    if not events:
                        continue
                    for event in events:
                        event_mask = int(event["events"])
                        if event_mask & int(zlink.PollEvent.POLLOUT):
                            while pending:
                                routing_id, payload = pending[0]
                                if not send_nonblocking(
                                    router,
                                    payload,
                                    routing_id=routing_id,
                                ):
                                    break
                                pending.popleft()
                        if not (event_mask & int(zlink.PollEvent.POLLIN)):
                            continue
                        while True:
                            received = recv_nonblocking(router)
                            if received is None:
                                break
                            with received:
                                payload = received.to_bytes_list()[0]
                                routing_id = bytes(received.routing_id)
                            if pending or not send_nonblocking(
                                router,
                                payload,
                                routing_id=routing_id,
                            ):
                                pending.append((routing_id, payload))


if __name__ == "__main__":
    main()
