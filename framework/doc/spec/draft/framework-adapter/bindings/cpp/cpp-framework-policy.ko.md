<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework For C++](README.ko.md) | [다음: ZLink Framework C++ Interface Design](cpp-framework-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./cpp-channel-messaging.ko.md) | [SPOT](./cpp-spot.ko.md) | [STREAM](./cpp-stream.ko.md)

# Draft -- ZLink Framework C++ Policy

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++`에서 `ZLink Framework`를 어떤 제품과 API 표면으로
> 만들지 정리하기 위한 정책 문서다.
> 구현이 끝난 뒤에는 실제 public header, 테스트, 샘플, 패키지 구조와 맞춰 정식
> spec 문서로 나누어 반영한다.
>
> 이 문서는 `framework/doc/spec` 아래의 공통 framework 정책을 상위 기준으로 따른다.
> 언어별 스펙은 공통 정책을 반드시 반영해야 하며, 이 문서는 그 공통 정책을 `C++`
> 언어 특성과 standalone host/runtime 형태에 맞춰 구체화한다.

## 1. 포지셔닝

`ZLink Framework for C++`는 `HTTP` 또는 `Web` 프레임워크가 아니다. 이 프레임워크는
실시간 메시징 애플리케이션을 만들기 위한 standalone host/runtime이다.

중심에 두는 개념은 아래와 같다.

- 메시지 송수신
- actor와 비슷하지만 네트워크와 라우팅을 먼저 고려하는 `SPOT`
- 여러 프로세스와 여러 노드를 전제로 하는 distributed runtime
- channel, topic, routing id 기반 routing
- 애플리케이션 lifecycle
- handler 실행과 오류 격리
- backpressure와 graceful shutdown

따라서 `C++` 프레임워크는 `HTTP route`나 `controller`를 중심으로 설명하지 않는다.
사용자가 raw socket, poll loop, service discovery 배선을 직접 다루지 않고,
서비스 등록, handler 등록, 메시징 client, spot, hosted service 같은 상위 개념으로
분산 메시징 앱을 구성하게 만드는 것이 목표다.

`C++` 문서는 기존 framework adapter 공통 초안을 그대로 반복하지 않는다. 공통 초안의
상호작용 모델, 메시지 모델, channel topology, naming policy를 반영한 뒤, `C++`
언어 특성에 맞는 세부 구현 사항을 이 문서와 하위 문서에서 구체화한다.

특히 `C++`에는 `.NET`, `Java`, `Node.js`처럼 기준으로 삼을 메이저 애플리케이션
프레임워크가 없으므로, 다른 언어보다 app, host, DI, handler registry, executor,
lifecycle 같은 기반 프레임워크 설계 내용을 더 많이 담는다. 이 내용은 공통 정책을
대체하는 것이 아니라, 공통 정책에서 다루지 않은 `C++` standalone framework 세부
스펙을 채우기 위한 것이다.

이 문서와 기존 `C++` 세부 초안의 bootstrap API가 다르면, 구현 전 정렬 작업에서 기존
세부 초안을 이 문서의 `C++` 상세 방향에 맞춰 갱신한다. 다만 공통 framework 정책과
충돌하는 내용이 발견되면 먼저 공통 정책을 확인하고, 필요하면 공통 정책을 갱신한 뒤
언어별 문서를 맞춘다.

## 2. 사용자 목표 표면

최종 사용자는 아래 수준의 코드로 메시징 앱을 시작할 수 있어야 한다.

```cpp
struct order_created_t {
    std::string order_id;
};

class order_service_t {
public:
    void save_created(const order_created_t &event);
};

class order_handler_t final {
public:
    explicit order_handler_t(order_service_t &service);
    void on_created(const order_created_t &event);
};

