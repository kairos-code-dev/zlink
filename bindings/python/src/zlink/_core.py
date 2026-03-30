# SPDX-License-Identifier: MPL-2.0

import ctypes
import errno
import sys
import types

from ._ffi import ZlinkMsg, ZlinkRoutingId, lib


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
            return None, 0, data
        return ctypes.c_char_p(data), size, data

    view = _as_bytes_view(data)
    size = view.nbytes
    if size == 0:
        return None, 0, view
    if view.readonly:
        raw = view.tobytes()
        return ctypes.c_char_p(raw), size, raw
    return ctypes.addressof((ctypes.c_char * size).from_buffer(view)), size, view


_ZlinkRoutingId = ZlinkRoutingId
_SOCKET_RECV_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(ZlinkRoutingId),
    ctypes.POINTER(ZlinkMsg),
    ctypes.c_size_t,
    ctypes.c_void_p,
)
_SOCKET_SUBSCRIBE_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(ZlinkRoutingId),
    ctypes.c_char_p,
    ctypes.c_size_t,
    ctypes.POINTER(ZlinkMsg),
    ctypes.c_size_t,
    ctypes.c_void_p,
)
_SOCKET_SEND_READY_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,
    ctypes.c_void_p,
)
_LEGACY_SOCKET_TYPE_MAP = {
    0: 0x1001,
    1: 0x1002,
    2: 0x1003,
    5: 0x1004,
    6: 0x1005,
    9: 0x1006,
    10: 0x1007,
    11: 0x1008,
}


def _copy_routing_id(routing_id):
    view = _as_bytes_view(routing_id)
    size = view.nbytes
    if size <= 0 or size > 255:
        raise ValueError("routing_id length must be between 1 and 255")
    native = ZlinkRoutingId()
    native.size = size
    for index in range(size):
        native.data[index] = view[index]
    return native


def _msg_data_ptr(msg):
    return lib().zlink_msg_data(ctypes.byref(msg))


def _msg_size(msg):
    return int(lib().zlink_msg_size(ctypes.byref(msg)))


def _msg_to_bytes(msg):
    size = _msg_size(msg)
    if size <= 0:
        return b""
    ptr = _msg_data_ptr(msg)
    if not ptr:
        return b""
    return ctypes.string_at(ptr, size)


def _init_msg_from_buffer(msg, data, *, borrow):
    ptr, size, keepalive = _send_buffer(data)
    if borrow:
        data_ptr = ctypes.c_void_p(ptr if isinstance(ptr, int) else ctypes.cast(ptr, ctypes.c_void_p).value or 0)
        rc = lib().zlink_msg_init_data(ctypes.byref(msg), data_ptr, size, None, None)
    else:
        rc = lib().zlink_msg_init_size(ctypes.byref(msg), size)
        if rc == 0 and size:
            ctypes.memmove(_msg_data_ptr(msg), ptr, size)
    if rc != 0:
        _raise_last_error()
    return keepalive


def _clone_native_msg(src):
    dst = ZlinkMsg()
    rc = lib().zlink_msg_init(ctypes.byref(dst))
    if rc != 0:
        _raise_last_error()
    rc = lib().zlink_msg_copy(ctypes.byref(dst), ctypes.byref(src))
    if rc != 0:
        lib().zlink_msg_close(ctypes.byref(dst))
        _raise_last_error()
    return dst


def _close_multipart(parts_ptr, part_count):
    if parts_ptr and part_count:
        lib().zlink_multipart_close(parts_ptr, part_count)


def _routing_id_bytes(routing_id):
    return bytes(routing_id.data[: routing_id.size]) or None


def _is_eagain(exc):
    return isinstance(exc, ZlinkError) and exc.errno == errno.EAGAIN


def _report_unhandled_callback_exception(handler):
    exc_type, exc_value, exc_traceback = sys.exc_info()
    if exc_type is None:
        return
    sys.unraisablehook(
        types.SimpleNamespace(
            exc_type=exc_type,
            exc_value=exc_value,
            exc_traceback=exc_traceback,
            err_msg="Unhandled zlink callback exception",
            object=handler,
        )
    )


class _ReceivedPartsOwner:
    def __init__(self, parts_ptr, part_count):
        self._parts_ptr = parts_ptr
        self._part_count = part_count
        self._closed = False
        self._open_parts = [True] * part_count

    def msg(self, index):
        if self._closed or not self._open_parts[index]:
            raise RuntimeError("received message is closed")
        return self._parts_ptr[index]

    def close_part(self, index):
        if self._closed or not self._open_parts[index]:
            return
        self._open_parts[index] = False
        if not any(self._open_parts):
            self.close()

    def close(self):
        if self._closed:
            return
        _close_multipart(self._parts_ptr, self._part_count)
        self._parts_ptr = None
        self._open_parts = [False] * self._part_count
        self._closed = True


