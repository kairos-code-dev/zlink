use std::ffi::c_void;

use crate::core_context::Context;
use crate::error::ConfigError;
use crate::ffi;
use crate::runtime_bridge::PairSocketRuntime;
use crate::socket_contracts::PairSocket;

use super::SocketInner;

struct NativePairSocket {
    inner: SocketInner,
}

impl PairSocketRuntime for NativePairSocket {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

impl PairSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: Box::new(NativePairSocket {
                inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_PAIR)?,
            }),
        })
    }
}

pub(crate) fn pair_inner(socket: &PairSocket) -> &SocketInner {
    &socket
        .inner
        .as_any()
        .downcast_ref::<NativePairSocket>()
        .expect("zlink native pair socket")
        .inner
}

pub(crate) fn pair_inner_mut(socket: &mut PairSocket) -> &mut SocketInner {
    &mut socket
        .inner
        .as_any_mut()
        .downcast_mut::<NativePairSocket>()
        .expect("zlink native pair socket")
        .inner
}

pub(crate) fn pair_handle(socket: &PairSocket) -> *mut c_void {
    pair_inner(socket).handle
}
