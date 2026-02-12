# SPDX-License-Identifier: MPL-2.0

import ctypes
from ._ffi import lib


class ZlinkError(RuntimeError):
    def __init__(self, errno, message):
        super().__init__(message)
        self.errno = errno


def _raise_last_error():
    L = lib()
    err = L.zlink_errno()
    msg = L.zlink_strerror(err)
    if msg:
        message = msg.decode("utf-8", errors="replace")
    else:
        message = "zlink error"
    raise ZlinkError(err, message)


def _as_bytes_view(data):
    if isinstance(data, bytes):
        return memoryview(data)
    try:
        view = memoryview(data)
    except TypeError as exc:
        raise TypeError("data must support the buffer protocol") from exc
    if view.ndim != 1 or view.format != "B":
        try:
            view = view.cast("B")
        except TypeError:
            view = memoryview(bytes(view))
    if not view.c_contiguous:
        view = memoryview(bytes(view))
    return view


def _send_buffer(data):
    if isinstance(data, bytes):
        size = len(data)
        if size == 0:
            return None, 0, None
        return data, size, None
    view = _as_bytes_view(data)
    size = view.nbytes
    if size == 0:
        return None, 0, None
    if view.readonly:
        # bytes are accepted directly for c_void_p parameters.
        raw = view.tobytes()
        return raw, size, None
    return (ctypes.c_char * size).from_buffer(view), size, view


class Context:
    def __init__(self):
        self._handle = lib().zlink_ctx_new()
        if not self._handle:
            _raise_last_error()

    def set(self, option, value):
        rc = lib().zlink_ctx_set(self._handle, int(option), int(value))
        if rc != 0:
            _raise_last_error()

    def get(self, option):
        rc = lib().zlink_ctx_get(self._handle, int(option))
        if rc < 0:
            _raise_last_error()
        return rc

    def shutdown(self):
        rc = lib().zlink_ctx_shutdown(self._handle)
        if rc != 0:
            _raise_last_error()

    def close(self):
        if self._handle:
            lib().zlink_ctx_term(self._handle)
            self._handle = None


class Socket:
    def __init__(self, context, sock_type):
        self._handle = lib().zlink_socket(context._handle, int(sock_type))
        if not self._handle:
            _raise_last_error()
        self._own = True

    @classmethod
    def _from_handle(cls, handle, own=False):
        obj = cls.__new__(cls)
        obj._handle = handle
        obj._own = own
        return obj

    def bind(self, endpoint: str):
        rc = lib().zlink_bind(self._handle, endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def connect(self, endpoint: str):
        rc = lib().zlink_connect(self._handle, endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def unbind(self, endpoint: str):
        rc = lib().zlink_unbind(self._handle, endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def disconnect(self, endpoint: str):
        rc = lib().zlink_disconnect(self._handle, endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def send(self, data: bytes, flags: int = 0):
        buf, size, keepalive = _send_buffer(data)
        rc = lib().zlink_send(self._handle, buf, size, flags)
        if rc < 0:
            _raise_last_error()
        # Keep the backing object alive until native call returns.
        _ = keepalive
        return rc

    def send_const(self, data: bytes, flags: int = 0):
        buf, size, keepalive = _send_buffer(data)
        rc = lib().zlink_send_const(self._handle, buf, size, flags)
        if rc < 0:
            _raise_last_error()
        _ = keepalive
        return rc

    def recv(self, size: int, flags: int = 0) -> bytes:
        buf = ctypes.create_string_buffer(size)
        rc = lib().zlink_recv(self._handle, buf, size, flags)
        if rc < 0:
            _raise_last_error()
        return buf.raw[:rc]

    def recv_into(self, buffer, flags: int = 0):
        view = _as_bytes_view(buffer)
        if view.readonly:
            raise TypeError("buffer must be writable")
        size = view.nbytes
        if size <= 0:
            raise ValueError("buffer must not be empty")
        buf = (ctypes.c_char * size).from_buffer(view)
        rc = lib().zlink_recv(self._handle, buf, size, flags)
        if rc < 0:
            _raise_last_error()
        return rc

    def setsockopt(self, option: int, value: bytes):
        buf = ctypes.create_string_buffer(value)
        rc = lib().zlink_setsockopt(self._handle, option, buf, len(value))
        if rc != 0:
            _raise_last_error()

    def getsockopt(self, option: int, size: int = 256) -> bytes:
        buf = ctypes.create_string_buffer(size)
        sz = ctypes.c_size_t(size)
        rc = lib().zlink_getsockopt(self._handle, option, buf, ctypes.byref(sz))
        if rc != 0:
            _raise_last_error()
        return buf.raw[: sz.value]

    def close(self):
        if self._handle and self._own:
            lib().zlink_close(self._handle)
        self._handle = None


class ZlinkMsg(ctypes.Structure):
    _fields_ = [("data", ctypes.c_ubyte * 64)]


class Message:
    def __init__(self, size: int | None = None):
        self._msg = ZlinkMsg()
        if size is None:
            rc = lib().zlink_msg_init(ctypes.byref(self._msg))
        else:
            rc = lib().zlink_msg_init_size(ctypes.byref(self._msg), size)
        if rc != 0:
            _raise_last_error()
        self._valid = True

    @staticmethod
    def from_bytes(data: bytes):
        msg = Message(len(data))
        ptr = lib().zlink_msg_data(ctypes.byref(msg._msg))
        if ptr and data:
            ctypes.memmove(ptr, data, len(data))
        return msg

    def size(self):
        return lib().zlink_msg_size(ctypes.byref(self._msg))

    def data(self):
        ptr = lib().zlink_msg_data(ctypes.byref(self._msg))
        size = self.size()
        if not ptr or size == 0:
            return b""
        return ctypes.string_at(ptr, size)

    def send(self, socket, flags: int = 0):
        rc = lib().zlink_msg_send(ctypes.byref(self._msg), socket._handle, flags)
        if rc < 0:
            _raise_last_error()
        self._valid = False

    def recv(self, socket, flags: int = 0):
        rc = lib().zlink_msg_recv(ctypes.byref(self._msg), socket._handle, flags)
        if rc < 0:
            _raise_last_error()
        self._valid = True

    def close(self):
        if self._valid:
            lib().zlink_msg_close(ctypes.byref(self._msg))
            self._valid = False
