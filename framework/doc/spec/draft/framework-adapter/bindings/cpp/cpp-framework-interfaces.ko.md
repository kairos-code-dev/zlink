<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework C++ Policy](cpp-framework-policy.ko.md) | [다음: ZLink Framework C++ Interface Alignment](handler-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [channel](./cpp-channel-messaging.ko.md) | [SPOT](./cpp-spot.ko.md) | [STREAM](./cpp-stream.ko.md)

# Draft -- ZLink Framework C++ Interface Design

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` framework의 전반적인 public interface 방향을
> 정리한다.
> 이 문서는 `framework/doc/spec` 아래 공통 framework 정책을 상위 기준으로 따르고,
> C++ binding의 public 라이브러리 표면을 기반으로 framework 계층을 설계한다.

## 1. 설계 기준

`C++` framework는 기존 C++ binding을 대체하지 않는다. framework는 C++ binding 위에
올라가며, binding이 제공하는 typed public API를 내부 runtime substrate로 사용한다.

binding 기준은 아래 문서를 따른다.

- [C++ Binding Specification](/home/hep7/project/kairos/zlink/doc/spec/bindings/cpp/README.md)
- [C++ Codec Extension Specification](/home/hep7/project/kairos/zlink/doc/spec/bindings/cpp/codec.md)

framework public API는 `zlink::framework` namespace 아래에 둔다. binding의 public
타입은 framework 내부 구현과 일부 고급 extension point에서 사용할 수 있지만, 일반
사용자는 raw socket이나 poller를 직접 만지지 않아도 앱을 만들 수 있어야 한다.

## 2. Binding 대응표

framework 구현은 아래 C++ binding 타입을 기준으로 삼는다.

| Framework 개념 | Binding 기준 타입 | Framework에서의 역할 |
|----------------|------------------|----------------------|
| runtime context | `zlink::context_t` | app lifecycle 안에서 생성하고 종료한다. |
| message buffer | `zlink::message_t`, `zlink::multipart_t` | serializer가 typed payload를 변환하는 내부 메시지 단위다. |
| request/reply channel | `zlink::router_socket_t`, `zlink::dealer_socket_t` | channel server/client capability 구현에 사용한다. |
| pub/sub channel | `zlink::pub_socket_t`, `zlink::sub_socket_t` | topic publish/subscribe capability 구현에 사용한다. |
| stream ingress | `zlink::stream_socket_t` | STREAM packet/session capability 구현에 사용한다. |
| discovery | `zlink::service::discovery_t` | registry 기반 channel/spot 연결에 사용한다. |
| registry | `zlink::service::registry_t`, `zlink::service::registry_query_client_t` | embedded registry와 topology query에 사용한다. |
| spot node | `zlink::service::spot_node_t` | spot lifecycle과 channel attach를 관리한다. |
| spot | `zlink::service::spot_t` | spot publish, subscribe, direct routing, channel request/send에 사용한다. |
| async request | `zlink::async_result_t<T>` | framework future/pending request 구현의 기반이다. |
| codec extension | `zlink::codec::json` | JSON serializer 기본 구현에 사용한다. |

framework는 binding의 native handle, raw callback userdata, raw option key를 public
API로 올리지 않는다. 사용자가 필요한 것은 channel name, topic, typed payload,
handler, service lifetime, timeout 같은 framework 개념이다.

## 3. Header 와 Namespace

권장 public header layout은 아래와 같다.

```text
zlink/framework.hpp
zlink/framework/app.hpp
zlink/framework/services.hpp
zlink/framework/handlers.hpp
zlink/framework/messaging.hpp
zlink/framework/runtime.hpp
zlink/framework/spot.hpp
zlink/framework/serialization.hpp
zlink/framework/config.hpp
zlink/framework/logging.hpp
zlink/framework/observability.hpp
zlink/framework/hosting.hpp
zlink/framework/modules.hpp
```

모든 framework 타입은 `zlink::framework` namespace 아래에 둔다.

```cpp
namespace zlink::framework {

class app_t;
class service_collection_t;
class service_provider_t;
class handler_registry_t;
class serializer_registry_t;
class config_builder_t;
class logging_builder_t;
class metrics_builder_t;
class health_builder_t;
class zlink_builder_t;
class registry_builder_t;
class discovery_builder_t;
class channel_builder_t;
class spot_node_builder_t;
class stream_builder_t;
class message_bus_t;
class publisher_t;
class request_client_t;
class spot_context_t;
class send_ready_context_t;
class stream_header_t;
class stream_t;
class module_t;
class hosted_service_t;

} // namespace zlink::framework
```

## 4. App / Host

`app_t`는 framework의 가장 바깥 public type이다. 사용자는 `app_t::create()`로 앱을
만들고, services, handlers, zlink runtime을 구성한 뒤 `run`을 호출한다.

```cpp
namespace zlink::framework {

class app_t {
public:
    static app_t create();

    service_collection_t &services();
    handler_registry_t &handlers();
    serializer_registry_t &serializers();
    config_builder_t &config();
    logging_builder_t &logging();
    metrics_builder_t &metrics();
    health_builder_t &health();

    app_t &use_zlink(std::function<void(zlink_builder_t &)> configure);
    app_t &add_module(module_t &module);
    app_t &add_hosted_service(std::unique_ptr<hosted_service_t> service);

    int run(int argc, char **argv);
    void request_stop();
};

} // namespace zlink::framework
```

`run`은 MVP에서 `int`를 반환한다. 반환값은 process exit code로 사용할 수 있어야 한다.
handler 예외, runtime 오류, signal shutdown은 host가 수집하고 종료 경로를 닫는다.

## 5. DI

DI는 MVP에서 자체 container로 구현한다. C++ binding에는 DI 개념이 없으므로, framework
계층이 service lifetime과 handler owner resolve를 직접 제공한다.

```cpp
namespace zlink::framework {

enum class service_lifetime_t {
    singleton,
    transient
};

class service_provider_t {
public:
    template <typename T>
    T &get_required();

    template <typename T>
    std::optional<std::reference_wrapper<T>> get();
};

class service_collection_t {
public:
    template <typename T>
    service_collection_t &add_singleton();

    template <typename T>
    service_collection_t &add_singleton(std::unique_ptr<T> instance);

    template <typename T>
    service_collection_t &add_transient();

    template <typename T>
    service_collection_t &add_factory(
      std::function<std::unique_ptr<T>(service_provider_t &)> factory);
};

} // namespace zlink::framework
```

MVP 생성 규칙은 아래와 같다.

- `add_singleton<T>()`, `add_transient<T>()`는 기본 생성 가능한 타입만 자동 생성한다.
- 생성자 의존성이 있는 타입은 `add_factory<T>()`를 사용한다.
- handler owner는 service collection에 등록되어 있어야 한다.
- 등록되지 않은 handler owner를 framework가 암묵적으로 생성하지 않는다.
- `Boost.Ext.DI` 같은 외부 DI 라이브러리는 MVP 필수 dependency로 두지 않는다.

예시는 아래와 같다.

```cpp
app.services()
  .add_singleton<order_repository_t>()
  .add_factory<order_service_t>([](service_provider_t &services) {
      return std::make_unique<order_service_t>(
        services.get_required<order_repository_t>());
  })
  .add_transient<order_handler_t>();
```

## 6. Runtime Builder

runtime builder는 binding의 `zlink::context_t`, socket classes,
`zlink::service::discovery_t`, `zlink::service::spot_node_t` 생성을 숨긴다.

```cpp
namespace zlink::framework {

class zlink_builder_t {
public:
    zlink_builder_t &node(std::string node_name);
    zlink_builder_t &registry(std::function<void(registry_builder_t &)> configure);
    zlink_builder_t &discovery(std::function<void(discovery_builder_t &)> configure);
    zlink_builder_t &channel(std::string channel_name,
      std::function<void(channel_builder_t &)> configure);
    zlink_builder_t &spot_node(std::string spot_node_name,
      std::function<void(spot_node_builder_t &)> configure);
    zlink_builder_t &stream(std::string stream_name,
      std::function<void(stream_builder_t &)> configure);
};

class discovery_builder_t {
public:
    discovery_builder_t &connect_registry(std::string endpoint);
};

class registry_builder_t {
public:
    registry_builder_t &bind(std::string endpoint);
};

class stream_builder_t {
public:
    stream_builder_t &bind(std::string endpoint);
    stream_builder_t &packet_session(std::string session_name);
};

} // namespace zlink::framework
```

`zlink_builder_t`는 raw socket 생성 순서를 사용자가 기억하지 않게 해야 한다.
framework 내부는 아래 binding 타입을 조합한다.

- `zlink::context_t`
- `zlink::router_socket_t`
- `zlink::dealer_socket_t`
- `zlink::pub_socket_t`
- `zlink::sub_socket_t`
- `zlink::service::discovery_t`
- `zlink::service::spot_node_t`
- `zlink::stream_socket_t`

## 7. Channel Builder

channel은 framework에서 request/reply와 pub/sub capability를 묶는 이름이다.

```cpp
namespace zlink::framework {

class channel_builder_t {
public:
    channel_builder_t &enable_server();
    channel_builder_t &enable_server(
      std::function<void(server_capability_builder_t &)> configure);

    channel_builder_t &enable_client();
    channel_builder_t &enable_client(
      std::function<void(client_capability_builder_t &)> configure);

    channel_builder_t &enable_publisher();
    channel_builder_t &enable_publisher(
      std::function<void(publisher_capability_builder_t &)> configure);

    channel_builder_t &enable_subscriber();
    channel_builder_t &enable_subscriber(
      std::function<void(subscriber_capability_builder_t &)> configure);
};

class server_capability_builder_t {
public:
    server_capability_builder_t &bind(std::string endpoint);
};

class client_capability_builder_t {
public:
    client_capability_builder_t &connect(std::string endpoint);
    client_capability_builder_t &use_discovery();

    client_capability_builder_t &send_timeout(std::chrono::milliseconds timeout);
    client_capability_builder_t &request_timeout(std::chrono::milliseconds timeout);
    client_capability_builder_t &pending_queue_limit(std::size_t count);
};

class publisher_capability_builder_t {
public:
    publisher_capability_builder_t &bind(std::string endpoint);
    publisher_capability_builder_t &send_timeout(std::chrono::milliseconds timeout);
};

class subscriber_capability_builder_t {
public:
    subscriber_capability_builder_t &connect(std::string endpoint);
    subscriber_capability_builder_t &use_discovery();
};

} // namespace zlink::framework
```

내부 매핑은 아래와 같다.

| Capability | Binding 구현 기준 |
|------------|------------------|
| server | `zlink::router_socket_t` |
| client | `zlink::dealer_socket_t` |
| publisher | `zlink::pub_socket_t` |
| subscriber | `zlink::sub_socket_t` |

같은 channel 안에서도 capability별 연결 집합은 분리한다. 예를 들어
`orders.client`와 `orders.subscriber`는 같은 channel 이름을 공유하지만 서로 다른
socket과 연결 정책을 가진다.

따라서 `bind`, `connect`, `use_discovery` 같은 연결 설정은 channel 전체가 아니라
`server`, `client`, `publisher`, `subscriber` capability builder에 둔다.

## 8. Handler Registry

handler registry는 typed payload를 함수 수준에서 처리하게 하는 표면이다.

```cpp
namespace zlink::framework {

struct handler_options_t {
    std::optional<std::string> packet_name;
    std::optional<std::chrono::milliseconds> timeout;
    std::optional<std::size_t> max_concurrency;
    bool ordered = false;
};

class stream_header_t {
public:
    std::string_view session_id() const;
    std::string_view packet_name() const;
    std::string_view content_type() const;
    std::optional<std::string_view> correlation_id() const;
};

class stream_t {
public:
    virtual ~stream_t() = default;
    virtual std::string session_id() const = 0;
    virtual std::future<void> write_packet(
      const stream_header_t &header,
      const zlink::message_t &payload) = 0;
};

class handler_registry_t {
public:
    template <typename TEvent, typename TOwner>
    handler_registry_t &subscribe(
      std::string channel_name,
      std::string topic,
      void (TOwner::*method)(const TEvent &),
      handler_options_t options = {});

    template <typename TRequest, typename TReply, typename TOwner>
    handler_registry_t &request(
      std::string channel_name,
      std::string packet_name,
      TReply (TOwner::*method)(const TRequest &),
      handler_options_t options = {});

    template <typename TCommand, typename TOwner>
    handler_registry_t &send(
      std::string channel_name,
      std::string packet_name,
      void (TOwner::*method)(const TCommand &),
      handler_options_t options = {});

    handler_registry_t &send_raw(
      std::string channel_name,
      std::string packet_name,
      std::function<void(const zlink::message_t &)> handler,
      handler_options_t options = {});

    handler_registry_t &packet_stream(
      std::string stream_name,
      std::function<void(stream_t &, const stream_header_t &, const zlink::message_t &)> handler);
};

} // namespace zlink::framework
```

handler owner 타입은 service collection에서 resolve한다.

```cpp
app.services().add_transient<order_handler_t>();

app.handlers()
  .subscribe<order_created_t, order_handler_t>(
    "orders",
    "orders.created",
    &order_handler_t::on_created);
```

handler dispatch는 binding의 `zlink::message_t`와 `zlink::multipart_t`를 받은 뒤,
serializer를 통해 typed payload로 변환하고, DI에서 owner를 resolve한 다음 method를
호출한다.

STREAM handler는 일반 request/send/event handler와 분리한다. framework MVP는 packet
방식만 지원하고, header도 framework가 정의한 `stream_header_t`만 사용한다. raw stream
session과 사용자 정의 header framing은 MVP 범위에 넣지 않는다.

stream callback은 transport callback 안에서 직접 실행하지 않고 framework executor로
넘어간 뒤 실행한다. 같은 stream session의 packet/lifecycle callback은 직렬로 처리한다.

## 9. Messaging API

사용자 코드에서 raw socket 대신 주입받아 쓰는 messaging 표면은 아래와 같다.

```cpp
namespace zlink::framework {

struct send_options_t {
    std::optional<std::string> packet_name;
    std::optional<std::chrono::milliseconds> timeout;
};

struct request_options_t {
    std::optional<std::string> packet_name;
    std::optional<std::chrono::milliseconds> timeout;
};

class publisher_t {
public:
    template <typename TEvent>
    void publish(std::string_view channel_name,
      std::string_view topic,
      const TEvent &event,
      send_options_t options = {});
};

class request_client_t {
public:
    template <typename TCommand>
    void send(std::string_view channel_name, const TCommand &command,
      send_options_t options = {});

    template <typename TReply, typename TRequest>
    std::future<TReply> request(std::string_view channel_name,
      const TRequest &request,
      request_options_t options = {});
};

class message_bus_t {
public:
    publisher_t &publisher();
    request_client_t &client();
};

} // namespace zlink::framework
```

내부 구현은 binding의 `dealer_socket_t::request(...)`, `pub_socket_t::publish(...)`,
`service::spot_t::request_channel(...)`, `service::spot_t::send_channel(...)` 중
runtime topology에 맞는 경로를 선택한다. public API는 channel name과 typed payload를
기준으로 유지한다.

framework는 아래 서비스를 기본 등록한다. 사용자는 직접 생성하지 않고 DI에서
주입받아 사용할 수 있다.

- `message_bus_t`
- `publisher_t`
- `request_client_t`
- `serializer_registry_t`

## 10. Serialization

framework serializer는 binding codec extension 위에 얹는다. JSON 기본 구현은
`zlink::codec::json`과 `nlohmann/json`을 기준으로 한다.

```cpp
namespace zlink::framework {

class serializer_registry_t {
public:
    template <typename T>
    serializer_registry_t &add_json();

    template <typename T>
    serializer_registry_t &add(
      std::function<zlink::message_t(const T &)> serialize,
      std::function<T(const zlink::message_t &)> deserialize);
};

template <typename T>
class serializer_t {
public:
    zlink::message_t serialize(const T &value) const;
    T deserialize(const zlink::message_t &message) const;
};

} // namespace zlink::framework
```

framework public handler와 messaging API는 `zlink::message_t`를 일반 사용자에게
강요하지 않는다. 다만 고급 handler는 raw message를 직접 받을 수 있다.

```cpp
app.handlers()
  .send_raw("orders", "orders.raw", [](const zlink::message_t &message) {
      // raw payload path
  });
```

## 11. Spot Framework API

framework spot 표면은 binding의 `zlink::service::spot_node_t`와
`zlink::service::spot_t`를 기반으로 한다.

```cpp
namespace zlink::framework {

class spot_node_builder_t {
public:
    spot_node_builder_t &bind(std::string endpoint);
    spot_node_builder_t &use_discovery(std::string channel_name);
    spot_node_builder_t &attach_channel_client(std::string channel_name);
    spot_node_builder_t &attach_publisher(std::string channel_name);

    template <typename TSpot>
    spot_node_builder_t &add_spot(std::string spot_name);
};

class send_ready_context_t {
public:
    std::size_t pending_count() const;
    void resume_pending();
};

class spot_context_t {
public:
    zlink::routing_id_t spot_rid() const;

    spot_context_t &on_send_ready(
      std::function<void(send_ready_context_t &)> callback);

    template <typename TCommand>
    void send_to(zlink::routing_id_t node_rid,
      zlink::routing_id_t spot_rid,
      const TCommand &command,
      send_options_t options = {});

    template <typename TReply, typename TRequest>
    std::future<TReply> request_to(zlink::routing_id_t node_rid,
      zlink::routing_id_t spot_rid,
      const TRequest &request,
      request_options_t options = {});

    template <typename TEvent>
    void publish(std::string_view topic, const TEvent &event,
      send_options_t options = {});
};

} // namespace zlink::framework
```

`spot_context_t::publish(...)`는 현재 spot channel 안의 topic publish를 뜻하므로
별도 channel name을 받지 않는다. 직접 `routing_id_t`를 다루는 API는 spot-to-spot
경로에 제한한다. 일반 application handler와 client는 channel name과 topic을 먼저
사용한다.

## 12. Hosted Service 와 Module

hosted service는 app lifecycle에 묶이는 background worker다.

```cpp
namespace zlink::framework {

class hosted_service_t {
public:
    virtual ~hosted_service_t() = default;
    virtual void start(service_provider_t &services) = 0;
    virtual void stop() = 0;
};

class module_t {
public:
    virtual ~module_t() = default;
    virtual void configure_services(service_collection_t &services) {}
    virtual void configure_zlink(zlink_builder_t &zlink) {}
    virtual void configure_handlers(handler_registry_t &handlers) {}
};

} // namespace zlink::framework
```

module은 서비스 등록, runtime 구성, handler 등록을 한 기능 단위로 묶는다.

```cpp
class order_module_t final : public zlink::framework::module_t {
public:
    void configure_services(
      zlink::framework::service_collection_t &services) override
    {
        services.add_singleton<order_repository_t>();
        services.add_factory<order_service_t>([](auto &sp) {
            return std::make_unique<order_service_t>(
              sp.get_required<order_repository_t>());
        });
        services.add_transient<order_handler_t>();
    }

    void configure_handlers(
      zlink::framework::handler_registry_t &handlers) override
    {
        handlers.subscribe<order_created_t, order_handler_t>(
          "orders",
          "orders.created",
          &order_handler_t::on_created);
    }
};
```

## 13. Configuration 과 Logging

configuration은 JSON, environment variables, CLI args를 MVP 범위로 둔다.

```cpp
namespace zlink::framework {

class config_builder_t {
public:
    config_builder_t &load_json(std::string path);
    config_builder_t &load_env(std::string prefix);
    config_builder_t &load_cli(int argc, char **argv);
};

class logging_builder_t {
public:
    logging_builder_t &use_console();
    logging_builder_t &set_level(std::string level);
};

class metrics_builder_t {
public:
    metrics_builder_t &add_runtime_metrics();
};

class health_builder_t {
public:
    health_builder_t &add_zlink_runtime_check();
};

} // namespace zlink::framework
```

JSON loader는 `nlohmann/json`을 사용한다. YAML은 MVP 범위에 넣지 않는다.
metrics와 health 표면은 MVP에서 최소 형태만 둔다. exporter, label schema, tracing
hook은 별도 observability 초안에서 확정한다.

## 14. 전체 샘플

아래 샘플은 framework 사용자가 기대하는 최종 표면이다.

```cpp
#include <zlink/framework.hpp>

struct order_created_t {
    std::string order_id;
};

class order_repository_t {
public:
    void save_created(const order_created_t &event);
};

class order_handler_t final {
public:
    explicit order_handler_t(order_repository_t &repository)
      : repository_(repository)
    {
    }

    void on_created(const order_created_t &event)
    {
        repository_.save_created(event);
    }

private:
    order_repository_t &repository_;
};

int main(int argc, char **argv)
{
    auto app = zlink::framework::app_t::create();

    app.config()
      .load_json("appsettings.json")
      .load_env("ZLINK_")
      .load_cli(argc, argv);

    app.services()
      .add_singleton<order_repository_t>()
      .add_factory<order_handler_t>([](auto &services) {
          return std::make_unique<order_handler_t>(
            services.template get_required<order_repository_t>());
      });

    app.use_zlink([](auto &zlink) {
        zlink.node("order-node")
          .channel("orders", [](auto &channel) {
              channel.enable_server([](auto &server) {
                  server.bind("tcp://0.0.0.0:7001");
              });
              channel.enable_subscriber([](auto &subscriber) {
                  subscriber.use_discovery();
              });
          })
          .spot_node("orders-spot", [](auto &spot_node) {
              spot_node.bind("tcp://0.0.0.0:7101");
              spot_node.use_discovery("orders");
          });
    });

    app.handlers()
      .subscribe<order_created_t, order_handler_t>(
        "orders",
        "orders.created",
        &order_handler_t::on_created);

    return app.run(argc, argv);
}
```

이 샘플에서 사용자는 `zlink::router_socket_t`, `zlink::dealer_socket_t`,
`zlink::service::spot_node_t`를 직접 만들지 않는다. framework host가 C++ binding
타입을 생성하고 lifecycle을 관리한다.

## 15. 기존 C++ 세부 초안 정렬 항목

이 문서를 기준으로 기존 `C++` 세부 초안은 아래 방향으로 정리해야 한다.

- 이전 bootstrap 표기는 `app_t::create()`로 맞춘다.
- 이전 raw handler registration 중심 샘플은 `app.handlers()` 표면으로 맞춘다.
- raw `request_handler_t`, `send_handler_t`, `event_handler_t` 중심 표면은 고급 raw
  handler extension으로 내리고, 일반 샘플은 typed handler registry를 사용한다.
- 이전 channel client와 event publisher 문서는 `message_bus_t`,
  `request_client_t`, `publisher_t` 주입 표면과 맞춘다.
- handler와 publisher 표면은 channel name을 먼저 받고, topic 또는 packet name을
  그 다음에 받는 형태로 맞춘다.
- channel 연결 설정은 channel 전체가 아니라 capability builder에 둔다.
- SPOT 문서는 binding의 `service::spot_node_t`, `service::spot_t` 기능을 framework
  builder와 `spot_context_t`로 감싸는 방식으로 정리한다.
- SPOT discovery 설정은 `spot_node.use_discovery(channel_name)`처럼 active SPOT
  channel view 이름을 명시하는 형태로 맞춘다.
