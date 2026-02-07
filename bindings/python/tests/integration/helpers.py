import socket
import time

import zlink

ZLINK_PAIR = 0
ZLINK_PUB = 1
ZLINK_SUB = 2
ZLINK_DEALER = 5
ZLINK_ROUTER = 6
ZLINK_XPUB = 9
ZLINK_XSUB = 10

ZLINK_SUBSCRIBE = 6
ZLINK_XPUB_VERBOSE = 40
ZLINK_DONTWAIT = 1
ZLINK_SNDMORE = 2


def get_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def transports(prefix):
    return [
        ("tcp", ""),
        ("ws", ""),
        ("inproc", f"inproc://{prefix}-{int(time.time() * 1000)}"),
    ]


def endpoint_for(name, base_endpoint, suffix):
    if name == "inproc":
        return base_endpoint + suffix
    port = get_port()
    return f"{name}://127.0.0.1:{port}"


def try_transport(name, fn):
    try:
        fn()
    except Exception:
        if name == "ws":
            return
        raise


def recv_with_timeout(sock, size, timeout_ms):
    deadline = time.time() + (timeout_ms / 1000.0)
    last = None
    while time.time() < deadline:
        try:
            return sock.recv(size, ZLINK_DONTWAIT)
        except zlink.ZlinkError as exc:
            last = exc
            time.sleep(0.01)
    if last:
        raise last
    raise RuntimeError("timeout")


def send_with_retry(sock, data, flags, timeout_ms):
    deadline = time.time() + (timeout_ms / 1000.0)
    last = None
    while time.time() < deadline:
        try:
            sock.send(data, flags)
            return
        except zlink.ZlinkError as exc:
            last = exc
            time.sleep(0.01)
    if last:
        raise last
    raise RuntimeError("timeout")


def gateway_send_with_retry(gw, service, parts, flags, timeout_ms):
    deadline = time.time() + (timeout_ms / 1000.0)
    last = None
    while time.time() < deadline:
        try:
            gw.send(service, parts, flags)
            return
        except zlink.ZlinkError as exc:
            last = exc
            time.sleep(0.01)
    if last:
        raise last
    raise RuntimeError("timeout")


def wait_until(fn, timeout_ms, interval_ms=10):
    deadline = time.time() + (timeout_ms / 1000.0)
    while time.time() < deadline:
        try:
            if fn():
                return True
        except zlink.ZlinkError:
            pass
        time.sleep(interval_ms / 1000.0)
    return False


def gateway_recv_with_timeout(gw, timeout_ms):
    deadline = time.time() + (timeout_ms / 1000.0)
    last = None
    while time.time() < deadline:
        try:
            return gw.recv(ZLINK_DONTWAIT)
        except zlink.ZlinkError as exc:
            last = exc
            time.sleep(0.01)
    if last:
        raise last
    raise RuntimeError("timeout")


def spot_recv_with_timeout(spot, timeout_ms):
    deadline = time.time() + (timeout_ms / 1000.0)
    last = None
    while time.time() < deadline:
        try:
            return spot.recv(ZLINK_DONTWAIT)
        except zlink.ZlinkError as exc:
            last = exc
            time.sleep(0.01)
    if last:
        raise last
    raise RuntimeError("timeout")
