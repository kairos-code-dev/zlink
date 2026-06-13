<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework For C++](../README.ko.md) | [다음: Draft -- ZLink Framework C++ Policy](./cpp-framework-policy.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](../README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Application Framework](../spec/cpp-application-framework.ko.md) | [Framework 인터페이스](../spec/cpp-framework-interfaces.ko.md) | [HTTP Client](../../http-client/doc/spec/cpp-http-client.ko.md)

# Draft -- ZLink Framework C++ Implementation Plan

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `framework/languages/cpp/doc` 아래 C++ framework
> 초안 전체를 빠짐없이 구현하기 위한 실행 계획이다.

## 1. 목적

이 문서는 C++ framework 구현을 22개 goal로 나누어 진행하기 위한 기준이다.
각 goal은 구현, 검증, POSD 기반 리팩토링, 문서 대조를 끝낸 뒤에만 완료로 본다.

이 문서는 C++ framework 구현의 단일 실행 기준이다. 관련 draft 문서는 배경과 상세 설명으로
참고할 수 있지만, 작업자는 이 문서 하나만 읽고도 구현 범위, public interface, runtime 경계,
테스트, 성능 gate를 판단할 수 있어야 한다. 관련 draft나 코드에서 이 문서에 없는 요구를 발견하면
구현을 시작하기 전에 먼저 이 문서의 해당 goal에 요구사항을 추가한다.

최종 완료 기준은 아래 세 가지다.

- C++ framework는 `.NET` framework와 동일한 구조를 가진다.
- C++ framework는 `.NET` framework와 동일한 기능을 제공한다.
- C++ framework는 `.NET` framework와 동일 수준의 사용성을 제공한다.

주 벤치마크는 `.NET Core`다. host, DI, configuration, logging, lifecycle은
`.NET Generic Host` 계열을 기준으로 삼고, HTTP routing과 route handler 표면은
`ASP.NET Core Minimal API`를 기준으로 삼는다. 구현 순서는 다르더라도 최종 기능 범위는
줄이지 않는다.

### 1.1 단일 문서 실행 원칙

작업자는 이 문서의 goal 순서와 완료 기준을 기준으로 구현한다.

- 관련 draft는 참고 문서다. 완료 여부는 이 문서의 구현 항목, 완료 기준, 검증 명령으로 판단한다.
- 관련 draft에만 있는 요구사항은 완료 기준으로 쓰지 않는다. 필요한 요구라면 먼저 이 문서에
  추가한 뒤 구현한다.
- 이 문서와 관련 draft가 충돌하면 이 문서를 우선한다. 충돌이 구현을 막으면 이 문서를 먼저
  수정해 단일 기준을 복구한다.
- 각 goal의 public interface, runtime owner, 테스트 label, 성능 gate는 이 문서 안에 있어야 한다.
- "범위 밖"으로 남겨 둔 항목은 해당 goal 완료 기준이 아니다. 필요한 기능은 구현 순서만 나눌 수
  있고, 최종 완료 범위에서는 빠지면 안 된다.
- Goal 1부터 Goal 22까지 모든 항목이 구현되고, 회귀 테스트와 POSD 재리뷰가 통과해야 전체 완료다.

## 2. 공통 실행 규칙

모든 goal은 아래 규칙을 따른다.

- C++20 이상만 지원한다.
- public async 표면에는 `std::future`를 쓰지 않는다.
- public 호출은 call object를 만들고 `co_await async()`로 실행한다.
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
- runtime 세부는 `framework/src/runtime/*`, `connector/core/src/runtime/*`,
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
| C++ Stream Connector core | `framework/languages/cpp/connector/core` | `zlink::stream_connector` | client-side STREAM connector core |
| Stream E2E Client | `framework/languages/cpp/connector/e2e-client` | `zlink::stream_e2e_client` | server e2e/smoke/perf scenario client |
| ZLink HTTP Client | `framework/languages/cpp/http-client` | `zlink::http_client` | client-side HTTP/JSON connector |
| Unreal Stream Connector | `framework/languages/cpp/connector/engines/unreal` | Unreal module/plugin | Unreal 전용 client connector |
| framework extensions | `framework/languages/cpp/extensions` | extension targets | bridge, config, codec, observability 확장 |
| framework samples | `framework/languages/cpp/samples` | sample executables | `Bingo`, `TicTacToe` 리뷰 샘플 |
| framework tests | `framework/languages/cpp/tests/Zlink.Framework.*Tests` | CTest labels | `.NET` test project 분류에 맞춘 contract, unit, e2e, package |
| connector tests | `framework/languages/cpp/tests/Systems.Zlink.Stream.Connector.Tests` | CTest labels | connector contract, protocol, transport, typed 흐름 |
| Unreal connector tests | `framework/languages/cpp/tests/Zlink.Unreal.Stream.Connector.Tests` | CTest labels | Unreal public API compile/smoke와 automation source check |

각 산출물은 `.NET` framework의 `Contracts/*`와 `Runtime/*` 분리를 따른다.

| `.NET` 기준 | C++ framework 기준 | C++ connector 기준 | HTTP client 기준 | 공개 여부 |
|-------------|--------------------|---------------------|------------------|-----------|
| `Contracts/*` | `framework/include/zlink/framework/contracts/*` | `connector/core/include/zlink/stream_connector/contracts/*` | `http-client/include/zlink/http_client/contracts/*` | public |
| `Runtime/*` | `framework/src/runtime/*` | `connector/core/src/runtime/*` | `http-client/src/runtime/*` | private implementation |
| `Runtime/Backend/Contracts` | `framework/src/runtime/backend/contracts` | `connector/core/src/runtime/backend/contracts` | 현재 없음 | private backend contract. HTTP client는 현재 별도 backend adapter가 없으므로 placeholder 디렉터리를 만들지 않는다 |
| project facade | `zlink/framework.hpp`, `zlink/framework/*.hpp` | `zlink/stream_connector.hpp` | `zlink/http_client.hpp` | public |
| implementation glue | `framework/src/runtime/*` | `connector/core/src/runtime/*` | `http-client/src/runtime/*` | private |

