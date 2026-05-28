use crate::error::RequestResult;
use crate::message::Message;
use crate::routing_id::RoutingId;
use crate::spot_models::SpotKind;

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
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ActorRoute {
    pub actor: ActorRef,
    pub current_spot_rid: RoutingId,
    pub current_spot_kind: SpotKind,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SpotRoute {
    pub spot_rid: RoutingId,
    pub owner_node_rid: RoutingId,
    pub spot_kind: SpotKind,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ActorRecvInfo {
    pub actor: ActorRef,
    pub source_node_rid: RoutingId,
    pub source_session_rid: RoutingId,
    pub flags: u32,
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
    pub(crate) request_cookie: usize,
}

pub struct ActorJoinRequest {
    pub info: ActorJoinInfo,
    pub message: Message,
}

#[derive(Debug, Clone)]
pub struct ActorJoinResult {
    pub result: RequestResult,
    pub join_result_code: i32,
    pub actor: ActorRef,
    pub joined_spot_rid: RoutingId,
    pub join_epoch: u64,
    pub flags: u32,
}

#[derive(Debug, Clone)]
pub struct ActorJoinEntrySpotResult {
    pub result: RequestResult,
    pub actor: ActorRef,
    pub target_node_rid: RoutingId,
    pub join_epoch: u64,
    pub flags: u32,
}

#[derive(Debug, Clone)]
pub struct ActorLookupResult {
    pub result: RequestResult,
    pub actor: ActorRef,
    pub flags: u32,
}

#[derive(Debug, Clone)]
pub struct SpotActorLifecycleInfo {
    pub previous_actor: ActorRef,
    pub current_actor: ActorRef,
    pub previous_spot_rid: Option<RoutingId>,
    pub current_spot_rid: Option<RoutingId>,
    pub join_epoch: u64,
    pub flags: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpotActorLifecycleEventKind {
    Joined,
    Left,
}

#[derive(Debug, Clone)]
pub struct SpotActorLifecycleEvent {
    pub kind: SpotActorLifecycleEventKind,
    pub info: SpotActorLifecycleInfo,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SpotNodeActorEntry {
    pub actor: ActorRef,
    pub current_spot_rid: RoutingId,
    pub current_spot_kind: SpotKind,
    pub route_synced: bool,
    pub pending_message_count: u32,
    pub last_changed_ms: u64,
}
