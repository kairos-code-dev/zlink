// SPDX-License-Identifier: MPL-2.0

use std::ffi::c_void;
use std::time::Duration;

use crate::error::{RequestError, SubmitError};
use crate::ffi;
use crate::flags::SendFlags;
use crate::message::{Message, RoutingId};
use crate::messaging_operations::{CallbackReady, Empty, Ready, RequestOp};
use crate::native_errors::{check_submit_rc, submit_validation_error};
use crate::request_progress::RequestProgressGuard;
use crate::runtime_bridge::{
    RequestOpCallbackReadyRuntime, RequestOpEmptyRuntime, RequestOpReadyRuntime, RequestOpRuntime,
};
use crate::socket::{prepare_send_parts, submit_part_sequence};

pub(crate) struct NativeRequestOp {
    pub(crate) handle: *mut c_void,
    pub(crate) kind: RequestOpKind,
    pub(crate) parts: Vec<Message>,
    pub(crate) flags: Option<SendFlags>,
    pub(crate) timeout: Duration,
}

unsafe impl Send for NativeRequestOp {}

impl RequestOpRuntime for NativeRequestOp {
    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }

    fn into_any(self: Box<Self>) -> Box<dyn std::any::Any> {
        self
    }
}

pub(crate) fn wrap_request_op<State>(inner: NativeRequestOp) -> RequestOp<State> {
    RequestOp {
        inner: Box::new(inner),
        _state: std::marker::PhantomData,
    }
}

fn take_request_op<State>(op: RequestOp<State>) -> NativeRequestOp {
    *op.inner
        .into_any()
        .downcast::<NativeRequestOp>()
        .expect("zlink native request op")
}

fn request_op_mut<State>(op: &mut RequestOp<State>) -> &mut NativeRequestOp {
    op.inner
        .as_mut()
        .as_any_mut()
        .downcast_mut::<NativeRequestOp>()
        .expect("zlink native request op")
}

pub(crate) enum RequestOpKind {
    DealerRequest,
    RouterRequest { peer_rid: Box<RoutingId> },
}

pub(crate) fn dealer_request_op(handle: *mut c_void) -> RequestOp<Empty> {
    wrap_request_op(NativeRequestOp {
        handle,
        kind: RequestOpKind::DealerRequest,
        parts: Vec::new(),
        flags: None,
        timeout: Duration::ZERO,
    })
}

pub(crate) fn router_request_op(handle: *mut c_void, peer_rid: RoutingId) -> RequestOp<Empty> {
    wrap_request_op(NativeRequestOp {
        handle,
        kind: RequestOpKind::RouterRequest {
            peer_rid: Box::new(peer_rid),
        },
        parts: Vec::new(),
        flags: None,
        timeout: Duration::ZERO,
    })
}

impl RequestOpEmptyRuntime for RequestOp<Empty> {
    fn message(self, message: Message) -> RequestOp<Ready> {
        let op = take_request_op(self);
        wrap_request_op(NativeRequestOp {
            handle: op.handle,
            kind: op.kind,
            parts: vec![message],
            flags: op.flags,
            timeout: op.timeout,
        })
    }
}

impl RequestOpReadyRuntime for RequestOp<Ready> {
    fn message(mut self, message: Message) -> Self {
        request_op_mut(&mut self).parts.push(message);
        self
    }

    fn timeout(mut self, timeout: Duration) -> Self {
        request_op_mut(&mut self).timeout = timeout;
        self
    }

    fn flags(self, flags: SendFlags) -> RequestOp<CallbackReady> {
        let mut op = take_request_op(self);
        op.flags = Some(flags);
        wrap_request_op(op)
    }

    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        submit_request(self, SendFlags::NONE, callback)
    }
}

impl RequestOpCallbackReadyRuntime for RequestOp<CallbackReady> {
    fn message(mut self, message: Message) -> Self {
        request_op_mut(&mut self).parts.push(message);
        self
    }

    fn timeout(mut self, timeout: Duration) -> Self {
        request_op_mut(&mut self).timeout = timeout;
        self
    }

    fn flags(mut self, flags: SendFlags) -> Self {
        request_op_mut(&mut self).flags = Some(flags);
        self
    }

    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        let op = take_request_op(self);
        let flags = op.flags.unwrap_or(SendFlags::NONE);
        submit_request_inner(op, flags, callback)
    }
}

fn submit_request<State, F>(
    operation: RequestOp<State>,
    flags: SendFlags,
    callback: F,
) -> Result<(), SubmitError>
where
    F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
{
    submit_request_inner(take_request_op(operation), flags, callback)
}

fn submit_request_inner<F>(
    mut op: NativeRequestOp,
    flags: SendFlags,
    callback: F,
) -> Result<(), SubmitError>
where
    F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
{
    if op.parts.is_empty() {
        return Err(submit_validation_error());
    }

    let mut native = prepare_send_parts(&mut op.parts)?;
    let progress = Some(RequestProgressGuard::attach_socket(op.handle));
    let state_ptr = Box::into_raw(Box::new(crate::operations::ReplyCallbackState {
        callback: Some(Box::new(callback)),
        progress,
    }));
    let timeout_ms = timeout_to_timeout_ms(op.timeout);
    let rc = match &op.kind {
        RequestOpKind::DealerRequest => {
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_dealer_request_part(
                    op.handle,
                    part,
                    flags.bits(),
                    part_flag,
                    if is_final { timeout_ms } else { 0 },
                    if is_final {
                        Some(crate::operations::reply_callback)
                    } else {
                        None
                    },
                    if is_final {
                        state_ptr.cast()
                    } else {
                        std::ptr::null_mut()
                    },
                )
            })?
        }
        RequestOpKind::RouterRequest { peer_rid } => {
            let peer_rid = peer_rid.as_raw() as *const ffi::zlink_routing_id_t;
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_router_request_part(
                    op.handle,
                    peer_rid,
                    part,
                    flags.bits(),
                    part_flag,
                    if is_final { timeout_ms } else { 0 },
                    if is_final {
                        Some(crate::operations::reply_callback)
                    } else {
                        None
                    },
                    if is_final {
                        state_ptr.cast()
                    } else {
                        std::ptr::null_mut()
                    },
                )
            })?
        }
    };

    if rc != 0 {
        unsafe {
            drop(Box::from_raw(state_ptr));
        }
        for part in &mut native {
            unsafe { ffi::zlink_msg_close(part) };
        }
    }
    check_submit_rc(rc)
}

fn timeout_to_timeout_ms(timeout: Duration) -> u32 {
    timeout
        .as_millis()
        .min(u32::MAX as u128)
        .try_into()
        .unwrap_or(u32::MAX)
}
