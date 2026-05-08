use std::ffi::c_void;
use std::time::Duration;

use crate::ctx::Context;
use crate::domain::Received;
use crate::error::{ConfigError, HandlerError, RecvError, SubmitError};
use crate::ffi;
use crate::flags::{RecvFlags, SendFlags};
use crate::message::IntoMultipart;
use crate::options::CommonSocketOptions;

use super::{
    SendHandle, SocketInner, impl_base_socket, impl_connect, impl_recv_options, impl_send_options,
};

/// PAIR socket – bidirectional one-to-one messaging.
///
/// Capabilities: `send`, `recv`, `on_send_ready`.
pub struct PairSocket {
    pub(crate) inner: SocketInner,
}

impl PairSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_PAIR)?,
        })
    }

    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), SubmitError> {
        self.inner.send(parts)
    }

    pub fn try_send(&self, parts: impl IntoMultipart) -> Result<bool, SubmitError> {
        self.inner.try_send(parts)
    }

    pub fn send_with_flags(
        &self,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<bool, SubmitError> {
        self.inner.send_with_flags(parts, flags)
    }

    pub fn recv(&self) -> Result<Received, RecvError> {
        self.inner.recv()
    }

    pub fn try_recv(&self) -> Result<Option<Received>, RecvError> {
        self.inner.recv_no_wait()
    }

    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Option<Received>, RecvError> {
        self.inner.recv_with_flags(flags)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        self.inner.on_send_ready(handler)
    }

    /// Obtain a lightweight, cloneable handle for sending from callbacks or
    /// other threads. The returned handle does not own the socket; the
    /// `PairSocket` must remain alive while the handle is in use.
    pub fn send_handle(&self) -> SendHandle {
        SendHandle::new(self.inner.handle)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_, Self> {
        CommonSocketOptions::new(self)
    }
}

impl_base_socket!(PairSocket);
impl_connect!(PairSocket);
impl_send_options!(PairSocket);
impl_recv_options!(PairSocket);
