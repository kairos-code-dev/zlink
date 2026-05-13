# SPDX-License-Identifier: MPL-2.0

import asyncio
import ctypes
import errno
import queue
import threading
import time
from dataclasses import dataclass, field

from ._socket_base import (
    _classify_nonblocking_send_errno,
    _clone_received_owner,
    _ensure_not_in_callback,
    _enter_callback,
    _leave_callback,
)
from ._request_reply import _ensure_reply_flags_supported
from ._enums import (
    AutoHwmProfile,
    ServiceKind,
    SocketType,
    SpotDispatchEvent,
    SpotDispatchSubjectKind,
    SpotNodeMode,
    SpotNodeOption,
    SpotNodeSocketOwner,
    SpotNodeState,
    SpotPeerSource,
    SpotPeerState,
    SpotRole,
    SubjectKind,
)
from ._ffi import (
    ZLINK_PART_FINAL,
    ZLINK_PART_MORE,
    ZlinkActorJoinInfo,
    ZlinkActorJoinResult,
    ZlinkActorLookupResult,
    ZlinkActorRecvInfo,
    ZlinkActorRef,
    ZlinkMsg,
    ZlinkRoutingId,
    ZlinkSpotActorLifecycleInfo,
    ZlinkSpotDispatchInfo,
    ZlinkSpotNodePeerEntry,
    ZlinkSpotNodePeerFilter,
    ZlinkSpotNodeOptions,
    ZlinkSpotNodeSocketSnapshotEntry,
    ZlinkSpotNodeSocketSnapshotFilter,
    ZlinkSpotNodeActorEntry,
    ZlinkSpotNodeSpotEntry,
    ZlinkSpotNodeStatus,
    ZlinkSpotNodeSubjectEntry,
    ZlinkSpotNodeSubjectFilter,
    lib,
)
from ._core import (
    BindError,
    BindResult,
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
    Received,
    TopicMessage,
    RecvError,
    RecvResult,
    RequestError,
    RequestResult,
    RoutingId,
    SubmitError,
    SubmitResult,
    _SOCKET_SEND_READY_HANDLER,
    _ReceivedPartsOwner,
    _REPLY_HANDLER,
    _as_bytes_view,
    _clone_native_msg,
    _decode_topic_text,
    _is_eagain,
    _init_msg_from_buffer,
    _msg_to_bytes,
    _report_unhandled_callback_exception,
    _raise_config_error_from_errno,
    _raise_result_error,
    _routing_id_bytes,
    _request_result_from_code,
    _request_result_internal_errno,
    _validated_c_string_bytes,
    _validated_c_string_text,
    _validated_c_string_value,
    _validated_int32,
    _validated_routing_id_bytes,
)
from ._monitor import MonitorSnapshot, _monitor_snapshot_from_native


_SPOT_CALLBACK_SENTINEL = object()
_ERRNO_ETERM = getattr(errno, "ETERM", 156)
_ERRNO_ENOTSUP = getattr(errno, "ENOTSUP", getattr(errno, "EOPNOTSUPP", 95))
_SPOT_INIT_TOKEN = object()
_UNSET = object()
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


@dataclass(frozen=True)
class SpotDispatchInfo:
    event: SpotDispatchEvent
    subject_kind: SpotDispatchSubjectKind
    timer: "object | None" = None
    channel_dealer: "object | None" = None
    actor: "ActorRef | None" = None
    _node_handle: ctypes.c_void_p | None = field(default=None, repr=False, compare=False, hash=False)

    def recv_actor_part(self, *, flags=0):
        if (
            self.event != SpotDispatchEvent.ACTOR_READABLE
            or self.subject_kind != SpotDispatchSubjectKind.ACTOR
            or self.actor is None
            or self._node_handle is None
        ):
            raise RecvError(RecvResult.NOT_SUPPORTED, _ERRNO_ENOTSUP)
        try:
            return _recv_actor_part(self._node_handle, self.actor, flags)
        except RecvError as ex:
            if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                return None
            raise


@dataclass(frozen=True)
class ActorRef:
    node_rid: RoutingId
    actor_id: str
    generation: int

    @property
    def unchecked(self):
        return self.generation == 0

    def is_unchecked(self):
        return self.unchecked


@dataclass(frozen=True)
class ActorRoute:
    actor: ActorRef
    joined: bool
    joined_spot_rid: RoutingId | None


@dataclass(frozen=True)
class ActorRecvInfo:
    actor: ActorRef
    source_node_rid: RoutingId
    source_session_rid: RoutingId
    flags: int


@dataclass(frozen=True)
class ActorJoinInfo:
    source_actor: ActorRef
    target_actor: ActorRef
    source_node_rid: RoutingId
    source_spot_rid: RoutingId
    target_node_rid: RoutingId
    target_spot_rid: RoutingId
    join_epoch: int
    flags: int


@dataclass(frozen=True)
class ActorPart:
    info: ActorRecvInfo
    message: Message
    more: bool


@dataclass(frozen=True)
class ActorJoinRequest:
    info: ActorJoinInfo
    message: Message
    _native: object = field(default=None, repr=False, compare=False, hash=False)

    def __iter__(self):
        yield self.info
        yield self.message


@dataclass(frozen=True)
class ActorJoinResult:
    result: RequestResult
    actor: ActorRef
    joined_spot_rid: RoutingId
    join_epoch: int
    flags: int


@dataclass(frozen=True)
class ActorLookupResult:
    result: RequestResult
    actor: ActorRef
    flags: int


@dataclass(frozen=True)
class SpotActorLifecycleInfo:
    previous_actor: ActorRef
    current_actor: ActorRef
    previous_spot_rid: "RoutingId | None"
    current_spot_rid: "RoutingId | None"
    join_epoch: int
    flags: int


@dataclass(frozen=True)
class SpotNodeSpotEntry:
    spot_rid: RoutingId
    dispatch_handler_attached: bool
    joined_actor_count: int
    pending_actor_join_count: int
    route_synced: bool
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodeActorEntry:
    actor: ActorRef
    joined: bool
    joined_spot_rid: RoutingId | None
    route_synced: bool
    pending_message_count: int
    last_changed_ms: int


def _actor_id_bytes(actor_id):
    return _validated_c_string_value(actor_id, field="actor_id", max_length=255)


def _actor_ref_from_native(native):
    actor_id = bytes(native.actor_id).split(b"\0", 1)[0].decode(
        "utf-8", errors="replace"
    )
    return ActorRef(
        node_rid=_routing_id_bytes(native.node_rid),
        actor_id=actor_id,
        generation=int(native.generation),
    )


