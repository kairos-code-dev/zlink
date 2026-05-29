// SPDX-License-Identifier: MPL-2.0

use crate::socket_contracts::{PairSocketRuntime, SocketRuntime};
use crate::{
    BindError, CommonSocketOptions, ConfigError, ConnectError, DealerSocketOptions, HandlerError,
    Received, RecvError, RecvFlags,
};
use crate::{Discovery, Empty, RequestOp, RoutingId, SendOp};

/// PAIR socket, a bidirectional one-to-one messaging socket.
pub struct PairSocket {
    pub(crate) inner: Box<dyn PairSocketRuntime>,
}

impl std::panic::UnwindSafe for PairSocket {}
impl std::panic::RefUnwindSafe for PairSocket {}

/// DEALER socket, the asynchronous request/reply client-side socket.
pub struct DealerSocket {
    pub(crate) inner: Box<dyn SocketRuntime>,
}

impl std::panic::UnwindSafe for DealerSocket {}
impl std::panic::RefUnwindSafe for DealerSocket {}

impl PairSocket {
    pub fn send(&self) -> SendOp<Empty> {
        crate::service::socket_send_op(crate::socket::pair_handle(self))
    }

    /// Canonical caller-provided storage recv.
    pub fn recv(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        crate::socket::pair_inner(self).recv(out, flags)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        crate::socket::pair_inner_mut(self).on_send_ready(handler)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(crate::socket::pair_inner(self))
    }

    pub fn close(&mut self) -> Result<(), crate::error::CloseError> {
        crate::socket::pair_inner_mut(self).close()
    }

    pub fn bind(&self, addr: &str) -> Result<(), BindError> {
        crate::socket::pair_inner(self).bind(addr)
    }

    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError> {
        crate::socket::pair_inner(self).unbind(addr)
    }

    pub fn last_endpoint(&self) -> Result<String, ConfigError> {
        crate::socket::pair_inner(self).last_endpoint()
    }

    pub fn set_tls_cert(&self, cert: &str) -> Result<(), ConfigError> {
        crate::socket::pair_inner(self).set_tls_cert(cert)
    }

    pub fn set_tls_key(&self, key: &str) -> Result<(), ConfigError> {
        crate::socket::pair_inner(self).set_tls_key(key)
    }

    pub fn set_tls_ca(&self, ca_cert: &str) -> Result<(), ConfigError> {
        crate::socket::pair_inner(self).set_tls_ca(ca_cert)
    }

    pub fn set_tls_hostname(&self, hostname: &str) -> Result<(), ConfigError> {
        crate::socket::pair_inner(self).set_tls_hostname(hostname)
    }

    pub fn set_tls_trust_system(&self, trust_system: bool) -> Result<(), ConfigError> {
        crate::socket::pair_inner(self).set_tls_trust_system(trust_system)
    }

    pub fn set_tls_server(
        &self,
        cert: &str,
        key: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        crate::socket::pair_inner(self).set_tls_server(cert, key, require_client_cert)
    }

    pub fn set_tls_client(
        &self,
        ca_cert: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        crate::socket::pair_inner(self).set_tls_client(ca_cert, hostname, trust_system)
    }

    pub fn connect(&self, addr: &str) -> Result<(), ConnectError> {
        crate::socket::pair_inner(self).connect(addr)
    }

    pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError> {
        crate::socket::pair_inner(self).disconnect(addr)
    }

    pub fn disconnect_rid(&self, peer_rid: &crate::message::RoutingId) -> Result<(), ConnectError> {
        crate::socket::pair_inner(self).disconnect_rid(peer_rid)
    }
}

impl DealerSocket {
    pub fn send(&self) -> SendOp<Empty> {
        crate::service::socket_send_op(crate::socket::dealer_inner(self).handle)
    }

    pub fn recv(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        crate::socket::dealer_inner(self).recv(out, flags)
    }

    pub fn request(&self) -> RequestOp<Empty> {
        crate::service::dealer_request_op(crate::socket::dealer_inner(self).handle)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        crate::socket::dealer_inner_mut(self).on_send_ready(handler)
    }

    pub fn set_channel_name(&self, channel_name: &str) -> Result<(), ConfigError> {
        crate::socket::dealer_inner(self).set_channel_name(channel_name)
    }

    pub fn channel_name(&self) -> Result<String, ConfigError> {
        crate::socket::dealer_inner(self).channel_name()
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(crate::socket::dealer_inner(self))
    }

    pub fn dealer_options(&self) -> DealerSocketOptions<'_> {
        DealerSocketOptions::new(self)
    }

    pub fn close(&mut self) -> Result<(), crate::error::CloseError> {
        crate::socket::dealer_inner_mut(self).close()
    }

    pub fn bind(&self, addr: &str) -> Result<(), BindError> {
        crate::socket::dealer_inner(self).bind(addr)
    }

    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError> {
        crate::socket::dealer_inner(self).unbind(addr)
    }

    pub fn last_endpoint(&self) -> Result<String, ConfigError> {
        crate::socket::dealer_inner(self).last_endpoint()
    }

    pub fn set_tls_cert(&self, cert: &str) -> Result<(), ConfigError> {
        crate::socket::dealer_inner(self).set_tls_cert(cert)
    }

    pub fn set_tls_key(&self, key: &str) -> Result<(), ConfigError> {
        crate::socket::dealer_inner(self).set_tls_key(key)
    }

    pub fn set_tls_ca(&self, ca_cert: &str) -> Result<(), ConfigError> {
        crate::socket::dealer_inner(self).set_tls_ca(ca_cert)
    }

    pub fn set_tls_hostname(&self, hostname: &str) -> Result<(), ConfigError> {
        crate::socket::dealer_inner(self).set_tls_hostname(hostname)
    }

    pub fn set_tls_trust_system(&self, trust_system: bool) -> Result<(), ConfigError> {
        crate::socket::dealer_inner(self).set_tls_trust_system(trust_system)
    }

    pub fn set_tls_server(
        &self,
        cert: &str,
        key: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        crate::socket::dealer_inner(self).set_tls_server(cert, key, require_client_cert)
    }

    pub fn set_tls_client(
        &self,
        ca_cert: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        crate::socket::dealer_inner(self).set_tls_client(ca_cert, hostname, trust_system)
    }

    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError> {
        crate::socket::dealer_inner(self).attach_discovery(discovery)
    }

    pub fn connect(&self, addr: &str) -> Result<(), ConnectError> {
        crate::socket::dealer_inner(self).connect(addr)
    }

    pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError> {
        crate::socket::dealer_inner(self).disconnect(addr)
    }

    pub fn disconnect_rid(&self, peer_rid: &RoutingId) -> Result<(), ConnectError> {
        crate::socket::dealer_inner(self).disconnect_rid(peer_rid)
    }

    pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError> {
        crate::socket::dealer_inner(self).set_routing_id(id)
    }

    pub fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        crate::socket::dealer_inner(self).routing_id()
    }
}
