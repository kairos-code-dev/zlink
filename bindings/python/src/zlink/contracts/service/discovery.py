# SPDX-License-Identifier: MPL-2.0

import ctypes
from dataclasses import dataclass

from ..sockets.codes import SocketOption
from .codes import (
    AutoConnectType,
    RegistryState,
    ServiceKind,
    ServiceRole,
    SpotKind,
    TopologySource,
    TopologyState,
)
from ..._native.ffi import (
    ZlinkActorRoute,
    ZlinkMemberPeerEntry,
    ZlinkMsg,
    ZlinkRegistryServiceSummaryEntry,
    ZlinkRegistryServiceSummaryFilter,
    ZlinkRegistryStatus,
    ZlinkRegistryTopologyEntry,
    ZlinkRegistryTopologyFilter,
    ZlinkSpotRoute,
    lib,
)
from .spot import ActorRoute, _actor_id_bytes, _actor_ref_from_native
from ..._runtime.handles.native_support import (
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
    auto_connect_type: AutoConnectType
    service_role: ServiceRole
    channel_name: str
    endpoint: str
    routing_id: RoutingId
    value: int
    weight: int


@dataclass(frozen=True)
class RegistryStatus:
    registry_id: int
    bind_endpoint: str
    state: RegistryState
    topology_entry_count: int
    peer_registry_count: int
    connected_peer_registry_count: int
    list_seq: int
    last_error: int
    last_changed_ms: int


@dataclass(frozen=True)
class RegistryServiceSummaryEntry:
    auto_connect_type: AutoConnectType
    service_role: ServiceRole
    channel_name: str
    total_count: int
    connecting_count: int
    ready_count: int
    error_count: int
    stopped_count: int
    last_reported_ms: int


@dataclass(frozen=True)
class RegistryServiceSummaryFilter:
    auto_connect_type: AutoConnectType | None = None
    service_role: ServiceRole | None = None
    channel_name: str | None = None


@dataclass(frozen=True)
class RegistryTopologyEntry:
    auto_connect_type: AutoConnectType
    routing_id: RoutingId
    service_kind: ServiceKind
    service_role: ServiceRole
    channel_name: str
    endpoint: str
    source: TopologySource
    state: TopologyState
    desired_count: int
    ready_count: int
    error_code: int
    last_reported_ms: int
    spot_kind: SpotKind


@dataclass(frozen=True)
class SpotRoute:
    spot_rid: RoutingId
    owner_node_rid: RoutingId
    spot_kind: SpotKind


@dataclass(frozen=True)
class RegistryTopologyFilter:
    auto_connect_type: AutoConnectType | None = None
    service_kind: ServiceKind | None = None
    service_role: ServiceRole | None = None
    channel_name: str | None = None
    routing_id: RoutingId | None = None
    state: TopologyState | None = None
    source: TopologySource | None = None


def _member_peer_from_native(entry):
    return MemberPeerEntry(
        auto_connect_type=AutoConnectType(int(entry.auto_connect_type)),
        service_role=ServiceRole(int(entry.service_role)),
        channel_name=_decode_fixed(entry.channel_name),
        endpoint=_decode_fixed(entry.endpoint),
        routing_id=_routing_id_bytes(entry.routing_id),
        value=int(entry.value),
        weight=int(entry.weight),
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
        auto_connect_type=AutoConnectType(int(entry.auto_connect_type)),
        routing_id=_routing_id_bytes(entry.routing_id),
        service_kind=ServiceKind(int(entry.service_kind)),
        service_role=ServiceRole(int(entry.service_role)),
        channel_name=_decode_fixed(entry.channel_name),
        endpoint=_decode_fixed(entry.endpoint),
        source=TopologySource(int(entry.source)),
        state=TopologyState(int(entry.state)),
        desired_count=int(entry.desired_count),
        ready_count=int(entry.ready_count),
        error_code=int(entry.error_code),
        last_reported_ms=int(entry.last_reported_ms),
        spot_kind=SpotKind(int(entry.spot_kind)),
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
    native.auto_connect_type = (
        0 if filter_.auto_connect_type is None else int(filter_.auto_connect_type)
    )
    native.service_kind = 0 if filter_.service_kind is None else int(filter_.service_kind)
    native.service_role = 0 if filter_.service_role is None else int(filter_.service_role)
    native.channel_name = _validated_c_string_text(
        filter_.channel_name or "",
        field="channel_name",
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

    REGISTRY_OPT_ID = 0x3801
    REGISTRY_OPT_HEARTBEAT_INTERVAL_MS = 0x3802
    REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS = 0x3803
    REGISTRY_OPT_BROADCAST_INTERVAL_MS = 0x3804

    def set_option(self, option: int, value: int):
        rc = lib().zlink_registry_set(
            self._handle,
            int(option),
            _validated_uint32(value, field="value"),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def get_option(self, option: int) -> int:
        error = ctypes.c_int()
        value = lib().zlink_registry_get(self._handle, int(option), ctypes.byref(error))
        if error.value != 0:
            _raise_result_error(
                ConfigError, ConfigResult, error.value, lib().zlink_errno()
            )
        return int(value)

    def set_id(self, registry_id: int):
        self.set_option(self.REGISTRY_OPT_ID, registry_id)

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
        self.set_option(
            self.REGISTRY_OPT_HEARTBEAT_INTERVAL_MS,
            _validated_uint32(interval_ms, field="interval_ms"),
        )
        self.set_option(
            self.REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS,
            _validated_uint32(timeout_ms, field="timeout_ms"),
        )

    def set_broadcast_interval(self, interval_ms: int):
        self.set_option(
            self.REGISTRY_OPT_BROADCAST_INTERVAL_MS,
            _validated_uint32(interval_ms, field="interval_ms"),
        )

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
            filter_native.auto_connect_type = (
                0
                if filter_.auto_connect_type is None
                else int(filter_.auto_connect_type)
            )
            filter_native.service_role = 0 if filter_.service_role is None else int(filter_.service_role)
            filter_native.channel_name = _validated_c_string_text(
                filter_.channel_name or "",
                field="channel_name",
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
                auto_connect_type=AutoConnectType(int(entry.auto_connect_type)),
                service_role=ServiceRole(int(entry.service_role)),
                channel_name=_decode_fixed(entry.channel_name),
                total_count=int(entry.total_count),
                connecting_count=int(entry.connecting_count),
                ready_count=int(entry.ready_count),
                error_count=int(entry.error_count),
                stopped_count=int(entry.stopped_count),
                last_reported_ms=int(entry.last_reported_ms),
            )
            for entry in entries[: count.value]
        ]

    def member_peers(self, channel_name):
        return _query_member_peers(
            self._handle,
            lib().zlink_registry_member_peers,
            _validated_c_string_text(
                channel_name, field="channel_name", max_length=255
            ),
        )

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
    def __init__(self, ctx, auto_connect_type, channel_name: str):
        self._handle = lib().zlink_discovery_new(
            ctx._handle,
            int(auto_connect_type),
            _validated_c_string_text(
                channel_name, field="channel_name", max_length=255
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

    def member_peers(self):
        return _query_member_peers(self._handle, lib().zlink_discovery_member_peers)

    def resolve_spot(self, spot_rid):
        native_spot_rid = _copy_routing_id(spot_rid)
        native_route = ZlinkSpotRoute()
        rc = lib().zlink_discovery_resolve_spot(
            self._handle,
            ctypes.byref(native_spot_rid),
            ctypes.byref(native_route),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return SpotRoute(
            spot_rid=_routing_id_bytes(native_route.spot_rid),
            owner_node_rid=_routing_id_bytes(native_route.owner_node_rid),
            spot_kind=SpotKind(int(native_route.spot_kind)),
        )

    def resolve_actor(self, actor_id):
        native = ZlinkActorRoute()
        rc = lib().zlink_discovery_resolve_actor(
            self._handle, _actor_id_bytes(actor_id), ctypes.byref(native)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return ActorRoute(
            actor=_actor_ref_from_native(native.actor),
            current_spot_rid=_routing_id_bytes(native.current_spot_rid),
            current_spot_kind=SpotKind(int(native.current_spot_kind)),
        )

    @property
    def spot_owner_sync_enabled(self):
        value = ctypes.c_int32()
        size = ctypes.c_size_t(ctypes.sizeof(value))
        rc = lib().zlink_get_option(
            self._handle,
            int(SocketOption.DISCOVERY_SPOT_OWNER_SYNC),
            ctypes.byref(value),
            ctypes.byref(size),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return value.value != 0

    @spot_owner_sync_enabled.setter
    def spot_owner_sync_enabled(self, enabled):
        value = ctypes.c_int32(1 if enabled else 0)
        rc = lib().zlink_set_option(
            self._handle,
            int(SocketOption.DISCOVERY_SPOT_OWNER_SYNC),
            ctypes.byref(value),
            ctypes.sizeof(value),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    @property
    def actor_route_sync_enabled(self):
        value = ctypes.c_int32()
        size = ctypes.c_size_t(ctypes.sizeof(value))
        rc = lib().zlink_get_option(
            self._handle,
            int(SocketOption.DISCOVERY_ACTOR_ROUTE_SYNC),
            ctypes.byref(value),
            ctypes.byref(size),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return value.value != 0

    @actor_route_sync_enabled.setter
    def actor_route_sync_enabled(self, enabled):
        value = ctypes.c_int32(1 if enabled else 0)
        rc = lib().zlink_set_option(
            self._handle,
            int(SocketOption.DISCOVERY_ACTOR_ROUTE_SYNC),
            ctypes.byref(value),
            ctypes.sizeof(value),
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
