# SPDX-License-Identifier: MPL-2.0

import ctypes
import errno as _errno
import sys
import types

from ._enums import (
    BindResult,
    CloseResult,
    ConfigResult,
    ConnectResult,
    ContextOption,
    HandlerResult,
    RecvResult,
    RequestResult,
    SubmitResult,
)
from ._ffi import ZlinkMsg, ZlinkRoutingId, lib


class ZlinkError(RuntimeError):
    def __init__(self, code: int, internal_errno: int = 0):
        self._code = int(code)
        self._internal_errno = int(internal_errno)
        super().__init__(
            f"{self.__class__.__name__}(code={self._code}, internal_errno={self._internal_errno})"
        )

    @property
    def code(self):
        return self._code

    @property
    def internal_errno(self):
        return self._internal_errno


class _TypedZlinkError(ZlinkError):
    _result_type = None

    def __init__(self, result, internal_errno: int = 0):
        if self._result_type is None:
            raise TypeError("typed zlink error missing result type")
        self._result = self._result_type(int(result))
        super().__init__(int(self._result), internal_errno)

    @property
    def result(self):
        return self._result


class SubmitError(_TypedZlinkError):
    _result_type = SubmitResult


class RequestError(_TypedZlinkError):
    _result_type = RequestResult


class RecvError(_TypedZlinkError):
    _result_type = RecvResult


class HandlerError(_TypedZlinkError):
    _result_type = HandlerResult


class CloseError(_TypedZlinkError):
    _result_type = CloseResult


class BindError(_TypedZlinkError):
    _result_type = BindResult


class ConnectError(_TypedZlinkError):
    _result_type = ConnectResult


class ConfigError(_TypedZlinkError):
    _result_type = ConfigResult


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
    return RequestResult.PROTOCOL_ERROR


def _request_result_from_code(code):
    try:
        return RequestResult(int(code))
    except ValueError:
        return RequestResult.PROTOCOL_ERROR


def _request_result_internal_errno(result):
    typed = _request_result_from_code(result)
    if typed == RequestResult.TIMED_OUT:
        return _errno.ETIMEDOUT
    if typed == RequestResult.TERMINATED:
        return getattr(_errno, "ETERM", 0)
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


def _raise_zlink_error(error_type, result, internal_errno=None):
    if internal_errno is None:
        internal_errno = lib().zlink_errno()
    raise error_type(result, internal_errno)


def _raise_result_error(error_type, result_type, rc, internal_errno=None):
    try:
        result = result_type(int(rc))
    except ValueError:
        result = result_type(0)
    _raise_zlink_error(error_type, result, internal_errno)


def _raise_last_error():
    err = lib().zlink_errno()
    raise ZlinkError(err, err)


def _raise_mapped_error(error_type, mapper, internal_errno=None):
    if internal_errno is None:
        internal_errno = lib().zlink_errno()
    _raise_zlink_error(error_type, mapper(int(internal_errno)), int(internal_errno))


def _raise_submit_error_from_errno(internal_errno=None):
    _raise_mapped_error(SubmitError, _submit_result_from_errno, internal_errno)


def _raise_request_error_from_errno(internal_errno=None):
    _raise_mapped_error(RequestError, _request_result_from_errno, internal_errno)


def _raise_recv_error_from_errno(internal_errno=None):
    _raise_mapped_error(RecvError, _recv_result_from_errno, internal_errno)


def _raise_handler_error_from_errno(internal_errno=None):
    _raise_mapped_error(HandlerError, _handler_result_from_errno, internal_errno)


def _raise_close_error_from_errno(internal_errno=None):
    _raise_mapped_error(CloseError, _close_result_from_errno, internal_errno)


def _raise_bind_error_from_errno(internal_errno=None):
    _raise_mapped_error(BindError, _bind_result_from_errno, internal_errno)


def _raise_connect_error_from_errno(internal_errno=None):
    _raise_mapped_error(ConnectError, _connect_result_from_errno, internal_errno)


