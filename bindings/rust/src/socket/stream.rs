use std::ffi::c_void;
use std::mem::MaybeUninit;
use std::time::Duration;

use crate::ctx::Context;
use crate::domain::Received;
use crate::error::{
    ConfigError, HandlerError, RecvError, SubmitError, check_config_rc, check_handler_rc,
};
use crate::ffi;
use crate::flags::{RecvFlags, SendFlags};
use crate::message::{IntoMultipart, Message, RoutingId};
use crate::options::{CommonSocketOptions, StreamSocketOptions};

use super::{
    SendHandle, SocketInner, impl_base_socket, impl_recv_options, impl_routing_id_options,
    impl_send_options,
};

/// STREAM socket – raw TCP/transport-level messaging with routing-id.
///
/// All sends are routed to a specific peer. STREAM supports direct recv and
/// packet-handler callback mode.
///
/// Capabilities: `send` (routed), `recv`, `on_packet`, `on_send_ready`.
/// No general `connect` / `disconnect` – peers connect inward.
pub struct StreamSocket {
    pub(crate) inner: SocketInner,
}

impl StreamSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_STREAM)?,
        })
    }

    pub fn send(&self, target: &RoutingId, parts: impl IntoMultipart) -> Result<(), SubmitError> {
        self.inner.send_to(target, parts)
    }

    pub fn send_with_flags(
        &self,
        target: &RoutingId,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        self.inner.send_to_with_flags(target, parts, flags)
    }

    pub fn recv(&self) -> Result<Received, RecvError> {
        self.inner.recv()
    }

    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError> {
        self.inner.recv_with_flags(flags)
    }

    pub fn on_packet<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn(RoutingId, Message, Message) + Send + 'static,
    {
        let (cb, userdata) = super::CallbackBox::new(handler);
        let rc = unsafe {
            ffi::zlink_stream_packet_handler(
                self.inner.handle,
                stream_packet_trampoline::<F>,
                userdata,
            )
        };
        if rc != 0 {
            drop(cb);
            return check_handler_rc(rc);
        }
        self.inner.packet_cb = Some(cb);
        Ok(())
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        self.inner.on_send_ready(handler)
    }

    /// Obtain a lightweight, cloneable handle for sending from callbacks or
    /// other threads. The returned handle does not own the socket; the
    /// `StreamSocket` must remain alive while the handle is in use.
    pub fn send_handle(&self) -> SendHandle {
        SendHandle::new(self.inner.handle)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_, Self> {
        CommonSocketOptions::new(self)
    }

    pub fn stream_options(&self) -> StreamSocketOptions<'_> {
        StreamSocketOptions::new(self)
    }

    // -- STREAM-specific typed options -------------------------------------

    pub(crate) fn set_notify(&self, enabled: bool) -> Result<(), ConfigError> {
        set_stream_bool_option(
            self.inner.handle,
            ffi::zlink_stream_option_t::ZLINK_STREAM_OPT_NOTIFY,
            enabled,
        )
    }

    pub(crate) fn notify(&self) -> Result<bool, ConfigError> {
        get_stream_bool_option(
            self.inner.handle,
            ffi::zlink_stream_option_t::ZLINK_STREAM_OPT_NOTIFY,
        )
    }
}

fn set_stream_bool_option(
    handle: *mut c_void,
    option: ffi::zlink_stream_option_t,
    enabled: bool,
) -> Result<(), ConfigError> {
    let value: i32 = if enabled { 1 } else { 0 };
    check_config_rc(unsafe {
        ffi::zlink_set_stream_option(
            handle,
            option,
            &value as *const i32 as *const c_void,
            std::mem::size_of::<i32>(),
        )
    })
}

fn get_stream_bool_option(
    handle: *mut c_void,
    option: ffi::zlink_stream_option_t,
) -> Result<bool, ConfigError> {
    let mut value: i32 = 0;
    let mut len = std::mem::size_of::<i32>();
    check_config_rc(unsafe {
        ffi::zlink_get_stream_option(
            handle,
            option,
            &mut value as *mut i32 as *mut c_void,
            &mut len,
        )
    })?;
    Ok(value != 0)
}

impl_base_socket!(StreamSocket);
// No impl_connect – STREAM socket does not use general connect
impl_send_options!(StreamSocket);
impl_recv_options!(StreamSocket);
impl_routing_id_options!(StreamSocket);

fn take_message(raw: *mut ffi::zlink_msg_t) -> Message {
    unsafe {
        let mut dest = MaybeUninit::<ffi::zlink_msg_t>::uninit();
        ffi::zlink_msg_init(dest.as_mut_ptr());
        ffi::zlink_msg_move(dest.as_mut_ptr(), raw);
        Message::from_raw(dest.assume_init())
    }
}

unsafe extern "C" fn stream_packet_trampoline<F: Fn(RoutingId, Message, Message) + Send + 'static>(
    _stream: *mut c_void,
    source_rid: *const ffi::zlink_routing_id_t,
    header: *mut ffi::zlink_msg_t,
    body: *mut ffi::zlink_msg_t,
    userdata: *mut c_void,
) {
    let handler = unsafe { &*(userdata as *const F) };
    let header = take_message(header);
    let body = take_message(body);
    if source_rid.is_null() {
        return;
    }
    let routing_id = unsafe { RoutingId::from_raw(*source_rid) };
    handler(routing_id, header, body);
}
