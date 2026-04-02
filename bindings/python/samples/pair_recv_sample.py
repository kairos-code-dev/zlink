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


def _wait_connected(*monitors, timeout_ms=5000):
    pending = list(monitors)
    deadline = time.monotonic() + (timeout_ms / 1000.0)
    while pending:
        remaining_ms = int((deadline - time.monotonic()) * 1000)
        if remaining_ms <= 0:
            raise TimeoutError("connection handshake did not complete")
        next_pending = []
        for monitor in pending:
            event = _poll_monitor_event(monitor, remaining_ms)
            if event is None:
                next_pending.append(monitor)
                continue
            if not (int(event.event) & int(zlink.MonitorEvent.CONNECTION_READY_CHANGED)):
                next_pending.append(monitor)
        pending = next_pending


def main():
    port = _reserve_tcp_port()
    endpoint = f"tcp://127.0.0.1:{port}"

    with zlink.Context() as ctx:
        with zlink.PairSocket(ctx) as server:
            with zlink.PairSocket(ctx) as client:
                with server.open_monitor(zlink.MonitorEvent.CONNECTION_READY_CHANGED) as server_monitor:
                    with client.open_monitor(zlink.MonitorEvent.CONNECTION_READY_CHANGED) as client_monitor:
                        server.bind(endpoint)
                        client.connect(endpoint)
                        _wait_connected(server_monitor, client_monitor)

                client.send(b"hello-pair")
                with server.recv() as received:
                    payload = received.to_bytes_list()
                    if payload != [b"hello-pair"]:
                        raise AssertionError(f"unexpected pair payload: {payload!r}")
                print('[pair/recv] send: "hello-pair" → recv: "hello-pair"')


if __name__ == "__main__":
    main()
