<!-- framework-adapter-nav:start -->
[스펙 목차](README.ko.md) | [이전: C++ 시스템 구조](01-system-structure.ko.md) | [다음: C++ HTTP Hosting](60-http-hosting.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../common/README.ko.md)


# Spec -- ZLink Framework C++ Interface Design

> 이 문서는 ZLink Framework 10.0.0의 C++ 정식 public interface 계약이다.
> 이 문서는 `framework/doc/framework/spec` 아래 공통 framework 정책을 상위 기준으로 따르고,
> C++ binding의 public 라이브러리 표면을 기반으로 framework 계층을 설계한다.

## 1. 계약 기준

`C++` framework는 기존 C++ binding을 대체하지 않는다. framework는 C++ binding 위에
올라가며, binding이 제공하는 typed public API를 내부 runtime substrate로 사용한다.

기능과 사용성 개념은 framework 공통 스펙을 기준으로 맞춘다. 즉 app/host, DI scope,
handler registry, channel messaging, `STREAM`, `SPOT`, ActorGateway session relay,
monitoring, graceful shutdown은 같은 모델을 제공하고, C++ public API는 C++20 coroutine,
callback, RAII ownership에 맞게 표현만 바꾼다.

binding 기준은 아래 문서를 따른다.

- [C++ Binding Specification](../../../../../../../bindings/doc/spec/cpp/README.md)
- [C++ Codec Extension Specification](../../../../../../../bindings/doc/spec/cpp/codec.md)

framework public API는 `zlink::framework` namespace 아래에 둔다. 설치되는 public header에는
정식 contract와 명시적인 extension point만 포함한다. 일반 사용자는 raw socket이나 poller를 직접
다루지 않고 application을 구성할 수 있어야 한다.

## 2. Binding public dependency 경계

Framework package는 C++ binding의 public API만 의존한다. Application은 framework 기능을 사용할 때
binding의 native handle, raw callback userdata, raw option key, socket과 poller를 직접 다루지 않는다.
공개 handler와 client에는 ChannelName, topic, typed payload, timeout과 lifecycle처럼 framework
계약에 정의된 값만 나타난다.

사용자가 binding 값을 직접 넘길 수 있는 곳은 `message_t`처럼 정식 signature가 명시한 payload
경계로 제한한다. 그 밖의 binding 타입은 framework public signature에 나타나지 않는다.

## 3. Header 와 Namespace

권장 public header layout은 아래와 같다. `contracts/*` 아래 header가 `.NET`
`Contracts/*`에 대응하는 실제 public contract owner이고, `zlink/framework.hpp`는
사용자가 전체 framework 표면을 한 번에 include할 수 있는 facade다. 한 줄짜리
`zlink/framework/*.hpp` compatibility wrapper는 유지하지 않는다.

```text
zlink/framework.hpp
zlink/framework/version.hpp
zlink/framework/contracts/actors/*.hpp
zlink/framework/contracts/channels/*.hpp
zlink/framework/contracts/codecs/*.hpp
zlink/framework/contracts/configuration/*.hpp
zlink/framework/contracts/dispatch/*.hpp
zlink/framework/contracts/errors/*.hpp
zlink/framework/contracts/eventing/*.hpp
zlink/framework/contracts/handlers/*.hpp
zlink/framework/contracts/http/*.hpp
zlink/framework/contracts/locations/*.hpp
zlink/framework/contracts/messaging/*.hpp
zlink/framework/contracts/spots/*.hpp
zlink/framework/contracts/streams/*.hpp
zlink/framework/contracts/timers/*.hpp
zlink/framework/contracts/workers/*.hpp
```

`zlink/framework/runtime.hpp` 같은 public header는 제공하지 않는다. public API에는 `app_t`,
`channel_client_t`, `spot_context_t`처럼 사용자가 이해하는 계약 이름만 노출한다.

`bindings/cpp`보다 framework 쪽의 분리를 더 강하게 잡는다. binding은 zlink core의
native 개념을 C++로 안전하게 감싸는 계층이지만, framework는 application contract를
제공하는 계층이다. 그래서 framework contract header가 binding public 타입을 내부
substrate로 참조할 수는 있어도, native socket owner, CAPI dispatch callback, raw recv
순서, frame codec 구현을 public contract로 끌어올리면 안 된다.

public header에 template 구현이 필요한 경우에는 `contracts/detail/*`만 사용한다.
이 detail 영역은 type trait, concept check, facade forwarding을 위한 곳이며,
runtime 구현을 숨겨 넣는 장소가 아니다.

이 구조는 `.NET`의 public interface를 C++ pure virtual class로 모두 옮긴다는 뜻이
아니다. C++ public API는 concrete facade와 value type을 적극적으로 사용할 수 있다.
다만 facade의 멤버, 생성자, method signature가 runtime 구현 타입을 노출하지 않아야 한다.
runtime 객체를 가리켜야 하는 public facade는 PIMPL, type-erased state, shared internal
state 같은 방식으로 구현을 숨긴다. 사용자 확장점만 abstract interface 또는 concept
contract로 둔다.

설치되는 header는 contract와 facade만 포함한다. 구현 전용 header는 install 결과와 package의 public
include 경로에 포함하지 않는다.

public type을 만들 때는 아래 질문에 모두 답해야 한다.

| 질문 | public contract에 둘 수 있는 경우 | runtime에 숨겨야 하는 경우 |
|------|----------------------------------|-----------------------------|
| 사용자가 직접 구현하는가? | handler, filter, serializer, hosted service처럼 구현 대상이면 둔다. | framework가 내부에서만 구현하면 숨긴다. |
| 사용자가 값을 조합하는가? | option, builder, typed result처럼 조합 대상이면 둔다. | queue node, dispatch token, recv state처럼 조합하지 않으면 숨긴다. |
| 공통 기능을 사용자에게 제공하는가? | 같은 기능 축의 public 계약이면 C++ contract로 둔다. | runtime 실행에만 필요한 타입이면 숨긴다. |
| native 실행 순서를 드러내는가? | 드러내지 않으면 facade로 둘 수 있다. | poll/recv/drain 순서가 보이면 숨긴다. |

### 3.1 공개 계약 경계

C++ 공개 header는 사용자가 구성하거나 호출하는 타입과 결과만 정의한다. socket owner, queue,
pending operation 저장소, dispatch 순서와 native transport adapter는 공개 signature에 노출하지
않는다. 공개 facade가 상태를 유지해야 할 때도 사용자는 그 상태의 자료구조나 처리 순서를 알 필요가
없어야 한다.

공개 `route_client_t`와 `route_send_call_t`는 node와 Spot을 대상으로 하는 typed 호출을
제공한다. node 대상 호출은 `send_to_node(...)`와 `request_to_node(...)`, Spot 대상 호출은
`send_to_spot(handle, ...)`와 `request_to_spot(handle, ...)`을 사용한다. request 계열은
`channel_request_call_t`을 반환한다. 사용자는 target RID 또는 불투명한 Spot handle과 typed
payload만 넘기며, routing envelope와 serializer 선택은 framework가 처리한다.

일반 request는 `request_to_node(...).timeout(...).async<TReply>()`로 typed reply를 받는다.
`.metadata(key, value)`로 설정한 값은 application metadata 계약에 따라 snapshot되며, transport
세부와 correlation 상태는 공개 API에 드러나지 않는다.

native result와 error envelope는 다음 공개 오류 의미로 변환한다.

| native/error code | C++ error kind | retriable |
|-------------------|----------------|-----------|
| `timed_out`, `timeout` | 경계 timeout — public enum 값이 아니라 `framework_exception_t`의 `code() == std::errc::timed_out` 값(§15.9) | no |
| `not_connected`, `route_not_connected` | `route_not_connected` | yes |
| `not_found`, `request_target_not_found` | `request_target_not_found` | no |
| `rejected`, `request_rejected` | `request_rejected` | no |
| `busy`, `conflict` | `request_rejected` | yes |
| `protocol_error`, `request_protocol_error` | `request_protocol_error` | no |
| `handler_not_found` | `handler_not_found` | no |

이 표는 request completion과 error envelope reply에 같은 의미로 적용한다.

DTO message name은 `static constexpr const char *packet_name`을 우선 사용한다. framework
handler 등록과 Stream Connector의 send, request와 on 기본 이름은 이 값을 읽는다. 이름이 없는
타입은 C++ type name을 사용할 수 있지만, 공개 sample과 정식 DTO는 명시적인 packet name을
가져야 한다.
### 3.2 C++ 공개 header 제약

C++는 설치된 header가 곧 공개 표면이므로 다음 규칙을 지킨다.

- template header에는 type check와 공개 facade forwarding만 둔다.
- public class의 state는 공개 계약 타입만 사용하며 native socket, queue와 pending operation 타입을
  노출하지 않는다.
- JSON, MessagePack, Protobuf와 같은 선택 dependency 타입은 해당 codec extension의 공개 계약에만
  나타날 수 있다.
- contract test는 설치된 public header만 include한다.
- public inline 함수는 공개 validation과 forwarding을 넘어서 transport state를 조작하지 않는다.

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
class dispatch_options_t;
class codec_options_builder_t;
class metadata_policy_builder_t;
class mesh_node_builder_t;
class mesh_channel_builder_t;
class location_store_t;
struct location_options_t;
struct worker_options_t;
class stream_builder_t;
class message_bus_t;
class message_t;
class publisher_t;
class request_client_t;
class publish_call_t;
class spot_publisher_client_t;
class spot_context_t;
class stream_dispatch_context_t;
class stream_error_t;
class stream_t;
class packet_stream_session_t;
class module_t;
class hosted_service_t;
enum class message_flow_log_mode_t;
struct message_flow_event_t;
class message_flow_observer_t;
struct runtime_error_event_t;
class runtime_error_sink_t;

} // namespace zlink::framework
```

## 4. App / Host

`app_t`는 framework의 가장 바깥 public type이다. 사용자는 `app_t::create()`로 앱을
만들고, `add_zlink_framework(...)`에서 services, handlers, zlink runtime을 한 번에
구성한 뒤 `run`을 호출한다. 낮은 수준의 runtime builder는 일반 애플리케이션 표면에
직접 노출하지 않는다.

```cpp
namespace zlink::framework {

enum class drain_force_reason_t {
    deadline_exceeded,
    draining_state_publish_failed,
    owner_cleanup_failed,
    teardown_failed
};
struct drained_t {};
struct force_stopped_t { drain_force_reason_t reason; };
using drain_result_t = std::variant<drained_t, force_stopped_t>;

class app_t {
public:
    static app_t create();

    config_builder_t &config();
    logging_builder_t &logging();
    monitoring_builder_t &monitoring();
    app_advanced_t advanced();

    app_t &add_module(module_t &module);
    app_t &add_zlink_framework(
      std::function<void(zlink_framework_options_t &)> configure);
    template <typename TModule, typename... TArgs>
    app_t &add_zlink_framework(TArgs &&...args);
    app_t &add_hosted_service(std::unique_ptr<hosted_service_t> service);

    task_t<drain_result_t> drain(std::chrono::milliseconds deadline);
    task_t<drain_result_t> drain();
    task_t<drain_result_t> await_drained();
    bool is_ready() const;
    app_t &set_message_flow_mode(message_flow_log_mode_t mode) noexcept;

    int run(int argc, char **argv);
    void stop();
    void request_stop();
};

class app_advanced_t {
public:
    service_collection_t &services();
    handler_registry_t &handlers();
    zlink_builder_t &zlink() noexcept;
};

} // namespace zlink::framework
```

`app_advanced_t`는 framework extension, contract test, 상위 options로 승격하지 않은
낮은 수준 기능을 위한 탈출구다. Bingo, TicTacToe 같은 일반 샘플은 이 표면을 사용하지
않고 `add_zlink_framework(...)`만 사용해야 한다.

`run`은 `int`를 반환한다. 반환값은 process exit code로 사용할 수 있어야 한다.
handler 예외, runtime 오류, signal shutdown은 host가 수집하고 종료 경로를 닫는다.

## 5. DI

DI는 자체 container로 구현한다. C++ binding에는 DI 개념이 없으므로, framework
계층이 service lifetime과 handler owner resolve를 직접 제공한다.

```cpp
namespace zlink::framework {

enum class service_lifetime_t {
    singleton,
    scoped,
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

    template <typename T, typename... TDependencies>
    service_collection_t &add_singleton();

    template <typename T>
    service_collection_t &add_singleton(std::unique_ptr<T> instance);

    template <typename T>
    service_collection_t &add_scoped();

    template <typename T, typename... TDependencies>
    service_collection_t &add_scoped();

    template <typename T>
    service_collection_t &add_transient();

    template <typename T, typename... TDependencies>
    service_collection_t &add_transient();

