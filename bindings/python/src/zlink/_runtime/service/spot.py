# SPDX-License-Identifier: MPL-2.0
#
# Re-export shim. The spot service implementation lives in the public
# contract source at zlink/contracts/service/spot.py.

from ...contracts.service.spot import (  # noqa: F401
    Actor,
    ActorBindOp,
    ActorJoinEntrySpotResult,
    ActorJoinInfo,
    ActorJoinRequest,
    ActorJoinResult,
    ActorPart,
    ActorRecvInfo,
    ActorRef,
    ActorRoute,
    ActorUnbindOp,
    ReplyOp,
    RequestCallbackOp,
    RequestOp,
    SendOp,
    Spot,
    SpotDispatchInfo,
    SpotNode,
    SpotSubscribedPart,
    _PendingRequest,
    _acquire_external_request_progress,
    _actor_id_bytes,
    _actor_ref_from_native,
    _actor_ref_to_native,
    _clone_payload,
    _close_native_parts_array,
    _prepare_native_parts,
    _release_external_request_progress,
    _timeout_to_ms,
)


def __getattr__(name):
    """Forward private (underscore-prefixed) names that internal _runtime
    callers may still import from this module path."""
    from ...contracts.service import spot as _impl

    try:
        return getattr(_impl, name)
    except AttributeError:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}") from None
