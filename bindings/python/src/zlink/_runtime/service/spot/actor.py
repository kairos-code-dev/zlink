# SPDX-License-Identifier: MPL-2.0

import ctypes

from ...handles.native_support import (
    RecvError,
    RecvResult,
    RequestError,
    RequestResult,
    _raise_result_error,
)
from ...._native.ffi import lib
from .actor_ops import ActorJoinOp, ActorLeaveOp
from .spot_models_runtime import ActorRef, _actor_ref_to_native, _recv_actor_part
from .spot_ops import SendOp


class Actor:
    def __init__(self, node, actor_ref):
        from .spot_node import SpotNode
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
        from .spot import Spot
        if not isinstance(spot, Spot):
            raise TypeError("spot must be Spot")
        return ActorJoinOp(
            self._node,
            self.ref(),
            spot._node.routing_id,
            spot.routing_id,
        )

    def leave(self, spot):
        from .spot import Spot
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
        from .spot import _timeout_to_ms

        native_actor = _actor_ref_to_native(self.ref())
        rc = lib().zlink_spot_node_actor_close_bound_session(
            self._node._handle,
            ctypes.byref(native_actor),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            _raise_result_error(RequestError, RequestResult, rc, lib().zlink_errno())

    def close(self, *, timeout=0):
        from .spot import _timeout_to_ms, _wait_for_reply_submit

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


Actor.__module__ = "zlink.contracts.service.spot"

__all__ = ["Actor"]
