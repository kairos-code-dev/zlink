use super::{SocketInner, impl_attach_discovery, impl_base_socket, impl_connect};
use crate::ctx::Context;
use crate::domain::TopicMessage;
use crate::error::{ConfigError, RecvError};
use crate::ffi;
use crate::flags::RecvFlags;
use crate::options::{CommonSocketOptions, SubSocketOptions};

/// SUB socket – subscribe to topics and receive published messages.
///
/// Capabilities: `subscribe` (blocking recv), `set_subscription`,
/// `unset_subscription`.
/// No send capabilities – no send options exposed.
pub struct SubSocket {
    pub(crate) inner: SocketInner,
}

impl SubSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_SUB)?,
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

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(&self.inner)
    }

    pub fn sub_options(&self) -> SubSocketOptions<'_> {
        SubSocketOptions::new(&self.inner)
    }
}

impl_base_socket!(SubSocket);
impl_attach_discovery!(SubSocket);
impl_connect!(SubSocket);
