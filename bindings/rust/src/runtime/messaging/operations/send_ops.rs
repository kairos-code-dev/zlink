// SPDX-License-Identifier: MPL-2.0

use std::ffi::{CString, c_void};

use crate::error::SubmitError;
use crate::ffi;
use crate::flags::SendFlags;
use crate::message::{Message, RoutingId};
use crate::messaging_operations::{Empty, Ready, SendOp};
use crate::native_errors::check_submit_rc;
use crate::runtime_bridge::{SendOpEmptyRuntime, SendOpReadyRuntime, SendOpRuntime};
use crate::socket::{prepare_send_parts, submit_part_sequence};

pub(crate) struct NativeSendOp {
    pub(crate) handle: *mut c_void,
    pub(crate) kind: SendOpKind,
    pub(crate) parts: Vec<Message>,
    pub(crate) flags: SendFlags,
}

unsafe impl Send for NativeSendOp {}

impl SendOpRuntime for NativeSendOp {
    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }

    fn into_any(self: Box<Self>) -> Box<dyn std::any::Any> {
        self
    }
}

pub(crate) fn wrap_send_op<State>(inner: NativeSendOp) -> SendOp<State> {
    SendOp {
        inner: Box::new(inner),
        _state: std::marker::PhantomData,
    }
}

fn take_send_op<State>(op: SendOp<State>) -> NativeSendOp {
    *op.inner
        .into_any()
        .downcast::<NativeSendOp>()
        .expect("zlink native send op")
}

fn send_op_mut<State>(op: &mut SendOp<State>) -> &mut NativeSendOp {
    op.inner
        .as_mut()
        .as_any_mut()
        .downcast_mut::<NativeSendOp>()
        .expect("zlink native send op")
}

pub(crate) enum SendOpKind {
    SocketSend,
    SocketSendTo { target: RoutingId },
    SocketPublish { topic: CString },
}

impl SendOpEmptyRuntime for SendOp<Empty> {
    fn message(self, message: Message) -> SendOp<Ready> {
        let op = take_send_op(self);
        wrap_send_op(NativeSendOp {
            handle: op.handle,
            kind: op.kind,
            parts: vec![message],
            flags: op.flags,
        })
    }
}

impl SendOpReadyRuntime for SendOp<Ready> {
    fn message(mut self, message: Message) -> Self {
        send_op_mut(&mut self).parts.push(message);
        self
    }

    fn flags(self, flags: SendFlags) -> Self {
        let mut op = take_send_op(self);
        op.flags = flags;
        wrap_send_op(op)
    }

    fn submit(self) -> Result<bool, SubmitError> {
        let mut op = take_send_op(self);
        let mut native = prepare_send_parts(&mut op.parts)?;
        let flags = op.flags.bits();
        let rc = match &op.kind {
            SendOpKind::SocketSend => {
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_send_part(op.handle, part, flags, part_flag)
                })?
            }
            SendOpKind::SocketSendTo { target } => {
                let target = target.as_raw() as *const ffi::zlink_routing_id_t;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_send_part_rid(op.handle, target, part, flags, part_flag)
                })?
            }
            SendOpKind::SocketPublish { topic } => {
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_publish_part(op.handle, topic.as_ptr(), part, flags, part_flag)
                })?
            }
        };

        drop(op.parts);
        match check_submit_rc(rc) {
            Ok(()) => Ok(true),
            Err(error) if error.code() == crate::error::SubmitResult::Backpressured => Ok(false),
            Err(error) => Err(error),
        }
    }
}

pub(crate) fn socket_send_op(handle: *mut c_void) -> SendOp<Empty> {
    wrap_send_op(NativeSendOp {
        handle,
        kind: SendOpKind::SocketSend,
        parts: Vec::new(),
        flags: SendFlags::NONE,
    })
}

pub(crate) fn socket_send_to_op(handle: *mut c_void, target: RoutingId) -> SendOp<Empty> {
    wrap_send_op(NativeSendOp {
        handle,
        kind: SendOpKind::SocketSendTo { target },
        parts: Vec::new(),
        flags: SendFlags::NONE,
    })
}

pub(crate) fn socket_publish_op(handle: *mut c_void, topic: CString) -> SendOp<Empty> {
    wrap_send_op(NativeSendOp {
        handle,
        kind: SendOpKind::SocketPublish { topic },
        parts: Vec::new(),
        flags: SendFlags::NONE,
    })
}