    template <typename T>
    service_collection_t &add_factory(
      std::function<std::unique_ptr<T>(service_provider_t &)> factory);
};

} // namespace zlink::framework
```

기본 생성 규칙은 아래와 같다.

- `add_singleton<T>()`, `add_transient<T>()`는 기본 생성 가능한 타입만 자동 생성한다.
- 생성자 의존성이 있는 타입은 `add_singleton<T, Dep1, Dep2>()`,
  `add_scoped<T, Dep1, Dep2>()`, `add_transient<T, Dep1, Dep2>()`처럼 의존 타입을 명시한다.
  framework는 `service_provider_t`에서 `Dep1`, `Dep2`를 resolve한 뒤 `T(Dep1 &, Dep2 &)`를
  호출한다.
- `add_scoped<T>()`는 framework가 소유하는 scope 안에서만 resolve한다.
- 복잡한 외부 객체 생성이나 조건부 생성이 필요한 경우에만 `add_factory<T>()`를 사용한다.
- handler owner는 service collection에 등록되어 있어야 한다.
- 등록되지 않은 handler owner를 framework가 암묵적으로 생성하지 않는다.
- `Boost.Ext.DI` 같은 외부 DI 라이브러리는 public dependency로 두지 않는다.

`scoped` lifetime은 zlink core 기능이 아니라 framework가 소유하는 DI lifetime이다. `.NET`
framework가 `IServiceScope`를 만들어 handler dispatch, STREAM session, Spot activation
수명에 붙이는 것처럼, C++ framework도 자체 DI container에서 같은 scope 경계를 만든다.
channel handler는 dispatch마다 scope를 만들고, STREAM session은 session scope를 가지며,
Spot과 Entry Spot은 activation scope를 가진다. actor factory는 actor creation scope에서
resolve하고, actor instance 자체는 actor runtime이 소유한다.

예시는 아래와 같다.

```cpp
options.services()
  .add_singleton<order_repository_t>()
  .add_transient<order_service_t, order_repository_t>()
  .add_transient<order_handler_t, order_service_t>();
```

## 6. RouteMesh 등록

RouteMesh builder는 물리 mesh 하나와 그 MeshNode를 등록한다. 논리 channel은 같은
builder에 membership으로 추가하며 별도 socket을 만들지 않는다.

```cpp
namespace zlink::framework {

class zlink_builder_t {
public:
    zlink_builder_t &add_node(std::string node_name);
    zlink_builder_t &max_pending(std::size_t count);
    zlink_builder_t &default_request_timeout(std::chrono::milliseconds timeout);
    zlink_builder_t &actor_transfer_timeout(std::chrono::milliseconds timeout);
    zlink_builder_t &actor_transfer_forward_window(std::chrono::milliseconds window);
    zlink_builder_t &add_location_store(std::shared_ptr<location_store_t> store);
    location_options_t &configure_locations();
    mesh_node_builder_t add_route_mesh(std::string mesh_name);
    fanout_channel_builder_t add_fanout_channel(std::string channel_name);
    stream_builder_t stream(std::string stream_name);
};

class mesh_peer_connections_t {
public:
    void connect(std::string endpoint);
    void connect(zlink::routing_id_t expected_routing_id, std::string endpoint);
    void disconnect(std::string endpoint);
    std::vector<mesh_peer_connection_t> list_connections() const;
};

class mesh_channel_builder_t {
public:
    mesh_channel_builder_t &set_weight(int weight);
    mesh_channel_builder_t &use_handler_group(std::string group_name);

    template <typename THandler, typename TMessage>
    mesh_channel_builder_t &add_send_handler(std::string packet_name = {});

    template <typename THandler, typename TRequest, typename TReply>
    mesh_channel_builder_t &add_request_handler(std::string packet_name = {});
};

struct mesh_node_socket_config_t {
    std::int64_t max_message_size = 0;
    int send_high_water_mark = 1000;
    int receive_high_water_mark = 1000;
    std::optional<std::chrono::milliseconds> receive_timeout;
    std::optional<std::chrono::milliseconds> send_timeout;
};

struct spot_publisher_config_t {
    bool no_drop = true;
};

struct entry_spot_options_t {
    std::optional<zlink::routing_id_t> routing_id;
};

enum class mesh_node_drain_policy_t {
    drain_natural,
    release_and_recreate
};

class mesh_node_builder_t {
public:
    mesh_channel_builder_t channel_name(std::string channel_name);
    mesh_node_builder_t &listen(std::string endpoint);
    mesh_node_builder_t &set_routing_id(zlink::routing_id_t routing_id);
    mesh_node_builder_t &use_allocated_routing_id(
      std::size_t slot_count,
      std::string routing_id_prefix = {});
    mesh_node_builder_t &set_routing_id_allocation_group(std::string group_name);
    mesh_node_socket_config_t &configure_router_socket();
    spot_publisher_config_t &configure_spot_publisher();
    entry_spot_options_t &configure_entry_spot();
    mesh_node_builder_t &use_drain_policy(mesh_node_drain_policy_t policy);
    mesh_peer_connections_t &peer_connections();
    mesh_node_builder_t &set_default_request_timeout(std::chrono::milliseconds timeout);

    template <typename THandler, typename TMessage>
    mesh_node_builder_t &add_route_send_handler(std::string packet_name = {});

    template <typename THandler, typename TRequest, typename TReply>
    mesh_node_builder_t &add_route_request_handler(std::string packet_name = {});

    template <typename TEntrySpot>
    mesh_node_builder_t &add_entry_spot();

    template <typename TSpot>
    mesh_node_builder_t &add_spot(std::string spot_name);

    template <typename TActorFactory>
    mesh_node_builder_t &add_actor_factory(std::string actor_type);

    template <typename TActor, typename TAdapter>
    mesh_node_builder_t &add_actor_transfer_adapter(std::string actor_type);
};

enum class mesh_node_state_t {
    starting,
    serving,
    draining,
    drained,
    force_stopping,
    stopped,
    faulted
};

struct mesh_peer_snapshot_t {
    zlink::routing_id_t rid;
    std::uint64_t lifecycle_generation;
    std::uint64_t descriptor_revision;
    std::string endpoint;
    std::string admission_state;
    bool ready;
    std::string drain_state;
    std::vector<std::string> channel_names;
    std::optional<std::string> last_failure;
};

struct mesh_channel_snapshot_t {
    std::string channel_name;
    int local_weight;
    std::uint64_t ready_member_count;
    bool selectable;
};

struct logical_multicast_snapshot_t {
    bool no_drop;
    std::uint64_t submitted;
    std::uint64_t backpressured;
    std::uint64_t dropped;
    std::uint64_t remote_snapshot_count;
    std::uint64_t remote_admitted_count;
    std::uint64_t remote_dropped_count;
    std::uint64_t local_snapshot_count;
    std::uint64_t local_admitted_count;
    std::uint64_t local_dropped_count;
    std::uint64_t pending_admission_count;
};

struct mesh_claim_snapshot_t {
    bool application_active;
    std::uint64_t pending_application_work;
    bool infrastructure_active;
    std::uint64_t pending_infrastructure_work;
};

struct location_runtime_snapshot_t {
    std::string state;
    std::optional<std::chrono::system_clock::time_point> last_success_at;
    std::optional<std::chrono::system_clock::time_point> last_failure_at;
};

struct mesh_drain_snapshot_t {
    mesh_node_state_t state;
    std::optional<std::chrono::system_clock::time_point> deadline;
    bool work_sealed;
    std::uint64_t pending_request_count;
    std::uint64_t pending_transfer_count;
    std::uint64_t pending_stream_barrier_count;
};

struct mesh_node_snapshot_t {
    std::string mesh_name;
    zlink::routing_id_t rid;
    std::uint64_t lifecycle_generation;
    std::uint64_t descriptor_revision;
    std::string endpoint;
    mesh_node_state_t state;
    std::uint64_t sequence;
    std::chrono::system_clock::time_point observed_at;
    std::vector<std::string> descriptor_sources;
    std::vector<mesh_peer_snapshot_t> peers;
    std::vector<mesh_channel_snapshot_t> channels;
    logical_multicast_snapshot_t multicast;
    mesh_claim_snapshot_t claims;
    location_runtime_snapshot_t location;
    mesh_drain_snapshot_t drain;
};

struct mesh_runtime_event_t {
    std::string identifier;
    std::uint64_t sequence;
    std::chrono::system_clock::time_point timestamp;
    std::string mesh_name;
    zlink::routing_id_t source_rid;
    std::optional<zlink::routing_id_t> peer_rid;
    std::optional<std::uint64_t> lifecycle_generation;
    std::optional<std::uint64_t> descriptor_revision;
    std::optional<std::string> channel_name;
    std::optional<std::string> claim_domain;
    std::optional<std::string> message_kind;
    std::optional<std::uint64_t> remote_snapshot_count;
    std::optional<std::uint64_t> remote_admitted_count;
    std::optional<std::uint64_t> remote_dropped_count;
    std::optional<std::uint64_t> local_snapshot_count;
    std::optional<std::uint64_t> local_admitted_count;
    std::optional<std::uint64_t> local_dropped_count;
    std::optional<std::string> reason;
    std::optional<mesh_node_state_t> state;
};

class mesh_runtime_observation_t {
public:
    virtual ~mesh_runtime_observation_t() = default;
    virtual void close() = 0;
};

class route_mesh_runtime_t {
public:
    virtual mesh_node_snapshot_t snapshot(std::string mesh_name) const = 0;
    virtual std::unique_ptr<mesh_runtime_observation_t> observe(
      std::string mesh_name,
      std::size_t capacity,
      std::function<void(const mesh_runtime_event_t &)> observer) = 0;
    virtual bool is_ready(std::string mesh_name) const = 0;
    virtual task_t<drain_result_t> drain(
      std::string mesh_name,
      std::chrono::milliseconds deadline = std::chrono::seconds(30)) = 0;
    virtual task_t<drain_result_t> await_drained(std::string mesh_name) = 0;
};

class stream_builder_t {
public:
    stream_builder_t &bind(std::string endpoint);
    stream_builder_t &register_session(std::string session_name);
};

class stream_node_options_builder_t {
public:
    stream_node_options_builder_t &bind(std::string endpoint);

    template<typename TSession>
    stream_node_options_builder_t &register_session();

