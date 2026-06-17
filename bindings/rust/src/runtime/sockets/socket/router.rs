use std::ffi::c_void;
use std::ptr;

use super::{SocketInner, close_unreceived_part};
use crate::core_context::Context;
use crate::domain::Received;
use crate::error::{ConfigError, RecvError};
use crate::ffi;
use crate::message::{Message, RoutingId};
use crate::native_errors::check_recv_rc;
use crate::runtime_bridge::SocketRuntime;
use crate::socket_contracts::RouterSocket;

struct NativeRouterSocket {
    inner: SocketInner,
}

impl SocketRuntime for NativeRouterSocket {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

impl RouterSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: Box::new(NativeRouterSocket {
                inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_ROUTER)?,
            }),
        })
    }
}

pub(crate) fn router_inner(socket: &RouterSocket) -> &SocketInner {
    &socket
        .inner
        .as_any()
        .downcast_ref::<NativeRouterSocket>()
        .expect("zlink native router socket")
        .inner
}

pub(crate) fn router_inner_mut(socket: &mut RouterSocket) -> &mut SocketInner {
    &mut socket
        .inner
        .as_any_mut()
        .downcast_mut::<NativeRouterSocket>()
        .expect("zlink native router socket")
        .inner
}

fn router_received_from_raw(
    handle: *mut c_void,
    source_node_rid: *const ffi::zlink_routing_id_t,
    source_spot_rid: *const ffi::zlink_routing_id_t,
    request_seq: u64,
    parts: Vec<Message>,
) -> Received {
    let rid = if source_node_rid.is_null() {
        RoutingId::from_raw(ffi::zlink_routing_id_t::empty())
    } else {
        unsafe { RoutingId::from_raw(*source_node_rid) }
    };
    let spot_rid = if source_spot_rid.is_null() {
        None
    } else {
        let rid = unsafe { RoutingId::from_raw(*source_spot_rid) };
        if rid.is_empty() { None } else { Some(rid) }
    };
    if let Some(spot_rid) = spot_rid {
        if request_seq == 0 {
            Received::with_router_spot_send_context(handle, rid, spot_rid, parts)
        } else {
            Received::with_spot_reply_context(handle, rid, spot_rid, request_seq, parts)
        }
    } else if request_seq == 0 {
        Received::with_router_send_context(handle, rid, parts)
    } else {
        Received::with_router_reply_context(handle, rid, request_seq, parts)
    }
}

pub(crate) fn recv_router_once(
    handle: *mut c_void,
    flags: u32,
) -> Result<Option<Received>, RecvError> {
    let mut source_node_rid = ptr::null();
    let mut source_spot_rid = ptr::null();
    let mut request_seq = 0u64;
    let mut parts = Vec::new();
    let mut recv_flags = flags;

    loop {
        let mut part = std::mem::MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(part.as_mut_ptr());
        }
        let mut has_more = 0;
        let mut current_source_node_rid = ptr::null();
        let mut current_source_spot_rid = ptr::null();
        let mut current_request_seq = 0u64;
        let rc = unsafe {
            ffi::zlink_router_recv_part(
                handle,
                &mut current_source_node_rid,
                &mut current_source_spot_rid,
                &mut current_request_seq,
                part.as_mut_ptr(),
                &mut has_more,
                recv_flags,
            )
        };
        if parts.is_empty() {
            if rc == crate::error::RecvResult::NoData as i32 {
                close_unreceived_part(&mut part);
                return Ok(None);
            }
            if rc != 0 {
                close_unreceived_part(&mut part);
                let errno = unsafe { ffi::zlink_errno() };
                if errno == libc::EAGAIN {
                    return Ok(None);
                }
                return Err(check_recv_rc(rc).unwrap_err());
            }
            source_node_rid = current_source_node_rid;
            source_spot_rid = current_source_spot_rid;
            request_seq = current_request_seq;
        } else if rc != 0 {
            close_unreceived_part(&mut part);
            return Err(check_recv_rc(rc).unwrap_err());
        }

        parts.push(unsafe { crate::message::Message::from_raw(part.assume_init()) });
        if has_more == 0 {
            return Ok(Some(router_received_from_raw(
                handle,
                source_node_rid,
                source_spot_rid,
                request_seq,
                parts,
            )));
        }
        recv_flags = ffi::ZLINK_DONTWAIT;
    }
}
