# SPDX-License-Identifier: MPL-2.0

import ctypes
import errno as _errno
import sys
import traceback

from ...contracts.core.options import AutoHwmProfile, ContextOption
from ...contracts.errors.codes import (
    BindResult,
    CloseResult,
    ConfigResult,
    ConnectResult,
)
from ...contracts.sockets.codes import (
    HandlerResult,
    RecvResult,
    RequestResult,
    SubmitResult,
)
from ...contracts.errors.errors import (
    BindError,
    CloseError,
    ConfigError,
    ConnectError,
    HandlerError,
    RecvError,
    RequestError,
    SubmitError,
    ZlinkError,
    _TypedZlinkError,
)
from ..._native.ffi import ZlinkMsg, ZlinkRoutingId, lib


def _submit_result_from_errno(err):
    if err in (_errno.EAGAIN,):
        return SubmitResult.BACKPRESSURED
    if err in (_errno.ENOTCONN, getattr(_errno, "EHOSTUNREACH", -1)):
        return SubmitResult.NOT_CONNECTED
    if err in (_errno.ENOENT,):
        return SubmitResult.NOT_FOUND
    if err in (_errno.EINVAL,):
        return SubmitResult.INVALID_ARGUMENT
    if err in (_errno.EFAULT,):
        return SubmitResult.INVALID_HANDLE
    if err in (_errno.ENOTSUP, getattr(_errno, "EOPNOTSUPP", _errno.ENOTSUP)):
        return SubmitResult.NOT_SUPPORTED
    if err in (_errno.EBUSY, getattr(_errno, "EFSM", _errno.EBUSY)):
        return SubmitResult.INVALID_STATE
    if err in (getattr(_errno, "EMTHREAD", _errno.EBUSY),):
        return SubmitResult.THREAD_VIOLATION
    if err in (_errno.ENOMEM, _errno.ENOBUFS):
        return SubmitResult.OUT_OF_MEMORY
    return SubmitResult.INTERNAL_ERROR


def _request_result_from_errno(err):
    if err == 0:
        return RequestResult.OK
    if err in (_errno.ETIMEDOUT,):
        return RequestResult.TIMED_OUT
    if err in (_errno.ENOENT,):
        return RequestResult.NOT_FOUND
    if err in (getattr(_errno, "ETERM", 0),):
        return RequestResult.TERMINATED
    if err in (_errno.EIO,):
        return RequestResult.INTERNAL_ERROR
    if err in (_errno.ECONNREFUSED,):
        return RequestResult.REJECTED
    if err in (getattr(_errno, "ESTALE", 116),):
        return RequestResult.CONFLICT
    if err in (_errno.EBUSY,):
        return RequestResult.BUSY
    if err in (_errno.ENOTCONN,):
        return RequestResult.NOT_CONNECTED
    if err in (_errno.EINVAL,):
        return RequestResult.INVALID_ARGUMENT
    if err in (getattr(_errno, "EALREADY", 114),):
        return RequestResult.INVALID_STATE
    if err in (_errno.ENOTSUP, getattr(_errno, "EOPNOTSUPP", _errno.ENOTSUP)):
        return RequestResult.NOT_SUPPORTED
    return RequestResult.PROTOCOL_ERROR


def _request_result_from_code(code):
    try:
        return RequestResult(int(code))
    except ValueError:
        return RequestResult.PROTOCOL_ERROR


def _request_result_native_errno(result):
    typed = _request_result_from_code(result)
    if typed == RequestResult.TIMED_OUT:
        return _errno.ETIMEDOUT
    if typed == RequestResult.TERMINATED:
        return getattr(_errno, "ETERM", 0)
    if typed == RequestResult.NOT_FOUND:
        return _errno.ENOENT
    if typed == RequestResult.INTERNAL_ERROR:
        return _errno.EIO
    if typed == RequestResult.REJECTED:
        return _errno.ECONNREFUSED
    if typed == RequestResult.CONFLICT:
        return _errno.EINVAL
    if typed == RequestResult.BUSY:
        return _errno.EBUSY
    if typed == RequestResult.NOT_CONNECTED:
        return _errno.ENOTCONN
    if typed == RequestResult.INVALID_ARGUMENT:
        return _errno.EINVAL
    if typed == RequestResult.INVALID_STATE:
        return _errno.EINVAL
    if typed == RequestResult.NOT_SUPPORTED:
        return _errno.ENOTSUP
    return 0


