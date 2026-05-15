use std::time::Duration;

use crate::error::ConfigError;
use crate::message::{Message, RoutingId};
use crate::socket::{
    DealerSocket, PairSocket, PubSocket, RouterSocket, StreamSocket, SubSocket, XPubSocket,
    XSubSocket,
};

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum RidDuplicatePolicy {
    Reject = 0,
    Handover = 1,
}

pub struct CommonSocketOptions<'a, T> {
    socket: &'a T,
}

impl<'a, T> CommonSocketOptions<'a, T> {
    pub(crate) fn new(socket: &'a T) -> Self {
        Self { socket }
    }
}

pub(crate) trait CommonSocketOptionAccess {
    fn set_linger(&self, d: Duration) -> Result<(), ConfigError>;
    fn linger(&self) -> Result<Duration, ConfigError>;
    fn set_send_hwm(&self, value: i32) -> Result<(), ConfigError>;
    fn send_hwm(&self) -> Result<i32, ConfigError>;
    fn set_recv_hwm(&self, value: i32) -> Result<(), ConfigError>;
    fn recv_hwm(&self) -> Result<i32, ConfigError>;
    fn set_send_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn send_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_recv_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn recv_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError>;
    fn immediate(&self) -> Result<bool, ConfigError>;
    fn set_rid_duplicate_policy(&self, value: i32) -> Result<(), ConfigError>;
    fn rid_duplicate_policy(&self) -> Result<i32, ConfigError>;
    fn set_connect_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn connect_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_ipv6(&self, enabled: bool) -> Result<(), ConfigError>;
    fn ipv6(&self) -> Result<bool, ConfigError>;
    fn set_tcp_nodelay(&self, enabled: bool) -> Result<(), ConfigError>;
    fn tcp_nodelay(&self) -> Result<bool, ConfigError>;
    fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ConfigError>;
    fn tcp_keepalive(&self) -> Result<bool, ConfigError>;
    fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ConfigError>;
    fn heartbeat_interval(&self) -> Result<Duration, ConfigError>;
    fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ConfigError>;
    fn heartbeat_ttl(&self) -> Result<Duration, ConfigError>;
    fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn heartbeat_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_max_msg_size(&self, bytes: i64) -> Result<(), ConfigError>;
    fn max_msg_size(&self) -> Result<i64, ConfigError>;
    fn set_auto_hwm_msg_unit_bytes(&self, bytes: i32) -> Result<(), ConfigError>;
    fn auto_hwm_msg_unit_bytes(&self) -> Result<i32, ConfigError>;
    fn set_backlog(&self, value: i32) -> Result<(), ConfigError>;
    fn backlog(&self) -> Result<i32, ConfigError>;
    fn set_reconnect_interval(&self, d: Duration) -> Result<(), ConfigError>;
    fn reconnect_interval(&self) -> Result<Duration, ConfigError>;
    fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ConfigError>;
    fn reconnect_interval_max(&self) -> Result<Duration, ConfigError>;
}

