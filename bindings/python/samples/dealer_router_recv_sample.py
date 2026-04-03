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
            if not (int(event.event) & int(zlink.MonitorEvent.CONNECTION_READY)):
                next_pending.append(monitor)
        pending = next_pending


def main():
    port = _reserve_tcp_port()
    endpoint = f"tcp://127.0.0.1:{port}"

    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            with zlink.DealerSocket(ctx) as dealer:
                with router.open_monitor(zlink.MonitorEvent.CONNECTION_READY) as router_monitor:
                    with dealer.open_monitor(zlink.MonitorEvent.CONNECTION_READY) as dealer_monitor:
                        dealer.set_routing_id(b"CLIENT")
                        router.bind(endpoint)
                        dealer.connect(endpoint)
                        _wait_connected(router_monitor, dealer_monitor)

                dealer.send(b"ping")
                with router.recv() as request:
                    if request.routing_id != zlink.RoutingId(b"CLIENT"):
                        raise AssertionError(f"unexpected routing id: {request.routing_id!r}")
                    if request.to_bytes_list() != [b"ping"]:
                        raise AssertionError("unexpected dealer-router request payload")
                    router.send(b"pong", routing_id=request.routing_id)

                with dealer.recv() as reply:
                    if reply.to_bytes_list() != [b"pong"]:
                        raise AssertionError("unexpected dealer-router reply payload")
                print('[dealer-router/recv] send: "ping" → recv: "pong"')


if __name__ == "__main__":
    main()
