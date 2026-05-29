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
    pub fn new(ctx: &crate::core_context::Context) -> Result<Self, ConfigError> {
        crate::service::registry_new(ctx)
    }

    pub fn bind(&self, pub_endpoint: &str, router_endpoint: &str) -> Result<(), BindError> {
        crate::service::registry_bind(self, pub_endpoint, router_endpoint)
    }

    pub fn set_id(&self, id: u32) -> Result<(), ConfigError> {
        crate::service::registry_set_id(self, id)
    }

    pub fn add_peer(&self, peer_pub_endpoint: &str) -> Result<(), ConnectError> {
        crate::service::registry_add_peer(self, peer_pub_endpoint)
    }

    pub fn set_heartbeat(&self, interval_ms: u32, timeout_ms: u32) -> Result<(), ConfigError> {
        crate::service::registry_set_heartbeat(self, interval_ms, timeout_ms)
    }

    pub fn set_broadcast_interval(&self, interval_ms: u32) -> Result<(), ConfigError> {
        crate::service::registry_set_broadcast_interval(self, interval_ms)
    }

    pub fn set_tls_server(
        &self,
        cert_pem: &str,
        key_pem: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        crate::service::registry_set_tls_server(self, cert_pem, key_pem, require_client_cert)
    }

    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        crate::service::registry_set_tls_client(self, ca_cert_pem, hostname, trust_system)
    }

    pub fn status(&self) -> Result<RegistryStatus, ConfigError> {
        crate::service::registry_status(self)
    }

    pub fn service_summary(&self) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
        crate::service::registry_service_summary(self)
    }

    pub fn service_summary_query(
        &self,
        filter: &RegistryServiceSummaryFilter,
    ) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError> {
        crate::service::registry_service_summary_query(self, filter)
    }

    pub fn member_peers(&self, channel_name: &str) -> Result<Vec<MemberPeerEntry>, ConfigError> {
        crate::service::registry_member_peers(self, channel_name)
    }

    pub fn topology(&self) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        crate::service::registry_topology(self)
    }

    pub fn topology_query(
        &self,
        filter: &RegistryTopologyFilter,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        crate::service::registry_topology_query(self, filter)
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        crate::service::registry_close(self)
    }
}
