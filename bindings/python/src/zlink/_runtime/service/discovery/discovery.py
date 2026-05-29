# SPDX-License-Identifier: MPL-2.0

import ctypes
from dataclasses import dataclass

from ....contracts.sockets.codes import SocketOption
from ....contracts.service.codes import (
    AutoConnectType,
    RegistryState,
    ServiceKind,
    ServiceRole,
    SpotKind,
    TopologySource,
    TopologyState,
)
from ...._native.ffi import (
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
from ....contracts.service.discovery import (
    MemberPeerEntry,
    RegistryServiceSummaryEntry,
    RegistryServiceSummaryFilter,
    RegistryStatus,
    RegistryTopologyEntry,
    RegistryTopologyFilter,
    SpotRoute,
)
from ....contracts.service.spot import ActorRoute
from ..spot import _actor_id_bytes, _actor_ref_from_native
from ...handles.native_support import (
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


from ..registry.registry import Registry, create_registry
from ..registry.registry_query_client import RegistryQueryClient, create_registry_query_client


for _public_type in (Registry, Discovery, RegistryQueryClient):
    _public_type.__module__ = "zlink.contracts.service.discovery"



def create_discovery(ctx, auto_connect_type, channel_name):
    return Discovery(ctx, auto_connect_type, channel_name)
