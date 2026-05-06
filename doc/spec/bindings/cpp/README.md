[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# C++ Binding Specification

This document defines the complete public API surface of the C++ binding.
Every class, its purpose, and all public method signatures are listed.
Internal helpers and implementation details are omitted.

This document is the source-of-truth contract for the C++ binding
implementation. If the installed C++ headers, tests, samples, or packaged
runtime behavior differ from this document, those artifacts must be updated to
match this document unless the contract itself is changed in this file first.

All types live in the `zlink` namespace. Service types live in `zlink::service`.

Only installed public headers are part of the contract. Private helper headers,
native bridge headers, and source-tree-only utilities are internal. Perf,
samples, and tests must include the public C++ headers only and must not rely
on non-installed internal headers.

## Design Basis

The C++ binding follows the repository POSD design policy. Public classes must
hide native sequencing, ownership, and option encoding behind typed, deep
interfaces so callers do not need core implementation details.

The public C++ surface must be a set of deep types, not a class-shaped mirror
of the C API. A public type is justified only when it owns a domain concept
such as context lifetime, socket capability, message ownership, routed receive
metadata, service membership, or typed options. Native handles, part-loop
sequencing, request tokens, callback userdata, and raw option encoding remain
inside the implementation.

Design review uses these POSD constraints:

- common send/recv, nonblocking, ownership, and error rules are implemented in
  shared internal owners instead of repeated in every socket class
- canonical result and facade methods do not require callers to pass values
  the object already captures, such as the source socket, request sequence, or
  service address
- compatibility headers and namespaces may preserve source compatibility, but
  new docs, samples, and tests use the canonical typed classes
- a wrapper that only forwards to a native call without adding validation,
  ownership, or shape semantics is internal, not a public module

## High-Performance Requirements

The C++ binding is part of a high-performance messaging library. Public APIs
and implementation paths must not put reflection-like dynamic dispatch,
unnecessary heap allocation, avoidable message copies, coarse lock contention,
hidden waits, sleeps, busy waits, or thread joins on send/recv, dispatch,
poller, timer, or request-completion hot paths. The implementation must build
multipart values directly from the core `*_part` substrate instead of first
creating native aggregate arrays and then copying them into C++ containers.

---

## Core Alignment Rules

The rules in this section are normative. When a later API listing conflicts
with these rules, the API listing must be corrected to match this section.

- `pair_socket_t`, `dealer_socket_t`, and `router_socket_t` do not expose
  direct data-plane receive callbacks. They keep their typed send/recv,
  request, and reply methods according to the socket capability matrix.
  `on_receive(...)` is not part of their public contract.
- `sub_socket_t` and `xsub_socket_t` are receive-only topic sockets and do not
  expose `on_subscribe(...)`.
- `stream_socket_t` keeps `recv(...)` and typed `on_receive(...)`, and must also
  expose a dedicated packet callback surface named `on_packet(...)`, mapped to
  `zlink_stream_packet_handler()`.
- `service::spot_node_t` must expose channel-aware attachment methods:
  `attach_channel_dealer(...)`,
  `attach_channel_dealer_manual(...)`,
  `attach_pub_ingress(...)`, and `attach_discovery(...)`.
- `service::spot_t` must expose channel-aware data-plane methods:
  `send_channel(...)`, `send_to_spot(...)`, `request_channel(...)`, and
  `publish(const std::string& service_name, const std::string& topic, ...)`.
- `service::spot_t::subscribe(...)` returns `service_topic_message_t`.
  Raw `SUB` / `XSUB` return `topic_message_t`. Service-aware fields are not
  optional on raw topic messages.
- `service::spot_t` must not expose `on_subscribe(...)`. SPOT topic readable
  notifications come from `on_dispatch_event(...)`, then callers drain with
  `subscribe(...)` / `recv_routed(...)` / timer recv.
- `SUBSCRIBE_READABLE` and `ROUTED_READABLE` are readiness notifications, not
  one-event-per-message delivery counters. Binding docs and samples must use
  drain-until-`EAGAIN` loops.
- `service::spot_t::on_routed_receive(...)` and
  `service::spot_t::on_dispatch_event(...)` are mutually exclusive on the
  routed axis.
- Peer weight is exposed only on `router_socket_t` and `dealer_socket_t`
  through typed option/property surfaces. The value range is `0..100`,
  default `100`; `0` drains new outbound selection. Submit attempts to a
  weight-`0` peer fail with
  `submit_error_t{.code = submit_result_t::not_admitted}`.
- `pollout` is a send-recovery readiness signal, shared with
  `on_send_ready(...)`. It is not a "transport writable" bit.
- ROUTER / PUB socket option defaults follow the core header:
  `mandatory = true`, `handover = false`, `nodrop = true`.
- SPOT admission HWM defaults follow the core header. Router and pubsub
  admission profile/numeric options are exposed; relay and delivery HWM stay
  `0` and are not public C++ options.
- Internal pairing rule: when auto-connect pairs two same-service ROUTERs via
  Discovery, the library picks one initiator per pair by total order on
  `(routing_id, advertise endpoint)`. Users do not configure this.

## Public Header Layout

The C++ binding exposes one umbrella header and class-oriented installed
headers. The header layout is part of the public contract because this binding
is header-only and users discover the public surface through installed headers.

- `zlink.hpp` is the umbrella header and includes the complete public surface.
- Core/domain headers expose shared value types and infrastructure:
  `zlink/context.hpp`, `zlink/message.hpp`, `zlink/multipart.hpp`,
  `zlink/types.hpp`, `zlink/error.hpp`, `zlink/monitor.hpp`,
  `zlink/poller.hpp`, `zlink/timers.hpp`, and utility headers.
- Concrete socket classes have class-oriented entrypoint headers:
  `zlink/sockets/pair.hpp`, `zlink/sockets/dealer.hpp`,
  `zlink/sockets/router.hpp`, `zlink/sockets/stream.hpp`,
  `zlink/sockets/pub.hpp`, `zlink/sockets/xpub.hpp`,
  `zlink/sockets/sub.hpp`, and `zlink/sockets/xsub.hpp`.
- Service classes have class-oriented entrypoint headers:
  `zlink/services/registry.hpp`, `zlink/services/discovery.hpp`,
  `zlink/services/query.hpp`, `zlink/services/spot_node.hpp`,
  `zlink/services/spot.hpp`, and `zlink/services/actor.hpp`.
- `zlink/services/actor.hpp` is the owning public header for Actor dispatch.
  It must contain the declarations and inline definitions for Actor value
  types and `service::actor_t` itself. A file that only includes
  `zlink/services/spot.hpp` or re-exports the SPOT service header is not a
  compliant Actor header.
- SPOT headers may include `zlink/services/actor.hpp` when they need Actor
  types, but the dependency direction must keep Actor discoverable as its own
  public surface instead of hiding it inside the SPOT header.
- Compatibility aggregate headers such as `zlink/socket_types.hpp` may remain
  installed, but they must not be the only discoverable public entrypoint for
  concrete public classes.

## Core Feature Mapping

This list maps every public feature group from `core/include/zlink.h` to the
canonical C++ binding surface that implementations must provide. If the current
binding implementation differs from this section, the implementation must be
updated to match this document.

- Error/version: `zlink_strerror(...)`, `zlink_version(...)`, and
  `zlink_error_t`-derived exceptions.
- Context lifecycle: `context_t`, `shutdown()`, and `term()`.
- Context options: `context_options_t`, including typed thread, duration,
  byte-size, and auto-HWM configuration.
- Context auto-HWM recalculation: `context_t` must expose an explicit
  recalculation method mapped to `zlink_ctx_auto_hwm_recalculate(...)`.
- Message lifecycle: owned `message_t`, `multipart_t`, advanced external
  message wrappers, and codec extension helpers.
- Routing identities and service addresses: `routing_id_t`, `spot_address_t`,
  `actor_ref_t`, and `service::spot_node_t::remote_actor_ref(...)`.
- Raw socket creation/lifecycle: typed socket classes only. Native socket
  handles are internal implementation details and are not part of the
  canonical public binding API.
- Common socket bind/connect/disconnect: `bind(...)`, `connect(...)`,
  `unbind(...)`, `disconnect(...)`, and `disconnect_rid(...)`.
- TLS configuration: `set_tls_server(...)` and `set_tls_client(...)`.
- Generic socket options: `common_socket_options_t`. Deprecated generic option
  key wrappers, if retained, live only under `compat::options`.
- Socket-specific options: `pub_socket_options_t`, `sub_socket_options_t`,
  `dealer_socket_options_t`, `router_socket_options_t`, and
  `stream_socket_options_t`. Raw integer policy values are not canonical.
- SPOT and SPOT-node native options: node mode is exposed through
  `spot_node_options_t`; SPOT request timeout and SPOT-node HWM admission
  options must have typed option surfaces.
- Socket channel names: generic channel-name get/set must be represented by
  typed socket or service APIs, and channel data-plane APIs must use explicit
  channel names.
- PAIR: `pair_socket_t::send(...)` and `pair_socket_t::recv(...)`.
- PUB/XPUB: `pub_socket_t::publish(...)`, `xpub_socket_t::publish(...)`,
  and `xpub_socket_t::receive_subscription_event(...)`.
- SUB/XSUB: `set_subscription(...)`, `unset_subscription(...)`,
  `subscription_at(...)`, and `subscribe(...)`.
- DEALER: `dealer_socket_t::send(...)`, `recv(...)`, and `request(...)`.
- ROUTER: `router_socket_t::send(...)`, `recv(...)`, `request(...)`,
  `reply(...)`, `send_to_spot(...)`, `request_to_spot(...)`, and
  `reply_to_spot(...)`.
- STREAM: `stream_socket_t::recv(...)`, `on_receive(...)`, `on_packet(...)`,
  and `stream_session_t` for per-session send and Actor binding. A
  `stream_session_t` captures both the session routing id and the owning
  `service::spot_node_t`.
- Send-ready callbacks and `pollout`: `on_send_ready(...)` on send-capable
  sockets and SPOT.
- Socket monitoring: `monitor_handle_t`, `monitor_event_t`, and
  `monitor_snapshot_t`.
- Registry service: `service::registry_t`.
- Discovery service: `service::discovery_t`, including `resolve_spot(...)`
  and `resolve_actor(...)`.
- SPOT node lifecycle and peer wiring: `service::spot_node_t`.
- SPOT publish/subscribe and channels: `service::spot_t::publish(...)`,
  `send_channel(...)`, `request_channel(...)`, and `subscribe(...)`.
- SPOT routed data plane: `send_to_spot(...)`, `request_to_spot(...)`,
  `request_to_router(...)`, `reply_to_spot(...)`, `reply_to_router(...)`,
  and `recv_routed(...)`. Spot destinations use `spot_address_t` instead of
  repeated node/spot routing-id pairs.
- SPOT dispatch events: `service::spot_t::on_dispatch_event(...)`.
  Dispatch info is exposed through C++ typed value objects, not raw native
  `zlink_spot_dispatch_info_t` pointers.
- SPOT channel-reply progress: channel request futures must progress replies
  internally, and the dispatch-event contract must identify channel reply
  readiness.
- Actor lifecycle and dispatch: `service::spot_node_t` Actor methods,
  `service::spot_t` Actor join methods, and `service::actor_t`.
- Registry and service snapshots: service entry structs and snapshot/query
  methods.
- Polling: `poller_t` is the canonical C++ mapping for the core polling
  contract, including socket, file-descriptor, and timer readiness.
- Proxy and capability query: `proxy(...)`, `proxy_steerable(...)`,
  and `has(...)`.
- Timers: `timer_t`, including `timer_t::from_spot(...)`.
- Utilities: `atomic_counter_t`, `stopwatch_t`, `thread_t`, and `sleep(...)`.

## Actor Dispatch Public Surface

C++ exposes Actor dispatch through installed public headers in namespace
`zlink::service`.

`zlink/services/actor.hpp` is the canonical include for the declarations below.
Including only `zlink/services/actor.hpp` must be enough for user code to name
the Actor value types and `service::actor_t`. Forwarding-only headers are
non-compliant because they make Actor look like an incidental part of SPOT
instead of a separate service-layer capability.

```cpp
struct actor_ref_t;
struct actor_create_result_t;
struct actor_route_t;
struct actor_recv_info_t;
struct actor_join_info_t;
struct actor_part_t;
class actor_t;
// Convenience form is service::spot_node_t::remote_actor_ref(...).
```

`spot_node_t` exposes Actor factory/lookup, remote create-or-get, admission,
join/leave, and Actor snapshots. `spot_t` exposes Actor join receive/reply and
joined Actor snapshots. `stream_socket_t` creates `stream_session_t` facades;
`stream_session_t` captures the owner node and exposes Actor bind/unbind and
bound Actor send for one STREAM session. Discovery exposes Actor route resolve.

`generation == 0` is an unchecked remote ref. One Actor can join only one Spot
at a time; one STREAM session can bind multiple Actors.

## Core

### context_t

RAII wrapper for a zlink context. Manages the lifecycle of IO threads and sockets.

```cpp
class context_t {
    context_t();
    explicit context_t(io_thread_count_t io_threads);
    ~context_t();

    context_t(context_t&& other) noexcept;
    context_t& operator=(context_t&& other) noexcept;

    bool valid() const noexcept;

    /// @throws close_error_t
    void shutdown();
    void term() noexcept;

    /// @throws config_error_t
    void recalculate_auto_hwm();

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
    io_thread_count_t io_threads() const;
    /// @throws config_error_t
    void io_threads(io_thread_count_t value);
    /// @throws config_error_t
    socket_count_t max_sockets() const;
    /// @throws config_error_t
    void max_sockets(socket_count_t value);
    /// @throws config_error_t
    byte_size_t max_msg_size() const;
    /// @throws config_error_t
    void max_msg_size(byte_size_t value);
    /// @throws config_error_t
    std::optional<thread_priority_t> thread_priority() const;
    /// @throws config_error_t
    void thread_priority(thread_priority_t value);
    /// @throws config_error_t
    thread_scheduling_policy_t thread_scheduling_policy() const;
    /// @throws config_error_t
    void thread_scheduling_policy(thread_scheduling_policy_t value);
    /// @throws config_error_t
    std::string thread_name_prefix() const;
    /// @throws config_error_t
    void thread_name_prefix(const std::string& value);
    /// @throws config_error_t
    spot_worker_count_t spot_worker_threads() const;
    /// @throws config_error_t
    void spot_worker_threads(spot_worker_count_t value);
    /// @throws config_error_t
    bool blocky() const;
    /// @throws config_error_t
    void blocky(bool enabled);
    /// @throws config_error_t
    bool auto_hwm_enabled() const;
    /// @throws config_error_t
    void auto_hwm_enabled(bool enabled);
    /// @throws config_error_t
    std::chrono::milliseconds auto_hwm_recalc_debounce() const;
    /// @throws config_error_t
    void auto_hwm_recalc_debounce(std::chrono::milliseconds value);
    /// @throws config_error_t
    auto_hwm_profile auto_hwm_profile_value() const;
    /// @throws config_error_t
    void auto_hwm_profile_value(auto_hwm_profile profile);
    /// @throws config_error_t
    socket_count_t socket_limit() const;
    /// @throws config_error_t
    byte_size_t msg_t_size() const;
    /// @throws config_error_t
    void add_thread_affinity(cpu_index_t cpu);
    /// @throws config_error_t
    void remove_thread_affinity(cpu_index_t cpu);
};
```

```cpp
enum class auto_hwm_profile : int {
    compact = ZLINK_AUTO_HWM_PROFILE_COMPACT,
    low_latency = ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY,
    balanced = ZLINK_AUTO_HWM_PROFILE_BALANCED,
    throughput = ZLINK_AUTO_HWM_PROFILE_THROUGHPUT
};

enum class thread_scheduling_policy_t : int {
    default_policy = -1,
    other,
    fifo,
    round_robin
};

enum class rid_duplicate_policy_t : int {
    reject,
    replace
};

class io_thread_count_t {
    static io_thread_count_t value(int value);
    int value() const noexcept;
};

class socket_count_t {
    static socket_count_t value(int value);
    int value() const noexcept;
};

class thread_priority_t {
    static thread_priority_t value(int value);
    int value() const noexcept;
};

class cpu_index_t {
    static cpu_index_t value(int value);
    int value() const noexcept;
};

class spot_worker_count_t {
    static spot_worker_count_t value(int value);
    int value() const noexcept;
};

class byte_size_t {
    static byte_size_t bytes(int64_t value);
    int64_t bytes() const noexcept;
};

class peer_weight_t {
    static peer_weight_t value(uint32_t value); // valid range: 0..100
    uint32_t value() const noexcept;
};

class message_count_t {
    static message_count_t value(int value);
    int value() const noexcept;
};

class socket_backlog_t {
    static socket_backlog_t value(int value);
    int value() const noexcept;
};
```

---

## Socket Types

The socket signatures below use shared domain types declared in
`zlink/types.hpp`, including `request_options_t`, `request_callback_t`,
`spot_address_t`, and `stream_session_t`.

### Common base methods

All socket types inherit from `base_socket_t` and expose these common operations.
Individual socket classes re-expose them as public.

Nonblocking data-plane helpers follow this rule:

- `send(...)` and `publish(...)` return `false` only for temporary
  backpressure when `send_flags_t::dontwait` is used.
- Blocking submit returns `true` on success. Route-not-ready and other submit
  failures still throw `submit_error_t`.
- `recv(...)` and `subscribe(...)` return `std::nullopt` when
  `recv_flags_t::dontwait` finds no message and still throw `recv_error_t`
  for real recv failures.

```cpp
// Available on all socket types
bool valid() const noexcept;
/// @throws bind_error_t
void bind(const std::string& endpoint);
/// @throws connect_error_t
void unbind(const std::string& endpoint);
/// @throws config_error_t
common_socket_options_t options();
// No common peer-weight accessor. Bindings expose weight only on
// router_socket_t and dealer_socket_t.
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
/// @throws connect_error_t
void disconnect_rid(const routing_id_t& peer_rid);
```

### pair_socket_t

Bidirectional exclusive pair socket. Sends and receives messages without routing.

```cpp
class pair_socket_t : public message_socket_t {
    explicit pair_socket_t(context_t& ctx);

    // --- send ---
    /// @throws submit_error_t
    bool send(message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool send(std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- receive ---
    /// @throws recv_error_t
    std::optional<received_t> recv(recv_flags_t flags = recv_flags_t::none);
    /// @throws handler_error_t
    template<class Handler>
    void on_send_ready(Handler&& handler);
};
```

### pub_socket_t

Publisher socket. Sends topic-prefixed messages to all matching subscribers.

```cpp
class pub_socket_t : public publisher_socket_t {
    explicit pub_socket_t(context_t& ctx);

    // --- publish ---
    /// @throws submit_error_t
    bool publish(const std::string& topic,
                 message_t& part,
                 send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool publish(const std::string& topic,
                 std::vector<message_t>& parts,
                 send_flags_t flags = send_flags_t::none);
    /// @throws handler_error_t
    template<class Handler>
    void on_send_ready(Handler&& handler);

    // --- pub-specific options ---
    /// @throws config_error_t
    pub_socket_options_t pub_options();

    // --- discovery ---
    /// @throws config_error_t
    void attach_discovery(service::discovery_t& discovery);
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
    subscription_filter_t subscription_at(size_t index);

    // --- receive ---
    /// @throws recv_error_t
    std::optional<topic_message_t> subscribe(recv_flags_t flags = recv_flags_t::none);
    // --- sub-specific options ---
    /// @throws config_error_t
    sub_socket_options_t sub_options();

    // --- discovery ---
    /// @throws config_error_t
    void attach_discovery(service::discovery_t& discovery);
};
```

### dealer_socket_t

Asynchronous client socket for fair-queued request distribution.

```cpp
class dealer_socket_t : public message_socket_t {
    explicit dealer_socket_t(context_t& ctx);

    // --- send ---
    /// @throws submit_error_t
    bool send(message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool send(std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- receive ---
    /// @throws recv_error_t
    std::optional<received_t> recv(recv_flags_t flags = recv_flags_t::none);
    /// @throws handler_error_t
    template<class Handler>
    void on_send_ready(Handler&& handler);

    // --- request (coroutine, blocking submit — no flags) ---
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request(message_t& part,
                                                   request_options_t options = {});
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request(std::vector<message_t>& parts,
                                                   request_options_t options = {});

    // --- request (callback submit; callback receives request_completion_t) ---
    /// @throws submit_error_t
    bool request(message_t& part,
                 request_callback_t callback,
                 request_options_t options = {});
    /// @throws submit_error_t
    bool request(std::vector<message_t>& parts,
                 request_callback_t callback,
                 request_options_t options = {});

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
    void attach_discovery(service::discovery_t& discovery);
};
```

### router_socket_t

Server socket that routes messages to specific peers by routing id.

```cpp
class router_socket_t : public routed_message_socket_t {
    explicit router_socket_t(context_t& ctx);

    // --- routed send ---
    /// @throws submit_error_t
    bool send(const routing_id_t& target_rid,
              message_t& part,
              send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool send(const routing_id_t& target_rid,
              std::vector<message_t>& parts,
              send_flags_t flags = send_flags_t::none);

    // --- receive (unified routed surface) ---
    // One recv surface covers regular ROUTER traffic and spot-origin routed
    // traffic. Empty routed_received_t::spot_rid() means regular ROUTER traffic.
    // A populated spot_rid() means reply_to_spot must be used.
    /// @throws recv_error_t
    std::optional<routed_received_t> recv(recv_flags_t flags = recv_flags_t::none);
    /// @throws handler_error_t
    template<class Handler>
    void on_send_ready(Handler&& handler);

    // --- identity / routing ---
    /// @throws config_error_t
    void set_routing_id(const routing_id_t& routing_id);
    /// @throws config_error_t
    void get_routing_id(routing_id_t& routing_id) const;

    // --- request (coroutine, blocking submit — no flags) ---
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request(const routing_id_t& peer_rid,
                                                   message_t& part,
                                                   request_options_t options = {});
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request(const routing_id_t& peer_rid,
                                                   std::vector<message_t>& parts,
                                                   request_options_t options = {});

    // --- request (callback submit; callback receives request_completion_t) ---
    /// @throws submit_error_t
    bool request(const routing_id_t& peer_rid,
                 message_t& part,
                 request_callback_t callback,
                 request_options_t options = {});
    /// @throws submit_error_t
    bool request(const routing_id_t& peer_rid,
                 std::vector<message_t>& parts,
                 request_callback_t callback,
                 request_options_t options = {});

    // --- reply ---
    /// @throws submit_error_t
    void reply(const routing_id_t& rid, request_sequence_t request,
               message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    void reply(const routing_id_t& rid, request_sequence_t request,
               std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- router → spot routed send ---
    /// @throws submit_error_t
    bool send_to_spot(const spot_address_t& dest,
                      message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool send_to_spot(const spot_address_t& dest,
                      std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- router → spot routed request (coroutine, blocking submit — no flags) ---
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request_to_spot(const spot_address_t& dest,
                                                           message_t message,
                                                           request_options_t options = {});

    // --- router → spot routed request (callback receives request_completion_t) ---
    /// @throws submit_error_t
    bool request_to_spot(const spot_address_t& dest,
                         message_t message,
                         request_callback_t callback,
                         request_options_t options = {});

    // --- router → spot routed reply ---
    /// @throws submit_error_t
    void reply_to_spot(const spot_address_t& dest,
                       request_sequence_t request,
                       message_t message,
                       send_flags_t flags = send_flags_t::none);

    // --- router-specific options ---
    /// @throws config_error_t
    router_socket_options_t router_options();

    // --- discovery ---
    /// @throws config_error_t
    void attach_discovery(service::discovery_t& discovery);
};
```

### xpub_socket_t

Extended publisher. Like pub_socket_t but also receives subscription events.

```cpp
class xpub_socket_t : public publisher_socket_t {
    explicit xpub_socket_t(context_t& ctx);

    // --- publish ---
    /// @throws submit_error_t
    bool publish(const std::string& topic,
                 message_t& part,
                 send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool publish(const std::string& topic,
                 std::vector<message_t>& parts,
                 send_flags_t flags = send_flags_t::none);
    /// @throws handler_error_t
    template<class Handler>
    void on_send_ready(Handler&& handler);

    // --- subscription events ---
    /// @throws recv_error_t
    std::optional<subscription_event_t> receive_subscription_event(
        recv_flags_t flags = recv_flags_t::none);

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
    subscription_filter_t subscription_at(size_t index);

    // --- receive ---
    /// @throws recv_error_t
    std::optional<topic_message_t> subscribe(recv_flags_t flags = recv_flags_t::none);

    // --- sub-specific options ---
    /// @throws config_error_t
    sub_socket_options_t sub_options();
};
```

### stream_socket_t

Raw TCP stream socket. Bind-only; it does not expose `connect` or
`disconnect`.

```cpp
class stream_socket_t : public routed_message_socket_t {
    explicit stream_socket_t(context_t& ctx);

    // --- routed send ---
    /// @throws submit_error_t
    bool send(const routing_id_t& target_rid,
              message_t& part,
              send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool send(const routing_id_t& target_rid,
              std::vector<message_t>& parts,
              send_flags_t flags = send_flags_t::none);

    // --- receive (three mutually-exclusive modes) ---
    /// (1) raw recv. Returns a multipart received_t.
    /// @throws recv_error_t
    std::optional<received_t> recv(recv_flags_t flags = recv_flags_t::none);
    /// (2) raw direct callback (zlink_recv_handler). Mutually exclusive
    /// with recv() and on_packet(). Second attach on the same stream returns
    /// BUSY (EBUSY).
    /// @throws handler_error_t
    template<class Handler>
    void on_receive(Handler&& handler);
    /// (3) framed packet callback (zlink_stream_packet_handler). Wire frame
    /// is big-endian u16 header_size + u32 body_size + header + body.
    /// Handler receives the source routing_id, a header message_t, and a
    /// body message_t; ownership of both messages transfers to the callback.
    /// Mutually exclusive with recv() and on_receive() on the same stream;
    /// second attach returns BUSY.
    /// @throws handler_error_t
    template<class Handler>
    void on_packet(Handler&& handler);
    /// @throws handler_error_t
    template<class Handler>
    void on_send_ready(Handler&& handler);

    // --- identity ---
    /// @throws config_error_t
    void set_routing_id(const routing_id_t& routing_id);
    /// @throws config_error_t
    void get_routing_id(routing_id_t& routing_id) const;

    // --- stream-specific options ---
    /// @throws config_error_t
    stream_socket_options_t stream_options();

    // --- per-session facade ---
    stream_session_t session(service::spot_node_t& owner_node,
                             const routing_id_t& session_rid);
};
```

### stream_session_t

Per-session facade for one STREAM client session routing id owned by one
`service::spot_node_t`. This type hides both `session_rid` and owner-node
plumbing from repeated calls and keeps STREAM session Actor binding as session
behavior instead of socket-wide behavior.

```cpp
class stream_session_t {
    const routing_id_t& routing_id() const noexcept;

    /// @throws submit_error_t
    bool send(message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool send(std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    /// @throws request_error_t
    void bind_actor(const actor_ref_t& actor, std::chrono::milliseconds timeout = {});
    /// @throws request_error_t
    void unbind_actor(const std::string& actor_id, std::chrono::milliseconds timeout = {});
    /// @throws submit_error_t
    bool send_bound_actor_part(const std::string& actor_id, message_t& part,
                               send_flags_t flags = send_flags_t::none);
};
```

### Socket Option Facades

Raw `set_option(key, value)` / `get_option(key)` bags are not part of the
canonical public API. C++ exposes typed option facades instead.

```cpp
class common_socket_options_t {
    /// @throws config_error_t
    std::chrono::milliseconds linger() const;
    /// @throws config_error_t
    void linger(std::chrono::milliseconds value);
    /// @throws config_error_t
    message_count_t send_hwm() const;
    /// @throws config_error_t
    void send_hwm(message_count_t value);
    /// @throws config_error_t
    message_count_t recv_hwm() const;
    /// @throws config_error_t
    void recv_hwm(message_count_t value);
    /// @throws config_error_t
    std::chrono::milliseconds send_timeout() const;
    /// @throws config_error_t
    void send_timeout(std::chrono::milliseconds value);
    /// @throws config_error_t
    std::chrono::milliseconds recv_timeout() const;
    /// @throws config_error_t
    void recv_timeout(std::chrono::milliseconds value);
    /// @throws config_error_t
    bool immediate() const;
    /// @throws config_error_t
    void immediate(bool value);
    /// @throws config_error_t
    std::chrono::milliseconds connect_timeout() const;
    /// @throws config_error_t
    void connect_timeout(std::chrono::milliseconds value);
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
    std::chrono::milliseconds heartbeat_interval() const;
    /// @throws config_error_t
    void heartbeat_interval(std::chrono::milliseconds value);
    /// @throws config_error_t
    std::chrono::milliseconds heartbeat_ttl() const;
    /// @throws config_error_t
    void heartbeat_ttl(std::chrono::milliseconds value);
    /// @throws config_error_t
    std::chrono::milliseconds heartbeat_timeout() const;
    /// @throws config_error_t
    void heartbeat_timeout(std::chrono::milliseconds value);
    /// @throws config_error_t
    rid_duplicate_policy_t rid_duplicate_policy() const;
    /// @throws config_error_t
    void rid_duplicate_policy(rid_duplicate_policy_t value);
    /// @throws config_error_t
    byte_size_t max_message_size() const;
    /// @throws config_error_t
    void max_message_size(byte_size_t value);
    /// @throws config_error_t
    socket_backlog_t backlog() const;
    /// @throws config_error_t
    void backlog(socket_backlog_t value);
    /// @throws config_error_t
    std::chrono::milliseconds reconnect_interval() const;
    /// @throws config_error_t
    void reconnect_interval(std::chrono::milliseconds value);
    /// @throws config_error_t
    std::chrono::milliseconds reconnect_interval_max() const;
    /// @throws config_error_t
    void reconnect_interval_max(std::chrono::milliseconds value);
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
    /// @throws config_error_t
    peer_weight_t peer_weight() const;
    /// @throws config_error_t
    void peer_weight(peer_weight_t value);
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
    /// @throws config_error_t
    peer_weight_t peer_weight() const;
    /// @throws config_error_t
    void peer_weight(peer_weight_t value);
};

class stream_socket_options_t {
    /// @throws config_error_t
    bool notify() const;
    /// @throws config_error_t
    void notify(bool value);
};
```

Compatibility headers may retain low-level option enums and key wrappers for
existing generic option code, but only under a clearly separated compatibility
namespace. They are not canonical public API, and samples, tests, and new
application code must use the typed facade classes above.

```cpp
namespace compat::options {
enum class socket_option : int;
enum class router_option : int;
enum class dealer_option : int;
enum class pub_option : int;
enum class sub_option : int;
enum class stream_option : int;

template<typename T> struct socket_option_key_t;
template<typename T> struct router_option_key_t;
template<typename T> struct dealer_option_key_t;
template<typename T> struct pub_option_key_t;
template<typename T> struct sub_option_key_t;
template<typename T> struct stream_option_key_t;
}
```

The compatibility option namespace must not be re-exported as the default
option surface.

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
    static message_t from_bytes(std::span<const std::byte> bytes);
    static message_t from_bytes(const std::vector<uint8_t>& bytes);
    static message_t from_string(const std::string& text);
    // --- init ---
    void init();
    void init(size_t size);

    // --- accessors ---
    std::span<std::byte> bytes() noexcept;
    std::span<const std::byte> bytes() const noexcept;
    // Compatibility views for existing code live in compat::message.
    size_t size() const noexcept;
    int ref_count() const noexcept;
    std::optional<std::string> property(const std::string& name) const;

    // --- conversions ---
    std::vector<uint8_t> to_bytes() const;
    std::string to_string() const;

    // --- lifecycle ---
    void close() noexcept;
};
```

### advanced::external_message_t

External-buffer messages are an advanced C++-only escape hatch. They are kept
outside the canonical `message_t` factory surface because callers must
understand buffer lifetime and release callbacks. Normal application code,
samples, tests, and codec helpers use owned `message_t` construction.

```cpp
namespace zlink::advanced {

class external_message_t {
    static message_t adopt(void* data, size_t size,
                           zlink_free_fn* release, void* hint = nullptr);
};

} // namespace zlink::advanced
```

### multipart_t

Move-only RAII owner for an ordered multipart message sequence. The public
C++ API exposes `message_t` parts only; it does not expose the native
`zlink_msg_t` array representation used by the C layer.

```cpp
class multipart_t {
    multipart_t();
    ~multipart_t();

