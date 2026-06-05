// SPDX-License-Identifier: MPL-2.0

//! Rust bindings for the zlink messaging library.
//!
//! This crate wraps the zlink C API (`zlink.h`) with an idiomatic, safe Rust
//! surface following the bindings API policy:
//!
//! - **Builder-based** public send, publish, request, reply, and actor operations.
//! - **Multipart-only** public send/receive surface.
//! - **Typed options** per socket type – no raw option bags.
//! - **Ownership** via RAII: [`Message`] drop calls `zlink_msg_close`;
//!   `send` consumes messages and suppresses drop.
//! - **Message diagnostics** via [`Message::get_property`] and
//!   [`Message::ref_count`].
//! - **Domain objects**: [`Received`], [`TopicMessage`], [`SubscriptionEvent`],
//!   [`SendResult`], [`RoutingId`].
//! - **Socket capability isolation**: each socket type exposes only its own
//!   capabilities (e.g. `PubSocket` has no `recv`).

#[path = "runtime/native/ffi.rs"]
mod ffi;
#[path = "runtime/messaging/request_progress.rs"]
mod request_progress;

#[path = "contracts/service/spot/actor_models.rs"]
mod actor_models;
#[path = "contracts/service/spot/actor_received.rs"]
mod actor_received;
#[path = "contracts/service/spot/actor.rs"]
mod actor_resource;
#[path = "contracts/core/context.rs"]
mod core_context;
#[path = "runtime/handles/ctx.rs"]
mod ctx;
#[path = "contracts/service/discovery/discovery.rs"]
mod discovery_resource;
#[path = "contracts/messaging/received.rs"]
mod domain;
#[path = "contracts/errors/errors.rs"]
mod error;
#[path = "contracts/sockets/socket_options.rs"]
mod flags;
#[path = "contracts/messaging/message.rs"]
mod message;
#[path = "runtime/messaging/message.rs"]
mod message_factory;
#[path = "contracts/sockets/message_socket_contracts.rs"]
mod message_socket_contracts;
#[path = "runtime/messaging/domain.rs"]
mod received_operations;
#[path = "contracts/messaging/operation_contracts.rs"]
mod messaging_operation_contracts;
#[path = "contracts/messaging/subscription_event.rs"]
mod messaging_subscription_event;
#[path = "runtime/eventing/monitor.rs"]
mod monitor;
#[path = "contracts/eventing/monitor.rs"]
mod monitor_contracts;
#[path = "runtime/errors/native_errors.rs"]
mod native_errors;
#[path = "runtime/native/routing_id.rs"]
mod native_routing_id;
#[path = "runtime/sockets/options.rs"]
mod options;
#[path = "runtime/eventing/poller.rs"]
mod poller;
#[path = "contracts/eventing/poller.rs"]
mod poller_contracts;
#[path = "contracts/sockets/pubsub_socket_contracts.rs"]
mod pubsub_socket_contracts;
#[path = "contracts/service/registry/registry_models.rs"]
mod registry_models;
#[path = "contracts/service/registry/registry_query_client.rs"]
mod registry_query_client_resource;
#[path = "contracts/service/registry/registry.rs"]
mod registry_resource;
#[path = "contracts/errors/results.rs"]
mod results;
#[path = "contracts/sockets/routed_socket_contracts.rs"]
mod routed_socket_contracts;
#[path = "contracts/core/routing_id.rs"]
mod routing_id;
#[path = "runtime/core/runtime.rs"]
mod runtime;
#[path = "runtime/contract_bridge.rs"]
mod runtime_bridge;
#[path = "runtime/service/service.rs"]
mod service;
#[path = "runtime/sockets/socket/mod.rs"]
mod socket;
#[path = "contracts/sockets/socket.rs"]
mod socket_contracts;
#[path = "contracts/service/spot/spot_models.rs"]
mod spot_models;
#[path = "contracts/service/spot/spot_node.rs"]
mod spot_node_resource;
#[path = "contracts/service/spot/spot_operations.rs"]
mod spot_operations;
#[path = "contracts/service/spot/spot.rs"]
mod spot_resource;
#[path = "contracts/sockets/stream_socket.rs"]
mod stream_socket_contract;
#[path = "contracts/messaging/topic_message.rs"]
mod topic_message_contract;

// -- Public re-exports -------------------------------------------------------

