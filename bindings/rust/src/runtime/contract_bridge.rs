// SPDX-License-Identifier: MPL-2.0

use std::any::Any;
use std::os::fd::RawFd;
use std::time::Duration;

use crate::actor_models::{
    ActorJoinEntrySpotResult, ActorJoinRequest, ActorJoinResult, ActorLookupResult, ActorRef,
    SpotActorLifecycleEvent,
};
use crate::actor_received::ActorReceived;
use crate::core_context::AutoHwmProfile;
use crate::error::{
    CloseError, ConfigError, ConnectError, HandlerError, RecvError, RequestError, SubmitError,
};
use crate::flags::{RecvFlags, RidDuplicatePolicy, SendFlags, SubmitRetryMode};
use crate::message::{Message, RoutingId};
use crate::messaging_subscription_event::SubscriptionEvent;
use crate::monitor_contracts::{MonitorEvent, MonitorStatus};
use crate::poller_contracts::{PollEvent, Pollable, Timer};
use crate::spot_models::{
    SpotDispatchInfo, SpotNodeOptions, SpotNodePeerEntry, SpotNodePeerFilter, SpotNodeSocketEntry,
    SpotNodeSocketFilter, SpotNodeSpotEntry, SpotNodeStatus, SpotNodeSubjectEntry,
    SpotNodeSubjectFilter,
};
use crate::spot_operations::{
    ActorDestroyOp, ActorJoinEntrySpotOp, ActorJoinOp, ActorJoinReplyOp, ActorLeaveOp,
    ActorLookupOp, CallbackReady, Empty, Ready, ReplyOp, RequestOp, SendOp,
};
use crate::topic_message_contract::TopicMessage;
use crate::{Actor, Discovery, Received, Spot, SpotNodeActorEntry};

pub(crate) trait MessageRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn as_bytes(&self) -> &[u8];
    fn data_mut(&mut self) -> &mut [u8];
    fn size(&self) -> usize;
    fn try_clone_box(&self) -> Result<Box<dyn MessageRuntime>, ConfigError>;
    fn get_property(&self, name: &str) -> Result<Option<String>, ConfigError>;
    fn ref_count(&self) -> i32;
}

pub(crate) trait ReceivedReplyRuntime: Send {
    fn reply_op(&self) -> ReplyOp<Empty>;
}

pub(crate) trait ReceivedSendRuntime: Send {
    fn send_op(&self) -> SendOp<Empty>;
}

pub(crate) trait ContextOptionRuntime {
    fn io_threads(&self) -> Result<i32, ConfigError>;
    fn set_io_threads(&self, threads: i32) -> Result<(), ConfigError>;
    fn max_sockets(&self) -> Result<i32, ConfigError>;
    fn set_max_sockets(&self, max: i32) -> Result<(), ConfigError>;
    fn socket_limit(&self) -> Result<i32, ConfigError>;
    fn thread_priority(&self) -> Result<i32, ConfigError>;
    fn set_thread_priority(&self, priority: i32) -> Result<(), ConfigError>;
    fn thread_scheduling_policy(&self) -> Result<i32, ConfigError>;
    fn set_thread_scheduling_policy(&self, policy: i32) -> Result<(), ConfigError>;
    fn max_message_size(&self) -> Result<i32, ConfigError>;
    fn set_max_message_size(&self, size: i32) -> Result<(), ConfigError>;
    fn msg_t_size(&self) -> Result<i32, ConfigError>;
    fn blocky(&self) -> Result<bool, ConfigError>;
    fn set_blocky(&self, blocky: bool) -> Result<(), ConfigError>;
    fn thread_name_prefix(&self) -> Result<String, ConfigError>;
    fn set_thread_name_prefix(&self, prefix: &str) -> Result<(), ConfigError>;
    fn auto_hwm_enabled(&self) -> Result<bool, ConfigError>;
    fn set_auto_hwm_enabled(&self, enabled: bool) -> Result<(), ConfigError>;
    fn auto_hwm_recalc_debounce(&self) -> Result<Duration, ConfigError>;
    fn set_auto_hwm_recalc_debounce(&self, value: Duration) -> Result<(), ConfigError>;
    fn auto_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError>;
    fn set_auto_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError>;
    fn auto_hwm_msg_unit_bytes(&self) -> Result<i32, ConfigError>;
    fn set_auto_hwm_msg_unit_bytes(&self, bytes: i32) -> Result<(), ConfigError>;
    fn add_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError>;
    fn remove_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError>;
}

