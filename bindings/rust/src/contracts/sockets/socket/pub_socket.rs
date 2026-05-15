use std::ffi::c_void;
use std::time::Duration;

use crate::ctx::Context;
use crate::error::{ConfigError, HandlerError, check_config_rc};
use crate::ffi;
use crate::message::{Message, RoutingId};
use crate::options::{CommonSocketOptions, PubSocketOptions};
use crate::service::{Empty, SendOp};

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
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_PUB)?,
        })
    }

    pub fn publish(&self, topic: &str) -> SendOp<Empty> {
        let topic = crate::service::fixed_cstring_or_panic(topic, "topic");
        crate::service::socket_publish_op(self.inner.handle, topic)
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

    // -- PUB-specific typed options ----------------------------------------

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

impl_base_socket!(PubSocket);
impl_attach_discovery!(PubSocket);
impl_connect!(PubSocket);
impl_send_options!(PubSocket);
impl_recv_options!(PubSocket);

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

pub(crate) fn set_pub_bytes(
    handle: *mut c_void,
    opt: ffi::zlink_pub_option_t,
    value: &[u8],
) -> Result<(), ConfigError> {
    let ptr = if value.is_empty() {
        std::ptr::null()
    } else {
        value.as_ptr() as *const c_void
    };
    check_config_rc(unsafe { ffi::zlink_set_pub_option(handle, opt, ptr, value.len()) })
}

pub(crate) fn get_pub_int(
    handle: *mut c_void,
    opt: ffi::zlink_pub_option_t,
) -> Result<i32, ConfigError> {
    let mut value: i32 = 0;
    let mut len = std::mem::size_of::<i32>();
    check_config_rc(unsafe {
        ffi::zlink_get_pub_option(handle, opt, &mut value as *mut i32 as *mut c_void, &mut len)
    })?;
    Ok(value)
}

pub(crate) fn get_pub_bool(
    handle: *mut c_void,
    opt: ffi::zlink_pub_option_t,
) -> Result<bool, ConfigError> {
    Ok(get_pub_int(handle, opt)? != 0)
}

pub(crate) fn get_pub_message(
    handle: *mut c_void,
    opt: ffi::zlink_pub_option_t,
) -> Result<Message, ConfigError> {
    let mut buf = vec![0u8; 64 * 1024];
    let mut len = buf.len();
    check_config_rc(unsafe {
        ffi::zlink_get_pub_option(handle, opt, buf.as_mut_ptr() as *mut c_void, &mut len)
    })?;
    Message::from_bytes(&buf[..len])
}