def _actor_ref_to_native(actor_ref):
    if isinstance(actor_ref, ActorRef):
        native = ZlinkActorRef()
        native.node_rid = _copy_routing_id(actor_ref.node_rid)
        actor_id = _actor_id_bytes(actor_ref.actor_id)
        native.actor_id = actor_id
        native.generation = int(actor_ref.generation)
        return native
    raise TypeError("actor_ref must be ActorRef")


def _message_from_native(native):
    msg = Message.__new__(Message)
    msg._msg = native
    msg._valid = True
    msg._keepalive = None
    return msg


def remote_actor_ref(target_node_rid, actor_id):
    return ActorRef(node_rid=_routing_id_bytes(_copy_routing_id(target_node_rid)),
                    actor_id=_actor_id_bytes(actor_id).decode(), generation=0)


def _routing_id_or_none(native):
    return _routing_id_bytes(native)


def _routing_id_or_empty(native):
    raw = bytes(native.data[: native.size])
    return RoutingId(raw)


def _routing_id_optional(native):
    """Return None for a zero-size routing id, else RoutingId(bytes)."""
    size = int(native.size)
    if size == 0:
        return None
    raw = bytes(native.data[:size])
    return RoutingId(raw)


def _recv_actor_part(node_handle, actor_ref, flags=0):
    info = ZlinkActorRecvInfo()
    part = ZlinkMsg()
    more = ctypes.c_int()
    native_actor = _actor_ref_to_native(actor_ref)
    rc = lib().zlink_spot_node_actor_recv_part(
        node_handle,
        ctypes.byref(native_actor),
        ctypes.byref(info),
        ctypes.byref(part),
        ctypes.byref(more),
        int(flags),
    )
    if rc != 0:
        _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
    return ActorPart(
        info=ActorRecvInfo(
            actor=_actor_ref_from_native(info.actor),
            source_node_rid=_routing_id_bytes(info.source_node_rid),
            source_session_rid=_routing_id_bytes(info.source_session_rid),
            flags=int(info.flags),
        ),
        message=_message_from_native(part),
        more=int(more.value) != ZLINK_PART_FINAL,
    )


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


def _payload_parts(payload):
    if isinstance(payload, (list, tuple)):
        parts = list(payload)
    else:
        parts = [payload]
    if not parts:
        raise ValueError("payload must not be empty")
    return parts


def _clone_payload(payload):
    native_parts = []
    for part in _payload_parts(payload):
        if isinstance(part, Message):
            native_parts.append(_clone_native_msg(part._msg))
            continue
        native = ZlinkMsg()
        _init_msg_from_buffer(native, part, borrow=False)
        native_parts.append(native)
    return native_parts


def _prepare_native_parts(native_parts):
    parts_array = (ZlinkMsg * len(native_parts))()
    for index, native in enumerate(native_parts):
        parts_array[index] = native
    return parts_array


def _part_flag(part_index, part_count):
    return ZLINK_PART_FINAL if part_index == part_count - 1 else ZLINK_PART_MORE


def _close_native_parts(native_parts, start=0):
    for native in native_parts[start:]:
        lib().zlink_msg_close(ctypes.byref(native))


def _close_native_parts_array(parts_array, part_count):
    for index in range(part_count):
        lib().zlink_msg_close(ctypes.byref(parts_array[index]))


def _submit_parts(native_parts, submit_part):
    part_count = len(native_parts)
    for index, native in enumerate(native_parts):
        rc = submit_part(ctypes.byref(native), _part_flag(index, part_count))
        if rc != 0:
            err = lib().zlink_errno()
            _close_native_parts(native_parts, index)
            return rc, err
    return 0, 0


def _make_received_owner(parts_ptr, part_count):
    parts_array = (ZlinkMsg * part_count)()
    for index in range(part_count):
        parts_array[index] = _clone_native_msg(parts_ptr[index])
    return _clone_received_owner(parts_array, part_count)


def _make_routed_received(
    source_node_rid,
    source_spot_rid,
    request_seq,
    parts_ptr,
    part_count,
    *,
    reply_sender=None,
    send_sender=None,
):
    routing_id = (
        _routing_id_bytes(source_node_rid)
        if source_node_rid is not None
        else None
    )
    spot_routing_id = (
        _routing_id_bytes(source_spot_rid)
        if source_spot_rid is not None
        else None
    )
    owner = _make_received_owner(parts_ptr, int(part_count))
    received = Received(
        owner,
        routing_id=routing_id,
        request_seq=int(request_seq),
        spot_rid=spot_routing_id,
        reply_sender=reply_sender,
        send_sender=send_sender,
    )
    received.source_node_rid = routing_id
    received.source_spot_rid = received.spot_rid
    return received


def _make_received(request_seq, parts_ptr, part_count, routing_id=None, *, reply_sender=None):
    owner = _make_received_owner(parts_ptr, int(part_count))
    received = Received(
        owner,
        routing_id=routing_id,
        request_seq=request_seq,
        reply_sender=reply_sender,
    )
    return received


def _make_message_list(parts_ptr, part_count):
    messages = []
    for index in range(int(part_count)):
        msg = Message.__new__(Message)
        msg._msg = _clone_native_msg(parts_ptr[index])
        msg._valid = True
        msg._keepalive = None
        messages.append(msg)
    return messages


class _PendingRequest:
    def __init__(self, *, loop=None, callback=None):
        self.loop = loop
        self.future = loop.create_future() if loop is not None else None
        self.callback = callback

    def resolve(self, result, received, errnum=0):
        if self.future is not None:
            if result == RequestResult.OK:
                self.loop.call_soon_threadsafe(self.future.set_result, received)
            else:
                self.loop.call_soon_threadsafe(
                    self.future.set_exception,
                    RequestError(result, errnum),
                )
            return
        if self.callback is None:
            return
        try:
            self.callback(result, received if result == RequestResult.OK else [])
        except Exception:
            _report_unhandled_callback_exception(self.callback)


class _PendingActorJoin:
    """Pending state for ActorJoinOp: surfaces (ActorJoinResult, list[Message])."""

    def __init__(self, *, loop=None, callback=None):
        self.loop = loop
        self.future = loop.create_future() if loop is not None else None
        self.callback = callback

    def resolve(self, join_result, messages, errnum=0):
        result = join_result.result
        if self.future is not None:
            if result == RequestResult.OK:
                self.loop.call_soon_threadsafe(
                    self.future.set_result, (join_result, messages)
                )
            else:
                self.loop.call_soon_threadsafe(
                    self.future.set_exception, RequestError(result, errnum)
                )
            return
        if self.callback is None:
            return
        try:
            self.callback(join_result, messages if result == RequestResult.OK else [])
        except Exception:
            _report_unhandled_callback_exception(self.callback)