Unreal Connector는 Unreal 관례에 따라 `Source/ZLinkStreamConnector/Public`과
`Source/ZLinkStreamConnector/Private`를 사용한다. `Public`은 Unreal 전용 contract와
Blueprint/Game Thread 표면만 담고, transport와 codec 구현은 `Private`에 둔다.

## 4. Goal 공통 완료 절차

각 goal은 아래 순서로 진행한다.

1. 이 문서의 해당 goal 구현 항목, 구현 계약, 완료 기준, 검증 명령을 먼저 읽는다.
2. 같은 기능의 `.NET` framework 구조와 sample/test를 확인한다.
3. 관련 draft는 배경 확인이 필요할 때만 참고한다. 필요한 요구가 관련 draft에만 있으면 이 문서에
   먼저 추가한 뒤 구현한다.
4. public contract owner와 runtime implementation owner를 확정한다.
5. public surface gate를 통과한다.
6. public header와 target 구조를 구현한다.
7. internal runtime 구현을 붙인다.
8. 해당 goal의 contract/unit/integration/e2e 테스트를 추가한다.
9. 필요한 샘플 코드를 갱신한다.
10. POSD 기반 리팩토링 게이트를 통과할 때까지 리팩토링을 반복한다.
11. 리팩토링 뒤 같은 검증을 다시 실행한다.
12. `git diff --check`와 해당 CTest label을 실행한다.
13. 남아 있는 POSD 위험 신호와 리팩토링 이슈가 없는지 확인한다.
14. 완료 기준을 문서 항목별로 대조한다.

### 4.1 Goal별 구현 계약 기준

각 goal은 아래 네 가지 계약을 이 문서 기준으로 만족해야 한다. 이 기준을 만족하지 못하면
코드가 빌드되더라도 해당 goal은 완료로 보지 않는다.

- Public contract: 사용자가 include하고 호출하는 namespace, header, type, method 이름이 정해져야 한다.
- Runtime owner: public contract 뒤의 상태, 스레드, native handle, queue, serializer, socket 소유자가
  정해져야 한다.
- Error contract: 실패가 어떤 `framework_error_kind_t`, `http_client_error_kind_t`, 또는 connector
  error kind로 올라오는지 정해져야 한다.
- Regression contract: 최소한 contract, unit, integration/e2e, negative path 테스트 축이 정해져야 한다.

### 4.2 Goal별 최소 구현 계약

아래 목록은 Goal 1부터 Goal 22까지의 최소 구현 계약이다. 각 goal 본문에 더 상세한 항목이 있으면
그 항목이 우선하며, 아래 목록은 빠지면 안 되는 공통 기준이다.

- Goal 1:
  - Public contract: `zlink::framework`, `zlink::http_client`, `zlink::stream_connector`,
    Unreal connector include root와 CMake imported target 이름.
  - Runtime owner: 각 산출물의 `src/runtime`과 public include 경계.
  - Error contract: configure, build, install, package consumer 실패를 서로 다른 검증 실패로 기록한다.
  - Regression contract: target 생성, install include layout, package config, CLion/Visual Studio configure.
- Goal 2:
  - Public contract: `message_t` codec factory/parse method와 선택 codec target 이름.
  - Runtime owner: codec dependency는 선택 target이 소유하고 base binding target은 소유하지 않는다.
  - Error contract: decode 실패는 payload decode 계열 error로 집약한다.
  - Regression contract: base target dependency 검사와 JSON/MessagePack/Protobuf round-trip.
- Goal 3:
  - Public contract: `task_t<T>`, call object, coroutine submit.
  - Runtime owner: completion state, cancellation, timeout timer, executor resumption.
  - Error contract: timeout, shutdown, disconnected, queue full, decode failure, handler not found.
  - Regression contract: coroutine parity, first-complete-wins, no `std::future` public exposure.
- Goal 4:
  - Public contract: `app_t`, config/logging builders, `logger_t<TCategory>`, `logger_factory_t`.
  - Runtime owner: app host가 native context, signal, config source merge, sink fan-out, flush를 소유한다.
  - Error contract: startup validation 실패는 `configuration_invalid` 또는 `startup_failed`로 닫는다.
  - Regression contract: config precedence, graceful shutdown, logger DI injection, sink failure isolation.
- Goal 5:
  - Public contract: `service_collection_t`, `service_provider_t`, `service_scope_t`.
  - Runtime owner: service provider가 singleton/scoped/transient 생성, 파괴 순서, scope cache를 소유한다.
  - Error contract: duplicate registration, missing dependency, shutdown resolve 실패를 구분한다.
  - Regression contract: scope isolation, constructor injection, handler/session/spot/actor scope disposal.
- Goal 6:
  - Public contract: framework 진입점과 handler/middleware/filter 공통 규칙.
  - Runtime owner: host가 HTTP, zlink messaging, timer, hosted service lifecycle을 하나의 실행 모델로 묶는다.
  - Error contract: 모든 handler 계열은 같은 error mapping과 logging/monitoring 규칙을 따른다.
  - Regression contract: `.NET` sample parity, API naming parity, cross-feature lifecycle parity.
- Goal 7:
  - Public contract: runtime 시작/중지 표면과 executor/offload option.
  - Runtime owner: CAPI handle, socket recv, poller slot, dispatch callback, offload executor.
  - Error contract: native error는 framework error kind로 변환하고 native errno를 public precondition으로
    만들지 않는다.
  - Regression contract: ordering, drain, shutdown race, CPU-bound offload, transport validation.
- Goal 8:
  - Public contract: `handler_registry_t`, serializer registry, handler alias conventions.
  - Runtime owner: type metadata cache, DI resolve, serializer lookup, exception mapper.
  - Error contract: decode failure, ambiguous handler, missing serializer, handler exception.
  - Regression contract: handler alias detection, custom serializer, exception mapping, no duplicate type listing.
