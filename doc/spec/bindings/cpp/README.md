[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# C++ Binding Specification

This document defines the complete public API surface of the C++ binding.
Every class, its purpose, and all public method signatures are listed.
Internal helpers and implementation details are omitted.

All types live in the `zlink` namespace. Service types live in `zlink::service`.

---

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

    int ioThreads() const;
    void ioThreads(int value);
    int maxSockets() const;
    void maxSockets(int value);
    int maxMsgSize() const;
    void maxMsgSize(int value);
    int threadPriority() const;
    void threadPriority(int value);
    int threadSchedulingPolicy() const;
    void threadSchedulingPolicy(int value);
    bool blocky() const;
    void blocky(bool enabled);
    int socketLimit() const;
    int msgTSize() const;
    void addThreadAffinity(int cpu);
    void removeThreadAffinity(int cpu);
};
```

---

## Socket Types

### Common base methods

All socket types inherit from `base_socket_t` and expose these common operations.
Individual socket classes re-expose them as public.

```cpp
// Available on all socket types
bool valid() const noexcept;
void bind(const std::string& endpoint);
void unbind(const std::string& endpoint);
void set_option(socket_option_key_t<T> key, const T& value);
void get_option(socket_option_key_t<T> key, T* value) const;
void set_option(socket_option_key_t<std::string> key, const std::string& value);
void get_option(socket_option_key_t<std::string> key, std::string& value) const;
void set_tls_server(const std::string& cert, const std::string& key,
                    bool require_client_cert = false);
void set_tls_client(const std::string& ca_cert, const std::string& hostname,
                    bool trust_system = false);
monitor_handle_t monitor_handle(monitor_event events = monitor_event::all) const;

// Available on connectable socket types (all except stream_socket_t)
void connect(const std::string& endpoint);
void disconnect(const std::string& endpoint);
```

### pair_socket_t

Bidirectional exclusive pair socket. Sends and receives messages without routing.

```cpp
class pair_socket_t : public message_socket_t {
    explicit pair_socket_t(context_t& ctx);

    // --- send (throws zlink_error_t on failure) ---
    void send(message_t& part, send_flags_t flags = send_flags_t::none);
    void send(std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- receive (throws zlink_error_t on failure) ---
    received_t recv(recv_flags_t flags = recv_flags_t::none);
    void on_receive(zlink_socket_msg_handler_fn handler, void* userdata = NULL);
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);
};
```

### pub_socket_t

Publisher socket. Sends topic-prefixed messages to all matching subscribers.

```cpp
class pub_socket_t : public publisher_socket_t {
    explicit pub_socket_t(context_t& ctx);

    // --- publish (throws zlink_error_t on failure) ---
    void publish(const std::string& topic_id, message_t& part, send_flags_t flags = send_flags_t::none);
    void publish(const std::string& topic_id, std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- pub-specific options ---
    void set_option(pub_option_key_t<T> key, const T& value);
    void get_option(pub_option_key_t<T> key, T* value) const;

    // --- discovery ---
    void attach_discovery(DiscoveryT& discovery);
};
```

### sub_socket_t

Subscriber socket. Receives topic-filtered messages from publishers.

```cpp
class sub_socket_t : public subscriber_socket_t {
    explicit sub_socket_t(context_t& ctx);

    // --- subscription ---
    void set_subscription(const std::string& filter);
    void unset_subscription(const std::string& filter);
    void subscription_at(size_t index, std::string& filter, bool* is_pattern = NULL);

    // --- receive (throws zlink_error_t on failure) ---
    subscribed_t subscribe(recv_flags_t flags = recv_flags_t::none);
    void on_subscribe(zlink_subscribe_handler_fn handler, void* userdata = NULL);

    // --- sub-specific options ---
    void set_option(sub_option_key_t<T> key, const T& value);
    void get_option(sub_option_key_t<T> key, T* value) const;

    // --- discovery ---
    void attach_discovery(DiscoveryT& discovery);
};
```

### dealer_socket_t

Asynchronous client socket for fair-queued request distribution.

```cpp
class dealer_socket_t : public message_socket_t {
    explicit dealer_socket_t(context_t& ctx);

