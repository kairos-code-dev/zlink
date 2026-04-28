import sys
import threading
from collections import deque

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    benchmark_endpoint,
    parse_server_args,
    send_nonblocking,
)


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    endpoint = benchmark_endpoint(args.transport, "multi-stream")
    stop = threading.Event()
    pending = deque()
    pending_lock = threading.Lock()

    def wait_stop():
        for line in sys.stdin:
            if line.strip() in {"STOP", "QUIT"}:
                stop.set()
                return

    threading.Thread(target=wait_stop, daemon=True).start()

    with zlink.Context() as ctx:
        with zlink.StreamSocket(ctx) as server:
            apply_multi_socket_options(server)
            server.options.tcp_no_delay = True
            server.bind(endpoint)
            print(f"READY,{endpoint}", flush=True)

            def packet_handler(routing_id, header, body):
                frame = build_packet_frame(header, body)
                with pending_lock:
                    pending.append((bytes(routing_id), frame))

            server.on_packet(packet_handler)
            while not stop.is_set():
                progressed = False
                while True:
                    with pending_lock:
                        item = pending[0] if pending else None
                    if item is None:
                        break
                    routing_id, frame = item
                    if not send_nonblocking(server, frame, routing_id=routing_id):
                        break
                    with pending_lock:
                        if pending and pending[0] == item:
                            pending.popleft()
                    progressed = True
                if not progressed:
                    stop.wait(0.001)


def build_packet_frame(header, body):
    header_bytes = header.to_bytes()
    body_bytes = body.to_bytes()
    return (
        len(header_bytes).to_bytes(2, "big")
        + len(body_bytes).to_bytes(4, "big")
        + header_bytes
        + body_bytes
    )


if __name__ == "__main__":
    main()
