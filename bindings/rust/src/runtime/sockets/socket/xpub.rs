use crate::ctx::Context;
use crate::domain::SubscriptionEvent;
use crate::error::{ConfigError, HandlerError, RecvError};
use crate::ffi;
use crate::flags::RecvFlags;
use crate::options::{CommonSocketOptions, PubSocketOptions};
use crate::service::{Empty, SendOp};

use super::{impl_base_socket, impl_connect, SocketInner};

/// XPUB socket – extended publish with subscription event access.
///
/// Capabilities: `publish`, `receive_subscription_event`, `on_send_ready`.
/// Send + subscription-event recv (not data recv).
pub struct XPubSocket {
    pub(crate) inner: SocketInner,
}

impl XPubSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_XPUB)?,
        })
    }

    pub fn publish(&self, topic: &str) -> SendOp<Empty> {
        let topic = crate::service::fixed_cstring_or_panic(topic, "topic");
        crate::service::socket_publish_op(self.inner.handle, topic)
    }

    pub fn receive_subscription_event(
        &self,
        out: &mut SubscriptionEvent,
        flags: RecvFlags,
    ) -> Result<bool, RecvError> {
        self.inner.receive_subscription_event(out, flags)
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

impl_base_socket!(XPubSocket);
impl_connect!(XPubSocket);
