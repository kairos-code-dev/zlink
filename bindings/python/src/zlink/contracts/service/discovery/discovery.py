# SPDX-License-Identifier: MPL-2.0

from dataclasses import dataclass

from ...core.routing_id import RoutingId
from ..codes import (
    AutoConnectType,
    RegistryState,
    ServiceKind,
    ServiceRole,
    SpotKind,
    TopologySource,
    TopologyState,
)

_registry_factory = None
_discovery_factory = None
_registry_query_client_factory = None


def register_discovery_factories(
    *,
    registry_factory,
    discovery_factory,
    registry_query_client_factory,
):
    global _registry_factory
    global _discovery_factory
    global _registry_query_client_factory
    _registry_factory = registry_factory
    _discovery_factory = discovery_factory
    _registry_query_client_factory = registry_query_client_factory


def _require(factory, name):
    if factory is None:
        raise RuntimeError(f"zlink {name} runtime is not registered")
    return factory


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


class _ClosableContract:
    def close(self): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


class Registry(_ClosableContract):
    REGISTRY_OPT_ID = 0x3801
    REGISTRY_OPT_HEARTBEAT_INTERVAL_MS = 0x3802
    REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS = 0x3803
    REGISTRY_OPT_BROADCAST_INTERVAL_MS = 0x3804

    def __new__(cls, ctx):
        if cls is Registry:
            return _require(_registry_factory, "registry")(ctx)
        return object.__new__(cls)

    def bind(self, pub_endpoint: str, router_endpoint: str): ...

    def set_option(self, option: int, value: int): ...

    def get_option(self, option: int) -> int: ...

    def set_id(self, registry_id: int): ...

    def add_peer(self, peer_pub_endpoint: str): ...

    def set_tls_server(self, cert: str, key: str, require_client_cert: bool = False): ...

    def set_tls_client(
        self, ca_cert: str | None, hostname: str | None, trust_system: bool = False
    ): ...

    def set_heartbeat(self, interval_ms: int, timeout_ms: int): ...

    def set_broadcast_interval(self, interval_ms: int): ...

    def status(self): ...

    def service_summary(self, filter_=None): ...

    def member_peers(self, channel_name): ...

    def topology(self, filter_=None): ...


class Discovery(_ClosableContract):
    def __new__(cls, ctx, auto_connect_type, channel_name: str):
        if cls is Discovery:
            return _require(_discovery_factory, "discovery")(
                ctx, auto_connect_type, channel_name
            )
        return object.__new__(cls)

    def connect_registry(self, registry_endpoint: str): ...

    def set_value(self, value: int): ...

    def get_value(self) -> int: ...

    def member_peers(self): ...

    def resolve_spot(self, spot_rid): ...

    def resolve_actor(self, actor_id): ...

    @property
    def spot_owner_sync_enabled(self): ...

    @spot_owner_sync_enabled.setter
    def spot_owner_sync_enabled(self, enabled): ...

    @property
    def actor_route_sync_enabled(self): ...

    @actor_route_sync_enabled.setter
    def actor_route_sync_enabled(self, enabled): ...

    def set_tls_client(
        self, ca_cert: str | None, hostname: str | None, trust_system: bool = False
    ): ...


class RegistryQueryClient(_ClosableContract):
    def __new__(cls, ctx):
        if cls is RegistryQueryClient:
            return _require(_registry_query_client_factory, "registry query client")(
                ctx
            )
        return object.__new__(cls)

    def connect(self, endpoint: str): ...

    def topology(self, filter_=None): ...
