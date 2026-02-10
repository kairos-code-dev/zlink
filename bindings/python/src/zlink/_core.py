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
        buf = ctypes.create_string_buffer(data)
        rc = lib().zlink_send(self._handle, buf, len(data), flags)
        if rc < 0:
            _raise_last_error()
        return rc

    def recv(self, size: int, flags: int = 0) -> bytes:
        buf = ctypes.create_string_buffer(size)
        rc = lib().zlink_recv(self._handle, buf, size, flags)
        if rc < 0:
            _raise_last_error()
        return buf.raw[:rc]

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
