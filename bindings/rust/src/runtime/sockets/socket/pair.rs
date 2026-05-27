use crate::ctx::Context;
use crate::domain::Received;
use crate::error::{ConfigError, HandlerError, RecvError};
use crate::ffi;
use crate::flags::RecvFlags;
use crate::options::CommonSocketOptions;
use crate::service::{Empty, SendOp};

use super::{SendHandle, SocketInner, impl_base_socket, impl_connect};

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

    pub fn send(&self) -> SendOp<Empty> {
        crate::service::socket_send_op(self.inner.handle)
    }

    /// Canonical caller-provided storage recv.
    ///
    /// See `doc/spec/bindings/README.md` "Canonical Recv: Caller-Provided
    /// Storage". `Ok(true)` on success, `Ok(false)` when
    /// [`RecvFlags::DONTWAIT`] finds no data, `Err(_)` on hard error.
    pub fn recv(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        self.inner.recv(out, flags)
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

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(&self.inner)
    }
}

impl_base_socket!(PairSocket);
impl_connect!(PairSocket);
