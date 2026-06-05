# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

from ...core.routing_id import RoutingId
from .actor import Actor
from .spot_models import ActorRef
from .spot_node import SpotNode

_spot_node_factory = None
_spot_factory = None


def _register_spot_factories(*, spot_node_factory, spot_factory):
    global _spot_node_factory
    global _spot_factory
    _spot_node_factory = spot_node_factory
    _spot_factory = spot_factory


def _require(factory, name):
    if factory is None:
        raise RuntimeError(f"zlink {name} runtime is not registered")
    return factory


def create_spot_node(ctx, mode=None):
    """Create a spot node on ``ctx`` in ``mode`` (or the default). The caller
    owns it."""
    return _require(_spot_node_factory, "spot node")(ctx, mode)


def create_spot(node):
    """Create a spot on ``node``. The caller owns it."""
    return _require(_spot_factory, "spot")(node)


@runtime_checkable
class _ClosableContract(Protocol):
    """Base for closable resources that support the context-manager protocol."""

    def close(self, *args, **kwargs):
        """Close the resource and release its native resources."""
        ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


def remote_actor_ref(target_node_rid, actor_id):
    """Build a reference to a remote actor ``actor_id`` on ``target_node_rid``
    without contacting it (an unchecked, generation-0 reference)."""
    return ActorRef(
        node_rid=RoutingId(target_node_rid),
        actor_id=str(actor_id),
        generation=0,
    )


@runtime_checkable
class Spot(_ClosableContract, Protocol):
    """A spot: a multi-role messaging endpoint that can publish, subscribe,
    route, request, reply, and host actors.

    Each ``publish``/``send_*``/``request_*``/``reply_*`` returns a builder whose
    parts are consumed on a successful submit (see :class:`SendOp`).
    """

    def set_routing_id(self, routing_id):
        """Set the routing id that identifies this spot to its peers."""
        ...
    def get_routing_id(self):
        """Return the routing id that identifies this spot to its peers."""
        ...
    @property
    def routing_id(self):
        """The routing id that identifies this spot to its peers."""
        ...
    def publish(self, topic):
        """Begin publishing under ``topic``."""
        ...
    def send_to_channel(self, channel_name):
        """Begin a send addressed to the channel ``channel_name``."""
        ...
    def send_to_spot(self, dest_node_rid, dest_spot_rid):
        """Begin a send addressed to a spot on another node."""
        ...
    def request_to_channel(self, channel_name):
        """Begin a request to the channel ``channel_name`` and await a reply."""
        ...
    def request_to_spot(self, dest_node_rid, dest_spot_rid):
        """Begin a request to a spot on another node and await a reply."""
        ...
    def request_to_router(self, peer_rid):
        """Begin a request to a ROUTER peer ``peer_rid`` and await a reply."""
        ...
    def subscribe_into(self, topic_message, *, flags=0):
        """Receive the next matching topic message into ``topic_message``;
        ``False`` when ``DONT_WAIT`` is set and none is available."""
        ...
    def receive_subscription_event_into(self, event, *, flags=0):
        """Receive the next subscriber (un)subscription event into ``event``;
        ``False`` when ``DONT_WAIT`` is set and none is available."""
        ...
    def set_subscription(self, topic):
        """Add a subscription for ``topic``; subscriptions accumulate."""
        ...
    def unset_subscription(self, topic):
        """Remove a subscription previously added for ``topic``."""
        ...
    def on_send_ready(self, handler):
        """Register ``handler``, invoked when the spot can accept more sends
        after back-pressure, on a background dispatch thread."""
        ...
    def reply_to_spot(self, dest_node_rid, dest_spot_rid, request_seq):
        """Begin a reply to the spot request ``request_seq``."""
        ...
    def reply_to_router(self, peer_rid, request_seq):
        """Begin a reply to a ROUTER peer's request ``request_seq``."""
        ...
    def recv_routed(self, *, flags=0):
        """Receive the next routed message, or ``None`` when ``DONT_WAIT`` is set
        and none is available."""
        ...
    def recv_routed_into(self, received, *, flags=0):
        """Receive the next routed message into ``received`` storage; ``False``
        when ``DONT_WAIT`` is set and none is available."""
        ...
    def on_dispatch_event(self, handler):
        """Register ``handler``, invoked for each spot dispatch event on a
        background dispatch thread."""
        ...
    def recv_actor_join(self, *, flags=0):
        """Receive the next pending actor-join request, or ``None`` when none is
        available."""
        ...
    def recv_actor_lifecycle(self, *, flags=0):
        """Receive the next actor lifecycle event, or ``None`` when none is
        available."""
        ...
    def reply_actor_join(self, request, join_result_code):
        """Begin a reply to ``request`` carrying ``join_result_code``."""
        ...
    def actors(self):
        """Return the actors currently hosted on this spot."""
        ...


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
