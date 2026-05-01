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
    config_validation_error, last_errno, submit_not_supported_error, submit_validation_error,
};
use crate::ffi;
use crate::flags::{RecvFlags, SendFlags};
use crate::message::{IntoMultipart, Message, RoutingId};
use crate::monitor::MonitorSnapshot;
use crate::request_progress::RequestProgressGuard;
use crate::socket::{
    CallbackBox, cstr_buf_to_string, prepare_send_parts, routing_id_from_ptr,
    send_ready_trampoline, submit_part_sequence, take_parts,
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
        check_config_rc(unsafe { ffi::zlink_registry_set_id(self.handle, id) })
    }

    pub fn add_peer(&self, peer_pub_endpoint: &str) -> Result<(), ConnectError> {
        let c = CString::new(peer_pub_endpoint).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe { ffi::zlink_registry_add_peer(self.handle, c.as_ptr()) })
    }

    pub fn set_heartbeat(&self, interval_ms: u32, timeout_ms: u32) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_registry_set_heartbeat(self.handle, interval_ms, timeout_ms)
        })
    }

    pub fn set_broadcast_interval(&self, interval_ms: u32) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_registry_set_broadcast_interval(self.handle, interval_ms)
        })
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
// ServiceType / ServiceRole newtypes
// ---------------------------------------------------------------------------

/// The service family for a Discovery instance.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ServiceType {
    Spot,
    Socket,
}

impl ServiceType {
    fn to_raw(self) -> ffi::zlink_service_type_t {
        match self {
            Self::Spot => ffi::zlink_service_type_t::ZLINK_SERVICE_TYPE_SPOT,
            Self::Socket => ffi::zlink_service_type_t::ZLINK_SERVICE_TYPE_SOCKET,
        }
    }

