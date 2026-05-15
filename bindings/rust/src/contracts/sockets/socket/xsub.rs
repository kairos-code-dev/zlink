use std::ffi::c_void;
use std::time::Duration;

use super::{SocketInner, impl_base_socket, impl_connect, impl_recv_options, impl_send_options};
use crate::ctx::Context;
use crate::domain::TopicMessage;
use crate::error::{ConfigError, RecvError, check_config_rc};
use crate::ffi;
use crate::flags::RecvFlags;
use crate::options::{CommonSocketOptions, SubSocketOptions};

/// XSUB socket – extended subscribe, pairs with XPUB.
///
/// Capabilities: `subscribe` (blocking recv), `set_subscription`,
/// `unset_subscription`.
/// No send capabilities – no send options exposed.
pub struct XSubSocket {
    pub(crate) inner: SocketInner,
}

impl XSubSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_XSUB)?,
        })
    }

    pub fn subscribe(&self, out: &mut TopicMessage, flags: RecvFlags) -> Result<bool, RecvError> {
        self.inner.subscribe_recv(out, flags)
    }

    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        self.inner.set_subscription(filter)
    }

    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        self.inner.unset_subscription(filter)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_, Self> {
        CommonSocketOptions::new(self)
    }

    pub fn sub_options(&self) -> SubSocketOptions<'_, Self> {
        SubSocketOptions::new(self)
    }

    pub(crate) fn topics_count(&self) -> Result<i32, ConfigError> {
        let mut v: i32 = 0;
        let mut len = std::mem::size_of::<i32>();
        check_config_rc(unsafe {
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

impl_base_socket!(XSubSocket);
impl_connect!(XSubSocket);
impl_send_options!(XSubSocket);
impl_recv_options!(XSubSocket);
