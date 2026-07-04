# SPDX-License-Identifier: MPL-2.0

import ctypes

from ...._native.ffi import ZlinkActorJoinEntrySpotResult, ZlinkActorJoinResult, ZlinkActorLookupResult, ZlinkActorRecvInfo, ZlinkActorRef, lib
from ....contracts.core.routing_id import RoutingId
from ...handles.native_support import (
    ConfigError,
    ConfigResult,
    RequestResult,
    SubmitError,
    SubmitResult,
    _REPLY_HANDLER,
    _copy_routing_id,
    _raise_result_error,
    _request_result_from_code,
    _request_result_native_errno,
    _routing_id_bytes,
)
from .actor_ops import ActorDestroyOp, ActorJoinEntrySpotOp, ActorJoinOp, ActorLeaveOp, ActorLookupOp
from .native_parts import close_native_parts as _close_native_parts, prepare_native_parts as _prepare_native_parts
from .request_progress import PendingRequest
from .spot import _ACTOR_JOIN_ENTRY_SPOT_HANDLER, _ACTOR_JOIN_HANDLER, _ACTOR_LOOKUP_HANDLER, _timeout_to_ms
from .spot_models_runtime import (
    ActorJoinEntrySpotResult,
    ActorJoinResult,
    ActorLookupResult,
    ActorRef,
    _actor_id_bytes,
    _actor_ref_from_native,
    _actor_ref_to_native,
)
from .spot_ops import RequestOp, SendOp
from .spot_receive import _clone_payload, _make_message_list


