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
    pub fn new() -> Result<Self, ZlinkError>;
    pub fn shutdown(&self) -> Result<(), ZlinkError>;
    pub fn options(&self) -> ContextOptions<'_>;

    // Socket factories
    pub fn pair_socket(&self) -> Result<PairSocket, ZlinkError>;
    pub fn pub_socket(&self) -> Result<PubSocket, ZlinkError>;
    pub fn sub_socket(&self) -> Result<SubSocket, ZlinkError>;
    pub fn dealer_socket(&self) -> Result<DealerSocket, ZlinkError>;
    pub fn router_socket(&self) -> Result<RouterSocket, ZlinkError>;
    pub fn xpub_socket(&self) -> Result<XPubSocket, ZlinkError>;
    pub fn xsub_socket(&self) -> Result<XSubSocket, ZlinkError>;
    pub fn stream_socket(&self) -> Result<StreamSocket, ZlinkError>;
}
// Drop calls zlink_ctx_term
```

### ContextOptions

```rust
impl<'a> ContextOptions<'a> {
    pub fn io_threads(&self) -> Result<i32, ZlinkError>;
    pub fn set_io_threads(&self, threads: i32) -> Result<(), ZlinkError>;
    pub fn max_sockets(&self) -> Result<i32, ZlinkError>;
    pub fn set_max_sockets(&self, max: i32) -> Result<(), ZlinkError>;
    pub fn socket_limit(&self) -> Result<i32, ZlinkError>;
    pub fn thread_priority(&self) -> Result<i32, ZlinkError>;
    pub fn set_thread_priority(&self, priority: i32) -> Result<(), ZlinkError>;
    pub fn thread_scheduling_policy(&self) -> Result<i32, ZlinkError>;
    pub fn set_thread_scheduling_policy(&self, policy: i32) -> Result<(), ZlinkError>;
    pub fn max_msg_size(&self) -> Result<i32, ZlinkError>;
    pub fn set_max_msg_size(&self, size: i32) -> Result<(), ZlinkError>;
    pub fn msg_t_size(&self) -> Result<i32, ZlinkError>;
    pub fn blocky(&self) -> Result<bool, ZlinkError>;
    pub fn set_blocky(&self, blocky: bool) -> Result<(), ZlinkError>;
    pub fn add_thread_affinity(&self, cpu: i32) -> Result<(), ZlinkError>;
    pub fn remove_thread_affinity(&self, cpu: i32) -> Result<(), ZlinkError>;
}
```

### Module-Level Functions

```rust
pub fn version() -> (i32, i32, i32);
pub fn has(capability: &str) -> bool;

/// Start a built-in proxy between frontend and backend sockets.
pub fn proxy<'a>(frontend: impl Into<PollTarget<'a>>,
                 backend: impl Into<PollTarget<'a>>,
                 capture: Option<impl Into<PollTarget<'a>>>) -> Result<(), ZlinkError>;

/// Start a steerable proxy with an additional control socket.
pub fn proxy_steerable<'a>(frontend: impl Into<PollTarget<'a>>,
                           backend: impl Into<PollTarget<'a>>,
                           capture: Option<impl Into<PollTarget<'a>>>,
                           control: impl Into<PollTarget<'a>>) -> Result<(), ZlinkError>;

/// Sleep for the given number of seconds.
pub fn sleep(seconds: i32);

/// Close all parts in a multipart message array.
pub fn multipart_close(parts: &mut [Message]);
```

### Errno / Strerror

Errors are mapped through the `ZlinkError` type, which implements
`std::error::Error` and `Display`. The C-level `zlink_errno()` and
`zlink_strerror()` are used internally to construct `ZlinkError`
values returned via `Result`. Direct access is not exposed.

---

## Socket Types

All sockets implement `Drop` (calls `close`). Common connection and
option methods are provided through the internal `SocketCore` but appear
as direct methods on each socket type.

### PairSocket

```rust
impl PairSocket {
    pub fn bind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn unbind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn connect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn disconnect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), ZlinkError>;
    pub fn try_send(&self, parts: impl IntoMultipart) -> Result<SendResult, ZlinkError>;
    pub fn recv(&self) -> Result<Received, ZlinkError>;
    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError>;
    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(Received) + Send + 'static;
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn() + Send + 'static;
    pub fn send_handle(&self) -> SendHandle;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### PubSocket

