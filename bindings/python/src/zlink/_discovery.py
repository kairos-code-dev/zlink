# SPDX-License-Identifier: MPL-2.0

import ctypes

from ._enums import RegistryState, ServiceRole, TopologySource, TopologyState
from ._ffi import (
    ZlinkMemberPeerEntry,
    ZlinkMsg,
    ZlinkRegistryServiceSummaryEntry,
    ZlinkRegistryServiceSummaryFilter,
    ZlinkRegistryStatus,
    ZlinkRegistryTopologyEntry,
    ZlinkRegistryTopologyFilter,
    lib,
)
from ._core import (
    _copy_routing_id,
    _msg_to_bytes,
    _raise_last_error,
    _routing_id_bytes,
    _validated_c_string_text,
    _validated_uint32,
)


def _decode_fixed(buf):
    return bytes(buf).split(b"\0", 1)[0].decode("utf-8", errors="replace")


class MemberPeerEntry:
    def __init__(
        self,
        *,
        service_type,
        service_role,
        service_name,
        endpoint,
        routing_id,
        value,
    ):
        self.service_type = service_type
        self.service_role = service_role
        self.service_name = service_name
        self.endpoint = endpoint
        self.routing_id = routing_id
        self.value = value


class RegistryStatus:
    def __init__(
        self,
        *,
        registry_id,
        bind_endpoint,
        state,
        topology_entry_count,
        peer_registry_count,
        connected_peer_registry_count,
        list_seq,
        last_error,
        last_changed_ms,
    ):
        self.registry_id = registry_id
        self.bind_endpoint = bind_endpoint
        self.state = state
        self.topology_entry_count = topology_entry_count
        self.peer_registry_count = peer_registry_count
        self.connected_peer_registry_count = connected_peer_registry_count
        self.list_seq = list_seq
        self.last_error = last_error
        self.last_changed_ms = last_changed_ms


class RegistryServiceSummaryEntry:
    def __init__(
        self,
        *,
        service_kind,
        service_role,
        service_name,
        total_count,
        connecting_count,
        ready_count,
        error_count,
        stopped_count,
        last_reported_ms,
    ):
        self.service_kind = service_kind
        self.service_role = service_role
        self.service_name = service_name
        self.total_count = total_count
        self.connecting_count = connecting_count
        self.ready_count = ready_count
        self.error_count = error_count
        self.stopped_count = stopped_count
        self.last_reported_ms = last_reported_ms


class RegistryServiceSummaryFilter:
    def __init__(self, *, service_kind=0, service_role=0, service_name=None):
        self.service_kind = service_kind
        self.service_role = service_role
        self.service_name = service_name


class RegistryTopologyEntry:
    def __init__(
        self,
        *,
        routing_id,
        service_kind,
        service_role,
        service_name,
        endpoint,
        source,
        state,
        desired_count,
        ready_count,
        error_code,
        last_reported_ms,
    ):
        self.routing_id = routing_id
        self.service_kind = service_kind
        self.service_role = service_role
        self.service_name = service_name
        self.endpoint = endpoint
        self.source = source
        self.state = state
        self.desired_count = desired_count
        self.ready_count = ready_count
        self.error_code = error_code
        self.last_reported_ms = last_reported_ms


class RegistryTopologyFilter:
    def __init__(
        self,
        *,
        service_kind=0,
        service_role=0,
        service_name=None,
        routing_id=None,
        state=0,
        source=0,
    ):
        self.service_kind = service_kind
        self.service_role = service_role
        self.service_name = service_name
        self.routing_id = routing_id
        self.state = state
        self.source = source


def _member_peer_from_native(entry):
    return MemberPeerEntry(
        service_type=int(entry.service_type),
        service_role=ServiceRole(int(entry.service_role)),
        service_name=_decode_fixed(entry.service_name),
        endpoint=_decode_fixed(entry.endpoint),
        routing_id=_routing_id_bytes(entry.routing_id),
        value=int(entry.value),
    )


def _query_member_peers(handle, fn, *args):
    count = ctypes.c_size_t(0)
    rc = fn(handle, *args, None, ctypes.byref(count))
    if rc != 0:
        _raise_last_error()
    if count.value == 0:
        return []

    entries = (ZlinkMemberPeerEntry * count.value)()
    rc = fn(handle, *args, entries, ctypes.byref(count))
    if rc != 0:
        _raise_last_error()
    return [_member_peer_from_native(entries[index]) for index in range(count.value)]


def _topology_entry_from_native(entry):
    return RegistryTopologyEntry(
        routing_id=_routing_id_bytes(entry.routing_id),
        service_kind=int(entry.service_kind),
        service_role=ServiceRole(int(entry.service_role)),
        service_name=_decode_fixed(entry.service_name),
        endpoint=_decode_fixed(entry.endpoint),
        source=TopologySource(int(entry.source)),
        state=TopologyState(int(entry.state)),
        desired_count=int(entry.desired_count),
        ready_count=int(entry.ready_count),
        error_code=int(entry.error_code),
        last_reported_ms=int(entry.last_reported_ms),
    )