    multipart_t(multipart_t&& other) noexcept;
    multipart_t& operator=(multipart_t&& other) noexcept;

    multipart_t(const multipart_t&) = delete;
    multipart_t& operator=(const multipart_t&) = delete;

    void reset();
    message_t* data();
    const message_t* data() const;
    size_t size() const;

    message_t& operator[](size_t idx);
    const message_t& operator[](size_t idx) const;

    void adopt(std::vector<message_t>&& parts);
};
```

### Codec Extensions

The binding exposes separate codec extension libraries. The distribution
library names and public header names are fixed to:

- `zlink-codec-protobuf`
- `zlink-codec-json`
- `zlink-codec-messagepack`

- `<zlink/codec/protobuf.hpp>`
- `<zlink/codec/proto.hpp>` (protobuf compatibility header)
- `<zlink/codec/json.hpp>`
- `<zlink/codec/messagepack.hpp>`

These are separate public headers layered on top of the core `<zlink/...>`
surface. They must not force codec dependencies on users who only include the
core binding headers.

JSON codec baseline: `nlohmann/json`.
MessagePack codec baseline: `msgpack-c`.

```cpp
namespace zlink::codec::proto {

template<class T>
T decode(const message_t& message);

template<class T>
message_t encode(const T& value);

template<class T>
T parse(const message_t& message);

template<class T>
message_t to_message(const T& value);

} // namespace zlink::codec::proto

