import socket as _socket
import time

import zlink
from sample_support import wait_until


def _reserve_tcp_port():
    sock = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def _poll_service_event(monitor, timeout_ms):
    with zlink.Poller() as poller:
        poller.add_socket(monitor, zlink.PollEvent.POLLIN)
        ready = poller.poll(timeout_ms)
    if not ready:
        return None
    return monitor.recv()


def _wait_service_event_type(monitor, expected_event_type, timeout_ms=5000):
    deadline = time.monotonic() + (timeout_ms / 1000.0)
    expected_value = int(expected_event_type)
    while time.monotonic() < deadline:
        remaining_ms = max(1, int((deadline - time.monotonic()) * 1000))
        event = _poll_service_event(monitor, remaining_ms)
        if event is None:
            continue
        if int(event.event_type) == expected_value:
            return event
    raise TimeoutError("timed out waiting for spot filter event")


def main():
    port = _reserve_tcp_port()
    endpoint = f"tcp://127.0.0.1:{port}"

    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as node:
            with zlink.Spot(node) as spot:
                node.bind(endpoint)
                node.connect_peer(endpoint)
                wait_until(
                    lambda: node.status_snapshot().connected_peer_count >= 1,
                    description="spot connection readiness",
                )

                spot.on_send_ready(lambda _: None)
                with spot.open_monitor(
                    zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED
                ) as monitor:
                    spot.set_subscription(b"room:lobby")
                    _wait_service_event_type(
                        monitor,
                        zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED,
                    )

                spot.publish(b"room:lobby", [b"hello-spot"])
                with spot.subscribe() as received:
                    if received.topic != b"room:lobby":
                        raise AssertionError(f"unexpected spot topic: {received.topic!r}")
                    if received.to_bytes_list() != [b"hello-spot"]:
                        raise AssertionError("unexpected spot payload")
                print('[spot/recv] publish: "room:lobby/hello-spot" → subscribe: "room:lobby/hello-spot"')


if __name__ == "__main__":
    main()
