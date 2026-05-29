# SPDX-License-Identifier: MPL-2.0

import asyncio
import ctypes
import errno
import threading

from ...eventing.dispatcher import CallbackDispatcher
from ...sockets.socket_base import (
    _ensure_not_in_callback,
    _enter_callback,
    _leave_callback,
)
from ...messaging.request_reply import _ensure_reply_flags_supported
from ....contracts.core.options import AutoHwmProfile
from ....contracts.sockets.codes import SocketType
from ....contracts.service.codes import (
    ServiceKind,
    SpotActorLifecycleEventKind,
    SpotDispatchEvent,
    SpotDispatchSubjectKind,
    SpotNodeMode,
    SpotNodeOption,
    SpotNodeSocketOwner,
    SpotNodeState,
    SpotKind,
    SpotPeerKind,
    SpotPeerSource,
    SpotPeerState,
    SpotRole,
    SubjectKind,
)
from ...._native.ffi import (
    ZlinkActorJoinInfo,
    ZlinkActorJoinEntrySpotResult,
    ZlinkActorJoinResult,
    ZlinkActorLookupResult,
    ZlinkMsg,
    ZlinkRoutingId,
    ZlinkSpotActorLifecycleInfo,
    ZlinkSpotActorLifecycleEvent,
    ZlinkSpotDispatchInfo,
    ZlinkSpotNodePeerEntry,
    ZlinkSpotNodePeerFilter,
    ZlinkSpotNodeOptions,
    ZlinkSpotNodeSocketEntry,
    ZlinkSpotNodeSocketFilter,
    ZlinkSpotNodeActorEntry,
    ZlinkSpotNodeSpotEntry,
    ZlinkSpotNodeStatus,
    ZlinkSpotNodeSubjectEntry,
    ZlinkSpotNodeSubjectFilter,
    lib,
)
from ...handles.native_support import (
    CloseError,
    CloseResult,
    ConnectError,
    ConnectResult,
    ConfigError,
    ConfigResult,
    _copy_routing_id,
    HandlerError,
    HandlerResult,
    Message,
    RecvError,
    RecvResult,
    RequestError,
    RequestResult,
    RoutingId,
    SubmitError,
    SubmitResult,
    _SOCKET_SEND_READY_HANDLER,
    _REPLY_HANDLER,
    _as_bytes_view,
    _report_unhandled_callback_exception,
    _raise_config_error_from_errno,
    _raise_result_error,
    _routing_id_bytes,
    _request_result_from_code,
    _request_result_internal_errno,
    _validated_c_string_text,
    _validated_c_string_value,
    _validated_int32,
    _validated_routing_id_bytes,
)
from ....contracts.eventing.monitor import MonitorStatus
from ...eventing.monitor import _monitor_status_from_native
from .actor_ops import (
    ActorBindOp,
    ActorDestroyOp,
    ActorJoinCallbackOp,
    ActorJoinEntrySpotOp,
    ActorJoinOp,
    ActorJoinReplyOp,
    ActorLeaveOp,
    ActorLookupOp,
    ActorUnbindOp,
)
from .request_progress import (
    PendingRequest as _PendingRequest,
    RequestProgressPump as _RequestProgressPump,
    acquire_external_request_progress as _acquire_external_request_progress,
    release_external_request_progress as _release_external_request_progress,
)
from .native_parts import (
    clone_payload as _clone_payload_parts,
    close_native_parts as _close_native_parts,
    close_native_parts_array as _close_native_parts_array,
    prepare_native_parts as _prepare_native_parts,
    submit_parts as _submit_parts,
)
from .spot_submit import (
    submit_channel_request as _submit_channel_request,
    submit_channel_send as _submit_channel_send,
    submit_spot_request as _submit_spot_request,
    submit_spot_send as _submit_spot_send,
)
from .spot_ops import (
    ReplyOp,
    RequestCallbackOp,
    RequestOp,
    SendOp,
)
from .spot_receive import (
    _clone_payload,
    _make_message_list,
    _recv_spot_routed,
    _recv_spot_subscribed,
    _recv_spot_subscription_event,
)
from .spot_models_runtime import (
    ActorJoinEntrySpotResult,
    ActorJoinInfo,
    ActorJoinRequest,
    ActorJoinResult,
    ActorLookupResult,
    ActorPart,
    ActorRecvInfo,
    ActorRef,
    ActorRoute,
    SpotActorLifecycleEvent,
    SpotActorLifecycleInfo,
    SpotDispatchInfo,
    SpotNodeActorEntry,
    SpotNodePeerEntry,
    SpotNodePeerFilter,
    SpotNodeSocketEntry,
    SpotNodeSocketFilter,
    SpotNodeSpotEntry,
    SpotNodeStatus,
    SpotNodeSubjectEntry,
    SpotNodeSubjectFilter,
    _actor_id_bytes,
    _actor_ref_from_native,
    _actor_ref_to_native,
    _message_from_native,
    _recv_actor_part,
    _routing_id_or_empty,
    _spot_actor_lifecycle_info_from_native,
    remote_actor_ref,
)


