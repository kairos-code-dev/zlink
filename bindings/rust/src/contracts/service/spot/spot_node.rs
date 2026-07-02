use crate::runtime_bridge::{SpotNodeContract, SpotNodeStorage};
use crate::spot_operations::{
    ActorDestroyOp, ActorJoinEntrySpotOp, ActorJoinOp, ActorLeaveOp, ActorLookupOp,
};
use crate::{
    Actor, ActorRef, AutoHwmProfile, CloseError, ConfigError, ConnectError, Empty, Message, Ready,
    RoutingId, SendOp, Spot, SpotNodeActorEntry, SpotNodeOptions, SpotNodePeerEntry,
    SpotNodePeerFilter, SpotNodePublisher, SpotNodeSocketEntry, SpotNodeSocketFilter,
    SpotNodeSpotEntry, SpotNodeStatus, SpotNodeSubjectEntry, SpotNodeSubjectFilter,
    SpotRouteBridge,
};

/// A spot node: hosts spots and actors, tunes their sockets, and exposes the
/// node's peers, subjects, and topology. The caller owns it and releases it on
/// drop.
pub struct SpotNode {
    pub(crate) inner: Box<dyn SpotNodeStorage>,
}

impl SpotNode {
    /// Creates a spot node. The caller owns it and releases it on drop.
    pub fn new(ctx: &crate::core_context::Context) -> Result<Self, ConfigError> {
        <Self as SpotNodeContract>::new(ctx)
    }

    /// Creates a spot node with the given options.
    pub fn new_with_options(
        ctx: &crate::core_context::Context,
        options: SpotNodeOptions,
    ) -> Result<Self, ConfigError> {
        <Self as SpotNodeContract>::new_with_options(ctx, options)
    }

    /// Sets the publish endpoint the node binds to.
    pub fn set_pub_bind(&self, endpoint: &str) -> Result<(), ConfigError> {
        <Self as SpotNodeContract>::set_pub_bind(self, endpoint)
    }

    /// Sets the router endpoint the node binds to.
    pub fn set_router_bind(&self, endpoint: &str) -> Result<(), ConfigError> {
        <Self as SpotNodeContract>::set_router_bind(self, endpoint)
    }

    /// Returns the concrete endpoint the node last bound to, resolving any wildcard.
    pub fn last_endpoint(&self) -> Result<String, ConfigError> {
        <Self as SpotNodeContract>::last_endpoint(self)
    }

    /// Connects to a peer node at `peer_endpoint`.
    pub fn connect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError> {
        <Self as SpotNodeContract>::connect_peer(self, peer_endpoint)
    }

    /// Disconnects the peer previously connected at `peer_endpoint`.
    pub fn disconnect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError> {
        <Self as SpotNodeContract>::disconnect_peer(self, peer_endpoint)
    }

    /// Disconnects the peer identified by `target_node_rid`.
    pub fn disconnect_peer_rid(&self, target_node_rid: &RoutingId) -> Result<(), ConnectError> {
        <Self as SpotNodeContract>::disconnect_peer_rid(self, target_node_rid)
    }

    /// Creates a bridge from caller-owned channel sockets to this node's SPOT routed plane.
    pub fn create_route_bridge(&self) -> Result<SpotRouteBridge, ConfigError> {
        SpotRouteBridge::new(self)
    }

    /// Creates a publisher handle for this node's local topic plane.
    pub fn create_publisher(&self) -> Result<SpotNodePublisher, ConfigError> {
        SpotNodePublisher::new(self)
    }

    /// Returns the high-water mark applied to the node's router sockets.
    pub fn router_high_water_mark(&self) -> Result<i32, ConfigError> {
        <Self as SpotNodeContract>::router_high_water_mark(self)
    }

    /// Sets the high-water mark applied to the node's router sockets.
    pub fn set_router_high_water_mark(&self, value: i32) -> Result<(), ConfigError> {
        <Self as SpotNodeContract>::set_router_high_water_mark(self, value)
    }

    /// Returns the high-water mark applied to the node's pub/sub sockets.
    pub fn pubsub_high_water_mark(&self) -> Result<i32, ConfigError> {
        <Self as SpotNodeContract>::pubsub_high_water_mark(self)
    }