macro_rules! impl_common_access {
    ($($ty:ty),+ $(,)?) => {
        $(
            impl CommonSocketOptionAccess for $ty {
                fn set_linger(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_linger(self, d) }
                fn linger(&self) -> Result<Duration, ConfigError> { <$ty>::linger(self) }
                fn set_send_hwm(&self, value: i32) -> Result<(), ConfigError> { <$ty>::set_send_hwm(self, value) }
                fn send_hwm(&self) -> Result<i32, ConfigError> { <$ty>::send_hwm(self) }
                fn set_recv_hwm(&self, value: i32) -> Result<(), ConfigError> { <$ty>::set_recv_hwm(self, value) }
                fn recv_hwm(&self) -> Result<i32, ConfigError> { <$ty>::recv_hwm(self) }
                fn set_send_timeout(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_send_timeout(self, d) }
                fn send_timeout(&self) -> Result<Duration, ConfigError> { <$ty>::send_timeout(self) }
                fn set_recv_timeout(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_recv_timeout(self, d) }
                fn recv_timeout(&self) -> Result<Duration, ConfigError> { <$ty>::recv_timeout(self) }
                fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError> { <$ty>::set_immediate(self, enabled) }
                fn immediate(&self) -> Result<bool, ConfigError> { <$ty>::immediate(self) }
                fn set_rid_duplicate_policy(&self, value: i32) -> Result<(), ConfigError> { <$ty>::set_rid_duplicate_policy(self, value) }
                fn rid_duplicate_policy(&self) -> Result<i32, ConfigError> { <$ty>::rid_duplicate_policy(self) }
                fn set_connect_timeout(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_connect_timeout(self, d) }
                fn connect_timeout(&self) -> Result<Duration, ConfigError> { <$ty>::connect_timeout(self) }
                fn set_ipv6(&self, enabled: bool) -> Result<(), ConfigError> { <$ty>::set_ipv6(self, enabled) }
                fn ipv6(&self) -> Result<bool, ConfigError> { <$ty>::ipv6(self) }
                fn set_tcp_nodelay(&self, enabled: bool) -> Result<(), ConfigError> { <$ty>::set_tcp_nodelay(self, enabled) }
                fn tcp_nodelay(&self) -> Result<bool, ConfigError> { <$ty>::tcp_nodelay(self) }
                fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ConfigError> { <$ty>::set_tcp_keepalive(self, enabled) }
                fn tcp_keepalive(&self) -> Result<bool, ConfigError> { <$ty>::tcp_keepalive(self) }
                fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_heartbeat_interval(self, d) }
                fn heartbeat_interval(&self) -> Result<Duration, ConfigError> { <$ty>::heartbeat_interval(self) }
                fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_heartbeat_ttl(self, d) }
                fn heartbeat_ttl(&self) -> Result<Duration, ConfigError> { <$ty>::heartbeat_ttl(self) }
                fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_heartbeat_timeout(self, d) }
                fn heartbeat_timeout(&self) -> Result<Duration, ConfigError> { <$ty>::heartbeat_timeout(self) }
                fn set_max_msg_size(&self, bytes: i64) -> Result<(), ConfigError> { <$ty>::set_max_msg_size(self, bytes) }
                fn max_msg_size(&self) -> Result<i64, ConfigError> { <$ty>::max_msg_size(self) }
                fn set_auto_hwm_msg_unit_bytes(&self, bytes: i32) -> Result<(), ConfigError> { <$ty>::set_auto_hwm_msg_unit_bytes(self, bytes) }
                fn auto_hwm_msg_unit_bytes(&self) -> Result<i32, ConfigError> { <$ty>::auto_hwm_msg_unit_bytes(self) }
                fn set_backlog(&self, value: i32) -> Result<(), ConfigError> { <$ty>::set_backlog(self, value) }
                fn backlog(&self) -> Result<i32, ConfigError> { <$ty>::backlog(self) }
                fn set_reconnect_interval(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_reconnect_interval(self, d) }
                fn reconnect_interval(&self) -> Result<Duration, ConfigError> { <$ty>::reconnect_interval(self) }
                fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_reconnect_interval_max(self, d) }
                fn reconnect_interval_max(&self) -> Result<Duration, ConfigError> { <$ty>::reconnect_interval_max(self) }
            }
        )+
    };
}

impl_common_access!(
    PairSocket,
    DealerSocket,
    RouterSocket,
    PubSocket,
    SubSocket,
    XPubSocket,
    XSubSocket,
    StreamSocket
);