class SpotNodeActorMixin:
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

    def join_actor_entry_spot(self, actor_ref, dest_node_rid, request):
        return ActorJoinEntrySpotOp(self, actor_ref, dest_node_rid, request)

    def leave_actor(self, actor_ref, current_spot_rid):
        return ActorLeaveOp(self, actor_ref, current_spot_rid)

    def send_bound_session_msg(self, actor_ref):
        return SendOp(
            self,
            lambda parts, flags: self._actor_send_bound_session_submit(
                actor_ref, parts, flags
            ),
        )

    def send_to_actor(self, actor_ref):
        return SendOp(
            self,
            lambda parts, flags: self._send_to_actor_submit(actor_ref, parts, flags),
        )

    def request_to_actor(self, actor_ref):
        return RequestOp(
            self,
            lambda parts, callback, flags=0, timeout=0: self._request_to_actor_submit(
                actor_ref, parts, callback, flags, timeout
            ),
        )

    def reply_actor_no_bind(self, info, parts, result=RequestResult.OK):
        native_info = ZlinkActorRecvInfo()
        native_info.actor = _actor_ref_to_native(info.actor)
        _copy_routing_id(native_info.source_node_rid, info.source_node_rid)
        _copy_routing_id(native_info.source_session_rid, info.source_session_rid)
        native_info.request_id = int(info.request_id)
        native_info.flags = int(info.flags)
        native_parts = _clone_payload(parts)
        native_array = _prepare_native_parts(native_parts)
        rc = lib().zlink_spot_node_actor_reply_no_bind(
            self._handle,
            ctypes.byref(native_info),
            native_array if native_parts else None,
            len(native_parts),
            int(result),
        )
        if rc != 0:
            _close_native_parts(native_parts)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())

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

    def _send_to_actor_submit(self, actor_ref, parts, flags=0):
        native_parts = _clone_payload(parts)
        native_array = _prepare_native_parts(native_parts)
        native_actor = _actor_ref_to_native(actor_ref)
        handle = id(native_parts)
        self._actor_request_pending[handle] = PendingRequest()
        rc = lib().zlink_spot_node_send_to_actor(
            self._handle,
            ctypes.byref(native_actor),
            native_array if native_parts else None,
            len(native_parts),
            self._ensure_actor_reply_handler(),
            ctypes.c_void_p(handle),
            int(flags),
            0,
        )
        if rc != 0:
            self._actor_request_pending.pop(handle, None)
            _close_native_parts(native_parts)
            if int(flags) & 1 and rc == int(SubmitResult.BACKPRESSURED):
                return False
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())
        return True

    def _request_to_actor_submit(self, actor_ref, parts, callback, flags=0, timeout=0):
        native_actor = _actor_ref_to_native(actor_ref)
        native_parts = _clone_payload(parts)
        native_array = _prepare_native_parts(native_parts)
        pending = PendingRequest(callback=callback)
        handle = id(pending)
        self._actor_request_pending[handle] = pending
        rc = lib().zlink_spot_node_request_to_actor(
            self._handle,
            ctypes.byref(native_actor),
            native_array if native_parts else None,
            len(native_parts),
            self._ensure_actor_reply_handler(),
            ctypes.c_void_p(handle),
            int(flags),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            self._actor_request_pending.pop(handle, None)
            _close_native_parts(native_parts)
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
            pending.resolve(join_result, [], _request_result_native_errno(RequestResult.INTERNAL_ERROR))
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
        if parts:
            lib().zlink_multipart_close(parts, part_count)
        pending.resolve(join_result, messages, _request_result_native_errno(result))

    def _on_actor_join_entry_spot_reply(self, result_ptr, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._actor_join_entry_spot_pending.pop(handle, None)
        if pending is None:
            return
        if not result_ptr:
            join_result = ActorJoinEntrySpotResult(
                result=RequestResult.INTERNAL_ERROR,
                join_result_code=0,
                actor=ActorRef(node_rid=RoutingId(b""), actor_id="", generation=0),
                target_node_rid=RoutingId(b""),
                joined_spot_rid=RoutingId(b""),
                join_epoch=0,
                flags=0,
            )
            pending.resolve(join_result, [], _request_result_native_errno(RequestResult.INTERNAL_ERROR))
            return
        native = result_ptr.contents
        result = _request_result_from_code(int(native.result))
        join_result = ActorJoinEntrySpotResult(
            result=result,
            join_result_code=int(native.join_result_code),
            actor=_actor_ref_from_native(native.actor),
            target_node_rid=_routing_id_bytes(native.target_node_rid),
            joined_spot_rid=_routing_id_bytes(native.joined_spot_rid),
            join_epoch=int(native.join_epoch),
            flags=int(native.flags),
        )
        messages = []
        if result == RequestResult.OK:
            messages = _make_message_list(parts, part_count)
        if parts:
            lib().zlink_multipart_close(parts, part_count)
        pending.resolve(join_result, messages, _request_result_native_errno(result))

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
            pending.resolve(lookup_result, _request_result_native_errno(RequestResult.INTERNAL_ERROR))
            return
        native = result_ptr.contents
        result = _request_result_from_code(int(native.result))
        lookup_result = ActorLookupResult(
            result=result,
            actor=_actor_ref_from_native(native.actor),
            flags=int(native.flags),
        )
        pending.resolve(lookup_result, _request_result_native_errno(result))

    def _on_actor_reply(self, result_code, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._actor_request_pending.pop(handle, None)
        if pending is None:
            return
        result = _request_result_from_code(int(result_code))
        received = []
        if result == RequestResult.OK:
            received = _make_message_list(parts, part_count)
        pending.resolve(result, received, _request_result_native_errno(result))

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

    def _submit_actor_join_entry_spot(self, actor_ref, dest_node_rid, parts, pending, flags=0, timeout=0):
        native_actor = _actor_ref_to_native(actor_ref)
        native_node = _copy_routing_id(dest_node_rid)
        native_parts = _clone_payload(parts)
        native_array = _prepare_native_parts(native_parts)
        handle = id(pending)
        self._actor_join_entry_spot_pending[handle] = pending
        rc = lib().zlink_spot_node_actor_join_entry_spot(
            self._handle,
            ctypes.byref(native_actor),
            ctypes.byref(native_node),
            native_array if native_parts else None,
            len(native_parts),
            self._ensure_actor_join_entry_spot_handler(),
            ctypes.c_void_p(handle),
            int(flags),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            self._actor_join_entry_spot_pending.pop(handle, None)
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