    fn from_raw(raw: ffi::zlink_service_type_t) -> Self {
        match raw {
            ffi::zlink_service_type_t::ZLINK_SERVICE_TYPE_SPOT => Self::Spot,
            ffi::zlink_service_type_t::ZLINK_SERVICE_TYPE_SOCKET => Self::Socket,
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

    fn from_raw_u16(raw: u16) -> Result<Self, ConfigError> {
        match raw {
            0 => Ok(Self::Invalid),
            2 => Ok(Self::Spot),
            3 => Ok(Self::Router),
            4 => Ok(Self::Dealer),
            5 => Ok(Self::Pub),
            6 => Ok(Self::Sub),
            _ => Err(config_validation_error()),
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
    pub mode: SpotNodeMode,
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
    pub service_name: String,
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
            service_name: fixed_cstr_to_string(&raw.service_name),
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
    pub service_name: String,
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
            service_name: fixed_cstr_to_string(&raw.service_name),
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

#[derive(Debug, Clone)]
pub struct SpotNodeSubjectEntry {
    pub role: SpotRole,
    pub subject: String,
    pub subject_kind: u32,
    pub ready_peer_count: u32,
    pub active_peer_count: u32,
    pub last_changed_ms: u64,
}

impl SpotNodeSubjectEntry {
    fn from_raw(raw: &ffi::zlink_spot_node_subject_entry_t) -> Self {
        Self {
            role: SpotRole::from_raw(raw.role),
            subject: fixed_cstr_to_string(&raw.subject),
            subject_kind: raw.subject_kind,
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
    pub subject_kind: Option<u32>,
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
    pub service_kind: ServiceKind,
    pub service_role: ServiceRole,
    pub service_name: String,
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
            service_kind: ServiceKind::from_raw(raw.service_kind),
            service_role: ServiceRole::from_raw(raw.service_role),
            service_name: fixed_cstr_to_string(&raw.service_name),
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
    pub service_kind: Option<ServiceKind>,
    pub service_role: Option<ServiceRole>,
    pub service_name: Option<String>,
}

#[derive(Debug, Clone)]
pub struct MemberPeerEntry {
    pub service_type: ServiceType,
    pub service_role: ServiceRole,
    pub service_name: String,
    pub endpoint: String,
    pub routing_id: RoutingId,
    pub weight: u32,
    pub value: i64,
}

impl MemberPeerEntry {
    fn from_raw(raw: &ffi::zlink_member_peer_entry_t) -> Result<Self, ConfigError> {
        Ok(Self {
            service_type: ServiceType::from_raw(raw.service_type),
            service_role: ServiceRole::from_raw_u16(raw.service_role)?,
            service_name: fixed_cstr_to_string(&raw.service_name),
            endpoint: fixed_cstr_to_string(&raw.endpoint),
            routing_id: RoutingId::from_raw(raw.routing_id),
            weight: raw.weight,
            value: raw.value,
        })
    }
}

#[derive(Debug, Clone)]
pub struct RegistryTopologyEntry {
    pub routing_id: RoutingId,
    pub service_kind: ServiceKind,
    pub service_role: ServiceRole,
    pub service_name: String,
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
            routing_id: RoutingId::from_raw(raw.routing_id),
            service_kind: ServiceKind::from_raw(raw.service_kind),
            service_role: ServiceRole::from_raw(raw.service_role),
            service_name: fixed_cstr_to_string(&raw.service_name),
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
    pub service_kind: Option<ServiceKind>,
    pub service_role: Option<ServiceRole>,
    pub service_name: Option<String>,
    pub routing_id: Option<RoutingId>,
    pub state: Option<TopologyState>,
    pub source: Option<TopologySource>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DiscoveryDealerPeerMode {
    Router = 1,
    Dealer = 2,
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
        service_type: ServiceType,
        service_name: &str,
    ) -> Result<Self, ConfigError> {
        let c_name = fixed_cstring_config(service_name, "service_name")?;
        let handle =
            unsafe { ffi::zlink_discovery_new(ctx.raw(), service_type.to_raw(), c_name.as_ptr()) };
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

    pub fn set_metadata(&self, data: &[u8]) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_discovery_set_metadata(
                self.handle,
                data.as_ptr() as *const c_void,
                data.len(),
            )
        })
    }

    pub fn get_metadata(&self) -> Result<Message, ConfigError> {
        let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(msg.as_mut_ptr());
        }
        check_config_rc(unsafe {
            ffi::zlink_discovery_get_metadata(self.handle, msg.as_mut_ptr())
        })?;
        Ok(unsafe { Message::from_raw(msg.assume_init()) })
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

    pub fn set_dealer_peer_mode(&self, mode: DiscoveryDealerPeerMode) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_discovery_set_dealer_peer_mode(self.handle, mode as u32)
        })
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

    pub fn member_peer_metadata(
        &self,
        service_role: ServiceRole,
        endpoint: &str,
    ) -> Result<Message, ConfigError> {
        let c_endpoint = fixed_cstring_config(endpoint, "endpoint")?;
        let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(msg.as_mut_ptr());
        }
        check_config_rc(unsafe {
            ffi::zlink_discovery_member_peer_metadata(
                self.handle,
                service_role.to_raw() as u16,
                c_endpoint.as_ptr(),
                msg.as_mut_ptr(),
            )
        })?;
        Ok(unsafe { Message::from_raw(msg.assume_init()) })
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
            mode: options.mode.to_raw(),
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

    pub fn set_router_hwm(&self, value: i32) -> Result<(), ConfigError> {
        self.set_option_i32(
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_ROUTER_HWM,
            value,
        )
    }

    pub fn set_pubsub_hwm(&self, value: i32) -> Result<(), ConfigError> {
        self.set_option_i32(
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_PUBSUB_HWM,
            value,
        )
    }

    pub fn set_router_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        self.set_option_i32(
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE,
            profile.to_raw(),
        )
    }

    pub fn set_pubsub_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        self.set_option_i32(
            ffi::zlink_spot_node_option_t::ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE,
            profile.to_raw(),
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

    pub fn create_spot(&self) -> Result<Spot, ConfigError> {
        Spot::new(self)
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

// ---------------------------------------------------------------------------
// Spot
// ---------------------------------------------------------------------------

/// Unified SPOT facade over an existing SPOT node.
///
/// The Spot handle borrows a `SpotNode`, lazily creates side sockets, and
/// exposes the canonical data-plane API (publish, subscribe, etc.).
pub struct Spot {
    handle: *mut c_void,
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
            send_ready_cb: None,
            routed_cb: None,
            dispatch_cb: None,
        })
    }

    pub fn publish(
        &self,
        service_name: &str,
        topic: &str,
        parts: impl IntoMultipart,
    ) -> Result<(), SubmitError> {
        self.publish_with_flags(service_name, topic, parts, SendFlags::NONE)
    }

    pub fn publish_with_flags(
        &self,
        service_name: &str,
        topic: &str,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        let c_service_name = fixed_cstring_config(service_name, "service_name")
            .map_err(|_| submit_validation_error())?;
        let c_topic =
            fixed_cstring_config(topic, "topic").map_err(|_| submit_validation_error())?;
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
            ffi::zlink_spot_publish_part(
                self.handle,
                c_service_name.as_ptr(),
                c_topic.as_ptr(),
                part,
                flags.bits(),
                part_flag,
            )
        })?;
        drop(parts);
        check_submit_rc(rc)
    }

