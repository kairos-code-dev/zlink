# SPDX-License-Identifier: MPL-2.0

from dataclasses import dataclass, field

from ..core.options import AutoHwmProfile
from ..core.routing_id import RoutingId
from ..eventing.monitor import MonitorStatus
from ..messaging.messages import Message
from ..sockets.codes import SocketType
from .codes import (
    ServiceKind,
    SpotActorLifecycleEventKind,
    SpotDispatchEvent,
    SpotDispatchSubjectKind,
    SpotKind,
    SpotNodeMode,
    SpotNodeOption,
    SpotNodeSocketOwner,
    SpotNodeState,
    SpotPeerKind,
    SpotPeerSource,
    SpotPeerState,
    SpotRole,
    SubjectKind,
)

_spot_node_factory = None
_spot_factory = None
_implementation_spot_types = {}


def register_spot_factories(*, spot_node_factory, spot_factory):
    global _spot_node_factory
    global _spot_factory
    _spot_node_factory = spot_node_factory
    _spot_factory = spot_factory


def _require(factory, name):
    if factory is None:
        raise RuntimeError(f"zlink {name} runtime is not registered")
    return factory


def register_spot_implementation_types(**implementation_types):
    _implementation_spot_types.update(implementation_types)


class _SpotContractMeta(type):
    def __instancecheck__(cls, instance):
        implementation_type = _implementation_spot_types.get(cls.__name__)
        if implementation_type is not None and isinstance(instance, implementation_type):
            return True
        return type.__instancecheck__(cls, instance)


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


def remote_actor_ref(target_node_rid, actor_id):
    return ActorRef(node_rid=RoutingId(target_node_rid), actor_id=str(actor_id), generation=0)


class _ClosableContract(metaclass=_SpotContractMeta):
    def close(self, *args, **kwargs): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


class Actor(_ClosableContract):
    def ref(self): ...

    @property
    def actor_ref(self): ...

    def join(self, spot): ...

    def leave(self, spot): ...

    def recv_part(self, *, flags=0): ...

    def send_bound_session(self): ...

    def close_bound_session(self, *, timeout=0): ...


class SpotNode(_ClosableContract):
    def __new__(cls, ctx, mode: int | SpotNodeMode | None = None):
        if cls is SpotNode:
            return _require(_spot_node_factory, "spot node")(ctx, mode)
        return object.__new__(cls)

    def set_pub_bind(self, endpoint: str): ...
    def set_router_bind(self, endpoint: str): ...
    def last_endpoint(self) -> str: ...
    def connect_peer(self, endpoint: str): ...
    def disconnect_peer(self, endpoint: str): ...
    def disconnect_peer_rid(self, target_node_rid): ...
    def connect_router_channel_peer(self, channel_name: str, endpoint: str): ...
    def disconnect_router_channel_peer(self, channel_name: str, endpoint: str): ...
    def disconnect_router_channel_peer_rid(self, channel_name: str, peer_rid): ...
    def attach_discovery(self, discovery): ...
    def attach_spot_route_channel_discovery(self, channel_name: str, discovery): ...
    def attach_channel_dealer(self, discovery, dealer): ...
    def attach_channel_dealer_manual(self, channel_name, dealer): ...
    def attach_pub_ingress(self, pub): ...
    def set_router_hwm(self, value: int): ...
    def set_pubsub_hwm(self, value: int): ...
    def set_router_hwm_profile(self, value: int | AutoHwmProfile): ...
    def set_pubsub_hwm_profile(self, value: int | AutoHwmProfile): ...

    @property
    def router_hwm_profile(self): ...
    @router_hwm_profile.setter
    def router_hwm_profile(self, value): ...
    @property
    def router_hwm(self): ...
    @router_hwm.setter
    def router_hwm(self, value): ...
    @property
    def pubsub_hwm_profile(self): ...
    @pubsub_hwm_profile.setter
    def pubsub_hwm_profile(self, value): ...
    @property
    def pubsub_hwm(self): ...
    @pubsub_hwm.setter
    def pubsub_hwm(self, value): ...
    @property
    def dispatch_workers_min(self): ...
    @dispatch_workers_min.setter
    def dispatch_workers_min(self, value: int): ...
    @property
    def dispatch_workers_max(self): ...
    @dispatch_workers_max.setter
    def dispatch_workers_max(self, value: int): ...

    def create_spot(self): ...
    def entry_spot(self): ...
    def spot_lookup(self, spot_rid): ...
    def get_or_create_spot(self, spot_rid): ...
    def actor(self, actor_id): ...
    def actor_lookup(self, actor_id): ...
    def destroy_actor(self, actor_ref): ...
    def remote_actor_get_ref(self, target_node_rid, actor_id): ...
    def join_actor(self, actor_ref, dest_node_rid, dest_spot_rid): ...
    def join_actor_entry_spot(self, actor_ref, dest_node_rid): ...
    def leave_actor(self, actor_ref, current_spot_rid): ...
    def send_bound_session_msg(self, actor_ref): ...
    def spots(self): ...
    def actors(self): ...
    def set_routing_id(self, routing_id): ...
    def get_routing_id(self): ...
    @property
    def routing_id(self): ...
    def set_tls_server(self, cert: str, key: str, require_client_cert: bool = False): ...
    def set_tls_client(
        self, ca_cert: str | None, hostname: str | None, trust_system: bool = False
    ): ...
    def status(self): ...
    def peers(self): ...
    def peers_query(self, filter_=None): ...
    def subjects(self, filter_=None): ...
    def internal_sockets(self, filter_=None): ...


