# SPDX-License-Identifier: MPL-2.0

from ...core.routing_id import RoutingId
from .actor import Actor
from .spot_models import ActorRef
from .spot_node import SpotNode

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


class _ClosableContract(metaclass=_SpotContractMeta):
    def close(self, *args, **kwargs): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


def remote_actor_ref(target_node_rid, actor_id):
    return ActorRef(
        node_rid=RoutingId(target_node_rid),
        actor_id=str(actor_id),
        generation=0,
    )


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


def __getattr__(name):
    if name == "Actor":
        from .actor import Actor as value
    elif name == "SpotNode":
        from .spot_node import SpotNode as value
    elif name in {
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
    }:
        from . import spot_models

        value = getattr(spot_models, name)
    elif name in {
        "ActorBindOp",
        "ActorDestroyOp",
        "ActorJoinCallbackOp",
        "ActorJoinEntrySpotOp",
        "ActorJoinOp",
        "ActorJoinReplyOp",
        "ActorLeaveOp",
        "ActorLookupOp",
        "ActorUnbindOp",
        "ReplyOp",
        "RequestCallbackOp",
        "RequestOp",
        "SendOp",
    }:
        from . import spot_operations

        value = getattr(spot_operations, name)
    else:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    globals()[name] = value
    return value