    pub fn send_channel(
        &self,
        channel_name: &str,
        parts: impl IntoMultipart,
    ) -> Result<(), SubmitError> {
        self.send_channel_with_flags(channel_name, parts, SendFlags::NONE)
    }

    pub fn send_channel_with_flags(
        &self,
        channel_name: &str,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        let c_channel_name = fixed_cstring_config(channel_name, "channel_name")
            .map_err(|_| submit_validation_error())?;
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
            ffi::zlink_spot_send_channel_part(
                self.handle,
                c_channel_name.as_ptr(),
                part,
                flags.bits(),
                part_flag,
            )
        })?;
        drop(parts);
        check_submit_rc(rc)
    }

    pub async fn request_channel(
        &self,
        channel_name: &str,
        parts: impl IntoMultipart,
        timeout: Duration,
    ) -> Result<Vec<Message>, ZlinkError> {
        let (tx, rx) = mpsc::channel();
        self.request_channel_callback(
            channel_name,
            parts,
            move |result| {
                let _ = tx.send(result);
            },
            SendFlags::NONE,
            timeout,
        )?;
        rx.recv()
            .unwrap_or_else(|_| {
                Err(RequestError::new(
                    crate::error::RequestResult::ProtocolError,
                    libc::EINVAL,
                ))
            })
            .map_err(ZlinkError::from)
    }

    pub fn request_channel_callback<F>(
        &self,
        channel_name: &str,
        parts: impl IntoMultipart,
        callback: F,
        flags: SendFlags,
        timeout: Duration,
    ) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        let c_channel_name = fixed_cstring_config(channel_name, "channel_name")
            .map_err(|_| submit_validation_error())?;
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let state_ptr = Box::into_raw(Box::new(SpotReplyCallbackState {
            callback: Some(Box::new(callback)),
            _progress: RequestProgressGuard::attach_spot(self.handle),
        }));
        let timeout_ms = timeout_to_timeout_ms(timeout);
        let rc = submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
            ffi::zlink_spot_request_channel_part(
                self.handle,
                c_channel_name.as_ptr(),
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
        })?;
        if rc != 0 {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
        }
        check_submit_rc(rc)
    }

    pub fn request_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        parts: impl IntoMultipart,
        timeout: Duration,
    ) -> Result<Vec<Message>, ZlinkError> {
        let (tx, rx) = mpsc::channel();
        self.request_to_spot_callback(
            dest_node_rid,
            dest_spot_rid,
            parts,
            move |result| {
                let _ = tx.send(result);
            },
            SendFlags::NONE,
            timeout,
        )?;
        rx.recv()
            .unwrap_or_else(|_| {
                Err(RequestError::new(
                    crate::error::RequestResult::ProtocolError,
                    libc::EINVAL,
                ))
            })
            .map_err(ZlinkError::from)
    }

    pub fn request_to_spot_callback<F>(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        parts: impl IntoMultipart,
        callback: F,
        flags: SendFlags,
        timeout: Duration,
    ) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let state_ptr = Box::into_raw(Box::new(SpotReplyCallbackState {
            callback: Some(Box::new(callback)),
            _progress: RequestProgressGuard::attach_spot(self.handle),
        }));
        let timeout_ms = timeout_to_timeout_ms(timeout);
        let rc = submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
            ffi::zlink_spot_request_spot_part(
                self.handle,
                dest_node_rid.as_raw(),
                dest_spot_rid.as_raw(),
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
        })?;
        if rc != 0 {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
        }
        check_submit_rc(rc)
    }

    pub fn drain_channel_reply_from(&self, subject: *mut c_void) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_spot_channel_reply_progress_from(self.handle, subject)
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

    pub fn subscribe(&self) -> Result<TopicMessage, RecvError> {
        self.subscribe_with_flags(RecvFlags::NONE)
    }

    pub fn subscribe_with_flags(&self, flags: RecvFlags) -> Result<TopicMessage, RecvError> {
        let mut service_buf = [0i8; 256];
        let mut topic_buf = [0i8; 256];
        let (routing_id, service_name, topic, parts) = recv_spot_subscribed_parts(
            self.handle,
            &mut service_buf,
            &mut topic_buf,
            flags.bits(),
        )?
        .ok_or_else(|| RecvError::new(crate::error::RecvResult::NoData, libc::EAGAIN))?;
        Ok(TopicMessage::new(
            routing_id,
            Some(service_name),
            topic,
            parts,
        ))
    }

    pub fn receive_subscription_event(&self) -> Result<SubscriptionEvent, RecvError> {
        self.receive_subscription_event_with_flags(RecvFlags::NONE)
    }

    pub fn receive_subscription_event_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<SubscriptionEvent, RecvError> {
        let _ = flags;
        Err(RecvError::new(
            crate::error::RecvResult::NotSupported,
            libc::ENOTSUP,
        ))
    }

    pub fn reply_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
        parts: impl IntoMultipart,
    ) -> Result<(), SubmitError> {
        self.reply_to_spot_with_flags(
            dest_node_rid,
            dest_spot_rid,
            request_seq,
            parts,
            SendFlags::NONE,
        )
    }

    pub fn reply_to_spot_with_flags(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        if flags.bits() != 0 {
            return Err(submit_not_supported_error());
        }
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
            ffi::zlink_spot_reply_spot_part(
                self.handle,
                dest_node_rid.as_raw(),
                dest_spot_rid.as_raw(),
                request_seq,
                part,
                part_flag,
            )
        })?;
        drop(parts);
        check_submit_rc(rc)
    }

    pub fn send_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        parts: impl IntoMultipart,
    ) -> Result<(), SubmitError> {
        self.send_to_spot_with_flags(dest_node_rid, dest_spot_rid, parts, SendFlags::NONE)
    }

    pub fn send_to_spot_with_flags(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
            ffi::zlink_spot_send_spot_part(
                self.handle,
                dest_node_rid.as_raw(),
                dest_spot_rid.as_raw(),
                part,
                flags.bits(),
                part_flag,
            )
        })?;
        drop(parts);
        check_submit_rc(rc)
    }

    pub fn reply_to_router(
        &self,
        peer_rid: RoutingId,
        request_seq: u64,
        parts: impl IntoMultipart,
    ) -> Result<(), SubmitError> {
        self.reply_to_router_with_flags(peer_rid, request_seq, parts, SendFlags::NONE)
    }

    pub fn reply_to_router_with_flags(
        &self,
        peer_rid: RoutingId,
        request_seq: u64,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        if flags.bits() != 0 {
            return Err(submit_not_supported_error());
        }
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
            ffi::zlink_spot_reply_router_part(
                self.handle,
                peer_rid.as_raw(),
                request_seq,
                part,
                part_flag,
            )
        })?;
        drop(parts);
        check_submit_rc(rc)
    }

    pub fn recv_routed(&self) -> Result<Received, RecvError> {
        self.recv_routed_with_flags(RecvFlags::NONE)
    }

    pub fn recv_routed_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError> {
        recv_spot_routed_parts(self.handle, flags.bits())?
            .ok_or_else(|| RecvError::new(crate::error::RecvResult::NoData, libc::EAGAIN))
    }

    pub fn on_routed_receive<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn(RoutingId, RoutingId, u64, Vec<Message>) + Send + 'static,
    {
        let (cb, userdata) = CallbackBox::new(handler);
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
        F: Fn(SpotDispatchInfo) + Send + 'static,
    {
        let (cb, userdata) = CallbackBox::new(handler);
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

    #[allow(dead_code)]
    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        destroy_handle_close(&mut self.handle, ffi::zlink_spot_destroy)
    }
}

