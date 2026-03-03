#!/usr/bin/env python3
"""Shared helpers for Python single-perf runners."""

from __future__ import annotations

import atexit
import os
import socket
import struct
import sys
import time
from pathlib import Path
from typing import Callable, List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parents[4]
PY_SRC_DIR = ROOT / "bindings" / "python" / "src"
if str(PY_SRC_DIR) not in sys.path:
    sys.path.insert(0, str(PY_SRC_DIR))

import zlink


FASTPATH_CEXT = None


def make_cext_send_many_const(*_args, **_kwargs):
    return None


def make_cext_recv_many_into(*_args, **_kwargs):
    return None


def make_cext_send_routed_many_const(*_args, **_kwargs):
    return None


def make_cext_recv_pair_many_into(*_args, **_kwargs):
    return None


def make_cext_recv_pair_drain_into(*_args, **_kwargs):
    return None


def make_cext_gateway_send_many_const(*_args, **_kwargs):
    return None


def make_cext_spot_publish_many_const(*_args, **_kwargs):
    return None


def make_cext_spot_recv_many(*_args, **_kwargs):
    return None


_IPC_ENDPOINTS: set[str] = set()


def _register_ipc_endpoint(endpoint: str) -> None:
    prefix = "ipc://"
    if not endpoint.startswith(prefix):
        return
    ipc_path = endpoint[len(prefix) :]
    if not ipc_path.startswith("/"):
        return
    _IPC_ENDPOINTS.add(ipc_path)
    try:
        os.unlink(ipc_path)
    except FileNotFoundError:
        pass
    except OSError:
        pass


def _cleanup_ipc_endpoints() -> None:
    for ipc_path in list(_IPC_ENDPOINTS):
        try:
            os.unlink(ipc_path)
        except FileNotFoundError:
            pass
        except OSError:
            pass


atexit.register(_cleanup_ipc_endpoints)


def get_port() -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = int(sock.getsockname()[1])
    sock.close()
    return port


def endpoint_for(transport: str, name: str) -> str:
    if transport == "inproc":
        return f"inproc://bench-{name}-{int(time.time() * 1000)}"
    if transport == "ipc":
        endpoint = f"ipc:///tmp/zlink-bench-{name}-{get_port()}.sock"
        _register_ipc_endpoint(endpoint)
        return endpoint
    return f"{transport}://127.0.0.1:{get_port()}"


def _read_int_env(name: str) -> int:
    candidates = [name]
    if name.startswith("PERF_"):
        candidates.append(name[len("PERF_") :])
    else:
        candidates.append(f"PERF_{name}")
    for key in candidates:
        raw = os.environ.get(key)
        if not raw:
            continue
        try:
            parsed = int(raw)
        except ValueError:
            continue
        return parsed
    return 0


def resolve_msg_count(size: int) -> int:
    env_count = _read_int_env("PERF_MSG_COUNT")
    if env_count > 0:
        return env_count
    return 200000 if size <= 1024 else 20000


def parse_env(name: str, default: int) -> int:
    parsed = _read_int_env(name)
    return parsed if parsed > 0 else default


def parse_pattern_args(
    pattern: str, args: Sequence[str]
) -> Optional[Tuple[str, int]]:
    tokens = list(args)
    if len(tokens) >= 3 and tokens[0].upper() == pattern.upper():
        transport = tokens[1]
        size_text = tokens[2]
    elif len(tokens) >= 2:
        transport = tokens[0]
        size_text = tokens[1]
    else:
        return None

    transport = str(transport).strip().lower()
    if transport not in {"tcp", "tls", "ws", "wss", "inproc", "ipc"}:
        return None

    try:
        size = int(size_text)
    except (TypeError, ValueError):
        return None
    if size <= 0:
        return None
    return transport, size


def settle() -> None:
    time.sleep(0.3)


def int_sockopt(value: int) -> bytes:
    return struct.pack("i", int(value))


_ECHO_PATTERNS = set()


def _bandwidth_mbps(pattern: str, throughput: float, size: int) -> float:
    multiplier = 2.0 if pattern.upper() in _ECHO_PATTERNS else 1.0
    return (throughput * float(size) * multiplier) / 1_000_000.0


