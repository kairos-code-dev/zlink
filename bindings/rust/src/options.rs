use std::time::Duration;

use crate::error::ConfigError;
use crate::message::RoutingId;
use crate::socket::{
    DealerSocket, PairSocket, PubSocket, RouterSocket, StreamSocket, SubSocket, XPubSocket,
    XSubSocket,
};

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
    fn set_send_hwm(&self, value: i32) -> Result<(), ConfigError>;
    fn send_hwm(&self) -> Result<i32, ConfigError>;
    fn set_recv_hwm(&self, value: i32) -> Result<(), ConfigError>;
    fn recv_hwm(&self) -> Result<i32, ConfigError>;
    fn set_send_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn set_recv_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_rid_duplicate_policy(&self, value: i32) -> Result<(), ConfigError>;
    fn rid_duplicate_policy(&self) -> Result<i32, ConfigError>;
    fn set_connect_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn set_ipv6(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_tcp_nodelay(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ConfigError>;
    fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ConfigError>;
    fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn set_max_msg_size(&self, bytes: i64) -> Result<(), ConfigError>;
    fn set_auto_hwm_msg_unit_bytes(&self, bytes: i32) -> Result<(), ConfigError>;
    fn auto_hwm_msg_unit_bytes(&self) -> Result<i32, ConfigError>;
    fn set_backlog(&self, value: i32) -> Result<(), ConfigError>;
    fn set_reconnect_interval(&self, d: Duration) -> Result<(), ConfigError>;
    fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ConfigError>;
}

macro_rules! impl_common_access {
    ($($ty:ty),+ $(,)?) => {
        $(
            impl CommonSocketOptionAccess for $ty {
                fn set_linger(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_linger(self, d) }
                fn set_send_hwm(&self, value: i32) -> Result<(), ConfigError> { <$ty>::set_send_hwm(self, value) }
                fn send_hwm(&self) -> Result<i32, ConfigError> { <$ty>::send_hwm(self) }
                fn set_recv_hwm(&self, value: i32) -> Result<(), ConfigError> { <$ty>::set_recv_hwm(self, value) }
                fn recv_hwm(&self) -> Result<i32, ConfigError> { <$ty>::recv_hwm(self) }
                fn set_send_timeout(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_send_timeout(self, d) }
                fn set_recv_timeout(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_recv_timeout(self, d) }
                fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError> { <$ty>::set_immediate(self, enabled) }
                fn set_rid_duplicate_policy(&self, value: i32) -> Result<(), ConfigError> { <$ty>::set_rid_duplicate_policy(self, value) }
                fn rid_duplicate_policy(&self) -> Result<i32, ConfigError> { <$ty>::rid_duplicate_policy(self) }
                fn set_connect_timeout(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_connect_timeout(self, d) }
                fn set_ipv6(&self, enabled: bool) -> Result<(), ConfigError> { <$ty>::set_ipv6(self, enabled) }
                fn set_tcp_nodelay(&self, enabled: bool) -> Result<(), ConfigError> { <$ty>::set_tcp_nodelay(self, enabled) }
                fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ConfigError> { <$ty>::set_tcp_keepalive(self, enabled) }
                fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_heartbeat_interval(self, d) }
                fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_heartbeat_ttl(self, d) }
                fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_heartbeat_timeout(self, d) }
                fn set_max_msg_size(&self, bytes: i64) -> Result<(), ConfigError> { <$ty>::set_max_msg_size(self, bytes) }
                fn set_auto_hwm_msg_unit_bytes(&self, bytes: i32) -> Result<(), ConfigError> { <$ty>::set_auto_hwm_msg_unit_bytes(self, bytes) }
                fn auto_hwm_msg_unit_bytes(&self) -> Result<i32, ConfigError> { <$ty>::auto_hwm_msg_unit_bytes(self) }
                fn set_backlog(&self, value: i32) -> Result<(), ConfigError> { <$ty>::set_backlog(self, value) }
                fn set_reconnect_interval(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_reconnect_interval(self, d) }
                fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ConfigError> { <$ty>::set_reconnect_interval_max(self, d) }
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
    pub fn set_recv_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_recv_timeout(d)
    }
    pub fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_immediate(enabled)
    }
    pub fn set_rid_duplicate_policy(&self, value: i32) -> Result<(), ConfigError> {
        self.socket.set_rid_duplicate_policy(value)
    }
    pub fn rid_duplicate_policy(&self) -> Result<i32, ConfigError> {
        self.socket.rid_duplicate_policy()
    }
    pub fn set_connect_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_connect_timeout(d)
    }
    pub fn set_ipv6(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_ipv6(enabled)
    }
    pub fn set_tcp_nodelay(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_tcp_nodelay(enabled)
    }
    pub fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ConfigError> {
        self.socket.set_tcp_keepalive(enabled)
    }
    pub fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_heartbeat_interval(d)
    }
    pub fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_heartbeat_ttl(d)
    }
    pub fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_heartbeat_timeout(d)
    }
    pub fn set_max_msg_size(&self, bytes: i64) -> Result<(), ConfigError> {
        self.socket.set_max_msg_size(bytes)
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
    pub fn set_reconnect_interval(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_reconnect_interval(d)
    }
    pub fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ConfigError> {
        self.socket.set_reconnect_interval_max(d)
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
