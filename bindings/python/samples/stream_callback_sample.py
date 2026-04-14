import socket
import socket as _socket
import threading
import time

import zlink


def _tcp_endpoint():
    sock = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port, f"tcp://127.0.0.1:{port}"


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
    port, endpoint = _tcp_endpoint()

    with zlink.Context() as ctx:
        with zlink.StreamSocket(ctx) as server:
            with server.monitor_open(zlink.MonitorEventMask.ACCEPTED) as server_monitor:
                done = threading.Event()
                observed = {}

                def on_message(received):
                    observed["payload"] = received.to_bytes_list()
                    observed["routing_id"] = received.routing_id
                    done.set()

                server.bind(endpoint)
                server.on_receive(on_message)
                with socket.create_connection(("127.0.0.1", port), timeout=3.0) as client:
                    _wait_socket_monitor_event(server_monitor, zlink.MonitorEventMask.ACCEPTED)
                    client.sendall(b"hello-stream")
                    if not done.wait(3.0):
                        raise TimeoutError("stream callback did not receive a message")
                    if observed["payload"] != [b"hello-stream"]:
                        raise AssertionError("unexpected stream callback payload")
                    if not observed["routing_id"]:
                        raise AssertionError("stream callback expected a routing id")
                    server.send(b"hello-stream", routing_id=observed["routing_id"])
                    reply = client.recv(64)
                    if reply != b"hello-stream":
                        raise AssertionError(f"unexpected stream callback reply: {reply!r}")
            print('[stream/callback] send: "hello-stream" → recv: "hello-stream"')


if __name__ == "__main__":
    main()
