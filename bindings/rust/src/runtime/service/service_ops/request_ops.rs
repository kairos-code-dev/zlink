// SPDX-License-Identifier: MPL-2.0

use super::super::*;
use crate::runtime_bridge::{
    RequestOpCallbackReadyRuntime, RequestOpEmptyRuntime, RequestOpReadyRuntime,
};

pub(in crate::service) struct NativeRequestOp {
    pub(in crate::service) handle: *mut c_void,
    pub(in crate::service) kind: RequestOpKind,
    pub(in crate::service) parts: Vec<Message>,
    pub(in crate::service) flags: Option<SendFlags>,
    pub(in crate::service) timeout: Duration,
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

pub(in crate::service) fn wrap_request_op<State>(inner: NativeRequestOp) -> RequestOp<State> {
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

#[allow(clippy::large_enum_variant)]
pub(in crate::service) enum RequestOpKind {
    // Spot-layer (handle = spot)
    Channel {
        channel_name: std::ffi::CString,
    },
    ToSpot {
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    },
    ToRouter {
        peer_rid: RoutingId,
    },
    // Raw socket (handle = socket)
    DealerRequest,
    RouterRequest {
        peer_rid: RoutingId,
    },
    RouterRequestToSpot {
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    },
    ActorRequest {
        actor: ffi::zlink_actor_ref_t,
    },
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
        kind: RequestOpKind::RouterRequest { peer_rid },
        parts: Vec::new(),
        flags: None,
        timeout: Duration::ZERO,
    })
}

