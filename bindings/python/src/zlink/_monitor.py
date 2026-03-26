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
from ._core import _raise_last_error, _routing_id_bytes


def _decode_fixed(buf):
    return bytes(buf).split(b"\0", 1)[0].decode("utf-8", errors="replace")


class MonitorSocket:
    def __init__(self, handle):
        self._handle = handle
        if not self._handle:
            _raise_last_error()

    def recv(self):
        native = ZlinkMonitorEvent()
        rc = lib().zlink_socket_monitor_recv(self._handle, ctypes.byref(native))
        if rc != 0:
            _raise_last_error()
        return {
            "event": int(native.event),
            "value": int(native.value),
            "routing_id": _routing_id_bytes(native.routing_id),
            "local_addr": _decode_fixed(native.local_addr),
            "remote_addr": _decode_fixed(native.remote_addr),
        }

    def snapshot(self):
        snapshot = ZlinkMonitorSnapshot()
        rc = lib().zlink_monitor_snapshot(self._handle, ctypes.byref(snapshot))
        if rc != 0:
            _raise_last_error()
        return {
            "source_kind": int(snapshot.source_kind),
            "state_flags": int(snapshot.state_flags),
            "detail_flags": int(snapshot.detail_flags),
            "ready_count": int(snapshot.ready_count),
            "snd_pending_msgs": int(snapshot.snd_pending_msgs),
            "rcv_pending_msgs": int(snapshot.rcv_pending_msgs),
        }

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


class ServiceMonitor:
    def __init__(self, handle):
        self._handle = handle
        if not self._handle:
            _raise_last_error()

    def recv(self):
        native = ZlinkServiceEvent()
        rc = lib().zlink_service_monitor_recv(self._handle, ctypes.byref(native))
        if rc != 0:
            _raise_last_error()
        return {
            "service_kind": int(native.service_kind),
            "event_type": int(native.event_type),
            "status": int(native.status),
            "error_code": int(native.error_code),
            "value": int(native.value),
            "detail_flags": int(native.detail_flags),
            "service_name": _decode_fixed(native.service_name),
            "endpoint": _decode_fixed(native.endpoint),
            "routing_id": _routing_id_bytes(native.routing_id),
            "subject": _decode_fixed(native.subject),
            "subject_kind": int(native.subject_kind),
        }

    def snapshot(self):
        snapshot = ZlinkMonitorSnapshot()
        rc = lib().zlink_monitor_snapshot(self._handle, ctypes.byref(snapshot))
        if rc != 0:
            _raise_last_error()
        return {
            "source_kind": int(snapshot.source_kind),
            "state_flags": int(snapshot.state_flags),
            "detail_flags": int(snapshot.detail_flags),
            "ready_count": int(snapshot.ready_count),
            "snd_pending_msgs": int(snapshot.snd_pending_msgs),
            "rcv_pending_msgs": int(snapshot.rcv_pending_msgs),
        }

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
