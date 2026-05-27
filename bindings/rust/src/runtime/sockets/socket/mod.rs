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
use crate::domain::{Received, SubscriptionEvent, TopicMessage};
use crate::error::{
    BindError, CloseError, ConfigError, ConnectError, HandlerError, RecvError, RecvResult,
    SubmitError, check_bind_rc, check_close_rc, check_config_rc, check_connect_rc,
    check_handler_rc, check_recv_rc, config_validation_error, last_errno, submit_validation_error,
};
use crate::ffi;
use crate::flags::RecvFlags;
use crate::message::{Message, RoutingId};
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
    send_ready_cb: Option<CallbackBox>,
    packet_cb: Option<CallbackBox>,
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
            send_ready_cb: None,
            packet_cb: None,
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

    pub fn disconnect_rid(&self, peer_rid: &RoutingId) -> Result<(), ConnectError> {
        check_connect_rc(unsafe { ffi::zlink_disconnect_rid(self.handle, peer_rid.as_raw()) })
    }

    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_socket_attach_discovery(self.handle, discovery.raw()) })
    }

    pub fn set_channel_name(&self, channel_name: &str) -> Result<(), ConfigError> {
        let c = CString::new(channel_name).map_err(|_| config_validation_error())?;
        check_config_rc(unsafe { ffi::zlink_socket_set_channel_name(self.handle, c.as_ptr()) })
    }

    pub fn channel_name(&self) -> Result<String, ConfigError> {
        let mut buf = [0i8; 256];
        let mut len = 0usize;
        check_config_rc(unsafe {
            ffi::zlink_socket_get_channel_name(self.handle, buf.as_mut_ptr(), buf.len(), &mut len)
        })?;
        Ok(cstr_buf_to_string(&buf, len))
    }

    // -- Recv (direct) -----------------------------------------------------

    /// Canonical caller-provided storage recv. Pass a long-lived
    /// [`Received`] and the binding refills its internal state in place
    /// each successful call.
    ///
    /// Returns `Ok(true)` on success, `Ok(false)` when [`RecvFlags::DONTWAIT`]
    /// finds no data, `Err(_)` on hard error. See
    /// `doc/spec/bindings/README.md` "Canonical Recv: Caller-Provided Storage".
    pub fn recv(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        match recv_basic_parts(self.handle, flags.bits())? {
            Some((routing_id, parts)) => {
                out.adopt_from(Received::new(routing_id, parts));
                Ok(true)
            }
            None => Ok(false),
        }
    }

    pub(crate) fn recv_no_wait(&self) -> Result<Option<Received>, RecvError> {
        recv_basic_parts(self.handle, ffi::ZLINK_DONTWAIT)
            .map(|opt| opt.map(|(routing_id, parts)| Received::new(routing_id, parts)))
    }

    // -- Subscribe (blocking recv) -----------------------------------------

    pub fn subscribe_recv(
        &self,
        out: &mut TopicMessage,
        flags: RecvFlags,
    ) -> Result<bool, RecvError> {
        let mut topic_buf = [0i8; 256];
        match recv_subscribed_parts(self.handle, &mut topic_buf, flags.bits())? {
            Some((routing_id, topic, parts)) => {
                out.adopt_from(TopicMessage::new(routing_id, topic, parts));
                Ok(true)
            }
            None => Ok(false),
        }
    }

    pub(crate) fn subscribe_recv_no_wait(&self) -> Result<Option<TopicMessage>, RecvError> {
        let mut topic_buf = [0i8; 256];
        recv_subscribed_parts(self.handle, &mut topic_buf, ffi::ZLINK_DONTWAIT).map(|opt| {
            opt.map(|(routing_id, topic, parts)| TopicMessage::new(routing_id, topic, parts))
        })
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

    pub fn receive_subscription_event(
        &self,
        out: &mut SubscriptionEvent,
        flags: RecvFlags,
    ) -> Result<bool, RecvError> {
        let mut subscribed: i32 = 0;
        let mut topic_buf = [0i8; 256];
        let mut topic_len: usize = 256;
        let mut source_rid_ptr = ptr::null();

        let rc = unsafe {
            ffi::zlink_xpub_recv_part(
                self.handle,
                &mut source_rid_ptr,
                &mut subscribed,
                topic_buf.as_mut_ptr(),
                topic_buf.len(),
                &mut topic_len,
                flags.bits(),
            )
        };
        if rc == RecvResult::NoData as i32 {
            return Ok(false);
        }
        if rc != 0 {
            let errno = unsafe { ffi::zlink_errno() };
            if errno == libc::EAGAIN {
                return Ok(false);
            }
            return Err(RecvError::new(crate::error::RecvResult::Terminated, errno));
        }

        let topic = cstr_buf_to_smolstr(&topic_buf, topic_len);
        out.adopt_from(SubscriptionEvent::new(
            routing_id_from_ptr(source_rid_ptr),
            subscribed != 0,
            topic,
        ));
        Ok(true)
    }

    pub(crate) fn try_receive_subscription_event(
        &self,
    ) -> Result<Option<SubscriptionEvent>, RecvError> {
        let mut event = SubscriptionEvent::empty();
        Ok(self
            .receive_subscription_event(&mut event, RecvFlags::DONT_WAIT)?
            .then_some(event))
    }

    // -- Callback installation ---------------------------------------------

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

    pub fn linger(&self) -> Result<Duration, ConfigError> {
        get_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_LINGER)
    }

    pub fn set_send_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_SNDTIMEO, d)
    }

    pub fn send_timeout(&self) -> Result<Duration, ConfigError> {
        get_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_SNDTIMEO)
    }

    pub fn set_recv_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_RCVTIMEO, d)
    }

    pub fn recv_timeout(&self) -> Result<Duration, ConfigError> {
        get_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_RCVTIMEO)
    }

    pub fn set_reconnect_interval(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_RECONNECT_IVL, d)
    }

    pub fn reconnect_interval(&self) -> Result<Duration, ConfigError> {
        get_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_RECONNECT_IVL)
    }

    pub fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_RECONNECT_IVL_MAX,
            d,
        )
    }

    pub fn reconnect_interval_max(&self) -> Result<Duration, ConfigError> {
        get_duration_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_RECONNECT_IVL_MAX,
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

    pub fn max_msg_size(&self) -> Result<i64, ConfigError> {
        get_i64_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_MAXMSGSIZE)
    }

    pub fn set_backlog(&self, value: i32) -> Result<(), ConfigError> {
        set_int_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_BACKLOG, value)
    }

    pub fn backlog(&self) -> Result<i32, ConfigError> {
        get_int_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_BACKLOG)
    }

    pub fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ConfigError> {
        set_bool_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_TCP_KEEPALIVE,
            enabled,
        )
    }

    pub fn tcp_keepalive(&self) -> Result<bool, ConfigError> {
        get_bool_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_TCP_KEEPALIVE)
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

    pub fn tcp_nodelay(&self) -> Result<bool, ConfigError> {
        get_bool_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_TCP_NODELAY)
    }

    pub fn set_ipv6(&self, enabled: bool) -> Result<(), ConfigError> {
        set_bool_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_IPV6, enabled)
    }

    pub fn ipv6(&self) -> Result<bool, ConfigError> {
        get_bool_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_IPV6)
    }

    pub fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError> {
        set_bool_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_IMMEDIATE,
            enabled,
        )
    }

    pub fn immediate(&self) -> Result<bool, ConfigError> {
        get_bool_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_IMMEDIATE)
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

    pub fn connect_timeout(&self) -> Result<Duration, ConfigError> {
        get_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_CONNECT_TIMEOUT)
    }

    pub fn set_rid_duplicate_policy(&self, value: i32) -> Result<(), ConfigError> {
        set_int_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_RID_DUPLICATE_POLICY,
            value,
        )
    }

    pub fn rid_duplicate_policy(&self) -> Result<i32, ConfigError> {
        get_int_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_RID_DUPLICATE_POLICY,
        )
    }

    pub fn set_handshake_interval(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_HANDSHAKE_IVL, d)
    }

    pub fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_HEARTBEAT_IVL, d)
    }

    pub fn heartbeat_interval(&self) -> Result<Duration, ConfigError> {
        get_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_HEARTBEAT_IVL)
    }

    pub fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_HEARTBEAT_TTL, d)
    }

    pub fn heartbeat_ttl(&self) -> Result<Duration, ConfigError> {
        get_duration_opt(self.handle, ffi::zlink_option_t::ZLINK_OPT_HEARTBEAT_TTL)
    }

    pub fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        set_duration_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_HEARTBEAT_TIMEOUT,
            d,
        )
    }

    pub fn heartbeat_timeout(&self) -> Result<Duration, ConfigError> {
        get_duration_opt(
            self.handle,
            ffi::zlink_option_t::ZLINK_OPT_HEARTBEAT_TIMEOUT,
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
        self.send_ready_cb = None;
        self.packet_cb = None;
        Ok(())
    }

    // -- PUB-option helpers ------------------------------------------------
    //
    // These were previously duplicated as free functions in pub_socket.rs and
    // xpub.rs plus pub(crate) methods on both PubSocket and XPubSocket. Hosting
    // them on SocketInner lets PubSocketOptions hold a `&SocketInner` directly
    // and call them without the trait + impl trampoline chain.

    pub(crate) fn set_pub_bool_opt(
        &self,
        opt: ffi::zlink_pub_option_t,
        value: bool,
    ) -> Result<(), ConfigError> {
        let v: i32 = if value { 1 } else { 0 };
        check_config_rc(unsafe {
            ffi::zlink_set_pub_option(
                self.handle,
                opt,
                &v as *const i32 as *const c_void,
                std::mem::size_of::<i32>(),
            )
        })
    }

    pub(crate) fn get_pub_int_opt(&self, opt: ffi::zlink_pub_option_t) -> Result<i32, ConfigError> {
        let mut value: i32 = 0;
        let mut len = std::mem::size_of::<i32>();
        check_config_rc(unsafe {
            ffi::zlink_get_pub_option(
                self.handle,
                opt,
                &mut value as *mut i32 as *mut c_void,
                &mut len,
            )
        })?;
        Ok(value)
    }

    pub(crate) fn get_pub_bool_opt(
        &self,
        opt: ffi::zlink_pub_option_t,
    ) -> Result<bool, ConfigError> {
        Ok(self.get_pub_int_opt(opt)? != 0)
    }

    pub(crate) fn set_pub_bytes_opt(
        &self,
        opt: ffi::zlink_pub_option_t,
        value: &[u8],
    ) -> Result<(), ConfigError> {
        let ptr = if value.is_empty() {
            std::ptr::null()
        } else {
            value.as_ptr() as *const c_void
        };
        check_config_rc(unsafe { ffi::zlink_set_pub_option(self.handle, opt, ptr, value.len()) })
    }

    pub(crate) fn get_pub_message_opt(
        &self,
        opt: ffi::zlink_pub_option_t,
    ) -> Result<crate::message::Message, ConfigError> {
        // Two-step query: first probe the length with a null buffer, then
        // allocate exactly what's needed. Previously this always allocated a
        // 64 KiB scratch buffer up front; for the welcome-message case the
        // payload is typically a few hundred bytes at most.
        let mut len: usize = 0;
        check_config_rc(unsafe {
            ffi::zlink_get_pub_option(self.handle, opt, std::ptr::null_mut(), &mut len)
        })?;
        if len == 0 {
            return crate::message::Message::try_from(&[]);
        }
        let mut buf = vec![0u8; len];
        let mut filled = len;
        check_config_rc(unsafe {
            ffi::zlink_get_pub_option(
                self.handle,
                opt,
                buf.as_mut_ptr() as *mut c_void,
                &mut filled,
            )
        })?;
        crate::message::Message::try_from(&buf[..filled])
    }

    // -- SUB-option helper -------------------------------------------------

    pub(crate) fn get_sub_int_opt(&self, opt: ffi::zlink_sub_option_t) -> Result<i32, ConfigError> {
        let mut value: i32 = 0;
        let mut len = std::mem::size_of::<i32>();
        check_config_rc(unsafe {
            ffi::zlink_get_sub_option(
                self.handle,
                opt,
                &mut value as *mut i32 as *mut c_void,
                &mut len,
            )
        })?;
        Ok(value)
    }

    // -- ROUTER-option helpers --------------------------------------------
    //
    // Same shape as the PUB/SUB helpers above: RouterSocketOptions holds a
    // `&SocketInner` and dispatches here directly, eliminating the previous
    // wrapper -> trait -> per-type forward indirection.

    pub(crate) fn set_router_bool_opt(
        &self,
        opt: ffi::zlink_router_option_t,
        value: bool,
    ) -> Result<(), ConfigError> {
        let v: i32 = if value { 1 } else { 0 };
        check_config_rc(unsafe {
            ffi::zlink_set_router_option(
                self.handle,
                opt,
                &v as *const i32 as *const c_void,
                std::mem::size_of::<i32>(),
            )
        })
    }

    pub(crate) fn set_router_i32_opt(
        &self,
        opt: ffi::zlink_router_option_t,
        value: i32,
    ) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_router_option(
                self.handle,
                opt,
                &value as *const i32 as *const c_void,
                std::mem::size_of::<i32>(),
            )
        })
    }

    pub(crate) fn set_router_u32_opt(
        &self,
        opt: ffi::zlink_router_option_t,
        value: u32,
    ) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_router_option(
                self.handle,
                opt,
                &value as *const u32 as *const c_void,
                std::mem::size_of::<u32>(),
            )
        })
    }

    pub(crate) fn set_router_bytes_opt(
        &self,
        opt: ffi::zlink_router_option_t,
        value: &[u8],
    ) -> Result<(), ConfigError> {
        let ptr = if value.is_empty() {
            std::ptr::null()
        } else {
            value.as_ptr() as *const c_void
        };
        check_config_rc(unsafe { ffi::zlink_set_router_option(self.handle, opt, ptr, value.len()) })
    }

    pub(crate) fn get_router_i32_opt(
        &self,
        opt: ffi::zlink_router_option_t,
    ) -> Result<i32, ConfigError> {
        let mut value: i32 = 0;
        let mut len = std::mem::size_of::<i32>();
        check_config_rc(unsafe {
            ffi::zlink_get_router_option(
                self.handle,
                opt,
                &mut value as *mut i32 as *mut c_void,
                &mut len,
            )
        })?;
        Ok(value)
    }

    pub(crate) fn get_router_u32_opt(
        &self,
        opt: ffi::zlink_router_option_t,
    ) -> Result<u32, ConfigError> {
        let mut value: u32 = 0;
        let mut len = std::mem::size_of::<u32>();
        check_config_rc(unsafe {
            ffi::zlink_get_router_option(
                self.handle,
                opt,
                &mut value as *mut u32 as *mut c_void,
                &mut len,
            )
        })?;
        Ok(value)
    }

    // -- DEALER-option helpers --------------------------------------------

    pub(crate) fn set_dealer_bool_opt(
        &self,
        opt: ffi::zlink_dealer_option_t,
        value: bool,
    ) -> Result<(), ConfigError> {
        let v: i32 = if value { 1 } else { 0 };
        check_config_rc(unsafe {
            ffi::zlink_set_dealer_option(
                self.handle,
                opt,
                &v as *const i32 as *const c_void,
                std::mem::size_of::<i32>(),
            )
        })
    }

    pub(crate) fn set_dealer_u32_opt(
        &self,
        opt: ffi::zlink_dealer_option_t,
        value: u32,
    ) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_dealer_option(
                self.handle,
                opt,
                &value as *const u32 as *const c_void,
                std::mem::size_of::<u32>(),
            )
        })
    }

    pub(crate) fn set_dealer_i32_opt(
        &self,
        opt: ffi::zlink_dealer_option_t,
        value: i32,
    ) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_dealer_option(
                self.handle,
                opt,
                &value as *const i32 as *const c_void,
                std::mem::size_of::<i32>(),
            )
        })
    }

    // -- STREAM-option helpers --------------------------------------------

    pub(crate) fn set_stream_bool_opt(
        &self,
        opt: ffi::zlink_stream_option_t,
        value: bool,
    ) -> Result<(), ConfigError> {
        let v: i32 = if value { 1 } else { 0 };
        check_config_rc(unsafe {
            ffi::zlink_set_stream_option(
                self.handle,
                opt,
                &v as *const i32 as *const c_void,
                std::mem::size_of::<i32>(),
            )
        })
    }

    pub(crate) fn get_stream_bool_opt(
        &self,
        opt: ffi::zlink_stream_option_t,
    ) -> Result<bool, ConfigError> {
        let mut value: i32 = 0;
        let mut len = std::mem::size_of::<i32>();
        check_config_rc(unsafe {
            ffi::zlink_get_stream_option(
                self.handle,
                opt,
                &mut value as *mut i32 as *mut c_void,
                &mut len,
            )
        })?;
        Ok(value != 0)
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
    parts: &mut [Message],
) -> Result<Vec<ffi::zlink_msg_t>, SubmitError> {
    let mut native: Vec<ffi::zlink_msg_t> = Vec::with_capacity(parts.len());
    unsafe {
        for part in parts.iter_mut() {
            let mut dest = MaybeUninit::<ffi::zlink_msg_t>::uninit();
            ffi::zlink_msg_init(dest.as_mut_ptr());
            ffi::zlink_msg_move(dest.as_mut_ptr(), &mut part.inner);
            native.push(dest.assume_init());
        }
    }
    Ok(native)
}