- Goal 9:
  - Public contract: channel option builder, `request_client_t`, `publisher_t`, typed request/send/publish calls.
  - Runtime owner: channel runtime이 bind/connect/discovery, reply correlation, pending requests, subscription fan-out을
    소유한다.
  - Error contract: timeout, disconnected, handler not found, request rejected, duplicate capability.
  - Regression contract: server-to-client, dealer mesh, router mesh, pub/sub, route channel, outbound-only host.
- Goal 10:
  - Public contract: bounded queue, timeout, retry/dead-letter hook, idempotency hook option.
  - Runtime owner: send readiness, HWM observation, pending queue, graceful drain.
  - Error contract: queue full은 `request_rejected`, shutdown 중 미완료 작업은 shutdown/disconnected 계열.
  - Regression contract: slow subscriber isolation, disconnect during pending send, timeout-before-ready.
- Goal 11:
  - Public contract: `spot_node_builder_t`, `spot_context_t`, `spot_handler_registry_t`, actor handler 등록 API.
  - Runtime owner: app host가 SPOT node lifecycle, Entry Spot, user Spot activation, Registry-backed lookup을
    소유한다.
  - Error contract: missing spot, duplicate actor, stale actor, actor handler exception을 구분한다.
  - Regression contract: spot-to-spot, spot-to-router, router-to-spot, actor join/packet/left/disconnected ordering.
- Goal 12:
  - Public contract: `timer_t`, `timer_options_t`, `timer_tick_t`, timer handler 등록 API.
  - Runtime owner: CAPI timer handle, tick projection, skipped tick 계산, 재진입 방지.
  - Error contract: handler failure, timer create failure, shutdown tick drop을 구분한다.
  - Regression contract: overrun policy, same timer serialization, failure event immediacy, shutdown cleanup.
- Goal 13:
  - Public contract: `stream_builder_t`, `stream_t`, `packet_stream_session_t`, stream handler callbacks.
  - Runtime owner: stream bind, session table, header codec, write queue, session ordering.
  - Error contract: header validation failure, semantic validation failure, disconnected write.
  - Regression contract: session lifecycle, packet reply, invalid header drop, write backpressure, close cleanup.
- Goal 14:
  - Public contract: `actor_ref_t`, `bound_session_t`, actor context/result, ActorGateway attach API.
  - Runtime owner: session actor manager가 actor binding, generation, remote ref, relay routing, cleanup을 소유한다.
  - Error contract: type mismatch, duplicate actor, stale generation, session disconnected.
  - Regression contract: session-to-actor relay, actor push, remote ref round-trip, disconnect cleanup.
- Goal 15:
  - Public contract: registry/topology builder, registry query client, Spot resolver hook.
  - Runtime owner: embedded registry, remote query cache, stale address cleanup, monitoring source.
  - Error contract: duplicate resolver, ambiguous route channel, discovery missing, stale endpoint.
  - Regression contract: embedded registry bootstrap, remote lookup, duplicate rejection, topology summary.
- Goal 16:
  - Public contract: monitoring builder, runtime event enum, health/readiness/liveness 표면.
  - Runtime owner: runtime event bus, metric/tracing hook, health state aggregator, correlation id propagation.
  - Error contract: transport error와 handler exception을 다른 event kind로 올린다.
  - Regression contract: health aggregation, timer immediate failure, event payload shape, metric label stability.
- Goal 17:
  - Public contract: `module_t`, `framework_module_contract_t`, `hosted_service_t`,
    `app_t::add_zlink_framework(...)`.
  - Runtime owner: host가 module registration, option builder state, hosted service start/stop 순서를 소유한다.
  - Error contract: module validation failure, hosted service startup failure, shutdown failure.
  - Regression contract: module ordering, option builder naming, hosted lifecycle, sample factory readability.
- Goal 18:
  - Public contract: `zlink::http_client`, `client_t`, request call builder, typed JSON response.
  - Runtime owner: HTTP client runtime이 resolver, connection, TLS stream, request serializer, response parser를
    소유한다.
  - Error contract: DNS/connect/TLS/status/timeout/decode 실패를 client error kind로 구분한다.
  - Regression contract: HTTP/HTTPS typed JSON, timeout, status mapping, TLS verify fail, test CA trust success.
- Goal 19:
  - Public contract: 이 goal 본문의 `Goal 19 public interface`를 따른다.
  - Runtime owner: embedded server runtime이 acceptor, connection, TLS context, route table, handler executor를
    소유한다.
  - Error contract: startup validation, media type, body/header limit, ambiguous handler, handler exception.
  - Regression contract: 이 goal 본문의 HTTP regression matrix와 perf gate를 따른다.
- Goal 20:
  - Public contract: `zlink::stream_connector` namespace, `connector_t`, connection/request/send call,
    Unreal Blueprint callable 표면.
  - Runtime owner: 일반 C++ connector는 Asio runtime이 transport, reconnect, heartbeat, pending correlation을
    소유하고, Unreal connector는 Unreal `Sockets`/`Networking` runtime이 같은 의미를 소유한다.
  - Error contract: unsupported scheme, endpoint mismatch, connect timeout, heartbeat timeout, pending request
    timeout, codec failure.
  - Regression contract: TCP, TLS, WebSocket, WebSocket over TLS, reconnect, heartbeat, pending request correlation,
    Unreal Game Thread dispatch.
- Goal 21:
  - Public contract: `Bingo`와 `TicTacToe` sample executable, shared DTO/serializer hook, role host factory.
  - Runtime owner: sample host factory가 app/framework/client/connector 설정을 한곳에서 보여 주고,
    sample runtime은 실제 process 간 통신을 사용한다.
  - Error contract: sample smoke 실패는 client exit code와 server log assertion 실패를 구분한다.
  - Regression contract: process e2e, server file log, monitoring event, HTTP client, connector, ActorGateway.
