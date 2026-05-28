use super::{SocketInner, impl_attach_discovery, impl_base_socket, impl_connect};
use crate::core_context::Context;
use crate::error::{ConfigError, RecvError};
use crate::ffi;
use crate::flags::RecvFlags;
use crate::flags::{CommonSocketOptions, SubSocketOptions};
use crate::socket_contracts::{SocketRuntime, SubSocket};
use crate::topic_message_contract::TopicMessage;

struct NativeSubSocket {
    inner: SocketInner,
}

impl SocketRuntime for NativeSubSocket {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

impl SubSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: Box::new(NativeSubSocket {
                inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_SUB)?,
            }),
        })
    }

    pub fn subscribe(&self, out: &mut TopicMessage, flags: RecvFlags) -> Result<bool, RecvError> {
        sub_inner(self).subscribe_recv(out, flags)
    }

    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        sub_inner(self).set_subscription(filter)
    }

    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        sub_inner(self).unset_subscription(filter)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(sub_inner(self))
    }

    pub fn sub_options(&self) -> SubSocketOptions<'_> {
        SubSocketOptions::new(sub_inner(self))
    }
}

impl_base_socket!(SubSocket, sub_inner, sub_inner_mut);
impl_attach_discovery!(SubSocket, sub_inner);
impl_connect!(SubSocket, sub_inner);

pub(crate) fn sub_inner(socket: &SubSocket) -> &SocketInner {
    &socket
        .inner
        .as_any()
        .downcast_ref::<NativeSubSocket>()
        .expect("zlink native sub socket")
        .inner
}

pub(crate) fn sub_inner_mut(socket: &mut SubSocket) -> &mut SocketInner {
    &mut socket
        .inner
        .as_any_mut()
        .downcast_mut::<NativeSubSocket>()
        .expect("zlink native sub socket")
        .inner
}
