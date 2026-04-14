mod dealer;
mod pair;
mod pub_socket;
mod router;
mod send_handle;
mod stream;
mod sub;
mod xpub;
mod xsub;

pub use dealer::DealerSocket;
pub use pair::PairSocket;
pub use pub_socket::PubSocket;
pub use router::RouterSocket;
pub use send_handle::SendHandle;
pub use stream::StreamSocket;
pub use sub::SubSocket;
pub use xpub::XPubSocket;
pub use xsub::XSubSocket;

use std::ffi::{CStr, CString, c_void};
use std::mem::MaybeUninit;
use std::ptr;
use std::time::Duration;

use crate::ctx::duration_to_millis;
use crate::domain::{Received, SendResult, SubscriptionEvent, TopicMessage};
use crate::error::{
    BindError, CloseError, ConfigError, ConnectError, HandlerError, RecvError, RecvResult,
    SubmitError, check_bind_rc, check_close_rc, check_config_rc, check_connect_rc,
    check_handler_rc, check_recv_rc, check_submit_rc, config_validation_error, last_errno,
    submit_validation_error,
};
use crate::ffi;
use crate::flags::{RecvFlags, SendFlags};
use crate::message::{IntoMultipart, Message, RoutingId};
use crate::service::Discovery;

// ---------------------------------------------------------------------------
// CallbackBox – type-erased, owned callback pointer
// ---------------------------------------------------------------------------

pub(crate) struct CallbackBox {
    data: *mut c_void,
    drop_fn: unsafe fn(*mut c_void),
}

unsafe impl Send for CallbackBox {}

impl CallbackBox {
    pub fn new<F: 'static>(f: F) -> (Self, *mut c_void) {
        let ptr = Box::into_raw(Box::new(f));
        let cb = Self {
            data: ptr as *mut c_void,
            drop_fn: drop_erased::<F>,
        };
        (cb, ptr as *mut c_void)
    }
}

impl Drop for CallbackBox {
    fn drop(&mut self) {
        unsafe { (self.drop_fn)(self.data) }
    }
}

unsafe fn drop_erased<F>(ptr: *mut c_void) {
    unsafe {
        drop(Box::from_raw(ptr as *mut F));
    }
}

// ---------------------------------------------------------------------------
// SocketInner – shared handle + callback storage
// ---------------------------------------------------------------------------

#[allow(dead_code)]
pub(crate) struct SocketInner {
    pub handle: *mut c_void,
    recv_cb: Option<CallbackBox>,
    sub_cb: Option<CallbackBox>,
    send_ready_cb: Option<CallbackBox>,
}

unsafe impl Send for SocketInner {}

