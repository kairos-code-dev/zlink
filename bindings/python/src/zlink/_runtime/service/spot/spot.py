# SPDX-License-Identifier: MPL-2.0

import ctypes
import errno
import threading

from ...eventing.dispatcher import CallbackDispatcher
from ...sockets.socket_base import (
    _ensure_not_in_callback,
    _enter_callback,
    _leave_callback,
)
from ...messaging.request_reply import _ensure_reply_flags_supported, _timeout_to_ms
from ....contracts.core.options import AutoHwmProfile
from ....contracts.sockets.codes import SocketType
from ....contracts.service.codes import (
    ServiceKind,
    SpotActorLifecycleEventKind,
    SpotDispatchEvent,
    SpotDispatchSubjectKind,
    SpotNodeMode,
    SpotNodeSocketOwner,
    SpotNodeState,
    SpotKind,
    SpotPeerKind,
    SpotPeerSource,
    SpotPeerState,
    SpotRole,
    SubjectKind,
)
from ...native_codes import SpotNodeOption
from ...._native.ffi import (
    ZlinkActorJoinEntrySpotResult,
    ZlinkActorJoinResult,
    ZlinkActorLookupResult,
    ZlinkActorRef,
    ZlinkMsg,
    ZlinkRoutingId,
    ZlinkSpotActorLifecycleInfo,
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
from ...._native import bridge as _native_bridge
from ...messaging.message_materializer import Message
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
    _raise_last_error,
    _raise_result_error,
    _routing_id_bytes,
    _request_result_from_code,
    _request_result_native_errno,
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
    close_native_parts_array as _close_native_parts_array,
    submit_parts as _submit_parts,
)
from .spot_submit import (
    submit_channel_request as _submit_channel_request,
    submit_channel_send as _submit_channel_send,
    submit_spot_request as _submit_spot_request,
    submit_spot_send as _submit_spot_send,
)
from .spot_actor_join_runtime import SpotActorJoinMixin
from .spot_ops import (
    PublishOp,
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
    remote_actor_ref,
)


_ERRNO_ETERM = getattr(errno, "ETERM", 156)
_ERRNO_ENOTSUP = getattr(errno, "ENOTSUP", getattr(errno, "EOPNOTSUPP", 95))
_native_extension = getattr(_native_bridge, "_zlink_native", None)
_native_spot_publish_submit_parts = (
    getattr(_native_extension, "spot_publish_submit_parts", None)
    if _native_extension is not None
    else None
)
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
    ctypes.POINTER(ZlinkMsg),
    ctypes.c_size_t,
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
        box["errno"] = _request_result_native_errno(result)
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


def _payload_can_use_native_bridge(payload):
    if isinstance(payload, (bytes, bytearray)):
        return True
    if isinstance(payload, memoryview):
        return payload.c_contiguous
    parts = payload if isinstance(payload, (list, tuple)) else (payload,)
    if not parts:
        return False
    for part in parts:
        if isinstance(part, Message):
            return False
        try:
            view = memoryview(part)
        except TypeError:
            return False
        if not view.c_contiguous:
            return False
    return True



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


class Spot(SpotActorJoinMixin):
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
        from .spot_node import SpotNode

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
        return PublishOp(self, topic)

    def _publish_topic_bytes(self, topic):
        cache = getattr(self, "_publish_topic_bytes_cache", None)
        if cache is None:
            cache = {}
            self._publish_topic_bytes_cache = cache
        try:
            return cache[topic]
        except (KeyError, TypeError):
            topic_bytes = _validated_c_string_value(topic, field="topic", max_length=255)
            try:
                cache[topic] = topic_bytes
            except TypeError:
                pass
            return topic_bytes

    def _publish_submit(self, topic, parts, flags=0):
        topic_bytes = _validated_c_string_value(topic, field="topic", max_length=255)
        return self._publish_submit_prevalidated(topic_bytes, parts, flags)

    def _publish_submit_prevalidated(self, topic_bytes, parts, flags=0):
        try:
            _ensure_not_in_callback("blocking publish")
            if _payload_can_use_native_bridge(parts):
                if _native_spot_publish_submit_parts is not None:
                    bridged = _native_spot_publish_submit_parts(
                        int(self._handle),
                        topic_bytes,
                        parts,
                        int(flags),
                    )
                else:
                    bridged = _native_bridge.spot_publish_submit_parts(
                        self._handle,
                        topic_bytes,
                        parts,
                        flags,
                    )
                if bridged is not None:
                    rc, err = bridged
                    if int(rc) != 0:
                        _raise_result_error(SubmitError, SubmitResult, rc, err)
                    return True
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
            if _payload_can_use_native_bridge(parts):
                bridged = _native_bridge.spot_send_channel_parts(
                    self._handle,
                    channel_bytes,
                    parts,
                    flags,
                )
                if bridged is not None:
                    rc, err = bridged
                    if int(rc) != 0:
                        _raise_result_error(SubmitError, SubmitResult, rc, err)
                    return True
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
            node_rid_bytes = _validated_routing_id_bytes(dest_node_rid)
            spot_rid_bytes = _validated_routing_id_bytes(dest_spot_rid)
            if _payload_can_use_native_bridge(parts):
                bridged = _native_bridge.spot_send_spot_parts(
                    self._handle,
                    node_rid_bytes,
                    spot_rid_bytes,
                    parts,
                    flags,
                )
                if bridged is not None:
                    rc, err = bridged
                    if int(rc) != 0:
                        _raise_result_error(SubmitError, SubmitResult, rc, err)
                    return True
            native_parts = self._native_parts_from_payload(parts)
            native_node = _copy_routing_id(node_rid_bytes)
            native_spot = _copy_routing_id(spot_rid_bytes)
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
            lambda parts, callback, *, flags=0, timeout=0: self._request_to_channel_callback(
                channel_bytes, parts, callback, flags=flags, timeout=timeout
            ),
        )

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
            lambda parts, callback, *, flags=0, timeout=0: self._request_to_spot_callback(
                dest_node_rid, dest_spot_rid, parts, callback, flags=flags, timeout=timeout
            ),
        )

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
            result = self._recv_subscribed(flags)
            if result is False:
                return None
            return result
        except RecvError as ex:
            if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                return None
            raise

    def subscribe_into(self, topic_message, *, flags=0):
        if topic_message is None or not hasattr(topic_message, "_adopt_from"):
            raise TypeError("topic_message must be a TopicMessage")
        try:
            fresh = self._recv_subscribed(flags)
            if fresh is False:
                return False
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
        pending.resolve(result, received, _request_result_native_errno(result))

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


from .actor import Actor
from .spot_node import SpotNode


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
    PublishOp,
    SendOp,
    RequestOp,
    RequestCallbackOp,
    ReplyOp,
    ActorJoinOp,
    ActorJoinCallbackOp,
    ActorJoinEntrySpotOp,
    ActorLeaveOp,
    ActorDestroyOp,
    ActorLookupOp,
    ActorBindOp,
    ActorUnbindOp,
    Spot,
):
    _public_type.__module__ = "zlink.contracts.service.spot"


def create_spot_node(ctx, mode=None):
    from .spot_node import SpotNode

    return SpotNode(ctx, mode)


def create_spot(node):
    return Spot._create(node)
