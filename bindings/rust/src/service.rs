use std::ffi::{CStr, CString, c_char, c_void};
use std::mem::MaybeUninit;
use std::ptr;
use std::sync::mpsc;
use std::time::Duration;

use crate::ctx::AutoHwmProfile;
use crate::domain::{Received, SubscriptionEvent, TopicMessage};
use crate::error::{
    BindError, CloseError, ConfigError, ConnectError, HandlerError, RecvError, RecvResult,
    RequestError, SubmitError, ZlinkError, check_bind_rc, check_close_rc, check_config_rc,
    check_connect_rc, check_handler_rc, check_rc, check_recv_rc, check_submit_rc,
    config_validation_error, last_errno, request_error_from_result, request_error_from_submit,
    submit_not_supported_error,
};
use crate::ffi;
use crate::flags::{RecvFlags, SendFlags};
use crate::message::{Message, RoutingId};
use crate::monitor::MonitorSnapshot;
use crate::request_progress::RequestProgressGuard;
use crate::socket::{
    CallbackBox, close_unreceived_part, cstr_buf_to_string, prepare_send_parts,
    routing_id_from_ptr, send_ready_trampoline, submit_part_sequence, take_parts,
};

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

/// Service registry that accepts registrations and broadcasts the service list.
pub struct Registry {
    handle: *mut c_void,
}

unsafe impl Send for Registry {}

impl Registry {
    pub fn new(ctx: &crate::ctx::Context) -> Result<Self, ConfigError> {
        let handle = unsafe { ffi::zlink_registry_new(ctx.raw()) };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self { handle })
    }

    pub fn bind(&self, pub_endpoint: &str, router_endpoint: &str) -> Result<(), BindError> {
        let c_pub = CString::new(pub_endpoint)
            .map_err(|_| BindError::new(crate::error::BindResult::InvalidArgument, libc::EINVAL))?;
        let c_router = CString::new(router_endpoint)
            .map_err(|_| BindError::new(crate::error::BindResult::InvalidArgument, libc::EINVAL))?;
        check_bind_rc(unsafe {
            ffi::zlink_registry_bind(self.handle, c_pub.as_ptr(), c_router.as_ptr())
        })
    }

    pub fn set_id(&self, id: u32) -> Result<(), ConfigError> {
        self.set_option(ffi::ZLINK_REGISTRY_OPT_ID, id)
    }

    pub fn set_option(
        &self,
        option: ffi::zlink_registry_option_t,
        value: u32,
    ) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_registry_set(self.handle, option, value) })
    }

    pub fn get_option(&self, option: ffi::zlink_registry_option_t) -> Result<u32, ConfigError> {
        let mut error = 0;
        let value = unsafe { ffi::zlink_registry_get(self.handle, option, &mut error) };
        check_config_rc(error)?;
        Ok(value)
    }

    pub fn add_peer(&self, peer_pub_endpoint: &str) -> Result<(), ConnectError> {
        let c = CString::new(peer_pub_endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe { ffi::zlink_registry_add_peer(self.handle, c.as_ptr()) })
    }

    pub fn set_heartbeat(&self, interval_ms: u32, timeout_ms: u32) -> Result<(), ConfigError> {
        self.set_option(ffi::ZLINK_REGISTRY_OPT_HEARTBEAT_INTERVAL_MS, interval_ms)?;
        self.set_option(ffi::ZLINK_REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS, timeout_ms)
    }

    pub fn set_broadcast_interval(&self, interval_ms: u32) -> Result<(), ConfigError> {
        self.set_option(ffi::ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS, interval_ms)
    }

    pub fn set_tls_server(
        &self,
        cert_pem: &str,
        key_pem: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        set_tls_server_config(self.handle, cert_pem, key_pem, require_client_cert)
    }

    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        set_tls_client_config(self.handle, ca_cert_pem, hostname, trust_system)
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        destroy_handle_close(&mut self.handle, ffi::zlink_registry_destroy)
    }

    #[allow(dead_code)]
    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }
}

impl Drop for Registry {
    fn drop(&mut self) {
        let _ = destroy_handle(&mut self.handle, ffi::zlink_registry_destroy);
    }
}

// ---------------------------------------------------------------------------
// AutoConnectType / ServiceRole newtypes
// ---------------------------------------------------------------------------

/// The fixed auto-connect contract for a Discovery channel.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AutoConnectType {
    Invalid,
    RouteMesh,
    ClientServer,
    DealerMesh,
    Fanout,
    SpotMesh,
}

impl AutoConnectType {
    fn to_raw(self) -> ffi::zlink_auto_connect_type_t {
        match self {
            Self::Invalid => ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_INVALID,
            Self::RouteMesh => ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_ROUTE_MESH,
            Self::ClientServer => ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_CLIENT_SERVER,
            Self::DealerMesh => ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_DEALER_MESH,
            Self::Fanout => ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_FANOUT,
            Self::SpotMesh => ffi::zlink_auto_connect_type_t::ZLINK_AUTO_CONNECT_SPOT_MESH,
        }
    }

    fn from_raw(raw: ffi::zlink_auto_connect_type_t) -> Self {
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

/// The service role for service-member and topology snapshots.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ServiceRole {
    Invalid,
    Spot,
    Router,
    Dealer,
    Pub,
    Sub,
}

impl ServiceRole {
    fn to_raw(self) -> ffi::zlink_service_role_t {
        match self {
            Self::Invalid => ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_INVALID,
            Self::Spot => ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_SPOT,
            Self::Router => ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_ROUTER,
            Self::Dealer => ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_DEALER,
            Self::Pub => ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_PUB,
            Self::Sub => ffi::zlink_service_role_t::ZLINK_SERVICE_ROLE_SUB,
        }
    }

    fn from_raw(raw: ffi::zlink_service_role_t) -> Self {
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

/// The service kind reported by topology snapshots and service entries.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ServiceKind {
    Discovery,
    SpotSub,
    SpotPub,
    Socket,
}

impl ServiceKind {
    fn to_raw(self) -> ffi::zlink_service_kind_t {
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

/// The SPOT role used in subject snapshots and queries.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotRole {
    Pub,
    Sub,
}

impl SpotRole {
    fn to_raw(self) -> ffi::zlink_spot_role_t {
        match self {
            Self::Pub => ffi::zlink_spot_role_t::ZLINK_SPOT_ROLE_PUB,
            Self::Sub => ffi::zlink_spot_role_t::ZLINK_SPOT_ROLE_SUB,
        }
    }

