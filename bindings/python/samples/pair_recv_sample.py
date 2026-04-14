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

                client.send(b"hello-pair")
                with server.recv() as received:
                    payload = received.to_bytes_list()
                    if payload != [b"hello-pair"]:
                        raise AssertionError(f"unexpected pair payload: {payload!r}")
                print('[pair/recv] send: "hello-pair" → recv: "hello-pair"')


if __name__ == "__main__":
    main()
