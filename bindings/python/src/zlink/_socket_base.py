# SPDX-License-Identifier: MPL-2.0

import ctypes
import warnings

from ._enums import SocketType
from ._ffi import ZlinkMsg, lib
from ._core import (
    Message,
    ReceivedMessage,
    ReceivedMultipart,
    ReceivedTopicMessage,
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
    _msg_data_ptr,
    _msg_to_bytes,
    _raise_last_error,
    _recv_native_parts,
    _report_unhandled_callback_exception,
    _routing_id_bytes,
    _send_buffer,
)


def _compat_warning(message):
    warnings.warn(message, DeprecationWarning, stacklevel=3)


def _native_socket_type(sock_type):
    return _LEGACY_SOCKET_TYPE_MAP.get(int(sock_type), int(sock_type))


def _socket_type_name(socket_type):
    try:
        return SocketType(int(socket_type)).name
    except ValueError:
        return str(int(socket_type))


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
    _routing_id_socket_types = {
        int(SocketType.DEALER),
        int(SocketType.ROUTER),
        int(SocketType.STREAM),
    }
    _subscribe_socket_types = {
        int(SocketType.SUB),
        int(SocketType.XSUB),
    }
    _pub_option_socket_types = {
        int(SocketType.PUB),
        int(SocketType.XPUB),
    }
    _sub_option_socket_types = _subscribe_socket_types
    _dealer_option_socket_types = {int(SocketType.DEALER)}
    _router_option_socket_types = {int(SocketType.ROUTER)}
    _stream_option_socket_types = {int(SocketType.STREAM)}

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
        self._pending_send_parts = []
        self._legacy_recv_queue = []
        self._recv_handler = None
        self._recv_handler_cb = None
        self._subscribe_handler = None
        self._subscribe_handler_cb = None
        self._send_ready_handler = None
        self._send_ready_handler_cb = None

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

    def _require_socket_type(self, allowed_socket_types, capability):
        if self._socket_type in allowed_socket_types:
            return
        actual = _socket_type_name(self._socket_type)
        supported = ", ".join(
            _socket_type_name(socket_type) for socket_type in sorted(allowed_socket_types)
        )
        raise TypeError(
            f"{actual} sockets do not support {capability}; supported socket types: {supported}"
        )

    def set_option(self, option: int, value):
        if int(option) == 5:
            self._require_socket_type(self._routing_id_socket_types, "routing IDs")
            topic_bytes = bytes(_as_bytes_view(value))
            rc = lib().zlink_set_routing_id(
                self._handle,
                ctypes.c_char_p(topic_bytes),
                len(topic_bytes),
            )
        elif int(option) == 6:
            self._require_socket_type(self._subscribe_socket_types, "subscriptions")
            rc = lib().zlink_set_subscription(
                self._handle, bytes(_as_bytes_view(value))
            )
        elif int(option) == 7:
            self._require_socket_type(self._subscribe_socket_types, "subscriptions")
            rc = lib().zlink_unset_subscription(
                self._handle, bytes(_as_bytes_view(value))
            )
        elif int(option) == 40:
            self._require_socket_type(
                self._pub_option_socket_types, "publisher options"
            )
            self.set_pub_option(0x3301, value)
            return
        elif 0x3100 <= int(option) < 0x3200:
            self._require_socket_type(self._router_option_socket_types, "router options")
            self.set_router_option(option, value)
            return
        elif 0x3200 <= int(option) < 0x3300:
            self._require_socket_type(self._dealer_option_socket_types, "dealer options")
            self.set_dealer_option(option, value)
            return
        elif 0x3300 <= int(option) < 0x3400:
            self._require_socket_type(self._pub_option_socket_types, "publisher options")
            self.set_pub_option(option, value)
            return
        elif 0x3400 <= int(option) < 0x3500:
            self._require_socket_type(
                self._sub_option_socket_types, "subscriber options"
            )
            self.set_sub_option(option, value)
            return
        elif 0x3500 <= int(option) < 0x3600:
            self._require_socket_type(self._stream_option_socket_types, "stream options")
            self.set_stream_option(option, value)
            return
        else:
            self._set_raw_option(lib().zlink_set_option, option, value)
            return
        if rc != 0:
            _raise_last_error()

    def get_option(self, option: int, size: int = 256):
        if int(option) == 5:
            self._require_socket_type(self._routing_id_socket_types, "routing IDs")
            rid = ZlinkRoutingId()
            rc = lib().zlink_get_routing_id(self._handle, ctypes.byref(rid))
            if rc != 0:
                _raise_last_error()
            return bytes(rid.data[: rid.size])
        if 0x3100 <= int(option) < 0x3200:
            self._require_socket_type(self._router_option_socket_types, "router options")
            return self.get_router_option(option, size)
        if 0x3300 <= int(option) < 0x3400:
            self._require_socket_type(self._pub_option_socket_types, "publisher options")
            return self.get_pub_option(option, size)
        if 0x3400 <= int(option) < 0x3500:
            self._require_socket_type(
                self._sub_option_socket_types, "subscriber options"
            )
            return self.get_sub_option(option, size)
        if 0x3500 <= int(option) < 0x3600:
            self._require_socket_type(self._stream_option_socket_types, "stream options")
            return self.get_stream_option(option, size)
        return self._get_raw_option(lib().zlink_get_option, option, size)

    def set_routing_id(self, routing_id):
        self.set_option(5, routing_id)

    def get_routing_id(self) -> bytes:
        return self.get_option(5)

    def on_send_ready(self, handler):
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

    def set_send_ready_handler(self, handler):
        _compat_warning(
            "set_send_ready_handler() is deprecated; use on_send_ready() instead"
        )
        self.on_send_ready(handler)

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
        self._socket_handle.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class Socket(_BaseSocket):
    _dispatch = {}

    def __new__(cls, context, sock_type=None):
        if cls is Socket:
            target_cls = cls._dispatch.get(_native_socket_type(sock_type))
            if target_cls is None:
                raise ValueError(f"unsupported socket type: {sock_type!r}")
            return super().__new__(target_cls)
        return super().__new__(cls)

    @classmethod
    def register_socket_type(cls, sock_type, socket_cls):
        cls._dispatch[_native_socket_type(sock_type)] = socket_cls

class MessageSocket(Socket):
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

    def send(self, data, flags: int = 0):
        if int(flags) & 0x2:
            self._pending_send_parts.append(data)
            return len(bytes(_as_bytes_view(data)))

        if self._pending_send_parts:
            parts = self._pending_send_parts + [data]
            self._pending_send_parts = []
            if self._socket_type == int(SocketType.ROUTER) and parts:
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

    def on_receive(self, handler):
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

    def set_recv_handler(self, handler):
        _compat_warning("set_recv_handler() is deprecated; use on_receive() instead")
        self.on_receive(handler)


class PublisherSocket(Socket):
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


class SubscriberSocket(Socket):
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

    def on_topic_message(self, handler):
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

    def set_subscribe_handler(self, handler):
        _compat_warning(
            "set_subscribe_handler() is deprecated; use on_topic_message() instead"
        )
        self.on_topic_message(handler)
