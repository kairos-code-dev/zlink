use std::ffi::c_void;
use std::sync::atomic::{AtomicU32, Ordering};
use std::time::Duration;

use crate::ctx::Context;
use crate::domain::Received;
use crate::error::{ConfigError, HandlerError, RecvError, check_config_rc};
use crate::ffi;
use crate::flags::RecvFlags;
use crate::message::RoutingId;
use crate::options::{CommonSocketOptions, DealerSocketOptions};
use crate::service::{Empty, RequestOp, SendOp};

use super::{
    SendHandle, SocketInner, impl_attach_discovery, impl_base_socket, impl_connect,
    impl_routing_id_options,
};

/// DEALER socket – asynchronous request/reply pattern (client side).
///
/// Capabilities: `send`, `recv`, `on_send_ready`.
pub struct DealerSocket {
    pub(crate) inner: SocketInner,
    weight: AtomicU32,
}

impl DealerSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_DEALER)?,
            weight: AtomicU32::new(100),
        })
    }

    pub fn send(&self) -> SendOp<Empty> {
        crate::service::socket_send_op(self.inner.handle)
    }

    /// Canonical caller-provided storage recv. See
    /// `doc/spec/bindings/README.md`.
    pub fn recv(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        self.inner.recv(out, flags)
    }

    pub fn request(&self) -> RequestOp<Empty> {
        crate::service::dealer_request_op(self.inner.handle)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        self.inner.on_send_ready(handler)
    }

    pub fn set_channel_name(&self, channel_name: &str) -> Result<(), ConfigError> {
        self.inner.set_channel_name(channel_name)
    }

    pub fn channel_name(&self) -> Result<String, ConfigError> {
        self.inner.channel_name()
    }

    /// Obtain a lightweight, cloneable handle for sending from callbacks or
    /// other threads. The returned handle does not own the socket; the
    /// `DealerSocket` must remain alive while the handle is in use.
    pub fn send_handle(&self) -> SendHandle {
        SendHandle::new(self.inner.handle)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(&self.inner)
    }

    pub fn dealer_options(&self) -> DealerSocketOptions<'_> {
        DealerSocketOptions::new(self)
    }

    pub(crate) fn set_probe(&self, enabled: bool) -> Result<(), ConfigError> {
        set_dealer_bool_option(
            self.inner.handle,
            ffi::zlink_dealer_option_t::ZLINK_DEALER_OPT_PROBE,
            enabled,
        )
    }

    pub(crate) fn weight(&self) -> Result<u32, ConfigError> {
        Ok(self.weight.load(Ordering::Relaxed))
    }

    pub(crate) fn set_weight(&self, value: u32) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_dealer_option(
                self.inner.handle,
                ffi::zlink_dealer_option_t::ZLINK_DEALER_OPT_WEIGHT,
                &value as *const u32 as *const c_void,
                std::mem::size_of::<u32>(),
            )
        })?;
        self.weight.store(value, Ordering::Relaxed);
        Ok(())
    }

    pub(crate) fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError> {
        let millis = timeout_to_ms(value).min(i32::MAX as u32) as i32;
        check_config_rc(unsafe {
            ffi::zlink_set_dealer_option(
                self.inner.handle,
                ffi::zlink_dealer_option_t::ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS,
                &millis as *const i32 as *const c_void,
                std::mem::size_of::<i32>(),
            )
        })
    }
}

fn set_dealer_bool_option(
    handle: *mut c_void,
    option: ffi::zlink_dealer_option_t,
    enabled: bool,
) -> Result<(), ConfigError> {
    let value: i32 = if enabled { 1 } else { 0 };
    check_config_rc(unsafe {
        ffi::zlink_set_dealer_option(
            handle,
            option,
            &value as *const i32 as *const c_void,
            std::mem::size_of::<i32>(),
        )
    })
}

impl_base_socket!(DealerSocket);
impl_attach_discovery!(DealerSocket);
impl_connect!(DealerSocket);
impl_routing_id_options!(DealerSocket);

fn timeout_to_ms(timeout: Duration) -> u32 {
    let millis = timeout.as_millis();
    if millis == 0 {
        0
    } else {
        millis.min(u32::MAX as u128) as u32
    }
}
