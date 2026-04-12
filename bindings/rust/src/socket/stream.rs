use std::ffi::c_void;
use std::time::Duration;

use crate::ctx::Context;
use crate::domain::{Received, SendResult};
use crate::error::{ZlinkError, check_rc};
use crate::ffi;
use crate::flags::{RecvFlags, SendFlags};
use crate::message::{IntoMultipart, RoutingId};
use crate::options::{CommonSocketOptions, StreamSocketOptions};

use super::{
    SendHandle, SocketInner, impl_base_socket, impl_recv_options, impl_routing_id_options,
    impl_send_options,
};

/// STREAM socket – raw TCP/transport-level messaging with routing-id.
///
/// All sends are routed to a specific peer. STREAM supports both direct
/// recv and callback modes.
///
/// Capabilities: `send` (routed), `recv`, `on_receive`, `on_send_ready`.
/// No general `connect` / `disconnect` – peers connect inward.
pub struct StreamSocket {
    pub(crate) inner: SocketInner,
}

impl StreamSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ZlinkError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_STREAM)?,
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
        self.inner.recv()
    }

    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, ZlinkError> {
        self.inner.recv_with_flags(flags)
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
    /// `StreamSocket` must remain alive while the handle is in use.
    pub fn send_handle(&self) -> SendHandle {
        SendHandle::new(self.inner.handle)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_, Self> {
        CommonSocketOptions::new(self)
    }

    pub fn stream_options(&self) -> StreamSocketOptions<'_> {
        StreamSocketOptions::new(self)
    }

    // -- STREAM-specific typed options -------------------------------------

    pub(crate) fn set_notify(&self, enabled: bool) -> Result<(), ZlinkError> {
        set_stream_bool_option(
            self.inner.handle,
            ffi::zlink_stream_option_t::ZLINK_STREAM_OPT_NOTIFY,
            enabled,
        )
    }

    pub(crate) fn notify(&self) -> Result<bool, ZlinkError> {
        get_stream_bool_option(
            self.inner.handle,
            ffi::zlink_stream_option_t::ZLINK_STREAM_OPT_NOTIFY,
        )
    }
}

fn set_stream_bool_option(
    handle: *mut c_void,
    option: ffi::zlink_stream_option_t,
    enabled: bool,
) -> Result<(), ZlinkError> {
    let value: i32 = if enabled { 1 } else { 0 };
    check_rc(unsafe {
        ffi::zlink_set_stream_option(
            handle,
            option,
            &value as *const i32 as *const c_void,
            std::mem::size_of::<i32>(),
        )
    })
}

fn get_stream_bool_option(
    handle: *mut c_void,
    option: ffi::zlink_stream_option_t,
) -> Result<bool, ZlinkError> {
    let mut value: i32 = 0;
    let mut len = std::mem::size_of::<i32>();
    check_rc(unsafe {
        ffi::zlink_get_stream_option(
            handle,
            option,
            &mut value as *mut i32 as *mut c_void,
            &mut len,
        )
    })?;
    Ok(value != 0)
}

impl_base_socket!(StreamSocket);
// No impl_connect – STREAM socket does not use general connect
impl_send_options!(StreamSocket);
impl_recv_options!(StreamSocket);
impl_routing_id_options!(StreamSocket);
