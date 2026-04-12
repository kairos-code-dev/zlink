# SPDX-License-Identifier: MPL-2.0

import ctypes
import queue
import threading

from ._enums import MonitorEvent, ServiceMonitorMask
from ._ffi import (
    ZlinkMonitorSnapshot,
    ZlinkMonitorEvent,
    ZlinkServiceEvent,
    ZlinkServiceMonitorOpenOptions,
    ZlinkSocketMonitorOpenOptions,
    lib,
)
from ._core import (
    CloseError,
    CloseResult,
    ConfigError,
    ConfigResult,
    HandlerError,
    HandlerResult,
    RecvError,
    RecvResult,
    _report_unhandled_callback_exception,
    _routing_id_bytes,
    _raise_result_error,
)
from ._socket_base import _enter_callback, _leave_callback


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
    ):
        self.source_kind = source_kind
        self.state_flags = state_flags
        self.detail_flags = detail_flags
        self.snd_pending_msgs = snd_pending_msgs
        self.rcv_pending_msgs = rcv_pending_msgs


class SocketMonitorEvent:
    def __init__(self, *, event, value, routing_id, local_addr, remote_addr):
        self.event = event
        self.value = value
        self.routing_id = routing_id
        self.local_addr = local_addr
        self.remote_addr = remote_addr


class ServiceMonitorEvent:
    def __init__(
        self,
        *,
        service_kind,
        event_type,
        status,
        error_code,
        value,
        detail_flags,
        service_name,
        endpoint,
        routing_id,
        subject,
        subject_kind,
    ):
        self.service_kind = service_kind
        self.event_type = event_type
        self.status = status
        self.error_code = error_code
        self.value = value
        self.detail_flags = detail_flags
        self.service_name = service_name
        self.endpoint = endpoint
        self.routing_id = routing_id
        self.subject = subject
        self.subject_kind = subject_kind


ServiceEvent = ServiceMonitorEvent


_CALLBACK_SENTINEL = object()
_SOCKET_MONITOR_HANDLER = ctypes.CFUNCTYPE(
    None, ctypes.POINTER(ZlinkMonitorEvent), ctypes.c_void_p
)
_SERVICE_MONITOR_HANDLER = ctypes.CFUNCTYPE(
    None, ctypes.POINTER(ZlinkServiceEvent), ctypes.c_void_p
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

        if isinstance(self, MonitorSocket):
            decode = MonitorSocket._decode_event
            native_handler = _SOCKET_MONITOR_HANDLER
            register = lib().zlink_socket_monitor_handler
        else:
            decode = ServiceMonitor._decode_event
            native_handler = _SERVICE_MONITOR_HANDLER
            register = lib().zlink_service_monitor_handler

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
        return MonitorSnapshot(
            source_kind=int(snapshot.source_kind),
            state_flags=int(snapshot.state_flags),
            detail_flags=int(snapshot.detail_flags),
            snd_pending_msgs=int(snapshot.snd_pending_msgs),
            rcv_pending_msgs=int(snapshot.rcv_pending_msgs),
        )

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
    @staticmethod
    def _decode_event(native):
        return SocketMonitorEvent(
            event=int(native.event),
            value=int(native.value),
            routing_id=_routing_id_bytes(native.routing_id),
            local_addr=_decode_fixed(native.local_addr),
            remote_addr=_decode_fixed(native.remote_addr),
        )

    def recv(self):
        native = ZlinkMonitorEvent()
        rc = lib().zlink_socket_monitor_recv(self._handle, ctypes.byref(native), 0)
        if rc != 0:
            _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
        return self._decode_event(native)

    def on_event(self, handler):
        self._start_event_dispatch(handler)


class ServiceMonitor(_BaseMonitor):
    @staticmethod
    def _decode_event(native):
        return ServiceMonitorEvent(
            service_kind=int(native.service_kind),
            event_type=int(native.event_type),
            status=int(native.status),
            error_code=int(native.error_code),
            value=int(native.value),
            detail_flags=int(native.detail_flags),
            service_name=_decode_fixed(native.service_name),
            endpoint=_decode_fixed(native.endpoint),
            routing_id=_routing_id_bytes(native.routing_id),
            subject=_decode_fixed(native.subject),
            subject_kind=int(native.subject_kind),
        )

    def recv(self):
        native = ZlinkServiceEvent()
        rc = lib().zlink_service_monitor_recv(self._handle, ctypes.byref(native), 0)
        if rc != 0:
            _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
        return self._decode_event(native)

    def on_event(self, handler):
        self._start_event_dispatch(handler)


def open_socket_monitor(socket, events=MonitorEvent.ALL):
    options = ZlinkSocketMonitorOpenOptions()
    options.events = int(events)
    handle = lib().zlink_socket_monitor_open(socket._handle, ctypes.byref(options))
    return MonitorSocket(handle)


def open_service_monitor(service, events=ServiceMonitorMask.ALL):
    options = ZlinkServiceMonitorOpenOptions()
    options.events = int(events)
    handle = lib().zlink_service_monitor_open(service._handle, ctypes.byref(options))
    return ServiceMonitor(handle)
