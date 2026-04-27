import socket as _socket
import threading
import time

import zlink


_MONITOR_POLL_SLEEP_S = 0.005
_MONITOR_WAIT = threading.Event()


def tcp_endpoint():
    sock = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port, f"tcp://127.0.0.1:{port}"


def _poll_monitor_event(monitor, timeout_ms):
    deadline = time.monotonic() + (timeout_ms / 1000.0)
    while time.monotonic() < deadline:
        if monitor.snapshot().is_ready():
            return monitor.recv()
        _MONITOR_WAIT.wait(_MONITOR_POLL_SLEEP_S)
    return None


def wait_connected(*monitors, timeout_ms=5000):
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
            if not (int(event.event) & int(zlink.MonitorEventMask.CONNECTION_READY)):
                next_pending.append(monitor)
        pending = next_pending


def wait_socket_monitor_event(monitor, expected_event, timeout_ms=5000):
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


def wait_until(predicate, timeout_ms=5000, description="condition"):
    deadline = time.monotonic() + (timeout_ms / 1000.0)
    waiter = threading.Event()
    while time.monotonic() < deadline:
        if predicate():
            return
        waiter.wait(0.001)
    raise TimeoutError(f"timed out waiting for {description}")


def wait_spot_peer_connected(node, timeout_ms=5000):
    wait_until(
        lambda: node.status_snapshot().connected_peer_count >= 1,
        timeout_ms=timeout_ms,
        description="spot peer connection",
    )
