use std::ffi::c_void;
use std::time::Duration;

use crate::ctx::Context;
use crate::domain::{Received, SendResult};
use crate::error::{ZlinkError, check_rc};
use crate::ffi;
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

    pub fn try_send(
        &self,
        target: &RoutingId,
        parts: impl IntoMultipart,
    ) -> Result<SendResult, ZlinkError> {
        self.inner.try_send_to(target, parts)
    }

    pub fn recv(&self) -> Result<Received, ZlinkError> {
        self.inner.recv()
    }

    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError> {
        self.inner.try_recv()
    }

    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), ZlinkError>
    where
        F: Fn(Received) + Send + 'static,
    {
        self.inner.on_receive(handler)
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

    pub fn set_mandatory(&self, enabled: bool) -> Result<(), ZlinkError> {
        set_router_bool(
            self.inner.handle,
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_MANDATORY,
            enabled,
        )
    }

    pub fn set_handover(&self, enabled: bool) -> Result<(), ZlinkError> {
        set_router_bool(
            self.inner.handle,
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_HANDOVER,
            enabled,
        )
    }

    pub fn set_probe(&self, enabled: bool) -> Result<(), ZlinkError> {
        set_router_bool(
            self.inner.handle,
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_PROBE,
            enabled,
        )
    }

    pub fn set_connect_routing_id(&self, id: &RoutingId) -> Result<(), ZlinkError> {
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
