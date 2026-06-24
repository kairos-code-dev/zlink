// SPDX-License-Identifier: MPL-2.0

use super::*;

mod reply_ops;
mod request_ops;
mod send_ops;

pub(super) use reply_ops::{NativeReplyOp, ReplyOpKind, wrap_reply_op};
pub(super) use request_ops::{NativeRequestOp, RequestOpKind, wrap_request_op};
pub(super) use send_ops::{NativeSendOp, SendOpKind, wrap_send_op};

pub(crate) use reply_ops::{
    router_reply_op, router_reply_to_spot_op, spot_reply_to_router_op, spot_reply_to_spot_op,
};
pub(crate) use request_ops::{dealer_request_op, router_request_op, router_request_to_spot_op};
pub(crate) use send_ops::{
    router_send_to_spot_op, socket_publish_op, socket_send_op, socket_send_to_op,
    spot_send_to_spot_op, stream_bound_actor_send_op,
};

pub(crate) fn actor_bind_op_new(
    handle: *mut c_void,
    session_rid: RoutingId,
    actor: ffi::zlink_actor_ref_t,
) -> ActorBindOp<Empty> {
    wrap_actor_reply_op(NativeActorReplyOp {
        handle,
        kind: NativeActorReplyOpKind::Bind { session_rid, actor },
        timeout: Duration::ZERO,
    })
}

pub(crate) fn actor_unbind_op_new(
    handle: *mut c_void,
    session_rid: RoutingId,
    actor_id: std::ffi::CString,
) -> ActorUnbindOp<Empty> {
    wrap_actor_reply_op(NativeActorReplyOp {
        handle,
        kind: NativeActorReplyOpKind::Unbind {
            session_rid,
            actor_id,
        },
        timeout: Duration::ZERO,
    })
}