    stream_node_options_builder_t &register_session(std::string session_name);
      std::string mesh_name);
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
- MeshNode runtime handle
- `zlink::stream_socket_t`

## 7. Channel Builder

channel은 framework에서 request/reply와 pub/sub 역할을 묶는 이름이다.

```cpp
namespace zlink::framework {

class channel_builder_t {
public:
    capability_builder_t enable_server();
    capability_builder_t enable_client();
    capability_builder_t enable_publisher();
    capability_builder_t enable_subscriber();
};

class capability_builder_t {
public:
    capability_builder_t &bind(std::string endpoint);
    capability_builder_t &connect(std::string endpoint);
    capability_builder_t &set_routing_id(zlink::routing_id_t routing_id);
    };

} // namespace zlink::framework
```

요청 timeout은 call object의 `.timeout(...)`과 route request fluent 표면에서 설정한다. pending
queue 상한은 `zlink_builder_t::max_pending(...)`이 runtime 단위로 소유한다. C++ 공개 계약은
`.NET` 역할 builder에 없는 per-역할 timeout/pending option을 만들지 않는다.

내부 매핑은 아래와 같다.

| Capability | Binding 구현 기준 |
|------------|------------------|
| server | `zlink::router_socket_t` |
| client | `zlink::dealer_socket_t` |
| publisher | `zlink::pub_socket_t` |
| subscriber | `zlink::sub_socket_t` |

같은 channel 안에서도 역할별 연결 집합은 분리한다. 예를 들어
`orders.client`와 `orders.subscriber`는 같은 channel 이름을 공유하지만 서로 다른
socket과 연결 정책을 가진다.

따라서 `bind`, `connect`, 인자 없는 `enable_client`/`enable_subscriber` 같은 연결 설정은 channel 전체가 아니라
`server`, `client`, `publisher`, `subscriber` 역할 builder에 둔다.

## 8. Handler Registry

handler registry는 typed payload를 함수 수준에서 처리하게 하는 표면이다.

```cpp
namespace zlink::framework {

enum class handler_execution_t {
    standard,
    offload
};

struct handler_options_t {
    std::optional<std::string> packet_name;
    std::optional<std::chrono::milliseconds> timeout;
    std::optional<std::size_t> max_concurrency;
    handler_execution_t execution = handler_execution_t::standard;
    bool ordered = false;
};

enum class timer_overrun_policy_t {
    skip_late_ticks = 1,
    catch_up_bounded = 2,
    delay_next_tick = 3
};

struct timer_options_t {
    timer_overrun_policy_t overrun_policy =
      timer_overrun_policy_t::skip_late_ticks;
    std::uint32_t max_catch_up_ticks = 1;
    bool stop_on_unhandled_exception = false;
};

struct timer_tick_t {
    std::string name;
    std::uint64_t delivery_index;
    std::uint64_t scheduled_index;
    std::chrono::nanoseconds period;
    std::chrono::system_clock::time_point scheduled_at;
    std::chrono::system_clock::time_point started_at;
    std::chrono::steady_clock::duration scheduled_elapsed;
    std::chrono::steady_clock::duration started_elapsed;
    std::chrono::steady_clock::duration delay;
    std::uint64_t skipped_ticks;
};

class timer_t;
class send_call_t;
class stream_write_call_t;
class actor_join_spot_call_t;
class actor_join_entry_spot_call_t;
class actor_context_t;
class spot_handle_t;
class spot_handle_resolver_t;

template <typename T>
class task_t;

template <typename T>
class result_t;

class endpoint_connections_t {
public:
    void connect(std::string endpoint);
    void disconnect(std::string endpoint);
    std::vector<std::string> list_connections() const;
};

class actor_ref_t {
public:
    actor_ref_t(node_rid_t node_rid,
      std::string actor_type,
      std::string actor_id,
      std::uint64_t generation = 1);

    node_rid_t node_rid() const;
    std::string_view actor_type() const;
    std::string_view actor_id() const;
    std::uint64_t generation() const;
    bool empty() const;
};

template <typename TReply>
struct actor_join_accepted_t {
    actor_ref_t actor;
    TReply reply;
};

template <typename TReply>
struct actor_join_rejected_t {
    TReply reply;
};

template <typename TReply>
using typed_actor_join_result_t =
  std::variant<actor_join_accepted_t<TReply>, actor_join_rejected_t<TReply>>;

using actor_join_result_t = typed_actor_join_result_t<message_t>;

class actor_join_spot_call_t {
public:
    actor_join_spot_call_t &timeout(std::chrono::milliseconds timeout);
    task_t<actor_join_result_t> async();
    task_t<actor_join_result_t> yield();

    template <typename TReply>
    task_t<typed_actor_join_result_t<TReply>> async();

    template <typename TReply>
    task_t<typed_actor_join_result_t<TReply>> yield();
};

class actor_join_entry_spot_call_t {
public:
    actor_join_entry_spot_call_t &timeout(std::chrono::milliseconds timeout);
    task_t<actor_join_result_t> async();
    task_t<actor_join_result_t> yield();

    template <typename TReply>
    task_t<typed_actor_join_result_t<TReply>> async();

    template <typename TReply>
    task_t<typed_actor_join_result_t<TReply>> yield();
};

enum class submit_status_t {
    submitted,
    backpressured,
    timed_out,
    target_not_found,
    route_not_connected,
    shutdown
};

struct submit_result_t {
    submit_status_t status;
};

// 숫자 값은 관측·진단 데이터의 안정 키이므로 고정한다(framework API §13).
enum class framework_error_kind_t {
    actor_route_not_found = 0,
    actor_create_failed = 1,
    actor_already_exists = 2,
    actor_type_mismatch = 3,
    spot_create_failed = 4,
    spot_route_not_found = 5,
    spot_type_mismatch = 6,
    actor_session_not_bound = 7,
    handler_not_found = 8,
    route_handler_not_found = 9,
    actor_dispatch_handler_not_found = 10,
    payload_decode_failed = 11,
    route_not_connected = 12,          // retriable
    request_target_not_found = 13,
    request_rejected = 14,
    request_protocol_error = 15,
    request_failed = 16,
    worker_queue_full = 17,
    worker_timed_out = 18,
    worker_failed = 19,
    actor_location_stale = 20,         // retriable
    actor_create_rejected = 21
};

class framework_exception_t : public std::exception {
public:
    framework_error_kind_t kind() const noexcept;
    bool is_retriable() const noexcept;
    // 경계 상태(timed_out, shutdown, disconnected, closed, cancelled)는
    // public enum 값이 아니라 이 error_code로 노출한다(§15.9).
    // stale Actor ref는 actor_location_stale error kind로 분류한다.
    std::error_code code() const noexcept;
    const char *what() const noexcept override;
};

template <typename TReply>
class request_call_t {
public:
    request_call_t &timeout(std::chrono::milliseconds timeout);
    request_call_t &metadata(std::string key, std::string value);
    task_t<TReply> async();
    task_t<TReply> yield();
};

class channel_request_call_t {
public:
    channel_request_call_t &timeout(std::chrono::milliseconds timeout);
    channel_request_call_t &metadata(std::string key, std::string value);

    template <typename TReply>
    task_t<TReply> async();

    template <typename TReply>
    task_t<TReply> yield();
};

class send_call_t {
public:
    send_call_t &timeout(std::chrono::milliseconds timeout);
    send_call_t &metadata(std::string key, std::string value);
    submit_result_t try_submit();
    submit_result_t submit();
};

class stream_write_call_t {
public:
    stream_write_call_t &metadata(std::string key, std::string value);
    stream_write_call_t &packet_name(std::string packet_name);
    stream_write_call_t &compress();
    void submit();
};

template <typename TActor>
class bind_actor_call_t {
public:
    bind_actor_call_t &timeout(std::chrono::milliseconds timeout);
    task_t<TActor> async();
};

enum class stream_codec_t : std::uint8_t {
    raw = 0,
    json = 1,
    message_pack = 2,
    protobuf = 3
};

enum class stream_session_error_t {
    internal,
    transport_error
};

class stream_error_t {
public:
    stream_session_error_t error() const;
    int native_code() const;
    std::string_view message() const;
};

class stream_dispatch_context_t {
public:
    std::string_view packet_name() const;
    const stream_metadata_t &metadata() const;
    bool can_reply() const;
};

class stream_t {
public:
    virtual ~stream_t() = default;
    virtual std::string session_id() const = 0;
    virtual task_t<void> close() = 0;
    virtual stream_write_call_t write_packet(zlink::message_t payload) = 0;
    virtual stream_write_call_t reply_packet(zlink::message_t payload) = 0;
};

class session_actor_t {
public:
    task_t<void> relay(const zlink::message_t &payload);
    task_t<void> notify_disconnected();
};

class session_actor_manager_t {
public:
    result_t<session_actor_t> create(
        std::string actor_type,
        std::string actor_id);
    result_t<session_actor_t> create(
        std::string actor_type,
        std::string actor_id,
        message_t create_request);
    std::optional<session_actor_t> find(std::string actor_id) const;
    result_t<session_actor_t> get_or_create(
        std::string actor_type,
        std::string actor_id);
    result_t<session_actor_t> get_or_create(
        std::string actor_type,
        std::string actor_id,
        message_t create_request);
    request_call_t<session_actor_t> bind(actor_ref_t actor);
};

class packet_stream_session_t {
public:
    virtual ~packet_stream_session_t() = default;
    virtual task_t<void> on_connected(stream_t &stream) = 0;
    virtual task_t<void> on_disconnected(stream_t &stream) = 0;
    virtual task_t<void> on_error(stream_t &stream, const stream_error_t &error) = 0;
    virtual task_t<void> on_packet(
      stream_t &stream,
      const stream_dispatch_context_t &dispatch,
      const zlink::message_t &payload);
};

struct handler_invocation_context_t {
    handler_descriptor_t descriptor;
    handler_context_t context;
    std::shared_ptr<const zlink::message_t> message;
};

struct handler_context_t {
    std::string channel_name;
    std::string packet_name;
    std::string content_type;
};

struct request_context_t : handler_context_t {};
struct send_context_t : handler_context_t {};

struct publish_context_t : handler_context_t {
    std::string topic;
    std::string source;
};

using handler_next_t = std::function<task_t<zlink::message_t>()>;

class handler_registry_t {
public:
    template <typename TOwner, typename TEvent>
    handler_registry_t &on_event(
      std::string channel_name,
      std::string topic,
      void (TOwner::*method)(const TEvent &),
      handler_options_t options = {});

    template <typename TOwner, typename TEvent>
    handler_registry_t &on_event(
      std::string channel_name,
      std::string topic,
      void (TOwner::*method)(const TEvent &, const publish_context_t &),
      handler_options_t options = {});

    template <typename TOwner, typename TRequest, typename TReply>
    handler_registry_t &on_request(
      std::string channel_name,
      std::string topic,
      TReply (TOwner::*method)(const TRequest &),
      handler_options_t options = {});

    template <typename TOwner, typename TRequest, typename TReply>
    handler_registry_t &on_request(
      std::string channel_name,
      std::string topic,
      TReply (TOwner::*method)(const TRequest &, const request_context_t &),
      handler_options_t options = {});

    template <typename TOwner, typename TCommand>
    handler_registry_t &on_send(
      std::string channel_name,
      std::string topic,
      void (TOwner::*method)(const TCommand &),
      handler_options_t options = {});

    template <typename TOwner, typename TCommand>
    handler_registry_t &on_send(
      std::string channel_name,
      std::string topic,
      void (TOwner::*method)(const TCommand &, const send_context_t &),
      handler_options_t options = {});

    handler_registry_t &send_raw(
      std::string channel_name,
      std::string topic,
      std::string packet_name,
      std::function<result_t<void>(const payload_view_t &)> handler,
      handler_options_t options = {});

    template <typename TFilter>
    handler_registry_t &use_filter();

    handler_registry_t &observe_failures(
      std::function<void(const handler_failure_event_t &)> observer);
};

} // namespace zlink::framework
```

handler owner 타입은 service collection에서 resolve한다. 일반 application은
`add_zlink_framework(...)` 안에서 handler와 service를 함께 등록한다.

```cpp
options.services().add_transient<order_handler_t>();

options.handlers()
  .group ("orders-api")
  .add<order_created_handler_t> ();
```

STREAM application 업무 경로는 header 객체를 직접 받지 않는다. C++ stream session과 actor relay는
`zlink::message_t` payload 하나를 사용하고, reply와 relay에 필요한 header 값은 runtime 내부
dispatch state가 보존한다. 별도 `_raw` 이름의 public API는 두지 않는다.

handler dispatch는 binding의 `zlink::message_t`와 `zlink::multipart_t`를 받은 뒤,
serializer를 통해 typed payload로 변환하고, DI에서 owner를 resolve한 다음 method를
호출한다.

handler method는 payload만 받을 수도 있고, payload 뒤에 typed context를 함께 받을 수도
있다. request handler는 `request_context_t`, send handler는 `send_context_t`, event/publish
handler는 `publish_context_t`를 받는다. context에는 channel, packet 이름, content type처럼
사용자가 정책 판단에 쓰는 값만 둔다. raw multipart header나 dispatch table은 public context로
노출하지 않는다.

handler filter는 `.NET`의 handler filter처럼 handler 호출 앞뒤의 공통 처리를 맡는다.
일반 application 설정에서는 `options.use_filter<TFilter>()`로 등록한다. 낮은 수준 extension이나
unit test가 직접 registry를 다룰 때만 `handlers.use_filter<TFilter>()`를 사용한다. filter 타입은
`invoke(const handler_invocation_context_t &, handler_next_t)`를 제공하며, 계속 처리하려면
`co_await next()`를 호출하고 요청을 가로채야 하면 reply message를 직접 반환한다. descriptor
lookup, serializer 선택, DI resolve 순서와 filter chain 저장 방식은 public API로 노출하지 않는다.

STREAM handler는 일반 request/send/event handler와 분리한다. framework core는 packet
방식만 지원한다. 내부 wire header는 runtime이 만들고 검증하며, raw stream session과 사용자
정의 header framing은 core public 표면에 넣지 않는다.

stream callback은 framework가 packet을 수신하고 header 검증을 마친 뒤 호출한다. 별도
실행기로 넘기는 것이 기본은 아니며, 같은 stream session의 packet/lifecycle callback은
직렬로 처리한다. CPU-bound 또는 blocking 가능성이 있는 stream handler는 offload 실행
정책을 명시한다.

request handler 반환값은 `TReply` 또는 `task_t<TReply>`를 허용한다. `task_t<TReply>`를
반환하는 handler는 `.NET`의 `async Task<TReply>` handler와 같은 의미이며, 내부
request처럼 결과를 기다려야 하는 호출은 `co_await call.async()` 형태로 사용한다.
one-way send/push는 `call.try_submit()`으로 즉시 admission 결과를 확인하거나
`call.submit()`으로 send timeout까지 bounded admission 결과를 기다린다. 두 terminator 모두 remote
handler 완료는 기다리지 않는다.

Handler coroutine은 blocking wait 없이 `task_t<T>`로 완료된다. 같은 task의 terminal 결과는 한 번만
확정되며 중복 완료가 기존 결과를 바꾸지 않는다. Handler 실행 scheduler와 continuation 배치는 public
API에 노출하지 않는다.

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

class spot_publisher_client_t {
public:
    template <typename TEvent>
    publish_call_t publish(std::string mesh_name,
                           std::string channel_name, std::string topic,
                           const TEvent &event) const;
};

class request_client_t {
public:
    template <typename TCommand>
    send_call_t send(std::string_view mesh_name,
      std::string_view channel_name, const TCommand &command,
      send_options_t options = {});

    template <typename TRequest>
    channel_request_call_t request(std::string_view mesh_name,
      std::string_view channel_name,
      const TRequest &request,
      request_options_t options = {});
};

class message_bus_t {
public:
    publisher_t &publisher();
    request_client_t &client();
};

class route_client_t {
public:
    // node 대상 — infra 계층과 owner 일관 라우팅용
    template <typename TMessage>
    route_send_call_t send_to_node(std::string mesh_name,
      zlink::routing_id_t target_node_rid,
      TMessage message);

    template <typename TRequest>
    channel_request_call_t request_to_node(std::string mesh_name,
      zlink::routing_id_t target_node_rid,
      TRequest request);

