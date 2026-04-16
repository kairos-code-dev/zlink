import socket

import zlink
from sample_support import tcp_endpoint, wait_socket_monitor_event


def main():
    port, endpoint = tcp_endpoint()

    with zlink.Context() as ctx:
        with zlink.StreamSocket(ctx) as server:
            with server.monitor_open(zlink.MonitorEventMask.ACCEPTED) as server_monitor:
                server.bind(endpoint)
                with socket.create_connection(("127.0.0.1", port), timeout=3.0) as client:
                    wait_socket_monitor_event(server_monitor, zlink.MonitorEventMask.ACCEPTED)
                    client.sendall(b"hello-stream")
                    with server.recv() as received:
                        if received.to_bytes_list() != [b"hello-stream"]:
                            raise AssertionError("unexpected stream payload")
                        if not received.routing_id:
                            raise AssertionError("stream sample expected a routing id")
                        peer_id = received.routing_id.to_u32()
                        server.send(received.routing_id, b"hello-stream")
                    reply = client.recv(64)
                    if reply != b"hello-stream":
                        raise AssertionError(f"unexpected stream reply: {reply!r}")
            print(f'[stream/recv] peer: {peer_id} send: "hello-stream" → recv: "hello-stream"')


if __name__ == "__main__":
    main()