class _PendingActorLookup:
    """Pending state for ActorLookupOp: surfaces ActorLookupResult."""

    def __init__(self, *, loop=None, callback=None):
        self.loop = loop
        self.future = loop.create_future() if loop is not None else None
        self.callback = callback

    def resolve(self, lookup_result, errnum=0):
        result = lookup_result.result
        if self.future is not None:
            if result == RequestResult.OK:
                self.loop.call_soon_threadsafe(
                    self.future.set_result, lookup_result
                )
            else:
                self.loop.call_soon_threadsafe(
                    self.future.set_exception, RequestError(result, errnum)
                )
            return
        if self.callback is None:
            return
        try:
            self.callback(lookup_result)
        except Exception:
            _report_unhandled_callback_exception(self.callback)


class _RequestProgressPump:
    def __init__(self, step, is_active):
        self._step = step
        self._is_active = is_active
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = None

    def ensure_running(self):
        with self._lock:
            if self._stop.is_set():
                return
            if self._thread is not None and self._thread.is_alive():
                return
            self._thread = threading.Thread(
                target=self._run,
                name="zlink-spot-request-progress",
                daemon=True,
            )
            self._thread.start()

    def _run(self):
        idle_since = None
        try:
            while not self._stop.is_set():
                if self._is_active():
                    idle_since = None
                    try:
                        self._step()
                    except Exception:
                        pass
                else:
                    if idle_since is None:
                        idle_since = time.monotonic()
                    elif (
                        time.monotonic() - idle_since
                        >= _REQUEST_PROGRESS_IDLE_GRACE_S
                    ):
                        break
                self._stop.wait(0.001)
        finally:
            with self._lock:
                if self._thread is threading.current_thread():
                    self._thread = None

    def stop(self):
        self._stop.set()
        with self._lock:
            thread = self._thread
        if (
            thread is not None
            and thread.is_alive()
            and thread is not threading.current_thread()
        ):
            thread.join(timeout=1.0)


def _recv_spot_subscribed(handle, flags):
    routing_id = None
    native_parts = []
    topic_buf = ctypes.create_string_buffer(256)
    topic_len = 0
    recv_flags = int(flags)
    try:
        while True:
            routing_ptr = ctypes.POINTER(ZlinkRoutingId)()
            current_topic_len = ctypes.c_size_t(len(topic_buf))
            native_part = ZlinkMsg()
            has_more = ctypes.c_int()
            rc = lib().zlink_spot_subscribe_part(
                handle,
                ctypes.byref(routing_ptr),
                topic_buf,
                len(topic_buf),
                ctypes.byref(current_topic_len),
                ctypes.byref(native_part),
                ctypes.byref(has_more),
                recv_flags,
            )
            if rc != 0:
                _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
            if not native_parts:
                if routing_ptr:
                    routing_id = routing_ptr.contents
                topic_len = int(current_topic_len.value)
            native_parts.append(native_part)
            if has_more.value == ZLINK_PART_FINAL:
                break
            recv_flags = 1
    except Exception:
        _close_native_parts(native_parts)
        raise

    owner = _ReceivedPartsOwner(_prepare_native_parts(native_parts), len(native_parts))
    topic = _decode_topic_text(topic_buf.raw[:topic_len])
    return TopicMessage(
        topic,
        owner,
        _routing_id_bytes(routing_id) if routing_id is not None else None,
    )


def _recv_spot_routed(handle, flags, *, reply_sender_factory=None, send_sender_factory=None):
    source_node_rid = None
    source_spot_rid = None
    request_seq = 0
    native_parts = []
    recv_flags = int(flags)
    try:
        while True:
            current_source_node_rid = ctypes.POINTER(ZlinkRoutingId)()
            current_source_spot_rid = ctypes.POINTER(ZlinkRoutingId)()
            current_request_seq = ctypes.c_uint64()
            native_part = ZlinkMsg()
            has_more = ctypes.c_int()
            rc = lib().zlink_spot_recv_part(
                handle,
                ctypes.byref(current_source_node_rid),
                ctypes.byref(current_source_spot_rid),
                ctypes.byref(current_request_seq),
                ctypes.byref(native_part),
                ctypes.byref(has_more),
                recv_flags,
            )
            if rc != 0:
                _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
            if not native_parts:
                if current_source_node_rid:
                    source_node_rid = current_source_node_rid.contents
                if current_source_spot_rid:
                    source_spot_rid = current_source_spot_rid.contents
                request_seq = int(current_request_seq.value)
            native_parts.append(native_part)
            if has_more.value == ZLINK_PART_FINAL:
                break
            recv_flags = 1
    except Exception:
        _close_native_parts(native_parts)
        raise

    node_rid = _routing_id_bytes(source_node_rid) if source_node_rid is not None else None
    spot_rid = _routing_id_bytes(source_spot_rid) if source_spot_rid is not None else None
    reply_sender = None
    if reply_sender_factory is not None:
        reply_sender = reply_sender_factory(node_rid, spot_rid, request_seq)
    send_sender = None
    if send_sender_factory is not None:
        send_sender = send_sender_factory(node_rid, spot_rid)

    return _make_routed_received(
        source_node_rid,
        source_spot_rid,
        request_seq,
        _prepare_native_parts(native_parts),
        len(native_parts),
        reply_sender=reply_sender,
        send_sender=send_sender,
    )


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


@dataclass(frozen=True)
class SpotNodeStatus:
    channel_name: str
    local_endpoint: str
    node_routing_id: RoutingId
    state: SpotNodeState
    configured_peer_count: int
    active_peer_count: int
    connected_peer_count: int
    subject_count: int
    ready_subject_count: int
    disconnected_sub_target_count: int
    disconnected_routed_target_count: int
    last_error: int
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodePeerEntry:
    channel_name: str
    local_endpoint: str
    peer_endpoint: str
    source: SpotPeerSource
    state: SpotPeerState
    weight: int
    connected_since_ms: int
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodePeerFilter:
    peer_endpoint: str | None = None
    source: SpotPeerSource | None = None
    state: SpotPeerState | None = None


