use std::any::Any;

use crate::error::{CloseError, ConfigError, ConnectError};
use crate::registry_models::{RegistryTopologyEntry, RegistryTopologyFilter};

pub(crate) trait RegistryQueryClientRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// Read-only registry query client for remote topology snapshots.
pub struct RegistryQueryClient {
    pub(crate) inner: Box<dyn RegistryQueryClientRuntime>,
}

impl RegistryQueryClient {
    pub fn new(ctx: &crate::core_context::Context) -> Result<Self, ConfigError> {
        crate::service::registry_query_client_new(ctx)
    }

    pub fn connect(&self, endpoint: &str) -> Result<(), ConnectError> {
        crate::service::registry_query_client_connect(self, endpoint)
    }

    pub fn topology(
        &self,
        filter: Option<&RegistryTopologyFilter>,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        crate::service::registry_query_client_topology(self, filter)
    }

    pub fn snapshot(
        &self,
        filter: Option<&RegistryTopologyFilter>,
    ) -> Result<Vec<RegistryTopologyEntry>, ConfigError> {
        crate::service::registry_query_client_snapshot(self, filter)
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        crate::service::registry_query_client_close(self)
    }
}
