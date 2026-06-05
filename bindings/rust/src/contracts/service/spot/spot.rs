use crate::Received;
use crate::actor_models::{ActorJoinRequest, ActorRef, SpotActorLifecycleEvent};
use crate::error::{CloseError, ConfigError, HandlerError, RecvError};
use crate::flags::RecvFlags;
use crate::message::RoutingId;
use crate::messaging_subscription_event::SubscriptionEvent;
use crate::runtime_bridge::{SpotContract, SpotStorage};
use crate::spot_models::SpotDispatchInfo;
use crate::spot_operations::ActorJoinReplyOp;
use crate::spot_operations::{Empty, ReplyOp, RequestOp, SendOp};
use crate::topic_message_contract::TopicMessage;

/// A spot: a multi-role messaging endpoint that can publish, subscribe, route,
/// request, reply, and host actors over an existing spot node.
pub struct Spot {
    pub(crate) inner: Box<dyn SpotStorage>,
}

impl Spot {
    /// Begins publishing under `topic`; parts are consumed on a successful
    /// submit (see [`SendOp`]).
    pub fn publish(&self, topic: &str) -> SendOp<Empty> {
        <Self as SpotContract>::publish(self, topic)
    }

    /// Begins a send addressed to the channel `channel_name`; parts are consumed
    /// on a successful submit.
    pub fn send_to_channel(&self, channel_name: &str) -> SendOp<Empty> {
        <Self as SpotContract>::send_to_channel(self, channel_name)
    }

    /// Begins a send addressed to a spot on another node; parts are consumed on
    /// a successful submit.
    pub fn send_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    ) -> SendOp<Empty> {
        <Self as SpotContract>::send_to_spot(self, dest_node_rid, dest_spot_rid)
    }

    /// Begins a request to the channel `channel_name`; parts are consumed on a
    /// successful submit and a reply is awaited.
    pub fn request_to_channel(&self, channel_name: &str) -> RequestOp<Empty> {
        <Self as SpotContract>::request_to_channel(self, channel_name)
    }

    /// Begins a request to a spot on another node; parts are consumed on a
    /// successful submit and a reply is awaited.
    pub fn request_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
    ) -> RequestOp<Empty> {
        <Self as SpotContract>::request_to_spot(self, dest_node_rid, dest_spot_rid)
    }

    /// Begins a request to a ROUTER peer; parts are consumed on a successful
    /// submit and a reply is awaited.
    pub fn request_to_router(&self, peer_rid: RoutingId) -> RequestOp<Empty> {
        <Self as SpotContract>::request_to_router(self, peer_rid)
    }

    /// Begins a reply to the spot request `request_seq`; parts are consumed on a
    /// successful submit.
    pub fn reply_to_spot(
        &self,
        dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId,
        request_seq: u64,
    ) -> ReplyOp<Empty> {
        <Self as SpotContract>::reply_to_spot(self, dest_node_rid, dest_spot_rid, request_seq)
    }

    /// Begins a reply to a ROUTER peer's request `request_seq`; parts are
    /// consumed on a successful submit.
    pub fn reply_to_router(&self, peer_rid: RoutingId, request_seq: u64) -> ReplyOp<Empty> {
        <Self as SpotContract>::reply_to_router(self, peer_rid, request_seq)
    }

    /// Sets the routing id that identifies this spot to its peers.
    pub fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError> {
        <Self as SpotContract>::set_routing_id(self, rid)
    }

    /// Returns the routing id that identifies this spot to its peers.
    pub fn routing_id(&self) -> Result<RoutingId, ConfigError> {
        <Self as SpotContract>::routing_id(self)
    }

    /// Adds a subscription for `filter` (an exact topic or pattern).
    /// Subscriptions accumulate.
    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        <Self as SpotContract>::set_subscription(self, filter)
    }

    /// Removes a subscription previously added for `filter`.
    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError> {
        <Self as SpotContract>::unset_subscription(self, filter)
    }

    /// Receives the next matching topic message into caller-provided `out`
    /// storage; `Ok(false)` when [`RecvFlags::DONT_WAIT`] is set and none is
    /// available.
    pub fn subscribe(&self, out: &mut TopicMessage, flags: RecvFlags) -> Result<bool, RecvError> {
        <Self as SpotContract>::subscribe(self, out, flags)
    }

    /// Receives the next subscriber (un)subscription event into caller-provided
    /// `out` storage; `Ok(false)` when [`RecvFlags::DONT_WAIT`] is set and none
    /// is available.
    pub fn receive_subscription_event(
        &self,
        out: &mut SubscriptionEvent,
        flags: RecvFlags,
    ) -> Result<bool, RecvError> {
        <Self as SpotContract>::receive_subscription_event(self, out, flags)
    }

    /// Receives the next pending actor-join request, or `Ok(None)` when
    /// [`RecvFlags::DONT_WAIT`] is set and none is available.
    pub fn recv_actor_join_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<Option<ActorJoinRequest>, RecvError> {
        <Self as SpotContract>::recv_actor_join_with_flags(self, flags)
    }

    /// Blocks until the next actor-join request arrives and returns it.
    pub fn recv_actor_join(&self) -> Result<ActorJoinRequest, RecvError> {
        <Self as SpotContract>::recv_actor_join(self)
    }

    /// Begins a reply to `request` carrying `join_result_code`; parts are
    /// consumed on a successful submit.
    pub fn reply_actor_join(
        &self,
        request: &ActorJoinRequest,
        join_result_code: i32,
    ) -> ActorJoinReplyOp<Empty> {
        <Self as SpotContract>::reply_actor_join(self, request, join_result_code)
    }

    /// Returns the actors currently hosted on this spot. The caller owns the
    /// returned `Vec`.
    pub fn actors(&self) -> Result<Vec<ActorRef>, ConfigError> {
        <Self as SpotContract>::actors(self)
    }

    /// Registers the callback invoked for each spot dispatch event. The callback
    /// runs on a background dispatch thread.
    pub fn on_dispatch_event<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: for<'a> Fn(SpotDispatchInfo<'a>) + Send + 'static,
    {
        <Self as SpotContract>::on_dispatch_event(self, handler)
    }

    /// Registers a callback invoked when the spot can accept more sends after
    /// back-pressure. The callback runs on a background dispatch thread.
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn() + Send + 'static,
    {
        <Self as SpotContract>::on_send_ready(self, handler)
    }

    /// Receives the next actor lifecycle event, or `Ok(None)` when
    /// [`RecvFlags::DONT_WAIT`] is set and none is available.
    pub fn recv_actor_lifecycle_with_flags(
        &self,
        flags: RecvFlags,
    ) -> Result<Option<SpotActorLifecycleEvent>, RecvError> {
        <Self as SpotContract>::recv_actor_lifecycle_with_flags(self, flags)
    }

    /// Blocks until the next actor lifecycle event arrives and returns it.
    pub fn recv_actor_lifecycle(&self) -> Result<SpotActorLifecycleEvent, RecvError> {
        <Self as SpotContract>::recv_actor_lifecycle(self)
    }

    /// Receives the next routed message into caller-provided `out` storage;
    /// `Ok(false)` when [`RecvFlags::DONT_WAIT`] is set and none is available.
    pub fn recv_routed(&self, out: &mut Received, flags: RecvFlags) -> Result<bool, RecvError> {
        <Self as SpotContract>::recv_routed(self, out, flags)
    }

    /// Closes the spot and releases its resources.
    pub fn close(&mut self) -> Result<(), CloseError> {
        <Self as SpotContract>::close(self)
    }
}