    /// Sets the high-water mark applied to the node's pub/sub sockets.
    pub fn set_pubsub_high_water_mark(&self, value: i32) -> Result<(), ConfigError> {
        <Self as SpotNodeContract>::set_pubsub_high_water_mark(self, value)
    }

    /// Returns the auto-HWM profile for the node's router sockets.
    pub fn router_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError> {
        <Self as SpotNodeContract>::router_hwm_profile(self)
    }

    /// Sets the auto-HWM profile for the node's router sockets.
    pub fn set_router_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        <Self as SpotNodeContract>::set_router_hwm_profile(self, profile)
    }

    /// Returns the auto-HWM profile for the node's pub/sub sockets.
    pub fn pubsub_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError> {
        <Self as SpotNodeContract>::pubsub_hwm_profile(self)
    }

    /// Sets the auto-HWM profile for the node's pub/sub sockets.
    pub fn set_pubsub_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        <Self as SpotNodeContract>::set_pubsub_hwm_profile(self, profile)
    }

    /// Returns the minimum number of dispatch worker threads.
    pub fn dispatch_workers_min(&self) -> Result<i32, ConfigError> {
        <Self as SpotNodeContract>::dispatch_workers_min(self)
    }

    /// Sets the minimum number of dispatch worker threads.
    pub fn set_dispatch_workers_min(&self, value: i32) -> Result<(), ConfigError> {
        <Self as SpotNodeContract>::set_dispatch_workers_min(self, value)
    }

    /// Returns the maximum number of dispatch worker threads.
    pub fn dispatch_workers_max(&self) -> Result<i32, ConfigError> {
        <Self as SpotNodeContract>::dispatch_workers_max(self)
    }

    /// Sets the maximum number of dispatch worker threads.
    pub fn set_dispatch_workers_max(&self, value: i32) -> Result<(), ConfigError> {
        <Self as SpotNodeContract>::set_dispatch_workers_max(self, value)
    }

    /// Configures the node as a TLS server; apply before binding.
    pub fn set_tls_server(
        &self,
        cert_pem: &str,
        key_pem: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        <Self as SpotNodeContract>::set_tls_server(self, cert_pem, key_pem, require_client_cert)
    }

    /// Configures TLS for the node's outbound connections.
    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        <Self as SpotNodeContract>::set_tls_client(self, ca_cert_pem, hostname, trust_system)
    }

    /// Creates a local actor with id `actor_id`. The caller owns it.
    pub fn create_actor(&self, actor_id: &str) -> Result<Actor, ConfigError> {
        <Self as SpotNodeContract>::create_actor(self, actor_id)
    }

    /// Looks up a local actor by id, returning its reference.
    pub fn actor_lookup(&self, actor_id: &str) -> Result<ActorRef, ConfigError> {
        <Self as SpotNodeContract>::actor_lookup(self, actor_id)
    }

    /// Builds a reference to a remote actor on `target_node_rid` without contacting it.
    pub fn remote_actor_ref(
        target_node_rid: &RoutingId,
        actor_id: &str,
    ) -> Result<ActorRef, ConfigError> {
        <Self as SpotNodeContract>::remote_actor_ref(target_node_rid, actor_id)
    }

    /// Begins looking up a remote actor's reference; submit the returned operation.
    pub fn remote_actor_get_ref(
        &self,
        target_node_rid: &RoutingId,
        actor_id: &str,
    ) -> ActorLookupOp<Empty> {
        <Self as SpotNodeContract>::remote_actor_get_ref(self, target_node_rid, actor_id)
    }

    /// Begins destroying `actor`; submit the returned operation to apply it.
    pub fn destroy_actor(&self, actor: &ActorRef) -> ActorDestroyOp<Empty> {
        <Self as SpotNodeContract>::destroy_actor(self, actor)
    }

    /// Begins joining `actor` to a spot on another node; submit the returned operation.
    pub fn join_actor(
        &self,
        actor: &ActorRef,
        dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId,
    ) -> ActorJoinOp<Empty> {
        <Self as SpotNodeContract>::join_actor(self, actor, dest_node_rid, dest_spot_rid)
    }

    /// Begins joining `actor` to a node's entry spot with an explicit request
    /// message; submit the returned operation.
    pub fn join_actor_entry_spot(
        &self,
        actor: &ActorRef,
        dest_node_rid: &RoutingId,
        request: Message,
    ) -> ActorJoinEntrySpotOp<Ready> {
        <Self as SpotNodeContract>::join_actor_entry_spot(self, actor, dest_node_rid, request)
    }

    /// Begins removing `actor` from `current_spot_rid`; submit the returned operation.
    pub fn leave_actor(
        &self,
        actor: &ActorRef,
        current_spot_rid: &RoutingId,
    ) -> ActorLeaveOp<Empty> {
        <Self as SpotNodeContract>::leave_actor(self, actor, current_spot_rid)
    }

    /// Begins a send to `actor`'s bound session; parts are consumed on a successful submit (see [`SendOp`]).
    pub fn send_bound_session_msg(&self, actor: &ActorRef) -> SendOp<Empty> {
        <Self as SpotNodeContract>::send_bound_session_msg(self, actor)
    }

    /// Returns a snapshot of the node's status.
    pub fn status(&self) -> Result<SpotNodeStatus, ConfigError> {
        <Self as SpotNodeContract>::status(self)
    }

    /// Sets the node's routing id.
    pub fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError> {
        <Self as SpotNodeContract>::set_routing_id(self, rid)
    }

    /// Returns the node's routing id.
    pub fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        <Self as SpotNodeContract>::routing_id(self)
    }

    /// Returns the node's entry spot.
    pub fn entry_spot(&self) -> Result<Spot, ConfigError> {
        <Self as SpotNodeContract>::entry_spot(self)
    }

    /// Creates a new user spot on the node. The caller owns it.
    pub fn create_spot(&self) -> Result<Spot, ConfigError> {
        <Self as SpotNodeContract>::create_spot(self)
    }

    /// Looks up a spot by routing id, or `None` when absent.
    pub fn spot_lookup(&self, spot_rid: &RoutingId) -> Result<Option<Spot>, ConfigError> {
        <Self as SpotNodeContract>::spot_lookup(self, spot_rid)
    }

    /// Returns the spot for `spot_rid`, creating it if absent; the bool is `true` when newly created.
    pub fn get_or_create_spot(&self, spot_rid: &RoutingId) -> Result<(Spot, bool), ConfigError> {
        <Self as SpotNodeContract>::get_or_create_spot(self, spot_rid)
    }

    /// Returns the node's peers. The caller owns the returned `Vec`.
    pub fn peers(&self) -> Result<Vec<SpotNodePeerEntry>, ConfigError> {
        <Self as SpotNodeContract>::peers(self)
    }

    /// Returns the node's peers matching `filter`. The caller owns the returned `Vec`.
    pub fn peers_query(
        &self,
        filter: &SpotNodePeerFilter,
    ) -> Result<Vec<SpotNodePeerEntry>, ConfigError> {
        <Self as SpotNodeContract>::peers_query(self, filter)
    }

    /// Returns the node's subjects, optionally filtered. The caller owns the returned `Vec`.
    pub fn subjects(
        &self,
        filter: Option<&SpotNodeSubjectFilter>,
    ) -> Result<Vec<SpotNodeSubjectEntry>, ConfigError> {
        <Self as SpotNodeContract>::subjects(self, filter)
    }

    /// Returns the node's internal sockets, optionally filtered. The caller owns the returned `Vec`.
    pub fn internal_sockets(
        &self,
        filter: Option<&SpotNodeSocketFilter>,
    ) -> Result<Vec<SpotNodeSocketEntry>, ConfigError> {
        <Self as SpotNodeContract>::internal_sockets(self, filter)
    }

    /// Returns the spots hosted on the node. The caller owns the returned `Vec`.
    pub fn spots(&self) -> Result<Vec<SpotNodeSpotEntry>, ConfigError> {
        <Self as SpotNodeContract>::spots(self)
    }

    /// Returns the actors hosted on the node. The caller owns the returned `Vec`.
    pub fn actors(&self) -> Result<Vec<SpotNodeActorEntry>, ConfigError> {
        <Self as SpotNodeContract>::actors(self)
    }

    /// Closes the node and releases its resources.
    pub fn close(&mut self) -> Result<(), CloseError> {
        <Self as SpotNodeContract>::close(self)
    }
}
