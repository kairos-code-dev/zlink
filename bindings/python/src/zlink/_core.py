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


_STREAM_MSG_SIZE = 64
_STREAM_DISPATCH_LEN32BE = 0x0001


class _ZlinkRoutingId(ctypes.Structure):
    _fields_ = [
        ("size", ctypes.c_ubyte),
        ("data", ctypes.c_ubyte * 255),
    ]


_STREAM_PACKETS_CB_T = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_void_p,
)

_STREAM_RAW_CB_T = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_void_p,
)


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
        self._handle = lib().zlink_socket(context._handle, int(sock_type), None)
        if not self._handle:
            _raise_last_error()
        self._own = True
        self._stream_handler = None
        self._stream_callback = None
        self._stream_attached = False

    @classmethod
    def _from_handle(cls, handle, own=False):
        obj = cls.__new__(cls)
        obj._handle = handle
        obj._own = own
        obj._stream_handler = None
        obj._stream_callback = None
        obj._stream_attached = False
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

    def stream_attach(self, handler, mode: int = 0):
        if handler is None or not callable(handler):
            raise TypeError("handler must be callable")
        if self._stream_attached:
            raise RuntimeError("STREAM callback already attached")

        mode = int(mode)
        if mode == _STREAM_DISPATCH_LEN32BE:
            callback = self._build_stream_packets_callback(handler)
            rc = lib().zlink_stream_attach_len32be(self._handle, callback)
        elif mode == 0:
            callback = self._build_stream_raw_callback(handler)
            rc = lib().zlink_stream_attach_raw(self._handle, callback, None)
        else:
            raise ValueError("mode must be 0 (raw) or 1 (len32be)")
        if rc != 0:
            _raise_last_error()

        self._stream_handler = handler
        self._stream_callback = callback
        self._stream_attached = True

    def stream_attach_raw(self, handler):
        self.stream_attach(handler, 0)

    def stream_attach_len32be(self, handler):
        self.stream_attach(handler, _STREAM_DISPATCH_LEN32BE)

    def stream_detach(self):
        if not self._stream_attached:
            return
        rc = lib().zlink_stream_detach(self._handle)
        self._stream_attached = False
        self._stream_handler = None
        self._stream_callback = None
        if rc != 0:
            _raise_last_error()

    def stream_peer_routing_id(self, index: int = 0):
        rid = _ZlinkRoutingId()
        rc = lib().zlink_socket_peer_routing_id(
            self._handle, int(index), ctypes.byref(rid)
        )
        if rc != 0 or rid.size == 0:
            return None
        return bytes(rid.data[: rid.size])

    def stream_send(self, routing_id, payload, flags: int = 0):
        rid_view = _as_bytes_view(routing_id)
        rid_len = rid_view.nbytes
        if rid_len <= 0 or rid_len > 255:
            raise ValueError("routing_id length must be between 1 and 255")

        payload_buf, payload_size, keepalive = _send_buffer(payload)

        rid = _ZlinkRoutingId()
        rid.size = rid_len
        for i in range(rid_len):
            rid.data[i] = rid_view[i]

        rc = lib().zlink_stream_send(self._handle, ctypes.byref(rid),
                                     payload_buf, payload_size, int(flags))
        if rc < 0:
            _raise_last_error()
        _ = keepalive
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

    @staticmethod
    def _stream_callback_result(value):
        if not value:
            return 0
        try:
            return int(value)
        except Exception:
            return 1

    @staticmethod
    def _routing_id_view(rid_ptr):
        rid = ctypes.cast(rid_ptr, ctypes.POINTER(_ZlinkRoutingId)).contents
        rid_size = int(rid.size)
        if rid_size < 0:
            rid_size = 0
        if rid_size > 255:
            rid_size = 255
        return memoryview(bytes(rid.data[:rid_size]))

    def _build_stream_packets_callback(self, handler):
        @_STREAM_PACKETS_CB_T
        def _on_packets(rid_ptr, msgs_ptr, msg_count, userdata_):
            if rid_ptr is None or msgs_ptr is None:
                return 0

            rid_view = self._routing_id_view(rid_ptr)

            msg_count_int = int(msg_count)
            for i in range(msg_count_int):
                msg_addr = msgs_ptr + i * _STREAM_MSG_SIZE
                msg_ptr = ctypes.c_void_p(msg_addr)
                stop_rc = 0
                try:
                    payload_ptr = lib().zlink_msg_data(msg_ptr)
                    payload_size = int(lib().zlink_msg_size(msg_ptr))
                    if payload_ptr and payload_size > 0:
                        payload_view = memoryview(
                            (ctypes.c_ubyte * payload_size).from_address(payload_ptr)
                        )
                    else:
                        payload_view = memoryview(b"")

                    try:
                        rc = handler(rid_view, payload_view)
                    except Exception:
                        stop_rc = 1
                    else:
                        stop_rc = self._stream_callback_result(rc)
                finally:
                    lib().zlink_msg_close(msg_ptr)

                if stop_rc:
                    for j in range(i + 1, msg_count_int):
                        rem_addr = msgs_ptr + j * _STREAM_MSG_SIZE
                        lib().zlink_msg_close(ctypes.c_void_p(rem_addr))
                    return stop_rc

            return 0

        return _on_packets

    def _build_stream_raw_callback(self, handler):
        @_STREAM_RAW_CB_T
        def _on_raw(rid_ptr, msg_ptr, userdata_):
            if rid_ptr is None or msg_ptr is None:
                return 0

            rid_view = self._routing_id_view(rid_ptr)
            msg_handle = ctypes.c_void_p(msg_ptr)
            stop_rc = 0
            try:
                payload_ptr = lib().zlink_msg_data(msg_handle)
                payload_size = int(lib().zlink_msg_size(msg_handle))
                if payload_ptr and payload_size > 0:
                    payload_view = memoryview(
                        (ctypes.c_ubyte * payload_size).from_address(payload_ptr)
                    )
                else:
                    payload_view = memoryview(b"")

                try:
                    rc = handler(rid_view, payload_view)
                except Exception:
                    stop_rc = 1
                else:
                    stop_rc = self._stream_callback_result(rc)
            finally:
                lib().zlink_msg_close(msg_handle)
            return stop_rc

        return _on_raw

    def close(self):
        if self._handle and self._stream_attached:
            try:
                lib().zlink_stream_detach(self._handle)
            except Exception:
                pass
            self._stream_attached = False
            self._stream_handler = None
            self._stream_callback = None
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
