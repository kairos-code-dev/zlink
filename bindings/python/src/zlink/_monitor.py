# SPDX-License-Identifier: MPL-2.0

import ctypes
from ._ffi import lib
from ._core import _raise_last_error


class RoutingId(ctypes.Structure):
    _fields_ = [("size", ctypes.c_ubyte), ("data", ctypes.c_ubyte * 255)]


class MonitorEvent(ctypes.Structure):
    _fields_ = [
        ("event", ctypes.c_uint64),
        ("value", ctypes.c_uint64),
        ("routing_id", RoutingId),
        ("local_addr", ctypes.c_char * 256),
        ("remote_addr", ctypes.c_char * 256),
    ]


class MonitorSocket:
    def __init__(self, handle):
        self._handle = handle

    def recv(self, flags=0):
        evt = MonitorEvent()
        rc = lib().zlink_monitor_recv(self._handle, ctypes.byref(evt), flags)
        if rc != 0:
            _raise_last_error()
        local = evt.local_addr.split(b"\0", 1)[0].decode("utf-8", errors="replace")
        remote = evt.remote_addr.split(b"\0", 1)[0].decode("utf-8", errors="replace")
        routing = bytes(evt.routing_id.data[: evt.routing_id.size])
        return {
            "event": evt.event,
            "value": evt.value,
            "routing_id": routing,
            "local_addr": local,
            "remote_addr": remote,
        }
