# SPDX-License-Identifier: MPL-2.0

import ctypes
import errno

from ._enums import (
    SendResult,
    SpotNodeState,
    SpotPeerSource,
    SpotPeerState,
)
from ._ffi import (
    ZlinkMsg,
    ZlinkRoutingId,
    ZlinkSpotNodePeerEntry,
    ZlinkSpotNodePeerFilter,
    ZlinkSpotNodeStatus,
    ZlinkSpotNodeSubjectEntry,
    ZlinkSpotNodeSubjectFilter,
    lib,
)
from ._core import (
    Message,
    Subscribed,
    _SOCKET_SEND_READY_HANDLER,
    _ReceivedPartsOwner,
    _as_bytes_view,
    _clone_native_msg,
    _is_eagain,
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


def _try_send_result_from_errno():
    err = lib().zlink_errno()
    if err in (errno.EAGAIN, 10035):
        return SendResult.BACKPRESSURED
    if err in (errno.ENOTCONN, 10057, errno.EHOSTUNREACH, 10065,
               errno.ETIMEDOUT, 10060):
        return SendResult.NOT_READY
    return None


def _decode_fixed(buf):
    return bytes(buf).split(b"\0", 1)[0].decode("utf-8", errors="replace")


def _fixed_buffer_value(value, size):
    raw = b""
    if value is not None:
        raw = bytes(_as_bytes_view(value))
    if len(raw) >= size:
        raise ValueError(f"value exceeds fixed buffer size {size - 1}")
    return raw


class SpotNodeStatus:
    def __init__(
        self,
        *,
        service_name,
        local_endpoint,
        node_routing_id,
        state,
        configured_peer_count,
        active_peer_count,
        connected_peer_count,
        subject_count,
        ready_subject_count,
        last_error,
        last_changed_ms,
    ):
        self.service_name = service_name
        self.local_endpoint = local_endpoint
        self.node_routing_id = node_routing_id
        self.state = state
        self.configured_peer_count = configured_peer_count
        self.active_peer_count = active_peer_count
        self.connected_peer_count = connected_peer_count
        self.subject_count = subject_count
        self.ready_subject_count = ready_subject_count
        self.last_error = last_error
        self.last_changed_ms = last_changed_ms


class SpotNodePeerEntry:
    def __init__(
        self,
        *,
        service_name,
        local_endpoint,
        peer_endpoint,
        source,
        state,
        connected_since_ms,
        last_changed_ms,
    ):
        self.service_name = service_name
        self.local_endpoint = local_endpoint
        self.peer_endpoint = peer_endpoint
        self.source = source
        self.state = state
        self.connected_since_ms = connected_since_ms
        self.last_changed_ms = last_changed_ms


class SpotNodePeerFilter:
    def __init__(self, *, peer_endpoint=None, source=None, state=None):
        self.peer_endpoint = peer_endpoint
        self.source = source
        self.state = state


class SpotNodeSubjectEntry:
    def __init__(
        self,
        *,
        role,
        subject,
        subject_kind,
        ready_peer_count,
        active_peer_count,
        last_changed_ms,
    ):
        self.role = role
        self.subject = subject
        self.subject_kind = subject_kind
        self.ready_peer_count = ready_peer_count
        self.active_peer_count = active_peer_count
        self.last_changed_ms = last_changed_ms


class SpotNodeSubjectFilter:
    def __init__(self, *, role=None, subject=None, subject_kind=0):
        self.role = role
        self.subject = subject
        self.subject_kind = subject_kind


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

    def _set_option(self, option: int, value):
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

    def wrap_handle(self):
        handle = lib().zlink_spot_new(self._handle)
        if not handle:
            _raise_last_error()
        return Spot._from_native_handle(handle)

    def status_snapshot(self):
        native = ZlinkSpotNodeStatus()
        rc = lib().zlink_spot_node_status_snapshot(self._handle, ctypes.byref(native))
        if rc != 0:
            _raise_last_error()
        return SpotNodeStatus(
            service_name=_decode_fixed(native.service_name),
            local_endpoint=_decode_fixed(native.local_endpoint),
            node_routing_id=_routing_id_bytes(native.node_routing_id),
            state=SpotNodeState(int(native.state)),
            configured_peer_count=int(native.configured_peer_count),
            active_peer_count=int(native.active_peer_count),
            connected_peer_count=int(native.connected_peer_count),
            subject_count=int(native.subject_count),
            ready_subject_count=int(native.ready_subject_count),
            last_error=int(native.last_error),
            last_changed_ms=int(native.last_changed_ms),
        )

    def peers_snapshot(self):
        return self.peers_query(None)

    def peers_query(self, filter_=None):
        count = ctypes.c_size_t()
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = ZlinkSpotNodePeerFilter()
            filter_native.peer_endpoint = _fixed_buffer_value(
                filter_.peer_endpoint, 256
            )
            filter_native.source = 0 if filter_.source is None else int(filter_.source)
            filter_native.state = 0 if filter_.state is None else int(filter_.state)
            filter_ptr = ctypes.byref(filter_native)
        rc = lib().zlink_spot_node_peers_query(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_last_error()
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodePeerEntry * int(count.value))()
        rc = lib().zlink_spot_node_peers_query(
            self._handle, filter_ptr, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_last_error()
        return [
            SpotNodePeerEntry(
                service_name=_decode_fixed(entry.service_name),
                local_endpoint=_decode_fixed(entry.local_endpoint),
                peer_endpoint=_decode_fixed(entry.peer_endpoint),
                source=SpotPeerSource(int(entry.source)),
                state=SpotPeerState(int(entry.state)),
                connected_since_ms=int(entry.connected_since_ms),
                last_changed_ms=int(entry.last_changed_ms),
            )
            for entry in entries[: int(count.value)]
        ]

    def subjects_snapshot(self, filter_=None):
        count = ctypes.c_size_t()
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = ZlinkSpotNodeSubjectFilter()
            filter_native.role = 0 if filter_.role is None else int(filter_.role)
            filter_native.subject = _fixed_buffer_value(filter_.subject, 256)
            filter_native.subject_kind = int(filter_.subject_kind)
            filter_ptr = ctypes.byref(filter_native)
        rc = lib().zlink_spot_node_subjects_snapshot(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_last_error()
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeSubjectEntry * int(count.value))()
        rc = lib().zlink_spot_node_subjects_snapshot(
            self._handle, filter_ptr, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_last_error()
        return [
            SpotNodeSubjectEntry(
                role=int(entry.role),
                subject=_decode_fixed(entry.subject),
                subject_kind=int(entry.subject_kind),
                ready_peer_count=int(entry.ready_peer_count),
                active_peer_count=int(entry.active_peer_count),
                last_changed_ms=int(entry.last_changed_ms),
            )
            for entry in entries[: int(count.value)]
        ]

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
    @classmethod
    def _from_native_handle(cls, handle):
        obj = cls.__new__(cls)
        obj._own = True
        obj._handle = handle
        obj._handler = None
        obj._handler_cb = None
        obj._send_ready_handler = None
        obj._send_ready_handler_cb = None
        return obj

    def __init__(self, node):
        self._handler = None
        self._handler_cb = None
        self._send_ready_handler = None
        self._send_ready_handler_cb = None
        self._own = True
        if not isinstance(node, SpotNode):
            raise TypeError("Spot requires a SpotNode")
        self._handle = lib().zlink_spot_new(node._handle)
        if not self._handle:
            _raise_last_error()

    def _native_parts_from_payload(self, payload):
        if isinstance(payload, (list, tuple)):
            parts = list(payload)
        else:
            parts = [payload]
        if not parts:
            raise ValueError("parts must not be empty")
        native_parts = []
        for part in parts:
            if isinstance(part, Message):
                native_parts.append(_clone_native_msg(part._msg))
                continue
            native = ZlinkMsg()
            _ = _init_msg_from_buffer(native, part, borrow=False)
            native_parts.append(native)
        return native_parts

    def publish(self, topic, payload):
        topic_bytes = bytes(_as_bytes_view(topic))
        native_parts = self._native_parts_from_payload(payload)
        part_count = len(native_parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native in enumerate(native_parts):
            parts_array[index] = native
        rc = lib().zlink_publish(
            self._handle, topic_bytes, parts_array, part_count, 0
        )
        if rc != 0:
            for index in range(part_count):
                lib().zlink_msg_close(ctypes.byref(parts_array[index]))
            _raise_last_error()

    def try_publish(self, topic, payload):
        topic_bytes = bytes(_as_bytes_view(topic))
        native_parts = self._native_parts_from_payload(payload)
        part_count = len(native_parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native in enumerate(native_parts):
            parts_array[index] = native
        result = lib().zlink_publish(
            self._handle, topic_bytes, parts_array, part_count, 1
        )
        if int(result) == 0:
            return SendResult.SENT
        mapped = _try_send_result_from_errno()
        if mapped is None:
            _raise_last_error()
        return mapped

    def _recv_subscribed(self, flags):
        routing_id = ZlinkRoutingId()
        parts = ctypes.POINTER(ZlinkMsg)()
        part_count = ctypes.c_size_t()
        topic_buf = ctypes.create_string_buffer(256)
        topic_len = ctypes.c_size_t(len(topic_buf))
        native = lib()
        rc = native.zlink_subscribe(
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
        return Subscribed(topic, owner, _routing_id_bytes(routing_id))

    def recv(self):
        return self._recv_subscribed(0)

    def try_recv(self):
        try:
            return self._recv_subscribed(1)
        except Exception as exc:
            if _is_eagain(exc):
                return None
            raise

    def set_subscription(self, topic):
        rc = lib().zlink_set_subscription(self._handle, bytes(_as_bytes_view(topic)))
        if rc != 0:
            _raise_last_error()

    def unset_subscription(self, topic):
        rc = lib().zlink_unset_subscription(self._handle, bytes(_as_bytes_view(topic)))
        if rc != 0:
            _raise_last_error()

    def _set_pub_option(self, option, value):
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

    def _set_sub_option(self, option, value):
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
            message = Subscribed(topic, owner, routing_id)
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
        handle = ctypes.c_void_p(self._handle)
        self._handle = None
        rc = lib().zlink_spot_destroy(ctypes.byref(handle))
        if rc != 0:
            _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
