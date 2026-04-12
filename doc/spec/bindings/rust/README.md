[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Rust Binding Specification

This document defines the complete public API surface of the zlink Rust binding.
Every struct, method, and function listed here is part of the contract that the
binding must expose. Private/internal items are omitted.

---

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
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError>;
    /// # Errors: HandlerError
    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(Received) + Send + 'static;
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
    /// # Errors: HandlerError
    pub fn on_subscribe<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(TopicMessage) + Send + 'static;
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
    /// # Errors: SubmitError
    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_with_flags(&self, parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError>;
    /// # Errors: HandlerError
    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(Received) + Send + 'static;
    /// # Errors: HandlerError
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn() + Send + 'static;
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
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError>;
    /// # Errors: HandlerError
    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(Received) + Send + 'static;
    /// # Errors: HandlerError
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn() + Send + 'static;
    pub fn send_handle(&self) -> SendHandle;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn router_options(&self) -> RouterSocketOptions<'_>;
    /// # Errors: ConfigError
    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ConfigError>;

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
        timeout: Duration) -> Result<Received, ZlinkError>;

    // --- router → spot routed request (callback) ---
    // Duration::ZERO uses the socket default timeout.
    // The callback receives Result<Received, RequestError>.
    /// # Errors: SubmitError (submit failure). Callback receives Result<Received, RequestError>.
    pub fn request_to_spot_callback<F>(&self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, parts: impl IntoMultipart, callback: F,
        flags: SendFlags, timeout: Duration)
        -> Result<(), SubmitError>
        where F: FnOnce(Result<Received, RequestError>) + Send + 'static;

    // --- router → spot routed reply ---
    /// # Errors: SubmitError
    pub fn reply_to_spot(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        request_seq: u64, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn reply_to_spot_with_flags(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        request_seq: u64, parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;

    // --- router spot receive ---
    /// # Errors: RecvError
    pub fn recv_spot(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_spot_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError>;
    /// # Errors: HandlerError
    pub fn on_spot_receive<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(RoutingId, RoutingId, u64, Vec<Message>) + Send + 'static;

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
    /// # Errors: HandlerError
    pub fn on_subscribe<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(TopicMessage) + Send + 'static;
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
    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError>;
    /// # Errors: HandlerError
    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(Received) + Send + 'static;
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

// Message implements Drop (calls zlink_msg_close).
// Message implements IntoMultipart.
```

### RoutingId

```rust
pub struct RoutingId { /* ... */ }
```

### Received

```rust
pub struct Received {
    pub routing_id: Option<RoutingId>,
    pub parts: Vec<Message>,
}
```

### TopicMessage

```rust
pub struct TopicMessage {
    pub routing_id: Option<RoutingId>,
    pub topic: String,
    pub parts: Vec<Message>,
}
```

### SubscriptionEvent

```rust
pub struct SubscriptionEvent {
    pub routing_id: Option<RoutingId>,
    pub topic: String,
    pub subscribed: bool,
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

Result codes for handler registration operations (on_receive, on_subscribe, etc.).

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

Errors from handler registration (`on_receive`, `on_subscribe`,
`on_send_ready`, `on_event`, `on_fire`, `on_dispatch_event`, etc.).

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
// Implemented for Message, Vec<Message>, &[u8], Vec<u8>, etc.
```

---

## Request-Reply

### RequestDealer

```rust
impl RequestDealer {
    /// # Errors: ConfigError
    pub fn new(socket: DealerSocket) -> Result<Self, ConfigError>;
    pub fn set_default_request_timeout(&self, timeout: Duration);
    pub fn get_default_request_timeout(&self) -> Duration;

    // Async request — no flags. Duration::ZERO uses socket default timeout.
    // Submit failure yields SubmitError; request failure yields RequestError;
    // both unify under ZlinkError at this API seam.
    /// # Errors: ZlinkError (SubmitError on submit, RequestError on completion)
    pub async fn request(&self, parts: impl IntoMultipart,
        timeout: Duration) -> Result<Received, ZlinkError>;

    // Callback request — Duration::ZERO uses socket default timeout.
    // The callback receives Result<Received, RequestError>.
    /// # Errors: SubmitError (submit failure). Callback receives Result<Received, RequestError>.
    pub fn request_callback<F>(&self, parts: impl IntoMultipart, callback: F,
        flags: SendFlags, timeout: Duration)
        -> Result<(), SubmitError>
        where F: FnOnce(Result<Received, RequestError>) + Send + 'static;

    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError>;
    /// # Errors: HandlerError
    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(Received) + Send + 'static;
}
```

### RequestRouter

```rust
impl RequestRouter {
    /// # Errors: ConfigError
    pub fn new(socket: RouterSocket) -> Result<Self, ConfigError>;
    pub fn set_default_request_timeout(&self, timeout: Duration);
    pub fn get_default_request_timeout(&self) -> Duration;

    // Async request — no flags. Duration::ZERO uses socket default timeout.
    // Submit failure yields SubmitError; request failure yields RequestError;
    // both unify under ZlinkError at this API seam.
    /// # Errors: ZlinkError (SubmitError on submit, RequestError on completion)
    pub async fn request(&self, routing_id: RoutingId, parts: impl IntoMultipart,
        timeout: Duration) -> Result<Received, ZlinkError>;

    // Callback request — Duration::ZERO uses socket default timeout.
    // The callback receives Result<Received, RequestError>.
    /// # Errors: SubmitError (submit failure). Callback receives Result<Received, RequestError>.
    pub fn request_callback<F>(&self, routing_id: RoutingId, parts: impl IntoMultipart,
        callback: F, flags: SendFlags, timeout: Duration)
        -> Result<(), SubmitError>
        where F: FnOnce(Result<Received, RequestError>) + Send + 'static;

    /// # Errors: SubmitError
    pub fn reply(&self, routing_id: RoutingId, request_seq: u64,
        parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn reply_with_flags(&self, routing_id: RoutingId, request_seq: u64,
        parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;

    /// # Errors: RecvError
    pub fn recv(&self) -> Result<Received, RecvError>;
    /// # Errors: RecvError
    pub fn recv_with_flags(&self, flags: RecvFlags) -> Result<Received, RecvError>;
    pub fn on_receive<F>(&self, handler: F)
        where F: Fn(Received) + Send + 'static;
}
```

---

## Monitoring

### SocketMonitor

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
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### ServiceMonitor

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
    pub state_flags: u32,
    pub detail_flags: u32,
    pub snd_pending_msgs: u64,
    pub rcv_pending_msgs: u64,
}

impl MonitorSnapshot {
    pub fn is_ready(&self) -> bool;
    pub fn is_closed(&self) -> bool;
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
    pub fn set_tls_server(&self, cert_path: &str, key_path: &str,
        require_client_cert: bool) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn set_tls_client(&self, ca_cert_path: &str, hostname: &str,
        trust_system: bool) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn status_snapshot(&self) -> Result<SpotNodeStatus, ConfigError>;
    /// # Errors: ConfigError
    pub fn peers_snapshot(&self) -> Result<Vec<SpotNodePeerEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn peers_query(&self, filter: &SpotNodePeerFilter)
        -> Result<Vec<SpotNodePeerEntry>, ConfigError>;
    /// # Errors: ConfigError
    pub fn subjects_snapshot(&self) -> Result<Vec<SpotNodeSubjectEntry>, ConfigError>;
    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

### Spot

```rust
impl Spot {
    /// # Errors: ConfigError
    pub fn new(node: &SpotNode) -> Result<Self, ConfigError>;
    /// # Errors: SubmitError
    pub fn publish(&self, topic: &str, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn publish_with_flags(&self, topic: &str, parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;
    /// # Errors: ConfigError
    pub fn set_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    /// # Errors: ConfigError
    pub fn unset_subscription(&self, filter: &str) -> Result<(), ConfigError>;
    /// # Errors: RecvError
    pub fn subscribe(&self) -> Result<TopicMessage, RecvError>;
    /// # Errors: RecvError
    pub fn subscribe_with_flags(&self, flags: RecvFlags) -> Result<TopicMessage, RecvError>;
    /// # Errors: HandlerError
    pub fn on_subscribe<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn(TopicMessage) + Send + 'static;
    /// # Errors: HandlerError
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), HandlerError>
        where F: Fn() + Send + 'static;

    // --- routed send (spot → spot) ---
    /// # Errors: SubmitError
    pub fn send_to_spot(&self, dest_node_rid: &RoutingId, dest_spot_rid: &RoutingId,
        parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_to_spot_with_flags(&self, dest_node_rid: &RoutingId, dest_spot_rid: &RoutingId,
        parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;

    // --- routed request (spot → spot, async) — no flags ---
    // Duration::ZERO uses the socket default timeout.
    // Submit failure yields SubmitError; request failure yields RequestError;
    // both unify under ZlinkError at this API seam.
    /// # Errors: ZlinkError (SubmitError on submit, RequestError on completion)
    pub async fn request_to_spot(&self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, parts: impl IntoMultipart,
        timeout: Duration) -> Result<Received, ZlinkError>;

    // --- routed request (spot → spot, callback) ---
    // Duration::ZERO uses the socket default timeout.
    // The callback receives Result<Received, RequestError>.
    /// # Errors: SubmitError (submit failure). Callback receives Result<Received, RequestError>.
    pub fn request_to_spot_callback<F>(&self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, parts: impl IntoMultipart, callback: F,
        flags: SendFlags, timeout: Duration)
        -> Result<(), SubmitError>
        where F: FnOnce(Result<Received, RequestError>) + Send + 'static;

    // --- routed reply (spot → spot) ---
    /// # Errors: SubmitError
    pub fn reply_to_spot(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        request_seq: u64, parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn reply_to_spot_with_flags(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        request_seq: u64, parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;

    // --- routed send (spot → router) ---
    /// # Errors: SubmitError
    pub fn send_to_router(&self, peer_rid: &RoutingId,
        parts: impl IntoMultipart) -> Result<(), SubmitError>;
    /// # Errors: SubmitError
    pub fn send_to_router_with_flags(&self, peer_rid: &RoutingId,
        parts: impl IntoMultipart, flags: SendFlags) -> Result<(), SubmitError>;

    // --- routed request (spot → router, async) — no flags ---
    // Duration::ZERO uses the socket default timeout.
    // Submit failure yields SubmitError; request failure yields RequestError;
    // both unify under ZlinkError at this API seam.
    /// # Errors: ZlinkError (SubmitError on submit, RequestError on completion)
    pub async fn request_to_router(&self, peer_rid: RoutingId,
        parts: impl IntoMultipart, timeout: Duration) -> Result<Received, ZlinkError>;

    // --- routed request (spot → router, callback) ---
    // Duration::ZERO uses the socket default timeout.
    // The callback receives Result<Received, RequestError>.
    /// # Errors: SubmitError (submit failure). Callback receives Result<Received, RequestError>.
    pub fn request_to_router_callback<F>(&self, peer_rid: RoutingId,
        parts: impl IntoMultipart, callback: F,
        flags: SendFlags, timeout: Duration)
        -> Result<(), SubmitError>
        where F: FnOnce(Result<Received, RequestError>) + Send + 'static;

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
        where F: Fn(SpotDispatchEvent) + Send + 'static;

    /// # Errors: CloseError
    pub fn close(&mut self) -> Result<(), CloseError>;
}
```

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
    pub fn size(&self) -> i32;
}
// Drop calls zlink_poller_destroy

/// Legacy poll function for simple use cases.
/// # Errors: RecvError
pub fn poll(items: &mut [PollItem], timeout_ms: i64) -> Result<i32, RecvError>;

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
