use std::any::Any;

use crate::Received;
use crate::actor_models::{ActorJoinRequest, ActorRef, SpotActorLifecycleEvent};
use crate::error::{CloseError, ConfigError, HandlerError, RecvError};
use crate::flags::RecvFlags;
use crate::message::RoutingId;
use crate::messaging_subscription_event::SubscriptionEvent;
use crate::spot_models::SpotDispatchInfo;
use crate::spot_operations::ActorJoinReplyOp;
use crate::spot_operations::{Empty, ReplyOp, RequestOp, SendOp};
use crate::topic_message_contract::TopicMessage;

pub(crate) trait SpotRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

/// Unified SPOT facade over an existing SPOT node.
pub struct Spot {
    pub(crate) inner: Box<dyn SpotRuntime>,
}

pub(crate) trait SpotPublicRuntime {
    fn publish(&self, topic: &str) -> SendOp<Empty>;
    fn send_to_channel(&self, channel_name: &str) -> SendOp<Empty>;
    fn send_to_spot(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId) -> SendOp<Empty>;
    fn request_to_channel(&self, channel_name: &str) -> RequestOp<Empty>;
    fn request_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    ) -> RequestOp<Empty>;
    fn request_to_router(&self, peer_rid: RoutingId) -> RequestOp<Empty>;
    fn reply_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
    ) -> ReplyOp<Empty>;
    fn reply_to_router(&self, peer_rid: RoutingId, request_seq: u64) -> ReplyOp<Empty>;
    fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError>;
    fn routing_id(&self) -> Result<RoutingId, ConfigError>;
    fn set_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    fn subscribe(&self, out: &mut TopicMessage, flags: RecvFlags) -> Result<bool, RecvError>;
    fn receive_subscription_event(
        &self,
        out: &mut SubscriptionEvent,
        flags: RecvFlags,
    ) -> Result<bool, RecvError>;
    fn recv_actor_join_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<Option<ActorJoinRequest>, RecvError>;
    fn recv_actor_join(&self) -> Result<ActorJoinRequest, RecvError>;
    fn reply_actor_join(
        &self,
        request: &ActorJoinRequest,
        join_result_code: i32,
    ) -> ActorJoinReplyOp<Empty>;
    fn actors(&self) -> Result<Vec<ActorRef>, ConfigError>;
    fn on_dispatch_event<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: for<'a> Fn(SpotDispatchInfo<'a>) + Send + 'static;
    fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static;
    fn recv_actor_lifecycle_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<Option<SpotActorLifecycleEvent>, RecvError>;
    fn recv_actor_lifecycle(&self) -> Result<SpotActorLifecycleEvent, RecvError>;
    fn recv_routed(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError>;
    fn close(&mut self) -> Result<(), CloseError>;
}

impl Spot {
    pub fn publish(&self, topic: &str) -> SendOp<Empty> {
        <Self as SpotPublicRuntime>::publish(self, topic)
    }

    pub fn send_to_channel(&self, channel_name: &str) -> SendOp<Empty> {
        <Self as SpotPublicRuntime>::send_to_channel(self, channel_name)
    }

    pub fn send_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    ) -> SendOp<Empty> {
        <Self as SpotPublicRuntime>::send_to_spot(self, dest_node_rid, dest_spot_rid)
    }

    pub fn request_to_channel(&self, channel_name: &str) -> RequestOp<Empty> {
        <Self as SpotPublicRuntime>::request_to_channel(self, channel_name)
    }

    pub fn request_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    ) -> RequestOp<Empty> {
        <Self as SpotPublicRuntime>::request_to_spot(self, dest_node_rid, dest_spot_rid)
    }

    pub fn request_to_router(&self, peer_rid: RoutingId) -> RequestOp<Empty> {
        <Self as SpotPublicRuntime>::request_to_router(self, peer_rid)
    }

    pub fn reply_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
    ) -> ReplyOp<Empty> {
        <Self as SpotPublicRuntime>::reply_to_spot(self, dest_node_rid, dest_spot_rid, request_seq)
    }

    pub fn reply_to_router(&self, peer_rid: RoutingId, request_seq: u64) -> ReplyOp<Empty> {
        <Self as SpotPublicRuntime>::reply_to_router(self, peer_rid, request_seq)
    }

    pub fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError> {
        <Self as SpotPublicRuntime>::set_routing_id(self, rid)
    }

    pub fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        <Self as SpotPublicRuntime>::routing_id(self)
    }

    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        <Self as SpotPublicRuntime>::set_subscription(self, filter)
    }

    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        <Self as SpotPublicRuntime>::unset_subscription(self, filter)
    }

    pub fn subscribe(&self, out: &mut TopicMessage, flags: RecvFlags) -> Result<bool, RecvError> {
        <Self as SpotPublicRuntime>::subscribe(self, out, flags)
    }

    pub fn receive_subscription_event(
        &self,
        out: &mut SubscriptionEvent,
        flags: RecvFlags,
    ) -> Result<bool, RecvError> {
        <Self as SpotPublicRuntime>::receive_subscription_event(self, out, flags)
    }

    pub fn recv_actor_join_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<Option<ActorJoinRequest>, RecvError> {
        <Self as SpotPublicRuntime>::recv_actor_join_with_flags(self, flags)
    }

    pub fn recv_actor_join(&self) -> Result<ActorJoinRequest, RecvError> {
        <Self as SpotPublicRuntime>::recv_actor_join(self)
    }

    pub fn reply_actor_join(
        &self,
        request: &ActorJoinRequest,
        join_result_code: i32,
    ) -> ActorJoinReplyOp<Empty> {
        <Self as SpotPublicRuntime>::reply_actor_join(self, request, join_result_code)
    }

    pub fn actors(&self) -> Result<Vec<ActorRef>, ConfigError> {
        <Self as SpotPublicRuntime>::actors(self)
    }

    pub fn on_dispatch_event<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: for<'a> Fn(SpotDispatchInfo<'a>) + Send + 'static,
    {
        <Self as SpotPublicRuntime>::on_dispatch_event(self, handler)
    }

    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        <Self as SpotPublicRuntime>::on_send_ready(self, handler)
    }

    pub fn recv_actor_lifecycle_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<Option<SpotActorLifecycleEvent>, RecvError> {
        <Self as SpotPublicRuntime>::recv_actor_lifecycle_with_flags(self, flags)
    }

    pub fn recv_actor_lifecycle(&self) -> Result<SpotActorLifecycleEvent, RecvError> {
        <Self as SpotPublicRuntime>::recv_actor_lifecycle(self)
    }

    pub fn recv_routed(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        <Self as SpotPublicRuntime>::recv_routed(self, out, flags)
    }

    pub fn close(&mut self) -> Result<(), CloseError> {
        <Self as SpotPublicRuntime>::close(self)
    }
}