#[allow(private_bounds)]
impl<T> CommonSocketOptions<'_, T>
where
    T: CommonSocketOptionAccess,
{
    pub fn set_linger(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_linger(d)
    }
    pub fn linger(&self) -> Result<Duration, ConfigError> {
        self.socket.linger()
    }
    pub fn set_send_hwm(&self, value: i32) -> Result<(), ConfigError> {
        self.socket.set_send_hwm(value)
    }
    pub fn send_hwm(&self) -> Result<i32, ConfigError> {
        self.socket.send_hwm()
    }
    pub fn set_recv_hwm(&self, value: i32) -> Result<(), ConfigError> {
        self.socket.set_recv_hwm(value)
    }
    pub fn recv_hwm(&self) -> Result<i32, ConfigError> {
        self.socket.recv_hwm()
    }
    pub fn set_send_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_send_timeout(d)
    }
    pub fn send_timeout(&self) -> Result<Duration, ConfigError> {
        self.socket.send_timeout()
    }
    pub fn set_recv_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_recv_timeout(d)
    }
    pub fn recv_timeout(&self) -> Result<Duration, ConfigError> {
        self.socket.recv_timeout()
    }
    pub fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_immediate(enabled)
    }
    pub fn immediate(&self) -> Result<bool, ConfigError> {
        self.socket.immediate()
    }
    pub fn set_rid_duplicate_policy(&self, value: RidDuplicatePolicy) -> Result<(), ConfigError> {
        self.socket.set_rid_duplicate_policy(value as i32)
    }
    pub fn rid_duplicate_policy(&self) -> Result<RidDuplicatePolicy, ConfigError> {
        let raw = self.socket.rid_duplicate_policy()?;
        Ok(if raw == 1 {
            RidDuplicatePolicy::Handover
        } else {
            RidDuplicatePolicy::Reject
        })
    }
    pub fn set_connect_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_connect_timeout(d)
    }
    pub fn connect_timeout(&self) -> Result<Duration, ConfigError> {
        self.socket.connect_timeout()
    }
    pub fn set_ipv6(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_ipv6(enabled)
    }
    pub fn ipv6(&self) -> Result<bool, ConfigError> {
        self.socket.ipv6()
    }
    pub fn set_tcp_nodelay(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_tcp_nodelay(enabled)
    }
    pub fn tcp_nodelay(&self) -> Result<bool, ConfigError> {
        self.socket.tcp_nodelay()
    }
    pub fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_tcp_keepalive(enabled)
    }
    pub fn tcp_keepalive(&self) -> Result<bool, ConfigError> {
        self.socket.tcp_keepalive()
    }
    pub fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_heartbeat_interval(d)
    }
    pub fn heartbeat_interval(&self) -> Result<Duration, ConfigError> {
        self.socket.heartbeat_interval()
    }
    pub fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_heartbeat_ttl(d)
    }
    pub fn heartbeat_ttl(&self) -> Result<Duration, ConfigError> {
        self.socket.heartbeat_ttl()
    }
    pub fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_heartbeat_timeout(d)
    }
    pub fn heartbeat_timeout(&self) -> Result<Duration, ConfigError> {
        self.socket.heartbeat_timeout()
    }
    pub fn set_max_msg_size(&self, bytes: i64) -> Result<(), ConfigError> {
        self.socket.set_max_msg_size(bytes)
    }
    pub fn max_msg_size(&self) -> Result<i64, ConfigError> {
        self.socket.max_msg_size()
    }
    pub fn set_auto_hwm_msg_unit_bytes(&self, bytes: i32) -> Result<(), ConfigError> {
        self.socket.set_auto_hwm_msg_unit_bytes(bytes)
    }
    pub fn auto_hwm_msg_unit_bytes(&self) -> Result<i32, ConfigError> {
        self.socket.auto_hwm_msg_unit_bytes()
    }
    pub fn set_backlog(&self, value: i32) -> Result<(), ConfigError> {
        self.socket.set_backlog(value)
    }
    pub fn backlog(&self) -> Result<i32, ConfigError> {
        self.socket.backlog()
    }
    pub fn set_reconnect_interval(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_reconnect_interval(d)
    }
    pub fn reconnect_interval(&self) -> Result<Duration, ConfigError> {
        self.socket.reconnect_interval()
    }
    pub fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_reconnect_interval_max(d)
    }
    pub fn reconnect_interval_max(&self) -> Result<Duration, ConfigError> {
        self.socket.reconnect_interval_max()
    }
}

pub struct RouterSocketOptions<'a> {
    socket: &'a RouterSocket,
}

impl<'a> RouterSocketOptions<'a> {
    pub(crate) fn new(socket: &'a RouterSocket) -> Self {
        Self { socket }
    }
    pub fn set_mandatory(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_mandatory(enabled)
    }
    pub fn set_probe(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_probe(enabled)
    }
    pub fn set_connect_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError> {
        self.socket.set_connect_routing_id(id)
    }
    pub fn weight(&self) -> Result<u32, ConfigError> {
        self.socket.weight()
    }
    pub fn set_weight(&self, value: u32) -> Result<(), ConfigError> {
        self.socket.set_weight(value)
    }
    pub fn request_timeout(&self) -> Result<Duration, ConfigError> {
        self.socket.request_timeout()
    }
    pub fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError> {
        self.socket.set_request_timeout(value)
    }
}

pub struct DealerSocketOptions<'a> {
    socket: &'a DealerSocket,
}

impl<'a> DealerSocketOptions<'a> {
    pub(crate) fn new(socket: &'a DealerSocket) -> Self {
        Self { socket }
    }
    pub fn set_probe(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_probe(enabled)
    }
    pub fn weight(&self) -> Result<u32, ConfigError> {
        self.socket.weight()
    }
    pub fn set_weight(&self, value: u32) -> Result<(), ConfigError> {
        self.socket.set_weight(value)
    }
    pub fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError> {
        self.socket.set_request_timeout(value)
    }
}

pub struct StreamSocketOptions<'a> {
    socket: &'a StreamSocket,
}

impl<'a> StreamSocketOptions<'a> {
    pub(crate) fn new(socket: &'a StreamSocket) -> Self {
        Self { socket }
    }
    pub fn set_notify(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_notify(enabled)
    }
    pub fn notify(&self) -> Result<bool, ConfigError> {
        self.socket.notify()
    }
}

