// SPDX-License-Identifier: MPL-2.0

use super::*;

impl AutoConnectType {
    pub(super) fn to_raw(self) -> ffi::zlink_auto_connect_type_t {
        match self {
            Self::Invalid => ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_INVALID,
            Self::RouteMesh => ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_ROUTE_MESH,
            Self::ClientServer => ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_CLIENT_SERVER,
            Self::DealerMesh => ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_DEALER_MESH,
            Self::Fanout => ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_FANOUT,
            Self::SpotMesh => ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_SPOT_MESH,
        }
    }

    pub(super) fn from_raw(raw: ffi::zlink_auto_connect_type_t) -> Self {
        match raw {
            ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_INVALID => Self::Invalid,
            ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_ROUTE_MESH => Self::RouteMesh,
            ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_CLIENT_SERVER => Self::ClientServer,
            ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_DEALER_MESH => Self::DealerMesh,
            ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_FANOUT => Self::Fanout,
            ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_SPOT_MESH => Self::SpotMesh,
        }
    }
}

impl ServiceRole {
    pub(super) fn to_raw(self) -> ffi::zlink_service_role_t {
        match self {
            Self::Invalid => ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_INVALID,
            Self::Spot => ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_SPOT,
            Self::Router => ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_ROUTER,
            Self::Dealer => ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_DEALER,
            Self::Pub => ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_PUB,
            Self::Sub => ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_SUB,
        }
    }

    pub(super) fn from_raw(raw: ffi::zlink_service_role_t) -> Self {
        match raw {
            ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_INVALID => Self::Invalid,
            ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_SPOT => Self::Spot,
            ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_ROUTER => Self::Router,
            ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_DEALER => Self::Dealer,
            ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_PUB => Self::Pub,
            ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_SUB => Self::Sub,
        }
    }
}

impl ServiceKind {
    pub(super) fn to_raw(self) -> ffi::zlink_service_kind_t {
        match self {
            Self::Discovery => ffi::zlink_service_kind_t::ZLINK_SERVICE_KIND_DISCOVERY,
            Self::SpotSub => ffi::zlink_service_kind_t::ZLINK_SERVICE_KIND_SPOT_SUB,
            Self::SpotPub => ffi::zlink_service_kind_t::ZLINK_SERVICE_KIND_SPOT_PUB,
            Self::Socket => ffi::zlink_service_kind_t::ZLINK_SERVICE_KIND_SOCKET,
        }
    }

    pub(crate) fn from_raw(raw: ffi::zlink_service_kind_t) -> Self {
        match raw {
            ffi::zlink_service_kind_t::ZLINK_SERVICE_KIND_DISCOVERY => Self::Discovery,
            ffi::zlink_service_kind_t::ZLINK_SERVICE_KIND_SPOT_SUB => Self::SpotSub,
            ffi::zlink_service_kind_t::ZLINK_SERVICE_KIND_SPOT_PUB => Self::SpotPub,
            ffi::zlink_service_kind_t::ZLINK_SERVICE_KIND_SOCKET => Self::Socket,
        }
    }
}

impl SpotKind {
    pub(super) fn from_raw(raw: ffi::zlink_spot_kind_t) -> Self {
        match raw {
            ffi::zlink_spot_kind_t::ZLINK_SPOT_KIND_ENTRY => Self::Entry,
            ffi::zlink_spot_kind_t::ZLINK_SPOT_KIND_USER => Self::User,
            ffi::zlink_spot_kind_t::ZLINK_SPOT_KIND_INVALID => Self::Invalid,
        }
    }
}

impl SpotRole {
    pub(super) fn to_raw(self) -> ffi::zlink_spot_role_t {
        match self {
            Self::Pub => ffi::zlink_spot_role_t::ZLINK_SPOT_ROLE_PUB,
            Self::Sub => ffi::zlink_spot_role_t::ZLINK_SPOT_ROLE_SUB,
        }
    }