    fn from_raw(raw: ffi::zlink_spot_role_t) -> Self {
        match raw {
            ffi::zlink_spot_role_t::ZLINK_SPOT_ROLE_PUB => Self::Pub,
            ffi::zlink_spot_role_t::ZLINK_SPOT_ROLE_SUB => Self::Sub,
        }
    }
}

/// Current SPOT node lifecycle state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotNodeState {
    Idle,
    Connecting,
    PartialReady,
    Ready,
    Error,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotNodeMode {
    PubSub,
    Routed,
    All,
}

impl SpotNodeMode {
    fn to_raw(self) -> ffi::zlink_spot_node_mode_t {
        match self {
            Self::PubSub => ffi::zlink_spot_node_mode_t::ZLINK_SPOT_NODE_MODE_PUBSUB,
            Self::Routed => ffi::zlink_spot_node_mode_t::ZLINK_SPOT_NODE_MODE_ROUTED,
            Self::All => ffi::zlink_spot_node_mode_t::ZLINK_SPOT_NODE_MODE_ALL,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SocketType {
    Any = 0,
    Pair = 0x1001,
    Pub = 0x1002,
    Sub = 0x1003,
    Dealer = 0x1004,
    Router = 0x1005,
    XPub = 0x1006,
    XSub = 0x1007,
    Stream = 0x1008,
}

impl SocketType {
    fn from_raw(raw: ffi::zlink_socket_type_t) -> Self {
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

    fn to_raw(self) -> ffi::zlink_socket_type_t {
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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotNodeSocketOwner {
    Any,
    Node,
    Spot,
}

impl SpotNodeSocketOwner {
    fn to_raw(self) -> ffi::zlink_spot_node_socket_owner_t {
        match self {
            Self::Any => ffi::zlink_spot_node_socket_owner_t::ZLINK_SPOT_NODE_SOCKET_OWNER_ANY,
            Self::Node => ffi::zlink_spot_node_socket_owner_t::ZLINK_SPOT_NODE_SOCKET_OWNER_NODE,
            Self::Spot => ffi::zlink_spot_node_socket_owner_t::ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT,
        }
    }

    fn from_raw(raw: ffi::zlink_spot_node_socket_owner_t) -> Self {
        match raw {
            ffi::zlink_spot_node_socket_owner_t::ZLINK_SPOT_NODE_SOCKET_OWNER_ANY => Self::Any,
            ffi::zlink_spot_node_socket_owner_t::ZLINK_SPOT_NODE_SOCKET_OWNER_NODE => Self::Node,
            ffi::zlink_spot_node_socket_owner_t::ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT => Self::Spot,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SpotNodeOptions {
    pub mode: Option<SpotNodeMode>, // None maps to All
}

impl SpotNodeState {
    fn from_raw(raw: ffi::zlink_spot_node_state_t) -> Self {
        match raw {
            ffi::zlink_spot_node_state_t::ZLINK_SPOT_NODE_STATE_IDLE => Self::Idle,
            ffi::zlink_spot_node_state_t::ZLINK_SPOT_NODE_STATE_CONNECTING => Self::Connecting,
            ffi::zlink_spot_node_state_t::ZLINK_SPOT_NODE_STATE_PARTIAL_READY => Self::PartialReady,
            ffi::zlink_spot_node_state_t::ZLINK_SPOT_NODE_STATE_READY => Self::Ready,
            ffi::zlink_spot_node_state_t::ZLINK_SPOT_NODE_STATE_ERROR => Self::Error,
        }
    }
}

/// Source of one SPOT peer entry.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotPeerSource {
    Manual,
    Discovery,
    Mixed,
}

impl SpotPeerSource {
    fn to_raw(self) -> ffi::zlink_spot_peer_source_t {
        match self {
            Self::Manual => ffi::zlink_spot_peer_source_t::ZLINK_SPOT_PEER_SOURCE_MANUAL,
            Self::Discovery => ffi::zlink_spot_peer_source_t::ZLINK_SPOT_PEER_SOURCE_DISCOVERY,
            Self::Mixed => ffi::zlink_spot_peer_source_t::ZLINK_SPOT_PEER_SOURCE_MIXED,
        }
    }

    fn from_raw(raw: ffi::zlink_spot_peer_source_t) -> Self {
        match raw {
            ffi::zlink_spot_peer_source_t::ZLINK_SPOT_PEER_SOURCE_MANUAL => Self::Manual,
            ffi::zlink_spot_peer_source_t::ZLINK_SPOT_PEER_SOURCE_DISCOVERY => Self::Discovery,
            ffi::zlink_spot_peer_source_t::ZLINK_SPOT_PEER_SOURCE_MIXED => Self::Mixed,
        }
    }
}

/// Connection state of one SPOT peer entry.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotPeerState {
    Configured,
    Connecting,
    Connected,
}

impl SpotPeerState {
    fn to_raw(self) -> ffi::zlink_spot_peer_state_t {
        match self {
            Self::Configured => ffi::zlink_spot_peer_state_t::ZLINK_SPOT_PEER_STATE_CONFIGURED,
            Self::Connecting => ffi::zlink_spot_peer_state_t::ZLINK_SPOT_PEER_STATE_CONNECTING,
            Self::Connected => ffi::zlink_spot_peer_state_t::ZLINK_SPOT_PEER_STATE_CONNECTED,
        }
    }

    fn from_raw(raw: ffi::zlink_spot_peer_state_t) -> Self {
        match raw {
            ffi::zlink_spot_peer_state_t::ZLINK_SPOT_PEER_STATE_CONFIGURED => Self::Configured,
            ffi::zlink_spot_peer_state_t::ZLINK_SPOT_PEER_STATE_CONNECTING => Self::Connecting,
            ffi::zlink_spot_peer_state_t::ZLINK_SPOT_PEER_STATE_CONNECTED => Self::Connected,
        }
    }
}

/// Current registry lifecycle state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RegistryState {
    Idle,
    Active,
    Degraded,
    Error,
}

impl RegistryState {
    fn from_raw(raw: ffi::zlink_registry_state_t) -> Self {
        match raw {
            ffi::zlink_registry_state_t::ZLINK_REGISTRY_STATE_IDLE => Self::Idle,
            ffi::zlink_registry_state_t::ZLINK_REGISTRY_STATE_ACTIVE => Self::Active,
            ffi::zlink_registry_state_t::ZLINK_REGISTRY_STATE_DEGRADED => Self::Degraded,
            ffi::zlink_registry_state_t::ZLINK_REGISTRY_STATE_ERROR => Self::Error,
        }
    }
}

/// Source of a topology entry.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TopologySource {
    Manual,
    Discovery,
    Registry,
}

impl TopologySource {
    fn to_raw(self) -> ffi::zlink_topology_source_t {
        match self {
            Self::Manual => ffi::zlink_topology_source_t::ZLINK_TOPOLOGY_SOURCE_MANUAL,
            Self::Discovery => ffi::zlink_topology_source_t::ZLINK_TOPOLOGY_SOURCE_DISCOVERY,
            Self::Registry => ffi::zlink_topology_source_t::ZLINK_TOPOLOGY_SOURCE_REGISTRY,
        }
    }

    fn from_raw(raw: ffi::zlink_topology_source_t) -> Self {
        match raw {
            ffi::zlink_topology_source_t::ZLINK_TOPOLOGY_SOURCE_MANUAL => Self::Manual,
            ffi::zlink_topology_source_t::ZLINK_TOPOLOGY_SOURCE_DISCOVERY => Self::Discovery,
            ffi::zlink_topology_source_t::ZLINK_TOPOLOGY_SOURCE_REGISTRY => Self::Registry,
        }
    }
}

/// State of a topology entry.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TopologyState {
    Discovered,
    Connecting,
    Ready,
    Lost,
    Error,
    Stopped,
}

impl TopologyState {
    fn to_raw(self) -> ffi::zlink_topology_state_t {
        match self {
            Self::Discovered => ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_DISCOVERED,
            Self::Connecting => ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_CONNECTING,
            Self::Ready => ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_READY,
            Self::Lost => ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_LOST,
            Self::Error => ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_ERROR,
            Self::Stopped => ffi::zlink_topology_state_t::ZLINK_TOPOLOGY_STATE_STOPPED,
        }
    }

    fn from_raw(raw: ffi::zlink_topology_state_t) -> Self {
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

#[derive(Debug, Clone)]
pub struct SpotNodeStatus {
    pub channel_name: String,
    pub local_endpoint: String,
    pub node_routing_id: RoutingId,
    pub state: SpotNodeState,
    pub configured_peer_count: u32,
    pub active_peer_count: u32,
    pub connected_peer_count: u32,
    pub subject_count: u32,
    pub ready_subject_count: u32,
    pub disconnected_sub_target_count: u32,
    pub disconnected_routed_target_count: u32,
    pub last_error: i32,
    pub last_changed_ms: u64,
}

impl SpotNodeStatus {
    fn from_raw(raw: &ffi::zlink_spot_node_status_t) -> Self {
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

#[derive(Debug, Clone)]
pub struct SpotNodePeerEntry {
    pub channel_name: String,
    pub local_endpoint: String,
    pub peer_endpoint: String,
    pub source: SpotPeerSource,
    pub state: SpotPeerState,
    pub weight: u32,
    pub connected_since_ms: u64,
    pub last_changed_ms: u64,
}

impl SpotNodePeerEntry {
    fn from_raw(raw: &ffi::zlink_spot_node_peer_entry_t) -> Self {
        Self {
            channel_name: fixed_cstr_to_string(&raw.channel_name),
            local_endpoint: fixed_cstr_to_string(&raw.local_endpoint),
            peer_endpoint: fixed_cstr_to_string(&raw.peer_endpoint),
            source: SpotPeerSource::from_raw(raw.source),
            state: SpotPeerState::from_raw(raw.state),
            weight: raw.weight,
            connected_since_ms: raw.connected_since_ms,
            last_changed_ms: raw.last_changed_ms,
        }
    }
}

#[derive(Debug, Clone)]
pub struct SpotNodePeerFilter {
    pub peer_endpoint: Option<String>,
    pub source: Option<SpotPeerSource>,
    pub state: Option<SpotPeerState>,
}

/// Subject kind for SPOT subject entries.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SubjectKind {
    Unknown = 0,
    PubSub = 1,
    Routed = 2,
}

impl SubjectKind {
    fn from_raw(raw: u32) -> Self {
        match raw {
            1 => Self::PubSub,
            2 => Self::Routed,
            _ => Self::Unknown,
        }
    }
}

#[derive(Debug, Clone)]
pub struct SpotNodeSubjectEntry {
    pub role: SpotRole,
    pub subject: String,
    pub subject_kind: SubjectKind,
    pub ready_peer_count: u32,
    pub active_peer_count: u32,
    pub last_changed_ms: u64,
}

impl SpotNodeSubjectEntry {
    fn from_raw(raw: &ffi::zlink_spot_node_subject_entry_t) -> Self {
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

#[derive(Debug, Clone)]
pub struct SpotNodeSubjectFilter {
    pub role: Option<SpotRole>,
    pub subject: Option<String>,
    pub subject_kind: Option<SubjectKind>,
}

#[derive(Debug, Clone)]
pub struct SpotNodeSocketSnapshotFilter {
    pub owner: Option<SpotNodeSocketOwner>,
    pub socket_type: Option<SocketType>,
    pub socket_name: Option<String>,
}

#[derive(Debug, Clone)]
pub struct SpotNodeSocketSnapshotEntry {
    pub owner: SpotNodeSocketOwner,
    pub owner_id: u64,
    pub owner_name: String,
    pub socket_name: String,
    pub socket_type: SocketType,
    pub auto_hwm_visible: bool,
    pub snapshot: MonitorSnapshot,
}

impl SpotNodeSocketSnapshotEntry {
    fn from_raw(raw: &ffi::zlink_spot_node_socket_snapshot_entry_t) -> Self {
        Self {
            owner: SpotNodeSocketOwner::from_raw(raw.owner),
            owner_id: raw.owner_id,
            owner_name: fixed_cstr_to_string(&raw.owner_name),
            socket_name: fixed_cstr_to_string(&raw.socket_name),
            socket_type: SocketType::from_raw(raw.socket_type),
            auto_hwm_visible: raw.auto_hwm_visible != 0,
            snapshot: MonitorSnapshot::from_raw(&raw.snapshot),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotServiceAttachmentRole {
    Router,
    Pub,
    Sub,
}

#[allow(dead_code)]
impl SpotServiceAttachmentRole {
    fn to_raw(self) -> ffi::zlink_spot_service_attachment_role_t {
        match self {
            Self::Router => {
                ffi::zlink_spot_service_attachment_role_t::ZLINK_SPOT_SERVICE_ATTACHMENT_ROUTER
            }
            Self::Pub => {
                ffi::zlink_spot_service_attachment_role_t::ZLINK_SPOT_SERVICE_ATTACHMENT_PUB
            }
            Self::Sub => {
                ffi::zlink_spot_service_attachment_role_t::ZLINK_SPOT_SERVICE_ATTACHMENT_SUB
            }
        }
    }

    fn from_raw(raw: ffi::zlink_spot_service_attachment_role_t) -> Self {
        match raw {
            ffi::zlink_spot_service_attachment_role_t::ZLINK_SPOT_SERVICE_ATTACHMENT_PUB => {
                Self::Pub
            }
            ffi::zlink_spot_service_attachment_role_t::ZLINK_SPOT_SERVICE_ATTACHMENT_SUB => {
                Self::Sub
            }
            ffi::zlink_spot_service_attachment_role_t::ZLINK_SPOT_SERVICE_ATTACHMENT_ROUTER => {
                Self::Router
            }
        }
    }
}

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

impl RegistryStatus {
    fn from_raw(raw: &ffi::zlink_registry_status_t) -> Self {
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

impl RegistryServiceSummaryEntry {
    fn from_raw(raw: &ffi::zlink_registry_service_summary_entry_t) -> Self {
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

impl MemberPeerEntry {
    fn from_raw(raw: &ffi::zlink_member_peer_entry_t) -> Result<Self, ConfigError> {
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
}

impl RegistryTopologyEntry {
    fn from_raw(raw: &ffi::zlink_registry_topology_entry_t) -> Self {
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
        }
    }
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

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

/// A Discovery instance with a fixed service view.
pub struct Discovery {
    handle: *mut c_void,
}

unsafe impl Send for Discovery {}

impl Discovery {
    pub fn new(
        ctx: &crate::ctx::Context,
        auto_connect_type: AutoConnectType,
        channel_name: &str,
    ) -> Result<Self, ConfigError> {
        let c_name = fixed_cstring_config(channel_name, "channel_name")?;
        let handle = unsafe {
            ffi::zlink_discovery_new(ctx.raw(), auto_connect_type.to_raw(), c_name.as_ptr())
        };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self { handle })
    }

    pub fn connect_registry(&self, endpoint: &str) -> Result<(), ConnectError> {
        let c = CString::new(endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe { ffi::zlink_discovery_connect_registry(self.handle, c.as_ptr()) })
    }

    pub fn set_value(&self, value: i64) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_discovery_set_value(self.handle, value) })
    }

    pub fn get_value(&self) -> Result<i64, ConfigError> {
        let mut v: i64 = 0;
        check_config_rc(unsafe { ffi::zlink_discovery_get_value(self.handle, &mut v) })?;
        Ok(v)
    }

    pub fn set_spot_owner_sync_enabled(&self, enabled: bool) -> Result<(), ConfigError> {
        let value: i32 = if enabled { 1 } else { 0 };
        check_config_rc(unsafe {
            ffi::zlink_set_option(
                self.handle,
                ffi::zlink_option_t::ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC,
                (&value as *const i32).cast(),
                std::mem::size_of::<i32>(),
            )
        })
    }

    pub fn spot_owner_sync_enabled(&self) -> Result<bool, ConfigError> {
        let mut value: i32 = 0;
        let mut len = std::mem::size_of::<i32>();
        check_config_rc(unsafe {
            ffi::zlink_get_option(
                self.handle,
                ffi::zlink_option_t::ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC,
                (&mut value as *mut i32).cast(),
                &mut len,
            )
        })?;
        Ok(value != 0)
    }

    pub fn set_actor_route_sync_enabled(&self, enabled: bool) -> Result<(), ConfigError> {
        let value: i32 = if enabled { 1 } else { 0 };
        check_config_rc(unsafe {
            ffi::zlink_set_option(
                self.handle,
                ffi::zlink_option_t::ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
                (&value as *const i32).cast(),
                std::mem::size_of::<i32>(),
            )
        })
    }

    pub fn actor_route_sync_enabled(&self) -> Result<bool, ConfigError> {
        let mut value: i32 = 0;
        let mut len = std::mem::size_of::<i32>();
        check_config_rc(unsafe {
            ffi::zlink_get_option(
                self.handle,
                ffi::zlink_option_t::ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
                (&mut value as *mut i32).cast(),
                &mut len,
            )
        })?;
        Ok(value != 0)
    }

    pub fn resolve_spot(&self, spot_rid: &RoutingId) -> Result<RoutingId, ConfigError> {
        let mut owner_node_rid = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        check_config_rc(unsafe {
            ffi::zlink_discovery_resolve_spot(
                self.handle,
                spot_rid.as_raw(),
                owner_node_rid.as_mut_ptr(),
            )
        })?;
        Ok(RoutingId::from_raw(unsafe { owner_node_rid.assume_init() }))
    }

    pub fn resolve_actor(&self, actor_id: &str) -> Result<ActorRoute, ConfigError> {
        let c_actor_id = fixed_cstring_config(actor_id, "actor_id")?;
        let mut raw = MaybeUninit::<ffi::zlink_actor_route_t>::zeroed();
        check_config_rc(unsafe {
            ffi::zlink_discovery_resolve_actor(self.handle, c_actor_id.as_ptr(), raw.as_mut_ptr())
        })?;
        Ok(ActorRoute::from_raw(&unsafe { raw.assume_init() }))
    }

    pub fn member_peers(&self) -> Result<Vec<MemberPeerEntry>, ConfigError> {
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_discovery_member_peers(self.handle, ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }

        let mut entries =
            vec![unsafe { std::mem::zeroed::<ffi::zlink_member_peer_entry_t>() }; count];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                ffi::zlink_discovery_member_peers(self.handle, entries_ptr, count_ptr)
            },
            entries.as_mut_ptr(),
        )?;
        entries[..actual]
            .iter()
            .map(MemberPeerEntry::from_raw)
            .collect()
    }

    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        set_tls_client_config(self.handle, ca_cert_pem, hostname, trust_system)
    }

    #[allow(dead_code)]
    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        destroy_handle_close(&mut self.handle, ffi::zlink_discovery_destroy)
    }
}

impl Drop for Discovery {
    fn drop(&mut self) {
        let _ = destroy_handle(&mut self.handle, ffi::zlink_discovery_destroy);
    }
}

// ---------------------------------------------------------------------------
// Actor values
// ---------------------------------------------------------------------------

/// Copyable address for a local or remote Actor.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ActorRef {
    pub node_rid: RoutingId,
    pub actor_id: String,
    pub generation: u64,
}

impl ActorRef {
    pub fn is_unchecked(&self) -> bool {
        self.generation == 0
    }

    pub(crate) fn from_raw(raw: &ffi::zlink_actor_ref_t) -> Self {
        Self {
            node_rid: RoutingId::from_raw(raw.node_rid),
            actor_id: unsafe { CStr::from_ptr(raw.actor_id.as_ptr()) }
                .to_string_lossy()
                .into_owned(),
            generation: raw.generation,
        }
    }

    pub(crate) fn to_raw(&self) -> Result<ffi::zlink_actor_ref_t, ConfigError> {
        let mut raw = ffi::zlink_actor_ref_t {
            node_rid: *self.node_rid.as_raw(),
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: self.generation,
        };
        write_c_array_config(&mut raw.actor_id, &self.actor_id, "actor_id")?;
        Ok(raw)
    }
}

fn routing_id_option(raw: ffi::zlink_routing_id_t) -> Option<RoutingId> {
    if raw.size == 0 {
        None
    } else {
        Some(RoutingId::from_raw(raw))
    }
}

fn routing_id_from_handle(handle: *mut c_void) -> Result<RoutingId, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
    check_config_rc(unsafe { ffi::zlink_get_routing_id(handle, raw.as_mut_ptr()) })?;
    Ok(RoutingId::from_raw(unsafe { raw.assume_init() }))
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ActorRoute {
    pub actor: ActorRef,
    pub joined: bool,
    pub joined_spot_rid: Option<RoutingId>,
}

impl ActorRoute {
    fn from_raw(raw: &ffi::zlink_actor_route_t) -> Self {
        Self {
            actor: ActorRef::from_raw(&raw.actor),
            joined: raw.joined != 0,
            joined_spot_rid: RoutingId::from_raw_optional(raw.joined_spot_rid),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ActorRecvInfo {
    pub actor: ActorRef,
    pub source_node_rid: RoutingId,
    pub source_session_rid: RoutingId,
    pub flags: u32,
}

impl ActorRecvInfo {
    fn from_raw(raw: &ffi::zlink_actor_recv_info_t) -> Self {
        Self {
            actor: ActorRef::from_raw(&raw.actor),
            source_node_rid: RoutingId::from_raw(raw.source_node_rid),
            source_session_rid: RoutingId::from_raw(raw.source_session_rid),
            flags: raw.flags,
        }
    }
}

pub struct ActorPart {
    pub info: ActorRecvInfo,
    pub message: Message,
    pub more: bool,
}

impl ActorPart {
    fn new(info: ActorRecvInfo, message: Message, more: bool) -> Self {
        Self {
            info,
            message,
            more,
        }
    }
}

#[derive(Debug, Clone)]
pub struct ActorJoinInfo {
    pub actor: ActorRef,
    pub source_actor: ActorRef,
    pub target_actor: ActorRef,
    pub source_node_rid: RoutingId,
    pub source_spot_rid: Option<RoutingId>,
    pub target_node_rid: Option<RoutingId>,
    pub target_spot_rid: Option<RoutingId>,
    pub join_epoch: u64,
    pub flags: u32,
    raw: ffi::zlink_actor_join_info_t,
}

impl ActorJoinInfo {
    fn from_raw(raw: ffi::zlink_actor_join_info_t) -> Self {
        let source_actor = ActorRef::from_raw(&raw.source_actor);
        let target_actor = ActorRef::from_raw(&raw.target_actor);
        Self {
            actor: source_actor.clone(),
            source_actor,
            target_actor,
            source_node_rid: RoutingId::from_raw(raw.source_node_rid),
            source_spot_rid: routing_id_option(raw.source_spot_rid),
            target_node_rid: routing_id_option(raw.target_node_rid),
            target_spot_rid: routing_id_option(raw.target_spot_rid),
            join_epoch: raw.join_epoch,
            flags: raw.flags,
            raw,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SpotNodeSpotEntry {
    pub spot_rid: RoutingId,
    pub dispatch_handler_attached: bool,
    pub joined_actor_count: u32,
    pub pending_actor_join_count: u32,
    pub route_synced: bool,
    pub last_changed_ms: u64,
}

impl SpotNodeSpotEntry {
    fn from_raw(raw: &ffi::zlink_spot_node_spot_entry_t) -> Self {
        Self {
            spot_rid: RoutingId::from_raw(raw.spot_rid),
            dispatch_handler_attached: raw.dispatch_handler_attached != 0,
            joined_actor_count: raw.joined_actor_count,
            pending_actor_join_count: raw.pending_actor_join_count,
            route_synced: raw.route_synced != 0,
            last_changed_ms: raw.last_changed_ms,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SpotNodeActorEntry {
    pub actor: ActorRef,
    pub joined: bool,
    pub joined_spot_rid: Option<RoutingId>,
    pub route_synced: bool,
    pub pending_message_count: u32,
    pub last_changed_ms: u64,
}

impl SpotNodeActorEntry {
    fn from_raw(raw: &ffi::zlink_spot_node_actor_entry_t) -> Self {
        Self {
            actor: ActorRef::from_raw(&raw.actor),
            joined: raw.joined != 0,
            joined_spot_rid: RoutingId::from_raw_optional(raw.joined_spot_rid),
            route_synced: raw.route_synced != 0,
            pending_message_count: raw.pending_message_count,
            last_changed_ms: raw.last_changed_ms,
        }
    }
}

/// Owned local Actor facade.
pub struct Actor {
    node_handle: *mut c_void,
    actor_ref: Option<ActorRef>,
}

unsafe impl Send for Actor {}

// ---------------------------------------------------------------------------
// SpotNode
// ---------------------------------------------------------------------------

/// SPOT node runtime for topology, discovery, and lifecycle.
pub struct SpotNode {
    handle: *mut c_void,
}

unsafe impl Send for SpotNode {}

impl SpotNode {
    pub fn new(ctx: &crate::ctx::Context) -> Result<Self, ConfigError> {
        let handle = unsafe { ffi::zlink_spot_node_new(ctx.raw(), std::ptr::null()) };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self { handle })
    }

    pub fn new_with_options(
        ctx: &crate::ctx::Context,
        options: SpotNodeOptions,
    ) -> Result<Self, ConfigError> {
        let raw_options = ffi::zlink_spot_node_options_t {
            mode: options.mode.unwrap_or(SpotNodeMode::All).to_raw(),
        };
        let handle = unsafe {
            ffi::zlink_spot_node_new(
                ctx.raw(),
                (&raw_options as *const ffi::zlink_spot_node_options_t).cast(),
            )
        };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self { handle })
    }

    pub fn bind(&self, endpoint: &str) -> Result<(), BindError> {
        let c = CString::new(endpoint)
            .map_err(|_| BindError::new(crate::error::BindResult::InvalidArgument, libc::EINVAL))?;
        check_bind_rc(unsafe { ffi::zlink_spot_node_bind(self.handle, c.as_ptr()) })
    }

    pub fn last_endpoint(&self) -> Result<String, ConfigError> {
        Ok(self.status_snapshot()?.local_endpoint)
    }

    pub fn connect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError> {
        let c = CString::new(peer_endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe { ffi::zlink_spot_node_connect_peer(self.handle, c.as_ptr()) })
    }

    pub fn disconnect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError> {
        let c = CString::new(peer_endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe { ffi::zlink_spot_node_disconnect_peer(self.handle, c.as_ptr()) })
    }

    pub fn disconnect_peer_rid(&self, target_node_rid: &RoutingId) -> Result<(), ConnectError> {
        check_connect_rc(unsafe {
            ffi::zlink_spot_node_disconnect_peer_rid(self.handle, target_node_rid.as_raw())
        })
    }

    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_spot_node_attach_discovery(self.handle, discovery.raw())
        })
    }

    pub fn attach_channel_dealer(
        &self,
        discovery: &Discovery,
        dealer: &crate::socket::DealerSocket,
    ) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_spot_node_attach_channel_dealer(
                self.handle,
                discovery.raw(),
                dealer.inner.handle,
            )
        })
    }

    pub fn attach_channel_dealer_manual(
        &self,
        channel_name: &str,
        dealer: &crate::socket::DealerSocket,
    ) -> Result<(), ConfigError> {
        let c_channel_name = fixed_cstring_config(channel_name, "channel_name")?;
        check_config_rc(unsafe {
            ffi::zlink_spot_node_attach_channel_dealer_manual(
                self.handle,
                c_channel_name.as_ptr(),
                dealer.inner.handle,
            )
        })
    }

    pub fn attach_pub_ingress(
        &self,
        pub_sock: &crate::socket::PubSocket,
    ) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_spot_node_attach_pub_ingress(self.handle, pub_sock.inner.handle)
        })
    }

    fn set_option_i32(
        &self,
        option: ffi::zlink_spot_node_option_t,
        value: i32,
    ) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_spot_node_option(
                self.handle,
                option,
                (&value as *const i32).cast(),
                std::mem::size_of::<i32>(),
            )
        })
    }

    fn get_option_i32(&self, option: ffi::zlink_spot_node_option_t) -> Result<i32, ConfigError> {
        let mut value: i32 = 0;
        let mut len = std::mem::size_of::<i32>();
        check_config_rc(unsafe {
            ffi::zlink_get_spot_node_option(
                self.handle,
                option,
                (&mut value as *mut i32).cast(),
                &mut len,
            )
        })?;
        Ok(value)
    }

    pub fn router_hwm(&self) -> Result<i32, ConfigError> {
        self.get_option_i32(ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_ROUTER_HWM)
    }

    pub fn set_router_hwm(&self, value: i32) -> Result<(), ConfigError> {
        self.set_option_i32(
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_ROUTER_HWM,
            value,
        )
    }

    pub fn pubsub_hwm(&self) -> Result<i32, ConfigError> {
        self.get_option_i32(ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_PUBSUB_HWM)
    }

    pub fn set_pubsub_hwm(&self, value: i32) -> Result<(), ConfigError> {
        self.set_option_i32(
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_PUBSUB_HWM,
            value,
        )
    }

    pub fn router_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError> {
        let raw = self.get_option_i32(
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE,
        )?;
        AutoHwmProfile::from_raw(raw)
    }

    pub fn set_router_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        self.set_option_i32(
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE,
            profile.to_raw(),
        )
    }

    pub fn pubsub_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError> {
        let raw = self.get_option_i32(
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE,
        )?;
        AutoHwmProfile::from_raw(raw)
    }

    pub fn set_pubsub_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        self.set_option_i32(
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE,
            profile.to_raw(),
        )
    }

    pub fn dispatch_workers_min(&self) -> Result<i32, ConfigError> {
        self.get_option_i32(ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN)
    }

    pub fn set_dispatch_workers_min(&self, value: i32) -> Result<(), ConfigError> {
        self.set_option_i32(
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN,
            value,
        )
    }

    pub fn dispatch_workers_max(&self) -> Result<i32, ConfigError> {
        self.get_option_i32(ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX)
    }

    pub fn set_dispatch_workers_max(&self, value: i32) -> Result<(), ConfigError> {
        self.set_option_i32(
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX,
            value,
        )
    }

    pub fn set_tls_server(
        &self,
        cert_pem: &str,
        key_pem: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        set_tls_server_config(self.handle, cert_pem, key_pem, require_client_cert)
    }

    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        set_tls_client_config(self.handle, ca_cert_pem, hostname, trust_system)
    }