pub(crate) trait PubSocketOptionAccess {
    fn set_verbose(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_verboser(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_nodrop(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_manual(&self, enabled: bool) -> Result<(), ConfigError>;
    fn manual_last_value(&self) -> Result<bool, ConfigError>;
    fn set_manual_last_value(&self, enabled: bool) -> Result<(), ConfigError>;
    fn welcome_message(&self) -> Result<Message, ConfigError>;
    fn set_welcome_message(&self, message: &Message) -> Result<(), ConfigError>;
    fn approve_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError>;
    fn reject_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError>;
    fn topics_count(&self) -> Result<i32, ConfigError>;
}

impl PubSocketOptionAccess for PubSocket {
    fn set_verbose(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_verbose(enabled)
    }
    fn set_verboser(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_verboser(enabled)
    }
    fn set_nodrop(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_nodrop(enabled)
    }
    fn set_manual(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_manual(enabled)
    }
    fn manual_last_value(&self) -> Result<bool, ConfigError> {
        self.manual_last_value()
    }
    fn set_manual_last_value(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_manual_last_value(enabled)
    }
    fn welcome_message(&self) -> Result<Message, ConfigError> {
        self.welcome_message()
    }
    fn set_welcome_message(&self, message: &Message) -> Result<(), ConfigError> {
        self.set_welcome_message(message)
    }
    fn approve_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        self.approve_subscribe(routing_id)
    }
    fn reject_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        self.reject_subscribe(routing_id)
    }
    fn topics_count(&self) -> Result<i32, ConfigError> {
        self.topics_count()
    }
}

impl PubSocketOptionAccess for XPubSocket {
    fn set_verbose(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_verbose(enabled)
    }
    fn set_verboser(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_verboser(enabled)
    }
    fn set_nodrop(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_nodrop(enabled)
    }
    fn set_manual(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_manual(enabled)
    }
    fn manual_last_value(&self) -> Result<bool, ConfigError> {
        self.manual_last_value()
    }
    fn set_manual_last_value(&self, enabled: bool) -> Result<(), ConfigError> {
        self.set_manual_last_value(enabled)
    }
    fn welcome_message(&self) -> Result<Message, ConfigError> {
        self.welcome_message()
    }
    fn set_welcome_message(&self, message: &Message) -> Result<(), ConfigError> {
        self.set_welcome_message(message)
    }
    fn approve_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        self.approve_subscribe(routing_id)
    }
    fn reject_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        self.reject_subscribe(routing_id)
    }
    fn topics_count(&self) -> Result<i32, ConfigError> {
        self.topics_count()
    }
}

pub struct PubSocketOptions<'a, T> {
    socket: &'a T,
}

impl<'a, T> PubSocketOptions<'a, T> {
    pub(crate) fn new(socket: &'a T) -> Self {
        Self { socket }
    }
}

#[allow(private_bounds)]
impl<T> PubSocketOptions<'_, T>
where
    T: PubSocketOptionAccess,
{
    pub fn set_verbose(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_verbose(enabled)
    }
    pub fn set_verboser(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_verboser(enabled)
    }
    pub fn set_nodrop(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_nodrop(enabled)
    }
    pub fn set_manual(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_manual(enabled)
    }
    pub fn manual_last_value(&self) -> Result<bool, ConfigError> {
        self.socket.manual_last_value()
    }
    pub fn set_manual_last_value(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_manual_last_value(enabled)
    }
    pub fn welcome_message(&self) -> Result<Message, ConfigError> {
        self.socket.welcome_message()
    }
    pub fn set_welcome_message(&self, message: &Message) -> Result<(), ConfigError> {
        self.socket.set_welcome_message(message)
    }
    pub fn approve_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        self.socket.approve_subscribe(routing_id)
    }
    pub fn reject_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError> {
        self.socket.reject_subscribe(routing_id)
    }
    pub fn topics_count(&self) -> Result<i32, ConfigError> {
        self.socket.topics_count()
    }
}

pub(crate) trait SubSocketOptionAccess {
    fn topics_count(&self) -> Result<i32, ConfigError>;
}

impl SubSocketOptionAccess for SubSocket {
    fn topics_count(&self) -> Result<i32, ConfigError> {
        self.topics_count()
    }
}

impl SubSocketOptionAccess for XSubSocket {
    fn topics_count(&self) -> Result<i32, ConfigError> {
        self.topics_count()
    }
}

pub struct SubSocketOptions<'a, T> {
    socket: &'a T,
}

impl<'a, T> SubSocketOptions<'a, T> {
    pub(crate) fn new(socket: &'a T) -> Self {
        Self { socket }
    }
}

#[allow(private_bounds)]
impl<T> SubSocketOptions<'_, T>
where
    T: SubSocketOptionAccess,
{
    pub fn topics_count(&self) -> Result<i32, ConfigError> {
        self.socket.topics_count()
    }
}
