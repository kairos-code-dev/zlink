# SPDX-License-Identifier: MPL-2.0

from dataclasses import dataclass

from ...core.routing_id import RoutingId
from ...eventing.monitor import MonitorStatus
from ...messaging.message import Message
from ...sockets.codes import SocketType
from ..codes import (
    SpotActorLifecycleEventKind,
    SpotDispatchEvent,
    SpotDispatchSubjectKind,
    SpotKind,
    SpotNodeSocketOwner,
    SpotNodeState,
    SpotPeerKind,
    SpotPeerSource,
    SpotPeerState,
    SpotRole,
    SubjectKind,
)


@dataclass(frozen=True)
class SpotDispatchInfo:
    event: SpotDispatchEvent
    subject_kind: SpotDispatchSubjectKind
    timer: object | None = None
    channel_dealer: object | None = None
    actor: "ActorRef | None" = None

    def recv_actor_part(self, *, flags=0): ...


@dataclass(frozen=True)
class ActorRef:
    node_rid: RoutingId
    actor_id: str
    generation: int

    @property
    def unchecked(self):
        return self.generation == 0

    def is_unchecked(self):
        return self.unchecked


@dataclass(frozen=True)
class ActorRoute:
    actor: ActorRef
    current_spot_rid: RoutingId
    current_spot_kind: SpotKind


@dataclass(frozen=True)
class ActorRecvInfo:
    actor: ActorRef
    source_node_rid: RoutingId
    source_session_rid: RoutingId
    flags: int


@dataclass(frozen=True)
class ActorJoinInfo:
    source_actor: ActorRef
    target_actor: ActorRef
    source_node_rid: RoutingId
    source_spot_rid: RoutingId
    target_node_rid: RoutingId
    target_spot_rid: RoutingId
    join_epoch: int
    flags: int


@dataclass(frozen=True)
class ActorJoinRequest:
    info: ActorJoinInfo
    message: Message

    def __iter__(self):
        yield self.info
        yield self.message


@dataclass(frozen=True)
class ActorJoinResult:
    result: object
    join_result_code: int
    actor: ActorRef
    joined_spot_rid: RoutingId
    join_epoch: int
    flags: int


@dataclass(frozen=True)
class ActorJoinEntrySpotResult:
    result: object
    actor: ActorRef
    target_node_rid: RoutingId
    join_epoch: int
    flags: int


@dataclass(frozen=True)
class ActorLookupResult:
    result: object
    actor: ActorRef
    flags: int


@dataclass(frozen=True)
class SpotActorLifecycleInfo:
    previous_actor: ActorRef
    current_actor: ActorRef
    previous_spot_rid: RoutingId | None
    current_spot_rid: RoutingId | None
    join_epoch: int
    flags: int


@dataclass(frozen=True)
class SpotActorLifecycleEvent:
    kind: SpotActorLifecycleEventKind
    info: SpotActorLifecycleInfo


@dataclass(frozen=True)
class SpotNodeSpotEntry:
    spot_rid: RoutingId
    spot_kind: SpotKind
    dispatch_handler_attached: bool
    joined_actor_count: int
    pending_actor_join_count: int
    route_synced: bool
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodeActorEntry:
    actor: ActorRef
    current_spot_rid: RoutingId
    current_spot_kind: SpotKind
    route_synced: bool
    pending_message_count: int
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodeStatus:
    channel_name: str
    local_endpoint: str
    node_routing_id: RoutingId
    state: SpotNodeState
    configured_peer_count: int
    active_peer_count: int
    connected_peer_count: int
    subject_count: int
    ready_subject_count: int
    disconnected_sub_target_count: int
    disconnected_routed_target_count: int
    last_error: int
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodePeerEntry:
    channel_name: str
    local_endpoint: str
    peer_endpoint: str
    source: SpotPeerSource
    kind: SpotPeerKind
    state: SpotPeerState
    weight: int
    connected_since_ms: int
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodePeerFilter:
    peer_endpoint: str | None = None
    source: SpotPeerSource | None = None
    state: SpotPeerState | None = None


@dataclass(frozen=True)
class SpotNodeSubjectEntry:
    role: SpotRole
    subject: str
    subject_kind: SubjectKind
    ready_peer_count: int
    active_peer_count: int
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodeSubjectFilter:
    role: SpotRole | None = None
    subject: str | None = None
    subject_kind: SubjectKind | None = None


@dataclass(frozen=True)
class SpotNodeSocketFilter:
    owner: SpotNodeSocketOwner | None = None
    socket_type: SocketType | None = None
    socket_name: str | None = None


@dataclass(frozen=True)
class SpotNodeSocketEntry:
    owner: SpotNodeSocketOwner
    owner_id: int
    owner_name: str
    socket_name: str
    socket_type: SocketType
    auto_hwm_visible: bool
    snapshot: MonitorStatus


__all__ = [
    "ActorJoinEntrySpotResult",
    "ActorJoinInfo",
    "ActorJoinRequest",
    "ActorJoinResult",
    "ActorLookupResult",
    "ActorRecvInfo",
    "ActorRef",
    "ActorRoute",
    "SpotActorLifecycleEvent",
    "SpotActorLifecycleInfo",
    "SpotDispatchInfo",
    "SpotNodeActorEntry",
    "SpotNodePeerEntry",
    "SpotNodePeerFilter",
    "SpotNodeSocketEntry",
    "SpotNodeSocketFilter",
    "SpotNodeSpotEntry",
    "SpotNodeStatus",
    "SpotNodeSubjectEntry",
    "SpotNodeSubjectFilter",
]
