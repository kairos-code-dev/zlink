# SPDX-License-Identifier: MPL-2.0

from enum import IntEnum, IntFlag

class AutoConnectType(IntEnum):
    """How a discovery service automatically wires connections between peers (route mesh, client-server, dealer mesh, fanout, spot mesh)."""
    INVALID = 0
    ROUTE_MESH = 1
    CLIENT_SERVER = 2
    DEALER_MESH = 3
    FANOUT = 4
    SPOT_MESH = 5

class ServiceRole(IntEnum):
    """The messaging role a service plays in the topology."""
    INVALID = 0
    SPOT = 2
    ROUTER = 3
    DEALER = 4
    PUB = 5
    SUB = 6

class RegistrySocketRole(IntEnum):
    """The role a socket plays within a registry."""
    PUB = 1
    ROUTER = 2
    PEER_SUB = 3

class DiscoverySocketRole(IntEnum):
    """The role a socket plays within a discovery service."""
    SUB = 1

class SpotNodeSocketRole(IntEnum):
    """The role a socket plays within a spot node."""
    NODE = 0
    PUB = 1
    SUB = 2
    DEALER = 3

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

class SpotNodeOption(IntEnum):
    """Raw native spot-node option codes used by the runtime."""
    ROUTER_HWM_PROFILE = 0x360E
    ROUTER_HWM = 0x360F
    PUBSUB_HWM_PROFILE = 0x3610
    PUBSUB_HWM = 0x3611
    DISPATCH_WORKERS_MIN = 0x3612
    DISPATCH_WORKERS_MAX = 0x3613

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

class SpotSocketRole(IntEnum):
    """The role a socket plays within a spot."""
    PUB = 1
    SUB = 2

class SpotServiceAttachmentRole(IntEnum):
    """The role a service attachment plays on a spot."""
    ROUTER = 1
    PUB = 2
    SUB = 3

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

class RegistryState(IntEnum):
    """The operational state of a registry."""
    IDLE = 1
    ACTIVE = 2
    DEGRADED = 3
    ERROR = 4

class TopologySource(IntEnum):
    """Where a topology entry was learned from."""
    MANUAL = 1
    DISCOVERY = 2
    REGISTRY = 3

class TopologyState(IntEnum):
    """The lifecycle state of a topology connection."""
    DISCOVERED = 1
    CONNECTING = 2
    READY = 3
    LOST = 4
    ERROR = 5
    STOPPED = 6

class ServiceKind(IntEnum):
    """The kind of service a topology entry represents."""
    DISCOVERY = 1
    SPOT_SUB = 3
    SPOT_PUB = 4
    SOCKET = 5

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
    "AutoConnectType",
    "ServiceRole",
    "RegistrySocketRole",
    "DiscoverySocketRole",
    "SpotNodeSocketRole",
    "SpotNodeMode",
    "SpotNodeSocketOwner",
    "SpotNodeOption",
    "SpotNodeState",
    "SpotPeerSource",
    "SpotPeerKind",
    "SpotPeerState",
    "SpotKind",
    "SpotSocketRole",
    "SpotServiceAttachmentRole",
    "SpotDispatchEvent",
    "SpotDispatchSubjectKind",
    "RegistryState",
    "TopologySource",
    "TopologyState",
    "ServiceKind",
    "SubjectKind",
    "SpotRole",
]
