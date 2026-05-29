# SPDX-License-Identifier: MPL-2.0

import ctypes

from ...._native.ffi import (
    ZlinkActorJoinInfo,
    ZlinkActorRef,
    ZlinkMsg,
    ZlinkSpotActorLifecycleEvent,
    lib,
)
from ....contracts.service.codes import SpotActorLifecycleEventKind
from ...handles.native_support import (
    ConfigError,
    ConfigResult,
    RecvError,
    RecvResult,
    SubmitError,
    SubmitResult,
    _raise_result_error,
    _routing_id_bytes,
)
from ...messaging.message_materializer import Message
from .actor_ops import ActorJoinReplyOp
from .native_parts import (
    close_native_parts as _close_native_parts,
    prepare_native_parts as _prepare_native_parts,
)
from .spot_receive import _clone_payload, _make_message_list
from .spot_models_runtime import (
    ActorJoinInfo,
    ActorJoinRequest,
    SpotActorLifecycleEvent,
    _actor_ref_from_native,
    _routing_id_or_empty,
    _spot_actor_lifecycle_info_from_native,
)


class SpotActorJoinMixin:
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
