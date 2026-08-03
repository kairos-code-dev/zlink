// SPDX-License-Identifier: MPL-2.0

//! Private trait bridges between the public Rust contracts and raw Core 11
//! implementations. Specialized domain objects are intentionally absent: the
//! raw header and socket contracts own the complete runtime boundary.

use std::any::Any;
use std::os::fd::RawFd;
use std::time::Duration;

use crate::core_context::AutoHwmProfile;
use crate::error::{CloseError, ConfigError, HandlerError, RecvError, RequestError, SubmitError};
use crate::flags::{RecvFlags, RidDuplicatePolicy, SendFlags, SubmitRetryMode};
use crate::message::{Message, RoutingId};
use crate::messaging_operations::{CallbackReady, Empty, Ready, ReplyOp, RequestOp, SendOp};
use crate::monitor_contracts::{MonitorEvent, MonitorStatus};
use crate::poller_contracts::{PollEvent, Pollable, Timer};

pub(crate) trait MessageRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn as_bytes(&self) -> &[u8];
    fn data_mut(&mut self) -> &mut [u8];
    fn size(&self) -> usize;
    fn try_clone_box(&self) -> Result<Box<dyn MessageRuntime>, ConfigError>;
    fn ref_count(&self) -> i32;
}

pub(crate) trait ReceivedReplyRuntime: Send {
    fn reply_op(&self) -> ReplyOp<Empty>;
}

pub(crate) trait ReceivedSendRuntime: Send {
    fn send_op(&self) -> SendOp<Empty>;
}

pub(crate) trait ContextOptionRuntime {
    fn io_threads(&self) -> Result<i32, ConfigError>;
    fn set_io_threads(&self, threads: i32) -> Result<(), ConfigError>;
    fn max_sockets(&self) -> Result<i32, ConfigError>;
    fn set_max_sockets(&self, max: i32) -> Result<(), ConfigError>;
    fn socket_limit(&self) -> Result<i32, ConfigError>;
    fn thread_priority(&self) -> Result<i32, ConfigError>;
    fn set_thread_priority(&self, priority: i32) -> Result<(), ConfigError>;
    fn thread_scheduling_policy(&self) -> Result<i32, ConfigError>;
    fn set_thread_scheduling_policy(&self, policy: i32) -> Result<(), ConfigError>;
    fn max_message_size(&self) -> Result<i32, ConfigError>;
    fn set_max_message_size(&self, size: i32) -> Result<(), ConfigError>;
    fn msg_t_size(&self) -> Result<i32, ConfigError>;
    fn blocky(&self) -> Result<bool, ConfigError>;
    fn set_blocky(&self, blocky: bool) -> Result<(), ConfigError>;
    fn thread_name_prefix(&self) -> Result<String, ConfigError>;
    fn set_thread_name_prefix(&self, prefix: &str) -> Result<(), ConfigError>;
    fn auto_hwm_enabled(&self) -> Result<bool, ConfigError>;
    fn set_auto_hwm_enabled(&self, enabled: bool) -> Result<(), ConfigError>;
    fn auto_hwm_recalc_debounce(&self) -> Result<Duration, ConfigError>;
    fn set_auto_hwm_recalc_debounce(&self, value: Duration) -> Result<(), ConfigError>;
    fn auto_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError>;
    fn set_auto_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError>;
    fn auto_hwm_msg_unit_bytes(&self) -> Result<u64, ConfigError>;
    fn set_auto_hwm_msg_unit_bytes(&self, bytes: u64) -> Result<(), ConfigError>;
    fn add_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError>;
    fn remove_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError>;
}

pub(crate) trait ContextRuntime: Any + ContextOptionRuntime + Send + Sync {
    fn as_any(&self) -> &dyn Any;
    fn shutdown(&self) -> Result<(), CloseError>;
    fn recalculate_auto_hwm(&self) -> Result<(), ConfigError>;
}

pub(crate) trait PairSocketRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

pub(crate) trait SocketRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

pub(crate) type TimerFireHandler = Box<dyn Fn(&Timer, u64) + Send>;

