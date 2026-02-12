#!/usr/bin/env python3
import ctypes
import os
import platform
import socket
import struct
import subprocess
import sys
import time
from typing import Callable, Optional, Sequence, Tuple

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
sys.path.insert(0, os.path.join(ROOT, "bindings/python/src"))
import zlink  # noqa: E402
from zlink._ffi import lib as ffi_lib  # noqa: E402


def _python_native_dir() -> str:
    sys_name = platform.system().lower()
    machine = platform.machine().lower()
    py_native_root = os.path.join(ROOT, "bindings", "python", "src", "zlink", "native")
    if "windows" in sys_name:
        arch = "x86_64" if machine in ("x86_64", "amd64") else "aarch64"
        return os.path.join(py_native_root, f"windows-{arch}")
    if "darwin" in sys_name:
        arch = "aarch64" if machine in ("arm64", "aarch64") else "x86_64"
        return os.path.join(py_native_root, f"darwin-{arch}")
    arch = "aarch64" if machine in ("arm64", "aarch64") else "x86_64"
    return os.path.join(py_native_root, f"linux-{arch}")


def _preload_native_for_fastpath() -> None:
    native_dir = _python_native_dir()
    candidates = []
    if os.name == "nt":
        dll = os.path.join(native_dir, "zlink.dll")
        if os.path.exists(dll):
            if hasattr(os, "add_dll_directory"):
                os.add_dll_directory(native_dir)  # type: ignore[attr-defined]
            ctypes.CDLL(dll)
        return
    if platform.system().lower() == "darwin":
        candidates.append(os.path.join(native_dir, "libzlink.dylib"))
    candidates.extend(
        [
            os.path.join(native_dir, "libzlink.so.5"),
            os.path.join(native_dir, "libzlink.so"),
        ]
    )
    for candidate in candidates:
        if not os.path.exists(candidate):
            continue
        try:
            ctypes.CDLL(candidate, mode=getattr(ctypes, "RTLD_GLOBAL", 0))
            return
        except OSError:
            continue


FASTPATH_CEXT = None


def _env_enabled(name: str, default: str = "1") -> bool:
    req = os.environ.get(name, default).strip().lower()
    return req not in ("0", "false", "off", "no")


def _build_fastpath_extension() -> bool:
    setup_py = os.path.join(os.path.dirname(__file__), "setup_fastpath.py")
    if not os.path.exists(setup_py):
        return False
    try:
        subprocess.run(
            [sys.executable, setup_py, "build_ext", "--inplace"],
            cwd=os.path.dirname(__file__),
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        return False
    return True


def _load_fastpath_extension():
    _preload_native_for_fastpath()
    import _zlink_fastpath as cext  # type: ignore
    return cext


_fastpath_on = _env_enabled("BENCH_PY_FASTPATH_CEXT", "1")
_fastpath_build_on = _env_enabled("BENCH_PY_FASTPATH_BUILD", "1")
if _fastpath_on:
    load_exc = None
    try:
        FASTPATH_CEXT = _load_fastpath_extension()
    except Exception as exc:
        load_exc = exc
        FASTPATH_CEXT = None
        if _fastpath_build_on and _build_fastpath_extension():
            try:
                FASTPATH_CEXT = _load_fastpath_extension()
                load_exc = None
            except Exception as exc2:
                load_exc = exc2
                FASTPATH_CEXT = None
    if FASTPATH_CEXT is None and os.environ.get("BENCH_PY_FASTPATH_REQUIRE", "0") == "1":
        raise RuntimeError(f"BENCH_PY_FASTPATH_REQUIRE=1 but C-extension load failed: {load_exc}")


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


def make_raw_send_const(sock, payload: bytes):
    native = ffi_lib()
    send_const = native.zlink_send_const
    handle = sock._handle
    size = len(payload)
    storage = ctypes.create_string_buffer(payload)

    def send(flags: int) -> int:
        rc = send_const(handle, storage, size, int(flags))
        if rc < 0:
            raise RuntimeError("send_const failed")
        return rc

    return send


def make_raw_recv_into(sock, buffer):
    native = ffi_lib()
    recv = native.zlink_recv
    handle = sock._handle
    view = memoryview(buffer)
    if view.readonly:
        raise TypeError("buffer must be writable")
    if view.ndim != 1 or view.format != "B":
        view = view.cast("B")
    size = view.nbytes
    if size <= 0:
        raise ValueError("buffer must not be empty")
    storage = (ctypes.c_char * size).from_buffer(view)

    def recv_into(flags: int) -> int:
        rc = recv(handle, storage, size, int(flags))
        if rc < 0:
            raise RuntimeError("recv failed")
        return rc

    return recv_into


def make_cext_send_many_const(sock, payload: bytes):
    if FASTPATH_CEXT is None:
        return None
    handle = int(sock._handle)
    const_payload = bytes(payload)

    def send_many(count: int, flags: int) -> int:
        return int(FASTPATH_CEXT.send_many_const(handle, const_payload, int(flags), int(count)))

    return send_many


def make_cext_recv_many_into(sock, buffer):
    if FASTPATH_CEXT is None:
        return None
    handle = int(sock._handle)

    def recv_many(count: int, flags: int) -> int:
        return int(FASTPATH_CEXT.recv_many_into(handle, buffer, int(flags), int(count)))

    return recv_many


def make_cext_send_routed_many_const(sock, routing_id: bytes, payload: bytes):
    if FASTPATH_CEXT is None:
        return None
    handle = int(sock._handle)
    rid = bytes(routing_id)
    body = bytes(payload)

    def send_routed_many(count: int, payload_flags: int) -> int:
        return int(
            FASTPATH_CEXT.send_routed_many_const(
                handle, rid, body, int(payload_flags), int(count)
            )
        )

    return send_routed_many


def make_cext_recv_pair_many_into(sock, first_buffer, second_buffer):
    if FASTPATH_CEXT is None:
        return None
    handle = int(sock._handle)

    def recv_pair_many(count: int, flags: int) -> int:
        return int(
            FASTPATH_CEXT.recv_pair_many_into(
                handle, first_buffer, second_buffer, int(flags), int(count)
            )
        )

    return recv_pair_many


def make_cext_recv_pair_drain_into(sock, first_buffer, second_buffer):
    if FASTPATH_CEXT is None:
        return None
    handle = int(sock._handle)

    def recv_pair_drain(max_count: int) -> int:
        return int(
            FASTPATH_CEXT.recv_pair_drain_into(
                handle, first_buffer, second_buffer, int(max_count)
            )
        )

    return recv_pair_drain


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
    send_more = int(zlink.SendFlag.SNDMORE)
    send_none = int(zlink.SendFlag.NONE)
    if isinstance(rid, bytes):
        sock.send_const(rid, send_more)
    else:
        sock.send(rid, send_more)
    if isinstance(payload, bytes):
        sock.send_const(payload, send_none)
    else:
        sock.send(payload, send_none)


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
