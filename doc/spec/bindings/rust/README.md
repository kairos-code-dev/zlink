[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Rust Binding Specification

This document defines the complete public API surface of the zlink Rust binding.
Every struct, method, and function listed here is part of the contract that the
binding must expose. Private/internal items are omitted.

Only the items re-exported as public crate API are part of the contract.
Private modules, `pub(crate)` helpers, FFI modules, and source-tree-only
support code are internal. Perf, samples, and tests must use the public crate
surface only and must not rely on internal modules.

---

## Current Core Alignment Overrides

The sections below still contain some older signatures. When they conflict
with the rules here, this section wins.

- `PairSocket`, `DealerSocket`, and `RouterSocket` are recv-only on the data
  plane. Remove `on_receive(...)` from their public contract.
- Peer weight is exposed only on `RouterSocket` and `DealerSocket` through typed option/property surfaces. The value range is `0..100`, default `100`; `0` drains new outbound selection. Submit to a weight-`0` peer returns
  `Err(SubmitError { code: SubmitResult::NotAdmitted, .. })`.
- `POLLOUT` is a send-recovery readiness signal, shared with
  `on_send_ready(...)`. It is not a "transport writable" bit.
- ROUTER / PUB socket option defaults follow the core header: `mandatory =
  true`, `handover = false`, `nodrop = true`.
- Internal pairing rule: when auto-connect pairs two same-service ROUTERs
  via Discovery, the library picks one initiator per pair by a total order
  on `(routing_id, advertise_endpoint)`. Users do not configure this.
- `SubSocket` and `XSubSocket` are recv-only. Remove `on_subscribe(...)` from
  their public contract.
- `StreamSocket` keeps `recv` and exposes a packet callback surface mapped to
  `zlink_stream_packet_handler()`. Recommended canonical name: `on_packet`.
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

## Core

### Context

```rust
impl Context {
    /// # Errors: ConfigError
    pub fn new() -> Result<Self, ConfigError>;
    /// # Errors: CloseError
    pub fn shutdown(&self) -> Result<(), CloseError>;
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
    pub fn auto_hwm_total_memory_budget_mb(&self) -> Result<i32, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_auto_hwm_total_memory_budget_mb(&self, value: i32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn add_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn remove_thread_affinity(&self, cpu: i32) -> Result<(), ConfigError>;
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

All sockets implement `Drop` (calls `close`). Common connection and
option methods are provided through the internal `SocketCore` but appear
as direct methods on each socket type.

Rust nonblocking data-plane helpers follow this rule:

- `try_send...()` returns `Ok(false)` only for temporary backpressure.
- Route-not-ready and other submit failures still return
  `Err(SubmitError)`.
- `try_recv()` returns `Ok(None)` when no message is currently available and
  still returns `Err(RecvError)` for real recv failures.

Peer weight is not part of `CommonSocketOptions`. Rust exposes weight only on
`RouterSocket`, `DealerSocket`, `SpotNode`, and `Spot`:

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
    /// # Errors: SubmitError
    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_with_flags(&self, parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn try_send(&self, parts: impl IntoMultipart) -> Result<bool, SubmitError>;
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn try_recv(&self) -> Result<Option<Received>, RecvError>;
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
    /// # Errors: SubmitError
    pub fn publish(&self, topic: &str, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn publish_with_flags(&self, topic: &str, parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;
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
    /// # Errors: ConfigError
    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    /// # Errors: RecvError
    pub fn subscribe(&self) -> Result<TopicMessage, RecvError>;
    /// # Errors: RecvError
    pub fn subscribe_with_flags(&self, flags: RecvFlags) -> Result<TopicMessage, RecvError>;
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
    pub fn send_with_flags(&self, parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn try_send(&self, parts: impl IntoMultipart) -> Result<bool, SubmitError>;
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn try_recv(&self) -> Result<Option<Received>, RecvError>;
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
    // --- dealer request (callback, nonblocking submit) ---
    // `timeout = None` uses the socket default request timeout.
    // Returns Ok(false) only for temporary backpressure.
    pub fn try_request_callback<F: FnOnce(Result<Vec<Message>, RequestError>) + 'static>(
        &self, parts: &[&[u8]], cb: F, timeout: Option<Duration>)
        -> Result<bool, SubmitError>;

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
    /// # Errors: ConfigError
    pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn routing_id(&self) -> Result<RoutingId, ConfigError>;
    /// # Errors: SubmitError
    pub fn send(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_with_flags(&self, target: &RoutingId, parts: impl IntoMultipart,
                           flags: SendFlags) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn try_send(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<bool, SubmitError>;
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn try_recv(&self) -> Result<Option<Received>, RecvError>;
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
    // --- router request (callback, nonblocking submit) ---
    // `timeout = None` uses the socket default request timeout.
    // Returns Ok(false) only for temporary backpressure.
    pub fn try_request_callback<F: FnOnce(Result<Vec<Message>, RequestError>) + 'static>(
        &self, peer_rid: &RoutingId, parts: &[&[u8]], cb: F,
        timeout: Option<Duration>) -> Result<bool, SubmitError>;

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

    // NOTE: RouterSocket 의 routed 수신 plane 은 단일 recv 표면이다. 일반
    // ROUTER 트래픽과 spot-origin routed 트래픽을 모두 recv/recv_with_flags
    // 로 받는다. `Received::routing_id()` 는 source_node_rid,
    // `Received::spot_rid()` 는 spot-origin 트래픽에서만 값이 있다.
    // data-plane callback install surface (e.g. on_receive) 는 ROUTER 에
    // 제공하지 않는다. request completion 은 request() 경로에서만 유지된다.

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
    /// # Errors: SubmitError
    pub fn publish(&self, topic: &str, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn publish_with_flags(&self, topic: &str, parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;
    /// # Errors: RecvError
    pub fn receive_subscription_event(&self) -> Result<SubscriptionEvent, RecvError>;
    /// # Errors: RecvError
    pub fn receive_subscription_event_with_flags(&self, flags: RecvFlags) -> Result<SubscriptionEvent, RecvError>;
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
    /// # Errors: ConfigError
    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    /// # Errors: RecvError
    pub fn subscribe(&self) -> Result<TopicMessage, RecvError>;
    /// # Errors: RecvError
    pub fn subscribe_with_flags(&self, flags: RecvFlags) -> Result<TopicMessage, RecvError>;
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
    /// # Errors: SubmitError
    pub fn send(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_with_flags(&self, target: &RoutingId, parts: impl IntoMultipart,
                           flags: SendFlags) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn try_send(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<bool, SubmitError>;
    /// Two mutually-exclusive receive modes on the same StreamSocket:
    ///   (1) recv(), (2) on_packet(handler). Second attach returns
    ///   Err(HandlerError { code: HandlerResult::Busy, .. }).
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn try_recv(&self) -> Result<Option<Received>, RecvError>;
    /// Mode (3): framed packet callback mapped to
    /// `zlink_stream_packet_handler`. Wire frame is big-endian `u16`
    /// header_size + `u32` body_size + header + body. The handler receives
    /// the source routing id, a header `Message`, and a body `Message`;
    /// both messages transfer ownership to the handler.
    /// # Errors: HandlerError
    pub fn on_packet<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(RoutingId, Message, Message) + Send + 'static;
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
    pub fn send_with_flags(&self, parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_to(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_to_with_flags(&self, target: &RoutingId, parts: impl IntoMultipart,
        flags: SendFlags) -> Result<(), SubmitError>;
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

The binding exposes separate codec extension crates. The Cargo crate names and
Rust import crate names are fixed to:

- crate `zlink-codec-protobuf` -> `zlink_codec_protobuf`
- crate `zlink-codec-json` -> `zlink_codec_json`
- crate `zlink-codec-messagepack` -> `zlink_codec_messagepack`

These are separate public crates layered on top of the core `zlink` crate.
They must not become required dependencies of the core crate.

JSON codec baseline: `serde_json`.
MessagePack codec baseline: `rmp-serde`.

```rust
// zlink_codec_protobuf
pub fn decode<T>(message: &zlink::Message) -> Result<T, Error>
where
    T: prost::Message + Default;

pub fn encode<T>(value: &T) -> Result<zlink::Message, Error>
where
    T: prost::Message;
```

```rust
// zlink_codec_json
pub fn decode<T>(message: &zlink::Message) -> Result<T, Error>
where
    T: serde::de::DeserializeOwned;

pub fn encode<T>(value: &T) -> Result<zlink::Message, Error>
where
    T: serde::Serialize;
```

```rust
// zlink_codec_messagepack
pub fn decode<T>(message: &zlink::Message) -> Result<T, Error>
where
    T: serde::de::DeserializeOwned;

pub fn encode<T>(value: &T) -> Result<zlink::Message, Error>
where
    T: serde::Serialize;
```
Each codec crate defines its own `Error` type. The helper reads from
`Message::as_bytes()` and creates new frames with `Message::from_bytes()`.

These extension crates are optional. The core `zlink` crate must not depend on
them.

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
    pub spot_rid: Option<RoutingId>,     // SPOT routed recv 에만 값 있음
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
globally unique `i32` that spans all result enum ranges (0-703).

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
    /// Globally unique code spanning all result enum ranges (0-703).
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

### ServiceMonitor

Starts in recv model. `on_event(...)` transitions one-way to callback-only
model; after that `recv()` returns a busy recv error and `snapshot()` still works.

```rust
impl ServiceMonitor {
    /// # Errors: ConfigError
    pub fn open<'a>(target: impl Into<ServiceMonitorTarget<'a>>) -> Result<Self, ConfigError>;
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<ServiceEvent, RecvError>;
    /// # Errors: ConfigError
    pub fn snapshot(&self) -> Result<MonitorSnapshot, ConfigError>;
    /// # Errors: HandlerError
    pub fn on_event<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(&ServiceEvent) + Send + 'static;
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
    pub auto_hwm_role: u32,
    pub auto_hwm_managed_connections: u32,
    pub auto_hwm_active_hwm_connections: u32,
    pub auto_hwm_planning_transport_connections: u32,
    pub auto_hwm_base_floor_per_connection: u32,
    pub auto_hwm_applied_sndhwm: i32,
    pub auto_hwm_applied_rcvhwm: i32,
    pub auto_hwm_requested_sndbuf: i32,
    pub auto_hwm_requested_rcvbuf: i32,
    pub auto_hwm_effective_sndbuf: i32,
    pub auto_hwm_effective_rcvbuf: i32,
    pub auto_hwm_total_memory_budget_bytes: u64,
    pub auto_hwm_queue_budget_bytes: u64,
    pub auto_hwm_transport_budget_bytes: u64,
    pub auto_hwm_runtime_reserve_bytes: u64,
    pub auto_hwm_group_budget_bytes: u64,
    pub auto_hwm_group_message_slots: u64,
    pub auto_hwm_effective_message_bytes: u64,
    pub auto_hwm_control_budget_bytes: u64,
    pub auto_hwm_routed_budget_bytes: u64,
    pub auto_hwm_fanout_budget_bytes: u64,
    pub auto_hwm_recv_ingress_budget_bytes: u64,
    pub auto_hwm_control_active_connections: u32,
    pub auto_hwm_routed_active_connections: u32,
    pub auto_hwm_fanout_active_connections: u32,
    pub auto_hwm_recv_ingress_active_connections: u32,
    pub auto_hwm_estimated_max_memory_bytes: u64,
    pub auto_hwm_last_recalc_ms: u64,
    pub auto_hwm_last_recalc_reason: u32,
    pub auto_hwm_send_blocked_ratio_ppm: u32,
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

`MonitorEventType` includes `PeerWeightChanged` (bit 15). Service
monitors surface the same change through
`ServiceMonitorEventMask::PeerWeightChanged` (bit 8).

### ServiceEvent

Event emitted by Discovery `ServiceMonitor::recv` / `on_event`.
Required by all bindings.

```rust
pub struct ServiceEvent {
    pub service_kind: ServiceKind,       // ZLINK_SERVICE_KIND_DISCOVERY, SPOT_SUB, SPOT_PUB, SOCKET
    pub event_type: ServiceEventType,    // UP, DOWN, PROVIDERS_CHANGED, ERROR, ...
    pub status: u32,                     // status code
    pub error_code: u32,                 // errno on error events
    pub value: u64,                      // event-specific value
    pub detail_flags: u32,               // detail bitmask
    pub service_name: String,            // service name
    pub endpoint: String,                // endpoint
    pub routing_id: Option<RoutingId>,   // peer routing id
    pub subject: String,                 // subscribe subject (topic)
    pub subject_kind: SubjectKind,       // subject category
}
```

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
    pub fn member_peers(&self, service_type: ServiceType, service_name: &str)
        -> Result<Vec<MemberPeerEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn member_peer_metadata(&self, service_type: ServiceType, service_name: &str,
        service_role: ServiceRole, endpoint: &str) -> Result<Message, ConfigError>;
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
/// Auto-connect target policy used by DEALER sockets in a Discovery view.
#[repr(i32)]
pub enum DiscoveryDealerPeerMode {
    Router = 1,
    Dealer = 2,
}

impl Discovery {
    /// # Errors: ConfigError
    pub fn new(ctx: &Context, service_type: ServiceType, service_name: &str)
        -> Result<Self, ConfigError>;
    /// # Errors: ConnectError
    pub fn connect_registry(&self, endpoint: &str) -> Result<(), ConnectError>;
    /// # Errors: ConfigError
    pub fn set_value(&self, value: i64) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn get_value(&self) -> Result<i64, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_metadata(&self, data: &[u8]) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn get_metadata(&self) -> Result<Message, ConfigError>;
    /// # Errors: ConfigError
    pub fn member_peers(&self) -> Result<Vec<MemberPeerEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn member_peer_metadata(&self, service_role: ServiceRole, endpoint: &str)
        -> Result<Message, ConfigError>;
    /// # Errors: ConfigError
    pub fn monitor_open(&self) -> Result<ServiceMonitor, ConfigError>;
    /// # Errors: ConfigError
    pub fn set_tls_client(&self, ca_cert_path: &str, hostname: &str,
        trust_system: bool) -> Result<(), ConfigError>;
    /// Resolve the current owner node routing id for a logical spot routing id.
    /// Intended for send/request destination lookup. Maps to
    /// `zlink_discovery_resolve_spot`.
    pub fn resolve_spot(&self, spot_rid: &RoutingId) -> Result<RoutingId, ConfigError>;
    /// Set the auto-connect target policy used by DEALER sockets in this
    /// discovery view. Default is Router. Maps to
    /// `zlink_discovery_set_dealer_peer_mode`.
    pub fn set_dealer_peer_mode(&self, mode: DiscoveryDealerPeerMode)
        -> Result<(), ConfigError>;
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### SpotNode

```rust
impl SpotNode {
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
    /// # Errors: ConfigError
    pub fn set_tls_server(&self, cert_path: &str, key_path: &str,
        require_client_cert: bool) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn set_tls_client(&self, ca_cert_path: &str, hostname: &str,
        trust_system: bool) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn create_spot(&self) -> Result<Spot, ConfigError>;
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

`SpotNode` owns the lifecycle. `Spot` is created only through
`SpotNode::create_spot()`. Direct `Spot::new(&node)` construction is
internal and is not part of the public API contract.

### Spot

```rust
impl Spot {
    // Spot::new(&node) is internal. Public code must use SpotNode::create_spot().
    /// # Errors: SubmitError
    pub fn publish(&self, service_name: &str, topic: &str,
        parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn publish_with_flags(&self, service_name: &str, topic: &str,
        parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_channel(&self, channel_name: &str,
        parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_channel_with_flags(&self, channel_name: &str,
        parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;
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
    pub fn subscribe_with_flags(&self, flags: RecvFlags) -> Result<TopicMessage, RecvError>;
    pub fn receive_subscription_event(&self) -> Result<SubscriptionEvent, RecvError>;
    /// # Errors: RecvError
    pub fn receive_subscription_event_with_flags(&self, flags: RecvFlags)
        -> Result<SubscriptionEvent, RecvError>;
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
    pub fn recv_routed_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError>;
    /// # Errors: HandlerError
    pub fn on_routed_receive<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(RoutingId, RoutingId, u64, Vec<Message>) + Send + 'static;
    /// # Errors: HandlerError
    pub fn on_dispatch_event<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(SpotDispatchInfo) + Send + 'static;
    /// # Errors: ConfigError
    pub fn drain_channel_reply_from(&self, subject: *mut c_void)
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
}

pub enum SpotDispatchSubjectKind {
    Spot,
    Timer,
    ChannelDealer,
}

pub struct SpotDispatchInfo {
    pub event: SpotDispatchEvent,
    pub subject_kind: SpotDispatchSubjectKind,
    pub subject: *mut c_void,
}
```

`ChannelReplyReadable` identifies the attached DEALER source through
`SpotDispatchInfo.subject_kind` and `SpotDispatchInfo.subject`. Read the
logical channel name from that attached DEALER with
`DealerSocket::channel_name()`.

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
    pub service_type: ServiceType,
    pub service_role: u16,
    pub service_name: String,
    pub endpoint: String,
    pub routing_id: RoutingId,
    pub value: i64,
    pub weight: u32,
}

pub struct RegistryTopologyEntry {
    pub routing_id: RoutingId,
    pub service_kind: ServiceKind,
    pub service_role: ServiceRole,
    pub service_name: String,
    pub endpoint: String,
    pub source: TopologySource,
    pub state: TopologyState,
    pub desired_count: u32,
    pub ready_count: u32,
    pub error_code: u32,
    pub last_reported_ms: u64,
}

pub struct SpotNodeStatus {
    pub service_name: String,
    pub local_endpoint: String,
    pub node_routing_id: RoutingId,
    pub state: SpotNodeState,
    pub configured_peer_count: u32,
    pub active_peer_count: u32,
    pub connected_peer_count: u32,
    pub subject_count: u32,
    pub ready_subject_count: u32,
    pub last_error: i32,
    pub last_changed_ms: u64,
}
```

Advanced / Diagnostic entry types and filters:

```rust
pub struct RegistryServiceSummaryEntry {
    pub service_kind: ServiceKind,
    pub service_role: ServiceRole,
    pub service_name: String,
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
    pub service_name: String,
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
    pub subject_kind: u32,
    pub ready_peer_count: u32,
    pub active_peer_count: u32,
    pub last_changed_ms: u64,
}

pub struct RegistryServiceSummaryFilter {
    pub service_kind: Option<ServiceKind>,
    pub service_role: Option<ServiceRole>,
    pub service_name: Option<String>,
}

pub struct RegistryTopologyFilter {
    pub service_kind: Option<ServiceKind>,
    pub service_role: Option<ServiceRole>,
    pub service_name: Option<String>,
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
    pub subject_kind: Option<u32>,
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
    pub fn recv(&self, flags: i32) -> Result<u64, RecvError>;
    /// # Errors: HandlerError
    pub fn on_fire<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(&Timer, u64) + Send + 'static;
}
// Drop calls zlink_timer_destroy
```

---

## Poller

```rust
impl Poller {
    /// # Errors: ConfigError
    pub fn new() -> Result<Self, ConfigError>;
    /// # Errors: ConfigError
    pub fn add_socket<'a>(&self, socket: impl Into<PollTarget<'a>>,
        events: i16) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn modify_socket<'a>(&self, socket: impl Into<PollTarget<'a>>,
        events: i16) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn remove_socket<'a>(&self, socket: impl Into<PollTarget<'a>>)
        -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn add_fd(&self, fd: RawFd, events: i16, user_data: Option<*mut c_void>)
        -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn modify_fd(&self, fd: RawFd, events: i16) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn remove_fd(&self, fd: RawFd) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn add_timer(&self, timer: &Timer, user_data: Option<*mut c_void>)
        -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn remove_timer(&self, timer: &Timer) -> Result<(), ConfigError>;
    /// # Errors: RecvError
    pub fn wait(&self, timeout_ms: i64) -> Result<Option<PollEvent>, RecvError>;
    /// # Errors: RecvError
    pub fn wait_all(&self, timeout_ms: i64) -> Result<Vec<PollEvent>, RecvError>;
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

pub struct PollEvent {
    // ...
}

impl PollEvent {
    pub fn is_readable(&self) -> bool;
    pub fn is_writable(&self) -> bool;
}

pub const POLLIN: i16;
pub const POLLOUT: i16;
```

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
