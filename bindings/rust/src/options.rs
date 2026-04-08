use std::time::Duration;

use crate::error::ZlinkError;
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
    fn set_linger(&self, d: Duration) -> Result<(), ZlinkError>;
    fn set_send_hwm(&self, value: i32) -> Result<(), ZlinkError>;
    fn send_hwm(&self) -> Result<i32, ZlinkError>;
    fn set_recv_hwm(&self, value: i32) -> Result<(), ZlinkError>;
    fn recv_hwm(&self) -> Result<i32, ZlinkError>;
    fn set_send_timeout(&self, d: Duration) -> Result<(), ZlinkError>;
    fn set_recv_timeout(&self, d: Duration) -> Result<(), ZlinkError>;
    fn set_immediate(&self, enabled: bool) -> Result<(), ZlinkError>;
    fn set_connect_timeout(&self, d: Duration) -> Result<(), ZlinkError>;
    fn set_ipv6(&self, enabled: bool) -> Result<(), ZlinkError>;
    fn set_tcp_nodelay(&self, enabled: bool) -> Result<(), ZlinkError>;
    fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ZlinkError>;
    fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ZlinkError>;
    fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ZlinkError>;
    fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ZlinkError>;
    fn set_max_msg_size(&self, bytes: i64) -> Result<(), ZlinkError>;
    fn set_backlog(&self, value: i32) -> Result<(), ZlinkError>;
    fn set_reconnect_interval(&self, d: Duration) -> Result<(), ZlinkError>;
    fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ZlinkError>;
}

