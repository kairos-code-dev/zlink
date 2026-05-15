# SPDX-License-Identifier: MPL-2.0

import ctypes
import queue
import threading

from ..enums.enums import AutoHwmRecalcReason, MonitorEventMask
from ..._native.ffi import (
    ZlinkMonitorSnapshot,
    ZlinkMonitorEvent,
    ZlinkSocketMonitorOpenOptions,
    lib,
)
from ..core.core import (
    CloseError,
    CloseResult,
    ConfigError,
    ConfigResult,
    HandlerError,
    HandlerResult,
    RecvError,
    RecvResult,
    _raise_last_error,
    _report_unhandled_callback_exception,
    _routing_id_bytes,
    _raise_result_error,
)
from ..sockets.socket_base import _enter_callback, _leave_callback


def _decode_fixed(buf):
    return bytes(buf).split(b"\0", 1)[0].decode("utf-8", errors="replace")


class MonitorSnapshot:
    def __init__(
        self,
        *,
        source_kind,
        state_flags,
        detail_flags,
        snd_pending_msgs,
        rcv_pending_msgs,
        auto_hwm_enabled=None,
        auto_hwm_profile=None,
        auto_hwm_role=None,
        auto_hwm_policy_class=None,
        auto_hwm_unit_budget_bytes=None,
        auto_hwm_size_cap=None,
        auto_hwm_socket_message_slots=None,
        auto_hwm_effective_message_bytes=None,
        auto_hwm_applied_sndhwm=None,
        auto_hwm_applied_rcvhwm=None,
        auto_hwm_effective_sndbuf=None,
        auto_hwm_effective_rcvbuf=None,
        auto_hwm_last_recalc_ms=None,
        auto_hwm_last_recalc_reason=None,
        auto_hwm_send_blocked_ratio_ppm=None,
        auto_hwm_deferred_sndhwm=None,
        auto_hwm_deferred_rcvhwm=None,
    ):
        self.source_kind = source_kind
        self.state_flags = state_flags
        self.detail_flags = detail_flags
        self.snd_pending_msgs = snd_pending_msgs
        self.rcv_pending_msgs = rcv_pending_msgs
        self.auto_hwm_enabled = auto_hwm_enabled
        self.auto_hwm_profile = auto_hwm_profile
        self.auto_hwm_role = auto_hwm_role
        self.auto_hwm_policy_class = auto_hwm_policy_class
        self.auto_hwm_unit_budget_bytes = auto_hwm_unit_budget_bytes
        self.auto_hwm_size_cap = auto_hwm_size_cap
        self.auto_hwm_socket_message_slots = auto_hwm_socket_message_slots
        self.auto_hwm_effective_message_bytes = auto_hwm_effective_message_bytes
        self.auto_hwm_applied_sndhwm = auto_hwm_applied_sndhwm
        self.auto_hwm_applied_rcvhwm = auto_hwm_applied_rcvhwm
        self.auto_hwm_effective_sndbuf = auto_hwm_effective_sndbuf
        self.auto_hwm_effective_rcvbuf = auto_hwm_effective_rcvbuf
        self.auto_hwm_last_recalc_ms = auto_hwm_last_recalc_ms
        self.auto_hwm_last_recalc_reason = auto_hwm_last_recalc_reason
        self.auto_hwm_send_blocked_ratio_ppm = auto_hwm_send_blocked_ratio_ppm
        self.auto_hwm_deferred_sndhwm = auto_hwm_deferred_sndhwm
        self.auto_hwm_deferred_rcvhwm = auto_hwm_deferred_rcvhwm

    def is_ready(self):
        return bool(self.state_flags & (1 << 0))


class MonitorEvent:
    def __init__(self, *, event, value, routing_id, local_addr, remote_addr):
        self.event = event
        self.value = value
        self.routing_id = routing_id
        self.local_addr = local_addr
        self.remote_addr = remote_addr


SocketMonitorEvent = MonitorEvent


