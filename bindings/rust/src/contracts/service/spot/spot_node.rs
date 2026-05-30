use std::any::Any;

use crate::spot_operations::{
    ActorDestroyOp, ActorJoinEntrySpotOp, ActorJoinOp, ActorLeaveOp, ActorLookupOp,
};
use crate::{
    Actor, ActorRef, AutoHwmProfile, CloseError, ConfigError, ConnectError, Discovery, Empty,
    RoutingId, SendOp, Spot, SpotNodeActorEntry, SpotNodeOptions, SpotNodePeerEntry,
    SpotNodePeerFilter, SpotNodeSocketEntry, SpotNodeSocketFilter, SpotNodeSpotEntry,
    SpotNodeStatus, SpotNodeSubjectEntry, SpotNodeSubjectFilter,
};

pub(crate) trait SpotNodeRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// A spot node: hosts spots and actors, tunes their sockets, and exposes the
/// node's peers, subjects, and topology. The caller owns it and releases it on
/// drop.
pub struct SpotNode {
    pub(crate) inner: Box<dyn SpotNodeRuntime>,
}

pub(crate) trait SpotNodePublicRuntime {
    fn new(ctx: &crate::core_context::Context) -> Result<Self, ConfigError>
    where
        Self: Sized;
    fn new_with_options(
        ctx: &crate::core_context::Context,
        options: SpotNodeOptions,
    ) -> Result<Self, ConfigError>
    where
        Self: Sized;
    fn set_pub_bind(&self, endpoint: &str) -> Result<(), ConfigError>;
    fn set_router_bind(&self, endpoint: &str) -> Result<(), ConfigError>;
    fn last_endpoint(&self) -> Result<String, ConfigError>;
    fn connect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError>;
    fn disconnect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError>;
    fn disconnect_peer_rid(&self, target_node_rid: &RoutingId) -> Result<(), ConnectError>;
    fn connect_router_channel_peer(
        &self,
        channel_name: &str,
        endpoint: &str,
    ) -> Result<(), ConnectError>;
    fn disconnect_router_channel_peer(
        &self,
        channel_name: &str,
        endpoint: &str,
    ) -> Result<(), ConnectError>;
    fn disconnect_router_channel_peer_rid(
        &self,
        channel_name: &str,
        peer_rid: &RoutingId,
    ) -> Result<(), ConnectError>;
    fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError>;
    fn attach_spot_route_channel_discovery(
        &self,
        channel_name: &str,
        discovery: &Discovery,
    ) -> Result<(), ConfigError>;
    fn attach_channel_dealer(
        &self,
        discovery: &Discovery,
        dealer: &crate::DealerSocket,
    ) -> Result<(), ConfigError>;
    fn attach_channel_dealer_manual(
        &self,
        channel_name: &str,
        dealer: &crate::DealerSocket,
    ) -> Result<(), ConfigError>;
    fn attach_pub_ingress(&self, pub_sock: &crate::PubSocket) -> Result<(), ConfigError>;
    fn router_high_water_mark(&self) -> Result<i32, ConfigError>;
    fn set_router_high_water_mark(&self, value: i32) -> Result<(), ConfigError>;
    fn pubsub_high_water_mark(&self) -> Result<i32, ConfigError>;
    fn set_pubsub_high_water_mark(&self, value: i32) -> Result<(), ConfigError>;
    fn router_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError>;
    fn set_router_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError>;
    fn pubsub_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError>;
    fn set_pubsub_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError>;
    fn dispatch_workers_min(&self) -> Result<i32, ConfigError>;
    fn set_dispatch_workers_min(&self, value: i32) -> Result<(), ConfigError>;
    fn dispatch_workers_max(&self) -> Result<i32, ConfigError>;
    fn set_dispatch_workers_max(&self, value: i32) -> Result<(), ConfigError>;
    fn set_tls_server(
        &self,
        cert_pem: &str,
        key_pem: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError>;
    fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError>;
    fn create_actor(&self, actor_id: &str) -> Result<Actor, ConfigError>;
    fn actor_lookup(&self, actor_id: &str) -> Result<ActorRef, ConfigError>;
    fn remote_actor_ref(
        target_node_rid: &RoutingId,
        actor_id: &str,
    ) -> Result<ActorRef, ConfigError>;
    fn remote_actor_get_ref(
        &self,
        target_node_rid: &RoutingId,
        actor_id: &str,
    ) -> ActorLookupOp<Empty>;
    fn destroy_actor(&self, actor: &ActorRef) -> ActorDestroyOp<Empty>;
    fn join_actor(
        &self,
        actor: &ActorRef,
        dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId,
    ) -> ActorJoinOp<Empty>;
    fn join_actor_entry_spot(
        &self,
        actor: &ActorRef,
        dest_node_rid: &RoutingId,
    ) -> ActorJoinEntrySpotOp<Empty>;
    fn leave_actor(&self, actor: &ActorRef, current_spot_rid: &RoutingId) -> ActorLeaveOp<Empty>;
    fn send_bound_session_msg(&self, actor: &ActorRef) -> SendOp<Empty>;
    fn status(&self) -> Result<SpotNodeStatus, ConfigError>;
    fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError>;
    fn routing_id(&self) -> Result<RoutingId, ConfigError>;
    fn entry_spot(&self) -> Result<Spot, ConfigError>;
    fn create_spot(&self) -> Result<Spot, ConfigError>;
    fn spot_lookup(&self, spot_rid: &RoutingId) -> Result<Option<Spot>, ConfigError>;
    fn get_or_create_spot(&self, spot_rid: &RoutingId) -> Result<(Spot, bool), ConfigError>;
    fn peers(&self) -> Result<Vec<SpotNodePeerEntry>, ConfigError>;
    fn peers_query(
        &self,
        filter: &SpotNodePeerFilter,
    ) -> Result<Vec<SpotNodePeerEntry>, ConfigError>;
    fn subjects(
        &self,
        filter: Option<&SpotNodeSubjectFilter>,
    ) -> Result<Vec<SpotNodeSubjectEntry>, ConfigError>;
    fn internal_sockets(
        &self,
        filter: Option<&SpotNodeSocketFilter>,
    ) -> Result<Vec<SpotNodeSocketEntry>, ConfigError>;
    fn spots(&self) -> Result<Vec<SpotNodeSpotEntry>, ConfigError>;
    fn actors(&self) -> Result<Vec<SpotNodeActorEntry>, ConfigError>;
    fn close(&mut self) -> Result<(), CloseError>;
}

impl SpotNode {
    /// Creates a spot node. The caller owns it and releases it on drop.
    pub fn new(ctx: &crate::core_context::Context) -> Result<Self, ConfigError> {
        <Self as SpotNodePublicRuntime>::new(ctx)
    }

