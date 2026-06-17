# SPDX-License-Identifier: MPL-2.0

import errno
from dataclasses import dataclass

from ...core.routing_id import RoutingId
from ...errors import RecvError
from ...eventing.monitor import MonitorStatus
from ...messaging.message import Message
from ...sockets.codes import RecvResult, SocketType
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
    """The event and context passed to an on-dispatch-event callback."""
    event: SpotDispatchEvent
    subject_kind: SpotDispatchSubjectKind
    timer: object | None = None
    channel_dealer: object | None = None
    actor: "ActorRef | None" = None

    def recv_actor_part(self, *, flags=0):
        err = getattr(errno, "ENOTSUP", getattr(errno, "EOPNOTSUPP", 95))
        raise RecvError(RecvResult.NOT_SUPPORTED, err)


@dataclass(frozen=True)
class ActorRef:
    """A reference to an actor: the node hosting it, its id, and its generation."""
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
    """The resolved route to an actor: which spot it currently lives on."""
    actor: ActorRef
    current_spot_rid: RoutingId
    current_spot_kind: SpotKind


@dataclass(frozen=True)
class ActorRecvInfo:
    """Metadata about a message received for an actor."""
    actor: ActorRef
    source_node_rid: RoutingId
    source_session_rid: RoutingId
    flags: int


@dataclass(frozen=True)
class ActorJoinInfo:
    """Details of an actor-join request: the actors and spots on each side."""
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
    """A pending request from an actor to join a spot, awaiting a reply."""
    info: ActorJoinInfo
    message: Message

    def __iter__(self):
        yield self.info
        yield self.message


@dataclass(frozen=True)
class ActorJoinResult:
    """The outcome of an actor join."""
    result: object
    join_result_code: int
    actor: ActorRef
    joined_spot_rid: RoutingId
    join_epoch: int
    flags: int


@dataclass(frozen=True)
class ActorJoinEntrySpotResult:
    """The outcome of an actor join routed through an entry spot."""
    result: object
    join_result_code: int
    actor: ActorRef
    target_node_rid: RoutingId
    joined_spot_rid: RoutingId
    join_epoch: int
    flags: int


@dataclass(frozen=True)
class ActorLookupResult:
    """The outcome of an actor lookup."""
    result: object
    actor: ActorRef
    flags: int


@dataclass(frozen=True)
class SpotActorLifecycleInfo:
    """Details of an actor lifecycle change, before and after."""
    previous_actor: ActorRef
    current_actor: ActorRef
    previous_spot_rid: RoutingId | None
    current_spot_rid: RoutingId | None
    join_epoch: int
    flags: int


@dataclass(frozen=True)
class SpotActorLifecycleEvent:
    """An actor join/leave lifecycle event observed on a spot."""
    kind: SpotActorLifecycleEventKind
    info: SpotActorLifecycleInfo


@dataclass(frozen=True)
class SpotNodeSpotEntry:
    """One spot hosted on a spot node and its actor counts."""
    spot_rid: RoutingId
    spot_kind: SpotKind
    dispatch_handler_attached: bool
    joined_actor_count: int
    pending_actor_join_count: int
    route_synced: bool
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodeActorEntry:
    """One actor hosted on a spot node and its current placement."""
    actor: ActorRef
    current_spot_rid: RoutingId
    current_spot_kind: SpotKind
    route_synced: bool
    pending_message_count: int
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodeStatus:
    """A snapshot of a spot node's status and peer/subject counts."""
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
    """One peer of a spot node and its connection details."""
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
    """Filter for a spot node peer query; unset fields match anything."""
    peer_endpoint: str | None = None
    source: SpotPeerSource | None = None
    state: SpotPeerState | None = None


@dataclass(frozen=True)
class SpotNodeSubjectEntry:
    """One subject (topic or pattern) served by a spot node."""
    role: SpotRole
    subject: str
    subject_kind: SubjectKind
    ready_peer_count: int
    active_peer_count: int
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodeSubjectFilter:
    """Filter for a spot node subject query; unset fields match anything."""
    role: SpotRole | None = None
    subject: str | None = None
    subject_kind: SubjectKind | None = None


@dataclass(frozen=True)
class SpotNodeSocketFilter:
    """Filter for a spot node socket query; unset fields match anything."""
    owner: SpotNodeSocketOwner | None = None
    socket_type: SocketType | None = None
    socket_name: str | None = None


@dataclass(frozen=True)
class SpotNodeSocketEntry:
    """One socket owned within a spot node and its monitored status."""
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