def print_result(
    pattern: str,
    transport: str,
    size: int,
    throughput: float,
    latency_us: float,
) -> None:
    bandwidth = _bandwidth_mbps(pattern, throughput, size)
    print(f"RESULT,current,{pattern},{transport},{size},throughput,{throughput}")
    print(f"RESULT,current,{pattern},{transport},{size},bandwidth,{bandwidth}")
    print(f"RESULT,current,{pattern},{transport},{size},latency,{latency_us}")


class SocketWaiter:
    def __init__(self, sock) -> None:
        self._poller = zlink.Poller()
        self._poller.add_socket(sock, int(zlink.PollEvent.POLLIN))

    def wait(self, timeout_ms: int) -> bool:
        timeout = -1 if timeout_ms < 0 else timeout_ms
        events = self._poller.poll(timeout)
        return len(events) > 0


def wait_for_input(
    sock, timeout_ms: int, waiter: Optional[SocketWaiter] = None
) -> bool:
    if waiter is None:
        waiter = SocketWaiter(sock)
    timeout = -1 if timeout_ms < 0 else timeout_ms
    return waiter.wait(timeout)


def configure_one_way_socket(sock) -> None:
    hwm = parse_env("PERF_HWM", 100000)
    snd_timeout_ms = parse_env("PERF_SNDTIMEO_MS", 5000)
    rcv_timeout_ms = parse_env("PERF_RCVTIMEO_MS", 5000)
    try:
        sock.setsockopt(int(zlink.SocketOption.SNDHWM), int_sockopt(hwm))
    except Exception:
        pass
    try:
        sock.setsockopt(int(zlink.SocketOption.RCVHWM), int_sockopt(hwm))
    except Exception:
        pass
    try:
        sock.setsockopt(
            int(zlink.SocketOption.SNDTIMEO), int_sockopt(snd_timeout_ms)
        )
    except Exception:
        pass
    try:
        sock.setsockopt(
            int(zlink.SocketOption.RCVTIMEO), int_sockopt(rcv_timeout_ms)
        )
    except Exception:
        pass


def drain_single_part(
    sock, recv_buf: bytearray, expected_max: int, idle_drain_ms: int
) -> int:
    recv_dontwait = int(zlink.ReceiveFlag.DONTWAIT)
    idle_limit = max(1, int(idle_drain_ms))
    recv_count = 0
    idle = 0
    while idle < idle_limit:
        if expected_max > 0 and recv_count >= expected_max:
            break
        try:
            sock.recv_into(recv_buf, recv_dontwait)
            recv_count += 1
            idle = 0
        except Exception:
            idle += 1
            time.sleep(0.001)
    return recv_count


def drain_multipart(
    sock,
    rid_buf: bytearray,
    data_buf: bytearray,
    expected_max: int,
    idle_drain_ms: int,
) -> int:
    recv_dontwait = int(zlink.ReceiveFlag.DONTWAIT)
    idle_limit = max(1, int(idle_drain_ms))
    recv_count = 0
    idle = 0
    while idle < idle_limit:
        if expected_max > 0 and recv_count >= expected_max:
            break
        try:
            sock.recv_into(rid_buf, recv_dontwait)
            sock.recv_into(data_buf, recv_dontwait)
            recv_count += 1
            idle = 0
        except Exception:
            idle += 1
            time.sleep(0.001)
    return recv_count


def gateway_send_with_retry(
    gateway, service: str, parts, flags: int, timeout_ms: int
) -> None:
    deadline = time.time() + (max(1, timeout_ms) / 1000.0)
    while time.time() < deadline:
        try:
            gateway.send(service, parts, flags)
            return
        except Exception:
            time.sleep(0.01)
    raise RuntimeError("timeout")


def spot_recv_with_timeout(spot, timeout_ms: int):
    recv_dontwait = int(zlink.ReceiveFlag.DONTWAIT)
    deadline = time.time() + (max(1, timeout_ms) / 1000.0)
    while time.time() < deadline:
        try:
            return spot.recv(recv_dontwait)
        except Exception:
            time.sleep(0.01)
    raise RuntimeError("timeout")