    template <typename TMessage>
    route_send_call_t send_to_channel(std::string mesh_name,
      std::string channel_name,
      TMessage message);

    template <typename TRequest>
    channel_request_call_t request_to_channel(std::string mesh_name,
      std::string channel_name,
      TRequest request);

    // spot 대상 — 대상 인자는 불투명한 spot handle 하나다.
    // spot rid와 node rid를 나란히 받는 overload는 두지 않는다(공통 스펙 24 §3).
    template <typename TMessage>
    route_send_call_t send_to_spot(const spot_handle_t &target, TMessage message);

    template <typename TRequest>
    channel_request_call_t request_to_spot(const spot_handle_t &target, TRequest request);
};

class route_send_call_t {
public:
    route_send_call_t &timeout(std::chrono::milliseconds timeout);
    route_send_call_t &metadata(std::string key, std::string value);
    submit_result_t try_submit();
    submit_result_t submit();
};

struct logical_multicast_detail_t {
    std::uint64_t snapshot_remote_node_count;
    std::uint64_t admitted_remote_node_count;
    std::uint64_t dropped_remote_node_count;
    std::uint64_t snapshot_local_spot_count;
    std::uint64_t admitted_local_spot_count;
    std::uint64_t dropped_local_spot_count;
};

struct publish_result_t {
    submit_status_t status;
    logical_multicast_detail_t detail;
};

class publish_call_t {
public:
    publish_call_t &metadata(std::string key, std::string value);
    publish_result_t try_submit();
    task_t<publish_result_t> async();
};

} // namespace zlink::framework
```

Public API는 transport 종류와 무관하게 channel name과 typed payload를 기준으로 유지한다.

framework는 아래 서비스를 기본 등록한다. 사용자는 직접 생성하지 않고 DI에서
주입받아 사용할 수 있다.

- `message_bus_t`
- `publisher_t`
- `spot_publisher_client_t` (MeshNode Logical Multicast client)
- `request_client_t`
- `route_client_t`
- `serializer_registry_t`

## 10. Serialization

framework serializer는 binding의 message 중심 codec API를 사용한다. binding의
codec 구조도 connector와 같은 방향으로 맞춘다. 즉 base binding은 raw `message_t`와
protocol enum만 제공하고, JSON, MessagePack, Protobuf 구현은 선택 codec target이
제공한다. JSON 기본 구현은 `message_t::from_json(...)`,
`message.parse_json<T>()` 같은 표면과 `nlohmann/json`을 기준으로 한다.

```cpp
namespace zlink::framework {

class serializer_registry_t {
public:
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

binding codec helper는 다음 public 표면을 사용한다.

```cpp
auto message = zlink::message_t::from_json(order);
auto order = message.parse_json<order_created_t>();
```

bindings package는 JSON, MessagePack, Protobuf dependency를 갖지 않는다. JSON은 framework
기본 codec으로 제공하고, Protobuf와 MessagePack은 framework codec extension package가
제공한다. framework, connector, HTTP client가 codec을 바꿔도 handler/client 업무 API는
바뀌지 않는다.

```cmake
target_link_libraries(app PRIVATE zlink::cpp)

# Protobuf가 필요할 때만 추가한다.
target_link_libraries(app PRIVATE zlink::framework_codec_protobuf)
```

```cpp
app.advanced().handlers()
  .send_raw("orders", "orders.raw", [](const zlink::message_t &message) {
      // raw payload path
  });
```

## 11. Spot Framework API

Framework Spot 표면은 owner MeshNode와 `zlink::service::spot_t`를 기반으로 한다.

```cpp
namespace zlink::framework {

class spot_t {
public:
    virtual ~spot_t() = default;
};

class entry_spot_t : public spot_t {
public:
    ~entry_spot_t() override = default;
};

class spot_common_context_t {
public:
    std::string_view mesh_name() const;
    node_rid_t node_rid() const;
    spot_rid_t spot_rid() const;
    std::string spot_name() const;
    spot_handler_registry_t handlers();

    template <typename TCommand>
    send_call_t send_to(node_rid_t node_rid,
      spot_rid_t spot_rid,
      TCommand command);

    template <typename TReply, typename TRequest>
    request_call_t<TReply> request_to(node_rid_t node_rid,
      spot_rid_t spot_rid,
      TRequest request);

    template <typename TEvent>
    publish_call_t publish(
      std::string channel_name,
      std::string topic,
      TEvent event);

    template <typename THandler>
    timer_t add_timer(std::string name,
      std::chrono::milliseconds period,
      timer_options_t options = {});

    template <typename TResult, typename TWork>
    worker_call_t<TResult> run_worker(TWork work);
};

class spot_context_t : public spot_common_context_t {
public:
    task_t<bool> close();
};

class entry_spot_context_t : public spot_common_context_t {
public:
    task_t<void> destroy_actor(const actor_ref_t &actor);
};

struct spot_actor_join_response_t {
    bool accepted;
    std::optional<zlink::framework::message_t> reply;
};

template <typename TActor>
class actor_transfer_adapter_t {
public:
    virtual ~actor_transfer_adapter_t() = default;

    virtual task_t<zlink::framework::message_t> transfer_out(
      const TActor &actor) = 0;

    virtual task_t<TActor> transfer_in(
      std::string actor_id,
      actor_context_t &context,
      zlink::framework::message_t state) = 0;
};

enum class spot_create_state_t {
    existing,
    created,
    rejected
};

struct spot_create_response_t {
    bool accepted;
    std::optional<zlink::framework::message_t> reply;
};

struct spot_create_result_t {
    spot_rid_t spot_rid;
    spot_create_state_t state;
    std::optional<zlink::framework::message_t> reply;
    spot_context_t context;
};

struct spot_actor_message_metadata_t {
    std::optional<std::string_view> find(std::string_view key) const;
    bool contains(std::string_view key) const;
    bool empty() const;
    std::map<std::string, std::string> values;
};

class spot_actor_reply_options_t {
public:
    spot_actor_reply_options_t &compress(bool enabled = true);
};

struct spot_actor_send_context_t {
    std::string packet_name;
    std::string content_type;
    spot_actor_message_metadata_t metadata;
};

struct spot_actor_request_context_t {
    std::string packet_name;
    std::string content_type;
    spot_actor_message_metadata_t metadata;
    spot_actor_reply_options_t reply;
};

class spot_handler_registry_t {
public:
    template <auto Method>
    spot_handler_registry_t &add_handler(std::string packet_name = {});

    template <auto Method>
    spot_handler_registry_t &add_subscribe(std::string topic);

    template <typename TSpot>
    result_t<message_t> invoke_packet(std::string_view packet_name,
      TSpot &spot,
      service_provider_t &services,
      serializer_registry_t &serializers,
      const message_t &message) const;

};

class actor_handler_registry_t {
public:
    template <auto Method>
    actor_handler_registry_t &add_send(std::string packet_name = {});

    template <auto Method>
    actor_handler_registry_t &add_request(std::string packet_name = {});
};

class bound_session_t {
public:
    template <typename TMessage>
    send_call_t send(const TMessage &message);

    task_t<void> disconnect();
};

class actor_context_t {
public:
    std::string_view mesh_name() const;
    std::optional<spot_rid_t> spot_rid() const;
    actor_handler_registry_t handlers();
    bound_session_t bound_session() const;

    actor_join_spot_call_t join_spot(spot_rid_t spot_rid,
      const zlink::framework::message_t &request);

    actor_join_entry_spot_call_t join_entry_spot(node_rid_t mesh_node_rid,
      const zlink::framework::message_t &request);

    template <typename TRequest>
    actor_join_spot_call_t join_spot(spot_rid_t spot_rid,
      const TRequest &request);

    template <typename TRequest>
    actor_join_entry_spot_call_t join_entry_spot(node_rid_t mesh_node_rid,
      const TRequest &request);

    task_t<void> leave_spot();
};

} // namespace zlink::framework
```

`spot_context_t::publish(...)`는 target ChannelName과 topic을 함께 받는다. publish는
MeshNode ROUTER를 통해 remote MeshNode마다 한 번 제출하고, 수신 node는 node-local
subscription만 검사한다. 기본 `no_drop=true`이며 모든 remote admission과 local queue
reserve가 성공해야 수락한다. Spot·Actor 등록은 owner `mesh_node_builder_t`에 속한다.

`mesh_node_socket_config_t::max_message_size`의 `0`은 framework 상한 없음이다. Adapter는 이를
Core의 `ZLINK_OPT_MAXMSGSIZE=-1`로 변환하고 음수 입력은 startup 설정 오류로 거부한다.
`send_timeout`을 지정하지 않으면 framework 기본값 1초를 사용한다. `receive_timeout`을 지정하지 않으면
수신 대기 상한을 따로 두지 않는다. HWM은 0 이상이어야 한다.

Spot Actor Join / Transfer 관련 interface도 이 문서에 기록된 정식 계약이며,
그 동작 의미는 [공통 스펙](../../23-spot-actor.ko.md)을 따른다. 구현이나 contract test가
이 시그니처와 다르면 계약 불일치로 처리한다.

`entry_spot_context_t::destroy_actor(...)`는 Entry Spot에서만 호출한다. user Spot에 있는 Actor는
먼저 `leave_actor(...)` 또는 Entry Spot join을 완료해야 한다. Destroy는 membership 이동이 아니므로
`on_leave_actor`를 다시 호출하지 않으며, 같은 Actor instance의 중복 destroy는 lifecycle callback을
추가로 실행하지 않고 성공으로 끝난다. 전체 순서는 [Actor model §6](../../22-actor-model.ko.md#6-lifecycle)을
따른다.

`actor_transfer_adapter_t<TActor>`는 remote transfer에서 domain state를 옮겨야 하는 actor type에만
등록한다. 등록이 없으면 framework는 빈 `message_t`를 전송하고 target의 actor factory로 actor를
만드는 기본 경로를 사용한다. custom state를 전달해야 하는 actor type은
`add_actor_transfer_adapter<TActor, TAdapter>(...)`로 adapter type을 등록한다.

C++의 일반 Spot packet handler registry는 `spot_context_t::handlers()`가 맡고 Actor payload handler는
`actor_context_t::handlers()`가 맡는다. Actor handler는 mutable Actor와 읽기 전용 handler context만
받으며 mutable Spot을 함께 받지 않는다. Spot 상태 변경은 `spot_handle_t` direct call로 제출한다. Actor
lifecycle은 registry 등록 표면이 아니다. user Spot은
`on_actor_join(actor_join_request_t, zlink::framework::message_t)`,
`on_actor_joined(actor_membership_t)`, `on_leave_actor(actor_membership_t)`,
`on_disconnect_actor(actor_membership_t)`
member callback을 직접 제공한다. Entry Spot도 user Spot에서 Entry Spot으로 돌아오는
명시적 join을 `on_actor_join(actor_join_request_t, zlink::framework::message_t)`에서 accept/reject하고,
commit 이후 callback인 `on_actor_joined(actor_membership_t)`, `on_leave_actor(actor_membership_t)`와
session binding 종료 callback인 `on_disconnect_actor(actor_membership_t)`를 제공한다. Membership과
join request는 immutable snapshot이다.
일반 Spot 타입은 `zlink::framework::spot_t`를 상속해야 하고, Entry Spot 타입은
`zlink::framework::entry_spot_t`를 상속해야 한다. 이름이나 파일 위치로 역할을 추론하지 않는다.
`add_spot<TSpot>()`와 `add_entry_spot<TEntrySpot>()`가 이 계약을 compile-time으로 확인한다.

```cpp
class bingo_room_spot_t : public zlink::framework::spot_t,
                          public bingo_room_t {
public:
    zlink::framework::spot_actor_join_response_t on_actor_join(
      const zlink::framework::actor_join_request_t &actor,
      const zlink::framework::message_t &request);

    void on_actor_joined(const zlink::framework::actor_membership_t &actor);

    void on_leave_actor(const zlink::framework::actor_membership_t &actor);

    void on_disconnect_actor(const zlink::framework::actor_membership_t &actor);
};

class player_actor_t : public zlink::framework::actor_t {
public:
    start_bingo_game_res_t start_game(
      const zlink::framework::spot_actor_request_context_t &context,
      const start_bingo_game_req_t &request);

    void configure(zlink::framework::actor_context_t &context)
    {
        context.handlers().add_request<&player_actor_t::start_game>();
    }
};

class bingo_entry_spot_t : public zlink::framework::entry_spot_t {
public:
    void configure(zlink::framework::spot_context_t &context);
};
```

일반 Spot packet member와 subscription member는 payload 하나를 받는다.
actor join admission을 처리하는 member는 immutable `actor_join_request_t`와
`zlink::framework::message_t` request만 받으며,
`spot_actor_join_response_t`로 accepted 여부와 optional reply `zlink::framework::message_t`를 돌려준다.
actor type과 source/target Spot 및 node 정보는 framework 내부 routing과 검증에만 사용한다.
accepted가 `true`일 때만 actor 위치를 user Spot으로 commit하고
`on_actor_joined(actor_membership_t)`를 호출한다. accepted가 `false`이면 actor 위치를 바꾸지 않고
    post-joined callback도 호출하지 않는다. Commit 이후 결과는 callback 이름으로 구분한다.
actor packet member는 `spot_actor_request_context_t` 또는 `spot_actor_send_context_t`와 DTO를 받으며,
member owner인 Actor instance 이외의 mutable owner는 받지 않는다. actor disconnected callback은
`actor_membership_t`만 받을 수 있다.
등록된 member는 descriptor로만 남지 않는다. dispatch 경로는 `serializer_registry_t`로
`message_t`를 DTO로 바꾸고, runtime이 현재 Spot instance와 actor를 찾아 typed member
function을 호출한다. 샘플도 이 경로를 통과해야 framework 동작을 확인했다고
볼 수 있다.
Entry Spot membership 상태에서도 actor packet은 일반 Spot packet으로 등록하지 않는다. Actor의
`actor_handler_registry_t`에 request 또는 one-way send handler를 등록하고 member는 handler context와
DTO를 받는다.
stream header metadata 전체를 actor handler에 그대로 노출하지 않는다. 사용자는
`options.metadata().allow_session_to_actor("trace-id")`처럼 application metadata forwarding 정책을 선언하고,
framework는 허용된 key만 `spot_actor_message_metadata_t`로 project해서 actor context에 넣는다.
handler는 `find(...)` 또는 `contains(...)`로 값을 조회한다. `values`는 단순 반복을 제공하고,
`find(...)`와 `contains(...)`는 handler code가 `std::map` 구조에 직접 묶이지 않게 한다. 빈 metadata
key와 공백만 있는 key는 의미가 모호하므로 두 방향의 allowlist method 모두 이런 key를 거부한다.
이 정책은 stream frame 구조나 ActorGateway 내부 frame을 public handler 표면에 드러내지 않기
위한 경계다.

timer는 native timer handle을 application에 넘기지 않는다. `timer_t`는 CAPI timer
등록의 lifetime과 취소를 표현하는 public handle이며, callback은 user Spot에서는 core
SPOT dispatch boundary를 따르고 Entry Spot에서는 Entry Spot 전체를 전역 직렬화하지
않는다.

Timer backend 선택은 [비동기 실행 정책](../../../04-async-execution-policy.ko.md#5-spot-timer)을 따른다.
`timer_tick_t`는 native timer event를 노출하지 않고 공통 timer dispatch metadata만 제공한다.

ActorGateway session relay의 public 표면은 `session_actor_manager_t`, `session_actor_t`,
`actor_context_t`, `bound_session_t`다. MeshNode transport metadata는 이 표면에 노출하지 않는다.
actor context의 `join_spot(...)` request와 reply는 DTO 또는 `zlink::framework::message_t`다.
JSON DTO는 기본 serializer를 사용하므로 message type별 codec 설정이 필요 없다. Protobuf,
MessagePack, custom binary payload처럼 기본 JSON으로 표현할 수 없는 타입만 startup/options 에
serializer extension을 연결하고 업무 코드는 같은 join 호출을 유지한다. join 결과는
승인과 거절 `variant`다. 승인 값만 join 이후 actor ref를 가지며 두 값 모두 reply
`zlink::framework::message_t`를 담는다. typed reply가 필요하면
`async<TReply>()`가 같은 serializer registry로 decode한다. Entry Spot join도 같은 결과 타입을 돌려준다.
raw payload 처리는 framework 내부 invoker가 맡으며 application public actor context에
별도 raw join overload를 두지 않는다.

호출 실행 표면은 공통 비동기 call 계약을 C++ coroutine 관례로 표현한다.
`request(...)`, `send(...)`, `relay(...)`, `join_spot(...)`, `join_entry_spot(...)` 같은
호출은 call object를 반환한다. one-way call은 `try_submit()`이 즉시 수락 결과를 반환하고,
`submit()`은 send timeout까지 bounded admission 결과를 반환한다. request와 join은 `async()`가 reply
완료를 기다리는 지점이다.
일반 channel `request_call_t`와 `send_call_t`는 metadata와 timeout을 submit 전에 모으고,
submit 시점에 framework envelope 정책으로 넘긴다. typed packet name은 registration
descriptor가 결정한다. Request와 join의 `async()`는 terminal reply 또는 결과까지 현재 owner turn을
유지하고 `yield()`는 현재 turn을 반납한다. Worker call은 결과를 기다리지 않는 `submit()`도 함께
제공한다. 장기 작업 중단 표면이 필요하면 C++ 표준 중단 관례를 사용하는 별도 정식 시그니처를 먼저
정의해야 한다.

```cpp
auto reply = co_await client
  .request("profile", query)
  .async<profile_reply_t>();

use_profile(reply);
```

public framework async 표면에 `std::future`를 사용하지 않는다. blocking wait는 handler,
timer, STREAM session callback, actor relay 경로에서 허용하지 않는다.

오류 종류는 `.NET` framework의 `ZLinkFrameworkErrorKind`를 C++ naming으로 투영한다.
`async()`는 실패 시 같은 정보를 가진 `framework_exception_t`를 throw한다.

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
    virtual void configure_monitoring(monitoring_builder_t &monitoring) {}
};

template <typename TModule>
concept framework_module_contract_t =
  requires(TModule &module,
    service_collection_t &services,
    zlink_builder_t &zlink,
    handler_registry_t &handlers,
    monitoring_builder_t &monitoring) {
      module.configure_services(services);
      module.configure_zlink(zlink);
      module.configure_handlers(handlers);
      module.configure_monitoring(monitoring);
  };

} // namespace zlink::framework
```

module은 서비스 등록, runtime 구성, handler 등록, monitoring 구성을 한 기능 단위로 묶는
낮은 수준 확장 단위다. 일반 애플리케이션 설정의 주 표면은 module type이 아니라
`app_t::add_zlink_framework(options_callback)`이다.

`app_t::add_zlink_framework(options_callback)`는 `.NET`의
`AddZLinkFramework(options => ...)`에 대응하는 C++ 고수준 구성 진입점이다. C++에는 assembly
reflection이 없으므로 `.NET`의 `AddHandlersFromAssemblyOf(...)`만 그대로 옮기지 않는다.
그 대신 handler group을 먼저 고르고, 그 group 안에 handler 타입을 명시해서
`options.handlers().group(group_name).add<THandler>()`,
`add_send<THandler>()`, `add_publish<THandler>()`로 등록한다.
나머지 codec, discovery, RouteMesh membership, handler group 구성은 `.NET`과 같은 읽기 수준을
유지한다.

JSON은 기본 codec이므로 별도 등록하지 않는다. 사용자가 모든 request/reply message
type을 codec 설정에 나열하지 않는다. C++ framework는 `options.handlers().group(...).add<THandler>()`에서
handler의 `request_type`, `reply_type`을 읽고 기본 JSON serializer를 내부에서 선택한다.
send handler는 `message_type`, publish handler는 `event_type`을 읽어 같은 방식으로 serializer와
handler registry 항목을 등록한다. `options.codecs().use(...)`는 일반 message type을 나열하는
단계가 아니라, 기본 JSON으로 표현할 수 없는 payload나 별도 binary serializer extension을
연결하는 고급 확장점이다. 따라서 request/send/publish handler를 같은 group 이름으로
묶고, channel builder의 `.use_handler_group(...)`에서 channel에 연결할 수 있다.
handler group은 channel 종류와 맞아야 한다. RouteMesh ChannelName은 request/send
handler group을 받을 수 있고, fanout channel은 publish handler group만 받을 수 있다. 맞지 않는
group을 연결하면 options 작성 시점에 설정 오류로 실패한다.
같은 channel에 같은 packet 이름의 handler가 두 번 노출되면 `request_protocol_error`로 실패한다.
이 규칙은 low-level `handler_registry_t` 직접 등록뿐 아니라 fluent options의 handler group
경로에도 적용한다. channel이 group을 먼저 참조한 뒤 handler가 들어오는 경우와 handler가 먼저
등록되고 channel이 나중에 group을 참조하는 경우 모두 중복을 허용하지 않는다.
MeshNode는 ROUTER listen endpoint와 하나 이상의 ChannelName을 가져야 한다. 각 ChannelName은
request/send handler group을 가질 수 있다. fanout subscriber는 publish handler group을 하나
이상 등록해야 한다.
handler에 생성자 의존성이 있으면 `using dependency_types =
zlink::framework::dependency_list_t<dep1_t, dep2_t>;`처럼 의존 타입을 명시한다. framework는
handler를 등록할 때 `add_singleton<THandler, dep1_t, dep2_t>()`와 같은 DI 생성자 주입 등록을
사용한다.
`logger_t<THandler>`는 framework 기본 dependency다. handler가
`dependency_types`에 `logger_t<THandler>`를 넣으면 사용자가 별도 service registration을
작성하지 않아도 DI가 `.NET`의 `ILogger<T>`처럼 category logger를 주입한다. 로그 출력 대상은
handler 등록이 아니라 `app.logging().use_console()`, `app.logging().use_file(...)` 같은
host logging 설정에서 정한다. custom category가 필요하면 `logger_factory_t`를 dependency로
받아 handler 내부에서 category logger를 만들 수 있다.

```cpp
app.add_zlink_framework ([&](zlink::framework::zlink_framework_options_t &options) {
    options.use_filter<audit_filter_t>();
    options.metadata()
      .allow_session_to_actor("trace-id")
      .allow_actor_to_session("trace-id");

    auto mesh = options.add_route_mesh(sample_names_t::application_mesh)
      .listen(topology.api_channel_endpoint)
      .set_routing_id(topology.application_rid);
    mesh.channel_name(sample_names_t::api_channel)
      .use_handler_group("api");
    mesh.channel_name(sample_names_t::play_channel);

    // 시작 시 사용할 message-flow 관측 수준을 설정한다.
    options.configure_dispatch().message_flow(
      zlink::framework::message_flow_log_mode_t::errors_only);

    options.handlers()
      .group("api")
      .add<authenticate_player_handler_t>()
      .add<match_bingo_api_handler_t>()
      .add_send<player_command_handler_t>();

    options.handlers()
      .group("events")
      .add_publish<notification_event_handler_t>();
});
```

자동 peer discovery를 사용하면 등록된 Redis location store에서 같은 MeshName의 descriptor를
찾는다. 수동 peer는 `peer_connections().connect(endpoint)` 또는 expected RID를 함께 받는 overload로
등록한다. fanout subscriber의 endpoint 목록은 RouteMesh peer intent와 별도다.

이 구조에서는 샘플 `main.cpp`, role `*HostFactory`, 일반 사용자 설정 예제가 handler member
function pointer, handler용 DI factory lambda, monitoring channel 문자열, serializer smoke 검증,
message type을 모두 나열하는 codec 등록 같은 세부 구현을 직접 알 필요가 없다. 그런 내용이 보이면
framework options builder가 충분히 깊지 않은 것으로 본다.

`zlink_framework_options_t`의 사용자 표면은 fluent options builder로 제한한다.
일반 사용자 설정에는 낮은 수준 channel runtime builder를 직접 노출하지 않는다. C++ 내부 runtime builder에는 낮은 수준 API가 남아 있을 수 있지만,
샘플과 guide 수준의 설정은 아래처럼 역할이 바로 보이는 형태를 사용한다.

`options.configure_dispatch(...)`는 interface graph를 만들지 않고
`dispatch_options_t` value를 람다에 넘긴다. 이 value는 Spot과
STREAM dispatch mode, unhandled request/send/publish 정책, message flow diagnostics 설정을
담는다. native dispatch token, queue slot, handler lookup table은 이 표면에 나오지 않는다.
diagnostics sample rate는 `0.0`에서 `1.0` 사이여야 하며 NaN은 허용하지 않는다.
send와 publish는 reply path가 없으므로 unhandled 정책에 `reply_error`를 사용할 수 없다.

```cpp
auto mesh = options.add_route_mesh(sample_names_t::application_mesh)
  .listen(topology.api_endpoint)
  .set_routing_id(topology.application_rid);
mesh.channel_name(sample_names_t::api_channel)
  .use_handler_group("api");

options.add_fanout_channel(sample_names_t::notification_channel)
  .enable_publisher(topology.notification_endpoint)
  .enable_subscriber(topology.notification_subscriber_endpoint)
  .use_handler_group("events");

mesh.channel_name(sample_names_t::game_channel);
mesh.peer_connections().connect(topology.play_router_endpoint);
mesh.add_entry_spot<session_entry_spot_t>();

options.add_stream_node(sample_names_t::stream_name)
  .bind(topology.stream_endpoint)
  .register_session<client_session_t>()
```

`register_session<TSession>()`은 `.NET`의 `RegisterSession<TSession>()`에 맞춘 typed session
등록 표면이다. `TSession`은 `packet_stream_session_t`를 상속해야 하며, framework service
collection에 stream-session scope 서비스로 등록된다. `TSession::session_name`이 있으면 그 값을
native packet session 이름으로 사용하고, 없으면 타입 이름 기반 message name을 사용한다.
`register_session(name)`은 session 이름을 직접 지정해야 하는 low-level 구성에 남긴다.
하나의 stream node에는 packet session을 하나만 선언한다. `register_session<T>()`과
`register_session(...)`을 중복 호출하면 마지막 값으로 덮어쓰지 않고 설정 오류로 처리한다.

MeshNode는 `listen(...)`으로 ROUTER endpoint를 열고 하나 이상의 `channel_name(...)` membership을
등록해야 한다. 자동 peer는 Redis descriptor로, 수동 peer는 `peer_connections()`로 구성한다.
Node·Channel·Spot·Actor 메시지는 같은 MeshNode ROUTER를 사용한다.
fluent options에서 channel 이름, handler group 이름, endpoint, MeshName, stream node
이름처럼 식별자나 연결 주소로 쓰이는 값은 빈 문자열이나 공백 문자열을 허용하지 않는다.
잘못된 값은 low-level socket/runtime까지 전달하지 않고 builder 호출 또는 options 적용 시점의
framework error로 닫는다.
Spot 코드는 owner MeshNode의 client로 Node direct, ChannelName select-one과 Logical Multicast를
사용한다. Logical Multicast는 별도 PUB/SUB 역할을 구성하지 않는다. classic
fanout만 독립 PUB/SUB socket을 사용한다.

### 12.1 HTTP Hosting

HTTP hosting은 ASP.NET Core Minimal API의 `MapGet`, `MapPost`, `MapPut`,
`MapDelete`에 대응하는 C++ framework 표면이다. MVC controller, Razor page,
template rendering, WebSocket transport는 범위에 넣지 않는다. 대신 route handler,
DI scope, JSON binding, middleware/filter, logging, validation, error mapping,
zlink channel 호출은 같은 application host 안에서 제공한다.

```cpp
namespace zlink::framework {

class http_options_builder_t {
public:
    http_options_builder_t &listen(std::string endpoint);
    http_options_builder_t &configure_tls(
      std::function<void(http_tls_options_builder_t &)> configure);
    http_options_builder_t &configure_server(
      std::function<void(http_server_options_builder_t &)> configure);