    // --- send (throws zlink_error_t on failure) ---
    void send(message_t& part, send_flags_t flags = send_flags_t::none);
    void send(std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- receive (throws zlink_error_t on failure) ---
    received_t recv(recv_flags_t flags = recv_flags_t::none);
    void on_receive(zlink_socket_msg_handler_fn handler, void* userdata = NULL);
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- identity / routing ---
    void set_routing_id(const routing_id_t& routing_id);
    void get_routing_id(routing_id_t& routing_id) const;

    // --- dealer-specific options ---
    void set_option(dealer_option_key_t<T> key, const T& value);

    // --- discovery ---
    void attach_discovery(DiscoveryT& discovery);
};
```

### router_socket_t

Server socket that routes messages to specific peers by routing id.

```cpp
class router_socket_t : public routed_message_socket_t {
    explicit router_socket_t(context_t& ctx);

    // --- routed send (throws zlink_error_t on failure) ---
    void send(const routing_id_t& target_rid, message_t& part, send_flags_t flags = send_flags_t::none);
    void send(const routing_id_t& target_rid, std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- receive (throws zlink_error_t on failure) ---
    received_t recv(recv_flags_t flags = recv_flags_t::none);
    void on_receive(zlink_socket_msg_handler_fn handler, void* userdata = NULL);
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- identity / routing ---
    void set_routing_id(const routing_id_t& routing_id);
    void get_routing_id(routing_id_t& routing_id) const;

    // --- router → spot routed send (throws zlink_error_t on failure) ---
    void send_to_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                      message_t& part, send_flags_t flags = send_flags_t::none);
    void send_to_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                      std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- router → spot routed request (coroutine, blocking submit — no flags) ---
    async_result_t<received_t> request_to_spot(const routing_id_t& dest_node_rid,
                                               const routing_id_t& dest_spot_rid,
                                               message_t message,
                                               std::chrono::milliseconds timeout = {});

    // --- router → spot routed request (callback, throws on submit failure) ---
    void request_to_spot(const routing_id_t& dest_node_rid,
                         const routing_id_t& dest_spot_rid,
                         message_t message,
                         std::function<void(request_result_t, received_t)> callback,
                         send_flags_t flags = send_flags_t::none,
                         std::chrono::milliseconds timeout = {});

    // --- router → spot routed reply (throws zlink_error_t on failure) ---
    void reply_to_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                       uint64_t request_seq, message_t message, send_flags_t flags = send_flags_t::none);

    // --- router spot receive (throws zlink_error_t on failure) ---
    received_t recv_spot(recv_flags_t flags = recv_flags_t::none);
    void on_spot_receive(zlink_router_spot_handler_fn handler, void* userdata = NULL);

    // --- router-specific options ---
    void set_option(router_option_key_t<T> key, const T& value);
    void get_option(router_option_key_t<T> key, T* value) const;

    // --- discovery ---
    void attach_discovery(DiscoveryT& discovery);
};
```

### xpub_socket_t

Extended publisher. Like pub_socket_t but also receives subscription events.

```cpp
class xpub_socket_t : public publisher_socket_t {
    explicit xpub_socket_t(context_t& ctx);

    // --- publish (throws zlink_error_t on failure) ---
    void publish(const std::string& topic_id, message_t& part, send_flags_t flags = send_flags_t::none);
    void publish(const std::string& topic_id, std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- subscription events (throws zlink_error_t on failure) ---
    subscription_event_t receive_subscription_event(recv_flags_t flags = recv_flags_t::none);

    // --- pub-specific options ---
    void set_option(pub_option_key_t<T> key, const T& value);
    void get_option(pub_option_key_t<T> key, T* value) const;
};
```

### xsub_socket_t

Extended subscriber. Like sub_socket_t with raw subscription forwarding.

```cpp
class xsub_socket_t : public subscriber_socket_t {
    explicit xsub_socket_t(context_t& ctx);

    // --- subscription ---
    void set_subscription(const std::string& filter);
    void unset_subscription(const std::string& filter);
    void subscription_at(size_t index, std::string& filter, bool* is_pattern = NULL);

    // --- receive (throws zlink_error_t on failure) ---
    subscribed_t subscribe(recv_flags_t flags = recv_flags_t::none);
    void on_subscribe(zlink_subscribe_handler_fn handler, void* userdata = NULL);