def recv_exact(sock, size: int, flags: int = 0) -> bytes:
    return sock.recv(size, flags)


_STREAM_DISPATCH_LEN32BE = 0x0001
_STREAM_MAX_FRAME_BYTES = 16 * 1024 * 1024


class StreamLen32BeStash:
    def __init__(self) -> None:
        self._buf = bytearray()
        self._start = 0

    def append(self, data) -> None:
        if data:
            self._buf.extend(data)

    def try_read_into(self, payload_buffer) -> int | None:
        available = len(self._buf) - self._start
        if available < 4:
            return None
        head = self._start
        body_len = struct.unpack("!I", self._buf[head : head + 4])[0]
        if body_len > _STREAM_MAX_FRAME_BYTES:
            self._buf.clear()
            self._start = 0
            return None
        frame_len = 4 + body_len
        if available < frame_len:
            return None
        if body_len > len(payload_buffer):
            raise RuntimeError("len32be payload buffer too small")
        frame_start = head + 4
        frame_end = frame_start + body_len
        if body_len > 0:
            payload_buffer[:body_len] = self._buf[frame_start:frame_end]
        self._start = frame_end
        self._compact()
        return body_len

    def extract_raw_frames(self, chunk) -> List[bytes]:
        self.append(chunk)
        frames: List[bytes] = []
        while (len(self._buf) - self._start) >= 4:
            head = self._start
            body_len = struct.unpack("!I", self._buf[head : head + 4])[0]
            if body_len > _STREAM_MAX_FRAME_BYTES:
                self._buf.clear()
                self._start = 0
                break
            frame_len = 4 + body_len
            if (len(self._buf) - self._start) < frame_len:
                break
            frame_end = head + frame_len
            frames.append(bytes(self._buf[head:frame_end]))
            self._start = frame_end
        self._compact()
        return frames

    def _compact(self) -> None:
        if self._start <= 0:
            return
        if self._start >= len(self._buf):
            self._buf.clear()
            self._start = 0
            return
        if self._start > 4096 and (self._start * 2) >= len(self._buf):
            self._buf = self._buf[self._start :]
            self._start = 0


def encode_len32be_frame(payload: bytes) -> bytes:
    return struct.pack("!I", len(payload)) + payload


class StreamCallbackEcho:
    def __init__(self, sock, len32be_dispatch: bool, echo: bool = True) -> None:
        self._sock = sock
        self._len32be = bool(len32be_dispatch)
        self._echo = bool(echo)
        self._received = 0
        self._stash = StreamLen32BeStash()

    @property
    def received(self) -> int:
        return self._received

    def attach(self) -> None:
        mode = _STREAM_DISPATCH_LEN32BE if self._len32be else 0
        self._sock.stream_attach(self._handler, mode)

    def detach(self) -> None:
        self._sock.stream_detach()

    def close(self) -> None:
        try:
            self.detach()
        except Exception:
            pass

    def _handler(self, rid_view: memoryview, payload_view: memoryview):
        payload_size = len(payload_view)
        if payload_size == 0:
            return 0
        if payload_size == 1 and payload_view[0] in (0x00, 0x01):
            return 0

        if self._echo:
            self._sock.stream_send(rid_view, payload_view)
            return 0

        if self._len32be:
            self._received += 1
            return 0

        frames = self._stash.extract_raw_frames(payload_view)
        self._received += len(frames)
        return 0


def stream_send(sock, rid: bytes, payload: bytes) -> None:
    rc = sock.stream_send(rid, payload)
    if rc < 0:
        raise RuntimeError("stream_send failed")


def stream_wait_peer_routing_id(sock, timeout_ms: int = 5000) -> bytes:
    deadline = time.time() + (max(timeout_ms, 1) / 1000.0)
    while time.time() < deadline:
        rid = sock.stream_peer_routing_id(0)
        if rid is not None and len(rid) == 4:
            return rid
        time.sleep(0.001)
    raise RuntimeError("STREAM peer routing id unavailable")


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
