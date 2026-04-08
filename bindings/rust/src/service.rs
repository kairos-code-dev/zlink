use std::ffi::{CStr, CString, c_char, c_void};
use std::mem::MaybeUninit;
use std::ptr;

use crate::domain::{SendResult, TopicMessage};
use crate::error::{ZlinkError, check_rc};
use crate::ffi;
use crate::message::{IntoMultipart, Message, RoutingId};
use crate::socket::{
    CallbackBox, check_send_result, cstr_buf_to_string, prepare_send_parts, send_ready_trampoline,
    subscribe_trampoline, take_parts,
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
    pub fn new(ctx: &crate::ctx::Context) -> Result<Self, ZlinkError> {
        let handle = unsafe { ffi::zlink_registry_new(ctx.raw()) };
        if handle.is_null() {
            return Err(ZlinkError::last());
        }
        Ok(Self { handle })
    }

    pub fn bind(&self, pub_endpoint: &str, router_endpoint: &str) -> Result<(), ZlinkError> {
        let c_pub = CString::new(pub_endpoint)
            .map_err(|_| ZlinkError::validation("pub_endpoint contains null byte"))?;
        let c_router = CString::new(router_endpoint)
            .map_err(|_| ZlinkError::validation("router_endpoint contains null byte"))?;
        check_rc(unsafe {
            ffi::zlink_registry_bind(self.handle, c_pub.as_ptr(), c_router.as_ptr())
        })
    }

    pub fn set_id(&self, id: u32) -> Result<(), ZlinkError> {
        check_rc(unsafe { ffi::zlink_registry_set_id(self.handle, id) })
    }

    pub fn add_peer(&self, peer_pub_endpoint: &str) -> Result<(), ZlinkError> {
        let c = CString::new(peer_pub_endpoint)
            .map_err(|_| ZlinkError::validation("endpoint contains null byte"))?;
        check_rc(unsafe { ffi::zlink_registry_add_peer(self.handle, c.as_ptr()) })
    }

    pub fn set_heartbeat(&self, interval_ms: u32, timeout_ms: u32) -> Result<(), ZlinkError> {
        check_rc(unsafe { ffi::zlink_registry_set_heartbeat(self.handle, interval_ms, timeout_ms) })
    }

    pub fn set_broadcast_interval(&self, interval_ms: u32) -> Result<(), ZlinkError> {
        check_rc(unsafe { ffi::zlink_registry_set_broadcast_interval(self.handle, interval_ms) })
    }

    pub fn set_tls_server(
        &self,
        cert_pem: &str,
        key_pem: &str,
        require_client_cert: bool,
    ) -> Result<(), ZlinkError> {
        set_tls_server(self.handle, cert_pem, key_pem, require_client_cert)
    }

    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ZlinkError> {
        set_tls_client(self.handle, ca_cert_pem, hostname, trust_system)
    }

    pub fn close(&mut self) -> Result<(), ZlinkError> {
        destroy_handle(&mut self.handle, ffi::zlink_registry_destroy)
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

    fn from_raw_u16(raw: u16) -> Result<Self, ZlinkError> {
        match raw {
            0 => Ok(Self::Invalid),
            2 => Ok(Self::Spot),
            3 => Ok(Self::Router),
            4 => Ok(Self::Dealer),
            5 => Ok(Self::Pub),
            6 => Ok(Self::Sub),
            _ => Err(ZlinkError::state(format!("unknown ServiceRole: {raw}"))),
        }
    }
}

/// The service kind reported by service monitors and topology snapshots.
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
    pub value: i64,
}

