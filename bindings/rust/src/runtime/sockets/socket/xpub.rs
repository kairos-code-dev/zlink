use crate::core_context::Context;
use crate::error::{ConfigError, HandlerError, RecvError};
use crate::ffi;
use crate::flags::RecvFlags;
use crate::flags::{CommonSocketOptions, PubSocketOptions};
use crate::messaging_subscription_event::SubscriptionEvent;
use crate::socket_contracts::{SocketRuntime, XPubSocket};
use crate::spot_operations::Empty;
use crate::spot_operations::SendOp;

use super::{SocketInner, impl_base_socket, impl_connect};

struct NativeXPubSocket {
    inner: SocketInner,
}

impl SocketRuntime for NativeXPubSocket {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

impl XPubSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: Box::new(NativeXPubSocket {
                inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_XPUB)?,
            }),
        })
    }

    pub fn publish(&self, topic: &str) -> SendOp<Empty> {
        let topic = crate::service::fixed_cstring_or_panic(topic, "topic");
        crate::service::socket_publish_op(xpub_inner(self).handle, topic)
    }

    pub fn receive_subscription_event(
        &self,
        out: &mut SubscriptionEvent,
        flags: RecvFlags,
    ) -> Result<bool, RecvError> {
        xpub_inner(self).receive_subscription_event(out, flags)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        xpub_inner_mut(self).on_send_ready(handler)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(xpub_inner(self))
    }

    pub fn pub_options(&self) -> PubSocketOptions<'_> {
        PubSocketOptions::new(xpub_inner(self))
    }
}

impl_base_socket!(XPubSocket, xpub_inner, xpub_inner_mut);
impl_connect!(XPubSocket, xpub_inner);

pub(crate) fn xpub_inner(socket: &XPubSocket) -> &SocketInner {
    &socket
        .inner
        .as_any()
        .downcast_ref::<NativeXPubSocket>()
        .expect("zlink native xpub socket")
        .inner
}

pub(crate) fn xpub_inner_mut(socket: &mut XPubSocket) -> &mut SocketInner {
    &mut socket
        .inner
        .as_any_mut()
        .downcast_mut::<NativeXPubSocket>()
        .expect("zlink native xpub socket")
        .inner
}