type SpotReplyCallback = Box<dyn FnOnce(Result<Vec<Message>, RequestError>) + Send>;
type SpotSubscribedParts =
    Result<Option<(Option<RoutingId>, String, String, Vec<Message>)>, RecvError>;

struct SpotReplyCallbackState {
    callback: Option<SpotReplyCallback>,
    _progress: RequestProgressGuard,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotDispatchEvent {
    SubscribeReadable,
    RoutedReadable,
    TimerReadable,
    ChannelReplyReadable,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotDispatchSubjectKind {
    Spot,
    Timer,
    ChannelDealer,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SpotDispatchInfo {
    pub event: SpotDispatchEvent,
    pub subject_kind: SpotDispatchSubjectKind,
    pub subject: *mut c_void,
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
        }
    }
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

fn recv_spot_subscribed_parts(
    handle: *mut c_void,
    service_buf: &mut [i8; 256],
    topic_buf: &mut [i8; 256],
    flags: ffi::zlink_recv_flags_t,
) -> SpotSubscribedParts {
    let mut routing_id = None;
    let mut service_name = String::new();
    let mut topic = String::new();
    let mut parts = Vec::new();
    let mut recv_flags = flags;

    loop {
        let mut source_rid_ptr = ptr::null();
        let mut service_name_len = service_buf.len();
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
                service_buf.as_mut_ptr(),
                service_buf.len(),
                &mut service_name_len,
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
                return Ok(None);
            }
            if rc != 0 {
                let errno = unsafe { ffi::zlink_errno() };
                if errno == libc::EAGAIN {
                    return Ok(None);
                }
                return Err(check_recv_rc(rc).unwrap_err());
            }
            routing_id = routing_id_from_ptr(source_rid_ptr);
            service_name = cstr_buf_to_string(service_buf, service_name_len);
            topic = cstr_buf_to_string(topic_buf, topic_len);
        } else if rc != 0 {
            return Err(check_recv_rc(rc).unwrap_err());
        }

        parts.push(unsafe { Message::from_raw(part.assume_init()) });
        if has_more == 0 {
            return Ok(Some((routing_id, service_name, topic, parts)));
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
            let mut received = Received::new(Some(node_rid), parts);
            received.spot_rid = Some(spot_rid);
            received
        } else {
            Received::with_spot_reply_context(handle, node_rid, spot_rid, request_seq, parts)
        }
    } else if request_seq == 0 {
        Received::new(Some(node_rid), parts)
    } else {
        Received::with_spot_router_reply_context(handle, node_rid, request_seq, parts)
    }
}

