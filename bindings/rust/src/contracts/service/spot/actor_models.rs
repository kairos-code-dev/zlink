use crate::error::RequestResult;
use crate::message::Message;
use crate::routing_id::RoutingId;
use crate::spot_models::SpotKind;

/// A reference to an actor: the node that hosts it, its id, and its generation.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ActorRef {
    /// The routing id of the node that hosts the actor.
    pub node_rid: RoutingId,
    /// The actor's id.
    pub actor_id: String,
    /// The actor's generation; 0 means an unchecked reference.
    pub generation: u64,
}

impl ActorRef {
    /// Returns `true` when this reference omits generation checking (generation
    /// 0).
    pub fn is_unchecked(&self) -> bool {
        self.generation == 0
    }
}

/// The resolved route to an actor: which spot it currently lives on.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ActorRoute {
    /// The actor.
    pub actor: ActorRef,
    /// The routing id of the spot the actor is currently on.
    pub current_spot_rid: RoutingId,
    /// The kind of the current spot.
    pub current_spot_kind: SpotKind,
}

/// The resolved route to a spot: the spot, its owning node, and its kind.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SpotRoute {
    /// The spot's routing id.
    pub spot_rid: RoutingId,
    /// The routing id of the node that owns the spot.
    pub owner_node_rid: RoutingId,
    /// The kind of spot.
    pub spot_kind: SpotKind,
}

/// Metadata about a message received for an actor.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ActorRecvInfo {
    /// The actor the message was received for.
    pub actor: ActorRef,
    /// The routing id of the source node.
    pub source_node_rid: RoutingId,
    /// The routing id of the source session.
    pub source_session_rid: RoutingId,
    /// Implementation-defined receive flags.
    pub flags: u32,
}

/// Details of an actor-join request: the actors and spots on each side.
#[derive(Debug, Clone)]
pub struct ActorJoinInfo {
    /// The actor this request concerns.
    pub actor: ActorRef,
    /// The actor requesting the join.
    pub source_actor: ActorRef,
    /// The actor being joined.
    pub target_actor: ActorRef,
    /// The routing id of the source node.
    pub source_node_rid: RoutingId,
    /// The routing id of the source spot, if any.
    pub source_spot_rid: Option<RoutingId>,
    /// The routing id of the target node, if any.
    pub target_node_rid: Option<RoutingId>,
    /// The routing id of the target spot, if any.
    pub target_spot_rid: Option<RoutingId>,
    /// The join epoch (generation) of this join.
    pub join_epoch: u64,
    /// Implementation-defined join flags.
    pub flags: u32,
    pub(crate) request_cookie: usize,
}

/// A pending request from an actor to join a spot, awaiting a reply.
pub struct ActorJoinRequest {
    /// Details of the join request.
    pub info: ActorJoinInfo,
    /// The request payload, owned by this request.
    pub message: Message,
}

/// The outcome of an actor join.
#[derive(Debug, Clone)]
pub struct ActorJoinResult {
    /// The outcome of the join request.
    pub result: RequestResult,
    /// The application-supplied join result code.
    pub join_result_code: i32,
    /// The joined actor.
    pub actor: ActorRef,
    /// The routing id of the spot that was joined.
    pub joined_spot_rid: RoutingId,
    /// The join epoch (generation) assigned.
    pub join_epoch: u64,
    /// Implementation-defined join flags.
    pub flags: u32,
}

/// The outcome of an actor join routed through an entry spot.
#[derive(Debug, Clone)]
pub struct ActorJoinEntrySpotResult {
    /// The outcome of the join request.
    pub result: RequestResult,
    /// The joined actor.
    pub actor: ActorRef,
    /// The routing id of the node the actor was routed to.
    pub target_node_rid: RoutingId,
    /// The join epoch (generation) assigned.
    pub join_epoch: u64,
    /// Implementation-defined join flags.
    pub flags: u32,
}

/// The outcome of an actor lookup.
#[derive(Debug, Clone)]
pub struct ActorLookupResult {
    /// The outcome of the lookup.
    pub result: RequestResult,
    /// The actor that was found.
    pub actor: ActorRef,
    /// Implementation-defined lookup flags.
    pub flags: u32,
}

/// Details of an actor lifecycle change, before and after.
#[derive(Debug, Clone)]
pub struct SpotActorLifecycleInfo {
    /// The actor before the change.
    pub previous_actor: ActorRef,
    /// The actor after the change.
    pub current_actor: ActorRef,
    /// The routing id of the spot before the change, if any.
    pub previous_spot_rid: Option<RoutingId>,
    /// The routing id of the spot after the change, if any.
    pub current_spot_rid: Option<RoutingId>,
    /// The join epoch (generation) involved.
    pub join_epoch: u64,
    /// Implementation-defined lifecycle flags.
    pub flags: u32,
}

/// Whether an actor joined or left a spot.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotActorLifecycleEventKind {
    /// The actor joined a spot.
    Joined,
    /// The actor left a spot.
    Left,
}

/// An actor join/leave lifecycle event observed on a spot.
#[derive(Debug, Clone)]
pub struct SpotActorLifecycleEvent {
    /// Whether the actor joined or left.
    pub kind: SpotActorLifecycleEventKind,
    /// Details of the actor and spots involved.
    pub info: SpotActorLifecycleInfo,
}

/// One actor hosted on a spot node and its current placement.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SpotNodeActorEntry {
    /// The actor.
    pub actor: ActorRef,
    /// The routing id of the spot the actor is currently on.
    pub current_spot_rid: RoutingId,
    /// The kind of the current spot.
    pub current_spot_kind: SpotKind,
    /// Whether the actor's route is synced across peers.
    pub route_synced: bool,
    /// The number of messages queued for the actor.
    pub pending_message_count: u32,
    /// When the actor last changed, in milliseconds.
    pub last_changed_ms: u64,
}
