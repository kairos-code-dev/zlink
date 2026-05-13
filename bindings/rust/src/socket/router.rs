use std::ffi::c_void;
use std::ptr;
use std::time::Duration;

use super::{
    SendHandle, SocketInner, impl_attach_discovery, impl_base_socket, impl_connect,
    impl_recv_options, impl_routing_id_options, impl_send_options,
};
use crate::ctx::Context;
use crate::domain::Received;
use crate::error::{
    ConfigError, HandlerError, RecvError, check_config_rc, check_recv_rc,
};
use crate::ffi;
use crate::flags::RecvFlags;
use crate::message::{Message, RoutingId};
use crate::options::{CommonSocketOptions, RouterSocketOptions};
use crate::service::{Empty, ReplyOp, RequestOp, SendOp};

/// ROUTER socket – asynchronous request/reply pattern (server side).
///
/// All sends are routed: `send(target, parts)` addresses a specific peer.
/// Capabilities: `send` (routed), `recv`, `on_send_ready`.
pub struct RouterSocket {
    pub(crate) inner: SocketInner,
}

impl RouterSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_ROUTER)?,
        })
    }

    pub fn send(&self, target: &RoutingId) -> SendOp<Empty> {
        crate::service::socket_send_to_op(self.inner.handle, target.clone())
    }

    /// Canonical caller-provided storage routed recv. See
    /// `doc/spec/bindings/README.md`.
    pub fn recv(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        if flags.bits() == 0 {
            if let Some(received) =
                recv_router_once(self.inner.handle, ffi::ZLINK_DONTWAIT)?
            {
                out.adopt_from(received);
                return Ok(true);
            }
        }
        match recv_router_once(self.inner.handle, flags.bits())? {
            Some(received) => {
                out.adopt_from(received);
                Ok(true)
            }
            None => Ok(false),
        }
    }

    pub fn request(&self, peer_rid: &RoutingId) -> RequestOp<Empty> {
        crate::service::router_request_op(self.inner.handle, peer_rid.clone())
    }

    pub fn reply(&self, rid: &RoutingId, request_seq: u64) -> ReplyOp<Empty> {
        crate::service::router_reply_op(self.inner.handle, rid.clone(), request_seq)
    }

    pub fn send_to_spot(
        &self,
        dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId,
    ) -> SendOp<Empty> {
        crate::service::router_send_to_spot_op(
            self.inner.handle,
            dest_node_rid.clone(),
            dest_spot_rid.clone(),
        )
    }

    pub fn request_to_spot(
        &self,
        dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId,
    ) -> RequestOp<Empty> {
        crate::service::router_request_to_spot_op(
            self.inner.handle,
            dest_node_rid.clone(),
            dest_spot_rid.clone(),
        )
    }

    pub fn reply_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
    ) -> ReplyOp<Empty> {
        crate::service::router_reply_to_spot_op(
            self.inner.handle,
            dest_node_rid,
            dest_spot_rid,
            request_seq,
        )
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        self.inner.on_send_ready(handler)
    }

    /// Obtain a lightweight, cloneable handle for sending from callbacks or
    /// other threads. The returned handle does not own the socket; the
    /// `RouterSocket` must remain alive while the handle is in use.
    pub fn send_handle(&self) -> SendHandle {
        SendHandle::new(self.inner.handle)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_, Self> {
        CommonSocketOptions::new(self)
    }

    pub fn router_options(&self) -> RouterSocketOptions<'_> {
        RouterSocketOptions::new(self)
    }

    // -- ROUTER-specific typed options -------------------------------------

    pub(crate) fn set_mandatory(&self, enabled: bool) -> Result<(), ConfigError> {
        set_router_bool(
            self.inner.handle,
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_MANDATORY,
            enabled,
        )
    }

    pub(crate) fn set_probe(&self, enabled: bool) -> Result<(), ConfigError> {
        set_router_bool(
            self.inner.handle,
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_PROBE,
            enabled,
        )
    }

    pub(crate) fn set_connect_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_router_option(
                self.inner.handle,
                ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
                id.data().as_ptr() as *const c_void,
                id.len(),
            )
        })
    }

    pub(crate) fn weight(&self) -> Result<u32, ConfigError> {
        let mut value: u32 = 0;
        let mut len = std::mem::size_of::<u32>();
        check_config_rc(unsafe {
            ffi::zlink_get_router_option(
                self.inner.handle,
                ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_WEIGHT,
                &mut value as *mut u32 as *mut c_void,
                &mut len,
            )
        })?;
        Ok(value)
    }

    pub(crate) fn set_weight(&self, value: u32) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_set_router_option(
                self.inner.handle,
                ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_WEIGHT,
                &value as *const u32 as *const c_void,
                std::mem::size_of::<u32>(),
            )
        })
    }

    pub(crate) fn request_timeout(&self) -> Result<Duration, ConfigError> {
        let mut value: i32 = 0;
        let mut len = std::mem::size_of::<i32>();
        check_config_rc(unsafe {
            ffi::zlink_get_router_option(
                self.inner.handle,
                ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS,
                &mut value as *mut i32 as *mut c_void,
                &mut len,
            )
        })?;
        Ok(Duration::from_millis(value as u64))
    }

    pub(crate) fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError> {
        let millis = timeout_to_ms(value).min(i32::MAX as u32) as i32;
        check_config_rc(unsafe {
            ffi::zlink_set_router_option(
                self.inner.handle,
                ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS,
                &millis as *const i32 as *const c_void,
                std::mem::size_of::<i32>(),
            )
        })
    }

}