    pub(super) fn from_raw(raw: ffi::zlink_spot_role_t) -> Self {
        match raw {
            ffi::zlink_spot_role_t::ZLINK_SPOT_ROLE_PUB => Self::Pub,
            ffi::zlink_spot_role_t::ZLINK_SPOT_ROLE_SUB => Self::Sub,
        }
    }
}

impl SpotNodeMode {
    pub(super) fn to_raw(self) -> ffi::zlink_spot_node_mode_t {
        match self {
            Self::PubSub => ffi::zlink_spot_node_mode_t::ZLINK_SPOT_NODE_MODE_PUBSUB,
            Self::Routed => ffi::zlink_spot_node_mode_t::ZLINK_SPOT_NODE_MODE_ROUTED,
            Self::All => ffi::zlink_spot_node_mode_t::ZLINK_SPOT_NODE_MODE_ALL,
        }
    }
}

impl SocketType {
    pub(super) fn from_raw(raw: ffi::zlink_socket_type_t) -> Self {
        match raw {
            ffi::zlink_socket_type_t::ZLINK_SOCKET_ANY => Self::Any,
            ffi::zlink_socket_type_t::ZLINK_SOCKET_PAIR => Self::Pair,
            ffi::zlink_socket_type_t::ZLINK_SOCKET_PUB => Self::Pub,
            ffi::zlink_socket_type_t::ZLINK_SOCKET_SUB => Self::Sub,
            ffi::zlink_socket_type_t::ZLINK_SOCKET_DEALER => Self::Dealer,
            ffi::zlink_socket_type_t::ZLINK_SOCKET_ROUTER => Self::Router,
            ffi::zlink_socket_type_t::ZLINK_SOCKET_XPUB => Self::XPub,
            ffi::zlink_socket_type_t::ZLINK_SOCKET_XSUB => Self::XSub,
            ffi::zlink_socket_type_t::ZLINK_SOCKET_STREAM => Self::Stream,
        }
    }

    pub(super) fn to_raw(self) -> ffi::zlink_socket_type_t {
        match self {
            Self::Any => ffi::zlink_socket_type_t::ZLINK_SOCKET_ANY,
            Self::Pair => ffi::zlink_socket_type_t::ZLINK_SOCKET_PAIR,
            Self::Pub => ffi::zlink_socket_type_t::ZLINK_SOCKET_PUB,
            Self::Sub => ffi::zlink_socket_type_t::ZLINK_SOCKET_SUB,
            Self::Dealer => ffi::zlink_socket_type_t::ZLINK_SOCKET_DEALER,
            Self::Router => ffi::zlink_socket_type_t::ZLINK_SOCKET_ROUTER,
            Self::XPub => ffi::zlink_socket_type_t::ZLINK_SOCKET_XPUB,
            Self::XSub => ffi::zlink_socket_type_t::ZLINK_SOCKET_XSUB,
            Self::Stream => ffi::zlink_socket_type_t::ZLINK_SOCKET_STREAM,
        }
    }
}

impl SpotNodeSocketOwner {
    pub(super) fn to_raw(self) -> ffi::zlink_spot_node_socket_owner_t {
        match self {
            Self::Any => ffi::zlink_spot_node_socket_owner_t::ZLINK_SPOT_NODE_SOCKET_OWNER_ANY,
            Self::Node => ffi::zlink_spot_node_socket_owner_t::ZLINK_SPOT_NODE_SOCKET_OWNER_NODE,
            Self::Spot => ffi::zlink_spot_node_socket_owner_t::ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT,
        }
    }

    pub(super) fn from_raw(raw: ffi::zlink_spot_node_socket_owner_t) -> Self {
        match raw {
            ffi::zlink_spot_node_socket_owner_t::ZLINK_SPOT_NODE_SOCKET_OWNER_ANY => Self::Any,
            ffi::zlink_spot_node_socket_owner_t::ZLINK_SPOT_NODE_SOCKET_OWNER_NODE => Self::Node,
            ffi::zlink_spot_node_socket_owner_t::ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT => Self::Spot,
        }
    }
}

