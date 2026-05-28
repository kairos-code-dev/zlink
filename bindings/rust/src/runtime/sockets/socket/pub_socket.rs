use crate::core_context::Context;
use crate::error::{ConfigError, HandlerError};
use crate::ffi;
use crate::flags::{CommonSocketOptions, PubSocketOptions};
use crate::socket_contracts::{PubSocket, SocketRuntime};
use crate::spot_operations::Empty;
use crate::spot_operations::SendOp;

use super::{SocketInner, impl_attach_discovery, impl_base_socket, impl_connect};

struct NativePubSocket {
    inner: SocketInner,
}

impl SocketRuntime for NativePubSocket {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

impl PubSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: Box::new(NativePubSocket {
                inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_PUB)?,
            }),
        })
    }

    pub fn publish(&self, topic: &str) -> SendOp<Empty> {
        let topic = crate::service::fixed_cstring_or_panic(topic, "topic");
        crate::service::socket_publish_op(pub_inner(self).handle, topic)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        pub_inner_mut(self).on_send_ready(handler)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(pub_inner(self))
    }

    pub fn pub_options(&self) -> PubSocketOptions<'_> {
        PubSocketOptions::new(pub_inner(self))
    }
}

impl_base_socket!(PubSocket, pub_inner, pub_inner_mut);
impl_attach_discovery!(PubSocket, pub_inner);
impl_connect!(PubSocket, pub_inner);

pub(crate) fn pub_inner(socket: &PubSocket) -> &SocketInner {
    &socket
        .inner
        .as_any()
        .downcast_ref::<NativePubSocket>()
        .expect("zlink native pub socket")
        .inner
}

pub(crate) fn pub_inner_mut(socket: &mut PubSocket) -> &mut SocketInner {
    &mut socket
        .inner
        .as_any_mut()
        .downcast_mut::<NativePubSocket>()
        .expect("zlink native pub socket")
        .inner
}