pub use actor_models::{
    ActorJoinEntrySpotResult, ActorJoinInfo, ActorJoinRequest, ActorJoinResult, ActorLookupResult,
    ActorRecvInfo, ActorRef, ActorRoute, SpotActorLifecycleEvent, SpotActorLifecycleEventKind,
    SpotActorLifecycleInfo, SpotNodeActorEntry, SpotRoute,
};
pub use actor_received::ActorReceived;
pub use actor_resource::Actor;
pub use core_context::{AutoHwmProfile, AutoHwmRecalcReason, Context, ContextOptions};
pub use discovery_resource::Discovery;
pub use domain::Received;
pub use error::{
    BindError, CloseError, ConfigError, ConnectError, HandlerError, RecvError, RequestError,
    SubmitError, ZlinkError,
};
pub use flags::{
    CommonSocketOptions, DealerSocketOptions, PubSocketOptions, RecvFlags, RidDuplicatePolicy,
    RouterSocketOptions, SendFlags, StreamSocketOptions, SubSocketOptions, SubmitRetryMode,
};
pub use message::Message;
pub use message_socket_contracts::{DealerSocket, PairSocket};
pub use messaging_operation_contracts::SendResult;
pub use messaging_subscription_event::SubscriptionEvent;
pub use monitor_contracts::{
    MONITOR_EVENT_ALL, MONITOR_EVENT_CONNECTION_READY, MonitorEvent, MonitorEventType,
    MonitorSourceKind, MonitorStatus, Monitorable, SocketMonitor, SocketMonitorEventMask,
};
pub use poller_contracts::{
    POLLCOMPLETION, POLLIN, POLLOUT, PollEvent, PollItem, PollSourceKind, Pollable, Poller, Timer,
};
pub use pubsub_socket_contracts::{PubSocket, SubSocket, XPubSocket, XSubSocket};
pub use registry_models::{
    MemberPeerEntry, RegistryServiceSummaryEntry, RegistryServiceSummaryFilter, RegistryStatus,
    RegistryTopologyEntry, RegistryTopologyFilter,
};
pub use registry_query_client_resource::RegistryQueryClient;
pub use registry_resource::Registry;
pub use results::{
    BindResult, CloseResult, ConfigResult, ConnectResult, HandlerResult, RecvResult, RequestResult,
    SubmitResult,
};
pub use routed_socket_contracts::RouterSocket;
pub use routing_id::RoutingId;
pub use spot_models::{
    AutoConnectType, RegistryState, ServiceKind, ServiceRole, SocketType, SpotDispatchEvent,
    SpotDispatchInfo, SpotKind, SpotNodeMode, SpotNodeOptions, SpotNodePeerEntry,
    SpotNodePeerFilter, SpotNodeSocketEntry, SpotNodeSocketFilter, SpotNodeSocketOwner,
    SpotNodeSpotEntry, SpotNodeState, SpotNodeStatus, SpotNodeSubjectEntry, SpotNodeSubjectFilter,
    SpotPeerKind, SpotPeerSource, SpotPeerState, SpotRole, SubjectKind, TopologySource,
    TopologyState,
};
pub use spot_node_resource::SpotNode;
pub use spot_operations::{
    ActorBindOp, ActorDestroyOp, ActorJoinEntrySpotOp, ActorJoinOp, ActorJoinReplyOp, ActorLeaveOp,
    ActorLookupOp, ActorUnbindOp, CallbackReady, Empty, Ready, ReplyOp, RequestOp, SendOp,
};
pub use spot_resource::Spot;
pub use stream_socket_contract::StreamSocket;
pub use topic_message_contract::TopicMessage;

pub fn version() -> (i32, i32, i32) {
    ctx::version()
}

pub fn has(capability: &str) -> bool {
    ctx::has(capability)
}

pub fn proxy(
    frontend: &dyn Pollable,
    backend: &dyn Pollable,
    capture: Option<&dyn Pollable>,
) -> Result<(), ConfigError> {
    runtime::proxy(frontend, backend, capture)
}

pub fn proxy_steerable(
    frontend: &dyn Pollable,
    backend: &dyn Pollable,
    capture: Option<&dyn Pollable>,
    control: &dyn Pollable,
) -> Result<(), ConfigError> {
    runtime::proxy_steerable(frontend, backend, capture, control)
}

pub fn sleep(seconds: i32) {
    runtime::sleep(seconds);
}

pub fn multipart_close(parts: &mut [Message]) {
    runtime::multipart_close(parts);
}

pub fn poll(items: &mut [PollItem], timeout_ms: i64) -> Result<i32, RecvError> {
    poller::poll(items, timeout_ms)
}
