use std::ffi::c_void;
use std::ptr;
use std::sync::mpsc;
use std::time::Duration;

use super::{
    SendHandle, SocketInner, impl_attach_discovery, impl_base_socket, impl_connect,
    impl_recv_options, impl_routing_id_options, impl_send_options, prepare_send_parts,
};
use crate::ctx::Context;
use crate::domain::Received;
use crate::error::{
    ConfigError, HandlerError, RecvError, RequestError, RequestResult, SubmitError, ZlinkError,
    check_config_rc, check_recv_rc, check_submit_rc, request_error_from_result,
    submit_error_from_config, submit_not_supported_error,
};
use crate::ffi;
use crate::flags::{RecvFlags, SendFlags};
use crate::message::{IntoMultipart, Message, RoutingId};
use crate::options::{CommonSocketOptions, RouterSocketOptions};

const DEFAULT_REQUEST_TIMEOUT: Duration = Duration::from_secs(5);

struct BlockingRouterRequestState {
    tx: mpsc::Sender<Result<Vec<Message>, RequestError>>,
}

struct CallbackRouterRequestState {
    callback: Option<Box<dyn FnOnce(Result<Vec<Message>, RequestError>) + Send>>,
    native_parts: Vec<ffi::zlink_msg_t>,
}

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
        self.recv_with_flags(RecvFlags::NONE)
    }

    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError> {
        if flags.bits() == 0 {
            let primed = recv_router_once(self.inner.handle, ffi::ZLINK_DONTWAIT)?;
            if let Some(received) = primed {
                return Ok(received);
            }
        }
        recv_router_once(self.inner.handle, flags.bits())?
            .ok_or_else(|| RecvError::new(crate::error::RecvResult::NoData, libc::EAGAIN))
    }

    pub async fn request(
        &self,
        peer_rid: &RoutingId,
        parts: &[&[u8]],
        timeout: Option<Duration>,
    ) -> Result<Vec<Message>, RequestError> {
        let messages = parts
            .iter()
            .map(|part| Message::from_bytes(part))
            .collect::<Result<Vec<_>, _>>()
            .map_err(|err| RequestError::new(RequestResult::ProtocolError, err.internal_errno()))?;
        let (tx, rx) = mpsc::channel();
        let state_ptr = Box::into_raw(Box::new(BlockingRouterRequestState { tx }));
        let submit_result = submit_router_request(
            self.inner.handle,
            peer_rid,
            messages,
            router_blocking_request_callback,
            state_ptr.cast(),
            SendFlags::NONE,
            timeout.unwrap_or(DEFAULT_REQUEST_TIMEOUT),
        );
        if let Err(err) = submit_result {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
            return Err(RequestError::new(
                RequestResult::ProtocolError,
                err.internal_errno(),
            ));
        }
        rx.recv().unwrap_or_else(|_| {
            Err(RequestError::new(
                RequestResult::ProtocolError,
                libc::EINVAL,
            ))
        })
    }

    pub fn request_callback<F>(
        &self,
        peer_rid: &RoutingId,
        parts: &[&[u8]],
        callback: F,
        flags: SendFlags,
        timeout: Option<Duration>,
    ) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        let mut messages = parts
            .iter()
            .map(|part| Message::from_bytes(part))
            .collect::<Result<Vec<_>, _>>()
            .map_err(submit_error_from_config)?;
        let native_parts = prepare_send_parts(&mut messages)?;
        let mut state = Box::new(CallbackRouterRequestState {
            callback: Some(Box::new(callback)),
            native_parts,
        });
        let rc = unsafe {
            ffi::zlink_router_request(
                self.inner.handle,
                peer_rid.as_raw(),
                state.native_parts.as_mut_ptr(),
                state.native_parts.len(),
                router_callback_request_callback,
                (&mut *state as *mut CallbackRouterRequestState).cast(),
                flags.bits(),
                timeout_to_ms(timeout.unwrap_or(DEFAULT_REQUEST_TIMEOUT)),
            )
        };
        let submit_result = check_submit_rc(rc);
        if let Err(err) = submit_result {
            drop(state);
            return Err(err);
        }
        let _ = Box::into_raw(state);
        Ok(())
    }

    pub fn reply(
        &self,
        rid: &RoutingId,
        request_seq: u64,
        parts: impl IntoMultipart,
    ) -> Result<(), SubmitError> {
        self.reply_with_flags(rid, request_seq, parts, SendFlags::NONE)
    }

    pub fn reply_with_flags(
        &self,
        rid: &RoutingId,
        request_seq: u64,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        submit_router_reply(self.inner.handle, rid, request_seq, parts, flags)
    }

    pub fn send_to_spot(
        &self,
        dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId,
        parts: impl IntoMultipart,
    ) -> Result<(), SubmitError> {
        self.send_to_spot_with_flags(dest_node_rid, dest_spot_rid, parts, SendFlags::NONE)
    }

    pub fn send_to_spot_with_flags(
        &self,
        dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        let mut parts = parts.into_parts();
        let mut native = super::prepare_send_parts(&mut parts)?;
        let rc = unsafe {
            ffi::zlink_router_send_spot(
                self.inner.handle,
                dest_node_rid.as_raw(),
                dest_spot_rid.as_raw(),
                native.as_mut_ptr(),
                native.len(),
                flags.bits(),
            )
        };
        drop(parts);
        check_submit_rc(rc)
    }

    pub async fn request_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        parts: impl IntoMultipart,
        timeout: Duration,
    ) -> Result<Vec<Message>, ZlinkError> {
        let (tx, rx) = mpsc::channel();
        self.request_to_spot_callback(
            dest_node_rid,
            dest_spot_rid,
            parts,
            move |result| {
                let _ = tx.send(result);
            },
            SendFlags::NONE,
            timeout,
        )?;
        rx.recv()
            .unwrap_or_else(|_| {
                Err(crate::error::RequestError::new(
                    crate::error::RequestResult::ProtocolError,
                    libc::EINVAL,
                ))
            })
            .map_err(ZlinkError::from)
    }

    pub fn request_to_spot_callback<F>(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        parts: impl IntoMultipart,
        callback: F,
        flags: SendFlags,
        timeout: Duration,
    ) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        let mut parts = parts.into_parts();
        let mut native = super::prepare_send_parts(&mut parts)?;
        let state_ptr = Box::into_raw(Box::new(RouterSpotReplyCallbackState {
            callback: Some(Box::new(callback)),
        }));
        let rc = unsafe {
            ffi::zlink_router_request_spot(
                self.inner.handle,
                dest_node_rid.as_raw(),
                dest_spot_rid.as_raw(),
                native.as_mut_ptr(),
                native.len(),
                router_spot_reply_callback,
                state_ptr.cast(),
                flags.bits(),
                timeout_to_ms(timeout),
            )
        };
        if rc != 0 {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
        }
        check_submit_rc(rc)
    }

    pub fn reply_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
        parts: impl IntoMultipart,
    ) -> Result<(), SubmitError> {
        self.reply_to_spot_with_flags(
            dest_node_rid,
            dest_spot_rid,
            request_seq,
            parts,
            SendFlags::NONE,
        )
    }

    pub fn reply_to_spot_with_flags(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        if flags.bits() != 0 {
            return Err(submit_not_supported_error());
        }
        let mut parts = parts.into_parts();
        let mut native = super::prepare_send_parts(&mut parts)?;
        let rc = unsafe {
            ffi::zlink_router_reply_spot(
                self.inner.handle,
                dest_node_rid.as_raw(),
                dest_spot_rid.as_raw(),
                request_seq,
                native.as_mut_ptr(),
                native.len(),
            )
        };
        drop(parts);
        check_submit_rc(rc)
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

    pub(crate) fn set_handover(&self, enabled: bool) -> Result<(), ConfigError> {
        set_router_bool(
            self.inner.handle,
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_HANDOVER,
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
    parts_ptr: *mut ffi::zlink_msg_t,
    part_count: usize,
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
    let parts = super::take_parts(parts_ptr, part_count);
    if let Some(spot_rid) = spot_rid {
        if request_seq == 0 {
            let mut received = Received::new(Some(rid), parts);
            received.spot_rid = Some(spot_rid);
            received
        } else {
            Received::with_spot_reply_context(handle, rid, spot_rid, request_seq, parts)
        }
    } else if request_seq == 0 {
        Received::new(Some(rid), parts)
    } else {
        Received::with_router_reply_context(handle, rid, request_seq, parts)
    }
}

fn recv_router_once(handle: *mut c_void, flags: u32) -> Result<Option<Received>, RecvError> {
    let mut source_node_rid = ptr::null();
    let mut source_spot_rid = ptr::null();
    let mut request_seq = 0u64;
    let mut parts_ptr: *mut ffi::zlink_msg_t = ptr::null_mut();
    let mut part_count: usize = 0;
    let rc = unsafe {
        ffi::zlink_router_recv(
            handle,
            &mut source_node_rid,
            &mut source_spot_rid,
            &mut request_seq,
            &mut parts_ptr,
            &mut part_count,
            flags,
        )
    };
    if rc == crate::error::RecvResult::NoData as i32 {
        return Ok(None);
    }
    check_recv_rc(rc)?;
    Ok(Some(router_received_from_raw(
        handle,
        source_node_rid,
        source_spot_rid,
        request_seq,
        parts_ptr,
        part_count,
    )))
}

fn submit_router_request(
    handle: *mut c_void,
    peer_rid: &RoutingId,
    parts: impl IntoMultipart,
    callback: ffi::zlink_reply_handler_fn,
    userdata: *mut c_void,
    flags: SendFlags,
    timeout: Duration,
) -> Result<(), SubmitError> {
    let mut parts = parts.into_parts();
    let mut native = prepare_send_parts(&mut parts)?;
    let rc = unsafe {
        ffi::zlink_router_request(
            handle,
            peer_rid.as_raw(),
            native.as_mut_ptr(),
            native.len(),
            callback,
            userdata,
            flags.bits(),
            timeout_to_ms(timeout),
        )
    };
    check_submit_rc(rc)
}

fn submit_router_reply(
    handle: *mut c_void,
    rid: &RoutingId,
    request_seq: u64,
    parts: impl IntoMultipart,
    flags: SendFlags,
) -> Result<(), SubmitError> {
    if flags.bits() != 0 {
        return Err(submit_not_supported_error());
    }
    let mut parts = parts.into_parts();
    let mut native = prepare_send_parts(&mut parts)?;
    let rc = unsafe {
        ffi::zlink_router_reply(
            handle,
            rid.as_raw(),
            request_seq,
            native.as_mut_ptr(),
            native.len(),
        )
    };
    check_submit_rc(rc)
}

fn request_parts_from_callback(
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
) -> Result<Vec<Message>, RequestError> {
    let mut out = Vec::with_capacity(part_count);
    for index in 0..part_count {
        let part = unsafe { parts.add(index) };
        let size = unsafe { ffi::zlink_msg_size(part.cast_const()) };
        let data = unsafe { ffi::zlink_msg_data(part) } as *const u8;
        let bytes = if data.is_null() || size == 0 {
            &[][..]
        } else {
            unsafe { std::slice::from_raw_parts(data, size) }
        };
        out.push(Message::from_bytes(bytes).map_err(|err| {
            RequestError::new(RequestResult::ProtocolError, err.internal_errno())
        })?);
    }
    Ok(out)
}

fn request_result_from_ffi(result: ffi::zlink_request_result_t) -> RequestResult {
    match result {
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK => RequestResult::Ok,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_TIMED_OUT => RequestResult::TimedOut,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_NOT_FOUND => RequestResult::NotFound,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_TERMINATED => RequestResult::Terminated,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_PROTOCOL_ERROR => {
            RequestResult::ProtocolError
        }
    }
}

unsafe extern "C" fn router_blocking_request_callback(
    result: ffi::zlink_request_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let state = unsafe { Box::from_raw(userdata.cast::<BlockingRouterRequestState>()) };
    let delivery = if result == ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK {
        request_parts_from_callback(parts, part_count)
    } else {
        Err(request_error_from_result(request_result_from_ffi(result)))
    };
    let _ = state.tx.send(delivery);
}

unsafe extern "C" fn router_callback_request_callback(
    result: ffi::zlink_request_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let mut state = unsafe { Box::from_raw(userdata.cast::<CallbackRouterRequestState>()) };
    let callback = state.callback.take().expect("router request callback");
    let delivery = if result == ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK {
        request_parts_from_callback(parts, part_count)
    } else {
        Err(request_error_from_result(request_result_from_ffi(result)))
    };
    callback(delivery);
}

struct RouterSpotReplyCallbackState {
    callback: Option<Box<dyn FnOnce(Result<Vec<Message>, crate::error::RequestError>) + Send>>,
}

unsafe extern "C" fn router_spot_reply_callback(
    result_: ffi::zlink_request_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let mut state = unsafe { Box::from_raw(userdata.cast::<RouterSpotReplyCallbackState>()) };
    let callback = state.callback.take().expect("router spot callback");
    if result_ == ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK {
        callback(Ok(super::take_parts(parts, part_count)));
    } else {
        let request_result = match result_ {
            ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK => RequestResult::Ok,
            ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_TIMED_OUT => RequestResult::TimedOut,
            ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_NOT_FOUND => RequestResult::NotFound,
            ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_TERMINATED => {
                RequestResult::Terminated
            }
            ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_PROTOCOL_ERROR => {
                RequestResult::ProtocolError
            }
        };
        callback(Err(request_error_from_result(request_result)));
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
