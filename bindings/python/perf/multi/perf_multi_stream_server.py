import sys
import threading

import zlink

from perf_multi_common import drain_len32be_frames, parse_server_args, safe_poll, tcp_endpoint


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    endpoint = tcp_endpoint()
    stop = threading.Event()
    lock = threading.Lock()
    buffers = {}

    def wait_stop():
        sys.stdin.readline()
        stop.set()

    def handle_chunk(received, server):
        routing_id = received.routing_id
        chunk = received.to_bytes_list()[0]
        if routing_id is None:
            return
        with lock:
            buffer = buffers.setdefault(routing_id, bytearray())
            buffer.extend(chunk)
            frames = drain_len32be_frames(buffer)
        for frame in frames:
            server.send_to(routing_id, frame)

    threading.Thread(target=wait_stop, daemon=True).start()

    with zlink.Context() as ctx:
        with zlink.StreamSocket(ctx) as server:
            server.bind(endpoint)
            print(f"READY,{endpoint}", flush=True)
            if args.recv == 'callback':
                server.on_receive(lambda received: handle_chunk(received, server))
                stop.wait()
                return
            with zlink.Poller() as poller:
                poller.add_socket(server, zlink.PollEvent.POLLIN)
                while not stop.is_set():
                    events = safe_poll(poller, 100)
                    if not events:
                        continue
                    while True:
                        received = server.try_recv()
                        if received is None:
                            break
                        with received:
                            handle_chunk(received, server)


if __name__ == "__main__":
    main()