    // --- sub-specific options ---
    void set_option(sub_option_key_t<T> key, const T& value);
    void get_option(sub_option_key_t<T> key, T* value) const;
};
```

### stream_socket_t

Raw TCP stream socket. Bind-only; connect is deleted.

```cpp
class stream_socket_t : public routed_message_socket_t {
    explicit stream_socket_t(context_t& ctx);

    void connect(const std::string&) = delete;

    // --- routed send (throws zlink_error_t on failure) ---
    void send(const routing_id_t& target_rid, message_t& part, send_flags_t flags = send_flags_t::none);
    void send(const routing_id_t& target_rid, std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- receive (throws zlink_error_t on failure) ---
    received_t recv(recv_flags_t flags = recv_flags_t::none);
    void on_receive(zlink_socket_msg_handler_fn handler, void* userdata = NULL);
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- identity ---
    void set_routing_id(const routing_id_t& routing_id);
    void get_routing_id(routing_id_t& routing_id) const;

    // --- stream-specific options ---
    void set_option(stream_option_key_t<T> key, const T& value);
    void get_option(stream_option_key_t<T> key, T* value) const;
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
    static message_t from_bytes(const void* data, size_t size);
    static message_t from_bytes(const std::vector<uint8_t>& bytes);
    static message_t from_string(const std::string& text);
    static message_t from_external(void* data, size_t size,
                                   zlink_free_fn* ffn = NULL, void* hint = NULL);

    // --- init ---
    void init();
    void init(size_t size);
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

Immutable binary-safe routing identity value object (max 255 bytes).

```cpp
class routing_id_t {
    routing_id_t() noexcept;
    explicit routing_id_t(const std::string& bytes);
    routing_id_t(const void* bytes, size_t size);
    routing_id_t(const zlink_routing_id_t& native);

    size_t size() const noexcept;
    bool empty() const noexcept;
    std::vector<uint8_t> to_bytes() const;
    std::string to_string() const;
    const zlink_routing_id_t& native() const noexcept;
    operator zlink_routing_id_t() const noexcept;
};
```

### received_t

Aggregates one recv result with optional routing id and message parts.

```cpp
struct received_t {
    routing_id_t routing_id;
    std::vector<message_t> parts;
    uint64_t request_seq = 0;
    bool has_request_seq = false;
};
```

### subscribed_t

Aggregates one subscribe recv result with topic and source routing id.

```cpp
struct subscribed_t {
    routing_id_t routing_id;
    std::string topic;
    std::vector<message_t> parts;
};
```

### subscription_event_t

Reports a subscribe/unsubscribe event from xpub sockets.

```cpp
struct subscription_event_t {
    routing_id_t routing_id;
    std::string topic;
    bool subscribed;
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
    internal_error   = 12
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
Covers handler registration operations (on_receive, on_subscribe, etc.).

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

Exception thrown by all socket operations on failure.
The `code` field is a globally unique `int` that spans all result enum
ranges (0-703). The code alone identifies the error without needing to
know which enum it belongs to.

```cpp
class zlink_error_t : public std::runtime_error {
public:
    explicit zlink_error_t(int errnum);
    explicit zlink_error_t(int code);

    int errnum() const noexcept;
    int code() const noexcept;
    const char* what() const noexcept override;
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

## Request-Reply

### request_router_t

Request-reply layer on top of a router_socket_t. Manages request correlation
and reply dispatch.

```cpp
class request_router_t {
    explicit request_router_t(router_socket_t& socket);

    void set_default_request_timeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds get_default_request_timeout() const;

    // --- request (coroutine, blocking submit — no flags) ---
    async_result_t<received_t> request(const routing_id_t& routing_id, message_t message,
                                       std::chrono::milliseconds timeout = {});

    // --- request (callback, throws on submit failure) ---
    void request(const routing_id_t& routing_id, message_t message,
                 std::function<void(request_result_t, received_t)> callback,
                 send_flags_t flags = send_flags_t::none,
                 std::chrono::milliseconds timeout = {});

    // --- reply (throws zlink_error_t on failure) ---
    void reply(const routing_id_t& routing_id, uint64_t request_seq, message_t message,
               send_flags_t flags = send_flags_t::none);

    // --- receive (throws zlink_error_t on failure) ---
    received_t recv(recv_flags_t flags = recv_flags_t::none);
    void on_receive(std::function<void(received_t)> handler);
};
```

### request_dealer_t

Request-reply layer on top of a dealer_socket_t.

```cpp
class request_dealer_t {
    explicit request_dealer_t(dealer_socket_t& socket);

    void set_default_request_timeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds get_default_request_timeout() const;

    // --- request (coroutine, blocking submit — no flags) ---
    async_result_t<received_t> request(message_t message,
                                       std::chrono::milliseconds timeout = {});

    // --- request (callback, throws on submit failure) ---
    void request(message_t message,
                 std::function<void(request_result_t, received_t)> callback,
                 send_flags_t flags = send_flags_t::none,
                 std::chrono::milliseconds timeout = {});

    // --- receive (throws zlink_error_t on failure) ---
    received_t recv(recv_flags_t flags = recv_flags_t::none);
    void on_receive(zlink_socket_msg_handler_fn handler, void* userdata = NULL);
};
```

---

## Monitoring

### monitor_handle_t

RAII wrapper for socket monitoring. Receives connect, disconnect, and handshake events.

```cpp
class monitor_handle_t {
    monitor_handle_t();
    ~monitor_handle_t();

    monitor_handle_t(monitor_handle_t&& other) noexcept;
    monitor_handle_t& operator=(monitor_handle_t&& other) noexcept;

    template<typename SocketLike>
    static monitor_handle_t open(const SocketLike& socket,
                                 monitor_event events = monitor_event::all);

    bool valid() const noexcept;
    void* handle() noexcept;
    const void* handle() const noexcept;

    void on_event(monitor_event_handler_fn handler, void* userdata = NULL);
    monitor_event_t recv();
    maybe_t<monitor_event_t> recv(non_blocking_t);
    monitor_snapshot_t snapshot() const;
    void close() noexcept;
};
```

### service_monitor_handle_t

RAII wrapper for service-level monitoring (discovery peer events, subject changes).

```cpp
class service_monitor_handle_t {
    service_monitor_handle_t();
    ~service_monitor_handle_t();

    service_monitor_handle_t(service_monitor_handle_t&& other) noexcept;
    service_monitor_handle_t& operator=(service_monitor_handle_t&& other) noexcept;

    bool valid() const noexcept;
    void* handle() noexcept;
    const void* handle() const noexcept;

    void on_event(service_event_handler_fn handler, void* userdata = NULL);
    service_event_t recv();
    maybe_t<service_event_t> recv(non_blocking_t);
    monitor_snapshot_t snapshot() const;
    void close() noexcept;
};
```

---

## Services

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
    int last_error() const noexcept;
    void* handle() const;

    void bind(const std::string& pub_endpoint, const std::string& router_endpoint);
    void set_id(uint32_t registry_id);
    void add_peer(const std::string& peer_pub_endpoint);
    void set_heartbeat(uint32_t interval_ms, uint32_t timeout_ms);
    void set_broadcast_interval(uint32_t interval_ms);

    void status_snapshot(zlink_registry_status_t& out) const;
    void service_summary_snapshot(zlink_registry_service_summary_entry_t* entries,
                                  size_t* count,
                                  const zlink_registry_service_summary_filter_t* filter = NULL) const;
    void topology_snapshot(zlink_registry_topology_entry_t* entries, size_t* count) const;
    void topology_query(zlink_registry_topology_entry_t* entries, size_t* count,
                        const zlink_registry_topology_filter_t* filter) const;
    void member_peers(service_type service_type, const std::string& service_name,
                      zlink_member_peer_entry_t* entries, size_t* count) const;
    void member_peer_metadata(service_type service_type, const std::string& service_name,
                              service_role service_role, const std::string& endpoint,
                              message_t& metadata_out) const;

    void close();
};

} // namespace service
```

### service::discovery_t

Fixed-service discovery view. Tracks one service type/name pair and
provides metadata, member peer snapshots, and service monitor access.

```cpp
namespace service {

class discovery_t {
    discovery_t(context_t& ctx, service_type service_type, const std::string& service_name);
    ~discovery_t();

