# SPDX-License-Identifier: MPL-2.0

import ctypes

from ....contracts.service.discovery import Registry as _ContractRegistry
from ...._native.ffi import (
    ZlinkRegistryServiceSummaryEntry,
    ZlinkRegistryServiceSummaryFilter,
    ZlinkRegistryStatus,
    lib,
)
from ...handles.native_support import (
    BindError,
    BindResult,
    CloseError,
    CloseResult,
    ConfigError,
    ConfigResult,
    ConnectError,
    ConnectResult,
    _raise_config_error_from_errno,
    _raise_result_error,
    _validated_c_string_text,
    _validated_uint32,
)
from ..discovery.discovery import (
    RegistryServiceSummaryEntry,
    RegistryServiceSummaryFilter,
    RegistryStatus,
    _build_topology_filter,
    _decode_fixed,
    _query_member_peers,
    _query_topology,
)


class Registry(_ContractRegistry):
    def __init__(self, ctx):
        self._handle = lib().zlink_registry_new(ctx._handle)
        if not self._handle:
            _raise_config_error_from_errno()

    def bind(self, pub_endpoint: str, router_endpoint: str):
        rc = lib().zlink_registry_bind(
            self._handle,
            _validated_c_string_text(
                pub_endpoint, field="pub_endpoint", max_length=255
            ),
            _validated_c_string_text(
                router_endpoint, field="router_endpoint", max_length=255
            ),
        )
        if rc != 0:
            _raise_result_error(BindError, BindResult, rc, lib().zlink_errno())

    REGISTRY_OPT_ID = 0x3801
    REGISTRY_OPT_HEARTBEAT_INTERVAL_MS = 0x3802
    REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS = 0x3803
    REGISTRY_OPT_BROADCAST_INTERVAL_MS = 0x3804

    def set_option(self, option: int, value: int):
        rc = lib().zlink_registry_set(
            self._handle,
            int(option),
            _validated_uint32(value, field="value"),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def get_option(self, option: int) -> int:
        error = ctypes.c_int()
        value = lib().zlink_registry_get(self._handle, int(option), ctypes.byref(error))
        if error.value != 0:
            _raise_result_error(
                ConfigError, ConfigResult, error.value, lib().zlink_errno()
            )
        return int(value)

    def set_id(self, registry_id: int):
        self.set_option(self.REGISTRY_OPT_ID, registry_id)

    def add_peer(self, peer_pub_endpoint: str):
        rc = lib().zlink_registry_add_peer(
            self._handle,
            _validated_c_string_text(
                peer_pub_endpoint, field="peer_pub_endpoint", max_length=255
            ),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def set_tls_server(self, cert: str, key: str, require_client_cert: bool = False):
        rc = lib().zlink_set_tls_server(
            self._handle,
            _validated_c_string_text(cert, field="cert"),
            _validated_c_string_text(key, field="key"),
            int(require_client_cert),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def set_tls_client(
        self, ca_cert: str | None, hostname: str | None, trust_system: bool = False
    ):
        ca_value = (
            None
            if ca_cert is None
            else _validated_c_string_text(ca_cert, field="ca_cert")
        )
        host_value = (
            None if hostname is None else _validated_c_string_text(hostname, field="hostname")
        )
        rc = lib().zlink_set_tls_client(
            self._handle, ca_value, host_value, int(trust_system)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def set_heartbeat(self, interval_ms: int, timeout_ms: int):
        self.set_option(
            self.REGISTRY_OPT_HEARTBEAT_INTERVAL_MS,
            _validated_uint32(interval_ms, field="interval_ms"),
        )
        self.set_option(
            self.REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS,
            _validated_uint32(timeout_ms, field="timeout_ms"),
        )

    def set_broadcast_interval(self, interval_ms: int):
        self.set_option(
            self.REGISTRY_OPT_BROADCAST_INTERVAL_MS,
            _validated_uint32(interval_ms, field="interval_ms"),
        )

    def status(self):
        native = ZlinkRegistryStatus()
        rc = lib().zlink_registry_status(self._handle, ctypes.byref(native))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return RegistryStatus(
            registry_id=int(native.registry_id),
            bind_endpoint=_decode_fixed(native.bind_endpoint),
            state=RegistryState(int(native.state)),
            topology_entry_count=int(native.topology_entry_count),
            peer_registry_count=int(native.peer_registry_count),
            connected_peer_registry_count=int(native.connected_peer_registry_count),
            list_seq=int(native.list_seq),
            last_error=int(native.last_error),
            last_changed_ms=int(native.last_changed_ms),
        )

    def service_summary(self, filter_=None):
        count = ctypes.c_size_t(0)
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = ZlinkRegistryServiceSummaryFilter()
            filter_native.auto_connect_type = (
                0
                if filter_.auto_connect_type is None
                else int(filter_.auto_connect_type)
            )
            filter_native.service_role = 0 if filter_.service_role is None else int(filter_.service_role)
            filter_native.channel_name = _validated_c_string_text(
                filter_.channel_name or "",
                field="channel_name",
                max_length=255,
            )
            filter_ptr = ctypes.byref(filter_native)

        rc = lib().zlink_registry_service_summary(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []

        entries = (ZlinkRegistryServiceSummaryEntry * count.value)()
        rc = lib().zlink_registry_service_summary(
            self._handle, filter_ptr, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            RegistryServiceSummaryEntry(
                auto_connect_type=AutoConnectType(int(entry.auto_connect_type)),
                service_role=ServiceRole(int(entry.service_role)),
                channel_name=_decode_fixed(entry.channel_name),
                total_count=int(entry.total_count),
                connecting_count=int(entry.connecting_count),
                ready_count=int(entry.ready_count),
                error_count=int(entry.error_count),
                stopped_count=int(entry.stopped_count),
                last_reported_ms=int(entry.last_reported_ms),
            )
            for entry in entries[: count.value]
        ]

    def member_peers(self, channel_name):
        return _query_member_peers(
            self._handle,
            lib().zlink_registry_member_peers,
            _validated_c_string_text(
                channel_name, field="channel_name", max_length=255
            ),
        )

    def topology(self, filter_=None):
        if filter_ is None:
            return _query_topology(self._handle, lib().zlink_registry_topology)
        return _query_topology(
            self._handle,
            lib().zlink_registry_topology,
            ctypes.byref(_build_topology_filter(filter_)),
        )

    def close(self):
        if not self._handle:
            return
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_registry_destroy(ctypes.byref(handle))
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


def create_registry(ctx):
    return Registry(ctx)


__all__ = ["Registry", "create_registry"]
