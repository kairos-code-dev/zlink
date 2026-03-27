import socket
import threading

import zlink


def _tcp_endpoint():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return f"tcp://127.0.0.1:{port}"


def main():
    with zlink.Context() as ctx:
        with zlink.PairSocket(ctx) as server:
            with zlink.PairSocket(ctx) as client:
                done = threading.Event()

                def on_message(received):
                    print(received.to_bytes_list()[0].decode("utf-8"))
                    done.set()

                endpoint = _tcp_endpoint()
                server.bind(endpoint)
                client.connect(endpoint)
                server.on_receive(on_message)

                client.send(b"hello from callback")
                if not done.wait(3.0):
                    raise TimeoutError("pair callback did not receive a message")


if __name__ == "__main__":
    main()