namespace zlink::codec {
namespace protobuf = proto; // compatibility namespace alias
}

namespace zlink::codec::json {

template<class T>
T decode(const message_t& message);

template<class T>
message_t encode(const T& value);

template<class T>
T parse(const message_t& message);

template<class T>
message_t to_message(const T& value);

} // namespace zlink::codec::json

namespace zlink::codec::messagepack {

template<class T>
T decode(const message_t& message);

template<class T>
message_t encode(const T& value);

template<class T>
T parse(const message_t& message);

template<class T>
message_t to_message(const T& value);

} // namespace zlink::codec::messagepack
```

### routing_id_t

Immutable binary-safe routing identity value object (1-255 bytes).
Routing ids are binary — the primary construction surface takes raw
bytes. String conversion is a hex display/parse path, not a raw byte string
transport.

```cpp
class routing_id_t {
    routing_id_t() = delete;
    routing_id_t(const uint8_t* bytes, size_t size);          // primary binary ctor

    /// Named factories (equivalent to the primary byte constructor).
    static routing_id_t from_bytes(const uint8_t* bytes, size_t size);
    static routing_id_t from_bytes(const std::vector<uint8_t>& bytes);
    // Parses hex; invalid or >255 decoded bytes throws.
    static routing_id_t from_string(const std::string& value);

