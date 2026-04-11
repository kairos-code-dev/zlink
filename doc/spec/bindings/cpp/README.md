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

    int shutdown();
    int term() noexcept;

    context_options_t options();
};
```

### context_options_t

Typed facade for context configuration options.

```cpp
class context_options_t {
    explicit context_options_t(context_t& ctx);

    int ioThreads() const;
    int ioThreads(int value);
    int maxSockets() const;
    int maxSockets(int value);
    int maxMsgSize() const;
    int maxMsgSize(int value);
    int threadPriority() const;
    int threadPriority(int value);
    int threadSchedulingPolicy() const;
    int threadSchedulingPolicy(int value);
    bool blocky() const;
    int blocky(bool enabled);
    int socketLimit() const;
    int msgTSize() const;
    int addThreadAffinity(int cpu);
    int removeThreadAffinity(int cpu);
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
int bind(const std::string& endpoint);
int unbind(const std::string& endpoint);
int set_option(socket_option_key_t<T> key, const T& value);
int get_option(socket_option_key_t<T> key, T* value) const;
int set_option(socket_option_key_t<std::string> key, const std::string& value);
int get_option(socket_option_key_t<std::string> key, std::string& value) const;
int set_tls_server(const std::string& cert, const std::string& key,
                   bool require_client_cert = false);
int set_tls_client(const std::string& ca_cert, const std::string& hostname,
                   bool trust_system = false);
monitor_handle_t monitor_handle(monitor_event events = monitor_event::all) const;

// Available on connectable socket types (all except stream_socket_t)
int connect(const std::string& endpoint);
int disconnect(const std::string& endpoint);
```

### pair_socket_t

Bidirectional exclusive pair socket. Sends and receives messages without routing.

```cpp
class pair_socket_t : public message_socket_t {
    explicit pair_socket_t(context_t& ctx);

    // --- send ---
    void send(message_t& part);
    void send(std::vector<message_t>& parts);
    send_result_t try_send(message_t& part);
    send_result_t try_send(std::vector<message_t>& parts);

    // --- receive ---
    received_t recv();
    maybe_t<received_t> try_recv();
    int on_receive(zlink_socket_msg_handler_fn handler, void* userdata = NULL);
    int on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);
};
```

### pub_socket_t

Publisher socket. Sends topic-prefixed messages to all matching subscribers.

```cpp
class pub_socket_t : public publisher_socket_t {
    explicit pub_socket_t(context_t& ctx);

    // --- publish ---
    void publish(const std::string& topic_id, message_t& part);
    void publish(const std::string& topic_id, std::vector<message_t>& parts);
    send_result_t try_publish(const std::string& topic_id, message_t& part);
    send_result_t try_publish(const std::string& topic_id, std::vector<message_t>& parts);
    int on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- pub-specific options ---
    int set_option(pub_option_key_t<T> key, const T& value);
    int get_option(pub_option_key_t<T> key, T* value) const;

    // --- discovery ---
    int attach_discovery(DiscoveryT& discovery);
};
```

### sub_socket_t

Subscriber socket. Receives topic-filtered messages from publishers.

```cpp
class sub_socket_t : public subscriber_socket_t {
    explicit sub_socket_t(context_t& ctx);

    // --- subscription ---
    int set_subscription(const std::string& filter);
    int unset_subscription(const std::string& filter);
    int subscription_at(size_t index, std::string& filter, bool* is_pattern = NULL);

    // --- receive ---
    subscribed_t subscribe();
    maybe_t<subscribed_t> try_subscribe();
    int on_subscribe(zlink_subscribe_handler_fn handler, void* userdata = NULL);

    // --- sub-specific options ---
    int set_option(sub_option_key_t<T> key, const T& value);
    int get_option(sub_option_key_t<T> key, T* value) const;

    // --- discovery ---
    int attach_discovery(DiscoveryT& discovery);
};
```

### dealer_socket_t

Asynchronous client socket for fair-queued request distribution.

```cpp
class dealer_socket_t : public message_socket_t {
    explicit dealer_socket_t(context_t& ctx);

    // --- send / receive ---
    void send(message_t& part);
    void send(std::vector<message_t>& parts);
    send_result_t try_send(message_t& part);
    send_result_t try_send(std::vector<message_t>& parts);
    received_t recv();
    maybe_t<received_t> try_recv();
    int on_receive(zlink_socket_msg_handler_fn handler, void* userdata = NULL);
    int on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- identity / routing ---
    int set_routing_id(const routing_id_t& routing_id);
    int get_routing_id(routing_id_t& routing_id) const;

