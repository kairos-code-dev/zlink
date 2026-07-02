use crate::actor_models::ActorRef;
use crate::actor_received::ActorReceived;
use crate::error::RecvError;
use crate::flags::RecvFlags;
use crate::monitor_contracts::MonitorStatus;
use crate::routing_id::RoutingId;
use std::marker::PhantomData;

/// The kind of a spot.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotKind {
    /// No spot kind (unset).
    Invalid,
    /// An entry spot (a node's well-known entry point).
    Entry,
    /// A user-created spot.
    User,
}

/// The pub/sub role of a spot subject.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotRole {
    /// A publisher subject.
    Pub,
    /// A subscriber subject.
    Sub,
}

/// The overall readiness state of a spot node.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotNodeState {
    /// Not yet connecting to any peer.
    Idle,
    /// Establishing connections to peers.
    Connecting,
    /// Some but not all peers are connected.
    PartialReady,
    /// All expected peers are connected.
    Ready,
    /// The node is in an error state.
    Error,
}

/// Which messaging patterns a spot node enables.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotNodeMode {
    /// Publish/subscribe only.
    PubSub,
    /// Routed request/reply only.
    Routed,
    /// Both pub/sub and routed.
    All,
}

/// Identifies a socket's messaging pattern.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SocketType {
    /// Unspecified socket type.
    Any = 0,
    /// Exclusive one-to-one peering.
    Pair = 0x1001,
    /// Publishes topic-filtered messages.
    Pub = 0x1002,
    /// Subscribes to published topics.
    Sub = 0x1003,
    /// Load-balances across connected peers.
    Dealer = 0x1004,
    /// Routes by routing id.
    Router = 0x1005,
    /// Publisher that surfaces subscription events.
    XPub = 0x1006,
    /// Subscriber whose subscriptions are messages.
    XSub = 0x1007,
    /// Exchanges framed packets with raw TCP peers.
    Stream = 0x1008,
}

/// Which component owns a spot node socket.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotNodeSocketOwner {
    /// Any owner (no filter).
    Any,
    /// Owned by the node itself.
    Node,
    /// Owned by a spot.
    Spot,
}

/// Options for creating a spot node.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SpotNodeOptions {
    /// The messaging patterns to enable; `None` uses the default.
    pub mode: Option<SpotNodeMode>,
}

/// How a spot peer became known to the node.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotPeerSource {
    /// Added manually by the application.
    Manual,
    /// Learned from a discovery service.
    Discovery,
    /// Both manually added and discovered.
    Mixed,
}

/// The connection style of a spot peer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotPeerKind {
    /// A peer in the spot mesh.
    SpotMesh,
    /// A peer reached over a router channel.
    RouterChannel,
}

/// The connection state of a spot peer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotPeerState {
    /// Configured but not yet connecting.
    Configured,
    /// A connection is being established.
    Connecting,
    /// The peer is connected.
    Connected,
}

/// The messaging pattern a subject belongs to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SubjectKind {
    /// Unknown or unset.
    Unknown = 0,
    /// A publish/subscribe subject.
    PubSub = 1,
    /// A routed request/reply subject.
    Routed = 2,
}

/// The kind of readable event surfaced by a spot dispatch.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotDispatchEvent {
    /// A subscription has a message ready to read.
    SubscribeReadable,
    /// A routed message is ready to read.
    RoutedReadable,
    /// A timer has expired.
    TimerReadable,
    /// A channel reply is ready to read.
    ChannelReplyReadable,
    /// An actor message is ready to read.
    ActorReadable,
    /// An actor join request is ready to read.
    ActorJoinReadable,
    /// An actor lifecycle event is ready to read.
    ActorLifecycleReadable,
}

/// The event and context passed to an `on_dispatch_event` callback.
pub struct SpotDispatchInfo<'a> {
    /// The kind of readable event being dispatched.
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

    /// Receives the actor message that this dispatch event signaled into
    /// caller-provided `out` storage; `Ok(false)` when [`RecvFlags::DONT_WAIT`]
    /// is set and none is available.
    pub fn recv_actor(&self, out: &mut ActorReceived, flags: RecvFlags) -> Result<bool, RecvError> {
        crate::service::spot_dispatch_info_recv_actor(self, out, flags)
    }
}

