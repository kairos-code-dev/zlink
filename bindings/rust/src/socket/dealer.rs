use std::ffi::c_void;
use std::time::Duration;

use crate::ctx::Context;
use crate::domain::{Received, SendResult};
use crate::error::{ZlinkError, check_rc};
use crate::ffi;
use crate::message::{IntoMultipart, RoutingId};
use crate::options::{CommonSocketOptions, DealerSocketOptions};

use super::{
    SendHandle, SocketInner, impl_attach_discovery, impl_base_socket, impl_connect,
    impl_recv_options, impl_routing_id_options, impl_send_options,
};

/// DEALER socket – asynchronous request/reply pattern (client side).
///
/// Capabilities: `send`, `recv`, `on_receive`, `on_send_ready`.
pub struct DealerSocket {
    pub(crate) inner: SocketInner,
}

impl DealerSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ZlinkError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_DEALER)?,
        })
    }

    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), ZlinkError> {
        self.inner.send(parts)
    }

    pub fn try_send(&self, parts: impl IntoMultipart) -> Result<SendResult, ZlinkError> {
        self.inner.try_send(parts)
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
    /// `DealerSocket` must remain alive while the handle is in use.
    pub fn send_handle(&self) -> SendHandle {
        SendHandle::new(self.inner.handle)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_, Self> {
        CommonSocketOptions::new(self)
    }

    pub fn dealer_options(&self) -> DealerSocketOptions<'_> {
        DealerSocketOptions::new(self)
    }

    pub(crate) fn set_probe(&self, enabled: bool) -> Result<(), ZlinkError> {
        set_dealer_bool_option(
            self.inner.handle,
            ffi::zlink_dealer_option_t::ZLINK_DEALER_OPT_PROBE,
            enabled,
        )
    }
}

fn set_dealer_bool_option(
    handle: *mut c_void,
    option: ffi::zlink_dealer_option_t,
    enabled: bool,
) -> Result<(), ZlinkError> {
    let value: i32 = if enabled { 1 } else { 0 };
    check_rc(unsafe {
        ffi::zlink_set_dealer_option(
            handle,
            option,
            &value as *const i32 as *const c_void,
            std::mem::size_of::<i32>(),
        )
    })
}

impl_base_socket!(DealerSocket);
impl_attach_discovery!(DealerSocket);
impl_connect!(DealerSocket);
impl_send_options!(DealerSocket);
impl_recv_options!(DealerSocket);
impl_routing_id_options!(DealerSocket);
