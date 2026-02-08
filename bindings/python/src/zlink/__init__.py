import ctypes

from ._ffi import lib
from ._core import Context, Socket, Message, ZlinkError
from ._poller import Poller
from ._monitor import MonitorSocket, MonitorEvent as _MonitorEventStruct
from ._discovery import (
    Registry,
    Discovery,
    Gateway,
    Receiver,
)
from ._spot import SpotNode, Spot
from ._enums import (
    SocketType,
    ContextOption,
    SocketOption,
    SendFlag,
    ReceiveFlag,
    MonitorEvent,
    DisconnectReason,
    PollEvent,
    ServiceType,
    GatewayLbStrategy,
    SpotTopicMode,
    RegistrySocketRole,
    DiscoverySocketRole,
    GatewaySocketRole,
    ReceiverSocketRole,
    SpotNodeSocketRole,
    SpotSocketRole,
)

# Backward-compatible aliases
SERVICE_TYPE_GATEWAY = ServiceType.GATEWAY
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
    "Poller",
    "MonitorSocket",
    "Registry",
    "Discovery",
    "Gateway",
    "Receiver",
    "SERVICE_TYPE_GATEWAY",
    "SERVICE_TYPE_SPOT",
    "SpotNode",
    "Spot",
    "ZlinkError",
    "SocketType",
    "ContextOption",
    "SocketOption",
    "SendFlag",
    "ReceiveFlag",
    "MonitorEvent",
    "DisconnectReason",
    "PollEvent",
    "ServiceType",
    "GatewayLbStrategy",
    "SpotTopicMode",
    "RegistrySocketRole",
    "DiscoverySocketRole",
    "GatewaySocketRole",
    "ReceiverSocketRole",
    "SpotNodeSocketRole",
    "SpotSocketRole",
]