def _recv_result_from_errno(err):
    if err == 0:
        return RecvResult.OK
    if err == _errno.EAGAIN:
        return RecvResult.NO_DATA
    if err == _errno.EBUSY:
        return RecvResult.BUSY
    if err in (getattr(_errno, "ETERM", 0),):
        return RecvResult.TERMINATED
    if err == _errno.EFAULT:
        return RecvResult.INVALID_HANDLE
    if err in (_errno.ENOTSUP, getattr(_errno, "EOPNOTSUPP", _errno.ENOTSUP)):
        return RecvResult.NOT_SUPPORTED
    return RecvResult.TERMINATED


def _handler_result_from_errno(err):
    if err == 0:
        return HandlerResult.OK
    if err == _errno.EINVAL:
        return HandlerResult.INVALID_ARGUMENT
    if err == _errno.EBUSY:
        return HandlerResult.BUSY
    if err in (_errno.ENOTSUP, getattr(_errno, "EOPNOTSUPP", _errno.ENOTSUP)):
        return HandlerResult.NOT_SUPPORTED
    if err == _errno.EDEADLK:
        return HandlerResult.DEADLOCK
    if err == _errno.EFAULT:
        return HandlerResult.INVALID_HANDLE
    return HandlerResult.INVALID_HANDLE


def _close_result_from_errno(err):
    if err == 0:
        return CloseResult.OK
    if err == _errno.EBUSY:
        return CloseResult.BUSY
    if err in (getattr(_errno, "ESHUTDOWN", None),):
        return CloseResult.SHUTDOWN
    if err == _errno.EFAULT:
        return CloseResult.INVALID_HANDLE
    return CloseResult.INVALID_HANDLE


def _bind_result_from_errno(err):
    if err == 0:
        return BindResult.OK
    if err == _errno.EINVAL:
        return BindResult.INVALID_ARGUMENT
    if err == _errno.EADDRINUSE:
        return BindResult.ADDR_IN_USE
    if err in (_errno.ENOTSUP, getattr(_errno, "EOPNOTSUPP", _errno.ENOTSUP)):
        return BindResult.NOT_SUPPORTED
    if err == _errno.EFAULT:
        return BindResult.INVALID_HANDLE
    return BindResult.INVALID_HANDLE


def _connect_result_from_errno(err):
    if err == 0:
        return ConnectResult.OK
    if err == _errno.EINVAL:
        return ConnectResult.INVALID_ARGUMENT
    if err in (_errno.ENOTSUP, getattr(_errno, "EOPNOTSUPP", _errno.ENOTSUP)):
        return ConnectResult.NOT_SUPPORTED
    if err == _errno.EFAULT:
        return ConnectResult.INVALID_HANDLE
    return ConnectResult.INVALID_HANDLE


def _config_result_from_errno(err):
    if err == 0:
        return ConfigResult.OK
    if err == _errno.EFAULT:
        return ConfigResult.INVALID_HANDLE
    if err == _errno.EINVAL:
        return ConfigResult.INVALID_ARGUMENT
    if err in (_errno.ENOTSUP, getattr(_errno, "EOPNOTSUPP", _errno.ENOTSUP)):
        return ConfigResult.NOT_SUPPORTED
    return ConfigResult.INVALID_ARGUMENT


def _raise_zlink_error(error_type, result, native_errno=None):
    if native_errno is None:
        native_errno = lib().zlink_errno()
    raise error_type(result, native_errno)


def _raise_result_error(error_type, result_type, rc, native_errno=None):
    try:
        result = result_type(int(rc))
    except ValueError:
        result = result_type(0)
    _raise_zlink_error(error_type, result, native_errno)


def _raise_last_error():
    err = lib().zlink_errno()
    raise ZlinkError(err, err)


def _raise_mapped_error(error_type, mapper, native_errno=None):
    if native_errno is None:
        native_errno = lib().zlink_errno()
    _raise_zlink_error(error_type, mapper(int(native_errno)), int(native_errno))


def _raise_submit_error_from_errno(native_errno=None):
    _raise_mapped_error(SubmitError, _submit_result_from_errno, native_errno)


def _raise_request_error_from_errno(native_errno=None):
    _raise_mapped_error(RequestError, _request_result_from_errno, native_errno)


def _raise_recv_error_from_errno(native_errno=None):
    _raise_mapped_error(RecvError, _recv_result_from_errno, native_errno)


def _raise_handler_error_from_errno(native_errno=None):
    _raise_mapped_error(HandlerError, _handler_result_from_errno, native_errno)


def _raise_close_error_from_errno(native_errno=None):
    _raise_mapped_error(CloseError, _close_result_from_errno, native_errno)


def _raise_bind_error_from_errno(native_errno=None):
    _raise_mapped_error(BindError, _bind_result_from_errno, native_errno)


def _raise_connect_error_from_errno(native_errno=None):
    _raise_mapped_error(ConnectError, _connect_result_from_errno, native_errno)


