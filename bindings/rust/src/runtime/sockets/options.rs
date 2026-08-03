use std::time::Duration;

use crate::error::ConfigError;
use crate::ffi;
use crate::flags::{RidDuplicatePolicy, SubmitRetryMode};
use crate::message::{Message, RoutingId};
use crate::runtime_bridge::{
    CommonSocketOptionRuntime, DealerSocketOptionRuntime, PubSocketOptionRuntime,
    RouterSocketOptionRuntime, StreamSocketOptionRuntime, SubSocketOptionRuntime,
};
use crate::socket::SocketInner;
use crate::socket_contracts::DealerSocket;

impl CommonSocketOptionRuntime for SocketInner {
    fn set_linger(&self, d: Duration) -> Result<(), ConfigError> {
        self.set_linger(d)
    }
    fn linger(&self) -> Result<Duration, ConfigError> {
        self.linger()
    }
    fn set_send_high_water_mark(&self, value: u64) -> Result<(), ConfigError> {
        self.set_send_high_water_mark(value)
    }
    fn send_high_water_mark(&self) -> Result<u64, ConfigError> {
        self.send_high_water_mark()
    }
    fn set_receive_high_water_mark(&self, value: u64) -> Result<(), ConfigError> {
        self.set_receive_high_water_mark(value)
    }
    fn receive_high_water_mark(&self) -> Result<u64, ConfigError> {
        self.receive_high_water_mark()
    }
    fn set_send_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.set_send_timeout(d)
    }
    fn send_timeout(&self) -> Result<Duration, ConfigError> {
        self.send_timeout()
    }
    fn set_receive_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.set_receive_timeout(d)
    }
    fn receive_timeout(&self) -> Result<Duration, ConfigError> {
        self.receive_timeout()
    }
    fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_immediate(enabled)
    }
    fn immediate(&self) -> Result<bool, ConfigError> {
        self.immediate()
    }
    fn set_rid_duplicate_policy(&self, value: RidDuplicatePolicy) -> Result<(), ConfigError> {
        self.set_rid_duplicate_policy(value as i32)
    }
    fn rid_duplicate_policy(&self) -> Result<RidDuplicatePolicy, ConfigError> {
        let raw = self.rid_duplicate_policy()?;
        Ok(if raw == 1 {
            RidDuplicatePolicy::Handover
        } else {
            RidDuplicatePolicy::Reject
        })
    }
    fn set_connect_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.set_connect_timeout(d)
    }
    fn connect_timeout(&self) -> Result<Duration, ConfigError> {
        self.connect_timeout()
    }
    fn set_ipv6(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_ipv6(enabled)
    }
    fn ipv6(&self) -> Result<bool, ConfigError> {
        self.ipv6()
    }
    fn set_tcp_no_delay(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_tcp_no_delay(enabled)
    }
    fn tcp_no_delay(&self) -> Result<bool, ConfigError> {
        self.tcp_no_delay()
    }
    fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_tcp_keepalive(enabled)
    }
    fn tcp_keepalive(&self) -> Result<bool, ConfigError> {
        self.tcp_keepalive()
    }
    fn set_max_message_size(&self, bytes: i64) -> Result<(), ConfigError> {
        self.set_max_message_size(bytes)
    }
    fn max_message_size(&self) -> Result<i64, ConfigError> {
        self.max_message_size()
    }
    fn set_backlog(&self, value: i32) -> Result<(), ConfigError> {
        self.set_backlog(value)
    }
    fn backlog(&self) -> Result<i32, ConfigError> {
        self.backlog()
    }
    fn set_reconnect_interval(&self, d: Duration) -> Result<(), ConfigError> {
        self.set_reconnect_interval(d)
    }
    fn reconnect_interval(&self) -> Result<Duration, ConfigError> {
        self.reconnect_interval()
    }
    fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ConfigError> {
        self.set_reconnect_interval_max(d)
    }
    fn reconnect_interval_max(&self) -> Result<Duration, ConfigError> {
        self.reconnect_interval_max()
    }
    fn set_submit_retry_mode(&self, value: SubmitRetryMode) -> Result<(), ConfigError> {
        self.set_submit_retry_mode(value as i32)
    }
    fn submit_retry_mode(&self) -> Result<SubmitRetryMode, ConfigError> {
        Ok(match self.submit_retry_mode()? {
            1 => SubmitRetryMode::LocalFailure,
            _ => SubmitRetryMode::Off,
        })
    }
    fn set_submit_retry_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.set_submit_retry_timeout(d)
    }
    fn submit_retry_timeout(&self) -> Result<Duration, ConfigError> {
        self.submit_retry_timeout()
    }
    fn set_submit_retry_attempts(&self, value: i32) -> Result<(), ConfigError> {
        self.set_submit_retry_attempts(value)
    }
    fn submit_retry_attempts(&self) -> Result<i32, ConfigError> {
        self.submit_retry_attempts()
    }
}

