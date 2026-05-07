[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Rust Binding Specification

This document defines the complete public API surface of the zlink Rust binding.
Every struct, method, and function listed here is part of the contract that the
binding must expose. Private/internal items are omitted.

Only the items re-exported as public crate API are part of the contract.
Private modules, `pub(crate)` helpers, FFI modules, and source-tree-only
support code are internal. Perf, samples, and tests must use the public crate
surface only and must not rely on internal modules.

## Design Basis

The Rust binding follows the repository POSD design policy. Public crate items
must hide native sequencing, ownership, and option encoding behind typed, deep
interfaces so callers do not need core implementation details.

The public Rust API must model stable domain concepts, not FFI call steps.
Public structs, enums, traits, and methods are justified when they own
context/socket lifetime, message ownership, receive metadata, service
membership, callbacks, or typed options. Raw FFI handles, part-loop sequencing,
request tokens, callback userdata, and raw option encoding stay in private or
`pub(crate)` modules.

Design review uses these POSD constraints:

- shared send/recv, nonblocking, ownership, and error mapping rules are
  centralized instead of copied across socket types
- canonical result and facade methods do not ask callers to pass state already
  captured by the receiver, such as a source socket, request sequence, or
  service address
- compatibility modules, if retained, are not the canonical API and are not
  used by new docs, samples, or tests
- a public wrapper that only forwards to FFI without adding validation,
  ownership, lifetime, or result-shape semantics is too shallow and must be
  removed or made private

---

## High-Performance Requirements

The Rust binding is part of a high-performance messaging library. Hot paths
must not use reflection-like type erasure, dynamic dispatch where static
dispatch is practical, unnecessary allocation, avoidable byte copies, coarse
lock contention, hidden waits, sleeps, busy waits, or thread joins. FFI bridge
code must construct public `Message` and result values directly from the core
`*_part` substrate and must not create native aggregate arrays only to copy
them into Rust `Vec` values.

## Core Alignment Rules

The detailed sections below are the canonical Rust binding contract. This
section states cross-cutting constraints once so the per-type API lists can
stay focused on signatures.

- `PairSocket`, `DealerSocket`, and `RouterSocket` keep their documented
  send, recv, request, and reply methods, but they do not expose direct
  data-plane receive callbacks such as `on_receive(...)`.
- Peer weight is exposed only on `RouterSocket` and `DealerSocket` through
  typed option/property surfaces. The value range is `0..100`, default
  `100`; `0` drains new outbound selection. Submit to a weight-`0` peer
  returns `Err(SubmitError { code: SubmitResult::NotAdmitted, .. })`.
- `POLLOUT` is a send-recovery readiness signal, shared with
  `on_send_ready(...)`. It is not a "transport writable" bit.
- ROUTER / PUB socket option defaults follow the core header: `mandatory =
  true`, `handover = false`, `nodrop = true`.
- SPOT admission HWM defaults follow the core header. Router and pubsub
  admission profile/numeric options are exposed through `SpotNode`; relay and
  delivery HWM stay `0` and are not public Rust options.
- SPOT dispatch worker min/max are `SpotNode` callback worker-pool options.
  They are not context options and must not be described as transport I/O
  threads.
- Internal pairing rule: when auto-connect pairs two same-service ROUTERs
  via Discovery, the library picks one initiator per pair by a total order
  on `(routing_id, advertise_endpoint)`. Users do not configure this.
- `SubSocket` and `XSubSocket` are receive-only topic sockets and do not
  expose direct topic callbacks such as `on_subscribe(...)`.
- `StreamSocket` keeps `recv` and exposes a packet callback surface mapped to
  `zlink_stream_packet_handler()` as `on_packet`.
- `SpotNode` must expose channel-aware attachment APIs:
  `attach_discovery(...)`,
  `attach_channel_dealer(...)`,
  `attach_channel_dealer_manual(...)`, and
  `attach_pub_ingress(...)`.
- `Spot` must expose channel-aware data-plane methods:
  `send_channel(...)`, `send_to_spot(...)`, `request_channel(...)`, and
  `publish(service_name, topic, ...)`.
- `Spot::subscribe(...)` returns a service-aware `TopicMessage`.
  `TopicMessage` therefore needs `service_name: Option<String>`, populated for
  SPOT subscribe results and `None` for raw `SUB` / `XSUB`.
- `Spot` must not expose `on_subscribe(...)`. Use `on_dispatch_event(...)`
  plus `subscribe(...)` / routed recv / timer recv.
- `SpotDispatchEvent::SubscribeReadable` and `::RoutedReadable` are readiness
  notifications, not one-event-per-message delivery counters. Binding docs and
  samples must drain until the recv path reports `EAGAIN`.
- `Spot::on_routed_receive(...)` and `Spot::on_dispatch_event(...)` are
  mutually exclusive on the routed axis.

## Actor Dispatch Public Surface

Rust exposes Actor dispatch through public crate items.

```rust
pub struct ActorRef { pub node_rid: RoutingId, pub actor_id: String, pub generation: u64 }
pub struct ActorCreateResult { pub status: ActorCreateStatus, pub actor: ActorRef }
pub struct ActorRoute { pub actor: ActorRef, pub joined: bool, pub joined_spot_rid: Option<RoutingId> }
pub struct ActorRecvInfo { pub actor: ActorRef, pub source_node_rid: RoutingId, pub source_session_rid: RoutingId, pub flags: u32 }
pub struct ActorJoinInfo {
    pub source_actor: ActorRef,
    pub target_actor: ActorRef,
    pub source_node_rid: RoutingId,
    pub source_spot_rid: RoutingId,
    pub target_node_rid: RoutingId,
    pub target_spot_rid: RoutingId,
    pub join_epoch: u64,
    pub flags: u32,
}
pub struct ActorJoinRequest { pub info: ActorJoinInfo, pub message: Message }
impl SpotNode {
    pub fn remote_actor_ref(target_node_rid: &RoutingId, actor_id: &str)
        -> Result<ActorRef, ConfigError>;
}
```

`SpotNode` exposes local Actor lifecycle, remote create-or-get, admission,
join/leave, and Actor snapshots. `Spot` exposes Actor join receive/reply and
joined Actor snapshots. `StreamSocket` exposes Actor bind/unbind and bound
Actor send. `Discovery` exposes Actor route resolve.

`generation == 0` is an unchecked remote ref and is not invalid. Actor handles
are represented by `ActorRef`; public `void *actor` handles are not exposed.
Actor IDs are non-empty UTF-8 strings up to 255 bytes and must not contain NUL.
Leaving a Spot does not drain unread Actor messages. Remote actor creation is
create-or-get: the admission callback runs only when the target actor is missing.
Every Actor belongs to exactly one Spot. The Entry Spot is the default Spot
assigned immediately after Actor creation and cannot be removed by the
application. Joining a user Spot requires the Actor to have a bound STREAM
session. One Actor may bind to one STREAM session; one session may bind many
Actors. Per-Actor queue limit options are not part of the Rust public surface.

## Core

### Context

```rust
impl Context {
    /// # Errors: ConfigError
    pub fn new() -> Result<Self, ConfigError>;
    /// # Errors: CloseError
    pub fn shutdown(&self) -> Result<(), CloseError>;
    /// # Errors: ConfigError
    pub fn recalculate_auto_hwm(&self) -> Result<(), ConfigError>;
    pub fn options(&self) -> ContextOptions<'_>;

    // Socket factories — each returns ConfigError on failure.
    /// # Errors: ConfigError
    pub fn pair_socket(&self) -> Result<PairSocket, ConfigError>;
    /// # Errors: ConfigError
    pub fn pub_socket(&self) -> Result<PubSocket, ConfigError>;
    /// # Errors: ConfigError
    pub fn sub_socket(&self) -> Result<SubSocket, ConfigError>;
    /// # Errors: ConfigError
    pub fn dealer_socket(&self) -> Result<DealerSocket, ConfigError>;
    /// # Errors: ConfigError
    pub fn router_socket(&self) -> Result<RouterSocket, ConfigError>;
    /// # Errors: ConfigError
    pub fn xpub_socket(&self) -> Result<XPubSocket, ConfigError>;
    /// # Errors: ConfigError
    pub fn xsub_socket(&self) -> Result<XSubSocket, ConfigError>;
    /// # Errors: ConfigError
    pub fn stream_socket(&self) -> Result<StreamSocket, ConfigError>;
}
// Drop calls zlink_ctx_term
```

### ContextOptions

```rust
// All ContextOptions getters/setters return ConfigError on failure.
impl<'a> ContextOptions<'a> {
    /// # Errors: ConfigError
    pub fn io_threads(&self) -> Result<i32, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_io_threads(&self, threads: i32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn max_sockets(&self) -> Result<i32, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_max_sockets(&self, max: i32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn socket_limit(&self) -> Result<i32, ConfigError>;
    /// # Errors: ConfigError
    pub fn thread_priority(&self) -> Result<i32, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_thread_priority(&self, priority: i32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn thread_scheduling_policy(&self) -> Result<i32, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_thread_scheduling_policy(&self, policy: i32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn thread_name_prefix(&self) -> Result<String, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_thread_name_prefix(&self, prefix: &str) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn max_msg_size(&self) -> Result<i32, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_max_msg_size(&self, size: i32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn msg_t_size(&self) -> Result<i32, ConfigError>;
    /// # Errors: ConfigError
    pub fn blocky(&self) -> Result<bool, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_blocky(&self, blocky: bool) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn auto_hwm_enabled(&self) -> Result<bool, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_auto_hwm_enabled(&self, enabled: bool) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn auto_hwm_recalc_debounce(&self) -> Result<Duration, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_auto_hwm_recalc_debounce(&self, value: Duration) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn auto_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_auto_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn add_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn remove_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError>;
}
```

The native context memory-budget and bootstrap auto-HWM options are
deprecated no-op compatibility options. The Rust binding does not expose typed
methods for them.

```rust
pub enum AutoHwmProfile {
    Compact,
    LowLatency,
    Balanced,
    Throughput,
}
```

### Module-Level Functions

```rust
pub fn version() -> (i32, i32, i32);
pub fn has(capability: &str) -> bool;

/// Start a built-in proxy between frontend and backend sockets.
/// # Errors: ConfigError
pub fn proxy<'a>(frontend: impl Into<PollTarget<'a>>,
                 backend: impl Into<PollTarget<'a>>,
                 capture: Option<impl Into<PollTarget<'a>>>) -> Result<(), ConfigError>;

/// Start a steerable proxy with an additional control socket.
/// # Errors: ConfigError
pub fn proxy_steerable<'a>(frontend: impl Into<PollTarget<'a>>,
                           backend: impl Into<PollTarget<'a>>,
                           capture: Option<impl Into<PollTarget<'a>>>,
                           control: impl Into<PollTarget<'a>>) -> Result<(), ConfigError>;

/// Sleep for the given number of seconds.
pub fn sleep(seconds: i32);

/// Close all parts in a multipart message array.
pub fn multipart_close(parts: &mut [Message]);
```

### Errno / Strerror

Errors are mapped through per-category error types (see
[Per-Function Error Types](#per-function-error-types)) which all
implement `std::error::Error` and `Display` and convert into the
top-level `ZlinkError` enum via `From`. The C-level `zlink_errno()` and
`zlink_strerror()` are used internally to construct error values
returned via `Result`. Direct access is not exposed.

---

## Send / Recv Flags

```rust
bitflags! {
    pub struct SendFlags: u32 {
        const NONE = 0;
        const DONT_WAIT = 0x0001;
    }
}
```

```rust
bitflags! {
    pub struct RecvFlags: u32 {
        const NONE = 0;
        const DONT_WAIT = 0x0001;
    }
}
```

---

## Socket Types

All sockets implement `Drop` (calls `close`) and expose common connection and
option method groups as direct methods on each socket type. The internal owner
that implements those methods is not part of this contract.

Rust nonblocking data-plane helpers follow this rule:

- `_with_flags(...)` submit variants return `Ok(false)` only for temporary
  backpressure when `SendFlags::DontWait` is used.
- Route-not-ready and other submit failures still return
  `Err(SubmitError)`.
- `_with_flags(...)` receive variants return `Ok(None)` when no message is
  currently available and still return `Err(RecvError)` for real recv failures.

Peer weight is not part of `CommonSocketOptions`. Rust exposes weight only on
`RouterSocket` and `DealerSocket`:

```rust
impl<'a, S> CommonSocketOptions<'a, S> {
    // No common peer-weight accessor. RouterSocket and DealerSocket expose weight on their typed option facade.
}
```

### PairSocket

```rust
impl PairSocket {
    /// # Errors: BindError
    pub fn bind(&self, addr: &str) -> Result<(), BindError>;
    /// # Errors: ConnectError
    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn connect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect_rid(&self, rid: &RoutingId) -> Result<(), ConnectError>;
    /// # Errors: SubmitError
    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_with_flags(&self, parts: impl IntoMultipart, flags: SendFlags) -> Result<bool, SubmitError>;
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Option<Received>, RecvError>;
    /// # Errors: HandlerError
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn() + Send + 'static;
    pub fn send_handle(&self) -> SendHandle;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### PubSocket

```rust
impl PubSocket {
    /// # Errors: BindError
    pub fn bind(&self, addr: &str) -> Result<(), BindError>;
    /// # Errors: ConnectError
    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn connect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect_rid(&self, rid: &RoutingId) -> Result<(), ConnectError>;
    /// # Errors: SubmitError
    pub fn publish(&self, topic: &str, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn publish_with_flags(&self, topic: &str, parts: impl IntoMultipart, flags: SendFlags) -> Result<bool, SubmitError>;
    /// # Errors: HandlerError
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn() + Send + 'static;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn pub_options(&self) -> PubSocketOptions<'_, Self>;
    /// # Errors: ConfigError
    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError>;
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### SubSocket

```rust
impl SubSocket {
    /// # Errors: BindError
    pub fn bind(&self, addr: &str) -> Result<(), BindError>;
    /// # Errors: ConnectError
    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn connect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect_rid(&self, rid: &RoutingId) -> Result<(), ConnectError>;
    /// # Errors: ConfigError
    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    /// # Errors: RecvError
    pub fn subscribe(&self) -> Result<TopicMessage, RecvError>;
    /// # Errors: RecvError
    pub fn subscribe_with_flags(&self, flags: RecvFlags) -> Result<Option<TopicMessage>, RecvError>;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn sub_options(&self) -> SubSocketOptions<'_, Self>;
    /// # Errors: ConfigError
    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError>;
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### DealerSocket

```rust
impl DealerSocket {
    /// # Errors: BindError
    pub fn bind(&self, addr: &str) -> Result<(), BindError>;
    /// # Errors: ConnectError
    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn connect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect_rid(&self, rid: &RoutingId) -> Result<(), ConnectError>;
    /// # Errors: ConfigError
    pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn routing_id(&self) -> Result<RoutingId, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_channel_name(&self, channel_name: &str) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn channel_name(&self) -> Result<String, ConfigError>;
    /// # Errors: SubmitError
    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_with_flags(&self, parts: impl IntoMultipart, flags: SendFlags) -> Result<bool, SubmitError>;
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Option<Received>, RecvError>;
    /// # Errors: HandlerError
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn() + Send + 'static;

    // --- dealer request (async) — no flags ---
    // `timeout = None` uses the socket default request timeout.
    /// # Errors: SubmitError on submit, RequestError on completion
    pub async fn request(&self, parts: &[&[u8]], timeout: Option<Duration>)
        -> Result<Vec<Message>, RequestError>;

    // --- dealer request (callback, blocking submit) ---
    // `timeout = None` uses the socket default request timeout.
    // The callback receives Result<Vec<Message>, RequestError>.
    /// # Errors: SubmitError on submit; callback receives Result<Vec<Message>, RequestError>
    pub fn request_callback<F: FnOnce(Result<Vec<Message>, RequestError>) + 'static>(
        &self, parts: &[&[u8]], cb: F, timeout: Option<Duration>)
        -> Result<(), SubmitError>;
    pub fn send_handle(&self) -> SendHandle;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn dealer_options(&self) -> DealerSocketOptions<'_>;
    /// # Errors: ConfigError
    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError>;
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### RouterSocket

```rust
impl RouterSocket {
    /// # Errors: BindError
    pub fn bind(&self, addr: &str) -> Result<(), BindError>;
    /// # Errors: ConnectError
    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn connect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect_rid(&self, rid: &RoutingId) -> Result<(), ConnectError>;
    /// # Errors: ConfigError
    pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn routing_id(&self) -> Result<RoutingId, ConfigError>;
    /// # Errors: SubmitError
    pub fn send(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_with_flags(&self, target: &RoutingId, parts: impl IntoMultipart,
                           flags: SendFlags) -> Result<bool, SubmitError>;
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Option<Received>, RecvError>;
    /// # Errors: HandlerError
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn() + Send + 'static;
    pub fn send_handle(&self) -> SendHandle;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn router_options(&self) -> RouterSocketOptions<'_>;
    /// # Errors: ConfigError
    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError>;

    // --- router request (async) — no flags ---
    // `timeout = None` uses the socket default request timeout.
    /// # Errors: SubmitError on submit, RequestError on completion
    pub async fn request(&self, peer_rid: &RoutingId, parts: &[&[u8]],
        timeout: Option<Duration>) -> Result<Vec<Message>, RequestError>;

    // --- router request (callback, blocking submit) ---
    // `timeout = None` uses the socket default request timeout.
    // The callback receives Result<Vec<Message>, RequestError>.
    /// # Errors: SubmitError on submit; callback receives Result<Vec<Message>, RequestError>
    pub fn request_callback<F: FnOnce(Result<Vec<Message>, RequestError>) + 'static>(
        &self, peer_rid: &RoutingId, parts: &[&[u8]], cb: F,
        timeout: Option<Duration>) -> Result<(), SubmitError>;
    // --- router reply ---
    /// # Errors: SubmitError
    pub fn reply(&self, rid: &RoutingId, request_seq: u64,
        parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn reply_with_flags(&self, rid: &RoutingId, request_seq: u64,
        parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;

    // --- router → spot routed send ---
    /// # Errors: SubmitError
    pub fn send_to_spot(&self, dest_node_rid: &RoutingId, dest_spot_rid: &RoutingId,
        parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_to_spot_with_flags(&self, dest_node_rid: &RoutingId, dest_spot_rid: &RoutingId,
        parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;

    // --- router → spot routed request (async) — no flags ---
    // Duration::ZERO uses the socket default timeout.
    // Submit failure yields SubmitError; request failure (timeout, etc.)
    // yields RequestError; both unify under ZlinkError at this API seam.
    /// # Errors: ZlinkError (SubmitError on submit, RequestError on completion)
    pub async fn request_to_spot(&self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, parts: impl IntoMultipart,
        timeout: Duration) -> Result<Vec<Message>, ZlinkError>;

    // --- router → spot routed request (callback) ---
    // Duration::ZERO uses the socket default timeout.
    // The callback receives Result<Vec<Message>, RequestError>.
    /// # Errors: SubmitError (submit failure). Callback receives Result<Vec<Message>, RequestError>.
    pub fn request_to_spot_callback<F>(&self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, parts: impl IntoMultipart, callback: F,
        flags: SendFlags, timeout: Duration)
        -> Result<(), SubmitError>
        where F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;

    // --- router → spot routed reply ---
    /// # Errors: SubmitError
    pub fn reply_to_spot(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        request_seq: u64, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn reply_to_spot_with_flags(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        request_seq: u64, parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;

    // NOTE: RouterSocket has one routed receive surface. recv and
    // recv_with_flags receive both regular ROUTER traffic and spot-origin
    // routed traffic. `Received::routing_id()` is source_node_rid, and
    // `Received::spot_rid()` is set only for spot-origin traffic. ROUTER does
    // not expose a data-plane callback install surface such as on_receive.
    // Request completion remains available only through request().

    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### XPubSocket

```rust
impl XPubSocket {
    /// # Errors: BindError
    pub fn bind(&self, addr: &str) -> Result<(), BindError>;
    /// # Errors: ConnectError
    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn connect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect_rid(&self, rid: &RoutingId) -> Result<(), ConnectError>;
    /// # Errors: SubmitError
    pub fn publish(&self, topic: &str, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn publish_with_flags(&self, topic: &str, parts: impl IntoMultipart, flags: SendFlags) -> Result<bool, SubmitError>;
    /// # Errors: RecvError
    pub fn receive_subscription_event(&self) -> Result<SubscriptionEvent, RecvError>;
    /// # Errors: RecvError
    pub fn receive_subscription_event_with_flags(&self, flags: RecvFlags) -> Result<Option<SubscriptionEvent>, RecvError>;
    /// # Errors: HandlerError
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn() + Send + 'static;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn pub_options(&self) -> PubSocketOptions<'_, Self>;
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### XSubSocket

```rust
impl XSubSocket {
    /// # Errors: BindError
    pub fn bind(&self, addr: &str) -> Result<(), BindError>;
    /// # Errors: ConnectError
    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn connect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect_rid(&self, rid: &RoutingId) -> Result<(), ConnectError>;
    /// # Errors: ConfigError
    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    /// # Errors: RecvError
    pub fn subscribe(&self) -> Result<TopicMessage, RecvError>;
    /// # Errors: RecvError
    pub fn subscribe_with_flags(&self, flags: RecvFlags) -> Result<Option<TopicMessage>, RecvError>;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn sub_options(&self) -> SubSocketOptions<'_, Self>;
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### StreamSocket

```rust
impl StreamSocket {
    /// # Errors: BindError
    pub fn bind(&self, addr: &str) -> Result<(), BindError>;
    /// # Errors: ConnectError
    pub fn unbind(&self, addr: &str) -> Result<(), ConnectError>;
    /// # Errors: ConfigError
    pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn routing_id(&self) -> Result<RoutingId, ConfigError>;
    /// # Errors: ConnectError
    pub fn disconnect_rid(&self, rid: &RoutingId) -> Result<(), ConnectError>;
    /// # Errors: SubmitError
    pub fn send(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_with_flags(&self, target: &RoutingId, parts: impl IntoMultipart,
                           flags: SendFlags) -> Result<bool, SubmitError>;
    /// Two mutually-exclusive receive modes on the same StreamSocket:
    ///   (1) recv(), (2) on_packet(handler). Second attach returns
    ///   Err(HandlerError { code: HandlerResult::Busy, .. }).
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Option<Received>, RecvError>;
    /// Mode (3): framed packet callback mapped to
    /// `zlink_stream_packet_handler`. Wire frame is big-endian `u16`
    /// header_size + `u32` body_size + header + body. The handler receives
    /// the source routing id, a header `Message`, and a body `Message`;
    /// both messages transfer ownership to the handler.
    /// # Errors: HandlerError
    pub fn on_packet<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(RoutingId, Message, Message) + Send + 'static;
    /// Bind an Actor to one STREAM session before user Spot join.
    /// # Errors: RequestError
    pub fn bind_actor(&self, node: &SpotNode, session_rid: &RoutingId,
        actor: &ActorRef, timeout: Duration) -> Result<(), RequestError>;
    /// # Errors: RequestError
    pub fn unbind_actor(&self, node: &SpotNode, session_rid: &RoutingId,
        actor_id: &str, timeout: Duration) -> Result<(), RequestError>;
    /// # Errors: SubmitError
    pub fn send_bound_actor_part(&self, node: &SpotNode, session_rid: &RoutingId,
        actor_id: &str, parts: impl IntoMultipart, flags: SendFlags)
        -> Result<(), SubmitError>;
    /// # Errors: HandlerError
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn() + Send + 'static;
    pub fn send_handle(&self) -> SendHandle;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn stream_options(&self) -> StreamSocketOptions<'_>;
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### SendHandle

A thread-safe handle for sending from callbacks.

```rust
impl SendHandle {
    /// # Errors: SubmitError
    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_with_flags(&self, parts: impl IntoMultipart, flags: SendFlags) -> Result<bool, SubmitError>;
    /// # Errors: SubmitError
    pub fn send_to(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_to_with_flags(&self, target: &RoutingId, parts: impl IntoMultipart,
        flags: SendFlags) -> Result<(), SubmitError>;
}
```

### Socket Option Facades

All socket option facade getters and setters return `ConfigError` on failure.
Peer weight is only exposed by `DealerSocketOptions` and
`RouterSocketOptions`; it is not part of `CommonSocketOptions`.

```rust
pub enum RidDuplicatePolicy {
    Reject,
    Handover,
}

impl<'a, S> CommonSocketOptions<'a, S> {
    pub fn rid_duplicate_policy(&self) -> Result<RidDuplicatePolicy, ConfigError>;
    pub fn set_rid_duplicate_policy(&self, value: RidDuplicatePolicy)
        -> Result<(), ConfigError>;
    pub fn auto_hwm_msg_unit_bytes(&self) -> Result<i32, ConfigError>;
    pub fn set_auto_hwm_msg_unit_bytes(&self, value: i32)
        -> Result<(), ConfigError>;
}

impl<'a> DealerSocketOptions<'a> {
    pub fn weight(&self) -> Result<u32, ConfigError>;
    pub fn set_weight(&self, value: u32) -> Result<(), ConfigError>;
}

impl<'a> RouterSocketOptions<'a> {
    pub fn weight(&self) -> Result<u32, ConfigError>;
    pub fn set_weight(&self, value: u32) -> Result<(), ConfigError>;
}
```

---

## Message / Domain

### Message

```rust
pub struct Message { /* ... */ }

impl Message {
    /// # Errors: ConfigError
    pub fn new() -> Result<Self, ConfigError>;
    /// # Errors: ConfigError
    pub fn with_size(size: usize) -> Result<Self, ConfigError>;
    /// Copy bytes into an owned zlink message.
    /// # Errors: ConfigError
    pub fn copy_from(data: &[u8]) -> Result<Self, ConfigError>;
    /// Copy a `bytes::Bytes` buffer into an owned zlink message.
    /// # Errors: ConfigError
    pub fn copy_from_bytes(data: bytes::Bytes) -> Result<Self, ConfigError>;
    /// Copy a `bytes::BytesMut` buffer into an owned zlink message.
    /// # Errors: ConfigError
    pub fn copy_from_bytes_mut(data: bytes::BytesMut) -> Result<Self, ConfigError>;
    /// Copy bytes into an owned zlink message.
    /// # Errors: ConfigError
    pub fn from_bytes(data: &[u8]) -> Result<Self, ConfigError>;
    pub fn as_bytes(&self) -> &[u8];
    pub fn size(&self) -> usize;
    pub fn ref_count(&self) -> i32;
}

// Message implements Drop (calls zlink_msg_close).
// Message implements IntoMultipart.
// Public input adapters are copy-based only; generic external-buffer attach
// with a release hook is not part of the Rust public surface.
```

### Codec Extensions

Codec adapters are separate public extension crates layered on top of the core
crate. Their contract lives in
[Rust Codec Extension Specification](codec.md). The core `zlink` crate does
not expose codec entrypoints or require codec dependencies.

### RoutingId

Binary-safe routing id value object (1-255 bytes). Immutable; the public
API treats the identifier as raw bytes. String conversions are provided
as convenience only.

```rust
#[derive(Clone, PartialEq, Eq, Hash)]
pub struct RoutingId { /* ... */ }

impl RoutingId {
    /// Construct from raw bytes (must be 1-255 bytes).
    pub fn from_bytes(bytes: &[u8]) -> RoutingId;
    /// Parse the hex string returned by `to_hex()` / `Display`.
    /// Hex input must be at most 510 chars and decode to 1-255 bytes.
    /// Panics if the input is not hex or decodes above 255 bytes.
    pub fn from_string(value: &str) -> RoutingId;
    /// Parse the hex string returned by `to_hex()` / `Display`.
    /// Hex input must be at most 510 chars and decode to 1-255 bytes.
    /// Returns ConfigError if the input is not hex or decodes above 255 bytes.
    pub fn try_from_string(value: &str) -> Result<RoutingId, ConfigError>;
    /// Borrow the raw byte view (immutable).
    pub fn as_bytes(&self) -> &[u8];
    /// Byte length (1-255).
    pub fn size(&self) -> usize;
    /// Hex-encoded convenience string.
    pub fn to_hex(&self) -> String;
}

impl std::fmt::Display for RoutingId { /* hex form */ }
```

### Received

PAIR / DEALER / ROUTER recv result. Mirrors `TopicMessage` but without a
topic field. `Drop` releases owned `Message` parts; explicit `close()`
is available for deterministic cleanup.

```rust
pub struct Received {
    pub routing_id: Option<RoutingId>,   // peer_rid (Router) / source_node_rid (Spot)
    pub spot_rid: Option<RoutingId>,     // Set only for SPOT routed recv
    pub request_seq: Option<u64>,        // Set in request-reply mode, else None
    pub parts: Vec<Message>,
    // non-public: source socket ref (Arc<SocketInner>) for reply()
}

impl Received {
    pub fn is_single_part(&self) -> bool;
    /// # Errors: RecvError
    pub fn first_part(&self) -> Result<&Message, RecvError>;
    /// # Errors: RecvError
    pub fn single_part_or_error(self) -> Result<Message, RecvError>;

    /// Reply to this received request. Only valid when `request_seq` is
    /// `Some(..)`; otherwise returns `SubmitError` for invalid reply
    /// context. On submit failure
    /// returns `SubmitError`. routing_id / spot_rid / request_seq are
    /// encapsulated — caller does not pass them again.
    /// # Errors: SubmitError on invalid reply context or submit failure.
    pub fn reply(&self, parts: Vec<Message>) -> Result<(), SubmitError>;
    /// # Errors: SubmitError on invalid reply context or submit failure.
    pub fn reply_with_flags(
        &self,
        parts: Vec<Message>,
        flags: SendFlags,
    ) -> Result<(), SubmitError>;

    /// # Errors: CloseError
    pub fn close(self) -> Result<(), CloseError>;
}
// Drop releases any remaining parts.
```

### TopicMessage

Topic-aware recv result used by SUB / XSUB / Spot subscribe paths.
`Drop` releases owned `Message` parts; explicit `close()` is available
for deterministic cleanup.

```rust
pub struct TopicMessage {
    pub routing_id: Option<RoutingId>,   // None when transport carries no source id
    pub service_name: Option<String>,    // Spot subscribe only; None for raw SUB / XSUB
    pub topic: String,                   // UTF-8
    pub parts: Vec<Message>,
}

impl TopicMessage {
    pub fn is_single_part(&self) -> bool;
    /// # Errors: RecvError
    pub fn first_part(&self) -> Result<&Message, RecvError>;
    /// # Errors: RecvError
    pub fn single_part_or_error(self) -> Result<Message, RecvError>;
    /// # Errors: CloseError
    pub fn close(self) -> Result<(), CloseError>;
}
// Drop releases any remaining parts.
```

### SubscriptionEvent

Value struct emitted by `XPubSocket::receive_subscription_event` and
`Spot::receive_subscription_event`. Pure value type: no methods, no
lifecycle. `topic` is UTF-8 `String` (never raw bytes).

```rust
pub struct SubscriptionEvent {
    pub routing_id: Option<RoutingId>,   // None when transport carries no source id
    pub service_name: Option<String>,    // Spot subscription event only; None for XPub
    pub topic: String,                   // UTF-8 subscribe/unsubscribe topic
    pub subscribed: bool,                // true = subscribe, false = unsubscribe
}
```

### SubmitResult

Result codes for send/request/reply/publish operations. Surfaced by
`SubmitError::code()`; the top-level `ZlinkError::code()` returns a
globally unique `i32` that spans all result enum ranges (0-706).

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum SubmitResult {
    Ok = 0,
    Backpressured = 1,
    NotConnected = 2,
    NotFound = 3,
    Terminated = 4,
    InvalidHandle = 5,
    InvalidArgument = 6,
    NotSupported = 7,
    InvalidState = 8,
    ThreadViolation = 9,
    OutOfMemory = 10,
    SeqExhausted = 11,
    InternalError = 12,
    NotAdmitted = 13,   // target peer has weight 0
}
```

### RequestResult

Result codes for request completion callbacks.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum RequestResult {
    Ok = 0,
    TimedOut = 101,
    NotFound = 102,
    Terminated = 103,
    ProtocolError = 104,
    InternalError = 105,
    Rejected = 106,
    Conflict = 107,
    Busy = 108,
    NotConnected = 109,
    InvalidArgument = 110,
    InvalidState = 111,
    NotSupported = 112,
}
```

### RecvResult

Result codes for recv, subscribe, and subscription event operations.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum RecvResult {
    Ok = 0,
    NoData = 201,
    Busy = 202,
    Terminated = 203,
    InvalidHandle = 204,
    NotSupported = 205,
    InternalError = 206,
}
```

### HandlerResult

Result codes for handler registration operations (`on_packet`,
`on_send_ready`, `on_routed_receive`, `on_dispatch_event`, etc.).

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum HandlerResult {
    Ok = 0,
    InvalidArgument = 301,
    Busy = 302,
    NotSupported = 303,
    Deadlock = 304,
    InvalidHandle = 305,
    InternalError = 306,
}
```

### CloseResult

Result codes for close and destroy operations.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum CloseResult {
    Ok = 0,
    Busy = 401,
    Shutdown = 402,
    InvalidHandle = 403,
    InternalError = 404,
}
```

### BindResult

Result codes for bind operations.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum BindResult {
    Ok = 0,
    InvalidArgument = 501,
    AddrInUse = 502,
    NotSupported = 503,
    InvalidHandle = 504,
    InternalError = 505,
}
```

### ConnectResult

Result codes for connect, disconnect, and unbind operations.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum ConnectResult {
    Ok = 0,
    InvalidArgument = 601,
    NotSupported = 602,
    InvalidHandle = 603,
    InternalError = 604,
    NotFound = 605,
    Conflict = 606,
    Busy = 607,
}
```

### ConfigResult

Result codes for configuration, option, and snapshot operations.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum ConfigResult {
    Ok = 0,
    InvalidHandle = 701,
    InvalidArgument = 702,
    NotSupported = 703,
    InternalError = 704,
    InvalidState = 705,
    NotFound = 706,
}
```

## Per-Function Error Types

The Rust binding mirrors the C API's **eight function-category result
enums** as eight concrete error struct types, one per function category.
Each struct implements `std::error::Error` + `Display`, converts into
the top-level `ZlinkError` via `From`/`Into`, and carries its
category-specific result code enum plus the OS errno.

Function signatures return `Result<T, <Category>Error>` naming the
specific category. Callers that need to handle any zlink failure catch
`ZlinkError`; callers that need to narrow to a category match or
downcast to the specific struct.

### ZlinkError

`ZlinkError` is the top-level error enum that aggregates all eight
per-category error types. Every per-category error converts into
`ZlinkError` via `From`, so callers may write
`fn foo() -> Result<(), ZlinkError>` at API seams where multiple
categories can fail.

```rust
#[derive(Debug)]
pub enum ZlinkError {
    Submit(SubmitError),
    Request(RequestError),
    Recv(RecvError),
    Handler(HandlerError),
    Close(CloseError),
    Bind(BindError),
    Connect(ConnectError),
    Config(ConfigError),
}

impl ZlinkError {
    /// Globally unique code spanning all result enum ranges (0-706).
    /// The code alone identifies the error without needing to know
    /// which enum it belongs to.
    pub fn code(&self) -> i32;
    /// Underlying OS errno (0 if not applicable).
    pub fn internal_errno(&self) -> i32;
}

impl std::fmt::Display for ZlinkError { /* ... */ }
impl std::error::Error for ZlinkError { /* ... */ }

impl From<SubmitError>  for ZlinkError { /* ... */ }
impl From<RequestError> for ZlinkError { /* ... */ }
impl From<RecvError>    for ZlinkError { /* ... */ }
impl From<HandlerError> for ZlinkError { /* ... */ }
impl From<CloseError>   for ZlinkError { /* ... */ }
impl From<BindError>    for ZlinkError { /* ... */ }
impl From<ConnectError> for ZlinkError { /* ... */ }
impl From<ConfigError>  for ZlinkError { /* ... */ }
```

### SubmitError

Errors from `send` / `publish` / `reply` / request submit operations.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SubmitError {
    pub code: SubmitResult,
    pub internal_errno: i32,
}

impl SubmitError {
    pub fn code(&self) -> SubmitResult;
    pub fn internal_errno(&self) -> i32;
}

impl std::fmt::Display for SubmitError { /* ... */ }
impl std::error::Error for SubmitError {}
impl From<SubmitError> for ZlinkError { /* wraps as ZlinkError::Submit */ }
```

### RequestError

Errors from request completion (delivered via callback parameter).

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RequestError {
    pub code: RequestResult,
    pub internal_errno: i32,
}

impl RequestError {
    pub fn code(&self) -> RequestResult;
    pub fn internal_errno(&self) -> i32;
}

impl std::fmt::Display for RequestError { /* ... */ }
impl std::error::Error for RequestError {}
impl From<RequestError> for ZlinkError { /* wraps as ZlinkError::Request */ }
```

### RecvError

Errors from `recv` / `subscribe` / `receive_subscription_event` /
monitor recv / timer recv operations.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RecvError {
    pub code: RecvResult,
    pub internal_errno: i32,
}

impl RecvError {
    pub fn code(&self) -> RecvResult;
    pub fn internal_errno(&self) -> i32;
}

impl std::fmt::Display for RecvError { /* ... */ }
impl std::error::Error for RecvError {}
impl From<RecvError> for ZlinkError { /* wraps as ZlinkError::Recv */ }
```

### HandlerError

Errors from handler registration (`on_packet`, `on_send_ready`,
`on_event`, `on_fire`, `on_dispatch_event`, `on_routed_receive`, etc.).

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HandlerError {
    pub code: HandlerResult,
    pub internal_errno: i32,
}

impl HandlerError {
    pub fn code(&self) -> HandlerResult;
    pub fn internal_errno(&self) -> i32;
}

impl std::fmt::Display for HandlerError { /* ... */ }
impl std::error::Error for HandlerError {}
impl From<HandlerError> for ZlinkError { /* wraps as ZlinkError::Handler */ }
```

### CloseError

Errors from `close` / `destroy` operations.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CloseError {
    pub code: CloseResult,
    pub internal_errno: i32,
}

impl CloseError {
    pub fn code(&self) -> CloseResult;
    pub fn internal_errno(&self) -> i32;
}

impl std::fmt::Display for CloseError { /* ... */ }
impl std::error::Error for CloseError {}
impl From<CloseError> for ZlinkError { /* wraps as ZlinkError::Close */ }
```

### BindError

Errors from `bind` operations.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BindError {
    pub code: BindResult,
    pub internal_errno: i32,
}

impl BindError {
    pub fn code(&self) -> BindResult;
    pub fn internal_errno(&self) -> i32;
}

impl std::fmt::Display for BindError { /* ... */ }
impl std::error::Error for BindError {}
impl From<BindError> for ZlinkError { /* wraps as ZlinkError::Bind */ }
```

### ConnectError

Errors from `connect` / `disconnect` / `unbind` operations.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ConnectError {
    pub code: ConnectResult,
    pub internal_errno: i32,
}

impl ConnectError {
    pub fn code(&self) -> ConnectResult;
    pub fn internal_errno(&self) -> i32;
}

impl std::fmt::Display for ConnectError { /* ... */ }
impl std::error::Error for ConnectError {}
impl From<ConnectError> for ZlinkError { /* wraps as ZlinkError::Connect */ }
```

### ConfigError

Errors from option get/set, snapshot, poller mutation, timer start/stop,
`attach_discovery`, message lifecycle, and TLS configuration operations.

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ConfigError {
    pub code: ConfigResult,
    pub internal_errno: i32,
}

impl ConfigError {
    pub fn code(&self) -> ConfigResult;
    pub fn internal_errno(&self) -> i32;
}

impl std::fmt::Display for ConfigError { /* ... */ }
impl std::error::Error for ConfigError {}
impl From<ConfigError> for ZlinkError { /* wraps as ZlinkError::Config */ }
```

### IntoMultipart trait

```rust
pub trait IntoMultipart {
    fn into_multipart(self) -> Vec<Message>;
}
// Implemented for Message, Vec<Message>, &[u8], Vec<u8>,
// bytes::Bytes, bytes::BytesMut, etc.
```

---

## Monitoring

### SocketMonitor

Starts in recv model. `on_event(...)` transitions one-way to callback-only
model; after that `recv()` returns a busy recv error and `snapshot()` still works.

```rust
impl SocketMonitor {
    /// # Errors: ConfigError
    pub fn open<'a>(socket: impl Into<MonitorTarget<'a>>) -> Result<Self, ConfigError>;
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<MonitorEvent, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Option<MonitorEvent>, RecvError>;
    /// # Errors: ConfigError
    pub fn snapshot(&self) -> Result<MonitorSnapshot, ConfigError>;
    /// # Errors: HandlerError
    pub fn on_event<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(&MonitorEvent) + Send + 'static;
    /// No-op callback for callback-only model. Pass to `on_event` to keep a
    /// valid handler when the application does not care about events; once
    /// installed the monitor is in callback-only model and `recv()` returns
    /// a busy recv error (`snapshot()` still works). To drive the monitor
    /// via `snapshot()` / `recv()` instead, leave `on_event` unset.
    /// Maps to `zlink_monitor_ignore_handler`.
    pub fn ignore_handler() -> fn(&MonitorEvent);
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### MonitorSnapshot

```rust
pub struct MonitorSnapshot {
    pub source_kind: MonitorSourceKind,   // monitor target category
    pub state_flags: u32,                 // state bitmask
    pub detail_flags: u32,                // detail bitmask
    pub snd_pending_msgs: u64,            // pending send-queue message count
    pub rcv_pending_msgs: u64,            // pending recv-queue message count
    pub auto_hwm_enabled: bool,
    pub auto_hwm_profile: u32,
    pub auto_hwm_role: u32,
    pub auto_hwm_policy_class: u32,
    pub auto_hwm_unit_budget_bytes: u64,
    pub auto_hwm_size_cap: u32,
    pub auto_hwm_socket_message_slots: u64,
    pub auto_hwm_effective_message_bytes: u64,
    pub auto_hwm_applied_sndhwm: i32,
    pub auto_hwm_applied_rcvhwm: i32,
    pub auto_hwm_effective_sndbuf: i32,
    pub auto_hwm_effective_rcvbuf: i32,
    pub auto_hwm_last_recalc_ms: u64,
    pub auto_hwm_last_recalc_reason: u32,
    pub auto_hwm_send_blocked_ratio_ppm: u32,
    pub auto_hwm_deferred_sndhwm: i32,
    pub auto_hwm_deferred_rcvhwm: i32,
}

impl MonitorSnapshot {
    /// Convenience check for the ready bit in `state_flags`.
    /// Use this only for raw socket monitor sources.
    pub fn is_ready(&self) -> bool;
    pub fn is_closed(&self) -> bool;
}
```

### MonitorEvent

Event emitted by `SocketMonitor::recv` / `on_event`. Required by all
bindings.

```rust
pub struct MonitorEvent {
    pub event: MonitorEventType,         // event kind (CONNECTION_READY, CONNECTED, DISCONNECTED, PeerWeightChanged, ...)
    pub value: u32,                      // event-specific detail (PeerWeightChanged carries the new 0..100 weight)
    pub routing_id: Option<RoutingId>,   // peer routing id when the event carries one
    pub local_addr: String,              // local endpoint
    pub remote_addr: String,             // remote endpoint
}
```

`MonitorEventType` includes `PeerWeightChanged` (bit 15).

---

## Services

### Registry

```rust
impl Registry {
    /// # Errors: ConfigError
    pub fn new(ctx: &Context) -> Result<Self, ConfigError>;
    /// # Errors: BindError
    pub fn bind(&self, pub_endpoint: &str, router_endpoint: &str) -> Result<(), BindError>;
    /// # Errors: ConfigError
    pub fn set_id(&self, id: u32) -> Result<(), ConfigError>;
    /// # Errors: ConnectError
    pub fn add_peer(&self, peer_pub_endpoint: &str) -> Result<(), ConnectError>;
    /// # Errors: ConfigError
    pub fn set_heartbeat(&self, interval_ms: u32, timeout_ms: u32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn set_broadcast_interval(&self, interval_ms: u32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn set_tls_server(&self, cert_path: &str, key_path: &str,
        require_client_cert: bool) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn set_tls_client(&self, ca_cert_path: &str, hostname: &str,
        trust_system: bool) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn status_snapshot(&self) -> Result<RegistryStatus, ConfigError>;
    /// # Errors: ConfigError
    pub fn service_summary_snapshot(&self) -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn service_summary_query(&self, filter: &RegistryServiceSummaryFilter)
        -> Result<Vec<RegistryServiceSummaryEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn member_peers(&self, channel_name: &str)
        -> Result<Vec<MemberPeerEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn topology_snapshot(&self) -> Result<Vec<RegistryTopologyEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn topology_query(&self, filter: &RegistryTopologyFilter)
        -> Result<Vec<RegistryTopologyEntry>, ConfigError>;
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### Discovery

```rust
/// Fixed auto-connect channel contract selected when a Discovery handle is created.
#[repr(i32)]
pub enum AutoConnectType {
    Invalid,
    RouteMesh,
    ClientServer,
    DealerMesh,
    Fanout,
    SpotMesh,
}

impl Discovery {
    /// # Errors: ConfigError
    pub fn new(ctx: &Context, auto_connect_type: AutoConnectType, channel_name: &str)
        -> Result<Self, ConfigError>;
    /// # Errors: ConnectError
    pub fn connect_registry(&self, endpoint: &str) -> Result<(), ConnectError>;
    /// # Errors: ConfigError
    pub fn set_value(&self, value: i64) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn get_value(&self) -> Result<i64, ConfigError>;
    /// # Errors: ConfigError
    pub fn member_peers(&self) -> Result<Vec<MemberPeerEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_tls_client(&self, ca_cert_path: &str, hostname: &str,
        trust_system: bool) -> Result<(), ConfigError>;
    /// Resolve the current owner node routing id for a logical spot routing id.
    /// Intended for send/request destination lookup. Maps to
    /// `zlink_discovery_resolve_spot`. Registry-backed lookup requires the
    /// publishing Discovery to enable `ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC`.
    pub fn resolve_spot(&self, spot_rid: &RoutingId) -> Result<RoutingId, ConfigError>;
    /// Resolve the current route for an actor id in this discovery channel.
    /// Maps to `zlink_discovery_resolve_actor`.
    pub fn resolve_actor(&self, actor_id: &str) -> Result<ActorRoute, ConfigError>;
    pub fn set_spot_owner_sync_enabled(&self, enabled: bool) -> Result<(), ConfigError>;
    pub fn spot_owner_sync_enabled(&self) -> Result<bool, ConfigError>;
    pub fn set_actor_route_sync_enabled(&self, enabled: bool) -> Result<(), ConfigError>;
    pub fn actor_route_sync_enabled(&self) -> Result<bool, ConfigError>;
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### SpotNode

```rust
pub enum SpotNodeMode {
    PubSub = 1,
    Routed = 2,
    All = 3,
}

pub struct SpotNodeOptions {
    pub mode: Option<SpotNodeMode>, // None maps to All
}

impl SpotNode {
    pub fn new_with_options(ctx: &Context, options: SpotNodeOptions)
        -> Result<Self, ConfigError>;
    /// # Errors: ConfigError
    pub fn new(ctx: &Context) -> Result<Self, ConfigError>;
    /// # Errors: BindError
    pub fn bind(&self, endpoint: &str) -> Result<(), BindError>;
    /// # Errors: ConfigError
    pub fn last_endpoint(&self) -> Result<String, ConfigError>;
    /// # Errors: ConnectError
    pub fn connect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect_peer(&self, peer_endpoint: &str) -> Result<(), ConnectError>;
    /// # Errors: ConnectError
    pub fn disconnect_peer_rid(&self, target_node_rid: &RoutingId)
        -> Result<(), ConnectError>;
    /// # Errors: ConfigError
    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn attach_channel_dealer(&self, discovery: &Discovery, dealer: &DealerSocket)
        -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn attach_channel_dealer_manual(&self, channel_name: &str, dealer: &DealerSocket)
        -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn attach_pub_ingress(&self, pub: &PubSocket) -> Result<(), ConfigError>;
    // SpotNode admission and dispatch-worker options. These map to the six
    // public zlink_spot_node_option_t values; no raw option bag is public.
    /// # Errors: ConfigError
    pub fn router_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_router_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn router_hwm(&self) -> Result<i32, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_router_hwm(&self, value: i32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn pubsub_hwm_profile(&self) -> Result<AutoHwmProfile, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_pubsub_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn pubsub_hwm(&self) -> Result<i32, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_pubsub_hwm(&self, value: i32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn dispatch_workers_min(&self) -> Result<i32, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_dispatch_workers_min(&self, value: i32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn dispatch_workers_max(&self) -> Result<i32, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_dispatch_workers_max(&self, value: i32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn set_tls_server(&self, cert_path: &str, key_path: &str,
        require_client_cert: bool) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn set_tls_client(&self, ca_cert_path: &str, hostname: &str,
        trust_system: bool) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn entry_spot(&self) -> Result<Spot, ConfigError>;
    /// # Errors: ConfigError
    pub fn create_spot(&self) -> Result<Spot, ConfigError>;
    /// # Errors: ConfigError
    pub fn spot_lookup(&self, spot_rid: &RoutingId) -> Result<Option<Spot>, ConfigError>;
    /// # Errors: ConfigError
    pub fn create_actor(&self, actor_id: &str) -> Result<Actor, ConfigError>;
    /// # Errors: ConfigError
    pub fn actor_lookup(&self, actor_id: &str) -> Result<ActorRef, ConfigError>;
    /// # Errors: ConfigError
    pub fn remote_actor_ref(target_node_rid: &RoutingId, actor_id: &str)
        -> Result<ActorRef, ConfigError>;
    /// # Errors: RequestError
    pub fn create_remote_actor(&self, target_node_rid: &RoutingId, actor_id: &str,
        message: Message, timeout: Duration) -> Result<ActorCreateResult, RequestError>;
    /// Destroy a local or remote Actor through its ActorRef.
    /// # Errors: RequestError
    pub fn destroy_actor(&self, actor: &ActorRef, timeout: Duration)
        -> Result<(), RequestError>;
    /// # Errors: HandlerError
    pub fn on_actor_admission<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(&str, Message) -> ActorAdmissionResult + Send + 'static;
    /// # Errors: SubmitError
    pub fn join_actor_callback<F>(&self, actor: &ActorRef,
        dest_node_rid: &RoutingId, dest_spot_rid: &RoutingId, message: Message,
        callback: F, flags: SendFlags, timeout: Duration) -> Result<(), SubmitError>
        where F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;
    /// # Errors: RequestError
    pub fn leave_actor(&self, actor: &ActorRef, dest_spot_rid: &RoutingId,
        timeout: Duration) -> Result<(), RequestError>;
    /// # Errors: ConfigError
    pub fn spots_snapshot(&self) -> Result<Vec<SpotNodeSpotEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn actors_snapshot(&self) -> Result<Vec<SpotNodeActorEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn status_snapshot(&self) -> Result<SpotNodeStatus, ConfigError>;
    /// # Errors: ConfigError
    pub fn peers_snapshot(&self) -> Result<Vec<SpotNodePeerEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn peers_query(&self, filter: &SpotNodePeerFilter)
        -> Result<Vec<SpotNodePeerEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn subjects_snapshot(&self, filter: Option<&SpotNodeSubjectFilter>)
        -> Result<Vec<SpotNodeSubjectEntry>, ConfigError>;
    pub fn internal_sockets_snapshot(&self, filter: Option<&SpotNodeSocketSnapshotFilter>)
        -> Result<Vec<SpotNodeSocketSnapshotEntry>, ConfigError>;
    /// # Errors: ConfigError
    // --- identity / routing ---
    /// SpotNode's logical address.
    /// Maps to `zlink_set_routing_id(node, ...)`.
    pub fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError>;

    /// Maps to `zlink_get_routing_id(node, ...)`.
    pub fn routing_id(&self) -> Result<RoutingId, ConfigError>;

    // close() cascades: closes all live Spot handles before the node becomes invalid.
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

`SpotNode` owns the lifecycle. `Spot` handles are created through
`SpotNode::create_spot()`, Entry Spot facades through
`SpotNode::entry_spot()`, and lookup facades through
`SpotNode::spot_lookup(...)`. Direct `Spot::new(&node)` construction is
internal and is not part of the public API contract.

`dispatch_workers_min` must be at least `1`; `dispatch_workers_max` must be at
least `dispatch_workers_min`. If unset, core defaults are CPU count `1`:
`min=max=1`; otherwise `min=2`, `max=cpu_count`. These values size only the
SpotNode dispatch callback worker pool.

### Actor

```rust
pub struct Actor { /* owns an ActorRef and borrows its SpotNode handle */ }

impl Actor {
    /// # Errors: ConfigError
    pub fn actor_ref(&self) -> Result<ActorRef, ConfigError>;
    /// # Errors: RequestError
    pub fn close_with_timeout(&mut self, timeout: Duration) -> Result<(), RequestError>;
    /// # Errors: RequestError
    pub fn close(&mut self) -> Result<(), RequestError>;
    /// # Errors: SubmitError
    pub fn join_callback<F>(&self, spot: &Spot, message: Message,
        callback: F, flags: SendFlags, timeout: Duration) -> Result<(), SubmitError>
        where F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;
    /// # Errors: ConfigError
    pub fn leave(&self, spot: &Spot) -> Result<(), ConfigError>;
    /// # Errors: RecvError
    pub fn recv_part_with_flags(&self, flags: RecvFlags)
        -> Result<Option<(ActorRecvInfo, Message, bool)>, RecvError>;
    /// # Errors: RecvError
    pub fn recv_part(&self) -> Result<(ActorRecvInfo, Message, bool), RecvError>;
    /// # Errors: SubmitError
    pub fn send_bound_session_msg(&self, message: Message, flags: SendFlags)
        -> Result<(), SubmitError>;
    /// # Errors: RequestError
    pub fn close_bound_session(&self, timeout: Duration) -> Result<(), RequestError>;
}
```

`Actor::join_callback` resolves the destination node from the target `Spot`.
For remote joins where the destination node is already known, use
`SpotNode::join_actor_callback`.

### Spot

```rust
impl Spot {
    // Spot::new(&node) is internal. Public code obtains Spot handles through
    // SpotNode factories.
    /// # Errors: SubmitError
    pub fn publish(&self, service_name: &str, topic: &str,
        parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn publish_with_flags(&self, service_name: &str, topic: &str,
        parts: impl IntoMultipart, flags: SendFlags) -> Result<bool, SubmitError>;
    /// # Errors: SubmitError
    pub fn send_channel(&self, channel_name: &str,
        parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_channel_with_flags(&self, channel_name: &str,
        parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_to_spot(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_to_spot_with_flags(&self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, parts: impl IntoMultipart,
        flags: SendFlags) -> Result<(), SubmitError>;
    /// # Errors: SubmitError (submit failure). Callback receives Result<Vec<Message>, RequestError>.
    pub fn request_channel_callback<F>(&self, channel_name: &str,
        parts: impl IntoMultipart, callback: F,
        flags: SendFlags, timeout: Duration)
        -> Result<(), SubmitError>
        where F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;
    /// # Errors: ZlinkError (SubmitError on submit, RequestError on completion)
    pub async fn request_channel(&self, channel_name: &str,
        parts: impl IntoMultipart, timeout: Duration) -> Result<Vec<Message>, ZlinkError>;
    /// # Errors: ConfigError
    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    /// # Errors: RecvError
    pub fn subscribe(&self) -> Result<TopicMessage, RecvError>;
    /// # Errors: RecvError
    pub fn subscribe_with_flags(&self, flags: RecvFlags) -> Result<Option<TopicMessage>, RecvError>;
    pub fn receive_subscription_event(&self) -> Result<SubscriptionEvent, RecvError>;
    /// # Errors: RecvError
    pub fn receive_subscription_event_with_flags(&self, flags: RecvFlags)
        -> Result<Option<SubscriptionEvent>, RecvError>;
    /// # Errors: HandlerError
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn() + Send + 'static;

    // --- routed request (spot → spot, async) — no flags ---
    // Duration::ZERO uses the socket default timeout.
    /// # Errors: ZlinkError (SubmitError on submit, RequestError on completion)
    pub async fn request_to_spot(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        parts: impl IntoMultipart,
        timeout: Duration) -> Result<Vec<Message>, ZlinkError>;

    // --- routed request (spot → spot, callback) ---
    // Duration::ZERO uses the socket default timeout.
    /// # Errors: SubmitError (submit failure). Callback receives Result<Vec<Message>, RequestError>.
    pub fn request_to_spot_callback<F>(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        parts: impl IntoMultipart, callback: F, flags: SendFlags, timeout: Duration)
        -> Result<(), SubmitError>
        where F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;

    // --- routed request (spot → router, async) — no flags ---
    // Duration::ZERO uses the socket default timeout.
    /// # Errors: ZlinkError (SubmitError on submit, RequestError on completion)
    pub async fn request_to_router(&self, peer_rid: RoutingId,
        parts: impl IntoMultipart,
        timeout: Duration) -> Result<Vec<Message>, ZlinkError>;

    // --- routed request (spot → router, callback) ---
    // Duration::ZERO uses the socket default timeout.
    /// # Errors: SubmitError (submit failure). Callback receives Result<Vec<Message>, RequestError>.
    pub fn request_to_router_callback<F>(&self, peer_rid: RoutingId,
        parts: impl IntoMultipart, callback: F, flags: SendFlags, timeout: Duration)
        -> Result<(), SubmitError>
        where F: FnOnce(Result<Vec<Message>, RequestError>) + Send + 'static;

    // --- routed reply (spot → spot) ---
    /// # Errors: SubmitError
    pub fn reply_to_spot(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        request_seq: u64, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn reply_to_spot_with_flags(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        request_seq: u64, parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;

    // --- routed reply (spot → router) ---
    /// # Errors: SubmitError
    pub fn reply_to_router(&self, peer_rid: RoutingId, request_seq: u64,
        parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn reply_to_router_with_flags(&self, peer_rid: RoutingId, request_seq: u64,
        parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;

    // --- routed receive ---
    /// # Errors: RecvError
    pub fn recv_routed(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_routed_with_flags(&self, flags: RecvFlags) -> Result<Option<Received>, RecvError>;
    /// # Errors: RecvError
    pub fn recv_actor_join_with_flags(&self, flags: RecvFlags)
        -> Result<Option<ActorJoinRequest>, RecvError>;
    /// # Errors: RecvError
    pub fn recv_actor_join(&self) -> Result<ActorJoinRequest, RecvError>;
    /// # Errors: SubmitError
    pub fn reply_actor_join(&self, request: &ActorJoinRequest, accepted: bool,
        message: Message) -> Result<(), SubmitError>;
    /// # Errors: ConfigError
    pub fn actors_snapshot(&self) -> Result<Vec<ActorRef>, ConfigError>;
    /// # Errors: HandlerError
    pub fn on_routed_receive<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(Received) + Send + 'static;
    /// # Errors: HandlerError
    pub fn on_dispatch_event<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: for<'a> Fn(SpotDispatchInfo<'a>) + Send + 'static;
    /// # Errors: ConfigError
    pub fn drain_channel_reply_from(&self, dealer: &DealerSocket)
        -> Result<(), ConfigError>;

    // --- identity / routing ---
    /// Spot's logical address / routed ownership key.
    /// Maps to `zlink_set_routing_id(spot, ...)`.
    pub fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ConfigError>;

    /// Maps to `zlink_get_routing_id(spot, ...)`.
    pub fn routing_id(&self) -> Result<RoutingId, ConfigError>;

    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

```rust
pub enum SpotDispatchEvent {
    SubscribeReadable,
    RoutedReadable,
    TimerReadable,
    ChannelReplyReadable,
    ActorReadable,
    ActorJoinReadable,
}

pub enum SpotDispatchSubject<'a> {
    Spot,
    Timer(&'a Timer),
    ChannelDealer(&'a DealerSocket),
    Actor(ActorRef),
}

pub struct SpotDispatchInfo<'a> {
    pub event: SpotDispatchEvent,
    pub subject: SpotDispatchSubject<'a>,
    // non-public: owner SpotNode handle for Actor recv
}

impl<'a> SpotDispatchInfo<'a> {
    /// # Errors: RecvError
    pub fn recv_actor_part_with_flags(&self, flags: RecvFlags)
        -> Result<Option<(ActorRecvInfo, Message, bool)>, RecvError>;
}
```

`ChannelReplyReadable` carries `SpotDispatchSubject::ChannelDealer(dealer)`.
Read the logical channel name from that attached DEALER with
`DealerSocket::channel_name()` and pass the dealer reference to
`drain_channel_reply_from(...)`.

For `ActorReadable`, `SpotDispatchSubject::Actor(actor)` contains a copied
`ActorRef`. The copied ref may be retained by user code; timer and dealer
references are callback-lifetime only. No native Actor pointer is part of the
public contract.

For `SubscribeReadable` and `RoutedReadable`, callers must keep draining
`subscribe(...)` / `recv_routed(...)` until the binding reports no data /
`EAGAIN`.

### RegistryQueryClient

```rust
impl RegistryQueryClient {
    /// # Errors: ConfigError
    pub fn new(ctx: &Context) -> Result<Self, ConfigError>;
    /// # Errors: ConnectError
    pub fn connect(&self, endpoint: &str) -> Result<(), ConnectError>;
    /// # Errors: ConfigError
    pub fn snapshot(&self, filter: Option<&RegistryTopologyFilter>)
        -> Result<Vec<RegistryTopologyEntry>, ConfigError>;
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### Service-Layer Entry Types

Value types returned by service-layer snapshot/query calls. Each type is
a plain Rust struct with `pub` fields — the raw `zlink_*_t` C structs are
never surfaced. Names follow Rust `PascalCase`; fields are `snake_case`
matching the canonical names.

Primary entry types used in the default service flow:

```rust
pub struct MemberPeerEntry {
    pub auto_connect_type: AutoConnectType,
    pub service_role: ServiceRole,
    pub channel_name: String,
    pub endpoint: String,
    pub routing_id: RoutingId,
    pub value: i64,
    pub weight: u32,
}

pub struct RegistryTopologyEntry {
    pub auto_connect_type: AutoConnectType,
    pub routing_id: RoutingId,
    pub service_kind: ServiceKind,
    pub service_role: ServiceRole,
    pub channel_name: String,
    pub endpoint: String,
    pub source: TopologySource,
    pub state: TopologyState,
    pub desired_count: u32,
    pub ready_count: u32,
    pub error_code: u32,
    pub last_reported_ms: u64,
}

pub struct SpotNodeStatus {
    pub channel_name: String,
    pub local_endpoint: String,
    pub node_routing_id: RoutingId,
    pub state: SpotNodeState,
    pub configured_peer_count: u32,
    pub active_peer_count: u32,
    pub connected_peer_count: u32,
    pub subject_count: u32,
    pub ready_subject_count: u32,
    pub disconnected_sub_target_count: u32,
    pub disconnected_routed_target_count: u32,
    pub last_error: i32,
    pub last_changed_ms: u64,
}
```

Advanced / Diagnostic entry types and filters:

```rust
pub struct RegistryServiceSummaryEntry {
    pub auto_connect_type: AutoConnectType,
    pub service_role: ServiceRole,
    pub channel_name: String,
    pub total_count: u32,
    pub connecting_count: u32,
    pub ready_count: u32,
    pub error_count: u32,
    pub stopped_count: u32,
    pub last_reported_ms: u64,
}

pub struct RegistryStatus {
    pub registry_id: u32,
    pub bind_endpoint: String,
    pub state: RegistryState,
    pub topology_entry_count: u32,
    pub peer_registry_count: u32,
    pub connected_peer_registry_count: u32,
    pub list_seq: u64,
    pub last_error: i32,
    pub last_changed_ms: u64,
}

pub struct SpotNodePeerEntry {
    pub channel_name: String,
    pub local_endpoint: String,
    pub peer_endpoint: String,
    pub source: SpotPeerSource,
    pub state: SpotPeerState,
    pub weight: u32,
    pub connected_since_ms: u64,
    pub last_changed_ms: u64,
}

pub struct SpotNodeSubjectEntry {
    pub role: SpotRole,
    pub subject: String,
    pub subject_kind: SubjectKind,
    pub ready_peer_count: u32,
    pub active_peer_count: u32,
    pub last_changed_ms: u64,
}

pub struct SpotNodeSocketSnapshotFilter {
    pub owner: Option<SpotNodeSocketOwner>,
    pub socket_type: Option<SocketType>,
    pub socket_name: Option<String>,
}

pub struct SpotNodeSocketSnapshotEntry {
    pub owner: SpotNodeSocketOwner,
    pub owner_id: u64,
    pub owner_name: String,
    pub socket_name: String,
    pub socket_type: SocketType,
    pub auto_hwm_visible: bool,
    pub snapshot: MonitorSnapshot,
}

pub struct RegistryServiceSummaryFilter {
    pub auto_connect_type: Option<AutoConnectType>,
    pub service_role: Option<ServiceRole>,
    pub channel_name: Option<String>,
}

pub struct RegistryTopologyFilter {
    pub auto_connect_type: Option<AutoConnectType>,
    pub service_kind: Option<ServiceKind>,
    pub service_role: Option<ServiceRole>,
    pub channel_name: Option<String>,
    pub routing_id: Option<RoutingId>,
    pub state: Option<TopologyState>,
    pub source: Option<TopologySource>,
}

pub struct SpotNodePeerFilter {
    pub peer_endpoint: Option<String>,
    pub source: Option<SpotPeerSource>,
    pub state: Option<SpotPeerState>,
}

pub struct SpotNodeSubjectFilter {
    pub role: Option<SpotRole>,
    pub subject: Option<String>,
    pub subject_kind: Option<SubjectKind>,
}

```

---

## Timer

### Timer

```rust
impl Timer {
    /// # Errors: ConfigError
    pub fn new() -> Result<Self, ConfigError>;
    /// # Errors: ConfigError
    pub fn from_spot(spot: &Spot) -> Result<Self, ConfigError>;
    /// # Errors: ConfigError
    pub fn start(&self, interval_ns: u64, repeat_count: u64) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn stop(&self) -> Result<(), ConfigError>;
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Option<u64>, RecvError>;
    /// # Errors: HandlerError
    pub fn on_fire<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(&Timer, u64) + Send + 'static;
}
// Drop calls zlink_timer_destroy
```

---

## Poller

```rust
pub struct Poller<T = ()> { /* ... */ }

impl<T: Clone + Send + Sync + 'static> Poller<T> {
    /// # Errors: ConfigError
    pub fn new() -> Result<Self, ConfigError>;
    /// # Errors: ConfigError
    pub fn add_socket<'a>(&self, socket: impl Into<PollTarget<'a>>,
        events: PollEventFlags, user_data: Option<T>) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn modify_socket<'a>(&self, socket: impl Into<PollTarget<'a>>,
        events: PollEventFlags) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn remove_socket<'a>(&self, socket: impl Into<PollTarget<'a>>)
        -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn add_fd(&self, fd: RawFd, events: PollEventFlags, user_data: Option<T>)
        -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn modify_fd(&self, fd: RawFd, events: PollEventFlags) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn remove_fd(&self, fd: RawFd) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn add_timer(&self, timer: &Timer, user_data: Option<T>)
        -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn remove_timer(&self, timer: &Timer) -> Result<(), ConfigError>;
    /// # Errors: RecvError
    pub fn wait(&self, timeout_ms: i64) -> Result<Option<PollEvent<T>>, RecvError>;
    /// # Errors: RecvError
    pub fn wait_all(&self, timeout_ms: i64) -> Result<Vec<PollEvent<T>>, RecvError>;
    /// Number of registered pollable items.
    /// Maps to `zlink_poller_size`.
    pub fn size(&self) -> Result<usize, ConfigError>;
}
// Drop calls zlink_poller_destroy

/// Legacy poll function for simple use cases.
/// # Errors: RecvError
pub fn poll(items: &mut [PollItem], timeout_ms: i64) -> Result<i32, RecvError>;

// The current public poller contract is still generic. It does not yet expose
// a Spot-aware result carrying owner Spot, dispatch event kind, and drain
// subject together.

pub struct PollEvent<T = ()> {
    pub source_kind: PollSourceKind,
    pub events: PollEventFlags,
    pub user_data: Option<T>,
    // socket, fd, and timer accessors are typed and source-specific.
}

impl<T> PollEvent<T> {
    pub fn is_readable(&self) -> bool;
    pub fn is_writable(&self) -> bool;
}

bitflags! {
    pub struct PollEventFlags: i16 {
        const IN = 0x0001;
        const OUT = 0x0002;
    }
}

pub enum PollSourceKind {
    Socket,
    Fd,
    Timer,
}
```

`PollEventFlags::OUT` is send-recovery readiness shared with
`on_send_ready(...)`, not a general transport-writable bit.

---

## Utilities

### Stopwatch

High-resolution stopwatch for measuring elapsed time.

```rust
pub struct Stopwatch { /* ... */ }

impl Stopwatch {
    pub fn start() -> Self;

    /// Return elapsed microseconds without stopping.
    pub fn intermediate(&self) -> u64;

    /// Stop the stopwatch and return total elapsed microseconds.
    pub fn stop(self) -> u64;
}
```

### Thread

Not wrapped: Rust has `std::thread` and async runtimes (tokio, etc.)
that are idiomatic and superior to the C thread API for all use cases.

### AtomicCounter

Not wrapped: Rust provides `std::sync::atomic` in the standard library,
which covers the same use case idiomatically.

## Peer Disconnect by Routing ID

Rust bindings expose `disconnect_rid(rid)` on raw sockets and
`disconnect_peer_rid(target_node_rid)` on `SpotNode`. The duplicate policy
option and `NotFound` / `Conflict` / `Busy` connect errors mirror the C core.
`Spot` does not expose a peer-rid disconnect method.
