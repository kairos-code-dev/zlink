[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [channel](./cpp-channel-messaging.ko.md) | [SPOT](./cpp-spot.ko.md) | [STREAM](./cpp-stream.ko.md) | [Monitoring](./cpp-monitoring.ko.md) | [Registry](./cpp-registry.ko.md)

# Draft -- ZLink Framework C++ Interface Catalog

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` host/runtime이 노출할 공용 타입과 registration
> 표면을 한 곳에 모은 기준 문서다.

## 0. 공통 정책 반영

이 문서는 [Framework Adapter 정책](../../policy/README.ko.md)과
[doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
규칙을 그대로 따른다. 따라서 `C++` 문서에서는 아래를 기본으로 본다.

- 메서드는 `snake_case`, 타입은 `_t` 접미사를 쓴다.
- 개념 이름은 공통 정책과 맞춘다. 예를 들어 `send`, `request`, `publish`,
  `send_to`, `request_to`, `send_channel`, `request_channel` 같은 action 이름을
  유지한다.
- blocking과 non-blocking을 `send_no_wait` 같은 별도 동사 이름으로 나누지 않는다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다.

## 1. Host 와 Context

```cpp
namespace zlink::framework {

struct send_options_t {
    std::optional<std::string> packet_name;
    bool dont_wait{false};
};

struct request_options_t {
    std::optional<std::string> packet_name;
    std::optional<std::chrono::milliseconds> timeout;
};

struct routed_peer_entry_t {
    std::string target_rid;
    std::string endpoint;
};

struct discovery_config_t {
    std::vector<std::string> registry_endpoints;
};

struct client_capability_options_t {
    std::vector<std::string> manual_connections;
};

struct subscriber_capability_options_t {
    std::vector<std::string> manual_connections;
};

struct spot_router_capability_options_t {
    std::vector<routed_peer_entry_t> manual_connections;
};

struct spot_pubsub_capability_options_t {
    std::vector<std::string> manual_connections;
};

struct spot_channel_client_capability_options_t {
    std::vector<std::string> manual_connections;
};

struct spot_publisher_client_capability_options_t {
    std::vector<std::string> manual_connections;
};

struct spot_factory_entry_t {
    std::string spot_name;
    std::string spot_type_name;
};

struct spot_node_options_t {
    std::optional<std::string> bind;
    std::optional<spot_router_capability_options_t> router;
    std::optional<spot_pubsub_capability_options_t> pub_sub;
    std::map<std::string, spot_channel_client_capability_options_t> channel_clients;
    std::map<std::string, spot_publisher_client_capability_options_t> spot_publishers;
    std::vector<spot_factory_entry_t> spot_factories;
};

struct handler_context_t {
    std::optional<std::string> channel_name;
    std::optional<std::string> packet_name;
    std::optional<std::string> content_type;
    std::optional<std::string> correlation_id;
};

} // namespace zlink::framework
```

## 2. Handler

```cpp
namespace zlink::framework {

class request_handler_t {
public:
    virtual ~request_handler_t() = default;
    virtual message_t handle(
      const message_t &request,
      const request_context_t &context) = 0;
};

class send_handler_t {
public:
    virtual ~send_handler_t() = default;
    virtual void handle(
      const message_t &message,
      const send_context_t &context) = 0;
};

class event_handler_t {
public:
    virtual ~event_handler_t() = default;
    virtual void handle(
      const message_t &event,
      const event_context_t &context) = 0;
};

enum class stream_session_error_t {
    internal = 0,
    transport_error,
    handshake_failed
};

struct stream_error_t {
    stream_session_error_t error;
    int internal_errno;
};

class stream_t {
public:
    virtual ~stream_t() = default;
    virtual std::string session_id() const = 0;
    virtual std::optional<routing_id_t> routing_id() const = 0;
    virtual bool write(
      const message_t &payload,
      send_flags_t flags = send_flags_t::none) = 0;
    virtual bool write_packet(
      const message_t &header,
      const message_t &body,
      send_flags_t flags = send_flags_t::none) = 0;
};

class packet_stream_session_t {
public:
    virtual ~packet_stream_session_t() = default;
    virtual void on_connected(stream_t &stream) = 0;
    virtual void on_disconnected(stream_t &stream) = 0;
    virtual void on_error(stream_t &stream, const stream_error_t &error) = 0;
    virtual void on_packet(
      stream_t &stream,
      const message_t &header,
      const message_t &body) = 0;
};

class raw_stream_session_t {
public:
    virtual ~raw_stream_session_t() = default;
    virtual void on_connected(stream_t &stream) = 0;
    virtual void on_disconnected(stream_t &stream) = 0;
    virtual void on_error(stream_t &stream, const stream_error_t &error) = 0;
    virtual void on_raw(stream_t &stream, const message_t &payload) = 0;
};

enum class socket_event_kind_t {
    connected = 0,
    connection_ready,
    disconnected,
    handshake_failed,
    peer_admission_changed,
    closed,
    internal
};

struct socket_event_t {
    std::string source_name;
    socket_event_kind_t event;
};

enum class discovery_event_kind_t {
    service_up = 0,
    service_down,
    providers_changed,
    peer_admission_changed,
    error,
    closed,
    internal
};

struct discovery_event_t {
    std::string source_name;
    discovery_event_kind_t event;
};

enum class registry_event_kind_t {
    status_changed = 0,
    topology_changed,
    service_summary_changed
};

struct registry_event_t {
    std::string source_name;
    registry_event_kind_t event;
};

enum class spot_event_kind_t {
    status_changed = 0,
    peers_changed,
    subjects_changed
};

struct spot_event_t {
    std::string source_name;
    spot_event_kind_t event;
};

class monitoring_options_t {
public:
    virtual ~monitoring_options_t() = default;
    virtual void add_socket_events(
      std::string_view source_name,
      std::uint32_t events) = 0;
    virtual void add_discovery_events(
      std::string_view source_name) = 0;
    virtual void add_registry_events(
      std::string_view source_name,
      std::chrono::milliseconds interval) = 0;
    virtual void add_spot_events(
      std::string_view source_name,
      std::chrono::milliseconds interval) = 0;
};

template <typename TEvent>
class runtime_event_handler_t {
public:
    virtual ~runtime_event_handler_t() = default;
    virtual void handle(const TEvent &event) = 0;
};

} // namespace zlink::framework
```

## 3. Client

```cpp
namespace zlink::framework {

class client_t {
public:
    virtual ~client_t() = default;

    virtual bool send(
      std::string_view channel_name,
      const message_t &message,
      const send_options_t &options = {}) = 0;

    virtual message_t request(
      std::string_view channel_name,
      const message_t &request,
      const request_options_t &options = {}) = 0;
};

class spot_client_t {
public:
    virtual ~spot_client_t() = default;

    virtual bool send_channel(
      std::string_view channel_name,
      const message_t &message,
      const send_options_t &options = {}) = 0;

    virtual message_t request_channel(
      std::string_view channel_name,
      const message_t &request,
      const request_options_t &options = {}) = 0;

    virtual bool send_to(
      routing_id_t target_rid,
      routing_id_t spot_rid,
      const message_t &message,
      const send_options_t &options = {}) = 0;

    virtual message_t request_to(
      routing_id_t target_rid,
      routing_id_t spot_rid,
      const message_t &request,
      const request_options_t &options = {}) = 0;

    virtual bool publish(
      std::string_view topic,
      const message_t &message,
      const send_options_t &options = {}) = 0;
};

class spot_publisher_client_t {
public:
    virtual ~spot_publisher_client_t() = default;

    virtual bool publish(
      std::string_view channel_name,
      std::string_view topic,
      const message_t &message,
      const send_options_t &options = {}) = 0;
};

class event_publisher_t {
public:
    virtual ~event_publisher_t() = default;

    virtual bool publish(
      std::string_view channel_name,
      std::string_view topic,
      const message_t &message,
      const send_options_t &options = {}) = 0;
};

struct spot_create_result_t {
    routing_id_t spot_rid;
    std::string spot_name;
    bool created;
};

struct spot_info_t {
    routing_id_t spot_rid;
    std::string spot_name;
};

class spot_manager_t {
public:
    virtual ~spot_manager_t() = default;

    virtual spot_create_result_t create(std::string_view spot_name) = 0;
    virtual spot_create_result_t create(
      std::string_view spot_name,
      routing_id_t spot_rid) = 0;
    virtual std::optional<spot_info_t> get(routing_id_t spot_rid) const = 0;
    virtual std::vector<spot_info_t> list() const = 0;
    virtual bool remove(routing_id_t spot_rid) = 0;
};

class timer_t {
public:
    virtual ~timer_t() = default;
    virtual bool is_disposed() const = 0;
    virtual void cancel() = 0;
};

class spot_t {
public:
    virtual ~spot_t() = default;
    virtual routing_id_t spot_rid() const = 0;
    virtual std::unique_ptr<timer_t> add_timer(
      std::string name,
      std::chrono::milliseconds period,
      std::string handler_type_name) = 0;
};

} // namespace zlink::framework
```

packet key 해석 규칙은 아래 순서를 기본으로 본다.

1. `options.packet_name`
2. payload registration metadata
3. payload 타입 이름

## 4. Host

```cpp
namespace zlink::framework {

class manual_peer_list_builder_t {
public:
    virtual ~manual_peer_list_builder_t() = default;
    virtual void connect(std::string endpoint) = 0;
};

class manual_router_peer_list_builder_t {
public:
    virtual ~manual_router_peer_list_builder_t() = default;
    virtual void connect(std::string endpoint) = 0;
};

class client_capability_builder_t {
public:
    virtual ~client_capability_builder_t() = default;
    virtual void use_manual_connections(
      std::function<void(manual_peer_list_builder_t &)> configure) = 0;
};

class subscriber_capability_builder_t {
public:
    virtual ~subscriber_capability_builder_t() = default;
    virtual void use_manual_connections(
      std::function<void(std::vector<std::string> &)> configure) = 0;
};

class spot_router_capability_builder_t {
public:
    virtual ~spot_router_capability_builder_t() = default;
    virtual void use_manual_connections(
      std::function<void(manual_router_peer_list_builder_t &)> configure) = 0;
};

class spot_pubsub_capability_builder_t {
public:
    virtual ~spot_pubsub_capability_builder_t() = default;
    virtual void use_manual_connections(
      std::function<void(manual_peer_list_builder_t &)> configure) = 0;
};

class spot_channel_client_capability_builder_t {
public:
    virtual ~spot_channel_client_capability_builder_t() = default;
    virtual void use_manual_connections(
      std::function<void(manual_peer_list_builder_t &)> configure) = 0;
};

class spot_publisher_client_capability_builder_t {
public:
    virtual ~spot_publisher_client_capability_builder_t() = default;
    virtual void use_manual_connections(
      std::function<void(manual_peer_list_builder_t &)> configure) = 0;
};

class spot_node_builder_t {
public:
    virtual ~spot_node_builder_t() = default;
    virtual void bind(std::string endpoint) = 0;
    virtual void enable_router() = 0;
    virtual void enable_router(
      std::function<void(spot_router_capability_builder_t &)> configure) = 0;
    virtual void enable_pub_sub() = 0;
    virtual void enable_pub_sub(
      std::function<void(spot_pubsub_capability_builder_t &)> configure) = 0;
    virtual void attach_channel_client(std::string channel_name) = 0;
    virtual void attach_channel_client(
      std::string channel_name,
      std::function<void(spot_channel_client_capability_builder_t &)> configure) = 0;
    virtual void attach_spot_publisher_client(std::string channel_name) = 0;
    virtual void attach_spot_publisher_client(
      std::string channel_name,
      std::function<void(spot_publisher_client_capability_builder_t &)> configure) = 0;
    virtual void add_spot_factory(
      std::string spot_name,
      std::string spot_type_name) = 0;
};

class channel_builder_t {
public:
    virtual ~channel_builder_t() = default;
    virtual void enable_server() = 0;
    virtual void enable_client() = 0;
    virtual void enable_client(
      std::function<void(client_capability_builder_t &)> configure) = 0;
    virtual void enable_publisher() = 0;
    virtual void enable_subscriber() = 0;
    virtual void enable_subscriber(
      std::function<void(subscriber_capability_builder_t &)> configure) = 0;
};

class app_t {
public:
    static app_t build();

    app_t &add_channel(
      std::string channel_name,
      std::function<void(channel_builder_t &)> configure);
    app_t &use_discovery(discovery_config_t config);
    app_t &use_spot_discovery(
      std::string channel_name,
      discovery_config_t config);
    app_t &add_spot_node(
      std::string spot_node_name,
      std::function<void(spot_node_builder_t &)> configure);
    app_t &add_request_handler(std::string packet_name, request_handler_t &handler);
    app_t &add_send_handler(std::string packet_name, send_handler_t &handler);
    app_t &run();
};

} // namespace zlink::framework
```

```cpp
namespace zlink::framework {

class channel_client_connections_t {
public:
    virtual ~channel_client_connections_t() = default;
    virtual void connect(std::string endpoint) = 0;
    virtual void disconnect(std::string_view endpoint) = 0;
    virtual std::vector<std::string> list_connections() const = 0;
};

class channel_subscriber_connections_t {
public:
    virtual ~channel_subscriber_connections_t() = default;
    virtual void connect(std::string endpoint) = 0;
    virtual void disconnect(std::string_view endpoint) = 0;
    virtual std::vector<std::string> list_connections() const = 0;
};

class channel_connection_manager_t {
public:
    virtual ~channel_connection_manager_t() = default;
    virtual channel_client_connections_t &get_client(std::string_view channel_name) = 0;
    virtual channel_subscriber_connections_t &get_subscriber(std::string_view channel_name) = 0;
};

class spot_router_connections_t {
public:
    virtual ~spot_router_connections_t() = default;
    virtual void connect(std::string endpoint) = 0;
    virtual void disconnect(std::string_view endpoint) = 0;
    virtual std::vector<std::string> list_connections() const = 0;
};

class spot_pubsub_connections_t {
public:
    virtual ~spot_pubsub_connections_t() = default;
    virtual void connect(std::string endpoint) = 0;
    virtual void disconnect(std::string_view endpoint) = 0;
    virtual std::vector<std::string> list_connections() const = 0;
};

class spot_channel_client_connections_t {
public:
    virtual ~spot_channel_client_connections_t() = default;
    virtual void connect(std::string endpoint) = 0;
    virtual void disconnect(std::string_view endpoint) = 0;
    virtual std::vector<std::string> list_connections() const = 0;
};

class spot_publisher_client_connections_t {
public:
    virtual ~spot_publisher_client_connections_t() = default;
    virtual void connect(std::string endpoint) = 0;
    virtual void disconnect(std::string_view endpoint) = 0;
    virtual std::vector<std::string> list_connections() const = 0;
};

class spot_connection_manager_t {
public:
    virtual ~spot_connection_manager_t() = default;
    virtual spot_router_connections_t &get_router(std::string_view spot_node_name) = 0;
    virtual spot_pubsub_connections_t &get_pub_sub(std::string_view spot_node_name) = 0;
    virtual spot_channel_client_connections_t &get_channel_client(
      std::string_view spot_node_name,
      std::string_view channel_name) = 0;
    virtual spot_publisher_client_connections_t &get_spot_publisher_client(
      std::string_view spot_node_name,
      std::string_view channel_name) = 0;
};

} // namespace zlink::framework
```

위 builder 타입들이 `cpp-channel-messaging.ko.md`와
`channel-messaging-samples.ko.md`, `cpp-spot.ko.md`, `spot-samples.ko.md`에서 쓰는
`add_channel(...)`, `channel.enable_client(...)`,
`client.use_manual_connections(...)`, `add_spot_node(...)`의 기준 표면이다.

일반 channel client manual 연결은 endpoint 집합만 다루고, `SPOT` router manual
연결도 같은 방식으로 endpoint 집합만 등록한다. 이 초안에서는 `connect(...)`
호출 시 remote router id를 따로 받지 않는다. `spot_manager_t`는 등록된
`spot_name`으로 factory를 고르고, `get(...)`와 `list()`는 runtime이 들고 있는
`spot_rid -> spot_name` 매핑을 다시 보는 용도다.

## 5. 중요한 규칙

- 같은 capability는 자동 연결과 수동 연결 중 하나만 선택한다.
- 수동 연결은 `channel + capability` 단위로 관리한다.
- manual capability는 startup 등록뿐 아니라 런타임 `connect`, `disconnect`,
  `list_connections`도 지원해야 한다.
- 일반 channel messaging의 handler dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)` 수신은 reply correlation 경로로 본다.
- `ROUTER -> DEALER` 임의 push는 channel messaging 공용 계약에 넣지 않는다.