- Goal 22:
  - Public contract: install package, public headers, extension boundary, user-facing docs.
  - Runtime owner: package config가 framework, connector, HTTP client, Unreal connector dependency boundary를
    유지한다.
  - Error contract: missing optional dependency는 configure/install consumer failure로 명확히 드러난다.
  - Regression contract: full CTest, 80% runtime line coverage, install consumer, package boundary, POSD records.

### 4.3 Public Surface Gate

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

### 4.4 POSD 리팩토링 게이트

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
tradeoff라고 판단하더라도, public 복잡성이 늘지 않는 이유와 같은 goal 안에서 해소하지 않아도
되는 근거를 적어야 한다. 근거를 명확히 적을 수 없으면 완료로 보지 않는다.

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
- `framework/languages/cpp/connector/core`
- `framework/languages/cpp/connector/e2e-client`
- `zlink::stream_connector` CMake target
- `framework/languages/cpp/http-client`
- `zlink::http_client` CMake target
- `framework/languages/cpp/connector/engines/unreal`
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
bindings/cpp/tests/run_tests.sh
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
- `co_await async()`
- timeout, shutdown, disconnected, queue full, decode failure, handler not found mapping

완료 기준:

- coroutine submit이 timeout, shutdown, disconnected, queue full, decode failure,
  handler not found error kind를 보존한다.
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
- `app_t::add_zlink_framework(...)`에서 app logging state와 연결된 `logger_factory_t` 기본 등록
- `dependency_types`의 `logger_t<TCategory>` 자동 DI 등록
- console/file/rotating file/callback sink
- async logging option

완료 기준:

- 사용자는 native context 생성/종료 순서를 알 필요가 없다.
- configuration은 `.NET Core` Generic Host model처럼 JSON, env, CLI source를 merge한다.
- `run()`은 process exit code를 반환한다.
- handler는 logger를 쓰기 위해 `options.services().add_singleton<logger_t<...>>()`를 직접
  작성하지 않는다. 출력 대상은 `app.logging()`에서 설정하고, logger 객체는 framework DI가
  `.NET`의 `ILogger<T>`처럼 제공한다.
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

- `add_client_server_channel(...)`
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
- `spot_t`
- `entry_spot_t`
- `spot_context_t`
- `spot_context_t::publish(...)`
- `spot_context_t::request_to(...)`
- `spot_context_t::handlers()`
- `spot_handler_registry_t`
- `add_handler<&TSpot::method>()`
- `add_subscribe<&TSpot::method>(topic)`
- `add_actor_packet<&TSpot::method>()`
- `on_actor_join(actor, message_t)`
- `onJoinActor(actor)`
- `onLeaveActor(actor)`
- `onDisconnectActor(actor)`
- `spot_context_t::close()`
- `spot_create_result_t`
- Entry Spot
- user Spot lifecycle
- Registry-backed Spot lookup

완료 기준:

- SPOT node lifecycle은 app host가 관리한다.
- core SPOT dispatch ordering이 typed handler 표면에 유지된다.
- Spot 등록부에서는 Spot member function만 나열한다.
- 일반 Spot은 `spot_t`, Entry Spot은 `entry_spot_t`를 상속한다. `add_spot<TSpot>()`와
  `add_entry_spot<TEntrySpot>()`는 이 계약을 compile-time으로 검증한다.
- actor join admission은 registry handler가 아니라 user Spot member callback으로 처리하고,
  request/reply는 DTO generic이 아니라 `message_t`를 사용한다.
- SPOT packet, subscription, actor packet, actor disconnected 등록은 별도 handler class를
  지원하지 않는다. actor join/post-join/left/disconnected lifecycle은 Spot member callback 이름이 계약이다.
  같은 동작을 여러 방식으로 등록하게 두면 샘플과 실제 구현이 갈라지므로 Spot 객체 하나가
  상태와 동작을 함께 갖는 방식으로 통일한다.
- Spot create result는 `existing`, `created`, `rejected` state와 optional reply message를
  담고, close는 actor가 남아 있으면 실패한다.
