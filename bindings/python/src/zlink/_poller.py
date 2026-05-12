# SPDX-License-Identifier: MPL-2.0

import ctypes
from dataclasses import dataclass

from ._ffi import ZlinkPollerEvent, lib
from ._core import (
    CloseError,
    CloseResult,
    ConfigError,
    ConfigResult,
    RecvError,
    RecvResult,
    _raise_last_error,
    _raise_result_error,
)
from ._enums import PollEventFlag, PollSourceKind


@dataclass(frozen=True)
class PollerEvent:
    source_kind: PollSourceKind
    events: PollEventFlag
    socket: object | None = None
    fd: int | None = None
    timer: object | None = None
    tag: object | None = None


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
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        self._items[user_data.value] = {
            "socket": socket,
            "fd": None,
            "timer": None,
            "tag": tag,
        }

    def add_fd(self, fd, events, tag=None):
        user_data = ctypes.c_void_p(self._next_user_data)
        self._next_user_data += 1
        rc = lib().zlink_poller_add_fd(self._handle, fd, user_data, int(events))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        self._items[user_data.value] = {
            "socket": None,
            "fd": int(fd),
            "timer": None,
            "tag": tag,
        }

    def add_timer(self, timer, user_data=None):
        user_data_ptr = ctypes.c_void_p(self._next_user_data)
        self._next_user_data += 1
        rc = lib().zlink_poller_add_timer(self._handle, timer._handle, user_data_ptr)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        self._items[user_data_ptr.value] = {
            "socket": None,
            "fd": None,
            "timer": timer,
            "tag": user_data,
        }

    def modify_socket(self, socket, events):
        rc = lib().zlink_poller_modify(self._handle, socket._handle, int(events))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def modify_fd(self, fd, events):
        rc = lib().zlink_poller_modify_fd(self._handle, int(fd), int(events))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def remove_socket(self, socket):
        rc = lib().zlink_poller_remove(self._handle, socket._handle)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        self._remove_item(lambda item: item["socket"] is socket)

    def remove_fd(self, fd):
        rc = lib().zlink_poller_remove_fd(self._handle, int(fd))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        self._remove_item(lambda item: item["fd"] == int(fd))

    def remove_timer(self, timer):
        rc = lib().zlink_poller_remove_timer(self._handle, timer._handle)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        self._remove_item(lambda item: item["timer"] is timer)

    def size(self):
        error_out = ctypes.c_int()
        rc = lib().zlink_poller_size(self._handle, ctypes.byref(error_out))
        if rc < 0:
            _raise_result_error(ConfigError, ConfigResult, error_out.value, lib().zlink_errno())
        return int(rc)

    def poll(self, timeout_ms):
        if not self._items:
            return []
        events = (ZlinkPollerEvent * len(self._items))()
        error_out = ctypes.c_int()
        ready = lib().zlink_poller_wait(
            self._handle,
            events,
            len(self._items),
            int(timeout_ms),
            ctypes.byref(error_out),
        )
        if ready < 0:
            _raise_result_error(RecvError, RecvResult, error_out.value, lib().zlink_errno())
        results = []
        for index in range(ready):
            event = events[index]
            item = self._items.get(event.user_data)
            if item is None:
                continue
            if item["socket"] is not None:
                source_kind = PollSourceKind.SOCKET
            elif item["fd"] is not None:
                source_kind = PollSourceKind.FD
            else:
                source_kind = PollSourceKind.TIMER
            results.append(
                PollerEvent(
                    source_kind=source_kind,
                    events=PollEventFlag(int(event.events)),
                    socket=item["socket"],
                    fd=item["fd"],
                    timer=item["timer"],
                    tag=item["tag"],
                )
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
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()

    def _remove_item(self, predicate):
        for key, item in list(self._items.items()):
            if predicate(item):
                del self._items[key]
                return
