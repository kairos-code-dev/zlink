/// Bind, unbind, last_endpoint, TLS, linger, and transport-level options.
/// Applied to all socket types.
macro_rules! impl_base_socket {
    ($ty:ident, $inner:path, $inner_mut:path) => {
        #[allow(dead_code)]
        impl $ty {
            pub fn close(&mut self) -> Result<(), crate::error::CloseError> {
                $inner_mut(self).close()
            }
            pub fn bind(&self, addr: &str) -> Result<(), crate::error::BindError> {
                $inner(self).bind(addr)
            }
            pub fn unbind(&self, addr: &str) -> Result<(), crate::error::ConnectError> {
                $inner(self).unbind(addr)
            }
            pub fn last_endpoint(&self) -> Result<String, crate::error::ConfigError> {
                $inner(self).last_endpoint()
            }
            pub fn set_tls_cert(&self, cert: &str) -> Result<(), crate::error::ConfigError> {
                $inner(self).set_tls_cert(cert)
            }
            pub fn set_tls_key(&self, key: &str) -> Result<(), crate::error::ConfigError> {
                $inner(self).set_tls_key(key)
            }
            pub fn set_tls_ca(&self, ca_cert: &str) -> Result<(), crate::error::ConfigError> {
                $inner(self).set_tls_ca(ca_cert)
            }
            pub fn set_tls_hostname(
                &self,
                hostname: &str,
            ) -> Result<(), crate::error::ConfigError> {
                $inner(self).set_tls_hostname(hostname)
            }
            pub fn set_tls_trust_system(
                &self,
                trust_system: bool,
            ) -> Result<(), crate::error::ConfigError> {
                $inner(self).set_tls_trust_system(trust_system)
            }
            pub fn set_tls_server(
                &self,
                cert: &str,
                key: &str,
                require_client_cert: bool,
            ) -> Result<(), crate::error::ConfigError> {
                $inner(self).set_tls_server(cert, key, require_client_cert)
            }
            pub fn set_tls_client(
                &self,
                ca_cert: &str,
                hostname: &str,
                trust_system: bool,
            ) -> Result<(), crate::error::ConfigError> {
                $inner(self).set_tls_client(ca_cert, hostname, trust_system)
            }
            pub(crate) fn handle(&self) -> *mut std::ffi::c_void {
                $inner(self).handle
            }
        }
    };
    ($ty:ident) => {
        #[allow(dead_code)]
        impl $ty {
            pub fn close(&mut self) -> Result<(), crate::error::CloseError> {
                self.inner.close()
            }
            pub fn bind(&self, addr: &str) -> Result<(), crate::error::BindError> {
                self.inner.bind(addr)
            }
            pub fn unbind(&self, addr: &str) -> Result<(), crate::error::ConnectError> {
                self.inner.unbind(addr)
            }
            pub fn last_endpoint(&self) -> Result<String, crate::error::ConfigError> {
                self.inner.last_endpoint()
            }
            pub fn set_tls_cert(&self, cert: &str) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_cert(cert)
            }
            pub fn set_tls_key(&self, key: &str) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_key(key)
            }
            pub fn set_tls_ca(&self, ca_cert: &str) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_ca(ca_cert)
            }
            pub fn set_tls_hostname(
                &self,
                hostname: &str,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_hostname(hostname)
            }
            pub fn set_tls_trust_system(
                &self,
                trust_system: bool,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_trust_system(trust_system)
            }
            pub fn set_tls_server(
                &self,
                cert: &str,
                key: &str,
                require_client_cert: bool,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_server(cert, key, require_client_cert)
            }
            pub fn set_tls_client(
                &self,
                ca_cert: &str,
                hostname: &str,
                trust_system: bool,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.set_tls_client(ca_cert, hostname, trust_system)
            }
            pub(crate) fn handle(&self) -> *mut std::ffi::c_void {
                self.inner.handle
            }
        }
    };
}

/// Routing-id get/set for DEALER and ROUTER sockets.
macro_rules! impl_routing_id_options {
    ($ty:ident, $inner:path) => {
        #[allow(dead_code)]
        impl $ty {
            pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), crate::error::ConfigError> {
                $inner(self).set_routing_id(id)
            }
            pub fn routing_id(&self) -> Result<RoutingId, crate::error::ConfigError> {
                $inner(self).routing_id()
            }
        }
    };
    ($ty:ident) => {
        #[allow(dead_code)]
        impl $ty {
            pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), crate::error::ConfigError> {
                self.inner.set_routing_id(id)
            }
            pub fn routing_id(&self) -> Result<RoutingId, crate::error::ConfigError> {
                self.inner.routing_id()
            }
        }
    };
}

/// Connect and disconnect for non-STREAM sockets.
macro_rules! impl_connect {
    ($ty:ident, $inner:path) => {
        #[allow(dead_code)]
        impl $ty {
            pub fn connect(&self, addr: &str) -> Result<(), crate::error::ConnectError> {
                $inner(self).connect(addr)
            }
            pub fn disconnect(&self, addr: &str) -> Result<(), crate::error::ConnectError> {
                $inner(self).disconnect(addr)
            }
            pub fn disconnect_rid(
                &self,
                peer_rid: &crate::message::RoutingId,
            ) -> Result<(), crate::error::ConnectError> {
                $inner(self).disconnect_rid(peer_rid)
            }
        }
    };
    ($ty:ident) => {
        #[allow(dead_code)]
        impl $ty {
            pub fn connect(&self, addr: &str) -> Result<(), crate::error::ConnectError> {
                self.inner.connect(addr)
            }
            pub fn disconnect(&self, addr: &str) -> Result<(), crate::error::ConnectError> {
                self.inner.disconnect(addr)
            }
            pub fn disconnect_rid(
                &self,
                peer_rid: &crate::message::RoutingId,
            ) -> Result<(), crate::error::ConnectError> {
                self.inner.disconnect_rid(peer_rid)
            }
        }
    };
}

/// Attach a socket to a discovery-owned lifecycle.
macro_rules! impl_attach_discovery {
    ($ty:ident, $inner:path) => {
        #[allow(dead_code)]
        impl $ty {
            pub fn attach_discovery(
                &self,
                discovery: &crate::Discovery,
            ) -> Result<(), crate::error::ConfigError> {
                $inner(self).attach_discovery(discovery)
            }
        }
    };
    ($ty:ident) => {
        #[allow(dead_code)]
        impl $ty {
            pub fn attach_discovery(
                &self,
                discovery: &crate::Discovery,
            ) -> Result<(), crate::error::ConfigError> {
                self.inner.attach_discovery(discovery)
            }
        }
    };
}

pub(crate) use impl_attach_discovery;
pub(crate) use impl_base_socket;
pub(crate) use impl_connect;
pub(crate) use impl_routing_id_options;
