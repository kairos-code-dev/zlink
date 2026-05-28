# SPDX-License-Identifier: MPL-2.0

import ctypes

from ...contracts.errors.codes import CloseResult, ConfigResult
from ...contracts.errors.errors import CloseError, ConfigError, RecvError
from ...contracts.eventing.codes import PollEventFlag, PollSourceKind
from ...contracts.eventing.poller import PollEvent, PollEvents, Poller
from ...contracts.sockets.codes import RecvResult
from ..._native.ffi import ZlinkPollerEvent, lib
from ..handles.native_support import _raise_last_error, _raise_result_error


class NativePollEvents(PollEvents):
    def __init__(self, capacity):
        capacity = int(capacity)
        if capacity <= 0:
            raise ValueError("capacity must be > 0")
        self._capacity = capacity
        self._events = (ZlinkPollerEvent * capacity)()
        self._ready_count = 0

    @property
    def capacity(self):
        return self._capacity

    @property
    def ready_count(self):
        return self._ready_count

    def source_kind(self, index):
        event = self._event(index)
        return PollSourceKind(int(event.source_kind))

    def slot(self, index):
        return int(self._event(index).user_data or 0)

    def revents(self, index):
        return int(self._event(index).events)

    def has_event(self, index, event):
        return (self.revents(index) & int(event)) != 0

    def fd(self, index):
        return int(self._event(index).fd)

    def event(self, index):
        native = self._event(index)
        return PollEvent(
            source_kind=PollSourceKind(int(native.source_kind)),
            slot=int(native.user_data or 0),
            revents=int(native.events),
            fd=int(native.fd),
        )

    def _mark_ready_count(self, count):
        count = int(count)
        if count < 0 or count > self._capacity:
            raise ValueError("ready count out of range")
        self._ready_count = count

    def _event(self, index):
        index = int(index)
        if index < 0 or index >= self._ready_count:
            raise IndexError(f"ready index {index}")
        return self._events[index]


class NativePoller(Poller):
    def __init__(self):
        if hasattr(self, "_handle"):
            return
        self._handle = lib().zlink_poller_new()
        if not self._handle:
            _raise_last_error()
        self._completion_handles = set()

    def add_socket(self, socket, events, slot):
        user_data = ctypes.c_void_p(_validate_slot(slot))
        rc = lib().zlink_poller_add(self._handle, socket._handle, user_data, int(events))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if int(events) & int(PollEventFlag.POLLCOMPLETION):
            _acquire_external_progress(socket._handle)
            self._completion_handles.add(socket._handle)

    def add_fd(self, fd, events, slot):
        user_data = ctypes.c_void_p(_validate_slot(slot))
        rc = lib().zlink_poller_add_fd(self._handle, fd, user_data, int(events))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def add_timer(self, timer, slot):
        user_data = ctypes.c_void_p(_validate_slot(slot))
        rc = lib().zlink_poller_add_timer(self._handle, timer._handle, user_data)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def modify_socket(self, socket, events):
        rc = lib().zlink_poller_modify(self._handle, socket._handle, int(events))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        wants_completion = int(events) & int(PollEventFlag.POLLCOMPLETION)
        has_completion = socket._handle in self._completion_handles
        if wants_completion and not has_completion:
            _acquire_external_progress(socket._handle)
            self._completion_handles.add(socket._handle)
        elif not wants_completion and has_completion:
            _release_external_progress(socket._handle)
            self._completion_handles.discard(socket._handle)

    def modify_fd(self, fd, events):
        rc = lib().zlink_poller_modify_fd(self._handle, int(fd), int(events))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def remove_socket(self, socket):
        rc = lib().zlink_poller_remove(self._handle, socket._handle)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if socket._handle in self._completion_handles:
            _release_external_progress(socket._handle)
            self._completion_handles.discard(socket._handle)

    def remove_fd(self, fd):
        rc = lib().zlink_poller_remove_fd(self._handle, int(fd))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def remove_timer(self, timer):
        rc = lib().zlink_poller_remove_timer(self._handle, timer._handle)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def size(self):
        error_out = ctypes.c_int()
        rc = lib().zlink_poller_size(self._handle, ctypes.byref(error_out))
        if rc < 0:
            _raise_result_error(ConfigError, ConfigResult, error_out.value, lib().zlink_errno())
        return int(rc)

    def wait(self, events, timeout_ms):
        if not isinstance(events, PollEvents):
            raise TypeError("events must be PollEvents")
        error_out = ctypes.c_int()
        ready = lib().zlink_poller_wait(
            self._handle,
            events._events,
            events.capacity,
            int(timeout_ms),
            ctypes.byref(error_out),
        )
        if ready < 0:
            _raise_result_error(RecvError, RecvResult, error_out.value, lib().zlink_errno())
        events._mark_ready_count(ready)
        return int(ready)

    def close(self):
        if not self._handle:
            return
        for handle in list(self._completion_handles):
            _release_external_progress(handle)
        self._completion_handles.clear()
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_poller_destroy(ctypes.byref(handle))
        self._handle = None
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


def _validate_slot(slot):
    if not isinstance(slot, int) or slot < 0:
        raise ValueError("slot must be a non-negative int")
    if slot > ctypes.c_size_t(-1).value:
        raise ValueError("slot is too large for uintptr_t")
    return slot


def _acquire_external_progress(handle):
    from ..service.spot import _acquire_external_request_progress

    _acquire_external_request_progress(handle)


def _release_external_progress(handle):
    from ..service.spot import _release_external_request_progress

    _release_external_request_progress(handle)


NativePoller.__module__ = "zlink.contracts.eventing.poller"
NativePoller.__name__ = "Poller"
NativePoller.__qualname__ = "Poller"
NativePollEvents.__module__ = "zlink.contracts.eventing.poller"
NativePollEvents.__name__ = "PollEvents"
NativePollEvents.__qualname__ = "PollEvents"


def create_poller():
    return NativePoller()


def create_poll_events(capacity):
    return NativePollEvents(capacity)
