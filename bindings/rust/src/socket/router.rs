use std::ffi::c_void;
use std::ptr;
use std::time::Duration;

use crate::ctx::Context;
use crate::domain::{Received, SendResult};
use crate::error::{RecvResult, ZlinkError, check_rc};
use crate::ffi;
use crate::flags::{RecvFlags, SendFlags};
use crate::message::{IntoMultipart, RoutingId};
use crate::options::{CommonSocketOptions, RouterSocketOptions};

use super::{
    SendHandle, SocketInner, impl_attach_discovery, impl_base_socket, impl_connect,
    impl_recv_options, impl_routing_id_options, impl_send_options,
};

/// ROUTER socket – asynchronous request/reply pattern (server side).
///
/// All sends are routed: `send(target, parts)` addresses a specific peer.
/// Capabilities: `send` (routed), `recv`, `on_receive`, `on_send_ready`.
pub struct RouterSocket {
    pub(crate) inner: SocketInner,
}

impl RouterSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ZlinkError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_ROUTER)?,
        })
    }

    pub fn send(&self, target: &RoutingId, parts: impl IntoMultipart) -> Result<(), ZlinkError> {
        self.inner.send_to(target, parts)
    }

    pub fn send_with_flags(
        &self,
        target: &RoutingId,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), ZlinkError> {
        self.inner.send_to_with_flags(target, parts, flags)
    }

    pub fn try_send(
        &self,
        target: &RoutingId,
        parts: impl IntoMultipart,
    ) -> Result<SendResult, ZlinkError> {
        self.inner.try_send_to(target, parts)
    }

    pub fn recv(&self) -> Result<Received, ZlinkError> {
        self.recv_with_flags(RecvFlags::NONE)
    }

    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, ZlinkError> {
        let mut peer_rid = ptr::null();
        let mut request_seq = 0u64;
        let mut parts_ptr: *mut ffi::zlink_msg_t = ptr::null_mut();
        let mut part_count: usize = 0;
        let rc = unsafe {
            ffi::zlink_router_recv(
                self.inner.handle,
                &mut peer_rid,
                &mut request_seq,
                &mut parts_ptr,
                &mut part_count,
                flags.bits(),
            )
        };
        check_rc(rc)?;
        Ok(router_received_from_raw(peer_rid, request_seq, parts_ptr, part_count))
    }

    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError> {
        let mut peer_rid = ptr::null();
        let mut request_seq = 0u64;
        let mut parts_ptr: *mut ffi::zlink_msg_t = ptr::null_mut();
        let mut part_count: usize = 0;
        let rc = unsafe {
            ffi::zlink_router_recv(
                self.inner.handle,
                &mut peer_rid,
                &mut request_seq,
                &mut parts_ptr,
                &mut part_count,
                ffi::ZLINK_DONTWAIT,
            )
        };
        if rc == RecvResult::NoData as i32 {
            return Ok(None);
        }
        if rc != 0 {
            return Err(ZlinkError::last());
        }
        Ok(Some(router_received_from_raw(
            peer_rid,
            request_seq,
            parts_ptr,
            part_count,
        )))
    }

    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), ZlinkError>
    where
        F: Fn(Received) + Send + 'static,
    {
        let (cb, userdata) = super::CallbackBox::new(handler);
        let rc =
            unsafe { ffi::zlink_router_handler(self.inner.handle, router_recv_trampoline::<F>, userdata) };
        if rc != 0 {
            drop(cb);
            return Err(ZlinkError::last());
        }
        self.inner.recv_cb = Some(cb);
        Ok(())
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), ZlinkError>
    where
        F: Fn() + Send + 'static,
    {
        self.inner.on_send_ready(handler)
    }

    /// Obtain a lightweight, cloneable handle for sending from callbacks or
    /// other threads. The returned handle does not own the socket; the
    /// `RouterSocket` must remain alive while the handle is in use.
    pub fn send_handle(&self) -> SendHandle {
        SendHandle::new(self.inner.handle)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_, Self> {
        CommonSocketOptions::new(self)
    }

    pub fn router_options(&self) -> RouterSocketOptions<'_> {
        RouterSocketOptions::new(self)
    }

    // -- ROUTER-specific typed options -------------------------------------

    pub(crate) fn set_mandatory(&self, enabled: bool) -> Result<(), ZlinkError> {
        set_router_bool(
            self.inner.handle,
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_MANDATORY,
            enabled,
        )
    }

    pub(crate) fn set_handover(&self, enabled: bool) -> Result<(), ZlinkError> {
        set_router_bool(
            self.inner.handle,
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_HANDOVER,
            enabled,
        )
    }

    pub(crate) fn set_probe(&self, enabled: bool) -> Result<(), ZlinkError> {
        set_router_bool(
            self.inner.handle,
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_PROBE,
            enabled,
        )
    }

    pub(crate) fn set_connect_routing_id(&self, id: &RoutingId) -> Result<(), ZlinkError> {
        check_rc(unsafe {
            ffi::zlink_set_router_option(
                self.inner.handle,
                ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
                id.data().as_ptr() as *const c_void,
                id.len(),
            )
        })
    }
}

impl_base_socket!(RouterSocket);
impl_attach_discovery!(RouterSocket);
impl_connect!(RouterSocket);
impl_send_options!(RouterSocket);
impl_recv_options!(RouterSocket);
impl_routing_id_options!(RouterSocket);

fn router_received_from_raw(
    peer_rid: *const ffi::zlink_routing_id_t,
    request_seq: u64,
    parts_ptr: *mut ffi::zlink_msg_t,
    part_count: usize,
) -> Received {
    let rid = if peer_rid.is_null() {
        RoutingId::from_raw(ffi::zlink_routing_id_t {
            size: 0,
            data: [0; 255],
        })
    } else {
        unsafe { RoutingId::from_raw(*peer_rid) }
    };
    let parts = super::take_parts(parts_ptr, part_count);
    if request_seq == 0 {
        Received::new(rid, parts)
    } else {
        Received::with_request_seq(rid, parts, request_seq)
    }
}

unsafe extern "C" fn router_recv_trampoline<F: Fn(Received) + Send + 'static>(
    peer_rid: *const ffi::zlink_routing_id_t,
    request_seq: u64,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let handler = unsafe { &*(userdata as *const F) };
    handler(router_received_from_raw(
        peer_rid,
        request_seq,
        parts,
        part_count,
    ));
}

fn set_router_bool(
    handle: *mut c_void,
    opt: ffi::zlink_router_option_t,
    value: bool,
) -> Result<(), ZlinkError> {
    let v: i32 = if value { 1 } else { 0 };
    check_rc(unsafe {
        ffi::zlink_set_router_option(
            handle,
            opt,
            &v as *const i32 as *const c_void,
            std::mem::size_of::<i32>(),
        )
    })
}