    // --- binary accessors ---
    const uint8_t* data() const noexcept;
    size_t size() const noexcept;
    std::vector<uint8_t> to_bytes() const;

    // --- convenience string views ---
    std::string to_hex() const;        // lowercase hex, no separators
    std::string to_string() const;     // same hex display value as to_hex()

    // --- equality / hash ---
    friend bool operator==(const routing_id_t& a, const routing_id_t& b) noexcept;
    friend bool operator!=(const routing_id_t& a, const routing_id_t& b) noexcept;
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
  pointer/size constructor. `from_string(...)` only parses the hex
  representation returned by `to_hex()`. Hex input longer than 510 chars,
  or input that decodes above 255 bytes, throws `std::invalid_argument`.
- Public `routing_id_t` values are always 1-255 bytes. Optional routing
  metadata uses `std::optional<routing_id_t>` instead of an empty routing id.
- Immutable after construction.

### spot_address_t

Logical address for a remote Spot. This value object prevents repeated
destination identity fields from leaking through routed Spot APIs.

```cpp
class spot_address_t {
    spot_address_t(routing_id_t node_rid, routing_id_t spot_rid);

    const routing_id_t& node_rid() const noexcept;
    const routing_id_t& spot_rid() const noexcept;
};

class local_spot_address_t {
    explicit local_spot_address_t(routing_id_t spot_rid);

    const routing_id_t& spot_rid() const noexcept;
};
```

### request_options_t / request_completion_t

Shared request-reply support types. They keep timeout and completion semantics
in one place instead of repeating callback signatures on every socket and
service facade.

```cpp
struct request_options_t {
    std::chrono::milliseconds timeout{};
    send_flags_t flags = send_flags_t::none;
};

class request_sequence_t {
    static request_sequence_t value(uint64_t value);
    uint64_t value() const noexcept;
};

class request_completion_t {
    request_result_t result() const noexcept;
    const std::vector<message_t>& parts() const noexcept;
    std::vector<message_t>& parts() noexcept;
    bool ok() const noexcept;
};

class request_callback_t {
    template<class Fn>
    request_callback_t(Fn&& fn);
    void operator()(request_completion_t completion);
};
```

### fd_handle_t

File descriptor value object for poller registration. It prevents OS handle
ownership and integer validity rules from leaking into `poller_t`.

```cpp
class fd_handle_t {
    explicit fd_handle_t(zlink_fd_t fd);

    zlink_fd_t native() const noexcept;
    bool valid() const noexcept;
};
```

### Actor Domain Types

Actor value types mirror the C structs used by SPOT Actor dispatch.

```cpp
class routed_received_t;
class service_topic_message_t;

enum class actor_create_status_t : int {
    created = ZLINK_ACTOR_CREATE_CREATED,
    existing = ZLINK_ACTOR_CREATE_EXISTING
};

enum class actor_admission_result_t : int {
    accept = ZLINK_ACTOR_ADMISSION_ACCEPT,
    reject = ZLINK_ACTOR_ADMISSION_REJECT
};

class actor_ref_t {
    actor_ref_t() noexcept;

    routing_id_t node_rid() const;
    std::string actor_id_string() const;
    uint64_t generation() const noexcept;
    bool unchecked() const noexcept;
};

struct actor_recv_info_t {
    actor_ref_t actor;
    routing_id_t source_node_rid;
    routing_id_t source_session_rid;
    uint32_t flags;
};

struct actor_join_info_t {
    actor_ref_t source_actor;
    actor_ref_t target_actor;
    routing_id_t source_node_rid;
    routing_id_t source_spot_rid;
    routing_id_t target_node_rid;
    routing_id_t target_spot_rid;
    uint64_t join_epoch;
    uint32_t flags;
};

struct actor_create_result_t {
    actor_create_status_t status;
    actor_ref_t actor;
};

struct actor_route_t {
    actor_ref_t actor;
    bool joined;
    std::optional<routing_id_t> joined_spot_rid;
};

struct actor_part_t {
    actor_recv_info_t info;
    message_t part;
    bool has_more;
};

enum class spot_dispatch_event_t : int {
    subscribe_readable = ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE,
    routed_readable = ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE,
    timer_readable = ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE,
    channel_reply_readable = ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE,
    actor_readable = ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE,
    actor_join_readable = ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE
};

enum class spot_dispatch_subject_kind_t : int {
    spot = ZLINK_SPOT_DISPATCH_SUBJECT_SPOT,
    timer = ZLINK_SPOT_DISPATCH_SUBJECT_TIMER,
    channel_dealer = ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER,
    actor = ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR
};

struct spot_subscribe_ready_t {
    std::vector<service_topic_message_t> drain();
};

struct spot_routed_ready_t {
    std::vector<routed_received_t> drain();
};

struct spot_timer_ready_t {
    std::vector<uint64_t> drain();
};

struct spot_channel_reply_ready_t {
    void progress();
};

struct spot_actor_ready_t {
    actor_ref_t actor;
    std::vector<actor_part_t> drain();
};

struct spot_actor_join_ready_t {
    std::vector<std::pair<actor_join_info_t, message_t>> drain();
};

using spot_dispatch_info_t = std::variant<
    spot_subscribe_ready_t,
    spot_routed_ready_t,
    spot_timer_ready_t,
    spot_channel_reply_ready_t,
    spot_actor_ready_t,
    spot_actor_join_ready_t>;
```

`spot_dispatch_info_t` never exposes the native `void* subject` pointer or
requires callers to switch on raw subject kind. Each variant owns the minimal
drain/progress helper for that readiness source. If the binding moves the
callback to another execution context, it pre-drains nonblocking where needed
and stores the public values in the variant.

### received_t / routed_received_t

`received_t` aggregates one non-replyable recv result used by PAIR / DEALER /
STREAM paths. `routed_received_t` is the replyable routed variant used by
ROUTER and Spot routed receive paths. Splitting the types prevents callers
from calling `reply(...)` on a message that has no reply context. Both types
own `message_t` parts; destructors release them. They match the canonical
`Received` shape (see
[Bindings Policy - Domain Object Policy](../README.md)).

```cpp
class received_t {
public:
    // nullopt if transport carries no source id
    const std::optional<routing_id_t>& routing_id() const noexcept;
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

class routed_received_t {
public:
    // peer_rid for Router, source_node_rid for Spot
    const routing_id_t& routing_id() const noexcept;
    // Set only for SPOT-origin routed recv.
    const std::optional<routing_id_t>& spot_rid() const noexcept;
    const std::vector<message_t>& parts() const noexcept;
    std::vector<message_t>& parts() noexcept;

    bool is_single_part() const noexcept;
    /// @throws recv_error_t
    message_t& first_part();
    /// @throws recv_error_t
    message_t single_part_or_throw();

    // Routing metadata and request sequence are encapsulated. Callers do not
    // pass routing_id, spot_rid, or request_sequence_t again.
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

`routed_received_t` stores the source socket reference internally. The binding
injects that reference when it creates `routed_received_t` from `recv` or a
handler, and `reply()` uses it to reply on the original socket.

### topic_message_t / service_topic_message_t

Topic-aware recv result used by raw SUB / XSUB paths. `service_topic_message_t`
is the service-aware Spot subscribe variant. Both own `message_t` parts;
destructors release them.

```cpp
class topic_message_t {
public:
    topic_message_t(std::optional<routing_id_t> routing_id,
                    std::string topic,
                    std::vector<message_t> parts);