#[allow(dead_code)]
impl SocketInner {
    pub fn create(
        ctx: &crate::ctx::Context,
        typ: ffi::zlink_socket_type_t,
    ) -> Result<Self, ConfigError> {
        let handle = unsafe { ffi::zlink_socket(ctx.raw(), typ) };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self {
            handle,
            recv_cb: None,
            sub_cb: None,
            send_ready_cb: None,
        })
    }

    // -- Connection --------------------------------------------------------

    pub fn bind(&self, addr: &str) -> Result<(), BindError> {
        let c = CString::new(addr)
            .map_err(|_| BindError::new(crate::error::BindResult::InvalidArgument, libc::EINVAL))?;
        check_bind_rc(unsafe { ffi::zlink_bind(self.handle, c.as_ptr()) })
    }

    pub fn connect(&self, addr: &str) -> Result<(), ConnectError> {
        let c = CString::new(addr).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe { ffi::zlink_connect(self.handle, c.as_ptr()) })
    }

    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError> {
        let c = CString::new(addr).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe { ffi::zlink_unbind(self.handle, c.as_ptr()) })
    }

    pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError> {
        let c = CString::new(addr).map_err(|_| {
            ConnectError::new(crate::error::ConnectResult::InvalidArgument, libc::EINVAL)
        })?;
        check_connect_rc(unsafe { ffi::zlink_disconnect(self.handle, c.as_ptr()) })
    }

    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_socket_attach_discovery(self.handle, discovery.raw()) })
    }

    // -- Send (non-routed) -------------------------------------------------

    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), SubmitError> {
        self.send_with_flags(parts, SendFlags::NONE)
    }

    pub fn send_with_flags(
        &self,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = unsafe {
            ffi::zlink_send(self.handle, native.as_mut_ptr(), native.len(), flags.bits())
        };
        // Native took ownership, close the empty source messages
        drop(parts);
        check_submit_rc(rc)
    }

    pub(crate) fn try_send(&self, parts: impl IntoMultipart) -> Result<SendResult, SubmitError> {
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = unsafe {
            ffi::zlink_send(
                self.handle,
                native.as_mut_ptr(),
                native.len(),
                ffi::ZLINK_DONTWAIT,
            )
        };
        drop(parts);
        check_send_result(rc)
    }

    // -- Send (routed) -----------------------------------------------------

    pub fn send_to(
        &self,
        target: &RoutingId,
        parts: impl IntoMultipart,
    ) -> Result<(), SubmitError> {
        self.send_to_with_flags(target, parts, SendFlags::NONE)
    }

    pub fn send_to_with_flags(
        &self,
        target: &RoutingId,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = unsafe {
            ffi::zlink_send_rid(
                self.handle,
                target.as_raw(),
                native.as_mut_ptr(),
                native.len(),
                flags.bits(),
            )
        };
        drop(parts);
        check_submit_rc(rc)
    }

    pub(crate) fn try_send_to(
        &self,
        target: &RoutingId,
        parts: impl IntoMultipart,
    ) -> Result<SendResult, SubmitError> {
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = unsafe {
            ffi::zlink_send_rid(
                self.handle,
                target.as_raw(),
                native.as_mut_ptr(),
                native.len(),
                ffi::ZLINK_DONTWAIT,
            )
        };
        drop(parts);
        check_send_result(rc)
    }

    // -- Recv (direct) -----------------------------------------------------

    pub fn recv(&self) -> Result<Received, RecvError> {
        self.recv_with_flags(RecvFlags::NONE)
    }

    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError> {
        let mut rid = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        let mut parts_ptr: *mut ffi::zlink_msg_t = ptr::null_mut();
        let mut part_count: usize = 0;

        let rc = unsafe {
            ffi::zlink_recv(
                self.handle,
                rid.as_mut_ptr(),
                &mut parts_ptr,
                &mut part_count,
                flags.bits(),
            )
        };
        check_recv_rc(rc)?;

        let rid = unsafe { rid.assume_init() };
        let parts = take_parts(parts_ptr, part_count);
        Ok(Received::new(RoutingId::from_raw_optional(rid), parts))
    }

    pub(crate) fn try_recv(&self) -> Result<Option<Received>, RecvError> {
        let mut rid = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        let mut parts_ptr: *mut ffi::zlink_msg_t = ptr::null_mut();
        let mut part_count: usize = 0;

        let rc = unsafe {
            ffi::zlink_recv(
                self.handle,
                rid.as_mut_ptr(),
                &mut parts_ptr,
                &mut part_count,
                ffi::ZLINK_DONTWAIT,
            )
        };
        if rc == RecvResult::NoData as i32 {
            return Ok(None);
        }
        if rc != 0 {
            let errno = unsafe { ffi::zlink_errno() };
            if errno == libc::EAGAIN {
                return Ok(None);
            }
            return Err(RecvError::new(crate::error::RecvResult::Terminated, errno));
        }

        let rid = unsafe { rid.assume_init() };
        let parts = take_parts(parts_ptr, part_count);
        Ok(Some(Received::new(
            RoutingId::from_raw_optional(rid),
            parts,
        )))
    }

    // -- Publish -----------------------------------------------------------

    pub fn publish(&self, topic: &str, parts: impl IntoMultipart) -> Result<(), SubmitError> {
        self.publish_with_flags(topic, parts, SendFlags::NONE)
    }

    pub fn publish_with_flags(
        &self,
        topic: &str,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        let c_topic = CString::new(topic).map_err(|_| submit_validation_error())?;
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = unsafe {
            ffi::zlink_publish(
                self.handle,
                c_topic.as_ptr(),
                native.as_mut_ptr(),
                native.len(),
                flags.bits(),
            )
        };
        drop(parts);
        check_submit_rc(rc)
    }

    pub(crate) fn try_publish(
        &self,
        topic: &str,
        parts: impl IntoMultipart,
    ) -> Result<SendResult, SubmitError> {
        let c_topic = CString::new(topic).map_err(|_| submit_validation_error())?;
        let mut parts = parts.into_parts();
        let mut native = prepare_send_parts(&mut parts)?;
        let rc = unsafe {
            ffi::zlink_publish(
                self.handle,
                c_topic.as_ptr(),
                native.as_mut_ptr(),
                native.len(),
                ffi::ZLINK_DONTWAIT,
            )
        };
        drop(parts);
        check_send_result(rc)
    }

    // -- Subscribe (blocking recv) -----------------------------------------

    pub fn subscribe_recv(&self) -> Result<TopicMessage, RecvError> {
        self.subscribe_recv_with_flags(RecvFlags::NONE)
    }

    pub fn subscribe_recv_with_flags(&self, flags: RecvFlags) -> Result<TopicMessage, RecvError> {
        let mut rid = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        let mut parts_ptr: *mut ffi::zlink_msg_t = ptr::null_mut();
        let mut part_count: usize = 0;
        let mut topic_buf = [0i8; 256];
        let mut topic_len: usize = 256;

        let rc = unsafe {
            ffi::zlink_subscribe(
                self.handle,
                rid.as_mut_ptr(),
                &mut parts_ptr,
                &mut part_count,
                topic_buf.as_mut_ptr(),
                &mut topic_len,
                flags.bits(),
            )
        };
        check_recv_rc(rc)?;

        let rid = unsafe { rid.assume_init() };
        let topic = cstr_buf_to_string(&topic_buf, topic_len);
        let parts = take_parts(parts_ptr, part_count);
        Ok(TopicMessage::new(
            RoutingId::from_raw_optional(rid),
            topic,
            parts,
        ))
    }

    pub(crate) fn try_subscribe_recv(&self) -> Result<Option<TopicMessage>, RecvError> {
        let mut rid = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        let mut parts_ptr: *mut ffi::zlink_msg_t = ptr::null_mut();
        let mut part_count: usize = 0;
        let mut topic_buf = [0i8; 256];
        let mut topic_len: usize = 256;

        let rc = unsafe {
            ffi::zlink_subscribe(
                self.handle,
                rid.as_mut_ptr(),
                &mut parts_ptr,
                &mut part_count,
                topic_buf.as_mut_ptr(),
                &mut topic_len,
                ffi::ZLINK_DONTWAIT as u32,
            )
        };
        if rc == RecvResult::NoData as i32 {
            return Ok(None);
        }
        if rc != 0 {
            let errno = unsafe { ffi::zlink_errno() };
            if errno == libc::EAGAIN {
                return Ok(None);
            }
            return Err(RecvError::new(crate::error::RecvResult::Terminated, errno));
        }

        let rid = unsafe { rid.assume_init() };
        let topic = cstr_buf_to_string(&topic_buf, topic_len);
        let parts = take_parts(parts_ptr, part_count);
        Ok(Some(TopicMessage::new(
            RoutingId::from_raw_optional(rid),
            topic,
            parts,
        )))
    }

    // -- Subscription management -------------------------------------------

    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        let c = CString::new(filter).map_err(|_| config_validation_error())?;
        check_config_rc(unsafe { ffi::zlink_set_subscription(self.handle, c.as_ptr()) })
    }

    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        let c = CString::new(filter).map_err(|_| config_validation_error())?;
        check_config_rc(unsafe { ffi::zlink_unset_subscription(self.handle, c.as_ptr()) })
    }

    // -- Subscription event (XPUB) -----------------------------------------

    pub fn receive_subscription_event(&self) -> Result<SubscriptionEvent, RecvError> {
        self.receive_subscription_event_with_flags(RecvFlags::NONE)
    }

    pub fn receive_subscription_event_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<SubscriptionEvent, RecvError> {
        let mut rid = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        let mut subscribed: i32 = 0;
        let mut topic_buf = [0i8; 256];
        let mut topic_len: usize = 256;

        let rc = unsafe {
            ffi::zlink_subscription_event(
                self.handle,
                rid.as_mut_ptr(),
                &mut subscribed,
                topic_buf.as_mut_ptr(),
                &mut topic_len,
                flags.bits(),
            )
        };
        check_recv_rc(rc)?;

        let rid = unsafe { rid.assume_init() };
        let topic = cstr_buf_to_string(&topic_buf, topic_len);
        Ok(SubscriptionEvent::new(
            RoutingId::from_raw_optional(rid),
            subscribed != 0,
            topic,
        ))
    }

    pub(crate) fn try_receive_subscription_event(
        &self,
    ) -> Result<Option<SubscriptionEvent>, RecvError> {
        let mut rid = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        let mut subscribed: i32 = 0;
        let mut topic_buf = [0i8; 256];
        let mut topic_len: usize = 256;

        let rc = unsafe {
            ffi::zlink_subscription_event(
                self.handle,
                rid.as_mut_ptr(),
                &mut subscribed,
                topic_buf.as_mut_ptr(),
                &mut topic_len,
                ffi::ZLINK_DONTWAIT as u32,
            )
        };
        if rc == RecvResult::NoData as i32 {
            return Ok(None);
        }
        if rc != 0 {
            let errno = unsafe { ffi::zlink_errno() };
            if errno == libc::EAGAIN {
                return Ok(None);
            }
            return Err(RecvError::new(crate::error::RecvResult::Terminated, errno));
        }

        let rid = unsafe { rid.assume_init() };
        let topic = cstr_buf_to_string(&topic_buf, topic_len);
        Ok(Some(SubscriptionEvent::new(
            RoutingId::from_raw_optional(rid),
            subscribed != 0,
            topic,
        )))
    }

    // -- Callback installation ---------------------------------------------

    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn(Received) + Send + 'static,
    {
        let (cb, userdata) = CallbackBox::new(handler);
        let rc = unsafe { ffi::zlink_recv_handler(self.handle, recv_trampoline::<F>, userdata) };
        if rc != 0 {
            drop(cb); // free on failure
            return check_handler_rc(rc);
        }
        self.recv_cb = Some(cb);
        Ok(())
    }

    pub fn on_subscribe<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn(TopicMessage) + Send + 'static,
    {
        let (cb, userdata) = CallbackBox::new(handler);
        let rc = unsafe {
            ffi::zlink_subscribe_handler(self.handle, subscribe_trampoline::<F>, userdata)
        };
        if rc != 0 {
            drop(cb);
            return check_handler_rc(rc);
        }
        self.sub_cb = Some(cb);
        Ok(())
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        let (cb, userdata) = CallbackBox::new(handler);
        let rc = unsafe {
            ffi::zlink_send_ready_handler(self.handle, send_ready_trampoline::<F>, userdata)
        };
        if rc != 0 {
            drop(cb);
            return check_handler_rc(rc);
        }
        self.send_ready_cb = Some(cb);
        Ok(())
    }

    // -- Common typed options (per Option Policy) --------------------------

    pub fn set_send_hwm(&self, value: i32) -> Result<(), ConfigError> {
        set_int_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_SNDHWM, value)
    }

    pub fn send_hwm(&self) -> Result<i32, ConfigError> {
        get_int_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_SNDHWM)
    }

    pub fn set_recv_hwm(&self, value: i32) -> Result<(), ConfigError> {
        set_int_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_RCVHWM, value)
    }

    pub fn recv_hwm(&self) -> Result<i32, ConfigError> {
        get_int_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_RCVHWM)
    }

    pub fn set_linger(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_LINGER, d)
    }

    pub fn set_send_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_SNDTIMEO, d)
    }

    pub fn set_recv_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_RCVTIMEO, d)
    }

    pub fn set_reconnect_interval(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_RECONNECT_IVL, d)
    }

    pub fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_RECONNECT_IVL_MAX,
            d,
        )
    }

    pub fn set_send_buffer_size(&self, bytes: i32) -> Result<(), ConfigError> {
        set_int_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_SNDBUF, bytes)
    }

    pub fn set_recv_buffer_size(&self, bytes: i32) -> Result<(), ConfigError> {
        set_int_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_RCVBUF, bytes)
    }

    pub fn set_max_msg_size(&self, bytes: i64) -> Result<(), ConfigError> {
        let v = bytes;
        check_config_rc(unsafe {
            ffi::zlink_set_option(
                self.handle,
                ffi::zlink_option_t::ZLINK_OPT_MAXMSGSIZE,
                &v as *const i64 as *const c_void,
                std::mem::size_of::<i64>(),
            )
        })
    }

    pub fn set_backlog(&self, value: i32) -> Result<(), ConfigError> {
        set_int_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_BACKLOG, value)
    }

    pub fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ConfigError> {
        set_bool_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_TCP_KEEPALIVE,
            enabled,
        )
    }

    pub fn set_tcp_keepalive_idle(&self, seconds: i32) -> Result<(), ConfigError> {
        set_int_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_TCP_KEEPALIVE_IDLE,
            seconds,
        )
    }

    pub fn set_tcp_keepalive_interval(&self, seconds: i32) -> Result<(), ConfigError> {
        set_int_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_TCP_KEEPALIVE_INTVL,
            seconds,
        )
    }

    pub fn set_tcp_keepalive_count(&self, count: i32) -> Result<(), ConfigError> {
        set_int_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_TCP_KEEPALIVE_CNT,
            count,
        )
    }

    pub fn set_tcp_nodelay(&self, enabled: bool) -> Result<(), ConfigError> {
        set_bool_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_TCP_NODELAY,
            enabled,
        )
    }

    pub fn set_ipv6(&self, enabled: bool) -> Result<(), ConfigError> {
        set_bool_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_IPV6, enabled)
    }

    pub fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError> {
        set_bool_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_IMMEDIATE,
            enabled,
        )
    }

    pub fn set_conflate(&self, enabled: bool) -> Result<(), ConfigError> {
        set_bool_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_CONFLATE,
            enabled,
        )
    }

    pub fn set_connect_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_CONNECT_TIMEOUT,
            d,
        )
    }

    pub fn set_handshake_interval(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_HANDSHAKE_IVL, d)
    }

    pub fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_HEARTBEAT_IVL, d)
    }

    pub fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_HEARTBEAT_TTL, d)
    }

    pub fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_HEARTBEAT_TIMEOUT,
            d,
        )
    }

    pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_routing_id(self.handle, id.data().as_ptr() as *const c_void, id.len())
        })
    }

    pub fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        let mut raw = MaybeUninit::<ffi::zlink_routing_id_t>::uninit();
        check_config_rc(unsafe { ffi::zlink_get_routing_id(self.handle, raw.as_mut_ptr()) })?;
        Ok(RoutingId::from_raw(unsafe { raw.assume_init() }))
    }

    pub fn last_endpoint(&self) -> Result<String, ConfigError> {
        let mut buf = [0u8; 256];
        let mut len = buf.len();
        check_config_rc(unsafe {
            ffi::zlink_get_option(
                self.handle,
                ffi::zlink_option_t::ZLINK_OPT_LAST_ENDPOINT,
                buf.as_mut_ptr() as *mut c_void,
                &mut len,
            )
        })?;
        let s = unsafe { CStr::from_ptr(buf.as_ptr() as *const i8) };
        Ok(s.to_string_lossy().into_owned())
    }

    pub fn set_tls_server(
        &self,
        cert: &str,
        key: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        let c_cert = CString::new(cert).map_err(|_| config_validation_error())?;
        let c_key = CString::new(key).map_err(|_| config_validation_error())?;
        match check_config_rc(unsafe {
            ffi::zlink_set_tls_server(
                self.handle,
                c_cert.as_ptr(),
                c_key.as_ptr(),
                if require_client_cert { 1 } else { 0 },
            )
        }) {
            Ok(()) => Ok(()),
            Err(err) if err.code() == crate::error::ConfigResult::InvalidHandle => {
                self.set_tls_cert(cert)?;
                self.set_tls_key(key)?;
                set_bool_opt(
                    self.handle,
                    ffi::zlink_option_t::ZLINK_OPT_TLS_REQUIRE_CLIENT_CERT,
                    require_client_cert,
                )
            }
            Err(err) => Err(err),
        }
    }

    pub fn set_tls_cert(&self, cert: &str) -> Result<(), ConfigError> {
        set_string_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_TLS_CERT, cert)
    }

    pub fn set_tls_key(&self, key: &str) -> Result<(), ConfigError> {
        set_string_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_TLS_KEY, key)
    }

    pub fn set_tls_ca(&self, ca_cert: &str) -> Result<(), ConfigError> {
        set_string_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_TLS_CA, ca_cert)
    }

    pub fn set_tls_hostname(&self, hostname: &str) -> Result<(), ConfigError> {
        set_string_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_TLS_HOSTNAME,
            hostname,
        )
    }

    pub fn set_tls_trust_system(&self, trust_system: bool) -> Result<(), ConfigError> {
        set_bool_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_TLS_TRUST_SYSTEM,
            trust_system,
        )
    }

    pub fn set_tls_client(
        &self,
        ca_cert: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        let c_ca = CString::new(ca_cert).map_err(|_| config_validation_error())?;
        let c_host = CString::new(hostname).map_err(|_| config_validation_error())?;
        match check_config_rc(unsafe {
            ffi::zlink_set_tls_client(
                self.handle,
                c_ca.as_ptr(),
                c_host.as_ptr(),
                if trust_system { 1 } else { 0 },
            )
        }) {
            Ok(()) => Ok(()),
            Err(err) if err.code() == crate::error::ConfigResult::InvalidHandle => {
                self.set_tls_ca(ca_cert)?;
                self.set_tls_hostname(hostname)?;
                self.set_tls_trust_system(trust_system)
            }
            Err(err) => Err(err),
        }
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        if self.handle.is_null() {
            return Ok(());
        }
        check_close_rc(unsafe { ffi::zlink_close(self.handle) })?;
        self.handle = ptr::null_mut();
        self.recv_cb = None;
        self.sub_cb = None;
        self.send_ready_cb = None;
        Ok(())
    }
}

