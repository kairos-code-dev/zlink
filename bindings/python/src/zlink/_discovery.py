# SPDX-License-Identifier: MPL-2.0

import ctypes

from ._ffi import ZlinkMemberPeerEntry, ZlinkMsg, lib
from ._core import _raise_last_error, _msg_to_bytes, _routing_id_bytes


def _decode_fixed(buf):
    return bytes(buf).split(b"\0", 1)[0].decode("utf-8", errors="replace")


def _member_peer_to_dict(entry):
    return {
        "service_type": int(entry.service_type),
        "service_role": int(entry.service_role),
        "service_name": _decode_fixed(entry.service_name),
        "endpoint": _decode_fixed(entry.endpoint),
        "routing_id": _routing_id_bytes(entry.routing_id),
        "value": int(entry.value),
    }


def _query_member_peers(handle, fn, *args):
    count = ctypes.c_size_t(0)
    rc = fn(handle, *args, None, ctypes.byref(count))
    if rc != 0:
        _raise_last_error()
    if count.value == 0:
        return []

    entries = (ZlinkMemberPeerEntry * count.value)()
    rc = fn(handle, *args, entries, ctypes.byref(count))
    if rc != 0:
        _raise_last_error()
    return [_member_peer_to_dict(entries[index]) for index in range(count.value)]


class Registry:
    def __init__(self, ctx):
        self._handle = lib().zlink_registry_new(ctx._handle)
        if not self._handle:
            _raise_last_error()

    def bind(self, pub_endpoint: str, router_endpoint: str):
        rc = lib().zlink_registry_bind(
            self._handle, pub_endpoint.encode(), router_endpoint.encode()
        )
        if rc != 0:
            _raise_last_error()

    def set_id(self, registry_id: int):
        rc = lib().zlink_registry_set_id(self._handle, int(registry_id))
        if rc != 0:
            _raise_last_error()

    def add_peer(self, peer_pub_endpoint: str):
        rc = lib().zlink_registry_add_peer(self._handle, peer_pub_endpoint.encode())
        if rc != 0:
            _raise_last_error()

    def set_heartbeat(self, interval_ms: int, timeout_ms: int):
        rc = lib().zlink_registry_set_heartbeat(
            self._handle, int(interval_ms), int(timeout_ms)
        )
        if rc != 0:
            _raise_last_error()

    def set_broadcast_interval(self, interval_ms: int):
        rc = lib().zlink_registry_set_broadcast_interval(
            self._handle, int(interval_ms)
        )
        if rc != 0:
            _raise_last_error()

    def close(self):
        if not self._handle:
            return
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_registry_destroy(ctypes.byref(handle))
        self._handle = None
        if rc != 0:
            _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class Discovery:
    def __init__(self, ctx, service_type, service_name: str):
        self._handle = lib().zlink_discovery_new(
            ctx._handle, int(service_type), service_name.encode()
        )
        if not self._handle:
            _raise_last_error()

    def connect_registry(self, registry_endpoint: str):
        rc = lib().zlink_discovery_connect_registry(
            self._handle, registry_endpoint.encode()
        )
        if rc != 0:
            _raise_last_error()

    def set_value(self, value: int):
        rc = lib().zlink_discovery_set_value(self._handle, int(value))
        if rc != 0:
            _raise_last_error()

    def get_value(self) -> int:
        value = ctypes.c_int64()
        rc = lib().zlink_discovery_get_value(self._handle, ctypes.byref(value))
        if rc != 0:
            _raise_last_error()
        return int(value.value)

    def set_metadata(self, data):
        if not data:
            rc = lib().zlink_discovery_set_metadata(self._handle, None, 0)
        else:
            raw = memoryview(data).tobytes()
            rc = lib().zlink_discovery_set_metadata(
                self._handle, ctypes.c_char_p(raw), len(raw)
            )
        if rc != 0:
            _raise_last_error()

    def get_metadata(self) -> bytes:
        msg = ZlinkMsg()
        rc = lib().zlink_msg_init(ctypes.byref(msg))
        if rc != 0:
            _raise_last_error()
        try:
            rc = lib().zlink_discovery_get_metadata(self._handle, ctypes.byref(msg))
            if rc != 0:
                _raise_last_error()
            return _msg_to_bytes(msg)
        finally:
            lib().zlink_msg_close(ctypes.byref(msg))

    def member_peers(self):
        return _query_member_peers(self._handle, lib().zlink_discovery_member_peers)

    def member_peer_metadata(self, service_role: int, endpoint: str) -> bytes:
        msg = ZlinkMsg()
        rc = lib().zlink_msg_init(ctypes.byref(msg))
        if rc != 0:
            _raise_last_error()
        try:
            rc = lib().zlink_discovery_member_peer_metadata(
                self._handle, int(service_role), endpoint.encode(), ctypes.byref(msg)
            )
            if rc != 0:
                _raise_last_error()
            return _msg_to_bytes(msg)
        finally:
            lib().zlink_msg_close(ctypes.byref(msg))

    def open_monitor(self, events):
        from ._monitor import open_service_monitor

        return open_service_monitor(self, events)

    def close(self):
        if not self._handle:
            return
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_discovery_destroy(ctypes.byref(handle))
        self._handle = None
        if rc != 0:
            _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
