import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import threading
import zlink
from sample_common import tcp_endpoint, wait_connected


def main():
    port, endpoint = tcp_endpoint()
    with zlink.Context() as ctx:
        with zlink.PairSocket(ctx) as server:
            with zlink.PairSocket(ctx) as client:
                with server.open_monitor(zlink.MonitorEvent.CONNECTION_READY) as srv_mon:
                    with client.open_monitor(zlink.MonitorEvent.CONNECTION_READY) as cli_mon:
                        server.bind(endpoint)
                        client.connect(endpoint)
                        wait_connected(srv_mon, cli_mon)

                done = threading.Event()
                result = {}

                def on_message(received):
                    result["data"] = received.to_bytes_list()[0].decode("utf-8")
                    done.set()

                server.on_receive(on_message)
                client.send(b"hello-pair")
                if not done.wait(3.0):
                    raise TimeoutError("pair callback did not receive a message")
                print(f'[pair/callback] send: "hello-pair" \u2192 recv: "{result["data"]}"')


if __name__ == "__main__":
    main()
