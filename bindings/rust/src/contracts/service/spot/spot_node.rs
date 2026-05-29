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

/// SPOT node runtime facade for topology, discovery, and lifecycle.
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
    pub fn new(ctx: &crate::core_context::Context) -> Result<Self, ConfigError> {
        <Self as SpotNodePublicRuntime>::new(ctx)
    }

    pub fn new_with_options(
        ctx: &crate::core_context::Context,
        options: SpotNodeOptions,
    ) -> Result<Self, ConfigError> {
        <Self as SpotNodePublicRuntime>::new_with_options(ctx, options)
    }

    pub fn set_pub_bind(&self, endpoint: &str) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_pub_bind(self, endpoint)
    }

    pub fn set_router_bind(&self, endpoint: &str) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_router_bind(self, endpoint)
    }

    pub fn last_endpoint(&self) -> Result<String, ConfigError> {
        <Self as SpotNodePublicRuntime>::last_endpoint(self)
    }

    pub fn connect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError> {
        <Self as SpotNodePublicRuntime>::connect_peer(self, peer_endpoint)
    }

    pub fn disconnect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError> {
        <Self as SpotNodePublicRuntime>::disconnect_peer(self, peer_endpoint)
    }

    pub fn disconnect_peer_rid(&self, target_node_rid: &RoutingId) -> Result<(), ConnectError> {
        <Self as SpotNodePublicRuntime>::disconnect_peer_rid(self, target_node_rid)
    }

    pub fn connect_router_channel_peer(
        &self,
        channel_name: &str,
        endpoint: &str,
    ) -> Result<(), ConnectError> {
        <Self as SpotNodePublicRuntime>::connect_router_channel_peer(self, channel_name, endpoint)
    }

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

    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::attach_discovery(self, discovery)
    }

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

    pub fn attach_channel_dealer(
        &self,
        discovery: &Discovery,
        dealer: &crate::DealerSocket,
    ) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::attach_channel_dealer(self, discovery, dealer)
    }

    pub fn attach_channel_dealer_manual(
        &self,
        channel_name: &str,
        dealer: &crate::DealerSocket,
    ) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::attach_channel_dealer_manual(self, channel_name, dealer)
    }

    pub fn attach_pub_ingress(&self, pub_sock: &crate::PubSocket) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::attach_pub_ingress(self, pub_sock)
    }

    pub fn router_high_water_mark(&self) -> Result<i32, ConfigError> {
        <Self as SpotNodePublicRuntime>::router_high_water_mark(self)
    }

    pub fn set_router_high_water_mark(&self, value: i32) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_router_high_water_mark(self, value)
    }

    pub fn pubsub_high_water_mark(&self) -> Result<i32, ConfigError> {
        <Self as SpotNodePublicRuntime>::pubsub_high_water_mark(self)
    }

    pub fn set_pubsub_high_water_mark(&self, value: i32) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_pubsub_high_water_mark(self, value)
    }

    pub fn router_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError> {
        <Self as SpotNodePublicRuntime>::router_hwm_profile(self)
    }

    pub fn set_router_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_router_hwm_profile(self, profile)
    }

    pub fn pubsub_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError> {
        <Self as SpotNodePublicRuntime>::pubsub_hwm_profile(self)
    }

    pub fn set_pubsub_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_pubsub_hwm_profile(self, profile)
    }

    pub fn dispatch_workers_min(&self) -> Result<i32, ConfigError> {
        <Self as SpotNodePublicRuntime>::dispatch_workers_min(self)
    }

    pub fn set_dispatch_workers_min(&self, value: i32) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_dispatch_workers_min(self, value)
    }

    pub fn dispatch_workers_max(&self) -> Result<i32, ConfigError> {
        <Self as SpotNodePublicRuntime>::dispatch_workers_max(self)
    }

    pub fn set_dispatch_workers_max(&self, value: i32) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_dispatch_workers_max(self, value)
    }

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

    pub fn set_tls_client(
        &self,
        ca_cert_pem: &str,
        hostname: &str,
        trust_system: bool,
    ) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_tls_client(self, ca_cert_pem, hostname, trust_system)
    }

    pub fn create_actor(&self, actor_id: &str) -> Result<Actor, ConfigError> {
        <Self as SpotNodePublicRuntime>::create_actor(self, actor_id)
    }

    pub fn actor_lookup(&self, actor_id: &str) -> Result<ActorRef, ConfigError> {
        <Self as SpotNodePublicRuntime>::actor_lookup(self, actor_id)
    }

    pub fn remote_actor_ref(
        target_node_rid: &RoutingId,
        actor_id: &str,
    ) -> Result<ActorRef, ConfigError> {
        <Self as SpotNodePublicRuntime>::remote_actor_ref(target_node_rid, actor_id)
    }

    pub fn remote_actor_get_ref(
        &self,
        target_node_rid: &RoutingId,
        actor_id: &str,
    ) -> ActorLookupOp<Empty> {
        <Self as SpotNodePublicRuntime>::remote_actor_get_ref(self, target_node_rid, actor_id)
    }

    pub fn destroy_actor(&self, actor: &ActorRef) -> ActorDestroyOp<Empty> {
        <Self as SpotNodePublicRuntime>::destroy_actor(self, actor)
    }

    pub fn join_actor(
        &self,
        actor: &ActorRef,
        dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId,
    ) -> ActorJoinOp<Empty> {
        <Self as SpotNodePublicRuntime>::join_actor(self, actor, dest_node_rid, dest_spot_rid)
    }

    pub fn join_actor_entry_spot(
        &self,
        actor: &ActorRef,
        dest_node_rid: &RoutingId,
    ) -> ActorJoinEntrySpotOp<Empty> {
        <Self as SpotNodePublicRuntime>::join_actor_entry_spot(self, actor, dest_node_rid)
    }

    pub fn leave_actor(
        &self,
        actor: &ActorRef,
        current_spot_rid: &RoutingId,
    ) -> ActorLeaveOp<Empty> {
        <Self as SpotNodePublicRuntime>::leave_actor(self, actor, current_spot_rid)
    }

    pub fn send_bound_session_msg(&self, actor: &ActorRef) -> SendOp<Empty> {
        <Self as SpotNodePublicRuntime>::send_bound_session_msg(self, actor)
    }

    pub fn status(&self) -> Result<SpotNodeStatus, ConfigError> {
        <Self as SpotNodePublicRuntime>::status(self)
    }

    pub fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError> {
        <Self as SpotNodePublicRuntime>::set_routing_id(self, rid)
    }

    pub fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        <Self as SpotNodePublicRuntime>::routing_id(self)
    }

    pub fn entry_spot(&self) -> Result<Spot, ConfigError> {
        <Self as SpotNodePublicRuntime>::entry_spot(self)
    }

    pub fn create_spot(&self) -> Result<Spot, ConfigError> {
        <Self as SpotNodePublicRuntime>::create_spot(self)
    }

    pub fn spot_lookup(&self, spot_rid: &RoutingId) -> Result<Option<Spot>, ConfigError> {
        <Self as SpotNodePublicRuntime>::spot_lookup(self, spot_rid)
    }

    pub fn get_or_create_spot(&self, spot_rid: &RoutingId) -> Result<(Spot, bool), ConfigError> {
        <Self as SpotNodePublicRuntime>::get_or_create_spot(self, spot_rid)
    }

    pub fn peers(&self) -> Result<Vec<SpotNodePeerEntry>, ConfigError> {
        <Self as SpotNodePublicRuntime>::peers(self)
    }

    pub fn peers_query(
        &self,
        filter: &SpotNodePeerFilter,
    ) -> Result<Vec<SpotNodePeerEntry>, ConfigError> {
        <Self as SpotNodePublicRuntime>::peers_query(self, filter)
    }

    pub fn subjects(
        &self,
        filter: Option<&SpotNodeSubjectFilter>,
    ) -> Result<Vec<SpotNodeSubjectEntry>, ConfigError> {
        <Self as SpotNodePublicRuntime>::subjects(self, filter)
    }

    pub fn internal_sockets(
        &self,
        filter: Option<&SpotNodeSocketFilter>,
    ) -> Result<Vec<SpotNodeSocketEntry>, ConfigError> {
        <Self as SpotNodePublicRuntime>::internal_sockets(self, filter)
    }

    pub fn spots(&self) -> Result<Vec<SpotNodeSpotEntry>, ConfigError> {
        <Self as SpotNodePublicRuntime>::spots(self)
    }

    pub fn actors(&self) -> Result<Vec<SpotNodeActorEntry>, ConfigError> {
        <Self as SpotNodePublicRuntime>::actors(self)
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        <Self as SpotNodePublicRuntime>::close(self)
    }
}