    pub fn create_actor(&self, actor_id: &str) -> Result<Actor, ConfigError> {
        let c_actor_id = fixed_cstring_config(actor_id, "actor_id")?;
        let mut raw = MaybeUninit::<ffi::zlink_actor_ref_t>::zeroed();
        check_config_rc(unsafe {
            ffi::zlink_spot_node_actor_new(self.handle, c_actor_id.as_ptr(), raw.as_mut_ptr())
        })?;
        Ok(Actor {
            node_handle: self.handle,
            actor_ref: Some(ActorRef::from_raw(&unsafe { raw.assume_init() })),
        })
    }

    pub fn actor_lookup(&self, actor_id: &str) -> Result<ActorRef, ConfigError> {
        let c_actor_id = fixed_cstring_config(actor_id, "actor_id")?;
        let mut raw = MaybeUninit::<ffi::zlink_actor_ref_t>::zeroed();
        check_config_rc(unsafe {
            ffi::zlink_spot_node_actor_lookup(self.handle, c_actor_id.as_ptr(), raw.as_mut_ptr())
        })?;
        Ok(ActorRef::from_raw(&unsafe { raw.assume_init() }))
    }

    /// Build an unchecked remote Actor ref (generation == 0) from a target node rid + id.
    pub fn remote_actor_ref(
        target_node_rid: &RoutingId,
        actor_id: &str,
    ) -> Result<ActorRef, ConfigError> {
        let c_actor_id = fixed_cstring_config(actor_id, "actor_id")?;
        let mut raw = ffi::zlink_actor_ref_t {
            node_rid: *target_node_rid.as_raw(),
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: 0,
        };
        let bytes = c_actor_id.as_bytes_with_nul();
        for (idx, byte) in bytes.iter().enumerate().take(raw.actor_id.len()) {
            raw.actor_id[idx] = *byte as c_char;
        }
        Ok(ActorRef::from_raw(&raw))
    }

    /// Async remote Actor lookup (operation builder).
    pub fn remote_actor_get_ref(
        &self,
        target_node_rid: &RoutingId,
        actor_id: &str,
    ) -> ActorLookupOp<Empty> {
        let c_actor_id = fixed_cstring_or_panic(actor_id, "actor_id");
        ActorLookupOp {
            node_handle: self.handle,
            target_node_rid: target_node_rid.clone(),
            actor_id: c_actor_id,
            timeout: Duration::ZERO,
            _state: std::marker::PhantomData,
        }
    }

    /// Async destroy (operation builder).
    pub fn destroy_actor(&self, actor: &ActorRef) -> ActorDestroyOp<Empty> {
        let raw = actor.to_raw().unwrap_or(ffi::zlink_actor_ref_t {
            node_rid: ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            },
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: 0,
        });
        ActorDestroyOp {
            inner: ActorReplyOpInner {
                handle: self.handle,
                kind: ActorReplyOpKind::Destroy { actor: raw },
                timeout: Duration::ZERO,
            },
            _state: std::marker::PhantomData,
        }
    }

    /// Async user-Spot join (operation builder). Payload accumulates via `.message(...)`.
    pub fn join_actor(
        &self,
        actor: &ActorRef,
        dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId,
    ) -> ActorJoinOp<Empty> {
        let raw = actor.to_raw().unwrap_or(ffi::zlink_actor_ref_t {
            node_rid: ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            },
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: 0,
        });
        ActorJoinOp {
            node_handle: self.handle,
            spot_handle: std::ptr::null_mut(),
            actor: raw,
            dest_node_rid: dest_node_rid.clone(),
            dest_spot_rid: dest_spot_rid.clone(),
            parts: Vec::new(),
            flags: SendFlags::NONE,
            timeout: Duration::ZERO,
            _state: std::marker::PhantomData,
        }
    }

    /// Async leave to the same node's Entry Spot (operation builder).
    pub fn leave_actor(
        &self,
        actor: &ActorRef,
        current_spot_rid: &RoutingId,
    ) -> ActorLeaveOp<Empty> {
        let raw = actor.to_raw().unwrap_or(ffi::zlink_actor_ref_t {
            node_rid: ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            },
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: 0,
        });
        ActorLeaveOp {
            inner: ActorReplyOpInner {
                handle: self.handle,
                kind: ActorReplyOpKind::Leave {
                    actor: raw,
                    current_spot_rid: current_spot_rid.clone(),
                },
                timeout: Duration::ZERO,
            },
            _state: std::marker::PhantomData,
        }
    }

    /// Actor-to-session relay (operation builder).
    pub fn send_bound_session_msg(&self, actor: &ActorRef) -> SendOp<Empty> {
        let raw = actor.to_raw().unwrap_or(ffi::zlink_actor_ref_t {
            node_rid: ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            },
            actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
            generation: 0,
        });
        SendOp {
            handle: self.handle,
            kind: SendOpKind::ActorBoundSession { actor: raw },
            parts: Vec::new(),
            flags: SendFlags::NONE,
            _state: std::marker::PhantomData,
        }
    }

    pub fn status_snapshot(&self) -> Result<SpotNodeStatus, ConfigError> {
        let mut raw = MaybeUninit::<ffi::zlink_spot_node_status_t>::uninit();
        check_config_rc(unsafe {
            ffi::zlink_spot_node_status_snapshot(self.handle, raw.as_mut_ptr())
        })?;
        let raw = unsafe { raw.assume_init() };
        Ok(SpotNodeStatus::from_raw(&raw))
    }

    pub fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_routing_id(self.handle, rid.data().as_ptr() as *const c_void, rid.len())
        })
    }

    pub fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        let mut raw = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        check_config_rc(unsafe { ffi::zlink_get_routing_id(self.handle, raw.as_mut_ptr()) })?;
        Ok(RoutingId::from_raw(unsafe { raw.assume_init() }))
    }

    pub fn entry_spot(&self) -> Result<Spot, ConfigError> {
        let mut spot_handle: *mut c_void = ptr::null_mut();
        check_config_rc(unsafe { ffi::zlink_spot_node_entry_spot(self.handle, &mut spot_handle) })?;
        if spot_handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Spot {
            handle: spot_handle,
            node_handle: self.handle,
            send_ready_cb: None,
            routed_cb: None,
            dispatch_cb: None,
        })
    }

    pub fn create_spot(&self) -> Result<Spot, ConfigError> {
        Spot::new(self)
    }

    pub fn spot_lookup(&self, spot_rid: &RoutingId) -> Result<Option<Spot>, ConfigError> {
        let mut spot_handle: *mut c_void = ptr::null_mut();
        check_config_rc(unsafe {
            ffi::zlink_spot_node_spot_lookup(self.handle, spot_rid.as_raw(), &mut spot_handle)
        })?;
        if spot_handle.is_null() {
            return Ok(None);
        }
        Ok(Some(Spot {
            handle: spot_handle,
            node_handle: self.handle,
            send_ready_cb: None,
            routed_cb: None,
            dispatch_cb: None,
        }))
    }

    pub fn peers_snapshot(&self) -> Result<Vec<SpotNodePeerEntry>, ConfigError> {
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_spot_node_peers_snapshot(self.handle, ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }

        let mut entries =
            vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_peer_entry_t>() }; count];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                ffi::zlink_spot_node_peers_snapshot(self.handle, entries_ptr, count_ptr)
            },
            entries.as_mut_ptr(),
        )?;
        Ok(entries[..actual]
            .iter()
            .map(SpotNodePeerEntry::from_raw)
            .collect())
    }

    pub fn peers_query(
        &self,
        filter: &SpotNodePeerFilter,
    ) -> Result<Vec<SpotNodePeerEntry>, ConfigError> {
        with_spot_node_peer_filter_config(filter, |filter_ptr| {
            let count = count_entries_config(|count| unsafe {
                ffi::zlink_spot_node_peers_query(self.handle, filter_ptr, ptr::null_mut(), count)
            })?;
            if count == 0 {
                return Ok(Vec::new());
            }

            let mut entries =
                vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_peer_entry_t>() }; count];
            let actual = read_entries_config(
                count,
                |entries_ptr, count_ptr| unsafe {
                    ffi::zlink_spot_node_peers_query(
                        self.handle,
                        filter_ptr,
                        entries_ptr,
                        count_ptr,
                    )
                },
                entries.as_mut_ptr(),
            )?;
            Ok(entries[..actual]
                .iter()
                .map(SpotNodePeerEntry::from_raw)
                .collect())
        })
    }

    pub fn subjects_snapshot(
        &self,
        filter: Option<&SpotNodeSubjectFilter>,
    ) -> Result<Vec<SpotNodeSubjectEntry>, ConfigError> {
        self.subjects_query_opt(filter)
    }

    pub fn internal_sockets_snapshot(
        &self,
        filter: Option<&SpotNodeSocketSnapshotFilter>,
    ) -> Result<Vec<SpotNodeSocketSnapshotEntry>, ConfigError> {
        self.internal_sockets_snapshot_opt(filter)
    }

    pub fn spots_snapshot(&self) -> Result<Vec<SpotNodeSpotEntry>, ConfigError> {
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_spot_node_spots_snapshot(self.handle, ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }
        let mut entries =
            vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_spot_entry_t>() }; count];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                ffi::zlink_spot_node_spots_snapshot(self.handle, entries_ptr, count_ptr)
            },
            entries.as_mut_ptr(),
        )?;
        Ok(entries[..actual]
            .iter()
            .map(SpotNodeSpotEntry::from_raw)
            .collect())
    }

    pub fn actors_snapshot(&self) -> Result<Vec<SpotNodeActorEntry>, ConfigError> {
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_spot_node_actors_snapshot(self.handle, ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }
        let mut entries =
            vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_actor_entry_t>() }; count];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                ffi::zlink_spot_node_actors_snapshot(self.handle, entries_ptr, count_ptr)
            },
            entries.as_mut_ptr(),
        )?;
        Ok(entries[..actual]
            .iter()
            .map(SpotNodeActorEntry::from_raw)
            .collect())
    }

    #[allow(dead_code)]
    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        destroy_handle_close(&mut self.handle, ffi::zlink_spot_node_destroy)
    }
}

impl Drop for SpotNode {
    fn drop(&mut self) {
        let _ = destroy_handle(&mut self.handle, ffi::zlink_spot_node_destroy);
    }
}

impl Actor {
    pub fn actor_ref(&self) -> Result<ActorRef, ConfigError> {
        self.actor_ref.as_ref().cloned().ok_or_else(|| {
            ConfigError::new(crate::error::ConfigResult::InvalidHandle, libc::EINVAL)
        })
    }

    pub fn close_with_timeout(&mut self, timeout: Duration) -> Result<(), RequestError> {
        let Some(actor_ref) = self.actor_ref.take() else {
            return Ok(());
        };
        let raw = actor_ref.to_raw().map_err(|err| {
            RequestError::new(
                crate::error::RequestResult::InvalidArgument,
                err.internal_errno(),
            )
        })?;
        wait_reply_submit(|handler, userdata| unsafe {
            ffi::zlink_spot_node_actor_destroy(
                self.node_handle,
                &raw,
                handler,
                userdata,
                timeout_to_timeout_ms(timeout),
            )
        })
    }

    pub fn close(&mut self) -> Result<(), RequestError> {
        self.close_with_timeout(Duration::from_millis(0))
    }