macro_rules! impl_common_access {
    ($($ty:ty),+ $(,)?) => {
        $(
            impl CommonSocketOptionAccess for $ty {
                fn set_linger(&self, d: Duration) -> Result<(), ZlinkError> { self.set_linger(d) }
                fn set_send_hwm(&self, value: i32) -> Result<(), ZlinkError> { self.set_send_hwm(value) }
                fn send_hwm(&self) -> Result<i32, ZlinkError> { self.send_hwm() }
                fn set_recv_hwm(&self, value: i32) -> Result<(), ZlinkError> { self.set_recv_hwm(value) }
                fn recv_hwm(&self) -> Result<i32, ZlinkError> { self.recv_hwm() }
                fn set_send_timeout(&self, d: Duration) -> Result<(), ZlinkError> { self.set_send_timeout(d) }
                fn set_recv_timeout(&self, d: Duration) -> Result<(), ZlinkError> { self.set_recv_timeout(d) }
                fn set_immediate(&self, enabled: bool) -> Result<(), ZlinkError> { self.set_immediate(enabled) }
                fn set_connect_timeout(&self, d: Duration) -> Result<(), ZlinkError> { self.set_connect_timeout(d) }
                fn set_ipv6(&self, enabled: bool) -> Result<(), ZlinkError> { self.set_ipv6(enabled) }
                fn set_tcp_nodelay(&self, enabled: bool) -> Result<(), ZlinkError> { self.set_tcp_nodelay(enabled) }
                fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ZlinkError> { self.set_tcp_keepalive(enabled) }
                fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ZlinkError> { self.set_heartbeat_interval(d) }
                fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ZlinkError> { self.set_heartbeat_ttl(d) }
                fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ZlinkError> { self.set_heartbeat_timeout(d) }
                fn set_max_msg_size(&self, bytes: i64) -> Result<(), ZlinkError> { self.set_max_msg_size(bytes) }
                fn set_backlog(&self, value: i32) -> Result<(), ZlinkError> { self.set_backlog(value) }
                fn set_reconnect_interval(&self, d: Duration) -> Result<(), ZlinkError> { self.set_reconnect_interval(d) }
                fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ZlinkError> { self.set_reconnect_interval_max(d) }
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
impl<'a, T> CommonSocketOptions<'a, T>
where
    T: CommonSocketOptionAccess,
{
    pub fn set_linger(&self, d: Duration) -> Result<(), ZlinkError> {
        self.socket.set_linger(d)
    }
    pub fn set_send_hwm(&self, value: i32) -> Result<(), ZlinkError> {
        self.socket.set_send_hwm(value)
    }
    pub fn send_hwm(&self) -> Result<i32, ZlinkError> {
        self.socket.send_hwm()
    }
    pub fn set_recv_hwm(&self, value: i32) -> Result<(), ZlinkError> {
        self.socket.set_recv_hwm(value)
    }
    pub fn recv_hwm(&self) -> Result<i32, ZlinkError> {
        self.socket.recv_hwm()
    }
    pub fn set_send_timeout(&self, d: Duration) -> Result<(), ZlinkError> {
        self.socket.set_send_timeout(d)
    }
    pub fn set_recv_timeout(&self, d: Duration) -> Result<(), ZlinkError> {
        self.socket.set_recv_timeout(d)
    }
    pub fn set_immediate(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.socket.set_immediate(enabled)
    }
    pub fn set_connect_timeout(&self, d: Duration) -> Result<(), ZlinkError> {
        self.socket.set_connect_timeout(d)
    }
    pub fn set_ipv6(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.socket.set_ipv6(enabled)
    }
    pub fn set_tcp_nodelay(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.socket.set_tcp_nodelay(enabled)
    }
    pub fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.socket.set_tcp_keepalive(enabled)
    }
    pub fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ZlinkError> {
        self.socket.set_heartbeat_interval(d)
    }
    pub fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ZlinkError> {
        self.socket.set_heartbeat_ttl(d)
    }
    pub fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ZlinkError> {
        self.socket.set_heartbeat_timeout(d)
    }
    pub fn set_max_msg_size(&self, bytes: i64) -> Result<(), ZlinkError> {
        self.socket.set_max_msg_size(bytes)
    }
    pub fn set_backlog(&self, value: i32) -> Result<(), ZlinkError> {
        self.socket.set_backlog(value)
    }
    pub fn set_reconnect_interval(&self, d: Duration) -> Result<(), ZlinkError> {
        self.socket.set_reconnect_interval(d)
    }
    pub fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ZlinkError> {
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
    pub fn set_mandatory(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.socket.set_mandatory(enabled)
    }
    pub fn set_handover(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.socket.set_handover(enabled)
    }
    pub fn set_probe(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.socket.set_probe(enabled)
    }
    pub fn set_connect_routing_id(&self, id: &RoutingId) -> Result<(), ZlinkError> {
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
    pub fn set_probe(&self, enabled: bool) -> Result<(), ZlinkError> {
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
    pub fn set_notify(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.socket.set_notify(enabled)
    }
    pub fn notify(&self) -> Result<bool, ZlinkError> {
        self.socket.notify()
    }
}

pub(crate) trait PubSocketOptionAccess {
    fn set_verbose(&self, enabled: bool) -> Result<(), ZlinkError>;
    fn set_verboser(&self, enabled: bool) -> Result<(), ZlinkError>;
    fn set_nodrop(&self, enabled: bool) -> Result<(), ZlinkError>;
    fn set_manual(&self, enabled: bool) -> Result<(), ZlinkError>;
}

impl PubSocketOptionAccess for PubSocket {
    fn set_verbose(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.set_verbose(enabled)
    }
    fn set_verboser(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.set_verboser(enabled)
    }
    fn set_nodrop(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.set_nodrop(enabled)
    }
    fn set_manual(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.set_manual(enabled)
    }
}

impl PubSocketOptionAccess for XPubSocket {
    fn set_verbose(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.set_verbose(enabled)
    }
    fn set_verboser(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.set_verboser(enabled)
    }
    fn set_nodrop(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.set_nodrop(enabled)
    }
    fn set_manual(&self, enabled: bool) -> Result<(), ZlinkError> {
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
impl<'a, T> PubSocketOptions<'a, T>
where
    T: PubSocketOptionAccess,
{
    pub fn set_verbose(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.socket.set_verbose(enabled)
    }
    pub fn set_verboser(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.socket.set_verboser(enabled)
    }
    pub fn set_nodrop(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.socket.set_nodrop(enabled)
    }
    pub fn set_manual(&self, enabled: bool) -> Result<(), ZlinkError> {
        self.socket.set_manual(enabled)
    }
}

pub(crate) trait SubSocketOptionAccess {
    fn topics_count(&self) -> Result<i32, ZlinkError>;
}

impl SubSocketOptionAccess for SubSocket {
    fn topics_count(&self) -> Result<i32, ZlinkError> {
        self.topics_count()
    }
}

impl SubSocketOptionAccess for XSubSocket {
    fn topics_count(&self) -> Result<i32, ZlinkError> {
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
impl<'a, T> SubSocketOptions<'a, T>
where
    T: SubSocketOptionAccess,
{
    pub fn topics_count(&self) -> Result<i32, ZlinkError> {
        self.socket.topics_count()
    }
}
