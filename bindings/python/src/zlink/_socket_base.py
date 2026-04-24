# SPDX-License-Identifier: MPL-2.0

import ctypes
import errno as _errno
import queue
import threading

from ._enums import (
    BindResult,
    CloseResult,
    ConfigResult,
    ConnectResult,
    HandlerResult,
    MonitorEventMask,
    SocketOption,
    SocketType,
    SubmitResult,
)
from ._ffi import ZLINK_PART_FINAL, ZLINK_PART_MORE, ZlinkMsg, lib
from ._core import (
    BindError,
    CloseError,
    ConfigError,
    ConnectError,
    HandlerError,
    HandlerResult,
    Message,
    RecvError,
    RecvResult,
    Received,
    TopicMessage,
    SubmitError,
    ZlinkRoutingId,
    _SOCKET_RECV_HANDLER,
    _SOCKET_SEND_READY_HANDLER,
    _SOCKET_SUBSCRIBE_HANDLER,
    _ReceivedPartsOwner,
    _LEGACY_SOCKET_TYPE_MAP,
    _as_bytes_view,
    _clone_native_msg,
    _copy_routing_id,
    _decode_topic_text,
    _init_msg_from_buffer,
    _is_eagain,
    _recv_native_parts,
    _report_unhandled_callback_exception,
    _raise_result_error,
    _routing_id_bytes,
    _validated_c_string_bytes,
    _validated_c_string_text,
    _validated_c_string_value,
    _validated_int32,
    _validated_routing_id_bytes,
    _send_buffer,
    ZlinkError,
)

_callback_depth = threading.local()
_CALLBACK_SENTINEL = object()


def _in_callback():
    return getattr(_callback_depth, "depth", 0) > 0


def _enter_callback():
    _callback_depth.depth = getattr(_callback_depth, "depth", 0) + 1


def _leave_callback():
    _callback_depth.depth = getattr(_callback_depth, "depth", 0) - 1


def _ensure_not_in_callback(operation):
    if _in_callback():
        raise SubmitError(
            SubmitResult.INVALID_STATE,
            _errno.EDEADLK,
        )


def _clone_received_owner(parts_ptr, part_count):
    parts_array = (ZlinkMsg * part_count)()
    for index in range(part_count):
        parts_array[index] = _clone_native_msg(parts_ptr[index])
    return _ReceivedPartsOwner(parts_array, part_count)


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


def _close_send_parts(parts_array, part_count, start=0):
    for index in range(start, part_count):
        lib().zlink_msg_close(ctypes.byref(parts_array[index]))


def _close_native_parts(native_parts, start=0):
    for native in native_parts[start:]:
        lib().zlink_msg_close(ctypes.byref(native))


def _part_flag(part_index, part_count):
    return ZLINK_PART_FINAL if part_index == part_count - 1 else ZLINK_PART_MORE


def _classify_nonblocking_send_errno():
    err = lib().zlink_errno()
    if err == _errno.EAGAIN:
        return SubmitResult.BACKPRESSURED
    if err in (_errno.ENOTCONN, getattr(_errno, "EHOSTUNREACH", _errno.ENOTCONN)):
        return SubmitResult.NOT_CONNECTED
    return SubmitResult.INTERNAL_ERROR


def _send_via_native_no_wait_result(handle, parts_array, part_count):
    for index in range(part_count):
        rc = lib().zlink_send_part(
            handle,
            ctypes.byref(parts_array[index]),
            1,
            _part_flag(index, part_count),
        )
        if rc != 0:
            result = _classify_nonblocking_send_errno()
            _close_send_parts(parts_array, part_count, index)
            return result
    return SubmitResult.OK


def _send_rid_via_native_no_wait_result(handle, routing_id, parts_array, part_count):
    for index in range(part_count):
        rc = lib().zlink_send_part_rid(
            handle,
            ctypes.byref(routing_id),
            ctypes.byref(parts_array[index]),
            1,
            _part_flag(index, part_count),
        )
        if rc != 0:
            result = _classify_nonblocking_send_errno()
            _close_send_parts(parts_array, part_count, index)
            return result
    return SubmitResult.OK