def _raise_config_error_from_errno(internal_errno=None):
    _raise_mapped_error(ConfigError, _config_result_from_errno, internal_errno)


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
    return isinstance(exc, ZlinkError) and exc.internal_errno == _errno.EAGAIN


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
    routing_id = ctypes.POINTER(ZlinkRoutingId)()
    native_parts = []
    try:
        while True:
            native_part = ZlinkMsg()
            has_more = ctypes.c_int()
            rc = lib().zlink_recv_part(
                handle,
                ctypes.byref(routing_id),
                ctypes.byref(native_part),
                ctypes.byref(has_more),
                int(flags),
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
    parts_array = (ZlinkMsg * part_count)()
    for index, native_part in enumerate(native_parts):
        parts_array[index] = native_part
    routing = _routing_id_bytes(routing_id.contents) if routing_id else None
    return routing, _ReceivedPartsOwner(parts_array, part_count)


def _recv_native_parts_no_wait(handle):
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
            _raise_result_error(ConfigError, ConfigResult, 701, lib().zlink_errno())
        self._options = ContextOptions(self)

    @property
    def options(self):
        return self._options

    def _set_option(self, option, value):
        rc = lib().zlink_ctx_set(
            self._handle, int(option), _validated_int32(value)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def _get_option(self, option):
        rc = lib().zlink_ctx_get(self._handle, int(option))
        if rc == -1 and lib().zlink_errno() != 0:
            _raise_zlink_error(ConfigError, _config_result_from_errno(lib().zlink_errno()))
        return rc

    def shutdown(self):
        rc = lib().zlink_ctx_shutdown(self._handle)
        if rc != 0:
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    def close(self):
        if self._handle:
            rc = lib().zlink_ctx_term(self._handle)
            self._handle = None
            if rc != 0:
                _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()


class ContextOptions:
    def __init__(self, context):
        self._context = context

    @property
    def io_threads(self):
        return self._context._get_option(ContextOption.IO_THREADS)

    @io_threads.setter
    def io_threads(self, value):
        self._context._set_option(ContextOption.IO_THREADS, value)

    @property
    def max_sockets(self):
        return self._context._get_option(ContextOption.MAX_SOCKETS)

    @max_sockets.setter
    def max_sockets(self, value):
        self._context._set_option(ContextOption.MAX_SOCKETS, value)

    @property
    def max_msg_size(self):
        return self._context._get_option(ContextOption.MAX_MSGSZ)

    @max_msg_size.setter
    def max_msg_size(self, value):
        self._context._set_option(ContextOption.MAX_MSGSZ, value)

    @property
    def thread_priority(self):
        return self._context._get_option(ContextOption.THREAD_PRIORITY)

    @thread_priority.setter
    def thread_priority(self, value):
        self._context._set_option(ContextOption.THREAD_PRIORITY, value)

    @property
    def thread_scheduling_policy(self):
        return self._context._get_option(ContextOption.THREAD_SCHED_POLICY)

    @thread_scheduling_policy.setter
    def thread_scheduling_policy(self, value):
        self._context._set_option(ContextOption.THREAD_SCHED_POLICY, value)

    @property
    def blocky(self):
        return bool(self._context._get_option(ContextOption.CTX_OPT_BLOCKY))

    @blocky.setter
    def blocky(self, value):
        self._context._set_option(ContextOption.CTX_OPT_BLOCKY, int(bool(value)))

    @property
    def socket_limit(self):
        return self._context._get_option(ContextOption.SOCKET_LIMIT)

    @property
    def msg_t_size(self):
        return self._context._get_option(ContextOption.MSG_T_SIZE)

    def add_thread_affinity(self, cpu):
        self._context._set_option(ContextOption.THREAD_AFFINITY_CPU_ADD, cpu)

    def remove_thread_affinity(self, cpu):
        self._context._set_option(ContextOption.THREAD_AFFINITY_CPU_REMOVE, cpu)


class RoutingId:
    def __init__(self, data):
        self._raw = _validated_routing_id_bytes(data)

    @classmethod
    def from_bytes(cls, data):
        return cls(data)

    @classmethod
    def from_string(cls, value):
        if not isinstance(value, str):
            raise TypeError("value must be str")
        if len(value) == 0 or len(value) % 2 != 0 or any(
            c not in "0123456789abcdefABCDEF" for c in value
        ):
            raise ValueError("routing id string must be a non-empty even-length hex string")
        if len(value) > 510:
            raise ValueError("routing id string must decode to at most 255 bytes")
        return cls(bytes.fromhex(value))

    def to_bytes(self):
        return self._raw

    @property
    def size(self):
        return len(self._raw)

    def __bytes__(self):
        return self._raw

    def __len__(self):
        return len(self._raw)

    def __hash__(self):
        return hash(self._raw)

    def __eq__(self, other):
        if isinstance(other, RoutingId):
            return self._raw == other._raw
        try:
            return self._raw == _validated_routing_id_bytes(other)
        except Exception:
            return False

    def __repr__(self):
        return f"RoutingId({self._raw!r})"

    def to_hex(self):
        return self._raw.hex()

    def __str__(self):
        return self.to_hex()


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

    def close(self):
        if self._closed:
            return
        self._closed = True
        if self._owner is not None:
            self._owner.close_part(self._index)
            return
        rc = lib().zlink_msg_close(ctypes.byref(self._msg))
        if rc != 0:
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()


class ReceivedMultipart:
    def __init__(
        self,
        owner,
        routing_id=None,
        request_seq=None,
        *,
        spot_rid=None,
        reply_sender=None,
    ):
        self._owner = owner
        self.parts = tuple(
            ReceivedMessage._from_owner(owner, index)
            for index in range(owner._part_count)
        )
        self.routing_id = routing_id
        self.spot_rid = spot_rid
        self.request_seq = request_seq
        self._reply_sender = reply_sender

    def __iter__(self):
        return iter(self.parts)

    def __len__(self):
        return len(self.parts)

    def to_bytes_list(self):
        return [message.to_bytes() for message in self.parts]

    def is_single_part(self):
        return len(self.parts) == 1

    def first_part(self):
        if not self.parts:
            raise RecvError(RecvResult.NO_DATA, 0)
        return self.parts[0]

    def single_part_or_throw(self):
        if len(self.parts) != 1:
            raise RecvError(RecvResult.NO_DATA, 0)
        return self.parts[0]

    def close(self):
        self._owner.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()


class TopicMessage:
    def __init__(
        self,
        topic,
        owner,
        routing_id=None,
        request_seq=None,
        *,
        service_name=None,
    ):
        self.topic = topic
        self.service_name = service_name
        self._owner = owner
        self.parts = tuple(
            ReceivedMessage._from_owner(owner, index)
            for index in range(owner._part_count)
        )
        self.routing_id = routing_id
        self.request_seq = request_seq

    def __iter__(self):
        return iter(self.parts)

    def __len__(self):
        return len(self.parts)

    def to_bytes_list(self):
        return [message.to_bytes() for message in self.parts]

    def is_single_part(self):
        return len(self.parts) == 1

    def first_part(self):
        if not self.parts:
            raise RecvError(RecvResult.NO_DATA, 0)
        return self.parts[0]

    def single_part_or_throw(self):
        if len(self.parts) != 1:
            raise RecvError(RecvResult.NO_DATA, 0)
        return self.parts[0]

    def close(self):
        self._owner.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()


class Received(ReceivedMultipart):
    def reply(self, parts, *, flags=0):
        if self.request_seq is None or self._reply_sender is None:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._reply_sender(parts, flags=int(flags))


class Subscribed(TopicMessage):
    pass


class SubscriptionEvent:
    def __init__(self, topic, subscribed, routing_id=None, service_name=None):
        self.routing_id = routing_id
        self.service_name = service_name
        self.topic = topic
        self.subscribed = subscribed


# Per-message metadata key range.
METADATA_KEY_USER_MIN = 0x0100
METADATA_VALUE_MAX = 65535


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
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
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
    def _wrap_buffer(cls, data):
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
        return _msg_size(self._msg) if self._valid else 0

    @property
    def data(self):
        if not self._valid:
            return memoryview(b"")
        ptr = _msg_data_ptr(self._msg)
        size = self.size()
        if not ptr or size <= 0:
            return memoryview(b"")
        return memoryview((ctypes.c_ubyte * size).from_address(ptr))

    def to_bytes(self):
        return _msg_to_bytes(self._msg) if self._valid else b""

    def get_property(self, name):
        return _msg_gets(self._msg, name) if self._valid else None

    def getProperty(self, name):
        return self.get_property(name)

    def ref_count(self):
        return _msg_refcnt(self._msg) if self._valid else -1

    def refCount(self):
        return self.ref_count()

    def send(self, socket):
        socket.send(self)

    def close(self):
        if not self._valid:
            return
        rc = lib().zlink_msg_close(ctypes.byref(self._msg))
        self._valid = False
        self._keepalive = None
        if rc != 0:
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()