impl Drop for SocketInner {
    fn drop(&mut self) {
        // Close the socket first (blocks until in-flight callbacks complete),
        // then drop callback boxes.
        if !self.handle.is_null() {
            unsafe {
                ffi::zlink_close(self.handle);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Trampoline functions for native callbacks
// ---------------------------------------------------------------------------

unsafe extern "C" fn recv_trampoline<F: Fn(Received) + Send + 'static>(
    source_rid: *const ffi::zlink_routing_id_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let handler = unsafe { &*(userdata as *const F) };
    let rid = unsafe { *source_rid };
    let parts_vec = take_parts(parts, part_count);
    handler(Received::new(RoutingId::from_raw_optional(rid), parts_vec));
}

pub(crate) unsafe extern "C" fn subscribe_trampoline<F: Fn(TopicMessage) + Send + 'static>(
    source_rid: *const ffi::zlink_routing_id_t,
    topic: *const i8,
    topic_len: usize,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let handler = unsafe { &*(userdata as *const F) };
    let rid = unsafe { *source_rid };
    let topic_str = if topic.is_null() || topic_len == 0 {
        String::new()
    } else {
        let bytes = unsafe { std::slice::from_raw_parts(topic as *const u8, topic_len) };
        String::from_utf8_lossy(bytes).into_owned()
    };
    let parts_vec = take_parts(parts, part_count);
    handler(TopicMessage::new(
        RoutingId::from_raw_optional(rid),
        topic_str,
        parts_vec,
    ));
}

pub(crate) unsafe extern "C" fn send_ready_trampoline<F: Fn() + Send + 'static>(
    _subject: *mut c_void,
    userdata: *mut c_void,
) {
    let handler = unsafe { &*(userdata as *const F) };
    handler();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Prepare a contiguous native `zlink_msg_t` array for sending.
///
/// Uses `zlink_msg_move` to properly transfer each Message's content into a
/// freshly-initialized contiguous buffer. This avoids memcpy-based relocation
/// of initialized `zlink_msg_t` structs, which can invalidate internal state.
/// After this call, the source Messages are empty (moved-from) and the returned
/// Vec owns the content ready for `zlink_send`.
pub(crate) fn prepare_send_parts(
    parts: &mut Vec<Message>,
) -> Result<Vec<ffi::zlink_msg_t>, SubmitError> {
    let count = parts.len();
    let mut native: Vec<ffi::zlink_msg_t> = Vec::with_capacity(count);
    unsafe {
        for i in 0..count {
            let mut dest = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            ffi::zlink_msg_init(dest.as_mut_ptr());
            ffi::zlink_msg_move(dest.as_mut_ptr(), &mut parts[i].inner);
            native.push(dest.assume_init());
        }
    }
    Ok(native)
}

/// Take ownership of `part_count` messages from a native-owned array.
pub(crate) fn take_parts(parts_ptr: *mut ffi::zlink_msg_t, part_count: usize) -> Vec<Message> {
    let mut out = Vec::with_capacity(part_count);
    for i in 0..part_count {
        unsafe {
            // Move content from the library-owned array into our own zlink_msg_t.
            // ptr::read would create a bitwise copy sharing the same internal
            // storage, leading to double-free on drop. zlink_msg_move properly
            // transfers ownership and leaves the source as an empty message.
            let mut dest = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            ffi::zlink_msg_init(dest.as_mut_ptr());
            ffi::zlink_msg_move(dest.as_mut_ptr(), parts_ptr.add(i));
            out.push(Message::from_raw(dest.assume_init()));
        }
    }
    out
}

pub(crate) fn cstr_buf_to_string(buf: &[i8], len: usize) -> String {
    let bytes: &[u8] = unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) };
    String::from_utf8_lossy(bytes).into_owned()
}

