use crate::core_context::Context;
use crate::error::ConfigError;
use crate::ffi;
use crate::runtime_bridge::SocketRuntime;
use crate::socket_contracts::XPubSocket;

use super::SocketInner;

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
}

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
