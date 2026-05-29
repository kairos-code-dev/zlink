// SPDX-License-Identifier: MPL-2.0

use crate::socket_contracts::SocketRuntime;
use crate::{
    BindError, CommonSocketOptions, ConfigError, ConnectError, Discovery, HandlerError, Received,
    RecvError, RecvFlags, RouterSocketOptions,
};
use crate::{Empty, ReplyOp, RequestOp, RoutingId, SendOp};

/// ROUTER socket, the asynchronous request/reply server-side socket.
pub struct RouterSocket {
    pub(crate) inner: Box<dyn SocketRuntime>,
}

impl std::panic::UnwindSafe for RouterSocket {}
impl std::panic::RefUnwindSafe for RouterSocket {}

impl RouterSocket {
    pub fn send(&self, target: &RoutingId) -> SendOp<Empty> {
        crate::service::socket_send_to_op(crate::socket::router_inner(self).handle, *target)
    }

    pub fn recv(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        match crate::socket::recv_router_once(
            crate::socket::router_inner(self).handle,
            flags.bits(),
        )? {
            Some(received) => {
                out.adopt_from(received);
                Ok(true)
            }
            None => Ok(false),
        }
    }

    pub fn request(&self, peer_rid: &RoutingId) -> RequestOp<Empty> {
        crate::service::router_request_op(crate::socket::router_inner(self).handle, *peer_rid)
    }

    pub fn reply(&self, rid: &RoutingId, request_seq: u64) -> ReplyOp<Empty> {
        crate::service::router_reply_op(crate::socket::router_inner(self).handle, *rid, request_seq)
    }

    pub fn send_to_spot(
        &self,
        dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId,
    ) -> SendOp<Empty> {
        crate::service::router_send_to_spot_op(
            crate::socket::router_inner(self).handle,
            *dest_node_rid,
            *dest_spot_rid,
        )
    }

    pub fn request_to_spot(
        &self,
        dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId,
    ) -> RequestOp<Empty> {
        crate::service::router_request_to_spot_op(
            crate::socket::router_inner(self).handle,
            *dest_node_rid,
            *dest_spot_rid,
        )
    }

    pub fn reply_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
    ) -> ReplyOp<Empty> {
        crate::service::router_reply_to_spot_op(
            crate::socket::router_inner(self).handle,
            dest_node_rid,
            dest_spot_rid,
            request_seq,
        )
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        crate::socket::router_inner_mut(self).on_send_ready(handler)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(crate::socket::router_inner(self))
    }

    pub fn router_options(&self) -> RouterSocketOptions<'_> {
        RouterSocketOptions::new(crate::socket::router_inner(self))
    }

    pub fn close(&mut self) -> Result<(), crate::error::CloseError> {
        crate::socket::router_inner_mut(self).close()
    }

    pub fn bind(&self, addr: &str) -> Result<(), BindError> {
        crate::socket::router_inner(self).bind(addr)
    }

    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError> {
        crate::socket::router_inner(self).unbind(addr)
    }

    pub fn last_endpoint(&self) -> Result<String, ConfigError> {
        crate::socket::router_inner(self).last_endpoint()
    }

    pub fn set_tls_cert(&self, cert: &str) -> Result<(), ConfigError> {
        crate::socket::router_inner(self).set_tls_cert(cert)
    }

    pub fn set_tls_key(&self, key: &str) -> Result<(), ConfigError> {
        crate::socket::router_inner(self).set_tls_key(key)
    }

    pub fn set_tls_ca(&self, ca_cert: &str) -> Result<(), ConfigError> {
        crate::socket::router_inner(self).set_tls_ca(ca_cert)
    }

    pub fn set_tls_hostname(&self, hostname: &str) -> Result<(), ConfigError> {
        crate::socket::router_inner(self).set_tls_hostname(hostname)
    }

    pub fn set_tls_trust_system(&self, trust_system: bool) -> Result<(), ConfigError> {
        crate::socket::router_inner(self).set_tls_trust_system(trust_system)
    }

    pub fn set_tls_server(
        &self,
        cert: &str,
        key: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        crate::socket::router_inner(self).set_tls_server(cert, key, require_client_cert)
    }

    pub fn set_tls_client(
        &self,
        ca_cert: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        crate::socket::router_inner(self).set_tls_client(ca_cert, hostname, trust_system)
    }

    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError> {
        crate::socket::router_inner(self).attach_discovery(discovery)
    }

    pub fn connect(&self, addr: &str) -> Result<(), ConnectError> {
        crate::socket::router_inner(self).connect(addr)
    }

    pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError> {
        crate::socket::router_inner(self).disconnect(addr)
    }

    pub fn disconnect_rid(&self, peer_rid: &RoutingId) -> Result<(), ConnectError> {
        crate::socket::router_inner(self).disconnect_rid(peer_rid)
    }

    pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError> {
        crate::socket::router_inner(self).set_routing_id(id)
    }

    pub fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        crate::socket::router_inner(self).routing_id()
    }
}
