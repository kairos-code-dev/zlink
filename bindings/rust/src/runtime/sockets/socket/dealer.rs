use crate::core_context::Context;
use crate::error::ConfigError;
use crate::ffi;
use crate::runtime_bridge::SocketRuntime;
use crate::socket_contracts::DealerSocket;

use super::SocketInner;

struct NativeDealerSocket {
    inner: SocketInner,
}

impl SocketRuntime for NativeDealerSocket {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

impl DealerSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: Box::new(NativeDealerSocket {
                inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_DEALER)?,
            }),
        })
    }
}

fn native_dealer(socket: &DealerSocket) -> &NativeDealerSocket {
    socket
        .inner
        .as_any()
        .downcast_ref::<NativeDealerSocket>()
        .expect("zlink native dealer socket")
}

pub(crate) fn dealer_inner(socket: &DealerSocket) -> &SocketInner {
    &native_dealer(socket).inner
}

pub(crate) fn dealer_inner_mut(socket: &mut DealerSocket) -> &mut SocketInner {
    &mut socket
        .inner
        .as_any_mut()
        .downcast_mut::<NativeDealerSocket>()
        .expect("zlink native dealer socket")
        .inner
}
