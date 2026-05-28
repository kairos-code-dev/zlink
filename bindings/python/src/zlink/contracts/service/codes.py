# SPDX-License-Identifier: MPL-2.0

from enum import IntEnum, IntFlag

class AutoConnectType(IntEnum):
    INVALID = 0
    ROUTE_MESH = 1
    CLIENT_SERVER = 2
    DEALER_MESH = 3
    FANOUT = 4
    SPOT_MESH = 5

class ServiceRole(IntEnum):
    INVALID = 0
    SPOT = 2
    ROUTER = 3
    DEALER = 4
    PUB = 5
    SUB = 6

class RegistrySocketRole(IntEnum):
    PUB = 1
    ROUTER = 2
    PEER_SUB = 3

class DiscoverySocketRole(IntEnum):
    SUB = 1

class SpotNodeSocketRole(IntEnum):
    NODE = 0
    PUB = 1
    SUB = 2
    DEALER = 3

class SpotNodeMode(IntEnum):
    PUBSUB = 1
    ROUTED = 2
    ALL = 3

class SpotNodeSocketOwner(IntEnum):
    ANY = 0
    NODE = 1
    SPOT = 2

class SpotNodeOption(IntEnum):
    ROUTER_HWM_PROFILE = 0x360E
    ROUTER_HWM = 0x360F
    PUBSUB_HWM_PROFILE = 0x3610
    PUBSUB_HWM = 0x3611
    DISPATCH_WORKERS_MIN = 0x3612
    DISPATCH_WORKERS_MAX = 0x3613

class SpotNodeState(IntEnum):
    IDLE = 1
    CONNECTING = 2
    PARTIAL_READY = 3
    READY = 4
    ERROR = 5

class SpotPeerSource(IntEnum):
    MANUAL = 1
    DISCOVERY = 2
    MIXED = 3

class SpotPeerKind(IntEnum):
    SPOT_MESH = 1
    ROUTER_CHANNEL = 2

class SpotPeerState(IntEnum):
    CONFIGURED = 1
    CONNECTING = 2
    CONNECTED = 3

class SpotKind(IntEnum):
    INVALID = 0
    ENTRY = 1
    USER = 2

class SpotSocketRole(IntEnum):
    PUB = 1
    SUB = 2

class SpotServiceAttachmentRole(IntEnum):
    ROUTER = 1
    PUB = 2
    SUB = 3

class SpotDispatchEvent(IntEnum):
    SUBSCRIBE_READABLE = 1
    ROUTED_READABLE = 2
    TIMER_READABLE = 3
    CHANNEL_REPLY_READABLE = 4
    ACTOR_READABLE = 5
    ACTOR_JOIN_READABLE = 6
    ACTOR_LIFECYCLE_READABLE = 7

class SpotActorLifecycleEventKind(IntEnum):
    JOINED = 1
    LEFT = 2

class SpotDispatchSubjectKind(IntEnum):
    SPOT = 1
    TIMER = 2
    CHANNEL_DEALER = 3
    ACTOR = 4

class RegistryState(IntEnum):
    IDLE = 1
    ACTIVE = 2
    DEGRADED = 3
    ERROR = 4

class TopologySource(IntEnum):
    MANUAL = 1
    DISCOVERY = 2
    REGISTRY = 3

class TopologyState(IntEnum):
    DISCOVERED = 1
    CONNECTING = 2
    READY = 3
    LOST = 4
    ERROR = 5
    STOPPED = 6

class ServiceKind(IntEnum):
    DISCOVERY = 1
    SPOT_SUB = 3
    SPOT_PUB = 4
    SOCKET = 5

class SubjectKind(IntEnum):
    NONE = 0
    TOPIC = 1
    PATTERN = 2

class SpotRole(IntEnum):
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