    discovery_t(discovery_t&& other) noexcept;
    discovery_t& operator=(discovery_t&& other) noexcept;

    bool valid() const noexcept;
    int last_error() const noexcept;
    void* handle() const;

    void connect_registry(const std::string& endpoint);
    void set_value(int64_t value);
    void get_value(int64_t* value_out) const;
    void set_metadata(const void* data, size_t size);
    void set_metadata(const std::vector<uint8_t>& bytes);
    void set_metadata(const std::string& text);
    void get_metadata(message_t& metadata_out) const;
    void member_peers(zlink_member_peer_entry_t* entries, size_t* count) const;
    void member_peer_metadata(service_role service_role, const std::string& endpoint,
                              message_t& metadata_out) const;

    service_monitor_handle_t monitor_open(service_monitor_event events = service_monitor_event::all);

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
    int last_error() const noexcept;
    void* handle() const;

    void bind(const std::string& endpoint);
    std::string last_endpoint() const;
    void connect_peer(const std::string& endpoint);
    void disconnect_peer(const std::string& endpoint);
    void attach_discovery(discovery_t& discovery);

    void set_routing_id(const routing_id_t& routing_id);
    void get_routing_id(routing_id_t& out) const;

    void set_tls_server(const std::string& cert, const std::string& key,
                        bool require_client_cert = false);
    void set_tls_client(const std::string& ca_cert, const std::string& hostname = "",
                        bool trust_system = false);