    template <typename THandler>
    http_options_builder_t &map_get(std::string path);

    template <typename THandler>
    http_options_builder_t &map_post(std::string path);

    template <typename THandler>
    http_options_builder_t &map_put(std::string path);

    template <typename THandler>
    http_options_builder_t &map_delete(std::string path);

    template <typename TMiddleware>
    http_options_builder_t &use();

    http_options_builder_t &map_health(std::string path);
    http_options_builder_t &map_readiness(std::string path);
    http_options_builder_t &map_liveness(std::string path);
};

struct http_context_t {
    http_method_t method;
    std::string path;
    std::string correlation_id;
    std::map<std::string, std::string> request_headers;
    std::map<std::string, std::string> response_headers;
    std::optional<std::string> response_body;
    int response_status;

    http_context_t &response_header(std::string name, std::string value);
    http_context_t &json_response(int status, std::string body);
};

struct http_request_t {
    http_method_t method;
    std::string path;
    std::string target;
    std::string query_string;
    std::string correlation_id;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> route_values;
    std::map<std::string, std::string> query_values;
    std::string body;
    std::string content_type;
    std::string remote_endpoint;
};

struct http_response_t {
    int status = 200;
    std::string body;
    std::string content_type = "application/json";
    std::map<std::string, std::string> headers;

