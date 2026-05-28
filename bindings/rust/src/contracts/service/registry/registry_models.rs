use crate::routing_id::RoutingId;
use crate::spot_models::{
    AutoConnectType, RegistryState, ServiceKind, ServiceRole, SpotKind, TopologySource,
    TopologyState,
};

#[derive(Debug, Clone)]
pub struct RegistryStatus {
    pub registry_id: u32,
    pub bind_endpoint: String,
    pub state: RegistryState,
    pub topology_entry_count: u32,
    pub peer_registry_count: u32,
    pub connected_peer_registry_count: u32,
    pub list_seq: u64,
    pub last_error: i32,
    pub last_changed_ms: u64,
}

#[derive(Debug, Clone)]
pub struct RegistryServiceSummaryEntry {
    pub auto_connect_type: AutoConnectType,
    pub service_role: ServiceRole,
    pub channel_name: String,
    pub total_count: u32,
    pub connecting_count: u32,
    pub ready_count: u32,
    pub error_count: u32,
    pub stopped_count: u32,
    pub last_reported_ms: u64,
}

#[derive(Debug, Clone)]
pub struct RegistryServiceSummaryFilter {
    pub auto_connect_type: Option<AutoConnectType>,
    pub service_role: Option<ServiceRole>,
    pub channel_name: Option<String>,
}

#[derive(Debug, Clone)]
pub struct MemberPeerEntry {
    pub auto_connect_type: AutoConnectType,
    pub service_role: ServiceRole,
    pub channel_name: String,
    pub endpoint: String,
    pub routing_id: RoutingId,
    pub weight: u32,
    pub value: i64,
}

#[derive(Debug, Clone)]
pub struct RegistryTopologyEntry {
    pub auto_connect_type: AutoConnectType,
    pub routing_id: RoutingId,
    pub service_kind: ServiceKind,
    pub service_role: ServiceRole,
    pub channel_name: String,
    pub endpoint: String,
    pub source: TopologySource,
    pub state: TopologyState,
    pub desired_count: u32,
    pub ready_count: u32,
    pub error_code: u32,
    pub last_reported_ms: u64,
    pub spot_kind: SpotKind,
}

#[derive(Debug, Clone)]
pub struct RegistryTopologyFilter {
    pub auto_connect_type: Option<AutoConnectType>,
    pub service_kind: Option<ServiceKind>,
    pub service_role: Option<ServiceRole>,
    pub channel_name: Option<String>,
    pub routing_id: Option<RoutingId>,
    pub state: Option<TopologyState>,
    pub source: Option<TopologySource>,
}
