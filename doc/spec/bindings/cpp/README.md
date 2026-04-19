[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# C++ Binding Specification

This document defines the complete public API surface of the C++ binding.
Every class, its purpose, and all public method signatures are listed.
Internal helpers and implementation details are omitted.

All types live in the `zlink` namespace. Service types live in `zlink::service`.

Only installed public headers are part of the contract. Private helper headers,
native bridge headers, and source-tree-only utilities are internal. Perf,
samples, and tests must include the public C++ headers only and must not rely
on non-installed internal headers.

---

## Current Core Alignment Overrides

The sections below still contain some older signatures. When a later section
conflicts with the rules here, this section wins.

- `pair_socket_t`, `dealer_socket_t`, and `router_socket_t` are recv-only on
  the data plane. Remove `on_receive(...)` from their public contract.
- `sub_socket_t` and `xsub_socket_t` are recv-only. Remove
  `on_subscribe(...)` from their public contract.
- `stream_socket_t` keeps `recv(...)` and raw `on_receive(...)`, and must also
  expose a dedicated packet callback surface mapped to
  `zlink_stream_packet_handler()`. Recommended canonical name:
  `on_packet(...)`.
- `service::spot_node_t` must expose channel-aware attachment methods:
  `attach_channel_dealer(...)`,
  `attach_channel_dealer_manual(...)`,
  `attach_pub_ingress(...)`, and `attach_discovery(...)`.
- `service::spot_t` must expose channel-aware data-plane methods:
  `send_channel(...)`, `request_channel(...)`, and
  `publish(const std::string& service_name, const std::string& topic, ...)`.
- `service::spot_t::subscribe(...)` returns a service-aware `topic_message_t`.
  `topic_message_t` therefore needs `std::optional<std::string> service_name()`
  populated for SPOT subscribe results and empty for raw `SUB` / `XSUB`.
- `service::spot_t` must not expose `on_subscribe(...)`. SPOT topic readable
  notifications come from `on_dispatch_event(...)`, then callers drain with
  `subscribe(...)` / `recv_routed(...)` / timer recv.
- `service::spot_t::on_routed_receive(...)` and
  `service::spot_t::on_dispatch_event(...)` are mutually exclusive on the
  routed axis.
- Every socket and `service::spot_t` exposes `set_admission_state(...)` /
  `get_admission_state(...)` returning the typed enum
  `admission_state_t { serving = 1, draining = 2 }`. Submit attempts to a
  drained peer fail with `submit_error_t{.code = submit_result_t::not_admitted}`.
- `pollout` is a send-recovery readiness signal, shared with
  `on_send_ready(...)`. It is not a "transport writable" bit.
- ROUTER / PUB socket option defaults follow the core header:
  `mandatory = true`, `handover = true`, `nodrop = true`.
- Internal pairing rule: when auto-connect pairs two same-service ROUTERs via
  Discovery, the library picks one initiator per pair by total order on
  `(routing_id, advertise endpoint)`. Users do not configure this.

## Core

### context_t

RAII wrapper for a zlink context. Manages the lifecycle of IO threads and sockets.

```cpp
class context_t {
    context_t();
    explicit context_t(int io_threads);
    ~context_t();

    context_t(context_t&& other) noexcept;
    context_t& operator=(context_t&& other) noexcept;

    bool valid() const noexcept;
    void* handle() noexcept;
    const void* handle() const noexcept;

    /// @throws close_error_t
    void shutdown();
    void term() noexcept;

    context_options_t options();
};
```

### context_options_t

Typed facade for context configuration options.

```cpp
class context_options_t {
    explicit context_options_t(context_t& ctx);

    // All option setters/getters throw config_error_t on failure.
    /// @throws config_error_t
    int ioThreads() const;
    /// @throws config_error_t
    void ioThreads(int value);
    /// @throws config_error_t
    int maxSockets() const;
    /// @throws config_error_t
    void maxSockets(int value);
    /// @throws config_error_t
    int maxMsgSize() const;
    /// @throws config_error_t
    void maxMsgSize(int value);
    /// @throws config_error_t
    int threadPriority() const;
    /// @throws config_error_t
    void threadPriority(int value);
    /// @throws config_error_t
    int threadSchedulingPolicy() const;
    /// @throws config_error_t
    void threadSchedulingPolicy(int value);
    /// @throws config_error_t
    bool blocky() const;
    /// @throws config_error_t
    void blocky(bool enabled);
    /// @throws config_error_t
    int socketLimit() const;
    /// @throws config_error_t
    int msgTSize() const;
    /// @throws config_error_t
    void addThreadAffinity(int cpu);
    /// @throws config_error_t
    void removeThreadAffinity(int cpu);
};
```

---

## Socket Types

### Common base methods

All socket types inherit from `base_socket_t` and expose these common operations.
Individual socket classes re-expose them as public.

Nonblocking data-plane helpers follow this rule:

- `try_send(...)` returns `false` only for temporary backpressure.
- Route-not-ready and other submit failures still throw `submit_error_t`.
- `try_recv()` returns `std::nullopt` when no message is currently available
  and still throws `recv_error_t` for real recv failures.

```cpp
// Available on all socket types
bool valid() const noexcept;
/// @throws bind_error_t
void bind(const std::string& endpoint);
/// @throws connect_error_t
void unbind(const std::string& endpoint);
/// @throws config_error_t
common_socket_options_t options();
/// @throws config_error_t
void set_admission_state(admission_state_t state);
/// @throws config_error_t
admission_state_t get_admission_state() const;
/// @throws config_error_t
void set_tls_server(const std::string& cert, const std::string& key,
                    bool require_client_cert = false);
/// @throws config_error_t
void set_tls_client(const std::string& ca_cert, const std::string& hostname,
                    bool trust_system = false);
/// @throws config_error_t
monitor_handle_t monitor_handle(monitor_event events = monitor_event::all) const;

// Available on connectable socket types (all except stream_socket_t)
/// @throws connect_error_t
void connect(const std::string& endpoint);
/// @throws connect_error_t
void disconnect(const std::string& endpoint);
```

### pair_socket_t

Bidirectional exclusive pair socket. Sends and receives messages without routing.

```cpp
class pair_socket_t : public message_socket_t {
    explicit pair_socket_t(context_t& ctx);

    // --- send ---
    /// @throws submit_error_t
    void send(message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void send(std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool try_send(message_t& part);
    /// @throws submit_error_t
    bool try_send(std::vector<message_t>& parts);

    // --- receive ---
    /// @throws recv_error_t
    received_t recv(recv_flags_t flags = recv_flags_t::none);
    /// @throws recv_error_t
    std::optional<received_t> try_recv();
    /// @throws handler_error_t
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);
};
```

### pub_socket_t

Publisher socket. Sends topic-prefixed messages to all matching subscribers.

```cpp
class pub_socket_t : public publisher_socket_t {
    explicit pub_socket_t(context_t& ctx);

    // --- publish ---
    /// @throws submit_error_t
    void publish(const std::string& topic_id, message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void publish(const std::string& topic_id, std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);
    /// @throws handler_error_t
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- pub-specific options ---
    /// @throws config_error_t
    pub_socket_options_t pub_options();

    // --- discovery ---
    /// @throws config_error_t
    void attach_discovery(DiscoveryT& discovery);
};
```

### sub_socket_t

Subscriber socket. Receives topic-filtered messages from publishers.

```cpp
class sub_socket_t : public subscriber_socket_t {
    explicit sub_socket_t(context_t& ctx);

    // --- subscription ---
    /// @throws config_error_t
    void set_subscription(const std::string& filter);
    /// @throws config_error_t
    void unset_subscription(const std::string& filter);
    /// @throws config_error_t
    void subscription_at(size_t index, std::string& filter, bool* is_pattern = NULL);

    // --- receive ---
    /// @throws recv_error_t
    topic_message_t subscribe(recv_flags_t flags = recv_flags_t::none);
    // --- sub-specific options ---
    /// @throws config_error_t
    sub_socket_options_t sub_options();

    // --- discovery ---
    /// @throws config_error_t
    void attach_discovery(DiscoveryT& discovery);
};
```

### dealer_socket_t

Asynchronous client socket for fair-queued request distribution.

```cpp
class dealer_socket_t : public message_socket_t {
    explicit dealer_socket_t(context_t& ctx);

    // --- send ---
    /// @throws submit_error_t
    void send(message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void send(std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool try_send(message_t& part);
    /// @throws submit_error_t
    bool try_send(std::vector<message_t>& parts);

    // --- receive ---
    /// @throws recv_error_t
    received_t recv(recv_flags_t flags = recv_flags_t::none);
    /// @throws recv_error_t
    std::optional<received_t> try_recv();
    /// @throws handler_error_t
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- request (coroutine, blocking submit — no flags) ---
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request(message_t& part,
                                                   std::chrono::milliseconds timeout = {});
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request(std::vector<message_t>& parts,
                                                   std::chrono::milliseconds timeout = {});

    // --- request (callback, blocking submit; callback receives request_result_t) ---
    /// @throws submit_error_t
    void request(message_t& part,
                 std::function<void(request_result_t, std::vector<message_t>)> callback,
                 std::chrono::milliseconds timeout = {});
    /// @throws submit_error_t
    void request(std::vector<message_t>& parts,
                 std::function<void(request_result_t, std::vector<message_t>)> callback,
                 std::chrono::milliseconds timeout = {});
    /// @throws submit_error_t
    bool try_request(message_t& part,
                     std::function<void(request_result_t, std::vector<message_t>)> callback,
                     std::chrono::milliseconds timeout = {});
    /// @throws submit_error_t
    bool try_request(std::vector<message_t>& parts,
                     std::function<void(request_result_t, std::vector<message_t>)> callback,
                     std::chrono::milliseconds timeout = {});

    // --- request configuration ---
    /// @throws config_error_t
    void set_default_request_timeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds get_default_request_timeout() const;

    // --- identity / routing ---
    /// @throws config_error_t
    void set_routing_id(const routing_id_t& routing_id);
    /// @throws config_error_t
    void get_routing_id(routing_id_t& routing_id) const;

    // --- dealer-specific options ---
    /// @throws config_error_t
    dealer_socket_options_t dealer_options();

    // --- discovery ---
    /// @throws config_error_t
    void attach_discovery(DiscoveryT& discovery);
};
```

### router_socket_t

Server socket that routes messages to specific peers by routing id.

```cpp
class router_socket_t : public routed_message_socket_t {
    explicit router_socket_t(context_t& ctx);

    // --- routed send ---
    /// @throws submit_error_t
    void send(const routing_id_t& target_rid, message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void send(const routing_id_t& target_rid, std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool try_send(const routing_id_t& target_rid, message_t& part);
    /// @throws submit_error_t
    bool try_send(const routing_id_t& target_rid, std::vector<message_t>& parts);

    // --- receive (unified routed surface) ---
    // 단일 recv 표면이다. 일반 ROUTER 트래픽과 spot-origin routed 트래픽을
    // 모두 이 하나로 받는다. `received_t::spot_rid()` 가 비어 있으면 일반
    // ROUTER 트래픽 (reply 는 reply_to peer), 값이 있으면 spot-origin
    // 트래픽 (reply 는 reply_to_spot 사용).
    /// @throws recv_error_t
    received_t recv(recv_flags_t flags = recv_flags_t::none);
    /// @throws recv_error_t
    std::optional<received_t> try_recv();
    /// @throws handler_error_t
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- identity / routing ---
    /// @throws config_error_t
    void set_routing_id(const routing_id_t& routing_id);
    /// @throws config_error_t
    void get_routing_id(routing_id_t& routing_id) const;

    // --- request (coroutine, blocking submit — no flags) ---
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request(const routing_id_t& peer_rid,
                                                   message_t& part,
                                                   std::chrono::milliseconds timeout = {});
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request(const routing_id_t& peer_rid,
                                                   std::vector<message_t>& parts,
                                                   std::chrono::milliseconds timeout = {});

    // --- request (callback, blocking submit; callback receives request_result_t) ---
    /// @throws submit_error_t
    void request(const routing_id_t& peer_rid,
                 message_t& part,
                 std::function<void(request_result_t, std::vector<message_t>)> callback,
                 std::chrono::milliseconds timeout = {});
    /// @throws submit_error_t
    void request(const routing_id_t& peer_rid,
                 std::vector<message_t>& parts,
                 std::function<void(request_result_t, std::vector<message_t>)> callback,
                 std::chrono::milliseconds timeout = {});
    /// @throws submit_error_t
    bool try_request(const routing_id_t& peer_rid,
                     message_t& part,
                     std::function<void(request_result_t, std::vector<message_t>)> callback,
                     std::chrono::milliseconds timeout = {});
    /// @throws submit_error_t
    bool try_request(const routing_id_t& peer_rid,
                     std::vector<message_t>& parts,
                     std::function<void(request_result_t, std::vector<message_t>)> callback,
                     std::chrono::milliseconds timeout = {});

    // --- reply ---
    /// @throws submit_error_t
    void reply(const routing_id_t& rid, uint64_t request_seq,
               message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void reply(const routing_id_t& rid, uint64_t request_seq,
               std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- request configuration ---
    /// @throws config_error_t
    void set_default_request_timeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds get_default_request_timeout() const;

    // --- router → spot routed send ---
    /// @throws submit_error_t
    void send_to_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                      message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void send_to_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                      std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- router → spot routed request (coroutine, blocking submit — no flags) ---
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request_to_spot(const routing_id_t& dest_node_rid,
                                                           const routing_id_t& dest_spot_rid,
                                                           message_t message,
                                                           std::chrono::milliseconds timeout = {});

    // --- router → spot routed request (callback; callback receives request_result_t) ---
    /// @throws submit_error_t
    void request_to_spot(const routing_id_t& dest_node_rid,
                         const routing_id_t& dest_spot_rid,
                         message_t message,
                         std::function<void(request_result_t, std::vector<message_t>)> callback,
                         send_flags_t flags = send_flags_t::none,
                         std::chrono::milliseconds timeout = {});

    // --- router → spot routed reply ---
    /// @throws submit_error_t
    void reply_to_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                       uint64_t request_seq, message_t message, send_flags_t flags = send_flags_t::none);

    // --- router-specific options ---
    /// @throws config_error_t
    router_socket_options_t router_options();

    // --- discovery ---
    /// @throws config_error_t
    void attach_discovery(DiscoveryT& discovery);
};
```

### xpub_socket_t

Extended publisher. Like pub_socket_t but also receives subscription events.

```cpp
class xpub_socket_t : public publisher_socket_t {
    explicit xpub_socket_t(context_t& ctx);

    // --- publish ---
    /// @throws submit_error_t
    void publish(const std::string& topic_id, message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void publish(const std::string& topic_id, std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);
    /// @throws handler_error_t
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- subscription events ---
    /// @throws recv_error_t
    subscription_event_t receive_subscription_event(recv_flags_t flags = recv_flags_t::none);

    // --- pub-specific options ---
    /// @throws config_error_t
    pub_socket_options_t pub_options();
};
```

### xsub_socket_t

Extended subscriber. Like sub_socket_t with raw subscription forwarding.

```cpp
class xsub_socket_t : public subscriber_socket_t {
    explicit xsub_socket_t(context_t& ctx);

    // --- subscription ---
    /// @throws config_error_t
    void set_subscription(const std::string& filter);
    /// @throws config_error_t
    void unset_subscription(const std::string& filter);
    /// @throws config_error_t
    void subscription_at(size_t index, std::string& filter, bool* is_pattern = NULL);

    // --- receive ---
    /// @throws recv_error_t
    topic_message_t subscribe(recv_flags_t flags = recv_flags_t::none);

    // --- sub-specific options ---
    /// @throws config_error_t
    sub_socket_options_t sub_options();
};
```

### stream_socket_t

Raw TCP stream socket. Bind-only; connect is deleted.

```cpp
class stream_socket_t : public routed_message_socket_t {
    explicit stream_socket_t(context_t& ctx);

    void connect(const std::string&) = delete;

    // --- routed send ---
    /// @throws submit_error_t
    void send(const routing_id_t& target_rid, message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void send(const routing_id_t& target_rid, std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool try_send(const routing_id_t& target_rid, message_t& part);
    /// @throws submit_error_t
    bool try_send(const routing_id_t& target_rid, std::vector<message_t>& parts);

    // --- receive (three mutually-exclusive modes) ---
    /// (1) raw recv. Returns a multipart received_t.
    /// @throws recv_error_t
    received_t recv(recv_flags_t flags = recv_flags_t::none);
    /// @throws recv_error_t
    std::optional<received_t> try_recv();
    /// (2) raw direct callback (zlink_recv_handler). Mutually exclusive
    /// with recv() and on_packet(). Second attach on the same stream returns
    /// BUSY (EBUSY).
    /// @throws handler_error_t
    void on_receive(zlink_socket_msg_handler_fn handler, void* userdata = NULL);
    /// (3) framed packet callback (zlink_stream_packet_handler). Wire frame
    /// is big-endian u16 header_size + u32 body_size + header + body.
    /// Handler receives the source routing_id, a header message_t, and a
    /// body message_t; ownership of both messages transfers to the callback.
    /// Mutually exclusive with recv() and on_receive() on the same stream;
    /// second attach returns BUSY.
    /// @throws handler_error_t
    void on_packet(zlink_stream_packet_handler_fn handler, void* userdata = NULL);
    /// @throws handler_error_t
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- identity ---
    /// @throws config_error_t
    void set_routing_id(const routing_id_t& routing_id);
    /// @throws config_error_t
    void get_routing_id(routing_id_t& routing_id) const;

    // --- stream-specific options ---
    /// @throws config_error_t
    stream_socket_options_t stream_options();
};
```

### Socket Option Facades

Raw `set_option(key, value)` / `get_option(key)` bags are not part of the
canonical public API. C++ exposes typed option facades instead.

```cpp
class common_socket_options_t {
    /// @throws config_error_t
    int linger() const;
    /// @throws config_error_t
    void linger(int value);
    /// @throws config_error_t
    int send_hwm() const;
    /// @throws config_error_t
    void send_hwm(int value);
    /// @throws config_error_t
    int recv_hwm() const;
    /// @throws config_error_t
    void recv_hwm(int value);
    /// @throws config_error_t
    int send_timeout() const;
    /// @throws config_error_t
    void send_timeout(int value);
    /// @throws config_error_t
    int recv_timeout() const;
    /// @throws config_error_t
    void recv_timeout(int value);
    /// @throws config_error_t
    bool immediate() const;
    /// @throws config_error_t
    void immediate(bool value);
    /// @throws config_error_t
    int connect_timeout() const;
    /// @throws config_error_t
    void connect_timeout(int value);
    /// @throws config_error_t
    bool ipv6() const;
    /// @throws config_error_t
    void ipv6(bool value);
    /// @throws config_error_t
    bool tcp_no_delay() const;
    /// @throws config_error_t
    void tcp_no_delay(bool value);
    /// @throws config_error_t
    bool tcp_keepalive() const;
    /// @throws config_error_t
    void tcp_keepalive(bool value);
    /// @throws config_error_t
    int heartbeat_interval() const;
    /// @throws config_error_t
    void heartbeat_interval(int value);
    /// @throws config_error_t
    int heartbeat_ttl() const;
    /// @throws config_error_t
    void heartbeat_ttl(int value);
    /// @throws config_error_t
    int heartbeat_timeout() const;
    /// @throws config_error_t
    void heartbeat_timeout(int value);
    /// @throws config_error_t
    int64_t max_message_size() const;
    /// @throws config_error_t
    void max_message_size(int64_t value);
    /// @throws config_error_t
    int backlog() const;
    /// @throws config_error_t
    void backlog(int value);
    /// @throws config_error_t
    int reconnect_interval() const;
    /// @throws config_error_t
    void reconnect_interval(int value);
    /// @throws config_error_t
    int reconnect_interval_max() const;
    /// @throws config_error_t
    void reconnect_interval_max(int value);
    /// @throws config_error_t
    std::string last_endpoint() const;
};

class pub_socket_options_t {
    /// @throws config_error_t
    bool verbose() const;
    /// @throws config_error_t
    void verbose(bool value);
    /// @throws config_error_t
    bool verboser() const;
    /// @throws config_error_t
    void verboser(bool value);
    /// @throws config_error_t
    bool no_drop() const;
    /// @throws config_error_t
    void no_drop(bool value);
    /// @throws config_error_t
    bool manual() const;
    /// @throws config_error_t
    void manual(bool value);
};

class sub_socket_options_t {
    /// @throws config_error_t
    int topics_count() const;
};

class dealer_socket_options_t {
    /// @throws config_error_t
    bool probe_router() const;
    /// @throws config_error_t
    void probe_router(bool value);
};

class router_socket_options_t {
    /// @throws config_error_t
    bool mandatory() const;
    /// @throws config_error_t
    void mandatory(bool value);
    /// @throws config_error_t
    bool handover() const;
    /// @throws config_error_t
    void handover(bool value);
    /// @throws config_error_t
    bool probe_router() const;
    /// @throws config_error_t
    void probe_router(bool value);
    /// @throws config_error_t
    std::optional<routing_id_t> connect_routing_id() const;
    /// @throws config_error_t
    void connect_routing_id(const routing_id_t& value);
};

class stream_socket_options_t {
    /// @throws config_error_t
    bool notify() const;
    /// @throws config_error_t
    void notify(bool value);
};
```

---

## Message / Domain Types

### message_t

RAII wrapper for a single message frame. Owns the payload buffer.

```cpp
class message_t {
    message_t();
    explicit message_t(size_t size);
    ~message_t();

    message_t(const message_t& other);
    message_t& operator=(const message_t& other);
    message_t(message_t&& other) noexcept;
    message_t& operator=(message_t&& other) noexcept;

    bool valid() const noexcept;

    // --- factories ---
    // copy-based factories
    static message_t from_bytes(const void* data, size_t size);
    static message_t from_bytes(const std::vector<uint8_t>& bytes);
    static message_t from_string(const std::string& text);
    // external attach with explicit release hook — C++ is the only binding
    // that exposes this generic public path.
    static message_t from_external(void* data, size_t size,
                                   zlink_free_fn* ffn = NULL, void* hint = NULL);

    // --- init ---
    void init();
    void init(size_t size);
    // external attach with explicit release hook.
    void init(void* data, size_t size, zlink_free_fn* ffn = NULL, void* hint = NULL);

    // --- accessors ---
    void* data() noexcept;
    const void* data() const noexcept;
    size_t size() const noexcept;
    int ref_count() const noexcept;
    const char* get_property(const std::string& property) const;
    void get_property(const std::string& property, std::string& out) const;

    // --- conversions ---
    std::vector<uint8_t> to_bytes() const;
    std::string to_string() const;

    // --- lifecycle ---
    void close() noexcept;
    zlink_msg_t* handle() noexcept;
    const zlink_msg_t* handle() const noexcept;
    void adopt(zlink_msg_t* src);
    void move_to(zlink_msg_t* dest);
    void copy_to(zlink_msg_t* dest) const;
};
```

### routing_id_t

Immutable binary-safe routing identity value object (1-255 bytes).
Routing ids are binary — the primary construction surface takes raw
bytes. `to_hex()` / `to_string()` are convenience views only.

```cpp
class routing_id_t {
    routing_id_t() noexcept;                                  // empty id
    routing_id_t(const uint8_t* bytes, size_t size);          // primary binary ctor
    routing_id_t(const zlink_routing_id_t& native);

    /// Named factories (equivalent to the primary byte constructor).
    static routing_id_t from_bytes(const uint8_t* bytes, size_t size);
    static routing_id_t from_bytes(const std::vector<uint8_t>& bytes);

    // --- binary accessors ---
    const uint8_t* data() const noexcept;
    size_t size() const noexcept;
    bool empty() const noexcept;
    std::vector<uint8_t> to_bytes() const;

    // --- convenience (non-canonical string views) ---
    std::string to_hex() const;        // lowercase hex, no separators
    std::string to_string() const;     // raw bytes as std::string (may contain non-UTF-8)

    // --- equality / hash ---
    friend bool operator==(const routing_id_t& a, const routing_id_t& b) noexcept;
    friend bool operator!=(const routing_id_t& a, const routing_id_t& b) noexcept;

    const zlink_routing_id_t& native() const noexcept;
    operator zlink_routing_id_t() const noexcept;
};

// std::hash specialization (enables use in std::unordered_{map,set}).
namespace std {
template<> struct hash<zlink::routing_id_t> {
    size_t operator()(const zlink::routing_id_t& rid) const noexcept;
};
} // namespace std
```

Rules:
- Binary-safe value type. A `routing_id_t(const std::string&)` primary
  constructor is **forbidden**; use `from_bytes(...)` or the byte
  pointer/size constructor.
- Immutable after construction.

### received_t

Aggregates one recv result used by PAIR / DEALER / ROUTER / STREAM /
Spot routed receive paths. Owns `message_t` parts; destructor releases
them. Matches the canonical `Received` shape (see
[Bindings Policy — 도메인 객체 Canonical Shape](../README.md#도메인-객체-canonical-shape-모든-바인딩-공통)).

```cpp
class received_t {
public:
    const std::optional<routing_id_t>& routing_id() const noexcept;  // peer_rid (Router) / source_node_rid (Spot); nullopt if transport carries no source id
    const std::optional<routing_id_t>& spot_rid() const noexcept;    // SPOT routed recv 에서만 값 있음
    const std::optional<uint64_t>& request_seq() const noexcept;     // set only for request-reply recv paths
    const std::vector<message_t>& parts() const noexcept;
    std::vector<message_t>& parts() noexcept;

    bool is_single_part() const noexcept;
    /// @throws recv_error_t
    message_t& first_part();
    /// @throws recv_error_t
    message_t single_part_or_throw();

    // reply — request_seq() 가 설정된 경우에만 유효. 아니면
    // invalid reply context 로 submit_error_t.
    // routing_id / spot_rid / request_seq 는 캡슐화됨 — caller 가 다시
    // 넘길 필요 없음. submit 실패 시 submit_error_t.
    /// @throws submit_error_t
    void reply(message_t& part);
    /// @throws submit_error_t
    void reply(message_t& part, send_flags_t flags);
    /// @throws submit_error_t
    void reply(std::vector<message_t>& parts);
    /// @throws submit_error_t
    void reply(std::vector<message_t>& parts, send_flags_t flags);

    /// @throws close_error_t
    void close();
};
```

`received_t` 는 내부적으로 source socket 참조를 보유한다. binding 이 recv /
handler 에서 `received_t` 를 만들 때 주입하며, `reply()` 는 그 참조를 사용해
원래 socket 으로 reply 한다.

### topic_message_t

Topic-aware recv result used by SUB / XSUB / Spot subscribe paths.
Owns `message_t` parts; destructor releases them.

```cpp
class topic_message_t {
public:
    topic_message_t(std::optional<routing_id_t> routing_id,
                    std::optional<std::string> service_name,
                    std::string topic,
                    std::vector<message_t> parts);

    const std::optional<routing_id_t>& routing_id() const noexcept;  // nullopt if transport carries no source id
    const std::optional<std::string>& service_name() const noexcept; // Spot subscribe only; empty for raw SUB / XSUB
    const std::string& topic() const noexcept;                       // UTF-8
    const std::vector<message_t>& parts() const noexcept;
    std::vector<message_t>& parts() noexcept;

    bool is_single_part() const noexcept;
    /// @throws recv_error_t
    message_t& first_part();
    /// @throws recv_error_t
    message_t single_part_or_throw();

    /// @throws close_error_t
    void close();
};
```

### subscription_event_t

Reports a subscribe/unsubscribe event from xpub sockets and Spot
subscription event recv. Plain value struct — no methods, no lifecycle.

```cpp
struct subscription_event_t {
    std::optional<routing_id_t> routing_id;  // nullopt if transport carries no subscriber id
    std::optional<std::string> service_name; // Spot subscription event only; empty for XPub
    std::string topic;                        // UTF-8
    bool subscribed;                          // true = subscribe, false = unsubscribe
};
```

### monitor_event_t

Socket monitor event payload. Value struct returned by
`monitor_handle_t::recv()`.

```cpp
struct monitor_event_t {
    monitor_event_type_t event;               // event kind (CONNECTED, DISCONNECTED, CONNECTION_READY, PEER_ADMISSION_CHANGED, ...)
    uint32_t value;                           // event-specific detail (e.g., DISCONNECTED reason code, PEER_ADMISSION_CHANGED -> admission_state_t)
    std::optional<routing_id_t> routing_id;   // peer routing id; nullopt when event carries none
    std::string local_addr;                   // local endpoint
    std::string remote_addr;                  // remote endpoint
};
```

`monitor_event_type_t` includes `peer_admission_changed` (bit 15). When this
event fires, `value` holds the new `admission_state_t` for the peer.

### monitor_snapshot_t

Runtime status snapshot returned by `monitor_handle_t::snapshot()` and
`service_monitor_handle_t::snapshot()`.

```cpp
struct monitor_snapshot_t {
    monitor_source_kind_t source_kind;        // monitor target kind
    uint32_t state_flags;                     // state bitmask
    uint32_t detail_flags;                    // detail bitmask
    uint64_t snd_pending_msgs;                // send-queue pending message count
    uint64_t rcv_pending_msgs;                // recv-queue pending message count

    bool is_ready() const noexcept;           // convenience: checks ready bit in state_flags
};
```

### service_event_t

Discovery service monitor event payload.
Returned by `service_monitor_handle_t::recv()`.

```cpp
struct service_event_t {
    service_kind_t service_kind;              // ZLINK_SERVICE_TYPE_SPOT, SOCKET, ...
    service_event_type_t event_type;          // UP, DOWN, PROVIDERS_CHANGED, ERROR, ...
    uint32_t status;                          // status code
    uint32_t error_code;                      // errno on error; 0 otherwise
    uint64_t value;                           // event-specific value
    uint32_t detail_flags;                    // detail bitmask
    std::string service_name;                 // service name
    std::string endpoint;                     // endpoint
    std::optional<routing_id_t> routing_id;   // peer routing id; nullopt when event carries none
    std::string subject;                      // subscribe subject (topic)
    service_event_subject_kind_t subject_kind; // subject kind
};
```

### Error and Result Types

#### submit_result_t

Maps to C API `zlink_submit_result_t`.

```cpp
enum class submit_result_t : int {
    ok               = 0,
    backpressured    = 1,
    not_connected    = 2,
    not_found        = 3,
    terminated       = 4,
    invalid_handle   = 5,
    invalid_argument = 6,
    not_supported    = 7,
    invalid_state    = 8,
    thread_violation = 9,
    out_of_memory    = 10,
    seq_exhausted    = 11,
    internal_error   = 12,
    not_admitted     = 13  // target peer is in admission_state_t::draining
};
```

#### request_result_t

Maps to C API `zlink_request_result_t`.
Values are offset to 101-104 to avoid collision with `submit_result_t` codes.

```cpp
enum class request_result_t : int {
    ok             = 0,
    timed_out      = 101,
    not_found      = 102,
    terminated     = 103,
    protocol_error = 104
};
```

#### recv_result_t

Maps to C API `zlink_recv_result_t`.
Covers recv, subscribe, and subscription event operations.

```cpp
enum class recv_result_t : int {
    ok             = 0,
    no_data        = 201,
    busy           = 202,
    terminated     = 203,
    invalid_handle = 204,
    not_supported  = 205
};
```

#### handler_result_t

Maps to C API `zlink_handler_result_t`.
Covers handler registration operations (`on_receive`, `on_send_ready`,
`on_packet`, `on_routed_receive`, `on_dispatch_event`, etc.).

```cpp
enum class handler_result_t : int {
    ok               = 0,
    invalid_argument = 301,
    busy             = 302,
    not_supported    = 303,
    deadlock         = 304,
    invalid_handle   = 305
};
```

#### close_result_t

Maps to C API `zlink_close_result_t`.
Covers close and destroy operations.

```cpp
enum class close_result_t : int {
    ok             = 0,
    busy           = 401,
    shutdown       = 402,
    invalid_handle = 403
};
```

#### bind_result_t

Maps to C API `zlink_bind_result_t`.
Covers bind operations.

```cpp
enum class bind_result_t : int {
    ok               = 0,
    invalid_argument = 501,
    addr_in_use      = 502,
    not_supported    = 503,
    invalid_handle   = 504
};
```

#### connect_result_t

Maps to C API `zlink_connect_result_t`.
Covers connect, disconnect, and unbind operations.

```cpp
enum class connect_result_t : int {
    ok               = 0,
    invalid_argument = 601,
    not_supported    = 602,
    invalid_handle   = 603
};
```

#### config_result_t

Maps to C API `zlink_config_result_t`.
Covers configuration, option, and snapshot operations.

```cpp
enum class config_result_t : int {
    ok               = 0,
    invalid_handle   = 701,
    invalid_argument = 702,
    not_supported    = 703
};
```

#### zlink_error_t

Abstract common parent of the C++ binding's exception hierarchy. Every
failing public method throws a concrete subclass that matches the
function category of the C API it wraps (see
[Per-Function Error Type Hierarchy](../README.md#per-function-error-type-hierarchy)).

The C API exposes **8 function-category result enums**
(`zlink_submit_result_t`, `zlink_request_result_t`,
`zlink_recv_result_t`, `zlink_handler_result_t`,
`zlink_close_result_t`, `zlink_bind_result_t`,
`zlink_connect_result_t`, `zlink_config_result_t`). The C++ binding
provides **8 matching subclasses** (`submit_error_t`, `request_error_t`,
`recv_error_t`, `handler_error_t`, `close_error_t`, `bind_error_t`,
`connect_error_t`, `config_error_t`), all of which derive from
`zlink_error_t`. Callers may catch the parent to catch any zlink failure
or a specific subclass to handle a single category.

Every public method documents the concrete subclass(es) it throws via
a Doxygen-style `/// @throws <subtype>` comment directly above the
method signature. Methods that may throw multiple categories use
`/// @throws zlink_error_t (e.g., <subtype_a>, <subtype_b>)`.

Language-native validation exceptions (e.g., `std::invalid_argument`)
are outside this hierarchy and are not enumerated by `@throws` notes.

```cpp
class zlink_error_t : public std::runtime_error {
public:
    explicit zlink_error_t(int code);
    zlink_error_t(int code, int internal_errno);

    /// Category-wide error code (one of the 8 result enum values,
    /// cast to `int`).
    int code() const noexcept;

    /// OS-level errno captured at the failure site (0 when not set).
    int internal_errno() const noexcept;

    const char* what() const noexcept override;

protected:
    // Abstract parent: only subclasses may be instantiated directly.
    zlink_error_t(const zlink_error_t&) = default;
};
```

##### submit_error_t

Wraps `submit_result_t`. Thrown by `send` / `publish` / `reply_to_*` /
callback-`request_to_*` submit failures.

```cpp
class submit_error_t : public zlink_error_t {
public:
    explicit submit_error_t(submit_result_t result);
    submit_error_t(submit_result_t result, int internal_errno);

    submit_result_t result() const noexcept;
};
```

##### request_error_t

Wraps `request_result_t`. Request completion callbacks receive
`request_result_t` directly (see Request Policy); this exception type is
used when a coroutine `request(...)` cannot complete successfully.

```cpp
class request_error_t : public zlink_error_t {
public:
    explicit request_error_t(request_result_t result);
    request_error_t(request_result_t result, int internal_errno);

    request_result_t result() const noexcept;
};
```

##### recv_error_t

Wraps `recv_result_t`. Thrown by `recv` / `subscribe` /
`receive_subscription_event` / monitor `recv` / timer `recv` failures.

```cpp
class recv_error_t : public zlink_error_t {
public:
    explicit recv_error_t(recv_result_t result);
    recv_error_t(recv_result_t result, int internal_errno);

    recv_result_t result() const noexcept;
};
```

##### handler_error_t

Wraps `handler_result_t`. Thrown by handler registration methods
(`on_receive`, `on_send_ready`, `on_packet`, `on_event`,
`on_routed_receive`, `on_dispatch_event`, `set_handler`).

```cpp
class handler_error_t : public zlink_error_t {
public:
    explicit handler_error_t(handler_result_t result);
    handler_error_t(handler_result_t result, int internal_errno);

    handler_result_t result() const noexcept;
};
```

##### close_error_t

Wraps `close_result_t`. Thrown by socket/service `close()` and
`destroy()` that invoke native close/destroy paths returning
`zlink_close_result_t`.

**`noexcept` carve-out**: RAII value types that own local resources only
(`message_t::close()`, `monitor_handle_t::close()`,
`service_monitor_handle_t::close()`, `poller_t::destroy()`) are marked
`noexcept` because their cleanup cannot fail in a way that the native C
API reports via `zlink_close_result_t`. These methods do not throw
`close_error_t`.

```cpp
class close_error_t : public zlink_error_t {
public:
    explicit close_error_t(close_result_t result);
    close_error_t(close_result_t result, int internal_errno);

    close_result_t result() const noexcept;
};
```

##### bind_error_t

Wraps `bind_result_t`. Thrown by `bind(...)`.

```cpp
class bind_error_t : public zlink_error_t {
public:
    explicit bind_error_t(bind_result_t result);
    bind_error_t(bind_result_t result, int internal_errno);

    bind_result_t result() const noexcept;
};
```

##### connect_error_t

Wraps `connect_result_t`. Thrown by `connect(...)`, `disconnect(...)`,
`unbind(...)`, `connect_peer(...)`, `disconnect_peer(...)`, and
`connect_registry(...)`.

```cpp
class connect_error_t : public zlink_error_t {
public:
    explicit connect_error_t(connect_result_t result);
    connect_error_t(connect_result_t result, int internal_errno);

    connect_result_t result() const noexcept;
};
```

##### config_error_t

Wraps `config_result_t`. Thrown by option setters/getters
(`set_option` / `get_option` / `set` / `get`), TLS configuration
(`set_tls_server` / `set_tls_client`), subscription-list manipulation
(`set_subscription` / `unset_subscription` / `subscription_at`),
identity setters/getters (`set_routing_id` / `get_routing_id`),
snapshot and query methods, poller mutations (`add` / `modify` /
`remove`), timer configuration (`start` / `stop`),
`attach_discovery(...)`, `set_default_request_timeout(...)`, and
similar configuration-surface operations.

```cpp
class config_error_t : public zlink_error_t {
public:
    explicit config_error_t(config_result_t result);
    config_error_t(config_result_t result, int internal_errno);

    config_result_t result() const noexcept;
};
```

#### send_flags_t

```cpp
enum class send_flags_t : int {
    none      = 0,
    dontwait  = 1
};
```

#### recv_flags_t

```cpp
enum class recv_flags_t : int {
    none      = 0,
    dontwait  = 1
};
```

### maybe_t\<T\>

Optional-like wrapper for non-blocking receive results.

```cpp
template<typename T>
class maybe_t {
    maybe_t();
    maybe_t(const T& value);
    maybe_t(T&& value);

    explicit operator bool() const noexcept;
    bool has_value() const noexcept;
    T& value() noexcept;
    const T& value() const noexcept;
    T& operator*() noexcept;
    T* operator->() noexcept;
};
```

### async_result_t\<T\>

Future wrapper for asynchronous request-reply results. Supports `co_await` in C++20.

```cpp
template<typename T>
class async_result_t {
    explicit async_result_t(std::future<T> future);

    bool valid() const;
    void wait() const;
    template<typename Rep, typename Period>
    std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout) const;
    T get();
};
```

---

## Monitoring

### monitor_handle_t

RAII wrapper for socket monitoring. Receives connect, disconnect, and handshake events.
Starts in recv model. `on_event(...)` transitions one-way to callback-only
model; after that `recv(...)` fails with busy and `snapshot()` still works.

```cpp
class monitor_handle_t {
    monitor_handle_t();
    ~monitor_handle_t();

    monitor_handle_t(monitor_handle_t&& other) noexcept;
    monitor_handle_t& operator=(monitor_handle_t&& other) noexcept;

    /// @throws config_error_t
    template<typename SocketLike>
    static monitor_handle_t open(const SocketLike& socket,
                                 monitor_event events = monitor_event::all);

    bool valid() const noexcept;
    void* handle() noexcept;
    const void* handle() const noexcept;

    /// @throws handler_error_t
    void on_event(monitor_event_handler_fn handler, void* userdata = NULL);
    /// @throws recv_error_t
    monitor_event_t recv();
    /// @throws recv_error_t
    maybe_t<monitor_event_t> recv(non_blocking_t);
    /// @throws config_error_t
    monitor_snapshot_t snapshot() const;
    void close() noexcept;

    // No-op callback for callback-only model. Pass to on_event() to keep a
    // valid handler symbol when the caller does not care about events; once
    // installed the monitor is in callback-only model and recv(...) fails
    // with busy (snapshot() still works). To drive the monitor through
    // snapshot() / recv(...) instead, leave on_event unset. Maps to
    // zlink_monitor_ignore_handler.
    static zlink_monitor_handler_fn ignore_handler;
};
```

### service_monitor_handle_t

RAII wrapper for service-level monitoring (discovery peer events, subject changes).
Starts in recv model. `on_event(...)` transitions one-way to callback-only
model; after that `recv(...)` fails with busy and `snapshot()` still works.

```cpp
class service_monitor_handle_t {
    service_monitor_handle_t();
    ~service_monitor_handle_t();

    service_monitor_handle_t(service_monitor_handle_t&& other) noexcept;
    service_monitor_handle_t& operator=(service_monitor_handle_t&& other) noexcept;

    bool valid() const noexcept;
    void* handle() noexcept;
    const void* handle() const noexcept;

    /// @throws handler_error_t
    void on_event(service_event_handler_fn handler, void* userdata = NULL);
    /// @throws recv_error_t
    service_event_t recv();
    /// @throws recv_error_t
    maybe_t<service_event_t> recv(non_blocking_t);
    /// @throws config_error_t
    monitor_snapshot_t snapshot() const;
    void close() noexcept;
};
```

---

## Services

### Service-Layer Entry Types

Service-layer snapshot and query methods return named C++ value types
rather than raw C structs. All entry types live in `zlink::service`
and expose typed fields (std::string for text, `routing_id_t` for
identities, and typed enums for categorical values).

#### Primary

#### member_peer_entry_t

Member peer entry returned by `registry_t::member_peers(...)` and
`discovery_t::member_peers(...)`.

```cpp
struct member_peer_entry_t {
    service_type_t service_type;
    service_role_t service_role;
    std::string service_name;
    std::string endpoint;
    std::optional<routing_id_t> routing_id;   // nullopt when peer carries no routing id
    int64_t value;
    admission_state_t admission_state;
};
```

#### registry_topology_entry_t

Topology entry returned by `registry_t::topology_snapshot(...)`,
`registry_t::topology_query(...)`, and
`registry_query_client_t::snapshot(...)`.

```cpp
struct registry_topology_entry_t {
    std::optional<routing_id_t> routing_id;
    service_kind_t service_kind;
    service_role_t service_role;
    std::string service_name;
    std::string endpoint;
    topology_source_t source;
    topology_state_t state;
    uint32_t desired_count;
    uint32_t ready_count;
    uint32_t error_code;
    uint64_t last_reported_ms;
};
```

#### spot_node_status_t

Status snapshot returned by `spot_node_t::status_snapshot()`.

```cpp
struct spot_node_status_t {
    std::string service_name;
    std::string local_endpoint;
    std::optional<routing_id_t> node_routing_id;
    spot_node_state_t state;
    uint32_t configured_peer_count;
    uint32_t active_peer_count;
    uint32_t connected_peer_count;
    uint32_t subject_count;
    uint32_t ready_subject_count;
    int32_t last_error;
    uint64_t last_changed_ms;
};
```

#### Advanced / Diagnostic

#### registry_service_summary_entry_t

Service summary entry returned by
`registry_t::service_summary_snapshot(...)`.

```cpp
struct registry_service_summary_entry_t {
    service_kind_t service_kind;
    service_role_t service_role;
    std::string service_name;
    uint32_t total_count;
    uint32_t connecting_count;
    uint32_t ready_count;
    uint32_t error_count;
    uint32_t stopped_count;
    uint64_t last_reported_ms;
};

struct registry_service_summary_filter_t {
    service_kind_t service_kind;
    service_role_t service_role;
    std::string service_name;
};
```

#### registry_status_t

Status snapshot returned by `registry_t::status_snapshot()`.

```cpp
struct registry_status_t {
    uint32_t registry_id;
    std::string bind_endpoint;
    registry_state_t state;
    uint32_t topology_entry_count;
    uint32_t peer_registry_count;
    uint32_t connected_peer_registry_count;
    uint64_t list_seq;
    int32_t last_error;
    uint64_t last_changed_ms;
};
```

#### spot_node_peer_entry_t

Peer entry returned by `spot_node_t::peers_snapshot(...)` and
`spot_node_t::peers_query(...)`.

```cpp
struct spot_node_peer_entry_t {
    std::string service_name;
    std::string local_endpoint;
    std::string peer_endpoint;
    spot_peer_source_t source;
    spot_peer_state_t state;
    admission_state_t admission_state;
    uint64_t connected_since_ms;
    uint64_t last_changed_ms;
};

struct spot_node_peer_filter_t {
    std::string peer_endpoint;
    spot_peer_source_t source;
    spot_peer_state_t state;
};
```

#### spot_node_subject_entry_t

Subject entry returned by `spot_node_t::subjects_snapshot(...)`.

```cpp
struct spot_node_subject_entry_t {
    spot_role_t role;
    std::string subject;
    subject_kind_t subject_kind;
    uint32_t ready_peer_count;
    uint32_t active_peer_count;
    uint64_t last_changed_ms;
};

enum class spot_service_attachment_role_t {
    router = 1,
    pub = 2,
    sub = 3,
};

enum class admission_state_t {
    serving  = 1,
    draining = 2,
};

struct spot_service_attachment_stats_t {
    std::string service_name;
    uint32_t router_count;
    uint32_t pub_count;
    uint32_t sub_count;
    uint32_t auto_router_count;
    uint32_t auto_pub_count;
    uint32_t auto_sub_count;
};

struct spot_service_monitor_event_t {
    std::string service_name;
    spot_service_attachment_role_t role;
    monitor_event_t event;
};

struct spot_node_subject_filter_t {
    spot_role_t role;
    std::string subject;
    subject_kind_t subject_kind;
};

struct registry_topology_filter_t {
    service_kind_t service_kind;
    service_role_t service_role;
    std::string service_name;
    std::optional<routing_id_t> routing_id;
    topology_state_t state;
    topology_source_t source;
};
```

### service::registry_t

Registry service node. Manages service topology and membership broadcast.

```cpp
namespace service {

class registry_t {
    explicit registry_t(context_t& ctx);
    ~registry_t();

    registry_t(registry_t&& other) noexcept;
    registry_t& operator=(registry_t&& other) noexcept;

    bool valid() const noexcept;
    void* handle() const;

    /// @throws bind_error_t
    void bind(const std::string& pub_endpoint, const std::string& router_endpoint);
    /// @throws config_error_t
    void set_id(uint32_t registry_id);
    /// @throws connect_error_t
    void add_peer(const std::string& peer_pub_endpoint);
    /// @throws config_error_t
    void set_heartbeat(uint32_t interval_ms, uint32_t timeout_ms);
    /// @throws config_error_t
    void set_broadcast_interval(uint32_t interval_ms);

    /// @throws config_error_t
    registry_status_t status_snapshot() const;
    /// @throws config_error_t
    std::vector<registry_service_summary_entry_t> service_summary_snapshot(
        const registry_service_summary_filter_t* filter = nullptr) const;
    /// @throws config_error_t
    std::vector<registry_topology_entry_t> topology_snapshot() const;
    /// @throws config_error_t
    std::vector<registry_topology_entry_t> topology_query(
        const registry_topology_filter_t& filter) const;
    /// @throws config_error_t
    std::vector<member_peer_entry_t> member_peers(service_type service_type,
                                                  const std::string& service_name) const;
    /// @throws config_error_t
    void member_peer_metadata(service_type service_type, const std::string& service_name,
                              service_role service_role, const std::string& endpoint,
                              message_t& metadata_out) const;

    /// @throws close_error_t
    void close();
};

} // namespace service
```

### service::discovery_t

Fixed-service discovery view. Tracks one service type/name pair and
provides metadata, member peer snapshots, and service monitor access.

```cpp
namespace service {

enum class discovery_dealer_peer_mode_t { router = 1, dealer = 2 };

class discovery_t {
    discovery_t(context_t& ctx, service_type service_type, const std::string& service_name);
    ~discovery_t();

    discovery_t(discovery_t&& other) noexcept;
    discovery_t& operator=(discovery_t&& other) noexcept;

    bool valid() const noexcept;
    void* handle() const;

    /// @throws connect_error_t
    void connect_registry(const std::string& endpoint);
    /// @throws config_error_t
    void set_value(int64_t value);
    /// @throws config_error_t
    void get_value(int64_t* value_out) const;
    /// @throws config_error_t
    void set_metadata(const void* data, size_t size);
    /// @throws config_error_t
    void set_metadata(const std::vector<uint8_t>& bytes);
    /// @throws config_error_t
    void set_metadata(const std::string& text);
    /// @throws config_error_t
    void get_metadata(message_t& metadata_out) const;
    /// @throws config_error_t
    std::vector<member_peer_entry_t> member_peers() const;
    /// @throws config_error_t
    void member_peer_metadata(service_role service_role, const std::string& endpoint,
                              message_t& metadata_out) const;

    /// @throws config_error_t
    service_monitor_handle_t monitor_open(service_monitor_event events = service_monitor_event::all);

    /// Resolve current owner node rid for a logical spot rid. Intended for
    /// send/request destination lookup. Maps to zlink_discovery_resolve_spot.
    /// @throws config_error_t
    routing_id_t resolve_spot(const routing_id_t& spot_rid);

    /// Set the auto-connect target policy for DEALER sockets in this
    /// discovery view. Default is router. Maps to zlink_discovery_set_dealer_peer_mode.
    /// @throws config_error_t
    void set_dealer_peer_mode(discovery_dealer_peer_mode_t mode);

    /// @throws close_error_t
    void close();
};

} // namespace service
```

### service::spot_node_t

Spot node lifecycle and topology facade. Manages peer connectivity and routing.

```cpp
namespace service {

class spot_node_t {
    explicit spot_node_t(context_t& ctx);
    ~spot_node_t();

    spot_node_t(spot_node_t&& other) noexcept;
    spot_node_t& operator=(spot_node_t&& other) noexcept;

    bool valid() const noexcept;
    void* handle() const;

    /// @throws bind_error_t
    void bind(const std::string& endpoint);
    /// @throws config_error_t
    std::string last_endpoint() const;
    /// @throws connect_error_t
    void connect_peer(const std::string& endpoint);
    /// @throws connect_error_t
    void disconnect_peer(const std::string& endpoint);
    /// @throws config_error_t
    void attach_discovery(discovery_t& discovery);

    // --- identity / routing ---
    /// Logical address / spot-level routed ownership key.
    /// Maps to zlink_set_routing_id(node, ...) / zlink_get_routing_id(node, ...).
    /// @throws config_error_t
    void set_routing_id(const routing_id_t& routing_id);
    /// @throws config_error_t
    void get_routing_id(routing_id_t& routing_id) const;

    /// @throws config_error_t
    void set_tls_server(const std::string& cert, const std::string& key,
                        bool require_client_cert = false);
    /// @throws config_error_t
    void set_tls_client(const std::string& ca_cert, const std::string& hostname = "",
                        bool trust_system = false);

    /// @throws config_error_t
    pub_socket_options_t publisher_options();
    /// @throws config_error_t
    sub_socket_options_t subscriber_options();

    // --- snapshots ---
    /// @throws config_error_t
    spot_node_status_t status_snapshot() const;
    /// @throws config_error_t
    std::vector<spot_node_peer_entry_t> peers_snapshot() const;
    /// @throws config_error_t
    std::vector<spot_node_peer_entry_t> peers_query(
        const spot_node_peer_filter_t& filter) const;
    /// @throws config_error_t
    std::vector<spot_node_subject_entry_t> subjects_snapshot(
        const spot_node_subject_filter_t* filter = nullptr) const;

    // --- factory: spot_t 생성은 반드시 spot_node_t 에서만 ---
    /// @throws config_error_t
    spot_t create_spot();

    // close() cascades: live spot_t 들을 먼저 정리한 후 node 종료
    /// @throws close_error_t
    void close();
};

} // namespace service
```

`spot_node_t` 가 lifecycle 소유자. `spot_t` 는 반드시
`spot_node_t::create_spot()` factory 로만 생성한다. 직접 `spot_t(node)`
호출은 internal (public 생성자 아님).

### service::spot_t

Spot messaging endpoint. Provides pub/sub, direct messaging, and subscription management.
**`spot_node_t::create_spot()` 로만 생성**.

```cpp
namespace service {

class spot_t {
    // explicit spot_t(spot_node_t&) 은 internal. 사용자 코드에서는
    // spot_node_t::create_spot() 을 사용한다.
    ~spot_t();

    spot_t(spot_t&& other) noexcept;
    spot_t& operator=(spot_t&& other) noexcept;

    bool valid() const noexcept;
    void* handle() const;

    // --- channel-aware publish / request ---
    /// @throws submit_error_t
    void publish(const std::string& service_name, const std::string& topic,
                 message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void publish(const std::string& service_name, const std::string& topic,
                 std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void send_channel(const std::string& channel_name,
                      std::vector<message_t>& parts,
                      send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void request_channel(const std::string& channel_name,
                         std::vector<message_t>& parts,
                         std::function<void(request_result_t, std::vector<message_t>)> callback,
                         send_flags_t flags = send_flags_t::none,
                         std::chrono::milliseconds timeout = {});

    // --- subscribe ---
    /// @throws recv_error_t
    topic_message_t subscribe(recv_flags_t flags = recv_flags_t::none);
    /// @throws recv_error_t
    subscription_event_t receive_subscription_event(recv_flags_t flags = recv_flags_t::none);
    /// @throws config_error_t
    void set_subscription(const std::string& filter);
    /// @throws config_error_t
    void unset_subscription(const std::string& filter);
    /// @throws config_error_t
    void subscription_at(size_t index, std::string& filter_out, bool* is_pattern_out = NULL) const;
    /// @throws handler_error_t
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- identity / routing ---
    /// Logical address / spot-level routed ownership key.
    /// Maps to zlink_set_routing_id(spot, ...) / zlink_get_routing_id(spot, ...).
    /// @throws config_error_t
    void set_routing_id(const routing_id_t& routing_id);
    /// @throws config_error_t
    void get_routing_id(routing_id_t& routing_id) const;

    // --- routed reply (spot → spot) ---
    /// @throws submit_error_t
    void reply_to_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                       uint64_t request_seq, message_t message, send_flags_t flags = send_flags_t::none);

    // --- routed send (spot → router) ---
    /// @throws submit_error_t
    void send_to_router(const routing_id_t& peer_rid, message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void send_to_router(const routing_id_t& peer_rid, std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- routed request (spot → router, coroutine, blocking submit — no flags) ---
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request_to_router(const routing_id_t& peer_rid,
                                                             message_t message,
                                                             std::chrono::milliseconds timeout = {});

    // --- routed request (spot → router, callback; callback receives request_result_t) ---
    /// @throws submit_error_t
    void request_to_router(const routing_id_t& peer_rid,
                           message_t message,
                           std::function<void(request_result_t, std::vector<message_t>)> callback,
                           send_flags_t flags = send_flags_t::none,
                           std::chrono::milliseconds timeout = {});

    // --- routed reply (spot → router) ---
    /// @throws submit_error_t
    void reply_to_router(const routing_id_t& peer_rid, uint64_t request_seq,
                         message_t message, send_flags_t flags = send_flags_t::none);

    // --- routed receive ---
    /// @throws recv_error_t
    received_t recv_routed(recv_flags_t flags = recv_flags_t::none);
    /// @throws handler_error_t
    void on_routed_receive(zlink_spot_handler_fn handler, void* userdata = NULL);
    /// @throws handler_error_t
    void on_dispatch_event(zlink_spot_dispatch_event_handler_fn handler,
                           void* userdata = NULL);

    // --- options ---
    /// @throws config_error_t
    common_socket_options_t options();
    /// @throws config_error_t
    pub_socket_options_t publisher_options();
    /// @throws config_error_t
    sub_socket_options_t subscriber_options();

    /// @throws close_error_t
    void close();
};

} // namespace service
```

### service::registry_query_client_t

Remote registry query client. Connects to a registry and fetches topology snapshots.

```cpp
namespace service {

class registry_query_client_t {
    explicit registry_query_client_t(context_t& ctx);
    ~registry_query_client_t();

    registry_query_client_t(registry_query_client_t&& other) noexcept;
    registry_query_client_t& operator=(registry_query_client_t&& other) noexcept;

    bool valid() const noexcept;
    void* handle() const;

    /// @throws connect_error_t
    void connect(const std::string& endpoint);
    /// @throws config_error_t
    std::vector<registry_topology_entry_t> snapshot(
        const registry_topology_filter_t* filter = nullptr) const;

    /// @throws close_error_t
    void close();
};

} // namespace service
```

---

## Poller

### poller_t

Event poller for multiplexing socket and file descriptor readiness.

```cpp
class poller_t {
    poller_t();
    ~poller_t();

    poller_t(poller_t&& other) noexcept;
    poller_t& operator=(poller_t&& other) noexcept;

    bool valid() const noexcept;
    void* handle() noexcept;
    const void* handle() const noexcept;
    /// @throws config_error_t
    int size() const noexcept;  // returns number of registered pollable items

    // --- socket registration ---
    /// @throws config_error_t
    template<typename SocketLike>
    void add(SocketLike& socket, poll_event events, void* user = NULL);
    /// @throws config_error_t
    template<typename SocketLike>
    void modify(SocketLike& socket, poll_event events);
    /// @throws config_error_t
    template<typename SocketLike>
    void remove(SocketLike& socket);

    // --- file descriptor registration ---
    /// @throws config_error_t
    void add(zlink_fd_t fd, poll_event events, void* user = NULL);
    /// @throws config_error_t
    void modify(zlink_fd_t fd, poll_event events);
    /// @throws config_error_t
    void remove(zlink_fd_t fd);

    // --- wait ---
    /// @throws recv_error_t
    int wait(poll_event_t* event, long timeout = -1);
    /// @throws recv_error_t
    int wait_all(std::vector<poll_event_t>& events, long timeout = -1);

    void destroy() noexcept;
};
```

---

## Timer

### timer_t

Interval timer with optional spot integration.

```cpp
class timer_t {
    timer_t();
    explicit timer_t(void* timer);
    ~timer_t();

    timer_t(timer_t&& other) noexcept;
    timer_t& operator=(timer_t&& other) noexcept;

    /// @throws config_error_t
    template<typename SpotLike>
    static timer_t from_spot(SpotLike& spot);

    bool valid() const noexcept;
    void* handle() noexcept;
    const void* handle() const noexcept;

    /// @throws config_error_t
    void start(uint64_t interval_ns, uint64_t repeat_count);
    /// @throws config_error_t
    void stop();
    /// @throws recv_error_t
    void recv(uint64_t* fire_count_out, int flags = 0);
    /// @throws handler_error_t
    void set_handler(zlink_timer_handler_fn handler, void* userdata = NULL);
    /// @throws close_error_t
    void destroy();
};
```

---

## Utilities

### stopwatch_t

Simple elapsed-time stopwatch.

```cpp
class stopwatch_t {
    stopwatch_t();
    ~stopwatch_t();

    stopwatch_t(stopwatch_t&& other) noexcept;
    stopwatch_t& operator=(stopwatch_t&& other) noexcept;

    unsigned long intermediate();
    unsigned long stop();
};
```

### thread_t

RAII wrapper for a background zlink thread.

```cpp
class thread_t {
    thread_t();
    explicit thread_t(zlink_thread_fn* fn, void* arg);
    ~thread_t();

    thread_t(thread_t&& other) noexcept;
    thread_t& operator=(thread_t&& other) noexcept;

    void close();
};
```

### atomic_counter_t

Lock-free atomic counter.

```cpp
class atomic_counter_t {
    atomic_counter_t();
    ~atomic_counter_t();

    atomic_counter_t(atomic_counter_t&& other) noexcept;
    atomic_counter_t& operator=(atomic_counter_t&& other) noexcept;

    void set(int value);
    int inc();
    int dec();
    int value() const;
    void destroy();
};
```

### Free Functions

```cpp
// Raw zlink_errno() is NOT public. Access internal errno through
// zlink::zlink_error_t::internal_errno() on the exception object.

/// Return a human-readable string for the given error number.
const char* zlink_strerror(int errnum);

/// Return the runtime library version.
void zlink_version(int& major, int& minor, int& patch);

/// Start a built-in proxy between frontend and backend sockets.
/// An optional capture socket receives copies of all messages.
/// @throws zlink_error_t
void proxy(void* frontend, void* backend, void* capture = NULL);

/// Start a steerable proxy with an additional control socket.
/// @throws zlink_error_t
void proxy_steerable(void* frontend, void* backend,
                     void* capture, void* control);

/// Check if the library supports a given capability (e.g. "ipc", "tls").
bool has(const std::string& capability);

/// Sleep for the given number of seconds.
void sleep(int seconds);

/// Close all parts in a multipart message array.
void multipart_close(zlink_msg_t* parts, size_t count);
```
