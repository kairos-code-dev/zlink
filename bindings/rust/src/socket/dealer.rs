use std::ffi::c_void;
use std::sync::mpsc;
use std::time::Duration;

use crate::ctx::Context;
use crate::domain::Received;
use crate::error::{
    ConfigError, HandlerError, RecvError, RequestError, RequestResult, SubmitError,
    check_config_rc, check_submit_rc, request_error_from_result, request_error_from_submit,
    submit_error_from_config,
};
use crate::ffi;
use crate::flags::{RecvFlags, SendFlags};
use crate::message::{IntoMultipart, Message, RoutingId};
use crate::options::{CommonSocketOptions, DealerSocketOptions};
use crate::request_progress::RequestProgressGuard;
use crate::service::request_result_from_raw;

use super::{
    SendHandle, SocketInner, check_send_result, impl_attach_discovery, impl_base_socket,
    impl_connect, impl_recv_options, impl_routing_id_options, impl_send_options,
    prepare_send_parts, submit_part_sequence,
};

const DEFAULT_REQUEST_TIMEOUT: Duration = Duration::from_secs(5);
type RequestCallback = Box<dyn FnOnce(Result<Vec<Message>, RequestError>) + Send>;

struct BlockingRequestState {
    tx: mpsc::Sender<Result<Vec<Message>, RequestError>>,
    _progress: RequestProgressGuard,
}

struct CallbackRequestState {
    callback: Option<RequestCallback>,
    native_parts: Vec<ffi::zlink_msg_t>,
    _progress: RequestProgressGuard,
}

/// DEALER socket – asynchronous request/reply pattern (client side).
///
/// Capabilities: `send`, `recv`, `on_send_ready`.
pub struct DealerSocket {
    pub(crate) inner: SocketInner,
}

impl DealerSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_DEALER)?,
        })
    }

    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), SubmitError> {
        self.inner.send(parts)
    }

    pub fn try_send(&self, parts: impl IntoMultipart) -> Result<bool, SubmitError> {
        self.inner.try_send(parts)
    }

    pub fn send_with_flags(
        &self,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        self.inner.send_with_flags(parts, flags)
    }

    pub fn recv(&self) -> Result<Received, RecvError> {
        self.inner.recv()
    }

    pub fn try_recv(&self) -> Result<Option<Received>, RecvError> {
        self.inner.recv_no_wait()
    }

    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError> {
        self.inner.recv_with_flags(flags)
    }

    pub async fn request(
        &self,
        parts: &[&[u8]],
        timeout: Option<Duration>,
    ) -> Result<Vec<Message>, RequestError> {
        let messages = parts
            .iter()
            .map(|part| Message::from_bytes(part))
            .collect::<Result<Vec<_>, _>>()
            .map_err(|err| RequestError::new(RequestResult::ProtocolError, err.internal_errno()))?;
        let (tx, rx) = mpsc::channel();
        let state_ptr = Box::into_raw(Box::new(BlockingRequestState {
            tx,
            _progress: RequestProgressGuard::attach_socket(self.inner.handle),
        }));
        let submit_result = submit_dealer_request(
            self.inner.handle,
            messages,
            dealer_blocking_request_callback,
            state_ptr.cast(),
            SendFlags::NONE,
            timeout.unwrap_or(DEFAULT_REQUEST_TIMEOUT),
        );
        if let Err(err) = submit_result {
            unsafe {
                drop(Box::from_raw(state_ptr));
            }
            return Err(request_error_from_submit(err));
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
        parts: &[&[u8]],
        callback: F,
        timeout: Option<Duration>,
    ) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        self.request_callback_with_flags(parts, callback, SendFlags::NONE, timeout)
    }

    pub fn try_request_callback<F>(
        &self,
        parts: &[&[u8]],
        callback: F,
        timeout: Option<Duration>,
    ) -> Result<bool, SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        let mut messages = parts
            .iter()
            .map(|part| Message::from_bytes(part))
            .collect::<Result<Vec<_>, _>>()
            .map_err(submit_error_from_config)?;
        let native_parts = prepare_send_parts(&mut messages)?;
        let mut state = Box::new(CallbackRequestState {
            callback: Some(Box::new(callback)),
            native_parts,
            _progress: RequestProgressGuard::attach_socket(self.inner.handle),
        });
        let timeout_ms = timeout_to_ms(timeout.unwrap_or(DEFAULT_REQUEST_TIMEOUT));
        let userdata = (&mut *state as *mut CallbackRequestState).cast();
        let rc = submit_part_sequence(
            state.native_parts.as_mut_slice(),
            |part, part_flag, is_final| unsafe {
                ffi::zlink_dealer_request_part(
                    self.inner.handle,
                    part,
                    ffi::ZLINK_DONTWAIT,
                    part_flag,
                    if is_final { timeout_ms } else { 0 },
                    if is_final {
                        Some(dealer_callback_request_callback)
                    } else {
                        None
                    },
                    if is_final {
                        userdata
                    } else {
                        std::ptr::null_mut()
                    },
                )
            },
        )?;
        match check_send_result(rc) {
            Ok(crate::domain::SendResult::Sent) => {
                let _ = Box::into_raw(state);
                Ok(true)
            }
            Ok(crate::domain::SendResult::Backpressured) => {
                drop(state);
                Ok(false)
            }
            Ok(crate::domain::SendResult::NotReady) => {
                drop(state);
                Err(SubmitError::new(
                    crate::error::SubmitResult::NotConnected,
                    libc::ENOTCONN,
                ))
            }
            Err(err) => {
                drop(state);
                Err(err)
            }
        }
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        self.inner.on_send_ready(handler)
    }

    pub fn set_channel_name(&self, channel_name: &str) -> Result<(), ConfigError> {
        self.inner.set_channel_name(channel_name)
    }

    pub fn channel_name(&self) -> Result<String, ConfigError> {
        self.inner.channel_name()
    }

    /// Obtain a lightweight, cloneable handle for sending from callbacks or
    /// other threads. The returned handle does not own the socket; the
    /// `DealerSocket` must remain alive while the handle is in use.
    pub fn send_handle(&self) -> SendHandle {
        SendHandle::new(self.inner.handle)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_, Self> {
        CommonSocketOptions::new(self)
    }

    pub fn dealer_options(&self) -> DealerSocketOptions<'_> {
        DealerSocketOptions::new(self)
    }

    pub(crate) fn set_probe(&self, enabled: bool) -> Result<(), ConfigError> {
        set_dealer_bool_option(
            self.inner.handle,
            ffi::zlink_dealer_option_t::ZLINK_DEALER_OPT_PROBE,
            enabled,
        )
    }

    fn request_callback_with_flags<F>(
        &self,
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
        let mut state = Box::new(CallbackRequestState {
            callback: Some(Box::new(callback)),
            native_parts,
            _progress: RequestProgressGuard::attach_socket(self.inner.handle),
        });
        let timeout_ms = timeout_to_ms(timeout.unwrap_or(DEFAULT_REQUEST_TIMEOUT));
        let userdata = (&mut *state as *mut CallbackRequestState).cast();
        let rc = submit_part_sequence(
            state.native_parts.as_mut_slice(),
            |part, part_flag, is_final| unsafe {
                ffi::zlink_dealer_request_part(
                    self.inner.handle,
                    part,
                    flags.bits(),
                    part_flag,
                    if is_final { timeout_ms } else { 0 },
                    if is_final {
                        Some(dealer_callback_request_callback)
                    } else {
                        None
                    },
                    if is_final {
                        userdata
                    } else {
                        std::ptr::null_mut()
                    },
                )
            },
        )?;
        let submit_result = check_submit_rc(rc);
        if let Err(err) = submit_result {
            drop(state);
            return Err(err);
        }
        let _ = Box::into_raw(state);
        Ok(())
    }
}

