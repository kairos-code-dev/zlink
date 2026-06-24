// SPDX-License-Identifier: MPL-2.0

use super::super::*;
use crate::runtime_bridge::{ReplyOpEmptyRuntime, ReplyOpReadyRuntime};

pub(in crate::service) struct NativeReplyOp {
    pub(in crate::service) handle: *mut c_void,
    pub(in crate::service) kind: ReplyOpKind,
    pub(in crate::service) parts: Vec<Message>,
    pub(in crate::service) flags: SendFlags,
}

unsafe impl Send for NativeReplyOp {}

impl ReplyOpRuntime for NativeReplyOp {
    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }

    fn into_any(self: Box<Self>) -> Box<dyn std::any::Any> {
        self
    }
}

pub(in crate::service) fn wrap_reply_op<State>(inner: NativeReplyOp) -> ReplyOp<State> {
    ReplyOp {
        inner: Box::new(inner),
        _state: std::marker::PhantomData,
    }
}

fn take_reply_op<State>(op: ReplyOp<State>) -> NativeReplyOp {
    *op.inner
        .into_any()
        .downcast::<NativeReplyOp>()
        .expect("zlink native reply op")
}

fn reply_op_mut<State>(op: &mut ReplyOp<State>) -> &mut NativeReplyOp {
    op.inner
        .as_mut()
        .as_any_mut()
        .downcast_mut::<NativeReplyOp>()
        .expect("zlink native reply op")
}

#[allow(clippy::large_enum_variant)]
pub(in crate::service) enum ReplyOpKind {
    // Spot-layer (handle = spot)
    ToSpot {
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
    },
    ToRouter {
        peer_rid: RoutingId,
        request_seq: u64,
    },
    // Router socket (handle = socket)
    RouterReply {
        rid: RoutingId,
        request_seq: u64,
    },
    RouterReplyToSpot {
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
    },
}

pub(crate) fn router_reply_op(
    handle: *mut c_void,
    rid: RoutingId,
    request_seq: u64,
) -> ReplyOp<Empty> {
    wrap_reply_op(NativeReplyOp {
        handle,
        kind: ReplyOpKind::RouterReply { rid, request_seq },
        parts: Vec::new(),
        flags: SendFlags::NONE,
    })
}

pub(crate) fn router_reply_to_spot_op(
    handle: *mut c_void,
    dest_node_rid: RoutingId,
    dest_spot_rid: RoutingId,
    request_seq: u64,
) -> ReplyOp<Empty> {
    wrap_reply_op(NativeReplyOp {
        handle,
        kind: ReplyOpKind::RouterReplyToSpot {
            dest_node_rid,
            dest_spot_rid,
            request_seq,
        },
        parts: Vec::new(),
        flags: SendFlags::NONE,
    })
}

pub(crate) fn spot_reply_to_spot_op(
    handle: *mut c_void,
    dest_node_rid: RoutingId,
    dest_spot_rid: RoutingId,
    request_seq: u64,
) -> ReplyOp<Empty> {
    wrap_reply_op(NativeReplyOp {
        handle,
        kind: ReplyOpKind::ToSpot {
            dest_node_rid,
            dest_spot_rid,
            request_seq,
        },
        parts: Vec::new(),
        flags: SendFlags::NONE,
    })
}

pub(crate) fn spot_reply_to_router_op(
    handle: *mut c_void,
    peer_rid: RoutingId,
    request_seq: u64,
) -> ReplyOp<Empty> {
    wrap_reply_op(NativeReplyOp {
        handle,
        kind: ReplyOpKind::ToRouter {
            peer_rid,
            request_seq,
        },
        parts: Vec::new(),
        flags: SendFlags::NONE,
    })
}

impl ReplyOpEmptyRuntime for ReplyOp<Empty> {
    fn message(self, message: Message) -> ReplyOp<Ready> {
        let op = take_reply_op(self);
        wrap_reply_op(NativeReplyOp {
            handle: op.handle,
            kind: op.kind,
            parts: vec![message],
            flags: op.flags,
        })
    }
}

impl ReplyOpReadyRuntime for ReplyOp<Ready> {
    fn message(mut self, message: Message) -> Self {
        reply_op_mut(&mut self).parts.push(message);
        self
    }

    fn flags(mut self, flags: SendFlags) -> Self {
        reply_op_mut(&mut self).flags = flags;
        self
    }

    /// # Errors: SubmitError
    fn submit(self) -> Result<(), SubmitError> {
        let mut op = take_reply_op(self);
        let flags = op.flags;
        if flags.bits() != 0 {
            return Err(submit_not_supported_error());
        }
        let mut native = prepare_send_parts(&mut op.parts)?;
        let handle = op.handle;
        let rc = match &op.kind {
            ReplyOpKind::ToSpot {
                dest_node_rid,
                dest_spot_rid,
                request_seq,
            } => {
                let nr = dest_node_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let sr = dest_spot_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let seq = *request_seq;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_spot_reply_spot_part(handle, nr, sr, seq, part, part_flag)
                })?
            }
            ReplyOpKind::ToRouter {
                peer_rid,
                request_seq,
            } => {
                let pr = peer_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let seq = *request_seq;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_spot_reply_router_part(handle, pr, seq, part, part_flag)
                })?
            }
            ReplyOpKind::RouterReply { rid, request_seq } => {
                let r = rid.as_raw() as *const ffi::zlink_routing_id_t;
                let seq = *request_seq;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_router_reply_part(handle, r, seq, part, part_flag)
                })?
            }
            ReplyOpKind::RouterReplyToSpot {
                dest_node_rid,
                dest_spot_rid,
                request_seq,
            } => {
                let nr = dest_node_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let sr = dest_spot_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let seq = *request_seq;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_router_reply_spot_part(handle, nr, sr, seq, part, part_flag)
                })?
            }
        };
        drop(op.parts);
        check_submit_rc(rc)
    }
}
