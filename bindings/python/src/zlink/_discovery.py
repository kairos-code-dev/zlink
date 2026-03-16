# SPDX-License-Identifier: MPL-2.0

import ctypes
from ._ffi import lib
from ._core import _raise_last_error, ZlinkMsg, Message, _ZlinkRoutingId, _as_bytes_view, _send_buffer


class ReceiverInfo(ctypes.Structure):
    _fields_ = [
        ("service_name", ctypes.c_char * 256),
        ("endpoint", ctypes.c_char * 256),
        ("routing_id", ctypes.c_ubyte * 256),
        ("weight", ctypes.c_uint),
        ("registered_at", ctypes.c_uint64),
    ]


class PeerInfo(ctypes.Structure):
    _fields_ = [
        ("routing_id", _ZlinkRoutingId),
        ("remote_addr", ctypes.c_char * 256),
        ("connected_time", ctypes.c_uint64),
        ("msgs_sent", ctypes.c_uint64),
        ("msgs_received", ctypes.c_uint64),
        ("snd_pending_msgs", ctypes.c_uint64),
        ("rcv_pending_msgs", ctypes.c_uint64),
    ]


def _to_routing_id_struct(routing_id):
    rid_view = _as_bytes_view(routing_id)
    rid_len = rid_view.nbytes
    if rid_len <= 0 or rid_len > 255:
        raise ValueError("routing_id length must be between 1 and 255")
    rid = _ZlinkRoutingId()
    rid.size = rid_len
    for i in range(rid_len):
        rid.data[i] = rid_view[i]
    return rid


def _peer_to_dict(peer):
    return {
        "routing_id": bytes(peer.routing_id.data[: peer.routing_id.size]),
        "remote_addr": peer.remote_addr.split(b"\0", 1)[0].decode(),
        "connected_time": int(peer.connected_time),
        "msgs_sent": int(peer.msgs_sent),
        "msgs_received": int(peer.msgs_received),
        "snd_pending_msgs": int(peer.snd_pending_msgs),
        "rcv_pending_msgs": int(peer.rcv_pending_msgs),
    }


def _query_peers(handle, fn):
    count = ctypes.c_size_t(0)
    rc = fn(handle, None, ctypes.byref(count))
    if rc != 0:
        _raise_last_error()
    if count.value == 0:
        return []

    arr = (PeerInfo * count.value)()
    rc = fn(handle, ctypes.cast(arr, ctypes.c_void_p), ctypes.byref(count))
    if rc != 0:
        _raise_last_error()

    return [_peer_to_dict(arr[i]) for i in range(count.value)]


class Registry:
    def __init__(self, ctx):
        self._handle = lib().zlink_registry_new(ctx._handle)
        if not self._handle:
            _raise_last_error()

    def set_endpoints(self, pub_ep, router_ep):
        rc = lib().zlink_registry_set_endpoints(self._handle, pub_ep.encode(), router_ep.encode())
        if rc != 0:
            _raise_last_error()

    def set_id(self, rid: int):
        rc = lib().zlink_registry_set_id(self._handle, rid)
        if rc != 0:
            _raise_last_error()

    def add_peer(self, pub_ep):
        rc = lib().zlink_registry_add_peer(self._handle, pub_ep.encode())
        if rc != 0:
            _raise_last_error()

    def set_heartbeat(self, interval_ms, timeout_ms):
        rc = lib().zlink_registry_set_heartbeat(self._handle, interval_ms, timeout_ms)
        if rc != 0:
            _raise_last_error()

    def set_broadcast_interval(self, interval_ms):
        rc = lib().zlink_registry_set_broadcast_interval(self._handle, interval_ms)
        if rc != 0:
            _raise_last_error()

    def start(self):
        rc = lib().zlink_registry_start(self._handle)
        if rc != 0:
            _raise_last_error()

    def set_sockopt(self, role, option, value: bytes):
        buf = ctypes.create_string_buffer(value)
        rc = lib().zlink_registry_setsockopt(self._handle, role, option, buf, len(value))
        if rc != 0:
            _raise_last_error()

    def close(self):
        if self._handle:
            handle = ctypes.c_void_p(self._handle)
            lib().zlink_registry_destroy(ctypes.byref(handle))
            self._handle = None