impl SpotNodeState {
    pub(super) fn from_raw(raw: ffi::zlink_spot_node_state_t) -> Self {
        match raw {
            ffi::zlink_spot_node_state_t::ZLINK_SPOT_NODE_STATE_IDLE => Self::Idle,
            ffi::zlink_spot_node_state_t::ZLINK_SPOT_NODE_STATE_CONNECTING => Self::Connecting,
            ffi::zlink_spot_node_state_t::ZLINK_SPOT_NODE_STATE_PARTIAL_READY => Self::PartialReady,
            ffi::zlink_spot_node_state_t::ZLINK_SPOT_NODE_STATE_READY => Self::Ready,
            ffi::zlink_spot_node_state_t::ZLINK_SPOT_NODE_STATE_ERROR => Self::Error,
        }
    }
}

impl SpotPeerSource {
    pub(super) fn to_raw(self) -> ffi::zlink_spot_peer_source_t {
        match self {
            Self::Manual => ffi::zlink_spot_peer_source_t::ZLINK_SPOT_PEER_SOURCE_MANUAL,
            Self::Discovery => ffi::zlink_spot_peer_source_t::ZLINK_SPOT_PEER_SOURCE_DISCOVERY,
            Self::Mixed => ffi::zlink_spot_peer_source_t::ZLINK_SPOT_PEER_SOURCE_MIXED,
        }
    }

    pub(super) fn from_raw(raw: ffi::zlink_spot_peer_source_t) -> Self {
        match raw {
            ffi::zlink_spot_peer_source_t::ZLINK_SPOT_PEER_SOURCE_MANUAL => Self::Manual,
            ffi::zlink_spot_peer_source_t::ZLINK_SPOT_PEER_SOURCE_DISCOVERY => Self::Discovery,
            ffi::zlink_spot_peer_source_t::ZLINK_SPOT_PEER_SOURCE_MIXED => Self::Mixed,
        }
    }
}

impl SpotPeerKind {
    pub(super) fn from_raw(raw: ffi::zlink_spot_peer_kind_t) -> Self {
        match raw {
            ffi::zlink_spot_peer_kind_t::ZLINK_SPOT_PEER_KIND_SPOT_MESH => Self::SpotMesh,
            ffi::zlink_spot_peer_kind_t::ZLINK_SPOT_PEER_KIND_ROUTER_CHANNEL => Self::RouterChannel,
        }
    }
}

impl SpotPeerState {
    pub(super) fn to_raw(self) -> ffi::zlink_spot_peer_state_t {
        match self {
            Self::Configured => ffi::zlink_spot_peer_state_t::ZLINK_SPOT_PEER_STATE_CONFIGURED,
            Self::Connecting => ffi::zlink_spot_peer_state_t::ZLINK_SPOT_PEER_STATE_CONNECTING,
            Self::Connected => ffi::zlink_spot_peer_state_t::ZLINK_SPOT_PEER_STATE_CONNECTED,
        }
    }

    pub(super) fn from_raw(raw: ffi::zlink_spot_peer_state_t) -> Self {
        match raw {
            ffi::zlink_spot_peer_state_t::ZLINK_SPOT_PEER_STATE_CONFIGURED => Self::Configured,
            ffi::zlink_spot_peer_state_t::ZLINK_SPOT_PEER_STATE_CONNECTING => Self::Connecting,
            ffi::zlink_spot_peer_state_t::ZLINK_SPOT_PEER_STATE_CONNECTED => Self::Connected,
        }
    }
}

impl RegistryState {
    pub(super) fn from_raw(raw: ffi::zlink_registry_state_t) -> Self {
        match raw {
            ffi::zlink_registry_state_t::ZLINK_REGISTRY_STATE_IDLE => Self::Idle,
            ffi::zlink_registry_state_t::ZLINK_REGISTRY_STATE_ACTIVE => Self::Active,
            ffi::zlink_registry_state_t::ZLINK_REGISTRY_STATE_DEGRADED => Self::Degraded,
            ffi::zlink_registry_state_t::ZLINK_REGISTRY_STATE_ERROR => Self::Error,
        }
    }
}