_ERRNO_ETERM = getattr(errno, "ETERM", 156)
_ERRNO_ENOTSUP = getattr(errno, "ENOTSUP", getattr(errno, "EOPNOTSUPP", 95))
_SPOT_INIT_TOKEN = object()
_REQUEST_PROGRESS_IDLE_GRACE_S = 0.1

_SPOT_ROUTED_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(ZlinkRoutingId),
    ctypes.POINTER(ZlinkRoutingId),
    ctypes.c_uint64,
    ctypes.POINTER(ZlinkMsg),
    ctypes.c_size_t,
    ctypes.c_void_p,
)
_ROUTER_SPOT_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(ZlinkRoutingId),
    ctypes.POINTER(ZlinkRoutingId),
    ctypes.c_uint64,
    ctypes.POINTER(ZlinkMsg),
    ctypes.c_size_t,
    ctypes.c_void_p,
)
_SPOT_DISPATCH_EVENT_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,
    ctypes.POINTER(ZlinkSpotDispatchInfo),
    ctypes.c_void_p,
)
_ACTOR_ADMISSION_HANDLER = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.POINTER(ZlinkMsg),
    ctypes.c_size_t,
    ctypes.c_void_p,
)
_ACTOR_JOIN_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(ZlinkActorJoinResult),
    ctypes.POINTER(ZlinkMsg),
    ctypes.c_size_t,
    ctypes.c_void_p,
)
_ACTOR_JOIN_ENTRY_SPOT_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(ZlinkActorJoinEntrySpotResult),
    ctypes.c_void_p,
)
_ACTOR_LOOKUP_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(ZlinkActorLookupResult),
    ctypes.c_void_p,
)

_ACTOR_LIFECYCLE_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,
    ctypes.POINTER(ZlinkSpotActorLifecycleInfo),
    ctypes.c_void_p,
)

_SPOT_SUBSCRIBE_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.POINTER(ZlinkRoutingId),
    ctypes.c_char_p,
    ctypes.c_size_t,
    ctypes.POINTER(ZlinkMsg),
    ctypes.c_size_t,
    ctypes.c_void_p,
)


def _wait_for_reply_submit(submit, timeout=0):
    event = threading.Event()
    box = {"result": RequestResult.INTERNAL_ERROR, "errno": 0}

    def _callback(result_code, parts, part_count, _userdata):
        result = _request_result_from_code(int(result_code))
        if parts is not None and part_count:
            for index in range(int(part_count)):
                lib().zlink_msg_close(ctypes.byref(parts[index]))
        box["result"] = result
        box["errno"] = _request_result_internal_errno(result)
        event.set()

    callback = _REPLY_HANDLER(_callback)
    rc = submit(callback)
    if rc != 0:
        _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())
    wait_timeout = None if timeout in (None, 0) else float(timeout)
    if not event.wait(wait_timeout):
        raise RequestError(RequestResult.TIMED_OUT, errno.ETIMEDOUT)
    if box["result"] != RequestResult.OK:
        raise RequestError(box["result"], box["errno"])


def _decode_fixed(buf):
    return bytes(buf).split(b"\0", 1)[0].decode("utf-8", errors="replace")


def _fixed_buffer_value(value, size):
    raw = b""
    if value is not None:
        raw = bytes(_as_bytes_view(value))
    if len(raw) >= size:
        raise ValueError(f"value exceeds fixed buffer size {size - 1}")
    return raw


def _timeout_to_ms(timeout):
    if timeout in (None, 0):
        return 0
    return max(1, int(float(timeout) * 1000))



def _make_spot_routed_reply_sender(spot, node_rid, spot_rid, seq):
    """Return a zero-arg factory that yields a fresh ReplyOp for this routed
    receive. Each call yields a new builder so per-call payload accumulation
    stays isolated."""
    if spot_rid:
        return lambda: spot.reply_to_spot(node_rid, spot_rid, seq)
    return lambda: spot.reply_to_router(node_rid, seq)


def _make_spot_routed_send_sender(spot, node_rid, spot_rid):
    """Return a zero-arg factory that yields a fresh SendOp routed back to the
    source spot of this received message, or None if there is no source spot."""
    if not node_rid or not spot_rid:
        return None
    return lambda: spot.send_to_spot(node_rid, spot_rid)


