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

#[path = "contracts/core/ctx.rs"]
mod ctx;
#[path = "contracts/messaging/domain.rs"]
mod domain;
#[path = "contracts/errors/error.rs"]
mod error;
#[path = "contracts/enums/flags.rs"]
mod flags;
#[path = "contracts/messaging/message.rs"]
mod message;
#[path = "contracts/monitoring/monitor.rs"]
pub mod monitor;
#[path = "contracts/sockets/options.rs"]
mod options;
#[path = "contracts/monitoring/poller.rs"]
pub mod poller;
#[path = "contracts/core/runtime.rs"]
mod runtime;
#[path = "contracts/service/service.rs"]
pub mod service;
#[path = "contracts/sockets/socket/mod.rs"]
pub mod socket;

// -- Public re-exports -------------------------------------------------------

pub use ctx::{AutoHwmProfile, Context, ContextOptions, has, version};
pub use domain::{Received, SendResult, SubscriptionEvent, TopicMessage};
pub use error::{
    BindError, BindResult, CloseError, CloseResult, ConfigError, ConfigResult, ConnectError,
    ConnectResult, HandlerError, HandlerResult, RecvError, RecvResult, RequestError, RequestResult,
    SubmitError, SubmitResult, ZlinkError,
};
pub use flags::{RecvFlags, SendFlags};
pub use message::{IntoMultipart, Message, RoutingId};
pub use monitor::{MONITOR_EVENT_ALL, MONITOR_EVENT_CONNECTION_READY, SocketMonitorEventMask};
pub use monitor::{
    MonitorEvent, MonitorEventType, MonitorSnapshot, MonitorSourceKind, MonitorTarget,
    SocketMonitor,
};
pub use options::{
    CommonSocketOptions, DealerSocketOptions, PubSocketOptions, RidDuplicatePolicy,
    RouterSocketOptions, StreamSocketOptions, SubSocketOptions,
};
pub use poller::{
    POLLIN, POLLOUT, PollEvent, PollItem, PollTarget, Poller, Stopwatch, Timer, poll,
};
pub use runtime::{multipart_close, proxy, proxy_steerable, sleep};
pub use service::{
    Actor, ActorJoinInfo, ActorJoinRequest, ActorPart, ActorRecvInfo, ActorRef, ActorRoute,
    AutoConnectType, CallbackReady, Discovery, Empty, MemberPeerEntry, Ready, Registry,
    RegistryQueryClient, RegistryServiceSummaryEntry, RegistryServiceSummaryFilter, RegistryState,
    RegistryStatus, RegistryTopologyEntry, RegistryTopologyFilter, ReplyOp, RequestOp, SendOp,
    ServiceKind, ServiceRole, SocketType, Spot, SpotDispatchEvent, SpotDispatchInfo,
    SpotDispatchSubject, SpotDispatchSubjectKind, SpotNode, SpotNodeActorEntry, SpotNodeMode,
    SpotNodeOptions, SpotNodePeerEntry, SpotNodePeerFilter, SpotNodeSocketOwner,
    SpotNodeSocketSnapshotEntry, SpotNodeSocketSnapshotFilter, SpotNodeSpotEntry, SpotNodeState,
    SpotNodeStatus, SpotNodeSubjectEntry, SpotNodeSubjectFilter, SpotPeerSource, SpotPeerState,
    SpotRole, SpotServiceAttachmentRole, SubjectKind, TopologySource, TopologyState,
};
pub use socket::{
    DealerSocket, PairSocket, PubSocket, RouterSocket, SendHandle, StreamSocket, SubSocket,
    XPubSocket, XSubSocket,
};
