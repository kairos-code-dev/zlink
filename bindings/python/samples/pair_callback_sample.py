import threading

import zlink
from sample_support import tcp_endpoint, wait_connected


def main():
    _, endpoint = tcp_endpoint()

    with zlink.Context() as ctx:
        with zlink.PairSocket(ctx) as server:
            with zlink.PairSocket(ctx) as client:
                with server.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as server_monitor:
                    with client.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as client_monitor:
                        server.bind(endpoint)
                        client.connect(endpoint)
                        wait_connected(server_monitor, client_monitor)

                done = threading.Event()
                observed = {}

                def on_ready(_sock):
                    done.set()

                server.on_send_ready(on_ready)
                client.send(b"hello-pair")
                if not done.wait(3.0):
                    raise TimeoutError("pair send-ready callback did not fire")
                with server.recv() as received:
                    observed["payload"] = received.to_bytes_list()
                if observed["payload"] != [b"hello-pair"]:
                    raise AssertionError(f"unexpected pair payload: {observed['payload']!r}")
                print('[pair/callback] send-ready + recv: "hello-pair"')


if __name__ == "__main__":
    main()