    // --- options ---
    void set(socket_option_key_t<T> key, const T& value);
    void get(socket_option_key_t<T> key, T& value) const;

    // --- snapshots ---
    void status_snapshot(zlink_spot_node_status_t& out) const;
    void peers_snapshot(zlink_spot_node_peer_entry_t* entries, size_t* count) const;
    void peers_query(zlink_spot_node_peer_entry_t* entries, size_t* count,
                     const zlink_spot_node_peer_filter_t* filter) const;
    void subjects_snapshot(zlink_spot_node_subject_entry_t* entries, size_t* count,
                           const zlink_spot_node_subject_filter_t* filter = NULL) const;

    void close();
};

} // namespace service
```

### service::spot_t

Spot messaging endpoint. Provides pub/sub, direct messaging, and subscription management.

```cpp
namespace service {

class spot_t {
    explicit spot_t(spot_node_t& node);
    ~spot_t();

    spot_t(spot_t&& other) noexcept;
    spot_t& operator=(spot_t&& other) noexcept;

    bool valid() const noexcept;
    int last_error() const noexcept;
    void* handle() const;

    // --- publish (throws zlink_error_t on failure) ---
    void publish(const std::string& topic, message_t& part, send_flags_t flags = send_flags_t::none);
    void publish(const std::string& topic, std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);
    void publish(const char* topic, message_t& part, send_flags_t flags = send_flags_t::none);
    void publish(const char* topic, std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- subscribe (throws zlink_error_t on failure) ---
    subscribed_t subscribe(recv_flags_t flags = recv_flags_t::none);
    void set_subscription(const std::string& filter);
    void unset_subscription(const std::string& filter);
    void subscription_at(size_t index, std::string& filter_out, bool* is_pattern_out = NULL) const;
    void on_subscribe(zlink_subscribe_handler_fn handler, void* userdata = NULL);
    void on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- identity ---
    void set_routing_id(const routing_id_t& routing_id);
    void get_routing_id(routing_id_t& out) const;

    // --- routed send (spot → spot, throws zlink_error_t on failure) ---
    void send_to_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                      message_t& part, send_flags_t flags = send_flags_t::none);
    void send_to_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                      std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- routed request (spot → spot, coroutine, blocking submit — no flags) ---
    async_result_t<received_t> request_to_spot(const routing_id_t& dest_node_rid,
                                               const routing_id_t& dest_spot_rid,
                                               message_t message,
                                               std::chrono::milliseconds timeout = {});

    // --- routed request (spot → spot, callback, throws on submit failure) ---
    void request_to_spot(const routing_id_t& dest_node_rid,
                         const routing_id_t& dest_spot_rid,
                         message_t message,
                         std::function<void(request_result_t, received_t)> callback,
                         send_flags_t flags = send_flags_t::none,
                         std::chrono::milliseconds timeout = {});

