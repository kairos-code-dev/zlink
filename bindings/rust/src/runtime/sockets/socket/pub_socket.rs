use crate::ctx::Context;
use crate::error::{ConfigError, HandlerError};
use crate::ffi;
use crate::options::{CommonSocketOptions, PubSocketOptions};
use crate::service::{Empty, SendOp};

use super::{impl_attach_discovery, impl_base_socket, impl_connect, SocketInner};

/// PUB socket – publish messages to topic subscribers.
///
/// Capabilities: `publish`, `on_send_ready`.
/// No receive capabilities – no recv options exposed.
pub struct PubSocket {
    pub(crate) inner: SocketInner,
}

impl PubSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_PUB)?,
        })
    }

    pub fn publish(&self, topic: &str) -> SendOp<Empty> {
        let topic = crate::service::fixed_cstring_or_panic(topic, "topic");
        crate::service::socket_publish_op(self.inner.handle, topic)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        self.inner.on_send_ready(handler)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(&self.inner)
    }

    pub fn pub_options(&self) -> PubSocketOptions<'_> {
        PubSocketOptions::new(&self.inner)
    }
}

impl_base_socket!(PubSocket);
impl_attach_discovery!(PubSocket);
impl_connect!(PubSocket);