    http_response_t &header(std::string name, std::string value);
};

class metadata_policy_builder_t {
public:
    metadata_policy_builder_t &allow_session_to_actor(std::string key);
    metadata_policy_builder_t &allow_actor_to_session(std::string key);
};

class zlink_framework_options_t : public zlink_builder_t {
public:
    service_collection_t &services();
    handler_registry_t &handlers();
    codec_options_builder_t &codecs();
    metadata_policy_builder_t &metadata();
    dispatch_options_t &configure_dispatch();
    worker_options_t &configure_worker();

    template <typename TFilter>
    zlink_framework_options_t &use_filter();

    http_options_builder_t http();
};

} // namespace zlink::framework
```

사용 예시는 아래와 같다.

```cpp
app.add_zlink_framework([&](auto &options) {
    auto mesh = options.add_route_mesh(sample_names_t::application_mesh)
      .listen(topology.api_channel_endpoint)
      .set_routing_id(topology.application_rid);
    mesh.channel_name(sample_names_t::api_channel)
      .use_handler_group("api");
    mesh.channel_name(sample_names_t::play_channel);

    options.http()
      .listen(topology.api_http_endpoint)
      .map_post<create_game_http_handler_t>("/games");
});
```

HTTP handler는 message handler와 같은 type alias 규칙을 사용한다.

```cpp
class create_game_http_handler_t {
public:
    using request_type = create_game_http_req_t;
    using reply_type = create_game_http_res_t;
    using dependency_types =
      dependency_list_t<channel_client_t, logger_t<create_game_http_handler_t>>;

    explicit create_game_http_handler_t(
      channel_client_t &client,
      logger_t<create_game_http_handler_t> &logger);