- Play sample smoke는 handler 객체 직접 호출만으로 통과하지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-zlink-spot
ctest --test-dir framework/languages/cpp/build -L framework-regression -R spot
```

### Goal 12. SPOT Timer

목표는 CAPI timer를 기반으로 `.NET` framework timer와 같은 기능성을 C++ framework에서
제공하는 것이다. C++ framework는 CAPI timer와 CAPI SPOT dispatch event 후 recv 경계를
사용하므로 timer callback 실행 직렬화를 위한 별도 queue나 자체 timer scheduler를 만들지
않는다.

구현 항목:

- `timer_t`
- `timer_options_t`
- `timer_overrun_policy_t`
- `timer_tick_t`
- `spot_context_t::add_timer<THandler>(...)`
- CAPI timer lifecycle
- CAPI timer dispatch event projection
- CAPI SPOT dispatch event 후 timer recv 경계
- `fire_count` 기반 skipped tick 계산
- `scheduled_index`
- `skip_late_ticks`
- `catch_up_bounded`
- `delay_next_tick`
- same timer instance 재진입 금지
- timer handler exception monitoring

완료 기준:

- 사용자는 native timer handle, poller slot, timer recv 순서를 직접 다루지 않는다.
- user Spot timer는 같은 Spot의 packet/subscription/channel reply와 같은 CAPI SPOT dispatch
  event 후 recv 순서 정책을 따른다.
- Entry Spot timer는 Entry Spot actor packet, lifecycle callback, request continuation과
  같은 Entry Spot 실행 줄에서 처리한다.
- Entry Spot application callback 직렬 실행 queue를 Spot runtime 안에서 소유한다.
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
- `actor_join_result_t`
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
- raw coroutine submit
- typed coroutine submit
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
- public 호출은 zlink 스타일 call object를 만들고 `co_await submit<T>()`로 실행한다.
- JSON 변환은 `message_t` 또는 DTO serializer hook을 통해 처리하고, application sample code가
  `nlohmann::json::parse`로 field를 직접 꺼내지 않는다.
- retry, redirect, cookie, proxy, multipart, streaming download는 일반 HTTP client 기능으로
  지원 범위에 포함한다. typed JSON 경로는 그 위의 편의 계층이며, `PATCH`/`HEAD`/`OPTIONS`,
  query 파라미터, raw/form/multipart body, gzip 압축 해제까지 fluent builder로 제공한다.
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
- `zlink/framework.hpp`
- `zlink/framework/contracts/http/http.hpp`
- `http_options_builder_t`
- `options.http().listen(endpoint)`
- `options.http().configure_tls(...)`
- `options.http().configure_server(...)`
- `map_get<THandler>(path)`
- `map_post<THandler>(path)`
- `map_put<THandler>(path)`
- `map_delete<THandler>(path)`
- `http_request_t`
- `http_response_t`
- `use<TMiddleware>()`
- typed DTO HTTP handler shape
- typed DTO + `http_context_t` handler shape
- typed DTO + `http_request_t` handler shape
- `http_response_t` 반환 handler shape
- raw `http_request_t` to `http_response_t` handler shape
- HTTP handler shape resolution algorithm
- typed/raw route invoker generation
- response precedence rules
- JSON body binding
- raw HTTP body/header binding
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
- embedded HTTP server hardening
- keep-alive request loop
- request header/body timeout
- request body/header size limit
- max connections와 overload response
- graceful shutdown drain
- HTTP server logging/health integration
- Drogon/Oat++ 분석 기반 고성능 runtime 구조
- bounded I/O worker pool과 framework handler executor 분리
- connection당 OS thread 생성 방지와 lock 최소화
- startup route table compile
- connection buffer와 response serializer 재사용
- hot path logging allocation 최소화
- HTTP handler e2e와 HTTP server perf gate 분리
- Drogon/Oat++급 C++ backend API framework 성능 gate

완료 기준:

- HTTP handler는 message handler와 같은 `request_type`, `reply_type`,
  `dependency_types`, `handle(...)` 규칙을 사용한다.
- HTTP handler는 typed DTO, typed DTO + `http_context_t`, typed DTO + `http_request_t`,
  `http_response_t` 반환, raw `http_request_t` 입력 shape를 모두 지원한다.
- 위 HTTP handler shape는 sync 반환과 `task_t<T>` async 반환을 모두 지원한다.
- handler shape 판별, typed/raw route invoker 생성, response precedence는 아래 구현 알고리즘을
  따른다.
- raw HTTP handler도 `Boost.Beast`, `Boost.Asio`, OpenSSL/SSL context, TCP socket 타입을
  signature에 노출하지 않고 `http_request_t`와 `http_response_t`만 사용한다.
- `http://`와 `https://` endpoint를 모두 지원한다.
- HTTPS endpoint는 TLS certificate/private key 설정을 요구하고, 누락되면 startup validation이
  실패한다.
- `Boost.Beast`, `Boost.Asio`, OpenSSL/SSL context, TCP socket, acceptor, HTTP parser 타입은
  public header에 나타나지 않는다.
- MVC controller, template rendering, Razor page, WebSocket transport는 포함하지 않는다.
- exception, logging, validation, auth, correlation id 처리는 middleware/filter extension
  point로 둔다.
- 내장 HTTP server는 keep-alive, timeout, body/header limit, max connections, graceful shutdown
  drain을 제공한다.
- 내장 HTTP server는 bounded I/O worker pool, connection lifecycle, route dispatch,
  handler executor를 분리한다. connection마다 OS thread를 새로 만드는 구조이면 완료로 보지 않는다.
- route table, TLS context, serializer registry는 startup에서 준비하고 request마다 다시 만들지
  않는다.
- request hot path의 logging은 global mutex나 high-cardinality label에 의존하지 않는다.
- plain HTTP route와 JSON route는 같은 조건에서 측정한 `Drogon`, `Oat++` baseline 중 더 빠른
  baseline 대비 처리량 하락 10% 이내여야 한다. HTTPS JSON route는 같은 TLS 조건의 baseline 대비
  처리량 하락 15% 이내여야 한다. 이 성능 gate를 통과하지 못하면 Goal 19는 완료로 보지 않는다.
- HTTP server perf gate는 `zlink::http_client`가 아니라 같은 load generator로 zlink, Drogon,
  Oat++ server를 호출해 client 구현 비용이 server 수치에 섞이지 않게 한다.
- `framework-http-perf` label은 정책 문서 검사와 report gate를 모두 포함한다. 최종 완료 판단에서는
  `ZLINK_FRAMEWORK_HTTP_PERF_REPORT`에 perf runner가 생성한 CMake report를 지정하고
  `ZLINK_FRAMEWORK_HTTP_PERF_REQUIRED=1`로 실행해야 한다. report가 없는 기본 regression 실행은
  perf 기준을 평가하지 않았다는 의미이며, 최종 성능 완료 증거가 아니다.
- HTTP handler e2e 테스트는 외부 HTTP 도구나 sample-local client가 아니라
  `zlink::http_client`로 `GET`, `POST`, `PUT`, `DELETE` route를 호출한다.
- TicTacToe sample은 HTTP `POST /games`로 시작한다.

Goal 19 public interface:

```cpp
namespace zlink::framework {

enum class http_method_t {
    get,
    post,
    put,
    delete_
};

struct http_context_t {
    http_method_t method;
    std::string path;
    std::string correlation_id;
    std::map<std::string, std::string> request_headers;
    std::map<std::string, std::string> response_headers;
    std::optional<std::string> response_body;
    int response_status = 200;

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

class http_tls_options_builder_t {
public:
    http_tls_options_builder_t &certificate_file(std::string path);
    http_tls_options_builder_t &private_key_file(std::string path);
};

class http_server_options_builder_t {
public:
    http_server_options_builder_t &set_max_connections(std::size_t value);
    http_server_options_builder_t &set_max_request_body_size(std::size_t bytes);
    http_server_options_builder_t &set_max_header_size(std::size_t bytes);
    http_server_options_builder_t &set_request_headers_timeout(
      std::chrono::milliseconds value);
    http_server_options_builder_t &set_request_body_timeout(
      std::chrono::milliseconds value);
    http_server_options_builder_t &set_write_timeout(
      std::chrono::milliseconds value);
    http_server_options_builder_t &set_keep_alive_timeout(
      std::chrono::milliseconds value);
    http_server_options_builder_t &set_graceful_shutdown_timeout(
      std::chrono::milliseconds value);
    http_server_options_builder_t &set_max_keep_alive_requests(
      std::size_t value);
};

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

} // namespace zlink::framework
```

