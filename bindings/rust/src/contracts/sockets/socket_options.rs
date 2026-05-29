#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SendFlags(u32);

impl SendFlags {
    pub const NONE: Self = Self(0);
    pub const DONT_WAIT: Self = Self(0x0001);

    pub const fn bits(self) -> u32 {
        self.0
    }
}

impl Default for SendFlags {
    fn default() -> Self {
        Self::NONE
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RecvFlags(u32);

impl RecvFlags {
    pub const NONE: Self = Self(0);
    pub const DONT_WAIT: Self = Self(0x0001);

    pub const fn bits(self) -> u32 {
        self.0
    }
}

impl Default for RecvFlags {
    fn default() -> Self {
        Self::NONE
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum RidDuplicatePolicy {
    Reject = 0,
    Handover = 1,
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum SubmitRetryMode {
    Off = 0,
    LocalFailure = 1,
}

pub(crate) trait CommonSocketOptionRuntime {
    fn set_linger(&self, d: Duration) -> Result<(), ConfigError>;
    fn linger(&self) -> Result<Duration, ConfigError>;
    fn set_send_high_water_mark(&self, value: i32) -> Result<(), ConfigError>;
    fn send_high_water_mark(&self) -> Result<i32, ConfigError>;
    fn set_receive_high_water_mark(&self, value: i32) -> Result<(), ConfigError>;
    fn receive_high_water_mark(&self) -> Result<i32, ConfigError>;
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
    fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ConfigError>;
    fn heartbeat_interval(&self) -> Result<Duration, ConfigError>;
    fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ConfigError>;
    fn heartbeat_ttl(&self) -> Result<Duration, ConfigError>;
    fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn heartbeat_timeout(&self) -> Result<Duration, ConfigError>;
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

pub struct CommonSocketOptions<'a> {
    inner: &'a dyn CommonSocketOptionRuntime,
}

impl<'a> CommonSocketOptions<'a> {
    pub(crate) fn new(inner: &'a dyn CommonSocketOptionRuntime) -> Self {
        Self { inner }
    }

    pub fn set_linger(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_linger(d)
    }
    pub fn linger(&self) -> Result<Duration, ConfigError> {
        self.inner.linger()
    }
    pub fn set_send_high_water_mark(&self, value: i32) -> Result<(), ConfigError> {
        self.inner.set_send_high_water_mark(value)
    }
    pub fn send_high_water_mark(&self) -> Result<i32, ConfigError> {
        self.inner.send_high_water_mark()
    }
    pub fn set_receive_high_water_mark(&self, value: i32) -> Result<(), ConfigError> {
        self.inner.set_receive_high_water_mark(value)
    }
    pub fn receive_high_water_mark(&self) -> Result<i32, ConfigError> {
        self.inner.receive_high_water_mark()
    }
    pub fn set_send_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_send_timeout(d)
    }
    pub fn send_timeout(&self) -> Result<Duration, ConfigError> {
        self.inner.send_timeout()
    }
    pub fn set_receive_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_receive_timeout(d)
    }
    pub fn receive_timeout(&self) -> Result<Duration, ConfigError> {
        self.inner.receive_timeout()
    }
    pub fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_immediate(enabled)
    }
    pub fn immediate(&self) -> Result<bool, ConfigError> {
        self.inner.immediate()
    }
    pub fn set_rid_duplicate_policy(&self, value: RidDuplicatePolicy) -> Result<(), ConfigError> {
        self.inner.set_rid_duplicate_policy(value)
    }
    pub fn rid_duplicate_policy(&self) -> Result<RidDuplicatePolicy, ConfigError> {
        self.inner.rid_duplicate_policy()
    }
    pub fn set_connect_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_connect_timeout(d)
    }
    pub fn connect_timeout(&self) -> Result<Duration, ConfigError> {
        self.inner.connect_timeout()
    }
    pub fn set_ipv6(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_ipv6(enabled)
    }
    pub fn ipv6(&self) -> Result<bool, ConfigError> {
        self.inner.ipv6()
    }
    pub fn set_tcp_no_delay(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_tcp_no_delay(enabled)
    }
    pub fn tcp_no_delay(&self) -> Result<bool, ConfigError> {
        self.inner.tcp_no_delay()
    }
    pub fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_tcp_keepalive(enabled)
    }
    pub fn tcp_keepalive(&self) -> Result<bool, ConfigError> {
        self.inner.tcp_keepalive()
    }
    pub fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_heartbeat_interval(d)
    }
    pub fn heartbeat_interval(&self) -> Result<Duration, ConfigError> {
        self.inner.heartbeat_interval()
    }
    pub fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_heartbeat_ttl(d)
    }
    pub fn heartbeat_ttl(&self) -> Result<Duration, ConfigError> {
        self.inner.heartbeat_ttl()
    }
    pub fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_heartbeat_timeout(d)
    }
    pub fn heartbeat_timeout(&self) -> Result<Duration, ConfigError> {
        self.inner.heartbeat_timeout()
    }
    pub fn set_max_message_size(&self, bytes: i64) -> Result<(), ConfigError> {
        self.inner.set_max_message_size(bytes)
    }
    pub fn max_message_size(&self) -> Result<i64, ConfigError> {
        self.inner.max_message_size()
    }
    pub fn set_backlog(&self, value: i32) -> Result<(), ConfigError> {
        self.inner.set_backlog(value)
    }
    pub fn backlog(&self) -> Result<i32, ConfigError> {
        self.inner.backlog()
    }
    pub fn set_reconnect_interval(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_reconnect_interval(d)
    }
    pub fn reconnect_interval(&self) -> Result<Duration, ConfigError> {
        self.inner.reconnect_interval()
    }
    pub fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_reconnect_interval_max(d)
    }
    pub fn reconnect_interval_max(&self) -> Result<Duration, ConfigError> {
        self.inner.reconnect_interval_max()
    }
    pub fn set_submit_retry_mode(&self, value: SubmitRetryMode) -> Result<(), ConfigError> {
        self.inner.set_submit_retry_mode(value)
    }
    pub fn submit_retry_mode(&self) -> Result<SubmitRetryMode, ConfigError> {
        self.inner.submit_retry_mode()
    }
    pub fn set_submit_retry_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_submit_retry_timeout(d)
    }
    pub fn submit_retry_timeout(&self) -> Result<Duration, ConfigError> {
        self.inner.submit_retry_timeout()
    }
    pub fn set_submit_retry_attempts(&self, value: i32) -> Result<(), ConfigError> {
        self.inner.set_submit_retry_attempts(value)
    }
    pub fn submit_retry_attempts(&self) -> Result<i32, ConfigError> {
        self.inner.submit_retry_attempts()
    }
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

