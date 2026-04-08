# SPDX-License-Identifier: MPL-2.0

import ctypes
import threading
import warnings

from ._enums import SendResult, SocketOption, SocketType
from ._ffi import ZlinkMsg, lib
from ._core import (
    Message,
    Received,
    Subscribed,
    ZlinkRoutingId,
    _SOCKET_RECV_HANDLER,
    _SOCKET_SEND_READY_HANDLER,
    _SOCKET_SUBSCRIBE_HANDLER,
    _ReceivedPartsOwner,
    _LEGACY_SOCKET_TYPE_MAP,
    _as_bytes_view,
    _clone_native_msg,
    _copy_routing_id,
    _init_msg_from_buffer,
    _is_eagain,
    _raise_last_error,
    _recv_native_parts,
    _try_recv_native_parts,
    _report_unhandled_callback_exception,
    _routing_id_bytes,
    _validated_c_string_bytes,
    _validated_c_string_text,
    _validated_int32,
    _validated_routing_id_bytes,
    _send_buffer,
)

_DONTWAIT = 0x0001

_callback_depth = threading.local()


def _in_callback():
    return getattr(_callback_depth, "depth", 0) > 0


def _enter_callback():
    _callback_depth.depth = getattr(_callback_depth, "depth", 0) + 1


def _leave_callback():
    _callback_depth.depth = getattr(_callback_depth, "depth", 0) - 1


def _callback_send_flags(flags):
    """When inside a callback, force DONTWAIT to prevent deadlock.

    Blocking sends from a callback run on the socket I/O thread.  If the
    send needs to retry (pipe full / EAGAIN), the retry loop blocks on
    ``process_commands`` -- which waits for I/O events that can only be
    delivered by the very same I/O thread, causing a deadlock.

    Forcing ``DONTWAIT`` avoids the retry loop.  If the pipe is full the
    send returns immediately with EAGAIN, which the binding surfaces as
    an exception instead of hanging forever.
    """
    if _in_callback():
        return int(flags) | _DONTWAIT
    return int(flags)


def _compat_warning(message):
    warnings.warn(message, DeprecationWarning, stacklevel=3)


def _native_socket_type(sock_type):
    return _LEGACY_SOCKET_TYPE_MAP.get(int(sock_type), int(sock_type))


def _socket_type_name(socket_type):
    try:
        return SocketType(int(socket_type)).name
    except ValueError:
        return str(int(socket_type))


def _payload_parts(payload):
    if isinstance(payload, (list, tuple)):
        parts = list(payload)
    else:
        parts = [payload]
    if not parts:
        raise ValueError("parts must not be empty")
    return parts


def _int32_bytes(value):
    native = ctypes.c_int32(_validated_int32(value))
    return ctypes.string_at(ctypes.byref(native), ctypes.sizeof(native))


def _bool_bytes(value):
    return _int32_bytes(1 if value else 0)


def _close_send_parts(parts_array, part_count):
    for index in range(part_count):
        lib().zlink_msg_close(ctypes.byref(parts_array[index]))


def _try_send_via_native(handle, parts_array, part_count):
    native_result = ctypes.c_int(int(SendResult.SENT))
    rc = lib().zlink_try_send(
        handle,
        parts_array,
        part_count,
        ctypes.byref(native_result),
    )
    if rc != 0:
        _close_send_parts(parts_array, part_count)
        _raise_last_error()
    result = SendResult(native_result.value)
    if result is not SendResult.SENT:
        _close_send_parts(parts_array, part_count)
    return result


def _try_send_rid_via_native(handle, routing_id, parts_array, part_count):
    native_result = ctypes.c_int(int(SendResult.SENT))
    rc = lib().zlink_try_send_rid(
        handle,
        ctypes.byref(routing_id),
        parts_array,
        part_count,
        ctypes.byref(native_result),
    )
    if rc != 0:
        _close_send_parts(parts_array, part_count)
        _raise_last_error()
    result = SendResult(native_result.value)
    if result is not SendResult.SENT:
        _close_send_parts(parts_array, part_count)
    return result


