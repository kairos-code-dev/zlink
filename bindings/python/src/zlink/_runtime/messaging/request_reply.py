# SPDX-License-Identifier: MPL-2.0

import ctypes
import errno
import queue
import threading
import time

from ..handles.native_support import (
    HandlerError,
    HandlerResult,
    RecvError,
    RecvResult,
    RequestError,
    RequestResult,
    RoutingId,
    SubmitError,
    SubmitResult,
    ZlinkMsg,
    _REPLY_HANDLER,
    _clone_native_msg,
    _copy_routing_id,
    _report_unhandled_callback_exception,
    _request_result_from_code,
    _request_result_native_errno,
    _raise_result_error,
    _routing_id_bytes,
)
from .message_materializer import Message, message_from
from ..sockets.socket_base import _enter_callback, _leave_callback
from ..._native.ffi import ZlinkPollerEvent, lib


_ERRNO_ETERM = getattr(errno, "ETERM", 156)
_ERRNO_ENOTSUP = getattr(errno, "ENOTSUP", getattr(errno, "EOPNOTSUPP", 95))


def _ensure_reply_flags_supported(flags):
    if int(flags) != 0:
        raise SubmitError(SubmitResult.NOT_SUPPORTED, _ERRNO_ENOTSUP)


def _timeout_to_ms(timeout):
    if timeout in (None, 0):
        return 0
    return max(1, int(float(timeout) * 1000))


def _payload_parts(payload):
    if isinstance(payload, (list, tuple)):
        parts = payload
    else:
        parts = [payload]
    if not parts:
        raise ValueError("payload must not be empty")
    return parts


def _clone_payload(payload):
    parts = []
    for part in _payload_parts(payload):
        if isinstance(part, Message):
            parts.append(_clone_native_msg(part._msg))
        else:
            copied = message_from(part)
            parts.append(_clone_native_msg(copied._msg))
            copied.close()
    return parts


def _prepare_native_parts(native_parts):
    parts_array = (ZlinkMsg * len(native_parts))()
    for index, native in enumerate(native_parts):
        parts_array[index] = native
    return parts_array


def _message_list_from_parts(parts_ptr, part_count):
    messages = []
    for index in range(int(part_count)):
        msg = Message.__new__(Message)
        msg._msg = _clone_native_msg(parts_ptr[index])
        msg._valid = True
        msg._keepalive = None
        messages.append(msg)
    return messages


class _PendingRequest:
    def __init__(self, *, callback=None):
        self.callback = callback

    def resolve(self, result, received, errnum=0):
        if self.callback is None:
            return

        try:
            _enter_callback()
            try:
                self.callback(result, received if result == RequestResult.OK else [])
            finally:
                _leave_callback()
        except Exception:
            _report_unhandled_callback_exception(self.callback)


_POLL_COMPLETION = 32  # ZLINK_POLLCOMPLETION


class _RequestProgressPump:
    """Per-handle background worker that drains request completions.

    Uses the canonical poller-based progress model:
    a dedicated zlink_poller is registered with the handle under the
    ZLINK_POLLCOMPLETION flag. zlink_poller_wait() blocks until reply
    completions are available and drains them internally; the worker only
    needs to wake the wait loop when handles complete.
    """

    def __init__(self, handle_getter, is_active):
        self._handle_getter = handle_getter
        self._is_active = is_active
        self._lock = threading.Lock()
        self._thread = None

    def ensure_running(self):
        with self._lock:
            if self._thread is not None and self._thread.is_alive():
                return
            self._thread = threading.Thread(
                target=self._run,
                name="zlink-request-progress",
                daemon=True,
            )
            self._thread.start()

    def _run(self):
        try:
            handle = self._handle_getter()
            if not handle:
                return
            poller = lib().zlink_poller_new()
            if not poller:
                return
            try:
                rc = lib().zlink_poller_add(
                    poller,
                    handle,
                    None,
                    ctypes.c_short(_POLL_COMPLETION),
                )
                if rc != 0:
                    return
                events = (ZlinkPollerEvent * 1)()
                error_out = ctypes.c_int()
                # Use a finite wait timeout so the worker can observe
                # _is_active() turning false (e.g. when the owning socket
                # closes and cancels its pending requests).
                while self._is_active():
                    try:
                        lib().zlink_poller_wait(
                            poller, events, 1, 50, ctypes.byref(error_out)
                        )
                    except Exception:
                        break
            finally:
                lib().zlink_poller_remove(poller, handle)
                handle_ptr = ctypes.c_void_p(poller)
                lib().zlink_poller_destroy(ctypes.byref(handle_ptr))
        finally:
            with self._lock:
                if self._thread is threading.current_thread():
                    self._thread = None
