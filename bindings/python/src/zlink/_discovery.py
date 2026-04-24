# SPDX-License-Identifier: MPL-2.0

import ctypes
from dataclasses import dataclass

from ._enums import (
    DiscoveryDealerPeerMode,
    RegistryState,
    ServiceMonitorMask,
    ServiceRole,
    TopologySource,
    TopologyState,
)
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
    BindError,
    BindResult,
    CloseError,
    CloseResult,
    ConfigError,
    ConfigResult,
    ConnectError,
    ConnectResult,
    RoutingId,
    _copy_routing_id,
    _msg_to_bytes,
    _raise_config_error_from_errno,
    _raise_result_error,
    _routing_id_bytes,
    _validated_c_string_text,
    _validated_uint32,
)


def _decode_fixed(buf):
    return bytes(buf).split(b"\0", 1)[0].decode("utf-8", errors="replace")


@dataclass(frozen=True)
class MemberPeerEntry:
    service_type: int
    service_role: int
    service_name: str
    endpoint: str
    routing_id: bytes
    weight: int
    value: int


@dataclass(frozen=True)
class RegistryStatus:
    registry_id: int
    bind_endpoint: str
    state: int
    topology_entry_count: int
    peer_registry_count: int
    connected_peer_registry_count: int
    list_seq: int
    last_error: int
    last_changed_ms: int


@dataclass(frozen=True)
class RegistryServiceSummaryEntry:
    service_kind: int
    service_role: int
    service_name: str
    total_count: int
    connecting_count: int
    ready_count: int
    error_count: int
    stopped_count: int
    last_reported_ms: int


@dataclass(frozen=True)
class RegistryServiceSummaryFilter:
    service_kind: int | None = None
    service_role: int | None = None
    service_name: str | None = None


@dataclass(frozen=True)
class RegistryTopologyEntry:
    routing_id: bytes
    service_kind: int
    service_role: int
    service_name: str
    endpoint: str
    source: int
    state: int
    desired_count: int
    ready_count: int
    error_code: int
    last_reported_ms: int


@dataclass(frozen=True)
class RegistryTopologyFilter:
    service_kind: int | None = None
    service_role: int | None = None
    service_name: str | None = None
    routing_id: bytes | None = None
    state: int | None = None
    source: int | None = None


def _member_peer_from_native(entry):
    return MemberPeerEntry(
        service_type=int(entry.service_type),
        service_role=ServiceRole(int(entry.service_role)),
        service_name=_decode_fixed(entry.service_name),
        endpoint=_decode_fixed(entry.endpoint),
        routing_id=_routing_id_bytes(entry.routing_id),
        weight=int(entry.weight),
        value=int(entry.value),
    )


def _query_member_peers(handle, fn, *args):
    count = ctypes.c_size_t(0)
    rc = fn(handle, *args, None, ctypes.byref(count))
    if rc != 0:
        _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
    if count.value == 0:
        return []

    entries = (ZlinkMemberPeerEntry * count.value)()
    rc = fn(handle, *args, entries, ctypes.byref(count))
    if rc != 0:
        _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
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
        _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
    if count.value == 0:
        return []

    entries = (ZlinkRegistryTopologyEntry * count.value)()
    rc = fn(handle, filter_ptr, entries, ctypes.byref(count))
    if rc != 0:
        _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
    return [_topology_entry_from_native(entry) for entry in entries[: count.value]]


def _build_topology_filter(filter_):
    native = ZlinkRegistryTopologyFilter()
    native.service_kind = 0 if filter_.service_kind is None else int(filter_.service_kind)
    native.service_role = 0 if filter_.service_role is None else int(filter_.service_role)
    native.service_name = _validated_c_string_text(
        filter_.service_name or "",
        field="service_name",
        max_length=255,
    )
    if filter_.routing_id is not None:
        native.routing_id = _copy_routing_id(filter_.routing_id)
    else:
        native.routing_id.size = 0
    native.state = 0 if filter_.state is None else int(filter_.state)
    native.source = 0 if filter_.source is None else int(filter_.source)
    return native