    // nullopt if transport carries no source id
    const std::optional<routing_id_t>& routing_id() const noexcept;
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

class service_topic_message_t {
public:
    service_topic_message_t(std::optional<routing_id_t> routing_id,
                            std::string service_name,
                            std::string topic,
                            std::vector<message_t> parts);

    const std::optional<routing_id_t>& routing_id() const noexcept;
    const std::string& service_name() const noexcept;
    const std::string& topic() const noexcept;
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

### subscription_event_t / service_subscription_event_t

Reports a subscribe/unsubscribe event from xpub sockets and Spot
subscription event recv. Plain value structs — no methods, no lifecycle.

```cpp
struct subscription_filter_t {
    std::string filter;
    bool is_pattern;
};

struct subscription_event_t {
    std::optional<routing_id_t> routing_id;  // nullopt if transport carries no subscriber id
    std::string topic;                        // UTF-8
    bool subscribed;                          // true = subscribe, false = unsubscribe
};

struct service_subscription_event_t {
    std::optional<routing_id_t> routing_id;
    std::string service_name;
    std::string topic;
    bool subscribed;
};
```

### monitor_event_t

Socket monitor event payload returned by `monitor_handle_t::recv()`.
Event-specific payload is represented as typed variants instead of a shared
integer detail field.

```cpp
enum class disconnect_reason : int {
    unknown,
    handshake_failed,
    transport_error,
    ctx_term
};

struct monitor_endpoint_t {
    std::optional<routing_id_t> routing_id;
    std::string local_addr;
    std::string remote_addr;
};

struct monitor_connected_t {
    monitor_endpoint_t endpoint;
};

struct monitor_disconnected_t {
    monitor_endpoint_t endpoint;
    disconnect_reason reason;
};

struct monitor_connection_ready_t {
    monitor_endpoint_t endpoint;
};

struct monitor_peer_weight_changed_t {
    monitor_endpoint_t endpoint;
    peer_weight_t weight;
};

using monitor_event_t = std::variant<
    monitor_connected_t,
    monitor_disconnected_t,
    monitor_connection_ready_t,
    monitor_peer_weight_changed_t>;
```

### monitor_snapshot_t

Runtime status snapshot returned by `monitor_handle_t::snapshot()`.

```cpp
enum class monitor_source_kind : int {
    socket,
    spot_pub,
    spot_sub
};

enum class monitor_state : uint32_t {
    ready,
    bound_ready,
    send_ready,
    closed
};

enum class monitor_snapshot_detail : uint32_t {
    snd_pending_msgs,
    rcv_pending_msgs,
    auto_hwm_budget,
    auto_hwm_buffers
};

struct monitor_snapshot_t {
    monitor_source_kind_t source_kind;        // monitor target kind
    std::set<monitor_state> states;
    std::set<monitor_snapshot_detail> details;
    uint64_t snd_pending_msgs;                // send-queue pending message count
    uint64_t rcv_pending_msgs;                // recv-queue pending message count
    bool auto_hwm_enabled;                    // automatic HWM currently active
    auto_hwm_profile auto_hwm_profile_value;  // selected auto-HWM profile
    uint32_t auto_hwm_role;                   // diagnostic role bucket
    uint32_t auto_hwm_policy_class;           // planner policy class
    byte_size_t auto_hwm_unit_budget;
    uint32_t auto_hwm_size_cap;
    uint64_t auto_hwm_socket_message_slots;
    byte_size_t auto_hwm_effective_message_size;
    int32_t auto_hwm_applied_sndhwm;          // applied send HWM
    int32_t auto_hwm_applied_rcvhwm;          // applied recv HWM
    byte_size_t auto_hwm_effective_sndbuf;    // applied send buffer size
    byte_size_t auto_hwm_effective_rcvbuf;    // applied recv buffer size
    std::chrono::milliseconds auto_hwm_last_recalc_age;
    uint32_t auto_hwm_last_recalc_reason;
    uint32_t auto_hwm_send_blocked_ratio_ppm;
    int32_t auto_hwm_deferred_sndhwm;
    int32_t auto_hwm_deferred_rcvhwm;

    bool is_ready() const noexcept;
};
```

### Compatibility Appendix: Enums and Aliases

The installed public headers may expose the low-level enums and aliases below
only for source compatibility. They are not the canonical C++ API surface.
New docs, samples, tests, and application code must use the typed classes above
instead of these names.

Deprecated no-op context options are compatibility-only names. They must
not be added back as canonical typed `context_options_t` getters or setters.

#### Core compatibility enums

```cpp
enum class socket_type : int {
    pair, dealer, router, stream, pub, xpub, sub, xsub
};

enum class context_option : int {
    io_threads,
    max_sockets,
    socket_limit,
    thread_priority,
    thread_sched_policy,
    max_msgsz,
    msg_t_size,
    thread_affinity_cpu_add,
    thread_affinity_cpu_remove,
    thread_name_prefix,
    blocky,
    spot_worker_threads,
    auto_hwm_enable,
    auto_hwm_total_memory_budget_mb, // deprecated no-op compatibility option
    auto_hwm_recalc_debounce_ms,
    auto_hwm_stream_bootstrap, // deprecated no-op compatibility option
    auto_hwm_spot_bootstrap,   // deprecated no-op compatibility option
    auto_hwm_profile
};

enum class send_result_t : int {
    sent,
    backpressured,
    not_ready
};
```

`send_result_t` is the lightweight classification used by internal and
compatibility helpers for non-blocking sends. The canonical public
throwing surface for submit failures remains `submit_result_t` plus
`submit_error_t`.

#### Low-level error and monitor enums

```cpp
enum class error_code : int {
    efsm,
    enocompatproto,
    eterm,
    emthread
};

enum class protocol_error : int {
    zmp_malformed_command_hello
};

```

#### Spot and topology enums

```cpp
enum class monitor_target_kind : int {
    socket,
    discovery,
    spot,
    spot_node
};

enum class registry_socket_role : int {
    pub,
    router,
    peer_sub
};

enum class service_role : int {
    router,
    dealer
};

enum class discovery_socket_role : int {
    sub
};

enum class spot_node_socket_role : int {
    node,
    pub,
    sub,
    dealer
};

enum class spot_socket_role : int {
    pub,
    sub
};

enum class spot_node_state : int {
    idle,
    connecting,
    partial_ready,
    ready,
    error
};

enum class spot_peer_source : int {
    manual,
    discovery,
    mixed
};

enum class spot_peer_state : int {
    configured,
    connecting,
    connected
};

enum class registry_state : int {
    idle,
    active,
    degraded,
    error
};

enum class topology_source : int {
    manual,
    discovery,
    registry
};

enum class topology_state : int {
    discovered,
    connecting,
    ready,
    lost,
    error,
    stopped
};
```

Installed alias declarations:

```cpp
using monitor_event_type_t = monitor_event;
using monitor_source_kind_t = monitor_source_kind;
using auto_connect_type_t = auto_connect_type;
using service_role_t = service_role;
using service_kind_t = service_kind;
using monitor_target_kind_t = monitor_target_kind;
using spot_role_t = spot_socket_role;
using spot_node_state_t = spot_node_state;
using spot_peer_source_t = spot_peer_source;
using spot_peer_state_t = spot_peer_state;
using topology_source_t = topology_source;
using topology_state_t = topology_state;
using subject_kind_t = subject_kind;
```

### Error and Result Types

#### error_t

Generic compatibility exception wrapper. This type is public because it
is installed in `<zlink/error.hpp>`, but canonical high-level API
surfaces throw the category-specific subclasses below.

```cpp
class error_t : public zlink_error_t {
public:
    explicit error_t(int code);
    error_t(int code, int internal_errno);
};
```

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
    not_admitted     = 13  // target peer has weight 0
};
```

#### request_result_t

Maps to C API `zlink_request_result_t`.
Values are offset to 101-112 to avoid collision with `submit_result_t` codes.

```cpp
enum class request_result_t : int {
    ok               = 0,
    timed_out        = 101,
    not_found        = 102,
    terminated       = 103,
    protocol_error   = 104,
    internal_error   = 105,
    rejected         = 106,
    conflict         = 107,
    busy             = 108,
    not_connected    = 109,
    invalid_argument = 110,
    invalid_state    = 111,
    not_supported    = 112
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
    not_supported  = 205,
    internal_error = 206
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
    invalid_handle   = 305,
    internal_error   = 306
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
    invalid_handle = 403,
    internal_error = 404
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
    invalid_handle   = 504,
    internal_error   = 505
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
    invalid_handle   = 603,
    internal_error   = 604,
    not_found        = 605,
    conflict         = 606,
    busy             = 607
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
    not_supported    = 703,
    internal_error   = 704,
    invalid_state    = 705,
    not_found        = 706
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

Wraps `close_result_t`. Thrown by socket `close()` and
`destroy()` that invoke native close/destroy paths returning
`zlink_close_result_t`.

**`noexcept` carve-out**: RAII value types that own local resources only
(`message_t::close()`, `monitor_handle_t::close()`,
`poller_t::destroy()`) are marked
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
`attach_discovery(...)` and
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

### async_result_t\<T\>

Future wrapper for asynchronous request-reply results. Supports `co_await` in C++20.

```cpp
template<typename T>
class async_result_t {
    explicit async_result_t(std::future<T> future);
    template<class Progress>
    async_result_t(std::future<T> future, Progress&& progress);

    bool valid() const;
    void wait() const;
    template<typename Rep, typename Period>
    std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout) const;
    template<typename Clock, typename Duration>
    std::future_status wait_until(
        const std::chrono::time_point<Clock, Duration>& deadline) const;
    T get();

    // Present only when the compiler provides coroutine support.
    bool await_ready() const;
    void await_suspend(std::coroutine_handle<> continuation);
    T await_resume();
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

    /// @throws handler_error_t
    template<class Handler>
    void on_event(Handler&& handler);
    /// @throws recv_error_t
    std::optional<monitor_event_t> recv(recv_flags_t flags = recv_flags_t::none);
    /// @throws config_error_t
    monitor_snapshot_t snapshot() const;
    void close() noexcept;

    // No-op callback for callback-only model. Pass to on_event() when the
    // caller does not care about individual events; once installed the monitor
    // is in callback-only model and recv(...) fails with busy (snapshot()
    // still works). To drive the monitor through snapshot() / recv(...) instead,
    // leave on_event unset.
    static void ignore_event(const monitor_event_t&) noexcept;
};
```

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
    auto_connect_type_t auto_connect_type;
    service_role_t service_role;
    std::string channel_name;
    std::string endpoint;
    std::optional<routing_id_t> routing_id;   // nullopt when peer carries no routing id
    uint32_t weight;
    int64_t value;
};
```

#### registry_topology_entry_t

Topology entry returned by `registry_t::topology_snapshot(...)`,
`registry_t::topology_query(...)`, and
`registry_query_client_t::snapshot(...)`.

```cpp
struct registry_topology_entry_t {
    auto_connect_type_t auto_connect_type;
    std::optional<routing_id_t> routing_id;
    service_kind_t service_kind;
    service_role_t service_role;
    std::string channel_name;
    std::string endpoint;
    topology_source_t source;
    topology_state_t state;
    uint32_t desired_count;
    uint32_t ready_count;
    uint32_t error_code;
    std::chrono::milliseconds last_reported;
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
    uint32_t disconnected_sub_target_count;
    uint32_t disconnected_routed_target_count;
    int32_t last_error;
    std::chrono::milliseconds last_changed;
};
```

#### Advanced / Diagnostic

#### registry_service_summary_entry_t

Service summary entry returned by
`registry_t::service_summary_snapshot(...)`.

```cpp
struct registry_service_summary_entry_t {
    auto_connect_type_t auto_connect_type;
    service_role_t service_role;
    std::string channel_name;
    uint32_t total_count;
    uint32_t connecting_count;
    uint32_t ready_count;
    uint32_t error_count;
    uint32_t stopped_count;
    std::chrono::milliseconds last_reported;
};

struct registry_service_summary_filter_t {
    auto_connect_type_t auto_connect_type;
    service_role_t service_role;
    std::string channel_name;
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
    std::chrono::milliseconds last_changed;
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
    uint32_t weight;
    std::chrono::milliseconds connected_since;
    std::chrono::milliseconds last_changed;
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
    std::chrono::milliseconds last_changed;
};

enum class spot_service_attachment_role_t {
    router = 1,
    pub = 2,
    sub = 3,
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

struct spot_node_subject_filter_t {
    spot_role_t role;
    std::string subject;
    subject_kind_t subject_kind;
};

struct spot_node_options_t {
    spot_node_mode_t mode;
    auto_hwm_profile router_admission_hwm_profile = auto_hwm_profile::balanced;
    message_count_t router_admission_hwm;
    auto_hwm_profile pubsub_admission_hwm_profile = auto_hwm_profile::balanced;
    message_count_t pubsub_admission_hwm;
};

struct spot_node_socket_snapshot_filter_t {
    spot_node_socket_owner_t owner;
    socket_type type;
    std::string socket_name;
};

struct spot_node_socket_snapshot_entry_t {
    spot_node_socket_owner_t owner;
    uint64_t owner_id;
    std::string owner_name;
    std::string socket_name;
    socket_type type;
    bool auto_hwm_visible;
    monitor_snapshot_t snapshot;
};

struct spot_node_spot_entry_t {
    routing_id_t spot_rid;
    bool dispatch_handler_attached;
    uint32_t joined_actor_count;
    uint32_t pending_actor_join_count;
    bool route_synced;
    std::chrono::milliseconds last_changed;
};

struct spot_node_actor_entry_t {
    actor_ref_t actor;
    bool joined;
    std::optional<routing_id_t> joined_spot_rid;
    bool route_synced;
    uint32_t pending_message_count;
    std::chrono::milliseconds last_changed;
};

struct registry_topology_filter_t {
    auto_connect_type_t auto_connect_type;
    service_kind_t service_kind;
    service_role_t service_role;
    std::string channel_name;
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

