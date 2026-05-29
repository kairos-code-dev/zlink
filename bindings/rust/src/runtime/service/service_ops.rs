// SPDX-License-Identifier: MPL-2.0

use super::*;
use crate::spot_operations::{
    ReplyOpEmptyRuntime, ReplyOpReadyRuntime, RequestOpCallbackReadyRuntime, RequestOpEmptyRuntime,
    RequestOpReadyRuntime, SendOpEmptyRuntime, SendOpReadyRuntime,
};

// ---------------------------------------------------------------------------
// Typestate operation builders
// ---------------------------------------------------------------------------

pub(super) struct NativeSendOp {
    pub(super) handle: *mut c_void,
    pub(super) kind: SendOpKind,
    pub(super) parts: Vec<Message>,
    pub(super) flags: SendFlags,
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

pub(super) fn wrap_send_op<State>(inner: NativeSendOp) -> SendOp<State> {
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

#[allow(clippy::large_enum_variant)]
pub(super) enum SendOpKind {
    // ---- Spot-layer dispatch (handle = spot) ----
    Publish {
        topic: std::ffi::CString,
    },
    SendToChannel {
        channel_name: std::ffi::CString,
    },
    SendToSpot {
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    },
    // ---- Raw socket sends (handle = socket) ----
    SocketSend,
    SocketSendTo {
        target: RoutingId,
    },
    SocketPublish {
        topic: std::ffi::CString,
    },
    RouterSendToSpot {
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    },
    // ---- Actor session bound relays ----
    /// `handle` is a SpotNode pointer. Sends one part via
    /// `zlink_spot_node_actor_send_bound_session_msg` (single-part only).
    ActorBoundSession {
        actor: ffi::zlink_actor_ref_t,
    },
    /// `handle` is a STREAM socket pointer. Sends parts via
    /// `zlink_stream_send_bound_actor_part`.
    StreamBoundActor {
        session_rid: RoutingId,
        actor_id: std::ffi::CString,
    },
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
        let op = send_op_mut(&mut self);
        op.parts.push(message);
        self
    }

    fn flags(self, flags: SendFlags) -> Self {
        let mut op = take_send_op(self);
        op.flags = flags;
        wrap_send_op(op)
    }

    /// Submit the send operation.
    /// Returns `Ok(false)` for temporary backpressure with `DONT_WAIT`, `Ok(true)` on success.
    /// # Errors: SubmitError
    fn submit(self) -> Result<bool, SubmitError> {
        let mut op = take_send_op(self);
        let flags = op.flags;
        let handle = op.handle;
        // For ActorBoundSession we send exactly one part via a single FFI call
        // that takes a `*mut zlink_msg_t` and not the part-stream API.
        if let SendOpKind::ActorBoundSession { actor } = &op.kind {
            if op.parts.len() != 1 {
                return Err(submit_validation_error());
            }
            let mut native = take_message_raw(&mut op.parts[0]);
            let rc = unsafe {
                ffi::zlink_spot_node_actor_send_bound_session_msg(
                    handle,
                    actor,
                    &mut native,
                    flags.bits(),
                )
            };
            if rc != 0 {
                unsafe {
                    ffi::zlink_msg_close(&mut native);
                }
            }
            return check_submit_rc(rc).map(|_| true);
        }
        let mut native = prepare_send_parts(&mut op.parts)?;
        let rc = match &op.kind {
            SendOpKind::Publish { topic } => {
                let tp = topic.as_ptr();
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_spot_publish_part(handle, tp, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::SendToChannel { channel_name } => {
                let cn = channel_name.as_ptr();
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_spot_send_channel_part(handle, cn, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::SendToSpot {
                dest_node_rid,
                dest_spot_rid,
            } => {
                let nr = dest_node_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let sr = dest_spot_rid.as_raw() as *const ffi::zlink_routing_id_t;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_spot_send_spot_part(handle, nr, sr, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::SocketSend => {
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_send_part(handle, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::SocketSendTo { target } => {
                let t = target.as_raw() as *const ffi::zlink_routing_id_t;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_send_part_rid(handle, t, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::SocketPublish { topic } => {
                let tp = topic.as_ptr();
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_publish_part(handle, tp, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::RouterSendToSpot {
                dest_node_rid,
                dest_spot_rid,
            } => {
                let nr = dest_node_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let sr = dest_spot_rid.as_raw() as *const ffi::zlink_routing_id_t;
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_router_send_spot_part(handle, nr, sr, part, flags.bits(), part_flag)
                })?
            }
            SendOpKind::StreamBoundActor {
                session_rid,
                actor_id,
            } => {
                let sr = session_rid.as_raw() as *const ffi::zlink_routing_id_t;
                let aid = actor_id.as_ptr();
                submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
                    ffi::zlink_stream_send_bound_actor_part(
                        handle,
                        sr,
                        aid,
                        part,
                        flags.bits(),
                        part_flag,
                    )
                })?
            }
            SendOpKind::ActorBoundSession { .. } => unreachable!(),
        };
        drop(op.parts);
        let result = check_submit_rc(rc);
        match result {
            Ok(()) => Ok(true),
            Err(e) if e.code() == crate::error::SubmitResult::Backpressured => Ok(false),
            Err(e) => Err(e),
        }
    }
}

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

pub(crate) fn stream_bound_actor_send_op(
    handle: *mut c_void,
    session_rid: RoutingId,
    actor_id: std::ffi::CString,
) -> SendOp<Empty> {
    wrap_send_op(NativeSendOp {
        handle,
        kind: SendOpKind::StreamBoundActor {
            session_rid,
            actor_id,
        },
        parts: Vec::new(),
        flags: SendFlags::NONE,
    })
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

pub(crate) fn socket_publish_op(handle: *mut c_void, topic: std::ffi::CString) -> SendOp<Empty> {
    wrap_send_op(NativeSendOp {
        handle,
        kind: SendOpKind::SocketPublish { topic },
        parts: Vec::new(),
        flags: SendFlags::NONE,
    })
}

pub(crate) fn router_send_to_spot_op(
    handle: *mut c_void,
    dest_node_rid: RoutingId,
    dest_spot_rid: RoutingId,
) -> SendOp<Empty> {
    wrap_send_op(NativeSendOp {
        handle,
        kind: SendOpKind::RouterSendToSpot {
            dest_node_rid,
            dest_spot_rid,
        },
        parts: Vec::new(),
        flags: SendFlags::NONE,
    })
}

pub(crate) fn spot_send_to_spot_op(
    handle: *mut c_void,
    dest_node_rid: RoutingId,
    dest_spot_rid: RoutingId,
) -> SendOp<Empty> {
    wrap_send_op(NativeSendOp {
        handle,
        kind: SendOpKind::SendToSpot {
            dest_node_rid,
            dest_spot_rid,
        },
        parts: Vec::new(),
        flags: SendFlags::NONE,
    })
}

pub(super) struct NativeRequestOp {
    pub(super) handle: *mut c_void,
    pub(super) kind: RequestOpKind,
    pub(super) parts: Vec<Message>,
    pub(super) flags: Option<SendFlags>,
    pub(super) timeout: Duration,
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

pub(super) fn wrap_request_op<State>(inner: NativeRequestOp) -> RequestOp<State> {
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
pub(super) enum RequestOpKind {
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

    /// Async submit — produces the reply.
    /// # Errors: ZlinkError (SubmitError on submit, RequestError on completion)
    async fn submit_async(self) -> Result<Vec<Message>, ZlinkError> {
        let (tx, rx) = mpsc::channel();
        request_op_submit_callback_owned(self, SendFlags::NONE, move |result| {
            let _ = tx.send(result);
        })?;
        rx.recv()
            .unwrap_or_else(|_| {
                Err(RequestError::new(
                    crate::error::RequestResult::ProtocolError,
                    libc::EINVAL,
                ))
            })
            .map_err(ZlinkError::from)
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
    };
    if rc != 0 {
        unsafe {
            drop(Box::from_raw(state_ptr));
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

pub(super) struct NativeReplyOp {
    pub(super) handle: *mut c_void,
    pub(super) kind: ReplyOpKind,
    pub(super) parts: Vec<Message>,
    pub(super) flags: SendFlags,
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

pub(super) fn wrap_reply_op<State>(inner: NativeReplyOp) -> ReplyOp<State> {
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
pub(super) enum ReplyOpKind {
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
