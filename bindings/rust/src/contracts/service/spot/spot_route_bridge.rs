use std::collections::HashMap;
use std::ffi::{CString, c_void};
use std::mem;
use std::ptr;
use std::time::Duration;

use crate::error::{CloseError, ConfigError, RequestError, SubmitError};
use crate::ffi;
use crate::message::{Message, RoutingId};
use crate::native_errors::{
    check_close_rc, check_config_rc, check_submit_rc, config_validation_error, last_errno,
    request_error_from_result,
};
use crate::request_progress::RequestProgressGuard;
use crate::socket::take_parts;
use crate::socket::{dealer_inner, prepare_send_parts, router_inner};
use crate::{DealerSocket, RequestResult, RouterSocket, SendFlags, SpotNode};

/// Bridges caller-owned channel sockets to the SPOT routed plane.
pub struct SpotRouteBridge {
    handle: *mut c_void,
    endpoint_handles: HashMap<String, *mut c_void>,
}

unsafe impl Send for SpotRouteBridge {}

impl SpotRouteBridge {
    pub(crate) fn new(node: &SpotNode) -> Result<Self, ConfigError> {
        let options = ffi::zlink_spot_route_bridge_options_t {
            struct_size: mem::size_of::<ffi::zlink_spot_route_bridge_options_t>() as u32,
            default_request_timeout_ms: 0,
            error_reply_policy: 0,
            receive_mode: 0,
        };
        let handle = unsafe {
            ffi::zlink_spot_route_bridge_new(
                crate::service::spot_node_context_handle(node),
                crate::service::spot_node_handle(node),
                &options,
            )
        };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self {
            handle,
            endpoint_handles: HashMap::new(),
        })
    }

    pub fn attach_dealer_channel(
        &mut self,
        channel_name: &str,
        dealer: &DealerSocket,
    ) -> Result<(), ConfigError> {
        let channel = fixed_channel_name(channel_name)?;
        let endpoint = dealer_inner(dealer).handle;
        let options = route_only_endpoint_options();
        check_config_rc(unsafe {
            ffi::zlink_spot_route_bridge_attach_dealer_channel(
                self.handle,
                channel.as_ptr(),
                endpoint,
                &options,
            )
        })?;
        self.endpoint_handles
            .insert(channel_name.to_owned(), endpoint);
        Ok(())
    }

    pub fn attach_router_channel(
        &mut self,
        channel_name: &str,
        router: &RouterSocket,
    ) -> Result<(), ConfigError> {
        let channel = fixed_channel_name(channel_name)?;
        let endpoint = router_inner(router).handle;
        let options = route_only_endpoint_options();
        check_config_rc(unsafe {
            ffi::zlink_spot_route_bridge_attach_router_channel(
                self.handle,
                channel.as_ptr(),
                endpoint,
                &options,
            )
        })?;
        self.endpoint_handles
            .insert(channel_name.to_owned(), endpoint);
        Ok(())
    }

    pub fn set_target_node(
        &self,
        channel_name: &str,
        target_node_rid: &RoutingId,
    ) -> Result<(), ConfigError> {
        let channel = fixed_channel_name(channel_name)?;
        check_config_rc(unsafe {
            ffi::zlink_spot_route_bridge_set_target_node(
                self.handle,
                channel.as_ptr(),
                target_node_rid.as_raw(),
            )
        })
    }

    pub fn send(
        &self,
        channel_name: &str,
        target_spot_rid: &RoutingId,
        parts: &mut [Message],
        flags: SendFlags,
    ) -> Result<bool, SubmitError> {
        let channel = fixed_channel_name_submit(channel_name)?;
        let mut native = prepare_send_parts(parts)?;
        let rc = unsafe {
            ffi::zlink_spot_route_bridge_send(
                self.handle,
                channel.as_ptr(),
                target_spot_rid.as_raw(),
                native.as_mut_ptr(),
                native.len(),
                flags.bits(),
            )
        };
        if rc != 0 {
            close_native_parts(&mut native);
        }
        check_submit_rc(rc).map(|_| true)
    }

    pub fn request<F>(
        &self,
        channel_name: &str,
        target_spot_rid: &RoutingId,
        parts: &mut [Message],
        flags: SendFlags,
        timeout: Duration,
        callback: F,
    ) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static,
    {
        let channel = fixed_channel_name_submit(channel_name)?;
        let mut native = prepare_send_parts(parts)?;
        let progress = self
            .endpoint_handles
            .get(channel_name)
            .copied()
            .map(RequestProgressGuard::attach_socket);
        let state = Box::into_raw(Box::new(BridgeRequestCallbackState {
            callback: Some(Box::new(callback)),
            _progress: progress,
        }));
        let rc = unsafe {
            ffi::zlink_spot_route_bridge_request(
                self.handle,
                channel.as_ptr(),
                target_spot_rid.as_raw(),
                native.as_mut_ptr(),
                native.len(),
                Some(bridge_reply_callback),
                state.cast(),
                flags.bits(),
                timeout_to_timeout_ms(timeout),
            )
        };
        if rc != 0 {
            close_native_parts(&mut native);
            unsafe {
                drop(Box::from_raw(state));
            }
        }
        check_submit_rc(rc)
    }

    pub fn drain(&self) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_spot_route_bridge_drain(self.handle) })
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        if self.handle.is_null() {
            return Ok(());
        }
        check_close_rc(unsafe { ffi::zlink_spot_route_bridge_close(self.handle) })?;
        self.handle = ptr::null_mut();
        self.endpoint_handles.clear();
        Ok(())
    }
}

