# SPDX-License-Identifier: MPL-2.0

import ctypes

from ._ffi import (
    ZlinkMonitorSnapshot,
    ZlinkMonitorEvent,
    ZlinkServiceEvent,
    ZlinkServiceMonitorOpenOptions,
    ZlinkSocketMonitorOpenOptions,
    lib,
)
from ._core import ZlinkError, _is_eagain, _raise_last_error, _routing_id_bytes
from ._poller import Poller


def _decode_fixed(buf):
    return bytes(buf).split(b"\0", 1)[0].decode("utf-8", errors="replace")


class MonitorSnapshot:
    def __init__(
        self,
        *,
        source_kind,
        state_flags,
        detail_flags,
        ready_count,
        snd_pending_msgs,
        rcv_pending_msgs,
    ):
        self.source_kind = source_kind
        self.state_flags = state_flags
        self.detail_flags = detail_flags
        self.ready_count = ready_count
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


class _BaseMonitor:
    def __init__(self, handle):
        self._handle = handle
        if not self._handle:
            _raise_last_error()

    def _is_ready(self):
        with Poller() as poller:
            poller.add_socket(self, 1)
            try:
                return bool(poller.poll(0))
            except ZlinkError as exc:
                if _is_eagain(exc):
                    return False
                raise

    def try_recv(self):
        if not self._is_ready():
            return None
        return self.recv()

    def snapshot(self):
        snapshot = ZlinkMonitorSnapshot()
        rc = lib().zlink_monitor_snapshot(self._handle, ctypes.byref(snapshot))
        if rc != 0:
            _raise_last_error()
        return MonitorSnapshot(
            source_kind=int(snapshot.source_kind),
            state_flags=int(snapshot.state_flags),
            detail_flags=int(snapshot.detail_flags),
            ready_count=int(snapshot.ready_count),
            snd_pending_msgs=int(snapshot.snd_pending_msgs),
            rcv_pending_msgs=int(snapshot.rcv_pending_msgs),
        )

    def close(self):
        if not self._handle:
            return
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_monitor_close(ctypes.byref(handle))
        self._handle = None
        if rc != 0:
            _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class MonitorSocket(_BaseMonitor):
    def recv(self):
        native = ZlinkMonitorEvent()
        rc = lib().zlink_socket_monitor_recv(self._handle, ctypes.byref(native), 0)
        if rc != 0:
            _raise_last_error()
        return SocketMonitorEvent(
            event=int(native.event),
            value=int(native.value),
            routing_id=_routing_id_bytes(native.routing_id),
            local_addr=_decode_fixed(native.local_addr),
            remote_addr=_decode_fixed(native.remote_addr),
        )


class ServiceMonitor(_BaseMonitor):
    def recv(self):
        native = ZlinkServiceEvent()
        rc = lib().zlink_service_monitor_recv(self._handle, ctypes.byref(native), 0)
        if rc != 0:
            _raise_last_error()
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


def open_socket_monitor(socket, events):
    options = ZlinkSocketMonitorOpenOptions()
    options.events = int(events)
    handle = lib().zlink_socket_monitor_open(socket._handle, ctypes.byref(options))
    return MonitorSocket(handle)


def open_service_monitor(service, events):
    options = ZlinkServiceMonitorOpenOptions()
    options.events = int(events)
    handle = lib().zlink_service_monitor_open(service._handle, ctypes.byref(options))
    return ServiceMonitor(handle)
