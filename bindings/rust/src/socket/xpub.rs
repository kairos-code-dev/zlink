use std::ffi::c_void;
use std::time::Duration;

use crate::ctx::Context;
use crate::domain::SubscriptionEvent;
use crate::error::{ConfigError, HandlerError, RecvError, SubmitError, check_config_rc};
use crate::ffi;
use crate::flags::{RecvFlags, SendFlags};
use crate::message::{IntoMultipart, Message, RoutingId};
use crate::options::{CommonSocketOptions, PubSocketOptions};

use super::pub_socket::{get_pub_bool, get_pub_int, get_pub_message, set_pub_bytes};
use super::{SocketInner, impl_base_socket, impl_connect, impl_recv_options, impl_send_options};

/// XPUB socket – extended publish with subscription event access.
///
/// Capabilities: `publish`, `receive_subscription_event`, `on_send_ready`.
/// Send + subscription-event recv (not data recv).
pub struct XPubSocket {
    pub(crate) inner: SocketInner,
}

impl XPubSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_XPUB)?,
        })
    }

    pub fn publish(&self, topic: &str, parts: impl IntoMultipart) -> Result<(), SubmitError> {
        self.inner.publish(topic, parts)
    }

    pub fn publish_with_flags(
        &self,
        topic: &str,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<bool, SubmitError> {
        self.inner.publish_with_flags(topic, parts, flags)
    }

    pub fn receive_subscription_event(&self) -> Result<SubscriptionEvent, RecvError> {
        self.inner.receive_subscription_event()
    }

    pub fn receive_subscription_event_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<Option<SubscriptionEvent>, RecvError> {
        self.inner.receive_subscription_event_with_flags(flags)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        self.inner.on_send_ready(handler)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_, Self> {
        CommonSocketOptions::new(self)
    }

    pub fn pub_options(&self) -> PubSocketOptions<'_, Self> {
        PubSocketOptions::new(self)
    }

    // -- XPUB-specific typed options ---------------------------------------

    pub(crate) fn set_verbose(&self, enabled: bool) -> Result<(), ConfigError> {
        set_pub_bool(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_VERBOSE,
            enabled,
        )
    }

    pub(crate) fn set_verboser(&self, enabled: bool) -> Result<(), ConfigError> {
        set_pub_bool(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_VERBOSER,
            enabled,
        )
    }

    pub(crate) fn set_nodrop(&self, enabled: bool) -> Result<(), ConfigError> {
        set_pub_bool(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_NODROP,
            enabled,
        )
    }

    pub(crate) fn set_manual(&self, enabled: bool) -> Result<(), ConfigError> {
        set_pub_bool(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_MANUAL,
            enabled,
        )
    }

    pub(crate) fn manual_last_value(&self) -> Result<bool, ConfigError> {
        get_pub_bool(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_MANUAL_LAST_VALUE,
        )
    }

    pub(crate) fn set_manual_last_value(&self, enabled: bool) -> Result<(), ConfigError> {
        set_pub_bool(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_MANUAL_LAST_VALUE,
            enabled,
        )
    }

    pub(crate) fn welcome_message(&self) -> Result<Message, ConfigError> {
        get_pub_message(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_WELCOME_MSG,
        )
    }

    pub(crate) fn set_welcome_message(&self, message: &Message) -> Result<(), ConfigError> {
        set_pub_bytes(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_WELCOME_MSG,
            message.as_bytes(),
        )
    }

    pub(crate) fn approve_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        set_pub_bytes(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_APPROVE_SUBSCRIBE,
            routing_id.data(),
        )
    }

    pub(crate) fn reject_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        set_pub_bytes(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_REJECT_SUBSCRIBE,
            routing_id.data(),
        )
    }

    pub(crate) fn topics_count(&self) -> Result<i32, ConfigError> {
        get_pub_int(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_TOPICS_COUNT,
        )
    }
}

impl_base_socket!(XPubSocket);
impl_connect!(XPubSocket);
impl_send_options!(XPubSocket);
impl_recv_options!(XPubSocket);

fn set_pub_bool(
    handle: *mut c_void,
    opt: ffi::zlink_pub_option_t,
    value: bool,
) -> Result<(), ConfigError> {
    let v: i32 = if value { 1 } else { 0 };
    check_config_rc(unsafe {
        ffi::zlink_set_pub_option(
            handle,
            opt,
            &v as *const i32 as *const c_void,
            std::mem::size_of::<i32>(),
        )
    })
}