```rust
impl PubSocket {
    pub fn bind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn unbind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn connect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn disconnect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn publish(&self, topic: &str, parts: impl IntoMultipart) -> Result<(), ZlinkError>;
    pub fn try_publish(&self, topic: &str, parts: impl IntoMultipart)
        -> Result<SendResult, ZlinkError>;
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn() + Send + 'static;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn pub_options(&self) -> PubSocketOptions<'_, Self>;
    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ZlinkError>;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### SubSocket

```rust
impl SubSocket {
    pub fn bind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn unbind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn connect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn disconnect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn set_subscription(&self, filter: &str) -> Result<(), ZlinkError>;
    pub fn unset_subscription(&self, filter: &str) -> Result<(), ZlinkError>;
    pub fn subscribe(&self) -> Result<TopicMessage, ZlinkError>;
    pub fn try_subscribe(&self) -> Result<Option<TopicMessage>, ZlinkError>;
    pub fn on_subscribe<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(TopicMessage) + Send + 'static;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn sub_options(&self) -> SubSocketOptions<'_, Self>;
    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ZlinkError>;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### DealerSocket

```rust
impl DealerSocket {
    pub fn bind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn unbind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn connect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn disconnect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), ZlinkError>;
    pub fn routing_id(&self) -> Result<RoutingId, ZlinkError>;
    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), ZlinkError>;
    pub fn try_send(&self, parts: impl IntoMultipart) -> Result<SendResult, ZlinkError>;
    pub fn recv(&self) -> Result<Received, ZlinkError>;
    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError>;
    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(Received) + Send + 'static;
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn() + Send + 'static;
    pub fn send_handle(&self) -> SendHandle;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn dealer_options(&self) -> DealerSocketOptions<'_>;
    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ZlinkError>;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### RouterSocket

```rust
impl RouterSocket {
    pub fn bind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn unbind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn connect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn disconnect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), ZlinkError>;
    pub fn routing_id(&self) -> Result<RoutingId, ZlinkError>;
    pub fn send(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<(), ZlinkError>;
    pub fn try_send(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<SendResult, ZlinkError>;
    pub fn recv(&self) -> Result<Received, ZlinkError>;
    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError>;
    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(Received) + Send + 'static;
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn() + Send + 'static;
    pub fn send_handle(&self) -> SendHandle;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn router_options(&self) -> RouterSocketOptions<'_>;
    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ZlinkError>;

    // --- router → spot routed send ---
    pub fn send_spot(&self, dest_node_rid: &RoutingId, dest_spot_rid: &RoutingId,
        parts: impl IntoMultipart) -> Result<(), ZlinkError>;
    pub fn try_send_spot(&self, dest_node_rid: &RoutingId, dest_spot_rid: &RoutingId,
        parts: impl IntoMultipart) -> Result<SendResult, ZlinkError>;

    // --- router → spot routed request (async) ---
    pub async fn request_spot(&self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, msg: Message) -> Result<Received, ZlinkError>;
    pub async fn request_spot_with_timeout(&self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, msg: Message, timeout: Duration)
        -> Result<Received, ZlinkError>;
    pub fn request_spot_callback<F>(&self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, msg: Message, callback: F)
        where F: FnOnce(Result<Received, ZlinkError>) + Send + 'static;

    // --- router → spot routed reply ---
    pub fn reply_spot(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        request_seq: u64, msg: Message) -> Result<(), ZlinkError>;

    // --- router spot receive ---
    pub fn recv_spot(&self) -> Result<Received, ZlinkError>;
    pub fn try_recv_spot(&self) -> Result<Option<Received>, ZlinkError>;
    pub fn on_spot_receive<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(RoutingId, RoutingId, u64, Vec<Message>) + Send + 'static;

    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### XPubSocket

```rust
impl XPubSocket {
    pub fn bind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn unbind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn connect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn disconnect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn publish(&self, topic: &str, parts: impl IntoMultipart) -> Result<(), ZlinkError>;
    pub fn try_publish(&self, topic: &str, parts: impl IntoMultipart)
        -> Result<SendResult, ZlinkError>;
    pub fn receive_subscription_event(&self) -> Result<SubscriptionEvent, ZlinkError>;
    pub fn try_receive_subscription_event(&self)
        -> Result<Option<SubscriptionEvent>, ZlinkError>;
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn() + Send + 'static;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn pub_options(&self) -> PubSocketOptions<'_, Self>;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### XSubSocket

```rust
impl XSubSocket {
    pub fn bind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn unbind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn connect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn disconnect(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn set_subscription(&self, filter: &str) -> Result<(), ZlinkError>;
    pub fn unset_subscription(&self, filter: &str) -> Result<(), ZlinkError>;
    pub fn subscribe(&self) -> Result<TopicMessage, ZlinkError>;
    pub fn try_subscribe(&self) -> Result<Option<TopicMessage>, ZlinkError>;
    pub fn on_subscribe<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(TopicMessage) + Send + 'static;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn sub_options(&self) -> SubSocketOptions<'_, Self>;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### StreamSocket

```rust
impl StreamSocket {
    pub fn bind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn unbind(&self, addr: &str) -> Result<(), ZlinkError>;
    pub fn set_routing_id(&self, id: &RoutingId) -> Result<(), ZlinkError>;
    pub fn routing_id(&self) -> Result<RoutingId, ZlinkError>;
    pub fn send(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<(), ZlinkError>;
    pub fn try_send(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<SendResult, ZlinkError>;
    pub fn recv(&self) -> Result<Received, ZlinkError>;
    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError>;
    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(Received) + Send + 'static;
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn() + Send + 'static;
    pub fn send_handle(&self) -> SendHandle;
    pub fn common_options(&self) -> CommonSocketOptions<'_, Self>;
    pub fn stream_options(&self) -> StreamSocketOptions<'_>;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### SendHandle

A thread-safe handle for sending from callbacks.

```rust
impl SendHandle {
    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), ZlinkError>;
    pub fn send_to(&self, target: &RoutingId, parts: impl IntoMultipart)
        -> Result<(), ZlinkError>;
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

### SendResult

```rust
pub enum SendResult {
    Sent,
    Backpressured,
    NotReady,
}
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
    pub fn new(socket: DealerSocket) -> Result<Self, ZlinkError>;
    pub fn set_default_request_timeout(&self, timeout: Duration);
    pub fn get_default_request_timeout(&self) -> Duration;

    pub async fn request(&self, msg: Message) -> Result<Received, ZlinkError>;
    pub async fn request_with_timeout(&self, msg: Message, timeout: Duration)
        -> Result<Received, ZlinkError>;
    pub async fn try_request(&self, msg: Message) -> Result<Received, ZlinkError>;
    pub async fn try_request_with_timeout(&self, msg: Message, timeout: Duration)
        -> Result<Received, ZlinkError>;

    pub fn request_callback<F>(&self, msg: Message, callback: F)
        where F: FnOnce(Result<Received, ZlinkError>) + Send + 'static;
    pub fn request_callback_with_timeout<F>(&self, msg: Message, callback: F,
        timeout: Duration)
        where F: FnOnce(Result<Received, ZlinkError>) + Send + 'static;
    pub fn try_request_callback<F>(&self, msg: Message, callback: F)
        where F: FnOnce(Result<Received, ZlinkError>) + Send + 'static;
    pub fn try_request_callback_with_timeout<F>(&self, msg: Message, callback: F,
        timeout: Duration)
        where F: FnOnce(Result<Received, ZlinkError>) + Send + 'static;

    pub fn recv(&self) -> Result<Received, ZlinkError>;
    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError>;
    pub fn on_receive<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(Received) + Send + 'static;
}
```

### RequestRouter

```rust
impl RequestRouter {
    pub fn new(socket: RouterSocket) -> Result<Self, ZlinkError>;
    pub fn set_default_request_timeout(&self, timeout: Duration);
    pub fn get_default_request_timeout(&self) -> Duration;

    pub async fn request(&self, routing_id: RoutingId, msg: Message)
        -> Result<Received, ZlinkError>;
    pub async fn request_with_timeout(&self, routing_id: RoutingId,
        msg: Message, timeout: Duration) -> Result<Received, ZlinkError>;
    pub async fn try_request(&self, routing_id: RoutingId, msg: Message)
        -> Result<Received, ZlinkError>;
    pub async fn try_request_with_timeout(&self, routing_id: RoutingId,
        msg: Message, timeout: Duration) -> Result<Received, ZlinkError>;

    pub fn request_callback<F>(&self, routing_id: RoutingId, msg: Message,
        callback: F)
        where F: FnOnce(Result<Received, ZlinkError>) + Send + 'static;
    pub fn request_callback_with_timeout<F>(&self, routing_id: RoutingId,
        msg: Message, callback: F, timeout: Duration)
        where F: FnOnce(Result<Received, ZlinkError>) + Send + 'static;
    pub fn try_request_callback<F>(&self, routing_id: RoutingId, msg: Message,
        callback: F)
        where F: FnOnce(Result<Received, ZlinkError>) + Send + 'static;
    pub fn try_request_callback_with_timeout<F>(&self, routing_id: RoutingId,
        msg: Message, callback: F, timeout: Duration)
        where F: FnOnce(Result<Received, ZlinkError>) + Send + 'static;

    pub fn reply(&self, routing_id: RoutingId, request_seq: u64,
        msg: Message) -> Result<(), ZlinkError>;
    pub fn try_reply(&self, routing_id: RoutingId, request_seq: u64,
        msg: Message) -> SendResult;

    pub fn recv(&self) -> Result<Received, ZlinkError>;
    pub fn try_recv(&self) -> Result<Option<Received>, ZlinkError>;
    pub fn on_receive<F>(&self, handler: F)
        where F: Fn(Received) + Send + 'static;
}
```

---

## Monitoring

### SocketMonitor

```rust
impl SocketMonitor {
    pub fn open<'a>(socket: impl Into<MonitorTarget<'a>>) -> Result<Self, ZlinkError>;
    pub fn recv(&self) -> Result<MonitorEvent, ZlinkError>;
    pub fn try_recv(&self) -> Result<Option<MonitorEvent>, ZlinkError>;
    pub fn snapshot(&self) -> Result<MonitorSnapshot, ZlinkError>;
    pub fn on_event<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(&MonitorEvent) + Send + 'static;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### ServiceMonitor

```rust
impl ServiceMonitor {
    pub fn open<'a>(target: impl Into<ServiceMonitorTarget<'a>>) -> Result<Self, ZlinkError>;
    pub fn recv(&self) -> Result<ServiceEvent, ZlinkError>;
    pub fn try_recv(&self) -> Result<Option<ServiceEvent>, ZlinkError>;
    pub fn snapshot(&self) -> Result<MonitorSnapshot, ZlinkError>;
    pub fn on_event<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(&ServiceEvent) + Send + 'static;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
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
    pub fn new(ctx: &Context) -> Result<Self, ZlinkError>;
    pub fn bind(&self, pub_endpoint: &str, router_endpoint: &str) -> Result<(), ZlinkError>;
    pub fn set_id(&self, id: u32) -> Result<(), ZlinkError>;
    pub fn add_peer(&self, peer_pub_endpoint: &str) -> Result<(), ZlinkError>;
    pub fn set_heartbeat(&self, interval_ms: u32, timeout_ms: u32) -> Result<(), ZlinkError>;
    pub fn set_broadcast_interval(&self, interval_ms: u32) -> Result<(), ZlinkError>;
    pub fn set_tls_server(&self, cert_path: &str, key_path: &str,
        require_client_cert: bool) -> Result<(), ZlinkError>;
    pub fn set_tls_client(&self, ca_cert_path: &str, hostname: &str,
        trust_system: bool) -> Result<(), ZlinkError>;
    pub fn status_snapshot(&self) -> Result<RegistryStatus, ZlinkError>;
    pub fn service_summary_snapshot(&self) -> Result<Vec<RegistryServiceSummaryEntry>, ZlinkError>;
    pub fn service_summary_query(&self, filter: &RegistryServiceSummaryFilter)
        -> Result<Vec<RegistryServiceSummaryEntry>, ZlinkError>;
    pub fn member_peers(&self, service_type: ServiceType, service_name: &str)
        -> Result<Vec<MemberPeerEntry>, ZlinkError>;
    pub fn member_peer_metadata(&self, service_type: ServiceType, service_name: &str,
        service_role: ServiceRole, endpoint: &str) -> Result<Message, ZlinkError>;
    pub fn topology_snapshot(&self) -> Result<Vec<RegistryTopologyEntry>, ZlinkError>;
    pub fn topology_query(&self, filter: &RegistryTopologyFilter)
        -> Result<Vec<RegistryTopologyEntry>, ZlinkError>;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### Discovery

```rust
impl Discovery {
    pub fn new(ctx: &Context, service_type: ServiceType, service_name: &str)
        -> Result<Self, ZlinkError>;
    pub fn connect_registry(&self, endpoint: &str) -> Result<(), ZlinkError>;
    pub fn set_value(&self, value: i64) -> Result<(), ZlinkError>;
    pub fn get_value(&self) -> Result<i64, ZlinkError>;
    pub fn set_metadata(&self, data: &[u8]) -> Result<(), ZlinkError>;
    pub fn get_metadata(&self) -> Result<Message, ZlinkError>;
    pub fn member_peers(&self) -> Result<Vec<MemberPeerEntry>, ZlinkError>;
    pub fn member_peer_metadata(&self, service_role: ServiceRole, endpoint: &str)
        -> Result<Message, ZlinkError>;
    pub fn monitor_open(&self) -> Result<ServiceMonitor, ZlinkError>;
    pub fn set_tls_client(&self, ca_cert_path: &str, hostname: &str,
        trust_system: bool) -> Result<(), ZlinkError>;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### SpotNode

```rust
impl SpotNode {
    pub fn new(ctx: &Context) -> Result<Self, ZlinkError>;
    pub fn bind(&self, endpoint: &str) -> Result<(), ZlinkError>;
    pub fn last_endpoint(&self) -> Result<String, ZlinkError>;
    pub fn connect_peer(&self, peer_endpoint: &str) -> Result<(), ZlinkError>;
    pub fn disconnect_peer(&self, peer_endpoint: &str) -> Result<(), ZlinkError>;
    pub fn attach_discovery(&self, discovery: &Discovery) -> Result<(), ZlinkError>;
    pub fn set_tls_server(&self, cert_path: &str, key_path: &str,
        require_client_cert: bool) -> Result<(), ZlinkError>;
    pub fn set_tls_client(&self, ca_cert_path: &str, hostname: &str,
        trust_system: bool) -> Result<(), ZlinkError>;
    pub fn status_snapshot(&self) -> Result<SpotNodeStatus, ZlinkError>;
    pub fn peers_snapshot(&self) -> Result<Vec<SpotNodePeerEntry>, ZlinkError>;
    pub fn peers_query(&self, filter: &SpotNodePeerFilter)
        -> Result<Vec<SpotNodePeerEntry>, ZlinkError>;
    pub fn subjects_snapshot(&self) -> Result<Vec<SpotNodeSubjectEntry>, ZlinkError>;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### Spot

```rust
impl Spot {
    pub fn new(node: &SpotNode) -> Result<Self, ZlinkError>;
    pub fn publish(&self, topic: &str, parts: impl IntoMultipart) -> Result<(), ZlinkError>;
    pub fn try_publish(&self, topic: &str, parts: impl IntoMultipart)
        -> Result<SendResult, ZlinkError>;
    pub fn set_subscription(&self, filter: &str) -> Result<(), ZlinkError>;
    pub fn unset_subscription(&self, filter: &str) -> Result<(), ZlinkError>;
    pub fn subscribe(&self) -> Result<TopicMessage, ZlinkError>;
    pub fn try_subscribe(&self) -> Result<Option<TopicMessage>, ZlinkError>;
    pub fn on_subscribe<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(TopicMessage) + Send + 'static;
    pub fn on_send_ready<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn() + Send + 'static;

    // --- routed send (spot → spot) ---
    pub fn send_spot(&self, dest_node_rid: &RoutingId, dest_spot_rid: &RoutingId,
        parts: impl IntoMultipart) -> Result<(), ZlinkError>;
    pub fn try_send_spot(&self, dest_node_rid: &RoutingId, dest_spot_rid: &RoutingId,
        parts: impl IntoMultipart) -> Result<SendResult, ZlinkError>;

    // --- routed request (spot → spot, async) ---
    pub async fn request_spot(&self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, msg: Message) -> Result<Received, ZlinkError>;
    pub async fn request_spot_with_timeout(&self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, msg: Message, timeout: Duration)
        -> Result<Received, ZlinkError>;
    pub fn request_spot_callback<F>(&self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, msg: Message, callback: F)
        where F: FnOnce(Result<Received, ZlinkError>) + Send + 'static;

    // --- routed reply (spot → spot) ---
    pub fn reply_spot(&self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
        request_seq: u64, msg: Message) -> Result<(), ZlinkError>;

    // --- routed send (spot → router) ---
    pub fn send_router(&self, peer_rid: &RoutingId,
        parts: impl IntoMultipart) -> Result<(), ZlinkError>;
    pub fn try_send_router(&self, peer_rid: &RoutingId,
        parts: impl IntoMultipart) -> Result<SendResult, ZlinkError>;

    // --- routed request (spot → router, async) ---
    pub async fn request_router(&self, peer_rid: RoutingId,
        msg: Message) -> Result<Received, ZlinkError>;
    pub async fn request_router_with_timeout(&self, peer_rid: RoutingId,
        msg: Message, timeout: Duration) -> Result<Received, ZlinkError>;
    pub fn request_router_callback<F>(&self, peer_rid: RoutingId, msg: Message,
        callback: F)
        where F: FnOnce(Result<Received, ZlinkError>) + Send + 'static;

    // --- routed reply (spot → router) ---
    pub fn reply_router(&self, peer_rid: RoutingId, request_seq: u64,
        msg: Message) -> Result<(), ZlinkError>;

    // --- routed receive ---
    pub fn recv_routed(&self) -> Result<Received, ZlinkError>;
    pub fn try_recv_routed(&self) -> Result<Option<Received>, ZlinkError>;
    pub fn on_routed_receive<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(RoutingId, RoutingId, u64, Vec<Message>) + Send + 'static;
    pub fn on_dispatch_event<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(SpotDispatchEvent) + Send + 'static;

    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

### RegistryQueryClient

```rust
impl RegistryQueryClient {
    pub fn new(ctx: &Context) -> Result<Self, ZlinkError>;
    pub fn connect(&self, endpoint: &str) -> Result<(), ZlinkError>;
    pub fn snapshot(&self, filter: Option<&RegistryTopologyFilter>)
        -> Result<Vec<RegistryTopologyEntry>, ZlinkError>;
    pub fn close(&mut self) -> Result<(), ZlinkError>;
}
```

---

## Timer

### Timer

```rust
impl Timer {
    pub fn new() -> Result<Self, ZlinkError>;
    pub fn from_spot(spot: &Spot) -> Result<Self, ZlinkError>;
    pub fn start(&self, interval_ns: u64, repeat_count: u64) -> Result<(), ZlinkError>;
    pub fn stop(&self) -> Result<(), ZlinkError>;
    pub fn recv(&self, flags: i32) -> Result<u64, ZlinkError>;
    pub fn on_fire<F>(&mut self, handler: F) -> Result<(), ZlinkError>
        where F: Fn(&Timer, u64) + Send + 'static;
}
// Drop calls zlink_timer_destroy
```

---

## Poller

```rust
impl Poller {
    pub fn new() -> Result<Self, ZlinkError>;
    pub fn add_socket<'a>(&self, socket: impl Into<PollTarget<'a>>,
        events: i16) -> Result<(), ZlinkError>;
    pub fn modify_socket<'a>(&self, socket: impl Into<PollTarget<'a>>,
        events: i16) -> Result<(), ZlinkError>;
    pub fn remove_socket<'a>(&self, socket: impl Into<PollTarget<'a>>)
        -> Result<(), ZlinkError>;
    pub fn add_fd(&self, fd: RawFd, events: i16, user_data: Option<*mut c_void>)
        -> Result<(), ZlinkError>;
    pub fn modify_fd(&self, fd: RawFd, events: i16) -> Result<(), ZlinkError>;
    pub fn remove_fd(&self, fd: RawFd) -> Result<(), ZlinkError>;
    pub fn add_timer(&self, timer: &Timer, user_data: Option<*mut c_void>)
        -> Result<(), ZlinkError>;
    pub fn remove_timer(&self, timer: &Timer) -> Result<(), ZlinkError>;
    pub fn wait(&self, timeout_ms: i64) -> Result<Option<PollEvent>, ZlinkError>;
    pub fn wait_all(&self, timeout_ms: i64) -> Result<Vec<PollEvent>, ZlinkError>;
    pub fn size(&self) -> i32;
}
// Drop calls zlink_poller_destroy

/// Legacy poll function for simple use cases.
pub fn poll(items: &mut [PollItem], timeout_ms: i64) -> Result<i32, ZlinkError>;

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