class Discovery:
    def __init__(self, ctx, service_type):
        self._handle = lib().zlink_discovery_new_typed(ctx._handle, service_type)
        if not self._handle:
            _raise_last_error()

    def connect_registry(self, registry_pub):
        rc = lib().zlink_discovery_connect_registry(self._handle, registry_pub.encode())
        if rc != 0:
            _raise_last_error()

    def receiver_count(self, service):
        rc = lib().zlink_discovery_receiver_count(self._handle, service.encode())
        if rc < 0:
            _raise_last_error()
        return rc

    def service_available(self, service):
        rc = lib().zlink_discovery_service_available(self._handle, service.encode())
        if rc < 0:
            _raise_last_error()
        return rc != 0

    def get_receivers(self, service):
        count = self.receiver_count(service)
        if count <= 0:
            return []
        arr = (ReceiverInfo * count)()
        sz = ctypes.c_size_t(count)
        rc = lib().zlink_discovery_get_receivers(self._handle, service.encode(), ctypes.byref(arr), ctypes.byref(sz))
        if rc != 0:
            _raise_last_error()
        result = []
        for i in range(sz.value):
            info = arr[i]
            result.append({
                "service_name": info.service_name.split(b"\0", 1)[0].decode(),
                "endpoint": info.endpoint.split(b"\0", 1)[0].decode(),
                "weight": info.weight,
                "registered_at": info.registered_at,
            })
        return result

    def set_tls_client(self, ca_cert: str, hostname: str, trust_system: int = 0):
        ca_buf = ca_cert.encode() if ca_cert is not None else None
        hostname_buf = hostname.encode() if hostname is not None else None
        rc = lib().zlink_discovery_set_tls_client(
            self._handle, ca_buf, hostname_buf, int(trust_system)
        )
        if rc != 0:
            _raise_last_error()

    def close(self):
        if self._handle:
            handle = ctypes.c_void_p(self._handle)
            lib().zlink_discovery_destroy(ctypes.byref(handle))
            self._handle = None


class Gateway:
    def __init__(self, ctx, discovery, routing_id=None):
        rid = routing_id.encode() if routing_id else None
        self._handle = lib().zlink_gateway_new(ctx._handle, discovery._handle, rid)
        if not self._handle:
            _raise_last_error()

    def send(self, service, payload_or_parts, flags=0):
        if isinstance(payload_or_parts, (list, tuple)):
            arr, built = _build_msg_array(payload_or_parts)
            rc = lib().zlink_gateway_send(
                self._handle,
                service.encode(),
                ctypes.byref(arr),
                len(payload_or_parts),
                flags,
            )
            if rc != 0:
                _close_msg_array(arr, built)
                _raise_last_error()
            return

        payload = payload_or_parts.encode() if isinstance(payload_or_parts, str) else payload_or_parts
        payload_buf, payload_size, keepalive = _send_buffer(payload)
        rc = lib().zlink_gateway_send_bytes(
            self._handle, service.encode(), payload_buf, payload_size, flags
        )
        if rc != 0:
            _raise_last_error()
        _ = keepalive

    def send_to_routing_id(self, service, routing_id, payload_or_parts, flags=0):
        rid = _to_routing_id_struct(routing_id)

        if isinstance(payload_or_parts, (list, tuple)):
            arr, built = _build_msg_array(payload_or_parts)
            rc = lib().zlink_gateway_send_rid(
                self._handle,
                service.encode(),
                ctypes.byref(rid),
                ctypes.byref(arr),
                len(payload_or_parts),
                flags,
            )
            if rc != 0:
                _close_msg_array(arr, built)
                _raise_last_error()
            return

        payload = payload_or_parts.encode() if isinstance(payload_or_parts, str) else payload_or_parts
        payload_buf, payload_size, keepalive = _send_buffer(payload)
        rc = lib().zlink_gateway_send_rid_bytes(
            self._handle,
            service.encode(),
            ctypes.byref(rid),
            payload_buf,
            payload_size,
            flags,
        )
        if rc != 0:
            _raise_last_error()
        _ = keepalive

    def recv(self, flags=0):
        parts = ctypes.c_void_p()
        count = ctypes.c_size_t()
        name_buf = ctypes.create_string_buffer(256)
        rc = lib().zlink_gateway_recv(self._handle, ctypes.byref(parts), ctypes.byref(count), flags, name_buf)
        if rc != 0:
            _raise_last_error()
        service = name_buf.value.decode()
        messages = _parts_to_bytes(parts, count.value)
        return service, messages

    def set_lb_strategy(self, service, strategy):
        rc = lib().zlink_gateway_set_lb_strategy(self._handle, service.encode(), strategy)
        if rc != 0:
            _raise_last_error()

    def set_tls_client(self, ca_cert, hostname, trust_system=0):
        rc = lib().zlink_gateway_set_tls_client(self._handle, ca_cert.encode(), hostname.encode(), trust_system)
        if rc != 0:
            _raise_last_error()

    def connection_count(self, service):
        rc = lib().zlink_gateway_connection_count(self._handle, service.encode())
        if rc < 0:
            _raise_last_error()
        return rc

    def router_socket(self):
        handle = lib().zlink_gateway_router_socket_unsafe(self._handle)
        if not handle:
            _raise_last_error()
        from ._core import Socket
        return Socket._from_handle(handle, own=False)

    def router_peers(self):
        return _query_peers(self._handle, lib().zlink_gateway_router_peers)

    def set_sockopt(self, option, value: bytes):
        buf = ctypes.create_string_buffer(value)
        rc = lib().zlink_gateway_setsockopt(self._handle, option, buf, len(value))
        if rc != 0:
            _raise_last_error()

    def close(self):
        if self._handle:
            handle = ctypes.c_void_p(self._handle)
            lib().zlink_gateway_destroy(ctypes.byref(handle))
            self._handle = None


