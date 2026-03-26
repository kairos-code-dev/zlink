# SPDX-License-Identifier: MPL-2.0

import ctypes
import sys
import types
import warnings

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
        self.messages = [
            ReceivedMessage._from_owner(owner, index)
            for index in range(owner._part_count)
        ]
        self.routing_id = routing_id

    def __iter__(self):
        return iter(self.messages)

    def __len__(self):
        return len(self.messages)

    def to_bytes_list(self):
        return [message.to_bytes() for message in self.messages]

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
        self.messages = [
            ReceivedMessage._from_owner(owner, index)
            for index in range(owner._part_count)
        ]
        self.routing_id = routing_id

    def to_bytes_list(self):
        return [message.to_bytes() for message in self.messages]

    def close(self):
        self._owner.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class Socket:
    def __init__(self, context, sock_type):
        native_type = _LEGACY_SOCKET_TYPE_MAP.get(int(sock_type), int(sock_type))
        self._handle = lib().zlink_socket(context._handle, native_type)
        if not self._handle:
            _raise_last_error()
        self._own = True
        self._socket_type = native_type
        self._pending_send_parts = []
        self._legacy_recv_queue = []
        self._recv_handler = None
        self._recv_handler_cb = None
        self._subscribe_handler = None
        self._subscribe_handler_cb = None
        self._send_ready_handler = None
        self._send_ready_handler_cb = None

    @classmethod
    def _from_handle(cls, handle, own=False):
        obj = cls.__new__(cls)
        obj._handle = handle
        obj._own = own
        obj._socket_type = None
        obj._pending_send_parts = []
        obj._legacy_recv_queue = []
        obj._recv_handler = None
        obj._recv_handler_cb = None
        obj._subscribe_handler = None
        obj._subscribe_handler_cb = None
        obj._send_ready_handler = None
        obj._send_ready_handler_cb = None
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

    def attach_discovery(self, discovery):
        rc = lib().zlink_socket_attach_discovery(self._handle, discovery._handle)
        if rc != 0:
            _raise_last_error()

    def _send_native_parts(self, native_parts, flags):
        part_count = len(native_parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native in enumerate(native_parts):
            parts_array[index] = native
        rc = lib().zlink_send(self._handle, parts_array, part_count, int(flags))
        if rc < 0:
            for index in range(part_count):
                lib().zlink_msg_close(ctypes.byref(parts_array[index]))
            _raise_last_error()
        return rc

    def _send_native_parts_to_routing_id(self, routing_id, native_parts, flags):
        part_count = len(native_parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native in enumerate(native_parts):
            parts_array[index] = native
        target = _copy_routing_id(routing_id)
        rc = lib().zlink_send_rid(
            self._handle, ctypes.byref(target), parts_array, part_count, int(flags)
        )
        if rc < 0:
            for index in range(part_count):
                lib().zlink_msg_close(ctypes.byref(parts_array[index]))
            _raise_last_error()
        return rc

    def _set_raw_option(self, setter, option, value):
        ptr, size, keepalive = _send_buffer(value)
        rc = setter(
            self._handle,
            int(option),
            ctypes.c_void_p(
                ptr if isinstance(ptr, int) else ctypes.cast(ptr, ctypes.c_void_p).value or 0
            ),
            size,
        )
        _ = keepalive
        if rc != 0:
            _raise_last_error()

    def _get_raw_option(self, getter, option, size):
        buf = ctypes.create_string_buffer(size)
        out_size = ctypes.c_size_t(size)
        rc = getter(self._handle, int(option), buf, ctypes.byref(out_size))
        if rc != 0:
            _raise_last_error()
        return buf.raw[: out_size.value]

    def send(self, data, flags: int = 0):
        if int(flags) & 0x2:
            self._pending_send_parts.append(data)
            return len(bytes(_as_bytes_view(data)))

        if self._pending_send_parts:
            parts = self._pending_send_parts + [data]
            self._pending_send_parts = []
            if self._socket_type == 0x1005 and parts:
                routing_id = parts[0]
                payload_parts = parts[1:] or [b""]
                native_parts = []
                for part in payload_parts:
                    if isinstance(part, Message):
                        native_parts.append(_clone_native_msg(part._msg))
                    else:
                        native = ZlinkMsg()
                        _keepalive = _init_msg_from_buffer(native, part, borrow=False)
                        _ = _keepalive
                        native_parts.append(native)
                return self._send_native_parts_to_routing_id(
                    routing_id, native_parts, 0
                )
            return self.send_multipart(parts, flags=0)

        if isinstance(data, Message):
            native = _clone_native_msg(data._msg)
            return self._send_native_parts([native], flags)

        native = ZlinkMsg()
        _keepalive = _init_msg_from_buffer(native, data, borrow=False)
        _ = _keepalive
        return self._send_native_parts([native], flags)

    def send_multipart(self, parts, flags: int = 0):
        native_parts = []
        for part in parts:
            if isinstance(part, Message):
                native_parts.append(_clone_native_msg(part._msg))
                continue
            native = ZlinkMsg()
            _keepalive = _init_msg_from_buffer(native, part, borrow=False)
            _ = _keepalive
            native_parts.append(native)
        self._send_native_parts(native_parts, flags)

    def recv_message(self, flags: int = 0):
        routing, owner = _recv_native_parts(self._handle, flags)
        if owner._part_count != 1:
            count = owner._part_count
            owner.close()
            raise ValueError(f"expected single-part message, got {count} parts")
        return ReceivedMessage._from_owner(owner, 0, routing)

    def recv_multipart(self, flags: int = 0):
        routing, owner = _recv_native_parts(self._handle, flags)
        return ReceivedMultipart(owner, routing)

    def recv_into(self, buffer, flags: int = 0):
        view = _as_bytes_view(buffer)
        if view.readonly:
            raise TypeError("buffer must be writable")
        if view.nbytes <= 0:
            raise ValueError("buffer must not be empty")
        with self.recv_message(flags=flags) as received:
            payload = received.view()
            payload_size = len(payload)
            if payload_size > view.nbytes:
                raise ValueError("buffer too small for received message")
            if payload_size:
                native = received._native_msg()
                ctypes.memmove(
                    ctypes.addressof((ctypes.c_ubyte * view.nbytes).from_buffer(view)),
                    _msg_data_ptr(native),
                    payload_size,
                )
            return payload_size

    def publish(self, topic, payload, flags: int = 0):
        topic_bytes = bytes(_as_bytes_view(topic))
        if isinstance(payload, (list, tuple)):
            parts = payload
        else:
            parts = [payload]
        native_parts = []
        for part in parts:
            if isinstance(part, Message):
                native_parts.append(_clone_native_msg(part._msg))
            else:
                native = ZlinkMsg()
                _keepalive = _init_msg_from_buffer(native, part, borrow=False)
                _ = _keepalive
                native_parts.append(native)

        part_count = len(native_parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native in enumerate(native_parts):
            parts_array[index] = native
        rc = lib().zlink_publish(
            self._handle, topic_bytes, parts_array, part_count, int(flags)
        )
        if rc < 0:
            for index in range(part_count):
                lib().zlink_msg_close(ctypes.byref(parts_array[index]))
            _raise_last_error()
        return rc

    def recv_topic_message(self, flags: int = 0):
        routing_id = ZlinkRoutingId()
        parts = ctypes.POINTER(ZlinkMsg)()
        part_count = ctypes.c_size_t()
        topic_buf = ctypes.create_string_buffer(256)
        topic_len = ctypes.c_size_t(len(topic_buf))
        rc = lib().zlink_subscribe(
            self._handle,
            ctypes.byref(routing_id),
            ctypes.byref(parts),
            ctypes.byref(part_count),
            topic_buf,
            ctypes.byref(topic_len),
            int(flags),
        )
        if rc != 0:
            _raise_last_error()

        owner = _ReceivedPartsOwner(parts, int(part_count.value))
        topic = topic_buf.raw[: topic_len.value]
        routing = _routing_id_bytes(routing_id)
        return ReceivedTopicMessage(topic, owner, routing)

    def subscription_event(self, flags: int = 0):
        routing_id = ZlinkRoutingId()
        subscribed = ctypes.c_int()
        topic_buf = ctypes.create_string_buffer(256)
        topic_len = ctypes.c_size_t(len(topic_buf))
        rc = lib().zlink_subscription_event(
            self._handle,
            ctypes.byref(routing_id),
            ctypes.byref(subscribed),
            topic_buf,
            ctypes.byref(topic_len),
            int(flags),
        )
        if rc != 0:
            _raise_last_error()
        return {
            "routing_id": bytes(routing_id.data[: routing_id.size]) or None,
            "subscribed": bool(subscribed.value),
            "topic": topic_buf.raw[: topic_len.value],
        }

    def set_recv_handler(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")

        def _callback(routing_id_ptr, parts_ptr, part_count, _):
            routing_id = None
            if routing_id_ptr:
                routing_id = _routing_id_bytes(routing_id_ptr.contents)
            received = ReceivedMultipart(
                _ReceivedPartsOwner(parts_ptr, int(part_count)),
                routing_id,
            )
            try:
                handler(received)
            except Exception:
                try:
                    received.close()
                finally:
                    _report_unhandled_callback_exception(handler)
            else:
                received.close()

        callback = _SOCKET_RECV_HANDLER(_callback)
        rc = lib().zlink_recv_handler(self._handle, callback, None)
        if rc != 0:
            _raise_last_error()
        self._recv_handler = handler
        self._recv_handler_cb = callback

    def set_subscribe_handler(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")

        def _callback(routing_id_ptr, topic_ptr, topic_len, parts_ptr, part_count, _):
            routing_id = None
            if routing_id_ptr:
                routing_id = _routing_id_bytes(routing_id_ptr.contents)
            topic = b""
            if topic_ptr and topic_len:
                topic = ctypes.string_at(topic_ptr, topic_len)
            received = ReceivedTopicMessage(
                topic,
                _ReceivedPartsOwner(parts_ptr, int(part_count)),
                routing_id,
            )
            try:
                handler(received)
            except Exception:
                try:
                    received.close()
                finally:
                    _report_unhandled_callback_exception(handler)
            else:
                received.close()

        callback = _SOCKET_SUBSCRIBE_HANDLER(_callback)
        rc = lib().zlink_subscribe_handler(self._handle, callback, None)
        if rc != 0:
            _raise_last_error()
        self._subscribe_handler = handler
        self._subscribe_handler_cb = callback

    def set_send_ready_handler(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")

        def _callback(_, __):
            try:
                handler(self)
            except Exception:
                _report_unhandled_callback_exception(handler)

        callback = _SOCKET_SEND_READY_HANDLER(_callback)
        rc = lib().zlink_send_ready_handler(self._handle, callback, None)
        if rc != 0:
            _raise_last_error()
        self._send_ready_handler = handler
        self._send_ready_handler_cb = callback

    def recv(self, size: int, flags: int = 0) -> bytes:
        warnings.warn(
            "Socket.recv(size) is legacy; use recv_message() instead",
            DeprecationWarning,
            stacklevel=2,
        )
        if self._legacy_recv_queue:
            data = self._legacy_recv_queue.pop(0)
            if len(data) > size:
                raise ValueError("received message larger than requested legacy size")
            return data

        routing_id, owner = _recv_native_parts(self._handle, flags)
        if routing_id:
            self._legacy_recv_queue.append(routing_id)
        self._legacy_recv_queue.extend(
            _msg_to_bytes(owner.msg(index))
            for index in range(owner._part_count)
        )
        owner.close()
        data = self._legacy_recv_queue.pop(0)
        if len(data) > size:
            raise ValueError("received message larger than requested legacy size")
        return data

    def set_router_option(self, option, value):
        self._set_raw_option(lib().zlink_set_router_option, option, value)

    def get_router_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_router_option, option, size)

    def set_dealer_option(self, option, value):
        self._set_raw_option(lib().zlink_set_dealer_option, option, value)

    def set_pub_option(self, option, value):
        self._set_raw_option(lib().zlink_set_pub_option, option, value)

    def get_pub_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_pub_option, option, size)

    def set_sub_option(self, option, value):
        self._set_raw_option(lib().zlink_set_sub_option, option, value)

    def get_sub_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_sub_option, option, size)

    def set_stream_option(self, option, value):
        self._set_raw_option(lib().zlink_set_stream_option, option, value)

    def get_stream_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_stream_option, option, size)

    def set_option(self, option: int, value):
        if int(option) == 5:
            topic_bytes = bytes(_as_bytes_view(value))
            rc = lib().zlink_set_routing_id(
                self._handle,
                ctypes.c_char_p(topic_bytes),
                len(topic_bytes),
            )
        elif int(option) == 6:
            rc = lib().zlink_set_subscription(
                self._handle, bytes(_as_bytes_view(value))
            )
        elif int(option) == 7:
            rc = lib().zlink_unset_subscription(
                self._handle, bytes(_as_bytes_view(value))
            )
        elif int(option) == 40:
            self.set_pub_option(0x3301, value)
            return
        elif 0x3100 <= int(option) < 0x3200:
            self.set_router_option(option, value)
            return
        elif 0x3200 <= int(option) < 0x3300:
            self.set_dealer_option(option, value)
            return
        elif 0x3300 <= int(option) < 0x3400:
            self.set_pub_option(option, value)
            return
        elif 0x3400 <= int(option) < 0x3500:
            self.set_sub_option(option, value)
            return
        elif 0x3500 <= int(option) < 0x3600:
            self.set_stream_option(option, value)
            return
        else:
            self._set_raw_option(lib().zlink_set_option, option, value)
            return
        if rc != 0:
            _raise_last_error()

    def get_option(self, option: int, size: int = 256):
        if int(option) == 5:
            rid = ZlinkRoutingId()
            rc = lib().zlink_get_routing_id(self._handle, ctypes.byref(rid))
            if rc != 0:
                _raise_last_error()
            return bytes(rid.data[: rid.size])
        if 0x3100 <= int(option) < 0x3200:
            return self.get_router_option(option, size)
        if 0x3300 <= int(option) < 0x3400:
            return self.get_pub_option(option, size)
        if 0x3400 <= int(option) < 0x3500:
            return self.get_sub_option(option, size)
        if 0x3500 <= int(option) < 0x3600:
            return self.get_stream_option(option, size)
        return self._get_raw_option(lib().zlink_get_option, option, size)

    def set_routing_id(self, routing_id):
        self.set_option(5, routing_id)

    def get_routing_id(self) -> bytes:
        return self.get_option(5)

    def subscribe(self, topic):
        topic_bytes = bytes(_as_bytes_view(topic))
        rc = lib().zlink_set_subscription(self._handle, topic_bytes)
        if rc != 0:
            _raise_last_error()

    def unsubscribe(self, topic):
        topic_bytes = bytes(_as_bytes_view(topic))
        rc = lib().zlink_unset_subscription(self._handle, topic_bytes)
        if rc != 0:
            _raise_last_error()

    def open_monitor(self, events=0xFFFF):
        from ._monitor import open_socket_monitor

        return open_socket_monitor(self, events)

    def close(self):
        self._pending_send_parts = []
        self._legacy_recv_queue = []
        self._recv_handler = None
        self._recv_handler_cb = None
        self._subscribe_handler = None
        self._subscribe_handler_cb = None
        self._send_ready_handler = None
        self._send_ready_handler_cb = None
        if self._handle and self._own:
            rc = lib().zlink_close(self._handle)
            self._handle = None
            if rc != 0:
                _raise_last_error()
            return
        self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


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

    def send(self, socket, flags: int = 0):
        socket.send(self, flags=flags)

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