    /// @throws bind_error_t
    void bind(const std::string& pub_endpoint, const std::string& router_endpoint);
    /// @throws config_error_t
    void set_id(uint32_t registry_id);
    /// @throws connect_error_t
    void add_peer(const std::string& peer_pub_endpoint);
    /// @throws config_error_t
    void set_heartbeat(std::chrono::milliseconds interval,
                       std::chrono::milliseconds timeout);
    /// @throws config_error_t
    void set_broadcast_interval(std::chrono::milliseconds interval);

    /// @throws config_error_t
    registry_status_t status_snapshot() const;
    /// @throws config_error_t
    std::vector<registry_service_summary_entry_t> service_summary_snapshot() const;
    /// @throws config_error_t
    std::vector<registry_service_summary_entry_t> service_summary_snapshot(
        const registry_service_summary_filter_t& filter) const;
    /// @throws config_error_t
    std::vector<registry_topology_entry_t> topology_snapshot() const;
    /// @throws config_error_t
    std::vector<registry_topology_entry_t> topology_query(
        const registry_topology_filter_t& filter) const;
    /// @throws config_error_t
    std::vector<member_peer_entry_t> member_peers(const std::string& channel_name) const;

    /// @throws close_error_t
    void close();
};

} // namespace service
```

### service::discovery_t

Fixed-channel discovery view. Tracks one auto-connect type and channel name.

```cpp
namespace service {

class discovery_t {
    discovery_t(context_t& ctx,
                auto_connect_type auto_connect_type,
                const std::string& channel_name);
    ~discovery_t();

