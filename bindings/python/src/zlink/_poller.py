# SPDX-License-Identifier: MPL-2.0

import ctypes
from ._ffi import lib
from ._core import _raise_last_error


class PollItem(ctypes.Structure):
    _fields_ = [
        ("socket", ctypes.c_void_p),
        ("fd", ctypes.c_int),
        ("events", ctypes.c_short),
        ("revents", ctypes.c_short),
    ]


class Poller:
    def __init__(self):
        self._items = []

    def add_socket(self, socket, events):
        self._items.append((socket, 0, events))

    def add_fd(self, fd, events):
        self._items.append((None, fd, events))

    def poll(self, timeout_ms: int):
        if not self._items:
            return []
        arr = (PollItem * len(self._items))()
        for i, (sock, fd, events) in enumerate(self._items):
            arr[i].socket = sock._handle if sock else None
            arr[i].fd = fd
            arr[i].events = events
            arr[i].revents = 0
        rc = lib().zlink_poll(arr, len(self._items), int(timeout_ms))
        if rc < 0:
            _raise_last_error()
        results = []
        for i, item in enumerate(arr):
            if item.revents:
                results.append((self._items[i][0], item.revents))
        return results
