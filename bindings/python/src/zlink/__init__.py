# SPDX-License-Identifier: MPL-2.0

import ctypes

from ._ffi import lib
from ._core import (
    Context,
    Message,
    ReceivedMessage,
    ReceivedMultipart,
    ReceivedTopicMessage,
    Socket,
    ZlinkError,
)
from ._poller import Poller
from ._monitor import MonitorSocket, ServiceMonitor
from ._discovery import (
    Registry,
    Discovery,
)
from ._spot import SpotNode, Spot
from ._enums import (
    SocketType,
    ContextOption,
    SocketOption,
    SendFlag,
    ReceiveFlag,
    StreamDispatchMode,
    ErrorCode,
    ProtocolError,
    MonitorEvent,
    DisconnectReason,
    PollEvent,
    ServiceType,
    RegistrySocketRole,
    DiscoverySocketRole,
    SpotNodeSocketRole,
    SpotNodeOption,
    SpotNodePubMode,
    SpotNodePubQueueFullPolicy,
    SpotSocketRole,
)

SERVICE_TYPE_SPOT = ServiceType.SPOT


def version():
    L = lib()
    major = ctypes.c_int()
    minor = ctypes.c_int()
    patch = ctypes.c_int()
    L.zlink_version(ctypes.byref(major), ctypes.byref(minor), ctypes.byref(patch))
    return major.value, minor.value, patch.value


__all__ = [
    "version",
    "Context",
    "Socket",
    "Message",
    "ReceivedMessage",
    "ReceivedMultipart",
    "ReceivedTopicMessage",
    "Poller",
    "MonitorSocket",
    "ServiceMonitor",
    "Registry",
    "Discovery",
    "SERVICE_TYPE_SPOT",
    "SpotNode",
    "Spot",
    "ZlinkError",
    "SocketType",
    "ContextOption",
    "SocketOption",
    "SendFlag",
    "ReceiveFlag",
    "StreamDispatchMode",
    "ErrorCode",
    "ProtocolError",
    "MonitorEvent",
    "DisconnectReason",
    "PollEvent",
    "ServiceType",
    "RegistrySocketRole",
    "DiscoverySocketRole",
    "SpotNodeSocketRole",
    "SpotNodeOption",
    "SpotNodePubMode",
    "SpotNodePubQueueFullPolicy",
    "SpotSocketRole",
]
