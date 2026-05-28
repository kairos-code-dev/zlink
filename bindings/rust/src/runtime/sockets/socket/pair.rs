use std::ffi::c_void;

use crate::core_context::Context;
use crate::domain::Received;
use crate::error::{BindError, ConfigError, ConnectError, HandlerError, RecvError};
use crate::ffi;
use crate::flags::CommonSocketOptions;
use crate::flags::RecvFlags;
use crate::socket_contracts::{PairSocket, PairSocketRuntime};
use crate::spot_operations::Empty;
use crate::spot_operations::SendOp;

use super::SocketInner;

struct NativePairSocket {
    inner: SocketInner,
}

impl PairSocketRuntime for NativePairSocket {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

impl PairSocket {
    pub(crate) fn new(ctx: &Context) -> Result<Self, ConfigError> {
        Ok(Self {
            inner: Box::new(NativePairSocket {
                inner: SocketInner::create(ctx, ffi::zlink_socket_type_t::ZLINK_SOCKET_PAIR)?,
            }),
        })
    }

    pub fn send(&self) -> SendOp<Empty> {
        crate::service::socket_send_op(pair_handle(self))
    }

    /// Canonical caller-provided storage recv.
    ///
    /// See `doc/spec/bindings/README.md` "Canonical Recv: Caller-Provided
    /// Storage". `Ok(true)` on success, `Ok(false)` when
    /// [`RecvFlags::DONT_WAIT`] finds no data, `Err(_)` on hard error.
    pub fn recv(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        pair_inner(self).recv(out, flags)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        pair_inner_mut(self).on_send_ready(handler)
    }

    pub fn common_options(&self) -> CommonSocketOptions<'_> {
        CommonSocketOptions::new(pair_inner(self))
    }

    pub fn close(&mut self) -> Result<(), crate::error::CloseError> {
        pair_inner_mut(self).close()
    }

    pub fn bind(&self, addr: &str) -> Result<(), BindError> {
        pair_inner(self).bind(addr)
    }

    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError> {
        pair_inner(self).unbind(addr)
    }

    pub fn last_endpoint(&self) -> Result<String, ConfigError> {
        pair_inner(self).last_endpoint()
    }

    pub fn set_tls_cert(&self, cert: &str) -> Result<(), ConfigError> {
        pair_inner(self).set_tls_cert(cert)
    }

    pub fn set_tls_key(&self, key: &str) -> Result<(), ConfigError> {
        pair_inner(self).set_tls_key(key)
    }

    pub fn set_tls_ca(&self, ca_cert: &str) -> Result<(), ConfigError> {
        pair_inner(self).set_tls_ca(ca_cert)
    }

    pub fn set_tls_hostname(&self, hostname: &str) -> Result<(), ConfigError> {
        pair_inner(self).set_tls_hostname(hostname)
    }

    pub fn set_tls_trust_system(&self, trust_system: bool) -> Result<(), ConfigError> {
        pair_inner(self).set_tls_trust_system(trust_system)
    }

    pub fn set_tls_server(
        &self,
        cert: &str,
        key: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        pair_inner(self).set_tls_server(cert, key, require_client_cert)
    }

    pub fn set_tls_client(
        &self,
        ca_cert: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        pair_inner(self).set_tls_client(ca_cert, hostname, trust_system)
    }

    pub fn connect(&self, addr: &str) -> Result<(), ConnectError> {
        pair_inner(self).connect(addr)
    }

    pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError> {
        pair_inner(self).disconnect(addr)
    }

    pub fn disconnect_rid(&self, peer_rid: &crate::message::RoutingId) -> Result<(), ConnectError> {
        pair_inner(self).disconnect_rid(peer_rid)
    }
}

pub(crate) fn pair_inner(socket: &PairSocket) -> &SocketInner {
    &socket
        .inner
        .as_any()
        .downcast_ref::<NativePairSocket>()
        .expect("zlink native pair socket")
        .inner
}

pub(crate) fn pair_inner_mut(socket: &mut PairSocket) -> &mut SocketInner {
    &mut socket
        .inner
        .as_any_mut()
        .downcast_mut::<NativePairSocket>()
        .expect("zlink native pair socket")
        .inner
}

pub(crate) fn pair_handle(socket: &PairSocket) -> *mut c_void {
    pair_inner(socket).handle
}
