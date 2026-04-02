use std::ffi::c_void;
use std::time::Duration;

use crate::ctx::Context;
use crate::domain::SendResult;
use crate::error::{ZlinkError, check_rc};
use crate::ffi;
use crate::message::IntoMultipart;
use crate::options::{CommonSocketOptions, PubSocketOptions};

use super::{
    SocketInner, impl_attach_discovery, impl_base_socket, impl_connect, impl_recv_options,
    impl_send_options,
};

/// PUB socket – publish messages to topic subscribers.
///
/// Capabilities: `publish`, `on_send_ready`.
/// No receive capabilities – no recv options exposed.
pub struct PubSocket {
    pub(crate) inner: SocketInner,
}

impl PubSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ZlinkError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_PUB)?,
        })
    }

    pub fn publish(&self, topic: &str, parts: impl IntoMultipart) -> Result<(), ZlinkError> {
        self.inner.publish(topic, parts)
    }

    pub fn try_publish(
        &self,
        topic: &str,
        parts: impl IntoMultipart,
    ) -> Result<SendResult, ZlinkError> {
        self.inner.try_publish(topic, parts)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), ZlinkError>
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

    // -- PUB-specific typed options ----------------------------------------

    pub fn set_verbose(&self, enabled: bool) -> Result<(), ZlinkError> {
        set_pub_bool(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_VERBOSE,
            enabled,
        )
    }

    pub fn set_verboser(&self, enabled: bool) -> Result<(), ZlinkError> {
        set_pub_bool(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_VERBOSER,
            enabled,
        )
    }

    pub fn set_nodrop(&self, enabled: bool) -> Result<(), ZlinkError> {
        set_pub_bool(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_NODROP,
            enabled,
        )
    }

    pub fn set_manual(&self, enabled: bool) -> Result<(), ZlinkError> {
        set_pub_bool(
            self.inner.handle,
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_MANUAL,
            enabled,
        )
    }
}

impl_base_socket!(PubSocket);
impl_attach_discovery!(PubSocket);
impl_connect!(PubSocket);
impl_send_options!(PubSocket);
impl_recv_options!(PubSocket);

fn set_pub_bool(
    handle: *mut c_void,
    opt: ffi::zlink_pub_option_t,
    value: bool,
) -> Result<(), ZlinkError> {
    let v: i32 = if value { 1 } else { 0 };
    check_rc(unsafe {
        ffi::zlink_set_pub_option(
            handle,
            opt,
            &v as *const i32 as *const c_void,
            std::mem::size_of::<i32>(),
        )
    })
}