pub(crate) fn submit_part_sequence(
    native: &mut [ffi::zlink_msg_t],
    mut submit: impl FnMut(*mut ffi::zlink_msg_t, ffi::zlink_part_flag_t, bool) -> i32,
) -> Result<i32, SubmitError> {
    if native.is_empty() {
        return Err(submit_validation_error());
    }

    for index in 0..native.len() {
        let is_final = index + 1 == native.len();
        let part_flag = if is_final {
            ffi::zlink_part_flag_t::ZLINK_PART_FINAL
        } else {
            ffi::zlink_part_flag_t::ZLINK_PART_MORE
        };
        let rc = submit(native.as_mut_ptr().wrapping_add(index), part_flag, is_final);
        if rc != 0 {
            close_native_parts_from(native, index + 1);
            return Ok(rc);
        }
    }

    Ok(0)
}

fn close_native_parts_from(parts: &mut [ffi::zlink_msg_t], start_index: usize) {
    for part in &mut parts[start_index..] {
        unsafe {
            ffi::zlink_msg_close(part);
        }
    }
}

pub(crate) fn close_unreceived_part(part: &mut MaybeUninit<ffi::zlink_msg_t>) {
    unsafe {
        ffi::zlink_msg_close(part.as_mut_ptr());
    }
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
    match std::str::from_utf8(bytes) {
        Ok(s) => s.to_owned(),
        Err(_) => String::from_utf8_lossy(bytes).into_owned(),
    }
}