    discovery_t(discovery_t&& other) noexcept;
    discovery_t& operator=(discovery_t&& other) noexcept;

    bool valid() const noexcept;

    /// @throws connect_error_t
    void connect_registry(const std::string& endpoint);
    /// @throws config_error_t
    void set_value(int64_t value);
    /// @throws config_error_t
    int64_t value() const;
    /// @throws config_error_t
    std::vector<member_peer_entry_t> member_peers() const;

    /// Resolve current owner node rid for a logical spot rid. Intended for
    /// send/request destination lookup. Maps to zlink_discovery_resolve_spot.
    /// Registry-backed lookup requires the publishing Discovery to enable
    /// ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC.
    /// @throws config_error_t
    routing_id_t resolve_spot(const routing_id_t& spot_rid);
    /// Enable or disable publishing SPOT owner rows to Registry.
    void set_spot_owner_sync_enabled(bool enabled);
    /// Read whether this Discovery publishes SPOT owner rows to Registry.
    bool spot_owner_sync_enabled() const;
    /// Enable or disable publishing Actor route rows to Registry.
    void set_actor_route_sync_enabled(bool enabled);
    /// Read whether this Discovery publishes Actor route rows to Registry.
    bool actor_route_sync_enabled() const;
    /// Resolve current Actor route information.
    /// @throws config_error_t
    actor_route_t resolve_actor(const std::string& actor_id);

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
    spot_node_t(context_t& ctx, const spot_node_options_t& options);
    ~spot_node_t();

    spot_node_t(spot_node_t&& other) noexcept;
    spot_node_t& operator=(spot_node_t&& other) noexcept;

    bool valid() const noexcept;

    /// @throws bind_error_t
    void bind(const std::string& endpoint);
    /// @throws config_error_t
    std::string last_endpoint() const;
    /// @throws connect_error_t
    void connect_peer(const std::string& endpoint);
    /// @throws connect_error_t
    void disconnect_peer(const std::string& endpoint);
    /// @throws connect_error_t
    void disconnect_peer_rid(const routing_id_t& target_node_rid);
    /// @throws config_error_t
    void attach_discovery(discovery_t& discovery);
    /// @throws config_error_t
    void attach_channel_dealer(discovery_t& discovery, dealer_socket_t& dealer);
    /// @throws config_error_t
    void attach_channel_dealer_manual(const std::string& channel_name,
                                      dealer_socket_t& dealer);
    /// @throws config_error_t
    void attach_pub_ingress(pub_socket_t& pub);

    // --- identity / routing ---
    /// Logical address / spot-level routed ownership key.
    /// Maps to zlink_set_routing_id(node, ...) / zlink_get_routing_id(node, ...).
    /// @throws config_error_t
    void set_routing_id(const routing_id_t& routing_id);
    /// @throws config_error_t
    void get_routing_id(routing_id_t& routing_id) const;
    /// @throws config_error_t
    routing_id_t routing_id() const;

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
    /// @throws config_error_t
    auto_hwm_profile router_admission_hwm_profile() const;
    /// @throws config_error_t
    void router_admission_hwm_profile(auto_hwm_profile profile);
    /// @throws config_error_t
    message_count_t router_admission_hwm() const;
    /// @throws config_error_t
    void router_admission_hwm(message_count_t value);
    /// @throws config_error_t
    auto_hwm_profile pubsub_admission_hwm_profile() const;
    /// @throws config_error_t
    void pubsub_admission_hwm_profile(auto_hwm_profile profile);
    /// @throws config_error_t
    message_count_t pubsub_admission_hwm() const;
    /// @throws config_error_t
    void pubsub_admission_hwm(message_count_t value);

    // --- snapshots ---
    /// @throws config_error_t
    spot_node_status_t status_snapshot() const;
    /// @throws config_error_t
    std::vector<spot_node_peer_entry_t> peers_snapshot() const;
    /// @throws config_error_t
    std::vector<spot_node_peer_entry_t> peers_query(
        const spot_node_peer_filter_t& filter) const;
    /// @throws config_error_t
    std::vector<spot_node_subject_entry_t> subjects_snapshot() const;
    /// @throws config_error_t
    std::vector<spot_node_subject_entry_t> subjects_snapshot(
        const spot_node_subject_filter_t& filter) const;
    /// @throws config_error_t
    std::vector<spot_node_socket_snapshot_entry_t> internal_sockets_snapshot() const;
    /// @throws config_error_t
    std::vector<spot_node_socket_snapshot_entry_t> internal_sockets_snapshot(
        const spot_node_socket_snapshot_filter_t& filter) const;

    // --- Actor dispatch ---
    /// @throws config_error_t
    actor_t create_actor(const std::string& actor_id);
    /// @throws config_error_t
    actor_ref_t actor_lookup(const std::string& actor_id) const;
    /// @throws config_error_t
    static actor_ref_t remote_actor_ref(const routing_id_t& target_node_rid,
                                        const std::string& actor_id);
    /// @throws request_error_t
    actor_create_result_t create_remote_actor(const routing_id_t& target_node_rid,
                                              const std::string& actor_id,
                                              message_t& message,
                                              std::chrono::milliseconds timeout = {});
    /// @throws request_error_t
    void destroy_actor(const actor_ref_t& actor,
                       std::chrono::milliseconds timeout = {});
    /// @throws handler_error_t
    template<class Handler>
    void on_actor_admission(Handler&& handler);
    /// @throws submit_error_t
    bool join_actor(const actor_ref_t& actor, const spot_address_t& dest,
                    message_t& message,
                    request_callback_t callback,
                    request_options_t options = {});
    /// @throws submit_error_t
    bool join_local_actor(const actor_ref_t& actor,
                          const local_spot_address_t& dest,
                          message_t& message,
                          request_callback_t callback,
                          request_options_t options = {});
    /// @throws request_error_t
    void leave_actor(const actor_ref_t& actor,
                     const local_spot_address_t& current_spot,
                     std::chrono::milliseconds timeout = {});
    /// @throws config_error_t
    std::vector<spot_node_spot_entry_t> spots_snapshot() const;
    /// @throws config_error_t
    std::vector<spot_node_actor_entry_t> actors_snapshot() const;

    // --- factory: spot_t is created only from spot_node_t ---
    /// @throws config_error_t
    spot_t create_spot();
    /// @throws config_error_t
    spot_t entry_spot();
    /// @throws config_error_t
    spot_t spot_lookup(const routing_id_t& spot_rid);

    // close() cascades: live spot_t handles are closed before the node exits.
    /// @throws close_error_t
    void close();
};

} // namespace service
```

`spot_node_t` owns the lifecycle. Public callers obtain `spot_t` handles only
through `spot_node_t::create_spot()`, `spot_node_t::entry_spot()`, or
`spot_node_t::spot_lookup(...)`. Direct `spot_t(node)` construction is
internal and is not a public constructor.

### service::spot_t

Spot messaging endpoint. Provides pub/sub, direct messaging, and subscription
management. Public callers obtain it only through `spot_node_t` factories.

```cpp
namespace service {

class spot_t {
    // explicit spot_t(spot_node_t&) is internal. User code creates a Spot
    // through spot_node_t factories.
    ~spot_t();

    spot_t(spot_t&& other) noexcept;
    spot_t& operator=(spot_t&& other) noexcept;

    bool valid() const noexcept;

