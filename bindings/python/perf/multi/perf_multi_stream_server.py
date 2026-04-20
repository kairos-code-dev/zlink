import sys
import threading
import queue

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    benchmark_endpoint,
    parse_server_args,
)


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    endpoint = benchmark_endpoint(args.transport, "multi-stream")
    stop = threading.Event()

    def wait_stop():
        sys.stdin.readline()
        stop.set()

    threading.Thread(target=wait_stop, daemon=True).start()

    with zlink.Context() as ctx:
        with zlink.StreamSocket(ctx) as server:
            apply_multi_socket_options(server)
            server.options.tcp_no_delay = True
            server.bind(endpoint)
            print(f"READY,{endpoint}", flush=True)
            send_queue = queue.SimpleQueue()

            def packet_handler(routing_id, header, body):
                send_queue.put((routing_id, build_packet_frame(header, body)))

            def sender_worker():
                while not stop.is_set():
                    try:
                        routing_id, payload = send_queue.get(timeout=0.1)
                    except queue.Empty:
                        continue
                    server.send(routing_id, payload)

            server.on_packet(packet_handler)
            threading.Thread(target=sender_worker, daemon=True).start()
            while not stop.is_set():
                stop.wait(0.1)


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
