<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Spec -- ZLink Framework C++ Interface Design](../spec/cpp-framework-interfaces.ko.md) | [다음: Spec -- ZLink Framework C++ Monitoring](../spec/cpp-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[C++ 묶음](../README.ko.md) | [Application Framework](../spec/cpp-application-framework.ko.md) | [Framework 인터페이스](../spec/cpp-framework-interfaces.ko.md) | [인터페이스](../spec/handler-interfaces.ko.md) | [channel](../spec/cpp-channel-messaging.ko.md) | [SPOT](../spec/cpp-spot.ko.md) | [STREAM](../spec/cpp-stream.ko.md) | [HTTP Client](../../../http-client/cpp/README.ko.md) | [HTTP Hosting](../spec/cpp-http-hosting.ko.md)

# ZLink Framework C++ Policy

> 이 문서는 `C++` `ZLink Framework`가 제공하는 제품/API 표면의 정책을 정리한다.
> 기준은 `framework/languages/cpp`의 public header, 테스트, 샘플이다.
>
> 이 문서는 `framework/doc/spec` 아래의 공통 framework 정책을 상위 기준으로 따른다.
> 언어별 스펙은 공통 정책을 반드시 반영해야 하며, 이 문서는 그 공통 정책을 `C++`
> 언어 특성과 zlink framework host 형태에 맞춰 구체화한다.

## 1. 포지셔닝

`ZLink Framework for C++`는 `.NET Core`/`ASP.NET Core`, `Spring Boot`, `NestJS`와 같은
포지션의 application framework다. 주 벤치마크는 `.NET Core`다. host, DI, configuration,
logging, lifecycle은 `.NET Generic Host` 계열을 기준으로 삼고, HTTP routing과 handler
표면은 `ASP.NET Core Minimal API`를 기준으로 삼는다. `Spring Boot`와 `NestJS`는
module/provider/filter/configuration 같은 기능 축을 확인하는 보조 기준으로만 사용한다.
이 프레임워크는 HTTP hosting, DI container, configuration, zlink messaging, logging,
observability, validation, hosted service를 한 app model 안에서 제공한다.

중심에 두는 개념은 아래와 같다.

- 메시지 송수신
- HTTP hosting
- DI container
- configuration
- actor와 비슷하지만 네트워크와 라우팅을 먼저 고려하는 `SPOT`
- STREAM session과 actor를 ActorGateway로 묶는 session relay
- 여러 프로세스와 여러 노드를 전제로 하는 distributed runtime
- channel, topic, routing id 기반 routing
- 애플리케이션 lifecycle
- ASP.NET Core Minimal API의 `MapGet`, `MapPost`, `MapPut`, `MapDelete`에 대응하는 HTTP hosting
- middleware와 filter
- logging과 observability
- validation과 error handling
- security/auth extension point
- scheduling과 background work
- handler 실행과 오류 격리
- backpressure와 graceful shutdown

따라서 `C++` 프레임워크는 HTTP, zlink messaging, timer, hosted service가 따로 노는
라이브러리 묶음이면 안 된다. 사용자는 raw socket, runtime event 처리, service discovery
배선을 직접 다루지 않고, DI, configuration, handler, middleware/filter, logging, health,
zlink runtime을 같은 application host 안에서 구성해야 한다.

`C++` 문서는 기존 framework adapter 공통 초안을 그대로 반복하지 않는다. 공통 초안의
상호작용 모델, 메시지 모델, channel topology, naming policy를 반영한 뒤, `C++`
언어 특성에 맞는 세부 구현 사항을 이 문서와 하위 문서에서 구체화한다.

특히 `C++`에는 `ASP.NET Core`, `Spring Boot`, `NestJS`처럼 널리 쓰이는 단일 표준
application framework가 없으므로, zlink framework가 app, host, DI, HTTP hosting,
configuration, handler registry, CAPI dispatch 연결, lifecycle, session relay 같은
기반 프레임워크 설계 내용을 직접 제공한다. 이 내용은 공통 정책을 대체하는 것이 아니라,
공통 정책에서 다루지 않은 `C++` application framework 세부 스펙을 채우기 위한 것이다.

이 문서와 기존 `C++` 세부 초안의 bootstrap API가 다르면, 구현 전 정렬 작업에서 기존
세부 초안을 이 문서의 `C++` 상세 방향에 맞춰 갱신한다. 다만 공통 framework 정책과
충돌하는 내용이 발견되면 먼저 공통 정책을 확인하고, 필요하면 공통 정책을 갱신한 뒤
언어별 문서를 맞춘다.

동작 기준은 현재 `.NET` framework 구현과 정식 spec 문서다. `C++` 문서는 그 동작을
`C++20` coroutine, RAII, CMake/package 구조에 맞게 투영한다. 기능
범위는 축소하지 않고, 언어별 표현과 구현 순서만 다르게 둔다.

최종 완료 기준은 아래 세 가지다.

- `.NET` framework와 동일한 구조를 가진다.
- `.NET` framework와 동일한 기능을 제공한다.
- `.NET` framework와 동일 수준의 사용성을 제공한다.

따라서 구현 중 어떤 기능을 나중 goal에서 처리하더라도 최종 스펙 범위에서 빠진 것으로
해석하지 않는다. 구현 순서가 다를 뿐이며, 최종 산출물은 `.NET` framework의 구조, 기능,
사용성 기준을 C++ 방식으로 모두 만족해야 한다.

## 2. 사용자 목표 표면

최종 사용자는 아래 수준의 코드로 HTTP와 zlink messaging을 함께 쓰는 application을 시작할 수
있어야 한다.

```cpp
struct create_order_http_req_t {
    std::string order_id;
};

struct create_order_http_res_t {
    std::string order_id;
    std::string state;
};

class create_order_http_handler_t final {
public:
    using request_type = create_order_http_req_t;
    using reply_type = create_order_http_res_t;
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::channel_client_t>;

    explicit create_order_http_handler_t(
      zlink::framework::channel_client_t &client);

    zlink::framework::task_t<create_order_http_res_t> handle(
      const create_order_http_req_t &request);
};

int main(int argc, char **argv)
{
    auto app = zlink::framework::app_t::create();

    app.config()
      .load_json("appsettings.json")
      .load_env("ORDER_")
      .load_cli(argc, argv);

    app.add_zlink_framework([](auto &options) {
        options.use_discovery().add_registry_endpoint ("tcp://registry:5551");
        options.codecs().add_json();
        options.services()
          .add_singleton<order_repository_t>()
          .add_scoped<order_service_t, order_repository_t>();

        options.http()
          .listen("http://0.0.0.0:8080")
          .map_post<create_order_http_handler_t>("/orders");

        options.add_client_server_channel("orders")
          .enable_server("tcp://0.0.0.0:7001")
          .use_handler_group("orders-api");

        options.handlers()
          .group ("orders-api")
          .add<create_order_handler_t> ();
    });

    return app.run(argc, argv);
}
```

위 코드는 정책 방향을 보여 주는 예시다. 구현은 아래 기준을 따른다.

- app 생성의 canonical 시작점은 `app_t::create()`다. 기존 세부 초안에 남아 있던
  이전 bootstrap 표기는 이 정책에 맞춰 정리한다.
- HTTP route, zlink channel handler, hosted service는 같은 DI, configuration, logging,
  validation, error handling 모델을 사용한다.
- handler 등록은 handler type을 명시하는 형태를 기본으로 한다. handler owner는 DI container에서
  resolve한다.
- `run`은 process exit code로 사용할 수 있는 `int`를 반환한다.
- typed payload 이름에서 packet key를 얻는 규칙을 serializer 정책과 맞춘다.

## 3. 권장 구현 루트

`C++` framework 구현은 `.NET` framework의 `Contracts/*`와 `Runtime/*` 분리를
기준으로 맞춘다. C++는 언어 특성상 public contract가 설치되는 header이고, runtime은
컴파일되는 구현 파일이다. 따라서 public 계약은 `include/zlink/framework/contracts/*`
아래에 두고, 구현 세부는 `src/runtime/*` 아래에 숨긴다.

```text
framework/languages/cpp/
+-- framework/
|   +-- include/zlink/framework/
|   |   +-- contracts/
|   |   |   +-- actors/
|   |   |   +-- channels/
|   |   |   +-- codecs/
|   |   |   +-- configuration/
|   |   |   +-- dispatch/
|   |   |   +-- errors/
|   |   |   +-- eventing/
|   |   |   +-- handlers/
|   |   |   +-- registry/
|   |   |   +-- spots/
|   |   |   +-- streams/
|   |   |   +-- timers/
|   |   +-- framework.hpp
|   |   +-- app.hpp
|   |   +-- channels.hpp
|   |   +-- handlers.hpp
|   |   +-- spots.hpp
|   |   +-- streams.hpp
|   |   +-- timers.hpp
|   +-- src/
|   |   +-- runtime/
|   |   |   +-- actors/
|   |   |   +-- backend/
|   |   |   +-- channels/
|   |   |   +-- codecs/
|   |   |   +-- configuration/
|   |   |   +-- diagnostics/
|   |   |   +-- dispatch/
|   |   |   +-- execution/
|   |   |   +-- handlers/
|   |   |   +-- host/
|   |   |   +-- messaging/
|   |   |   +-- registry/
|   |   |   +-- spots/
|   |   |   +-- streams/
|   |   |   +-- timers/
+-- connector/
|   +-- core/
|   +-- e2e-client/
|   +-- engines/
|       +-- unreal/
+-- tests/
+-- samples/
+-- CMakeLists.txt
```

public include path는 `zlink/framework/...` 아래로 제한한다. 사용자는
`#include <zlink/framework.hpp>` 또는 필요한 세부 contract header만 include한다.
native core나 C binding 세부 header를 사용자가 직접 include해야만 동작하는 표면은
만들지 않는다.

`.NET`과 C++의 구조 대응은 아래처럼 둔다.

| `.NET` framework | C++ framework | 역할 |
|------------------|---------------|------|
| `Contracts/Actors` | `framework/include/zlink/framework/contracts/actors` | actor public contract |
| `Contracts/Channels` | `framework/include/zlink/framework/contracts/channels` | channel client, call object contract |
| `Contracts/Codecs` | `framework/include/zlink/framework/contracts/codecs` | serializer and message codec public contract |
| `Contracts/Configuration` | `framework/include/zlink/framework/contracts/configuration` | app, host, config builder contract |
| `Contracts/Dispatch` | `framework/include/zlink/framework/contracts/dispatch` | handler execution policy contract |
| `Contracts/Errors` | `framework/include/zlink/framework/contracts/errors` | error kind, exception, result contract |
| `Contracts/Eventing` | `framework/include/zlink/framework/contracts/eventing` | monitoring event public contract |
| `Contracts/Handlers` | `framework/include/zlink/framework/contracts/handlers` | handler registration and invocation contract |
| `Contracts/Registry` | `framework/include/zlink/framework/contracts/registry` | registry query contract |
| `Contracts/Spots` | `framework/include/zlink/framework/contracts/spots` | Spot, actor, route contract |
| `Contracts/Streams` | `framework/include/zlink/framework/contracts/streams` | stream and session contract |
| `Contracts/Timers` | `framework/include/zlink/framework/contracts/timers` | high performance timer contract |
| `Runtime/*` | `framework/src/runtime/*` | native handle owner, dispatch, codec, queues, lifecycle implementation |
| `Runtime/Backend/Contracts` | `framework/src/runtime/backend/contracts` | binding adapter가 지켜야 하는 private backend contract |

C++ public umbrella header는 위 contract header를 다시 묶을 수 있지만, runtime header를
public include에 올리지 않는다. template 때문에 header 구현이 필요한 경우에도
`contracts/detail/*`까지만 허용하고, CAPI handle, dispatch callback, raw recv loop,
frame codec 같은 runtime 결정은 `src/runtime/*`에 둔다.

### 3.1 Contract/Runtime 분리 강도

이 분리는 `bindings/cpp`의 인터페이스/구현 분리보다 강하게 적용한다. binding은 core와
가까운 낮은 계층이므로 native option, message buffer, socket wrapper 같은 얇은 C++
표면을 제공한다. 반면 framework는 application host/runtime 계층이므로, 사용자가
framework 내부 실행 순서나 native transport 배선을 알 필요가 없어야 한다.

여기서 contract 분리는 C++의 모든 public 타입을 pure virtual interface로 만들라는 뜻이
아니다. `.NET`의 `interface`/`internal class` 구분을 C++에 그대로 복제하면 오히려 얕은
모듈이 늘 수 있다. C++에서는 사용자가 직접 다루는 `app_t`, `channel_client_t`,
`stream_t`, `spot_context_t` 같은 타입은 concrete facade일 수 있다. 중요한 기준은 그
facade가 runtime 구현 타입, native handle, 실행 순서를 public header에 드러내지 않는
것이다. 사용자 확장 지점인 handler, codec, hosted service, filter 같은 영역에만
abstract interface 또는 concept 기반 contract를 둔다.

따라서 framework public header에는 아래 항목을 노출하지 않는다.

- CAPI handle, socket handle, raw pointer ownership
- dispatch callback userdata
- raw recv loop, poller slot, pump step
- ActorGateway frame codec 구현
- pending queue 자료구조
- offload executor 구현 타입
- timer event fd 또는 native timer token

반대로 public contract로 올려야 하는 것은 사용자가 구현하거나 조합해야 하는 개념이다.
대표적으로 handler, filter, serializer, hosted service, module, timer handler,
stream session handler, spot handler, actor factory는 contract header가 소유한다. 다만
이 contract가 곧 runtime registry나 dispatcher 구현을 뜻하지 않는다. contract header는
호출 shape, 타입 제약, 결과 타입, lifetime 의미까지만 드러내고, 등록 테이블, DI resolve,
serializer cache, dispatch 순서, 실패 mapping은 `src/runtime/*` 구현이 소유한다.

C++에서 concrete facade를 쓰는 경우에는 아래 규칙을 따른다.

- facade의 public data member에는 runtime 구현 타입을 두지 않는다.
- facade가 상태를 가져야 하면 `unique_ptr<detail::...state_t>`,
  `shared_ptr<detail::...state_t>`, 또는 type-erased handle로 숨긴다.
- `detail::...state_t` forward declaration은 contract header에 둘 수 있지만, 정의는
  `src/runtime/*`에 둔다.
- facade method는 사용자 개념을 받는다. 예를 들어 `channel_name`, `topic`,
  `timeout`, typed payload는 받을 수 있지만 poller slot, recv step, native socket
  option owner는 받지 않는다.
- public header에서 binding 타입을 참조해야 할 때도 `message_t` 같은 payload boundary에
  제한한다. `context_t`, socket 타입, native owner는 runtime 내부에서만 사용한다.

템플릿 때문에 header 코드가 필요한 경우에도 허용 범위는 아래로 제한한다.

- compile-time handler shape 검사
- typed payload와 route name을 연결하는 thin adapter
- public facade에서 type-erased runtime call로 넘기는 forwarding
- `contracts/detail/*` 안의 type trait와 small helper

`contracts/detail/*`은 public header에 포함될 수 있지만 runtime 구현 장소가 아니다.
native handle owner, dispatch projection, recv/drain 순서, codec registry 구현은 반드시
`src/runtime/*`에 둔다.

이 원칙을 어기는 대표적인 신호는 아래와 같다.

- public header가 `src/runtime/*` header를 include한다.
- public class의 data member가 runtime 구현 class 또는 native handle owner다.
- public method 인자나 반환값에 poller, dispatch token, callback userdata가 들어간다.
- handler 등록 API가 recv 순서나 pump step을 호출자에게 요구한다.
- `contracts/detail/*`에 queue, executor, frame codec, dispatch projection 구현이 들어간다.
- connector나 Unreal connector public header가 server framework runtime 타입을 include한다.

새 기능을 구현할 때는 먼저 public contract owner와 runtime owner를 나눈다. 예를 들어
handler registry goal에서는 `handler_registry_t`, handler concept, callback result
타입은 `contracts/handlers/*`가 소유하고, descriptor map, DI 기반 owner resolve,
serializer 호출 순서, monitoring event 생성은 `src/runtime/handlers/*`가 소유한다.
serializer goal에서는 `serializer_registry_t`와 `serializer_t<T>` shape만
`contracts/codecs/*`가 소유하고, type-erased registry와 기본 JSON serializer wiring은
`src/runtime/codecs/*`가 소유한다. 이 나눔을 하지 않은 상태에서는 구현을 진행하지 않는다.

### 3.2 Install Boundary 와 CMake Boundary

인터페이스/구현 분리는 디렉토리 이름만 맞춘다고 끝나지 않는다. C++에서는 설치되는
header와 target include directory가 곧 공개 계약이 되므로, CMake와 테스트도 같은 경계를
강제해야 한다.

- `zlink::framework` public include directory는 `framework/include`만 공개한다.
- `framework/src/runtime/*`는 target private source와 private include 경로로만 사용한다.
- public facade header는 `contracts/*` header를 include할 수 있지만 `src/runtime/*`
  header를 include하지 않는다.
- runtime unit test는 private header를 include할 수 있다. contract/layout test는 public
  header만 include한다.
- connector target도 같은 규칙을 따른다. `zlink::stream_connector` public include
  directory는 `connector/core/include`이고, `connector/core/src/runtime/*`는 private이다.
- Unreal Connector의 `Public/` header는 Unreal 사용자 표면만 노출한다. 일반 C++
  connector runtime class나 framework runtime class를 public member로 두지 않는다.

각 goal의 첫 테스트는 가능하면 layout contract test여야 한다. 이 테스트는 public header
compile, runtime header 설치 제외, public header의 runtime include 금지를 확인한다.
그다음 기능별 unit/integration test를 붙인다.

### 3.3 C++에서 .NET식 분리를 적용할 때의 이슈

`.NET`의 interface와 `internal class`를 C++로 그대로 옮기면 public pure virtual class가
과하게 늘고, 실제 구현은 얕은 adapter 뒤에 숨는 문제가 생길 수 있다. C++ framework는
아래 방식으로 같은 분리 강도를 유지한다.

| 이슈 | 결정 | 이유 |
|------|------|------|
| 모든 public 타입을 interface로 만들지 여부 | 사용자 확장점만 abstract interface 또는 concept로 둔다. app, channel, stream, spot 같은 사용 표면은 concrete facade를 허용한다. | C++에서는 concrete facade + 숨겨진 state가 더 단순하고 깊은 모듈이 될 수 있다. |
| public facade의 ABI와 상태 | state는 PIMPL, opaque state, type-erased handle로 숨긴다. | runtime 자료구조 변경이 public header 변경으로 번지지 않게 한다. |
| template 기반 handler 등록 | header에는 shape 검사와 forwarding만 둔다. | descriptor map, DI resolve, dispatch 순서를 public header에서 없앤다. |
| codec dependency | base target에는 codec 외부 dependency를 넣지 않는다. | 사용하지 않는 Protobuf, MessagePack, LZ4를 강제 설치하지 않는다. |
| offload executor | option과 실행 정책은 public contract로, thread pool queue와 drain 구현은 runtime으로 둔다. | CPU-bound handler 가이드는 제공하되 실행 구현을 호출자에게 넘기지 않는다. |
| dispatch pump | handler 등록과 typed event projection만 public으로 둔다. | CAPI dispatch callback, recv loop, pump step은 framework가 소유해야 한다. |

따라서 C++ framework에서 `.NET`보다 더 많은 concrete type이 보이더라도 분리 약화로
보지 않는다. 판단 기준은 타입이 abstract인지가 아니라, 사용자가 내부 실행 순서와 native
소유권을 알아야 하는지다. 사용자가 handler, service, stream session, Spot actor,
connector callback 같은 application 개념만 구현하면 된다면 분리는 유지된 것이다.

C++ Stream Connector는 위 framework package에 포함하지 않는다. 같은 언어 디렉토리
아래에서 개발할 수는 있지만, 별도 library와 별도 배포 단위로 둔다.

```text
framework/languages/cpp/connector/
+-- include/zlink/stream_connector/
|   +-- contracts/
+-- src/
|   +-- runtime/
+-- tests/
+-- samples/
+-- CMakeLists.txt
```

connector도 같은 원칙을 따른다. public 계약은
`include/zlink/stream_connector/contracts/*`에 두고, connection, receive loop,
pending request table, frame sender, heartbeat 같은 구현은 `src/runtime/*`에 둔다.

framework package의 CMake target은 `zlink::framework`이고, connector package의 CMake
target은 `zlink::stream_connector`다. 서버 framework가 connector를 의존하거나,
connector가 서버 framework를 의존하는 구조로 만들지 않는다.

framework의 세부 디렉토리는 `.NET` `Zlink.Framework`의 실제 `Contracts/*`와
`Runtime/*` 축을 기준으로 고정한다. C++ 이름은 `snake_case`를 쓰지만, 역할은 아래
목록에서 벗어나지 않는다.

| public contract 축 | runtime 축 | C++에서 숨겨야 하는 구현 지식 |
|--------------------|------------|-------------------------------|
| `contracts/actors` | `src/runtime/actors` | actor mailbox, relay dispatch, activation bookkeeping |
| `contracts/channels` | `src/runtime/channels`, `src/runtime/messaging` | socket owner, correlation table, send-ready queue |
| `contracts/codecs` | `src/runtime/codecs` | serializer map, JSON backend, codec feature wiring |
| `contracts/configuration` | `src/runtime/configuration` | service descriptor store, scope destruction stack |
| `contracts/dispatch` | `src/runtime/dispatch`, `src/runtime/execution` | completion node, thread pool queue, drain state |
| `contracts/errors` | `src/runtime/messaging` | internal errno mapping table and retry bookkeeping |
| `contracts/eventing` | `src/runtime/diagnostics` | snapshot diff cache, telemetry backend adapter |
| `contracts/handlers` | `src/runtime/handlers` | descriptor map, DI resolve order, invoke cache |
| `contracts/registry` | `src/runtime/registry` | topology cache and backend query owner |
| `contracts/spots` | `src/runtime/spots` | activation table, subscription pump, native route dispatcher |
| `contracts/streams` | `src/runtime/streams` | frame codec, session table, serial dispatch queue |
| `contracts/timers` | `src/runtime/timers` | native timer token, fire-count drain loop |

`src/runtime/backend/contracts`는 이름에 contract가 들어가더라도 public contract가 아니다.
이 영역은 zlink binding substrate와 framework runtime 사이의 내부 seam이다. 설치
header, umbrella header, sample code, extension public header에서 include하지 않는다.
backend seam을 외부 확장점으로 열어야 할 때도 먼저 별도 `contracts/*` 타입을 만들고,
backend 내부 타입을 그대로 public으로 올리지 않는다.

Unreal Connector도 별도 배포 단위다. 일반 C++ connector public API를 그대로 노출하지 않고,
Unreal plugin/module, Unreal 타입, Game Thread dispatch, Blueprint 호출 표면을 가진 adapter로
둔다. private 구현은 기본 C++ Stream Connector를 소유해서 connect, send, request, dispatch,
close를 위임한다. wire protocol, codec, reconnect, heartbeat, pending request correlation은
기본 connector runtime의 책임이다. Unreal의 물리 구조는 Unreal 관례에 맞춰 `Public/`과
`Private/`를 쓰되, `Public/`은 Unreal type adapter만 담고, `Private/`는 기본 connector 호출과
thread dispatch 구현을 담는다.

## 3.0 Language Baseline

`C++` framework는 `C++20` 이상만 지원한다. 이 기준은
[framework 공통 비동기 정책](../../common/spec/async-execution-policy.ko.md)을
`C++`의 coroutine 기반 handler와 `task_t<T>` async 표면으로 투영하기 위한 결정이다.

필수 기준은 아래와 같다.

- public header와 samples는 `C++20`으로 작성한다.
- coroutine handler는 `task_t<T>` 또는 `task_t<void>`를 반환한다.
- public async 표면에는 `std::future`를 사용하지 않는다.
- handler, timer, stream session, actor relay 안에서 blocking wait를 허용하지 않는다.
  handler dispatch 내부에서 `.result()`로 task를 기다리는 bridge 구현도 허용하지 않는다.
- `task_t<T>`는 `.NET Task`처럼 같은 task를 여러 coroutine이 await할 수 있어야 한다.
  완료 결과는 한 번만 확정되며, 중복 완료 시도는 결과를 덮어쓰지 않는다.
- CPU-bound 또는 blocking 가능성이 있는 handler는 framework core의 offload executor를
  명시적으로 사용한다.
- C++20 표준 library 기능을 사용할 수 있지만, CAPI dispatch callback을 handler 등록
  표면에 연결하는 내부 경계는 framework가 직접 소유한다.

### 3.0.1 Server Network Async Policy

C++ server framework의 네트워크 public API는 `.NET` framework와 같은 방향으로 정리한다.
네트워크 호출, listen/connect, send, request/reply, stream write, packet wait, graceful
close, shutdown drain처럼 I/O 완료 조건을 가진 함수는 blocking 동기 API를 제공하지 않는다.

이 정책은 server framework에만 적용한다. C++ Stream Connector core는 게임 client와 엔진
환경을 위해 no-exception/no-coroutine 계약을 따른다. 서버 e2e, smoke, perf scenario에서만
필요한 awaitable 표면은 `stream_e2e_client` package로 분리한다. server framework는 framework
runtime과 coroutine scheduler를 소유하므로, 네트워크 API를 `task_t<T>` 기반 awaitable 표면으로
통일한다.

서버 public API의 이름 규칙은 아래와 같다.

- 네트워크 작업은 `*_async` 또는 call object의 `async()`로 완료한다.
- `*_async` 함수는 callback을 받지 않고 `task_t<T>` 또는 `task_t<void>`를 반환한다.
- `submit(callback)` 형태는 server framework public API로 제공하지 않는다.
- `submit()`이 thread를 block해서 `result_t<T>`를 반환하는 네트워크 API는 제공하지 않는다.
- 설정 builder, DI 등록, route 등록, serializer 등록처럼 I/O가 없는 구성 API는 동기 함수로
  유지한다.
- handler는 application callback이므로 동기 handler와 coroutine handler를 모두 허용할 수
  있다. 단, handler 안에서 네트워크 호출을 해야 하면 `co_await ...async()`를 사용한다.

네트워크 작업은 응답 payload가 없어도 async API로 둔다. reply가 없다는 뜻은 대기할 payload가
없다는 뜻일 뿐, backpressure, route ready, send timeout, cancellation, runtime shutdown,
graceful drain 같은 완료 조건이 사라지는 것은 아니다. blocking wrapper를 public API로 만들면
framework scheduler를 우회하고, shutdown 지연이나 worker 고갈을 만들 수 있으므로 제공하지
않는다.

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
dependency, framework dispatch integration 모델에 대한 제한이다.

JSON 구현은 `nlohmann/json` 하나로 고정한다. 설정 파일, JSON codec helper, 테스트
fixture에서 같은 JSON 타입과 같은 변환 규칙을 사용한다. C++ binding codec 구조는
connector와 같은 원칙으로 바꾼다. base binding은 codec dependency를 갖지 않고,
JSON, MessagePack, Protobuf는 선택 codec target이 제공한다. public codec 표면은 별도
codec namespace 함수를 사용자가 직접 찾아 호출하는 방식을 기본 사용자 경험으로 두지
않고, `message_t` 중심 API로 정리한다. 예를 들어 `message.parse<T>()`,
`message.parse_json<T>()`, `message_t::from_json(value)` 같은 형태가 기본이다.
성능 특화 JSON parser가 필요해지더라도 public JSON 정책을 늘리지 않고, 내부 최적화로
검토한다.

DI는 자체 구현한다. 사용자는 항상 `zlink::framework`가 제공하는
`service_collection_t`, `service_provider_t`, lifetime API만 사용해야 한다.
`Boost.Ext.DI` 같은 외부 DI 라이브러리는 public dependency로 두지 않는다.

## 3.2 권장 라이브러리 목록

아래 표는 `C++` framework 구현에서 사용할 수 있는 라이브러리 기준이다. 원칙은
dependency를 public API 밖에 숨기는 것이다. 사용자는 `zlink::framework` 타입을 보아야
하며, 외부 라이브러리 타입을 handler, module, config callback 시그니처에서 직접
보지 않아야 한다.

| 영역 | 권장 라이브러리 | 링크 | 적용 범위 | 정책 |
|------|----------------|------|----------|------|
| runtime / I/O | zlink C++ binding | 내부 binding | 필수 | framework runtime의 유일한 I/O 기반이다. `context_t`, socket, discovery, spot, stream binding을 내부 substrate로 사용한다. |
| coroutine executor | `Boost.Asio` | <https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/composition/cpp20_coroutines.html> | framework 내부 구현 | `boost::asio::awaitable`, `co_spawn`, `thread_pool`을 handler coroutine executor 내부에서 사용한다. public API에는 `boost::asio::awaitable`이나 executor 타입을 노출하지 않는다. |
| HTTP hosting | `Boost.Beast` | <https://www.boost.org/doc/libs/latest/libs/beast/> | framework 내부 구현 | ASP.NET Core Minimal API의 route handler에 대응하는 HTTP/HTTPS 요청을 처리한다. public API에는 `boost::beast` request, response, socket 타입을 노출하지 않는다. |
| HTTP TLS | OpenSSL / Boost.Asio SSL | framework 내부 구현 | framework 내부 구현 | HTTPS endpoint의 TLS handshake와 certificate/private key loading에 사용한다. public API에는 SSL context, SSL stream, OpenSSL 타입을 노출하지 않는다. |
| JSON | `nlohmann/json` | <https://github.com/nlohmann/json> | framework 필수 | 설정 파일, framework JSON serializer, 테스트 fixture의 기준 JSON 구현으로 고정한다. binding base dependency는 아니다. |
| logging backend | `spdlog` | <https://github.com/gabime/spdlog> | 내부 구현 | public logging API 뒤에 숨긴다. 사용자는 `app.logging()`만 사용하고 `spdlog` logger나 sink 타입을 직접 받지 않는다. |
| string formatting | `{fmt}` | <https://github.com/fmtlib/fmt> | 내부 구현 | 내부 메시지 formatting에 사용한다. public API에는 `fmt::format_string` 같은 타입을 노출하지 않는다. |
| CLI args | 자체 parser | framework 내부 | 선택 기능 | `app.config().load_cli(...)`는 자체 parser로 구현한다. CLI parsing을 위해 외부 타입을 public API에 올리지 않는다. |
| test runner | CTest | CMake 기본 기능 | 필수 | framework C++ test와 sample smoke를 CTest label로 등록한다. CI와 local regression 실행의 기준이다. |
| unit / regression test | GoogleTest | <https://github.com/google/googletest> | 개발 의존성 | framework C++ 객체, coroutine, DI, handler registry, lifecycle 회귀 테스트의 기본 harness로 사용한다. |
| mock / fake boundary | GoogleMock | GoogleTest 포함 | 개발 의존성 | runtime boundary, handler invoker, offload executor, monitoring sink 테스트의 mock/fake에 사용한다. |
| microbenchmark | google/benchmark | <https://github.com/google/benchmark> | 개발 선택 기능 | handler dispatch, serializer, DI resolve hot path 측정이 필요할 때만 추가한다. 공식 perf 수치와 섞지 않는다. |
| YAML | `yaml-cpp` | <https://github.com/jbeder/yaml-cpp> | 확장 기능 | YAML configuration은 framework core 밖의 configuration extension으로 둔다. core configuration은 JSON, env, CLI를 기준으로 한다. |
| MessagePack | MessagePack C++ | connector / binding 선택 기능 | 선택 codec | connector와 binding codec target에서 build option으로 켜고 끈다. framework server core 필수 dependency가 아니다. |
| Protobuf | Protocol Buffers C++ | <https://protobuf.dev/> | connector / binding 선택 기능 | connector와 binding codec target에서 optional build feature로 둔다. framework server core 필수 dependency가 아니다. |
| FlatBuffers | FlatBuffers | <https://flatbuffers.dev/> | 확장 기능 | framework core와 connector 기본 기능에는 넣지 않는다. 별도 요구가 생기면 확장 기능으로 설계한다. |

framework core의 public API는 외부 runtime 타입을 노출하지 않는다. 다만 C++20 coroutine
실행기 구현에는 `Boost.Asio`를 내부 dependency로 사용한다. 이 dependency는
`task_t<T>`, handler, module, options callback signature에 나타나면 안 된다. logging,
formatting, CLI parsing은 구현 편의와 패키징 비용을 비교한 뒤 public API 밖의 내부
dependency로만 추가한다. GoogleTest, GoogleMock, benchmark 라이브러리는 개발 의존성이고
배포 runtime dependency가 아니다.

아래 라이브러리는 framework public surface dependency로 두지 않는다.

| 영역 | 라이브러리 | 제외 이유 |
|------|------------|----------|
| async I/O public surface | `Boost.Asio` 타입 직접 노출 | 사용자는 `task_t<T>`와 zlink call object만 보아야 한다. `boost::asio::awaitable`, executor, strand는 runtime 내부 구현으로 숨긴다. |
| HTTP/Web public surface | `Boost.Beast`, SSL 타입 직접 노출 | HTTP/HTTPS hosting 구현에는 Beast와 SSL backend를 쓰지만, 사용자는 `options.http().listen(...).configure_tls(...).map_get/map_post/map_put/map_delete<THandler>(...)`만 보아야 한다. |
| WebSocket 구현 | `Boost.Beast` WebSocket 표면 | WebSocket transport는 STREAM Connector 또는 zlink transport 경계에서 다룬다. HTTP hosting의 route handler 기능과 섞지 않는다. |
| event loop | `libuv` | zlink poller와 별도 event loop를 함께 운영하면 shutdown, timer, readiness 의미가 복잡해진다. |
| DI | `Boost.Ext.DI` | framework 자체 container로 lifetime과 host shutdown 규칙을 직접 닫는다. |

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
실행 순서, session relay 순서를 기억해야 한다면 host 추상화가 실패한 것이다.

### 4.2 DI Container

DI container는 framework lifecycle과 handler wiring에 필요한 기능을 제공한다.

필수 public 등록 표면은 아래 정도로 제한한다.

```cpp
options.services().add_singleton<order_service_t>();
options.services().add_transient<order_handler_t>();
options.services().add_factory<clock_t>([](service_provider_t &services) {
    return std::make_unique<system_clock_t>();
});
```

지원 lifetime은 `singleton`, `scoped`, `transient`로 둔다. `scoped`는 zlink core
기능이 아니다. `.NET` framework가 `IServiceScope`를 core stream, spot, handler
lifecycle에 맞춰 붙이는 것처럼, C++ framework가 자체 DI container에서 만드는
framework가 소유하는 scope다. 사용자가 임의로 전역 scope를 만들고 lifetime을 해석하게 하지
않는다.

금지 방향은 아래와 같다.

- C++ reflection을 전제로 한 API
- annotation 또는 macro에 강하게 의존하는 등록 모델
- 생성자 의존성을 숨기는 service locator 남용
- 외부 DI 라이브러리의 injector, binding DSL, scope 타입을 public API로 노출하는 모델

기본 방향은 constructor injection이다. 자체 container를 구현하고, C++ reflection이 없다는
제약 때문에 생성자 타입을 자동 추론하지는 않는다. 대신 `add_singleton<T, Dep...>()`처럼
의존 타입을 명시하는 생성자 주입과 명시 factory를 함께 지원한다. lifetime registry, service
collection, module registration 의미는 framework가 소유한다. 이렇게 해야 handler lifecycle,
hosted service stop 순서, shutdown 중 resolve 금지 같은 규칙을 host와 함께 닫을 수 있다.

기본 생성 규칙은 아래처럼 닫는다.

- `add_singleton<T>()`, `add_transient<T>()`는 기본 생성 가능한 타입만 자동 생성한다.
- 생성자 의존성이 있는 타입은 `add_singleton<T, Dep...>()`,
  `add_scoped<T, Dep...>()`, `add_transient<T, Dep...>()`처럼 의존 타입을 명시해 등록한다.
- `add_scoped<T>()`는 framework가 소유하는 scope 안에서만 resolve할 수 있다.
- 복잡한 외부 객체 생성이나 조건부 생성이 필요한 경우에만 `add_factory<T>()`를 사용한다.
- handler owner에 의존성이 있으면 handler type이 `dependency_list_t<Dep...>`로 의존성을
  명시하고, handler options builder가 DI 생성자 주입 등록을 수행한다.
- 생성자 타입 자동 추론은 core 표면에 넣지 않는다. 필요하면 public API 밖의 helper나
  extension으로 검토한다.

내부 구현 경계는 아래처럼 나눈다.

| 계층 | 예시 타입 | 노출 여부 |
|------|----------|----------|
| public | `service_collection_t`, `service_provider_t`, `service_lifetime_t` | 노출 |
| internal | `service_registry_t`, `service_descriptor_t`, `factory_adapter_t` | 숨김 |

`Boost.Ext.DI`는 framework public DI 표면에 사용하지 않는다. 생성자 자동 wiring이
필요해져도 `service_collection_t`와 `service_provider_t` 계약 안에서 자체 helper로
흡수한다.

zlink core에는 아래 scope 개념이 없다. 이 경계는 모두 framework 계층이 core lifecycle
event를 기준으로 만들어 붙이는 DI lifetime이다.

framework가 소유하는 scope 경계는 아래와 같다.

| Scope | 생성 시점 | 종료 시점 | 용도 |
|-------|-----------|-----------|------|
| handler invocation scope | channel request/send/event dispatch 시작 | 해당 handler dispatch 완료 | stateless channel handler와 filter resolve |
| stream session scope | STREAM session accepted/connected | session close cleanup 완료 | session handler, session-scoped state |
| spot activation scope | user Spot 생성 | Spot remove/destroy cleanup 완료 | Spot instance, Spot handler, Spot timer handler |
| entry spot scope | SpotNode Entry Spot activation | SpotNode shutdown | Entry Spot instance와 Entry Spot handler |
| actor creation scope | actor factory 호출 | actor creation 완료 | actor factory resolve와 생성 보조 dependency |

actor instance 자체는 actor runtime이 소유한다. actor 상태를 DI scoped service로 숨기기보다
actor 객체와 `actor_context_t`에 명확히 둔다.

### 4.3 Runtime Integration

runtime integration은 zlink core와 framework 사이의 가장 중요한 경계다.

권장 표면은 `add_zlink_framework(...)` 안의 options builder다. 이 표면은 `.NET`
framework의 host factory처럼 필요한 runtime 구성만 선언하게 만들고, core builder의
세부 람다 조립은 framework 내부로 숨긴다.

```cpp
app.add_zlink_framework([](auto &options) {
    options.use_discovery().add_registry_endpoint ("tcp://registry:5551");
    options.add_client_server_channel("orders")
      .enable_server("tcp://0.0.0.0:7001")
      .use_handler_group("orders-api");
    options.add_spot_mesh("orders")
      .enable_router("tcp://0.0.0.0:7101");
});
```

`app.advanced().zlink()`는 framework extension이나 contract test에서만 사용한다.
일반 application code와 공식 샘플은 이 낮은 수준 표면을 사용하지 않는다.

이 계층은 아래 책임을 가진다.

- zlink context 생성과 종료
- socket lifecycle 관리
- channel 역할 연결
- spot node lifecycle
- service discovery 연결
- CAPI dispatch callback과 handler dispatch binding
- transport endpoint 검증

framework runtime은 `Boost.Asio`, `Boost.Beast`, `libuv` event loop를 public 실행기로
노출하지 않는다. timer, readiness, graceful shutdown은 CAPI dispatch callback을
framework handler dispatch 경계에 연결하는 내부 binding으로 처리한다. 단, handler coroutine
executor는 내부 구현으로 `Boost.Asio`를 사용한다.

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

auto reply = client.request(
  "orders",
  get_order_status_t{.order_id = order_id}).async<order_status_reply_t>();

spot.send_to(target_node, target_spot, command);
```

send와 publish는 기본 async submit이다. public API에 `send_nonblocking` 같은 이름을
늘리지 않고, backpressure는 pending queue, timeout, ready notification으로 다룬다.
framework 표면에는 protocol-specific received wrapper를 늘리지 않는다. binding에서
받은 수신 metadata는 framework runtime이 correlation, actor relay, handler dispatch에
필요한 범위로 해석하고, 사용자 handler에는 typed payload와 필요한 context만 넘긴다.

### 4.5 Spot / Actor Model

`SPOT`은 lightweight distributed endpoint다. actor와 비슷하지만 아래 차이를 명확히
둔다.

- network-first
- routing-aware
- distributed-aware
- local/remote transparency를 목표로 하지만, 필요하면 routing id를 드러낸다.

필수 기능은 아래와 같다.

- core dispatch ordering
- spot message dispatch
- handler execution
- routing-id addressing
- local/remote transparency
- spot lifecycle
- timer
- Entry Spot
- actor factory
- ActorGateway session relay
- bound session push

일반 application handler는 channel/topic 중심으로 시작하고, 직접 `routing_id_t`를
다루는 API는 spot-to-spot, Entry Spot join, 운영 진단 경로에 제한한다. current Spot
밖에서 target Spot으로 직접 send/request 하는 별도 public client는 기본 표면에 두지
않는다. actor 생성 또는 Entry Spot join으로 `actor_ref_t`를 얻고, session이 필요하면
session actor handle로 bind하는 흐름을 사용한다.

### 4.5.1 SPOT Timer

SPOT timer는 CAPI timer 등록을 framework 표면으로 감싼 server-side timer다.
application이 native C API timer handle을 직접 받지 않는다. framework는 별도 timer
scheduler를 기본으로 만들지 않고, CAPI timer dispatch event를 받은 뒤 timer recv를
수행해 해당 Spot handler로 투영한다. public 계약은 `spot_context_t::add_timer(...)`와
timer handler 중심으로 닫는다.

이 구조로 `.NET` framework timer와 같은 기능성을 구현할 수 있다. C core timer는
고성능 interval wakeup, poller readable event, `fire_count` 누적값, stop/destroy
lifecycle을 제공한다. C++ framework는 그 위에서 `.NET`과 같은 overrun policy, tick
metadata, Spot dispatch 사상, handler 예외 monitoring, shutdown drain을 완성한다.
따라서 C++ framework는 별도 timer thread나 별도 scheduler를 기본 실행 모델로 두지
않는다. C++ framework는 CAPI timer와 CAPI SPOT dispatch event 후 recv 경계를 사용하므로
timer callback 실행 직렬화를 위한 별도 queue도 추가하지 않는다.

timer는 cleanup, heartbeat, timeout sweep, room tick, match tick을 같은 표면으로
다룬다. 별도 `add_tick` 같은 이름은 만들지 않는다. 대신 tick metadata와 overrun
정책을 제공한다.

```cpp
struct timer_options_t {
    timer_overrun_policy_t overrun_policy =
      timer_overrun_policy_t::skip_late_ticks;
    std::uint32_t max_catch_up_ticks = 1;
    bool stop_on_unhandled_exception = false;
};
```

실행 규칙은 다음과 같다.

- user Spot timer callback은 같은 user Spot의 packet, actor packet, subscription,
  channel reply와 같은 CAPI SPOT dispatch event 후 recv 경계에서 순서 정책을 따른다.
- Entry Spot timer callback은 Entry Spot actor packet, lifecycle callback, request
  continuation과 같은 Entry Spot 실행 줄에서 처리한다.
- 같은 timer instance의 callback은 겹쳐 실행하지 않는다.
- CAPI `fire_count`와 framework가 보관하는 이전 fire count를 비교해 `skipped_ticks`,
  `scheduled_index`, catch-up 실행 대상을 계산한다.
- 시간 계산은 monotonic clock 기준으로 한다. wall-clock 시간은 로그와 monitoring
  payload에만 사용한다.
- handler 예외는 monitoring에 즉시 기록하고, option에 따라 timer를 계속 실행하거나
  중단한다.

### 4.5.2 ActorGateway Session Relay

Session 서버와 Play 서버를 분리하는 흐름은 ActorGateway를 기준으로 설명한다.
session은 actor handle에 bind한 뒤 `relay(...)`로 packet을 넘긴다.

이 경로는 application route mesh channel을 사용하지 않는다. route mesh channel은
일반 routed SPOT egress용으로 남을 수 있지만, session actor relay의 필수 구성 요소가
아니다.

구현해야 하는 framework 기능은 아래와 같다.

- actor factory 등록
- actor id/type 기반 create, find, get-or-create
- local actor handle과 remote actor ref bind
- session actor relay
- actor context의 `bound_session_t`
- stream close 시 session binding cleanup
- actor disconnect notification을 application 선택 사항으로 남기는 정책

### 4.6 Handler Framework

handler framework는 사용자가 메시지를 함수 수준에서 처리하게 만드는 계층이다.

권장 등록 표면은 아래와 같다.

```cpp
options.handlers()
  .group ("orders-api")
  .add<order_created_handler_t> ();

options.handlers()
  .group ("orders-api")
  .add<get_order_status_handler_t> ();
```

handler owner 타입은 handler class의 type alias와 `handle(...)` 함수로 드러낸다.
일반 등록 표면에서 channel과 topic을 반복하지 않아야 host factory가 `.NET` 샘플처럼
간결하게 유지된다.

```cpp
class get_order_status_handler_t {
public:
    using request_type = get_order_status_t;
    using reply_type = order_status_reply_t;
    using dependency_types = dependency_list_t<order_service_t>;
    static constexpr const char *topic_name = "GetOrderStatus";

    explicit get_order_status_handler_t(order_service_t &orders);
    task_t<order_status_reply_t> handle(const get_order_status_t &request);
};
```

이 경우 handler 타입은 `options.handlers().group(...).add<THandler>()`에서 service collection에
자동 등록된다. 생성자 주입이 필요하면 `dependency_types`에 의존 타입을 적는다. framework가
없는 타입을 임의로 추측해 생성하지 않도록, 생성자 후보와 의존성은 handler 타입 안에서
명시해야 한다.

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

지원 범위는 아래와 같다.

- raw bytes
- JSON (`nlohmann/json`)
- MessagePack
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

raw bytes와 `nlohmann/json` 기반 JSON codec을 framework serializer 기본값으로 둔다.
C++ public codec 표면은 message 중심이어야 한다. 사용자가 codec namespace의 함수형
helper를 찾아 호출하는 방식은 주 표면으로 두지 않는다. 기본 사용성은 아래 형태다.

```cpp
auto message = zlink::message_t::from_json(order);
auto order = message.parse_json<order_created_t>();
```

framework serializer registry는 이 message 중심 codec API 위에 얹는다. codec 선택이
transport lifecycle과 섞이면 안 된다. public serializer API는 `nlohmann::json`을 직접
요구하지 않는 typed serializer 추상화를 먼저 노출하고, JSON helper에서만
`nlohmann/json` 변환 규칙을 제공한다.

binding base target은 codec dependency를 갖지 않는다. JSON, MessagePack, Protobuf는
각각 선택 target으로 제공한다. framework와 connector는 이 선택 target을 필요한 범위에서
사용하지만, binding만 쓰는 사용자가 불필요한 codec dependency를 설치하게 만들지 않는다.

### 4.8 Handler Execution Model

C++ framework의 사용자 표면은 handler 등록 모델이다. application 개발자는 CAPI
dispatch callback이나 recv 순서를 직접 다루지 않고, channel, stream session, Spot,
actor, timer handler만 구현한다.

framework 내부는 CAPI dispatch callback을 받아 해당 객체의 recv 결과를 typed handler로
연결한다. 이 경계는 사용자 API가 아니며, 별도 event loop나 기본 실행 모델을 뜻하지
않는다.

필수 기능은 아래와 같다.

- CAPI dispatch callback 등록
- event kind와 대상 객체를 기준으로 한 recv 처리
- typed handler projection
- CAPI timer event projection
- core가 제공하는 ordering 보존
- framework core가 제공하는 coroutine executor
- framework core가 제공하는 offload executor
- backpressure와 handler timeout

중요한 정책은 사용자가 handler만 구현하면 같은 방식으로 동작하게 하는 것이다.
framework는 core가 이미 제공하는 dispatch boundary를 다시 queue로 복제하지 않고,
typed payload 변환, DI scope, handler 호출, 오류 격리, completion 규칙을 닫는다.
CPU-bound 또는 blocking 가능성이 있는 handler는 framework core의 offload executor로
넘기도록 가이드하고, 해당 선택은 handler option으로 명시한다.

모든 channel handler, route handler, SPOT handler, stream session callback은 framework
coroutine executor를 통과해 실행한다. 내부 구현은 `boost::asio::thread_pool` 위에서
`boost::asio::co_spawn`으로 `boost::asio::awaitable<result_t<T>>`를 실행한다. 사용자는
이 사실을 알 필요가 없고, handler에서는 `task_t<T>`와 `co_await call.async()`만 사용한다.
handler registry와 route/SPOT/stream dispatch의 내부 invoker는 `task_t<...>`를 반환해야
하며, executor coroutine은 이 task를 await한다. `.result()`는 C core callback처럼
동기 반환값이 필요한 framework 경계나 테스트 검증 경계에서만 사용한다.
handler coroutine executor의 기본 worker 수는 CPU 수 기반으로 잡는다. 전역 1개 thread로
모든 handler를 직렬화하지 않는다. 순서 보장은 worker 수를 줄여서 만들지 않고, channel,
spot, stream session 같은 의미 단위의 직렬화 정책으로 닫는다. 필요한 경우
`handler_coroutine_workers(n)`으로 worker 수를 명시한다.

실행 위치와 순서 보장은 아래 계약으로 닫는다.

| 입력 경로 | 사용자 표면 | 순서 보장 |
|-----------|------------|-----------|
| channel request/send | registered channel handler | core 도착 순서와 channel 정책을 따른다 |
| pub/sub event | registered subscriber handler | core 도착 순서와 topic 정책을 따른다 |
| STREAM session lifecycle/packet | registered stream session handler | 같은 session의 connected, packet, disconnected callback은 직렬 |
| Entry Spot actor packet | registered Entry Spot actor handler | 같은 actor id는 core actor ordering을 따른다 |
| user Spot packet/actor packet/subscription/timer | registered Spot handler | 같은 user Spot 안에서는 core SPOT dispatch boundary를 따른다 |
| Entry Spot timer | registered Entry Spot timer handler | Entry Spot actor packet, lifecycle callback, request continuation과 같은 실행 줄에서 처리하고, 같은 timer instance도 재진입 금지 |
| network operation resume | `co_await call.async()` | 성공 값 또는 `framework_exception_t`로 완료한다 |
| CPU-bound handler | `handler_options_t::execution = handler_execution_t::offload` | framework core offload executor에서 실행한다 |

application handler는 CAPI callback 함수 본문 안에서 직접 실행하지 않는다. framework는
recv, header 검증, deserialize, DI resolve, error isolation을 거친 뒤 등록된 handler를
호출한다. 일반 handler는 기본 경로를 사용하고, CPU-bound 작업이나 blocking 가능성이
있는 legacy API 호출은 offload 실행 정책을 명시해 별도 thread에서 처리한다.

### 4.9 Backpressure / Flow Control

backpressure는 zlink framework의 핵심 강점으로 다룬다.

지원할 기능은 아래와 같다.

- send-ready runtime integration
- HWM awareness
- monitoring을 통한 queue depth 조회
- drop / retry policy
- bounded pending queue
- graceful drain

기본 정책은 무한 queue가 아니다. queue 상한, submit timeout, overflow 정책을
명시적으로 둘 수 있어야 한다.

send-ready callback, pending queue resume, HWM drain 순서는 runtime 내부 구현이다.
application은 `co_await call.async()`, timeout exception, monitoring event로
backpressure를 본다.
`spot_context_t` 같은 public context가 pending queue를 직접 resume하는 API를 제공하지
않는다.

기본 backpressure 계약은 아래처럼 둔다.

| 상황 | 기본 결과 |
|------|-----------|
| pending queue 한도 초과 | `request_rejected` 실패 result |
| timeout 전 send-ready 발생 | pending 작업을 drain하고 success 또는 하위 submit result로 완료 |
| timeout까지 send-ready 없음 | `timeout` 실패 result |
| shutdown 시작 뒤 새 submit | `shutdown` 실패 result |
| shutdown 중 이미 pending인 작업 | graceful drain 안에서 처리하고, 만료되면 `shutdown` 실패 result |
| route가 연결되지 않음 | `route_not_connected` 실패 result |
| 대상 handler 또는 actor route 없음 | `handler_not_found`, `actor_route_not_found`, `spot_route_not_found` 계열 실패 result |

drop 정책은 기본값으로 두지 않는다. 명시 drop/retry/dead-letter 정책은 reliability
초안에서 별도 계약으로 닫는다.

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
app.logging()
  .use_console()
  .use_rotating_file("logs/zlink.log")
  .use_async({ .queue_capacity = 8192 })
  .set_min_level(log_level_t::info);
app.metrics().add_runtime_metrics();
app.health().add_zlink_runtime_check();
```

logging과 health는 core 관찰 표면이다. metrics와 tracing은 event 이름, label
cardinality, exporter 정책을 정한 뒤 observability extension으로 확장한다.

logging public API는 `zlink::framework::logger_t<TCategory>`와
`zlink::framework::logger_factory_t`가 소유한다. `.NET`의 `ILogger<T>`에 대응하는 C++
표면이며, handler와 hosted service는 이 타입을 통해 log를 남긴다. public header에는
`spdlog` logger나 sink 타입을 노출하지 않는다.

logging 설정과 logger 주입은 분리한다. `app.logging()`은 provider와 sink를 구성하는
host-level 표면이다. 예를 들어 console, file, rotating file, callback sink, minimum level은
여기서 설정한다. 반대로 handler와 service는 `dependency_types`에
`logger_t<handler_type>` 또는 `logger_factory_t`를 선언해서 logger를 받는다. 사용자가
`options.services().add_singleton<logger_t<...>>()`처럼 logger instance를 직접 등록하지
않는다. `app_t::add_zlink_framework(...)`는 app logging state와 연결된 `logger_factory_t`를
기본 서비스로 제공하고, DI는 `logger_t<TCategory>` dependency를 자동으로 생성한다. sink가
등록되지 않은 경우에도 logger 주입은 성공하며, 로그는 내부 captured record 외에는 외부로
출력되지 않는 null sink 의미로 동작한다.

샘플과 애플리케이션 코드에서도 같은 경계를 유지한다. `api_server_host_factory_t::build(...)`
같은 host factory는 console/file/callback sink와 level을 정할 수 있지만,
`add_bingo_api_server(...)`처럼 framework 기능을 추가하는 함수는 channel, codec, discovery,
handler 등록만 담당한다. 이렇게 나누면 같은 API server 구성을 테스트, 콘솔 샘플, 파일 로그
운영 설정에서 다시 사용할 때 logging 정책을 덮어쓰기 쉽다.

지원하는 sink는 아래와 같다.

| sink | API | 용도 |
|------|-----|------|
| null | sink 미등록 | 테스트 또는 완전 무음 기본값 |
| console | `use_console()` | 개발과 샘플 실행 |
| file | `use_file(path)` | 단일 파일 로그 |
| rotating file | `use_rotating_file(path, options)` | 운영 파일 로그 |
| callback | `use_callback_sink(callback)` | 테스트, 사용자 sink, 외부 exporter 연결 |

내부 backend는 기본 backend와 구조화된 backend 선택지를 둔다. 구조화된 backend가 어떤 logging
library를 쓰는지는 framework 내부 구현 세부로만 사용하며, C++ public API에는 드러내지 않는다.
현재 public 계약은 backend 선택을 `use_backend(logging_backend_t::structured)`로 표현하고,
실제 sink 호출은 logging runtime이 닫는다.

async logging은 `use_async(logging_async_options_t)`로 켠다. queue overflow 정책은
`drop_debug`, `drop_oldest`, `block` 중 하나로 명시한다. runtime thread에서 blocking I/O가
문제가 되는 환경에서는 `drop_debug` 또는 `drop_oldest`를 권장하고, `block`은 테스트나
특수 운영 환경에서만 사용한다.

event schema는 typed payload 기준으로 둔다. 모든 event는 최소한 아래 필드를 가진다.

- `source_name`
- `timestamp`
- `kind`
- `severity`
- `node_name`
- `correlation_id` 또는 빈 값

도메인별 event는 아래 범주로 나눈다.

| 범주 | 예시 kind |
|------|-----------|
| socket | connected, disconnected, send_ready, send_failed |
| discovery | endpoint_added, endpoint_removed, view_changed |
| registry | snapshot_changed, query_failed |
| stream | session_connected, session_disconnected, packet_rejected, write_failed |
| spot | status_changed, peers_changed, subjects_changed |
| spot timer | timer_handler_failed, timer_stopped_after_unhandled_exception |
| actor/session | actor_bound, actor_unbound, relay_failed, bound_session_closed |

snapshot diff event는 interval 설정을 따른다. timer failure, handler exception, packet
rejection, relay failure 같은 point-in-time event는 interval을 기다리지 않고 즉시
발행한다.

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

JSON loader는 `nlohmann/json`을 사용한다. YAML은 core configuration 표면에 넣지 않고,
필요하면 extension으로 둔다. 설정 key
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

### 4.13.1 Lifecycle / Ownership

app shutdown 순서는 아래처럼 닫는다.

1. stop requested event를 발행하고 새 submit을 `shutdown` result로 거부한다.
2. STREAM accept와 channel ingress를 멈춘다.
3. pending request/send/relay를 graceful drain timeout까지 처리한다.
4. hosted service `stop`을 호출한다.
5. timer를 취소하고 timer callback 재진입을 막는다.
6. SpotNode, stream socket, channel socket, discovery, registry client를 닫는다.
7. CAPI dispatch callback 연결을 끊고, offload executor를 drain한 뒤 context를
   종료한다.

ownership 규칙은 아래와 같다.

- callback에서 받은 `message_t`와 payload view는 callback 동안만 빌린 값이다.
- callback 밖으로 보관하려면 copy 또는 move 정책을 명시해야 한다.
- `relay(...)`와 `send(...)`는 caller payload를 소비하지 않는다. remote frame이
  필요하면 framework runtime이 별도 buffer를 만든다.
- stream close는 session binding cleanup만 수행하고 actor current Spot을 바꾸지 않는다.
- user Spot destroy/leave 중 pending 작업은 target lifecycle 결과에 맞춰 `closed` 또는
  `shutdown` result로 완료한다.

### 4.14 Discovery / Topology

Discovery와 topology는 zlink framework의 차별화 축이다.

제공할 기능은 아래와 같다.

- service discovery
- node registry
- spot registry
- gateway topology
- peer awareness
- topology snapshot

애플리케이션 코드는 가능하면 channel과 역할을 기준으로 연결을 설정한다.
직접 peer endpoint를 넣는 manual 연결도 지원하되, 같은 역할 안에서 Discovery와
manual 연결을 섞지 않는다.

Registry-backed 기본값은 Spot remote address 조회에 사용한다. session actor relay는
Registry actor route lookup을 hot path로 쓰지 않고, stream의 session relay와
logical actor handle을 사용한다. actor-session binding은 framework/core runtime state
이며 Registry row나 sample-only metadata store에 저장하지 않는다.

### 4.15 STREAM 범위

framework core에서 `STREAM`은 packet 방식만 지원한다. 그중에서도 header는 framework가
정의한 `stream_header_t` 형식만 사용한다. raw stream session, 사용자 정의 header
framing, 임의 byte stream dispatch는 framework core public 표면에 넣지 않는다.

이 제한은 `.NET` framework 쪽 STREAM 정책과 같은 방향이다. application handler가
transport별 framing이나 raw stream read loop를 직접 다루지 않게 하고, framework가
검증 가능한 packet header와 payload 단위로 lifecycle, dispatch, backpressure를 닫기
위한 정책이다.

### 4.16 Transport Abstraction

transport abstraction은 zlink core가 제공하는 transport 의미를 framework 표면으로
감싼다. HTTP hosting은 외부 HTTP client가 zlink runtime으로 들어오는 host 기능이고,
zlink channel/STREAM transport와는 별도 개념이다.

지원 범위는 zlink core 기준으로 아래와 같다.

- TCP
- IPC
- TLS
- WebSocket

프레임워크는 transport를 숨긴다. 사용자는 endpoint URI와 보안 옵션을 설정할 수
있지만, handler와 client 코드는 transport가 TCP인지 TLS인지 알 필요가 없어야 한다.
HTTP hosting은 `options.http().map_get/map_post/map_put/map_delete<THandler>(...)`로 다룬다. WebSocket 계열
transport가 필요하면 HTTP hosting과 섞지 않고 STREAM Connector 또는 zlink transport
경계에서 다룬다.
PGM은 `C++` framework 지원 범위에 넣지 않는다.

### 4.17 Reliability Features

reliability 기능은 완성 형태까지 고려해 public 표면이 막히지 않게 설계한다.

지원 범위는 아래와 같다.

- retry
- timeout
- dead-letter
- drain
- graceful close
- lifecycle cancellation
- idempotency key hook

timeout, graceful close, drain은 core reliability 표면이다. retry와 dead-letter는
handler 재실행 의미, ordering, 중복 처리 정책이 필요하므로 별도 초안으로 분리해
정확한 계약을 닫는다.

호출 실행 표면은 `.NET` framework의 awaitable network API와 같은 방향으로 둔다.
C++에서는 `request(...)`, `send(...)`, `relay(...)`가 call object를 만들고,
`co_await call.async()`가 실행 지점이다. `async()`는 callback을 받지 않고
`task_t<T>` 또는 `task_t<void>`를 반환한다. public async 표면에 `std::future`를 사용하지
않고, handler/runtime 내부에서 blocking wait를 허용하지 않는다.

coroutine submit은 성공 시 값을 반환하고, 실패 시 `framework_exception_t`를 throw한다.
이 exception은 `.NET` framework의
`ZLinkFrameworkException`처럼 error kind와 message, retriable 여부를 담는다.
error kind 이름은 C++ naming으로 바꾸되 의미는 `.NET`의 `ZLinkFrameworkErrorKind`를
기준으로 맞춘다.

### 4.18 Integration Layer

integration layer는 core framework 위의 확장 계층으로 둔다.

확장 영역은 아래와 같다.

- Kafka bridge
- gRPC bridge

구현 순서는 bridge보다 framework core를 먼저 안정화한다. 외부 시스템 bridge는
messaging core, serialization, backpressure, observability가 정리된 뒤 추가한다.
ASP.NET Core Minimal API의 route handler에 대응하는 HTTP hosting은 extension이 아니라
framework core hosting 기능이다. Kafka, gRPC 같은 외부 system bridge와 같은 분류로 두지 않는다.

## 5. 구현 순서

아래 순서는 기능 축소가 아니라 구현 순서다. 최종 C++ framework는 `.NET` framework와
동일한 수준의 실시간 메시징 프로그램 개발 기능을 제공하는 것을 목표로 한다.

| 순서 | 영역 | 완료 기준 |
|:--:|------|----------|
| 1 | app / host | `app_t::create`, 구성, `int run`, signal handling, graceful shutdown |
| 2 | DI | 자체 container, public service API, singleton/scoped/transient/factory |
| 3 | runtime integration | context, socket, discovery, spot node lifecycle을 host가 관리 |
| 4 | handler framework | typed subscribe/request handler 등록, invoke, offload 실행 정책 |
| 5 | messaging core | publish, request, send, reply dispatch |
| 6 | spot abstraction | spot lifecycle, core dispatch ordering, direct routing, publish |
| 7 | SPOT timer | tick metadata, overrun policy, timer failure monitoring |
| 8 | actor/session relay | session relay, session bind, relay, bound session push |
| 9 | hosted services | start/stop lifecycle과 shutdown 연동 |
| 10 | HTTP hosting | ASP.NET Core Minimal API route handler 대응, JSON DTO binding, DI handler, status mapping |
| 11 | logging / health | runtime 오류, handler 예외, runtime 상태를 볼 수 있는 core 표면 |
| 12 | graceful shutdown | drain, offload executor stop, socket close 순서 검증 |

core framework 밖의 확장 영역은 아래와 같다. 이 목록은 기능을 제외한다는 뜻이
아니라, framework 본체의 필수 계약과 분리해도 사용자 모델이 깨지지 않는 영역을
구분한 것이다.

- metrics
- tracing
- Kafka bridge
- advanced retry
- FlatBuffers integration
- YAML configuration

## 5.1 Packaging / Public Header

패키징은 아래 기준으로 둔다.

- 단일 umbrella header는 `#include <zlink/framework.hpp>`다.
- 세부 header는 `zlink/framework/*.hpp` 아래에 둔다.
- CMake target 이름은 `zlink::framework`다.
- framework target은 필요한 binding target을 private 또는 transitive dependency로
  정확히 연결한다. 사용자가 native C header를 직접 include해야 동작하는 표면은 만들지
  않는다.
- public template API는 header에 둘 수 있지만, 허용 범위는 type trait, concept check,
  thin forwarding으로 제한한다. CAPI dispatch binding, native handle owner,
  runtime event binding, ActorGateway frame codec은 `src/runtime/` 구현에 숨긴다.
- 외부 라이브러리 타입은 public handler/module/config callback signature에 노출하지
  않는다.
- public contract header는 `src/runtime/*` header를 include하지 않는다. 필요한 전방 선언,
  type trait, thin forwarding은 `contracts/detail/*` 안에서만 둔다.

public header review는 모든 기능에서 같은 순서로 진행한다. 먼저 `.NET`의 같은 기능이
`Contracts/*`에 무엇을 공개하고 `Runtime/*`에 무엇을 숨기는지 확인한다. 그 다음 C++에서
같은 개념을 value type, concrete facade, abstract extension point, template concept 중
무엇으로 표현할지 정한다. 마지막으로 runtime state와 실행 자료구조가 설치 header에
들어오지 않는지 확인한다. 이 순서를 거치지 않은 public 타입은 추가하지 않는다.

구현 중 이 경계가 흔들리면 해당 goal을 계속 진행하지 않는다. 먼저 관련 draft 문서를
수정해 public contract owner, runtime implementation owner, test boundary를 다시 고정한
뒤 구현을 재개한다. 특히 `bindings/cpp`에서 허용되던 얇은 inline wrapper 습관을
framework로 가져오지 않는다. framework public header는 application 계약을 제공하는
계층이며, native API 호출 편의를 그대로 노출하는 계층이 아니다.

아래 항목은 public header에 나타나면 안 되는 구현 지식이다.

- native context/socket/handle owner
- CAPI dispatch callback과 callback userdata
- poller slot과 recv/drain 순서
- pending request table과 send-ready queue
- stream header/frame codec 구현
- ActorGateway relay packet dispatcher
- Spot activation table과 native route dispatcher
- timer token과 `fire_count` drain loop
- monitoring snapshot diff cache

사용자가 조절해야 하는 값은 내부 구현 타입을 노출하지 않고 option, builder, callback,
result, event model로 표현한다. 이 기준을 지키면 C++ public API는 `.NET`처럼 단순한
계약을 제공하면서도 C++20의 RAII, template, coroutine 사용성을 유지할 수 있다.

C++ Stream Connector 패키징은 별도 문서인
[STREAM Connector 가이드](../../../stream-connector/cpp/guide/INDEX.ko.md)를 따른다. framework sample이나
framework target에 connector public API를 섞지 않는다.
Unreal Connector도 같은 문서에서 다루며 framework target에 포함하지 않는다.

## 5.1.1 Review Samples

framework 동작 리뷰 샘플은 `Bingo`와 `TicTacToe` 두 개로 둔다.

`Bingo`는 channel/SPOT/session stream 중심의 기본 실시간 메시징 샘플이다. app/host, DI,
channel request/reply, session packet dispatch, publish/subscribe, coroutine submit,
handler error mapping, user Spot, timer, monitoring, graceful shutdown,
CPU-bound handler offload를 검토한다. packet 이름과 handler 흐름은 `.NET` Bingo 샘플과
같은 수준으로 유지한다.

`TicTacToe`는 HTTP 시작 요청, STREAM, ActorGateway 기반 actor/session relay 샘플이다.
`.NET` TicTacToe처럼 client가 먼저 HTTP `POST /games`를 호출하고, C++ sample의
`CreateGameHttpReq` API handler 응답에 담긴 Play stream endpoint에 connector가 연결된다.
이 샘플은 HTTP hosting, STREAM endpoint, session relay, Entry Spot, actor factory,
session actor bind, relay, bound session push, actor join/move, disconnect cleanup을
검토한다.

권장 샘플 배치는 아래와 같다.

```text
framework/languages/cpp/samples/
+-- Bingo/
|   +-- README.ko.md
|   +-- Shared/
|   |   +-- Configuration/
|   |   |   +-- sample_names.hpp
|   |   |   +-- sample_topology.hpp
|   |   +-- Contracts/
|   |   |   +-- messages.hpp
|   |   +-- host_support.hpp
|   |   +-- sample.hpp
|   +-- Client/
|   |   +-- bingo_client_scenario.hpp
|   |   +-- main.cpp
|   +-- Server/
|       +-- Registry/
|       |   +-- main.cpp
|       +-- Api/
|       |   +-- Handlers/
|       |   |   +-- authenticate_player_handler.hpp
|       |   |   +-- match_bingo_handler.hpp
|       |   +-- main.cpp
|       +-- Play/
|       |   +-- Infrastructure/
|       |   |   +-- ZLink/
|       |   |       +-- Handlers/
|       |   |       |   +-- allocate_bingo_room_handler.hpp
|       |   |       |   +-- ensure_player_actor_handler.hpp
|       |   |       +-- Spots/
|       |   |           +-- EntrySpot/
|       |   |           |   +-- bingo_entry_spot.hpp
|       |   |           +-- BingoRoomSpot/
|       |   |               +-- bingo_room_spot.hpp
|       |   +-- main.cpp
|       +-- Session/
|           +-- main.cpp
+-- TicTacToe/
|   +-- README.ko.md
|   +-- Shared/
|   |   +-- Actors/
|   |   |   +-- player_actor.hpp
|   |   +-- Configuration/
|   |   |   +-- sample_names.hpp
|   |   |   +-- sample_topology.hpp
|   |   +-- Contracts/
|   |   |   +-- messages.hpp
|   |   +-- host_support.hpp
|   |   +-- sample.hpp
|   +-- Client/
|   |   +-- tictactoe_client_scenario.hpp
|   |   +-- main.cpp
|   +-- Server/
|       +-- Api/
|       |   +-- Handlers/
|       |   |   +-- authenticate_player_handler.hpp
|       |   |   +-- create_game_http_handler.hpp
|       |   +-- main.cpp
|       +-- Play/
|       |   +-- Application/
|       |   |   +-- GameCreation/
|       |   |       +-- tictactoe_game_creator.hpp
|       |   +-- Infrastructure/
|       |       +-- ZLink/
|       |           +-- Handlers/
|       |           |   +-- create_game_handler.hpp
|       |           |   +-- ensure_player_actor_handler.hpp
|       |           +-- Spots/
|       |               +-- EntrySpot/
|       |               |   +-- tictactoe_entry_spot.hpp
|       |               +-- TicTacToeGameSpot/
|       |                   +-- tictactoe_game_spot.hpp
|       |   +-- main.cpp
```

CTest sample smoke는 모든 역할 실행 파일을 `framework-sample-smoke` label로 묶고,
샘플별로 `framework-sample-bingo`, `framework-sample-tictactoe` label을 함께 붙인다.
또한 `framework-sample-parity` contract test로 `.NET` 샘플과 맞춰야 하는 packet 이름과
핵심 handler 흐름을 고정한다.

## 5.2 Test Matrix

구현 완료 기준은 아래 테스트 축을 포함한다.

테스트 도구 결정은 아래와 같다.

- CMake option은 `ZLINK_FRAMEWORK_CPP_BUILD_TESTS`, `ZLINK_FRAMEWORK_CPP_BUILD_SAMPLES`를 둔다.
- test runner는 CTest다. `framework/languages/cpp` 안의 test executable과 sample smoke를
  CTest label로 등록한다.
- test harness는 GoogleTest를 기본으로 사용한다. runtime boundary, handler invoker,
  offload executor, monitoring sink처럼 fake나 expectation이 필요한 테스트에는
  GoogleMock을 사용한다.
- GoogleTest와 GoogleMock은 framework C++ 개발 의존성이다. public framework header와
  배포 runtime dependency에는 노출하지 않는다.
- C++20 compile contract 테스트를 별도로 둔다. public header, concepts, coroutine
  return type, network `async()` signature가 깨지면 컴파일 단계에서 실패해야 한다.
- 회귀 테스트는 `.NET` framework와 같은 기능 축을 C++ 표면으로 반복한다. C++ 문법만
  다르고 기능 기대값은 같아야 한다.

| 축 | 검증 내용 |
|----|-----------|
| app/host | startup, signal stop, graceful shutdown, shutdown 중 submit result |
| DI/module | singleton/scoped/transient/factory, duplicate registration, shutdown 중 resolve 금지 |
| channel request/reply | typed request, typed reply, timeout, handler not found, payload decode failure, reply serialization failure, disconnected, queue full |
| channel send/event | no-reply send, command dispatch, event dispatch, handler exception masking, topic mismatch, no subscriber |
| pub/sub | single subscriber, multiple subscriber, unsubscribe, publisher close, subscriber disconnect, slow subscriber backpressure |
| route channel | manual route connection, discovery route connection, routing id selection, routed request/reply, route handler not found, ambiguous route validation |
| async surface | `co_await async()`, exception mapping, callback overload 금지, blocking wait 금지 |
| handler execution | 기본 handler 실행, CPU-bound handler offload, concurrency 제한, shutdown drain |
| STREAM | connected/disconnected/error callback, packet header validation, session ordering, write backpressure, close cleanup, invalid packet drop |
| SPOT | spot create/destroy, join/leave, actor handler, publish, request_to, route resolver, core SPOT dispatch ordering |
| timer | CAPI timer projection, fire_count 기반 skipped tick 계산, tick metadata, overrun policy, exception event, cancel, Entry Spot timer non-global serialization |
| ActorGateway relay | bind local/remote actor, relay request/reply, bound session push, actor generation round-trip, duplicate/type mismatch/missing actor, disconnect cleanup |
| Registry | Spot remote address lookup, discovery update, duplicate resolver rejection, ambiguous route channel validation, stale address cleanup |
| codec/serializer | raw message, JSON DTO, optional codec target on/off, serializer missing startup failure, invalid payload runtime failure |
| monitoring/logging | typed event payload, immediate failure event, snapshot diff interval, correlation id, server/client file log |
| samples | `Bingo`, `TicTacToe` sample smoke/e2e, zlink message log assertion |

회귀 테스트 레이어는 아래처럼 나눈다.

| 레이어 | 목적 | CTest label |
|--------|------|-------------|
| contract compile | public header와 타입 표면이 의도대로 컴파일되는지 확인. 필요한 경우 GoogleTest binary 안의 static assertion test로 묶는다 | `framework-contract` |
| unit | GoogleTest로 DI, serializer, handler registry, error mapping, call object 상태 전이를 process 내부에서 검증 | `framework-unit` |
| integration | GoogleTest fixture와 core runtime을 사용해 channel, STREAM, SPOT, ActorGateway 흐름을 inproc/tcp로 검증 | `framework-integration` |
| sample smoke | 문서 샘플과 실제 sample executable이 실행 가능한지 확인 | `framework-sample-smoke` |
| regression | GoogleTest로 과거 버그와 `.NET` parity 항목을 명시적으로 고정 | `framework-regression` |
| zlink channel | channel request/reply, send/event, pub/sub, route channel을 `.NET`과 같은 error/lifecycle 의미로 검증 | `framework-zlink-channel` |
| zlink SPOT | Spot lifecycle, actor handler, timer, ordering, remote address resolution을 검증 | `framework-zlink-spot` |
| zlink STREAM | packet session, header validation, write backpressure, session cleanup을 검증 | `framework-zlink-stream` |
| zlink ActorGateway | session bind, relay, bound session push, actor failure mapping을 검증 | `framework-zlink-actor-gateway` |
| zlink Registry | discovery/registry lookup, resolver validation, stale address cleanup을 검증 | `framework-zlink-registry` |

필수 회귀 항목은 아래와 같다.

- `co_await async()`가 timeout/error kind를 보존한 `framework_exception_t`로 실패한다.
- shutdown 이후 새 `async()`는 `shutdown`으로 실패한다.
- pending queue 한도 초과는 `request_rejected`로 실패한다.
- channel handler가 없으면 request는 `handler_not_found` 계열 error로 닫히고 runtime은 계속
  동작한다.
- handler decode 실패는 `payload_decode_failed`로 보고하고 runtime을 죽이지 않는다.
- reply serialization 실패는 caller에게 실패 result로 돌아가고 server-side log와 monitoring
  event를 남긴다.
- pub/sub에서 subscriber 하나가 느리거나 끊겨도 publisher와 다른 subscriber를 같이 죽이지
  않는다.
- manual connection과 Discovery connection을 같은 역할에 섞으면 startup validation이
  실패한다.
- scoped service는 handler dispatch, stream session, Spot activation 경계를 넘지 않는다.
- STREAM header validation 실패는 application handler로 전달되지 않는다.
- STREAM session callback은 같은 session 안에서 직렬로 처리된다.
- ActorGateway relay는 application route mesh channel을 우회하지 않는다.
- `actor_ref_t`의 `node_rid`, `actor_id`, `generation` round-trip이 유지된다.
- actor duplicate와 actor type mismatch는 각각 `actor_already_exists`,
  `actor_type_mismatch`로 보고한다.
- user Spot timer는 core SPOT dispatch ordering을 따르고, Entry Spot timer는 Entry Spot
  actor packet, lifecycle callback, request continuation과 같은 Entry Spot 실행 줄에서
  처리한다.
- Registry remote address snapshot이 바뀌면 route resolver는 stale address를 계속 사용하지
  않는다.
- `Bingo`와 `TicTacToe` e2e는 client 결과뿐 아니라 server file log에서 request, reply,
  push, disconnect, shutdown 이벤트를 확인한다.
- CPU-bound handler를 `handler_execution_t::offload`로 등록하면 offload executor drain이
  graceful shutdown에 포함된다.

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

## 7. 현재 결정 상태

이 문서 기준으로 C++ framework core 구현을 시작하기 위한 추가 사용자 결정 항목은 없다.
dead-letter 저장소, 재처리 표면, YAML 같은 추가 configuration source는
core framework 계약 밖의 확장으로 별도 설계한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Spec -- ZLink Framework C++ Interface Design](../spec/cpp-framework-interfaces.ko.md) | [다음: Spec -- ZLink Framework C++ Monitoring](../spec/cpp-monitoring.ko.md)
<!-- framework-adapter-nav:bottom:end -->