`http_request_t` field contract:

| field | 의미 |
|-------|------|
| `method` | route matching에 사용한 HTTP method |
| `path` | query string을 제거한 path |
| `target` | 원본 request target. path와 query string을 포함한다 |
| `query_string` | `?` 뒤 query 문자열. 없으면 빈 문자열 |
| `correlation_id` | `X-Correlation-Id`, `X-Request-Id`, 또는 runtime 생성 id |
| `headers` | HTTP header name/value. header name은 runtime canonical form을 사용한다 |
| `route_values` | `{name}` path segment binding 결과 |
| `query_values` | query string binding 결과 |
| `body` | limit 검증이 끝난 request body |
| `content_type` | `Content-Type` header 값. 없으면 빈 문자열 |
| `remote_endpoint` | 가능한 경우 client endpoint. 알 수 없으면 빈 문자열 |

`http_response_t` field contract:

| field | 의미 |
|-------|------|
| `status` | HTTP status code. 기본값은 `200` |
| `body` | response body bytes. string은 binary-safe byte buffer로 취급한다 |
| `content_type` | `Content-Type` response header. 기본값은 `application/json` |
| `headers` | response header name/value |

`http_request_t`와 `http_response_t`는 request 처리 중 runtime이 소유한 값의 복사본이다.
handler는 이 객체의 reference를 저장하면 안 된다. request 완료 뒤 lifetime은 보장하지 않는다.

Goal 19 handler shape:

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
- typed response async: `task_t<http_response_t> handle(const request_type &request)`
- typed response + context:
  `http_response_t handle(const request_type &request, http_context_t &context)`
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

Handler shape resolution algorithm:

1. `request_type` alias가 있으면 typed route 후보로 본다.
2. `request_type` alias가 없고 `handle(const http_request_t&)`가 있으면 raw route로 본다.
3. typed route는 `reply_type` 또는 `http_response_t` 반환 shape 중 하나를 가져야 한다.
4. typed route와 raw route shape가 한 handler에 같이 있으면 실패한다.
5. typed route 안에서 여러 shape가 있으면 아래 우선순위로 하나만 선택한다.

Typed route invocation priority:

1. `task_t<http_response_t> handle(const request_type&, const http_request_t&, http_context_t&)`
2. `http_response_t handle(const request_type&, const http_request_t&, http_context_t&)`
3. `task_t<http_response_t> handle(const request_type&, const http_request_t&)`
4. `http_response_t handle(const request_type&, const http_request_t&)`
5. `task_t<http_response_t> handle(const request_type&, http_context_t&)`
6. `http_response_t handle(const request_type&, http_context_t&)`
7. `task_t<http_response_t> handle(const request_type&)`
8. `http_response_t handle(const request_type&)`
9. `task_t<reply_type> handle(const request_type&, const http_request_t&, http_context_t&)`
10. `reply_type handle(const request_type&, const http_request_t&, http_context_t&)`
11. `task_t<reply_type> handle(const request_type&, const http_request_t&)`
12. `reply_type handle(const request_type&, const http_request_t&)`
13. `task_t<reply_type> handle(const request_type&, http_context_t&)`
14. `reply_type handle(const request_type&, http_context_t&)`
15. `task_t<reply_type> handle(const request_type&)`
16. `reply_type handle(const request_type&)`

Raw route invocation priority:

1. `task_t<http_response_t> handle(const http_request_t&)`
2. `http_response_t handle(const http_request_t&)`

Handler shape failure rules:

| 조건 | 실패 이유 |
|------|-----------|
| `request_type`이 있으나 호출 가능한 typed `handle(...)`이 없음 | route를 실행할 수 없다 |
| `request_type`이 있으나 DTO 반환 shape에 `reply_type`이 없음 | DTO serializer를 알 수 없다 |
| `request_type`이 없고 raw `handle(http_request_t)`도 없음 | route mode를 정할 수 없다 |
| typed shape와 raw shape를 동시에 제공 | typed/raw route mode가 모호하다 |
| raw handler가 `reply_type` 또는 임의 DTO를 반환 | raw response serializer를 추론할 수 없다 |
| handler가 Beast/Asio/OpenSSL 타입을 받음 | public dependency 경계를 위반한다 |
| 둘 이상의 같은 우선순위 overload가 호출 가능 | overload 선택이 모호하다 |

Invoker generation pseudocode:

```cpp
template <typename THandler>
http_route_invoker_t make_invoker()
{
    if constexpr (has_request_type<THandler>) {
        static_assert(!has_raw_http_only_shape<THandler>);
        register_json_serializer<typename THandler::request_type>();
        if constexpr (returns_typed_dto<THandler>) {
            register_json_serializer<typename THandler::reply_type>();
        }
        return make_typed_invoker<THandler>();
    } else {
        static_assert(has_raw_http_shape<THandler>);
        return make_raw_invoker<THandler>();
    }
}
```

Typed invoker steps:

1. body, route value, query value를 하나의 binding JSON으로 합친다.
2. `request_type` serializer로 DTO를 만든다.
3. `http_request_t`와 `http_context_t`를 만든다.
4. 우선순위에 따라 handler overload를 호출한다.
5. 결과가 `reply_type`이면 `http_context_t`의 status/header와 함께 JSON response를 만든다.
6. 결과가 `http_response_t`이면 response object를 기준으로 HTTP response를 만든다.

Raw invoker steps:

