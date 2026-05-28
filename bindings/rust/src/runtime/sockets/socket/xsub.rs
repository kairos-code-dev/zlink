use super::{SocketInner, impl_base_socket, impl_connect};
use crate::core_context::Context;
use crate::error::{ConfigError, RecvError};
use crate::ffi;
use crate::flags::RecvFlags;
use crate::flags::{CommonSocketOptions, SubSocketOptions};
use crate::socket_contracts::{SocketRuntime, XSubSocket};
use crate::topic_message_contract::TopicMessage;

struct NativeXSubSocket {
    inner: SocketInner,
}

impl SocketRuntime for NativeXSubSocket {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

impl XSubSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: Box::new(NativeXSubSocket {
                inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_XSUB)?,
            }),
        })
    }

    pub fn subscribe(&self, out: &mut TopicMessage, flags: RecvFlags) -> Result<bool, RecvError> {
        xsub_inner(self).subscribe_recv(out, flags)
    }

    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        xsub_inner(self).set_subscription(filter)
    }

    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        xsub_inner(self).unset_subscription(filter)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(xsub_inner(self))
    }

    pub fn sub_options(&self) -> SubSocketOptions<'_> {
        SubSocketOptions::new(xsub_inner(self))
    }
}

impl_base_socket!(XSubSocket, xsub_inner, xsub_inner_mut);
impl_connect!(XSubSocket, xsub_inner);

pub(crate) fn xsub_inner(socket: &XSubSocket) -> &SocketInner {
    &socket
        .inner
        .as_any()
        .downcast_ref::<NativeXSubSocket>()
        .expect("zlink native xsub socket")
        .inner
}

pub(crate) fn xsub_inner_mut(socket: &mut XSubSocket) -> &mut SocketInner {
    &mut socket
        .inner
        .as_any_mut()
        .downcast_mut::<NativeXSubSocket>()
        .expect("zlink native xsub socket")
        .inner
}
