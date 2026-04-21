// SPDX-License-Identifier: MPL-2.0

//! Rust bindings for the zlink messaging library.
//!
//! This crate wraps the zlink C API (`zlink.h`) with an idiomatic, safe Rust
//! surface following the bindings API policy:
//!
//! - **Multipart-only** public send/receive surface.
//! - **Blocking** vs **non-blocking** distinguished by flags (`send` / `send_with_flags`).
//! - **Typed options** per socket type – no raw option bags.
//! - **Ownership** via RAII: [`Message`] drop calls `zlink_msg_close`;
//!   `send` consumes messages and suppresses drop.
//! - **Message diagnostics** via [`Message::get_property`] and
//!   [`Message::ref_count`].
//! - **Domain objects**: [`Received`], [`TopicMessage`], [`SubscriptionEvent`],
//!   [`SendResult`], [`RoutingId`].
//! - **Socket capability isolation**: each socket type exposes only its own
//!   capabilities (e.g. `PubSocket` has no `recv`).

mod ffi;

mod ctx;
mod domain;
mod error;
mod flags;
mod message;
pub mod monitor;
mod options;
pub mod poller;
mod request_progress;
mod runtime;
pub mod service;
pub mod socket;

// -- Public re-exports -------------------------------------------------------

pub use ctx::{Context, ContextOptions, has, version};
pub use domain::{Received, SendResult, SubscriptionEvent, TopicMessage};
pub use error::{
    BindError, BindResult, CloseError, CloseResult, ConfigError, ConfigResult, ConnectError,
    ConnectResult, HandlerError, HandlerResult, RecvError, RecvResult, RequestError, RequestResult,
    SubmitError, SubmitResult, ZlinkError,
};
pub use flags::{RecvFlags, SendFlags};
pub use message::{IntoMultipart, Message, RoutingId};
pub use monitor::{
    MONITOR_EVENT_ALL, MONITOR_EVENT_CONNECTION_READY, SERVICE_MONITOR_EVENT_DISCOVERY_CLOSED,
    SERVICE_MONITOR_EVENT_DISCOVERY_ERROR, SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED,
    SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN, SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP,
    ServiceMonitorEventMask, SocketMonitorEventMask,
};
pub use monitor::{
    MonitorEvent, MonitorEventType, MonitorSnapshot, MonitorSourceKind, MonitorTarget,
    ServiceEvent, ServiceEventType, ServiceMonitor, ServiceMonitorTarget, SocketMonitor,
    SubjectKind,
};
pub use options::{
    CommonSocketOptions, DealerSocketOptions, PubSocketOptions, RouterSocketOptions,
    StreamSocketOptions, SubSocketOptions,
};
pub use poller::{
    POLLIN, POLLOUT, PollEvent, PollItem, PollTarget, Poller, Stopwatch, Timer, poll,
};
pub use runtime::{multipart_close, proxy, proxy_steerable, sleep};
pub use service::{
    AdmissionState, Discovery, DiscoveryDealerPeerMode, MemberPeerEntry, Registry,
    RegistryQueryClient, RegistryServiceSummaryEntry, RegistryServiceSummaryFilter, RegistryState,
    RegistryStatus, RegistryTopologyEntry, RegistryTopologyFilter, ServiceKind, ServiceRole,
    ServiceType, Spot, SpotDispatchEvent, SpotDispatchInfo, SpotDispatchSubjectKind, SpotNode,
    SpotNodePeerEntry, SpotNodePeerFilter, SpotNodeState, SpotNodeStatus, SpotNodeSubjectEntry,
    SpotNodeSubjectFilter, SpotPeerSource, SpotPeerState, SpotRole,
    SpotServiceAttachmentRole, TopologySource, TopologyState,
};
pub use socket::{
    DealerSocket, PairSocket, PubSocket, RouterSocket, SendHandle, StreamSocket, SubSocket,
    XPubSocket, XSubSocket,
};
