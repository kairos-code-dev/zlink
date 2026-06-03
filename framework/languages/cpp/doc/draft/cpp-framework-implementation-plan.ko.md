<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework For C++](./README.ko.md) | [다음: Draft -- ZLink Framework C++ Policy](./cpp-framework-policy.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Application Framework](./cpp-application-framework.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [HTTP Client](./cpp-http-client.ko.md)

# Draft -- ZLink Framework C++ Implementation Plan

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `framework/languages/cpp/doc/draft` 아래 C++ framework
> 초안 전체를 빠짐없이 구현하기 위한 실행 계획이다.

## 1. 목적

이 문서는 C++ framework 구현을 22개 goal로 나누어 진행하기 위한 기준이다.
각 goal은 구현, 검증, POSD 기반 리팩토링, 문서 대조를 끝낸 뒤에만 완료로 본다.

최종 완료 기준은 아래 세 가지다.

- C++ framework는 `.NET` framework와 동일한 구조를 가진다.
- C++ framework는 `.NET` framework와 동일한 기능을 제공한다.
- C++ framework는 `.NET` framework와 동일 수준의 사용성을 제공한다.

주 벤치마크는 `.NET Core`다. host, DI, configuration, logging, lifecycle은
`.NET Generic Host` 계열을 기준으로 삼고, HTTP routing과 route handler 표면은
`ASP.NET Core Minimal API`를 기준으로 삼는다. 구현 순서는 다르더라도 최종 기능 범위는
줄이지 않는다.

## 2. 공통 실행 규칙

모든 goal은 아래 규칙을 따른다.

- C++20 이상만 지원한다.
- public async 표면에는 `std::future`를 쓰지 않는다.
- public 호출은 call object를 만들고 마지막에 `submit(callback)` 또는
  `co_await submit()`으로 실행한다.
- public cancellation token 타입은 두지 않는다. shutdown, timeout, close, disconnected
  상태는 framework result 또는 `framework_exception_t`로 표현한다.
- handler, timer, stream session, actor relay 안에서 blocking wait를 허용하지 않는다.
- 모든 handler 계열은 coroutine executor를 통해 실행한다.
- CPU-bound 또는 blocking 가능성이 있는 handler는 `handler_execution_t::offload`로
  framework offload executor에서 실행한다.
- 사용자는 native C handle, raw poller slot, callback userdata, raw recv loop를 직접
  다루지 않는다.
- CAPI dispatch callback과 CAPI timer event는 framework runtime 내부에서 typed handler로
  사상한다.
- public contract header는 runtime 구현 header를 include하지 않는다.
- runtime 세부는 `framework/src/runtime/*`, `connector/src/runtime/*`,
  `http-client/src/runtime/*`, Unreal `Private/`에 둔다.
- `contracts/detail/*`은 type trait, concept check, facade forwarding만 가진다.
- public facade가 상태를 가지면 PIMPL 또는 type-erased state를 사용한다.
- codec 사용성은 binding, framework, connector, HTTP client 모두 `message_t` 중심으로 맞춘다.
- base C++ binding은 JSON, MessagePack, Protobuf dependency를 끌고 오지 않는다.
- MessagePack과 Protobuf는 선택 기능으로 둔다. LZ4 지원은 connector build feature 기본 ON으로
  두고, 실제 packet 압축은 호출 지점의 option으로 선택한다.
- application sample code는 serializer를 직접 호출하지 않는다.
- 샘플 client는 raw STREAM payload 조립, `nlohmann::json::parse`, field-by-field JSON 추출,
  sample-only stream payload helper를 사용하지 않는다.
- 테스트는 CTest로 등록하고, framework C++ 테스트는 GoogleTest와 GoogleMock을 사용한다.
- 모든 goal은 구현 뒤 POSD 기반 리팩토링을 반드시 한 번 이상 수행한다.
- 22개 goal을 모두 진행하면 POSD 기반 리팩토링 기록도 최소 22개가 있어야 한다.
- 한 goal 안에서 POSD 위험 신호나 리팩토링 이슈가 남아 있으면 다음 goal로 넘어가지 않는다.

## 3. 산출물 경계

| 산출물 | 위치 | target | 역할 |
|--------|------|--------|------|
| C++ framework | `framework/languages/cpp/framework` | `zlink::framework` | server application host/runtime |
| C++ Stream Connector | `framework/languages/cpp/connector` | `zlink::stream_connector` | client-side STREAM connector |
| ZLink HTTP Client | `framework/languages/cpp/http-client` | `zlink::http_client` | client-side HTTP/JSON connector |
| Unreal Stream Connector | `framework/languages/cpp/unreal-connector` | Unreal module/plugin | Unreal 전용 client connector |
| framework extensions | `framework/languages/cpp/extensions` | extension targets | bridge, config, codec, observability 확장 |
| framework samples | `framework/languages/cpp/samples` | sample executables | `Bingo`, `TicTacToe` 리뷰 샘플 |
| framework tests | `framework/languages/cpp/tests/Zlink.Framework.*Tests` | CTest labels | `.NET` test project 분류에 맞춘 contract, unit, e2e, package |
| connector tests | `framework/languages/cpp/tests/Systems.Zlink.Stream.Connector.Tests` | CTest labels | connector contract, protocol, transport, typed 흐름 |
| Unreal connector tests | `framework/languages/cpp/tests/Zlink.Unreal.Stream.Connector.Tests` | CTest labels | Unreal public API compile/smoke와 automation source check |

각 산출물은 `.NET` framework의 `Contracts/*`와 `Runtime/*` 분리를 따른다.

| `.NET` 기준 | C++ framework 기준 | C++ connector 기준 | HTTP client 기준 | 공개 여부 |
|-------------|--------------------|---------------------|------------------|-----------|
| `Contracts/*` | `framework/include/zlink/framework/contracts/*` | `connector/include/zlink/stream_connector/contracts/*` | `http-client/include/zlink/http_client/contracts/*` | public |
| `Runtime/*` | `framework/src/runtime/*` | `connector/src/runtime/*` | `http-client/src/runtime/*` | private implementation |
| `Runtime/Backend/Contracts` | `framework/src/runtime/backend/contracts` | `connector/src/runtime/backend/contracts` | 현재 없음 | private backend contract. HTTP client는 현재 별도 backend adapter가 없으므로 placeholder 디렉터리를 만들지 않는다 |
| project facade | `zlink/framework.hpp`, `zlink/framework/*.hpp` | `zlink/stream_connector.hpp` | `zlink/http_client.hpp` | public |
| implementation glue | `framework/src/runtime/*` | `connector/src/runtime/*` | `http-client/src/runtime/*` | private |

Unreal Connector는 Unreal 관례에 따라 `Source/ZLinkStreamConnector/Public`과
`Source/ZLinkStreamConnector/Private`를 사용한다. `Public`은 Unreal 전용 contract와
Blueprint/Game Thread 표면만 담고, transport와 codec 구현은 `Private`에 둔다.

## 4. Goal 공통 완료 절차

각 goal은 아래 순서로 진행한다.

1. 관련 draft 문서를 먼저 읽는다.
2. 같은 기능의 `.NET` framework 구조와 sample/test를 확인한다.
3. public contract owner와 runtime implementation owner를 확정한다.
4. public surface gate를 통과한다.
5. public header와 target 구조를 구현한다.
6. internal runtime 구현을 붙인다.
7. 해당 goal의 contract/unit/integration/e2e 테스트를 추가한다.
8. 필요한 샘플 코드를 갱신한다.
9. POSD 기반 리팩토링 게이트를 통과할 때까지 리팩토링을 반복한다.
10. 리팩토링 뒤 같은 검증을 다시 실행한다.
11. `git diff --check`와 해당 CTest label을 실행한다.
12. 남아 있는 POSD 위험 신호와 리팩토링 이슈가 없는지 확인한다.
13. 완료 기준을 문서 항목별로 대조한다.

### 4.1 Public Surface Gate

각 goal은 구현 전에 아래 항목을 통과해야 한다.

| 확인 항목 | 통과 기준 |
|-----------|-----------|
| `.NET` 대응 확인 | 같은 기능의 `.NET Contracts/*`, `Runtime/*`, sample, test를 먼저 확인한다. |
| contract owner | 새 public 타입이 들어갈 `contracts/*` header 또는 facade header가 정해져 있다. |
| runtime owner | state, registry, cache, queue, dispatcher, frame codec, native owner가 들어갈 `src/runtime/*` 파일이 정해져 있다. |
| public dependency | public header가 불필요한 외부 dependency를 강제하지 않는다. |
| native leakage | CAPI handle, socket, poller slot, callback userdata, dispatch token이 public signature에 없다. |
| detail 사용 | `contracts/detail/*`은 type trait, concept check, forwarding만 가진다. |
| state hiding | public facade가 상태를 가지면 PIMPL 또는 type-erased state를 사용한다. |
| validation | contract/layout test가 public header include와 runtime include 금지 규칙을 확인한다. |

### 4.2 POSD 리팩토링 게이트

각 goal의 POSD 리팩토링은 구현 완료 뒤 별도 단계로 처리하지 않는다. 구현이 끝난 직후
같은 goal 안에서 아래 순서를 반복해서 수행하고, 남아 있는 리팩토링 이슈가 없을 때만
그 goal을 완료로 본다.

1. 해당 goal에서 새로 만든 public contract, runtime 구현, 테스트, 샘플을 함께 읽고
   위험 신호를 찾는다.
2. 위험 신호마다 어떤 POSD 원칙에 위배되는지 적는다.
3. 수정 방향을 두 가지 이상 비교한다.
4. 더 나은 방향을 선택한 이유를 설명한다.
5. 선택한 방향으로 리팩토링한다.
6. 리팩토링 뒤 같은 위험 신호가 남았는지 다시 확인한다.
7. 추가로 드러난 리팩토링 이슈가 있으면 같은 순서를 다시 반복한다.
8. 검증 명령을 다시 실행한다.
9. 잔여 POSD 위험 신호와 리팩토링 이슈가 0개임을 기록한다.

다음 항목은 리팩토링 이슈로 본다.

- public facade가 단순 전달만 하고 호출자에게 runtime 세부를 노출한다.
- 같은 routing, channel, stream, actor, lifecycle 규칙이 두 곳 이상에 반복된다.
- 실행 순서만 기준으로 모듈을 나누어 호출자가 내부 순서를 알아야 한다.
- 특수 사례와 범용 runtime 경로가 한 파일이나 타입에 섞여 있다.
- 주석이 코드 구조의 부족함을 설명하거나, 코드와 같은 내용을 반복한다.
- 테스트나 샘플이 public API 대신 runtime 구현 세부에 의존한다.

위험 신호나 리팩토링 이슈가 남으면 같은 goal 안에서 다시 리팩토링한다. 의도된
tradeoff라고 판단하더라도, public 복잡성이 늘지 않는 이유와 어느 후속 goal에서
사라지는지를 적어야 한다. 이유와 후속 goal을 명확히 적을 수 없으면 완료로 보지 않는다.

다음 goal로 넘어가기 위한 POSD 완료 조건은 아래 세 가지다.

- 해당 goal의 POSD 리팩토링 기록에 발견 항목, 선택한 수정 방향, 재검토 결과가 남아 있다.
- 재검토 결과에 잔여 POSD 위험 신호와 리팩토링 이슈가 0개라고 적혀 있다.
- 리팩토링 뒤 해당 goal의 검증 명령과 `git diff --check`가 통과한다.

## 5. 22개 Goal

### Goal 1. Repository Skeleton And Tooling

목표는 framework, connector, HTTP client, Unreal connector, samples, tests가 독립 산출물로
빌드되는 기본 구조를 만드는 것이다.

구현 항목:

- `framework/languages/cpp/framework`
- `framework/include/zlink/framework/contracts/*`
- `framework/src/runtime/*`
- `zlink::framework` CMake target
- `framework/languages/cpp/connector`
- `zlink::stream_connector` CMake target
- `framework/languages/cpp/http-client`
- `zlink::http_client` CMake target
- `framework/languages/cpp/unreal-connector`
- `framework/languages/cpp/samples/Bingo`
- `framework/languages/cpp/samples/TicTacToe`
- `framework/languages/cpp/tests`
- CMake presets, vcpkg manifest, CLion/Visual Studio 설정
- CTest labels

완료 기준:

- framework, connector, HTTP client, Unreal connector가 물리적으로 분리된다.
- public contract header와 runtime 구현 디렉토리가 분리된다.
- installed public include에 runtime 구현 header가 들어가지 않는다.
- CLion과 Visual Studio에서 CMake configure가 가능하다.
- WSL 환경에서도 같은 CMake preset을 사용할 수 있다.

검증:

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L framework-contract
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_tooling_contract
git diff --check -- framework/languages/cpp
```

### Goal 2. Binding Codec Surface Alignment

목표는 기존 C++ binding codec 표면을 framework와 connector가 사용할 수 있는
`message_t` 중심 API로 정렬하는 것이다.

구현 항목:

- base binding target에서 JSON, MessagePack, Protobuf dependency 제거 확인
- 선택 codec target: JSON, MessagePack, Protobuf
- `message_t::from_json(value)`
- `message.parse_json<T>()`
- `message_t::from_messagepack(value)`
- `message.parse_messagepack<T>()`
- `message_t::from_protobuf(value)`
- `message.parse_protobuf<T>()`
- 신규 샘플은 message 중심 API만 사용

완료 기준:

- base binding만 링크하면 codec 외부 dependency가 필요 없다.
- 선택 codec target을 링크한 경우에만 해당 dependency를 요구한다.
- framework와 connector 샘플 application code가 직접 JSON parser로 field를 꺼내지 않는다.

검증:

```bash
cmake --build bindings/cpp/build
ctest --test-dir bindings/cpp/build -R codec
git diff --check -- bindings/cpp framework/languages/cpp
```

### Goal 3. Core Async, Task, Error Model

목표는 public async 표면, error kind, result, exception, call object, `task_t<T>`를 닫는
것이다.

구현 항목:

- `framework_error_kind_t`
- `framework_exception_t`
- `result_t<T>`
- `task_t<T>`
- internal `coroutine_executor_t`
- `send_call_t`
- `request_call_t<T>`
- `stream_write_call_t`
- `relay_call_t`
- `submit(callback)`
- `co_await submit()`
- timeout, shutdown, disconnected, queue full, decode failure, handler not found mapping

완료 기준:

- callback submit과 coroutine submit이 같은 error kind를 반환한다.
- public async 표면에 `std::future`가 없다.
- public async 표면에 `boost::asio::awaitable`이 없다.
- `task_t<T>`는 다중 await와 first-complete-wins를 지원한다.
- handler dispatch는 `.result()` blocking bridge 없이 coroutine executor를 통과한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-contract
ctest --test-dir framework/languages/cpp/build -L framework-unit -R async
```

### Goal 4. App Host, Configuration, Logging

목표는 standalone application host, configuration, logging, graceful shutdown을 구현하는
것이다.

구현 항목:

- `app_t::create()`
- `app_t::config()`
- `app_t::logging()`
- `app_t::run(argc, argv)`
- `app_t::stop()`
- signal handling
- startup validation
- JSON config loader
- environment variable loader
- CLI args parser
- typed options binding
- `logger_t<TCategory>`
- `logger_factory_t`
- console/file/rotating file/callback sink
- async logging option

완료 기준:

- 사용자는 native context 생성/종료 순서를 알 필요가 없다.
- configuration은 `.NET Core` Generic Host model처럼 JSON, env, CLI source를 merge한다.
- `run()`은 process exit code를 반환한다.
- `spdlog`와 `{fmt}` 타입은 public header에 노출하지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R host
ctest --test-dir framework/languages/cpp/build -L framework-config
```

### Goal 5. DI Container And Scope Lifetime

목표는 `.NET` framework의 scope 개념을 C++ 자체 DI와 RAII lifetime으로 구현하는 것이다.

구현 항목:

- `service_collection_t`
- `service_provider_t`
- `service_scope_t`
- singleton, scoped, transient
- constructor injection
- factory registration
- duplicate registration validation
- handler invocation scope
- stream session scope
- spot activation scope
- actor creation scope

완료 기준:

- scoped service는 자기 scope 밖으로 재사용되지 않는다.
- shutdown 중 resolve는 실패한다.
- handler 생성자 주입을 위해 sample handler가 service를 직접 생성하지 않는다.
- 외부 DI 타입은 public header에 드러나지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R DI
ctest --test-dir framework/languages/cpp/build -L framework-regression -R scope
```

### Goal 6. Application Framework Parity Model

목표는 C++ framework의 제품 포지션을 `.NET Core`/`ASP.NET Core` 같은 application
framework로 고정하는 것이다.

구현 항목:

- `.NET Core` / `ASP.NET Core` benchmark mapping
- app host, DI, configuration, logging, lifecycle 통합
- HTTP hosting, zlink messaging, timer, hosted service 통합
- handler, middleware/filter, validation, error mapping 공통 모델
- security/auth extension point
- scheduling/background work model
- developer convenience model
- regression label taxonomy

완료 기준:

- C++ framework는 `.NET` framework와 동일한 구조를 가진다.
- C++ framework는 `.NET` framework와 동일한 기능을 제공한다.
- C++ framework는 `.NET` framework와 동일 수준의 사용성을 제공한다.
- HTTP, zlink, timer, hosted service가 서로 다른 framework처럼 보이지 않는다.
- 모든 handler 계열은 DTO, `dependency_types`, DI scope, `task_t<T>`, logging, error
  mapping 규칙을 공유한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-contract
ctest --test-dir framework/languages/cpp/build -L framework-host
ctest --test-dir framework/languages/cpp/build -L framework-regression -R parity
```

### Goal 7. Runtime Integration And Execution

목표는 zlink C++ binding과 CAPI dispatch callback을 framework runtime 내부로 숨기고,
typed handler만 노출하는 것이다.

구현 항목:

- `zlink::context_t` lifecycle owner
- channel/stream/discovery/registry/spot lifecycle owner
- CAPI dispatch callback 등록과 recv 처리
- typed handler projection
- CAPI timer event projection
- core ordering 보존
- framework offload executor
- runtime drain
- transport abstraction
- endpoint URI validation
- TCP, IPC, TLS, WebSocket transport support through zlink core

완료 기준:

- public API가 CAPI handle, socket recv, poller slot을 노출하지 않는다.
- 모든 handler는 coroutine executor를 통과한다.
- CPU-bound handler는 offload executor에서 실행 가능하다.
- offload executor는 shutdown에서 drain된다.
- framework core는 zlink core transport 의미를 감싸며 별도 event loop를 public API로
  만들지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R runtime
ctest --test-dir framework/languages/cpp/build -L framework-integration
```

### Goal 8. Handler Registry And Serializer

목표는 typed payload와 handler owner를 framework가 관리하고, 사용자는 handler type만
등록하게 만드는 것이다.

구현 항목:

- `handler_registry_t`
- `options.handlers().add<THandler>(group)`
- handler alias: `request_type`, `reply_type`, `spot_type`, `actor_type`
- handler `topic_name`
- typed deserialize/serialize
- handler DI resolve
- handler exception mapping
- `serializer_registry_t`
- JSON serializer 기본값
- custom serializer 등록

완료 기준:

- 등록부가 channel, topic, spot, actor, request, reply type을 반복해서 나열하지 않는다.
- type 정보는 handler class alias와 `topic_name`에서 읽는다.
- decode 실패는 `payload_decode_failed`로 보고하고 runtime을 죽이지 않는다.
- serializer registry 구현은 public header에 노출하지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R handler
ctest --test-dir framework/languages/cpp/build -L framework-unit -R serializer
```

### Goal 9. Channel Messaging

목표는 request/reply, send/event, pub/sub, route channel, outbound client를 `.NET`
framework와 같은 사용성으로 제공하는 것이다.

구현 항목:

- `client_server_channel(...)`
- server/client/publisher/subscriber capability
- `bind(...)`
- `connect(...)`
- `use_discovery(...)`
- `request_client_t`
- `publisher_t`
- request timeout
- reply correlation
- outbound-only host
- route channel
- handler not found mapping
- disconnected result

완료 기준:

- channel messaging 기본 호출은 channel name 기준이다.
- 같은 capability 안에서 discovery와 manual connection을 섞지 않는다.
- pending queue 한도 초과는 `request_rejected`로 실패한다.
- route handler가 없으면 request는 handler not found 계열 error로 닫힌다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-zlink-channel
ctest --test-dir framework/languages/cpp/build -L framework-regression -R channel
```

### Goal 10. Backpressure And Reliability

목표는 send readiness, bounded queue, timeout, retry/dead-letter extension point를
framework 내부에서 관리하는 것이다.

구현 항목:

- nonblocking send
- send-ready runtime integration
- HWM awareness
- bounded pending queue
- timeout 처리
- disconnected 처리
- shutdown 처리
- explicit retry hook
- dead-letter extension point
- idempotency key hook
- graceful close와 drain

완료 기준:

- public non-blocking 옵션으로 책임을 사용자에게 넘기지 않는다.
- timeout 전 send-ready가 오면 pending 작업을 drain한다.
- shutdown 중 pending 작업은 graceful drain 또는 `shutdown` 실패로 닫힌다.
- slow subscriber나 disconnected subscriber가 publisher와 다른 subscriber를 같이
  실패시키지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R backpressure
ctest --test-dir framework/languages/cpp/build -L framework-regression -R reliability
```

### Goal 11. SPOT Runtime

목표는 SPOT node, Entry Spot, user Spot, actor handler를 framework 표면으로 제공하는
것이다.

구현 항목:

- `spot_node_builder_t`
- `spot_context_t`
- `spot_context_t::publish(...)`
- `spot_context_t::request_to(...)`
- `spot_context_t::handlers()`
- `spot_handler_registry_t`
- `add_handler<THandler>()`
- `add_subscribe<THandler>(topic)`
- `add_actor_join<THandler>()`
- `add_actor_packet<THandler>()`
- `add_post_actor_joined<THandler>()`
- `add_actor_left<THandler>()`
- `add_actor_disconnected<THandler>()`
- Entry Spot
- user Spot lifecycle
- Registry-backed Spot lookup

완료 기준:

- SPOT node lifecycle은 app host가 관리한다.
- core SPOT dispatch ordering이 typed handler 표면에 유지된다.
- Spot 등록부에서는 handler type만 나열한다.
- actor join/packet handler는 Spot instance와 actor를 함께 받는다.
- Play sample smoke는 handler 객체 직접 호출만으로 통과하지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-zlink-spot
ctest --test-dir framework/languages/cpp/build -L framework-regression -R spot
```

### Goal 12. SPOT Timer

목표는 CAPI timer를 기반으로 `.NET` framework timer와 같은 기능성을 C++ framework에서
제공하는 것이다.

구현 항목:

- `timer_t`
- `timer_options_t`
- `timer_overrun_policy_t`
- `timer_tick_t`
- `spot_context_t::add_timer<THandler>(...)`
- CAPI timer lifecycle
- CAPI timer dispatch event projection
- `fire_count` 기반 skipped tick 계산
- `scheduled_index`
- `skip_late_ticks`
- `catch_up_bounded`
- `delay_next_tick`
- same timer instance 재진입 금지
- timer handler exception monitoring

완료 기준:

- 사용자는 native timer handle, poller slot, timer recv 순서를 직접 다루지 않는다.
- user Spot timer는 같은 Spot의 packet/subscription/channel reply 순서 정책을 따른다.
- Entry Spot timer는 Entry Spot 전체를 전역 직렬화하지 않는다.
- timer failure event는 snapshot interval을 기다리지 않고 발생한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-zlink-spot -R timer
ctest --test-dir framework/languages/cpp/build -L framework-regression -R timer
```

### Goal 13. STREAM Framework

목표는 framework Header 기반 packet stream과 session handler를 구현하는 것이다.

구현 항목:

- `stream_builder_t`
- `stream_t`
- `packet_stream_session_t`
- `stream_header_t`
- `stream_error_t`
- STREAM bind
- packet session registration
- lifecycle callback
- packet callback
- packet reply
- `stream_t::write_packet(...)`
- header encode/decode
- semantic validation
- write backpressure
- session ordering
- close cleanup

완료 기준:

- raw byte stream dispatch는 core public 표면에 넣지 않는다.
- Header validation 실패 packet은 application handler로 넘기지 않는다.
- 같은 stream session lifecycle callback과 packet callback은 직렬이다.
- pending write 중 disconnect가 발생하면 caller는 disconnected 계열 error를 받는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-zlink-stream
ctest --test-dir framework/languages/cpp/build -L framework-regression -R stream
```

### Goal 14. ActorGateway Session Relay

목표는 STREAM session과 actor를 ActorGateway로 bind/relay하는 server-side runtime 기능을
구현하는 것이다.

구현 항목:

- `stream.attach_actor_gateway(spot_node_name)`
- `actor_ref_t`
- `session_actor_manager_t`
- `session_actor_t`
- `actor_context_t`
- `bound_session_t`
- `actor_join_result_t<TReply>`
- actor factory 등록
- local actor handle bind
- remote actor ref bind
- session actor relay
- bound session push
- actor disconnect cleanup
- actor type mismatch error
- duplicate actor error
- remote ActorGateway locator codec 숨김

완료 기준:

- STREAM session에서 actor로 보내는 packet은 application route mesh channel을 만들지 않는다.
- `actor_ref_t`의 node rid, actor id, generation round-trip이 유지된다.
- actor push는 `bound_session_t`를 통해 내려간다.
- actor-session binding은 Registry나 sample-only metadata store에 저장하지 않는다.
- `relay(...)`와 `send(...)`는 caller payload를 소비하지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-zlink-actor-gateway
ctest --test-dir framework/languages/cpp/build -L framework-regression -R actor
```

### Goal 15. Registry And Topology

목표는 embedded registry, remote query, topology 조회, Spot remote address lookup을
framework runtime에 통합하는 것이다.

구현 항목:

- embedded registry bootstrap
- registry query client
- topology query
- service summary
- Spot remote address lookup 기본값
- custom Spot resolver
- duplicate resolver rejection
- ambiguous route channel validation
- stale address cleanup
- monitoring snapshot source

완료 기준:

- Registry는 Spot remote address 조회 기본값으로 사용한다.
- session actor relay hot path의 actor route store로 Registry를 쓰지 않는다.
- Spot discovery 없이 Registry Spot 기본값을 켜면 validation 오류다.
- route channel이 둘 이상이면 resolver channel 이름을 명시해야 한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-zlink-registry
ctest --test-dir framework/languages/cpp/build -L framework-regression -R registry
```

### Goal 16. Monitoring, Health, Observability

목표는 runtime event를 typed event와 운영 표면으로 올리고 health/readiness/liveness를
제공하는 것이다.

구현 항목:

- monitoring builder
- runtime event enum
- typed event payload structs
- socket/discovery/registry/spot/stream/actor/session event projection
- timer immediate failure event
- correlation id
- health status
- readiness/liveness
- metrics/tracing hook
- logging integration

완료 기준:

- handler exception과 transport error가 구분된다.
- timer handler failure는 snapshot diff interval을 기다리지 않는다.
- health는 HTTP만이 아니라 zlink channel, registry, STREAM endpoint, hosted service 상태를
  함께 반영한다.
- public callback payload에 exception 객체 자체를 직접 싣지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-observability
ctest --test-dir framework/languages/cpp/build -L framework-unit -R monitoring
```

### Goal 17. Module System And Hosted Services

목표는 application 기능을 module과 hosted service로 구성하고 lifecycle을 host와 연동하는
것이다.

구현 항목:

- `module_t`
- `framework_module_contract_t`
- `hosted_service_t`
- `app_t::add_zlink_framework(...)`
- `zlink_framework_options_t`
- `options.services()`
- `options.handlers()`
- `options.codecs().add_json()`
- discovery/channel/spot/stream fluent options builders
- module service registration
- module handler registration
- stage wrapper module pattern
- hosted service start/stop lifecycle

완료 기준:

- `.NET`의 `AddZLinkFramework(options => ...)`에 해당하는 C++ 고수준 진입점은
  `app_t::add_zlink_framework(options_callback)`다.
- `options.codecs().add_json()`은 JSON codec 사용만 선언하고 message type을 모두 나열하지
  않는다.
- handler 생성자 의존성은 `dependency_list_t<Dep...>`와 DI 생성자 주입으로 처리한다.
- sample `main.cpp`와 role `*HostFactory`에는 handler용 DI factory, serializer smoke 검증,
  낮은 수준 zlink builder 람다를 두지 않는다.
- hosted service는 app startup/shutdown과 함께 시작하고 종료한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R module
ctest --test-dir framework/languages/cpp/build -L framework-integration -R hosted
```

### Goal 18. ZLink HTTP Client

목표는 C++ 샘플과 테스트가 사용할 HTTP/JSON client를 Stream Connector와 같은 독립
산출물로 구현하는 것이다. 이 client는 framework HTTP hosting을 검증하는 소비자이며,
framework core target의 기본 의존성이 아니다.

구현 항목:

- `zlink::http_client` namespace
- `zlink/http_client.hpp`
- `http-client/include/zlink/http_client/contracts/*`
- `http-client/src/runtime/*`
- `zlink::http_client` CMake target
- `client_t::create()`
- `base_url(endpoint)`
- `timeout(duration)`
- `default_header(name, value)`
- `json()`
- `get(path)`, `post(path)`, `put(path)`, `delete_(path)`
- typed request body: `body(dto)`
- callback submit
- coroutine submit
- typed JSON response: `submit<TReply>()`
- HTTP status와 transport error를 client result/error kind로 매핑
- HTTP client regression test suite
- `Boost.Beast` runtime private 구현
- HTTP와 HTTPS endpoint
- TLS verification option
- certificate authority bundle 또는 test certificate trust option

완료 기준:

- HTTP client는 framework sample 전용 helper나 framework target이 아니다.
- 샘플 application code는 `Boost.Beast`, `Boost.Asio`, OpenSSL, socket, resolver,
  request/response parser 타입을 직접 보지 않는다.
- `base_url(...)`은 `http://`와 `https://` endpoint를 모두 받는다.
- `https://` request는 TLS handshake, server certificate verification, hostname verification을
  수행한다.
- test certificate나 local development certificate를 trust하는 설정은 HTTP client option으로
  명시해야 하며, 묵시적으로 TLS verification을 끄지 않는다.
- public 호출은 zlink 스타일 call object를 만들고 마지막에 `submit(callback)` 또는
  `co_await submit<T>()`으로 실행한다.
- JSON 변환은 `message_t` 또는 DTO serializer hook을 통해 처리하고, application sample code가
  `nlohmann::json::parse`로 field를 직접 꺼내지 않는다.
- retry, redirect, cookie, proxy, multipart, streaming download는 초기 core 범위에 넣지
  않고 후속 extension point로 남긴다.
- TicTacToe client sample은 이 HTTP client로 `POST /games`를 호출한다.
- HTTP client 회귀 테스트는 HTTP와 HTTPS 모두에서 typed JSON request/response, timeout,
  status mapping, TLS verification 실패, test certificate trust 성공을 고정한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L http-client-contract
ctest --test-dir framework/languages/cpp/build -L http-client-unit
ctest --test-dir framework/languages/cpp/build -L http-client-e2e
ctest --test-dir framework/languages/cpp/build -L http-client-https
ctest --test-dir framework/languages/cpp/build -L http-client-regression
```

### Goal 19. HTTP Hosting

목표는 `ASP.NET Core Minimal API`의 `MapGet`, `MapPost`, `MapPut`, `MapDelete`에 대응하는
HTTP hosting을 core framework 기능으로 구현하는 것이다.

구현 항목:

- `contracts/http/*`
- `zlink/framework/http.hpp`
- `http_options_builder_t`
- `options.http().listen(endpoint)`
- `options.http().tls(...)`
- `map_get<THandler>(path)`
- `map_post<THandler>(path)`
- `map_put<THandler>(path)`
- `map_delete<THandler>(path)`
- `use<TMiddleware>()`
- JSON body binding
- route parameter binding
- query string binding
- status/error mapping
- middleware/filter pipeline
- correlation id propagation
- HTTP handler e2e test through `zlink::http_client`
- HTTP hosted service
- `Boost.Beast` runtime private 구현
- HTTPS endpoint
- TLS certificate/private key option
- OpenSSL/SSL context runtime private 구현

완료 기준:

- HTTP handler는 message handler와 같은 `request_type`, `reply_type`,
  `dependency_types`, `handle(...)` 규칙을 사용한다.
- `http://`와 `https://` endpoint를 모두 지원한다.
- HTTPS endpoint는 TLS certificate/private key 설정을 요구하고, 누락되면 startup validation이
  실패한다.
- `Boost.Beast`, `Boost.Asio`, OpenSSL/SSL context, TCP socket, acceptor, HTTP parser 타입은
  public header에 나타나지 않는다.
- MVC controller, template rendering, Razor page, WebSocket transport는 포함하지 않는다.
- exception, logging, validation, auth, correlation id 처리는 middleware/filter extension
  point로 둔다.
- HTTP handler e2e 테스트는 외부 HTTP 도구나 sample-local client가 아니라
  `zlink::http_client`로 `GET`, `POST`, `PUT`, `DELETE` route를 호출한다.
- TicTacToe sample은 HTTP `POST /games`로 시작한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-http
ctest --test-dir framework/languages/cpp/build -L framework-http-e2e
ctest --test-dir framework/languages/cpp/build -L framework-integration -R http
```

### Goal 20. Stream Connectors

목표는 C++ Stream Connector와 Unreal Stream Connector를 framework와 별도 배포물로
구현하는 것이다.

구현 항목:

- `zlink::stream_connector` namespace
- `zlink/stream_connector.hpp`
- Asio 기반 일반 C++ connector transport
- TCP transport
- TLS over TCP transport
- WebSocket binary transport
- WebSocket over TLS binary transport
- unsupported transport와 endpoint scheme mismatch validation
- typed send/request/on
- callback submit
- coroutine submit
- reconnect, heartbeat, pending request correlation
- JSON 기본 ON
- MessagePack/Protobuf 선택
- LZ4 build feature 기본 ON, packet 압축은 opt-in
- Unreal plugin/module packaging
- Unreal `Public/` / `Private` 분리
- Unreal `Sockets`/`Networking` 기반 transport
- Blueprint callable connect/close/send/request
- Game Thread callback dispatch
- Unreal Automation Test

완료 기준:

- connector는 framework sample이나 framework target이 아니다.
- 일반 C++ connector는 Asio를 내부 구현으로 사용하며, TCP/TLS/WebSocket/WebSocket over TLS가
  같은 packet API로 동작한다.
- Unreal connector는 일반 C++ connector wrapper가 아니라 Unreal network library 기반으로
  구현한다.
- Unreal public API에는 coroutine 표면을 두지 않는다.
- connector public header는 receive loop, transport connection, pending request table,
  frame sender 구현 타입을 노출하지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L connector-unit
ctest --test-dir framework/languages/cpp/build -L connector-integration
ctest --test-dir framework/languages/cpp/build -L connector-e2e
ctest --test-dir framework/languages/cpp/build -L connector-contract
ctest --test-dir framework/languages/cpp/build -L connector-protocol
ctest --test-dir framework/languages/cpp/build -L connector-transport
ctest --test-dir framework/languages/cpp/build -L connector-typed
ctest --test-dir framework/languages/cpp/build -L unreal-connector-contract
ctest --test-dir framework/languages/cpp/build -L unreal-connector-compile
ctest --test-dir framework/languages/cpp/build -L unreal-connector-smoke
```

### Goal 21. Review Samples

목표는 framework 전반 동작을 사용자가 리뷰하기 쉬운 `Bingo`, `TicTacToe` 샘플로 고정하는
것이다.

구현 항목:

- `.NET` 샘플과 같은 `Shared`, `Client`, `Server/Registry`, `Server/Api`,
  `Server/Play`, `Server/Session` 역할 분리
- `.NET` 샘플과 같은 packet 이름과 message contract 흐름
- Bingo: channel request/reply, session packet dispatch, pub/sub, SPOT, timer, monitoring
- TicTacToe: HTTP client `POST /games`, Play channel request, STREAM connector, ActorGateway
- server-side file logging
- client e2e
- sample log assertion

완료 기준:

- 샘플 이름에 별도 접미사를 붙이지 않는다.
- `main.cpp`가 smoke 검증과 낮은 수준 설정으로 난잡해지지 않는다.
- `Client` 샘플은 서버 handler를 직접 호출하지 않고 HTTP client와 connector를 사용한다.
- sample DTO는 serializer hook만 제공하고 application code가 직접 JSON field를 읽지 않는다.
- Bingo와 TicTacToe client executable은 실제 server process와 붙어 request/reply와 push를
  검증한다.
- CTest는 client 성공뿐 아니라 server file log의 request, reply, push, disconnect,
  shutdown 이벤트를 확인한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke
ctest --test-dir framework/languages/cpp/build -L framework-sample-parity
ctest --test-dir framework/languages/cpp/build -L framework-sample-e2e
ctest --test-dir framework/languages/cpp/build -L framework-sample-log
```

### Goal 22. Final Regression, Package, Extension Boundary

목표는 모든 draft 항목이 구현됐는지 최종 확인하고, `.NET` framework와 같은 구조, 기능,
사용성을 회귀 테스트로 고정하는 것이다.

구현 항목:

- full CTest regression
- C++ framework runtime line coverage 70% 이상
- zlink `.NET` parity e2e regression
- public header compile contract
- install/package consumer test
- CLion/Visual Studio configure smoke
- public docs and sample code alignment
- extension target naming
- extension dependency isolation
- metrics/tracing extension point
- Kafka/gRPC bridge boundary
- advanced retry/dead-letter boundary
- FlatBuffers/YAML/custom codec extension boundary

완료 기준:

- C++ framework는 `.NET` framework와 동일한 구조를 가진다.
- C++ framework는 `.NET` framework와 동일한 기능을 제공한다.
- C++ framework는 `.NET` framework와 동일 수준의 사용성을 제공한다.
- Goal 1부터 Goal 22까지 완료 기준이 충족된다.
- CTest label 전체가 통과한다.
- coverage build의 runtime line coverage가 70% 이상이다.
- Goal 1부터 Goal 22까지 각 goal에서 POSD 기반 리팩토링을 최소 한 번씩 수행했다.
- POSD 리팩토링 기록이 22개 이상 남아 있다.
- framework, connector, HTTP client, Unreal connector public header가 runtime implementation
  header를 include하지 않는다.
- public header에 GoogleTest, GoogleMock, spdlog, fmt, Boost.Asio, Boost.Beast, codec 외부
  타입이 불필요하게 노출되지 않는다.
- core framework target은 Kafka, gRPC, YAML, FlatBuffers dependency를 기본으로 끌고 오지
  않는다.
- zlink 회귀 테스트는 request/reply, send/event, pub/sub, route channel, SPOT, STREAM,
  ActorGateway, Registry, timer 동작을 `.NET` framework와 같은 의미로 고정한다.
- sample e2e는 client 성공만 보지 않고 server file log와 monitoring event를 확인한다.

검증:

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-zlink --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-zlink-channel --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-zlink-spot --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-zlink-stream --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-zlink-actor-gateway --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-zlink-registry --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-http --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-http-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-config --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-observability --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-regression --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-package --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-extension --output-on-failure
cmake -S framework/languages/cpp -B framework/languages/cpp/build-coverage -DZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE=ON -DZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD=70
cmake --build framework/languages/cpp/build-coverage
ctest --test-dir framework/languages/cpp/build-coverage --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## 6. Draft 추적표

| Draft 문서 | 구현 goal |
|------------|-----------|
| [cpp-framework-implementation-plan.ko.md](./cpp-framework-implementation-plan.ko.md) | Goal 1-22 실행 계획 |
| [README.ko.md](./README.ko.md) | Goal 1-22 |
| [cpp-framework-policy.ko.md](./cpp-framework-policy.ko.md) | Goal 1-22 |
| [cpp-application-framework.ko.md](./cpp-application-framework.ko.md) | Goal 6, Goal 19, Goal 21, Goal 22 |
| [cpp-framework-interfaces.ko.md](./cpp-framework-interfaces.ko.md) | Goal 1-19, Goal 22 |
| [handler-interfaces.ko.md](./handler-interfaces.ko.md) | Goal 8-14 |
| [cpp-channel-messaging.ko.md](./cpp-channel-messaging.ko.md) | Goal 9, Goal 10 |
| [channel-messaging-samples.ko.md](./channel-messaging-samples.ko.md) | Goal 9, Goal 21 |
| [cpp-spot.ko.md](./cpp-spot.ko.md) | Goal 11, Goal 12, Goal 14 |
| [spot-samples.ko.md](./spot-samples.ko.md) | Goal 11, Goal 12, Goal 14, Goal 21 |
| [stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md) | Goal 11, Goal 12, Goal 17 |
| [cpp-stream.ko.md](./cpp-stream.ko.md) | Goal 13, Goal 14 |
| [stream-open-items.ko.md](./stream-open-items.ko.md) | Goal 13 |
| [stream-samples.ko.md](./stream-samples.ko.md) | Goal 13, Goal 14, Goal 21 |
| [actor-gateway-session-relay.ko.md](./actor-gateway-session-relay.ko.md) | Goal 14, Goal 21 |
| [cpp-registry.ko.md](./cpp-registry.ko.md) | Goal 11, Goal 14, Goal 15 |
| [cpp-monitoring.ko.md](./cpp-monitoring.ko.md) | Goal 12, Goal 16 |
| [cpp-http-client.ko.md](./cpp-http-client.ko.md) | Goal 18, Goal 19, Goal 21 |
| [cpp-http-hosting.ko.md](./cpp-http-hosting.ko.md) | Goal 19, Goal 21 |
| [cpp-stream-connector.ko.md](./cpp-stream-connector.ko.md) | Goal 20 |
| [cpp-framework-posd-refactoring-log.ko.md](./cpp-framework-posd-refactoring-log.ko.md) | Goal 1-22 POSD 기록 |

## 7. 기능 축 추적표

| 기능 축 | 구현 goal | 회귀 테스트 축 |
|---------|-----------|----------------|
| C++20 baseline | Goal 1, Goal 3 | `framework-contract` |
| `.NET Core` benchmark model | Goal 6, Goal 22 | `framework-regression` |
| app/host lifecycle | Goal 4 | `framework-host` |
| DI scope | Goal 5 | `framework-unit`, `framework-regression` |
| configuration | Goal 4, Goal 6 | `framework-config` |
| async/coroutine submit | Goal 3, Goal 7, Goal 9, Goal 13, Goal 18, Goal 20 | `framework-unit`, `framework-regression`, `http-client-*` |
| channel request/reply | Goal 9 | `framework-zlink-channel` |
| send/event/pub-sub | Goal 9 | `framework-zlink-channel` |
| route channel | Goal 9, Goal 15 | `framework-zlink-channel`, `framework-zlink-registry` |
| backpressure/reliability | Goal 10 | `framework-regression` |
| SPOT lifecycle | Goal 11 | `framework-zlink-spot` |
| SPOT timer | Goal 12 | `framework-zlink-spot`, `timer` |
| STREAM packet | Goal 13 | `framework-zlink-stream` |
| ActorGateway relay | Goal 14 | `framework-zlink-actor-gateway` |
| Registry/discovery | Goal 15 | `framework-zlink-registry` |
| monitoring/health/logging | Goal 4, Goal 16 | `framework-observability` |
| module/hosted service | Goal 17 | `framework-integration` |
| ZLink HTTP client | Goal 18 | `http-client-*` |
| HTTP hosting | Goal 19 | `framework-http`, `framework-http-e2e` |
| C++/Unreal connector | Goal 20 | `connector-*`, `unreal-connector-*` |
| samples | Goal 21 | `framework-sample-*` |
| package/extensions/final audit | Goal 22 | `framework-package`, `framework-extension` |
| POSD 리팩토링 | Goal 1-22 | POSD 기록, `framework-regression` |

wildcard label은 아래 concrete label 묶음을 뜻한다. CTest 명령을 직접 실행할 때는 필요한
concrete label을 선택한다.

| wildcard label | concrete label |
|----------------|----------------|
| `http-client-*` | `http-client-contract`, `http-client-unit`, `http-client-e2e`, `http-client-https`, `http-client-regression` |
| `connector-*` | `connector-unit`, `connector-integration`, `connector-e2e`, `connector-contract`, `connector-protocol`, `connector-transport`, `connector-typed` |
| `unreal-connector-*` | `unreal-connector-contract`, `unreal-connector-compile`, `unreal-connector-smoke` |
| `framework-sample-*` | `framework-sample-smoke`, `framework-sample-parity`, `framework-sample-e2e`, `framework-sample-log` |

## 8. Goal 실행용 문구

goal을 만들 때는 아래 문구를 objective로 사용할 수 있다.

```text
framework/languages/cpp/doc/draft/cpp-framework-implementation-plan.ko.md의 Goal N을
완료 기준까지 구현하고, POSD 기반 리팩토링을 이슈가 없을 때까지 수행한 뒤 해당
검증 명령을 실행하고 누락 항목을 보고한다.
```

여러 goal을 한 번에 묶어 실행할 때도 중간 goal의 완료 기준을 건너뛰지 않는다.
draft 문서와 실제 코드가 충돌하면 먼저 충돌 항목을 적고, 문서 또는 구현 중 하나를
명시적으로 정리한 뒤 진행한다.
