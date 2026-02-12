#!/usr/bin/env python3
import os
import socket
import struct
import sys
import time
from typing import Callable, Optional, Sequence, Tuple

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
sys.path.insert(0, os.path.join(ROOT, "bindings/python/src"))
import zlink  # noqa: E402


def get_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def endpoint_for(transport: str, name: str) -> str:
    if transport == "inproc":
        return f"inproc://bench-{name}-{int(time.time() * 1000)}"
    return f"{transport}://127.0.0.1:{get_port()}"


def resolve_msg_count(size: int) -> int:
    env = os.environ.get("BENCH_MSG_COUNT")
    if env and env.isdigit() and int(env) > 0:
        return int(env)
    return 200000 if size <= 1024 else 20000


def parse_env(name: str, default: int) -> int:
    value = os.environ.get(name)
    if not value:
        return default
    try:
        parsed = int(value)
    except ValueError:
        return default
    return parsed if parsed > 0 else default


def settle() -> None:
    time.sleep(0.3)


def int_sockopt(value: int) -> bytes:
    return struct.pack("i", int(value))


def print_result(pattern: str, transport: str, size: int, throughput: float, latency_us: float) -> None:
    print(f"RESULT,current,{pattern},{transport},{size},throughput,{throughput}")
    print(f"RESULT,current,{pattern},{transport},{size},latency,{latency_us}")


class SocketWaiter:
    def __init__(self, sock) -> None:
        self._poller = zlink.Poller()
        self._poller.add_socket(sock, int(zlink.PollEvent.POLLIN))

    def wait(self, timeout_ms: int) -> bool:
        timeout = -1 if timeout_ms < 0 else timeout_ms
        events = self._poller.poll(timeout)
        return len(events) > 0


def wait_for_input(sock, timeout_ms: int, waiter: Optional[SocketWaiter] = None) -> bool:
    if waiter is None:
        waiter = SocketWaiter(sock)
    timeout = -1 if timeout_ms < 0 else timeout_ms
    return waiter.wait(timeout)


def recv_exact(sock, size: int, flags: int = 0) -> bytes:
    return sock.recv(size, flags)


def stream_expect_connect_event(sock) -> bytes:
    rid = bytearray(256)
    payload = bytearray(16)
    for _ in range(64):
        rid_len = sock.recv_into(rid, int(zlink.ReceiveFlag.NONE))
        payload_len = sock.recv_into(payload, int(zlink.ReceiveFlag.NONE))
        if payload_len == 1 and payload[0] == 0x01:
            return bytes(memoryview(rid)[:rid_len])
    raise RuntimeError("invalid STREAM connect event")


def stream_send(sock, rid: bytes, payload: bytes) -> None:
    sock.send(rid, int(zlink.SendFlag.SNDMORE))
    sock.send(payload, int(zlink.SendFlag.NONE))


def stream_recv(sock, max_size: int):
    rid = bytearray(256)
    data = bytearray(max(1, max_size))
    rid_len = sock.recv_into(rid, int(zlink.ReceiveFlag.NONE))
    data_len = sock.recv_into(data, int(zlink.ReceiveFlag.NONE))
    return bytes(memoryview(rid)[:rid_len]), bytes(memoryview(data)[:data_len])


def stream_recv_into(sock, rid_buffer, data_buffer):
    rid_len = sock.recv_into(rid_buffer, int(zlink.ReceiveFlag.NONE))
    data_len = sock.recv_into(data_buffer, int(zlink.ReceiveFlag.NONE))
    return rid_len, data_len


def wait_until(fn: Callable[[], bool], timeout_ms: int, interval_ms: int = 10) -> bool:
    deadline = time.time() + (timeout_ms / 1000.0)
    while time.time() < deadline:
        try:
            if fn():
                return True
        except Exception:
            pass
        time.sleep(interval_ms / 1000.0)
    return False


def recv_with_timeout(sock, size: int, timeout_ms: int, waiter: Optional[SocketWaiter] = None) -> bytes:
    if waiter is None:
        waiter = SocketWaiter(sock)
    if not waiter.wait(timeout_ms):
        raise RuntimeError("timeout")
    return sock.recv(size, int(zlink.ReceiveFlag.NONE))


def recv_into_with_timeout(sock, buffer, timeout_ms: int, waiter: Optional[SocketWaiter] = None) -> int:
    if waiter is None:
        waiter = SocketWaiter(sock)
    if not waiter.wait(timeout_ms):
        raise RuntimeError("timeout")
    return sock.recv_into(buffer, int(zlink.ReceiveFlag.NONE))


def gateway_send_with_retry(gateway, service: str, parts, flags: int, timeout_ms: int) -> None:
    deadline = time.time() + (timeout_ms / 1000.0)
    while time.time() < deadline:
        try:
            gateway.send(service, parts, flags)
            return
        except Exception:
            time.sleep(0.01)
    raise RuntimeError("timeout")


def spot_recv_with_timeout(spot, timeout_ms: int, waiter: Optional[SocketWaiter] = None):
    _ = waiter
    deadline = time.time() + (timeout_ms / 1000.0)
    while time.time() < deadline:
        try:
            return spot.recv(int(zlink.ReceiveFlag.DONTWAIT))
        except Exception:
            time.sleep(0.001)
    raise RuntimeError("timeout")


def parse_pattern_args(default_pattern: str, argv: Sequence[str]) -> Optional[Tuple[str, int]]:
    args = list(argv)
    if len(args) >= 3 and args[0].upper() == default_pattern:
        args = args[1:]
    if len(args) < 2:
        return None
    try:
        size = int(args[1])
    except ValueError:
        return None
    return args[0], size