/// Map the return code from a DONTWAIT send to `SendResult`.
///
/// When `zlink_send` / `zlink_send_rid` / `zlink_publish` is called with
/// `ZLINK_DONTWAIT`, the return value is the public `zlink_submit_result_t`
/// enum (0 = Sent, 1 = Backpressured, 2 = NotReady, other non-zero values are
/// submit failures).
pub(crate) fn check_send_result(rc: i32) -> Result<SendResult, SubmitError> {
    match rc {
        0 => Ok(SendResult::Sent),
        1 => Ok(SendResult::Backpressured),
        2 => Ok(SendResult::NotReady),
        _ => Err(SubmitError::new(
            crate::error::SubmitResult::InternalError,
            last_errno(),
        )),
    }
}

fn set_int_opt(
    handle: *mut c_void,
    opt: ffi::zlink_option_t,
    value: i32,
) -> Result<(), ConfigError> {
    check_config_rc(unsafe {
        ffi::zlink_set_option(
            handle,
            opt,
            &value as *const i32 as *const c_void,
            std::mem::size_of::<i32>(),
        )
    })
}

fn set_bool_opt(
    handle: *mut c_void,
    opt: ffi::zlink_option_t,
    enabled: bool,
) -> Result<(), ConfigError> {
    set_int_opt(handle, opt, if enabled { 1 } else { 0 })
}