/// A snapshot of a spot node's status and peer/subject counts.
#[derive(Debug, Clone)]
pub struct SpotNodeStatus {
    /// The logical channel name.
    pub channel_name: String,
    /// The node's local transport endpoint.
    pub local_endpoint: String,
    /// The node's routing id.
    pub node_routing_id: RoutingId,
    /// The node's overall readiness state.
    pub state: SpotNodeState,
    /// The number of configured peers.
    pub configured_peer_count: u32,
    /// The number of active peers.
    pub active_peer_count: u32,
    /// The number of connected peers.
    pub connected_peer_count: u32,
    /// The total number of subjects.
    pub subject_count: u32,
    /// The number of ready subjects.
    pub ready_subject_count: u32,
    /// The number of disconnected subscription targets.
    pub disconnected_sub_target_count: u32,
    /// The number of disconnected routed targets.
    pub disconnected_routed_target_count: u32,
    /// The last native error code, or 0 for none.
    pub last_error: i32,
    /// When the status last changed, in milliseconds.
    pub last_changed_ms: u64,
}

/// One peer of a spot node and its connection details.
#[derive(Debug, Clone)]
pub struct SpotNodePeerEntry {
    /// The logical channel name.
    pub channel_name: String,
    /// The node's local transport endpoint.
    pub local_endpoint: String,
    /// The peer's transport endpoint.
    pub peer_endpoint: String,
    /// How the peer became known.
    pub source: SpotPeerSource,
    /// The peer's connection style.
    pub kind: SpotPeerKind,
    /// The peer's connection state.
    pub state: SpotPeerState,
    /// The peer's load-balancing weight.
    pub weight: u32,
    /// When the peer connected, in milliseconds.
    pub connected_since_ms: u64,
    /// When the peer last changed state, in milliseconds.
    pub last_changed_ms: u64,
}

/// Filter for a spot node peer query; `None` fields match anything.
#[derive(Debug, Clone)]
pub struct SpotNodePeerFilter {
    /// Restrict to this peer endpoint.
    pub peer_endpoint: Option<String>,
    /// Restrict to peers from this source.
    pub source: Option<SpotPeerSource>,
    /// Restrict to peers in this state.
    pub state: Option<SpotPeerState>,
}

/// One subject (topic or pattern) served by a spot node.
#[derive(Debug, Clone)]
pub struct SpotNodeSubjectEntry {
    /// The subject's pub/sub role.
    pub role: SpotRole,
    /// The subject topic or pattern.
    pub subject: String,
    /// The subject's messaging pattern.
    pub subject_kind: SubjectKind,
    /// The number of peers ready on this subject.
    pub ready_peer_count: u32,
    /// The number of peers active on this subject.
    pub active_peer_count: u32,
    /// When the subject last changed, in milliseconds.
    pub last_changed_ms: u64,
}

/// Filter for a spot node subject query; `None` fields match anything.
#[derive(Debug, Clone)]
pub struct SpotNodeSubjectFilter {
    /// Restrict to subjects with this pub/sub role.
    pub role: Option<SpotRole>,
    /// Restrict to this subject topic or pattern.
    pub subject: Option<String>,
    /// Restrict to this subject pattern.
    pub subject_kind: Option<SubjectKind>,
}

/// Filter for a spot node socket query; `None` fields match anything.
#[derive(Debug, Clone)]
pub struct SpotNodeSocketFilter {
    /// Restrict to sockets owned by this component.
    pub owner: Option<SpotNodeSocketOwner>,
    /// Restrict to this socket type.
    pub socket_type: Option<SocketType>,
    /// Restrict to this socket name.
    pub socket_name: Option<String>,
}

/// One socket owned within a spot node and its monitored status.
#[derive(Debug, Clone)]
pub struct SpotNodeSocketEntry {
    /// Which component owns the socket.
    pub owner: SpotNodeSocketOwner,
    /// The identifier of the owning component.
    pub owner_id: u64,
    /// The name of the owning component.
    pub owner_name: String,
    /// The socket's name.
    pub socket_name: String,
    /// The socket's type.
    pub socket_type: SocketType,
    /// Whether the socket participates in automatic high-water-mark sizing.
    pub auto_hwm_visible: bool,
    /// The socket's monitored status snapshot.
    pub snapshot: MonitorStatus,
}

/// One spot hosted on a spot node and its actor counts.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SpotNodeSpotEntry {
    /// The spot's routing id.
    pub spot_rid: RoutingId,
    /// The kind of spot.
    pub spot_kind: SpotKind,
    /// Whether a dispatch handler is attached to the spot.
    pub dispatch_handler_attached: bool,
    /// The number of actors currently joined.
    pub joined_actor_count: u32,
    /// The number of pending actor joins.
    pub pending_actor_join_count: u32,
    /// Whether the spot's route is synced across peers.
    pub route_synced: bool,
    /// When the spot last changed, in milliseconds.
    pub last_changed_ms: u64,
}
