import socket as _socket
import time

import zlink


def _reserve_tcp_port():
    sock = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def _poll_monitor_event(monitor, timeout_ms):
    with zlink.Poller() as poller:
        poller.add_socket(monitor, zlink.PollEvent.POLLIN)
        ready = poller.poll(timeout_ms)
    if not ready:
        return None
    return monitor.recv()


def _wait_socket_monitor_event(monitor, expected_event, timeout_ms=5000):
    deadline = time.monotonic() + (timeout_ms / 1000.0)
    expected_value = int(expected_event)
    while True:
        remaining_ms = int((deadline - time.monotonic()) * 1000)
        if remaining_ms <= 0:
            raise TimeoutError("socket monitor did not produce the expected event")
        event = _poll_monitor_event(monitor, remaining_ms)
        if event is None:
            continue
        if int(event.event) & expected_value:
            return event


def main():
    port = _reserve_tcp_port()
    endpoint = f"tcp://127.0.0.1:{port}"

    with zlink.Context() as ctx:
        with zlink.PairSocket(ctx) as server:
            with zlink.PairSocket(ctx) as client:
                with server.open_monitor(zlink.MonitorEvent.CONNECTION_READY) as server_monitor:
                    with client.open_monitor(zlink.MonitorEvent.CONNECTION_READY) as client_monitor:
                        if server_monitor.try_recv() is not None:
                            raise AssertionError("monitor sample expected empty server try_recv")
                        if client_monitor.try_recv() is not None:
                            raise AssertionError("monitor sample expected empty client try_recv")
                        server.bind(endpoint)
                        client.connect(endpoint)
                        server_event = _wait_socket_monitor_event(
                            server_monitor,
                            zlink.MonitorEvent.CONNECTION_READY,
                        )
                        client_event = _wait_socket_monitor_event(
                            client_monitor,
                            zlink.MonitorEvent.CONNECTION_READY,
                        )
                        if not (int(server_event.event) & int(zlink.MonitorEvent.CONNECTION_READY)):
                            raise AssertionError("monitor sample expected server CONNECTION_READY")
                        if not (int(client_event.event) & int(zlink.MonitorEvent.CONNECTION_READY)):
                            raise AssertionError("monitor sample expected client CONNECTION_READY")
                print('[monitor/recv] recv: "connection-ready" → tryRecv: empty')


if __name__ == "__main__":
    main()