impl TopologySource {
    pub(super) fn to_raw(self) -> ffi::zlink_topology_source_t {
        match self {
            Self::Manual => ffi::zlink_topology_source_t::ZLINK_TOPOLOGY_SOURCE_MANUAL,
            Self::Discovery => ffi::zlink_topology_source_t::ZLINK_TOPOLOGY_SOURCE_DISCOVERY,
            Self::Registry => ffi::zlink_topology_source_t::ZLINK_TOPOLOGY_SOURCE_REGISTRY,
        }
    }

    pub(super) fn from_raw(raw: ffi::zlink_topology_source_t) -> Self {
        match raw {
            ffi::zlink_topology_source_t::ZLINK_TOPOLOGY_SOURCE_MANUAL => Self::Manual,
            ffi::zlink_topology_source_t::ZLINK_TOPOLOGY_SOURCE_DISCOVERY => Self::Discovery,
            ffi::zlink_topology_source_t::ZLINK_TOPOLOGY_SOURCE_REGISTRY => Self::Registry,
        }
    }
}

impl TopologyState {
    pub(super) fn to_raw(self) -> ffi::zlink_topology_state_t {
        match self {
            Self::Discovered => ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_DISCOVERED,
            Self::Connecting => ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_CONNECTING,
            Self::Ready => ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_READY,
            Self::Lost => ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_LOST,
            Self::Error => ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_ERROR,
            Self::Stopped => ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_STOPPED,
        }
    }

    pub(super) fn from_raw(raw: ffi::zlink_topology_state_t) -> Self {
        match raw {
            ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_DISCOVERED => Self::Discovered,
            ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_CONNECTING => Self::Connecting,
            ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_READY => Self::Ready,
            ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_LOST => Self::Lost,
            ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_ERROR => Self::Error,
            ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_STOPPED => Self::Stopped,
        }
    }
}

impl SpotNodeStatus {
    pub(super) fn from_raw(raw: &ffi::zlink_spot_node_status_t) -> Self {
        Self {
            channel_name: fixed_cstr_to_string(&raw.channel_name),
            local_endpoint: fixed_cstr_to_string(&raw.local_endpoint),
            node_routing_id: RoutingId::from_raw(raw.node_routing_id),
            state: SpotNodeState::from_raw(raw.state),
            configured_peer_count: raw.configured_peer_count,
            active_peer_count: raw.active_peer_count,
            connected_peer_count: raw.connected_peer_count,
            subject_count: raw.subject_count,
            ready_subject_count: raw.ready_subject_count,
            disconnected_sub_target_count: raw.disconnected_sub_target_count,
            disconnected_routed_target_count: raw.disconnected_routed_target_count,
            last_error: raw.last_error,
            last_changed_ms: raw.last_changed_ms,
        }
    }
}

impl SpotNodePeerEntry {
    pub(super) fn from_raw(raw: &ffi::zlink_spot_node_peer_entry_t) -> Self {
        Self {
            channel_name: fixed_cstr_to_string(&raw.channel_name),
            local_endpoint: fixed_cstr_to_string(&raw.local_endpoint),
            peer_endpoint: fixed_cstr_to_string(&raw.peer_endpoint),
            source: SpotPeerSource::from_raw(raw.source),
            kind: SpotPeerKind::from_raw(raw.kind),
            state: SpotPeerState::from_raw(raw.state),
            weight: raw.weight,
            connected_since_ms: raw.connected_since_ms,
            last_changed_ms: raw.last_changed_ms,
        }
    }
}

impl SubjectKind {
    pub(super) fn from_raw(raw: u32) -> Self {
        match raw {
            1 => Self::PubSub,
            2 => Self::Routed,
            _ => Self::Unknown,
        }
    }
}