1. content type이 JSON인지 검사하지 않는다.
2. body/header/route/query limit은 typed route와 동일하게 적용한다.
3. `http_request_t`를 만든다.
4. raw handler를 호출한다.
5. 반환된 `http_response_t`를 기준으로 HTTP response를 만든다.

Response precedence:

| handler result | 우선순위 |
|----------------|----------|
| `http_response_t` 반환 | `http_response_t`의 status/header/content type/body가 최우선 |
| DTO 반환 + `http_context_t::json_response(...)` 설정 | context의 status/body/header를 사용 |
| DTO 반환 + context header/status만 설정 | context status/header + DTO JSON body 사용 |
| DTO 반환만 있음 | `200 OK`, `application/json`, DTO JSON body 사용 |

middleware `after(...)`는 handler result가 만들어진 뒤 실행된다. `after(...)`가 response header를
추가하면 기존 header를 같은 이름으로 덮어쓸 수 있다. 단, `Content-Length`는 runtime이 최종 body
기준으로 계산하므로 handler나 middleware가 직접 고정하지 않는다.

Goal 19 regression matrix:

| 테스트 | 기대 |
|--------|------|
| DTO sync | `reply_type handle(request)`가 `200` JSON을 반환 |
| DTO async | `task_t<reply_type> handle(request)`가 await 뒤 JSON 반환 |
| DTO context sync | context header/status가 response에 반영 |
| DTO context async | async handler와 context 변경이 함께 반영 |
| DTO request sync | `http_request_t`의 header/query/body를 읽을 수 있음 |
| DTO request async | async handler가 `http_request_t`를 받고 정상 완료 |
| DTO request context | request와 context를 모두 받는 overload가 우선 호출 |
| response sync | `http_response_t` status/header/body가 그대로 반환 |
| response context | `http_response_t`가 context body보다 우선 |
| response request | `http_request_t`를 읽고 `http_response_t`로 응답 |
| raw request sync | serializer 없이 raw body를 받아 응답 |
| raw request async | raw request async handler가 정상 완료 |
| raw content type | JSON이 아닌 content type도 raw route에서 허용 |
| ambiguous route mode | typed shape와 raw shape가 한 handler에 있으면 실패 |
| invalid return type | raw route가 DTO를 반환하면 실패 |
| content length | handler가 준 `Content-Length`는 runtime 최종값으로 보정 |
| unsupported media type | JSON typed route의 잘못된 content type은 `415` |
| body limit | typed/raw route 모두 body limit 초과는 `413` |
| keep-alive | 같은 connection에서 두 request 처리 |
| graceful shutdown | 새 accept 중단, active request drain |
| metrics/logging | route, status, duration, connection counter 갱신 |

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-http
ctest --test-dir framework/languages/cpp/build -L framework-http-e2e
ctest --test-dir framework/languages/cpp/build -L framework-integration -R http
ctest --test-dir framework/languages/cpp/build -L framework-http-perf
ZLINK_FRAMEWORK_HTTP_PERF_REPORT=/path/to/http-perf-report.cmake \
ZLINK_FRAMEWORK_HTTP_PERF_REQUIRED=1 \
  ctest --test-dir framework/languages/cpp/build -L framework-http-perf
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
- `zlink::stream_e2e_client` server e2e/smoke/perf helper
- e2e client `async()` terminator
- reconnect, heartbeat, pending request correlation
- JSON 기본 ON
- MessagePack/Protobuf 선택
- LZ4 build feature 기본 ON, packet 압축은 opt-in
- Unreal plugin/module packaging
- Unreal `Public/` / `Private` 분리
- Unreal public API와 Game Thread dispatch adapter
- Unreal private 구현의 기본 connector 위임
- Blueprint callable connect/close/send/request
- Game Thread callback dispatch
- Unreal Automation Test

완료 기준:

- connector는 framework sample이나 framework target이 아니다.
- 일반 C++ connector는 Asio를 내부 구현으로 사용하며, TCP/TLS/WebSocket/WebSocket over TLS가
  같은 packet API로 동작한다.
