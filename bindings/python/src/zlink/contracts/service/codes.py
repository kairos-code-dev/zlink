# SPDX-License-Identifier: MPL-2.0

from enum import IntEnum, IntFlag

class SpotNodeMode(IntEnum):
    """Which messaging patterns a spot node enables (pub/sub, routed, or all)."""
    PUBSUB = 1
    ROUTED = 2
    ALL = 3

class SpotNodeSocketOwner(IntEnum):
    """Which component owns a spot node socket."""
    ANY = 0
    NODE = 1
    SPOT = 2

class SpotNodeState(IntEnum):
    """The overall readiness state of a spot node."""
    IDLE = 1
    CONNECTING = 2
    PARTIAL_READY = 3
    READY = 4
    ERROR = 5

class SpotPeerSource(IntEnum):
    """How a spot peer became known to the node."""
    MANUAL = 1
    DISCOVERY = 2
    MIXED = 3

class SpotPeerKind(IntEnum):
    """The connection style of a spot peer."""
    SPOT_MESH = 1
    ROUTER_CHANNEL = 2

class SpotPeerState(IntEnum):
    """The connection state of a spot peer."""
    CONFIGURED = 1
    CONNECTING = 2
    CONNECTED = 3

class SpotKind(IntEnum):
    """The kind of a spot (entry or user)."""
    INVALID = 0
    ENTRY = 1
    USER = 2

class SpotDispatchEvent(IntEnum):
    """The kind of readable event surfaced by a spot dispatch."""
    SUBSCRIBE_READABLE = 1
    ROUTED_READABLE = 2
    TIMER_READABLE = 3
    CHANNEL_REPLY_READABLE = 4
    ACTOR_READABLE = 5
    ACTOR_JOIN_READABLE = 6
    ACTOR_LIFECYCLE_READABLE = 7

class SpotActorLifecycleEventKind(IntEnum):
    """Whether an actor joined or left a spot."""
    JOINED = 1
    LEFT = 2

class SpotDispatchSubjectKind(IntEnum):
    """What kind of subject a spot dispatch event concerns."""
    SPOT = 1
    TIMER = 2
    CHANNEL_DEALER = 3
    ACTOR = 4

class SubjectKind(IntEnum):
    """The messaging pattern a subject belongs to."""
    NONE = 0
    TOPIC = 1
    PATTERN = 2

class SpotRole(IntEnum):
    """The pub/sub role of a spot subject."""
    PUB = 1
    SUB = 2

__all__ = [
    "SpotNodeMode",
    "SpotNodeSocketOwner",
    "SpotNodeState",
    "SpotPeerSource",
    "SpotPeerKind",
    "SpotPeerState",
    "SpotKind",
    "SpotDispatchEvent",
    "SpotDispatchSubjectKind",
    "SubjectKind",
    "SpotRole",
]
