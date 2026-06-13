use crate::actor_models::{ActorRoute, DiscoveryRoute, SpotRoute};
use crate::core_context::Context;
use crate::error::{CloseError, ConfigError, ConnectError};
use crate::message::RoutingId;
use crate::registry_models::MemberPeerEntry;
use crate::runtime_bridge::DiscoveryStorage;
use crate::spot_models::AutoConnectType;

/// The owner-scoped discovery route kind.
#[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum RouteKind {
    /// No route kind (unset).
    Invalid = 0,
    /// A route keyed by actor id.
    Actor = 1,
    /// A route keyed by spot name.
    SpotName = 2,
    /// A route keyed by actor session.
    ActorSession = 3,
}

impl RouteKind {
    pub(crate) fn to_raw(self) -> u32 {
        self as u32
    }
}

/// A discovery service that learns peer routes from a registry and resolves
/// spots and actors for a fixed channel.
pub struct Discovery {
    pub(crate) inner: Box<dyn DiscoveryStorage>,
}

impl Discovery {
    /// Creates a discovery service for `channel_name` that connects peers
    /// according to `auto_connect_type`. The caller owns it and releases it on
    /// drop.
    pub fn new(
        ctx: &Context,
        auto_connect_type: AutoConnectType,
        channel_name: &str,
    ) -> Result<Self, ConfigError> {
        crate::service::discovery_new(ctx, auto_connect_type, channel_name)
    }

    /// Connects to a registry's publish endpoint to receive route updates.
    pub fn connect_registry(&self, endpoint: &str) -> Result<(), ConnectError> {
        crate::service::discovery_connect_registry(self, endpoint)
    }

    /// Sets the application-defined value advertised for this peer.
    pub fn set_value(&self, value: i64) -> Result<(), ConfigError> {
        crate::service::discovery_set_value(self, value)
    }

    /// Returns the application-defined value advertised for this peer.
    pub fn get_value(&self) -> Result<i64, ConfigError> {
        crate::service::discovery_get_value(self)
    }

    /// Sets whether spot ownership changes are synchronized across peers.
    pub fn set_spot_owner_sync_enabled(&self, enabled: bool) -> Result<(), ConfigError> {
        crate::service::discovery_set_spot_owner_sync_enabled(self, enabled)
    }

    /// Returns whether spot ownership synchronization is enabled.
    pub fn spot_owner_sync_enabled(&self) -> Result<bool, ConfigError> {
        crate::service::discovery_spot_owner_sync_enabled(self)
    }

    /// Sets whether actor routes are synchronized across peers.
    pub fn set_actor_route_sync_enabled(&self, enabled: bool) -> Result<(), ConfigError> {
        crate::service::discovery_set_actor_route_sync_enabled(self, enabled)
    }

    /// Returns whether actor route synchronization is enabled.
    pub fn actor_route_sync_enabled(&self) -> Result<bool, ConfigError> {
        crate::service::discovery_actor_route_sync_enabled(self)
    }

    /// Resolves the route to the spot identified by `spot_rid`.
    pub fn resolve_spot(&self, spot_rid: &RoutingId) -> Result<SpotRoute, ConfigError> {
        crate::service::discovery_resolve_spot(self, spot_rid)
    }

    /// Resolves the route to the actor identified by `actor_id`.
    pub fn resolve_actor(&self, actor_id: &str) -> Result<ActorRoute, ConfigError> {
        crate::service::discovery_resolve_actor(self, actor_id)
    }

    /// Binds a custom route value under `kind` and `key`.
    pub fn bind_route(&self, kind: RouteKind, key: &[u8], value: &[u8]) -> Result<(), ConfigError> {
        crate::service::discovery_bind_route(self, kind, key, value)
    }

    /// Removes the custom route bound under `kind` and `key`.
    pub fn unbind_route(&self, kind: RouteKind, key: &[u8]) -> Result<(), ConfigError> {
        crate::service::discovery_unbind_route(self, kind, key)
    }

    /// Resolves the custom route bound under `kind` and `key`.
    pub fn resolve_route(
        &self,
        kind: RouteKind,
        key: &[u8],
    ) -> Result<DiscoveryRoute, ConfigError> {
        crate::service::discovery_resolve_route(self, kind, key)
    }

    /// Returns the registry member peers currently known to this service. The
    /// caller owns the returned `Vec`.
    pub fn member_peers(&self) -> Result<Vec<MemberPeerEntry>, ConfigError> {
        crate::service::discovery_member_peers(self)
    }

    /// Configures TLS for the registry connection; apply before
    /// [`connect_registry`](Self::connect_registry). `ca_cert_pem` is the CA
    /// bundle path, `hostname` the expected registry hostname, and
    /// `trust_system` also trusts the system CA store.
    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        crate::service::discovery_set_tls_client(self, ca_cert_pem, hostname, trust_system)
    }

    /// Closes the discovery service and releases its resources.
    pub fn close(&mut self) -> Result<(), CloseError> {
        crate::service::discovery_close(self)
    }
}
