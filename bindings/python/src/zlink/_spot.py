# SPDX-License-Identifier: MPL-2.0

import ctypes

from ._ffi import ZlinkMsg, ZlinkRoutingId, lib
from ._core import (
    Message,
    ReceivedTopicMessage,
    _SOCKET_SEND_READY_HANDLER,
    _ReceivedPartsOwner,
    _as_bytes_view,
    _clone_native_msg,
    _init_msg_from_buffer,
    _report_unhandled_callback_exception,
    _raise_last_error,
    _routing_id_bytes,
)


_SPOT_SUBSCRIBE_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(ZlinkRoutingId),
    ctypes.c_char_p,
    ctypes.c_size_t,
    ctypes.POINTER(ZlinkMsg),
    ctypes.c_size_t,
    ctypes.c_void_p,
)


class SpotNode:
    def __init__(self, ctx):
        self._handle = lib().zlink_spot_node_new(ctx._handle)
        if not self._handle:
            _raise_last_error()

    def bind(self, endpoint: str):
        rc = lib().zlink_spot_node_bind(self._handle, endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def connect_peer(self, endpoint: str):
        rc = lib().zlink_spot_node_connect_peer(self._handle, endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def disconnect_peer(self, endpoint: str):
        rc = lib().zlink_spot_node_disconnect_peer(self._handle, endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def attach_discovery(self, discovery):
        rc = lib().zlink_spot_node_attach_discovery(
            self._handle, discovery._handle
        )
        if rc != 0:
            _raise_last_error()

    def set_option(self, option: int, value):
        if int(option) == 5:
            self.set_routing_id(value)
            return
        view = _as_bytes_view(value)
        if view.nbytes == 0:
            rc = lib().zlink_set_option(self._handle, int(option), None, 0)
        elif view.readonly:
            raw = view.tobytes()
            rc = lib().zlink_set_option(
                self._handle, int(option), ctypes.c_char_p(raw), len(raw)
            )
        else:
            rc = lib().zlink_set_option(
                self._handle,
                int(option),
                ctypes.c_void_p(
                    ctypes.addressof(
                        (ctypes.c_ubyte * view.nbytes).from_buffer(view)
                    )
                ),
                view.nbytes,
            )
        if rc != 0:
            _raise_last_error()

    def set_routing_id(self, routing_id):
        raw = bytes(_as_bytes_view(routing_id))
        rc = lib().zlink_set_routing_id(
            self._handle, ctypes.c_char_p(raw), len(raw)
        )
        if rc != 0:
            _raise_last_error()

    def get_routing_id(self) -> bytes:
        routing_id = ZlinkRoutingId()
        rc = lib().zlink_get_routing_id(self._handle, ctypes.byref(routing_id))
        if rc != 0:
            _raise_last_error()
        return bytes(routing_id.data[: routing_id.size])

    def set_tls_server(self, cert: str, key: str, require_client_cert: bool = False):
        rc = lib().zlink_set_tls_server(
            self._handle, cert.encode(), key.encode(), int(require_client_cert)
        )
        if rc != 0:
            _raise_last_error()

    def set_tls_client(
        self, ca_cert: str | None, hostname: str | None, trust_system: bool = False
    ):
        ca_value = None if ca_cert is None else ca_cert.encode()
        host_value = None if hostname is None else hostname.encode()
        rc = lib().zlink_set_tls_client(
            self._handle, ca_value, host_value, int(trust_system)
        )
        if rc != 0:
            _raise_last_error()

    def open_monitor(self, events):
        from ._monitor import open_service_monitor

        return open_service_monitor(self, events)

    def close(self):
        if not self._handle:
            return
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_spot_node_destroy(ctypes.byref(handle))
        self._handle = None
        if rc != 0:
            _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class Spot:
    def __init__(self, ctx_or_node):
        self._own = not isinstance(ctx_or_node, SpotNode)
        self._handler = None
        self._handler_cb = None
        self._send_ready_handler = None
        self._send_ready_handler_cb = None
        if self._own:
            self._handle = lib().zlink_spot_new(ctx_or_node._handle)
        else:
            self._handle = ctx_or_node._handle
        if not self._handle:
            _raise_last_error()

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
                continue
            native = ZlinkMsg()
            _ = _init_msg_from_buffer(native, part, borrow=False)
            native_parts.append(native)

        part_count = len(native_parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native in enumerate(native_parts):
            parts_array[index] = native
        rc = lib().zlink_publish(
            self._handle, topic_bytes, parts_array, part_count, int(flags)
        )
        if rc != 0:
            for index in range(part_count):
                lib().zlink_msg_close(ctypes.byref(parts_array[index]))
            _raise_last_error()

    def recv(self, flags: int = 0):
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
        return ReceivedTopicMessage(
            topic_buf.raw[: topic_len.value],
            owner,
            _routing_id_bytes(routing_id),
        )

    def subscribe(self, topic):
        rc = lib().zlink_set_subscription(self._handle, bytes(_as_bytes_view(topic)))
        if rc != 0:
            _raise_last_error()

    def unsubscribe(self, topic):
        rc = lib().zlink_unset_subscription(self._handle, bytes(_as_bytes_view(topic)))
        if rc != 0:
            _raise_last_error()

    def set_pub_option(self, option, value):
        view = _as_bytes_view(value)
        if view.nbytes == 0:
            rc = lib().zlink_set_pub_option(self._handle, int(option), None, 0)
        elif view.readonly:
            raw = view.tobytes()
            rc = lib().zlink_set_pub_option(
                self._handle, int(option), ctypes.c_char_p(raw), len(raw)
            )
        else:
            rc = lib().zlink_set_pub_option(
                self._handle,
                int(option),
                ctypes.c_void_p(
                    ctypes.addressof(
                        (ctypes.c_ubyte * view.nbytes).from_buffer(view)
                    )
                ),
                view.nbytes,
            )
        if rc != 0:
            _raise_last_error()

    def set_sub_option(self, option, value):
        view = _as_bytes_view(value)
        if view.nbytes == 0:
            rc = lib().zlink_set_sub_option(self._handle, int(option), None, 0)
        elif view.readonly:
            raw = view.tobytes()
            rc = lib().zlink_set_sub_option(
                self._handle, int(option), ctypes.c_char_p(raw), len(raw)
            )
        else:
            rc = lib().zlink_set_sub_option(
                self._handle,
                int(option),
                ctypes.c_void_p(
                    ctypes.addressof(
                        (ctypes.c_ubyte * view.nbytes).from_buffer(view)
                    )
                ),
                view.nbytes,
            )
        if rc != 0:
            _raise_last_error()

    def set_handler(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")
        if self._handler_cb is not None:
            raise RuntimeError("subscribe handler is already attached")

        def _callback(routing_id_ptr, topic_ptr, topic_len, parts_ptr, part_count, _):
            routing_id = None
            if routing_id_ptr:
                routing_id = _routing_id_bytes(routing_id_ptr.contents)
            topic = b""
            if topic_ptr and topic_len:
                topic = ctypes.string_at(topic_ptr, topic_len)
            owner = _ReceivedPartsOwner(parts_ptr, int(part_count))
            message = ReceivedTopicMessage(topic, owner, routing_id)
            try:
                handler(message)
            except Exception:
                try:
                    message.close()
                finally:
                    _report_unhandled_callback_exception(handler)
            else:
                message.close()

        callback = _SPOT_SUBSCRIBE_HANDLER(_callback)
        rc = lib().zlink_subscribe_handler(self._handle, callback, None)
        if rc != 0:
            _raise_last_error()
        self._handler = handler
        self._handler_cb = callback

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

    def open_monitor(self, events):
        from ._monitor import open_service_monitor

        return open_service_monitor(self, events)

    def close(self):
        if not self._handle:
            return
        self._handler = None
        self._handler_cb = None
        self._send_ready_handler = None
        self._send_ready_handler_cb = None
        if self._own:
            handle = ctypes.c_void_p(self._handle)
            rc = lib().zlink_spot_destroy(ctypes.byref(handle))
            self._handle = None
            if rc != 0:
                _raise_last_error()
            return
        self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