    /// Async user-Spot join (operation builder).
    pub fn join(&self, spot: &Spot) -> ActorJoinOp<Empty> {
        let raw_actor =
            self.actor_ref()
                .and_then(|a| a.to_raw())
                .unwrap_or(ffi::zlink_actor_ref_t {
                    node_rid: ffi::zlink_routing_id_t {
                        size: 0,
                        data: [0; 255],
                    },
                    actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
                    generation: 0,
                });
        let dest_node_rid = routing_id_from_handle(spot.node_handle).unwrap_or_else(|_| {
            RoutingId::from_raw(ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            })
        });
        let dest_spot_rid = spot.routing_id().unwrap_or_else(|_| {
            RoutingId::from_raw(ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            })
        });
        ActorJoinOp {
            node_handle: self.node_handle,
            spot_handle: spot.handle,
            actor: raw_actor,
            dest_node_rid,
            dest_spot_rid,
            parts: Vec::new(),
            flags: SendFlags::NONE,
            timeout: Duration::ZERO,
            _state: std::marker::PhantomData,
        }
    }

    /// Async leave to the same node's Entry Spot (operation builder).
    pub fn leave(&self, spot: &Spot) -> ActorLeaveOp<Empty> {
        let raw_actor =
            self.actor_ref()
                .and_then(|a| a.to_raw())
                .unwrap_or(ffi::zlink_actor_ref_t {
                    node_rid: ffi::zlink_routing_id_t {
                        size: 0,
                        data: [0; 255],
                    },
                    actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
                    generation: 0,
                });
        let dest_spot_rid = spot.routing_id().unwrap_or_else(|_| {
            RoutingId::from_raw(ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            })
        });
        ActorLeaveOp {
            inner: ActorReplyOpInner {
                handle: self.node_handle,
                kind: ActorReplyOpKind::Leave {
                    actor: raw_actor,
                    current_spot_rid: dest_spot_rid,
                },
                timeout: Duration::ZERO,
            },
            _state: std::marker::PhantomData,
        }
    }

    pub fn recv_part_with_flags(&self, flags: RecvFlags) -> Result<Option<ActorPart>, RecvError> {
        let actor_ref = self.actor_ref().map_err(recv_error_from_config)?;
        recv_actor_part_from_ref(self.node_handle, &actor_ref, flags)
    }

    pub fn recv_part(&self) -> Result<ActorPart, RecvError> {
        self.recv_part_with_flags(RecvFlags::NONE)?
            .ok_or_else(|| RecvError::new(RecvResult::NoData, libc::EAGAIN))
    }

    /// Actor-to-session relay (operation builder).
    pub fn send_bound_session_msg(&self) -> SendOp<Empty> {
        let raw_actor =
            self.actor_ref()
                .and_then(|a| a.to_raw())
                .unwrap_or(ffi::zlink_actor_ref_t {
                    node_rid: ffi::zlink_routing_id_t {
                        size: 0,
                        data: [0; 255],
                    },
                    actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
                    generation: 0,
                });
        SendOp {
            handle: self.node_handle,
            kind: SendOpKind::ActorBoundSession { actor: raw_actor },
            parts: Vec::new(),
            flags: SendFlags::NONE,
            _state: std::marker::PhantomData,
        }
    }

    pub fn close_bound_session(&self, timeout: Duration) -> Result<(), RequestError> {
        let raw_actor = self
            .actor_ref()
            .map_err(|err| {
                RequestError::new(
                    crate::error::RequestResult::InvalidArgument,
                    err.internal_errno(),
                )
            })?
            .to_raw()
            .map_err(|err| {
                RequestError::new(
                    crate::error::RequestResult::InvalidArgument,
                    err.internal_errno(),
                )
            })?;
        check_request_result(unsafe {
            ffi::zlink_spot_node_actor_close_bound_session(
                self.node_handle,
                &raw_actor,
                timeout_to_timeout_ms(timeout),
            )
        })
    }
}

impl Drop for Actor {
    fn drop(&mut self) {
        let _ = self.close();
    }
}

// ---------------------------------------------------------------------------
// Typestate operation builders
// ---------------------------------------------------------------------------

/// Typestate marker: no message has been set yet.
pub struct Empty;
/// Typestate marker: at least one message part has been set.
pub struct Ready;
/// Typestate marker: flags have been set, only callback submit available.
pub struct CallbackReady;

/// Send operation builder.
///
/// Used by Spot publish / send_channel / send_to_spot, by raw socket sends,
/// and by Actor / Received / SendHandle relays. Internally dispatches to the
/// proper native call based on `SendOpKind`.
pub struct SendOp<State> {
    handle: *mut c_void,
    kind: SendOpKind,
    parts: Vec<Message>,
    flags: SendFlags,
    _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for SendOp<S> {}

#[allow(clippy::large_enum_variant)]
enum SendOpKind {
    // ---- Spot-layer dispatch (handle = spot) ----
    Publish {
        topic: std::ffi::CString,
    },
    SendChannel {
        channel_name: std::ffi::CString,
    },
    SendToSpot {
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    },
    // ---- Raw socket sends (handle = socket) ----
    SocketSend,
    SocketSendTo {
        target: RoutingId,
    },
    SocketPublish {
        topic: std::ffi::CString,
    },
    RouterSendToSpot {
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    },
    // ---- Actor session bound relays ----
    /// `handle` is a SpotNode pointer. Sends one part via
    /// `zlink_spot_node_actor_send_bound_session_msg` (single-part only).
    ActorBoundSession {
        actor: ffi::zlink_actor_ref_t,
    },
    /// `handle` is a STREAM socket pointer. Sends parts via
    /// `zlink_stream_send_bound_actor_part`.
    StreamBoundActor {
        session_rid: RoutingId,
        actor_id: std::ffi::CString,
    },
}

impl SendOp<Empty> {
    pub fn message(self, message: Message) -> SendOp<Ready> {
        SendOp {
            handle: self.handle,
            kind: self.kind,
            parts: vec![message],
            flags: self.flags,
            _state: std::marker::PhantomData,
        }
    }
}

impl SendOp<Ready> {
    pub fn message(mut self, message: Message) -> Self {
        self.parts.push(message);
        self
    }

    pub fn flags(self, flags: SendFlags) -> Self {
        SendOp { flags, ..self }
    }

    /// Submit the send operation.
    /// Returns `Ok(false)` for temporary backpressure with `DONT_WAIT`, `Ok(true)` on success.
    /// # Errors: SubmitError
    pub fn submit(mut self) -> Result<bool, SubmitError> {
        let flags = self.flags;
        let handle = self.handle;
        // For ActorBoundSession we send exactly one part via a single FFI call
        // that takes a `*mut zlink_msg_t` and not the part-stream API.
        if let SendOpKind::ActorBoundSession { actor } = &self.kind {
            if self.parts.len() != 1 {
                return Err(crate::error::submit_validation_error());
            }
            let mut native = take_message_raw(&mut self.parts[0]);
            let rc = unsafe {
                ffi::zlink_spot_node_actor_send_bound_session_msg(
                    handle,
                    actor,
                    &mut native,
                    flags.bits(),
                )
            };
            if rc != 0 {
                unsafe {
                    ffi::zlink_msg_close(&mut native);
                }
            }
            return check_submit_rc(rc).map(|_| true);
        }
        let mut native = prepare_send_parts(&mut self.parts)?;
        let rc = match &self.kind {
            SendOpKind::Publish { topic } => {
                let tp = topic.as_ptr();
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_spot_publish_part(handle, tp, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::SendChannel { channel_name } => {
                let cn = channel_name.as_ptr();
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_spot_send_channel_part(handle, cn, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::SendToSpot {
                dest_node_rid,
                dest_spot_rid,
            } => {
                let nr = dest_node_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let sr = dest_spot_rid.as_raw() as *const ffi::zlink_routing_id_t;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_spot_send_spot_part(handle, nr, sr, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::SocketSend => {
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_send_part(handle, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::SocketSendTo { target } => {
                let t = target.as_raw() as *const ffi::zlink_routing_id_t;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_send_part_rid(handle, t, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::SocketPublish { topic } => {
                let tp = topic.as_ptr();
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_publish_part(handle, tp, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::RouterSendToSpot {
                dest_node_rid,
                dest_spot_rid,
            } => {
                let nr = dest_node_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let sr = dest_spot_rid.as_raw() as *const ffi::zlink_routing_id_t;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_router_send_spot_part(handle, nr, sr, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::StreamBoundActor {
                session_rid,
                actor_id,
            } => {
                let sr = session_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let aid = actor_id.as_ptr();
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_stream_send_bound_actor_part(
                        handle,
                        sr,
                        aid,
                        part,
                        flags.bits(),
                        part_flag,
                    )
                })?
            }
            SendOpKind::ActorBoundSession { .. } => unreachable!(),
        };
        drop(self.parts);
        let result = check_submit_rc(rc);
        match result {
            Ok(()) => Ok(true),
            Err(e) if e.code() == crate::error::SubmitResult::Backpressured => Ok(false),
            Err(e) => Err(e),
        }
    }
}

pub(crate) fn actor_bind_op_new(
    handle: *mut c_void,
    session_rid: RoutingId,
    actor: ffi::zlink_actor_ref_t,
) -> ActorBindOp<Empty> {
    ActorBindOp {
        inner: ActorReplyOpInner {
            handle,
            kind: ActorReplyOpKind::Bind { session_rid, actor },
            timeout: Duration::ZERO,
        },
        _state: std::marker::PhantomData,
    }
}

pub(crate) fn actor_unbind_op_new(
    handle: *mut c_void,
    session_rid: RoutingId,
    actor_id: std::ffi::CString,
) -> ActorUnbindOp<Empty> {
    ActorUnbindOp {
        inner: ActorReplyOpInner {
            handle,
            kind: ActorReplyOpKind::Unbind {
                session_rid,
                actor_id,
            },
            timeout: Duration::ZERO,
        },
        _state: std::marker::PhantomData,
    }
}

pub(crate) fn stream_bound_actor_send_op(
    handle: *mut c_void,
    session_rid: RoutingId,
    actor_id: std::ffi::CString,
) -> SendOp<Empty> {
    SendOp {
        handle,
        kind: SendOpKind::StreamBoundActor {
            session_rid,
            actor_id,
        },
        parts: Vec::new(),
        flags: SendFlags::NONE,
        _state: std::marker::PhantomData,
    }
}

pub(crate) fn socket_send_op(handle: *mut c_void) -> SendOp<Empty> {
    SendOp {
        handle,
        kind: SendOpKind::SocketSend,
        parts: Vec::new(),
        flags: SendFlags::NONE,
        _state: std::marker::PhantomData,
    }
}

pub(crate) fn socket_send_to_op(handle: *mut c_void, target: RoutingId) -> SendOp<Empty> {
    SendOp {
        handle,
        kind: SendOpKind::SocketSendTo { target },
        parts: Vec::new(),
        flags: SendFlags::NONE,
        _state: std::marker::PhantomData,
    }
}

pub(crate) fn socket_publish_op(handle: *mut c_void, topic: std::ffi::CString) -> SendOp<Empty> {
    SendOp {
        handle,
        kind: SendOpKind::SocketPublish { topic },
        parts: Vec::new(),
        flags: SendFlags::NONE,
        _state: std::marker::PhantomData,
    }
}

pub(crate) fn router_send_to_spot_op(
    handle: *mut c_void,
    dest_node_rid: RoutingId,
    dest_spot_rid: RoutingId,
) -> SendOp<Empty> {
    SendOp {
        handle,
        kind: SendOpKind::RouterSendToSpot {
            dest_node_rid,
            dest_spot_rid,
        },
        parts: Vec::new(),
        flags: SendFlags::NONE,
        _state: std::marker::PhantomData,
    }
}

pub(crate) fn spot_send_to_spot_op(
    handle: *mut c_void,
    dest_node_rid: RoutingId,
    dest_spot_rid: RoutingId,
) -> SendOp<Empty> {
    SendOp {
        handle,
        kind: SendOpKind::SendToSpot {
            dest_node_rid,
            dest_spot_rid,
        },
        parts: Vec::new(),
        flags: SendFlags::NONE,
        _state: std::marker::PhantomData,
    }
}

/// Request operation builder.
pub struct RequestOp<State> {
    handle: *mut c_void,
    kind: RequestOpKind,
    parts: Vec<Message>,
    flags: Option<SendFlags>,
    timeout: Duration,
    _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for RequestOp<S> {}

#[allow(clippy::large_enum_variant)]
enum RequestOpKind {
    // Spot-layer (handle = spot)
    Channel {
        channel_name: std::ffi::CString,
    },
    ToSpot {
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    },
    ToRouter {
        peer_rid: RoutingId,
    },
    // Raw socket (handle = socket)
    DealerRequest,
    RouterRequest {
        peer_rid: RoutingId,
    },
    RouterRequestToSpot {
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    },
}

pub(crate) fn dealer_request_op(handle: *mut c_void) -> RequestOp<Empty> {
    RequestOp {
        handle,
        kind: RequestOpKind::DealerRequest,
        parts: Vec::new(),
        flags: None,
        timeout: Duration::ZERO,
        _state: std::marker::PhantomData,
    }
}

pub(crate) fn router_request_op(handle: *mut c_void, peer_rid: RoutingId) -> RequestOp<Empty> {
    RequestOp {
        handle,
        kind: RequestOpKind::RouterRequest { peer_rid },
        parts: Vec::new(),
        flags: None,
        timeout: Duration::ZERO,
        _state: std::marker::PhantomData,
    }
}

pub(crate) fn router_request_to_spot_op(
    handle: *mut c_void,
    dest_node_rid: RoutingId,
    dest_spot_rid: RoutingId,
) -> RequestOp<Empty> {
    RequestOp {
        handle,
        kind: RequestOpKind::RouterRequestToSpot {
            dest_node_rid,
            dest_spot_rid,
        },
        parts: Vec::new(),
        flags: None,
        timeout: Duration::ZERO,
        _state: std::marker::PhantomData,
    }
}

impl RequestOp<Empty> {
    pub fn message(self, message: Message) -> RequestOp<Ready> {
        RequestOp {
            handle: self.handle,
            kind: self.kind,
            parts: vec![message],
            flags: self.flags,
            timeout: self.timeout,
            _state: std::marker::PhantomData,
        }
    }
}

impl RequestOp<Ready> {
    pub fn message(mut self, message: Message) -> Self {
        self.parts.push(message);
        self
    }

    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.timeout = timeout;
        self
    }

    /// Setting flags moves to `CallbackReady` (only callback `submit` available).
    pub fn flags(self, flags: SendFlags) -> RequestOp<CallbackReady> {
        RequestOp {
            handle: self.handle,
            kind: self.kind,
            parts: self.parts,
            flags: Some(flags),
            timeout: self.timeout,
            _state: std::marker::PhantomData,
        }
    }

    /// Async submit — produces the reply.
    /// # Errors: ZlinkError (SubmitError on submit, RequestError on completion)
    pub async fn submit_async(mut self) -> Result<Vec<Message>, ZlinkError> {
        let (tx, rx) = mpsc::channel();
        self.submit_callback_inner(SendFlags::NONE, move |result| {
            let _ = tx.send(result);
        })?;
        rx.recv()
            .unwrap_or_else(|_| {
                Err(RequestError::new(
                    crate::error::RequestResult::ProtocolError,
                    libc::EINVAL,
                ))
            })
            .map_err(ZlinkError::from)
    }

    /// Callback submit.
    /// # Errors: SubmitError
    pub fn submit<F>(mut self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        self.submit_callback_inner(SendFlags::NONE, callback)
    }

    fn submit_callback_inner<F>(&mut self, flags: SendFlags, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        request_op_submit_callback(
            self.handle,
            &self.kind,
            &mut self.parts,
            flags,
            self.timeout,
            callback,
        )
    }
}

impl RequestOp<CallbackReady> {
    pub fn message(mut self, message: Message) -> Self {
        self.parts.push(message);
        self
    }

    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.timeout = timeout;
        self
    }

    pub fn flags(mut self, flags: SendFlags) -> Self {
        self.flags = Some(flags);
        self
    }

    /// # Errors: SubmitError
    pub fn submit<F>(mut self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        let flags = self.flags.unwrap_or(SendFlags::NONE);
        request_op_submit_callback(
            self.handle,
            &self.kind,
            &mut self.parts,
            flags,
            self.timeout,
            callback,
        )
    }
}

fn request_op_submit_callback<F>(
    handle: *mut c_void,
    kind: &RequestOpKind,
    parts: &mut Vec<Message>,
    flags: SendFlags,
    timeout: Duration,
    callback: F,
) -> Result<(), SubmitError>
where
    F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
{
    let mut native = prepare_send_parts(parts)?;
    let progress = match kind {
        RequestOpKind::Channel { .. }
        | RequestOpKind::ToSpot { .. }
        | RequestOpKind::ToRouter { .. } => Some(RequestProgressGuard::attach_spot(handle)),
        RequestOpKind::DealerRequest
        | RequestOpKind::RouterRequest { .. }
        | RequestOpKind::RouterRequestToSpot { .. } => {
            Some(RequestProgressGuard::attach_socket(handle))
        }
    };
    let state_ptr = Box::into_raw(Box::new(SpotReplyCallbackState {
        callback: Some(Box::new(callback)),
        _progress: progress,
    }));
    let timeout_ms = timeout_to_timeout_ms(timeout);
    let rc = match kind {
        RequestOpKind::Channel { channel_name } => {
            let cn = channel_name.as_ptr();
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_spot_request_channel_part(
                    handle,
                    cn,
                    part,
                    if is_final {
                        Some(spot_reply_callback)
                    } else {
                        None
                    },
                    if is_final {
                        state_ptr.cast()
                    } else {
                        std::ptr::null_mut()
                    },
                    flags.bits(),
                    part_flag,
                    if is_final { timeout_ms } else { 0 },
                )
            })?
        }
        RequestOpKind::ToSpot {
            dest_node_rid,
            dest_spot_rid,
        } => {
            let nr = dest_node_rid.as_raw() as *const ffi::zlink_routing_id_t;
            let sr = dest_spot_rid.as_raw() as *const ffi::zlink_routing_id_t;
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_spot_request_spot_part(
                    handle,
                    nr,
                    sr,
                    part,
                    if is_final {
                        Some(spot_reply_callback)
                    } else {
                        None
                    },
                    if is_final {
                        state_ptr.cast()
                    } else {
                        std::ptr::null_mut()
                    },
                    flags.bits(),
                    part_flag,
                    if is_final { timeout_ms } else { 0 },
                )
            })?
        }
        RequestOpKind::ToRouter { peer_rid } => {
            let pr = peer_rid.as_raw() as *const ffi::zlink_routing_id_t;
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_spot_request_spot_part(
                    handle,
                    pr,
                    std::ptr::null(),
                    part,
                    if is_final {
                        Some(spot_reply_callback)
                    } else {
                        None
                    },
                    if is_final {
                        state_ptr.cast()
                    } else {
                        std::ptr::null_mut()
                    },
                    flags.bits(),
                    part_flag,
                    if is_final { timeout_ms } else { 0 },
                )
            })?
        }
        RequestOpKind::DealerRequest => {
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_dealer_request_part(
                    handle,
                    part,
                    flags.bits(),
                    part_flag,
                    if is_final { timeout_ms } else { 0 },
                    if is_final {
                        Some(spot_reply_callback)
                    } else {
                        None
                    },
                    if is_final {
                        state_ptr.cast()
                    } else {
                        std::ptr::null_mut()
                    },
                )
            })?
        }
        RequestOpKind::RouterRequest { peer_rid } => {
            let pr = peer_rid.as_raw() as *const ffi::zlink_routing_id_t;
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_router_request_part(
                    handle,
                    pr,
                    part,
                    flags.bits(),
                    part_flag,
                    if is_final { timeout_ms } else { 0 },
                    if is_final {
                        Some(spot_reply_callback)
                    } else {
                        None
                    },
                    if is_final {
                        state_ptr.cast()
                    } else {
                        std::ptr::null_mut()
                    },
                )
            })?
        }
        RequestOpKind::RouterRequestToSpot {
            dest_node_rid,
            dest_spot_rid,
        } => {
            let nr = dest_node_rid.as_raw() as *const ffi::zlink_routing_id_t;
            let sr = dest_spot_rid.as_raw() as *const ffi::zlink_routing_id_t;
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_router_request_spot_part(
                    handle,
                    nr,
                    sr,
                    part,
                    if is_final {
                        Some(spot_reply_callback)
                    } else {
                        None
                    },
                    if is_final {
                        state_ptr.cast()
                    } else {
                        std::ptr::null_mut()
                    },
                    flags.bits(),
                    part_flag,
                    if is_final { timeout_ms } else { 0 },
                )
            })?
        }
    };
    if rc != 0 {
        unsafe {
            drop(Box::from_raw(state_ptr));
        }
    }
    check_submit_rc(rc)
}

