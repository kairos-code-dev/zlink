use std::time::Duration;

use crate::error::ConfigError;
use crate::ffi;
use crate::message::{Message, RoutingId};

use super::socket::SocketInner;

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum RidDuplicatePolicy {
    Reject = 0,
    Handover = 1,
}

/// Public-facing namespace for the option set shared by every socket type.
///
/// Holds a borrow of the underlying [`SocketInner`] and forwards directly to
/// its FFI-backed methods. The previous design routed every call through a
/// per-socket-type trait + macro trampoline; collapsing the indirection cuts
/// the path from "wrapper → trait → per-type forward → SocketInner" to a
/// single hop and removes ~200 lines of mechanical glue.
pub struct CommonSocketOptions<'a> {
    inner: &'a SocketInner,
}

impl<'a> CommonSocketOptions<'a> {
    pub(crate) fn new(inner: &'a SocketInner) -> Self {
        Self { inner }
    }

    pub fn set_linger(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_linger(d)
    }
    pub fn linger(&self) -> Result<Duration, ConfigError> {
        self.inner.linger()
    }
    pub fn set_send_hwm(&self, value: i32) -> Result<(), ConfigError> {
        self.inner.set_send_hwm(value)
    }
    pub fn send_hwm(&self) -> Result<i32, ConfigError> {
        self.inner.send_hwm()
    }
    pub fn set_recv_hwm(&self, value: i32) -> Result<(), ConfigError> {
        self.inner.set_recv_hwm(value)
    }
    pub fn recv_hwm(&self) -> Result<i32, ConfigError> {
        self.inner.recv_hwm()
    }
    pub fn set_send_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_send_timeout(d)
    }
    pub fn send_timeout(&self) -> Result<Duration, ConfigError> {
        self.inner.send_timeout()
    }
    pub fn set_recv_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.inner.set_recv_timeout(d)
    }
    pub fn recv_timeout(&self) -> Result<Duration, ConfigError> {
        self.inner.recv_timeout()
    }
    pub fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_immediate(enabled)
    }
    pub fn immediate(&self) -> Result<bool, ConfigError> {
        self.inner.immediate()
    }
    pub fn set_rid_duplicate_policy(&self, value: RidDuplicatePolicy) -> Result<(), ConfigError> {
        self.inner.set_rid_duplicate_policy(value as i32)
    }
    pub fn rid_duplicate_policy(&self) -> Result<RidDuplicatePolicy, ConfigError> {
        let raw = self.inner.rid_duplicate_policy()?;
        Ok(if raw == 1 {
            RidDuplicatePolicy::Handover
        } else {
            RidDuplicatePolicy::Reject
        })
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
    pub fn set_tcp_nodelay(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_tcp_nodelay(enabled)
    }
    pub fn tcp_nodelay(&self) -> Result<bool, ConfigError> {
        self.inner.tcp_nodelay()
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
    pub fn set_max_msg_size(&self, bytes: i64) -> Result<(), ConfigError> {
        self.inner.set_max_msg_size(bytes)
    }
    pub fn max_msg_size(&self) -> Result<i64, ConfigError> {
        self.inner.max_msg_size()
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
}

pub struct RouterSocketOptions<'a> {
    inner: &'a SocketInner,
}

impl<'a> RouterSocketOptions<'a> {
    pub(crate) fn new(inner: &'a SocketInner) -> Self {
        Self { inner }
    }
    pub fn set_mandatory(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_router_bool_opt(
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_MANDATORY,
            enabled,
        )
    }
    pub fn set_probe(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner
            .set_router_bool_opt(ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_PROBE, enabled)
    }
    pub fn set_connect_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError> {
        self.inner.set_router_bytes_opt(
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
            id.data(),
        )
    }
    pub fn weight(&self) -> Result<u32, ConfigError> {
        self.inner
            .get_router_u32_opt(ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_WEIGHT)
    }
    pub fn set_weight(&self, value: u32) -> Result<(), ConfigError> {
        self.inner
            .set_router_u32_opt(ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_WEIGHT, value)
    }
    pub fn request_timeout(&self) -> Result<Duration, ConfigError> {
        let ms = self
            .inner
            .get_router_i32_opt(ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS)?;
        Ok(Duration::from_millis(ms as u64))
    }
    pub fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError> {
        let millis = value.as_millis().min(i32::MAX as u128) as i32;
        self.inner.set_router_i32_opt(
            ffi::zlink_router_option_t::ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS,
            millis,
        )
    }
}

/// DealerSocketOptions keeps a `&DealerSocket` borrow rather than the bare
/// `SocketInner` used by the other typed-option facades. The dealer caches
/// its weight in an atomic on the socket itself (avoids an FFI round-trip
/// per read), so the wrapper still needs access to that field; FFI-touching
/// methods dispatch through `dealer.inner.*_dealer_*_opt(...)` directly.
pub struct DealerSocketOptions<'a> {
    socket: &'a crate::socket::DealerSocket,
}