impl_base_socket!(RouterSocket);
impl_attach_discovery!(RouterSocket);
impl_connect!(RouterSocket);
impl_send_options!(RouterSocket);
impl_recv_options!(RouterSocket);
impl_routing_id_options!(RouterSocket);

fn router_received_from_raw(
    handle: *mut c_void,
    source_node_rid: *const ffi::zlink_routing_id_t,
    source_spot_rid: *const ffi::zlink_routing_id_t,
    request_seq: u64,
    parts: Vec<Message>,
) -> Received {
    let rid = if source_node_rid.is_null() {
        RoutingId::from_raw(ffi::zlink_routing_id_t {
            size: 0,
            data: [0; 255],
        })
    } else {
        unsafe { RoutingId::from_raw(*source_node_rid) }
    };
    let spot_rid = if source_spot_rid.is_null() {
        None
    } else {
        let rid = unsafe { RoutingId::from_raw(*source_spot_rid) };
        if rid.is_empty() { None } else { Some(rid) }
    };
    if let Some(spot_rid) = spot_rid {
        if request_seq == 0 {
            Received::with_router_spot_send_context(handle, rid, spot_rid, parts)
        } else {
            Received::with_spot_reply_context(handle, rid, spot_rid, request_seq, parts)
        }
    } else if request_seq == 0 {
        Received::with_router_send_context(handle, rid, parts)
    } else {
        Received::with_router_reply_context(handle, rid, request_seq, parts)
    }
}

fn recv_router_once(handle: *mut c_void, flags: u32) -> Result<Option<Received>, RecvError> {
    let mut source_node_rid = ptr::null();
    let mut source_spot_rid = ptr::null();
    let mut request_seq = 0u64;
    let mut parts = Vec::new();
    let mut recv_flags = flags;

    loop {
        let mut part = std::mem::MaybeUninit::<ffi::zlink_msg_t>::uninit();
        unsafe {
            ffi::zlink_msg_init(part.as_mut_ptr());
        }
        let mut has_more = 0;
        let mut current_source_node_rid = ptr::null();
        let mut current_source_spot_rid = ptr::null();
        let mut current_request_seq = 0u64;
        let rc = unsafe {
            ffi::zlink_router_recv_part(
                handle,
                &mut current_source_node_rid,
                &mut current_source_spot_rid,
                &mut current_request_seq,
                part.as_mut_ptr(),
                &mut has_more,
                recv_flags,
            )
        };
        if parts.is_empty() {
            if rc == crate::error::RecvResult::NoData as i32 {
                return Ok(None);
            }
            if rc != 0 {
                let errno = unsafe { ffi::zlink_errno() };
                if errno == libc::EAGAIN {
                    return Ok(None);
                }
                return Err(check_recv_rc(rc).unwrap_err());
            }
            source_node_rid = current_source_node_rid;
            source_spot_rid = current_source_spot_rid;
            request_seq = current_request_seq;
        } else if rc != 0 {
            return Err(check_recv_rc(rc).unwrap_err());
        }

        parts.push(unsafe { crate::message::Message::from_raw(part.assume_init()) });
        if has_more == 0 {
            return Ok(Some(router_received_from_raw(
                handle,
                source_node_rid,
                source_spot_rid,
                request_seq,
                parts,
            )));
        }
        recv_flags = ffi::ZLINK_DONTWAIT;
    }
}

fn timeout_to_ms(timeout: Duration) -> u32 {
    let millis = timeout.as_millis();
    if millis == 0 {
        0
    } else {
        millis.min(u32::MAX as u128) as u32
    }
}

fn set_router_bool(
    handle: *mut c_void,
    opt: ffi::zlink_router_option_t,
    value: bool,
) -> Result<(), ConfigError> {
    let v: i32 = if value { 1 } else { 0 };
    check_config_rc(unsafe {
        ffi::zlink_set_router_option(
            handle,
            opt,
            &v as *const i32 as *const c_void,
            std::mem::size_of::<i32>(),
        )
    })
}
