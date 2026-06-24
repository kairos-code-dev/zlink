// SPDX-License-Identifier: MPL-2.0

use super::super::*;
use crate::runtime_bridge::{SendOpEmptyRuntime, SendOpReadyRuntime};

pub(in crate::service) struct NativeSendOp {
    pub(in crate::service) handle: *mut c_void,
    pub(in crate::service) kind: SendOpKind,
    pub(in crate::service) parts: Vec<Message>,
    pub(in crate::service) flags: SendFlags,
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

pub(in crate::service) fn wrap_send_op<State>(inner: NativeSendOp) -> SendOp<State> {
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
pub(in crate::service) enum SendOpKind {
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