fn set_duration_opt(
    handle: *mut c_void,
    opt: ffi::zlink_option_t,
    duration: Duration,
) -> Result<(), ConfigError> {
    set_int_opt(handle, opt, duration_to_millis(duration)?)
}

fn set_string_opt(
    handle: *mut c_void,
    opt: ffi::zlink_option_t,
    value: &str,
) -> Result<(), ConfigError> {
    let c_value = CString::new(value).map_err(|_| config_validation_error())?;
    check_config_rc(unsafe {
        ffi::zlink_set_option(
            handle,
            opt,
            c_value.as_ptr() as *const c_void,
            value.len() + 1,
        )
    })
}

fn get_int_opt(handle: *mut c_void, opt: ffi::zlink_option_t) -> Result<i32, ConfigError> {
    let mut value: i32 = 0;
    let mut len = std::mem::size_of::<i32>();
    check_config_rc(unsafe {
        ffi::zlink_get_option(handle, opt, &mut value as *mut i32 as *mut c_void, &mut len)
    })?;
    Ok(value)
}

// ---------------------------------------------------------------------------
// Capability-separated macros for socket delegations
// ---------------------------------------------------------------------------

/// Bind, unbind, last_endpoint, TLS, linger, and transport-level options.
/// Applied to all socket types.
macro_rules! impl_base_socket {
    ($ty:ident) => {
        #[allow(dead_code)]
        impl $ty {
            pub fn close(&mut self) -> Result<(), crate::error::CloseError> {
                self.inner.close()
            }
            pub fn bind(&self, addr: &str) -> Result<(), crate::error::BindError> {
                self.inner.bind(addr)
            }
            pub fn unbind(&self, addr: &str) -> Result<(), crate::error::ConnectError> {
                self.inner.unbind(addr)
            }
            pub fn last_endpoint(&self) -> Result<String, crate::error::ConfigError> {
                self.inner.last_endpoint()
            }
            pub(crate) fn set_linger(&self, d: Duration) -> Result<(), crate::error::ConfigError> {
                self.inner.set_linger(d)
            }
            pub(crate) fn set_max_msg_size(
                &self,
                bytes: i64,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_max_msg_size(bytes)
            }
            pub(crate) fn set_backlog(&self, value: i32) -> Result<(), crate::error::ConfigError> {
                self.inner.set_backlog(value)
            }
            pub(crate) fn set_tcp_keepalive(
                &self,
                enabled: bool,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tcp_keepalive(enabled)
            }
            pub(crate) fn set_tcp_nodelay(
                &self,
                enabled: bool,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tcp_nodelay(enabled)
            }
            pub(crate) fn set_ipv6(&self, enabled: bool) -> Result<(), crate::error::ConfigError> {
                self.inner.set_ipv6(enabled)
            }
            pub(crate) fn set_immediate(
                &self,
                enabled: bool,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_immediate(enabled)
            }
            pub(crate) fn set_reconnect_interval(
                &self,
                d: Duration,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_reconnect_interval(d)
            }
            pub(crate) fn set_reconnect_interval_max(
                &self,
                d: Duration,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_reconnect_interval_max(d)
            }
            pub(crate) fn set_connect_timeout(
                &self,
                d: Duration,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_connect_timeout(d)
            }
            pub(crate) fn set_heartbeat_interval(
                &self,
                d: Duration,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_heartbeat_interval(d)
            }
            pub(crate) fn set_heartbeat_ttl(
                &self,
                d: Duration,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_heartbeat_ttl(d)
            }
            pub(crate) fn set_heartbeat_timeout(
                &self,
                d: Duration,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_heartbeat_timeout(d)
            }
            pub fn set_tls_cert(&self, cert: &str) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_cert(cert)
            }
            pub fn set_tls_key(&self, key: &str) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_key(key)
            }
            pub fn set_tls_ca(&self, ca_cert: &str) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_ca(ca_cert)
            }
            pub fn set_tls_hostname(
                &self,
                hostname: &str,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_hostname(hostname)
            }
            pub fn set_tls_trust_system(
                &self,
                trust_system: bool,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_trust_system(trust_system)
            }
            pub fn set_tls_server(
                &self,
                cert: &str,
                key: &str,
                require_client_cert: bool,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_server(cert, key, require_client_cert)
            }
            pub fn set_tls_client(
                &self,
                ca_cert: &str,
                hostname: &str,
                trust_system: bool,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_client(ca_cert, hostname, trust_system)
            }
            pub(crate) fn handle(&self) -> *mut c_void {
                self.inner.handle
            }
        }
    };
}

