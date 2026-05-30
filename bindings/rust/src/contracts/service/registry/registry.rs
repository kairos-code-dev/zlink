use std::any::Any;

use crate::error::{BindError, CloseError, ConfigError, ConnectError};
use crate::registry_models::{
    MemberPeerEntry, RegistryServiceSummaryEntry, RegistryServiceSummaryFilter, RegistryStatus,
    RegistryTopologyEntry, RegistryTopologyFilter,
};

pub(crate) trait RegistryRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// Service registry that accepts registrations and broadcasts the service list.
pub struct Registry {
    pub(crate) inner: Box<dyn RegistryRuntime>,
}

impl Registry {
    /// Creates a registry. The caller owns it and releases it on drop.
    pub fn new(ctx: &crate::core_context::Context) -> Result<Self, ConfigError> {
        crate::service::registry_new(ctx)
    }

    /// Binds the registry's publish and router endpoints so members and peers
    /// can connect.
    pub fn bind(&self, pub_endpoint: &str, router_endpoint: &str) -> Result<(), BindError> {
        crate::service::registry_bind(self, pub_endpoint, router_endpoint)
    }

    /// Sets the registry's identifier.
    pub fn set_id(&self, id: u32) -> Result<(), ConfigError> {
        crate::service::registry_set_id(self, id)
    }

    /// Adds a peer registry to federate with, by its publish endpoint.
    pub fn add_peer(&self, peer_pub_endpoint: &str) -> Result<(), ConnectError> {
        crate::service::registry_add_peer(self, peer_pub_endpoint)
    }

    /// Sets the member heartbeat interval and the timeout, both in
    /// milliseconds, after which a silent member is dropped.
    pub fn set_heartbeat(&self, interval_ms: u32, timeout_ms: u32) -> Result<(), ConfigError> {
        crate::service::registry_set_heartbeat(self, interval_ms, timeout_ms)
    }

    /// Sets how often, in milliseconds, the registry broadcasts its state.
    pub fn set_broadcast_interval(&self, interval_ms: u32) -> Result<(), ConfigError> {
        crate::service::registry_set_broadcast_interval(self, interval_ms)
    }

    /// Configures the registry as a TLS server; apply before
    /// [`bind`](Self::bind). `cert_pem` is the certificate path, `key_pem` its
    /// private key path, and `require_client_cert` requires mutual TLS.
    pub fn set_tls_server(
        &self,
        cert_pem: &str,
        key_pem: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        crate::service::registry_set_tls_server(self, cert_pem, key_pem, require_client_cert)
    }

    /// Configures TLS for connections to peer registries. `ca_cert_pem` is the
    /// CA bundle path, `hostname` the expected peer hostname, and `trust_system`
    /// also trusts the system CA store.
    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        crate::service::registry_set_tls_client(self, ca_cert_pem, hostname, trust_system)
    }

    /// Returns a snapshot of the registry's current status.
    pub fn status(&self) -> Result<RegistryStatus, ConfigError> {
        crate::service::registry_status(self)
    }

    /// Returns a summary of registered services. The caller owns the returned
    /// `Vec`.
    pub fn service_summary(&self) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
        crate::service::registry_service_summary(self)
    }

    /// Returns a summary of registered services matching `filter`. The caller
    /// owns the returned `Vec`.
    pub fn service_summary_query(
        &self,
        filter: &RegistryServiceSummaryFilter,
    ) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
        crate::service::registry_service_summary_query(self, filter)
    }

    /// Returns the member peers registered on `channel_name`. The caller owns
    /// the returned `Vec`.
    pub fn member_peers(&self, channel_name: &str) -> Result<Vec<MemberPeerEntry>, ConfigError> {
        crate::service::registry_member_peers(self, channel_name)
    }

    /// Returns the registry's topology entries. The caller owns the returned
    /// `Vec`.
    pub fn topology(&self) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        crate::service::registry_topology(self)
    }

    /// Returns the registry's topology entries matching `filter`. The caller
    /// owns the returned `Vec`.
    pub fn topology_query(
        &self,
        filter: &RegistryTopologyFilter,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        crate::service::registry_topology_query(self, filter)
    }

    /// Closes the registry and releases its resources.
    pub fn close(&mut self) -> Result<(), CloseError> {
        crate::service::registry_close(self)
    }
}