    // --- routed reply (spot → spot, throws zlink_error_t on failure) ---
    void reply_to_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                       uint64_t request_seq, message_t message, send_flags_t flags = send_flags_t::none);

    // --- routed send (spot → router, throws zlink_error_t on failure) ---
    void send_to_router(const routing_id_t& peer_rid, message_t& part, send_flags_t flags = send_flags_t::none);
    void send_to_router(const routing_id_t& peer_rid, std::vector<message_t>& parts, send_flags_t flags = send_flags_t::none);

    // --- routed request (spot → router, coroutine, blocking submit — no flags) ---
    async_result_t<received_t> request_to_router(const routing_id_t& peer_rid,
                                                 message_t message,
                                                 std::chrono::milliseconds timeout = {});

    // --- routed request (spot → router, callback, throws on submit failure) ---
    void request_to_router(const routing_id_t& peer_rid,
                           message_t message,
                           std::function<void(request_result_t, received_t)> callback,
                           send_flags_t flags = send_flags_t::none,
                           std::chrono::milliseconds timeout = {});

    // --- routed reply (spot → router, throws zlink_error_t on failure) ---
    void reply_to_router(const routing_id_t& peer_rid, uint64_t request_seq,
                         message_t message, send_flags_t flags = send_flags_t::none);

    // --- routed receive (throws zlink_error_t on failure) ---
    received_t recv_routed(recv_flags_t flags = recv_flags_t::none);
    void on_routed_receive(zlink_spot_handler_fn handler, void* userdata = NULL);
    void on_dispatch_event(zlink_spot_dispatch_event_handler_fn handler,
                           void* userdata = NULL);

    // --- options ---
    void set(socket_option_key_t<T> key, const T& value);
    void get(socket_option_key_t<T> key, T& value) const;
    void set(pub_option_key_t<T> key, const T& value);
    void get(pub_option_key_t<T> key, T& value) const;
    void set(sub_option_key_t<T> key, const T& value);
    void get(sub_option_key_t<T> key, T& value) const;

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
    int last_error() const noexcept;
    void* handle() const;

    void connect(const std::string& endpoint);
    void snapshot(zlink_registry_topology_entry_t* entries, size_t* count,
                  const zlink_registry_topology_filter_t* filter = NULL) const;

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
    int size() const;

    // --- socket registration ---
    template<typename SocketLike>
    void add(SocketLike& socket, poll_event events, void* user = NULL);
    template<typename SocketLike>
    void modify(SocketLike& socket, poll_event events);
    template<typename SocketLike>
    void remove(SocketLike& socket);

    // --- file descriptor registration ---
    void add(zlink_fd_t fd, poll_event events, void* user = NULL);
    void modify(zlink_fd_t fd, poll_event events);
    void remove(zlink_fd_t fd);

    // --- wait ---
    int wait(poll_event_t* event, long timeout = -1);
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

    template<typename SpotLike>
    static timer_t from_spot(SpotLike& spot);

    bool valid() const noexcept;
    void* handle() noexcept;
    const void* handle() const noexcept;

    void start(uint64_t interval_ns, uint64_t repeat_count);
    void stop();
    void recv(uint64_t* fire_count_out, int flags = 0);
    void set_handler(zlink_timer_handler_fn handler, void* userdata = NULL);
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
/// Return the errno for the current thread.
int zlink_errno();

/// Return a human-readable string for the given error number.
const char* zlink_strerror(int errnum);

/// Return the runtime library version.
void zlink_version(int& major, int& minor, int& patch);

/// Start a built-in proxy between frontend and backend sockets.
/// An optional capture socket receives copies of all messages.
int proxy(void* frontend, void* backend, void* capture = NULL);

/// Start a steerable proxy with an additional control socket.
int proxy_steerable(void* frontend, void* backend,
                    void* capture, void* control);

/// Check if the library supports a given capability (e.g. "ipc", "tls").
bool has(const std::string& capability);

/// Sleep for the given number of seconds.
void sleep(int seconds);

/// Close all parts in a multipart message array.
void multipart_close(zlink_msg_t* parts, size_t count);
```
