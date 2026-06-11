# SPDX-License-Identifier: MPL-2.0

from ...handles.native_support import SubmitError, SubmitResult
from .request_progress import (
    PendingActorJoin as _PendingActorJoin,
    PendingActorJoinEntrySpot as _PendingActorJoinEntrySpot,
    PendingActorLookup as _PendingActorLookup,
    PendingRequest as _PendingRequest,
)


class ActorJoinOp:
    """Fluent builder for Actor join operations (async or callback)."""
    __slots__ = (
        "_node",
        "_actor_ref",
        "_dest_node_rid",
        "_dest_spot_rid",
        "_parts",
        "_timeout",
        "_submitted",
    )

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

    def submit(self, callback):
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
    __slots__ = (
        "_node",
        "_actor_ref",
        "_dest_node_rid",
        "_dest_spot_rid",
        "_parts",
        "_timeout",
        "_flags",
        "_submitted",
    )

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


class ActorJoinEntrySpotOp:
    __slots__ = ("_node", "_actor_ref", "_dest_node_rid", "_timeout", "_submitted")

    def __init__(self, node, actor_ref, dest_node_rid):
        self._node = node
        self._actor_ref = actor_ref
        self._dest_node_rid = dest_node_rid
        self._timeout = 0
        self._submitted = False

    def timeout(self, timeout):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._timeout = timeout
        return self

    def submit(self, callback):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if callback is None:
            raise ValueError("callback must not be None")
        self._submitted = True
        pending = _PendingActorJoinEntrySpot(callback=callback)
        self._node._submit_actor_join_entry_spot(
            self._actor_ref,
            self._dest_node_rid,
            pending,
            timeout=self._timeout,
        )
        return True


class ActorJoinReplyOp:
    __slots__ = ("_spot", "_request", "_join_result_code", "_parts", "_submitted")

    def __init__(self, spot, request, join_result_code):
        self._spot = spot
        self._request = request
        self._join_result_code = int(join_result_code)
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
            self._request, self._join_result_code, self._parts
        )


class _ActorRequestOp:
    __slots__ = ("_node", "_timeout", "_submitted")

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
    __slots__ = ("_actor_ref", "_current_spot_rid")

    def __init__(self, node, actor_ref, current_spot_rid):
        super().__init__(node)
        self._actor_ref = actor_ref
        self._current_spot_rid = current_spot_rid

    def _submit_native(self, pending, timeout):
        self._node._submit_actor_leave(
            self._actor_ref, self._current_spot_rid, pending, timeout
        )


class ActorDestroyOp(_ActorRequestOp):
    __slots__ = ("_actor_ref",)

    def __init__(self, node, actor_ref):
        super().__init__(node)
        self._actor_ref = actor_ref

    def _submit_native(self, pending, timeout):
        self._node._submit_actor_destroy(self._actor_ref, pending, timeout)


class ActorLookupOp:
    __slots__ = ("_node", "_target_node_rid", "_actor_id", "_timeout", "_submitted")

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
    __slots__ = ("_stream", "_session_rid", "_actor_ref")

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
    __slots__ = ("_stream", "_session_rid", "_actor_id")

    def __init__(self, stream, session_rid, actor_id):
        super().__init__(node=None)
        self._stream = stream
        self._session_rid = session_rid
        self._actor_id = actor_id

    def _submit_native(self, pending, timeout):
        self._stream._submit_unbind_actor(
            self._session_rid, self._actor_id, pending, timeout
        )