def _raise_config_error_from_errno(native_errno=None):
    _raise_mapped_error(ConfigError, _config_result_from_errno, native_errno)


def _as_bytes_view(data):
    if isinstance(data, bytes):
        return memoryview(data)
    if hasattr(data, "to_bytes") and callable(data.to_bytes):
        return memoryview(data.to_bytes())
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


def _validated_int32(value, *, field="value"):
    native = int(value)
    if native < -(1 << 31) or native > ((1 << 31) - 1):
        raise OverflowError(f"{field} must fit in signed 32-bit range")
    return native


def _validated_uint32(value, *, field="value"):
    native = int(value)
    if native < 0 or native > ((1 << 32) - 1):
        raise OverflowError(f"{field} must fit in unsigned 32-bit range")
    return native


def _validated_int64(value, *, field="value"):
    native = int(value)
    if native < -(1 << 63) or native > ((1 << 63) - 1):
        raise OverflowError(f"{field} must fit in signed 64-bit range")
    return native


def _validated_c_string_bytes(data, *, field="value", max_length=None):
    raw = bytes(_as_bytes_view(data))
    if b"\0" in raw:
        raise ValueError(f"{field} must not contain NUL bytes")
    if max_length is not None and len(raw) > max_length:
        raise ValueError(f"{field} must be at most {max_length} bytes")
    return raw


def _validated_c_string_value(value, *, field="value", max_length=None):
    if isinstance(value, str):
        return _validated_c_string_text(value, field=field, max_length=max_length)
    return _validated_c_string_bytes(value, field=field, max_length=max_length)


def _validated_c_string_text(text, *, field="value", max_length=None):
    if "\0" in text:
        raise ValueError(f"{field} must not contain NUL characters")
    raw = text.encode()
    if max_length is not None and len(raw) > max_length:
        raise ValueError(f"{field} must be at most {max_length} bytes")
    return raw


def _decode_topic_text(raw):
    return bytes(raw).decode("utf-8", errors="replace")


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
_REPLY_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.c_int,
    ctypes.POINTER(ZlinkMsg),
    ctypes.c_size_t,
    ctypes.c_void_p,
)
_ROUTER_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(ZlinkRoutingId),
    ctypes.POINTER(ZlinkRoutingId),
    ctypes.c_uint64,
    ctypes.POINTER(ZlinkMsg),
    ctypes.c_size_t,
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


def _validated_routing_id_bytes(routing_id):
    if isinstance(routing_id, bytes):
        size = len(routing_id)
        if size <= 0 or size > 255:
            raise ValueError("routing_id length must be between 1 and 255")
        return routing_id
    native = _copy_routing_id(routing_id)
    return bytes(native.data[: native.size])


def _msg_data_ptr(msg):
    return lib().zlink_msg_data(ctypes.byref(msg))


def _msg_size(msg):
    return int(lib().zlink_msg_size(ctypes.byref(msg)))


def _msg_refcnt(msg):
    return int(lib().zlink_msg_refcnt(ctypes.byref(msg)))


def _msg_gets(msg, property_name):
    if not property_name:
        return None
    if isinstance(property_name, str):
        property_name = property_name.encode("utf-8")
    value = lib().zlink_msg_gets(ctypes.byref(msg), property_name)
    if not value:
        return None
    return value.decode("utf-8", errors="replace")


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
        _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
    return keepalive


def _clone_native_msg(src):
    dst = ZlinkMsg()
    rc = lib().zlink_msg_init(ctypes.byref(dst))
    if rc != 0:
        _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
    rc = lib().zlink_msg_copy(ctypes.byref(dst), ctypes.byref(src))
    if rc != 0:
        lib().zlink_msg_close(ctypes.byref(dst))
        _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
    return dst


def _close_multipart(parts_ptr, part_count):
    if parts_ptr and part_count:
        lib().zlink_multipart_close(parts_ptr, part_count)


def _routing_id_bytes(routing_id):
    raw = bytes(routing_id.data[: routing_id.size])
    if not raw:
        return None
    return RoutingId(raw)


def _is_eagain(exc):
    return isinstance(exc, ZlinkError) and exc.native_errno == _errno.EAGAIN


def _report_unhandled_callback_exception(handler):
    exc_type, exc_value, exc_traceback = sys.exc_info()
    if exc_type is None:
        return
    print(f"Unhandled zlink callback exception in {handler!r}",
          file=sys.stderr)
    traceback.print_exception(exc_type, exc_value, exc_traceback)


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

    def size(self, index):
        return _msg_size(self.msg(index))

    def data(self, index):
        return memoryview(_msg_to_bytes(self.msg(index)))

    def to_bytes(self, index):
        return _msg_to_bytes(self.msg(index))

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


