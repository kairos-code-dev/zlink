use crate::routing_id::RoutingId;
use crate::spot_models::{
    AutoConnectType, RegistryState, ServiceKind, ServiceRole, SpotKind, TopologySource,
    TopologyState,
};

/// A snapshot of a registry's status.
#[derive(Debug, Clone)]
pub struct RegistryStatus {
    /// The registry's identifier.
    pub registry_id: u32,
    /// The endpoint the registry is bound to.
    pub bind_endpoint: String,
    /// The registry's operational state.
    pub state: RegistryState,
    /// The number of topology entries held.
    pub topology_entry_count: u32,
    /// The number of peer registries configured.
    pub peer_registry_count: u32,
    /// The number of peer registries currently connected.
    pub connected_peer_registry_count: u32,
    /// A monotonically increasing sequence number for the topology list.
    pub list_seq: u64,
    /// The last native error code, or 0 for none.
    pub last_error: i32,
    /// When the status last changed, in milliseconds.
    pub last_changed_ms: u64,
}

/// A per-service rollup of registered endpoints grouped by connection state.
#[derive(Debug, Clone)]
pub struct RegistryServiceSummaryEntry {
    /// The auto-connect topology.
    pub auto_connect_type: AutoConnectType,
    /// The service's messaging role.
    pub service_role: ServiceRole,
    /// The logical channel name.
    pub channel_name: String,
    /// The total number of endpoints.
    pub total_count: u32,
    /// The number of endpoints currently connecting.
    pub connecting_count: u32,
    /// The number of ready endpoints.
    pub ready_count: u32,
    /// The number of endpoints in error.
    pub error_count: u32,
    /// The number of stopped endpoints.
    pub stopped_count: u32,
    /// When this summary was last reported, in milliseconds.
    pub last_reported_ms: u64,
}

/// Filter for a service-summary query; `None` fields match anything.
#[derive(Debug, Clone)]
pub struct RegistryServiceSummaryFilter {
    /// Restrict to this auto-connect topology.
    pub auto_connect_type: Option<AutoConnectType>,
    /// Restrict to this service role.
    pub service_role: Option<ServiceRole>,
    /// Restrict to this logical channel name.
    pub channel_name: Option<String>,
}

/// One member peer registered on a channel.
#[derive(Debug, Clone)]
pub struct MemberPeerEntry {
    /// The auto-connect topology.
    pub auto_connect_type: AutoConnectType,
    /// The peer's messaging role.
    pub service_role: ServiceRole,
    /// The logical channel name.
    pub channel_name: String,
    /// The peer's transport endpoint.
    pub endpoint: String,
    /// The peer's routing id.
    pub routing_id: RoutingId,
    /// The peer's load-balancing weight.
    pub weight: u32,
    /// The application-defined value advertised by the peer.
    pub value: i64,
}

/// One entry in a registry's topology: a service endpoint and its state.
#[derive(Debug, Clone)]
pub struct RegistryTopologyEntry {
    /// The auto-connect topology.
    pub auto_connect_type: AutoConnectType,
    /// The endpoint's routing id.
    pub routing_id: RoutingId,
    /// The kind of service.
    pub service_kind: ServiceKind,
    /// The service's messaging role.
    pub service_role: ServiceRole,
    /// The logical channel name.
    pub channel_name: String,
    /// The transport endpoint.
    pub endpoint: String,
    /// Where this entry was learned from.
    pub source: TopologySource,
    /// The connection state of the entry.
    pub state: TopologyState,
    /// The desired number of connections.
    pub desired_count: u32,
    /// The number of ready connections.
    pub ready_count: u32,
    /// The last native error code, or 0 for none.
    pub error_code: u32,
    /// When this entry was last reported, in milliseconds.
    pub last_reported_ms: u64,
    /// The kind of spot, when the entry is a spot.
    pub spot_kind: SpotKind,
}

/// Filter for a registry topology query; `None` fields match anything.
#[derive(Debug, Clone)]
pub struct RegistryTopologyFilter {
    /// Restrict to this auto-connect topology.
    pub auto_connect_type: Option<AutoConnectType>,
    /// Restrict to this kind of service.
    pub service_kind: Option<ServiceKind>,
    /// Restrict to this service role.
    pub service_role: Option<ServiceRole>,
    /// Restrict to this logical channel name.
    pub channel_name: Option<String>,
    /// Restrict to this routing id.
    pub routing_id: Option<RoutingId>,
    /// Restrict to this topology state.
    pub state: Option<TopologyState>,
    /// Restrict to entries learned from this source.
    pub source: Option<TopologySource>,
}
