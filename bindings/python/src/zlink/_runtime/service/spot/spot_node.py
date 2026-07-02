# SPDX-License-Identifier: MPL-2.0

import ctypes

from ...._native.ffi import (
    ZlinkMsg,
    ZlinkRoutingId,
    ZlinkSpotNodeOptions,
    lib,
)
from ....contracts.core.options import AutoHwmProfile
from ....contracts.service.codes import (
    SpotNodeMode,
)
from ...native_codes import SpotNodeOption
from ...handles.native_support import (
    CloseError,
    CloseResult,
    ConfigError,
    ConfigResult,
    ConnectError,
    ConnectResult,
    RoutingId,
    _as_bytes_view,
    _copy_routing_id,
    _raise_config_error_from_errno,
    _raise_result_error,
    _routing_id_bytes,
    _validated_c_string_text,
    _validated_c_string_value,
    _validated_int32,
    _validated_routing_id_bytes,
)
from .spot_node_actor_runtime import SpotNodeActorMixin
from .spot_node_snapshot_runtime import SpotNodeSnapshotMixin


class SpotNode(SpotNodeActorMixin, SpotNodeSnapshotMixin):
    def __init__(self, ctx, mode: int | SpotNodeMode | None = None):
        self._ctx = ctx
        native_options = None
        options_ptr = None
        if mode is not None:
            native_options = ZlinkSpotNodeOptions()
            native_options.mode = int(mode)
            options_ptr = ctypes.byref(native_options)
        self._handle = lib().zlink_spot_node_new(ctx._handle, options_ptr)
        if not self._handle:
            _raise_config_error_from_errno()
        self._spots = set()
        self._actor_admission_handler = None
        self._actor_admission_cb = None
        self._actor_request_pending = {}
        self._actor_join_pending = {}
        self._actor_join_entry_spot_pending = {}
        self._actor_lookup_pending = {}
        self._actor_reply_handler = None
        self._actor_join_handler = None
        self._actor_join_entry_spot_handler = None
        self._actor_lookup_handler = None
        self._channel_dealers = {}

    def set_pub_bind(self, endpoint: str):
        rc = lib().zlink_spot_node_set_pub_bind(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def set_router_bind(self, endpoint: str):
        rc = lib().zlink_spot_node_set_router_bind(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def last_endpoint(self) -> str:
        return self.status().local_endpoint

    def connect_peer(self, endpoint: str):
        rc = lib().zlink_spot_node_connect_peer(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())

    def disconnect_peer(self, endpoint: str):
        rc = lib().zlink_spot_node_disconnect_peer(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())

    def disconnect_peer_rid(self, target_node_rid):
        native = _copy_routing_id(target_node_rid)
        rc = lib().zlink_spot_node_disconnect_peer_rid(
            self._handle, ctypes.byref(native)
        )
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())

    def _get_spot_node_option_int32(self, option: int | SpotNodeOption):
        opt = int(option)
        native = ctypes.c_int32(0)
        size = ctypes.c_size_t(ctypes.sizeof(native))
        rc = lib().zlink_get_spot_node_option(
            self._handle,
            opt,
            ctypes.byref(native),
            ctypes.byref(size),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return int(native.value)

    def _set_spot_node_option(self, option: int | SpotNodeOption, value):
        view = _as_bytes_view(value)
        opt = int(option)
        if view.nbytes == 0:
            rc = lib().zlink_set_spot_node_option(self._handle, opt, None, 0)
        elif view.readonly:
            raw = view.tobytes()
            rc = lib().zlink_set_spot_node_option(
                self._handle, opt, ctypes.c_char_p(raw), len(raw)
            )
        else:
            rc = lib().zlink_set_spot_node_option(
                self._handle,
                opt,
                ctypes.c_void_p(
                    ctypes.addressof(
                        (ctypes.c_ubyte * view.nbytes).from_buffer(view)
                    )
                ),
                view.nbytes,
            )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def set_router_high_water_mark(self, value: int):
        native = ctypes.c_int32(_validated_int32(value))
        self._set_spot_node_option(
            SpotNodeOption.ROUTER_HWM,
            ctypes.string_at(ctypes.byref(native), ctypes.sizeof(native)),
        )

    def set_pubsub_high_water_mark(self, value: int):
        native = ctypes.c_int32(_validated_int32(value))
        self._set_spot_node_option(
            SpotNodeOption.PUBSUB_HWM,
            ctypes.string_at(ctypes.byref(native), ctypes.sizeof(native)),
        )

    def set_router_hwm_profile(self, value: int | AutoHwmProfile):
        native = ctypes.c_int32(_validated_int32(int(value)))
        self._set_spot_node_option(
            SpotNodeOption.ROUTER_HWM_PROFILE,
            ctypes.string_at(ctypes.byref(native), ctypes.sizeof(native)),
        )

    def set_pubsub_hwm_profile(self, value: int | AutoHwmProfile):
        native = ctypes.c_int32(_validated_int32(int(value)))
        self._set_spot_node_option(
            SpotNodeOption.PUBSUB_HWM_PROFILE,
            ctypes.string_at(ctypes.byref(native), ctypes.sizeof(native)),
        )

    @property
    def router_hwm_profile(self):
        return AutoHwmProfile(self._get_spot_node_option_int32(SpotNodeOption.ROUTER_HWM_PROFILE))

    @router_hwm_profile.setter
    def router_hwm_profile(self, value):
        self.set_router_hwm_profile(value)

    @property
    def router_high_water_mark(self):
        return self._get_spot_node_option_int32(SpotNodeOption.ROUTER_HWM)

    @router_high_water_mark.setter
    def router_high_water_mark(self, value):
        self.set_router_high_water_mark(value)

    @property
    def pubsub_hwm_profile(self):
        return AutoHwmProfile(self._get_spot_node_option_int32(SpotNodeOption.PUBSUB_HWM_PROFILE))

    @pubsub_hwm_profile.setter
    def pubsub_hwm_profile(self, value):
        self.set_pubsub_hwm_profile(value)

    @property
    def pubsub_high_water_mark(self):
        return self._get_spot_node_option_int32(SpotNodeOption.PUBSUB_HWM)

    @pubsub_high_water_mark.setter
    def pubsub_high_water_mark(self, value):
        self.set_pubsub_high_water_mark(value)

    @property
    def dispatch_workers_min(self):
        return self._get_spot_node_option_int32(SpotNodeOption.DISPATCH_WORKERS_MIN)

    @dispatch_workers_min.setter
    def dispatch_workers_min(self, value: int):
        native = ctypes.c_int32(_validated_int32(value))
        self._set_spot_node_option(
            SpotNodeOption.DISPATCH_WORKERS_MIN,
            ctypes.string_at(ctypes.byref(native), ctypes.sizeof(native)),
        )

    @property
    def dispatch_workers_max(self):
        return self._get_spot_node_option_int32(SpotNodeOption.DISPATCH_WORKERS_MAX)

    @dispatch_workers_max.setter
    def dispatch_workers_max(self, value: int):
        native = ctypes.c_int32(_validated_int32(value))
        self._set_spot_node_option(
            SpotNodeOption.DISPATCH_WORKERS_MAX,
            ctypes.string_at(ctypes.byref(native), ctypes.sizeof(native)),
        )

    def create_spot(self):
        from .spot import Spot
        return Spot._create(self)

    def entry_spot(self):
        spot_handle = ctypes.c_void_p()
        rc = lib().zlink_spot_node_entry_spot(self._handle, ctypes.byref(spot_handle))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        from .spot import Spot
        return Spot._wrap_handle(self, spot_handle.value)

    def spot_lookup(self, spot_rid):
        native_rid = _copy_routing_id(spot_rid)
        spot_handle = ctypes.c_void_p()
        rc = lib().zlink_spot_node_spot_lookup(
            self._handle, ctypes.byref(native_rid), ctypes.byref(spot_handle)
        )
        if rc != 0:
            if rc == int(ConfigResult.NOT_FOUND):
                return None
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if not spot_handle.value:
            return None
        from .spot import Spot
        return Spot._wrap_handle(self, spot_handle.value)

    def get_or_create_spot(self, spot_rid):
        native_rid = _copy_routing_id(spot_rid)
        spot_handle = ctypes.c_void_p()
        created = ctypes.c_uint32()
        rc = lib().zlink_spot_node_spot_get_or_new(
            self._handle,
            ctypes.byref(native_rid),
            ctypes.byref(spot_handle),
            ctypes.byref(created),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        from .spot import Spot
        return Spot._wrap_handle(self, spot_handle.value), bool(created.value)

    def create_route_bridge(self, options=None):
        from .spot_route_bridge import SpotRouteBridge
        return SpotRouteBridge(self, options)

    def create_publisher(self):
        from .spot_route_bridge import SpotNodePublisher
        return SpotNodePublisher(self)

    def _register_spot(self, spot):
        self._spots.add(spot)

    def _unregister_spot(self, spot):
        self._spots.discard(spot)

    def _set_option(self, option: int, value):
        if int(option) == 5:
            self.set_routing_id(value)
            return
        self._set_spot_node_option(option, value)

    def set_routing_id(self, routing_id):
        raw = _validated_routing_id_bytes(routing_id)
        rc = lib().zlink_set_routing_id(
            self._handle, ctypes.c_char_p(raw), len(raw)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def get_routing_id(self):
        routing_id = ZlinkRoutingId()
        rc = lib().zlink_get_routing_id(self._handle, ctypes.byref(routing_id))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return _routing_id_bytes(routing_id)

    @property
    def routing_id(self):
        return RoutingId(self.get_routing_id())

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
            None
            if hostname is None
            else _validated_c_string_text(hostname, field="hostname")
        )
        rc = lib().zlink_set_tls_client(
            self._handle, ca_value, host_value, int(trust_system)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def close(self):
        if not self._handle:
            return
        first_error = None
        for spot in tuple(self._spots):
            try:
                spot.close()
            except CloseError as exc:
                if first_error is None:
                    first_error = exc
        handle = ctypes.c_void_p(self._handle)
        rc = lib().zlink_spot_node_destroy(ctypes.byref(handle))
        self._handle = None
        self._ctx = None
        self._spots.clear()
        self._actor_request_pending.clear()
        self._actor_join_pending.clear()
        self._actor_join_entry_spot_pending.clear()
        self._actor_lookup_pending.clear()
        self._actor_admission_handler = None
        self._actor_admission_cb = None
        self._actor_reply_handler = None
        self._actor_join_handler = None
        self._actor_lookup_handler = None
        if rc != 0 and first_error is None:
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())
        if first_error is not None:
            raise first_error

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()


def create_spot_node(ctx, mode=None):
    return SpotNode(ctx, mode)


SpotNode.__module__ = "zlink.contracts.service.spot"

__all__ = ["SpotNode", "create_spot_node"]