class _BytesReceivedPartsOwner:
    def __init__(self, parts):
        self._parts = tuple(bytes(part) for part in parts)
        self._part_count = len(self._parts)
        self._closed = False
        self._open_parts = [True] * self._part_count

    @classmethod
    def _from_trusted_bytes_tuple(cls, parts):
        owner = cls.__new__(cls)
        owner._parts = parts
        owner._part_count = len(parts)
        owner._closed = False
        owner._open_parts = [True] * owner._part_count
        return owner

    def _check_open(self, index):
        if self._closed or not self._open_parts[index]:
            raise RuntimeError("received message is closed")

    def msg(self, index):
        self._check_open(index)
        raise RuntimeError("received message does not own a native zlink_msg_t")

    def size(self, index):
        self._check_open(index)
        return len(self._parts[index])

    def data(self, index):
        self._check_open(index)
        return memoryview(self._parts[index])

    def to_bytes(self, index):
        self._check_open(index)
        return self._parts[index]

    def close_part(self, index):
        if self._closed or not self._open_parts[index]:
            return
        self._open_parts[index] = False
        if not any(self._open_parts):
            self.close()

    def close(self):
        if self._closed:
            return
        self._open_parts = [False] * self._part_count
        self._closed = True


def _recv_native_parts(handle, flags):
    # Fast path for single-part messages (the common case): allocate the
    # owner's `ZlinkMsg * 1` array directly and read into its first slot.
    # When `has_more` reports no additional parts we return that array
    # unchanged, skipping the list-then-copy pattern needed for multi-part.
    routing_id = ctypes.POINTER(ZlinkRoutingId)()
    parts_array = (ZlinkMsg * 1)()
    has_more = ctypes.c_int()
    rc = lib().zlink_recv_part(
        handle,
        ctypes.byref(routing_id),
        ctypes.byref(parts_array[0]),
        ctypes.byref(has_more),
        int(flags),
    )
    if rc != 0:
        _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())

    if has_more.value == 0:
        routing = _routing_id_bytes(routing_id.contents) if routing_id else None
        return routing, _ReceivedPartsOwner(parts_array, 1)

    native_parts = [parts_array[0]]
    try:
        while True:
            native_part = ZlinkMsg()
            rc = lib().zlink_recv_part(
                handle,
                ctypes.byref(routing_id),
                ctypes.byref(native_part),
                ctypes.byref(has_more),
                1,
            )
            if rc != 0:
                _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
            native_parts.append(native_part)
            if has_more.value == 0:
                break
    except Exception:
        for native_part in native_parts:
            lib().zlink_msg_close(ctypes.byref(native_part))
        raise

    part_count = len(native_parts)
    final_array = (ZlinkMsg * part_count)()
    for index, native_part in enumerate(native_parts):
        final_array[index] = native_part
    routing = _routing_id_bytes(routing_id.contents) if routing_id else None
    return routing, _ReceivedPartsOwner(final_array, part_count)


def _recv_native_parts_no_wait(handle):
    try:
        return _recv_native_parts(handle, 1)
    except ZlinkError as exc:
        if _is_eagain(exc):
            return None
        raise



# Public classes live in contracts/. The re-export below is intentionally
# deferred via __getattr__ so this module can be loaded before the contracts
# package finishes initializing. Internal _runtime callers can still write
# ``from ..handles.native_support import Context, Message, RoutingId, ...`` without inviting
# a circular import.
def __getattr__(name):
    _public = {
        "Context": ("zlink.contracts.core.context", "Context"),
        "ContextOptions": ("zlink.contracts.core.context", "ContextOptions"),
        "RoutingId": ("zlink.contracts.core.routing_id", "RoutingId"),
        "Message": ("zlink.contracts.messaging.message", "Message"),
        "Received": ("zlink.contracts.messaging.received", "Received"),
        "ReceivedMessage": ("zlink.contracts.messaging.received", "ReceivedMessage"),
        "ReceivedMultipart": ("zlink.contracts.messaging.received", "ReceivedMultipart"),
        "SubscriptionEvent": (
            "zlink.contracts.messaging.subscription_event",
            "SubscriptionEvent",
        ),
        "TopicMessage": ("zlink.contracts.messaging.topic_message", "TopicMessage"),
        "METADATA_KEY_USER_MIN": ("zlink.contracts.messaging.message", "METADATA_KEY_USER_MIN"),
        "METADATA_VALUE_MAX": ("zlink.contracts.messaging.message", "METADATA_VALUE_MAX"),
    }
    target = _public.get(name)
    if target is None:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    import importlib
    module = importlib.import_module(target[0])
    value = getattr(module, target[1])
    globals()[name] = value
    return value