pub struct RouterSocketOptions<'a> {
    inner: &'a dyn RouterSocketOptionRuntime,
}

impl<'a> RouterSocketOptions<'a> {
    pub(crate) fn new(inner: &'a dyn RouterSocketOptionRuntime) -> Self {
        Self { inner }
    }
    pub fn set_mandatory(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_mandatory(enabled)
    }
    pub fn set_probe(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_probe(enabled)
    }
    pub fn set_connect_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError> {
        self.inner.set_connect_routing_id(id)
    }
    pub fn weight(&self) -> Result<u32, ConfigError> {
        self.inner.weight()
    }
    pub fn set_weight(&self, value: u32) -> Result<(), ConfigError> {
        self.inner.set_weight(value)
    }
    pub fn request_timeout(&self) -> Result<Duration, ConfigError> {
        self.inner.request_timeout()
    }
    pub fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError> {
        self.inner.set_request_timeout(value)
    }
}

pub(crate) trait DealerSocketOptionRuntime {
    fn set_probe(&self, enabled: bool) -> Result<(), ConfigError>;
    fn weight(&self) -> Result<u32, ConfigError>;
    fn set_weight(&self, value: u32) -> Result<(), ConfigError>;
    fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError>;
}

pub struct DealerSocketOptions<'a> {
    inner: &'a dyn DealerSocketOptionRuntime,
}

impl<'a> DealerSocketOptions<'a> {
    pub(crate) fn new(inner: &'a dyn DealerSocketOptionRuntime) -> Self {
        Self { inner }
    }
    pub fn set_probe(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_probe(enabled)
    }
    pub fn weight(&self) -> Result<u32, ConfigError> {
        self.inner.weight()
    }
    pub fn set_weight(&self, value: u32) -> Result<(), ConfigError> {
        self.inner.set_weight(value)
    }
    pub fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError> {
        self.inner.set_request_timeout(value)
    }
}

pub(crate) trait StreamSocketOptionRuntime {
    fn set_notify(&self, enabled: bool) -> Result<(), ConfigError>;
    fn notify(&self) -> Result<bool, ConfigError>;
}

pub struct StreamSocketOptions<'a> {
    inner: &'a dyn StreamSocketOptionRuntime,
}

impl<'a> StreamSocketOptions<'a> {
    pub(crate) fn new(inner: &'a dyn StreamSocketOptionRuntime) -> Self {
        Self { inner }
    }
    pub fn set_notify(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_notify(enabled)
    }
    pub fn notify(&self) -> Result<bool, ConfigError> {
        self.inner.notify()
    }
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

pub struct PubSocketOptions<'a> {
    inner: &'a dyn PubSocketOptionRuntime,
}

impl<'a> PubSocketOptions<'a> {
    pub(crate) fn new(inner: &'a dyn PubSocketOptionRuntime) -> Self {
        Self { inner }
    }

    pub fn set_verbose(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_verbose(enabled)
    }
    pub fn set_verboser(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_verboser(enabled)
    }
    pub fn set_no_drop(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_no_drop(enabled)
    }
    pub fn set_manual(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_manual(enabled)
    }
    pub fn manual_last_value(&self) -> Result<bool, ConfigError> {
        self.inner.manual_last_value()
    }
    pub fn set_manual_last_value(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_manual_last_value(enabled)
    }
    pub fn welcome_message(&self) -> Result<Message, ConfigError> {
        self.inner.welcome_message()
    }
    pub fn set_welcome_message(&self, message: &Message) -> Result<(), ConfigError> {
        self.inner.set_welcome_message(message)
    }
    pub fn approve_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        self.inner.approve_subscribe(routing_id)
    }
    pub fn reject_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        self.inner.reject_subscribe(routing_id)
    }
    pub fn topics_count(&self) -> Result<i32, ConfigError> {
        self.inner.topics_count()
    }
}

pub(crate) trait SubSocketOptionRuntime {
    fn topics_count(&self) -> Result<i32, ConfigError>;
}

pub struct SubSocketOptions<'a> {
    inner: &'a dyn SubSocketOptionRuntime,
}

impl<'a> SubSocketOptions<'a> {
    pub(crate) fn new(inner: &'a dyn SubSocketOptionRuntime) -> Self {
        Self { inner }
    }

    pub fn topics_count(&self) -> Result<i32, ConfigError> {
        self.inner.topics_count()
    }
}
use std::time::Duration;

use crate::error::ConfigError;
use crate::message::{Message, RoutingId};