pub(crate) trait PollerRuntime: Send {
    fn add_socket(
        &self,
        socket: &dyn Pollable,
        events: i16,
        slot: usize,
    ) -> Result<(), ConfigError>;
    fn modify_socket(&self, socket: &dyn Pollable, events: i16) -> Result<(), ConfigError>;
    fn remove_socket(&self, socket: &dyn Pollable) -> Result<(), ConfigError>;
    fn add_fd(&self, fd: RawFd, events: i16, slot: usize) -> Result<(), ConfigError>;
    fn modify_fd(&self, fd: RawFd, events: i16) -> Result<(), ConfigError>;
    fn remove_fd(&self, fd: RawFd) -> Result<(), ConfigError>;
    fn add_timer(&self, timer: &Timer, slot: usize) -> Result<(), ConfigError>;
    fn remove_timer(&self, timer: &Timer) -> Result<(), ConfigError>;
    fn wait(&self, events: &mut [PollEvent], timeout_ms: i64) -> Result<usize, RecvError>;
    fn size(&self) -> i32;
}

pub(crate) trait TimerRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn start(&self, interval_ns: u64, repeat_count: u64) -> Result<(), ConfigError>;
    fn stop(&self) -> Result<(), ConfigError>;
    fn recv(&self) -> Result<Option<u64>, RecvError>;
    fn on_fire(&mut self, timer_ptr: usize, handler: TimerFireHandler) -> Result<(), HandlerError>;
}

pub(crate) trait SocketMonitorRuntime: Send {
    fn recv(&self) -> Result<MonitorEvent, RecvError>;
    fn recv_with_flags(&self, flags: RecvFlags) -> Result<Option<MonitorEvent>, RecvError>;
    fn status(&self) -> Result<MonitorStatus, ConfigError>;
    fn on_event(&mut self, handler: Box<dyn Fn(&MonitorEvent) + Send>) -> Result<(), HandlerError>;
    fn close(&mut self) -> Result<(), CloseError>;
}

pub(crate) trait CommonSocketOptionRuntime {
    fn set_linger(&self, d: Duration) -> Result<(), ConfigError>;
    fn linger(&self) -> Result<Duration, ConfigError>;
    fn set_send_high_water_mark(&self, value: u64) -> Result<(), ConfigError>;
    fn send_high_water_mark(&self) -> Result<u64, ConfigError>;
    fn set_receive_high_water_mark(&self, value: u64) -> Result<(), ConfigError>;
    fn receive_high_water_mark(&self) -> Result<u64, ConfigError>;
    fn set_send_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn send_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_receive_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn receive_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError>;
    fn immediate(&self) -> Result<bool, ConfigError>;
    fn set_rid_duplicate_policy(&self, value: RidDuplicatePolicy) -> Result<(), ConfigError>;
    fn rid_duplicate_policy(&self) -> Result<RidDuplicatePolicy, ConfigError>;
    fn set_connect_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn connect_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_ipv6(&self, enabled: bool) -> Result<(), ConfigError>;
    fn ipv6(&self) -> Result<bool, ConfigError>;
    fn set_tcp_no_delay(&self, enabled: bool) -> Result<(), ConfigError>;
    fn tcp_no_delay(&self) -> Result<bool, ConfigError>;
    fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ConfigError>;
    fn tcp_keepalive(&self) -> Result<bool, ConfigError>;
    fn set_max_message_size(&self, bytes: i64) -> Result<(), ConfigError>;
    fn max_message_size(&self) -> Result<i64, ConfigError>;
    fn set_backlog(&self, value: i32) -> Result<(), ConfigError>;
    fn backlog(&self) -> Result<i32, ConfigError>;
    fn set_reconnect_interval(&self, d: Duration) -> Result<(), ConfigError>;
    fn reconnect_interval(&self) -> Result<Duration, ConfigError>;
    fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ConfigError>;
    fn reconnect_interval_max(&self) -> Result<Duration, ConfigError>;
    fn set_submit_retry_mode(&self, value: SubmitRetryMode) -> Result<(), ConfigError>;
    fn submit_retry_mode(&self) -> Result<SubmitRetryMode, ConfigError>;
    fn set_submit_retry_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn submit_retry_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_submit_retry_attempts(&self, value: i32) -> Result<(), ConfigError>;
    fn submit_retry_attempts(&self) -> Result<i32, ConfigError>;
}