    /// Creates a spot node with the given options.
    pub fn new_with_options(
        ctx: &crate::core_context::Context,
        options: SpotNodeOptions,
    ) -> Result<Self, ConfigError> {
        <Self as SpotNodePublicRuntime>::new_with_options(ctx, options)
    }

    /// Sets the publish endpoint the node binds to.
    pub fn set_pub_bind(&self, endpoint: &str) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_pub_bind(self, endpoint)
    }

    /// Sets the router endpoint the node binds to.
    pub fn set_router_bind(&self, endpoint: &str) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_router_bind(self, endpoint)
    }

    /// Returns the concrete endpoint the node last bound to, resolving any wildcard.
    pub fn last_endpoint(&self) -> Result<String, ConfigError> {
        <Self as SpotNodePublicRuntime>::last_endpoint(self)
    }

    /// Connects to a peer node at `peer_endpoint`.
    pub fn connect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError> {
        <Self as SpotNodePublicRuntime>::connect_peer(self, peer_endpoint)
    }

    /// Disconnects the peer previously connected at `peer_endpoint`.
    pub fn disconnect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError> {
        <Self as SpotNodePublicRuntime>::disconnect_peer(self, peer_endpoint)
    }

    /// Disconnects the peer identified by `target_node_rid`.
    pub fn disconnect_peer_rid(&self, target_node_rid: &RoutingId) -> Result<(), ConnectError> {
        <Self as SpotNodePublicRuntime>::disconnect_peer_rid(self, target_node_rid)
    }

    /// Connects to a router-channel peer on `channel_name` at `endpoint`.
    pub fn connect_router_channel_peer(
        &self,
        channel_name: &str,
        endpoint: &str,
    ) -> Result<(), ConnectError> {
        <Self as SpotNodePublicRuntime>::connect_router_channel_peer(self, channel_name, endpoint)
    }

    /// Disconnects a router-channel peer on `channel_name` at `endpoint`.
    pub fn disconnect_router_channel_peer(
        &self,
        channel_name: &str,
        endpoint: &str,
    ) -> Result<(), ConnectError> {
        <Self as SpotNodePublicRuntime>::disconnect_router_channel_peer(
            self,
            channel_name,
            endpoint,
        )
    }

    /// Disconnects a router-channel peer on `channel_name` by `peer_rid`.
    pub fn disconnect_router_channel_peer_rid(
        &self,
        channel_name: &str,
        peer_rid: &RoutingId,
    ) -> Result<(), ConnectError> {
        <Self as SpotNodePublicRuntime>::disconnect_router_channel_peer_rid(
            self,
            channel_name,
            peer_rid,
        )
    }

    /// Attaches a discovery service so the node auto-connects discovered peers.
    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::attach_discovery(self, discovery)
    }

    /// Attaches a discovery service for the spot-route channel `channel_name`.
    pub fn attach_spot_route_channel_discovery(
        &self,
        channel_name: &str,
        discovery: &Discovery,
    ) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::attach_spot_route_channel_discovery(
            self,
            channel_name,
            discovery,
        )
    }

    /// Attaches a DEALER socket as the channel dealer, discovered via `discovery`.
    pub fn attach_channel_dealer(
        &self,
        discovery: &Discovery,
        dealer: &crate::DealerSocket,
    ) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::attach_channel_dealer(self, discovery, dealer)
    }

    /// Attaches a DEALER socket as the channel dealer for `channel_name` manually.
    pub fn attach_channel_dealer_manual(
        &self,
        channel_name: &str,
        dealer: &crate::DealerSocket,
    ) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::attach_channel_dealer_manual(self, channel_name, dealer)
    }

    /// Attaches a PUB socket as a publish ingress for the node.
    pub fn attach_pub_ingress(&self, pub_sock: &crate::PubSocket) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::attach_pub_ingress(self, pub_sock)
    }

    /// Returns the high-water mark applied to the node's router sockets.
    pub fn router_high_water_mark(&self) -> Result<i32, ConfigError> {
        <Self as SpotNodePublicRuntime>::router_high_water_mark(self)
    }

    /// Sets the high-water mark applied to the node's router sockets.
    pub fn set_router_high_water_mark(&self, value: i32) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_router_high_water_mark(self, value)
    }

    /// Returns the high-water mark applied to the node's pub/sub sockets.
    pub fn pubsub_high_water_mark(&self) -> Result<i32, ConfigError> {
        <Self as SpotNodePublicRuntime>::pubsub_high_water_mark(self)
    }

    /// Sets the high-water mark applied to the node's pub/sub sockets.
    pub fn set_pubsub_high_water_mark(&self, value: i32) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_pubsub_high_water_mark(self, value)
    }

    /// Returns the auto-HWM profile for the node's router sockets.
    pub fn router_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError> {
        <Self as SpotNodePublicRuntime>::router_hwm_profile(self)
    }

    /// Sets the auto-HWM profile for the node's router sockets.
    pub fn set_router_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_router_hwm_profile(self, profile)
    }

    /// Returns the auto-HWM profile for the node's pub/sub sockets.
    pub fn pubsub_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError> {
        <Self as SpotNodePublicRuntime>::pubsub_hwm_profile(self)
    }

    /// Sets the auto-HWM profile for the node's pub/sub sockets.
    pub fn set_pubsub_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_pubsub_hwm_profile(self, profile)
    }

    /// Returns the minimum number of dispatch worker threads.
    pub fn dispatch_workers_min(&self) -> Result<i32, ConfigError> {
        <Self as SpotNodePublicRuntime>::dispatch_workers_min(self)
    }

    /// Sets the minimum number of dispatch worker threads.
    pub fn set_dispatch_workers_min(&self, value: i32) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_dispatch_workers_min(self, value)
    }

    /// Returns the maximum number of dispatch worker threads.
    pub fn dispatch_workers_max(&self) -> Result<i32, ConfigError> {
        <Self as SpotNodePublicRuntime>::dispatch_workers_max(self)
    }

    /// Sets the maximum number of dispatch worker threads.
    pub fn set_dispatch_workers_max(&self, value: i32) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_dispatch_workers_max(self, value)
    }

    /// Configures the node as a TLS server; apply before binding.
    pub fn set_tls_server(
        &self,
        cert_pem: &str,
        key_pem: &str,
        require_client_cert: bool,
    ) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_tls_server(
            self,
            cert_pem,
            key_pem,
            require_client_cert,
        )
    }

    /// Configures TLS for the node's outbound connections.
    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_tls_client(self, ca_cert_pem, hostname, trust_system)
    }

    /// Creates a local actor with id `actor_id`. The caller owns it.
    pub fn create_actor(&self, actor_id: &str) -> Result<Actor, ConfigError> {
        <Self as SpotNodePublicRuntime>::create_actor(self, actor_id)
    }

    /// Looks up a local actor by id, returning its reference.
    pub fn actor_lookup(&self, actor_id: &str) -> Result<ActorRef, ConfigError> {
        <Self as SpotNodePublicRuntime>::actor_lookup(self, actor_id)
    }

    /// Builds a reference to a remote actor on `target_node_rid` without contacting it.
    pub fn remote_actor_ref(
        target_node_rid: &RoutingId,
        actor_id: &str,
    ) -> Result<ActorRef, ConfigError> {
        <Self as SpotNodePublicRuntime>::remote_actor_ref(target_node_rid, actor_id)
    }

    /// Begins looking up a remote actor's reference; submit the returned operation.
    pub fn remote_actor_get_ref(
        &self,
        target_node_rid: &RoutingId,
        actor_id: &str,
    ) -> ActorLookupOp<Empty> {
        <Self as SpotNodePublicRuntime>::remote_actor_get_ref(self, target_node_rid, actor_id)
    }

    /// Begins destroying `actor`; submit the returned operation to apply it.
    pub fn destroy_actor(&self, actor: &ActorRef) -> ActorDestroyOp<Empty> {
        <Self as SpotNodePublicRuntime>::destroy_actor(self, actor)
    }

    /// Begins joining `actor` to a spot on another node; submit the returned operation.
    pub fn join_actor(
        &self,
        actor: &ActorRef,
        dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId,
    ) -> ActorJoinOp<Empty> {
        <Self as SpotNodePublicRuntime>::join_actor(self, actor, dest_node_rid, dest_spot_rid)
    }

    /// Begins joining `actor` to a node's entry spot; submit the returned operation.
    pub fn join_actor_entry_spot(
        &self,
        actor: &ActorRef,
        dest_node_rid: &RoutingId,
    ) -> ActorJoinEntrySpotOp<Empty> {
        <Self as SpotNodePublicRuntime>::join_actor_entry_spot(self, actor, dest_node_rid)
    }

    /// Begins removing `actor` from `current_spot_rid`; submit the returned operation.
    pub fn leave_actor(
        &self,
        actor: &ActorRef,
        current_spot_rid: &RoutingId,
    ) -> ActorLeaveOp<Empty> {
        <Self as SpotNodePublicRuntime>::leave_actor(self, actor, current_spot_rid)
    }

    /// Begins a send to `actor`'s bound session; parts are consumed on a successful submit (see [`SendOp`]).
    pub fn send_bound_session_msg(&self, actor: &ActorRef) -> SendOp<Empty> {
        <Self as SpotNodePublicRuntime>::send_bound_session_msg(self, actor)
    }

    /// Returns a snapshot of the node's status.
    pub fn status(&self) -> Result<SpotNodeStatus, ConfigError> {
        <Self as SpotNodePublicRuntime>::status(self)
    }

    /// Sets the node's routing id.
    pub fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_routing_id(self, rid)
    }

    /// Returns the node's routing id.
    pub fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        <Self as SpotNodePublicRuntime>::routing_id(self)
    }

    /// Returns the node's entry spot.
    pub fn entry_spot(&self) -> Result<Spot, ConfigError> {
        <Self as SpotNodePublicRuntime>::entry_spot(self)
    }

    /// Creates a new user spot on the node. The caller owns it.
    pub fn create_spot(&self) -> Result<Spot, ConfigError> {
        <Self as SpotNodePublicRuntime>::create_spot(self)
    }

    /// Looks up a spot by routing id, or `None` when absent.
    pub fn spot_lookup(&self, spot_rid: &RoutingId) -> Result<Option<Spot>, ConfigError> {
        <Self as SpotNodePublicRuntime>::spot_lookup(self, spot_rid)
    }

    /// Returns the spot for `spot_rid`, creating it if absent; the bool is `true` when newly created.
    pub fn get_or_create_spot(&self, spot_rid: &RoutingId) -> Result<(Spot, bool), ConfigError> {
        <Self as SpotNodePublicRuntime>::get_or_create_spot(self, spot_rid)
    }

    /// Returns the node's peers. The caller owns the returned `Vec`.
    pub fn peers(&self) -> Result<Vec<SpotNodePeerEntry>, ConfigError> {
        <Self as SpotNodePublicRuntime>::peers(self)
    }

    /// Returns the node's peers matching `filter`. The caller owns the returned `Vec`.
    pub fn peers_query(
        &self,
        filter: &SpotNodePeerFilter,
    ) -> Result<Vec<SpotNodePeerEntry>, ConfigError> {
        <Self as SpotNodePublicRuntime>::peers_query(self, filter)
    }

    /// Returns the node's subjects, optionally filtered. The caller owns the returned `Vec`.
    pub fn subjects(
        &self,
        filter: Option<&SpotNodeSubjectFilter>,
    ) -> Result<Vec<SpotNodeSubjectEntry>, ConfigError> {
        <Self as SpotNodePublicRuntime>::subjects(self, filter)
    }

    /// Returns the node's internal sockets, optionally filtered. The caller owns the returned `Vec`.
    pub fn internal_sockets(
        &self,
        filter: Option<&SpotNodeSocketFilter>,
    ) -> Result<Vec<SpotNodeSocketEntry>, ConfigError> {
        <Self as SpotNodePublicRuntime>::internal_sockets(self, filter)
    }

    /// Returns the spots hosted on the node. The caller owns the returned `Vec`.
    pub fn spots(&self) -> Result<Vec<SpotNodeSpotEntry>, ConfigError> {
        <Self as SpotNodePublicRuntime>::spots(self)
    }

    /// Returns the actors hosted on the node. The caller owns the returned `Vec`.
    pub fn actors(&self) -> Result<Vec<SpotNodeActorEntry>, ConfigError> {
        <Self as SpotNodePublicRuntime>::actors(self)
    }

    /// Closes the node and releases its resources.
    pub fn close(&mut self) -> Result<(), CloseError> {
        <Self as SpotNodePublicRuntime>::close(self)
    }
}
