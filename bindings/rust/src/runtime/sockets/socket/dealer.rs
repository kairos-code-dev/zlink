use std::sync::atomic::{AtomicU32, Ordering};

use crate::core_context::Context;
use crate::domain::Received;
use crate::error::{ConfigError, HandlerError, RecvError};
use crate::ffi;
use crate::flags::RecvFlags;
use crate::flags::{CommonSocketOptions, DealerSocketOptions};
use crate::message::RoutingId;
use crate::socket_contracts::{DealerSocket, SocketRuntime};
use crate::spot_operations::Empty;
use crate::spot_operations::{RequestOp, SendOp};

use super::{
    SocketInner, impl_attach_discovery, impl_base_socket, impl_connect, impl_routing_id_options,
};

struct NativeDealerSocket {
    inner: SocketInner,
    weight: AtomicU32,
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
                weight: AtomicU32::new(100),
            }),
        })
    }

    pub fn send(&self) -> SendOp<Empty> {
        crate::service::socket_send_op(dealer_inner(self).handle)
    }

    /// Canonical caller-provided storage recv. See
    /// `doc/spec/bindings/README.md`.
    pub fn recv(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        dealer_inner(self).recv(out, flags)
    }

    pub fn request(&self) -> RequestOp<Empty> {
        crate::service::dealer_request_op(dealer_inner(self).handle)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        dealer_inner_mut(self).on_send_ready(handler)
    }

    pub fn set_channel_name(&self, channel_name: &str) -> Result<(), ConfigError> {
        dealer_inner(self).set_channel_name(channel_name)
    }

    pub fn channel_name(&self) -> Result<String, ConfigError> {
        dealer_inner(self).channel_name()
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(dealer_inner(self))
    }

    pub fn dealer_options(&self) -> DealerSocketOptions<'_> {
        DealerSocketOptions::new(self)
    }

    pub(crate) fn cached_weight(&self) -> u32 {
        native_dealer(self).weight.load(Ordering::Relaxed)
    }

    pub(crate) fn store_cached_weight(&self, value: u32) {
        native_dealer(self).weight.store(value, Ordering::Relaxed);
    }
}

impl_base_socket!(DealerSocket, dealer_inner, dealer_inner_mut);
impl_attach_discovery!(DealerSocket, dealer_inner);
impl_connect!(DealerSocket, dealer_inner);
impl_routing_id_options!(DealerSocket, dealer_inner);

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
