use std::ffi::c_void;
use std::mem::MaybeUninit;
use std::ptr;

use crate::actor_models::ActorRef;
use crate::core_context::Context;
use crate::error::{ConfigError, HandlerError};
use crate::ffi;
use crate::message::{Message, RoutingId};
use crate::native_errors::{check_config_rc, check_handler_rc};
use crate::runtime_bridge::SocketRuntime;
use crate::socket_contracts::StreamSocket;
use crate::spot_operations::Empty;
use crate::spot_operations::SendOp;
use crate::spot_operations::{ActorBindOp, ActorUnbindOp};
use crate::SpotNode;

use super::SocketInner;

struct NativeStreamSocket {
    inner: SocketInner,
}

impl SocketRuntime for NativeStreamSocket {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

impl StreamSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: Box::new(NativeStreamSocket {
                inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_STREAM)?,
            }),
        })
    }
}

pub(crate) fn stream_inner(socket: &StreamSocket) -> &SocketInner {
    &socket
        .inner
        .as_any()
        .downcast_ref::<NativeStreamSocket>()
        .expect("zlink native stream socket")
        .inner
}

pub(crate) fn stream_inner_mut(socket: &mut StreamSocket) -> &mut SocketInner {
    &mut socket
        .inner
        .as_any_mut()
        .downcast_mut::<NativeStreamSocket>()
        .expect("zlink native stream socket")
        .inner
}

pub(crate) fn stream_on_packet<F>(socket: &mut StreamSocket, handler: F) -> Result<(), HandlerError>
where
    F: Fn(RoutingId, Message, Message) + Send + 'static,
{
    let (cb, userdata) = super::CallbackBox::new(handler);
    let rc = unsafe {
        ffi::zlink_stream_packet_handler(
            stream_inner(socket).handle,
            stream_packet_trampoline::<F>,
            userdata,
        )
    };
    if rc != 0 {
        drop(cb);
        return check_handler_rc(rc);
    }
    stream_inner_mut(socket).packet_cb = Some(cb);
    Ok(())
}

pub(crate) fn stream_attach_actor_gateway(
    socket: &StreamSocket,
    node: &SpotNode,
) -> Result<(), ConfigError> {
    check_config_rc(unsafe {
        ffi::zlink_stream_attach_actor_gateway(
            stream_inner(socket).handle,
            crate::service::spot_node_handle(node),
        )
    })
}

pub(crate) fn stream_bind_actor(
    socket: &StreamSocket,
    session_rid: &RoutingId,
    actor: &ActorRef,
) -> ActorBindOp<Empty> {
    let raw_actor = actor.to_raw().unwrap_or(ffi::zlink_actor_ref_t {
        node_rid: ffi::zlink_routing_id_t::empty(),
        actor_id: [0; ffi::ZLINK_ACTOR_ID_MAX],
        generation: 0,
    });
    crate::service::actor_bind_op_new(stream_inner(socket).handle, *session_rid, raw_actor)
}

pub(crate) fn stream_unbind_actor(
    socket: &StreamSocket,
    session_rid: &RoutingId,
    actor_id: &str,
) -> ActorUnbindOp<Empty> {
    let c_actor_id = crate::service::fixed_cstring_or_panic(actor_id, "actor_id");
    crate::service::actor_unbind_op_new(stream_inner(socket).handle, *session_rid, c_actor_id)
}

pub(crate) fn stream_send_bound_actor(
    socket: &StreamSocket,
    session_rid: &RoutingId,
    actor_id: &str,
) -> SendOp<Empty> {
    let c_actor_id = crate::service::fixed_cstring_or_panic(actor_id, "actor_id");
    crate::service::stream_bound_actor_send_op(
        stream_inner(socket).handle,
        *session_rid,
        c_actor_id,
    )
}

pub(crate) fn stream_bound_actors(
    socket: &StreamSocket,
    session_rid: &RoutingId,
) -> Result<Vec<ActorRef>, ConfigError> {
    let mut count: usize = 0;
    check_config_rc(unsafe {
        ffi::zlink_stream_bound_actors(
            stream_inner(socket).handle,
            session_rid.as_raw(),
            ptr::null_mut(),
            &mut count,
        )
    })?;
    if count == 0 {
        return Ok(Vec::new());
    }

    let mut entries = vec![unsafe { std::mem::zeroed::<ffi::zlink_actor_ref_t>() }; count];
    let mut actual = count;
    check_config_rc(unsafe {
        ffi::zlink_stream_bound_actors(
            stream_inner(socket).handle,
            session_rid.as_raw(),
            entries.as_mut_ptr(),
            &mut actual,
        )
    })?;
    entries.truncate(std::cmp::min(entries.len(), actual));
    Ok(entries.iter().map(ActorRef::from_raw).collect())
}

fn take_message(raw: *mut ffi::zlink_msg_t) -> Message {
    unsafe {
        let mut dest = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        ffi::zlink_msg_init(dest.as_mut_ptr());
        ffi::zlink_msg_move(dest.as_mut_ptr(), raw);
        Message::from_raw(dest.assume_init())
    }
}

unsafe extern "C" fn stream_packet_trampoline<
    F: Fn(RoutingId, Message, Message) + Send + 'static,
>(
    _stream: *mut c_void,
    source_rid: *const ffi::zlink_routing_id_t,
    header: *mut ffi::zlink_msg_t,
    body: *mut ffi::zlink_msg_t,
    userdata: *mut c_void,
) {
    let handler = unsafe { &*(userdata as *const F) };
    let header = take_message(header);
    let body = take_message(body);
    if source_rid.is_null() {
        return;
    }
    let routing_id = unsafe { RoutingId::from_raw(*source_rid) };
    handler(routing_id, header, body);
}