- Unreal connector는 public header에 일반 C++ connector type을 노출하지 않는다.
- Unreal connector private 구현은 기본 connector를 소유하고 send/request/dispatch/lifecycle을
  위임한다. STREAM frame, reconnect, heartbeat, pending request correlation을 Unreal adapter에
  다시 구현하지 않는다.
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
ctest --test-dir framework/languages/cpp/build -L connector-package
ctest --test-dir framework/languages/cpp/build -L connector-unreal-contract
ctest --test-dir framework/languages/cpp/build -L connector-unreal-compile
ctest --test-dir framework/languages/cpp/build -L connector-unreal-smoke
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
ctest --test-dir framework/languages/cpp/build -L framework-sample-api
ctest --test-dir framework/languages/cpp/build -L framework-sample-bingo
ctest --test-dir framework/languages/cpp/build -L framework-sample-play
ctest --test-dir framework/languages/cpp/build -L framework-sample-registry
ctest --test-dir framework/languages/cpp/build -L framework-sample-session
ctest --test-dir framework/languages/cpp/build -L framework-sample-tictactoe
```

### Goal 22. Final Regression, Package, Extension Boundary

목표는 이 문서의 모든 goal 항목이 구현됐는지 최종 확인하고, `.NET` framework와 같은 구조, 기능,
사용성을 회귀 테스트로 고정하는 것이다.

구현 항목:

- full CTest regression
- C++ framework runtime line coverage 80% 이상
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
- coverage build의 runtime line coverage가 80% 이상이다.
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
ctest --test-dir framework/languages/cpp/build -L framework-http-perf --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-config --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-observability --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-contract --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-unit --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-regression --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-https --output-on-failure
ctest --test-dir framework/languages/cpp/build -L parity --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-parity --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-api --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-bingo --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-registry --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-play --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-session --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-tictactoe --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-unit --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-integration --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-contract --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-protocol --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-transport --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-typed --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-package --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-unreal-contract --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-unreal-compile --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-unreal-smoke --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-package --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-tooling --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-extension --output-on-failure
cmake -S framework/languages/cpp \
  -B framework/languages/cpp/build-coverage \
  -DZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE=ON \
  -DZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD=80
cmake --build framework/languages/cpp/build-coverage
ctest --test-dir framework/languages/cpp/build-coverage --output-on-failure
ctest --test-dir framework/languages/cpp/build-coverage -L framework-coverage --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## 6. 참고 문서 추적표

아래 문서는 배경 설명과 설계 기록을 제공한다. 구현 완료 여부는 이 문서의 goal별 구현 항목,
완료 기준, 검증 명령으로 판단한다. 아래 문서에서 새로운 요구사항을 발견하면 먼저 이 문서의
해당 goal에 반영한 뒤 구현한다.

| 참고 문서 | 관련 goal |
|-----------|-----------|
| [cpp-framework-implementation-plan.ko.md](./cpp-framework-implementation-plan.ko.md) | Goal 1-22 실행 계획 |
| [cpp-framework-overview.ko.md](./cpp-framework-overview.ko.md) | Goal 1-22 |
| [connector/doc/guide/INDEX.ko.md](../../connector/doc/guide/INDEX.ko.md) | Goal 14-16 |
| [cpp-http-client.ko.md](../../http-client/doc/spec/cpp-http-client.ko.md) | Goal 18 |
| [cpp-framework-policy.ko.md](./cpp-framework-policy.ko.md) | Goal 1-22 |
| [cpp-application-framework.ko.md](../spec/cpp-application-framework.ko.md) | Goal 6, Goal 19, Goal 21, Goal 22 |
| [cpp-framework-interfaces.ko.md](../spec/cpp-framework-interfaces.ko.md) | Goal 1-19, Goal 22 |
| [handler-interfaces.ko.md](../spec/handler-interfaces.ko.md) | Goal 8-14 |
| [cpp-channel-messaging.ko.md](../spec/cpp-channel-messaging.ko.md) | Goal 9, Goal 10 |
| [channel-messaging-samples.ko.md](./channel-messaging-samples.ko.md) | Goal 9, Goal 21 |
| [cpp-spot.ko.md](../spec/cpp-spot.ko.md) | Goal 11, Goal 12, Goal 14 |
| [spot-samples.ko.md](./spot-samples.ko.md) | Goal 11, Goal 12, Goal 14, Goal 21 |
| [stage-wrapper-on-spot.ko.md](../spec/stage-wrapper-on-spot.ko.md) | Goal 11, Goal 12, Goal 17 |
| [cpp-stream.ko.md](../spec/cpp-stream.ko.md) | Goal 13, Goal 14 |
| [stream-open-items.ko.md](./stream-open-items.ko.md) | Goal 13 |
| [stream-samples.ko.md](./stream-samples.ko.md) | Goal 13, Goal 14, Goal 21 |
| [actor-gateway-session-relay.ko.md](../spec/actor-gateway-session-relay.ko.md) | Goal 14, Goal 21 |
| [cpp-registry.ko.md](../spec/cpp-registry.ko.md) | Goal 11, Goal 14, Goal 15 |
| [cpp-monitoring.ko.md](../spec/cpp-monitoring.ko.md) | Goal 12, Goal 16 |
| [cpp-http-hosting.ko.md](../spec/cpp-http-hosting.ko.md) | Goal 19, Goal 21 |
| [cpp-embedded-http-server.ko.md](../spec/cpp-embedded-http-server.ko.md) | Goal 19, Goal 21 |
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
| HTTP hosting | Goal 19 | `framework-http`, `framework-http-e2e`, `framework-http-perf` |
| C++/Unreal connector | Goal 20 | `connector-*`, `connector-unreal-*` |
| samples | Goal 21 | `framework-sample-*` |
| package/extensions/final audit | Goal 22 | `framework-package`, `framework-extension` |
| POSD 리팩토링 | Goal 1-22 | POSD 기록, `framework-regression` |

wildcard label은 아래 concrete label 묶음을 뜻한다. CTest 명령을 직접 실행할 때는 필요한
concrete label을 선택한다.

| wildcard label | concrete label |
|----------------|----------------|
| `http-client-*` | `http-client-contract`, `http-client-unit`, `http-client-e2e`, `http-client-https`, `http-client-regression` |
| `connector-*` | `connector-unit`, `connector-integration`, `connector-e2e`, `connector-contract`, `connector-protocol`, `connector-transport`, `connector-typed`, `connector-package` |
| `connector-unreal-*` | `connector-unreal-contract`, `connector-unreal-compile`, `connector-unreal-smoke` |
| `framework-sample-*` | `framework-sample-smoke`, `framework-sample-parity`, `framework-sample-api`, `framework-sample-bingo`, `framework-sample-play`, `framework-sample-registry`, `framework-sample-session`, `framework-sample-tictactoe` |

## 8. Goal 실행용 문구

goal을 만들 때는 아래 문구를 objective로 사용할 수 있다.

```text
framework/languages/cpp/doc/draft/cpp-framework-implementation-plan.ko.md의 Goal N을
완료 기준까지 구현하고, POSD 기반 리팩토링을 이슈가 없을 때까지 수행한 뒤 해당
검증 명령을 실행한다. 관련 draft는 참고만 하고, 완료 여부는 이 문서 하나의 구현 항목,
완료 기준, regression matrix, 검증 명령으로 판단한다. 관련 draft에서 이 문서에 없는
요구사항을 발견하면 구현 전에 이 문서의 해당 goal에 먼저 반영한다.
```

여러 goal을 한 번에 묶어 실행할 때도 중간 goal의 완료 기준을 건너뛰지 않는다.
이 문서와 실제 코드가 충돌하면 먼저 충돌 항목을 적고, 이 문서 또는 구현 중 하나를
명시적으로 정리한 뒤 진행한다. 전체 작업은 Goal 1부터 Goal 22까지 미구현, 오구현,
미검증 항목이 0개가 될 때까지 반복한다.
