# SPDX-License-Identifier: MPL-2.0

import ctypes

from ...._native.ffi import lib
from ...handles.native_support import (
    CloseError,
    CloseResult,
    ConfigError,
    ConfigResult,
    ConnectError,
    ConnectResult,
    _raise_config_error_from_errno,
    _raise_result_error,
    _validated_c_string_text,
)
from ..discovery.discovery import _build_topology_filter, _query_topology


class RegistryQueryClient:
    def __init__(self, ctx):
        self._handle = lib().zlink_registry_query_client_new(ctx._handle)
        if not self._handle:
            _raise_config_error_from_errno()

    def connect(self, endpoint: str):
        rc = lib().zlink_registry_query_client_connect(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())

    def topology(self, filter_=None):
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = _build_topology_filter(filter_)
            filter_ptr = ctypes.byref(filter_native)
        return _query_topology(self._handle, lib().zlink_registry_query_client_topology, filter_ptr)

    def close(self):
        if not self._handle:
            return
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_registry_query_client_destroy(ctypes.byref(handle))
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


def create_registry_query_client(ctx):
    return RegistryQueryClient(ctx)


__all__ = ["RegistryQueryClient", "create_registry_query_client"]