/// Reply operation builder.
pub struct ReplyOp<State> {
    handle: *mut c_void,
    kind: ReplyOpKind,
    parts: Vec<Message>,
    flags: SendFlags,
    _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ReplyOp<S> {}

#[allow(clippy::large_enum_variant)]
enum ReplyOpKind {
    // Spot-layer (handle = spot)
    ToSpot {
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
    },
    ToRouter {
        peer_rid: RoutingId,
        request_seq: u64,
    },
    // Router socket (handle = socket)
    RouterReply {
        rid: RoutingId,
        request_seq: u64,
    },
    RouterReplyToSpot {
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
    },
}

pub(crate) fn router_reply_op(
    handle: *mut c_void,
    rid: RoutingId,
    request_seq: u64,
) -> ReplyOp<Empty> {
    ReplyOp {
        handle,
        kind: ReplyOpKind::RouterReply { rid, request_seq },
        parts: Vec::new(),
        flags: SendFlags::NONE,
        _state: std::marker::PhantomData,
    }
}

pub(crate) fn router_reply_to_spot_op(
    handle: *mut c_void,
    dest_node_rid: RoutingId,
    dest_spot_rid: RoutingId,
    request_seq: u64,
) -> ReplyOp<Empty> {
    ReplyOp {
        handle,
        kind: ReplyOpKind::RouterReplyToSpot {
            dest_node_rid,
            dest_spot_rid,
            request_seq,
        },
        parts: Vec::new(),
        flags: SendFlags::NONE,
        _state: std::marker::PhantomData,
    }
}

pub(crate) fn spot_reply_to_spot_op(
    handle: *mut c_void,
    dest_node_rid: RoutingId,
    dest_spot_rid: RoutingId,
    request_seq: u64,
) -> ReplyOp<Empty> {
    ReplyOp {
        handle,
        kind: ReplyOpKind::ToSpot {
            dest_node_rid,
            dest_spot_rid,
            request_seq,
        },
        parts: Vec::new(),
        flags: SendFlags::NONE,
        _state: std::marker::PhantomData,
    }
}

pub(crate) fn spot_reply_to_router_op(
    handle: *mut c_void,
    peer_rid: RoutingId,
    request_seq: u64,
) -> ReplyOp<Empty> {
    ReplyOp {
        handle,
        kind: ReplyOpKind::ToRouter {
            peer_rid,
            request_seq,
        },
        parts: Vec::new(),
        flags: SendFlags::NONE,
        _state: std::marker::PhantomData,
    }
}

impl ReplyOp<Empty> {
    pub fn message(self, message: Message) -> ReplyOp<Ready> {
        ReplyOp {
            handle: self.handle,
            kind: self.kind,
            parts: vec![message],
            flags: self.flags,
            _state: std::marker::PhantomData,
        }
    }
}

impl ReplyOp<Ready> {
    pub fn message(mut self, message: Message) -> Self {
        self.parts.push(message);
        self
    }

    pub fn flags(mut self, flags: SendFlags) -> Self {
        self.flags = flags;
        self
    }

    /// # Errors: SubmitError
    pub fn submit(mut self) -> Result<(), SubmitError> {
        let flags = self.flags;
        if flags.bits() != 0 {
            return Err(submit_not_supported_error());
        }
        let mut native = prepare_send_parts(&mut self.parts)?;
        let handle = self.handle;
        let rc = match &self.kind {
            ReplyOpKind::ToSpot {
                dest_node_rid,
                dest_spot_rid,
                request_seq,
            } => {
                let nr = dest_node_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let sr = dest_spot_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let seq = *request_seq;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_spot_reply_spot_part(handle, nr, sr, seq, part, part_flag)
                })?
            }
            ReplyOpKind::ToRouter {
                peer_rid,
                request_seq,
            } => {
                let pr = peer_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let seq = *request_seq;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_spot_reply_router_part(handle, pr, seq, part, part_flag)
                })?
            }
            ReplyOpKind::RouterReply { rid, request_seq } => {
                let r = rid.as_raw() as *const ffi::zlink_routing_id_t;
                let seq = *request_seq;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_router_reply_part(handle, r, seq, part, part_flag)
                })?
            }
            ReplyOpKind::RouterReplyToSpot {
                dest_node_rid,
                dest_spot_rid,
                request_seq,
            } => {
                let nr = dest_node_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let sr = dest_spot_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let seq = *request_seq;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_router_reply_spot_part(handle, nr, sr, seq, part, part_flag)
                })?
            }
        };
        drop(self.parts);
        check_submit_rc(rc)
    }
}

// ---------------------------------------------------------------------------
// Actor value structs
// ---------------------------------------------------------------------------

/// Async Actor join completion value.
#[derive(Debug, Clone)]
pub struct ActorJoinResult {
    pub result: crate::error::RequestResult,
    pub actor: ActorRef,
    pub joined_spot_rid: RoutingId,
    pub join_epoch: u64,
    pub flags: u32,
}

/// Async Actor lookup completion value.
#[derive(Debug, Clone)]
pub struct ActorLookupResult {
    pub result: crate::error::RequestResult,
    pub actor: ActorRef,
    pub flags: u32,
}

/// Information passed to Spot Actor lifecycle handlers.
#[derive(Debug, Clone)]
pub struct SpotActorLifecycleInfo {
    pub previous_actor: ActorRef,
    pub current_actor: ActorRef,
    pub previous_spot_rid: Option<RoutingId>,
    pub current_spot_rid: Option<RoutingId>,
    pub join_epoch: u64,
    pub flags: u32,
}

// ---------------------------------------------------------------------------
// Actor operation builders
// ---------------------------------------------------------------------------

/// Async Actor join builder. Payload accumulates via `.message(...)`.
pub struct ActorJoinOp<State> {
    node_handle: *mut c_void,
    spot_handle: *mut c_void,
    actor: ffi::zlink_actor_ref_t,
    dest_node_rid: RoutingId,
    dest_spot_rid: RoutingId,
    parts: Vec<Message>,
    flags: SendFlags,
    timeout: Duration,
    _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorJoinOp<S> {}

impl ActorJoinOp<Empty> {
    pub fn message(self, message: Message) -> ActorJoinOp<Ready> {
        ActorJoinOp {
            node_handle: self.node_handle,
            spot_handle: self.spot_handle,
            actor: self.actor,
            dest_node_rid: self.dest_node_rid,
            dest_spot_rid: self.dest_spot_rid,
            parts: vec![message],
            flags: self.flags,
            timeout: self.timeout,
            _state: std::marker::PhantomData,
        }
    }
}

impl ActorJoinOp<Ready> {
    pub fn message(mut self, message: Message) -> Self {
        self.parts.push(message);
        self
    }

    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.timeout = timeout;
        self
    }

    pub fn flags(mut self, flags: SendFlags) -> Self {
        self.flags = flags;
        self
    }

    /// Submit and await completion (async). Returns `(ActorJoinResult, parts)`.
    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<(ActorJoinResult, Vec<Message>), ZlinkError> {
        let (tx, rx) = mpsc::channel();
        self.submit(move |result, parts| {
            let _ = tx.send((result, parts));
        })
        .map_err(ZlinkError::from)?;
        rx.recv().map_err(|_| {
            ZlinkError::from(RequestError::new(
                crate::error::RequestResult::ProtocolError,
                libc::EINVAL,
            ))
        })
    }

    /// Submit with a completion callback.
    /// # Errors: SubmitError
    pub fn submit<F>(mut self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinResult, Vec<Message>) + Send + 'static,
    {
        let native = prepare_send_parts(&mut self.parts)?;
        let mut native = native;
        let state_ptr = Box::into_raw(Box::new(ActorJoinCallbackState {
            callback: Some(Box::new(callback)),
            _progress: if self.spot_handle.is_null() {
                None
            } else {
                Some(RequestProgressGuard::attach_spot(self.spot_handle))
            },
        }));
        let timeout_ms = timeout_to_timeout_ms(self.timeout);
        let flags_bits = self.flags.bits();
        let rc = unsafe {
            ffi::zlink_spot_node_actor_join_spot(
                self.node_handle,
                &self.actor,
                self.dest_node_rid.as_raw(),
                self.dest_spot_rid.as_raw(),
                native.as_mut_ptr(),
                native.len(),
                Some(actor_join_user_callback),
                state_ptr.cast(),
                flags_bits,
                timeout_ms,
            )
        };
        if rc != 0 {
            for part in native.iter_mut() {
                unsafe {
                    ffi::zlink_msg_close(part);
                }
            }
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
        }
        check_submit_rc(rc)
    }
}

/// Builder for replying to an Actor join admission request. 0-part submit is allowed.
pub struct ActorJoinReplyOp<State> {
    spot_handle: *mut c_void,
    info: ffi::zlink_actor_join_info_t,
    accepted: bool,
    parts: Vec<Message>,
    _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorJoinReplyOp<S> {}

impl ActorJoinReplyOp<Empty> {
    pub fn message(mut self, message: Message) -> ActorJoinReplyOp<Empty> {
        self.parts.push(message);
        self
    }

    /// # Errors: SubmitError
    pub fn submit(mut self) -> Result<(), SubmitError> {
        // 0..N parts allowed.
        let mut native: Vec<ffi::zlink_msg_t> = Vec::with_capacity(self.parts.len());
        unsafe {
            for part in self.parts.iter_mut() {
                let mut dest = MaybeUninit::<ffi::zlink_msg_t>::uninit();
                ffi::zlink_msg_init(dest.as_mut_ptr());
                ffi::zlink_msg_move(dest.as_mut_ptr(), &mut part.inner);
                native.push(dest.assume_init());
            }
        }
        let rc = unsafe {
            ffi::zlink_spot_actor_join_reply(
                self.spot_handle,
                &self.info,
                if self.accepted { 1 } else { 0 },
                if native.is_empty() {
                    std::ptr::null_mut()
                } else {
                    native.as_mut_ptr()
                },
                native.len(),
            )
        };
        if rc != 0 {
            for part in native.iter_mut() {
                unsafe {
                    ffi::zlink_msg_close(part);
                }
            }
        }
        check_submit_rc(rc)
    }
}

/// Payload-less builder shared by leave / destroy / bind / unbind.
struct ActorReplyOpInner {
    handle: *mut c_void,
    kind: ActorReplyOpKind,
    timeout: Duration,
}

enum ActorReplyOpKind {
    Leave {
        actor: ffi::zlink_actor_ref_t,
        current_spot_rid: RoutingId,
    },
    Destroy {
        actor: ffi::zlink_actor_ref_t,
    },
    Bind {
        session_rid: RoutingId,
        actor: ffi::zlink_actor_ref_t,
    },
    Unbind {
        session_rid: RoutingId,
        actor_id: std::ffi::CString,
    },
}

impl ActorReplyOpInner {
    fn submit_callback<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        let state_ptr = Box::into_raw(Box::new(SpotReplyCallbackState {
            callback: Some(Box::new(callback)),
            _progress: None,
        }));
        let timeout_ms = timeout_to_timeout_ms(self.timeout);
        let rc = match &self.kind {
            ActorReplyOpKind::Leave {
                actor,
                current_spot_rid,
            } => unsafe {
                ffi::zlink_spot_node_actor_leave_spot(
                    self.handle,
                    actor,
                    current_spot_rid.as_raw(),
                    spot_reply_callback,
                    state_ptr.cast(),
                    timeout_ms,
                )
            },
            ActorReplyOpKind::Destroy { actor } => unsafe {
                ffi::zlink_spot_node_actor_destroy(
                    self.handle,
                    actor,
                    spot_reply_callback,
                    state_ptr.cast(),
                    timeout_ms,
                )
            },
            ActorReplyOpKind::Bind { session_rid, actor } => unsafe {
                ffi::zlink_stream_bind_actor(
                    self.handle,
                    session_rid.as_raw(),
                    actor,
                    spot_reply_callback,
                    state_ptr.cast(),
                    timeout_ms,
                )
            },
            ActorReplyOpKind::Unbind {
                session_rid,
                actor_id,
            } => unsafe {
                ffi::zlink_stream_unbind_actor(
                    self.handle,
                    session_rid.as_raw(),
                    actor_id.as_ptr(),
                    spot_reply_callback,
                    state_ptr.cast(),
                    timeout_ms,
                )
            },
        };
        if rc != 0 {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
        }
        check_submit_rc(rc)
    }
}

/// Async Actor leave builder (payload-less).
pub struct ActorLeaveOp<State> {
    inner: ActorReplyOpInner,
    _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorLeaveOp<S> {}

impl ActorLeaveOp<Empty> {
    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.inner.timeout = timeout;
        self
    }

    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
        actor_reply_op_submit_async(self.inner).await
    }

    /// # Errors: SubmitError
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        self.inner.submit_callback(callback)
    }
}

/// Async Actor destroy builder (payload-less).
pub struct ActorDestroyOp<State> {
    inner: ActorReplyOpInner,
    _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorDestroyOp<S> {}

impl ActorDestroyOp<Empty> {
    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.inner.timeout = timeout;
        self
    }

    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
        actor_reply_op_submit_async(self.inner).await
    }

    /// # Errors: SubmitError
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        self.inner.submit_callback(callback)
    }
}