pub(crate) trait ContextRuntime: Any + ContextOptionRuntime + Send + Sync {
    fn as_any(&self) -> &dyn Any;
    fn shutdown(&self) -> Result<(), CloseError>;
    fn recalculate_auto_hwm(&self) -> Result<(), ConfigError>;
}

pub(crate) trait PairSocketRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

pub(crate) trait SocketRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

pub(crate) type TimerFireHandler = Box<dyn Fn(&Timer, u64) + Send>;

pub(crate) trait PollerRuntime: Send {
    fn add_socket(
        &self,
        socket: &dyn Pollable,
        events: i16,
        slot: usize,
    ) -> Result<(), ConfigError>;
    fn modify_socket(&self, socket: &dyn Pollable, events: i16) -> Result<(), ConfigError>;
    fn remove_socket(&self, socket: &dyn Pollable) -> Result<(), ConfigError>;
    fn add_fd(&self, fd: RawFd, events: i16, slot: usize) -> Result<(), ConfigError>;
    fn modify_fd(&self, fd: RawFd, events: i16) -> Result<(), ConfigError>;
    fn remove_fd(&self, fd: RawFd) -> Result<(), ConfigError>;
    fn add_timer(&self, timer: &Timer, slot: usize) -> Result<(), ConfigError>;
    fn remove_timer(&self, timer: &Timer) -> Result<(), ConfigError>;
    fn wait(&self, events: &mut [PollEvent], timeout_ms: i64) -> Result<usize, RecvError>;
    fn size(&self) -> i32;
}

pub(crate) trait TimerRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn start(&self, interval_ns: u64, repeat_count: u64) -> Result<(), ConfigError>;
    fn stop(&self) -> Result<(), ConfigError>;
    fn recv(&self) -> Result<Option<u64>, RecvError>;
    fn on_fire(&mut self, timer_ptr: usize, handler: TimerFireHandler) -> Result<(), HandlerError>;
}

pub(crate) trait SocketMonitorRuntime: Send {
    fn recv(&self) -> Result<MonitorEvent, RecvError>;
    fn recv_with_flags(&self, flags: RecvFlags) -> Result<Option<MonitorEvent>, RecvError>;
    fn status(&self) -> Result<MonitorStatus, ConfigError>;
    fn on_event(&mut self, handler: Box<dyn Fn(&MonitorEvent) + Send>) -> Result<(), HandlerError>;
    fn close(&mut self) -> Result<(), CloseError>;
}