fn set_dealer_bool_option(
    handle: *mut c_void,
    option: ffi::zlink_dealer_option_t,
    enabled: bool,
) -> Result<(), ConfigError> {
    let value: i32 = if enabled { 1 } else { 0 };
    check_config_rc(unsafe {
        ffi::zlink_set_dealer_option(
            handle,
            option,
            &value as *const i32 as *const c_void,
            std::mem::size_of::<i32>(),
        )
    })
}

impl_base_socket!(DealerSocket);
impl_attach_discovery!(DealerSocket);
impl_connect!(DealerSocket);
impl_send_options!(DealerSocket);
impl_recv_options!(DealerSocket);
impl_routing_id_options!(DealerSocket);

fn submit_dealer_request(
    handle: *mut c_void,
    parts: impl IntoMultipart,
    callback: ffi::zlink_reply_handler_fn,
    userdata: *mut c_void,
    flags: SendFlags,
    timeout: Duration,
) -> Result<(), SubmitError> {
    let mut parts = parts.into_parts();
    let mut native = prepare_send_parts(&mut parts)?;
    let timeout_ms = timeout_to_ms(timeout);
    let rc = submit_part_sequence(&mut native, |part, part_flag, is_final| unsafe {
        ffi::zlink_dealer_request_part(
            handle,
            part,
            flags.bits(),
            part_flag,
            if is_final { timeout_ms } else { 0 },
            if is_final { Some(callback) } else { None },
            if is_final {
                userdata
            } else {
                std::ptr::null_mut()
            },
        )
    })?;
    check_submit_rc(rc)
}

fn timeout_to_ms(timeout: Duration) -> u32 {
    let millis = timeout.as_millis();
    if millis == 0 {
        0
    } else {
        millis.min(u32::MAX as u128) as u32
    }
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
    request_result_from_raw(result)
}

unsafe extern "C" fn dealer_blocking_request_callback(
    result: ffi::zlink_request_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let state = unsafe { Box::from_raw(userdata.cast::<BlockingRequestState>()) };
    let delivery = if result == ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK {
        request_parts_from_callback(parts, part_count)
    } else {
        Err(request_error_from_result(request_result_from_ffi(result)))
    };
    let _ = state.tx.send(delivery);
}

unsafe extern "C" fn dealer_callback_request_callback(
    result: ffi::zlink_request_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
) {
    let mut state = unsafe { Box::from_raw(userdata.cast::<CallbackRequestState>()) };
    let callback = state.callback.take().expect("dealer request callback");
    let delivery = if result == ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK {
        request_parts_from_callback(parts, part_count)
    } else {
        Err(request_error_from_result(request_result_from_ffi(result)))
    };
    callback(delivery);
}