impl MemberPeerEntry {
    fn from_raw(raw: &ffi::zlink_member_peer_entry_t) -> Result<Self, ZlinkError> {
        Ok(Self {
            service_type: ServiceType::from_raw(raw.service_type),
            service_role: ServiceRole::from_raw_u16(raw.service_role)?,
            service_name: fixed_cstr_to_string(&raw.service_name),
            endpoint: fixed_cstr_to_string(&raw.endpoint),
            routing_id: RoutingId::from_raw(raw.routing_id),
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
    ) -> Result<Self, ZlinkError> {
        let c_name = fixed_cstring(service_name, "service_name")?;
        let handle =
            unsafe { ffi::zlink_discovery_new(ctx.raw(), service_type.to_raw(), c_name.as_ptr()) };
        if handle.is_null() {
            return Err(ZlinkError::last());
        }
        Ok(Self { handle })
    }

    pub fn connect_registry(&self, endpoint: &str) -> Result<(), ZlinkError> {
        let c = fixed_cstring(endpoint, "endpoint")?;
        check_rc(unsafe { ffi::zlink_discovery_connect_registry(self.handle, c.as_ptr()) })
    }

    pub fn set_value(&self, value: i64) -> Result<(), ZlinkError> {
        check_rc(unsafe { ffi::zlink_discovery_set_value(self.handle, value) })
    }

    pub fn get_value(&self) -> Result<i64, ZlinkError> {
        let mut v: i64 = 0;
        check_rc(unsafe { ffi::zlink_discovery_get_value(self.handle, &mut v) })?;
        Ok(v)
    }

    pub fn set_metadata(&self, data: &[u8]) -> Result<(), ZlinkError> {
        check_rc(unsafe {
            ffi::zlink_discovery_set_metadata(
                self.handle,
                data.as_ptr() as *const c_void,
                data.len(),
            )
        })
    }

    pub fn get_metadata(&self) -> Result<Message, ZlinkError> {
        let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(msg.as_mut_ptr());
        }
        check_rc(unsafe { ffi::zlink_discovery_get_metadata(self.handle, msg.as_mut_ptr()) })?;
        Ok(unsafe { Message::from_raw(msg.assume_init()) })
    }

    pub fn monitor_open(&self) -> Result<crate::monitor::ServiceMonitor, ZlinkError> {
        crate::monitor::ServiceMonitor::open(self)
    }

    pub fn member_peers(&self) -> Result<Vec<MemberPeerEntry>, ZlinkError> {
        let count = count_entries(|count| unsafe {
            ffi::zlink_discovery_member_peers(self.handle, ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }

        let mut entries =
            vec![unsafe { std::mem::zeroed::<ffi::zlink_member_peer_entry_t>() }; count];
        let actual = read_entries(
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
    ) -> Result<Message, ZlinkError> {
        let c_endpoint = fixed_cstring(endpoint, "endpoint")?;
        let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(msg.as_mut_ptr());
        }
        check_rc(unsafe {
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
    ) -> Result<(), ZlinkError> {
        set_tls_client(self.handle, ca_cert_pem, hostname, trust_system)
    }

    #[allow(dead_code)]
    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }

    pub fn close(&mut self) -> Result<(), ZlinkError> {
        destroy_handle(&mut self.handle, ffi::zlink_discovery_destroy)
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
    pub fn new(ctx: &crate::ctx::Context) -> Result<Self, ZlinkError> {
        let handle = unsafe { ffi::zlink_spot_node_new(ctx.raw()) };
        if handle.is_null() {
            return Err(ZlinkError::last());
        }
        Ok(Self { handle })
    }

    pub fn bind(&self, endpoint: &str) -> Result<(), ZlinkError> {
        let c = fixed_cstring(endpoint, "endpoint")?;
        check_rc(unsafe { ffi::zlink_spot_node_bind(self.handle, c.as_ptr()) })
    }

    pub fn last_endpoint(&self) -> Result<String, ZlinkError> {
        Ok(self.status_snapshot()?.local_endpoint)
    }

    pub fn connect_peer(&self, peer_endpoint: &str) -> Result<(), ZlinkError> {
        let c = fixed_cstring(peer_endpoint, "peer_endpoint")?;
        check_rc(unsafe { ffi::zlink_spot_node_connect_peer(self.handle, c.as_ptr()) })
    }

    pub fn disconnect_peer(&self, peer_endpoint: &str) -> Result<(), ZlinkError> {
        let c = fixed_cstring(peer_endpoint, "peer_endpoint")?;
        check_rc(unsafe { ffi::zlink_spot_node_disconnect_peer(self.handle, c.as_ptr()) })
    }

    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ZlinkError> {
        check_rc(unsafe { ffi::zlink_spot_node_attach_discovery(self.handle, discovery.raw()) })
    }

    pub fn set_tls_server(
        &self,
        cert_pem: &str,
        key_pem: &str,
        require_client_cert: bool,
    ) -> Result<(), ZlinkError> {
        set_tls_server(self.handle, cert_pem, key_pem, require_client_cert)
    }

    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ZlinkError> {
        set_tls_client(self.handle, ca_cert_pem, hostname, trust_system)
    }

    pub fn status_snapshot(&self) -> Result<SpotNodeStatus, ZlinkError> {
        let mut raw = MaybeUninit::<ffi::zlink_spot_node_status_t>::uninit();
        check_rc(unsafe { ffi::zlink_spot_node_status_snapshot(self.handle, raw.as_mut_ptr()) })?;
        let raw = unsafe { raw.assume_init() };
        Ok(SpotNodeStatus::from_raw(&raw))
    }

    pub fn peers_snapshot(&self) -> Result<Vec<SpotNodePeerEntry>, ZlinkError> {
        let count = count_entries(|count| unsafe {
            ffi::zlink_spot_node_peers_snapshot(self.handle, ptr::null_mut(), count)
        })?;
        if count == 0 {
            return Ok(Vec::new());
        }

        let mut entries =
            vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_peer_entry_t>() }; count];
        let actual = read_entries(
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
    ) -> Result<Vec<SpotNodePeerEntry>, ZlinkError> {
        with_spot_node_peer_filter(filter, |filter_ptr| {
            let count = count_entries(|count| unsafe {
                ffi::zlink_spot_node_peers_query(self.handle, filter_ptr, ptr::null_mut(), count)
            })?;
            if count == 0 {
                return Ok(Vec::new());
            }

            let mut entries =
                vec![unsafe { std::mem::zeroed::<ffi::zlink_spot_node_peer_entry_t>() }; count];
            let actual = read_entries(
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

    pub fn subjects_snapshot(&self) -> Result<Vec<SpotNodeSubjectEntry>, ZlinkError> {
        self.subjects_query_opt(None)
    }

    #[allow(dead_code)]
    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }

    pub fn close(&mut self) -> Result<(), ZlinkError> {
        destroy_handle(&mut self.handle, ffi::zlink_spot_node_destroy)
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
    sub_cb: Option<CallbackBox>,
    send_ready_cb: Option<CallbackBox>,
}

unsafe impl Send for Spot {}

impl Spot {
    pub fn new(node: &SpotNode) -> Result<Self, ZlinkError> {
        let handle = unsafe { ffi::zlink_spot_new(node.raw()) };
        if handle.is_null() {
            return Err(ZlinkError::last());
        }
        Ok(Self {
            handle,
            sub_cb: None,
            send_ready_cb: None,
        })
    }

    pub fn publish(&self, topic: &str, parts: impl IntoMultipart) -> Result<(), ZlinkError> {
        let c_topic =
            CString::new(topic).map_err(|_| ZlinkError::validation("topic contains null byte"))?;
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = unsafe {
            ffi::zlink_publish(
                self.handle,
                c_topic.as_ptr(),
                native.as_mut_ptr(),
                native.len(),
                0,
            )
        };
        drop(parts);
        check_rc(rc)
    }

    pub fn try_publish(
        &self,
        topic: &str,
        parts: impl IntoMultipart,
    ) -> Result<SendResult, ZlinkError> {
        let c_topic =
            CString::new(topic).map_err(|_| ZlinkError::validation("topic contains null byte"))?;
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = unsafe {
            ffi::zlink_publish(
                self.handle,
                c_topic.as_ptr(),
                native.as_mut_ptr(),
                native.len(),
                ffi::ZLINK_DONTWAIT,
            )
        };
        drop(parts);
        check_send_result(rc)
    }

    pub fn set_subscription(&self, filter: &str) -> Result<(), ZlinkError> {
        let c = CString::new(filter)
            .map_err(|_| ZlinkError::validation("filter contains null byte"))?;
        check_rc(unsafe { ffi::zlink_set_subscription(self.handle, c.as_ptr()) })
    }

    pub fn unset_subscription(&self, filter: &str) -> Result<(), ZlinkError> {
        let c = CString::new(filter)
            .map_err(|_| ZlinkError::validation("filter contains null byte"))?;
        check_rc(unsafe { ffi::zlink_unset_subscription(self.handle, c.as_ptr()) })
    }

    pub fn subscribe(&self) -> Result<TopicMessage, ZlinkError> {
        let mut rid = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        let mut parts_ptr: *mut ffi::zlink_msg_t = ptr::null_mut();
        let mut part_count: usize = 0;
        let mut topic_buf = [0i8; 256];
        let mut topic_len: usize = 256;

        let rc = unsafe {
            ffi::zlink_subscribe(
                self.handle,
                rid.as_mut_ptr(),
                &mut parts_ptr,
                &mut part_count,
                topic_buf.as_mut_ptr(),
                &mut topic_len,
                0,
            )
        };
        check_rc(rc)?;

        let rid = unsafe { rid.assume_init() };
        let topic = cstr_buf_to_string(&topic_buf, topic_len);
        let parts = take_parts(parts_ptr, part_count);
        Ok(TopicMessage::new(RoutingId::from_raw(rid), topic, parts))
    }

    pub fn try_subscribe(&self) -> Result<Option<TopicMessage>, ZlinkError> {
        let mut rid = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        let mut parts_ptr: *mut ffi::zlink_msg_t = ptr::null_mut();
        let mut part_count: usize = 0;
        let mut topic_buf = [0i8; 256];
        let mut topic_len: usize = 256;

        let rc = unsafe {
            ffi::zlink_subscribe(
                self.handle,
                rid.as_mut_ptr(),
                &mut parts_ptr,
                &mut part_count,
                topic_buf.as_mut_ptr(),
                &mut topic_len,
                ffi::ZLINK_DONTWAIT as u32,
            )
        };
        if rc == -1 {
            let errno = unsafe { ffi::zlink_errno() };
            if errno == libc::EAGAIN {
                return Ok(None);
            }
            return Err(ZlinkError::last());
        }

        let rid = unsafe { rid.assume_init() };
        let topic = cstr_buf_to_string(&topic_buf, topic_len);
        let parts = take_parts(parts_ptr, part_count);
        Ok(Some(TopicMessage::new(
            RoutingId::from_raw(rid),
            topic,
            parts,
        )))
    }

    pub fn on_subscribe<F>(&mut self, handler: F) -> Result<(), ZlinkError>
    where
        F: Fn(TopicMessage) + Send + 'static,
    {
        let (cb, userdata) = CallbackBox::new(handler);
        let rc = unsafe {
            ffi::zlink_subscribe_handler(self.handle, subscribe_trampoline::<F>, userdata)
        };
        if rc == -1 {
            drop(cb);
            return Err(ZlinkError::last());
        }
        self.sub_cb = Some(cb);
        Ok(())
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), ZlinkError>
    where
        F: Fn() + Send + 'static,
    {
        let (cb, userdata) = CallbackBox::new(handler);
        let rc = unsafe {
            ffi::zlink_send_ready_handler(self.handle, send_ready_trampoline::<F>, userdata)
        };
        if rc == -1 {
            drop(cb);
            return Err(ZlinkError::last());
        }
        self.send_ready_cb = Some(cb);
        Ok(())
    }

    #[allow(dead_code)]
    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }

    pub fn close(&mut self) -> Result<(), ZlinkError> {
        destroy_handle(&mut self.handle, ffi::zlink_spot_destroy)
    }
}

/// Read-only registry query client for remote topology snapshots.
pub struct RegistryQueryClient {
    handle: *mut c_void,
}

unsafe impl Send for RegistryQueryClient {}

impl RegistryQueryClient {
    pub fn new(ctx: &crate::ctx::Context) -> Result<Self, ZlinkError> {
        let handle = unsafe { ffi::zlink_registry_query_client_new(ctx.raw()) };
        if handle.is_null() {
            return Err(ZlinkError::last());
        }
        Ok(Self { handle })
    }

    pub fn connect(&self, endpoint: &str) -> Result<(), ZlinkError> {
        let c = fixed_cstring(endpoint, "endpoint")?;
        check_rc(unsafe { ffi::zlink_registry_query_client_connect(self.handle, c.as_ptr()) })
    }

    pub fn snapshot(
        &self,
        filter: Option<&RegistryTopologyFilter>,
    ) -> Result<Vec<RegistryTopologyEntry>, ZlinkError> {
        self.snapshot_query_opt(filter)
    }

    pub fn close(&mut self) -> Result<(), ZlinkError> {
        destroy_handle(&mut self.handle, ffi::zlink_registry_query_destroy)
    }
}

impl Drop for RegistryQueryClient {
    fn drop(&mut self) {
        let _ = destroy_handle(&mut self.handle, ffi::zlink_registry_query_destroy);
    }
}

impl Registry {
    pub fn status_snapshot(&self) -> Result<RegistryStatus, ZlinkError> {
        let mut raw = MaybeUninit::<ffi::zlink_registry_status_t>::uninit();
        check_rc(unsafe { ffi::zlink_registry_status_snapshot(self.handle, raw.as_mut_ptr()) })?;
        let raw = unsafe { raw.assume_init() };
        Ok(RegistryStatus::from_raw(&raw))
    }

    pub fn service_summary_snapshot(&self) -> Result<Vec<RegistryServiceSummaryEntry>, ZlinkError> {
        self.service_summary_query_opt(None)
    }

    pub fn service_summary_query(
        &self,
        filter: &RegistryServiceSummaryFilter,
    ) -> Result<Vec<RegistryServiceSummaryEntry>, ZlinkError> {
        self.service_summary_query_opt(Some(filter))
    }

    pub fn member_peers(
        &self,
        service_type: ServiceType,
        service_name: &str,
    ) -> Result<Vec<MemberPeerEntry>, ZlinkError> {
        let c_service_name = fixed_cstring(service_name, "service_name")?;
        let count = count_entries(|count| unsafe {
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
        let actual = read_entries(
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
    ) -> Result<Message, ZlinkError> {
        let c_service_name = fixed_cstring(service_name, "service_name")?;
        let c_endpoint = fixed_cstring(endpoint, "endpoint")?;
        let mut msg = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(msg.as_mut_ptr());
        }
        check_rc(unsafe {
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

    pub fn topology_snapshot(&self) -> Result<Vec<RegistryTopologyEntry>, ZlinkError> {
        self.topology_query_opt(None)
    }

    pub fn topology_query(
        &self,
        filter: &RegistryTopologyFilter,
    ) -> Result<Vec<RegistryTopologyEntry>, ZlinkError> {
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

fn fixed_cstring(value: &str, label: &str) -> Result<CString, ZlinkError> {
    if value.len() > 255 {
        return Err(ZlinkError::validation(format!(
            "{label} length {} exceeds maximum 255",
            value.len()
        )));
    }
    CString::new(value).map_err(|_| ZlinkError::validation(format!("{label} contains null byte")))
}

fn set_tls_server(
    handle: *mut c_void,
    cert_pem: &str,
    key_pem: &str,
    require_client_cert: bool,
) -> Result<(), ZlinkError> {
    let cert = fixed_cstring(cert_pem, "cert_pem")?;
    let key = fixed_cstring(key_pem, "key_pem")?;
    check_rc(unsafe {
        ffi::zlink_set_tls_server(
            handle,
            cert.as_ptr(),
            key.as_ptr(),
            if require_client_cert { 1 } else { 0 },
        )
    })
}

fn set_tls_client(
    handle: *mut c_void,
    ca_cert_pem: &str,
    hostname: &str,
    trust_system: bool,
) -> Result<(), ZlinkError> {
    let ca = fixed_cstring(ca_cert_pem, "ca_cert_pem")?;
    let host = fixed_cstring(hostname, "hostname")?;
    check_rc(unsafe {
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

fn write_c_array(buf: &mut [c_char], value: &str, label: &str) -> Result<(), ZlinkError> {
    if value.len() >= buf.len() {
        return Err(ZlinkError::validation(format!("{label} is too long")));
    }
    for b in buf.iter_mut() {
        *b = 0;
    }
    for (idx, byte) in value.as_bytes().iter().enumerate() {
        if *byte == 0 {
            return Err(ZlinkError::validation(format!(
                "{label} contains null byte"
            )));
        }
        buf[idx] = *byte as c_char;
    }
    Ok(())
}

fn count_entries(f: impl FnOnce(*mut usize) -> i32) -> Result<usize, ZlinkError> {
    let mut count: usize = 0;
    check_rc(f(&mut count))?;
    Ok(count)
}

fn read_entries<T>(
    capacity: usize,
    f: impl FnOnce(*mut T, *mut usize) -> i32,
    entries_ptr: *mut T,
) -> Result<usize, ZlinkError> {
    let mut count = capacity;
    check_rc(f(entries_ptr, &mut count))?;
    Ok(std::cmp::min(capacity, count))
}

fn with_spot_node_peer_filter<T>(
    filter: &SpotNodePeerFilter,
    f: impl FnOnce(*const ffi::zlink_spot_node_peer_filter_t) -> Result<T, ZlinkError>,
) -> Result<T, ZlinkError> {
    let mut raw = MaybeUninit::<ffi::zlink_spot_node_peer_filter_t>::zeroed();
    let ptr = raw.as_mut_ptr();
    unsafe {
        if let Some(peer_endpoint) = &filter.peer_endpoint {
            write_c_array(&mut (*ptr).peer_endpoint, peer_endpoint, "peer_endpoint")?;
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

fn with_spot_node_subject_filter<T>(
    filter: &SpotNodeSubjectFilter,
    f: impl FnOnce(*const ffi::zlink_spot_node_subject_filter_t) -> Result<T, ZlinkError>,
) -> Result<T, ZlinkError> {
    let mut raw = MaybeUninit::<ffi::zlink_spot_node_subject_filter_t>::zeroed();
    let ptr = raw.as_mut_ptr();
    unsafe {
        if let Some(role) = filter.role {
            (*ptr).role = role.to_raw();
        }
        if let Some(subject) = &filter.subject {
            write_c_array(&mut (*ptr).subject, subject, "subject")?;
        }
        if let Some(subject_kind) = filter.subject_kind {
            (*ptr).subject_kind = subject_kind;
        }
    }
    f(ptr)
}

fn with_registry_service_summary_filter<T>(
    filter: &RegistryServiceSummaryFilter,
    f: impl FnOnce(*const ffi::zlink_registry_service_summary_filter_t) -> Result<T, ZlinkError>,
) -> Result<T, ZlinkError> {
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
            write_c_array(&mut (*ptr).service_name, service_name, "service_name")?;
        }
    }
    f(ptr)
}

fn with_registry_topology_filter<T>(
    filter: &RegistryTopologyFilter,
    f: impl FnOnce(*const ffi::zlink_registry_topology_filter_t) -> Result<T, ZlinkError>,
) -> Result<T, ZlinkError> {
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
            write_c_array(&mut (*ptr).service_name, service_name, "service_name")?;
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
    ) -> Result<Vec<SpotNodeSubjectEntry>, ZlinkError> {
        let read = |filter_ptr: *const ffi::zlink_spot_node_subject_filter_t| {
            let count = count_entries(|count| unsafe {
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
            let actual = read_entries(
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
            Some(filter) => with_spot_node_subject_filter(filter, read),
            None => read(ptr::null()),
        }
    }
}

impl Registry {
    fn service_summary_query_opt(
        &self,
        filter: Option<&RegistryServiceSummaryFilter>,
    ) -> Result<Vec<RegistryServiceSummaryEntry>, ZlinkError> {
        let read = |filter_ptr: *const ffi::zlink_registry_service_summary_filter_t| {
            let count = count_entries(|count| unsafe {
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
            let actual = read_entries(
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
            Some(filter) => with_registry_service_summary_filter(filter, read),
            None => read(ptr::null()),
        }
    }

    fn topology_query_opt(
        &self,
        filter: Option<&RegistryTopologyFilter>,
    ) -> Result<Vec<RegistryTopologyEntry>, ZlinkError> {
        let read = |filter_ptr: *const ffi::zlink_registry_topology_filter_t| {
            let count = count_entries(|count| unsafe {
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
                vec![unsafe { std::mem::zeroed::<ffi::zlink_registry_topology_entry_t>() }; count];
            let actual = read_entries(
                count,
                |entries_ptr, count_ptr| unsafe {
                    if filter_ptr.is_null() {
                        ffi::zlink_registry_topology_snapshot(self.handle, entries_ptr, count_ptr)
                    } else {
                        ffi::zlink_registry_topology_query(
                            self.handle,
                            filter_ptr,
                            entries_ptr,
                            count_ptr,
                        )
                    }
                },
                entries.as_mut_ptr(),
            )?;
            Ok(entries[..actual]
                .iter()
                .map(RegistryTopologyEntry::from_raw)
                .collect())
        };

        match filter {
            Some(filter) => with_registry_topology_filter(filter, read),
            None => read(ptr::null()),
        }
    }
}

impl RegistryQueryClient {
    fn snapshot_query_opt(
        &self,
        filter: Option<&RegistryTopologyFilter>,
    ) -> Result<Vec<RegistryTopologyEntry>, ZlinkError> {
        let read = |filter_ptr| {
            let count = count_entries(|count| unsafe {
                ffi::zlink_registry_query_snapshot(self.handle, filter_ptr, ptr::null_mut(), count)
            })?;
            if count == 0 {
                return Ok(Vec::new());
            }

            let mut entries =
                vec![unsafe { std::mem::zeroed::<ffi::zlink_registry_topology_entry_t>() }; count];
            let actual = read_entries(
                count,
                |entries_ptr, count_ptr| unsafe {
                    ffi::zlink_registry_query_snapshot(
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
                .map(RegistryTopologyEntry::from_raw)
                .collect())
        };

        match filter {
            Some(filter) => with_registry_topology_filter(filter, read),
            None => read(ptr::null()),
        }
    }
}

impl Drop for Spot {
    fn drop(&mut self) {
        let _ = destroy_handle(&mut self.handle, ffi::zlink_spot_destroy);
    }
}
