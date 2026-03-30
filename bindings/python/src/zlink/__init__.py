# SPDX-License-Identifier: MPL-2.0

from ._core import (
    Context,
    Message,
    Received,
    Subscribed,
    SubscriptionEvent,
    ZlinkError,
)
from ._ffi import lib as _lib
from ._socket_base import Socket
from ._socket_types import (
    DealerSocket,
    PairSocket,
    PubSocket,
    RouterSocket,
    StreamSocket,
    SubSocket,
    XPubSocket,
    XSubSocket,
)
from ._poller import Poller
from ._monitor import (
    MonitorSnapshot,
    MonitorSocket,
    ServiceMonitor,
    ServiceMonitorEvent,
    SocketMonitorEvent,
)
from ._discovery import (
    Registry,
    Discovery,
)
from ._spot import (
    SpotNode,
    Spot,
    SpotNodeStatus,
    SpotNodePeerEntry,
    SpotNodePeerFilter,
    SpotNodeSubjectEntry,
    SpotNodeSubjectFilter,
)
from ._enums import (
    SocketType,
    ContextOption,
    SocketOption,
    RouterOption,
    SendResult,
    ErrorCode,
    ProtocolError,
    MonitorEvent,
    DisconnectReason,
    PollEvent,
    ServiceType,
    ServiceMonitorMask,
    RegistrySocketRole,
    DiscoverySocketRole,
    SpotNodeSocketRole,
    SpotNodeOption,
    SpotNodePubMode,
    SpotNodePubQueueFullPolicy,
    SpotNodeState,
    SpotPeerSource,
    SpotPeerState,
    SpotSocketRole,
)

SERVICE_TYPE_SPOT = ServiceType.SPOT


def version():
    import ctypes

    L = _lib()
    major = ctypes.c_int()
    minor = ctypes.c_int()
    patch = ctypes.c_int()
    L.zlink_version(ctypes.byref(major), ctypes.byref(minor), ctypes.byref(patch))
    return major.value, minor.value, patch.value


__all__ = [
    "version",
    "Context",
    "Socket",
    "PairSocket",
    "DealerSocket",
    "RouterSocket",
    "StreamSocket",
    "PubSocket",
    "SubSocket",
    "XPubSocket",
    "XSubSocket",
    "Message",
    "Received",
    "Subscribed",
    "SubscriptionEvent",
    "Poller",
    "MonitorSnapshot",
    "MonitorSocket",
    "ServiceMonitor",
    "SocketMonitorEvent",
    "ServiceMonitorEvent",
    "Registry",
    "Discovery",
    "SERVICE_TYPE_SPOT",
    "SpotNode",
    "Spot",
    "SpotNodeStatus",
    "SpotNodePeerEntry",
    "SpotNodePeerFilter",
    "SpotNodeSubjectEntry",
    "SpotNodeSubjectFilter",
    "ZlinkError",
    "SocketType",
    "ContextOption",
    "SocketOption",
    "RouterOption",
    "SendResult",
    "ErrorCode",
    "ProtocolError",
    "MonitorEvent",
    "DisconnectReason",
    "PollEvent",
    "ServiceType",
    "ServiceMonitorMask",
    "RegistrySocketRole",
    "DiscoverySocketRole",
    "SpotNodeSocketRole",
    "SpotNodeOption",
    "SpotNodePubMode",
    "SpotNodePubQueueFullPolicy",
    "SpotNodeState",
    "SpotPeerSource",
    "SpotPeerState",
    "SpotSocketRole",
]