class Actor:
    def __init__(self, node, actor_ref):
        if not isinstance(node, SpotNode):
            raise TypeError("node must be SpotNode")
        if not isinstance(actor_ref, ActorRef):
            raise TypeError("actor_ref must be ActorRef")
        self._node = node
        self._ref = actor_ref

    def ref(self):
        if self._ref is None:
            raise RuntimeError("actor is closed")
        return self._ref

    @property
    def actor_ref(self):
        return self.ref()

    def join(self, spot):
        if not isinstance(spot, Spot):
            raise TypeError("spot must be Spot")
        return ActorJoinOp(
            self._node,
            self.ref(),
            spot._node.routing_id,
            spot.routing_id,
        )

    def leave(self, spot):
        if not isinstance(spot, Spot):
            raise TypeError("spot must be Spot")
        return ActorLeaveOp(self._node, self.ref(), spot.routing_id)

    def recv_part(self, *, flags=0):
        try:
            return _recv_actor_part(self._node._handle, self.ref(), flags)
        except RecvError as ex:
            if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                return None
            raise

    def send_bound_session(self):
        actor_ref = self.ref()
        node = self._node
        return SendOp(
            node,
            lambda parts, flags: node._actor_send_bound_session_submit(
                actor_ref, parts, flags
            ),
        )

    def close_bound_session(self, *, timeout=0):
        native_actor = _actor_ref_to_native(self.ref())
        rc = lib().zlink_spot_node_actor_close_bound_session(
            self._node._handle,
            ctypes.byref(native_actor),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            _raise_result_error(RequestError, RequestResult, rc, lib().zlink_errno())

    def close(self, *, timeout=0):
        if self._ref is None:
            return
        native_actor = _actor_ref_to_native(self._ref)
        _wait_for_reply_submit(
            lambda handler: lib().zlink_spot_node_actor_destroy(
                self._node._handle,
                ctypes.byref(native_actor),
                handler,
                None,
                _timeout_to_ms(timeout),
            ),
            timeout,
        )
        self._ref = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

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
        return Spot._create(self)

    def entry_spot(self):
        spot_handle = ctypes.c_void_p()
        rc = lib().zlink_spot_node_entry_spot(self._handle, ctypes.byref(spot_handle))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
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

class Spot:
    @classmethod
    def _create(cls, node):
        return cls(node, _internal=_SPOT_INIT_TOKEN)

    @classmethod
    def _wrap_handle(cls, node, handle):
        """Wrap an existing native spot handle returned by entry_spot/spot_lookup."""
        spot = cls.__new__(cls)
        spot._init_state(node)
        spot._handle = handle
        node._register_spot(spot)
        return spot

    def _init_state(self, node):
        self._node = node
        self._request_pending = {}
        self._request_progress_targets = {}
        self._request_reply_handler = None
        self._routed_handler = None
        self._routed_handler_cb = None
        self._dispatch_handler = None
        self._dispatch_handler_cb = None
        self._send_ready_handler = None
        self._send_ready_handler_cb = None
        self._dispatcher = CallbackDispatcher(
            "zlink-spot-dispatch", _enter_callback, _leave_callback
        )
        self._actor_lifecycle_on_join = None
        self._actor_lifecycle_on_leave = None
        self._actor_lifecycle_on_join_cb = None
        self._actor_lifecycle_on_leave_cb = None
        self._own = True
        self._timers = {}
        self._request_progress = _RequestProgressPump(
            self._request_progress_handles,
            lambda: bool(self._request_pending),
            idle_grace_s=_REQUEST_PROGRESS_IDLE_GRACE_S,
        )

    def _register_timer(self, timer):
        self._timers[timer._handle] = timer

    def _unregister_timer(self, timer):
        self._timers.pop(timer._handle, None)

    def _request_progress_handles(self):
        """Yield handles to register on the request-completion poller."""
        seen = set()
        result = []
        for _kind, handle in list(self._request_progress_targets.values()):
            if not handle or handle in seen:
                continue
            seen.add(handle)
            result.append(handle)
        return result

    def _request_progress_target(self, channel_bytes=None):
        return ("spot", self._handle)

    def __init__(self, node, *, _internal=None):
        if _internal is not _SPOT_INIT_TOKEN:
            raise TypeError("Spot() is internal; use SpotNode.create_spot()")
        if not isinstance(node, SpotNode):
            raise TypeError("Spot requires a SpotNode")
        self._init_state(node)
        self._handle = lib().zlink_spot_new(node._handle)
        if not self._handle:
            _raise_config_error_from_errno()
        node._register_spot(self)

    def set_routing_id(self, routing_id):
        raw = _validated_routing_id_bytes(routing_id)
        rc = lib().zlink_set_routing_id(self._handle, ctypes.c_char_p(raw), len(raw))
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

    def _native_parts_from_payload(self, payload):
        if isinstance(payload, (list, tuple)):
            parts = list(payload)
        else:
            parts = [payload]
        if not parts:
            raise ValueError("parts must not be empty")
        return _clone_payload_parts(parts)

    def publish(self, topic):
        return SendOp(
            self,
            lambda parts, flags: self._publish_submit(topic, parts, flags),
        )

    def _publish_submit(self, topic, parts, flags=0):
        try:
            _ensure_not_in_callback("blocking publish")
            topic_bytes = _validated_c_string_value(topic, field="topic", max_length=255)
            native_parts = self._native_parts_from_payload(parts)
            rc, err = _submit_parts(
                native_parts,
                lambda part_ptr, part_flag: lib().zlink_spot_publish_part(
                    self._handle,
                    topic_bytes,
                    part_ptr,
                    int(flags),
                    part_flag,
                ),
            )
            if rc != 0:
                _raise_result_error(SubmitError, SubmitResult, rc, err)
            return True
        except SubmitError as ex:
            if int(flags) & 1 and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise

    def send_to_channel(self, channel_name):
        return SendOp(
            self,
            lambda parts, flags: self._send_to_channel_submit(
                channel_name, parts, flags
            ),
        )

    def _send_to_channel_submit(self, channel_name, parts, flags=0):
        try:
            _ensure_not_in_callback("blocking send")
            channel_bytes = _validated_c_string_value(
                channel_name, field="channel_name", max_length=255
            )
            native_parts = self._native_parts_from_payload(parts)
            _submit_channel_send(self._handle, channel_bytes, native_parts, flags)
            return True
        except SubmitError as ex:
            if int(flags) & 1 and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise

    def send_to_spot(self, dest_node_rid, dest_spot_rid):
        return SendOp(
            self,
            lambda parts, flags: self._send_to_spot_submit(
                dest_node_rid, dest_spot_rid, parts, flags
            ),
        )

    def _send_to_spot_submit(self, dest_node_rid, dest_spot_rid, parts, flags=0):
        try:
            _ensure_not_in_callback("blocking send")
            native_parts = self._native_parts_from_payload(parts)
            native_node = _copy_routing_id(dest_node_rid)
            native_spot = _copy_routing_id(dest_spot_rid)
            _submit_spot_send(
                self._handle,
                native_node,
                native_spot,
                native_parts,
                flags,
            )
            return True
        except SubmitError as ex:
            if int(flags) & 1 and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise

    def request_to_channel(self, channel_name):
        channel_bytes = _validated_c_string_value(
            channel_name, field="channel_name", max_length=255
        )
        return RequestOp(
            self,
            lambda parts, *, timeout=0: self._request_to_channel_async(
                channel_bytes, parts, timeout=timeout
            ),
            lambda parts, callback, *, flags=0, timeout=0: self._request_to_channel_callback(
                channel_bytes, parts, callback, flags=flags, timeout=timeout
            ),
        )

    def _request_to_channel_async(self, channel_bytes, payload, *, timeout=0):
        async def _run():
            loop = asyncio.get_running_loop()
            pending = _PendingRequest(loop=loop)
            handle = id(pending)
            self._request_pending[handle] = pending
            try:
                self._start_channel_request(
                    channel_bytes, payload, 0, timeout, handle
                )
            except Exception:
                self._request_pending.pop(handle, None)
                self._request_progress_targets.pop(handle, None)
                raise
            self._request_progress.ensure_running()
            return await pending.future

        return _run()

    def _request_to_channel_callback(self, channel_bytes, payload, callback, *, flags=0, timeout=0):
        pending = _PendingRequest(callback=callback)
        handle = id(pending)
        self._request_pending[handle] = pending
        try:
            self._start_channel_request(channel_bytes, payload, flags, timeout, handle)
            self._request_progress.ensure_running()
            return True
        except SubmitError as ex:
            self._request_pending.pop(handle, None)
            self._request_progress_targets.pop(handle, None)
            if int(flags) & 1 and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise
        except Exception:
            self._request_pending.pop(handle, None)
            self._request_progress_targets.pop(handle, None)
            raise

    def _start_channel_request(self, channel_bytes, payload, flags, timeout, handle):
        native_parts = self._native_parts_from_payload(payload)
        reply_handler = self._ensure_request_reply_handler()
        self._request_progress_targets[handle] = self._request_progress_target(
            channel_bytes
        )
        try:
            _submit_channel_request(
                self._handle,
                channel_bytes,
                native_parts,
                reply_handler,
                handle,
                flags,
                _timeout_to_ms(timeout),
            )
        except Exception:
            self._request_pending.pop(handle, None)
            self._request_progress_targets.pop(handle, None)
            raise

    def request_to_spot(self, dest_node_rid, dest_spot_rid):
        return RequestOp(
            self,
            lambda parts, *, timeout=0: self._request_to_spot_async(
                dest_node_rid, dest_spot_rid, parts, timeout=timeout
            ),
            lambda parts, callback, *, flags=0, timeout=0: self._request_to_spot_callback(
                dest_node_rid, dest_spot_rid, parts, callback, flags=flags, timeout=timeout
            ),
        )

    def _request_to_spot_async(self, dest_node_rid, dest_spot_rid, parts, *, timeout=0):
        async def _run():
            loop = asyncio.get_running_loop()
            pending = _PendingRequest(loop=loop)
            handle = id(pending)
            self._request_pending[handle] = pending
            try:
                self._start_spot_request(
                    dest_node_rid, dest_spot_rid, parts, 0, timeout, handle
                )
            except Exception:
                self._request_pending.pop(handle, None)
                self._request_progress_targets.pop(handle, None)
                raise
            self._request_progress.ensure_running()
            return await pending.future

        return _run()

    def _request_to_spot_callback(self, dest_node_rid, dest_spot_rid, parts, callback, *, flags=0, timeout=0):
        pending = _PendingRequest(callback=callback)
        handle = id(pending)
        self._request_pending[handle] = pending
        try:
            self._start_spot_request(
                dest_node_rid, dest_spot_rid, parts, flags, timeout, handle
            )
            self._request_progress.ensure_running()
            return True
        except SubmitError as ex:
            self._request_pending.pop(handle, None)
            self._request_progress_targets.pop(handle, None)
            if int(flags) & 1 and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise
        except Exception:
            self._request_pending.pop(handle, None)
            self._request_progress_targets.pop(handle, None)
            raise

    def request_to_router(self, peer_rid):
        return RequestOp(
            self,
            lambda parts, *, timeout=0: self._request_async(
                lib().zlink_spot_request_router_part,
                (peer_rid,),
                parts,
                timeout=timeout,
            ),
            lambda parts, callback, *, flags=0, timeout=0: self._request_callback(
                lib().zlink_spot_request_router_part,
                (peer_rid,),
                parts,
                callback,
                flags=flags,
                timeout=timeout,
            ),
        )

    def _start_spot_request(self, dest_node_rid, dest_spot_rid, payload, flags, timeout, handle):
        native_parts = self._native_parts_from_payload(payload)
        native_node = _copy_routing_id(dest_node_rid)
        native_spot = _copy_routing_id(dest_spot_rid)
        reply_handler = self._ensure_request_reply_handler()
        self._request_progress_targets[handle] = self._request_progress_target()
        try:
            _submit_spot_request(
                self._handle,
                native_node,
                native_spot,
                native_parts,
                reply_handler,
                handle,
                flags,
                _timeout_to_ms(timeout),
            )
        except Exception:
            self._request_pending.pop(handle, None)
            self._request_progress_targets.pop(handle, None)
            raise

    def _recv_subscribed(self, flags):
        return _recv_spot_subscribed(self._handle, flags)

    def _subscribe_allocated(self, *, flags=0):
        try:
            return self._recv_subscribed(flags)
        except RecvError as ex:
            if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                return None
            raise

    def subscribe_into(self, topic_message, *, flags=0):
        if topic_message is None or not hasattr(topic_message, "_adopt_from"):
            raise TypeError("topic_message must be a TopicMessage")
        try:
            fresh = self._recv_subscribed(flags)
        except RecvError as ex:
            if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                return False
            raise
        topic_message._adopt_from(fresh)
        return True

    def _receive_subscription_event(self, *, flags=0):
        try:
            return _recv_spot_subscription_event(self._handle, flags)
        except RecvError as ex:
            if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                return None
            raise

    def receive_subscription_event_into(self, event, *, flags=0):
        if event is None or not hasattr(event, "_adopt_from"):
            raise TypeError("event must be a SubscriptionEvent")
        try:
            fresh = self._receive_subscription_event(flags=flags)
        except RecvError as ex:
            if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                return False
            raise
        event._adopt_from(fresh)
        return True

    def set_subscription(self, topic):
        rc = lib().zlink_set_subscription(
            self._handle,
            _validated_c_string_value(topic, field="subscription"),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def unset_subscription(self, topic):
        rc = lib().zlink_unset_subscription(
            self._handle,
            _validated_c_string_value(topic, field="subscription"),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def _set_pub_option(self, option, value):
        view = _as_bytes_view(value)
        if view.nbytes == 0:
            rc = lib().zlink_set_pub_option(self._handle, int(option), None, 0)
        elif view.readonly:
            raw = view.tobytes()
            rc = lib().zlink_set_pub_option(
                self._handle, int(option), ctypes.c_char_p(raw), len(raw)
            )
        else:
            rc = lib().zlink_set_pub_option(
                self._handle,
                int(option),
                ctypes.c_void_p(
                    ctypes.addressof(
                        (ctypes.c_ubyte * view.nbytes).from_buffer(view)
                    )
                ),
                view.nbytes,
            )
        if rc != 0:
            _raise_last_error()

    def _set_sub_option(self, option, value):
        view = _as_bytes_view(value)
        if view.nbytes == 0:
            rc = lib().zlink_set_sub_option(self._handle, int(option), None, 0)
        elif view.readonly:
            raw = view.tobytes()
            rc = lib().zlink_set_sub_option(
                self._handle, int(option), ctypes.c_char_p(raw), len(raw)
            )
        else:
            rc = lib().zlink_set_sub_option(
                self._handle,
                int(option),
                ctypes.c_void_p(
                    ctypes.addressof(
                        (ctypes.c_ubyte * view.nbytes).from_buffer(view)
                    )
                ),
                view.nbytes,
            )
        if rc != 0:
            _raise_last_error()

    def on_send_ready(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")
        if self._send_ready_handler is not None:
            raise RuntimeError("send-ready handler is already attached")

        self._send_ready_handler = handler
        dispatcher = self._dispatcher

        def _invoke():
            try:
                handler(self)
            except Exception:
                _report_unhandled_callback_exception(handler)

        def _callback(_, __):
            dispatcher.submit(_invoke)

        callback = _SOCKET_SEND_READY_HANDLER(_callback)
        rc = lib().zlink_send_ready_handler(self._handle, callback, None)
        if rc != 0:
            self._send_ready_handler = None
            _raise_last_error()
        self._send_ready_handler_cb = callback

    def _ensure_request_reply_handler(self):
        if self._request_reply_handler is None:
            self._request_reply_handler = _REPLY_HANDLER(self._on_reply)
        return self._request_reply_handler

    def _request_async(self, native_func, routing_ids, payload, *, flags=0, timeout=0):
        async def _run():
            loop = asyncio.get_running_loop()
            pending = _PendingRequest(loop=loop)
            handle = id(pending)
            self._request_pending[handle] = pending
            try:
                self._start_request(native_func, routing_ids, payload, flags, timeout, handle)
            except Exception:
                self._request_pending.pop(handle, None)
                self._request_progress_targets.pop(handle, None)
                raise
            self._request_progress.ensure_running()
            return await pending.future

        return _run()

    def _request_callback(self, native_func, routing_ids, payload, callback, *, flags=0, timeout=0):
        pending = _PendingRequest(callback=callback)
        handle = id(pending)
        self._request_pending[handle] = pending
        try:
            self._start_request(native_func, routing_ids, payload, flags, timeout, handle)
            self._request_progress.ensure_running()
            return True
        except Exception:
            self._request_pending.pop(handle, None)
            self._request_progress_targets.pop(handle, None)
            raise

    def _start_request(self, native_func, routing_ids, payload, flags, timeout, handle):
        native_parts = _clone_payload(payload)
        native_rids = [_copy_routing_id(rid) for rid in routing_ids]
        reply_handler = self._ensure_request_reply_handler()
        self._request_progress_targets[handle] = self._request_progress_target()
        if len(native_rids) == 2:
            rc, err = _submit_parts(
                native_parts,
                lambda part_ptr, part_flag: native_func(
                    self._handle,
                    ctypes.byref(native_rids[0]),
                    ctypes.byref(native_rids[1]),
                    part_ptr,
                    reply_handler,
                    ctypes.c_void_p(handle),
                    int(flags),
                    part_flag,
                    _timeout_to_ms(timeout),
                ),
            )
        elif len(native_rids) == 1:
            rc, err = _submit_parts(
                native_parts,
                lambda part_ptr, part_flag: native_func(
                    self._handle,
                    ctypes.byref(native_rids[0]),
                    part_ptr,
                    reply_handler,
                    ctypes.c_void_p(handle),
                    int(flags),
                    part_flag,
                    _timeout_to_ms(timeout),
                ),
            )
        else:
            raise ValueError("routing_ids must not be empty")
        if rc != 0:
            self._request_pending.pop(handle, None)
            self._request_progress_targets.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, err)

    def _on_reply(self, result_code, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        self._request_progress_targets.pop(handle, None)
        pending = self._request_pending.pop(handle, None)
        if pending is None:
            return
        result = _request_result_from_code(int(result_code))
        received = []
        if result == RequestResult.OK:
            received = _make_message_list(parts, part_count)
        pending.resolve(result, received, _request_result_internal_errno(result))

    def _submit_reply_to_spot(self, dest_node_rid, dest_spot_rid, request_seq, parts, flags):
        _ensure_reply_flags_supported(flags)
        native_parts = _clone_payload(parts)
        native_node = _copy_routing_id(dest_node_rid)
        native_spot = _copy_routing_id(dest_spot_rid)
        rc, err = _submit_parts(
            native_parts,
            lambda part_ptr, part_flag: lib().zlink_spot_reply_spot_part(
                self._handle,
                ctypes.byref(native_node),
                ctypes.byref(native_spot),
                ctypes.c_uint64(request_seq),
                part_ptr,
                part_flag,
            ),
        )
        if rc != 0:
            _raise_result_error(SubmitError, SubmitResult, rc, err)

    def reply_to_spot(self, dest_node_rid, dest_spot_rid, request_seq):
        return ReplyOp(
            lambda parts, flags: self._submit_reply_to_spot(
                dest_node_rid, dest_spot_rid, request_seq, parts, flags
            )
        )

    def _submit_reply_to_router(self, peer_rid, request_seq, parts, flags):
        _ensure_reply_flags_supported(flags)
        native_parts = _clone_payload(parts)
        native_peer = _copy_routing_id(peer_rid)
        rc, err = _submit_parts(
            native_parts,
            lambda part_ptr, part_flag: lib().zlink_spot_reply_router_part(
                self._handle,
                ctypes.byref(native_peer),
                ctypes.c_uint64(request_seq),
                part_ptr,
                part_flag,
            ),
        )
        if rc != 0:
            _raise_result_error(SubmitError, SubmitResult, rc, err)

    def reply_to_router(self, peer_rid, request_seq):
        return ReplyOp(
            lambda parts, flags: self._submit_reply_to_router(
                peer_rid, request_seq, parts, flags
            )
        )

    def recv_routed(self, *, flags=0):
        try:
            return _recv_spot_routed(
                self._handle,
                flags,
                reply_sender_factory=lambda node_rid, spot_rid, seq: _make_spot_routed_reply_sender(
                    self,
                    node_rid,
                    spot_rid,
                    seq,
                ),
                send_sender_factory=lambda node_rid, spot_rid: _make_spot_routed_send_sender(
                    self,
                    node_rid,
                    spot_rid,
                ),
            )
        except RecvError as ex:
            if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                return None
            raise

    def recv_routed_into(self, received, *, flags=0):
        if received is None or not hasattr(received, "_adopt_from"):
            raise TypeError("received must be a Received")
        try:
            fresh = _recv_spot_routed(
                self._handle,
                flags,
                reply_sender_factory=lambda node_rid, spot_rid, seq: _make_spot_routed_reply_sender(
                    self,
                    node_rid,
                    spot_rid,
                    seq,
                ),
                send_sender_factory=lambda node_rid, spot_rid: _make_spot_routed_send_sender(
                    self,
                    node_rid,
                    spot_rid,
                ),
            )
        except RecvError as ex:
            if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                return False
            raise
        received._adopt_from(fresh)
        return True

    def on_dispatch_event(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")
        if self._dispatch_handler_cb is not None:
            raise RuntimeError("dispatch handler is already attached")

        def _callback(_spot, info_ptr, _):
            if not info_ptr:
                return
            info = info_ptr.contents
            try:
                actor_ref = None
                timer = None
                channel_dealer = None
                subject_kind = SpotDispatchSubjectKind(int(info.subject_kind))
                if info.subject:
                    if subject_kind == SpotDispatchSubjectKind.ACTOR:
                        actor_ref = _actor_ref_from_native(
                            ctypes.cast(
                                info.subject,
                                ctypes.POINTER(ZlinkActorRef),
                            ).contents
                        )
                    elif subject_kind == SpotDispatchSubjectKind.TIMER:
                        timer = self._timers.get(info.subject)
                    elif subject_kind == SpotDispatchSubjectKind.CHANNEL_DEALER:
                        channel_dealer = self._node._channel_dealers.get(info.subject)
                dispatch_info = SpotDispatchInfo(
                    event=SpotDispatchEvent(int(info.event)),
                    subject_kind=subject_kind,
                    timer=timer,
                    channel_dealer=channel_dealer,
                    actor=actor_ref,
                    _node_handle=self._node._handle,
                )
                _enter_callback()
                try:
                    handler(self, dispatch_info)
                finally:
                    _leave_callback()
            except Exception:
                _report_unhandled_callback_exception(handler)

        callback = _SPOT_DISPATCH_EVENT_HANDLER(_callback)
        rc = lib().zlink_spot_dispatch_event_handler(self._handle, callback, None)
        if rc != 0:
            _raise_result_error(HandlerError, HandlerResult, rc, lib().zlink_errno())
        self._dispatch_handler = handler
        self._dispatch_handler_cb = callback

    def recv_actor_join(self, *, flags=0):
        info = ZlinkActorJoinInfo()
        parts = ctypes.POINTER(ZlinkMsg)()
        part_count = ctypes.c_size_t()
        rc = lib().zlink_spot_actor_join_recv(
            self._handle,
            ctypes.byref(info),
            ctypes.byref(parts),
            ctypes.byref(part_count),
            int(flags),
        )
        if rc != 0:
            try:
                _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
            except RecvError as ex:
                if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                    return None
                raise
        messages = _make_message_list(parts, part_count.value)
        lib().zlink_multipart_close(parts, part_count.value)
        message = messages[0] if messages else Message()
        for extra in messages[1:]:
            extra.close()
        return ActorJoinRequest(
            info=ActorJoinInfo(
                source_actor=_actor_ref_from_native(info.source_actor),
                target_actor=_actor_ref_from_native(info.target_actor),
                source_node_rid=_routing_id_bytes(info.source_node_rid),
                source_spot_rid=_routing_id_or_empty(info.source_spot_rid),
                target_node_rid=_routing_id_or_empty(info.target_node_rid),
                target_spot_rid=_routing_id_or_empty(info.target_spot_rid),
                join_epoch=int(info.join_epoch),
                flags=int(info.flags),
            ),
            message=message,
            _native=info,
        )

    def recv_actor_lifecycle(self, *, flags=0):
        event = ZlinkSpotActorLifecycleEvent()
        rc = lib().zlink_spot_recv_actor_lifecycle(
            self._handle,
            ctypes.byref(event),
            int(flags),
        )
        if rc != 0:
            try:
                _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
            except RecvError as ex:
                if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                    return None
                raise
        return SpotActorLifecycleEvent(
            kind=SpotActorLifecycleEventKind(int(event.kind)),
            info=_spot_actor_lifecycle_info_from_native(event.info),
        )

    def reply_actor_join(self, request, join_result_code):
        if not isinstance(request, ActorJoinRequest):
            raise TypeError("request must be ActorJoinRequest")
        return ActorJoinReplyOp(self, request, join_result_code)

    def _submit_actor_join_reply(self, request, join_result_code, parts):
        if parts:
            native_parts = _clone_payload(parts)
            native_array = _prepare_native_parts(native_parts)
            parts_arg = native_array
            count = len(native_parts)
        else:
            native_parts = []
            parts_arg = None
            count = 0
        rc = lib().zlink_spot_actor_join_reply(
            self._handle,
            ctypes.byref(request._native),
            int(join_result_code),
            parts_arg,
            count,
        )
        if rc != 0:
            _close_native_parts(native_parts)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())

    def actors(self):
        count = ctypes.c_size_t()
        rc = lib().zlink_spot_actors(self._handle, None, ctypes.byref(count))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkActorRef * int(count.value))()
        rc = lib().zlink_spot_actors(
            self._handle, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [_actor_ref_from_native(entry) for entry in entries[: int(count.value)]]

    def _cancel_pending_requests(self):
        for handle, pending in list(self._request_pending.items()):
            self._request_pending.pop(handle, None)
            self._request_progress_targets.pop(handle, None)
            pending.resolve(RequestResult.TERMINATED, None, _ERRNO_ETERM)

    def close(self):
        if not self._handle:
            return
        send_ready_handler_cb = self._send_ready_handler_cb
        routed_handler_cb = self._routed_handler_cb
        dispatch_handler_cb = self._dispatch_handler_cb
        self._dispatcher.close()
        self._send_ready_handler = None
        self._routed_handler = None
        self._routed_handler_cb = None
        self._dispatch_handler = None
        self._dispatch_handler_cb = None
        self._cancel_pending_requests()
        self._request_progress.stop()
        self._request_reply_handler = None
        lifecycle_on_join_cb = self._actor_lifecycle_on_join_cb
        lifecycle_on_leave_cb = self._actor_lifecycle_on_leave_cb
        self._actor_lifecycle_on_join_cb = None
        self._actor_lifecycle_on_leave_cb = None
        self._actor_lifecycle_on_join = None
        self._actor_lifecycle_on_leave = None
        handle = ctypes.c_void_p(self._handle)
        self._handle = None
        rc = lib().zlink_spot_destroy(ctypes.byref(handle))
        self._send_ready_handler_cb = None
        del send_ready_handler_cb
        del routed_handler_cb
        del dispatch_handler_cb
        del lifecycle_on_join_cb
        del lifecycle_on_leave_cb
        if rc != 0:
            _raise_last_error()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()


for _public_type in (
    SpotDispatchInfo,
    ActorRef,
    ActorRoute,
    ActorRecvInfo,
    ActorJoinInfo,
    ActorJoinRequest,
    ActorJoinResult,
    ActorJoinEntrySpotResult,
    ActorLookupResult,
    SpotActorLifecycleInfo,
    SpotActorLifecycleEvent,
    SpotNodeSpotEntry,
    SpotNodeActorEntry,
    SpotNodeStatus,
    SpotNodePeerEntry,
    SpotNodePeerFilter,
    SpotNodeSubjectEntry,
    SpotNodeSubjectFilter,
    SpotNodeSocketFilter,
    SpotNodeSocketEntry,
    Actor,
    SpotNode,
    SendOp,
    RequestOp,
    RequestCallbackOp,
    ReplyOp,
    ActorJoinOp,
    ActorJoinCallbackOp,
    ActorJoinEntrySpotOp,
    ActorJoinReplyOp,
    ActorLeaveOp,
    ActorDestroyOp,
    ActorLookupOp,
    ActorBindOp,
    ActorUnbindOp,
    Spot,
):
    _public_type.__module__ = "zlink.contracts.service.spot"


def create_spot_node(ctx, mode=None):
    return SpotNode(ctx, mode)


def create_spot(node):
    return Spot._create(node)