    // --- dealer-specific options ---
    int set_option(dealer_option_key_t<T> key, const T& value);

    // --- discovery ---
    int attach_discovery(DiscoveryT& discovery);
};
```

### router_socket_t

Server socket that routes messages to specific peers by routing id.

```cpp
class router_socket_t : public routed_message_socket_t {
    explicit router_socket_t(context_t& ctx);

    // --- routed send ---
    void send(const routing_id_t& target_rid, message_t& part);
    void send(const routing_id_t& target_rid, std::vector<message_t>& parts);
    send_result_t try_send(const routing_id_t& target_rid, message_t& part);
    send_result_t try_send(const routing_id_t& target_rid, std::vector<message_t>& parts);

    // --- receive ---
    received_t recv();
    maybe_t<received_t> try_recv();
    int on_receive(zlink_socket_msg_handler_fn handler, void* userdata = NULL);
    int on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- identity / routing ---
    int set_routing_id(const routing_id_t& routing_id);
    int get_routing_id(routing_id_t& routing_id) const;

    // --- router → spot routed send ---
    void send_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                   message_t& part);
    void send_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                   std::vector<message_t>& parts);
    send_result_t try_send_spot(const routing_id_t& dest_node_rid,
                                const routing_id_t& dest_spot_rid,
                                message_t& part);
    send_result_t try_send_spot(const routing_id_t& dest_node_rid,
                                const routing_id_t& dest_spot_rid,
                                std::vector<message_t>& parts);

    // --- router → spot routed request (async) ---
    async_result_t<received_t> request_spot(const routing_id_t& dest_node_rid,
                                            const routing_id_t& dest_spot_rid,
                                            message_t message,
                                            std::chrono::milliseconds timeout = {});
    void request_spot(const routing_id_t& dest_node_rid,
                      const routing_id_t& dest_spot_rid,
                      message_t message,
                      std::function<void(received_t)> on_reply,
                      std::function<void(error_t)> on_error,
                      std::chrono::milliseconds timeout = {});

    // --- router → spot routed reply ---
    void reply_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                    uint64_t request_seq, message_t message);

    // --- router spot receive ---
    received_t recv_spot();
    maybe_t<received_t> try_recv_spot();
    int on_spot_receive(zlink_router_spot_handler_fn handler, void* userdata = NULL);

    // --- router-specific options ---
    int set_option(router_option_key_t<T> key, const T& value);
    int get_option(router_option_key_t<T> key, T* value) const;

    // --- discovery ---
    int attach_discovery(DiscoveryT& discovery);
};
```

### xpub_socket_t

Extended publisher. Like pub_socket_t but also receives subscription events.

```cpp
class xpub_socket_t : public publisher_socket_t {
    explicit xpub_socket_t(context_t& ctx);

    // --- publish ---
    void publish(const std::string& topic_id, message_t& part);
    void publish(const std::string& topic_id, std::vector<message_t>& parts);
    send_result_t try_publish(const std::string& topic_id, message_t& part);
    send_result_t try_publish(const std::string& topic_id, std::vector<message_t>& parts);
    int on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- subscription events ---
    subscription_event_t receive_subscription_event();
    maybe_t<subscription_event_t> try_receive_subscription_event();

    // --- pub-specific options ---
    int set_option(pub_option_key_t<T> key, const T& value);
    int get_option(pub_option_key_t<T> key, T* value) const;
};
```

### xsub_socket_t

Extended subscriber. Like sub_socket_t with raw subscription forwarding.

```cpp
class xsub_socket_t : public subscriber_socket_t {
    explicit xsub_socket_t(context_t& ctx);

    // --- subscription ---
    int set_subscription(const std::string& filter);
    int unset_subscription(const std::string& filter);
    int subscription_at(size_t index, std::string& filter, bool* is_pattern = NULL);

    // --- receive ---
    subscribed_t subscribe();
    maybe_t<subscribed_t> try_subscribe();
    int on_subscribe(zlink_subscribe_handler_fn handler, void* userdata = NULL);

    // --- sub-specific options ---
    int set_option(sub_option_key_t<T> key, const T& value);
    int get_option(sub_option_key_t<T> key, T* value) const;
};
```

### stream_socket_t

Raw TCP stream socket. Bind-only; connect is deleted.

```cpp
class stream_socket_t : public routed_message_socket_t {
    explicit stream_socket_t(context_t& ctx);