fn recv_spot_routed_parts(
    handle: *mut c_void,
    flags: ffi::zlink_recv_flags_t,
) -> Result<Option<Received>, RecvError> {
    let mut source_node_rid = ptr::null();
    let mut source_spot_rid = ptr::null();
    let mut request_seq = 0;
    let mut parts = Vec::new();
    let mut recv_flags = flags;

    loop {
        let mut current_source_node_rid = ptr::null();
        let mut current_source_spot_rid = ptr::null();
        let mut current_request_seq = 0;
        let mut part = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(part.as_mut_ptr());
        }
        let mut has_more = 0;
        let rc = unsafe {
            ffi::zlink_spot_recv_part(
                handle,
                &mut current_source_node_rid,
                &mut current_source_spot_rid,
                &mut current_request_seq,
                part.as_mut_ptr(),
                &mut has_more,
                recv_flags,
            )
        };

        if parts.is_empty() {
            if rc == RecvResult::NoData as i32 {
                return Ok(None);
            }
            if rc != 0 {
                let errno = unsafe { ffi::zlink_errno() };
                if errno == libc::EAGAIN {
                    return Ok(None);
                }
                return Err(check_recv_rc(rc).unwrap_err());
            }
            source_node_rid = current_source_node_rid;
            source_spot_rid = current_source_spot_rid;
            request_seq = current_request_seq;
        } else if rc != 0 {
            return Err(check_recv_rc(rc).unwrap_err());
        }

        parts.push(unsafe { Message::from_raw(part.assume_init()) });
        if has_more == 0 {
            return Ok(Some(spot_received_from_raw(
                handle,
                source_node_rid,
                source_spot_rid,
                request_seq,
                parts,
            )));
        }
        recv_flags = ffi::ZLINK_DONTWAIT;
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
        let request_result = match result_ {
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
        };
        callback(Err(crate::error::request_error_from_result(request_result)));
    }
}

