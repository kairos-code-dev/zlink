use std::ffi::c_void;
use std::time::Duration;

use super::{
    SocketInner, impl_attach_discovery, impl_base_socket, impl_connect, impl_recv_options,
    impl_send_options,
};
use crate::ctx::Context;
use crate::domain::TopicMessage;
use crate::error::{ZlinkError, check_rc};
use crate::ffi;
use crate::options::{CommonSocketOptions, SubSocketOptions};

/// SUB socket – subscribe to topics and receive published messages.
///
/// Capabilities: `subscribe` (blocking recv), `try_subscribe` (non-blocking recv),
/// `set_subscription`, `unset_subscription`, `on_subscribe`.
/// No send capabilities – no send options exposed.
pub struct SubSocket {
    pub(crate) inner: SocketInner,
}

impl SubSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ZlinkError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_SUB)?,
        })
    }

    pub fn subscribe(&self) -> Result<TopicMessage, ZlinkError> {
        self.inner.subscribe_recv()
    }

    pub fn try_subscribe(&self) -> Result<Option<TopicMessage>, ZlinkError> {
        self.inner.try_subscribe_recv()
    }

    pub fn set_subscription(&self, filter: &str) -> Result<(), ZlinkError> {
        self.inner.set_subscription(filter)
    }

    pub fn unset_subscription(&self, filter: &str) -> Result<(), ZlinkError> {
        self.inner.unset_subscription(filter)
    }

    pub fn on_subscribe<F>(&mut self, handler: F) -> Result<(), ZlinkError>
    where
        F: Fn(TopicMessage) + Send + 'static,
    {
        self.inner.on_subscribe(handler)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_, Self> {
        CommonSocketOptions::new(self)
    }

    pub fn sub_options(&self) -> SubSocketOptions<'_, Self> {
        SubSocketOptions::new(self)
    }

    // -- SUB-specific typed options ----------------------------------------

    pub fn topics_count(&self) -> Result<i32, ZlinkError> {
        let mut v: i32 = 0;
        let mut len = std::mem::size_of::<i32>();
        check_rc(unsafe {
            ffi::zlink_get_sub_option(
                self.inner.handle,
                ffi::zlink_sub_option_t::ZLINK_SUB_OPT_TOPICS_COUNT,
                &mut v as *mut i32 as *mut c_void,
                &mut len,
            )
        })?;
        Ok(v)
    }
}

impl_base_socket!(SubSocket);
impl_attach_discovery!(SubSocket);
impl_connect!(SubSocket);
impl_send_options!(SubSocket);
impl_recv_options!(SubSocket);
