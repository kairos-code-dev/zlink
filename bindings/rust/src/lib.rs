// SPDX-License-Identifier: MPL-2.0

//! Rust bindings for the zlink messaging library.
//!
//! This crate wraps the zlink C API (`zlink.h`) with an idiomatic, safe Rust
//! surface following the bindings API policy:
//!
//! - **Multipart-only** public send/receive surface.
//! - **Blocking** vs **non-blocking** distinguished by name (`send` / `try_send`).
//! - **Explicit send outcome** via [`SendResult`] (not a plain `bool`).
//! - **Typed options** per socket type – no raw option bags.
//! - **Ownership** via RAII: [`Message`] drop calls `zlink_msg_close`;
//!   `send` consumes messages and suppresses drop.
//! - **Domain objects**: [`Received`], [`TopicMessage`], [`SubscriptionEvent`],
//!   [`SendResult`], [`RoutingId`].
//! - **Socket capability isolation**: each socket type exposes only its own
//!   capabilities (e.g. `PubSocket` has no `recv`).

mod ffi;

mod ctx;
mod domain;
mod error;
mod message;
pub mod monitor;
mod options;
pub mod poller;
pub mod service;
pub mod socket;

// -- Public re-exports -------------------------------------------------------

pub use ctx::{Context, has, version};
pub use domain::{Received, SendResult, SubscriptionEvent, TopicMessage};
pub use error::ZlinkError;
pub use message::{IntoMultipart, Message, RoutingId};
pub use monitor::{
    MONITOR_EVENT_ALL, MONITOR_EVENT_CONNECTION_READY,
    SERVICE_MONITOR_EVENT_DISCOVERY_CLOSED, SERVICE_MONITOR_EVENT_DISCOVERY_ERROR,
    SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED,
    SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN,
    SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP, SERVICE_MONITOR_EVENT_SPOT_CLOSED,
    SERVICE_MONITOR_EVENT_SPOT_ERROR, SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED,
    SERVICE_MONITOR_EVENT_SPOT_PEER_DOWN, SERVICE_MONITOR_EVENT_SPOT_PEER_UP,
    SERVICE_MONITOR_EVENT_SPOT_PUB_QUEUE_DRAINED, SERVICE_MONITOR_EVENT_SPOT_PUB_QUEUE_FULL,
    SERVICE_MONITOR_EVENT_CONNECTION_READY,
};
pub use monitor::{
    MonitorEvent, MonitorSnapshot, MonitorTarget, ServiceEvent, ServiceEventType, ServiceMonitor,
    ServiceMonitorTarget, SocketMonitor,
};
pub use options::{
    CommonSocketOptions, DealerSocketOptions, PubSocketOptions, RouterSocketOptions,
    StreamSocketOptions, SubSocketOptions,
};
pub use poller::{POLLIN, POLLOUT, PollEvent, PollTarget, Poller};
pub use service::{
    Discovery, MemberPeerEntry, Registry, RegistryQueryClient, RegistryServiceSummaryEntry,
    RegistryServiceSummaryFilter, RegistryState, RegistryStatus, RegistryTopologyEntry,
    RegistryTopologyFilter, ServiceKind, ServiceRole, ServiceType, Spot, SpotNode,
    SpotNodePeerEntry, SpotNodePeerFilter, SpotNodeState, SpotNodeStatus, SpotNodeSubjectEntry,
    SpotNodeSubjectFilter, SpotPeerSource, SpotPeerState, SpotRole, TopologySource, TopologyState,
};
pub use socket::{
    DealerSocket, PairSocket, PubSocket, RouterSocket, SendHandle, StreamSocket, SubSocket,
    XPubSocket, XSubSocket,
};
