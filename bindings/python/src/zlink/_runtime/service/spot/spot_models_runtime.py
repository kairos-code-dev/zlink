# SPDX-License-Identifier: MPL-2.0

import ctypes
import errno
from dataclasses import dataclass, field

from ...messaging.message_materializer import Message as _RuntimeMessage
from ...messaging.message_materializer import Message
from ....contracts.sockets.codes import SocketType
from ....contracts.service.codes import (
    SpotActorLifecycleEventKind,
    SpotDispatchEvent,
    SpotDispatchSubjectKind,
    SpotKind,
    SpotNodeSocketOwner,
    SpotNodeState,
    SpotPeerKind,
    SpotPeerSource,
    SpotPeerState,
    SpotRole,
    SubjectKind,
)
from ...._native.ffi import (
    ZLINK_PART_FINAL,
    ZlinkActorRecvInfo,
    ZlinkActorRef,
    ZlinkMsg,
    lib,
)
from ...handles.native_support import (
    RecvError,
    RecvResult,
    RequestResult,
    RoutingId,
    _copy_routing_id,
    _raise_result_error,
    _routing_id_bytes,
    _validated_c_string_value,
)
from ....contracts.eventing.monitor import MonitorStatus


_ERRNO_ENOTSUP = getattr(errno, "ENOTSUP", getattr(errno, "EOPNOTSUPP", 95))


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
    current_spot_rid: RoutingId
    current_spot_kind: SpotKind


@dataclass(frozen=True)
class ActorRecvInfo:
    actor: ActorRef
    source_node_rid: RoutingId
    source_session_rid: RoutingId
    request_id: int
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
    join_result_code: int
    actor: ActorRef
    joined_spot_rid: RoutingId
    join_epoch: int
    flags: int


@dataclass(frozen=True)
class ActorJoinEntrySpotResult:
    result: RequestResult
    join_result_code: int
    actor: ActorRef
    target_node_rid: RoutingId
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
class SpotActorLifecycleEvent:
    kind: SpotActorLifecycleEventKind
    info: SpotActorLifecycleInfo


@dataclass(frozen=True)
class SpotNodeSpotEntry:
    spot_rid: RoutingId
    spot_kind: SpotKind
    dispatch_handler_attached: bool
    joined_actor_count: int
    pending_actor_join_count: int
    route_synced: bool
    last_changed_ms: int


@dataclass(frozen=True)
class SpotNodeActorEntry:
    actor: ActorRef
    current_spot_rid: RoutingId
    current_spot_kind: SpotKind
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
    msg = _RuntimeMessage.__new__(_RuntimeMessage)
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


def _spot_actor_lifecycle_info_from_native(native):
    return SpotActorLifecycleInfo(
        previous_actor=_actor_ref_from_native(native.previous_actor),
        current_actor=_actor_ref_from_native(native.current_actor),
        previous_spot_rid=_routing_id_optional(native.previous_spot_rid),
        current_spot_rid=_routing_id_optional(native.current_spot_rid),
        join_epoch=int(native.join_epoch),
        flags=int(native.flags),
    )


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
            request_id=int(info.request_id),
            flags=int(info.flags),
        ),
        message=_message_from_native(part),
        more=int(more.value) != ZLINK_PART_FINAL,
    )


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
    kind: SpotPeerKind
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
class SpotNodeSocketFilter:
    owner: SpotNodeSocketOwner | None = None
    socket_type: SocketType | None = None
    socket_name: str | None = None


@dataclass(frozen=True)
class SpotNodeSocketEntry:
    owner: SpotNodeSocketOwner
    owner_id: int
    owner_name: str
    socket_name: str
    socket_type: SocketType
    auto_hwm_visible: bool
    snapshot: MonitorStatus
