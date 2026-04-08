import socket as _socket

import zlink


def _reserve_tcp_port():
    sock = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port

def main():
    port = _reserve_tcp_port()
    endpoint = f"tcp://127.0.0.1:{port}"

    with zlink.Context() as ctx:
        with zlink.PairSocket(ctx) as server:
            with zlink.PairSocket(ctx) as client:
                with server.monitor_open(zlink.MonitorEvent.CONNECTION_READY) as server_monitor:
                    with client.monitor_open(zlink.MonitorEvent.CONNECTION_READY) as client_monitor:
                        if server_monitor.try_recv() is not None:
                            raise AssertionError("monitor sample expected empty server try_recv")
                        if client_monitor.try_recv() is not None:
                            raise AssertionError("monitor sample expected empty client try_recv")
                        server.bind(endpoint)
                        client.connect(endpoint)
                        server_event = server_monitor.recv()
                        client_event = client_monitor.recv()
                        if not (int(server_event.event) & int(zlink.MonitorEvent.CONNECTION_READY)):
                            raise AssertionError("monitor sample expected server CONNECTION_READY")
                        if not (int(client_event.event) & int(zlink.MonitorEvent.CONNECTION_READY)):
                            raise AssertionError("monitor sample expected client CONNECTION_READY")
                print('[monitor/recv] recv: "connection-ready" → tryRecv: empty')


if __name__ == "__main__":
    main()
