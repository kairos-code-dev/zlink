use std::any::Any;

use crate::actor_models::{ActorRoute, SpotRoute};
use crate::core_context::Context;
use crate::error::{CloseError, ConfigError, ConnectError};
use crate::message::RoutingId;
use crate::registry_models::MemberPeerEntry;
use crate::spot_models::AutoConnectType;

pub(crate) trait DiscoveryRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// Discovery instance with a fixed service view.
pub struct Discovery {
    pub(crate) inner: Box<dyn DiscoveryRuntime>,
}

impl Discovery {
    pub fn new(
        ctx: &Context,
        auto_connect_type: AutoConnectType,
        channel_name: &str,
    ) -> Result<Self, ConfigError> {
        crate::service::discovery_new(ctx, auto_connect_type, channel_name)
    }

    pub fn connect_registry(&self, endpoint: &str) -> Result<(), ConnectError> {
        crate::service::discovery_connect_registry(self, endpoint)
    }

    pub fn set_value(&self, value: i64) -> Result<(), ConfigError> {
        crate::service::discovery_set_value(self, value)
    }

    pub fn get_value(&self) -> Result<i64, ConfigError> {
        crate::service::discovery_get_value(self)
    }

    pub fn set_spot_owner_sync_enabled(&self, enabled: bool) -> Result<(), ConfigError> {
        crate::service::discovery_set_spot_owner_sync_enabled(self, enabled)
    }

    pub fn spot_owner_sync_enabled(&self) -> Result<bool, ConfigError> {
        crate::service::discovery_spot_owner_sync_enabled(self)
    }

    pub fn set_actor_route_sync_enabled(&self, enabled: bool) -> Result<(), ConfigError> {
        crate::service::discovery_set_actor_route_sync_enabled(self, enabled)
    }

    pub fn actor_route_sync_enabled(&self) -> Result<bool, ConfigError> {
        crate::service::discovery_actor_route_sync_enabled(self)
    }

    pub fn resolve_spot(&self, spot_rid: &RoutingId) -> Result<SpotRoute, ConfigError> {
        crate::service::discovery_resolve_spot(self, spot_rid)
    }

    pub fn resolve_actor(&self, actor_id: &str) -> Result<ActorRoute, ConfigError> {
        crate::service::discovery_resolve_actor(self, actor_id)
    }

    pub fn member_peers(&self) -> Result<Vec<MemberPeerEntry>, ConfigError> {
        crate::service::discovery_member_peers(self)
    }

    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        crate::service::discovery_set_tls_client(self, ca_cert_pem, hostname, trust_system)
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        crate::service::discovery_close(self)
    }
}
