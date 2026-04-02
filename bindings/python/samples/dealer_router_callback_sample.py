import socket as _socket
import threading
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
        with zlink.RouterSocket(ctx) as router:
            with zlink.DealerSocket(ctx) as dealer:
                with router.open_monitor(zlink.MonitorEvent.CONNECTION_READY_CHANGED) as router_monitor:
                    with dealer.open_monitor(zlink.MonitorEvent.CONNECTION_READY_CHANGED) as dealer_monitor:
                        dealer.set_routing_id(b"CLIENT")
                        router.bind(endpoint)
                        dealer.connect(endpoint)
                        _wait_connected(router_monitor, dealer_monitor)

                request_done = threading.Event()
                reply_done = threading.Event()
                observed = {}

                def on_request(received):
                    observed["request_routing_id"] = received.routing_id
                    observed["request_payload"] = received.to_bytes_list()
                    request_done.set()

                def on_reply(received):
                    observed["reply_payload"] = received.to_bytes_list()
                    reply_done.set()

                router.on_receive(on_request)
                dealer.on_receive(on_reply)

                dealer.send(b"ping")
                if not request_done.wait(3.0):
                    raise TimeoutError("router callback did not receive a request")
                if observed["request_routing_id"] != zlink.RoutingId(b"CLIENT"):
                    raise AssertionError("unexpected dealer-router callback routing id")
                if observed["request_payload"] != [b"ping"]:
                    raise AssertionError("unexpected dealer-router callback request payload")

                router.send(b"pong", routing_id=observed["request_routing_id"])
                if not reply_done.wait(3.0):
                    raise TimeoutError("dealer callback did not receive a reply")
                if observed["reply_payload"] != [b"pong"]:
                    raise AssertionError("unexpected dealer-router callback reply payload")
                print('[dealer-router/callback] send: "ping" → recv: "pong"')


if __name__ == "__main__":
    main()
