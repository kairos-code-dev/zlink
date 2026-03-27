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
        with zlink.RouterSocket(ctx) as router:
            with zlink.DealerSocket(ctx) as dealer:
                done = threading.Event()

                def on_request(received):
                    print(received.routing_id, received.to_bytes_list())
                    done.set()

                endpoint = _tcp_endpoint()
                dealer.set_routing_id(b"CLIENT")
                router.bind(endpoint)
                dealer.connect(endpoint)
                router.on_receive(on_request)

                dealer.send(b"ping")
                if not done.wait(3.0):
                    raise TimeoutError("dealer/router callback did not receive a message")


if __name__ == "__main__":
    main()
