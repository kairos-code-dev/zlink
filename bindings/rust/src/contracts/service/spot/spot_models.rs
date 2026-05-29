use crate::actor_models::ActorRef;
use crate::actor_received::ActorReceived;
use crate::error::RecvError;
use crate::flags::RecvFlags;
use crate::monitor_contracts::MonitorStatus;
use crate::routing_id::RoutingId;
use std::marker::PhantomData;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AutoConnectType {
    Invalid,
    RouteMesh,
    ClientServer,
    DealerMesh,
    Fanout,
    SpotMesh,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ServiceRole {
    Invalid,
    Spot,
    Router,
    Dealer,
    Pub,
    Sub,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ServiceKind {
    Discovery,
    SpotSub,
    SpotPub,
    Socket,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotKind {
    Invalid,
    Entry,
    User,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotRole {
    Pub,
    Sub,
}

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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotNodeSocketOwner {
    Any,
    Node,
    Spot,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SpotNodeOptions {
    pub mode: Option<SpotNodeMode>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotPeerSource {
    Manual,
    Discovery,
    Mixed,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotPeerKind {
    SpotMesh,
    RouterChannel,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotPeerState {
    Configured,
    Connecting,
    Connected,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RegistryState {
    Idle,
    Active,
    Degraded,
    Error,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TopologySource {
    Manual,
    Discovery,
    Registry,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TopologyState {
    Discovered,
    Connecting,
    Ready,
    Lost,
    Error,
    Stopped,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SubjectKind {
    Unknown = 0,
    PubSub = 1,
    Routed = 2,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotServiceAttachmentRole {
    Router,
    Pub,
    Sub,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotDispatchEvent {
    SubscribeReadable,
    RoutedReadable,
    TimerReadable,
    ChannelReplyReadable,
    ActorReadable,
    ActorJoinReadable,
    ActorLifecycleReadable,
}

/// Information passed to `on_dispatch_event` callbacks.
pub struct SpotDispatchInfo<'a> {
    pub event: SpotDispatchEvent,
    pub(crate) node_cookie: usize,
    pub(crate) actor_for_recv: Option<ActorRef>,
    pub(crate) _marker: PhantomData<&'a ()>,
}

impl SpotDispatchInfo<'_> {
    pub(crate) fn new(
        event: SpotDispatchEvent,
        node_cookie: usize,
        actor_for_recv: Option<ActorRef>,
    ) -> Self {
        Self {
            event,
            node_cookie,
            actor_for_recv,
            _marker: PhantomData,
        }
    }

    pub fn recv_actor(&self, out: &mut ActorReceived, flags: RecvFlags) -> Result<bool, RecvError> {
        crate::service::spot_dispatch_info_recv_actor(self, out, flags)
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

#[derive(Debug, Clone)]
pub struct SpotNodePeerEntry {
    pub channel_name: String,
    pub local_endpoint: String,
    pub peer_endpoint: String,
    pub source: SpotPeerSource,
    pub kind: SpotPeerKind,
    pub state: SpotPeerState,
    pub weight: u32,
    pub connected_since_ms: u64,
    pub last_changed_ms: u64,
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
    pub subject_kind: SubjectKind,
    pub ready_peer_count: u32,
    pub active_peer_count: u32,
    pub last_changed_ms: u64,
}

#[derive(Debug, Clone)]
pub struct SpotNodeSubjectFilter {
    pub role: Option<SpotRole>,
    pub subject: Option<String>,
    pub subject_kind: Option<SubjectKind>,
}

#[derive(Debug, Clone)]
pub struct SpotNodeSocketFilter {
    pub owner: Option<SpotNodeSocketOwner>,
    pub socket_type: Option<SocketType>,
    pub socket_name: Option<String>,
}

#[derive(Debug, Clone)]
pub struct SpotNodeSocketEntry {
    pub owner: SpotNodeSocketOwner,
    pub owner_id: u64,
    pub owner_name: String,
    pub socket_name: String,
    pub socket_type: SocketType,
    pub auto_hwm_visible: bool,
    pub snapshot: MonitorStatus,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SpotNodeSpotEntry {
    pub spot_rid: RoutingId,
    pub spot_kind: SpotKind,
    pub dispatch_handler_attached: bool,
    pub joined_actor_count: u32,
    pub pending_actor_join_count: u32,
    pub route_synced: bool,
    pub last_changed_ms: u64,
}
