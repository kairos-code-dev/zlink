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
            def on_message(received):
                print(received.routing_id, received.to_bytes_list())

            server.on_receive(on_message)
            server.bind(endpoint)
            print("stream callback surface prepared", endpoint)


if __name__ == "__main__":
    main()
