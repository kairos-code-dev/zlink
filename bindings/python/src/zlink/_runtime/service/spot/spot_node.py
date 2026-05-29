# SPDX-License-Identifier: MPL-2.0

import ctypes

from ...._native.ffi import (
    ZlinkActorJoinEntrySpotResult,
    ZlinkActorJoinResult,
    ZlinkActorLookupResult,
    ZlinkActorRef,
    ZlinkMsg,
    ZlinkRoutingId,
    ZlinkSpotNodeActorEntry,
    ZlinkSpotNodeOptions,
    ZlinkSpotNodePeerEntry,
    ZlinkSpotNodePeerFilter,
    ZlinkSpotNodeSocketEntry,
    ZlinkSpotNodeSocketFilter,
    ZlinkSpotNodeSpotEntry,
    ZlinkSpotNodeStatus,
    ZlinkSpotNodeSubjectEntry,
    ZlinkSpotNodeSubjectFilter,
    lib,
)
from ....contracts.core.options import AutoHwmProfile
from ....contracts.service.codes import (
    SpotKind,
    SpotNodeMode,
    SpotNodeOption,
    SpotNodeSocketOwner,
    SpotNodeState,
    SpotPeerKind,
    SpotPeerSource,
    SpotPeerState,
    SpotRole,
    SubjectKind,
)
from ....contracts.sockets.codes import SocketType
from ...eventing.monitor import _monitor_status_from_native
from ...handles.native_support import (
    CloseError,
    CloseResult,
    ConfigError,
    ConfigResult,
    ConnectError,
    ConnectResult,
    RequestResult,
    RoutingId,
    SubmitError,
    SubmitResult,
    _REPLY_HANDLER,
    _as_bytes_view,
    _copy_routing_id,
    _raise_config_error_from_errno,
    _raise_result_error,
    _request_result_from_code,
    _request_result_internal_errno,
    _routing_id_bytes,
    _validated_c_string_text,
    _validated_c_string_value,
    _validated_int32,
    _validated_routing_id_bytes,
)
from .actor_ops import (
    ActorDestroyOp,
    ActorJoinEntrySpotOp,
    ActorJoinOp,
    ActorLeaveOp,
    ActorLookupOp,
)
from .native_parts import close_native_parts as _close_native_parts
from .native_parts import prepare_native_parts as _prepare_native_parts
from .spot_models_runtime import (
    ActorJoinEntrySpotResult,
    ActorJoinResult,
    ActorLookupResult,
    ActorRef,
    SpotNodeActorEntry,
    SpotNodePeerEntry,
    SpotNodeSocketEntry,
    SpotNodeSpotEntry,
    SpotNodeStatus,
    SpotNodeSubjectEntry,
    _actor_id_bytes,
    _actor_ref_from_native,
    _actor_ref_to_native,
)
from .spot_ops import SendOp
from .spot_receive import _clone_payload, _make_message_list
from .spot import (
    _ACTOR_JOIN_ENTRY_SPOT_HANDLER,
    _ACTOR_JOIN_HANDLER,
    _ACTOR_LOOKUP_HANDLER,
    _decode_fixed,
    _fixed_buffer_value,
    _timeout_to_ms,
)


