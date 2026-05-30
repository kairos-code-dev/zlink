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


@dataclass(frozen=True)
class MemberPeerEntry:
    """One member peer registered on a channel."""
    auto_connect_type: AutoConnectType
    service_role: ServiceRole
    channel_name: str
    endpoint: str
    routing_id: RoutingId
    value: int
    weight: int


@dataclass(frozen=True)
class RegistryStatus:
    """A snapshot of a registry's status."""
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
    """A per-service rollup of registered endpoints grouped by connection state."""
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
    """Filter for a service-summary query; unset fields match anything."""
    auto_connect_type: AutoConnectType | None = None
    service_role: ServiceRole | None = None
    channel_name: str | None = None


@dataclass(frozen=True)
class RegistryTopologyEntry:
    """One entry in a registry's topology: a service endpoint and its state."""
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
class RegistryTopologyFilter:
    """Filter for a registry topology query; unset fields match anything."""
    auto_connect_type: AutoConnectType | None = None
    service_kind: ServiceKind | None = None
    service_role: ServiceRole | None = None
    channel_name: str | None = None
    routing_id: RoutingId | None = None
    state: TopologyState | None = None
    source: TopologySource | None = None

__all__ = [
    "MemberPeerEntry",
    "RegistryStatus",
    "RegistryServiceSummaryEntry",
    "RegistryServiceSummaryFilter",
    "RegistryTopologyEntry",
    "RegistryTopologyFilter",
]
