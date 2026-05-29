use super::SocketInner;
use crate::core_context::Context;
use crate::error::ConfigError;
use crate::ffi;
use crate::socket_contracts::{SocketRuntime, XSubSocket};

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
}

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