def _publish_via_native_no_wait_result(handle, topic_bytes, parts_array, part_count):
    for index in range(part_count):
        rc = lib().zlink_publish_part(
            handle,
            topic_bytes,
            ctypes.byref(parts_array[index]),
            1,
            _part_flag(index, part_count),
        )
        if rc != 0:
            result = _classify_nonblocking_send_errno()
            _close_send_parts(parts_array, part_count, index)
            return result
    return SubmitResult.OK


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

    @property
    def connect_timeout_ms(self):
        return self._socket._get_common_int_option(SocketOption.CONNECT_TIMEOUT)

    @connect_timeout_ms.setter
    def connect_timeout_ms(self, value):
        self._socket._set_common_int_option(SocketOption.CONNECT_TIMEOUT, value)

    @property
    def ipv6(self):
        return self._socket._get_common_bool_option(SocketOption.IPV6)

    @ipv6.setter
    def ipv6(self, value):
        self._socket._set_common_bool_option(SocketOption.IPV6, value)

    @property
    def tcp_no_delay(self):
        return self._socket._get_common_bool_option(SocketOption.TCP_NODELAY)

    @tcp_no_delay.setter
    def tcp_no_delay(self, value):
        self._socket._set_common_bool_option(SocketOption.TCP_NODELAY, value)

    @property
    def tcp_keepalive(self):
        return self._socket._get_common_int_option(SocketOption.TCP_KEEPALIVE)

    @tcp_keepalive.setter
    def tcp_keepalive(self, value):
        self._socket._set_common_int_option(SocketOption.TCP_KEEPALIVE, value)

    @property
    def heartbeat_interval_ms(self):
        return self._socket._get_common_int_option(SocketOption.HEARTBEAT_IVL)

    @heartbeat_interval_ms.setter
    def heartbeat_interval_ms(self, value):
        self._socket._set_common_int_option(SocketOption.HEARTBEAT_IVL, value)

    @property
    def heartbeat_ttl_ms(self):
        return self._socket._get_common_int_option(SocketOption.HEARTBEAT_TTL)

    @heartbeat_ttl_ms.setter
    def heartbeat_ttl_ms(self, value):
        self._socket._set_common_int_option(SocketOption.HEARTBEAT_TTL, value)

    @property
    def heartbeat_timeout_ms(self):
        return self._socket._get_common_int_option(SocketOption.HEARTBEAT_TIMEOUT)

    @heartbeat_timeout_ms.setter
    def heartbeat_timeout_ms(self, value):
        self._socket._set_common_int_option(SocketOption.HEARTBEAT_TIMEOUT, value)

    @property
    def max_msg_size(self):
        return self._socket._get_common_int_option(SocketOption.MAXMSGSIZE)

    @max_msg_size.setter
    def max_msg_size(self, value):
        self._socket._set_common_int_option(SocketOption.MAXMSGSIZE, value)

    @property
    def backlog(self):
        return self._socket._get_common_int_option(SocketOption.BACKLOG)

    @backlog.setter
    def backlog(self, value):
        self._socket._set_common_int_option(SocketOption.BACKLOG, value)

    @property
    def reconnect_interval_ms(self):
        return self._socket._get_common_int_option(SocketOption.RECONNECT_IVL)

    @reconnect_interval_ms.setter
    def reconnect_interval_ms(self, value):
        self._socket._set_common_int_option(SocketOption.RECONNECT_IVL, value)

    @property
    def reconnect_interval_max_ms(self):
        return self._socket._get_common_int_option(SocketOption.RECONNECT_IVL_MAX)

    @reconnect_interval_max_ms.setter
    def reconnect_interval_max_ms(self, value):
        self._socket._set_common_int_option(SocketOption.RECONNECT_IVL_MAX, value)


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
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()


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
            _raise_result_error(ConfigError, ConfigResult, 701, lib().zlink_errno())
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
        self._recv_handler_thread = None
        self._recv_handler_stop = None
        self._recv_handler_queue = None
        self._subscribe_handler = None
        self._subscribe_handler_cb = None
        self._subscribe_handler_thread = None
        self._subscribe_handler_stop = None
        self._subscribe_handler_queue = None
        self._send_ready_handler = None
        self._send_ready_handler_cb = None
        self._send_ready_handler_thread = None
        self._send_ready_handler_stop = None
        self._send_ready_handler_queue = None
        self._packet_handler = None
        self._packet_handler_cb = None
        self._packet_handler_thread = None
        self._packet_handler_stop = None
        self._packet_handler_queue = None
        self._options = CommonSocketOptions(self)

    @property
    def options(self):
        return self._options

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

    def _send_native_parts(self, native_parts, flags):
        _ensure_not_in_callback("blocking send")
        part_count = len(native_parts)
        for index, native in enumerate(native_parts):
            rc = lib().zlink_send_part(
                self._handle,
                ctypes.byref(native),
                int(flags),
                _part_flag(index, part_count),
            )
            if rc != 0:
                err = lib().zlink_errno()
                _close_native_parts(native_parts, index)
                _raise_result_error(SubmitError, SubmitResult, rc, err)
        return 0

    def _send_native_parts_to_routing_id(self, routing_id, native_parts, flags):
        _ensure_not_in_callback("blocking send")
        target = _copy_routing_id(routing_id)
        part_count = len(native_parts)
        for index, native in enumerate(native_parts):
            rc = lib().zlink_send_part_rid(
                self._handle,
                ctypes.byref(target),
                ctypes.byref(native),
                int(flags),
                _part_flag(index, part_count),
            )
            if rc != 0:
                err = lib().zlink_errno()
                _close_native_parts(native_parts, index)
                _raise_result_error(SubmitError, SubmitResult, rc, err)
        return 0

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
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def _get_raw_option(self, getter, option, size):
        buf = ctypes.create_string_buffer(size)
        out_size = ctypes.c_size_t(size)
        rc = getter(self._handle, int(option), buf, ctypes.byref(out_size))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
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
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def _get_routing_id_raw(self):
        rid = ZlinkRoutingId()
        rc = lib().zlink_get_routing_id(self._handle, ctypes.byref(rid))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return _routing_id_bytes(rid)

    def set_channel_name(self, channel_name):
        channel_bytes = _validated_c_string_bytes(channel_name, "channel_name")
        rc = lib().zlink_socket_set_channel_name(self._handle, channel_bytes)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def get_channel_name(self):
        buf = ctypes.create_string_buffer(256)
        out_size = ctypes.c_size_t()
        rc = lib().zlink_socket_get_channel_name(
            self._handle,
            buf,
            len(buf),
            ctypes.byref(out_size),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return buf.raw[: out_size.value].decode("utf-8")

    def _send_result(self, native_result):
        if int(native_result) < 0:
            _raise_result_error(SubmitError, SubmitResult, SubmitResult.INTERNAL_ERROR, lib().zlink_errno())
        return SubmitResult(int(native_result))

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

    def monitor_open(self, events=MonitorEventMask.ALL):
        from ._monitor import open_socket_monitor

        return open_socket_monitor(self, events)

    def close(self):
        self._recv_handler = None
        recv_stop = self._recv_handler_stop
        recv_thread = self._recv_handler_thread
        recv_queue = self._recv_handler_queue
        if recv_stop is not None:
            recv_stop.set()
        if recv_queue is not None:
            recv_queue.put(_CALLBACK_SENTINEL)
        if (
            recv_thread is not None
            and recv_thread.is_alive()
            and recv_thread is not threading.current_thread()
        ):
            recv_thread.join(timeout=1.0)
        self._recv_handler_cb = None
        self._recv_handler_thread = None
        self._recv_handler_stop = None
        self._recv_handler_queue = None
        self._subscribe_handler = None
        subscribe_stop = self._subscribe_handler_stop
        subscribe_thread = self._subscribe_handler_thread
        subscribe_queue = self._subscribe_handler_queue
        if subscribe_stop is not None:
            subscribe_stop.set()
        if subscribe_queue is not None:
            subscribe_queue.put(_CALLBACK_SENTINEL)
        if (
            subscribe_thread is not None
            and subscribe_thread.is_alive()
            and subscribe_thread is not threading.current_thread()
        ):
            subscribe_thread.join(timeout=1.0)
        self._subscribe_handler_cb = None
        self._subscribe_handler_thread = None
        self._subscribe_handler_stop = None
        self._subscribe_handler_queue = None
        self._send_ready_handler = None
        send_ready_stop = self._send_ready_handler_stop
        send_ready_thread = self._send_ready_handler_thread
        send_ready_queue = self._send_ready_handler_queue
        if send_ready_stop is not None:
            send_ready_stop.set()
        if send_ready_queue is not None:
            send_ready_queue.put(_CALLBACK_SENTINEL)
        if (
            send_ready_thread is not None
            and send_ready_thread.is_alive()
            and send_ready_thread is not threading.current_thread()
        ):
            send_ready_thread.join(timeout=1.0)
        self._send_ready_handler_cb = None
        self._send_ready_handler_thread = None
        self._send_ready_handler_stop = None
        self._send_ready_handler_queue = None
        self._packet_handler = None
        packet_stop = self._packet_handler_stop
        packet_thread = self._packet_handler_thread
        packet_queue = self._packet_handler_queue
        if packet_stop is not None:
            packet_stop.set()
        if packet_queue is not None:
            packet_queue.put(_CALLBACK_SENTINEL)
        if (
            packet_thread is not None
            and packet_thread.is_alive()
            and packet_thread is not threading.current_thread()
        ):
            packet_thread.join(timeout=1.0)
        self._packet_handler_cb = None
        self._packet_handler_thread = None
        self._packet_handler_stop = None
        self._packet_handler_queue = None
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
    def _register_socket_type(cls, sock_type, socket_cls):
        cls._dispatch[_native_socket_type(sock_type)] = socket_cls

    def set_tls_server(self, cert: str, key: str, require_client_cert: bool = False):
        rc = lib().zlink_set_tls_server(
            self._handle,
            _validated_c_string_text(cert, field="cert"),
            _validated_c_string_text(key, field="key"),
            int(require_client_cert),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def set_tls_client(
        self, ca_cert: str | None, hostname: str | None, trust_system: bool = False
    ):
        ca_value = (
            None
            if ca_cert is None
            else _validated_c_string_text(ca_cert, field="ca_cert")
        )
        host_value = (
            None
            if hostname is None
            else _validated_c_string_text(hostname, field="hostname")
        )
        rc = lib().zlink_set_tls_client(
            self._handle, ca_value, host_value, int(trust_system)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())


class _DiscoveryAttachSocket:
    def attach_discovery(self, discovery):
        rc = lib().zlink_socket_attach_discovery(self._handle, discovery._handle)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())


class _SendReadySocket:
    def on_send_ready(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")
        if self._send_ready_handler_thread is not None:
            raise RuntimeError("handler is already attached")

        stop = threading.Event()
        events = queue.SimpleQueue()
        self._send_ready_handler = handler
        self._send_ready_handler_stop = stop
        self._send_ready_handler_queue = events

        def _callback(_, __):
            if stop.is_set():
                return
            events.put(None)

        callback = _SOCKET_SEND_READY_HANDLER(_callback)
        rc = lib().zlink_send_ready_handler(self._handle, callback, None)
        if rc != 0:
            _raise_result_error(HandlerError, HandlerResult, rc, lib().zlink_errno())
        self._send_ready_handler_cb = callback

        def _dispatch():
            while True:
                item = events.get()
                if item is _CALLBACK_SENTINEL:
                    return
                _enter_callback()
                try:
                    handler(self)
                except Exception:
                    _report_unhandled_callback_exception(handler)
                finally:
                    _leave_callback()

        thread = threading.Thread(target=_dispatch, name="zlink-socket-send-ready")
        thread.daemon = True
        self._send_ready_handler_thread = thread
        thread.start()


class _BindSocket(_Socket):
    def bind(self, endpoint: str):
        rc = lib().zlink_bind(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(BindError, BindResult, rc, lib().zlink_errno())

    def unbind(self, endpoint: str):
        rc = lib().zlink_unbind(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())


class _ConnectSocket(_Socket):
    def connect(self, endpoint: str):
        rc = lib().zlink_connect(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())

    def disconnect(self, endpoint: str):
        rc = lib().zlink_disconnect(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())


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
    def send(self, payload, *, flags=0):
        try:
            self._send_native_parts(self._native_parts_from_payload(payload), flags)
            return True
        except SubmitError as ex:
            if (int(flags) & 1) and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise

    def recv(self, *, flags=0):
        try:
            routing, owner = _recv_native_parts(self._handle, flags)
            return Received(owner, routing)
        except RecvError as ex:
            if (int(flags) & 1) and ex.result == RecvResult.NO_DATA:
                return None
            raise

    def _attach_recv_handler(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")
        if self._recv_handler_thread is not None:
            raise RuntimeError("handler is already attached")

        stop = threading.Event()
        events = queue.SimpleQueue()
        self._recv_handler = handler
        self._recv_handler_stop = stop
        self._recv_handler_queue = events

        def _callback(routing_id_ptr, parts_ptr, part_count, _):
            if stop.is_set():
                return
            try:
                routing_id = None
                if routing_id_ptr:
                    routing_id = _routing_id_bytes(routing_id_ptr.contents)
                received = Received(
                    _clone_received_owner(parts_ptr, int(part_count)),
                    routing_id,
                )
                events.put(received)
            except Exception:
                _report_unhandled_callback_exception(handler)

        callback = _SOCKET_RECV_HANDLER(_callback)
        rc = lib().zlink_recv_handler(self._handle, callback, None)
        if rc != 0:
            _raise_result_error(HandlerError, HandlerResult, rc, lib().zlink_errno())
        self._recv_handler_cb = callback
        def _dispatch():
            while True:
                item = events.get()
                if item is _CALLBACK_SENTINEL:
                    return
                _enter_callback()
                try:
                    handler(item)
                except Exception:
                    try:
                        item.close()
                    finally:
                        _report_unhandled_callback_exception(handler)
                else:
                    item.close()
                finally:
                    _leave_callback()

        thread = threading.Thread(target=_dispatch, name="zlink-socket-recv")
        thread.daemon = True
        self._recv_handler_thread = thread
        thread.start()


class _RoutedMessageSocket(_MessageSocket):
    def send(self, routing_id, payload, *, flags=0):
        try:
            self._send_native_parts_to_routing_id(
                routing_id, self._native_parts_from_payload(payload), flags
            )
            return True
        except SubmitError as ex:
            if (int(flags) & 1) and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise


class _PublisherSocket(_Socket):
    def publish(self, topic, payload, *, flags=0):
        try:
            _ensure_not_in_callback("blocking publish")
            topic_bytes = _validated_c_string_value(topic, field="topic")
            native_parts = self._native_parts_from_payload(payload)
            part_count = len(native_parts)
            for index, native in enumerate(native_parts):
                rc = lib().zlink_publish_part(
                    self._handle,
                    topic_bytes,
                    ctypes.byref(native),
                    int(flags),
                    _part_flag(index, part_count),
                )
                if rc != 0:
                    err = lib().zlink_errno()
                    _close_native_parts(native_parts, index)
                    _raise_result_error(SubmitError, SubmitResult, rc, err)
            return True
        except SubmitError as ex:
            if (int(flags) & 1) and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise


class _SubscriberSocket(_Socket):
    def _subscribe_once(self, flags):
        routing_id = ctypes.POINTER(ZlinkRoutingId)()
        topic_buf = ctypes.create_string_buffer(256)
        parts = []
        try:
            while True:
                topic_len = ctypes.c_size_t()
                native_part = ZlinkMsg()
                has_more = ctypes.c_int()
                rc = lib().zlink_subscribe_part(
                    self._handle,
                    ctypes.byref(routing_id),
                    topic_buf,
                    len(topic_buf),
                    ctypes.byref(topic_len),
                    ctypes.byref(native_part),
                    ctypes.byref(has_more),
                    int(flags),
                )
                if rc != 0:
                    _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
                parts.append(native_part)
                if has_more.value == 0:
                    break
        except Exception:
            _close_native_parts(parts)
            raise

        part_count = len(parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native_part in enumerate(parts):
            parts_array[index] = native_part
        owner = _ReceivedPartsOwner(parts_array, part_count)
        topic = _decode_topic_text(topic_buf.raw[: topic_len.value])
        routing = _routing_id_bytes(routing_id.contents) if routing_id else None
        return TopicMessage(topic, owner, routing)

    def subscribe(self, *, flags=0):
        try:
            return self._subscribe_once(flags)
        except RecvError as ex:
            if (int(flags) & 1) and ex.result == RecvResult.NO_DATA:
                return None
            raise

    def set_subscription(self, topic):
        topic_bytes = _validated_c_string_value(topic, field="subscription")
        rc = lib().zlink_set_subscription(self._handle, topic_bytes)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def unset_subscription(self, topic):
        topic_bytes = _validated_c_string_value(topic, field="subscription")
        rc = lib().zlink_unset_subscription(self._handle, topic_bytes)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def _attach_subscribe_handler(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")
        if self._subscribe_handler_thread is not None:
            raise RuntimeError("handler is already attached")

        stop = threading.Event()
        events = queue.SimpleQueue()
        self._subscribe_handler = handler
        self._subscribe_handler_stop = stop
        self._subscribe_handler_queue = events

        def _callback(routing_id_ptr, topic_ptr, topic_len, parts_ptr, part_count, _):
            if stop.is_set():
                return
            try:
                routing_id = None
                if routing_id_ptr:
                    routing_id = _routing_id_bytes(routing_id_ptr.contents)
                topic = ""
                if topic_ptr and topic_len:
                    topic = _decode_topic_text(ctypes.string_at(topic_ptr, topic_len))
                received = TopicMessage(
                    topic,
                    _clone_received_owner(parts_ptr, int(part_count)),
                    routing_id,
                )
                events.put(received)
            except Exception:
                _report_unhandled_callback_exception(handler)

        callback = _SOCKET_SUBSCRIBE_HANDLER(_callback)
        rc = lib().zlink_subscribe_handler(self._handle, callback, None)
        if rc != 0:
            _raise_result_error(HandlerError, HandlerResult, rc, lib().zlink_errno())
        self._subscribe_handler_cb = callback
        def _dispatch():
            while True:
                item = events.get()
                if item is _CALLBACK_SENTINEL:
                    return
                _enter_callback()
                try:
                    handler(item)
                except Exception:
                    try:
                        item.close()
                    finally:
                        _report_unhandled_callback_exception(handler)
                else:
                    item.close()
                finally:
                    _leave_callback()

        thread = threading.Thread(target=_dispatch, name="zlink-socket-subscribe")
        thread.daemon = True
        self._subscribe_handler_thread = thread
        thread.start()