pub(crate) trait CommonSocketOptionRuntime {
    fn set_linger(&self, d: Duration) -> Result<(), ConfigError>;
    fn linger(&self) -> Result<Duration, ConfigError>;
    fn set_send_high_water_mark(&self, value: i32) -> Result<(), ConfigError>;
    fn send_high_water_mark(&self) -> Result<i32, ConfigError>;
    fn set_receive_high_water_mark(&self, value: i32) -> Result<(), ConfigError>;
    fn receive_high_water_mark(&self) -> Result<i32, ConfigError>;
    fn set_send_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn send_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_receive_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn receive_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_immediate(&self, enabled: bool) -> Result<(), ConfigError>;
    fn immediate(&self) -> Result<bool, ConfigError>;
    fn set_rid_duplicate_policy(&self, value: RidDuplicatePolicy) -> Result<(), ConfigError>;
    fn rid_duplicate_policy(&self) -> Result<RidDuplicatePolicy, ConfigError>;
    fn set_connect_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn connect_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_ipv6(&self, enabled: bool) -> Result<(), ConfigError>;
    fn ipv6(&self) -> Result<bool, ConfigError>;
    fn set_tcp_no_delay(&self, enabled: bool) -> Result<(), ConfigError>;
    fn tcp_no_delay(&self) -> Result<bool, ConfigError>;
    fn set_tcp_keepalive(&self, enabled: bool) -> Result<(), ConfigError>;
    fn tcp_keepalive(&self) -> Result<bool, ConfigError>;
    fn set_heartbeat_interval(&self, d: Duration) -> Result<(), ConfigError>;
    fn heartbeat_interval(&self) -> Result<Duration, ConfigError>;
    fn set_heartbeat_ttl(&self, d: Duration) -> Result<(), ConfigError>;
    fn heartbeat_ttl(&self) -> Result<Duration, ConfigError>;
    fn set_heartbeat_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn heartbeat_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_max_message_size(&self, bytes: i64) -> Result<(), ConfigError>;
    fn max_message_size(&self) -> Result<i64, ConfigError>;
    fn set_backlog(&self, value: i32) -> Result<(), ConfigError>;
    fn backlog(&self) -> Result<i32, ConfigError>;
    fn set_reconnect_interval(&self, d: Duration) -> Result<(), ConfigError>;
    fn reconnect_interval(&self) -> Result<Duration, ConfigError>;
    fn set_reconnect_interval_max(&self, d: Duration) -> Result<(), ConfigError>;
    fn reconnect_interval_max(&self) -> Result<Duration, ConfigError>;
    fn set_submit_retry_mode(&self, value: SubmitRetryMode) -> Result<(), ConfigError>;
    fn submit_retry_mode(&self) -> Result<SubmitRetryMode, ConfigError>;
    fn set_submit_retry_timeout(&self, d: Duration) -> Result<(), ConfigError>;
    fn submit_retry_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_submit_retry_attempts(&self, value: i32) -> Result<(), ConfigError>;
    fn submit_retry_attempts(&self) -> Result<i32, ConfigError>;
}