// Same shape as cstr_buf_to_string but produces a SmolStr: short subscribe
// topics now bypass heap allocation entirely (<=22 bytes live inline).
pub(crate) fn cstr_buf_to_smolstr(buf: &[i8], len: usize) -> smol_str::SmolStr {
    let bytes: &[u8] = unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) };
    match std::str::from_utf8(bytes) {
        Ok(s) => smol_str::SmolStr::new(s),
        Err(_) => smol_str::SmolStr::new(String::from_utf8_lossy(bytes)),
    }
}

pub(crate) fn routing_id_from_ptr(raw: *const ffi::zlink_routing_id_t) -> Option<RoutingId> {
    if raw.is_null() {
        None
    } else {
        RoutingId::from_raw_optional(unsafe { *raw })
    }
}

type RecvBasicParts = Result<Option<(Option<RoutingId>, Vec<Message>)>, RecvError>;
type RecvSubscribedParts =
    Result<Option<(Option<RoutingId>, smol_str::SmolStr, Vec<Message>)>, RecvError>;

pub(crate) fn recv_basic_parts(
    handle: *mut c_void,
    flags: ffi::zlink_recv_flags_t,
) -> RecvBasicParts {
    let mut routing_id = None;
    let mut parts = Vec::new();
    let mut recv_flags = flags;

    loop {
        let mut source_rid_ptr = ptr::null();
        let mut part = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(part.as_mut_ptr());
        }
        let mut has_more = 0;
        let rc = unsafe {
            ffi::zlink_recv_part(
                handle,
                &mut source_rid_ptr,
                part.as_mut_ptr(),
                &mut has_more,
                recv_flags,
            )
        };

        if parts.is_empty() {
            if rc == RecvResult::NoData as i32 {
                close_unreceived_part(&mut part);
                return Ok(None);
            }
            if rc != 0 {
                close_unreceived_part(&mut part);
                let errno = unsafe { ffi::zlink_errno() };
                if errno == libc::EAGAIN {
                    return Ok(None);
                }
                return Err(check_recv_rc(rc).unwrap_err());
            }
            routing_id = routing_id_from_ptr(source_rid_ptr);
        } else if rc != 0 {
            close_unreceived_part(&mut part);
            return Err(check_recv_rc(rc).unwrap_err());
        }

        parts.push(unsafe { Message::from_raw(part.assume_init()) });
        if has_more == 0 {
            return Ok(Some((routing_id, parts)));
        }
        recv_flags = ffi::ZLINK_DONTWAIT;
    }
}