    // --- channel-aware publish / request ---
    /// @throws submit_error_t
    bool publish(const std::string& service_name, const std::string& topic,
                 message_t& part, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool publish(const std::string& service_name, const std::string& topic,
                 std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool send_channel(const std::string& channel_name,
                      message_t& part,
                      send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool send_channel(const std::string& channel_name,
                      std::vector<message_t>& parts,
                      send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    async_result_t<std::vector<message_t>> request_channel(const std::string& channel_name,
                                                           message_t& part,
                                                           request_options_t options = {});
    /// @throws submit_error_t
    async_result_t<std::vector<message_t>> request_channel(const std::string& channel_name,
                                                           std::vector<message_t>& parts,
                                                           request_options_t options = {});
    /// @throws submit_error_t
    bool request_channel(const std::string& channel_name,
                         message_t& part,
                         request_callback_t callback,
                         request_options_t options = {});
    /// @throws submit_error_t
    bool request_channel(const std::string& channel_name,
                         std::vector<message_t>& parts,
                         request_callback_t callback,
                         request_options_t options = {});

    // --- subscribe ---
    /// @throws recv_error_t
    std::optional<service_topic_message_t> subscribe(recv_flags_t flags = recv_flags_t::none);
    /// @throws recv_error_t
    std::optional<service_subscription_event_t> receive_subscription_event(
        recv_flags_t flags = recv_flags_t::none);
    /// @throws config_error_t
    void set_subscription(const std::string& filter);
    /// @throws config_error_t
    void unset_subscription(const std::string& filter);
    /// @throws config_error_t
    subscription_filter_t subscription_at(size_t index) const;
    /// @throws handler_error_t
    template<class Handler>
    void on_send_ready(Handler&& handler);

    // --- identity / routing ---
    /// Logical address / spot-level routed ownership key.
    /// Maps to zlink_set_routing_id(spot, ...) / zlink_get_routing_id(spot, ...).
    /// @throws config_error_t
    void set_routing_id(const routing_id_t& routing_id);
    /// @throws config_error_t
    void get_routing_id(routing_id_t& routing_id) const;
    /// @throws config_error_t
    routing_id_t routing_id() const;

    // --- routed send (spot -> spot) ---
    /// @throws submit_error_t
    bool send_to_spot(const spot_address_t& dest,
                      message_t message,
                      send_flags_t flags = send_flags_t::none);
    /// @throws submit_error_t
    bool send_to_spot(const spot_address_t& dest,
                      std::vector<message_t>& parts,
                      send_flags_t flags = send_flags_t::none);

    // --- routed request (spot -> spot, coroutine, blocking submit — no flags) ---
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request_to_spot(
        const spot_address_t& dest,
        message_t message,
        request_options_t options = {});

    // --- routed request (spot -> spot, callback) ---
    /// @throws submit_error_t
    bool request_to_spot(
        const spot_address_t& dest,
        message_t message,
        request_callback_t callback,
        request_options_t options = {});

    // --- routed request (spot -> router, coroutine, blocking submit — no flags) ---
    /// @throws request_error_t (co_await), submit_error_t (submit)
    async_result_t<std::vector<message_t>> request_to_router(
        const routing_id_t& peer_rid,
        message_t message,
        request_options_t options = {});

    // --- routed request (spot -> router, callback) ---
    /// @throws submit_error_t
    bool request_to_router(
        const routing_id_t& peer_rid,
        message_t message,
        request_callback_t callback,
        request_options_t options = {});

    // --- routed reply (spot → spot) ---
    /// @throws submit_error_t
    void reply_to_spot(const spot_address_t& dest,
                       request_sequence_t request,
                       message_t message,
                       send_flags_t flags = send_flags_t::none);

    // --- routed reply (spot → router) ---
    /// @throws submit_error_t
    void reply_to_router(const routing_id_t& peer_rid,
                         request_sequence_t request,
                         message_t message,
                         send_flags_t flags = send_flags_t::none);

    // --- routed receive ---
    /// @throws recv_error_t
    std::optional<routed_received_t> recv_routed(recv_flags_t flags = recv_flags_t::none);
    /// @throws handler_error_t
    template<class Handler>
    void on_routed_receive(Handler&& handler);
    /// @throws handler_error_t
    template<class Handler>
    void on_dispatch_event(Handler&& handler);

    // --- Actor dispatch ---
    /// @throws recv_error_t
    std::optional<std::pair<actor_join_info_t, message_t>>
    recv_actor_join(recv_flags_t flags = recv_flags_t::none);
    /// @throws submit_error_t
    void reply_actor_join(const actor_join_info_t& info, bool accepted,
                          message_t& message);
    /// @throws config_error_t
    std::vector<actor_ref_t> actors_snapshot() const;

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

`on_dispatch_event(...)` is the canonical SPOT readable-notification surface.
For `SUBSCRIBE_READABLE` and `ROUTED_READABLE`, callers must keep draining with
`subscribe(...)` / `recv_routed(...)` until the underlying recv reports
`no_data` / `EAGAIN`.

### service::actor_t

Move-only Actor handle owned by a `spot_node_t`.

```cpp
namespace service {

class actor_t {
    ~actor_t();

    actor_t(actor_t&& other) noexcept;
    actor_t& operator=(actor_t&& other) noexcept;

    bool valid() const noexcept;
    actor_ref_t ref() const;

    /// @throws request_error_t
    void close(std::chrono::milliseconds timeout = {});
    /// @throws submit_error_t
    bool join(spot_t& spot, message_t& message,
              request_callback_t callback,
              request_options_t options = {});
    /// @throws request_error_t
    void leave(spot_t& spot, std::chrono::milliseconds timeout = {});
    /// @throws recv_error_t
    std::optional<actor_part_t> recv_part(recv_flags_t flags = recv_flags_t::none);
    /// @throws submit_error_t
    bool send_bound_session_msg(message_t& message,
                                send_flags_t flags = send_flags_t::none);
    /// @throws request_error_t
    void close_bound_session(std::chrono::milliseconds timeout = {});
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

    /// @throws connect_error_t
    void connect(const std::string& endpoint);
    /// @throws config_error_t
    std::vector<registry_topology_entry_t> snapshot() const;
    /// @throws config_error_t
    std::vector<registry_topology_entry_t> snapshot(
        const registry_topology_filter_t& filter) const;

    /// @throws close_error_t
    void close();
};

} // namespace service
```

---

## Poller

### poll_event_t

Readiness result returned by `poller_t::wait(...)` and
`poller_t::wait_all(...)`.

```cpp
enum class poll_event : short {
    none   = 0,
    pollin = 1,
    pollout = 2,
    pollerr = 4
};

enum class poll_source_kind_t {
    socket,
    fd,
    timer,
    spot
};

class poll_registration_t {
    bool valid() const noexcept;
};

struct socket_poll_event_t {
    poll_registration_t registration;
    poll_event events;
};

struct fd_poll_event_t {
    poll_registration_t registration;
    fd_handle_t fd;
    poll_event events;
};

struct timer_poll_event_t {
    poll_registration_t registration;
    uint64_t fire_count;
};

struct spot_poll_event_t {
    poll_registration_t registration;
    spot_dispatch_info_t dispatch;
};

using poll_event_t = std::variant<
    socket_poll_event_t,
    fd_poll_event_t,
    timer_poll_event_t,
    spot_poll_event_t>;
```

### poller_t

Event poller for multiplexing socket, file descriptor, timer, and Spot
dispatch readiness.

```cpp
class poller_t {
    poller_t();
    ~poller_t();

    poller_t(poller_t&& other) noexcept;
    poller_t& operator=(poller_t&& other) noexcept;

    bool valid() const noexcept;
    /// @throws config_error_t
    int size() const;  // returns number of registered pollable items

    // --- socket registration ---
    /// @throws config_error_t
    template<typename SocketLike>
    poll_registration_t add(SocketLike& socket, poll_event events);
    /// @throws config_error_t
    template<typename SocketLike>
    void modify(SocketLike& socket, poll_event events);
    /// @throws config_error_t
    template<typename SocketLike>
    void remove(SocketLike& socket);

    // --- file descriptor registration ---
    /// @throws config_error_t
    poll_registration_t add(fd_handle_t fd, poll_event events);
    /// @throws config_error_t
    void modify(fd_handle_t fd, poll_event events);
    /// @throws config_error_t
    void remove(fd_handle_t fd);

    // --- timer registration ---
    /// @throws config_error_t
    poll_registration_t add(timer_t& timer);
    /// @throws config_error_t
    void remove(timer_t& timer);

    // --- wait ---
    /// @throws recv_error_t
    std::optional<poll_event_t> wait(
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    /// @throws recv_error_t
    std::vector<poll_event_t> wait_all(
        std::optional<std::chrono::milliseconds> timeout = std::nullopt);

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
    ~timer_t();

    timer_t(timer_t&& other) noexcept;
    timer_t& operator=(timer_t&& other) noexcept;

    /// @throws config_error_t
    static timer_t from_spot(service::spot_t& spot);

    bool valid() const noexcept;

    /// @throws config_error_t
    template<class Rep, class Period>
    void start(std::chrono::duration<Rep, Period> interval,
               uint64_t repeat_count = 0);
    /// @throws config_error_t
    void stop();
    /// @throws recv_error_t
    std::optional<uint64_t> recv();
    /// @throws handler_error_t
    template<class Handler>
    void set_handler(Handler&& handler);
    /// @throws close_error_t
    void destroy();
};
```

---

## Utilities

### stopwatch_t

Compatibility elapsed-time stopwatch. Canonical C++ application code should
prefer `std::chrono` unless it needs to compare directly with the core
stopwatch helper.

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

Compatibility RAII wrapper for a background zlink thread. Canonical C++
application code should prefer `std::jthread` / `std::thread`; this wrapper is
only for code that intentionally exercises the core thread helper.

```cpp
class thread_t {
    thread_t();
    template<class Fn>
    explicit thread_t(Fn&& fn);
    ~thread_t();

    thread_t(thread_t&& other) noexcept;
    thread_t& operator=(thread_t&& other) noexcept;

    void close();
};
```

### atomic_counter_t

Compatibility wrapper for the core atomic counter helper. Canonical C++ code
should prefer `std::atomic`; this type remains for parity with the C utility
surface and tests that explicitly validate it.

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
template<typename FrontendSocket, typename BackendSocket>
void proxy(FrontendSocket& frontend, BackendSocket& backend);

template<typename FrontendSocket, typename BackendSocket, typename CaptureSocket>
void proxy(FrontendSocket& frontend, BackendSocket& backend, CaptureSocket& capture);

/// Start a steerable proxy with an additional control socket.
/// @throws zlink_error_t
template<typename FrontendSocket, typename BackendSocket,
         typename CaptureSocket, typename ControlSocket>
void proxy_steerable(FrontendSocket& frontend, BackendSocket& backend,
                     CaptureSocket& capture, ControlSocket& control);

/// Check if the library supports a given capability (e.g. "ipc", "tls").
bool has(const std::string& capability);

/// Sleep for the given duration.
template<class Rep, class Period>
void sleep(std::chrono::duration<Rep, Period> duration);
```

## Peer Disconnect by Routing ID

C++ bindings expose raw socket `disconnect_rid(routing_id_t)` and SpotNode
`disconnect_peer_rid(routing_id_t)`. They map the duplicate policy option and
the connect result values `not_found`, `conflict`, and `busy` to the same
contract as the C core. Spot facades do not expose a peer-rid disconnect
method.