pub(crate) fn router_request_to_spot_op(
    handle: *mut c_void,
    dest_node_rid: RoutingId,
    dest_spot_rid: RoutingId,
) -> RequestOp<Empty> {
    wrap_request_op(NativeRequestOp {
        handle,
        kind: RequestOpKind::RouterRequestToSpot {
            dest_node_rid,
            dest_spot_rid,
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

    /// Setting flags moves to `CallbackReady` (only callback `submit` available).
    fn flags(self, flags: SendFlags) -> RequestOp<CallbackReady> {
        let op = take_request_op(self);
        wrap_request_op(NativeRequestOp {
            handle: op.handle,
            kind: op.kind,
            parts: op.parts,
            flags: Some(flags),
            timeout: op.timeout,
        })
    }

    /// Callback submit.
    /// # Errors: SubmitError
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        request_op_submit_callback_owned(self, SendFlags::NONE, callback)
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

    /// # Errors: SubmitError
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        let mut op = take_request_op(self);
        let flags = op.flags.unwrap_or(SendFlags::NONE);
        request_op_submit_callback(
            op.handle,
            &op.kind,
            &mut op.parts,
            flags,
            op.timeout,
            callback,
        )
    }
}

fn request_op_submit_callback_owned<State, F>(
    op: RequestOp<State>,
    flags: SendFlags,
    callback: F,
) -> Result<(), SubmitError>
where
    F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
{
    let mut op = take_request_op(op);
    request_op_submit_callback(
        op.handle,
        &op.kind,
        &mut op.parts,
        flags,
        op.timeout,
        callback,
    )
}

fn request_op_submit_callback<F>(
    handle: *mut c_void,
    kind: &RequestOpKind,
    parts: &mut [Message],
    flags: SendFlags,
    timeout: Duration,
    callback: F,
) -> Result<(), SubmitError>
where
    F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
{
    let mut native = prepare_send_parts(parts)?;
    let progress = match kind {
        RequestOpKind::Channel { .. }
        | RequestOpKind::ToSpot { .. }
        | RequestOpKind::ToRouter { .. } => Some(RequestProgressGuard::attach_spot(handle)),
        RequestOpKind::ActorRequest { .. } => None,
        RequestOpKind::DealerRequest
        | RequestOpKind::RouterRequest { .. }
        | RequestOpKind::RouterRequestToSpot { .. } => {
            Some(RequestProgressGuard::attach_socket(handle))
        }
    };
    let state_ptr = Box::into_raw(Box::new(SpotReplyCallbackState {
        callback: Some(Box::new(callback)),
        _progress: progress,
    }));
    let timeout_ms = timeout_to_timeout_ms(timeout);
    let rc = match kind {
        RequestOpKind::Channel { channel_name } => {
            let cn = channel_name.as_ptr();
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_spot_request_channel_part(
                    handle,
                    cn,
                    part,
                    request_reply_handler(is_final),
                    request_reply_userdata(is_final, state_ptr),
                    flags.bits(),
                    part_flag,
                    request_reply_timeout(is_final, timeout_ms),
                )
            })?
        }
        RequestOpKind::ToSpot {
            dest_node_rid,
            dest_spot_rid,
        } => {
            let nr = dest_node_rid.as_raw() as *const ffi::zlink_routing_id_t;
            let sr = dest_spot_rid.as_raw() as *const ffi::zlink_routing_id_t;
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_spot_request_spot_part(
                    handle,
                    nr,
                    sr,
                    part,
                    request_reply_handler(is_final),
                    request_reply_userdata(is_final, state_ptr),
                    flags.bits(),
                    part_flag,
                    request_reply_timeout(is_final, timeout_ms),
                )
            })?
        }
        RequestOpKind::ToRouter { peer_rid } => {
            let pr = peer_rid.as_raw() as *const ffi::zlink_routing_id_t;
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_spot_request_spot_part(
                    handle,
                    pr,
                    std::ptr::null(),
                    part,
                    request_reply_handler(is_final),
                    request_reply_userdata(is_final, state_ptr),
                    flags.bits(),
                    part_flag,
                    request_reply_timeout(is_final, timeout_ms),
                )
            })?
        }
        RequestOpKind::DealerRequest => {
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_dealer_request_part(
                    handle,
                    part,
                    flags.bits(),
                    part_flag,
                    request_reply_timeout(is_final, timeout_ms),
                    request_reply_handler(is_final),
                    request_reply_userdata(is_final, state_ptr),
                )
            })?
        }
        RequestOpKind::RouterRequest { peer_rid } => {
            let pr = peer_rid.as_raw() as *const ffi::zlink_routing_id_t;
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_router_request_part(
                    handle,
                    pr,
                    part,
                    flags.bits(),
                    part_flag,
                    request_reply_timeout(is_final, timeout_ms),
                    request_reply_handler(is_final),
                    request_reply_userdata(is_final, state_ptr),
                )
            })?
        }
        RequestOpKind::RouterRequestToSpot {
            dest_node_rid,
            dest_spot_rid,
        } => {
            let nr = dest_node_rid.as_raw() as *const ffi::zlink_routing_id_t;
            let sr = dest_spot_rid.as_raw() as *const ffi::zlink_routing_id_t;
            submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
                ffi::zlink_router_request_spot_part(
                    handle,
                    nr,
                    sr,
                    part,
                    request_reply_handler(is_final),
                    request_reply_userdata(is_final, state_ptr),
                    flags.bits(),
                    part_flag,
                    request_reply_timeout(is_final, timeout_ms),
                )
            })?
        }
        RequestOpKind::ActorRequest { actor } => {
            let timeout_ms = timeout_to_timeout_ms(timeout);
            unsafe {
                ffi::zlink_spot_node_request_to_actor(
                    handle,
                    actor,
                    native.as_mut_ptr(),
                    native.len(),
                    crate::service::spot_runtime::spot_reply_callback,
                    state_ptr.cast(),
                    flags.bits(),
                    timeout_ms,
                )
            }
        }
    };
    if rc != 0 {
        unsafe {
            drop(Box::from_raw(state_ptr));
        }
        for part in &mut native {
            unsafe {
                ffi::zlink_msg_close(part);
            }
        }
    }
    check_submit_rc(rc)
}

fn request_reply_handler(is_final: bool) -> Option<ffi::zlink_reply_handler_fn> {
    if is_final {
        Some(spot_reply_callback)
    } else {
        None
    }
}

fn request_reply_userdata(is_final: bool, state_ptr: *mut SpotReplyCallbackState) -> *mut c_void {
    if is_final {
        state_ptr.cast()
    } else {
        std::ptr::null_mut()
    }
}

fn request_reply_timeout(is_final: bool, timeout_ms: u32) -> u32 {
    if is_final { timeout_ms } else { 0 }
}