pub(crate) trait RouterSocketOptionRuntime {
    fn set_mandatory(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_probe(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_connect_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError>;
    fn weight(&self) -> Result<u32, ConfigError>;
    fn set_weight(&self, value: u32) -> Result<(), ConfigError>;
    fn request_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError>;
}

pub(crate) trait DealerSocketOptionRuntime {
    fn set_probe(&self, enabled: bool) -> Result<(), ConfigError>;
    fn weight(&self) -> Result<u32, ConfigError>;
    fn set_weight(&self, value: u32) -> Result<(), ConfigError>;
    fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError>;
}

pub(crate) trait StreamSocketOptionRuntime {
    fn set_notify(&self, enabled: bool) -> Result<(), ConfigError>;
    fn notify(&self) -> Result<bool, ConfigError>;
}

pub(crate) trait PubSocketOptionRuntime {
    fn set_verbose(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_verboser(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_no_drop(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_manual(&self, enabled: bool) -> Result<(), ConfigError>;
    fn manual_last_value(&self) -> Result<bool, ConfigError>;
    fn set_manual_last_value(&self, enabled: bool) -> Result<(), ConfigError>;
    fn welcome_message(&self) -> Result<Message, ConfigError>;
    fn set_welcome_message(&self, message: &Message) -> Result<(), ConfigError>;
    fn approve_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError>;
    fn reject_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError>;
    fn topics_count(&self) -> Result<i32, ConfigError>;
}

pub(crate) trait SubSocketOptionRuntime {
    fn topics_count(&self) -> Result<i32, ConfigError>;
}

pub(crate) trait SendOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait RequestOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait ReplyOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait SendOpEmptyRuntime {
    fn message(self, message: Message) -> SendOp<Ready>;
}

pub(crate) trait SendOpReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn flags(self, flags: SendFlags) -> Self;
    fn submit(self) -> Result<bool, SubmitError>;
}

pub(crate) trait RequestOpEmptyRuntime {
    fn message(self, message: Message) -> RequestOp<Ready>;
}

pub(crate) trait RequestOpReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn timeout(self, timeout: Duration) -> Self;
    fn flags(self, flags: SendFlags) -> RequestOp<CallbackReady>;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;
}

pub(crate) trait RequestOpCallbackReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn timeout(self, timeout: Duration) -> Self;
    fn flags(self, flags: SendFlags) -> Self;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;
}

pub(crate) trait ReplyOpEmptyRuntime {
    fn message(self, message: Message) -> ReplyOp<Ready>;
}

pub(crate) trait ReplyOpReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn flags(self, flags: SendFlags) -> Self;
    fn submit(self) -> Result<(), SubmitError>;
}

pub(crate) use CommonSocketOptionRuntime as CommonSocketOptionEndpoint;
pub(crate) use ContextRuntime as ContextStorage;
pub(crate) use DealerSocketOptionRuntime as DealerSocketOptionEndpoint;
pub(crate) use MessageRuntime as MessageStorage;
pub(crate) use PairSocketRuntime as PairSocketStorage;
pub(crate) use PollerRuntime as PollerStorage;
pub(crate) use PubSocketOptionRuntime as PubSocketOptionEndpoint;
pub(crate) use ReceivedReplyRuntime as ReceivedReplyContext;
pub(crate) use ReceivedSendRuntime as ReceivedSendContext;
pub(crate) use ReplyOpEmptyRuntime as ReplyOpEmptyContract;
pub(crate) use ReplyOpReadyRuntime as ReplyOpReadyContract;
pub(crate) use ReplyOpRuntime as ReplyOpStorage;
pub(crate) use RequestOpCallbackReadyRuntime as RequestOpCallbackReadyContract;
pub(crate) use RequestOpEmptyRuntime as RequestOpEmptyContract;
pub(crate) use RequestOpReadyRuntime as RequestOpReadyContract;
pub(crate) use RequestOpRuntime as RequestOpStorage;
pub(crate) use RouterSocketOptionRuntime as RouterSocketOptionEndpoint;
pub(crate) use SendOpEmptyRuntime as SendOpEmptyContract;
pub(crate) use SendOpReadyRuntime as SendOpReadyContract;
pub(crate) use SendOpRuntime as SendOpStorage;
pub(crate) use SocketMonitorRuntime as SocketMonitorStorage;
pub(crate) use SocketRuntime as SocketStorage;
pub(crate) use StreamSocketOptionRuntime as StreamSocketOptionEndpoint;
pub(crate) use SubSocketOptionRuntime as SubSocketOptionEndpoint;
pub(crate) use TimerRuntime as TimerStorage;
