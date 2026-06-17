# SPDX-License-Identifier: MPL-2.0

import ctypes
import threading
import time

from ...._native.ffi import ZlinkPollerEvent, lib
from ...handles.native_support import RequestResult, _report_unhandled_callback_exception


class PendingRequest:
    def __init__(self, *, callback=None):
        self.callback = callback

    def resolve(self, result, received, errnum=0):
        if self.callback is None:
            return
        try:
            self.callback(result, received if result == RequestResult.OK else [])
        except Exception:
            _report_unhandled_callback_exception(self.callback)


class PendingActorJoin:
    """Pending state for ActorJoinOp: surfaces (ActorJoinResult, list[Message])."""

    def __init__(self, *, callback=None):
        self.callback = callback

    def resolve(self, join_result, messages, errnum=0):
        result = join_result.result
        if self.callback is None:
            return
        try:
            self.callback(join_result, messages if result == RequestResult.OK else [])
        except Exception:
            _report_unhandled_callback_exception(self.callback)


class PendingActorJoinEntrySpot:
    """Pending state for ActorJoinEntrySpotOp."""

    def __init__(self, *, callback=None):
        self.callback = callback

    def resolve(self, join_result, messages, errnum=0):
        result = join_result.result
        if self.callback is None:
            return
        try:
            self.callback(join_result, messages if result == RequestResult.OK else [])
        except Exception:
            _report_unhandled_callback_exception(self.callback)


class PendingActorLookup:
    """Pending state for ActorLookupOp: surfaces ActorLookupResult."""

    def __init__(self, *, callback=None):
        self.callback = callback

    def resolve(self, lookup_result, errnum=0):
        if self.callback is None:
            return
        try:
            self.callback(lookup_result)
        except Exception:
            _report_unhandled_callback_exception(self.callback)


_POLL_COMPLETION = 32
_EXTERNAL_REQUEST_PROGRESS = {}
_EXTERNAL_REQUEST_PROGRESS_LOCK = threading.Lock()


def acquire_external_request_progress(handle):
    if not handle:
        return
    with _EXTERNAL_REQUEST_PROGRESS_LOCK:
        _EXTERNAL_REQUEST_PROGRESS[handle] = (
            _EXTERNAL_REQUEST_PROGRESS.get(handle, 0) + 1
        )


def release_external_request_progress(handle):
    if not handle:
        return
    with _EXTERNAL_REQUEST_PROGRESS_LOCK:
        count = _EXTERNAL_REQUEST_PROGRESS.get(handle, 0) - 1
        if count > 0:
            _EXTERNAL_REQUEST_PROGRESS[handle] = count
        else:
            _EXTERNAL_REQUEST_PROGRESS.pop(handle, None)


def external_request_progress_active(handle):
    if not handle:
        return False
    with _EXTERNAL_REQUEST_PROGRESS_LOCK:
        return _EXTERNAL_REQUEST_PROGRESS.get(handle, 0) > 0


class RequestProgressPump:
    def __init__(
        self,
        target_provider,
        is_active,
        *,
        idle_grace_s,
    ):
        self._target_provider = target_provider
        self._is_active = is_active
        self._idle_grace_s = idle_grace_s
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = None

    def ensure_running(self):
        with self._lock:
            if self._stop.is_set():
                return
            if not any(
                not external_request_progress_active(handle)
                for handle in self._target_provider()
                if handle
            ):
                return
            if self._thread is not None and self._thread.is_alive():
                return
            self._thread = threading.Thread(
                target=self._run,
                name="zlink-spot-request-progress",
                daemon=True,
            )
            self._thread.start()

    def _run(self):
        try:
            poller = lib().zlink_poller_new()
            if not poller:
                return
            registered = set()
            try:
                idle_since = None
                events = (ZlinkPollerEvent * 8)()
                error_out = ctypes.c_int()
                while not self._stop.is_set():
                    current = set()
                    for handle in self._target_provider():
                        if handle and not external_request_progress_active(handle):
                            current.add(handle)
                    for handle in current - registered:
                        rc = lib().zlink_poller_add(
                            poller,
                            handle,
                            None,
                            ctypes.c_short(_POLL_COMPLETION),
                        )
                        if rc == 0:
                            registered.add(handle)
                    for handle in registered - current:
                        lib().zlink_poller_remove(poller, handle)
                        registered.discard(handle)
                    if not self._is_active() and not registered:
                        if idle_since is None:
                            idle_since = time.monotonic()
                        elif time.monotonic() - idle_since >= self._idle_grace_s:
                            break
                        if self._stop.wait(0.001):
                            break
                        continue
                    idle_since = None
                    if not registered:
                        if self._stop.wait(0.001):
                            break
                        continue
                    try:
                        lib().zlink_poller_wait(
                            poller, events, 8, 50, ctypes.byref(error_out)
                        )
                    except Exception:
                        break
            finally:
                for handle in list(registered):
                    try:
                        lib().zlink_poller_remove(poller, handle)
                    except Exception:
                        pass
                handle_ptr = ctypes.c_void_p(poller)
                lib().zlink_poller_destroy(ctypes.byref(handle_ptr))
        finally:
            with self._lock:
                self._thread = None

    def stop(self):
        self._stop.set()
        with self._lock:
            thread = self._thread
        if (
            thread is not None
            and thread.is_alive()
            and thread is not threading.current_thread()
        ):
            thread.join(timeout=1.0)