def _recv_native_parts(handle, flags):
    routing_id = ZlinkRoutingId()
    parts = ctypes.POINTER(ZlinkMsg)()
    part_count = ctypes.c_size_t()
    rc = lib().zlink_recv(
        handle,
        ctypes.byref(routing_id),
        ctypes.byref(parts),
        ctypes.byref(part_count),
        int(flags),
    )
    if rc != 0:
        _raise_last_error()
    return _routing_id_bytes(routing_id), _ReceivedPartsOwner(
        parts, int(part_count.value)
    )


def _try_recv_native_parts(handle):
    try:
        return _recv_native_parts(handle, 1)
    except ZlinkError as exc:
        if _is_eagain(exc):
            return None
        raise


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
            rc = lib().zlink_ctx_term(self._handle)
            self._handle = None
            if rc != 0:
                _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class ReceivedMessage:
    def __init__(self, msg=None, routing_id=None, *, owner=None, index=None):
        self._msg = msg
        self._owner = owner
        self._index = index
        self._closed = False
        self.routing_id = routing_id

    @classmethod
    def _from_owner(cls, owner, index, routing_id=None):
        return cls(routing_id=routing_id, owner=owner, index=index)

    def _native_msg(self):
        if self._owner is not None:
            return self._owner.msg(self._index)
        if self._closed:
            raise RuntimeError("received message is closed")
        return self._msg

    def __len__(self):
        return _msg_size(self._native_msg())

    def to_bytes(self):
        return _msg_to_bytes(self._native_msg())

    def view(self):
        native = self._native_msg()
        ptr = _msg_data_ptr(native)
        size = _msg_size(native)
        if not ptr or size <= 0:
            return memoryview(b"")
        return memoryview((ctypes.c_ubyte * size).from_address(ptr))

    def close(self):
        if self._closed:
            return
        self._closed = True
        if self._owner is not None:
            self._owner.close_part(self._index)
            return
        rc = lib().zlink_msg_close(ctypes.byref(self._msg))
        if rc != 0:
            _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class ReceivedMultipart:
    def __init__(self, owner, routing_id=None):
        self._owner = owner
        self.parts = tuple(
            ReceivedMessage._from_owner(owner, index)
            for index in range(owner._part_count)
        )
        self.routing_id = routing_id
        self.messages = self.parts

    def __iter__(self):
        return iter(self.parts)

    def __len__(self):
        return len(self.parts)

    def to_bytes_list(self):
        return [message.to_bytes() for message in self.parts]

    def close(self):
        self._owner.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class ReceivedTopicMessage:
    def __init__(self, topic, owner, routing_id=None):
        self.topic = topic
        self._owner = owner
        self.parts = tuple(
            ReceivedMessage._from_owner(owner, index)
            for index in range(owner._part_count)
        )
        self.routing_id = routing_id
        self.messages = self.parts

    def __iter__(self):
        return iter(self.parts)

    def __len__(self):
        return len(self.parts)

    def to_bytes_list(self):
        return [message.to_bytes() for message in self.parts]

    def close(self):
        self._owner.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class Received(ReceivedMultipart):
    pass


class Subscribed(ReceivedTopicMessage):
    pass


class SubscriptionEvent:
    def __init__(self, topic, subscribed, routing_id=None):
        self.routing_id = routing_id
        self.topic = topic
        self.subscribed = subscribed


class Message:
    def __init__(self, size: int | None = None):
        self._msg = ZlinkMsg()
        self._valid = False
        self._keepalive = None
        if size is None:
            rc = lib().zlink_msg_init(ctypes.byref(self._msg))
        else:
            rc = lib().zlink_msg_init_size(ctypes.byref(self._msg), size)
        if rc != 0:
            _raise_last_error()
        self._valid = True

    @classmethod
    def copy_from(cls, data):
        msg = cls.__new__(cls)
        msg._msg = ZlinkMsg()
        msg._valid = False
        msg._keepalive = _init_msg_from_buffer(msg._msg, data, borrow=False)
        msg._valid = True
        return msg

    @classmethod
    def wrap_buffer(cls, data):
        msg = cls.__new__(cls)
        msg._msg = ZlinkMsg()
        msg._valid = False
        msg._keepalive = _init_msg_from_buffer(msg._msg, data, borrow=True)
        msg._valid = True
        return msg

    @staticmethod
    def from_bytes(data: bytes):
        return Message.copy_from(data)

    def size(self):
        return _msg_size(self._msg)

    def data(self):
        return self.to_bytes()

    def to_bytes(self):
        return _msg_to_bytes(self._msg)

    def view(self):
        ptr = _msg_data_ptr(self._msg)
        size = self.size()
        if not ptr or size <= 0:
            return memoryview(b"")
        return memoryview((ctypes.c_ubyte * size).from_address(ptr))

    def send(self, socket):
        socket.send(self)

    def close(self):
        if not self._valid:
            return
        rc = lib().zlink_msg_close(ctypes.byref(self._msg))
        self._valid = False
        self._keepalive = None
        if rc != 0:
            _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