impl RouterSocketOptionRuntime for SocketInner {
    fn set_mandatory(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_router_bool_opt(
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_MANDATORY,
            enabled,
        )
    }
    fn set_probe(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_router_bool_opt(ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_PROBE, enabled)
    }
    fn set_connect_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError> {
        self.set_router_bytes_opt(
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
            id.data(),
        )
    }
    fn weight(&self) -> Result<u32, ConfigError> {
        self.get_router_u32_opt(ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_WEIGHT)
    }
    fn set_weight(&self, value: u32) -> Result<(), ConfigError> {
        self.set_router_u32_opt(ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_WEIGHT, value)
    }
    fn request_timeout(&self) -> Result<Duration, ConfigError> {
        let ms = self
            .get_router_i32_opt(ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS)?;
        Ok(Duration::from_millis(ms as u64))
    }
    fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError> {
        let millis = value.as_millis().min(i32::MAX as u128) as i32;
        self.set_router_i32_opt(
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS,
            millis,
        )
    }
}

impl DealerSocketOptionRuntime for DealerSocket {
    fn set_probe(&self, enabled: bool) -> Result<(), ConfigError> {
        crate::socket::dealer_inner(self)
            .set_dealer_bool_opt(ffi::zlink_dealer_option_t::ZLINK_DEALER_OPT_PROBE, enabled)
    }
    fn weight(&self) -> Result<u32, ConfigError> {
        crate::socket::dealer_inner(self)
            .get_dealer_u32_opt(ffi::zlink_dealer_option_t::ZLINK_DEALER_OPT_WEIGHT)
    }
    fn set_weight(&self, value: u32) -> Result<(), ConfigError> {
        crate::socket::dealer_inner(self)
            .set_dealer_u32_opt(ffi::zlink_dealer_option_t::ZLINK_DEALER_OPT_WEIGHT, value)
    }
    fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError> {
        let millis = value.as_millis().min(i32::MAX as u128) as i32;
        crate::socket::dealer_inner(self).set_dealer_i32_opt(
            ffi::zlink_dealer_option_t::ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS,
            millis,
        )
    }
}

impl StreamSocketOptionRuntime for SocketInner {
    fn set_notify(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_stream_bool_opt(ffi::zlink_stream_option_t::ZLINK_STREAM_OPT_NOTIFY, enabled)
    }
    fn notify(&self) -> Result<bool, ConfigError> {
        self.get_stream_bool_opt(ffi::zlink_stream_option_t::ZLINK_STREAM_OPT_NOTIFY)
    }
}

impl PubSocketOptionRuntime for SocketInner {
    fn set_verbose(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_pub_bool_opt(ffi::zlink_pub_option_t::ZLINK_PUB_OPT_VERBOSE, enabled)
    }
    fn set_verboser(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_pub_bool_opt(ffi::zlink_pub_option_t::ZLINK_PUB_OPT_VERBOSER, enabled)
    }
    fn set_no_drop(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_pub_bool_opt(ffi::zlink_pub_option_t::ZLINK_PUB_OPT_NODROP, enabled)
    }
    fn set_manual(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_pub_bool_opt(ffi::zlink_pub_option_t::ZLINK_PUB_OPT_MANUAL, enabled)
    }
    fn manual_last_value(&self) -> Result<bool, ConfigError> {
        self.get_pub_bool_opt(ffi::zlink_pub_option_t::ZLINK_PUB_OPT_MANUAL_LAST_VALUE)
    }
    fn set_manual_last_value(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_pub_bool_opt(
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_MANUAL_LAST_VALUE,
            enabled,
        )
    }
    fn welcome_message(&self) -> Result<Message, ConfigError> {
        self.get_pub_message_opt(ffi::zlink_pub_option_t::ZLINK_PUB_OPT_WELCOME_MSG)
    }
    fn set_welcome_message(&self, message: &Message) -> Result<(), ConfigError> {
        self.set_pub_bytes_opt(
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_WELCOME_MSG,
            message.as_bytes(),
        )
    }
    fn approve_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        self.set_pub_bytes_opt(
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_APPROVE_SUBSCRIBE,
            routing_id.data(),
        )
    }
    fn reject_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        self.set_pub_bytes_opt(
            ffi::zlink_pub_option_t::ZLINK_PUB_OPT_REJECT_SUBSCRIBE,
            routing_id.data(),
        )
    }
    fn topics_count(&self) -> Result<i32, ConfigError> {
        self.get_pub_int_opt(ffi::zlink_pub_option_t::ZLINK_PUB_OPT_TOPICS_COUNT)
    }
}

impl SubSocketOptionRuntime for SocketInner {
    fn topics_count(&self) -> Result<i32, ConfigError> {
        self.get_sub_int_opt(ffi::zlink_sub_option_t::ZLINK_SUB_OPT_TOPICS_COUNT)
    }
}