def _monitor_snapshot_from_native(snapshot):
    return MonitorSnapshot(
        source_kind=int(snapshot.source_kind),
        state_flags=int(snapshot.state_flags),
        detail_flags=int(snapshot.detail_flags),
        snd_pending_msgs=int(snapshot.snd_pending_msgs),
        rcv_pending_msgs=int(snapshot.rcv_pending_msgs),
        auto_hwm_enabled=bool(snapshot.auto_hwm_enabled),
        auto_hwm_profile=int(snapshot.auto_hwm_profile),
        auto_hwm_role=int(snapshot.auto_hwm_role),
        auto_hwm_policy_class=int(snapshot.auto_hwm_policy_class),
        auto_hwm_unit_budget_bytes=int(snapshot.auto_hwm_unit_budget_bytes),
        auto_hwm_size_cap=int(snapshot.auto_hwm_size_cap),
        auto_hwm_socket_message_slots=int(snapshot.auto_hwm_socket_message_slots),
        auto_hwm_effective_message_bytes=int(
            snapshot.auto_hwm_effective_message_bytes
        ),
        auto_hwm_applied_sndhwm=int(snapshot.auto_hwm_applied_sndhwm),
        auto_hwm_applied_rcvhwm=int(snapshot.auto_hwm_applied_rcvhwm),
        auto_hwm_effective_sndbuf=int(snapshot.auto_hwm_effective_sndbuf),
        auto_hwm_effective_rcvbuf=int(snapshot.auto_hwm_effective_rcvbuf),
        auto_hwm_last_recalc_ms=int(snapshot.auto_hwm_last_recalc_ms),
        auto_hwm_last_recalc_reason=AutoHwmRecalcReason(
            int(snapshot.auto_hwm_last_recalc_reason)
        ),
        auto_hwm_send_blocked_ratio_ppm=int(
            snapshot.auto_hwm_send_blocked_ratio_ppm
        ),
        auto_hwm_deferred_sndhwm=int(snapshot.auto_hwm_deferred_sndhwm),
        auto_hwm_deferred_rcvhwm=int(snapshot.auto_hwm_deferred_rcvhwm),
    )


_CALLBACK_SENTINEL = object()
_SOCKET_MONITOR_HANDLER = ctypes.CFUNCTYPE(
    None, ctypes.POINTER(ZlinkMonitorEvent), ctypes.c_void_p
)


class _BaseMonitor:
    def __init__(self, handle):
        self._handle = handle
        self._handler = None
        self._handler_thread = None
        self._handler_stop = None
        self._handler_queue = None
        self._handler_cb = None
        if not self._handle:
            _raise_last_error()

    def _start_event_dispatch(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")
        if self._handler_thread is not None:
            raise RuntimeError("handler is already attached")

        stop = threading.Event()
        events = queue.SimpleQueue()
        self._handler = handler
        self._handler_stop = stop

        decode = MonitorSocket._decode_event
        native_handler = _SOCKET_MONITOR_HANDLER
        register = lib().zlink_socket_monitor_handler

        def _callback(event_ptr, _):
            if stop.is_set():
                return
            try:
                event = decode(event_ptr.contents)
                events.put(event)
            except Exception:
                _report_unhandled_callback_exception(handler)

        callback = native_handler(_callback)
        rc = register(self._handle, callback, None)
        if rc != 0:
            _raise_result_error(HandlerError, HandlerResult, rc, lib().zlink_errno())
        self._handler_cb = callback
        self._handler_queue = events

        def _loop():
            while True:
                event = events.get()
                if event is _CALLBACK_SENTINEL:
                    return
                _enter_callback()
                try:
                    handler(event)
                except Exception:
                    _report_unhandled_callback_exception(handler)
                finally:
                    _leave_callback()

        thread = threading.Thread(target=_loop, name="zlink-monitor-event")
        thread.daemon = True
        self._handler_thread = thread
        thread.start()

    def snapshot(self):
        snapshot = ZlinkMonitorSnapshot()
        rc = lib().zlink_monitor_snapshot(self._handle, ctypes.byref(snapshot))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return _monitor_snapshot_from_native(snapshot)

    def close(self):
        if not self._handle:
            return
        self._handler = None
        stop = self._handler_stop
        thread = self._handler_thread
        events = self._handler_queue
        if stop is not None:
            stop.set()
        if events is not None:
            events.put(_CALLBACK_SENTINEL)
        if (
            thread is not None
            and thread.is_alive()
            and thread is not threading.current_thread()
        ):
            thread.join(timeout=1.0)
        self._handler_stop = None
        self._handler_thread = None
        self._handler_cb = None
        self._handler_queue = None
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_monitor_close(ctypes.byref(handle))
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


class MonitorSocket(_BaseMonitor):
    ignore_handler = staticmethod(lambda event: None)

    @staticmethod
    def _decode_event(native):
        return MonitorEvent(
            event=int(native.event),
            value=int(native.value),
            routing_id=_routing_id_bytes(native.routing_id),
            local_addr=_decode_fixed(native.local_addr),
            remote_addr=_decode_fixed(native.remote_addr),
        )

    def recv(self, *, flags=0):
        native = ZlinkMonitorEvent()
        rc = lib().zlink_socket_monitor_recv(self._handle, ctypes.byref(native), flags)
        if rc == RecvResult.NO_DATA:
            return None
        if rc != 0:
            _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
        return self._decode_event(native)

    def on_event(self, handler):
        self._start_event_dispatch(handler)


def open_socket_monitor(socket, events=MonitorEventMask.ALL):
    options = ZlinkSocketMonitorOpenOptions()
    options.events = int(events)
    handle = lib().zlink_socket_monitor_open(socket._handle, ctypes.byref(options))
    return MonitorSocket(handle)