    task_t<create_game_http_res_t> handle(const create_game_http_req_t &request);
};
```

`map_get<THandler>(...)`, `map_post<THandler>(...)`, `map_put<THandler>(...)`,
`map_delete<THandler>(...)`는 handler type을 DI에 등록하고, `request_type`과
`reply_type`의 JSON serializer를 등록하며, HTTP route table에 `method + path`를
연결한다. request마다 DI scope를 만들고 handler를 resolve한다. handler가 반환한 DTO는
JSON response body가 되고, 기본 status는 `200 OK`다.

HTTP handler는 아래 shape를 모두 지원한다.

- typed DTO: `reply_type handle(const request_type &request)`
- typed DTO async: `task_t<reply_type> handle(const request_type &request)`
- typed DTO + context:
  `reply_type handle(const request_type &request, http_context_t &context)`
- typed DTO + context async:
  `task_t<reply_type> handle(const request_type &request, http_context_t &context)`
- typed DTO + request:
  `reply_type handle(const request_type &request, const http_request_t &http)`
- typed DTO + request async:
  `task_t<reply_type> handle(const request_type &request, const http_request_t &http)`
- typed DTO + request + context:
  `reply_type handle(const request_type &request, const http_request_t &http, http_context_t &context)`
- typed DTO + request + context async:
  `task_t<reply_type> handle(const request_type &request, const http_request_t &http, http_context_t &context)`
- typed response: `http_response_t handle(const request_type &request)`
- typed response + context:
  `http_response_t handle(const request_type &request, http_context_t &context)`
- typed response async: `task_t<http_response_t> handle(const request_type &request)`
- typed response + context async:
  `task_t<http_response_t> handle(const request_type &request, http_context_t &context)`
- typed response + request:
  `http_response_t handle(const request_type &request, const http_request_t &http)`
- typed response + request async:
  `task_t<http_response_t> handle(const request_type &request, const http_request_t &http)`
- typed response + request + context:
  `http_response_t handle(const request_type &request, const http_request_t &http, http_context_t &context)`
- typed response + request + context async:
  `task_t<http_response_t> handle(const request_type &request, const http_request_t &http, http_context_t &context)`
- raw HTTP request: `http_response_t handle(const http_request_t &request)`
- raw HTTP request async: `task_t<http_response_t> handle(const http_request_t &request)`

`http_request_t`와 `http_response_t`는 framework public type이다. Raw HTTP handler도
`Boost.Beast` request, socket, SSL stream을 받지 않는다. `map_*<THandler>(...)`는 handler
shape를 compile-time으로 판별한다. typed route에서 여러 overload가 있으면 반환 타입보다
인자 shape를 먼저 본다. `http_request_t`와 `http_context_t`를 모두 받는 shape가 가장 먼저
선택되고, 그 다음 `http_request_t`, `http_context_t`, DTO-only shape 순서로 선택된다.
typed route와 raw route shape를 한 handler에 동시에 제공하면 static assertion 또는 startup
validation으로 실패해야 한다.

route parameter와 query string은 `request_type` DTO에 binding한다. 예를 들어
`/games/{gameId}/moves?actorId=p1`로 들어온 값은 body DTO와 합쳐 handler request가 된다.
같은 필드가 body, route, query에 동시에 있으면 route, query, body 순서로 우선한다. 이
우선순위는 URL에 드러난 식별자가 request body보다 더 명시적인 입력이라는 ASP.NET Core식
route handler 사용성을 따르기 위한 규칙이다.

`use<TMiddleware>()`는 exception, logging, validation, auth, correlation id 같은
cross-cutting 처리를 route handler 앞뒤에 연결한다. middleware/filter는 Beast나 Asio
타입을 받지 않고 `http_context_t`와 framework DTO만 다룬다.
middleware가 `before(http_context_t&)` 또는 `after(http_context_t&)`를 제공하면 runtime은
route handler 전후에 호출한다. request의 `X-Correlation-Id` 또는 `X-Request-Id`는
`http_context_t::correlation_id`로 들어가고 response의 `X-Correlation-Id`로 전파된다.
middleware가 `before(...)`에서 `json_response(...)`를 설정하면 runtime은 handler를 호출하지
않고 해당 JSON response를 반환한다. `map_health(...)`, `map_readiness(...)`,
`map_liveness(...)`는 `app.health()` report를 HTTP endpoint로 노출한다.

`listen(...)`은 `http://`와 `https://` endpoint를 모두 받는다. `https://` endpoint를
사용하면 `configure_tls(...)`로 server certificate와 private key를 설정해야 한다. TLS 설정 public
표면은 파일 경로, PEM data, reload policy 같은 framework 값만 사용하고 OpenSSL 또는
Boost.Asio SSL 타입을 노출하지 않는다.

HTTP runtime은 `hosted_service_t`로 app lifecycle에 묶인다. `Boost.Beast`, `Boost.Asio`,
OpenSSL/SSL context 타입은 runtime 구현에만 있고 public header에는 나타나지 않는다. HTTP error response는
`framework_error_kind_t`를 기반으로 `400`, `404`, `405`, `500`, `503`, `504`로 매핑한다.

handler 안에서 다른 channel로 request를 보낼 때도 호출자는 낮은 수준의 request/reply template
쌍이나 blocking wait를 보지 않아야 한다. `.NET`의 `await client.RequestAsync<TReply>(...)`와
같은 읽기 수준을 C++에서는 아래처럼 표현한다.

샘플 namespace에서는 `using zlink::framework::task_t;`를 두고 `task_t<T>`처럼 짧게 쓴다.
`zlink::framework::task_t<T>`를 handler signature마다 반복하면 async 의미보다 namespace
노이즈가 먼저 보이기 때문이다. framework public contract 문서에서는 전체 이름을 쓸 수 있지만,
application sample과 guide 예제는 짧은 alias를 기본으로 한다.

```cpp
task_t<match_bingo_api_res_t> handle(const match_bingo_api_req_t &request)
{
    allocate_bingo_room_res_t allocated = co_await _client
      .request(sample_names_t::play_channel, allocate_bingo_room_req_t { request.mode })
      .async<allocate_bingo_room_res_t>();

    co_return match_bingo_api_res_t { allocated.room_id };
}
```

샘플 handler는 `.async().result().value()`로 결과를 직접 꺼내지 않는다. 그런 코드는
handler가 runtime 안에서 blocking wait를 수행하는 것처럼 보이고, 모든 언어 버전에서 같은 async
모델을 제공한다는 목표와 맞지 않다.

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
        handlers.on_event<order_handler_t, order_created_t>(
          "orders",
          "orders.created",
          &order_handler_t::on_created);
    }
};
```

## 13. Configuration 과 Logging

configuration은 JSON, environment variables, CLI args를 core 표면으로 둔다.

```cpp
namespace zlink::framework {

class config_builder_t {
public:
    config_builder_t &load_json(std::string path);
    config_builder_t &load_json(std::string path, optional_t optional);
    config_builder_t &load_env(std::string prefix);
    config_builder_t &load_cli(int argc, char **argv);
    config_builder_t &use_environment(std::string name);
    std::string environment() const;
    bool is_environment(std::string_view name) const;
    template<typename T> std::optional<T> bind(std::string prefix) const;
    template<typename T> T bind_required(std::string prefix) const;
};

class logging_builder_t {
public:
    logging_builder_t &use_console();
    logging_builder_t &use_file(std::string path);
    logging_builder_t &use_rotating_file(
      std::string path,
      rotating_file_options_t options = {});
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

JSON loader는 `nlohmann/json`을 사용한다. YAML은 필요하면 configuration extension으로
둔다. metrics와 health 표면은 core 관찰 기능으로 둔다. exporter, label schema,
tracing hook은 공통 message-flow tracing 계약의 observer와 runtime control을 구현한다.

### 13.1 message-flow dispatch error event

미등록 메시지와 dispatch 실패 관측은 메시지 흐름 observer의 `event_id=zlink.dispatch_error`,
`outcome=failed` event로 처리한다.
channel 별, spot 별 observer 등록은 이 버전의 공개 계약이 아니다. request 실패는 reply path 가 있으면
error reply 로 끝나고, local actor call 처럼 reply frame 이 없는 경로는 `task_t` 또는 pending operation
을 framework error 로 완료한다. one-way 실패는 drop 되지만 기본 로그, counter, message-flow event 를 남긴다.

```cpp
class dispatch_options_t
{
  public:
    dispatch_options_t &message_flow(message_flow_log_mode_t mode);
    dispatch_options_t &set_message_flow_observer(
      std::shared_ptr<message_flow_observer_t> observer);

    dispatch_options_t &set_message_flow_observer(
      std::function<void (const message_flow_event_t &)> observer);

    dispatch_options_t &set_runtime_error_sink(
      std::shared_ptr<runtime_error_sink_t> sink);

    dispatch_options_t &set_runtime_error_sink(
      std::function<void (const runtime_error_event_t &)> sink);
};
```

`message_flow_event_t`의 dispatch error event는 `surface`, `message_kind`, `reason`, `action`,
`packet_name`, `channel_name`, `topic`, `spot_rid`, `actor_id`, `source_rid`,
`correlation_id`를 담는 snapshot이다. native message 소유권, frame 참조와 exception object는
포함하지 않는다. Observer 실패는 `runtime_error_event_t`로 변환해 별도 sink에 전달한다.

시작 전 기본 mode는 `configure_dispatch().message_flow(...)`에서 정한다. 실행 중 mode 변경은
`app_t::set_message_flow_mode(...)`만 소유하며, channel이나 Spot별 toggle은 제공하지 않는다.

```cpp
app.add_zlink_framework([](auto &options) {
  options.configure_dispatch()
    .set_message_flow_observer(
      std::make_shared<my_message_flow_observer>());
});
```

## 14. C++ 고유 계약

### 14.1 Backpressure

SPOT과 STREAM의 backpressure는 public **call object, timeout, result error kind**로만 관찰한다.

- **application handler가 pending queue를 직접 resume하거나 poller readiness를 다루는 API를 두지
  않는다.**
- **기본 정책은 무한 queue가 아니다.** queue 상한·submit timeout·overflow 정책은 framework runtime
  설정으로 닫고, **한도 초과는 실패 result로 돌려준다**(`request_rejected` 등).

### 14.2 Handler filter

**filter는 `handler_invocation_context_t`로 descriptor·dispatch context·immutable message payload를
읽는다.** **payload를 바꾸려면 `next()` 결과 대신 새 `message_t`를 반환한다.**

filter의 등록 순서·`next` 의미·scope는 [framework API §8.1](../../../05-framework-api.ko.md)이
소유한다.

### 14.3 Public surface 경계

- **public surface는 native socket, poller, callback userdata를 직접 노출하지 않는다.**
- **handler public contract는 `contracts/handlers/*`가 소유한다.** handler descriptor map, DI
  resolve, serializer 호출 순서와 dispatch lookup은 public signature에 노출하지 않는다.
- **handler template 코드는 handler shape 검사와 type-erased 호출로 제한한다.** pending queue,
  recv loop, monitoring event 생성 구현을 `contracts/detail/*`에 넣지 않는다.


### 14.4 Timer 실행

**C++ framework는 timer callback 직렬화를 위한 별도 queue나 자체 timer scheduler를 만들지 않는다.**
CAPI timer와 CAPI SPOT dispatch event 뒤의 recv 경계를 그대로 사용한다.

**같은 MeshNode의 Spot ordering과 handler concurrency도 CAPI SPOT dispatch event 경계를 따른다.**
framework가 별도 스케줄러를 추가하지 않는다.

**CPU-bound이거나 blocking 가능성이 있는 handler는 framework core의 offload 실행으로 넘긴다**
(§15.7 worker).

### 14.5 Actor gateway 결정

| 항목 | 결정 |
|------|------|
| **`actor_ref_t` public 형태** | node routing id, actor id, **generation**을 담는 C++ 값 타입. **native 내부 ref를 그대로 노출하지 않는다** |
| **session 생성** | session 구현체는 **DI에서 resolve한다.** handler registry callback은 낮은 수준 확장 표면으로만 둔다 |
| **remote ActorGateway locator codec** | wire metadata는 **runtime 내부 frame으로 숨긴다.** application에는 `actor_ref_t`와 session actor 표면만 보인다 |
| **actor factory 중복 정책** | 같은 actor id 중복은 **`actor_already_exists`**, actor id/type 불일치는 **`actor_type_mismatch`** 로 보고한다 |

**`actor_ref_t`의 `node_rid`·`actor_id`·`generation`은 bind·relay·push round-trip에서 보존된다.**
**local actor relay와 remote actor relay는 같은 public 표면을 쓴다.**

## 15. Public 타입 카탈로그

**이 절은 위 절들이 다루지 않은 public 타입을 채운다.** 여기 없는 `*_state_t`·`*_snapshot_t`는
**runtime 내부 상태**이며 공개 계약이 아니다.

### 15.1 Dispatch 오류 계약

Dispatch 실패는 별도 event type을 만들지 않고 §15.3의 `message_flow_event_t`로
표현한다. `surface`, `message_kind`, `reason`, `action`의 닫힌 값과 조건부 field 규칙은
[메시지 흐름 추적 §3~§4](../../52-message-flow-tracing.ko.md)이 소유한다.

### 15.2 Dispatch 실행 정책

```cpp
enum class handler_execution_t;         // handler 실행 방식
enum class unhandled_dispatch_action_t; // 처리되지 않은 dispatch의 처리
struct unhandled_dispatch_options_t;
class  dispatch_diagnostics_options_t;  // read-only 진단 옵션
struct dispatch_options_t;
```

### 15.3 메시지 흐름 관측

```cpp
enum class message_flow_log_mode_t;   // off, errors_only(기본), key_transitions, verbose
enum class message_flow_phase_t;      // received, admitted, dispatched, completed, replied, sent, reply_received, backpressured, dropped
enum class message_flow_outcome_t;    // succeeded, failed, backpressured, dropped, cancelled, shutdown
enum class flow_origin_t : std::uint8_t
{ inbound = 1, timer = 2, application = 3, lifecycle = 4 };
struct message_flow_event_t {
    std::string event_id;
    std::chrono::system_clock::time_point timestamp;
    std::optional<message_flow_phase_t> phase;
    std::string surface;
    std::string message_kind;
    std::string outcome;
    std::optional<std::string> reason;
    std::optional<std::string> action;
    std::optional<std::string> mesh_name;
    std::optional<std::string> channel_name;
    std::optional<zlink::routing_id_t> source_rid;
    std::optional<zlink::routing_id_t> target_rid;
    std::optional<std::string> packet_name;
    std::optional<std::string> topic;
    std::optional<zlink::routing_id_t> spot_rid;
    std::optional<std::string> actor_id;
    std::optional<std::string> correlation_id;
    std::optional<std::string> flow_id;
    std::optional<flow_origin_t> flow_origin;
    std::optional<std::uint64_t> remote_snapshot_count;
    std::optional<std::uint64_t> remote_admitted_count;
    std::optional<std::uint64_t> remote_dropped_count;
    std::optional<std::uint64_t> local_snapshot_count;
    std::optional<std::uint64_t> local_admitted_count;
    std::optional<std::uint64_t> local_dropped_count;
    std::optional<std::uint64_t> target_count;
    std::optional<std::uint64_t> drop_count;
    std::optional<std::int64_t> message_size_bytes;
    std::optional<double> duration_seconds;
};
class message_flow_observer_t {
public:
    virtual task_t<void> on_message_flow(const message_flow_event_t &event) = 0;
};
struct runtime_error_event_t {
    std::string event_id; // zlink.runtime_error
    std::chrono::system_clock::time_point timestamp;
    std::string kind;     // observer_failed
    std::string source;   // message_flow_observer
    std::string reason;
};
class runtime_error_sink_t {
public:
    virtual task_t<void> on_runtime_error(const runtime_error_event_t &event) = 0;
};
```

의미는 [메시지 흐름 추적](../../52-message-flow-tracing.ko.md)과
[흐름 상관관계](../../53-flow-correlation.ko.md)가 소유한다.

### 15.4 Health

```cpp
enum class health_status_t;             // healthy, degraded, unhealthy
enum class health_check_scope_t { readiness, liveness, readiness_and_liveness };

struct health_check_result_t
{
    std::string name, component;
    health_status_t     status = health_status_t::healthy;
    health_check_scope_t scope = health_check_scope_t::readiness_and_liveness;
    std::string message;
};

struct health_report_t
{
    health_status_t status, readiness, liveness;
    std::vector<health_check_result_t> checks;
    bool ready () const noexcept;   // readiness != unhealthy
    bool live  () const noexcept;   // liveness  != unhealthy
};

class health_builder_t
{
public:
    health_builder_t &add_zlink_runtime_check (std::string name = "zlink.runtime");
    health_builder_t &add_channel_check        (std::string name);
    health_builder_t &add_location_check       (std::string name);
    health_builder_t &add_stream_endpoint_check(std::string name);
    health_builder_t &add_hosted_service_check (std::string name);

    health_report_t report () const;
};
```

**`readiness`와 `liveness`를 분리한다.** 트래픽을 받을 준비(readiness)와 프로세스 생존(liveness)은
다른 질문이다. **`degraded`는 `ready()`·`live()`를 막지 않는다.**

### 15.5 HTTP route와 middleware

**HTTP hosting 시나리오는 [60](60-http-hosting.ko.md)·[61](61-embedded-http-server.ko.md)이
소유한다.** 여기서는 public 타입만 고정한다.

```cpp
enum class http_method_t { get, post, put, delete_ };

struct http_context_t;    // 요청 처리 문맥
struct http_request_t;
struct http_response_t;

class http_route_t
{
public:
    http_method_t method;
    std::string   path;
    std::string   handler_name;
    bool context_response_precedence = false;  // context가 만든 response를 우선한다
    bool validates_json_content_type = true;   // JSON content type을 검증한다
};

struct http_middleware_t
{
    std::string name;
    std::function<std::shared_ptr<void> ()> create_instance;
    std::function<void (service_provider_t &, http_context_t &, const std::shared_ptr<void> &)> before;
    std::function<void (service_provider_t &, http_context_t &, const std::shared_ptr<void> &)> after;
};

struct http_endpoint_t { std::string uri; std::optional<http_tls_options_t> tls; };
struct http_server_options_t;
struct http_tls_options_t;
class  http_tls_options_builder_t;
```

- **middleware는 `before`/`after` 쌍이다.** `next` delegate 방식이 아니다 —
  [handler filter](../../../05-framework-api.ko.md)와 모양이 다르다.
- **middleware 인스턴스는 `create_instance`로 만들고 DI provider를 함께 받는다.**

### 15.6 Location store

```cpp
class peer_location_store_t;
class spot_location_store_t;
class actor_location_store_t;
class route_location_store_t;
class location_change_stamp_store_t;
```

**store 계약과 owner lease 의미는 [location runtime §3](../../40-location-runtime.ko.md)이
소유한다.** Redis 구현의 key 규약은 [41](../../41-location-store-redis.ko.md)이 소유한다.

```cpp
enum class location_write_status_t;
struct location_write_intent_t;
struct location_write_result_t;
struct location_owner_token_t;
struct owner_lease_renewal_t;
enum class location_auto_connect_type_t;
enum class location_change_type_t;
```

### 15.7 Worker

```cpp
enum class worker_completion_mode_t;
class worker_scheduler_t;

struct worker_options_t {
    std::size_t min_threads;
    std::size_t max_threads;
    std::chrono::milliseconds idle_timeout;
    std::size_t max_queue_length;
};

template <typename TResult> class worker_call_t
{
public:
    worker_call_t &timeout (std::chrono::milliseconds value);
    void submit ();
    task_t<TResult> async ();
    task_t<TResult> yield ();
};
```

**worker는 spot·session 실행 문맥 밖에서 실행하는 작업이다.** 완료를 원래 실행 문맥에서 재개하는
규칙은 [비동기 실행 정책](../../../04-async-execution-policy.ko.md)이 소유한다.

### 15.8 Timer

```cpp
struct timer_failure_event_t;   // handler 실패. 계속 실행 / timer 중단을 구분한다
```

timer 등록 검증은 [stage-wrapper §4.1](../../25-stage-wrapper-on-spot.ko.md)이 소유한다.

### 15.9 오류 경계

```cpp
class framework_exception_t;                    // code() -> std::error_code
template <typename T> class result_t;
```

동기 validation과 명시적인 결과 객체를 반환하는 API는 `result_t<T>`로 실패를 돌려준다. 비동기 call의
`async()`는 실패하면 같은 오류 정보를 가진 `framework_exception_t`를 throw한다. 오류 code는
`framework_exception_t::code()`의 `std::error_code`로 노출한다.


### 15.10 Transport

```cpp
enum class transport_scheme_t { tcp, ipc, tls, websocket, websocket_tls };

class transport_endpoint_t
{
public:
    transport_endpoint_t (transport_scheme_t scheme, std::string uri);
};
```

**endpoint는 scheme과 URI를 함께 갖는다.** scheme→transport 매핑의 의미는
[Stream Connector §3](../../../stream-connector/32-stream-connector.ko.md)이 소유한다.

### 15.11 등록 builder

**등록 표면은 builder 계층이다.** 각 builder가 자기 역할의 설정만 소유한다.

```cpp
class fanout_channel_builder_t;               // fanout channel
class mesh_node_builder_t;                    // physical RouteMesh node
class mesh_channel_builder_t;                 // logical ChannelName membership
class mesh_peer_connections_t;                // manual peer intents
class group_builder_t;                        // handler group
class handler_options_builder_t;              // handler 옵션
class codec_options_builder_t;                // codec registry
class metadata_policy_builder_t;              // 전달할 metadata key
class stream_compression_options_builder_t;   // STREAM 압축
```

- RouteMesh ChannelName과 classic fanout channel은 서로 다른 namespace와 socket 계약이다.
- Spot·Actor 등록은 owner `mesh_node_builder_t`에 둔다.

```cpp
enum class drain_force_reason_t;
```

drain 정책의 의미는 [Graceful Drain §5](../../54-graceful-drain-handoff.ko.md)가 소유한다.

### 15.12 Channel 표면

```cpp
enum class channel_capability_t;              // server, client, publisher, subscriber
enum class route_handler_kind_t;              // route handler 종류
struct route_handler_context_t;
struct route_handler_registration_t;
struct channel_runtime_options_t;
struct mesh_channel_runtime_options_t;
struct channel_server_socket_runtime_options_t;
struct channel_reliability_event_t;           // 연결 신뢰성 event
class  channel_outbound_exchange_t;           // outbound 교환 표면
class  relay_call_t;
class  bound_session_send_call_t;
```

### 15.13 SPOT 표면

```cpp
enum class spot_handler_kind_t { packet, subscription };
enum class actor_handler_kind_t { send, request };

struct spot_route_t
{
    node_rid_t  node_rid;
    spot_rid_t  spot_rid;
    std::string spot_name;
};

struct accepted_spot_route_channel_t
{
    std::string              channel_name;
    std::vector<std::string> manual_connections;
};

struct spot_info_t;                       // 조회 결과. spot rid만 담는다
class spot_handle_t
{
public:
    std::string_view mesh_name() const;
    spot_rid_t spot_rid() const;
};

class spot_handle_resolver_t
{
public:
    virtual ~spot_handle_resolver_t() = default;
    virtual task_t<std::optional<spot_handle_t>> resolve_spot_handle(
      std::string mesh_name,
      spot_rid_t spot_rid) = 0;
    virtual task_t<std::optional<spot_handle_t>> resolve_actor_spot_handle(
      std::string mesh_name,
      std::string actor_id) = 0;
};

struct spot_packet_context_t;             // packet handler가 받는 문맥
struct spot_packet_descriptor_t;
struct spot_handler_descriptor_t;
struct spot_lifecycle_callbacks_t;        // 생성·초기화·종료
struct spot_actor_admission_callbacks_t;  // actor join admission
enum  class spot_accept_reject_result_t;  // admission 결과
class spot_manager_t
{
public:
    spot_create_result_t create_spot(std::string mesh_name, std::string spot_name);
    spot_create_result_t create_spot(
      std::string mesh_name,
      std::string spot_name,
      const message_t &request);

    template <typename TRequest>
    spot_create_result_t create_spot(
      std::string mesh_name,
      std::string spot_name,
      const TRequest &request);

    spot_create_result_t get_or_create_spot(
      std::string mesh_name,
      std::string spot_name,
      spot_rid_t spot_rid);
    spot_create_result_t get_or_create_spot(
      std::string mesh_name,
      std::string spot_name,
      spot_rid_t spot_rid,
      const message_t &request);

    template <typename TRequest>
    spot_create_result_t get_or_create_spot(
      std::string mesh_name,
      std::string spot_name,
      spot_rid_t spot_rid,
      const TRequest &request);

    task_t<std::optional<spot_info_t>> find_spot(
      std::string mesh_name,
      spot_rid_t spot_rid) const;
    task_t<std::vector<spot_info_t>> list_spots(std::string mesh_name) const;
    task_t<bool> close_spot(std::string mesh_name, spot_rid_t spot_rid);
};
```

**lifecycle callback의 호출 순서는 [MeshNode §7](../../21-mesh-node.ko.md)가 소유한다** —
handler 구성 → 생성 callback → **수락된 경우에만** 초기화 → 종료는 한 번.

### 15.14 Actor 표면

```cpp
struct actor_placement_t
{
    std::optional<node_rid_t> preferred_node_rid;
    std::optional<std::string> route_mesh;
};

struct actor_membership_t
{
    actor_ref_t actor;
    std::string actor_type;
    std::uint64_t membership_epoch;
};

struct actor_join_request_t
{
    actor_ref_t actor;
    std::string actor_type;
    std::uint64_t expected_membership_epoch;
};

struct actor_ref_snapshot_t
{
    node_rid_t    node_rid;
    std::string   actor_id;
    std::uint64_t generation = 0;

    static actor_ref_snapshot_t from (const actor_ref_t &);
    actor_ref_t   to_actor_ref (std::string actor_type) const;
};

struct actor_join_reply_t;                // join 결과
class actor_send_call_t
{
public:
    actor_send_call_t &timeout(std::chrono::milliseconds timeout);
    actor_send_call_t &metadata(std::string key, std::string value);
    submit_result_t try_submit();
    submit_result_t submit();
};

class actor_request_call_t
{
public:
    actor_request_call_t &metadata(std::string key, std::string value);
    actor_request_call_t &timeout(std::chrono::milliseconds timeout);

    template <typename TReply>
    task_t<TReply> async();

    template <typename TReply>
    task_t<TReply> yield();
};

class actor_client_t
{
public:
    virtual ~actor_client_t() = default;

    template <typename TMessage>
    actor_send_call_t send_to_actor(
      std::string mesh_name,
      actor_ref_t actor_ref,
      TMessage message);

    template <typename TRequest>
    actor_request_call_t request_to_actor(
      std::string mesh_name,
      actor_ref_t actor_ref,
      TRequest request);
};

class actor_directory_t
{
public:
    virtual ~actor_directory_t() = default;
    virtual task_t<std::optional<actor_ref_t>> find(
      std::string mesh_name,
      std::string actor_id) = 0;
    virtual task_t<actor_ref_t> ensure(
      std::string mesh_name,
      std::string actor_id,
      message_t create_request,
      actor_placement_t placement = {}) = 0;

    template <typename TCreation>
    task_t<actor_ref_t> ensure(
      std::string mesh_name,
      std::string actor_id,
      TCreation create_request,
      actor_placement_t placement = {});
};
class  actor_join_call_t;
class  relay_request_call_t;
```

**`generation`이 stale actor ref를 걸러낸다.** identity와 authority의 의미는
[spot-actor §1](../../23-spot-actor.ko.md#1-identity와-authority)이 소유한다. Forwarding window가
끝난 old ref의 실패 의미는
[spot-actor §10.4](../../23-spot-actor.ko.md#104-straggler-forwarding)이 소유한다.

### 15.15 STREAM 표면

```cpp
enum class stream_message_kind_t : std::uint8_t;   // wire kind
enum class stream_header_flags_t : std::uint8_t;   // wire flags
enum class stream_codec_t        : std::uint8_t;
enum class stream_session_error_t;                 // session에 귀속되는 오류

enum class stream_close_reason_t : std::uint8_t
{
    client_close = 1, idle_timeout = 2, heartbeat_timeout = 3,
    server_drain = 4, protocol_error = 5, transport_error = 6
};

struct stream_header_t;
class stream_compression_codec_t
{
public:
    virtual zlink::message_t compress   (const zlink::message_t &payload) const = 0;
    virtual zlink::message_t decompress (const zlink::message_t &payload) const = 0;
};
```

**wire 값이 계약이다.** `stream_close_reason_t`의 1~6은
[Stream Connector §4.6](../../../stream-connector/32-stream-connector.ko.md)의 `session-closing` payload와 같은 값이다.
**enum을 정수로 cast해 wire 값으로 쓰지 않는다** — codec이 명시적으로 변환한다.

### 15.16 Location 표면

Store-neutral MeshNode descriptor, Spot·Actor location, owner lease, Actor transfer authority와 공식 Redis
구현의 정확한 선언은 [03 Location Store·Redis](03-location-store.ko.md)가 소유한다. 역할별 store를 따로
등록하지 않으며 root에 전달한 `location_store_t` 한 인스턴스가 필요한 capability를 제공한다.

### 15.17 Runtime event

```cpp
enum class runtime_event_severity_t;
enum class socket_event_kind_t : std::uint8_t {
    connected = 0,
    connection_ready = 1,
    disconnected = 2,
    handshake_failed = 3,
    peer_admission_changed = 4,
    closed = 5
};
struct socket_event_payload_t;
enum class spot_event_kind_t;       struct spot_event_payload_t;
enum class actor_event_kind_t;      struct actor_event_payload_t;
enum class stream_event_kind_t;     struct stream_event_payload_t;
enum class location_event_kind_t;   struct location_event_payload_t;
struct drain_event_t;
struct spot_timer_diagnostic_t;
struct runtime_event_base_t;
class  runtime_event_publisher_t;
```

**source별로 표면을 나누는 근거는 [runtime-monitoring §2](../../50-runtime-monitoring.ko.md)가
소유한다.** timer 실패는 **timer 실행을 계속하는 실패**와 **timer가 중단된 실패**를 구분한다.

**metric:**

```cpp
enum class metric_instrument_kind_t;   // counter / histogram / gauge
enum class metric_temporality_t;
struct metric_event_payload_t;
```

계기 카탈로그는 [runtime-metrics](../../51-runtime-metrics.ko.md)가 소유한다.

### 15.18 실행 문맥

```cpp
class serial_turn_t;          // spot·session의 직렬 실행 턴
class serial_turn_scope_t;    // RAII
struct ambient_context_hooks_t;  // flow 등 ambient 문맥 훅
```

**같은 spot의 dispatch가 직렬화되는 근거는
[stage-wrapper §3](../../25-stage-wrapper-on-spot.ko.md)이 소유한다.**

### 15.19 Codec

```cpp
struct encoded_payload_t;              // codec이 만든 payload
template <typename T> struct is_json_serializable_t;
template <typename T> struct is_json_deserializable_t;

class serializer_registry_t
{
public:
    template <typename T>
    serializer_registry_t &add (typename serializer_t<T>::serialize_fn_t   serialize,
                                typename serializer_t<T>::deserialize_fn_t deserialize,
                                std::string content_type = "application/octet-stream");

    std::string content_type (std::type_index type) const;
};
```

**codec extension이 `add<T>(...)`로 payload 타입별 serializer를 등록한다.** `content_type`은
framework·HTTP client·stream connector가 공유한다.

### 15.20 Handler

```cpp
enum class handler_kind_t;   // request / send / publish
```


### 15.21 Configuration 조회

```cpp
class configuration_model_t;   // 계층으로 합친 설정
enum class optional_t;         // 필수/선택

class configuration_section_t
{
public:
    configuration_section_t (const configuration_model_t &model, std::string prefix);

    std::string key () const;                        // 이 section의 prefix
    bool contains (std::string_view key) const;      // 하위 key 존재 여부
    // 타입별 조회는 model이 제공한다
};
```

**section은 prefix로 잘라낸 view다.** 설정 소스를 계층으로 합치는 규칙은
[01 §5](01-system-structure.ko.md)가 소유한다.

### 15.22 Codec 등록

```cpp
class codec_registration_context_t
{
public:
    explicit codec_registration_context_t (serializer_registry_t &serializers);

    template <typename TPayload>
    codec_registration_context_t &add_serializer (
      typename serializer_t<TPayload>::serialize_fn_t serialize,
      typename serializer_t<TPayload>::deserialize_fn_t deserialize,
      std::string content_type = "application/octet-stream");
};
```

**codec extension이 `add_serializer<TPayload>(...)`로 serializer를 등록한다.** 같은 registry를 framework·HTTP
client·stream connector가 공유한다([channel-messaging §6](../../11-channel-messaging.ko.md)).

### 15.23 Location watch

```cpp
struct location_watch_filter_t
{
    location_kind_t             kind = location_kind_t::peer;
    std::optional<std::string>  mesh_name;
    std::optional<route_kind_t> route_kind;
};

enum class location_change_type_t { upserted = 1, removed = 2, expired = 3 };
```

**watch는 필터로 좁힌다.** `expired`는 owner lease 만료이며 `removed`와 구분한다
([location runtime §2.4](../../40-location-runtime.ko.md#24-owner-lease)).

### 15.24 설치 header 제외 규칙

정식 public type 카탈로그에 없는 runtime state, snapshot, access와 implementation type은 설치되는
header에 선언하거나 노출하지 않는다. Application이 구현 세부 이름을 include하거나 forward
declaration으로 참조해야 하는 구성을 공개 계약으로 인정하지 않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[스펙 목차](README.ko.md) | [이전: C++ 시스템 구조](01-system-structure.ko.md) | [다음: C++ HTTP Hosting](60-http-hosting.ko.md)
<!-- framework-adapter-nav:bottom:end -->