pub(crate) trait RouterSocketOptionRuntime {
    fn set_mandatory(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_probe(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_connect_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError>;
    fn weight(&self) -> Result<u32, ConfigError>;
    fn set_weight(&self, value: u32) -> Result<(), ConfigError>;
    fn request_timeout(&self) -> Result<Duration, ConfigError>;
    fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError>;
}

pub(crate) trait DealerSocketOptionRuntime {
    fn set_probe(&self, enabled: bool) -> Result<(), ConfigError>;
    fn weight(&self) -> Result<u32, ConfigError>;
    fn set_weight(&self, value: u32) -> Result<(), ConfigError>;
    fn set_request_timeout(&self, value: Duration) -> Result<(), ConfigError>;
}

pub(crate) trait StreamSocketOptionRuntime {
    fn set_notify(&self, enabled: bool) -> Result<(), ConfigError>;
    fn notify(&self) -> Result<bool, ConfigError>;
}

pub(crate) trait PubSocketOptionRuntime {
    fn set_verbose(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_verboser(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_no_drop(&self, enabled: bool) -> Result<(), ConfigError>;
    fn set_manual(&self, enabled: bool) -> Result<(), ConfigError>;
    fn manual_last_value(&self) -> Result<bool, ConfigError>;
    fn set_manual_last_value(&self, enabled: bool) -> Result<(), ConfigError>;
    fn welcome_message(&self) -> Result<Message, ConfigError>;
    fn set_welcome_message(&self, message: &Message) -> Result<(), ConfigError>;
    fn approve_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError>;
    fn reject_subscribe(&self, routing_id: &RoutingId) -> Result<(), ConfigError>;
    fn topics_count(&self) -> Result<i32, ConfigError>;
}

pub(crate) trait SubSocketOptionRuntime {
    fn topics_count(&self) -> Result<i32, ConfigError>;
}

pub(crate) trait DiscoveryRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

pub(crate) trait RegistryRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

pub(crate) trait RegistryQueryClientRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

pub(crate) trait ActorRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

pub(crate) trait ActorPublicRuntime {
    fn actor_ref(&self) -> Result<ActorRef, ConfigError>;
    fn close_with_timeout(&mut self, timeout: Duration) -> Result<(), RequestError>;
    fn close(&mut self) -> Result<(), RequestError>;
    fn join(&self, spot: &Spot) -> ActorJoinOp<Empty>;
    fn leave(&self, spot: &Spot) -> ActorLeaveOp<Empty>;
    fn recv(&self, out: &mut ActorReceived, flags: RecvFlags) -> Result<bool, RecvError>;
    fn send_bound_session_msg(&self) -> SendOp<Empty>;
    fn close_bound_session(&self, timeout: Duration) -> Result<(), RequestError>;
}

pub(crate) trait SpotRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

pub(crate) trait SpotNodeRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
}

pub(crate) trait SendOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait RequestOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait ReplyOpRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait ActorJoinOpInnerRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait ActorJoinEntrySpotOpInnerRuntime: Any + Send {
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait ActorJoinReplyOpInnerRuntime: Any + Send {
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait ActorReplyOpInnerRuntime: Any + Send {
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait ActorLookupOpInnerRuntime: Any + Send {
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

pub(crate) trait SendOpEmptyRuntime {
    fn message(self, message: Message) -> SendOp<Ready>;
}

pub(crate) trait SendOpReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn flags(self, flags: SendFlags) -> Self;
    fn submit(self) -> Result<bool, SubmitError>;
}

pub(crate) trait RequestOpEmptyRuntime {
    fn message(self, message: Message) -> RequestOp<Ready>;
}

pub(crate) trait RequestOpReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn timeout(self, timeout: Duration) -> Self;
    fn flags(self, flags: SendFlags) -> RequestOp<CallbackReady>;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;
}

pub(crate) trait RequestOpCallbackReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn timeout(self, timeout: Duration) -> Self;
    fn flags(self, flags: SendFlags) -> Self;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;
}

pub(crate) trait ReplyOpEmptyRuntime {
    fn message(self, message: Message) -> ReplyOp<Ready>;
}

pub(crate) trait ReplyOpReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn flags(self, flags: SendFlags) -> Self;
    fn submit(self) -> Result<(), SubmitError>;
}

pub(crate) trait ActorJoinEntrySpotOpRuntime {
    fn message(self, message: Message) -> Self;
    fn timeout(self, timeout: Duration) -> Self;
    fn flags(self, flags: SendFlags) -> Self;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinEntrySpotResult, Vec<Message>) + Send + 'static;
}

pub(crate) trait ActorJoinOpEmptyRuntime {
    fn message(self, message: Message) -> ActorJoinOp<Ready>;
}

pub(crate) trait ActorJoinOpReadyRuntime {
    fn message(self, message: Message) -> Self;
    fn timeout(self, timeout: Duration) -> Self;
    fn flags(self, flags: SendFlags) -> Self;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorJoinResult, Vec<Message>) + Send + 'static;
}

pub(crate) trait ActorJoinReplyOpRuntime {
    fn message(self, message: Message) -> ActorJoinReplyOp<Empty>;
    fn submit(self) -> Result<(), SubmitError>;
}

pub(crate) trait ActorReplyOpRuntime {
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;
}

pub(crate) trait ActorReplyOpTimeoutRuntime: ActorReplyOpRuntime {
    fn timeout(self, timeout: Duration) -> Self;
}

pub(crate) trait ActorLookupOpRuntime {
    fn timeout(self, timeout: Duration) -> Self;
    fn submit<F>(self, callback: F) -> Result<(), SubmitError>
    where
        F: FnOnce(ActorLookupResult) + Send + 'static;
}

pub(crate) use ActorJoinEntrySpotOpInnerRuntime as ActorJoinEntrySpotOpStorage;
pub(crate) use ActorJoinEntrySpotOpRuntime as ActorJoinEntrySpotOpContract;
pub(crate) use ActorJoinOpEmptyRuntime as ActorJoinOpEmptyContract;
pub(crate) use ActorJoinOpInnerRuntime as ActorJoinOpStorage;
pub(crate) use ActorJoinOpReadyRuntime as ActorJoinOpReadyContract;
pub(crate) use ActorJoinReplyOpInnerRuntime as ActorJoinReplyOpStorage;
pub(crate) use ActorJoinReplyOpRuntime as ActorJoinReplyOpContract;
pub(crate) use ActorLookupOpInnerRuntime as ActorLookupOpStorage;
pub(crate) use ActorLookupOpRuntime as ActorLookupOpContract;
pub(crate) use ActorPublicRuntime as ActorContract;
pub(crate) use ActorReplyOpInnerRuntime as ActorReplyOpStorage;
pub(crate) use ActorReplyOpRuntime as ActorReplyOpContract;
pub(crate) use ActorReplyOpTimeoutRuntime as ActorReplyOpTimeoutContract;
pub(crate) use ActorRuntime as ActorStorage;
pub(crate) use CommonSocketOptionRuntime as CommonSocketOptionEndpoint;
pub(crate) use ContextRuntime as ContextStorage;
pub(crate) use DealerSocketOptionRuntime as DealerSocketOptionEndpoint;
pub(crate) use DiscoveryRuntime as DiscoveryStorage;
pub(crate) use MessageRuntime as MessageStorage;
pub(crate) use PairSocketRuntime as PairSocketStorage;
pub(crate) use PollerRuntime as PollerStorage;
pub(crate) use PubSocketOptionRuntime as PubSocketOptionEndpoint;
pub(crate) use ReceivedReplyRuntime as ReceivedReplyContext;
pub(crate) use ReceivedSendRuntime as ReceivedSendContext;
pub(crate) use RegistryQueryClientRuntime as RegistryQueryClientStorage;
pub(crate) use RegistryRuntime as RegistryStorage;
pub(crate) use ReplyOpEmptyRuntime as ReplyOpEmptyContract;
pub(crate) use ReplyOpReadyRuntime as ReplyOpReadyContract;
pub(crate) use ReplyOpRuntime as ReplyOpStorage;
pub(crate) use RequestOpCallbackReadyRuntime as RequestOpCallbackReadyContract;
pub(crate) use RequestOpEmptyRuntime as RequestOpEmptyContract;
pub(crate) use RequestOpReadyRuntime as RequestOpReadyContract;
pub(crate) use RequestOpRuntime as RequestOpStorage;
pub(crate) use RouterSocketOptionRuntime as RouterSocketOptionEndpoint;
pub(crate) use SendOpEmptyRuntime as SendOpEmptyContract;
pub(crate) use SendOpReadyRuntime as SendOpReadyContract;
pub(crate) use SendOpRuntime as SendOpStorage;
pub(crate) use SocketMonitorRuntime as SocketMonitorStorage;
pub(crate) use SocketRuntime as SocketStorage;
pub(crate) use SpotNodePublicRuntime as SpotNodeContract;
pub(crate) use SpotNodeRuntime as SpotNodeStorage;
pub(crate) use SpotPublicRuntime as SpotContract;
pub(crate) use SpotRuntime as SpotStorage;
pub(crate) use StreamSocketOptionRuntime as StreamSocketOptionEndpoint;
pub(crate) use SubSocketOptionRuntime as SubSocketOptionEndpoint;
pub(crate) use TimerRuntime as TimerStorage;

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
    fn connect_router_channel_peer_rid(
        &self,
        channel_name: &str,
        peer_rid: &RoutingId,
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
        request: Message,
    ) -> ActorJoinEntrySpotOp<Ready>;
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