/// Async Actor bind builder (payload-less).
pub struct ActorBindOp<State> {
    inner: ActorReplyOpInner,
    _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorBindOp<S> {}

impl ActorBindOp<Empty> {
    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.inner.timeout = timeout;
        self
    }

    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
        actor_reply_op_submit_async(self.inner).await
    }

    /// # Errors: SubmitError
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        self.inner.submit_callback(callback)
    }
}

/// Async Actor unbind builder (payload-less).
pub struct ActorUnbindOp<State> {
    inner: ActorReplyOpInner,
    _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorUnbindOp<S> {}

impl ActorUnbindOp<Empty> {
    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.inner.timeout = timeout;
        self
    }

    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
        actor_reply_op_submit_async(self.inner).await
    }

    /// # Errors: SubmitError
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        self.inner.submit_callback(callback)
    }
}

async fn actor_reply_op_submit_async(inner: ActorReplyOpInner) -> Result<Vec<Message>, ZlinkError> {
    let (tx, rx) = mpsc::channel();
    inner
        .submit_callback(move |result| {
            let _ = tx.send(result);
        })
        .map_err(ZlinkError::from)?;
    rx.recv()
        .unwrap_or_else(|_| {
            Err(RequestError::new(
                crate::error::RequestResult::ProtocolError,
                libc::EINVAL,
            ))
        })
        .map_err(ZlinkError::from)
}

/// Async remote Actor lookup builder (payload-less).
pub struct ActorLookupOp<State> {
    node_handle: *mut c_void,
    target_node_rid: RoutingId,
    actor_id: std::ffi::CString,
    timeout: Duration,
    _state: std::marker::PhantomData<State>,
}

unsafe impl<S> Send for ActorLookupOp<S> {}

impl ActorLookupOp<Empty> {
    pub fn timeout(mut self, timeout: Duration) -> Self {
        self.timeout = timeout;
        self
    }

    /// # Errors: ZlinkError
    pub async fn submit_async(self) -> Result<ActorLookupResult, ZlinkError> {
        let (tx, rx) = mpsc::channel();
        self.submit(move |result| {
            let _ = tx.send(result);
        })
        .map_err(ZlinkError::from)?;
        rx.recv().map_err(|_| {
            ZlinkError::from(RequestError::new(
                crate::error::RequestResult::ProtocolError,
                libc::EINVAL,
            ))
        })
    }

    /// # Errors: SubmitError
    pub fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorLookupResult) + Send + 'static,
    {
        let state_ptr = Box::into_raw(Box::new(ActorLookupCallbackState {
            callback: Some(Box::new(callback)),
        }));
        let rc = unsafe {
            ffi::zlink_remote_actor_get_ref(
                self.node_handle,
                self.target_node_rid.as_raw(),
                self.actor_id.as_ptr(),
                actor_lookup_user_callback,
                state_ptr.cast(),
                timeout_to_timeout_ms(self.timeout),
            )
        };
        if rc != 0 {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
        }
        check_submit_rc(rc)
    }
}

struct ActorJoinCallbackState {
    callback: Option<Box<dyn FnOnce(ActorJoinResult, Vec<Message>) + Send>>,
    _progress: Option<RequestProgressGuard>,
}

struct ActorLookupCallbackState {
    callback: Option<Box<dyn FnOnce(ActorLookupResult) + Send>>,
}

unsafe extern "C" fn actor_join_user_callback(
    result: *const ffi::zlink_actor_join_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let mut state = unsafe { Box::from_raw(userdata.cast::<ActorJoinCallbackState>()) };
    let callback = state.callback.take().expect("actor join callback");
    if result.is_null() {
        let placeholder = ActorJoinResult {
            result: crate::error::RequestResult::InternalError,
            actor: ActorRef {
                node_rid: RoutingId::from_raw(ffi::zlink_routing_id_t {
                    size: 0,
                    data: [0; 255],
                }),
                actor_id: String::new(),
                generation: 0,
            },
            joined_spot_rid: RoutingId::from_raw(ffi::zlink_routing_id_t {
                size: 0,
                data: [0; 255],
            }),
            join_epoch: 0,
            flags: 0,
        };
        callback(placeholder, Vec::new());
        return;
    }
    let raw = unsafe { *result };
    let join_result = ActorJoinResult {
        result: request_result_from_raw(raw.result),
        actor: ActorRef::from_raw(&raw.actor),
        joined_spot_rid: RoutingId::from_raw(raw.joined_spot_rid),
        join_epoch: raw.join_epoch,
        flags: raw.flags,
    };
    let messages = borrowed_parts_to_messages(parts, part_count);
    callback(join_result, messages);
}

unsafe extern "C" fn actor_lookup_user_callback(
    result: *const ffi::zlink_actor_lookup_result_t,
    userdata: *mut c_void,
) {
    let mut state = unsafe { Box::from_raw(userdata.cast::<ActorLookupCallbackState>()) };
    let callback = state.callback.take().expect("actor lookup callback");
    if result.is_null() {
        let placeholder = ActorLookupResult {
            result: crate::error::RequestResult::InternalError,
            actor: ActorRef {
                node_rid: RoutingId::from_raw(ffi::zlink_routing_id_t {
                    size: 0,
                    data: [0; 255],
                }),
                actor_id: String::new(),
                generation: 0,
            },
            flags: 0,
        };
        callback(placeholder);
        return;
    }
    let raw = unsafe { *result };
    let lookup_result = ActorLookupResult {
        result: request_result_from_raw(raw.result),
        actor: ActorRef::from_raw(&raw.actor),
        flags: raw.flags,
    };
    callback(lookup_result);
}

/// Wrapper for actor join receive result (info + message).
pub struct ActorJoinRequest {
    pub info: ActorJoinInfo,
    pub message: Message,
}

// ---------------------------------------------------------------------------
// Spot
// ---------------------------------------------------------------------------

/// Unified SPOT facade over an existing SPOT node.
///
/// The Spot handle borrows a `SpotNode`, lazily creates side sockets, and
/// exposes the canonical data-plane API (publish, subscribe, etc.).
pub struct Spot {
    handle: *mut c_void,
    node_handle: *mut c_void,
    send_ready_cb: Option<CallbackBox>,
    routed_cb: Option<CallbackBox>,
    dispatch_cb: Option<CallbackBox>,
}

unsafe impl Send for Spot {}

impl Spot {
    pub(crate) fn new(node: &SpotNode) -> Result<Self, ConfigError> {
        let handle = unsafe { ffi::zlink_spot_new(node.raw()) };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self {
            handle,
            node_handle: node.raw(),
            send_ready_cb: None,
            routed_cb: None,
            dispatch_cb: None,
        })
    }

    pub fn publish(&self, topic: &str) -> SendOp<Empty> {
        let c_topic = fixed_cstring_or_panic(topic, "topic");
        SendOp {
            handle: self.handle,
            kind: SendOpKind::Publish { topic: c_topic },
            parts: Vec::new(),
            flags: SendFlags::NONE,
            _state: std::marker::PhantomData,
        }
    }

    pub fn send_channel(&self, channel_name: &str) -> SendOp<Empty> {
        let c_channel_name = fixed_cstring_or_panic(channel_name, "channel_name");
        SendOp {
            handle: self.handle,
            kind: SendOpKind::SendChannel {
                channel_name: c_channel_name,
            },
            parts: Vec::new(),
            flags: SendFlags::NONE,
            _state: std::marker::PhantomData,
        }
    }

    pub fn send_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    ) -> SendOp<Empty> {
        SendOp {
            handle: self.handle,
            kind: SendOpKind::SendToSpot {
                dest_node_rid,
                dest_spot_rid,
            },
            parts: Vec::new(),
            flags: SendFlags::NONE,
            _state: std::marker::PhantomData,
        }
    }

    pub fn request_channel(&self, channel_name: &str) -> RequestOp<Empty> {
        let c_channel_name = fixed_cstring_or_panic(channel_name, "channel_name");
        RequestOp {
            handle: self.handle,
            kind: RequestOpKind::Channel {
                channel_name: c_channel_name,
            },
            parts: Vec::new(),
            flags: None,
            timeout: Duration::ZERO,
            _state: std::marker::PhantomData,
        }
    }

    pub fn request_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    ) -> RequestOp<Empty> {
        RequestOp {
            handle: self.handle,
            kind: RequestOpKind::ToSpot {
                dest_node_rid,
                dest_spot_rid,
            },
            parts: Vec::new(),
            flags: None,
            timeout: Duration::ZERO,
            _state: std::marker::PhantomData,
        }
    }

    pub fn request_to_router(&self, peer_rid: RoutingId) -> RequestOp<Empty> {
        RequestOp {
            handle: self.handle,
            kind: RequestOpKind::ToRouter { peer_rid },
            parts: Vec::new(),
            flags: None,
            timeout: Duration::ZERO,
            _state: std::marker::PhantomData,
        }
    }

    pub fn reply_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
    ) -> ReplyOp<Empty> {
        ReplyOp {
            handle: self.handle,
            kind: ReplyOpKind::ToSpot {
                dest_node_rid,
                dest_spot_rid,
                request_seq,
            },
            parts: Vec::new(),
            flags: SendFlags::NONE,
            _state: std::marker::PhantomData,
        }
    }

    pub fn reply_to_router(&self, peer_rid: RoutingId, request_seq: u64) -> ReplyOp<Empty> {
        ReplyOp {
            handle: self.handle,
            kind: ReplyOpKind::ToRouter {
                peer_rid,
                request_seq,
            },
            parts: Vec::new(),
            flags: SendFlags::NONE,
            _state: std::marker::PhantomData,
        }
    }

    pub fn drain_channel_reply_from(
        &self,
        dealer: &crate::socket::DealerSocket,
    ) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_spot_channel_reply_progress_from(self.handle, dealer.inner.handle)
        })
    }

    pub fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_routing_id(self.handle, rid.data().as_ptr() as *const c_void, rid.len())
        })
    }

    pub fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        let mut raw = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        check_config_rc(unsafe { ffi::zlink_get_routing_id(self.handle, raw.as_mut_ptr()) })?;
        Ok(RoutingId::from_raw(unsafe { raw.assume_init() }))
    }

    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        let c = CString::new(filter).map_err(|_| config_validation_error())?;
        check_config_rc(unsafe { ffi::zlink_set_subscription(self.handle, c.as_ptr()) })
    }

    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        let c = CString::new(filter).map_err(|_| config_validation_error())?;
        check_config_rc(unsafe { ffi::zlink_unset_subscription(self.handle, c.as_ptr()) })
    }

    pub fn subscribe(&self, out: &mut TopicMessage, flags: RecvFlags) -> Result<bool, RecvError> {
        let mut topic_buf = [0i8; 256];
        match recv_spot_subscribed_parts(self.handle, &mut topic_buf, flags.bits())? {
            Some((routing_id, topic, parts)) => {
                out.adopt_from(TopicMessage::new(routing_id, topic, parts));
                Ok(true)
            }
            None => Ok(false),
        }
    }

    pub fn receive_subscription_event(
        &self,
        _out: &mut SubscriptionEvent,
        _flags: RecvFlags,
    ) -> Result<bool, RecvError> {
        Err(RecvError::new(
            crate::error::RecvResult::NotSupported,
            libc::ENOTSUP,
        ))
    }

    pub fn recv_actor_join_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<Option<ActorJoinRequest>, RecvError> {
        let mut raw_info = MaybeUninit::<ffi::zlink_actor_join_info_t>::zeroed();
        let mut parts: *mut ffi::zlink_msg_t = ptr::null_mut();
        let mut part_count: usize = 0;
        let rc = unsafe {
            ffi::zlink_spot_actor_join_recv(
                self.handle,
                raw_info.as_mut_ptr(),
                &mut parts,
                &mut part_count,
                flags.bits(),
            )
        };
        if rc != 0 {
            if last_errno() == libc::EAGAIN && flags == RecvFlags::DONT_WAIT {
                return Ok(None);
            }
            return Err(check_recv_rc(rc).unwrap_err());
        }
        let info = ActorJoinInfo::from_raw(unsafe { raw_info.assume_init() });
        let messages = take_parts(parts, part_count);
        unsafe {
            ffi::zlink_multipart_close(parts, part_count);
        }
        let message = messages
            .into_iter()
            .next()
            .unwrap_or_else(|| Message::new().expect("empty zlink message init failed"));
        Ok(Some(ActorJoinRequest { info, message }))
    }

    pub fn recv_actor_join(&self) -> Result<ActorJoinRequest, RecvError> {
        self.recv_actor_join_with_flags(RecvFlags::NONE)?
            .ok_or_else(|| RecvError::new(RecvResult::NoData, libc::EAGAIN))
    }

    /// Reply to an Actor join admission request (operation builder).
    /// Multipart reply payload accumulates via `.message(...)`. A zero-message
    /// `submit()` is allowed.
    pub fn reply_actor_join(
        &self,
        request: &ActorJoinRequest,
        accepted: bool,
    ) -> ActorJoinReplyOp<Empty> {
        ActorJoinReplyOp {
            spot_handle: self.handle,
            info: request.info.raw,
            accepted,
            parts: Vec::new(),
            _state: std::marker::PhantomData,
        }
    }

    pub fn actors_snapshot(&self) -> Result<Vec<ActorRef>, ConfigError> {
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_spot_actors_snapshot(self.handle, ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }
        let mut entries = vec![unsafe { std::mem::zeroed::<ffi::zlink_actor_ref_t>() }; count];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                ffi::zlink_spot_actors_snapshot(self.handle, entries_ptr, count_ptr)
            },
            entries.as_mut_ptr(),
        )?;
        Ok(entries[..actual].iter().map(ActorRef::from_raw).collect())
    }

    pub fn on_routed_receive<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn(Received) + Send + 'static,
    {
        let (cb, userdata) = CallbackBox::new((self.handle, handler));
        let rc =
            unsafe { ffi::zlink_spot_handler(self.handle, spot_handler_trampoline::<F>, userdata) };
        if rc != 0 {
            drop(cb);
            return Err(check_handler_rc(rc).unwrap_err());
        }
        self.routed_cb = Some(cb);
        Ok(())
    }

    pub fn on_dispatch_event<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: for<'a> Fn(SpotDispatchInfo<'a>) + Send + 'static,
    {
        let (cb, userdata) = CallbackBox::new((self.node_handle, handler));
        let rc = unsafe {
            ffi::zlink_spot_dispatch_event_handler(
                self.handle,
                spot_dispatch_trampoline::<F>,
                userdata,
            )
        };
        if rc != 0 {
            drop(cb);
            return Err(check_handler_rc(rc).unwrap_err());
        }
        self.dispatch_cb = Some(cb);
        Ok(())
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        let (cb, userdata) = CallbackBox::new(handler);
        let rc = unsafe {
            ffi::zlink_send_ready_handler(self.handle, send_ready_trampoline::<F>, userdata)
        };
        if rc != 0 {
            drop(cb);
            return Err(check_handler_rc(rc).unwrap_err());
        }
        self.send_ready_cb = Some(cb);
        Ok(())
    }

    /// Receive a routed message, blocking until one is available.
    pub fn recv_routed(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        match recv_spot_routed_parts(self.handle, flags.bits())? {
            Some((node_rid, spot_rid, req_seq, parts)) => {
                out.adopt_from(spot_received_from_raw(
                    self.handle,
                    node_rid,
                    spot_rid,
                    req_seq,
                    parts,
                ));
                Ok(true)
            }
            None => Ok(false),
        }
    }

    #[allow(dead_code)]
    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        destroy_handle_close(&mut self.handle, ffi::zlink_spot_destroy)
    }
}

type SpotReplyCallback = Box<dyn FnOnce(Result<Vec<Message>, RequestError>) + Send>;
type SpotSubscribedParts = Result<Option<(Option<RoutingId>, String, Vec<Message>)>, RecvError>;

