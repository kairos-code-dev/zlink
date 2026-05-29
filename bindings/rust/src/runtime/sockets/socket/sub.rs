use super::SocketInner;
use crate::core_context::Context;
use crate::error::ConfigError;
use crate::ffi;
use crate::socket_contracts::{SocketRuntime, SubSocket};

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
}

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
