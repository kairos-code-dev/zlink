import socket
import threading

import zlink


def _stream_port():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def main():
    port = _stream_port()
    endpoint = f"tcp://127.0.0.1:{port}"

    with zlink.Context() as ctx:
        with zlink.StreamSocket(ctx) as server:
            done = threading.Event()
            observed = {}

            def on_message(received):
                observed["routing_id"] = received.routing_id
                observed["parts"] = received.to_bytes_list()
                print(received.routing_id, received.to_bytes_list())
                done.set()

            server.on_receive(on_message)
            server.bind(endpoint)
            with socket.create_connection(("127.0.0.1", port), timeout=3.0) as client:
                client.sendall(b"stream-callback")
                if not done.wait(3.0):
                    raise TimeoutError("stream callback did not receive a message")
                server.send_to(observed["routing_id"], b"stream-callback-reply")
                print(client.recv(64).decode("utf-8"))


if __name__ == "__main__":
    main()