pub(crate) fn recv_subscribed_parts(
    handle: *mut c_void,
    topic_buf: &mut [i8; 256],
    flags: ffi::zlink_recv_flags_t,
) -> RecvSubscribedParts {
    let mut routing_id = None;
    let mut topic = smol_str::SmolStr::default();
    let mut parts = Vec::new();
    let mut recv_flags = flags;

    loop {
        let mut source_rid_ptr = ptr::null();
        let mut topic_len = topic_buf.len();
        let mut part = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(part.as_mut_ptr());
        }
        let mut has_more = 0;
        let rc = unsafe {
            ffi::zlink_subscribe_part(
                handle,
                &mut source_rid_ptr,
                topic_buf.as_mut_ptr(),
                topic_buf.len(),
                &mut topic_len,
                part.as_mut_ptr(),
                &mut has_more,
                recv_flags,
            )
        };

        if parts.is_empty() {
            if rc == RecvResult::NoData as i32 {
                close_unreceived_part(&mut part);
                return Ok(None);
            }
            if rc != 0 {
                close_unreceived_part(&mut part);
                let errno = unsafe { ffi::zlink_errno() };
                if errno == libc::EAGAIN {
                    return Ok(None);
                }
                return Err(check_recv_rc(rc).unwrap_err());
            }
            routing_id = routing_id_from_ptr(source_rid_ptr);
            topic = cstr_buf_to_smolstr(topic_buf, topic_len);
        } else if rc != 0 {
            close_unreceived_part(&mut part);
            return Err(check_recv_rc(rc).unwrap_err());
        }

        parts.push(unsafe { Message::from_raw(part.assume_init()) });
        if has_more == 0 {
            return Ok(Some((routing_id, topic, parts)));
        }
        recv_flags = ffi::ZLINK_DONTWAIT;
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

fn get_duration_opt(
    handle: *mut c_void,
    opt: ffi::zlink_option_t,
) -> Result<Duration, ConfigError> {
    let millis = get_int_opt(handle, opt)?;
    Ok(Duration::from_millis(millis as u64))
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

fn get_bool_opt(handle: *mut c_void, opt: ffi::zlink_option_t) -> Result<bool, ConfigError> {
    Ok(get_int_opt(handle, opt)? != 0)
}

fn get_i64_opt(handle: *mut c_void, opt: ffi::zlink_option_t) -> Result<i64, ConfigError> {
    let mut value: i64 = 0;
    let mut len = std::mem::size_of::<i64>();
    check_config_rc(unsafe {
        ffi::zlink_get_option(handle, opt, &mut value as *mut i64 as *mut c_void, &mut len)
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
            pub(crate) fn handle(&self) -> *mut std::ffi::c_void {
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
            pub fn disconnect_rid(
                &self,
                peer_rid: &crate::message::RoutingId,
            ) -> Result<(), crate::error::ConnectError> {
                self.inner.disconnect_rid(peer_rid)
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
pub(crate) use impl_attach_discovery;
pub(crate) use impl_base_socket;
pub(crate) use impl_connect;
pub(crate) use impl_routing_id_options;