class Receiver:
    def __init__(self, ctx, routing_id=None):
        rid = routing_id.encode() if routing_id else None
        self._handle = lib().zlink_receiver_new(ctx._handle, rid)
        if not self._handle:
            _raise_last_error()

    def bind(self, endpoint):
        rc = lib().zlink_receiver_bind(self._handle, endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def connect_registry(self, endpoint):
        rc = lib().zlink_receiver_connect_registry(self._handle, endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def register(self, service, advertise_endpoint, weight=1):
        rc = lib().zlink_receiver_register(self._handle, service.encode(), advertise_endpoint.encode(), weight)
        if rc != 0:
            _raise_last_error()

    def update_weight(self, service, weight):
        rc = lib().zlink_receiver_update_weight(self._handle, service.encode(), weight)
        if rc != 0:
            _raise_last_error()

    def unregister(self, service):
        rc = lib().zlink_receiver_unregister(self._handle, service.encode())
        if rc != 0:
            _raise_last_error()

    def register_result(self, service):
        status = ctypes.c_int()
        endpoint = ctypes.create_string_buffer(256)
        error = ctypes.create_string_buffer(256)
        rc = lib().zlink_receiver_register_result(self._handle, service.encode(), ctypes.byref(status), endpoint, error)
        if rc != 0:
            _raise_last_error()
        return status.value, endpoint.value.decode(), error.value.decode()

    def set_tls_server(self, cert, key):
        rc = lib().zlink_receiver_set_tls_server(self._handle, cert.encode(), key.encode())
        if rc != 0:
            _raise_last_error()

    def set_sockopt(self, role, option, value: bytes):
        buf = ctypes.create_string_buffer(value)
        rc = lib().zlink_receiver_setsockopt(self._handle, role, option, buf, len(value))
        if rc != 0:
            _raise_last_error()

    def router_socket(self):
        handle = lib().zlink_receiver_router_socket_unsafe(self._handle)
        if not handle:
            _raise_last_error()
        from ._core import Socket
        return Socket._from_handle(handle, own=False)

    def router_peers(self):
        return _query_peers(self._handle, lib().zlink_receiver_router_peers)

    def close(self):
        if self._handle:
            handle = ctypes.c_void_p(self._handle)
            lib().zlink_receiver_destroy(ctypes.byref(handle))
            self._handle = None


def _init_msg_from_bytes(data: bytes) -> ZlinkMsg:
    msg = ZlinkMsg()
    rc = lib().zlink_msg_init_size(ctypes.byref(msg), len(data))
    if rc != 0:
        _raise_last_error()
    if data:
        ptr = lib().zlink_msg_data(ctypes.byref(msg))
        ctypes.memmove(ptr, data, len(data))
    return msg


def _clone_msg(src: Message) -> ZlinkMsg:
    msg = ZlinkMsg()
    rc = lib().zlink_msg_init(ctypes.byref(msg))
    if rc != 0:
        _raise_last_error()
    rc = lib().zlink_msg_copy(ctypes.byref(msg), ctypes.byref(src._msg))
    if rc != 0:
        _raise_last_error()
    return msg


def _build_msg_array(parts):
    if not parts:
        raise ValueError("parts required")
    arr = (ZlinkMsg * len(parts))()
    built = 0
    for i, msg in enumerate(parts):
        if isinstance(msg, Message):
            arr[i] = _clone_msg(msg)
        else:
            arr[i] = _init_msg_from_bytes(bytes(msg))
        built += 1
    return arr, built


def _close_msg_array(arr, count: int):
    for i in range(count):
        lib().zlink_msg_close(ctypes.byref(arr[i]))


def _parts_to_bytes(parts_ptr, count):
    if not parts_ptr or count == 0:
        return []
    arr = ctypes.cast(parts_ptr, ctypes.POINTER(ZlinkMsg * count)).contents
    out = []
    for i in range(count):
        msg = arr[i]
        size = lib().zlink_msg_size(ctypes.byref(msg))
        data_ptr = lib().zlink_msg_data(ctypes.byref(msg))
        out.append(ctypes.string_at(data_ptr, size))
    lib().zlink_multipart_close(parts_ptr, count)
    return out
