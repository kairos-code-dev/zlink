# SPDX-License-Identifier: MPL-2.0

import asyncio
import ctypes
import errno
import queue
import threading
import time

from ..core.core import (
    HandlerError,
    HandlerResult,
    Message,
    RecvError,
    RecvResult,
    Received,
    RequestError,
    RequestResult,
    RoutingId,
    SubmitError,
    SubmitResult,
    ZlinkMsg,
    _REPLY_HANDLER,
    _ROUTER_HANDLER,
    _clone_native_msg,
    _copy_routing_id,
    _report_unhandled_callback_exception,
    _request_result_from_code,
    _request_result_internal_errno,
    _raise_result_error,
    _routing_id_bytes,
)
from ..sockets.socket_base import _enter_callback, _leave_callback
from ..._native.ffi import lib


_ERRNO_ETERM = getattr(errno, "ETERM", 156)
_ERRNO_ENOTSUP = getattr(errno, "ENOTSUP", getattr(errno, "EOPNOTSUPP", 95))
_CALLBACK_SENTINEL = object()


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
            copied = Message.copy_from(part)
            parts.append(_clone_native_msg(copied._msg))
            copied.close()
    return parts


def _prepare_native_parts(native_parts):
    parts_array = (ZlinkMsg * len(native_parts))()
    for index, native in enumerate(native_parts):
        parts_array[index] = native
    return parts_array


def _clone_received_owner(parts_ptr, part_count):
    parts_array = (ZlinkMsg * part_count)()
    for index in range(part_count):
        parts_array[index] = _clone_native_msg(parts_ptr[index])
    return _ReceivedPartsOwner(parts_array, part_count)


def _request_received(
    parts_ptr,
    part_count,
    routing_id=None,
    spot_rid=None,
    request_seq=None,
    *,
    send_sender=None,
    reply_sender=None,
):
    return Received(
        _clone_received_owner(parts_ptr, int(part_count)),
        routing_id=routing_id,
        spot_rid=spot_rid,
        request_seq=request_seq,
        send_sender=send_sender,
        reply_sender=reply_sender,
    )


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
    def __init__(self, *, loop=None, callback=None):
        self.loop = loop
        self.future = loop.create_future() if loop is not None else None
        self.callback = callback

    def resolve(self, result, received, errnum=0):
        if self.future is not None:
            if result == RequestResult.OK:
                self.loop.call_soon_threadsafe(self.future.set_result, received)
            else:
                self.loop.call_soon_threadsafe(
                    self.future.set_exception,
                    RequestError(result, errnum),
                )
            return

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


_PROGRESS_SPIN_ITERATIONS = 32
_PROGRESS_MAX_DELAY_S = 0.008


class _RequestProgressPump:
    def __init__(self, step, is_active):
        self._step = step
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
            idle = 0
            while self._is_active():
                try:
                    self._step()
                except Exception:
                    pass
                if not self._is_active():
                    break
                idle += 1
                if idle <= _PROGRESS_SPIN_ITERATIONS:
                    time.sleep(0)
                    continue
                shift = min(3, idle - _PROGRESS_SPIN_ITERATIONS - 1)
                time.sleep(min(_PROGRESS_MAX_DELAY_S, (1 << shift) / 1000.0))
        finally:
            with self._lock:
                if self._thread is threading.current_thread():
                    self._thread = None


class _ReceivedPartsOwner:
    def __init__(self, parts_ptr, part_count):
        self._parts_ptr = parts_ptr
        self._part_count = part_count
        self._closed = False
        self._open_parts = [True] * part_count

    def msg(self, index):
        if self._closed or not self._open_parts[index]:
            raise RuntimeError("received message is closed")
        return self._parts_ptr[index]

    def close_part(self, index):
        if self._closed or not self._open_parts[index]:
            return
        self._open_parts[index] = False
        if not any(self._open_parts):
            self.close()

    def close(self):
        if self._closed:
            return
        for index in range(self._part_count):
            lib().zlink_msg_close(ctypes.byref(self._parts_ptr[index]))
        self._parts_ptr = None
        self._open_parts = [False] * self._part_count
        self._closed = True