int main(int argc, char **argv)
{
    auto app = zlink::framework::app_t::create();

    app.use_zlink([](auto &zlink) {
        zlink.node("order-node")
          .discovery([](auto &discovery) {
              discovery.connect_registry("tcp://registry:5551");
          })
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

    app.services()
      .add_singleton<order_service_t>()
      .add_factory<order_handler_t>([](auto &services) {
          return std::make_unique<order_handler_t>(
            services.template get_required<order_service_t>());
      });

    app.handlers()
      .subscribe<order_created_t, order_handler_t>(
        "orders",
        "orders.created",
        &order_handler_t::on_created);

    return app.run(argc, argv);
}
```

위 코드는 정책 방향을 보여 주는 예시다. 실제 구현 전에는 아래를 함께 확정해야 한다.

- app 생성의 canonical 시작점은 `app_t::create()`다. 기존 세부 초안에 남아 있던
  이전 bootstrap 표기는 이 정책에 맞춰 정리한다.
- handler 등록은 member function pointer와 handler owner 타입을 함께 받는 형태를
  기본으로 한다. handler owner는 DI container에서 resolve한다.
- handler owner 타입이 명시되지 않는 축약형은 handler가 이미 service로 등록된 경우에만
  허용한다.
- `run`은 MVP에서 process exit code로 사용할 수 있는 `int`를 반환한다.
- typed payload 이름에서 packet key를 얻는 규칙을 serializer 정책과 맞춘다.

## 3. 권장 구현 루트

`C++` framework 구현은 아래 위치를 기준으로 둔다.

```text
framework/languages/cpp/
+-- include/zlink/framework/
|   +-- app/
|   +-- host/
|   +-- di/
|   +-- runtime/
|   +-- messaging/
|   +-- handlers/
|   +-- serialization/
|   +-- config/
|   +-- logging/
|   +-- observability/
|   +-- concurrency/
|   +-- modules/
|   +-- integrations/
|   +-- app.hpp
|   +-- host.hpp
|   +-- services.hpp
|   +-- handlers.hpp
|   +-- messaging.hpp
|   +-- runtime.hpp
|   +-- serialization.hpp
|   +-- config.hpp
|   +-- logging.hpp
|   +-- observability.hpp
|   +-- concurrency.hpp
|   +-- modules.hpp
|   +-- integrations.hpp
+-- src/
+-- tests/
+-- samples/
+-- CMakeLists.txt
```

public include path는 `zlink/framework/...` 아래로 제한한다. native core나 C binding
세부 헤더를 사용자가 직접 include해야만 동작하는 표면은 만들지 않는다.

## 3.1 외부 라이브러리 정책

`C++` framework core는 zlink runtime과 poller를 유일한 I/O 실행 기반으로 사용한다.
따라서 framework가 직접 노출하거나 직접 실행기로 삼는 dependency에는 아래
라이브러리를 넣지 않는다.

- `Boost.Asio`
- `Boost.Beast`
- `libuv`

이 라이브러리들은 각자 event loop나 비동기 I/O 모델을 제공하므로, framework
runtime의 중심에 들어오면 zlink poller와 책임이 겹친다. `C++` framework는 socket
readiness, send readiness, monitor event, shutdown drain을 zlink runtime 기준으로
설명해야 한다. 단, zlink core 내부 transport 구현이 사용하는 내부 dependency는 이
정책의 금지 대상이 아니다. 이 정책은 framework public header, framework 직접
dependency, framework executor 모델에 대한 제한이다.

JSON 구현은 `nlohmann/json` 하나로 고정한다. 설정 파일, JSON codec helper, 테스트
fixture에서 같은 JSON 타입과 같은 변환 규칙을 사용한다. 성능 특화 JSON parser가
필요해지더라도 public JSON 정책을 늘리지 않고, 내부 최적화 또는 별도 extension으로
검토한다.

DI는 MVP에서 자체 구현한다. 사용자는 항상 `zlink::framework`가 제공하는
`service_collection_t`, `service_provider_t`, lifetime API만 사용해야 한다.
`Boost.Ext.DI` 같은 외부 DI 라이브러리는 MVP 필수 dependency로 두지 않는다.

## 3.2 권장 라이브러리 목록

아래 표는 `C++` framework 구현에서 사용할 수 있는 라이브러리 기준이다. 원칙은
dependency를 public API 밖에 숨기는 것이다. 사용자는 `zlink::framework` 타입을 보아야
하며, 외부 라이브러리 타입을 handler, module, config callback 시그니처에서 직접
보지 않아야 한다.

| 영역 | 권장 라이브러리 | 링크 | 적용 범위 | 정책 |
|------|----------------|------|----------|------|
| runtime / I/O | zlink C++ binding | 내부 binding | 필수 | framework runtime의 유일한 I/O 기반이다. `context_t`, socket, discovery, spot, stream binding을 내부 substrate로 사용한다. |
| JSON | `nlohmann/json` | <https://github.com/nlohmann/json> | 필수 | 설정 파일, JSON codec helper, 테스트 fixture의 기준 JSON 구현으로 고정한다. |
| logging backend | `spdlog` | <https://github.com/gabime/spdlog> | MVP 후보 | public logging API 뒤에 숨긴다. 사용자는 `app.logging()`만 사용하고 `spdlog` logger나 sink 타입을 직접 받지 않는다. |
| string formatting | `{fmt}` | <https://github.com/fmtlib/fmt> | MVP 후보 | C++17 환경에서 내부 메시지 formatting에 사용한다. public API에는 `fmt::format_string` 같은 타입을 노출하지 않는다. |
| CLI args | `CLI11` | <https://github.com/CLIUtils/CLI11> | 선택 | `app.config().load_cli(...)` 구현 후보로 둔다. 간단한 MVP parser로 충분하면 필수 dependency로 올리지 않는다. |
| unit test | GoogleTest | <https://github.com/google/googletest> | 개발 의존성 후보 | framework C++ typed API 테스트에 사용한다. core 기존 테스트 방식과 충돌하지 않게 framework/languages/cpp 테스트에만 제한한다. |
| microbenchmark | google/benchmark | <https://github.com/google/benchmark> | MVP 이후 | handler dispatch, serializer, DI resolve hot path 측정이 필요할 때만 추가한다. 공식 perf 수치와 섞지 않는다. |
| YAML | `yaml-cpp` | <https://github.com/jbeder/yaml-cpp> | MVP 제외 | YAML configuration을 도입할 때만 extension으로 검토한다. MVP는 JSON, env, CLI만 지원한다. |
| Protobuf | Protocol Buffers C++ | <https://protobuf.dev/> | MVP 제외 | `zlink-codec-protobuf` extension 구현 후보로만 둔다. framework core 필수 dependency가 아니다. |
| FlatBuffers | FlatBuffers | <https://flatbuffers.dev/> | MVP 제외 | 별도 codec extension 후보로 둔다. framework core와 public handler registry에는 넣지 않는다. |

MVP 필수 외부 runtime dependency는 가능한 한 `nlohmann/json` 하나로 시작한다. logging,
formatting, CLI parsing은 구현 편의와 패키징 비용을 비교한 뒤 내부 dependency로만
추가한다. 테스트와 benchmark 라이브러리는 배포 runtime dependency가 아니다.

아래 라이브러리는 framework core 직접 dependency로 두지 않는다.

| 영역 | 라이브러리 | 제외 이유 |
|------|------------|----------|
| async I/O | `Boost.Asio` | zlink poller와 runtime event loop 책임이 겹친다. |
| WebSocket 구현 | `Boost.Beast` | HTTP/Web framework로 오해될 수 있고, zlink transport 또는 integration 경계에서 다루는 편이 맞다. |
| event loop | `libuv` | zlink poller와 별도 event loop를 함께 운영하면 shutdown, timer, readiness 의미가 복잡해진다. |
| DI | `Boost.Ext.DI` | MVP에서는 자체 container로 lifetime과 host shutdown 규칙을 직접 닫는다. |

## 4. 모듈 정책

### 4.1 App / Host

`app`과 `host`는 프로그램 lifecycle을 관리한다. 사용자는 `main`에서 app을 만들고
구성한 뒤 `run`을 호출한다.

필수 기능은 아래와 같다.

- startup
- shutdown
- signal handling
- graceful stop
- worker lifecycle
- drain
- runtime bootstrapping
- hosted service 시작과 종료 순서 관리

`app_t`는 깊은 모듈이어야 한다. 호출자가 zlink context, socket 생성 순서, poller
실행 순서를 기억해야 한다면 host 추상화가 실패한 것이다.

### 4.2 DI Container

DI container는 서비스 wiring을 위한 최소 기능만 제공한다.

필수 public 등록 표면은 아래 정도로 제한한다.

```cpp
app.services().add_singleton<order_service_t>();
app.services().add_transient<order_handler_t>();
app.services().add_factory<clock_t>([](service_provider_t &services) {
    return std::make_unique<system_clock_t>();
});
```

지원 lifetime은 MVP에서 `singleton`, `transient`만 둔다. `scoped` lifetime은
request scope, stream session scope, spot scope 중 무엇을 의미하는지 먼저 닫은 뒤
추가한다.

금지 방향은 아래와 같다.

- C++ reflection을 전제로 한 API
- annotation 또는 macro에 강하게 의존하는 등록 모델
- 생성자 의존성을 숨기는 service locator 남용
- 외부 DI 라이브러리의 injector, binding DSL, scope 타입을 public API로 노출하는 모델

기본 방향은 constructor injection이다. MVP에서는 자체 container를 구현하고,
생성자 자동 추론보다 명시 factory와 명시 등록을 먼저 지원한다. lifetime registry,
service collection, module registration 의미는 framework가 소유한다. 이렇게 해야
handler lifecycle, hosted service stop 순서, shutdown 중 resolve 금지 같은 규칙을
host와 함께 닫을 수 있다.

MVP의 생성 규칙은 아래처럼 닫는다.

- `add_singleton<T>()`, `add_transient<T>()`는 기본 생성 가능한 타입만 자동 생성한다.
- 생성자 의존성이 있는 타입은 `add_factory<T>()`로 등록한다.
- handler owner에 의존성이 있으면 handler owner도 `add_factory<T>()` 또는 동등한
  명시 등록을 사용한다.
- 생성자 자동 wiring은 MVP 범위에 넣지 않는다.

내부 구현 경계는 아래처럼 나눈다.

| 계층 | 예시 타입 | 노출 여부 |
|------|----------|----------|
| public | `service_collection_t`, `service_provider_t`, `service_lifetime_t` | 노출 |
| internal | `service_registry_t`, `service_descriptor_t`, `factory_adapter_t` | 숨김 |

`Boost.Ext.DI`는 나중에 생성자 자동 wiring 요구가 커졌을 때 내부 구현 후보로만
검토한다. 이 경우에도 public header와 샘플에는 외부 DI 타입을 노출하지 않는다.

### 4.3 Runtime Integration

runtime integration은 zlink core와 framework 사이의 가장 중요한 경계다.

권장 표면은 아래와 같다.

```cpp
app.use_zlink([](auto &zlink) {
    zlink.node("order-node")
      .discovery([](auto &discovery) {
          discovery.connect_registry("tcp://registry:5551");
      })
      .channel("orders", [](auto &channel) {
          channel.enable_server([](auto &server) {
              server.bind("tcp://0.0.0.0:7001");
          });
      })
      .spot_node("orders-spot", [](auto &spot_node) {
          spot_node.bind("tcp://0.0.0.0:7101");
          spot_node.use_discovery("orders");
      });
});
```

이 계층은 아래 책임을 가진다.

- zlink context 생성과 종료
- socket lifecycle 관리
- channel capability 연결
- spot node lifecycle
- service discovery 연결
- poller와 framework executor binding
- transport endpoint 검증

framework runtime은 `Boost.Asio`, `Boost.Beast`, `libuv` event loop를 중심 실행기로
사용하지 않는다. timer, readiness, graceful shutdown, worker wakeup은 zlink runtime과
framework executor 사이의 명시적인 binding으로 처리한다.

사용자가 native socket handle이나 poller 내부 규칙을 알아야 한다면 public surface가
너무 얕은 것이다.

### 4.4 Messaging Core

messaging core는 프레임워크의 중심 API다.

지원할 상호작용 모델은 아래와 같다.

- publish / subscribe
- request / reply
- direct routing
- broadcast
- multicast
- stream packet

권장 표면은 아래 방향으로 둔다.

```cpp
publisher.publish("orders", "orders.created", event);

auto reply = client.request<order_status_reply_t>(
  "orders",
  get_order_status_t{.order_id = order_id});

spot.send_to(target_node, target_spot, command);
```

send와 publish는 기본 async submit이다. public API에 `send_nonblocking` 같은 이름을
늘리지 않고, backpressure는 pending queue, timeout, ready notification으로 다룬다.

### 4.5 Spot / Actor Model

`SPOT`은 lightweight distributed endpoint다. actor와 비슷하지만 아래 차이를 명확히
둔다.

- network-first
- routing-aware
- distributed-aware
- local/remote transparency를 목표로 하지만, 필요하면 routing id를 드러낸다.

필수 기능은 아래와 같다.

- mailbox
- message queue
- handler execution
- routing-id addressing
- local/remote transparency
- spot lifecycle
- timer

일반 application handler는 channel/topic 중심으로 시작하고, 직접 `routing_id_t`를
다루는 API는 spot-to-spot 또는 운영 진단 경로에 제한한다.

### 4.6 Handler Framework

handler framework는 사용자가 메시지를 함수 수준에서 처리하게 만드는 계층이다.

권장 등록 표면은 아래와 같다.

```cpp
app.handlers()
  .subscribe<order_created_t, order_handler_t>(
    "orders",
    "orders.created",
    &order_handler_t::on_created);

app.handlers()
  .request<get_order_status_t, order_status_reply_t, order_handler_t>(
    "orders",
    "orders.status",
    &order_handler_t::get_status);
```

handler owner 타입을 명확히 드러내야 할 때는 아래 형태를 기본으로 본다.

```cpp
app.handlers()
  .subscribe<order_created_t, order_handler_t>(
    "orders",
    "orders.created",
    &order_handler_t::on_created);
```

이 경우 `order_handler_t`는 service collection에 등록되어 있어야 한다. 등록되지 않은
handler owner를 framework가 암묵적으로 생성하지 않는다.

내부 책임은 아래와 같다.

- topic routing
- packet key 해석
- deserialize
- handler instance resolve
- invoke
- exception handling
- retry 정책 적용
- reply dispatch

handler 실행 중 발생한 예외는 runtime thread를 죽이면 안 된다. 기본 정책은 예외를
격리하고, request는 실패 reply 또는 timeout/error로 닫고, event는 logging과
dead-letter 정책으로 넘기는 것이다.

### 4.7 Serialization

serialization은 message와 object 사이의 변환을 맡는다.

지원 후보는 아래와 같다.

- raw bytes
- JSON (`nlohmann/json`)
- Protobuf
- FlatBuffers
- custom codec

기본 인터페이스는 타입별 serializer 형태로 둔다.

```cpp
template <typename T>
class serializer_t {
public:
    message_t serialize(const T &value) const;
    T deserialize(const message_t &message) const;
};
```

MVP에서는 raw bytes와 `nlohmann/json` 기반 JSON codec을 기본으로 둔다. Protobuf와
FlatBuffers는 extension으로 둔다. codec 선택이 transport lifecycle과 섞이면 안 된다.
public serializer API는 `nlohmann::json`을 직접 요구하지 않는 typed serializer
추상화를 먼저 노출하고, JSON helper에서만 `nlohmann/json` 변환 규칙을 제공한다.

### 4.8 Concurrency / Execution Model

concurrency 계층은 handler 실행을 제어한다.

필수 기능은 아래와 같다.

- worker pool
- dispatch queue
- strand 또는 serialized execution
- message ordering
- backpressure
- handler timeout

중요한 정책은 사용자가 thread를 직접 만들고 관리하지 않게 하는 것이다. 사용자는
동시성 수준, ordering key, queue depth 같은 정책만 설정하고, 실제 thread와 poll loop
배선은 framework가 맡는다.

### 4.9 Backpressure / Flow Control

backpressure는 zlink framework의 핵심 강점으로 다룬다.

지원할 기능은 아래와 같다.

- send-ready callback
- HWM awareness
- queue depth 조회
- drop / retry policy
- bounded pending queue
- graceful drain

권장 표면은 아래와 같다.

```cpp
spot_context.on_send_ready([](send_ready_context_t &context) {
    context.resume_pending();
});
```

기본 정책은 무한 queue가 아니다. queue 상한, submit timeout, overflow 정책을
명시적으로 둘 수 있어야 한다.

### 4.10 Observability

observability는 운영 환경에서 필수 축이다.

포함 범위는 아래와 같다.

- logging
- metrics
- monitor events
- tracing hooks
- health status

권장 표면은 아래와 같다.

```cpp
app.logging().use_console();
app.metrics().add_runtime_metrics();
app.health().add_zlink_runtime_check();
```

MVP에서는 logging과 health를 먼저 둔다. metrics와 tracing은 event 이름, label
cardinality, exporter 정책을 정한 뒤 확장한다.

### 4.11 Configuration

configuration은 같은 설정을 코드, 파일, 환경 변수, CLI에서 일관되게 읽게 하는
계층이다.

지원 범위는 아래와 같다.

- JSON
- environment variables
- CLI args

권장 표면은 아래와 같다.

```cpp
app.config()
  .load_json("appsettings.json")
  .load_env("ZLINK_")
  .load_cli(argc, argv);
```

JSON loader는 `nlohmann/json`을 사용한다. YAML은 MVP 범위에 넣지 않는다. 설정 key
이름은 문서와 샘플에서 일관되게 유지한다. 설정 파일 구조가 runtime 내부 자료구조를
그대로 노출하면 안 된다.

### 4.12 Module System

module system은 서비스 등록, handler 등록, runtime 구성을 묶는 확장 단위다.

```cpp
class order_module_t final : public module_t {
public:
    void configure_services(service_collection_t &services) override;
    void configure_handlers(handler_registry_t &handlers) override;
    void configure_zlink(zlink_builder_t &zlink) override;
};
```

module은 아래 용도로 쓴다.

- handler 등록
- service 등록
- runtime 구성
- transport 추가
- observability 확장

module은 단순 파일 분리를 위한 개념이 아니다. 하나의 기능 영역이 필요로 하는
등록 지식을 한곳에 모아, 호출자가 순서를 기억하지 않아도 되게 하는 것이 목적이다.

### 4.13 Hosted Services

hosted service는 background worker를 위한 표면이다.

```cpp
class gateway_worker_t final : public hosted_service_t {
public:
    void start() override;
    void stop() override;
};
```

용도는 아래와 같다.

- subscriber loop
- heartbeat
- discovery maintenance
- retry worker
- periodic cleanup

hosted service는 app lifecycle에 묶인다. `stop`은 graceful shutdown timeout 안에서
끝나야 하며, 실패하면 host가 오류를 수집해 종료 경로로 넘긴다.

### 4.14 Discovery / Topology

Discovery와 topology는 zlink framework의 차별화 축이다.

제공할 기능은 아래와 같다.

- service discovery
- node registry
- spot registry
- gateway topology
- peer awareness
- topology snapshot

애플리케이션 코드는 가능하면 channel과 capability를 기준으로 연결을 설정한다.
직접 peer endpoint를 넣는 manual 연결도 지원하되, 같은 capability 안에서 Discovery와
manual 연결을 섞지 않는다.

### 4.15 STREAM 범위

framework MVP에서 `STREAM`은 packet 방식만 지원한다. 그중에서도 header는 framework가
정의한 `stream_header_t` 형식만 사용한다. raw stream session, 사용자 정의 header
framing, 임의 byte stream dispatch는 MVP 범위에 넣지 않는다.

이 제한은 `.NET` framework 쪽 STREAM 정책과 같은 방향이다. application handler가
transport별 framing이나 raw stream read loop를 직접 다루지 않게 하고, framework가
검증 가능한 packet header와 body 단위로 lifecycle, dispatch, backpressure를 닫기
위한 정책이다.

### 4.16 Transport Abstraction

transport abstraction은 zlink core가 제공하는 transport 의미를 framework 표면으로
감싼다. framework core는 별도 network I/O stack을 추가하지 않는다.

지원 후보는 zlink core 기준으로 아래와 같다.

- TCP
- IPC
- TLS
- WebSocket

프레임워크는 transport를 숨긴다. 사용자는 endpoint URI와 보안 옵션을 설정할 수
있지만, handler와 client 코드는 transport가 TCP인지 TLS인지 알 필요가 없어야 한다.
WebSocket 계열 transport가 필요해도 `Boost.Beast`를 framework core에 붙이는 방식이
아니라 zlink transport 또는 별도 integration 경계에서 다룬다.
PGM은 `C++` framework 지원 범위에 넣지 않는다.

### 4.17 Reliability Features

reliability 기능은 MVP 이후 확장까지 고려해 public 표면이 막히지 않게 설계한다.

지원 후보는 아래와 같다.

- retry
- timeout
- dead-letter
- drain
- graceful close
- request cancellation
- idempotency key hook

MVP에서는 timeout, graceful close, drain을 먼저 확정한다. retry와 dead-letter는
handler 재실행 의미, ordering, 중복 처리 정책이 필요하므로 별도 초안으로 분리한다.

### 4.18 Integration Layer

integration layer는 초기에는 최소 범위로 둔다.

후보는 아래와 같다.

- Kafka bridge
- gRPC bridge
- HTTP gateway

초기 구현은 bridge보다 framework core를 먼저 안정화한다. 외부 시스템 bridge는
messaging core, serialization, backpressure, observability가 정리된 뒤 추가한다.

## 5. MVP 우선순위

먼저 구현할 범위는 아래와 같다.

| 우선순위 | 영역 | 완료 기준 |
|:--:|------|----------|
| 1 | app / host | `app_t::create`, 구성, `int run`, signal handling, graceful shutdown |
| 2 | DI | 자체 container, public service API, singleton/transient/factory |
| 3 | runtime integration | context, socket, discovery, spot node lifecycle을 host가 관리 |
| 4 | handler framework | typed subscribe/request handler 등록과 invoke |
| 5 | messaging core | publish, request, send, reply dispatch |
| 6 | spot abstraction | spot lifecycle, mailbox, direct routing, publish |
| 7 | hosted services | start/stop lifecycle과 shutdown 연동 |
| 8 | logging / health | runtime 오류, handler 예외, runtime 상태를 볼 수 있는 최소 표면 |
| 9 | graceful shutdown | drain, worker stop, socket close 순서 검증 |

나중으로 미룰 범위는 아래와 같다.

- metrics
- tracing
- Kafka bridge
- advanced retry
- scoped lifetime
- Protobuf integration
- FlatBuffers integration
- HTTP gateway
- YAML configuration

## 6. 설계 원칙

이 정책 문서는 POSD 기준으로 아래 방향을 따른다.

- public API는 깊은 모듈이어야 한다. 호출자가 native socket 순서나 poller 세부
  규칙을 외워야 하면 실패다.
- runtime, serialization, handler dispatch 지식은 각각 한 모듈 안에 가둔다.
- 설정 파라미터를 많이 드러내기보다, 보수적인 기본값과 명시 extension point를
  먼저 둔다.
- 오류는 가능하면 정의로 없앤다. 예를 들어 shutdown 중 submit은 undefined behavior가
  아니라 명확한 failed result 또는 exception으로 닫는다.
- 비자명한 public surface는 최소 두 가지 대안을 비교한 뒤 선택한다.

## 7. 남은 결정 사항

구현 전에 아래 항목을 별도 초안이나 인터페이스 문서에서 닫아야 한다.

- `scoped` lifetime을 도입할 경우 scope 의미
- request cancellation과 timeout 결과 타입
- default executor의 worker 수와 queue 상한 기본값
- dead-letter의 저장소와 재처리 표면
- configuration key schema