@dataclass(frozen=True)
class SpotNodeSubjectEntry:
    role: SpotRole
    subject: str
    subject_kind: SubjectKind
    ready_peer_count: int
    active_peer_count: int
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodeSubjectFilter:
    role: SpotRole | None = None
    subject: str | None = None
    subject_kind: SubjectKind | None = None


@dataclass(frozen=True)
class SpotNodeSocketSnapshotFilter:
    owner: SpotNodeSocketOwner | None = None
    socket_type: SocketType | None = None
    socket_name: str | None = None


@dataclass(frozen=True)
class SpotNodeSocketSnapshotEntry:
    owner: SpotNodeSocketOwner
    owner_id: int
    owner_name: str
    socket_name: str
    socket_type: SocketType
    auto_hwm_visible: bool
    snapshot: MonitorSnapshot


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
        self._actor_lookup_pending = {}
        self._actor_reply_handler = None
        self._actor_join_handler = None
        self._actor_lookup_handler = None
        self._channel_dealers = {}

    def bind(self, endpoint: str):
        rc = lib().zlink_spot_node_bind(
            self._handle,
            _validated_c_string_text(endpoint, field="endpoint", max_length=255),
        )
        if rc != 0:
            _raise_result_error(BindError, BindResult, rc, lib().zlink_errno())

    def last_endpoint(self) -> str:
        return self.status_snapshot().local_endpoint

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

    def attach_discovery(self, discovery):
        rc = lib().zlink_spot_node_attach_discovery(
            self._handle, discovery._handle
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

    def set_router_hwm(self, value: int):
        native = ctypes.c_int32(_validated_int32(value))
        self._set_spot_node_option(
            SpotNodeOption.ROUTER_HWM,
            ctypes.string_at(ctypes.byref(native), ctypes.sizeof(native)),
        )

    def set_pubsub_hwm(self, value: int):
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
    def router_hwm(self):
        return self._get_spot_node_option_int32(SpotNodeOption.ROUTER_HWM)

    @router_hwm.setter
    def router_hwm(self, value):
        self.set_router_hwm(value)

    @property
    def pubsub_hwm_profile(self):
        return AutoHwmProfile(self._get_spot_node_option_int32(SpotNodeOption.PUBSUB_HWM_PROFILE))

    @pubsub_hwm_profile.setter
    def pubsub_hwm_profile(self, value):
        self.set_pubsub_hwm_profile(value)

    @property
    def pubsub_hwm(self):
        return self._get_spot_node_option_int32(SpotNodeOption.PUBSUB_HWM)

    @pubsub_hwm.setter
    def pubsub_hwm(self, value):
        self.set_pubsub_hwm(value)

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

    def _ensure_actor_lookup_handler(self):
        if self._actor_lookup_handler is None:
            self._actor_lookup_handler = _ACTOR_LOOKUP_HANDLER(self._on_actor_lookup_reply)
        return self._actor_lookup_handler

    def _on_actor_join_reply(self, result_ptr, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._actor_join_pending.pop(handle, None)
        if pending is None:
            # Fall back to plain reply path (legacy destroy/leave/bind/unbind).
            if not result_ptr:
                self._on_actor_reply(int(RequestResult.INTERNAL_ERROR), parts, part_count, userdata)
                return
            self._on_actor_reply(result_ptr.contents.result, parts, part_count, userdata)
            return
        if not result_ptr:
            join_result = ActorJoinResult(
                result=RequestResult.INTERNAL_ERROR,
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
            actor=_actor_ref_from_native(native.actor),
            joined_spot_rid=_routing_id_bytes(native.joined_spot_rid),
            join_epoch=int(native.join_epoch),
            flags=int(native.flags),
        )
        messages = []
        if result == RequestResult.OK:
            messages = _make_message_list(parts, part_count)
        pending.resolve(join_result, messages, _request_result_internal_errno(result))

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

    def spots_snapshot(self):
        count = ctypes.c_size_t()
        rc = lib().zlink_spot_node_spots_snapshot(self._handle, None, ctypes.byref(count))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeSpotEntry * int(count.value))()
        rc = lib().zlink_spot_node_spots_snapshot(
            self._handle, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodeSpotEntry(
                spot_rid=_routing_id_bytes(entry.spot_rid),
                dispatch_handler_attached=bool(entry.dispatch_handler_attached),
                joined_actor_count=int(entry.joined_actor_count),
                pending_actor_join_count=int(entry.pending_actor_join_count),
                route_synced=bool(entry.route_synced),
                last_changed_ms=int(entry.last_changed_ms),
            )
            for entry in entries[: int(count.value)]
        ]

    def actors_snapshot(self):
        count = ctypes.c_size_t()
        rc = lib().zlink_spot_node_actors_snapshot(self._handle, None, ctypes.byref(count))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeActorEntry * int(count.value))()
        rc = lib().zlink_spot_node_actors_snapshot(
            self._handle, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodeActorEntry(
                actor=_actor_ref_from_native(entry.actor),
                joined=bool(entry.joined),
                joined_spot_rid=_routing_id_bytes(entry.joined_spot_rid),
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

    def status_snapshot(self):
        native = ZlinkSpotNodeStatus()
        rc = lib().zlink_spot_node_status_snapshot(self._handle, ctypes.byref(native))
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

    def peers_snapshot(self):
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
        rc = lib().zlink_spot_node_peers_query(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodePeerEntry * int(count.value))()
        rc = lib().zlink_spot_node_peers_query(
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
                state=SpotPeerState(int(entry.state)),
                weight=int(entry.weight),
                connected_since_ms=int(entry.connected_since_ms),
                last_changed_ms=int(entry.last_changed_ms),
            )
            for entry in entries[: int(count.value)]
        ]

    def subjects_snapshot(self, filter_=None):
        count = ctypes.c_size_t()
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = ZlinkSpotNodeSubjectFilter()
            filter_native.role = 0 if filter_.role is None else int(filter_.role)
            filter_native.subject = _fixed_buffer_value(filter_.subject, 256)
            filter_native.subject_kind = 0 if filter_.subject_kind is None else int(filter_.subject_kind)
            filter_ptr = ctypes.byref(filter_native)
        rc = lib().zlink_spot_node_subjects_snapshot(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeSubjectEntry * int(count.value))()
        rc = lib().zlink_spot_node_subjects_snapshot(
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

    def internal_sockets_snapshot(self, filter_=None):
        count = ctypes.c_size_t()
        filter_ptr = None
        filter_native = None
        if filter_ is not None:
            filter_native = ZlinkSpotNodeSocketSnapshotFilter()
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
        rc = lib().zlink_spot_node_internal_sockets_snapshot(
            self._handle, filter_ptr, None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkSpotNodeSocketSnapshotEntry * int(count.value))()
        rc = lib().zlink_spot_node_internal_sockets_snapshot(
            self._handle, filter_ptr, entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [
            SpotNodeSocketSnapshotEntry(
                owner=SpotNodeSocketOwner(int(entry.owner)),
                owner_id=int(entry.owner_id),
                owner_name=_decode_fixed(entry.owner_name),
                socket_name=_decode_fixed(entry.socket_name),
                socket_type=SocketType(int(entry.socket_type)),
                auto_hwm_visible=bool(entry.auto_hwm_visible),
                snapshot=_monitor_snapshot_from_native(entry.snapshot),
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


class SendOp:
    """Fluent builder for Spot send operations."""
    __slots__ = ('_spot', '_op_fn', '_parts', '_flags', '_submitted')

    def __init__(self, spot, op_fn):
        self._spot = spot
        self._op_fn = op_fn
        self._parts = []
        self._flags = 0
        self._submitted = False

    def message(self, payload):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.append(payload)
        return self

    def messages(self, *payloads):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.extend(payloads)
        return self

    def flags(self, flags):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._flags = int(flags)
        return self

    def submit(self):
        """Returns True on success, False on DONTWAIT backpressure. Raises SubmitError on failure."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if not self._parts:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        return self._op_fn(self._parts, self._flags)


class RequestOp:
    """Fluent builder for Spot request operations (async or callback)."""
    __slots__ = ('_spot', '_op_async_fn', '_op_cb_fn', '_parts', '_timeout', '_submitted')

    def __init__(self, spot, op_async_fn, op_cb_fn):
        self._spot = spot
        self._op_async_fn = op_async_fn
        self._op_cb_fn = op_cb_fn
        self._parts = []
        self._timeout = 0
        self._submitted = False

    def message(self, payload):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.append(payload)
        return self

    def messages(self, *payloads):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.extend(payloads)
        return self

    def timeout(self, timeout):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._timeout = timeout
        return self

    def flags(self, flags):
        """Calling flags() transitions to a RequestCallbackOp."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._submitted = True
        return RequestCallbackOp(self._spot, self._op_cb_fn, self._parts, self._timeout, int(flags))

    def submit_async(self):
        """Submit as async coroutine. Returns awaitable list[Message]."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if not self._parts:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        return self._op_async_fn(self._parts, timeout=self._timeout)

    def submit(self, callback):
        """Submit with callback. Returns True/False for backpressure."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if not self._parts:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        return self._op_cb_fn(self._parts, callback, flags=0, timeout=self._timeout)


class RequestCallbackOp:
    """Request operation builder after flags() has been called - async submit is not available."""
    __slots__ = ('_spot', '_op_cb_fn', '_parts', '_timeout', '_flags', '_submitted')

    def __init__(self, spot, op_cb_fn, parts, timeout, flags):
        self._spot = spot
        self._op_cb_fn = op_cb_fn
        self._parts = parts
        self._timeout = timeout
        self._flags = flags
        self._submitted = False

    def message(self, payload):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.append(payload)
        return self

    def messages(self, *payloads):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.extend(payloads)
        return self

    def timeout(self, timeout):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._timeout = timeout
        return self

    def flags(self, flags):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._flags = int(flags)
        return self

    def submit(self, callback):
        """Submit with callback. Returns True/False for backpressure."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if not self._parts:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        return self._op_cb_fn(self._parts, callback, flags=self._flags, timeout=self._timeout)


class ReplyOp:
    """Fluent builder for Spot reply operations."""
    __slots__ = ('_op_fn', '_parts', '_flags', '_submitted')

    def __init__(self, op_fn):
        self._op_fn = op_fn
        self._parts = []
        self._flags = 0
        self._submitted = False

    def message(self, payload):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.append(payload)
        return self

    def messages(self, *payloads):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.extend(payloads)
        return self

    def flags(self, flags):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._flags = int(flags)
        return self

    def submit(self):
        """Submit the reply. Raises SubmitError on failure."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if not self._parts:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        self._op_fn(self._parts, self._flags)


class ActorJoinOp:
    """Fluent builder for Actor join operations (async or callback)."""
    __slots__ = ('_node', '_actor_ref', '_dest_node_rid', '_dest_spot_rid',
                 '_parts', '_timeout', '_submitted')

    def __init__(self, node, actor_ref, dest_node_rid, dest_spot_rid):
        self._node = node
        self._actor_ref = actor_ref
        self._dest_node_rid = dest_node_rid
        self._dest_spot_rid = dest_spot_rid
        self._parts = []
        self._timeout = 0
        self._submitted = False

    def message(self, payload):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.append(payload)
        return self

    def messages(self, *payloads):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.extend(payloads)
        return self

    def timeout(self, timeout):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._timeout = timeout
        return self

    def flags(self, flags):
        """Transitions to a callback-only builder. submit_async() is no longer
        available after flags()."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        op = ActorJoinCallbackOp(
            self._node,
            self._actor_ref,
            self._dest_node_rid,
            self._dest_spot_rid,
            self._parts,
            self._timeout,
            int(flags),
        )
        self._submitted = True
        return op

    def submit_async(self):
        """Submit as async coroutine. Returns awaitable
        tuple[ActorJoinResult, list[Message]]."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if not self._parts:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        parts = self._parts
        timeout = self._timeout

        async def _run():
            loop = asyncio.get_running_loop()
            pending = _PendingActorJoin(loop=loop)
            self._node._submit_actor_join(
                self._actor_ref,
                self._dest_node_rid,
                self._dest_spot_rid,
                parts,
                pending,
                flags=0,
                timeout=timeout,
            )
            return await pending.future

        return _run()

    def submit(self, callback):
        """Submit with callback. Returns True; raises SubmitError on failure."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if callback is None:
            raise ValueError("callback must not be None")
        if not self._parts:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        pending = _PendingActorJoin(callback=callback)
        self._node._submit_actor_join(
            self._actor_ref,
            self._dest_node_rid,
            self._dest_spot_rid,
            self._parts,
            pending,
            flags=0,
            timeout=self._timeout,
        )
        return True


class ActorJoinCallbackOp:
    """Callback-only ActorJoin builder (after flags() has been called)."""
    __slots__ = ('_node', '_actor_ref', '_dest_node_rid', '_dest_spot_rid',
                 '_parts', '_timeout', '_flags', '_submitted')

    def __init__(self, node, actor_ref, dest_node_rid, dest_spot_rid, parts, timeout, flags):
        self._node = node
        self._actor_ref = actor_ref
        self._dest_node_rid = dest_node_rid
        self._dest_spot_rid = dest_spot_rid
        self._parts = parts
        self._timeout = timeout
        self._flags = int(flags)
        self._submitted = False

    def message(self, payload):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.append(payload)
        return self

    def messages(self, *payloads):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.extend(payloads)
        return self

    def timeout(self, timeout):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._timeout = timeout
        return self

    def flags(self, flags):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._flags = int(flags)
        return self

    def submit(self, callback):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if callback is None:
            raise ValueError("callback must not be None")
        if not self._parts:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        pending = _PendingActorJoin(callback=callback)
        try:
            self._node._submit_actor_join(
                self._actor_ref,
                self._dest_node_rid,
                self._dest_spot_rid,
                self._parts,
                pending,
                flags=self._flags,
                timeout=self._timeout,
            )
        except SubmitError as ex:
            if self._flags & 1 and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise
        return True


class ActorJoinReplyOp:
    """Fluent builder for Spot.reply_actor_join. Multipart reply is optional;
    a zero-message submit() is allowed."""
    __slots__ = ('_spot', '_request', '_accepted', '_parts', '_submitted')

    def __init__(self, spot, request, accepted):
        self._spot = spot
        self._request = request
        self._accepted = bool(accepted)
        self._parts = []
        self._submitted = False

    def message(self, payload):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.append(payload)
        return self

    def messages(self, *payloads):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.extend(payloads)
        return self

    def submit(self):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._submitted = True
        self._spot._submit_actor_join_reply(
            self._request, self._accepted, self._parts
        )


class _ActorRequestOp:
    """Base class for payload-less Actor request builders (leave / destroy /
    bind / unbind). submit_async() returns list[Message]; submit(callback)
    invokes callback(RequestResult, list[Message])."""

    __slots__ = ('_node', '_timeout', '_submitted')

    def __init__(self, node):
        self._node = node
        self._timeout = 0
        self._submitted = False

    def timeout(self, timeout):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._timeout = timeout
        return self

    def _submit_native(self, pending, timeout):
        raise NotImplementedError

    def submit_async(self):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._submitted = True
        timeout = self._timeout

        async def _run():
            loop = asyncio.get_running_loop()
            pending = _PendingRequest(loop=loop)
            self._submit_native(pending, timeout)
            return await pending.future

        return _run()

    def submit(self, callback):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if callback is None:
            raise ValueError("callback must not be None")
        self._submitted = True
        pending = _PendingRequest(callback=callback)
        self._submit_native(pending, self._timeout)
        return True


class ActorLeaveOp(_ActorRequestOp):
    __slots__ = ('_actor_ref', '_current_spot_rid')

    def __init__(self, node, actor_ref, current_spot_rid):
        super().__init__(node)
        self._actor_ref = actor_ref
        self._current_spot_rid = current_spot_rid

    def _submit_native(self, pending, timeout):
        self._node._submit_actor_leave(
            self._actor_ref, self._current_spot_rid, pending, timeout
        )


class ActorDestroyOp(_ActorRequestOp):
    __slots__ = ('_actor_ref',)

    def __init__(self, node, actor_ref):
        super().__init__(node)
        self._actor_ref = actor_ref

    def _submit_native(self, pending, timeout):
        self._node._submit_actor_destroy(self._actor_ref, pending, timeout)


class ActorLookupOp:
    """Fluent builder for SpotNode.remote_actor_get_ref. submit_async() returns
    ActorLookupResult; submit(callback) invokes callback(ActorLookupResult)."""

    __slots__ = ('_node', '_target_node_rid', '_actor_id', '_timeout', '_submitted')

    def __init__(self, node, target_node_rid, actor_id):
        self._node = node
        self._target_node_rid = target_node_rid
        self._actor_id = actor_id
        self._timeout = 0
        self._submitted = False

    def timeout(self, timeout):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._timeout = timeout
        return self

    def submit_async(self):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._submitted = True
        timeout = self._timeout

        async def _run():
            loop = asyncio.get_running_loop()
            pending = _PendingActorLookup(loop=loop)
            self._node._submit_actor_lookup(
                self._target_node_rid, self._actor_id, pending, timeout
            )
            return await pending.future

        return _run()

    def submit(self, callback):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if callback is None:
            raise ValueError("callback must not be None")
        self._submitted = True
        pending = _PendingActorLookup(callback=callback)
        self._node._submit_actor_lookup(
            self._target_node_rid, self._actor_id, pending, self._timeout
        )
        return True


class ActorBindOp(_ActorRequestOp):
    """Async StreamSocket.bind_actor builder."""
    __slots__ = ('_stream', '_session_rid', '_actor_ref')

    def __init__(self, stream, session_rid, actor_ref):
        super().__init__(node=None)
        self._stream = stream
        self._session_rid = session_rid
        self._actor_ref = actor_ref

    def _submit_native(self, pending, timeout):
        self._stream._submit_bind_actor(
            self._session_rid, self._actor_ref, pending, timeout
        )


class ActorUnbindOp(_ActorRequestOp):
    """Async StreamSocket.unbind_actor builder."""
    __slots__ = ('_stream', '_session_rid', '_actor_id')

    def __init__(self, stream, session_rid, actor_id):
        super().__init__(node=None)
        self._stream = stream
        self._session_rid = session_rid
        self._actor_id = actor_id

    def _submit_native(self, pending, timeout):
        self._stream._submit_unbind_actor(
            self._session_rid, self._actor_id, pending, timeout
        )


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
        self._handler = None
        self._handler_cb = None
        self._handler_queue = None
        self._subscribe_thread = None
        self._subscribe_stop = None
        self._send_ready_handler_thread = None
        self._send_ready_handler_stop = None
        self._send_ready_handler_queue = None
        self._send_ready_handler = None
        self._send_ready_handler_cb = None
        self._actor_lifecycle_on_join = None
        self._actor_lifecycle_on_leave = None
        self._actor_lifecycle_on_join_cb = None
        self._actor_lifecycle_on_leave_cb = None
        self._own = True
        self._timers = {}
        self._request_progress = _RequestProgressPump(
            self._drain_request_progress,
            lambda: bool(self._request_pending),
        )

    def _register_timer(self, timer):
        self._timers[timer._handle] = timer

    def _unregister_timer(self, timer):
        self._timers.pop(timer._handle, None)

    def _drain_request_progress(self):
        seen = set()
        for kind, handle in list(self._request_progress_targets.values()):
            if not handle or (kind, handle) in seen:
                continue
            seen.add((kind, handle))
            if kind == "socket":
                lib().zlink_socket_request_progress_internal(handle)
            else:
                lib().zlink_spot_request_progress_internal(handle)

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
        native_parts = []
        for part in parts:
            if isinstance(part, Message):
                native_parts.append(_clone_native_msg(part._msg))
                continue
            native = ZlinkMsg()
            _ = _init_msg_from_buffer(native, part, borrow=False)
            native_parts.append(native)
        return native_parts

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

    def send_channel(self, channel_name):
        return SendOp(
            self,
            lambda parts, flags: self._send_channel_submit(
                channel_name, parts, flags
            ),
        )

    def _send_channel_submit(self, channel_name, parts, flags=0):
        try:
            _ensure_not_in_callback("blocking send")
            channel_bytes = _validated_c_string_value(
                channel_name, field="channel_name", max_length=255
            )
            native_parts = self._native_parts_from_payload(parts)
            rc, err = _submit_parts(
                native_parts,
                lambda part_ptr, part_flag: lib().zlink_spot_send_channel_part(
                    self._handle,
                    channel_bytes,
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
            rc, err = _submit_parts(
                native_parts,
                lambda part_ptr, part_flag: lib().zlink_spot_send_spot_part(
                    self._handle,
                    ctypes.byref(native_node),
                    ctypes.byref(native_spot),
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

    def request_channel(self, channel_name):
        channel_bytes = _validated_c_string_value(
            channel_name, field="channel_name", max_length=255
        )
        return RequestOp(
            self,
            lambda parts, *, timeout=0: self._request_channel_async(
                channel_bytes, parts, timeout=timeout
            ),
            lambda parts, callback, *, flags=0, timeout=0: self._request_channel_callback(
                channel_bytes, parts, callback, flags=flags, timeout=timeout
            ),
        )

    def _request_channel_async(self, channel_bytes, payload, *, timeout=0):
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

    def _request_channel_callback(self, channel_bytes, payload, callback, *, flags=0, timeout=0):
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
        rc, err = _submit_parts(
            native_parts,
            lambda part_ptr, part_flag: lib().zlink_spot_request_channel_part(
                self._handle,
                channel_bytes,
                part_ptr,
                reply_handler,
                ctypes.c_void_p(handle),
                int(flags),
                part_flag,
                _timeout_to_ms(timeout),
            ),
        )
        if rc != 0:
            self._request_pending.pop(handle, None)
            self._request_progress_targets.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, err)

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
        rc, err = _submit_parts(
            native_parts,
            lambda part_ptr, part_flag: lib().zlink_spot_request_spot_part(
                self._handle,
                ctypes.byref(native_node),
                ctypes.byref(native_spot),
                part_ptr,
                reply_handler,
                ctypes.c_void_p(handle),
                int(flags),
                part_flag,
                _timeout_to_ms(timeout),
            ),
        )
        if rc != 0:
            self._request_pending.pop(handle, None)
            self._request_progress_targets.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, err)

    def _recv_subscribed(self, flags):
        return _recv_spot_subscribed(self._handle, flags)

    def subscribe(self, *, flags=0):
        try:
            return self._recv_subscribed(flags)
        except RecvError as ex:
            if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                return None
            raise

    def receive_subscription_event(self, *, flags=0):
        _ = flags
        raise RecvError(RecvResult.NOT_SUPPORTED, _ERRNO_ENOTSUP)

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
        if self._send_ready_handler_thread is not None:
            raise RuntimeError("send-ready handler is already attached")

        stop = threading.Event()
        events = queue.SimpleQueue()
        self._send_ready_handler = handler
        self._send_ready_handler_stop = stop
        self._send_ready_handler_queue = events

        def _callback(_, __):
            if stop.is_set():
                return
            events.put(None)

        callback = _SOCKET_SEND_READY_HANDLER(_callback)
        rc = lib().zlink_send_ready_handler(self._handle, callback, None)
        if rc != 0:
            _raise_last_error()
        self._send_ready_handler_cb = callback
        def _dispatch():
            while True:
                item = events.get()
                if item is _SPOT_CALLBACK_SENTINEL:
                    return
                _enter_callback()
                try:
                    handler(self)
                except Exception:
                    _report_unhandled_callback_exception(handler)
                finally:
                    _leave_callback()

        thread = threading.Thread(target=_dispatch, name="zlink-spot-send-ready")
        thread.daemon = True
        self._send_ready_handler_thread = thread
        thread.start()

    def _ensure_request_reply_handler(self):
        if self._request_reply_handler is None:
            self._request_reply_handler = _REPLY_HANDLER(self._on_reply)
        return self._request_reply_handler

    def _request_with_native(self, native_func, routing_ids, payload, *, callback=None, flags=0, timeout=0):
        if callback is None:
            return self._request_async(native_func, routing_ids, payload, flags=flags, timeout=timeout)
        self._request_callback(native_func, routing_ids, payload, callback, flags=flags, timeout=timeout)
        return None

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

    def on_routed_receive(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")
        if self._routed_handler_cb is not None:
            raise RuntimeError("routed handler is already attached")

        def _callback(source_node_rid_ptr, source_spot_rid_ptr, request_seq, parts_ptr, part_count, _):
            try:
                source_node_rid = (
                    source_node_rid_ptr.contents
                    if source_node_rid_ptr
                    else None
                )
                source_spot_rid = (
                    source_spot_rid_ptr.contents
                    if source_spot_rid_ptr
                    else None
                )
                received = _make_routed_received(
                    source_node_rid,
                    source_spot_rid,
                    int(request_seq),
                    parts_ptr,
                    int(part_count),
                    reply_sender=_make_spot_routed_reply_sender(
                        self,
                        _routing_id_bytes(source_node_rid)
                        if source_node_rid is not None
                        else None,
                        _routing_id_bytes(source_spot_rid)
                        if source_spot_rid is not None
                        else None,
                        int(request_seq),
                    ),
                    send_sender=_make_spot_routed_send_sender(
                        self,
                        _routing_id_bytes(source_node_rid)
                        if source_node_rid is not None
                        else None,
                        _routing_id_bytes(source_spot_rid)
                        if source_spot_rid is not None
                        else None,
                    ),
                )
                handler(received)
            except Exception:
                _report_unhandled_callback_exception(handler)
            else:
                received.close()

        callback = _SPOT_ROUTED_HANDLER(_callback)
        rc = lib().zlink_spot_handler(self._handle, callback, None)
        if rc != 0:
            _raise_result_error(HandlerError, HandlerResult, rc, lib().zlink_errno())
        self._routed_handler = handler
        self._routed_handler_cb = callback

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
                handler(
                    self,
                    SpotDispatchInfo(
                        event=SpotDispatchEvent(int(info.event)),
                        subject_kind=subject_kind,
                        timer=timer,
                        channel_dealer=channel_dealer,
                        actor=actor_ref,
                        _node_handle=self._node._handle,
                    ),
                )
            except Exception:
                _report_unhandled_callback_exception(handler)

        callback = _SPOT_DISPATCH_EVENT_HANDLER(_callback)
        rc = lib().zlink_spot_dispatch_event_handler(self._handle, callback, None)
        if rc != 0:
            _raise_result_error(HandlerError, HandlerResult, rc, lib().zlink_errno())
        self._dispatch_handler = handler
        self._dispatch_handler_cb = callback

    def drain_channel_reply_from(self, subject):
        if hasattr(subject, "_handle"):
            subject = subject._handle
        if isinstance(subject, ctypes.c_void_p):
            subject = subject.value
        if not subject:
            raise ValueError("subject must not be null")
        rc = lib().zlink_spot_channel_reply_progress_from(
            self._handle,
            ctypes.c_void_p(int(subject)),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

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

    def reply_actor_join(self, request, accepted):
        if not isinstance(request, ActorJoinRequest):
            raise TypeError("request must be ActorJoinRequest")
        return ActorJoinReplyOp(self, request, accepted)

    def _submit_actor_join_reply(self, request, accepted, parts):
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
            1 if accepted else 0,
            parts_arg,
            count,
        )
        if rc != 0:
            _close_native_parts(native_parts)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())

    def on_actor_lifecycle(self, on_join, on_leave):
        """Register Actor lifecycle callbacks for this Spot. Passing None for
        both clears the registration."""
        on_join_cb = None
        on_leave_cb = None

        if on_join is not None:
            def _on_join(spot_handle, info_ptr, _ud):
                if not info_ptr:
                    return
                native = info_ptr.contents
                try:
                    info = SpotActorLifecycleInfo(
                        previous_actor=_actor_ref_from_native(native.previous_actor),
                        current_actor=_actor_ref_from_native(native.current_actor),
                        previous_spot_rid=_routing_id_optional(native.previous_spot_rid),
                        current_spot_rid=_routing_id_optional(native.current_spot_rid),
                        join_epoch=int(native.join_epoch),
                        flags=int(native.flags),
                    )
                    on_join(self, info)
                except Exception:
                    _report_unhandled_callback_exception(on_join)
            on_join_cb = _ACTOR_LIFECYCLE_HANDLER(_on_join)

        if on_leave is not None:
            def _on_leave(spot_handle, info_ptr, _ud):
                if not info_ptr:
                    return
                native = info_ptr.contents
                try:
                    info = SpotActorLifecycleInfo(
                        previous_actor=_actor_ref_from_native(native.previous_actor),
                        current_actor=_actor_ref_from_native(native.current_actor),
                        previous_spot_rid=_routing_id_optional(native.previous_spot_rid),
                        current_spot_rid=_routing_id_optional(native.current_spot_rid),
                        join_epoch=int(native.join_epoch),
                        flags=int(native.flags),
                    )
                    on_leave(self, info)
                except Exception:
                    _report_unhandled_callback_exception(on_leave)
            on_leave_cb = _ACTOR_LIFECYCLE_HANDLER(_on_leave)

        rc = lib().zlink_spot_actor_lifecycle_handler(
            self._handle,
            on_join_cb if on_join_cb is not None else ctypes.c_void_p(0),
            on_leave_cb if on_leave_cb is not None else ctypes.c_void_p(0),
            None,
        )
        if rc != 0:
            _raise_result_error(HandlerError, HandlerResult, rc, lib().zlink_errno())
        # Keep references alive so the callbacks aren't GC'd.
        self._actor_lifecycle_on_join_cb = on_join_cb
        self._actor_lifecycle_on_leave_cb = on_leave_cb
        self._actor_lifecycle_on_join = on_join
        self._actor_lifecycle_on_leave = on_leave

    def actors_snapshot(self):
        count = ctypes.c_size_t()
        rc = lib().zlink_spot_actors_snapshot(self._handle, None, ctypes.byref(count))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (ZlinkActorRef * int(count.value))()
        rc = lib().zlink_spot_actors_snapshot(
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
        handler_cb = self._handler_cb
        send_ready_handler_cb = self._send_ready_handler_cb
        routed_handler_cb = self._routed_handler_cb
        dispatch_handler_cb = self._dispatch_handler_cb
        stop = self._subscribe_stop
        thread = self._subscribe_thread
        handler_queue = self._handler_queue
        if stop is not None:
            stop.set()
        if handler_queue is not None:
            handler_queue.put(_SPOT_CALLBACK_SENTINEL)
        if (
            thread is not None
            and thread.is_alive()
            and thread is not threading.current_thread()
        ):
            thread.join(timeout=1.0)
        send_ready_stop = self._send_ready_handler_stop
        send_ready_thread = self._send_ready_handler_thread
        send_ready_queue = self._send_ready_handler_queue
        if send_ready_stop is not None:
            send_ready_stop.set()
        if send_ready_queue is not None:
            send_ready_queue.put(_SPOT_CALLBACK_SENTINEL)
        if (
            send_ready_thread is not None
            and send_ready_thread.is_alive()
            and send_ready_thread is not threading.current_thread()
        ):
            send_ready_thread.join(timeout=1.0)
        self._handler = None
        self._handler_queue = None
        self._subscribe_stop = None
        self._subscribe_thread = None
        self._send_ready_handler = None
        self._send_ready_handler_thread = None
        self._send_ready_handler_stop = None
        self._send_ready_handler_queue = None
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
        self._handler_cb = None
        self._send_ready_handler_cb = None
        del handler_cb
        del send_ready_handler_cb
        del routed_handler_cb
        del dispatch_handler_cb
        del lifecycle_on_join_cb
        del lifecycle_on_leave_cb
        if rc != 0:
            _raise_last_error()

    def _ensure_subscribe_direct_mode(self):
        if self._subscribe_thread is not None:
            raise ZlinkError(errno.EBUSY, "spot is in callback subscribe mode")

    def _validate_poller_events(self, events):
        if self._subscribe_thread is not None and (events & 1):
            raise ZlinkError(errno.EBUSY, "spot is in callback subscribe mode")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()
