import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import socket
import threading
import zlink
from sample_common import tcp_endpoint


def main():
    port, endpoint = tcp_endpoint()

    with zlink.Context() as ctx:
        with zlink.StreamSocket(ctx) as server:
            done = threading.Event()
            observed = {}

            def on_message(received):
                observed["routing_id"] = received.routing_id
                observed["parts"] = received.to_bytes_list()
                done.set()

            server.bind(endpoint)
            server.on_receive(on_message)
            with socket.create_connection(("127.0.0.1", port), timeout=3.0) as client:
                client.sendall(b"hello-stream")
                if not done.wait(3.0):
                    raise TimeoutError("stream callback did not receive a message")
                server.send(b"hello-stream", routing_id=observed["routing_id"])
                echo = client.recv(64).decode("utf-8")
                print(f'[stream/callback] send: "hello-stream" \u2192 recv: "{echo}"')


if __name__ == "__main__":
    main()