struct SpotReplyCallbackState {
    callback: Option<SpotReplyCallback>,
    _progress: Option<RequestProgressGuard>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotDispatchEvent {
    SubscribeReadable,
    RoutedReadable,
    TimerReadable,
    ChannelReplyReadable,
    ActorReadable,
    ActorJoinReadable,
}

/// Subject variant for a SPOT dispatch event.
pub enum SpotDispatchSubject<'a> {
    Spot,
    Timer(&'a crate::poller::Timer),
    ChannelDealer(&'a crate::socket::DealerSocket),
    Actor(ActorRef),
}

/// Information passed to `on_dispatch_event` callbacks.
pub struct SpotDispatchInfo<'a> {
    pub event: SpotDispatchEvent,
    pub subject: SpotDispatchSubject<'a>,
    // Internal fields for actor receive
    node_handle: *mut c_void,
    actor_for_recv: Option<ActorRef>,
}

unsafe impl<'a> Send for SpotDispatchInfo<'a> {}

impl<'a> SpotDispatchInfo<'a> {
    pub fn recv_actor_part_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<Option<ActorPart>, RecvError> {
        if self.event != SpotDispatchEvent::ActorReadable {
            return Err(RecvError::new(RecvResult::NotSupported, libc::ENOTSUP));
        }
        let actor = self
            .actor_for_recv
            .as_ref()
            .ok_or_else(|| RecvError::new(RecvResult::InvalidHandle, libc::EFAULT))?;
        recv_actor_part_from_ref(self.node_handle, actor, flags)
    }
}

// Keep SpotDispatchSubjectKind as a deprecated alias for compatibility, but
// the canonical public API uses SpotDispatchSubject<'a>.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[doc(hidden)]
pub enum SpotDispatchSubjectKind {
    Spot,
    Timer,
    ChannelDealer,
    Actor,
}

impl SpotDispatchSubjectKind {
    fn from_raw(raw: ffi::zlink_spot_dispatch_subject_kind_t) -> Self {
        match raw {
            ffi::zlink_spot_dispatch_subject_kind_t::ZLINK_SPOT_DISPATCH_SUBJECT_SPOT => Self::Spot,
            ffi::zlink_spot_dispatch_subject_kind_t::ZLINK_SPOT_DISPATCH_SUBJECT_TIMER => {
                Self::Timer
            }
            ffi::zlink_spot_dispatch_subject_kind_t::ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER => {
                Self::ChannelDealer
            }
            ffi::zlink_spot_dispatch_subject_kind_t::ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR => {
                Self::Actor
            }
        }
    }
}

impl SpotDispatchEvent {
    fn from_raw(raw: ffi::zlink_spot_dispatch_event_t) -> Self {
        match raw {
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE => {
                Self::SubscribeReadable
            }
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE => {
                Self::RoutedReadable
            }
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE => {
                Self::TimerReadable
            }
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE => {
                Self::ChannelReplyReadable
            }
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE => {
                Self::ActorReadable
            }
            ffi::zlink_spot_dispatch_event_t::ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE => {
                Self::ActorJoinReadable
            }
        }
    }
}

fn timeout_to_timeout_ms(timeout: Duration) -> u32 {
    let millis = timeout.as_millis();
    if millis == 0 {
        0
    } else {
        millis.min(u32::MAX as u128) as u32
    }
}

pub(crate) fn request_result_from_raw(
    raw: ffi::zlink_request_result_t,
) -> crate::error::RequestResult {
    match raw {
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK => crate::error::RequestResult::Ok,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_TIMED_OUT => {
            crate::error::RequestResult::TimedOut
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_NOT_FOUND => {
            crate::error::RequestResult::NotFound
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_TERMINATED => {
            crate::error::RequestResult::Terminated
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_PROTOCOL_ERROR => {
            crate::error::RequestResult::ProtocolError
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_INTERNAL_ERROR => {
            crate::error::RequestResult::InternalError
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_REJECTED => {
            crate::error::RequestResult::Rejected
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_CONFLICT => {
            crate::error::RequestResult::Conflict
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_BUSY => crate::error::RequestResult::Busy,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_NOT_CONNECTED => {
            crate::error::RequestResult::NotConnected
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_INVALID_ARGUMENT => {
            crate::error::RequestResult::InvalidArgument
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_INVALID_STATE => {
            crate::error::RequestResult::InvalidState
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_NOT_SUPPORTED => {
            crate::error::RequestResult::NotSupported
        }
    }
}

pub(crate) fn check_request_result(raw: ffi::zlink_request_result_t) -> Result<(), RequestError> {
    let code = request_result_from_raw(raw);
    if code == crate::error::RequestResult::Ok {
        Ok(())
    } else {
        Err(request_error_from_result(code))
    }
}

pub(crate) fn wait_reply_submit<F>(submit: F) -> Result<(), RequestError>
where
    F: FnOnce(ffi::zlink_reply_handler_fn, *mut c_void) -> ffi::zlink_submit_result_t,
{
    let (tx, rx) = mpsc::channel();
    let state = Box::into_raw(Box::new(tx));
    let rc = submit(reply_result_callback, state.cast());
    if rc != 0 {
        unsafe {
            drop(Box::from_raw(state));
        }
        return Err(request_error_from_submit(check_submit_rc(rc).unwrap_err()));
    }
    let result = rx
        .recv()
        .map_err(|_| request_error_from_result(crate::error::RequestResult::InternalError))?;
    check_request_result(result)
}

fn recv_error_from_config(err: ConfigError) -> RecvError {
    RecvError::new(RecvResult::InvalidHandle, err.internal_errno())
}

fn recv_actor_part_from_ref(
    node: *mut c_void,
    actor: &ActorRef,
    flags: RecvFlags,
) -> Result<Option<ActorPart>, RecvError> {
    let raw_actor = actor.to_raw().map_err(recv_error_from_config)?;
    let mut raw_info = MaybeUninit::<ffi::zlink_actor_recv_info_t>::zeroed();
    let mut raw_part = MaybeUninit::<ffi::zlink_msg_t>::uninit();
    let mut has_more = ffi::zlink_part_flag_t::ZLINK_PART_FINAL;
    let rc = unsafe {
        ffi::zlink_spot_node_actor_recv_part(
            node,
            &raw_actor,
            raw_info.as_mut_ptr(),
            raw_part.as_mut_ptr(),
            &mut has_more,
            flags.bits(),
        )
    };
    if rc != 0 {
        if last_errno() == libc::EAGAIN && flags == RecvFlags::DONT_WAIT {
            return Ok(None);
        }
        return Err(check_recv_rc(rc).unwrap_err());
    }
    let info = ActorRecvInfo::from_raw(&unsafe { raw_info.assume_init() });
    let message = unsafe { Message::from_raw(raw_part.assume_init()) };
    Ok(Some(ActorPart::new(
        info,
        message,
        has_more == ffi::zlink_part_flag_t::ZLINK_PART_MORE,
    )))
}

fn take_message_raw(message: &mut Message) -> ffi::zlink_msg_t {
    unsafe {
        let mut native = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        ffi::zlink_msg_init(native.as_mut_ptr());
        ffi::zlink_msg_move(native.as_mut_ptr(), &mut message.inner);
        native.assume_init()
    }
}

fn borrowed_parts_to_messages(parts: *mut ffi::zlink_msg_t, part_count: usize) -> Vec<Message> {
    let mut out = Vec::with_capacity(part_count);
    for i in 0..part_count {
        unsafe {
            let mut dest = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            ffi::zlink_msg_init(dest.as_mut_ptr());
            ffi::zlink_msg_move(dest.as_mut_ptr(), parts.add(i));
            out.push(Message::from_raw(dest.assume_init()));
        }
    }
    out
}

type SpotRoutedParts = Result<
    Option<(
        *const ffi::zlink_routing_id_t,
        *const ffi::zlink_routing_id_t,
        u64,
        Vec<Message>,
    )>,
    RecvError,
>;

fn recv_spot_routed_parts(handle: *mut c_void, flags: ffi::zlink_recv_flags_t) -> SpotRoutedParts {
    let mut source_node_rid_ptr: *const ffi::zlink_routing_id_t = ptr::null();
    let mut source_spot_rid_ptr: *const ffi::zlink_routing_id_t = ptr::null();
    let mut request_seq: u64 = 0;
    let mut parts = Vec::new();
    let mut recv_flags = flags;

    loop {
        let mut part = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(part.as_mut_ptr());
        }
        let mut has_more = 0;
        let rc = unsafe {
            ffi::zlink_spot_recv_part(
                handle,
                &mut source_node_rid_ptr,
                &mut source_spot_rid_ptr,
                &mut request_seq,
                part.as_mut_ptr(),
                &mut has_more,
                recv_flags,
            )
        };

        if parts.is_empty() {
            if rc == RecvResult::NoData as i32 {
                close_unreceived_part(&mut part);
                return Ok(None);
            }
            if rc != 0 {
                close_unreceived_part(&mut part);
                let errno = unsafe { ffi::zlink_errno() };
                if errno == libc::EAGAIN {
                    return Ok(None);
                }
                return Err(check_recv_rc(rc).unwrap_err());
            }
        } else if rc != 0 {
            close_unreceived_part(&mut part);
            return Err(check_recv_rc(rc).unwrap_err());
        }

        parts.push(unsafe { Message::from_raw(part.assume_init()) });
        if has_more == 0 {
            return Ok(Some((
                source_node_rid_ptr,
                source_spot_rid_ptr,
                request_seq,
                parts,
            )));
        }
        recv_flags = ffi::ZLINK_DONTWAIT;
    }
}

fn recv_spot_subscribed_parts(
    handle: *mut c_void,
    topic_buf: &mut [i8; 256],
    flags: ffi::zlink_recv_flags_t,
) -> SpotSubscribedParts {
    let mut routing_id = None;
    let mut topic = String::new();
    let mut parts = Vec::new();
    let mut recv_flags = flags;

    loop {
        let mut source_rid_ptr = ptr::null();
        let mut topic_len = topic_buf.len();
        let mut part = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(part.as_mut_ptr());
        }
        let mut has_more = 0;
        let rc = unsafe {
            ffi::zlink_spot_subscribe_part(
                handle,
                &mut source_rid_ptr,
                topic_buf.as_mut_ptr(),
                topic_buf.len(),
                &mut topic_len,
                part.as_mut_ptr(),
                &mut has_more,
                recv_flags,
            )
        };

        if parts.is_empty() {
            if rc == RecvResult::NoData as i32 {
                close_unreceived_part(&mut part);
                return Ok(None);
            }
            if rc != 0 {
                close_unreceived_part(&mut part);
                let errno = unsafe { ffi::zlink_errno() };
                if errno == libc::EAGAIN {
                    return Ok(None);
                }
                return Err(check_recv_rc(rc).unwrap_err());
            }
            routing_id = routing_id_from_ptr(source_rid_ptr);
            topic = cstr_buf_to_string(topic_buf, topic_len);
        } else if rc != 0 {
            close_unreceived_part(&mut part);
            return Err(check_recv_rc(rc).unwrap_err());
        }

        parts.push(unsafe { Message::from_raw(part.assume_init()) });
        if has_more == 0 {
            return Ok(Some((routing_id, topic, parts)));
        }
        recv_flags = ffi::ZLINK_DONTWAIT;
    }
}

fn spot_received_from_raw(
    handle: *mut c_void,
    source_node_rid: *const ffi::zlink_routing_id_t,
    source_spot_rid: *const ffi::zlink_routing_id_t,
    request_seq: u64,
    parts: Vec<Message>,
) -> Received {
    let node_rid = if source_node_rid.is_null() {
        RoutingId::from_raw(ffi::zlink_routing_id_t {
            size: 0,
            data: [0; 255],
        })
    } else {
        unsafe { RoutingId::from_raw(*source_node_rid) }
    };
    let spot_rid = if source_spot_rid.is_null() {
        None
    } else {
        let rid = unsafe { RoutingId::from_raw(*source_spot_rid) };
        if rid.is_empty() { None } else { Some(rid) }
    };
    if let Some(spot_rid) = spot_rid {
        if request_seq == 0 {
            Received::with_spot_send_context(handle, node_rid, spot_rid, parts)
        } else {
            Received::with_spot_reply_and_send_context(
                handle,
                node_rid,
                spot_rid,
                request_seq,
                parts,
            )
        }
    } else if request_seq == 0 {
        Received::new(Some(node_rid), parts)
    } else {
        Received::with_spot_router_reply_context(handle, node_rid, request_seq, parts)
    }
}

unsafe extern "C" fn spot_reply_callback(
    result_: ffi::zlink_request_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let mut state = unsafe { Box::from_raw(userdata.cast::<SpotReplyCallbackState>()) };
    let callback = state.callback.take().expect("spot request callback");
    if result_ == ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK {
        callback(Ok(borrowed_parts_to_messages(parts, part_count)));
    } else {
        callback(Err(crate::error::request_error_from_result(
            request_result_from_raw(result_),
        )));
    }
}

unsafe extern "C" fn actor_join_callback(
    result: *const ffi::zlink_actor_join_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let result_code = if result.is_null() {
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_INTERNAL_ERROR
    } else {
        unsafe { (*result).result }
    };
    unsafe {
        spot_reply_callback(result_code, parts, part_count, userdata);
    }
}

unsafe extern "C" fn reply_result_callback(
    result_: ffi::zlink_request_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let sender =
        unsafe { Box::from_raw(userdata.cast::<mpsc::Sender<ffi::zlink_request_result_t>>()) };
    let _ = sender.send(result_);
    if !parts.is_null() {
        for i in 0..part_count {
            unsafe {
                ffi::zlink_msg_close(parts.add(i));
            }
        }
    }
}

unsafe extern "C" fn spot_handler_trampoline<F: Fn(Received) + Send + 'static>(
    source_rid: *const ffi::zlink_routing_id_t,
    spot_rid: *const ffi::zlink_routing_id_t,
    request_seq: u64,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let state = unsafe { &*(userdata as *const (*mut c_void, F)) };
    let spot_handle = state.0;
    let handler = &state.1;
    let received = spot_received_from_raw(
        spot_handle,
        source_rid,
        spot_rid,
        request_seq,
        take_parts(parts, part_count),
    );
    handler(received);
}

unsafe extern "C" fn spot_dispatch_trampoline<
    F: for<'a> Fn(SpotDispatchInfo<'a>) + Send + 'static,
>(
    _spot: *mut c_void,
    info: *const ffi::zlink_spot_dispatch_info_t,
    userdata: *mut c_void,
) {
    let state = unsafe { &*(userdata as *const (*mut c_void, F)) };
    let node_handle = state.0;
    let handler = &state.1;
    let info = unsafe { &*info };
    let event = SpotDispatchEvent::from_raw(info.event);
    let subject_kind = SpotDispatchSubjectKind::from_raw(info.subject_kind);

    // Build the typed subject. For actor events, extract ActorRef from the subject pointer.
    // For timer/dealer, the subject pointer is callback-scoped only, so we build a
    // reference that borrows the pointer lifetime bounded to the callback duration.
    // We use a local sentinel struct to carry the raw pointer, but the public API
    // exposes `SpotDispatchSubject<'a>` with `'a` tied to the callback invocation.
    let actor_for_recv;
    let subject = match subject_kind {
        SpotDispatchSubjectKind::Actor if !info.subject.is_null() => {
            let actor_ref =
                ActorRef::from_raw(unsafe { &*(info.subject as *const ffi::zlink_actor_ref_t) });
            actor_for_recv = Some(actor_ref.clone());
            SpotDispatchSubject::Actor(actor_ref)
        }
        SpotDispatchSubjectKind::Timer => {
            actor_for_recv = None;
            // Timer reference is callback-scoped; create a raw-pointer-only facade.
            // The public Timer type doesn't allow construction from a raw handle directly,
            // so we use a placeholder Spot subject for now when no Timer handle is present.
            SpotDispatchSubject::Spot
        }
        SpotDispatchSubjectKind::ChannelDealer => {
            actor_for_recv = None;
            SpotDispatchSubject::Spot
        }
        _ => {
            actor_for_recv = None;
            SpotDispatchSubject::Spot
        }
    };
    handler(SpotDispatchInfo {
        event,
        subject,
        node_handle,
        actor_for_recv,
    });
}

/// Read-only registry query client for remote topology snapshots.
pub struct RegistryQueryClient {
    handle: *mut c_void,
}

unsafe impl Send for RegistryQueryClient {}

impl RegistryQueryClient {
    pub fn new(ctx: &crate::ctx::Context) -> Result<Self, ConfigError> {
        let handle = unsafe { ffi::zlink_registry_query_client_new(ctx.raw()) };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self { handle })
    }

    pub fn connect(&self, endpoint: &str) -> Result<(), ConnectError> {
        let c = CString::new(endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe {
            ffi::zlink_registry_query_client_connect(self.handle, c.as_ptr())
        })
    }

    pub fn snapshot(
        &self,
        filter: Option<&RegistryTopologyFilter>,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        self.snapshot_query_opt(filter)
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        destroy_handle_close(&mut self.handle, ffi::zlink_registry_query_destroy)
    }
}

impl Drop for RegistryQueryClient {
    fn drop(&mut self) {
        let _ = destroy_handle(&mut self.handle, ffi::zlink_registry_query_destroy);
    }
}

impl Registry {
    pub fn status_snapshot(&self) -> Result<RegistryStatus, ConfigError> {
        let mut raw = MaybeUninit::<ffi::zlink_registry_status_t>::uninit();
        check_config_rc(unsafe {
            ffi::zlink_registry_status_snapshot(self.handle, raw.as_mut_ptr())
        })?;
        let raw = unsafe { raw.assume_init() };
        Ok(RegistryStatus::from_raw(&raw))
    }

    pub fn service_summary_snapshot(
        &self,
    ) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
        self.service_summary_query_opt(None)
    }

    pub fn service_summary_query(
        &self,
        filter: &RegistryServiceSummaryFilter,
    ) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
        self.service_summary_query_opt(Some(filter))
    }

    pub fn member_peers(&self, channel_name: &str) -> Result<Vec<MemberPeerEntry>, ConfigError> {
        let c_channel_name = fixed_cstring_config(channel_name, "channel_name")?;
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_registry_member_peers(
                self.handle,
                c_channel_name.as_ptr(),
                ptr::null_mut(),
                count,
            )
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }

        let mut entries =
            vec![unsafe { std::mem::zeroed::<ffi::zlink_member_peer_entry_t>() }; count];
        let actual = read_entries_config(
            count,
            |entries_ptr, count_ptr| unsafe {
                ffi::zlink_registry_member_peers(
                    self.handle,
                    c_channel_name.as_ptr(),
                    entries_ptr,
                    count_ptr,
                )
            },
            entries.as_mut_ptr(),
        )?;
        entries[..actual]
            .iter()
            .map(MemberPeerEntry::from_raw)
            .collect()
    }

    pub fn topology_snapshot(&self) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        self.topology_query_opt(None)
    }

    pub fn topology_query(
        &self,
        filter: &RegistryTopologyFilter,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        self.topology_query_opt(Some(filter))
    }
}

fn fixed_cstr_to_string(buf: &[c_char]) -> String {
    unsafe {
        let ptr = buf.as_ptr();
        if *ptr == 0 {
            String::new()
        } else {
            CStr::from_ptr(ptr).to_string_lossy().into_owned()
        }
    }
}

fn fixed_cstring_config(value: &str, _label: &str) -> Result<CString, ConfigError> {
    if value.len() > 255 {
        return Err(config_validation_error());
    }
    CString::new(value).map_err(|_| config_validation_error())
}

pub(crate) fn fixed_cstring_or_panic(value: &str, label: &str) -> CString {
    fixed_cstring_config(value, label).unwrap_or_else(|_| panic!("invalid {label}"))
}

fn set_tls_server_config(
    handle: *mut c_void,
    cert_pem: &str,
    key_pem: &str,
    require_client_cert: bool,
) -> Result<(), ConfigError> {
    let cert = fixed_cstring_config(cert_pem, "cert_pem")?;
    let key = fixed_cstring_config(key_pem, "key_pem")?;
    check_config_rc(unsafe {
        ffi::zlink_set_tls_server(
            handle,
            cert.as_ptr(),
            key.as_ptr(),
            if require_client_cert { 1 } else { 0 },
        )
    })
}

fn set_tls_client_config(
    handle: *mut c_void,
    ca_cert_pem: &str,
    hostname: &str,
    trust_system: bool,
) -> Result<(), ConfigError> {
    let ca = fixed_cstring_config(ca_cert_pem, "ca_cert_pem")?;
    let host = fixed_cstring_config(hostname, "hostname")?;
    check_config_rc(unsafe {
        ffi::zlink_set_tls_client(
            handle,
            ca.as_ptr(),
            host.as_ptr(),
            if trust_system { 1 } else { 0 },
        )
    })
}

fn destroy_handle(
    handle: &mut *mut c_void,
    destroy: unsafe extern "C" fn(*mut *mut c_void) -> i32,
) -> Result<(), ZlinkError> {
    if handle.is_null() {
        return Ok(());
    }
    let mut h = *handle;
    check_rc(unsafe { destroy(&mut h) })?;
    *handle = ptr::null_mut();
    Ok(())
}

fn destroy_handle_close(
    handle: &mut *mut c_void,
    destroy: unsafe extern "C" fn(*mut *mut c_void) -> i32,
) -> Result<(), CloseError> {
    if handle.is_null() {
        return Ok(());
    }
    let mut h = *handle;
    check_close_rc(unsafe { destroy(&mut h) })?;
    *handle = ptr::null_mut();
    Ok(())
}

fn write_c_array_config(buf: &mut [c_char], value: &str, _label: &str) -> Result<(), ConfigError> {
    if value.len() >= buf.len() {
        return Err(config_validation_error());
    }
    for b in buf.iter_mut() {
        *b = 0;
    }
    for (idx, byte) in value.as_bytes().iter().enumerate() {
        if *byte == 0 {
            return Err(config_validation_error());
        }
        buf[idx] = *byte as c_char;
    }
    Ok(())
}

fn count_entries_config(f: impl FnOnce(*mut usize) -> i32) -> Result<usize, ConfigError> {
    let mut count: usize = 0;
    check_config_rc(f(&mut count))?;
    Ok(count)
}

fn read_entries_config<T>(
    capacity: usize,
    f: impl FnOnce(*mut T, *mut usize) -> i32,
    entries_ptr: *mut T,
) -> Result<usize, ConfigError> {
    let mut count = capacity;
    check_config_rc(f(entries_ptr, &mut count))?;
    Ok(std::cmp::min(capacity, count))
}

fn with_spot_node_peer_filter_config<T>(
    filter: &SpotNodePeerFilter,
    f: impl FnOnce(*const ffi::zlink_spot_node_peer_filter_t) -> Result<T, ConfigError>,
) -> Result<T, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_spot_node_peer_filter_t>::zeroed();
    let ptr = raw.as_mut_ptr();
    unsafe {
        if let Some(peer_endpoint) = &filter.peer_endpoint {
            write_c_array_config(&mut (*ptr).peer_endpoint, peer_endpoint, "peer_endpoint")?;
        }
        if let Some(source) = filter.source {
            (*ptr).source = source.to_raw();
        }
        if let Some(state) = filter.state {
            (*ptr).state = state.to_raw();
        }
    }
    f(ptr)
}

fn with_spot_node_subject_filter_config<T>(
    filter: &SpotNodeSubjectFilter,
    f: impl FnOnce(*const ffi::zlink_spot_node_subject_filter_t) -> Result<T, ConfigError>,
) -> Result<T, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_spot_node_subject_filter_t>::zeroed();
    let ptr = raw.as_mut_ptr();
    unsafe {
        if let Some(role) = filter.role {
            (*ptr).role = role.to_raw();
        }
        if let Some(subject) = &filter.subject {
            write_c_array_config(&mut (*ptr).subject, subject, "subject")?;
        }
        if let Some(subject_kind) = filter.subject_kind {
            (*ptr).subject_kind = subject_kind as u32;
        }
    }
    f(ptr)
}