/// Routing-id get/set – only for DEALER and ROUTER sockets.
macro_rules! impl_routing_id_options {
    ($ty:ident) => {
        #[allow(dead_code)]
        impl $ty {
            pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), crate::error::ConfigError> {
                self.inner.set_routing_id(id)
            }
            pub fn routing_id(&self) -> Result<RoutingId, crate::error::ConfigError> {
                self.inner.routing_id()
            }
        }
    };
}

/// Connect and disconnect – for non-STREAM sockets.
macro_rules! impl_connect {
    ($ty:ident) => {
        #[allow(dead_code)]
        impl $ty {
            pub fn connect(&self, addr: &str) -> Result<(), crate::error::ConnectError> {
                self.inner.connect(addr)
            }
            pub fn disconnect(&self, addr: &str) -> Result<(), crate::error::ConnectError> {
                self.inner.disconnect(addr)
            }
        }
    };
}

/// Attach a socket to a discovery-owned lifecycle.
macro_rules! impl_attach_discovery {
    ($ty:ident) => {
        #[allow(dead_code)]
        impl $ty {
            pub fn attach_discovery(
                &self,
                discovery: &crate::service::Discovery,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.attach_discovery(discovery)
            }
        }
    };
}

/// Send-side options – for sockets that can send.
macro_rules! impl_send_options {
    ($ty:ident) => {
        #[allow(dead_code)]
        impl $ty {
            pub(crate) fn set_send_hwm(&self, value: i32) -> Result<(), crate::error::ConfigError> {
                self.inner.set_send_hwm(value)
            }
            pub(crate) fn send_hwm(&self) -> Result<i32, crate::error::ConfigError> {
                self.inner.send_hwm()
            }
            pub(crate) fn set_send_timeout(
                &self,
                d: Duration,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_send_timeout(d)
            }
            pub(crate) fn set_send_buffer_size(
                &self,
                bytes: i32,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_send_buffer_size(bytes)
            }
        }
    };
}

/// Receive-side options – for sockets that can receive.
macro_rules! impl_recv_options {
    ($ty:ident) => {
        #[allow(dead_code)]
        impl $ty {
            pub(crate) fn set_recv_hwm(&self, value: i32) -> Result<(), crate::error::ConfigError> {
                self.inner.set_recv_hwm(value)
            }
            pub(crate) fn recv_hwm(&self) -> Result<i32, crate::error::ConfigError> {
                self.inner.recv_hwm()
            }
            pub(crate) fn set_recv_timeout(
                &self,
                d: Duration,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_recv_timeout(d)
            }
            pub(crate) fn set_recv_buffer_size(
                &self,
                bytes: i32,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_recv_buffer_size(bytes)
            }
        }
    };
}

pub(crate) use impl_attach_discovery;
pub(crate) use impl_base_socket;
pub(crate) use impl_connect;
pub(crate) use impl_recv_options;
pub(crate) use impl_routing_id_options;
pub(crate) use impl_send_options;