def _try_publish_via_native(handle, topic_bytes, parts_array, part_count):
    native_result = ctypes.c_int(int(SendResult.SENT))
    rc = lib().zlink_try_publish(
        handle,
        topic_bytes,
        parts_array,
        part_count,
        ctypes.byref(native_result),
    )
    if rc != 0:
        _close_send_parts(parts_array, part_count)
        _raise_last_error()
    result = SendResult(native_result.value)
    if result is not SendResult.SENT:
        _close_send_parts(parts_array, part_count)
    return result


def _read_int32(raw):
    if len(raw) != ctypes.sizeof(ctypes.c_int32):
        raise ValueError("native option payload size mismatch")
    return ctypes.c_int32.from_buffer_copy(raw).value


class CommonSocketOptions:
    def __init__(self, socket):
        self._socket = socket

    @property
    def linger_ms(self):
        return self._socket._get_common_int_option(SocketOption.LINGER)

    @linger_ms.setter
    def linger_ms(self, value):
        self._socket._set_common_int_option(SocketOption.LINGER, value)

    @property
    def send_high_water_mark(self):
        return self._socket._get_common_int_option(SocketOption.SNDHWM)

    @send_high_water_mark.setter
    def send_high_water_mark(self, value):
        self._socket._set_common_int_option(SocketOption.SNDHWM, value)

    @property
    def receive_high_water_mark(self):
        return self._socket._get_common_int_option(SocketOption.RCVHWM)

    @receive_high_water_mark.setter
    def receive_high_water_mark(self, value):
        self._socket._set_common_int_option(SocketOption.RCVHWM, value)

    @property
    def send_timeout_ms(self):
        return self._socket._get_common_int_option(SocketOption.SNDTIMEO)

    @send_timeout_ms.setter
    def send_timeout_ms(self, value):
        self._socket._set_common_int_option(SocketOption.SNDTIMEO, value)

    @property
    def receive_timeout_ms(self):
        return self._socket._get_common_int_option(SocketOption.RCVTIMEO)

    @receive_timeout_ms.setter
    def receive_timeout_ms(self, value):
        self._socket._set_common_int_option(SocketOption.RCVTIMEO, value)

    @property
    def immediate(self):
        return self._socket._get_common_bool_option(SocketOption.IMMEDIATE)

    @immediate.setter
    def immediate(self, value):
        self._socket._set_common_bool_option(SocketOption.IMMEDIATE, value)


class DealerSocketOptions:
    def __init__(self, socket):
        self._socket = socket

    @property
    def probe(self):
        return bool(getattr(self._socket, "_dealer_probe_option", False))

    @probe.setter
    def probe(self, value):
        self._socket._set_dealer_option(0x3201, _bool_bytes(value))
        self._socket._dealer_probe_option = bool(value)


class StreamSocketOptions:
    _NOTIFY = 0x3501

    def __init__(self, socket):
        self._socket = socket

    @property
    def notify(self):
        return bool(_read_int32(self._socket._get_stream_option(self._NOTIFY, 4)))

    @notify.setter
    def notify(self, value):
        self._socket._set_stream_option(self._NOTIFY, _bool_bytes(value))


class SubSocketOptions:
    _TOPICS_COUNT = 0x3400

    def __init__(self, socket):
        self._socket = socket

    @property
    def topics_count(self):
        return _read_int32(self._socket._get_sub_option(self._TOPICS_COUNT, 4))


class _SocketHandle:
    def __init__(self, handle, own):
        self.handle = handle
        self.own = own

    def close(self):
        if not self.handle:
            return
        handle = self.handle
        self.handle = None
        if not self.own:
            return
        rc = lib().zlink_close(handle)
        if rc != 0:
            _raise_last_error()