unsafe extern "C" fn spot_handler_trampoline<
    F: Fn(RoutingId, RoutingId, u64, Vec<Message>) + Send + 'static,
>(
    source_rid: *const ffi::zlink_routing_id_t,
    spot_rid: *const ffi::zlink_routing_id_t,
    request_seq: u64,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let handler = unsafe { &*(userdata as *const F) };
    let source = if source_rid.is_null() {
        RoutingId::from_raw(ffi::zlink_routing_id_t {
            size: 0,
            data: [0; 255],
        })
    } else {
        unsafe { RoutingId::from_raw(*source_rid) }
    };
    let spot = if spot_rid.is_null() {
        RoutingId::from_raw(ffi::zlink_routing_id_t {
            size: 0,
            data: [0; 255],
        })
    } else {
        unsafe { RoutingId::from_raw(*spot_rid) }
    };
    handler(source, spot, request_seq, take_parts(parts, part_count));
}

unsafe extern "C" fn spot_dispatch_trampoline<F: Fn(SpotDispatchInfo) + Send + 'static>(
    _spot: *mut c_void,
    info: *const ffi::zlink_spot_dispatch_info_t,
    userdata: *mut c_void,
) {
    let handler = unsafe { &*(userdata as *const F) };
    let info = unsafe { &*info };
    handler(SpotDispatchInfo {
        event: SpotDispatchEvent::from_raw(info.event),
        subject_kind: SpotDispatchSubjectKind::from_raw(info.subject_kind),
        subject: info.subject,
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

    pub fn member_peers(
        &self,
        service_type: ServiceType,
        service_name: &str,
    ) -> Result<Vec<MemberPeerEntry>, ConfigError> {
        let c_service_name = fixed_cstring_config(service_name, "service_name")?;
        let count = count_entries_config(|count| unsafe {
            ffi::zlink_registry_member_peers(
                self.handle,
                service_type.to_raw(),
                c_service_name.as_ptr(),
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
                    service_type.to_raw(),
                    c_service_name.as_ptr(),
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

    pub fn member_peer_metadata(
        &self,
        service_type: ServiceType,
        service_name: &str,
        service_role: ServiceRole,
        endpoint: &str,
    ) -> Result<Message, ConfigError> {
        let c_service_name = fixed_cstring_config(service_name, "service_name")?;
        let c_endpoint = fixed_cstring_config(endpoint, "endpoint")?;
        let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(msg.as_mut_ptr());
        }
        check_config_rc(unsafe {
            ffi::zlink_registry_member_peer_metadata(
                self.handle,
                service_type.to_raw(),
                c_service_name.as_ptr(),
                service_role.to_raw() as u16,
                c_endpoint.as_ptr(),
                msg.as_mut_ptr(),
            )
        })?;
        Ok(unsafe { Message::from_raw(msg.assume_init()) })
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
            (*ptr).subject_kind = subject_kind;
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
        if let Some(service_kind) = filter.service_kind {
            (*ptr).service_kind = service_kind.to_raw();
        }
        if let Some(service_role) = filter.service_role {
            (*ptr).service_role = service_role.to_raw();
        }
        if let Some(service_name) = &filter.service_name {
            write_c_array_config(&mut (*ptr).service_name, service_name, "service_name")?;
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
        if let Some(service_kind) = filter.service_kind {
            (*ptr).service_kind = service_kind.to_raw();
        }
        if let Some(service_role) = filter.service_role {
            (*ptr).service_role = service_role.to_raw();
        }
        if let Some(service_name) = &filter.service_name {
            write_c_array_config(&mut (*ptr).service_name, service_name, "service_name")?;
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
