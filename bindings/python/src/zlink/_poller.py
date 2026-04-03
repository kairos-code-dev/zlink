# SPDX-License-Identifier: MPL-2.0

import ctypes

from ._ffi import ZlinkPollerEvent, lib
from ._core import _raise_last_error


class Poller:
    def __init__(self):
        self._handle = lib().zlink_poller_new()
        if not self._handle:
            _raise_last_error()
        self._items = {}
        self._next_user_data = 1

    def add_socket(self, socket, events, tag=None):
        validator = getattr(socket, "_validate_poller_events", None)
        if validator is not None:
            validator(int(events))
        user_data = ctypes.c_void_p(self._next_user_data)
        self._next_user_data += 1
        rc = lib().zlink_poller_add(
            self._handle, socket._handle, user_data, int(events)
        )
        if rc != 0:
            _raise_last_error()
        self._items[user_data.value] = {
            "socket": socket,
            "fd": None,
            "tag": tag,
        }

    def add_fd(self, fd, events, tag=None):
        user_data = ctypes.c_void_p(self._next_user_data)
        self._next_user_data += 1
        rc = lib().zlink_poller_add_fd(self._handle, fd, user_data, int(events))
        if rc != 0:
            _raise_last_error()
        self._items[user_data.value] = {
            "socket": None,
            "fd": int(fd),
            "tag": tag,
        }

    def modify_socket(self, socket, events):
        rc = lib().zlink_poller_modify(self._handle, socket._handle, int(events))
        if rc != 0:
            _raise_last_error()

    def modify_fd(self, fd, events):
        rc = lib().zlink_poller_modify_fd(self._handle, int(fd), int(events))
        if rc != 0:
            _raise_last_error()

    def remove_socket(self, socket):
        rc = lib().zlink_poller_remove(self._handle, socket._handle)
        if rc != 0:
            _raise_last_error()
        self._remove_item(lambda item: item["socket"] is socket)

    def remove_fd(self, fd):
        rc = lib().zlink_poller_remove_fd(self._handle, int(fd))
        if rc != 0:
            _raise_last_error()
        self._remove_item(lambda item: item["fd"] == int(fd))

    def poll(self, timeout_ms):
        if not self._items:
            return []
        events = (ZlinkPollerEvent * len(self._items))()
        ready = lib().zlink_poller_wait_all(
            self._handle, events, len(self._items), int(timeout_ms)
        )
        if ready < 0:
            _raise_last_error()
        results = []
        for index in range(ready):
            event = events[index]
            item = self._items.get(event.user_data)
            if item is None:
                continue
            results.append(
                {
                    "socket": item["socket"],
                    "fd": item["fd"],
                    "events": int(event.events),
                    "tag": item["tag"],
                }
            )
        return results

    def close(self):
        if not self._handle:
            return
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_poller_destroy(ctypes.byref(handle))
        self._handle = None
        self._items = {}
        if rc != 0:
            _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    def _remove_item(self, predicate):
        for key, item in list(self._items.items()):
            if predicate(item):
                del self._items[key]
                return
