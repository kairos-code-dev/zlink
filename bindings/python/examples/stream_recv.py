import socket

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
            server.bind(endpoint)
            with socket.create_connection(("127.0.0.1", port), timeout=3.0) as client:
                client.sendall(b"stream-recv")
                with server.recv() as received:
                    print(received.routing_id, received.to_bytes_list())
                    server.send_to(received.routing_id, b"stream-reply")
                print(client.recv(64).decode("utf-8"))


if __name__ == "__main__":
    main()
