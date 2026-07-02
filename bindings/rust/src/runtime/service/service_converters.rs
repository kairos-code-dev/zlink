// SPDX-License-Identifier: MPL-2.0

use super::*;

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
