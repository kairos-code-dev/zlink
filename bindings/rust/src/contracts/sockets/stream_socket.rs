// SPDX-License-Identifier: MPL-2.0

use crate::socket_contracts::SocketRuntime;
use crate::spot_operations::{ActorBindOp, ActorUnbindOp};
use crate::{
    ActorRef, BindError, CommonSocketOptions, ConfigError, ConnectError, Empty, HandlerError,
    Message, Received, RecvError, RecvFlags, RoutingId, SendOp, SpotNode, StreamSocketOptions,
};

/// STREAM socket, the raw transport-level socket with routing ids.
pub struct StreamSocket {
    pub(crate) inner: Box<dyn SocketRuntime>,
}

impl std::panic::UnwindSafe for StreamSocket {}
impl std::panic::RefUnwindSafe for StreamSocket {}

impl StreamSocket {
    pub fn send(&self, target: &RoutingId) -> SendOp<Empty> {
        crate::service::socket_send_to_op(crate::socket::stream_inner(self).handle, *target)
    }

    /// Canonical caller-provided storage recv.
    pub fn recv(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        let received = crate::socket::stream_inner(self).recv(out, flags)?;
        if received {
            if let Some(routing_id) = out.routing_id {
                out.set_router_send_context(crate::socket::stream_inner(self).handle, routing_id);
            }
        }
        Ok(received)
    }

    pub fn disconnect_rid(&self, peer_rid: &RoutingId) -> Result<(), ConnectError> {
        crate::socket::stream_inner(self).disconnect_rid(peer_rid)
    }

    pub fn on_packet<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn(RoutingId, Message, Message) + Send + 'static,
    {
        crate::socket::stream_on_packet(self, handler)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        crate::socket::stream_inner_mut(self).on_send_ready(handler)
    }

    /// Attach this STREAM socket to the session owner SpotNode used for
    /// ActorGateway session relay.
    pub fn attach_actor_gateway(&self, node: &SpotNode) -> Result<(), ConfigError> {
        crate::socket::stream_attach_actor_gateway(self, node)
    }

    /// Async Actor bind (operation builder).
    pub fn bind_actor(&self, session_rid: &RoutingId, actor: &ActorRef) -> ActorBindOp<Empty> {
        crate::socket::stream_bind_actor(self, session_rid, actor)
    }

    /// Async Actor unbind (operation builder).
    pub fn unbind_actor(&self, session_rid: &RoutingId, actor_id: &str) -> ActorUnbindOp<Empty> {
        crate::socket::stream_unbind_actor(self, session_rid, actor_id)
    }

    /// Session-bound relay send (operation builder).
    pub fn send_bound_actor(&self, session_rid: &RoutingId, actor_id: &str) -> SendOp<Empty> {
        crate::socket::stream_send_bound_actor(self, session_rid, actor_id)
    }

    pub fn bound_actors(&self, session_rid: &RoutingId) -> Result<Vec<ActorRef>, ConfigError> {
        crate::socket::stream_bound_actors(self, session_rid)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(crate::socket::stream_inner(self))
    }

    pub fn stream_options(&self) -> StreamSocketOptions<'_> {
        StreamSocketOptions::new(crate::socket::stream_inner(self))
    }

    pub fn close(&mut self) -> Result<(), crate::error::CloseError> {
        crate::socket::stream_inner_mut(self).close()
    }

    pub fn bind(&self, addr: &str) -> Result<(), BindError> {
        crate::socket::stream_inner(self).bind(addr)
    }

    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError> {
        crate::socket::stream_inner(self).unbind(addr)
    }

    pub fn last_endpoint(&self) -> Result<String, ConfigError> {
        crate::socket::stream_inner(self).last_endpoint()
    }

    pub fn set_tls_cert(&self, cert: &str) -> Result<(), ConfigError> {
        crate::socket::stream_inner(self).set_tls_cert(cert)
    }

    pub fn set_tls_key(&self, key: &str) -> Result<(), ConfigError> {
        crate::socket::stream_inner(self).set_tls_key(key)
    }

    pub fn set_tls_ca(&self, ca_cert: &str) -> Result<(), ConfigError> {
        crate::socket::stream_inner(self).set_tls_ca(ca_cert)
    }

    pub fn set_tls_hostname(&self, hostname: &str) -> Result<(), ConfigError> {
        crate::socket::stream_inner(self).set_tls_hostname(hostname)
    }

    pub fn set_tls_trust_system(&self, trust_system: bool) -> Result<(), ConfigError> {
        crate::socket::stream_inner(self).set_tls_trust_system(trust_system)
    }

    pub fn set_tls_server(
        &self,
        cert: &str,
        key: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        crate::socket::stream_inner(self).set_tls_server(cert, key, require_client_cert)
    }

    pub fn set_tls_client(
        &self,
        ca_cert: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        crate::socket::stream_inner(self).set_tls_client(ca_cert, hostname, trust_system)
    }

    pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError> {
        crate::socket::stream_inner(self).set_routing_id(id)
    }

    pub fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        crate::socket::stream_inner(self).routing_id()
    }
}
