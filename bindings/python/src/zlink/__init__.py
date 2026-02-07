import ctypes

from ._ffi import lib
from ._core import Context, Socket, Message, ZlinkError
from ._poller import Poller
from ._monitor import MonitorSocket, MonitorEvent
from ._discovery import (
    Registry,
    Discovery,
    Gateway,
    Receiver,
    SERVICE_TYPE_GATEWAY,
    SERVICE_TYPE_SPOT,
)
from ._spot import SpotNode, Spot


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
    "MonitorEvent",
    "Registry",
    "Discovery",
    "Gateway",
    "Receiver",
    "SERVICE_TYPE_GATEWAY",
    "SERVICE_TYPE_SPOT",
    "SpotNode",
    "Spot",
    "ZlinkError",
]