class SpotNode:
    def __init__(self, ctx, mode: int | SpotNodeMode | None = None):
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

    def connect_router_channel_peer(self, channel_name: str, endpoint: str):
        rc = lib().zlink_spot_node_connect_router_channel_peer(
            self._handle,
            _validated_c_string_text(channel_name, field="channel_name", max_length=255),
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())

    def disconnect_router_channel_peer(self, channel_name: str, endpoint: str):
        rc = lib().zlink_spot_node_disconnect_router_channel_peer(
            self._handle,
            _validated_c_string_text(channel_name, field="channel_name", max_length=255),
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())

    def disconnect_router_channel_peer_rid(self, channel_name: str, peer_rid):
        native = _copy_routing_id(peer_rid)
        rc = lib().zlink_spot_node_disconnect_router_channel_peer_rid(
            self._handle,
            _validated_c_string_text(channel_name, field="channel_name", max_length=255),
            ctypes.byref(native),
        )
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())

    def attach_discovery(self, discovery):
        rc = lib().zlink_spot_node_attach_discovery(
            self._handle, discovery._handle
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def attach_spot_route_channel_discovery(self, channel_name: str, discovery):
        rc = lib().zlink_spot_node_attach_router_channel_discovery(
            self._handle,
            _validated_c_string_text(channel_name, field="channel_name", max_length=255),
            discovery._handle,
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def attach_channel_dealer(self, discovery, dealer):
        rc = lib().zlink_spot_node_attach_channel_dealer(
            self._handle, discovery._handle, dealer._handle
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        self._channel_dealers[dealer._handle] = dealer

    def attach_channel_dealer_manual(self, channel_name, dealer):
        channel_bytes = _validated_c_string_value(
            channel_name, field="channel_name", max_length=255
        )
        rc = lib().zlink_spot_node_attach_channel_dealer_manual(
            self._handle,
            channel_bytes,
            dealer._handle,
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        self._channel_dealers[dealer._handle] = dealer

    def attach_pub_ingress(self, pub):
        rc = lib().zlink_spot_node_attach_pub_ingress(self._handle, pub._handle)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

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
        from .spot import Spot
        return Spot._wrap_handle(self, spot_handle.value), bool(created.value)

    def actor(self, actor_id):
        native = ZlinkActorRef()
        rc = lib().zlink_spot_node_actor_new(
            self._handle,
            _actor_id_bytes(actor_id),
            ctypes.byref(native),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        from .actor import Actor
        return Actor(self, _actor_ref_from_native(native))

    def actor_lookup(self, actor_id):
        native = ZlinkActorRef()
        rc = lib().zlink_spot_node_actor_lookup(
            self._handle, _actor_id_bytes(actor_id), ctypes.byref(native)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return _actor_ref_from_native(native)

    def destroy_actor(self, actor_ref):
        return ActorDestroyOp(self, actor_ref)

    def remote_actor_get_ref(self, target_node_rid, actor_id):
        return ActorLookupOp(self, target_node_rid, actor_id)

    def join_actor(self, actor_ref, dest_node_rid, dest_spot_rid):
        return ActorJoinOp(self, actor_ref, dest_node_rid, dest_spot_rid)

    def join_actor_entry_spot(self, actor_ref, dest_node_rid):
        return ActorJoinEntrySpotOp(self, actor_ref, dest_node_rid)

    def leave_actor(self, actor_ref, current_spot_rid):
        return ActorLeaveOp(self, actor_ref, current_spot_rid)

    def send_bound_session_msg(self, actor_ref):
        return SendOp(
            self,
            lambda parts, flags: self._actor_send_bound_session_submit(
                actor_ref, parts, flags
            ),
        )

    def _actor_send_bound_session_submit(self, actor_ref, parts, flags=0):
        native_parts = _clone_payload(parts)
        if len(native_parts) != 1:
            _close_native_parts(native_parts)
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        native_actor = _actor_ref_to_native(actor_ref)
        rc = lib().zlink_spot_node_actor_send_bound_session_msg(
            self._handle,
            ctypes.byref(native_actor),
            ctypes.byref(native_parts[0]),
            int(flags),
        )
        if rc != 0:
            _close_native_parts(native_parts)
            if int(flags) & 1 and rc == int(SubmitResult.BACKPRESSURED):
                return False
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())
        return True

    def _ensure_actor_reply_handler(self):
        if self._actor_reply_handler is None:
            self._actor_reply_handler = _REPLY_HANDLER(self._on_actor_reply)
        return self._actor_reply_handler

    def _ensure_actor_join_handler(self):
        if self._actor_join_handler is None:
            self._actor_join_handler = _ACTOR_JOIN_HANDLER(self._on_actor_join_reply)
        return self._actor_join_handler

    def _ensure_actor_join_entry_spot_handler(self):
        if self._actor_join_entry_spot_handler is None:
            self._actor_join_entry_spot_handler = _ACTOR_JOIN_ENTRY_SPOT_HANDLER(
                self._on_actor_join_entry_spot_reply
            )
        return self._actor_join_entry_spot_handler

    def _ensure_actor_lookup_handler(self):
        if self._actor_lookup_handler is None:
            self._actor_lookup_handler = _ACTOR_LOOKUP_HANDLER(self._on_actor_lookup_reply)
        return self._actor_lookup_handler

    def _on_actor_join_reply(self, result_ptr, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._actor_join_pending.pop(handle, None)
        if pending is None:
            # Fall back to the plain reply path used by destroy/leave/bind/unbind.
            if not result_ptr:
                self._on_actor_reply(int(RequestResult.INTERNAL_ERROR), parts, part_count, userdata)
                return
            self._on_actor_reply(result_ptr.contents.result, parts, part_count, userdata)
            return
        if not result_ptr:
            join_result = ActorJoinResult(
                result=RequestResult.INTERNAL_ERROR,
                join_result_code=0,
                actor=ActorRef(node_rid=RoutingId(b""), actor_id="", generation=0),
                joined_spot_rid=RoutingId(b""),
                join_epoch=0,
                flags=0,
            )
            pending.resolve(join_result, [], _request_result_internal_errno(RequestResult.INTERNAL_ERROR))
            return
        native = result_ptr.contents
        result = _request_result_from_code(int(native.result))
        join_result = ActorJoinResult(
            result=result,
            join_result_code=int(native.join_result_code),
            actor=_actor_ref_from_native(native.actor),
            joined_spot_rid=_routing_id_bytes(native.joined_spot_rid),
            join_epoch=int(native.join_epoch),
            flags=int(native.flags),
        )
        messages = []
        if result == RequestResult.OK:
            messages = _make_message_list(parts, part_count)
        pending.resolve(join_result, messages, _request_result_internal_errno(result))

    def _on_actor_join_entry_spot_reply(self, result_ptr, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._actor_join_entry_spot_pending.pop(handle, None)
        if pending is None:
            return
        if not result_ptr:
            join_result = ActorJoinEntrySpotResult(
                result=RequestResult.INTERNAL_ERROR,
                actor=ActorRef(node_rid=RoutingId(b""), actor_id="", generation=0),
                target_node_rid=RoutingId(b""),
                join_epoch=0,
                flags=0,
            )
            pending.resolve(join_result, _request_result_internal_errno(RequestResult.INTERNAL_ERROR))
            return
        native = result_ptr.contents
        result = _request_result_from_code(int(native.result))
        join_result = ActorJoinEntrySpotResult(
            result=result,
            actor=_actor_ref_from_native(native.actor),
            target_node_rid=_routing_id_bytes(native.target_node_rid),
            join_epoch=int(native.join_epoch),
            flags=int(native.flags),
        )
        pending.resolve(join_result, _request_result_internal_errno(result))

    def _on_actor_lookup_reply(self, result_ptr, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._actor_lookup_pending.pop(handle, None)
        if pending is None:
            return
        if not result_ptr:
            lookup_result = ActorLookupResult(
                result=RequestResult.INTERNAL_ERROR,
                actor=ActorRef(node_rid=RoutingId(b""), actor_id="", generation=0),
                flags=0,
            )
            pending.resolve(lookup_result, _request_result_internal_errno(RequestResult.INTERNAL_ERROR))
            return
        native = result_ptr.contents
        result = _request_result_from_code(int(native.result))
        lookup_result = ActorLookupResult(
            result=result,
            actor=_actor_ref_from_native(native.actor),
            flags=int(native.flags),
        )
        pending.resolve(lookup_result, _request_result_internal_errno(result))

    def _on_actor_reply(self, result_code, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._actor_request_pending.pop(handle, None)
        if pending is None:
            return
        result = _request_result_from_code(int(result_code))
        received = []
        if result == RequestResult.OK:
            received = _make_message_list(parts, part_count)
        pending.resolve(result, received, _request_result_internal_errno(result))

    def _submit_actor_join(self, actor_ref, dest_node_rid, dest_spot_rid, parts, pending, flags=0, timeout=0):
        native_actor = _actor_ref_to_native(actor_ref)
        native_node = _copy_routing_id(dest_node_rid)
        native_spot = _copy_routing_id(dest_spot_rid)
        native_parts = _clone_payload(parts)
        native_array = _prepare_native_parts(native_parts)
        handle = id(pending)
        self._actor_join_pending[handle] = pending
        rc = lib().zlink_spot_node_actor_join_spot(
            self._handle,
            ctypes.byref(native_actor),
            ctypes.byref(native_node),
            ctypes.byref(native_spot),
            native_array if native_parts else None,
            len(native_parts),
            self._ensure_actor_join_handler(),
            ctypes.c_void_p(handle),
            int(flags),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            self._actor_join_pending.pop(handle, None)
            _close_native_parts(native_parts)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())
        return True

    def _submit_actor_join_entry_spot(self, actor_ref, dest_node_rid, pending, timeout=0):
        native_actor = _actor_ref_to_native(actor_ref)
        native_node = _copy_routing_id(dest_node_rid)
        handle = id(pending)
        self._actor_join_entry_spot_pending[handle] = pending
        rc = lib().zlink_spot_node_actor_join_entry_spot(
            self._handle,
            ctypes.byref(native_actor),
            ctypes.byref(native_node),
            self._ensure_actor_join_entry_spot_handler(),
            ctypes.c_void_p(handle),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            self._actor_join_entry_spot_pending.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())
        return True

    def _submit_actor_leave(self, actor_ref, current_spot_rid, pending, timeout=0):
        native_actor = _actor_ref_to_native(actor_ref)
        native_spot = _copy_routing_id(current_spot_rid)
        handle = id(pending)
        self._actor_request_pending[handle] = pending
        rc = lib().zlink_spot_node_actor_leave_spot(
            self._handle,
            ctypes.byref(native_actor),
            ctypes.byref(native_spot),
            self._ensure_actor_reply_handler(),
            ctypes.c_void_p(handle),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            self._actor_request_pending.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())
        return True

    def _submit_actor_destroy(self, actor_ref, pending, timeout=0):
        native_actor = _actor_ref_to_native(actor_ref)
        handle = id(pending)
        self._actor_request_pending[handle] = pending
        rc = lib().zlink_spot_node_actor_destroy(
            self._handle,
            ctypes.byref(native_actor),
            self._ensure_actor_reply_handler(),
            ctypes.c_void_p(handle),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            self._actor_request_pending.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())
        return True

    def _submit_actor_lookup(self, target_node_rid, actor_id, pending, timeout=0):
        native_rid = _copy_routing_id(target_node_rid)
        handle = id(pending)
        self._actor_lookup_pending[handle] = pending
        rc = lib().zlink_remote_actor_get_ref(
            self._handle,
            ctypes.byref(native_rid),
            _actor_id_bytes(actor_id),
            self._ensure_actor_lookup_handler(),
            ctypes.c_void_p(handle),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            self._actor_lookup_pending.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())
        return True

    def spots(self):
        count = ctypes.c_size_t()
        rc = lib().zlink_spot_node_spots(self._handle, None, ctypes.byref(count))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeSpotEntry * int(count.value))()
        rc = lib().zlink_spot_node_spots(
            self._handle, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodeSpotEntry(
                spot_rid=_routing_id_bytes(entry.spot_rid),
                spot_kind=SpotKind(int(entry.spot_kind)),
                dispatch_handler_attached=bool(entry.dispatch_handler_attached),
                joined_actor_count=int(entry.joined_actor_count),
                pending_actor_join_count=int(entry.pending_actor_join_count),
                route_synced=bool(entry.route_synced),
                last_changed_ms=int(entry.last_changed_ms),
            )
            for entry in entries[: int(count.value)]
        ]

    def actors(self):
        count = ctypes.c_size_t()
        rc = lib().zlink_spot_node_actors(self._handle, None, ctypes.byref(count))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeActorEntry * int(count.value))()
        rc = lib().zlink_spot_node_actors(
            self._handle, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodeActorEntry(
                actor=_actor_ref_from_native(entry.actor),
                current_spot_rid=_routing_id_bytes(entry.current_spot_rid),
                current_spot_kind=SpotKind(int(entry.current_spot_kind)),
                route_synced=bool(entry.route_synced),
                pending_message_count=int(entry.pending_message_count),
                last_changed_ms=int(entry.last_changed_ms),
            )
            for entry in entries[: int(count.value)]
        ]

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

    def status(self):
        native = ZlinkSpotNodeStatus()
        rc = lib().zlink_spot_node_status(self._handle, ctypes.byref(native))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return SpotNodeStatus(
            channel_name=_decode_fixed(native.channel_name),
            local_endpoint=_decode_fixed(native.local_endpoint),
            node_routing_id=_routing_id_bytes(native.node_routing_id),
            state=SpotNodeState(int(native.state)),
            configured_peer_count=int(native.configured_peer_count),
            active_peer_count=int(native.active_peer_count),
            connected_peer_count=int(native.connected_peer_count),
            subject_count=int(native.subject_count),
            ready_subject_count=int(native.ready_subject_count),
            disconnected_sub_target_count=int(native.disconnected_sub_target_count),
            disconnected_routed_target_count=int(native.disconnected_routed_target_count),
            last_error=int(native.last_error),
            last_changed_ms=int(native.last_changed_ms),
        )

    def peers(self):
        return self.peers_query(None)

    def peers_query(self, filter_=None):
        count = ctypes.c_size_t()
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = ZlinkSpotNodePeerFilter()
            filter_native.peer_endpoint = _fixed_buffer_value(
                filter_.peer_endpoint, 256
            )
            filter_native.source = 0 if filter_.source is None else int(filter_.source)
            filter_native.state = 0 if filter_.state is None else int(filter_.state)
            filter_ptr = ctypes.byref(filter_native)
        rc = lib().zlink_spot_node_peers(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodePeerEntry * int(count.value))()
        rc = lib().zlink_spot_node_peers(
            self._handle, filter_ptr, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodePeerEntry(
                channel_name=_decode_fixed(entry.channel_name),
                local_endpoint=_decode_fixed(entry.local_endpoint),
                peer_endpoint=_decode_fixed(entry.peer_endpoint),
                source=SpotPeerSource(int(entry.source)),
                kind=SpotPeerKind(int(entry.kind)),
                state=SpotPeerState(int(entry.state)),
                weight=int(entry.weight),
                connected_since_ms=int(entry.connected_since_ms),
                last_changed_ms=int(entry.last_changed_ms),
            )
            for entry in entries[: int(count.value)]
        ]

    def subjects(self, filter_=None):
        count = ctypes.c_size_t()
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = ZlinkSpotNodeSubjectFilter()
            filter_native.role = 0 if filter_.role is None else int(filter_.role)
            filter_native.subject = _fixed_buffer_value(filter_.subject, 256)
            filter_native.subject_kind = 0 if filter_.subject_kind is None else int(filter_.subject_kind)
            filter_ptr = ctypes.byref(filter_native)
        rc = lib().zlink_spot_node_subjects(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeSubjectEntry * int(count.value))()
        rc = lib().zlink_spot_node_subjects(
            self._handle, filter_ptr, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodeSubjectEntry(
                role=SpotRole(int(entry.role)),
                subject=_decode_fixed(entry.subject),
                subject_kind=SubjectKind(int(entry.subject_kind)),
                ready_peer_count=int(entry.ready_peer_count),
                active_peer_count=int(entry.active_peer_count),
                last_changed_ms=int(entry.last_changed_ms),
            )
            for entry in entries[: int(count.value)]
        ]

    def internal_sockets(self, filter_=None):
        count = ctypes.c_size_t()
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = ZlinkSpotNodeSocketFilter()
            filter_native.owner = (
                int(SpotNodeSocketOwner.ANY)
                if filter_.owner is None
                else int(filter_.owner)
            )
            filter_native.socket_type = (
                0 if filter_.socket_type is None else int(filter_.socket_type)
            )
            filter_native.socket_name = _fixed_buffer_value(filter_.socket_name, 64)
            filter_ptr = ctypes.byref(filter_native)
        rc = lib().zlink_spot_node_internal_sockets(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeSocketEntry * int(count.value))()
        rc = lib().zlink_spot_node_internal_sockets(
            self._handle, filter_ptr, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodeSocketEntry(
                owner=SpotNodeSocketOwner(int(entry.owner)),
                owner_id=int(entry.owner_id),
                owner_name=_decode_fixed(entry.owner_name),
                socket_name=_decode_fixed(entry.socket_name),
                socket_type=SocketType(int(entry.socket_type)),
                auto_hwm_visible=bool(entry.auto_hwm_visible),
                snapshot=_monitor_status_from_native(entry.monitor_status),
            )
            for entry in entries[: int(count.value)]
        ]

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