impl Drop for SpotRouteBridge {
    fn drop(&mut self) {
        let _ = self.close();
    }
}

/// Publishes into a local SpotNode topic plane without attaching a raw PUB socket.
pub struct SpotNodePublisher {
    handle: *mut c_void,
}

unsafe impl Send for SpotNodePublisher {}

impl SpotNodePublisher {
    pub(crate) fn new(node: &SpotNode) -> Result<Self, ConfigError> {
        let handle =
            unsafe { ffi::zlink_spot_node_publisher_new(crate::service::spot_node_handle(node)) };
        if handle.is_null() {
            return Err(ConfigError::new(
                crate::error::ConfigResult::InvalidHandle,
                last_errno(),
            ));
        }
        Ok(Self { handle })
    }

    pub fn publish(
        &self,
        topic: &str,
        parts: &mut [Message],
        flags: SendFlags,
    ) -> Result<bool, SubmitError> {
        let topic = fixed_topic_submit(topic)?;
        let mut native = prepare_send_parts(parts)?;
        let rc = unsafe {
            ffi::zlink_spot_node_publisher_publish(
                self.handle,
                topic.as_ptr(),
                native.as_mut_ptr(),
                native.len(),
                flags.bits(),
            )
        };
        if rc != 0 {
            close_native_parts(&mut native);
        }
        check_submit_rc(rc).map(|_| true)
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        if self.handle.is_null() {
            return Ok(());
        }
        check_close_rc(unsafe { ffi::zlink_spot_node_publisher_close(self.handle) })?;
        self.handle = ptr::null_mut();
        Ok(())
    }
}

impl Drop for SpotNodePublisher {
    fn drop(&mut self) {
        let _ = self.close();
    }
}

type BridgeRequestCallback = Box<dyn FnOnce(Result<Vec<Message>, RequestError>) + Send>;

struct BridgeRequestCallbackState {
    callback: Option<BridgeRequestCallback>,
    _progress: Option<RequestProgressGuard>,
}

unsafe extern "C" fn bridge_reply_callback(
    result: ffi::zlink_request_result_t,
    parts: *mut ffi::zlink_msg_t,
    part_count: usize,
    user_data: *mut c_void,
) {
    let mut state = unsafe { Box::from_raw(user_data.cast::<BridgeRequestCallbackState>()) };
    let callback = state.callback.take().expect("spot route bridge callback");
    if result == ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK {
        callback(Ok(take_parts(parts, part_count)));
    } else {
        callback(Err(request_error_from_result(request_result_from_raw(
            result,
        ))));
    }
}

fn route_only_endpoint_options() -> ffi::zlink_spot_route_bridge_endpoint_options_t {
    ffi::zlink_spot_route_bridge_endpoint_options_t {
        struct_size: mem::size_of::<ffi::zlink_spot_route_bridge_endpoint_options_t>() as u32,
        capabilities: ffi::ZLINK_SPOT_ROUTE_BRIDGE_ROUTE_ONLY,
        inbound_relay_policy: 0,
    }
}

fn fixed_channel_name(channel_name: &str) -> Result<CString, ConfigError> {
    if channel_name.is_empty() || channel_name.len() > 255 {
        return Err(config_validation_error());
    }
    CString::new(channel_name).map_err(|_| config_validation_error())
}

fn fixed_channel_name_submit(channel_name: &str) -> Result<CString, SubmitError> {
    if channel_name.is_empty() || channel_name.len() > 255 {
        return Err(SubmitError::new(
            crate::SubmitResult::InvalidArgument,
            libc::EINVAL,
        ));
    }
    CString::new(channel_name)
        .map_err(|_| SubmitError::new(crate::SubmitResult::InvalidArgument, libc::EINVAL))
}

fn fixed_topic_submit(topic: &str) -> Result<CString, SubmitError> {
    if topic.is_empty() || topic.len() > 255 {
        return Err(SubmitError::new(
            crate::SubmitResult::InvalidArgument,
            libc::EINVAL,
        ));
    }
    CString::new(topic)
        .map_err(|_| SubmitError::new(crate::SubmitResult::InvalidArgument, libc::EINVAL))
}

fn close_native_parts(parts: &mut [ffi::zlink_msg_t]) {
    for part in parts {
        unsafe {
            ffi::zlink_msg_close(part);
        }
    }
}

fn timeout_to_timeout_ms(timeout: Duration) -> u32 {
    timeout.as_millis().min(u128::from(u32::MAX)) as u32
}

fn request_result_from_raw(result: ffi::zlink_request_result_t) -> RequestResult {
    match result {
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_OK => RequestResult::Ok,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_TIMED_OUT => RequestResult::TimedOut,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_NOT_FOUND => RequestResult::NotFound,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_TERMINATED => RequestResult::Terminated,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_PROTOCOL_ERROR => {
            RequestResult::ProtocolError
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_REJECTED => RequestResult::Rejected,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_CONFLICT => RequestResult::Conflict,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_BUSY => RequestResult::Busy,
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_NOT_CONNECTED => {
            RequestResult::NotConnected
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_INVALID_ARGUMENT => {
            RequestResult::InvalidArgument
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_INVALID_STATE => {
            RequestResult::InvalidState
        }
        ffi::zlink_request_result_t::ZLINK_REQUEST_RESULT_NOT_SUPPORTED => {
            RequestResult::NotSupported
        }
        _ => RequestResult::InternalError,
    }
}
