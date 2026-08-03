// SPDX-License-Identifier: MPL-2.0

use std::ffi::c_void;

use crate::error::SubmitError;
use crate::ffi;
use crate::flags::SendFlags;
use crate::message::{Message, RoutingId};
use crate::messaging_operations::{Empty, Ready, ReplyOp};
use crate::native_errors::{check_submit_rc, submit_not_supported_error};
use crate::runtime_bridge::{ReplyOpEmptyRuntime, ReplyOpReadyRuntime, ReplyOpRuntime};
use crate::socket::{prepare_send_parts, submit_part_sequence};

pub(crate) struct NativeReplyOp {
    pub(crate) handle: *mut c_void,
    pub(crate) kind: ReplyOpKind,
    pub(crate) parts: Vec<Message>,
    pub(crate) flags: SendFlags,
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

pub(crate) fn wrap_reply_op<State>(inner: NativeReplyOp) -> ReplyOp<State> {
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

pub(crate) enum ReplyOpKind {
    RouterReply { rid: RoutingId, request_seq: u64 },
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

    fn submit(self) -> Result<(), SubmitError> {
        let mut op = take_reply_op(self);
        if op.flags.bits() != 0 {
            return Err(submit_not_supported_error());
        }
        let mut native = prepare_send_parts(&mut op.parts)?;
        let ReplyOpKind::RouterReply { rid, request_seq } = &op.kind;
        let rid = rid.as_raw() as *const ffi::zlink_routing_id_t;
        let rc = submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
            ffi::zlink_router_reply_part(op.handle, rid, *request_seq, part, part_flag)
        })?;
        drop(op.parts);
        check_submit_rc(rc)
    }
}