class _FluentMessageOp(metaclass=_SpotContractMeta):
    def message(self, payload): ...
    def messages(self, *payloads): ...
    def flags(self, flags): ...


class SendOp(_FluentMessageOp):
    def submit(self): ...


class RequestOp(_FluentMessageOp):
    def timeout(self, timeout): ...
    def submit_async(self): ...
    def submit(self, callback): ...


class RequestCallbackOp(_FluentMessageOp):
    def timeout(self, timeout): ...
    def submit(self, callback): ...


class ReplyOp(_FluentMessageOp):
    def submit(self): ...


class ActorJoinOp(_FluentMessageOp):
    def timeout(self, timeout): ...
    def submit_async(self): ...
    def submit(self, callback): ...


class ActorJoinCallbackOp(_FluentMessageOp):
    def timeout(self, timeout): ...
    def submit(self, callback): ...


class ActorJoinEntrySpotOp(metaclass=_SpotContractMeta):
    def timeout(self, timeout): ...
    def submit_async(self): ...
    def submit(self, callback): ...


class ActorJoinReplyOp(_FluentMessageOp):
    def submit(self): ...


class ActorLeaveOp(metaclass=_SpotContractMeta):
    def timeout(self, timeout): ...
    def submit_async(self): ...
    def submit(self, callback): ...


class ActorDestroyOp(ActorLeaveOp): ...


class ActorLookupOp(metaclass=_SpotContractMeta):
    def timeout(self, timeout): ...
    def submit_async(self): ...
    def submit(self, callback): ...


class ActorBindOp(ActorLeaveOp): ...


class ActorUnbindOp(ActorLeaveOp): ...


class Spot(_ClosableContract):
    def __new__(cls, node):
        if cls is Spot:
            raise TypeError("Spot() is internal; use SpotNode.create_spot()")
        return object.__new__(cls)

    def set_routing_id(self, routing_id): ...
    def get_routing_id(self): ...
    @property
    def routing_id(self): ...
    def publish(self, topic): ...
    def send_to_channel(self, channel_name): ...
    def send_to_spot(self, dest_node_rid, dest_spot_rid): ...
    def request_to_channel(self, channel_name): ...
    def request_to_spot(self, dest_node_rid, dest_spot_rid): ...
    def request_to_router(self, peer_rid): ...
    def subscribe_into(self, topic_message, *, flags=0): ...
    def receive_subscription_event_into(self, event, *, flags=0): ...
    def set_subscription(self, topic): ...
    def unset_subscription(self, topic): ...
    def on_send_ready(self, handler): ...
    def reply_to_spot(self, dest_node_rid, dest_spot_rid, request_seq): ...
    def reply_to_router(self, peer_rid, request_seq): ...
    def recv_routed(self, *, flags=0): ...
    def recv_routed_into(self, received, *, flags=0): ...
    def on_dispatch_event(self, handler): ...
    def recv_actor_join(self, *, flags=0): ...
    def recv_actor_lifecycle(self, *, flags=0): ...
    def reply_actor_join(self, request, join_result_code): ...
    def actors(self): ...