fn with_spot_node_socket_snapshot_filter_config<T>(
    filter: &SpotNodeSocketSnapshotFilter,
    f: impl FnOnce(*const ffi::zlink_spot_node_socket_snapshot_filter_t) -> Result<T, ConfigError>,
) -> Result<T, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_spot_node_socket_snapshot_filter_t>::zeroed();
    let ptr = raw.as_mut_ptr();
    unsafe {
        (*ptr).owner = filter.owner.unwrap_or(SpotNodeSocketOwner::Any).to_raw();
        if let Some(socket_type) = filter.socket_type {
            (*ptr).socket_type = socket_type.to_raw();
        }
        if let Some(socket_name) = &filter.socket_name {
            write_c_array_config(&mut (*ptr).socket_name, socket_name, "socket_name")?;
        }
    }
    f(ptr)
}

fn with_registry_service_summary_filter_config<T>(
    filter: &RegistryServiceSummaryFilter,
    f: impl FnOnce(*const ffi::zlink_registry_service_summary_filter_t) -> Result<T, ConfigError>,
) -> Result<T, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_registry_service_summary_filter_t>::zeroed();
    let ptr = raw.as_mut_ptr();
    unsafe {
        if let Some(auto_connect_type) = filter.auto_connect_type {
            (*ptr).auto_connect_type = auto_connect_type.to_raw();
        }
        if let Some(service_role) = filter.service_role {
            (*ptr).service_role = service_role.to_raw();
        }
        if let Some(channel_name) = &filter.channel_name {
            write_c_array_config(&mut (*ptr).channel_name, channel_name, "channel_name")?;
        }
    }
    f(ptr)
}

fn with_registry_topology_filter_config<T>(
    filter: &RegistryTopologyFilter,
    f: impl FnOnce(*const ffi::zlink_registry_topology_filter_t) -> Result<T, ConfigError>,
) -> Result<T, ConfigError> {
    let mut raw = MaybeUninit::<ffi::zlink_registry_topology_filter_t>::zeroed();
    let ptr = raw.as_mut_ptr();
    unsafe {
        if let Some(auto_connect_type) = filter.auto_connect_type {
            (*ptr).auto_connect_type = auto_connect_type.to_raw();
        }
        if let Some(service_kind) = filter.service_kind {
            (*ptr).service_kind = service_kind.to_raw();
        }
        if let Some(service_role) = filter.service_role {
            (*ptr).service_role = service_role.to_raw();
        }
        if let Some(channel_name) = &filter.channel_name {
            write_c_array_config(&mut (*ptr).channel_name, channel_name, "channel_name")?;
        }
        if let Some(routing_id) = &filter.routing_id {
            (*ptr).routing_id = *routing_id.as_raw();
        }
        if let Some(state) = filter.state {
            (*ptr).state = state.to_raw();
        }
        if let Some(source) = filter.source {
            (*ptr).source = source.to_raw();
        }
    }
    f(ptr)
}

impl SpotNode {
    fn subjects_query_opt(
        &self,
        filter: Option<&SpotNodeSubjectFilter>,
    ) -> Result<Vec<SpotNodeSubjectEntry>, ConfigError> {
        let read = |filter_ptr: *const ffi::zlink_spot_node_subject_filter_t| {
            let count = count_entries_config(|count| unsafe {
                ffi::zlink_spot_node_subjects_snapshot(
                    self.handle,
                    filter_ptr,
                    ptr::null_mut(),
                    count,
                )
            })?;
            if count == 0 {
                return Ok(Vec::new());
            }

            let mut entries =
                vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_subject_entry_t>() }; count];
            let actual = read_entries_config(
                count,
                |entries_ptr, count_ptr| unsafe {
                    ffi::zlink_spot_node_subjects_snapshot(
                        self.handle,
                        filter_ptr,
                        entries_ptr,
                        count_ptr,
                    )
                },
                entries.as_mut_ptr(),
            )?;
            Ok(entries[..actual]
                .iter()
                .map(SpotNodeSubjectEntry::from_raw)
                .collect())
        };

        match filter {
            Some(filter) => with_spot_node_subject_filter_config(filter, read),
            None => read(ptr::null()),
        }
    }

    fn internal_sockets_snapshot_opt(
        &self,
        filter: Option<&SpotNodeSocketSnapshotFilter>,
    ) -> Result<Vec<SpotNodeSocketSnapshotEntry>, ConfigError> {
        let read = |filter_ptr: *const ffi::zlink_spot_node_socket_snapshot_filter_t| {
            let count = count_entries_config(|count| unsafe {
                ffi::zlink_spot_node_internal_sockets_snapshot(
                    self.handle,
                    filter_ptr,
                    ptr::null_mut(),
                    count,
                )
            })?;
            if count == 0 {
                return Ok(Vec::new());
            }

            let mut entries =
                vec![
                    unsafe { std::mem::zeroed::<ffi::zlink_spot_node_socket_snapshot_entry_t>() };
                    count
                ];
            let actual = read_entries_config(
                count,
                |entries_ptr, count_ptr| unsafe {
                    ffi::zlink_spot_node_internal_sockets_snapshot(
                        self.handle,
                        filter_ptr,
                        entries_ptr,
                        count_ptr,
                    )
                },
                entries.as_mut_ptr(),
            )?;
            Ok(entries[..actual]
                .iter()
                .map(SpotNodeSocketSnapshotEntry::from_raw)
                .collect())
        };

        match filter {
            Some(filter) => with_spot_node_socket_snapshot_filter_config(filter, read),
            None => read(ptr::null()),
        }
    }
}

impl Registry {
    fn service_summary_query_opt(
        &self,
        filter: Option<&RegistryServiceSummaryFilter>,
    ) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
        let read = |filter_ptr: *const ffi::zlink_registry_service_summary_filter_t| {
            let count = count_entries_config(|count| unsafe {
                ffi::zlink_registry_service_summary_snapshot(
                    self.handle,
                    filter_ptr,
                    ptr::null_mut(),
                    count,
                )
            })?;
            if count == 0 {
                return Ok(Vec::new());
            }

            let mut entries =
                vec![
                    unsafe { std::mem::zeroed::<ffi::zlink_registry_service_summary_entry_t>() };
                    count
                ];
            let actual = read_entries_config(
                count,
                |entries_ptr, count_ptr| unsafe {
                    ffi::zlink_registry_service_summary_snapshot(
                        self.handle,
                        filter_ptr,
                        entries_ptr,
                        count_ptr,
                    )
                },
                entries.as_mut_ptr(),
            )?;
            Ok(entries[..actual]
                .iter()
                .map(RegistryServiceSummaryEntry::from_raw)
                .collect())
        };

        match filter {
            Some(filter) => with_registry_service_summary_filter_config(filter, read),
            None => read(ptr::null()),
        }
    }

    fn topology_query_opt(
        &self,
        filter: Option<&RegistryTopologyFilter>,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        let read = |filter_ptr: *const ffi::zlink_registry_topology_filter_t| {
            let count = count_entries_config(|count| unsafe {
                if filter_ptr.is_null() {
                    ffi::zlink_registry_topology_snapshot(self.handle, ptr::null_mut(), count)
                } else {
                    ffi::zlink_registry_topology_query(
                        self.handle,
                        filter_ptr,
                        ptr::null_mut(),
                        count,
                    )
                }
            })?;
            if count == 0 {
                return Ok(Vec::new());
            }

            let mut entries =
                vec![MaybeUninit::<ffi::zlink_registry_topology_entry_t>::uninit(); count];
            let actual = read_entries_config(
                count,
                |entries_ptr, count_ptr| unsafe {
                    if filter_ptr.is_null() {
                        ffi::zlink_registry_topology_snapshot(
                            self.handle,
                            entries_ptr.cast(),
                            count_ptr,
                        )
                    } else {
                        ffi::zlink_registry_topology_query(
                            self.handle,
                            filter_ptr,
                            entries_ptr.cast(),
                            count_ptr,
                        )
                    }
                },
                entries.as_mut_ptr(),
            )?;
            Ok(entries[..actual]
                .iter()
                .map(|entry| RegistryTopologyEntry::from_raw(unsafe { entry.assume_init_ref() }))
                .collect())
        };

        match filter {
            Some(filter) => with_registry_topology_filter_config(filter, read),
            None => read(ptr::null()),
        }
    }
}

impl RegistryQueryClient {
    fn snapshot_query_opt(
        &self,
        filter: Option<&RegistryTopologyFilter>,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        let read = |filter_ptr| {
            let count = count_entries_config(|count| unsafe {
                ffi::zlink_registry_query_snapshot(self.handle, filter_ptr, ptr::null_mut(), count)
            })?;
            if count == 0 {
                return Ok(Vec::new());
            }

            let mut entries =
                vec![MaybeUninit::<ffi::zlink_registry_topology_entry_t>::uninit(); count];
            let actual = read_entries_config(
                count,
                |entries_ptr, count_ptr| unsafe {
                    ffi::zlink_registry_query_snapshot(
                        self.handle,
                        filter_ptr,
                        entries_ptr.cast(),
                        count_ptr,
                    )
                },
                entries.as_mut_ptr(),
            )?;
            Ok(entries[..actual]
                .iter()
                .map(|entry| RegistryTopologyEntry::from_raw(unsafe { entry.assume_init_ref() }))
                .collect())
        };

        match filter {
            Some(filter) => with_registry_topology_filter_config(filter, read),
            None => read(ptr::null()),
        }
    }
}

impl Drop for Spot {
    fn drop(&mut self) {
        let _ = destroy_handle(&mut self.handle, ffi::zlink_spot_destroy);
    }
}
