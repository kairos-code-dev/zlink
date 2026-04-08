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
        with zlink.XPubSocket(ctx) as publisher:
            with zlink.SubSocket(ctx) as subscriber:
                with publisher.monitor_open(zlink.MonitorEvent.CONNECTION_READY) as publisher_monitor:
                    with subscriber.monitor_open(zlink.MonitorEvent.CONNECTION_READY) as subscriber_monitor:
                        publisher.bind(endpoint)
                        subscriber.connect(endpoint)
                        subscriber.set_subscription(b"prices")
                        _wait_connected(publisher_monitor, subscriber_monitor)

                event = publisher.receive_subscription_event()
                if not event.subscribed or event.topic != b"prices":
                    raise AssertionError("unexpected subscription event")
                publisher.publish(b"prices", b"101.25")
                with subscriber.subscribe() as received:
                    if received.topic != b"prices":
                        raise AssertionError(f"unexpected pubsub topic: {received.topic!r}")
                    if received.to_bytes_list() != [b"101.25"]:
                        raise AssertionError("unexpected pubsub payload")
                print('[pubsub/recv] publish: "prices/101.25" → subscribe: "prices/101.25"')


if __name__ == "__main__":
    main()