impl<'a> DealerSocketOptions<'a> {
    pub(crate) fn new(socket: &'a crate::socket::DealerSocket) -> Self {
        Self { socket }
    }
    pub fn set_probe(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket
            .inner
            .set_dealer_bool_opt(ffi::zlink_dealer_option_t::ZLINK_DEALER_OPT_PROBE, enabled)
    }
    pub fn weight(&self) -> Result<u32, ConfigError> {
        Ok(self.socket.cached_weight())
    }
    pub fn set_weight(&self, value: u32) -> Result<(), ConfigError> {
        self.socket
            .inner
            .set_dealer_u32_opt(ffi::zlink_dealer_option_t::ZLINK_DEALER_OPT_WEIGHT, value)?;
        self.socket.store_cached_weight(value);
        Ok(())
    }
    pub fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError> {
        let millis = value.as_millis().min(i32::MAX as u128) as i32;
        self.socket.inner.set_dealer_i32_opt(
            ffi::zlink_dealer_option_t::ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS,
            millis,
        )
    }
}

pub struct StreamSocketOptions<'a> {
    inner: &'a SocketInner,
}

impl<'a> StreamSocketOptions<'a> {
    pub(crate) fn new(inner: &'a SocketInner) -> Self {
        Self { inner }
    }
    pub fn set_notify(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner
            .set_stream_bool_opt(ffi::zlink_stream_option_t::ZLINK_STREAM_OPT_NOTIFY, enabled)
    }
    pub fn notify(&self) -> Result<bool, ConfigError> {
        self.inner
            .get_stream_bool_opt(ffi::zlink_stream_option_t::ZLINK_STREAM_OPT_NOTIFY)
    }
}

/// Public-facing namespace for the option set shared by PUB-style sockets.
///
/// Holds a borrow of the underlying [`SocketInner`] and forwards directly to
/// its FFI-backed methods, mirroring the same single-hop layout we adopted
/// for [`CommonSocketOptions`]. The previous design routed every call
/// through a `PubSocketOptionAccess` trait with per-socket impl blocks; that
/// indirection is now gone.
pub struct PubSocketOptions<'a> {
    inner: &'a SocketInner,
}

impl<'a> PubSocketOptions<'a> {
    pub(crate) fn new(inner: &'a SocketInner) -> Self {
        Self { inner }
    }

    pub fn set_verbose(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_pub_bool_opt(
            crate::ffi::zlink_pub_option_t::ZLINK_PUB_OPT_VERBOSE,
            enabled,
        )
    }
    pub fn set_verboser(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_pub_bool_opt(
            crate::ffi::zlink_pub_option_t::ZLINK_PUB_OPT_VERBOSER,
            enabled,
        )
    }
    pub fn set_nodrop(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_pub_bool_opt(
            crate::ffi::zlink_pub_option_t::ZLINK_PUB_OPT_NODROP,
            enabled,
        )
    }
    pub fn set_manual(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_pub_bool_opt(
            crate::ffi::zlink_pub_option_t::ZLINK_PUB_OPT_MANUAL,
            enabled,
        )
    }
    pub fn manual_last_value(&self) -> Result<bool, ConfigError> {
        self.inner
            .get_pub_bool_opt(crate::ffi::zlink_pub_option_t::ZLINK_PUB_OPT_MANUAL_LAST_VALUE)
    }
    pub fn set_manual_last_value(&self, enabled: bool) -> Result<(), ConfigError> {
        self.inner.set_pub_bool_opt(
            crate::ffi::zlink_pub_option_t::ZLINK_PUB_OPT_MANUAL_LAST_VALUE,
            enabled,
        )
    }
    pub fn welcome_message(&self) -> Result<Message, ConfigError> {
        self.inner
            .get_pub_message_opt(crate::ffi::zlink_pub_option_t::ZLINK_PUB_OPT_WELCOME_MSG)
    }
    pub fn set_welcome_message(&self, message: &Message) -> Result<(), ConfigError> {
        self.inner.set_pub_bytes_opt(
            crate::ffi::zlink_pub_option_t::ZLINK_PUB_OPT_WELCOME_MSG,
            message.as_bytes(),
        )
    }
    pub fn approve_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        self.inner.set_pub_bytes_opt(
            crate::ffi::zlink_pub_option_t::ZLINK_PUB_OPT_APPROVE_SUBSCRIBE,
            routing_id.data(),
        )
    }
    pub fn reject_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        self.inner.set_pub_bytes_opt(
            crate::ffi::zlink_pub_option_t::ZLINK_PUB_OPT_REJECT_SUBSCRIBE,
            routing_id.data(),
        )
    }
    pub fn topics_count(&self) -> Result<i32, ConfigError> {
        self.inner
            .get_pub_int_opt(crate::ffi::zlink_pub_option_t::ZLINK_PUB_OPT_TOPICS_COUNT)
    }
}

/// SUB-style options shared by SubSocket and XSubSocket.
pub struct SubSocketOptions<'a> {
    inner: &'a SocketInner,
}

impl<'a> SubSocketOptions<'a> {
    pub(crate) fn new(inner: &'a SocketInner) -> Self {
        Self { inner }
    }

    pub fn topics_count(&self) -> Result<i32, ConfigError> {
        self.inner
            .get_sub_int_opt(crate::ffi::zlink_sub_option_t::ZLINK_SUB_OPT_TOPICS_COUNT)
    }
}