    int connect(const std::string&) = delete;

    // --- routed send ---
    void send(const routing_id_t& target_rid, message_t& part);
    void send(const routing_id_t& target_rid, std::vector<message_t>& parts);
    send_result_t try_send(const routing_id_t& target_rid, message_t& part);
    send_result_t try_send(const routing_id_t& target_rid, std::vector<message_t>& parts);

    // --- receive ---
    received_t recv();
    maybe_t<received_t> try_recv();
    int on_receive(zlink_socket_msg_handler_fn handler, void* userdata = NULL);
    int on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- identity ---
    int set_routing_id(const routing_id_t& routing_id);
    int get_routing_id(routing_id_t& routing_id) const;

    // --- stream-specific options ---
    int set_option(stream_option_key_t<T> key, const T& value);
    int get_option(stream_option_key_t<T> key, T* value) const;
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
    int init();
    int init(size_t size);
    int init(void* data, size_t size, zlink_free_fn* ffn = NULL, void* hint = NULL);

    // --- accessors ---
    void* data() noexcept;
    const void* data() const noexcept;
    size_t size() const noexcept;
    int ref_count() const noexcept;
    const char* get_property(const std::string& property) const;
    int get_property(const std::string& property, std::string& out) const;

    // --- conversions ---
    std::vector<uint8_t> to_bytes() const;
    std::string to_string() const;