impl SpotNodeSubjectEntry {
    pub(super) fn from_raw(raw: &ffi::zlink_spot_node_subject_entry_t) -> Self {
        Self {
            role: SpotRole::from_raw(raw.role),
            subject: fixed_cstr_to_string(&raw.subject),
            subject_kind: SubjectKind::from_raw(raw.subject_kind),
            ready_peer_count: raw.ready_peer_count,
            active_peer_count: raw.active_peer_count,
            last_changed_ms: raw.last_changed_ms,
        }
    }
}

impl SpotNodeSocketEntry {
    pub(super) fn from_raw(raw: &ffi::zlink_spot_node_socket_entry_t) -> Self {
        Self {
            owner: SpotNodeSocketOwner::from_raw(raw.owner),
            owner_id: raw.owner_id,
            owner_name: fixed_cstr_to_string(&raw.owner_name),
            socket_name: fixed_cstr_to_string(&raw.socket_name),
            socket_type: SocketType::from_raw(raw.socket_type),
            auto_hwm_visible: raw.auto_hwm_visible != 0,
            snapshot: MonitorStatus::from_raw(&raw.monitor_status),
        }
    }
}

impl RegistryStatus {
    pub(super) fn from_raw(raw: &ffi::zlink_registry_status_t) -> Self {
        Self {
            registry_id: raw.registry_id,
            bind_endpoint: fixed_cstr_to_string(&raw.bind_endpoint),
            state: RegistryState::from_raw(raw.state),
            topology_entry_count: raw.topology_entry_count,
            peer_registry_count: raw.peer_registry_count,
            connected_peer_registry_count: raw.connected_peer_registry_count,
            list_seq: raw.list_seq,
            last_error: raw.last_error,
            last_changed_ms: raw.last_changed_ms,
        }
    }
}

impl RegistryServiceSummaryEntry {
    pub(super) fn from_raw(raw: &ffi::zlink_registry_service_summary_entry_t) -> Self {
        Self {
            auto_connect_type: AutoConnectType::from_raw(raw.auto_connect_type),
            service_role: ServiceRole::from_raw(raw.service_role),
            channel_name: fixed_cstr_to_string(&raw.channel_name),
            total_count: raw.total_count,
            connecting_count: raw.connecting_count,
            ready_count: raw.ready_count,
            error_count: raw.error_count,
            stopped_count: raw.stopped_count,
            last_reported_ms: raw.last_reported_ms,
        }
    }
}

impl MemberPeerEntry {
    pub(super) fn from_raw(raw: &ffi::zlink_member_peer_entry_t) -> Result<Self, ConfigError> {
        Ok(Self {
            auto_connect_type: AutoConnectType::from_raw(raw.auto_connect_type),
            service_role: ServiceRole::from_raw(raw.service_role),
            channel_name: fixed_cstr_to_string(&raw.channel_name),
            endpoint: fixed_cstr_to_string(&raw.endpoint),
            routing_id: RoutingId::from_raw(raw.routing_id),
            weight: raw.weight,
            value: raw.value,
        })
    }
}

impl RegistryTopologyEntry {
    pub(super) fn from_raw(raw: &ffi::zlink_registry_topology_entry_t) -> Self {
        Self {
            auto_connect_type: AutoConnectType::from_raw(raw.auto_connect_type),
            routing_id: RoutingId::from_raw(raw.routing_id),
            service_kind: ServiceKind::from_raw(raw.service_kind),
            service_role: ServiceRole::from_raw(raw.service_role),
            channel_name: fixed_cstr_to_string(&raw.channel_name),
            endpoint: fixed_cstr_to_string(&raw.endpoint),
            source: TopologySource::from_raw(raw.source),
            state: TopologyState::from_raw(raw.state),
            desired_count: raw.desired_count,
            ready_count: raw.ready_count,
            error_code: raw.error_code,
            last_reported_ms: raw.last_reported_ms,
            spot_kind: SpotKind::from_raw(raw.spot_kind),
        }
    }
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------