def _query_topology(handle, fn, filter_ptr=None):
    count = ctypes.c_size_t(0)
    rc = fn(handle, filter_ptr, None, ctypes.byref(count))
    if rc != 0:
        _raise_last_error()
    if count.value == 0:
        return []

    entries = (ZlinkRegistryTopologyEntry * count.value)()
    rc = fn(handle, filter_ptr, entries, ctypes.byref(count))
    if rc != 0:
        _raise_last_error()
    return [_topology_entry_from_native(entry) for entry in entries[: count.value]]


def _build_topology_filter(filter_):
    native = ZlinkRegistryTopologyFilter()
    native.service_kind = int(filter_.service_kind)
    native.service_role = int(filter_.service_role)
    native.service_name = _validated_c_string_text(
        filter_.service_name or "",
        field="service_name",
        max_length=255,
    )
    if filter_.routing_id is not None:
        native.routing_id = _copy_routing_id(filter_.routing_id)
    else:
        native.routing_id.size = 0
    native.state = int(filter_.state)
    native.source = int(filter_.source)
    return native


class Registry:
    def __init__(self, ctx):
        self._handle = lib().zlink_registry_new(ctx._handle)
        if not self._handle:
            _raise_last_error()

    def bind(self, pub_endpoint: str, router_endpoint: str):
        rc = lib().zlink_registry_bind(
            self._handle,
            _validated_c_string_text(
                pub_endpoint, field="pub_endpoint", max_length=255
            ),
            _validated_c_string_text(
                router_endpoint, field="router_endpoint", max_length=255
            ),
        )
        if rc != 0:
            _raise_last_error()

    def set_id(self, registry_id: int):
        rc = lib().zlink_registry_set_id(
            self._handle, _validated_uint32(registry_id, field="registry_id")
        )
        if rc != 0:
            _raise_last_error()

    def add_peer(self, peer_pub_endpoint: str):
        rc = lib().zlink_registry_add_peer(
            self._handle,
            _validated_c_string_text(
                peer_pub_endpoint, field="peer_pub_endpoint", max_length=255
            ),
        )
        if rc != 0:
            _raise_last_error()

    def set_heartbeat(self, interval_ms: int, timeout_ms: int):
        rc = lib().zlink_registry_set_heartbeat(
            self._handle,
            _validated_uint32(interval_ms, field="interval_ms"),
            _validated_uint32(timeout_ms, field="timeout_ms"),
        )
        if rc != 0:
            _raise_last_error()

    def set_broadcast_interval(self, interval_ms: int):
        rc = lib().zlink_registry_set_broadcast_interval(
            self._handle, _validated_uint32(interval_ms, field="interval_ms")
        )
        if rc != 0:
            _raise_last_error()

    def status_snapshot(self):
        native = ZlinkRegistryStatus()
        rc = lib().zlink_registry_status_snapshot(self._handle, ctypes.byref(native))
        if rc != 0:
            _raise_last_error()
        return RegistryStatus(
            registry_id=int(native.registry_id),
            bind_endpoint=_decode_fixed(native.bind_endpoint),
            state=RegistryState(int(native.state)),
            topology_entry_count=int(native.topology_entry_count),
            peer_registry_count=int(native.peer_registry_count),
            connected_peer_registry_count=int(native.connected_peer_registry_count),
            list_seq=int(native.list_seq),
            last_error=int(native.last_error),
            last_changed_ms=int(native.last_changed_ms),
        )

    def service_summary_snapshot(self, filter_=None):
        count = ctypes.c_size_t(0)
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = ZlinkRegistryServiceSummaryFilter()
            filter_native.service_kind = int(filter_.service_kind)
            filter_native.service_role = int(filter_.service_role)
            filter_native.service_name = _validated_c_string_text(
                filter_.service_name or "",
                field="service_name",
                max_length=255,
            )
            filter_ptr = ctypes.byref(filter_native)

        rc = lib().zlink_registry_service_summary_snapshot(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_last_error()
        if count.value == 0:
            return []

        entries = (ZlinkRegistryServiceSummaryEntry * count.value)()
        rc = lib().zlink_registry_service_summary_snapshot(
            self._handle, filter_ptr, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_last_error()
        return [
            RegistryServiceSummaryEntry(
                service_kind=int(entry.service_kind),
                service_role=ServiceRole(int(entry.service_role)),
                service_name=_decode_fixed(entry.service_name),
                total_count=int(entry.total_count),
                connecting_count=int(entry.connecting_count),
                ready_count=int(entry.ready_count),
                error_count=int(entry.error_count),
                stopped_count=int(entry.stopped_count),
                last_reported_ms=int(entry.last_reported_ms),
            )
            for entry in entries[: count.value]
        ]

    def member_peers(self, service_type, service_name):
        return _query_member_peers(
            self._handle,
            lib().zlink_registry_member_peers,
            int(service_type),
            _validated_c_string_text(
                service_name, field="service_name", max_length=255
            ),
        )

    def member_peer_metadata(
        self, service_type, service_name, service_role: int, endpoint: str
    ) -> bytes:
        msg = ZlinkMsg()
        rc = lib().zlink_msg_init(ctypes.byref(msg))
        if rc != 0:
            _raise_last_error()
        try:
            rc = lib().zlink_registry_member_peer_metadata(
                self._handle,
                int(service_type),
                _validated_c_string_text(
                    service_name, field="service_name", max_length=255
                ),
                int(service_role),
                _validated_c_string_text(endpoint, field="endpoint", max_length=255),
                ctypes.byref(msg),
            )
            if rc != 0:
                _raise_last_error()
            return _msg_to_bytes(msg)
        finally:
            lib().zlink_msg_close(ctypes.byref(msg))

    def topology_snapshot(self):
        return _query_topology(self._handle, lib().zlink_registry_topology_snapshot)

    def topology_query(self, filter_):
        return _query_topology(
            self._handle,
            lib().zlink_registry_topology_query,
            ctypes.byref(_build_topology_filter(filter_)),
        )

    def close(self):
        if not self._handle:
            return
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_registry_destroy(ctypes.byref(handle))
        self._handle = None
        if rc != 0:
            _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class Discovery:
    def __init__(self, ctx, service_type, service_name: str):
        self._handle = lib().zlink_discovery_new(
            ctx._handle,
            int(service_type),
            _validated_c_string_text(
                service_name, field="service_name", max_length=255
            ),
        )
        if not self._handle:
            _raise_last_error()

    def connect_registry(self, registry_endpoint: str):
        rc = lib().zlink_discovery_connect_registry(
            self._handle,
            _validated_c_string_text(
                registry_endpoint, field="registry_endpoint", max_length=255
            ),
        )
        if rc != 0:
            _raise_last_error()

    def set_value(self, value: int):
        rc = lib().zlink_discovery_set_value(self._handle, int(value))
        if rc != 0:
            _raise_last_error()

    def get_value(self) -> int:
        value = ctypes.c_int64()
        rc = lib().zlink_discovery_get_value(self._handle, ctypes.byref(value))
        if rc != 0:
            _raise_last_error()
        return int(value.value)

    def set_metadata(self, data):
        if not data:
            rc = lib().zlink_discovery_set_metadata(self._handle, None, 0)
        else:
            raw = memoryview(data).tobytes()
            rc = lib().zlink_discovery_set_metadata(
                self._handle, ctypes.c_char_p(raw), len(raw)
            )
        if rc != 0:
            _raise_last_error()

    def get_metadata(self) -> bytes:
        msg = ZlinkMsg()
        rc = lib().zlink_msg_init(ctypes.byref(msg))
        if rc != 0:
            _raise_last_error()
        try:
            rc = lib().zlink_discovery_get_metadata(self._handle, ctypes.byref(msg))
            if rc != 0:
                _raise_last_error()
            return _msg_to_bytes(msg)
        finally:
            lib().zlink_msg_close(ctypes.byref(msg))

    def member_peers(self):
        return _query_member_peers(self._handle, lib().zlink_discovery_member_peers)

    def member_peer_metadata(self, service_role: int, endpoint: str) -> bytes:
        msg = ZlinkMsg()
        rc = lib().zlink_msg_init(ctypes.byref(msg))
        if rc != 0:
            _raise_last_error()
        try:
            rc = lib().zlink_discovery_member_peer_metadata(
                self._handle,
                int(service_role),
                _validated_c_string_text(endpoint, field="endpoint", max_length=255),
                ctypes.byref(msg),
            )
            if rc != 0:
                _raise_last_error()
            return _msg_to_bytes(msg)
        finally:
            lib().zlink_msg_close(ctypes.byref(msg))

    def open_monitor(self, events):
        from ._monitor import open_service_monitor

        return open_service_monitor(self, events)

    def close(self):
        if not self._handle:
            return
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_discovery_destroy(ctypes.byref(handle))
        self._handle = None
        if rc != 0:
            _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class RegistryQueryClient:
    def __init__(self, ctx):
        self._handle = lib().zlink_registry_query_client_new(ctx._handle)
        if not self._handle:
            _raise_last_error()

    def connect(self, endpoint: str):
        rc = lib().zlink_registry_query_client_connect(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_last_error()

    def snapshot(self, filter_=None):
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = _build_topology_filter(filter_)
            filter_ptr = ctypes.byref(filter_native)
        return _query_topology(self._handle, lib().zlink_registry_query_snapshot, filter_ptr)

    def close(self):
        if not self._handle:
            return
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_registry_query_destroy(ctypes.byref(handle))
        self._handle = None
        if rc != 0:
            _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