    // --- lifecycle ---
    int close() noexcept;
    zlink_msg_t* handle() noexcept;
    const zlink_msg_t* handle() const noexcept;
    int adopt(zlink_msg_t* src);
    int move_to(zlink_msg_t* dest);
    int copy_to(zlink_msg_t* dest) const;
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

### send_result_t

```cpp
enum class send_result_t : int {
    sent,
    backpressured,
    not_ready
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

    // --- request (async) ---
    async_result_t<received_t> request(const routing_id_t& routing_id, message_t message);
    async_result_t<received_t> request(const routing_id_t& routing_id, message_t message,
                                       std::chrono::milliseconds timeout);

    // --- request (callback) ---
    void request(const routing_id_t& routing_id, message_t message,
                 std::function<void(received_t)> on_reply,
                 std::function<void(error_t)> on_error,
                 std::chrono::milliseconds timeout);
    void request(const routing_id_t& routing_id, message_t message,
                 std::function<void(received_t)> on_reply,
                 std::function<void(error_t)> on_error);

    // --- try_request (async) ---
    async_result_t<received_t> try_request(const routing_id_t& routing_id, message_t message);
    async_result_t<received_t> try_request(const routing_id_t& routing_id, message_t message,
                                           std::chrono::milliseconds timeout);

    // --- try_request (callback) ---
    void try_request(const routing_id_t& routing_id, message_t message,
                     std::function<void(received_t)> on_reply,
                     std::function<void(error_t)> on_error,
                     std::chrono::milliseconds timeout);
    void try_request(const routing_id_t& routing_id, message_t message,
                     std::function<void(received_t)> on_reply,
                     std::function<void(error_t)> on_error);

    // --- reply ---
    void reply(const routing_id_t& routing_id, uint64_t request_seq, message_t message);
    send_result_t try_reply(const routing_id_t& routing_id, uint64_t request_seq,
                            message_t message);

    // --- receive ---
    received_t recv();
    maybe_t<received_t> try_recv();
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

    // --- request (async) ---
    async_result_t<received_t> request(message_t message);
    async_result_t<received_t> request(message_t message,
                                       std::chrono::milliseconds timeout);

    // --- request (callback) ---
    void request(message_t message,
                 std::function<void(received_t)> on_reply,
                 std::function<void(error_t)> on_error,
                 std::chrono::milliseconds timeout);
    void request(message_t message,
                 std::function<void(received_t)> on_reply,
                 std::function<void(error_t)> on_error);

    // --- try_request (async) ---
    async_result_t<received_t> try_request(message_t message);
    async_result_t<received_t> try_request(message_t message,
                                           std::chrono::milliseconds timeout);

    // --- try_request (callback) ---
    void try_request(message_t message,
                     std::function<void(received_t)> on_reply,
                     std::function<void(error_t)> on_error,
                     std::chrono::milliseconds timeout);
    void try_request(message_t message,
                     std::function<void(received_t)> on_reply,
                     std::function<void(error_t)> on_error);

    // --- receive ---
    received_t recv();
    maybe_t<received_t> try_recv();
    int on_receive(zlink_socket_msg_handler_fn handler, void* userdata = NULL);
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

    int on_event(monitor_event_handler_fn handler, void* userdata = NULL);
    monitor_event_t recv();
    maybe_t<monitor_event_t> try_recv();
    monitor_snapshot_t snapshot() const;
    int close() noexcept;
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

    int on_event(service_event_handler_fn handler, void* userdata = NULL);
    service_event_t recv();
    maybe_t<service_event_t> try_recv();
    monitor_snapshot_t snapshot() const;
    int close() noexcept;
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

    int bind(const std::string& pub_endpoint, const std::string& router_endpoint);
    int set_id(uint32_t registry_id);
    int add_peer(const std::string& peer_pub_endpoint);
    int set_heartbeat(uint32_t interval_ms, uint32_t timeout_ms);
    int set_broadcast_interval(uint32_t interval_ms);

    int status_snapshot(zlink_registry_status_t& out) const;
    int service_summary_snapshot(zlink_registry_service_summary_entry_t* entries,
                                 size_t* count,
                                 const zlink_registry_service_summary_filter_t* filter = NULL) const;
    int topology_snapshot(zlink_registry_topology_entry_t* entries, size_t* count) const;
    int topology_query(zlink_registry_topology_entry_t* entries, size_t* count,
                       const zlink_registry_topology_filter_t* filter) const;
    int member_peers(service_type service_type, const std::string& service_name,
                     zlink_member_peer_entry_t* entries, size_t* count) const;
    int member_peer_metadata(service_type service_type, const std::string& service_name,
                             service_role service_role, const std::string& endpoint,
                             message_t& metadata_out) const;

    int close();
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

    int connect_registry(const std::string& endpoint);
    int set_value(int64_t value);
    int get_value(int64_t* value_out) const;
    int set_metadata(const void* data, size_t size);
    int set_metadata(const std::vector<uint8_t>& bytes);
    int set_metadata(const std::string& text);
    int get_metadata(message_t& metadata_out) const;
    int member_peers(zlink_member_peer_entry_t* entries, size_t* count) const;
    int member_peer_metadata(service_role service_role, const std::string& endpoint,
                             message_t& metadata_out) const;

    service_monitor_handle_t monitor_open(service_monitor_event events = service_monitor_event::all);

    int close();
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

    int bind(const std::string& endpoint);
    std::string last_endpoint() const;
    int connect_peer(const std::string& endpoint);
    int disconnect_peer(const std::string& endpoint);
    int attach_discovery(discovery_t& discovery);

    int set_routing_id(const routing_id_t& routing_id);
    int get_routing_id(routing_id_t& out) const;

    int set_tls_server(const std::string& cert, const std::string& key,
                       bool require_client_cert = false);
    int set_tls_client(const std::string& ca_cert, const std::string& hostname = "",
                       bool trust_system = false);

    // --- options ---
    int set(socket_option_key_t<T> key, const T& value);
    int get(socket_option_key_t<T> key, T& value) const;

    // --- snapshots ---
    int status_snapshot(zlink_spot_node_status_t& out) const;
    int peers_snapshot(zlink_spot_node_peer_entry_t* entries, size_t* count) const;
    int peers_query(zlink_spot_node_peer_entry_t* entries, size_t* count,
                    const zlink_spot_node_peer_filter_t* filter) const;
    int subjects_snapshot(zlink_spot_node_subject_entry_t* entries, size_t* count,
                          const zlink_spot_node_subject_filter_t* filter = NULL) const;

    int close();
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

    // --- publish ---
    void publish(const std::string& topic, message_t& part);
    void publish(const std::string& topic, std::vector<message_t>& parts);
    void publish(const char* topic, message_t& part);
    void publish(const char* topic, std::vector<message_t>& parts);
    send_result_t try_publish(const std::string& topic, message_t& part);
    send_result_t try_publish(const std::string& topic, std::vector<message_t>& parts);
    send_result_t try_publish(const char* topic, message_t& part);
    send_result_t try_publish(const char* topic, std::vector<message_t>& parts);

    // --- subscribe ---
    subscribed_t subscribe();
    maybe_t<subscribed_t> try_subscribe();
    int set_subscription(const std::string& filter);
    int unset_subscription(const std::string& filter);
    int subscription_at(size_t index, std::string& filter_out, bool* is_pattern_out = NULL) const;
    int on_subscribe(zlink_subscribe_handler_fn handler, void* userdata = NULL);
    int on_send_ready(zlink_send_ready_handler_fn handler, void* userdata = NULL);

    // --- identity ---
    int set_routing_id(const routing_id_t& routing_id);
    int get_routing_id(routing_id_t& out) const;

    // --- routed send (spot → spot) ---
    void send_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                   message_t& part);
    void send_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                   std::vector<message_t>& parts);
    send_result_t try_send_spot(const routing_id_t& dest_node_rid,
                                const routing_id_t& dest_spot_rid,
                                message_t& part);
    send_result_t try_send_spot(const routing_id_t& dest_node_rid,
                                const routing_id_t& dest_spot_rid,
                                std::vector<message_t>& parts);

    // --- routed request (spot → spot, async) ---
    async_result_t<received_t> request_spot(const routing_id_t& dest_node_rid,
                                            const routing_id_t& dest_spot_rid,
                                            message_t message,
                                            std::chrono::milliseconds timeout = {});
    void request_spot(const routing_id_t& dest_node_rid,
                      const routing_id_t& dest_spot_rid,
                      message_t message,
                      std::function<void(received_t)> on_reply,
                      std::function<void(error_t)> on_error,
                      std::chrono::milliseconds timeout = {});

    // --- routed reply (spot → spot) ---
    void reply_spot(const routing_id_t& dest_node_rid, const routing_id_t& dest_spot_rid,
                    uint64_t request_seq, message_t message);

    // --- routed send (spot → router) ---
    void send_router(const routing_id_t& peer_rid, message_t& part);
    void send_router(const routing_id_t& peer_rid, std::vector<message_t>& parts);
    send_result_t try_send_router(const routing_id_t& peer_rid, message_t& part);
    send_result_t try_send_router(const routing_id_t& peer_rid,
                                  std::vector<message_t>& parts);

    // --- routed request (spot → router, async) ---
    async_result_t<received_t> request_router(const routing_id_t& peer_rid,
                                              message_t message,
                                              std::chrono::milliseconds timeout = {});
    void request_router(const routing_id_t& peer_rid,
                        message_t message,
                        std::function<void(received_t)> on_reply,
                        std::function<void(error_t)> on_error,
                        std::chrono::milliseconds timeout = {});

    // --- routed reply (spot → router) ---
    void reply_router(const routing_id_t& peer_rid, uint64_t request_seq,
                      message_t message);

    // --- routed receive ---
    received_t recv_routed();
    maybe_t<received_t> try_recv_routed();
    int on_routed_receive(zlink_spot_handler_fn handler, void* userdata = NULL);
    int on_dispatch_event(zlink_spot_dispatch_event_handler_fn handler,
                          void* userdata = NULL);

    // --- options ---
    int set(socket_option_key_t<T> key, const T& value);
    int get(socket_option_key_t<T> key, T& value) const;
    int set(pub_option_key_t<T> key, const T& value);
    int get(pub_option_key_t<T> key, T& value) const;
    int set(sub_option_key_t<T> key, const T& value);
    int get(sub_option_key_t<T> key, T& value) const;

    int close();
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

    int connect(const std::string& endpoint);
    int snapshot(zlink_registry_topology_entry_t* entries, size_t* count,
                 const zlink_registry_topology_filter_t* filter = NULL) const;

    int close();
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
    int add(SocketLike& socket, poll_event events, void* user = NULL);
    template<typename SocketLike>
    int modify(SocketLike& socket, poll_event events);
    template<typename SocketLike>
    int remove(SocketLike& socket);

    // --- file descriptor registration ---
    int add(zlink_fd_t fd, poll_event events, void* user = NULL);
    int modify(zlink_fd_t fd, poll_event events);
    int remove(zlink_fd_t fd);

    // --- wait ---
    int wait(poll_event_t* event, long timeout = -1);
    int wait_all(std::vector<poll_event_t>& events, long timeout = -1);

    int destroy() noexcept;
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

    int start(uint64_t interval_ns, uint64_t repeat_count);
    int stop();
    int recv(uint64_t* fire_count_out, int flags = 0);
    int set_handler(zlink_timer_handler_fn handler, void* userdata = NULL);
    int destroy();
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