class Registry:
    def __init__(self, ctx):
        self._handle = lib().zlink_registry_new(ctx._handle)
        if not self._handle:
            _raise_config_error_from_errno()

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
            _raise_result_error(BindError, BindResult, rc, lib().zlink_errno())

    def set_id(self, registry_id: int):
        rc = lib().zlink_registry_set_id(
            self._handle, _validated_uint32(registry_id, field="registry_id")
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def add_peer(self, peer_pub_endpoint: str):
        rc = lib().zlink_registry_add_peer(
            self._handle,
            _validated_c_string_text(
                peer_pub_endpoint, field="peer_pub_endpoint", max_length=255
            ),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

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
            None if hostname is None else _validated_c_string_text(hostname, field="hostname")
        )
        rc = lib().zlink_set_tls_client(
            self._handle, ca_value, host_value, int(trust_system)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

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
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def status_snapshot(self):
        native = ZlinkRegistryStatus()
        rc = lib().zlink_registry_status_snapshot(self._handle, ctypes.byref(native))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
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
            filter_native.service_kind = 0 if filter_.service_kind is None else int(filter_.service_kind)
            filter_native.service_role = 0 if filter_.service_role is None else int(filter_.service_role)
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
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []

        entries = (ZlinkRegistryServiceSummaryEntry * count.value)()
        rc = lib().zlink_registry_service_summary_snapshot(
            self._handle, filter_ptr, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
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
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
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
                _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
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
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
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
            _raise_config_error_from_errno()

    def connect_registry(self, registry_endpoint: str):
        rc = lib().zlink_discovery_connect_registry(
            self._handle,
            _validated_c_string_text(
                registry_endpoint, field="registry_endpoint", max_length=255
            ),
        )
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())

    def set_value(self, value: int):
        rc = lib().zlink_discovery_set_value(self._handle, int(value))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def get_value(self) -> int:
        value = ctypes.c_int64()
        rc = lib().zlink_discovery_get_value(self._handle, ctypes.byref(value))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
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
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def get_metadata(self) -> bytes:
        msg = ZlinkMsg()
        rc = lib().zlink_msg_init(ctypes.byref(msg))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        try:
            rc = lib().zlink_discovery_get_metadata(self._handle, ctypes.byref(msg))
            if rc != 0:
                _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
            return _msg_to_bytes(msg)
        finally:
            lib().zlink_msg_close(ctypes.byref(msg))

    def member_peers(self):
        return _query_member_peers(self._handle, lib().zlink_discovery_member_peers)

    def member_peer_metadata(self, service_role: int, endpoint: str) -> bytes:
        msg = ZlinkMsg()
        rc = lib().zlink_msg_init(ctypes.byref(msg))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        try:
            rc = lib().zlink_discovery_member_peer_metadata(
                self._handle,
                int(service_role),
                _validated_c_string_text(endpoint, field="endpoint", max_length=255),
                ctypes.byref(msg),
            )
            if rc != 0:
                _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
            return _msg_to_bytes(msg)
        finally:
            lib().zlink_msg_close(ctypes.byref(msg))

    def resolve_spot(self, spot_rid):
        native_spot_rid = _copy_routing_id(spot_rid)
        owner_node_rid = type(native_spot_rid)()
        rc = lib().zlink_discovery_resolve_spot(
            self._handle,
            ctypes.byref(native_spot_rid),
            ctypes.byref(owner_node_rid),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return RoutingId(_routing_id_bytes(owner_node_rid))

    def set_dealer_peer_mode(self, mode: DiscoveryDealerPeerMode):
        rc = lib().zlink_discovery_set_dealer_peer_mode(self._handle, int(mode))
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
            None if hostname is None else _validated_c_string_text(hostname, field="hostname")
        )
        rc = lib().zlink_set_tls_client(
            self._handle, ca_value, host_value, int(trust_system)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def monitor_open(self, events=ServiceMonitorMask.ALL):
        from ._monitor import open_service_monitor

        return open_service_monitor(self, events)

    def close(self):
        if not self._handle:
            return
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_discovery_destroy(ctypes.byref(handle))
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


class RegistryQueryClient:
    def __init__(self, ctx):
        self._handle = lib().zlink_registry_query_client_new(ctx._handle)
        if not self._handle:
            _raise_config_error_from_errno()

    def connect(self, endpoint: str):
        rc = lib().zlink_registry_query_client_connect(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())

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
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()
