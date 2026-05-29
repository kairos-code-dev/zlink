use crate::core_context::Context;
use crate::error::ConfigError;
use crate::ffi;
use crate::socket_contracts::{PubSocket, SocketRuntime};

use super::SocketInner;

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
}

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