class _BaseSocket:
    _socket_type_value = None
    _OPTION_ROUTE_MISS = object()
    _OPTION_SET_ROUTES = (
        ((5,), "routing IDs", "set_routing_id", lambda option, value: (value,)),
        ((6,), "subscriptions", "set_subscription", lambda option, value: (value,)),
        ((7,), "subscriptions", "unset_subscription", lambda option, value: (value,)),
        ((40,), "publisher options", "_set_pub_option", lambda option, value: (0x3301, value)),
        ((0x3100, 0x3200), "router options", "_set_router_option", lambda option, value: (option, value)),
        ((0x3200, 0x3300), "dealer options", "_set_dealer_option", lambda option, value: (option, value)),
        ((0x3300, 0x3400), "publisher options", "_set_pub_option", lambda option, value: (option, value)),
        ((0x3400, 0x3500), "subscriber options", "_set_sub_option", lambda option, value: (option, value)),
        ((0x3500, 0x3600), "stream options", "_set_stream_option", lambda option, value: (option, value)),
    )
    _OPTION_GET_ROUTES = (
        ((5,), "routing IDs", "get_routing_id", lambda option, size: ()),
        ((0x3100, 0x3200), "router options", "_get_router_option", lambda option, size: (option, size)),
        ((0x3300, 0x3400), "publisher options", "_get_pub_option", lambda option, size: (option, size)),
        ((0x3400, 0x3500), "subscriber options", "_get_sub_option", lambda option, size: (option, size)),
        ((0x3500, 0x3600), "stream options", "_get_stream_option", lambda option, size: (option, size)),
    )

    def __init__(self, context, sock_type=None):
        socket_type = self._resolve_socket_type(sock_type)
        handle = lib().zlink_socket(context._handle, socket_type)
        if not handle:
            _raise_last_error()
        self._init_from_native_handle(handle, own=True, socket_type=socket_type)

    @classmethod
    def _resolve_socket_type(cls, sock_type=None):
        resolved = cls._socket_type_value if sock_type is None else sock_type
        if resolved is None:
            raise TypeError("sock_type is required")
        return _native_socket_type(resolved)

    def _init_from_native_handle(self, handle, *, own, socket_type):
        self._socket_handle = _SocketHandle(handle, own)
        self._socket_type = socket_type
        self._recv_handler = None
        self._recv_handler_cb = None
        self._subscribe_handler = None
        self._subscribe_handler_cb = None
        self._send_ready_handler = None
        self._send_ready_handler_cb = None
        self.options = CommonSocketOptions(self)

    @property
    def _handle(self):
        return self._socket_handle.handle

    @_handle.setter
    def _handle(self, value):
        if hasattr(self, "_socket_handle"):
            self._socket_handle.handle = value
        else:
            self._socket_handle = _SocketHandle(value, False)

    @classmethod
    def _from_handle(cls, handle, own=False):
        obj = cls.__new__(cls)
        obj._init_from_native_handle(
            handle,
            own=own,
            socket_type=cls._resolve_socket_type(None),
        )
        return obj

    def attach_discovery(self, discovery):
        rc = lib().zlink_socket_attach_discovery(self._handle, discovery._handle)
        if rc != 0:
            _raise_last_error()

    def _send_native_parts(self, native_parts, flags):
        part_count = len(native_parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native in enumerate(native_parts):
            parts_array[index] = native
        effective_flags = _callback_send_flags(flags)
        rc = lib().zlink_send(self._handle, parts_array, part_count, effective_flags)
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
        effective_flags = _callback_send_flags(flags)
        rc = lib().zlink_send_rid(
            self._handle, ctypes.byref(target), parts_array, part_count, effective_flags
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

    def _native_parts_from_payload(self, payload):
        native_parts = []
        for part in _payload_parts(payload):
            if isinstance(part, Message):
                native_parts.append(_clone_native_msg(part._msg))
                continue
            native = ZlinkMsg()
            _keepalive = _init_msg_from_buffer(native, part, borrow=False)
            _ = _keepalive
            native_parts.append(native)
        return native_parts

    def _unsupported_capability(self, capability):
        actual = _socket_type_name(self._socket_type)
        raise TypeError(f"{actual} sockets do not support {capability}")

    def _require_capability_method(self, method_name, capability):
        method = getattr(self, method_name, None)
        if method is None:
            self._unsupported_capability(capability)
        return method

    def _option_route_matches(self, route_key, option):
        if len(route_key) == 1:
            return int(option) == route_key[0]
        return route_key[0] <= int(option) < route_key[1]

    def _dispatch_option_route(self, option, value, routes):
        for route_key, capability, method_name, args_factory in routes:
            if not self._option_route_matches(route_key, option):
                continue
            method = self._require_capability_method(method_name, capability)
            return method(*args_factory(int(option), value))
        return self._OPTION_ROUTE_MISS

    def _set_routing_id_raw(self, routing_id):
        topic_bytes = _validated_routing_id_bytes(routing_id)
        rc = lib().zlink_set_routing_id(
            self._handle,
            ctypes.c_char_p(topic_bytes),
            len(topic_bytes),
        )
        if rc != 0:
            _raise_last_error()

    def _get_routing_id_raw(self):
        rid = ZlinkRoutingId()
        rc = lib().zlink_get_routing_id(self._handle, ctypes.byref(rid))
        if rc != 0:
            _raise_last_error()
        return _routing_id_bytes(rid)

    def _send_result(self, native_result):
        if int(native_result) < 0:
            _raise_last_error()
        return SendResult(int(native_result))

    def _set_option(self, option: int, value):
        if self._dispatch_option_route(option, value, self._OPTION_SET_ROUTES) is not self._OPTION_ROUTE_MISS:
            return
        self._set_raw_option(lib().zlink_set_option, option, value)

    def _get_option(self, option: int, size: int = 256):
        routed = self._dispatch_option_route(option, size, self._OPTION_GET_ROUTES)
        if routed is not self._OPTION_ROUTE_MISS:
            return routed
        return self._get_raw_option(lib().zlink_get_option, option, size)

    def _set_common_int_option(self, option: int, value):
        self._set_raw_option(lib().zlink_set_option, option, _int32_bytes(value))

    def _get_common_int_option(self, option: int):
        return _read_int32(self._get_raw_option(lib().zlink_get_option, option, 4))

    def _set_common_bool_option(self, option: int, value):
        self._set_raw_option(lib().zlink_set_option, option, _bool_bytes(value))

    def _get_common_bool_option(self, option: int):
        return bool(self._get_common_int_option(option))

    def _set_pub_bool_option(self, option: int, value):
        self._set_pub_option(option, _bool_bytes(value))

    def _get_pub_bool_option(self, option: int):
        return bool(_read_int32(self._get_pub_option(option, 4)))

    def on_send_ready(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")

        def _callback(_, __):
            _enter_callback()
            try:
                handler(self)
            except Exception:
                _report_unhandled_callback_exception(handler)
            finally:
                _leave_callback()

        callback = _SOCKET_SEND_READY_HANDLER(_callback)
        rc = lib().zlink_send_ready_handler(self._handle, callback, None)
        if rc != 0:
            _raise_last_error()
        self._send_ready_handler = handler
        self._send_ready_handler_cb = callback

    def set_send_ready_handler(self, handler):
        _compat_warning(
            "set_send_ready_handler() is deprecated; use on_send_ready() instead"
        )
        self.on_send_ready(handler)

    def open_monitor(self, events=0xFFFF):
        from ._monitor import open_socket_monitor

        return open_socket_monitor(self, events)

    def close(self):
        self._recv_handler = None
        self._recv_handler_cb = None
        self._subscribe_handler = None
        self._subscribe_handler_cb = None
        self._send_ready_handler = None
        self._send_ready_handler_cb = None
        self._socket_handle.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class _Socket(_BaseSocket):
    _dispatch = {}

    def __new__(cls, context, sock_type=None):
        if cls is _Socket:
            target_cls = cls._dispatch.get(_native_socket_type(sock_type))
            if target_cls is None:
                raise ValueError(f"unsupported socket type: {sock_type!r}")
            return super().__new__(target_cls)
        return super().__new__(cls)

    @classmethod
    def register_socket_type(cls, sock_type, socket_cls):
        cls._dispatch[_native_socket_type(sock_type)] = socket_cls


class _BindSocket(_Socket):
    def bind(self, endpoint: str):
        rc = lib().zlink_bind(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_last_error()

    def unbind(self, endpoint: str):
        rc = lib().zlink_unbind(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_last_error()


class _ConnectSocket(_Socket):
    def connect(self, endpoint: str):
        rc = lib().zlink_connect(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_last_error()

    def disconnect(self, endpoint: str):
        rc = lib().zlink_disconnect(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_last_error()


class _EndpointSocket(_BindSocket, _ConnectSocket):
    pass


class _RoutingIdSocket(_Socket):
    def set_routing_id(self, routing_id):
        self._set_routing_id_raw(routing_id)

    def get_routing_id(self):
        return self._get_routing_id_raw()


class _DealerOptionSocket(_Socket):
    def _set_dealer_option(self, option, value):
        self._set_raw_option(lib().zlink_set_dealer_option, option, value)

    @property
    def dealer_options(self):
        return DealerSocketOptions(self)


class _RouterOptionSocket(_Socket):
    def _set_router_option(self, option, value):
        self._set_raw_option(lib().zlink_set_router_option, option, value)

    def _get_router_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_router_option, option, size)

    def _set_router_bool_option(self, option, value):
        self._set_router_option(option, _bool_bytes(value))

    def _get_router_bool_option(self, option):
        return bool(_read_int32(self._get_router_option(option, ctypes.sizeof(ctypes.c_int32))))

    def _set_router_bytes_option(self, option, value):
        self._set_router_option(option, bytes(_as_bytes_view(value)))

    def _get_router_bytes_option(self, option, size: int = 256):
        return self._get_router_option(option, size)


class _StreamOptionSocket(_Socket):
    def _set_stream_option(self, option, value):
        self._set_raw_option(lib().zlink_set_stream_option, option, value)

    def _get_stream_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_stream_option, option, size)

    @property
    def stream_options(self):
        return StreamSocketOptions(self)


class _PublisherOptionSocket(_Socket):
    def _set_pub_option(self, option, value):
        self._set_raw_option(lib().zlink_set_pub_option, option, value)

    def _get_pub_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_pub_option, option, size)


class _SubscriberOptionSocket(_Socket):
    def _set_sub_option(self, option, value):
        self._set_raw_option(lib().zlink_set_sub_option, option, value)

    def _get_sub_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_sub_option, option, size)

    @property
    def subscriber_options(self):
        return SubSocketOptions(self)


class _MessageSocket(_Socket):
    def send(self, payload):
        self._send_native_parts(self._native_parts_from_payload(payload), 0)

    def try_send(self, payload):
        native_parts = self._native_parts_from_payload(payload)
        part_count = len(native_parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native in enumerate(native_parts):
            parts_array[index] = native
        return _try_send_via_native(self._handle, parts_array, part_count)

    def recv(self):
        routing, owner = _recv_native_parts(self._handle, 0)
        return Received(owner, routing)

    def try_recv(self):
        payload = _try_recv_native_parts(self._handle)
        if payload is None:
            return None
        routing, owner = payload
        return Received(owner, routing)

    def on_receive(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")

        def _callback(routing_id_ptr, parts_ptr, part_count, _):
            routing_id = None
            if routing_id_ptr:
                routing_id = _routing_id_bytes(routing_id_ptr.contents)
            received = Received(
                _ReceivedPartsOwner(parts_ptr, int(part_count)),
                routing_id,
            )
            _enter_callback()
            try:
                handler(received)
            except Exception:
                try:
                    received.close()
                finally:
                    _report_unhandled_callback_exception(handler)
            else:
                received.close()
            finally:
                _leave_callback()

        callback = _SOCKET_RECV_HANDLER(_callback)
        rc = lib().zlink_recv_handler(self._handle, callback, None)
        if rc != 0:
            _raise_last_error()
        self._recv_handler = handler
        self._recv_handler_cb = callback

    def set_recv_handler(self, handler):
        _compat_warning("set_recv_handler() is deprecated; use on_receive() instead")
        self.on_receive(handler)


class _RoutedMessageSocket(_MessageSocket):
    def send(self, payload, *, routing_id=None):
        if routing_id is None:
            return super().send(payload)
        self._send_native_parts_to_routing_id(
            routing_id, self._native_parts_from_payload(payload), 0
        )

    def try_send(self, payload, *, routing_id=None):
        if routing_id is None:
            return super().try_send(payload)
        native_parts = self._native_parts_from_payload(payload)
        part_count = len(native_parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native in enumerate(native_parts):
            parts_array[index] = native
        target = _copy_routing_id(routing_id)
        return _try_send_rid_via_native(
            self._handle, target, parts_array, part_count
        )


class _PublisherSocket(_Socket):
    def publish(self, topic, payload):
        topic_bytes = _validated_c_string_bytes(topic, field="topic")
        native_parts = self._native_parts_from_payload(payload)
        part_count = len(native_parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native in enumerate(native_parts):
            parts_array[index] = native
        effective_flags = _callback_send_flags(0)
        rc = lib().zlink_publish(self._handle, topic_bytes, parts_array, part_count, effective_flags)
        if rc < 0:
            for index in range(part_count):
                lib().zlink_msg_close(ctypes.byref(parts_array[index]))
            _raise_last_error()

    def try_publish(self, topic, payload):
        topic_bytes = _validated_c_string_bytes(topic, field="topic")
        native_parts = self._native_parts_from_payload(payload)
        part_count = len(native_parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native in enumerate(native_parts):
            parts_array[index] = native
        return _try_publish_via_native(
            self._handle, topic_bytes, parts_array, part_count
        )


class _SubscriberSocket(_Socket):
    def _subscribe_once(self, flags):
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
            flags,
        )
        if rc != 0:
            _raise_last_error()

        owner = _ReceivedPartsOwner(parts, int(part_count.value))
        topic = topic_buf.raw[: topic_len.value]
        routing = _routing_id_bytes(routing_id)
        return Subscribed(topic, owner, routing)

    def subscribe(self):
        return self._subscribe_once(0)

    def try_subscribe(self):
        try:
            return self._subscribe_once(1)
        except Exception as exc:
            if _is_eagain(exc):
                return None
            raise

    def set_subscription(self, topic):
        topic_bytes = _validated_c_string_bytes(topic, field="subscription")
        rc = lib().zlink_set_subscription(self._handle, topic_bytes)
        if rc != 0:
            _raise_last_error()

    def unset_subscription(self, topic):
        topic_bytes = _validated_c_string_bytes(topic, field="subscription")
        rc = lib().zlink_unset_subscription(self._handle, topic_bytes)
        if rc != 0:
            _raise_last_error()

    def on_subscribe(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")

        def _callback(routing_id_ptr, topic_ptr, topic_len, parts_ptr, part_count, _):
            routing_id = None
            if routing_id_ptr:
                routing_id = _routing_id_bytes(routing_id_ptr.contents)
            topic = b""
            if topic_ptr and topic_len:
                topic = ctypes.string_at(topic_ptr, topic_len)
            received = Subscribed(
                topic,
                _ReceivedPartsOwner(parts_ptr, int(part_count)),
                routing_id,
            )
            _enter_callback()
            try:
                handler(received)
            except Exception:
                try:
                    received.close()
                finally:
                    _report_unhandled_callback_exception(handler)
            else:
                received.close()
            finally:
                _leave_callback()

        callback = _SOCKET_SUBSCRIBE_HANDLER(_callback)
        rc = lib().zlink_subscribe_handler(self._handle, callback, None)
        if rc != 0:
            _raise_last_error()
        self._subscribe_handler = handler
        self._subscribe_handler_cb = callback
