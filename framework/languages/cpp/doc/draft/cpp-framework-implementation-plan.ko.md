<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework For C++](./README.ko.md) | [다음: Draft -- ZLink Framework C++ Policy](./cpp-framework-policy.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md)

# Draft -- ZLink Framework C++ Implementation Plan

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `framework/languages/cpp/doc/draft` 아래 C++ framework
> 초안 전체를 빠짐없이 구현하기 위한 실행 계획이다.

## 1. 목적

이 문서는 C++ framework 구현을 여러 개의 `goal`로 나누어 진행할 때 사용할 기준이다.
각 goal은 독립적으로 구현, 검증, 리뷰할 수 있어야 하며, 전체 goal이 끝나면 현재 draft
문서에 적힌 기능 범위가 빠짐없이 구현되어야 한다.

구현 범위는 `.NET` framework와 같은 기능성을 C++20 방식으로 제공하는 것이다. 구현
순서는 나뉘지만 기능 범위를 줄이지 않는다. C++ framework는 `zlink::framework` server
runtime이고, C++ Stream Connector와 Unreal Stream Connector는 별도 산출물로 구현한다.

## 2. 공통 실행 규칙

모든 goal은 아래 규칙을 따른다.

- C++20 이상만 지원한다.
- public async 표면에는 `std::future`를 쓰지 않는다.
- public 호출은 call object를 만들고 마지막에 `submit(callback)` 또는
  `co_await submit()`으로 실행한다.
- public cancellation token 타입은 두지 않는다. shutdown, timeout, close, disconnected
  상태는 framework result 또는 `framework_exception_t`로 표현한다.
- handler, timer, stream session, actor relay 안에서 blocking wait를 허용하지 않는다.
- CPU-bound 또는 blocking 가능성이 있는 handler는 `handler_execution_t::offload`로
  framework offload executor에서 실행한다.
- 사용자는 native C handle, raw poller slot, callback userdata, raw recv loop를 직접
  다루지 않는다.
- CAPI dispatch callback과 CAPI timer event는 framework runtime 내부에서 typed handler로
  사상한다.
- C++ framework는 `bindings/cpp`보다 더 강한 contract/runtime 분리를 적용한다.
  public contract header는 runtime 구현 header를 include하지 않고, runtime 세부는
  `src/runtime/*`에 둔다.
- `.NET`식 contract 분리는 C++ public 타입을 전부 pure virtual class로 만들라는 뜻이
  아니다. public facade는 concrete type일 수 있지만 runtime 구현 타입을 노출하지 않는다.
- 각 기능을 구현하기 전 public contract owner와 runtime implementation owner를 먼저
  문서 또는 코드 주석이 아닌 파일 구조로 나눈다. 이 분리가 정해지지 않으면 구현을
  진행하지 않는다.
- 구현 중 interface/implementation 경계가 애매해지면 코드를 먼저 작성하지 않는다.
  관련 draft 문서를 먼저 수정해 `.NET` 대응, public contract owner, runtime owner,
  test boundary를 다시 고정한 뒤 구현을 재개한다.
- `contracts/detail/*`은 type trait, concept check, facade forwarding만 허용한다.
  queue, executor, dispatch projection, serializer registry, frame codec 같은 runtime
  구현은 둘 수 없다.
- public facade가 상태를 가져야 하면 PIMPL 또는 type-erased state를 사용하고, state
  정의는 `src/runtime/*`에 둔다.
- codec 사용성은 binding, framework, connector 모두 `message_t` 중심으로 맞춘다.
- base C++ binding은 JSON, MessagePack, Protobuf dependency를 끌고 오지 않는다.
- MessagePack, Protobuf, LZ4는 선택 기능으로 두고 기본 설치에 강제하지 않는다.
- application sample code는 serializer를 직접 호출하지 않는다. typed DTO 변환은
  framework/connector codec helper 내부에서 처리하며, 샘플 DTO header에는 C++ serializer가
  찾을 수 있는 `to_json`/`from_json` hook만 둔다.
- 샘플 client는 raw STREAM payload 조립, `nlohmann::json::parse`, field-by-field JSON
  추출, sample-only `to_stream_payload`/`from_stream_payload` helper를 사용하지 않는다.
  `.NET` 샘플처럼 connector의 typed request/send/on 표면을 사용한다.
- 테스트는 CTest로 등록하고, framework C++ 테스트는 GoogleTest와 GoogleMock을 사용한다.
- 모든 goal은 구현 뒤 POSD 기반 리팩토링을 반드시 한 번 이상 수행한다. 21개 goal을
  모두 진행하면 POSD 기반 리팩토링도 최소 21번 수행되어야 한다.

## 3. 산출물 경계

| 산출물 | 위치 | target | 역할 |
|--------|------|--------|------|
| C++ framework | `framework/languages/cpp/framework` | `zlink::framework` | server application host/runtime |
| C++ Stream Connector | `framework/languages/cpp/connector` | `zlink::stream_connector` | client-side STREAM connector |
| Unreal Stream Connector | `framework/languages/cpp/unreal-connector` | Unreal module/plugin | Unreal 전용 client connector |
| framework extensions | `framework/languages/cpp/extensions` | extension targets | bridge, config, codec, observability 확장 |
| framework samples | `framework/languages/cpp/samples` | sample executables | `Bingo`, `TicTacToe` 리뷰 샘플 |
| framework tests | `framework/languages/cpp/tests/Zlink.Framework.*Tests` | CTest labels | `.NET` test project 분류에 맞춘 contract, unit, e2e, package |
| connector tests | `framework/languages/cpp/tests/Systems.Zlink.Stream.Connector.Tests` | CTest labels | connector contract, protocol, transport, typed 흐름 |
| Unreal connector tests | `framework/languages/cpp/tests/Zlink.Unreal.Stream.Connector.Tests` | CTest labels | Unreal public API compile/smoke와 automation source check |

각 산출물 안에서는 `.NET` framework의 `Contracts/*`와 `Runtime/*` 분리를 따른다.
C++에서는 `Contracts/*`가 설치되는 public header이고, `Runtime/*`가 컴파일되는 구현이다.
이 기준은 `bindings/cpp`의 인터페이스/구현 분리보다 강하게 적용한다. framework는 낮은
수준 native wrapper가 아니라 application host/runtime 계층이므로, public header는 계약과
facade만 노출하고 runtime 실행 세부는 `src/runtime/*`에 숨겨야 한다.

| `.NET` 기준 | C++ framework 기준 | C++ connector 기준 | 공개 여부 |
|-------------|--------------------|---------------------|-----------|
| `Contracts/*` | `framework/include/zlink/framework/contracts/*` | `connector/include/zlink/stream_connector/contracts/*` | public |
| `Runtime/*` | `framework/src/runtime/*` | `connector/src/runtime/*` | private implementation |
| `Runtime/Backend/Contracts` | `framework/src/runtime/backend/contracts` | `connector/src/runtime/backend/contracts` | private backend contract |
| project facade | `zlink/framework.hpp`, `zlink/framework/*.hpp` | `zlink/stream_connector.hpp` | public |
| implementation glue | `framework/src/runtime/*` 내부 helper | `connector/src/runtime/*` 내부 helper | private |

C++ connector runtime은 `.NET` `Systems.Zlink.Stream.Connector/Runtime`의 파일 분류를
아래처럼 투영한다. C++ 파일명은 snake_case를 사용하지만 책임 경계는 동일하게 둔다.

| `.NET` connector runtime 축 | C++ connector runtime owner |
|-----------------------------|------------------------------|
| `Runtime/Calls` | `connector/src/runtime/calls/*` |
| `Runtime/Protocol/Compression` | `connector/src/runtime/protocol/compression/*` |
| `Runtime/Protocol/Framing` | `connector/src/runtime/protocol/framing/*`, `connector/src/runtime/protocol/framing.*` |
| `Runtime/Protocol/*Codec`, packet name resolver | `connector/src/runtime/protocol/*` |
| `Runtime/Transport` | `connector/src/runtime/transport/*` |
| lifecycle, callbacks, heartbeat | `connector/src/runtime/connector_lifecycle.*`, `connector/src/runtime/connector_callbacks.*`, `connector/src/runtime/heartbeat_monitor.*` |
| receive dispatch/loop, task runner | `connector/src/runtime/receive_dispatcher.*`, `connector/src/runtime/receive_loop.hpp`, `connector/src/runtime/task_runner.hpp` |
| pending requests, typed handlers | `connector/src/runtime/pending_requests.hpp`, `connector/src/runtime/typed_handler_registry.hpp` |

framework의 public/runtime 하위 축은 `.NET` `Zlink.Framework`의 현재 구조를 기준으로
아래처럼 고정한다. C++ 파일 배치가 이 표와 맞지 않는다면 먼저 문서를 갱신하고, 그 다음
코드를 옮긴다.

| `.NET` public 축 | C++ public owner | `.NET` runtime 축 | C++ runtime owner |
|------------------|------------------|-------------------|-------------------|
| `Contracts/Actors` | `contracts/actors/*` | `Runtime/Actors` | `src/runtime/actors/*` |
| `Contracts/Assembly` | `contracts/assembly/*` | `Runtime/Host` | `src/runtime/host/*` |
| `Contracts/Channels` | `contracts/channels/*` | `Runtime/Channels`, `Runtime/Messaging` | `src/runtime/channels/*`, `src/runtime/messaging/*` |
| `Contracts/Codecs` | `contracts/codecs/*` | `Runtime/Codecs` | `src/runtime/codecs/*` |
| `Contracts/Configuration` | `contracts/configuration/*` | `Runtime/Configuration` | `src/runtime/configuration/*` |
| `Contracts/Dispatch` | `contracts/dispatch/*` | `Runtime/Dispatch`, `Runtime/Execution` | `src/runtime/dispatch/*`, `src/runtime/execution/*` |
| `Contracts/Errors` | `contracts/errors/*` | `Runtime/Messaging` | `src/runtime/messaging/*` |
| `Contracts/Eventing` | `contracts/eventing/*` | `Runtime/Diagnostics` | `src/runtime/diagnostics/*` |
| `Contracts/Handlers` | `contracts/handlers/*` | `Runtime/Handlers` | `src/runtime/handlers/*` |
| `Contracts/Registry` | `contracts/registry/*` | `Runtime/Registry` | `src/runtime/registry/*` |
| `Contracts/Spots` | `contracts/spots/*` | `Runtime/Spots` | `src/runtime/spots/*` |
| `Contracts/Streams` | `contracts/streams/*` | `Runtime/Streams` | `src/runtime/streams/*` |
| `Contracts/Timers` | `contracts/timers/*` | `Runtime/Timers` | `src/runtime/timers/*` |
| internal backend seam | 없음 | `Runtime/Backend`, `Runtime/Backend/Contracts` | `src/runtime/backend/*`, `src/runtime/backend/contracts/*` |

`src/runtime/backend/contracts/*`는 public contract가 아니다. 이 이름은 runtime 내부 seam을
뜻하며, 설치 header와 extension public header에서 include하면 안 된다.

Unreal Connector는 Unreal 관례에 따라 `Source/ZLinkStreamConnector/Public`과
`Source/ZLinkStreamConnector/Private`를 사용한다. `Public`은 Unreal 전용 contract와
Blueprint/Game Thread 표면만 담고, transport와 codec 구현은 `Private`에 둔다.

public header가 binding 타입을 노출할 수 있는 범위도 제한한다. `message_t`처럼 payload
copy/move boundary를 설명하는 타입은 contract에 나타날 수 있지만, `context_t`,
socket type, native handle owner, dispatch callback owner는 framework runtime 내부에만
둔다. 이 기준은 connector와 Unreal connector에도 적용한다. 일반 C++ connector public
API는 client endpoint, packet, codec option, callback/coroutine submit을 노출하고,
receive loop, reconnect state, heartbeat, frame codec 구현은 runtime에 둔다. Unreal
connector public API는 Unreal 타입, Blueprint callable 함수, Blueprint/native delegate,
Game Thread dispatch만 노출하며 일반 C++ connector의 `task_t`나 coroutine submit 표면을
가져오지 않는다.

## 4. Goal 구성 원칙

각 goal은 아래 형식으로 실행한다.

1. 관련 draft 문서를 먼저 읽는다.
2. public contract owner와 runtime implementation owner를 먼저 확정한다.
   확정할 수 없거나 `.NET` 구조와 어긋나는 부분이 있으면 구현을 멈추고 draft 문서를
   먼저 수정한다.
3. public header와 target 구조를 구현한다.
4. internal runtime 구현을 붙인다.
5. 해당 goal의 contract/unit/integration 테스트를 추가한다.
6. 필요한 샘플 코드를 갱신한다.
7. 해당 goal 범위에 대해 POSD 기반 리팩토링을 수행한다.
8. 리팩토링 뒤 같은 검증을 다시 실행한다.
9. `git diff --check`와 해당 CTest label을 실행한다.
10. goal 완료 기준을 문서 항목별로 대조한다.

goal 하나가 끝났다고 전체 기능이 완성된 것으로 보지 않는다. 다음 goal이 이전 public
표면을 바꿔야 하면, 호출자 복잡성이 줄어드는지와 `.NET` 기능 parity가 유지되는지를
먼저 확인한다.

### 4.1 Public Surface Gate

각 goal은 구현 전에 public surface gate를 통과해야 한다. 이 gate의 목적은 C++ 구현이
진행되면서 `.NET`의 `Contracts/*`와 `Runtime/*` 분리보다 약해지는 것을 막는 것이다.

확인 항목은 아래와 같다.

- 새 public 타입이 어느 `contracts/*` header의 소유인지 정한다.
- 같은 기능의 runtime state, registry, cache, queue, dispatcher, codec 구현이 어느
  `src/runtime/*` 파일의 소유인지 정한다.
- facade header는 contract header를 묶는 역할만 하며, runtime header를 include하지 않는다.
- public method 인자와 반환값에 native handle, socket, poller, dispatch token,
  callback userdata가 들어가지 않는다.
- `contracts/detail/*`에 둘 코드는 compile-time 검사와 forwarding으로 제한한다.
- 상태가 필요한 facade는 PIMPL 또는 type-erased state를 사용한다.
- 새 public header가 외부 dependency 타입을 노출하면 그 dependency가 기능상 필수인지
  확인한다. 필수가 아니면 runtime 또는 extension target 뒤로 숨긴다.
- layout/contract test에 public header include와 runtime include 금지 검사를 추가하거나
  기존 검사를 갱신한다.

이 gate를 통과하지 못하면 해당 goal의 runtime 구현을 시작하지 않는다.

#### 4.1.1 Interface Separation Review

각 goal을 시작할 때 아래 내용을 먼저 기록한다. 기록 위치는 해당 goal의 작업 로그,
PR 설명, 또는 `cpp-framework-posd-refactoring-log.ko.md`의 goal 항목이다.

| 확인 항목 | 통과 기준 |
|-----------|-----------|
| `.NET` 대응 확인 | 같은 기능의 `.NET Contracts/*` 타입과 `Runtime/*` 구현을 먼저 확인한다. |
| contract owner | 새 public 타입이 들어갈 `contracts/*` header 또는 facade header가 정해져 있다. |
| runtime owner | state, registry, cache, queue, dispatcher, frame codec, native owner가 들어갈 `src/runtime/*` 파일이 정해져 있다. |
| public dependency | public header가 불필요한 외부 dependency를 강제하지 않는다. |
| native leakage | CAPI handle, socket, poller slot, callback userdata, dispatch token이 public signature에 없다. |
| detail 사용 | `contracts/detail/*`은 type trait, concept check, forwarding만 가진다. |
| state hiding | public facade가 상태를 가지면 PIMPL 또는 type-erased state를 사용한다. |
| validation | contract/layout test가 public header include와 runtime include 금지 규칙을 확인한다. |

이 검토는 구현 선행 조건이다. 새 기능을 public header에 추가하면서 runtime owner를
정하지 못했다면 구현을 멈추고 draft를 먼저 갱신한다.

#### 4.1.2 Goal별 Interface/Implementation Owner Matrix

각 goal은 아래 owner matrix를 기준으로 시작한다. 구현 중 더 정확한 owner가 필요하면
먼저 이 문서와 관련 draft를 갱신한 뒤 코드를 수정한다. 이 표에 없는 public 타입이나
runtime state를 새로 만들 때도 같은 규칙을 적용한다.

| Goal | public contract owner | runtime implementation owner | public에 두지 않는 것 |
|------|-----------------------|------------------------------|-----------------------|
| 1. skeleton/build | facade header, `contracts/*` 빈 owner | `framework/src/runtime/*`, connector `src/runtime/*`, Unreal `Private/` | runtime header install, public runtime include |
| 2. binding codec | `bindings/cpp/include/zlink/message.hpp`, 선택 codec header | 선택 codec target 구현 | codec 외부 dependency의 base binding 강제 |
| 3. core types/error | `contracts/errors/*`, `contracts/dispatch/*`, call object contract | `src/runtime/messaging/*`, `src/runtime/dispatch/*` | pending node, completion token, blocking wait |
| 4. app/host/config/logging | `contracts/configuration/*`, `app.hpp` | `src/runtime/host/*`, `src/runtime/configuration/*`, `src/runtime/diagnostics/*` | native context owner, signal backend, logger backend |
| 5. DI/scope | service collection/provider/scope contract | `src/runtime/configuration/*` | service cache, destruction stack, scope registry |
| 6. runtime integration | zlink builder, dispatch/offload option contract | `src/runtime/backend/*`, `src/runtime/channels/*`, `src/runtime/execution/*` | CAPI handle, poller slot, recv/drain loop |
| 7. handler/serializer | `contracts/handlers/*`, `contracts/codecs/*` | `src/runtime/handlers/*`, `src/runtime/codecs/*` | descriptor map, serializer map, DI resolve order |
| 8. channel messaging | `contracts/channels/*` | `src/runtime/channels/*`, `src/runtime/messaging/*` | socket set, reply correlation table, send-ready queue |
| 9. flow/reliability | call result, retry/dead-letter hook contract | `src/runtime/messaging/*`, `src/runtime/channels/*` | bounded queue storage, timeout wheel, drain state |
| 10. SPOT runtime | `contracts/spots/*`, selected actor contract | `src/runtime/spots/*`, `src/runtime/actors/*` | activation table, native dispatch router, subscription pump |
| 11. SPOT timer | `contracts/timers/*`, Spot timer facade | `src/runtime/timers/*`, `src/runtime/spots/*` | native timer token, fire-count drain loop |
| 12. STREAM framework | `contracts/streams/*` | `src/runtime/streams/*` | frame codec, session table, transport loop |
| 13. ActorGateway relay | `contracts/actors/*`, bound session contract | `src/runtime/actors/*`, `src/runtime/streams/*` | actor mailbox, relay packet dispatcher, locator codec |
| 14. Registry/topology | `contracts/registry/*`, discovery builder contract | `src/runtime/registry/*`, `src/runtime/configuration/*` | topology cache, backend query owner, route resolver state |
| 15. monitoring | `contracts/eventing/*`, monitoring builder contract | `src/runtime/diagnostics/*`, 기능별 runtime event source | snapshot diff cache, telemetry backend |
| 16. module/hosted service | module and hosted service contract | `src/runtime/host/*`, `src/runtime/configuration/*` | lifecycle scheduler, hosted service drain set |
| 17. C++ connector | `connector/include/zlink/stream_connector/contracts/*` | `connector/src/runtime/*` | Asio receive loop, reconnect state, pending request table, frame codec |
| 18. Unreal connector | Unreal `Public/` contract | Unreal `Private/` implementation | Unreal `Sockets`/`Networking`, transport internals |
| 19. samples | sample source using public API only | sample support runtime only when private to sample | sample-only metadata store as framework contract |
| 20. final regression | installed public headers and package consumer tests | all runtime owner directories | accidental external dependency leak, runtime header include, missing native runtime soname |
| 21. extensions | extension public contract/target | extension private runtime | Kafka/gRPC/HTTP/YAML/FlatBuffers dependency in core target |

owner matrix를 만족하지 못하는 변경은 완료 기준을 통과한 것으로 보지 않는다. 특히
`contracts/detail/*`에 queue, executor, dispatch projection, codec registry, frame codec,
native lifecycle 구현을 넣는 방식은 금지한다. template 편의가 필요하면 public header에서
검사와 forwarding만 수행하고, 실제 저장소와 실행은 type-erased runtime call로 넘긴다.

### 4.2 POSD 리팩토링 게이트

각 goal은 다음 goal로 넘어가기 전에 아래 절차를 완료해야 한다. 이 절차는 선택 사항이
아니며, 구현이 작더라도 최소 한 번은 수행한다.

1. goal에서 변경한 public API, internal runtime, tests, samples를 대상으로 POSD 위험
   신호를 찾는다.
2. 위험 신호는 아래 기준으로 기록한다.
   - 얕은 모듈
   - native handle, poller, recv 순서 같은 내부 지식 누출
   - 패스스루 메서드
   - 시간적 분해
   - 특수 코드와 범용 코드 혼합
   - 호출자가 기억해야 하는 설정 순서
   - 같은 정책이 두 곳 이상에 흩어진 중복 지식
   - 코드를 반복 설명하는 주석
3. 위험 신호마다 두 가지 이상 수정 방향을 비교한다.
4. 더 나은 방향을 선택한 이유를 호출자 복잡성, 정보 은닉, 모듈 깊이 관점에서 적는다.
5. 선택한 방향으로 리팩토링한다.
6. 리팩토링 뒤 같은 위험 신호가 남았는지 다시 확인한다.
7. 위험 신호가 남아 있으면 같은 goal 안에서 다시 리팩토링한다.
8. 남은 항목이 의도된 tradeoff라면 이유와 후속 goal을 명시한다.

POSD 게이트 완료 기준은 아래와 같다.

- public API가 구현 세부를 호출자에게 넘기지 않는다.
- native socket, CAPI handle, timer recv, poller slot, ActorGateway frame codec 같은
  내부 결정은 public 표면 밖에 숨겨져 있다.
- handler, serializer, runtime, transport, monitoring 책임이 한 모듈에 섞이지 않는다.
- 테스트가 구현 순서가 아니라 observable contract를 검증한다.
- 리팩토링 뒤 해당 goal의 검증 명령이 다시 통과한다.

각 goal의 완료 보고에는 `POSD 리팩토링` 항목을 따로 적는다. 적어도 다음 내용을 포함한다.

- 발견한 위험 신호
- 비교한 대안
- 적용한 리팩토링
- 남은 tradeoff 또는 없음
- 재실행한 검증 명령

## 5. Goal 목록

### Goal 1. Repository Skeleton And Build

목표는 C++ framework, connector, Unreal connector, samples, tests가 서로 다른 산출물로
빌드될 수 있는 기본 구조를 만드는 것이다.

구현 항목:

- `framework/languages/cpp/framework` 디렉토리와 public include layout 생성
- `framework/include/zlink/framework/contracts/*`와 `framework/src/runtime/*` 분리
- `zlink/framework.hpp` umbrella header
- `zlink/framework/*.hpp` 세부 header
- `zlink::framework` CMake target
- `framework/languages/cpp/connector` 디렉토리와 `zlink::stream_connector` target
- `connector/include/zlink/stream_connector/contracts/*`와 `connector/src/runtime/*` 분리
- `framework/languages/cpp/unreal-connector` plugin/module 골격
- Unreal `Public/`과 `Private/` 분리
- `framework/languages/cpp/samples/Bingo`
- `framework/languages/cpp/samples/TicTacToe`
- `framework/languages/cpp/tests`와 CTest label 구조
- CMake option:
  - `ZLINK_FRAMEWORK_CPP_BUILD_TESTS`
  - `ZLINK_FRAMEWORK_CPP_BUILD_SAMPLES`
  - `ZLINK_STREAM_CONNECTOR_WITH_JSON`
  - `ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK`
  - `ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF`
  - `ZLINK_STREAM_CONNECTOR_WITH_LZ4`

완료 기준:

- framework target과 connector target이 독립적으로 configure된다.
- connector를 링크하지 않아도 framework target이 configure된다.
- framework target을 링크하지 않아도 connector target이 configure된다.
- public contract header와 runtime 구현 디렉토리가 물리적으로 분리된다.
- runtime 구현 header가 installed public include 표면에 들어가지 않는다.
- public contract header가 `src/runtime/*` header를 include하지 않는다.
- public header가 CAPI handle, dispatch callback userdata, raw recv loop, poller slot,
  frame codec, pending queue 구현 타입을 include하거나 노출하지 않는다.
- public facade header는 `contracts/*`를 묶는 역할만 하고 새 runtime 구현 계약을 만들지 않는다.
- layout contract test가 public/runtime 분리와 runtime include 금지를 검증한다.
- framework와 connector의 모든 `contracts/*.hpp`는 public header include compile test에
  직접 포함된다. 새 public contract header를 추가하고 coverage를 누락하면 layout contract
  test가 실패한다.
- public header include compile test가 통과한다.

검증:

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L framework-contract
git diff --check -- framework/languages/cpp
```

### Goal 2. Binding Codec Surface Alignment

목표는 기존 C++ binding codec 표면을 framework와 connector가 사용할 수 있는
`message_t` 중심 API로 정렬하는 것이다.

구현 항목:

- base binding target은 raw `message_t`, `multipart_t`, protocol enum만 제공
- base binding target에서 JSON, MessagePack, Protobuf dependency 제거 확인
- 선택 codec target:
  - `zlink::cpp_codec_json`
  - `zlink::cpp_codec_messagepack`
  - `zlink::cpp_codec_protobuf`
- JSON header를 include하고 target을 링크한 경우에만:
  - `message_t::from_json(value)`
  - `message.parse_json<T>()`
- MessagePack header를 include하고 target을 링크한 경우에만:
  - `message_t::from_messagepack(value)`
  - `message.parse_messagepack<T>()`
- Protobuf header를 include하고 target을 링크한 경우에만:
  - `message_t::from_protobuf(value)`
  - `message.parse_protobuf<T>()`
- 기존 codec namespace 함수형 helper는 이행 기간용 shim으로 유지 가능
- 신규 binding/framework/connector 샘플은 message 중심 API만 사용
- framework와 connector 샘플 application code는 `message_t::from_json`,
  `message.parse_json<T>()`, 또는 connector auto codec helper를 통해 typed DTO를 다룬다.
  직접 JSON parser를 호출해 field를 꺼내는 코드는 완료 기준을 통과하지 못한다.

완료 기준:

- `zlink::cpp`만 링크하는 app은 codec 외부 dependency를 요구하지 않는다.
- 각 codec target을 링크한 app만 해당 외부 dependency를 요구한다.
- 기존 shim은 새 API를 호출하는 얇은 wrapper다.
- framework와 connector 구현은 함수형 codec namespace API를 직접 사용하지 않는다.

검증:

```bash
cmake --build bindings/cpp/build
ctest --test-dir bindings/cpp/build -R codec
git diff --check -- bindings/cpp framework/languages/cpp
```

### Goal 3. Core Framework Types And Error Model

목표는 이후 goal이 사용할 public 타입, error kind, result, exception, call object의
기본 계약을 닫는 것이다.

구현 항목:

- namespace `zlink::framework`
- `framework_error_kind_t`
- `framework_exception_t`
- `result_t<T>`
- `task_t<T>`
- internal `coroutine_executor_t`
- `send_call_t`
- `request_call_t<T>`
- `stream_write_call_t`
- `relay_call_t`
- `actor_join_result_t<TReply>`
- `actor_join_spot_call_t<TReply>`
- `actor_join_entry_spot_call_t`
- `submit(callback)` 표면
- `co_await submit()` 표면
- timeout, shutdown, disconnected, queue full, decode failure, handler not found mapping
- blocking wait API 미제공

완료 기준:

- callback submit과 coroutine submit이 같은 error kind를 반환한다.
- shutdown 이후 새 submit은 `shutdown`으로 실패한다.
- timeout은 `timeout`으로 실패한다.
- public async 표면에 `std::future`가 없다.
- public async 표면에 `boost::asio::awaitable`이 없다.
- `task_t<T>`와 handler dispatch는 내부 `coroutine_executor_t`를 통해 실행한다.
- handler registry, route handler, SPOT handler, stream session callback invoker는
  내부적으로 `task_t<...>`를 반환한다. executor coroutine은 이 task를 await하고,
  handler 실행 중 `.result()`로 기다리는 bridge를 만들지 않는다.
- `task_t<T>`는 다중 await를 지원한다. pending task에 여러 continuation이 붙어도 모두
  재개되어야 한다.
- task completion은 first-complete-wins이다. 두 번째 완료 시도는 결과를 덮어쓰지 않는다.
- `coroutine_executor_t`는 `boost::asio::thread_pool`, `boost::asio::co_spawn`,
  `boost::asio::awaitable`을 runtime 구현 안에서만 사용한다.
- handler coroutine executor 기본 worker 수는 CPU 수 기반이다. `handler_coroutine_workers(n)`
  설정으로 조정할 수 있고, 설정은 executor 최초 생성 전에 반영된다.
- call object public header는 submit 계약만 제공하고, pending queue와 runtime submitter
  구현 타입을 노출하지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-contract
ctest --test-dir framework/languages/cpp/build -L framework-unit
```

### Goal 4. App, Host, Configuration, Logging

목표는 standalone C++ framework가 application host/runtime 역할을 시작하고 종료할 수
있게 만드는 것이다.

구현 항목:

- `app_t::create()`
- `app_t::services()`
- `app_t::handlers()`
- `app_t::use_zlink()`
- `app_t::config()`
- `app_t::logging()`
- `app_t::run(argc, argv)`
- `app_t::stop()`
- process exit code 반환
- signal handling
- graceful shutdown
- startup validation
- JSON config loader
- environment variable loader
- 자체 CLI args parser
- public logging API
- 내부 logging backend 숨김
- `logger_t<TCategory>`와 `logger_factory_t`
- console, file, rotating file, callback sink
- async logging option과 overflow policy
- `spdlog` backend는 public header에 노출하지 않음

완료 기준:

- 사용자는 native context 생성/종료 순서를 알 필요가 없다.
- `run()`은 종료 코드를 반환한다.
- shutdown 중 새 submit은 실패 result로 닫힌다.
- JSON, env, CLI config가 같은 configuration model로 합쳐진다.
- host/app public header는 runtime owner, signal backend, native context owner 구현을
  노출하지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit
ctest --test-dir framework/languages/cpp/build -L framework-regression
```

### Goal 5. DI Container And Scope Lifetime

목표는 `.NET` framework의 scope 개념을 C++ 자체 DI와 RAII lifetime으로 구현하는 것이다.

구현 항목:

- `service_collection_t`
- `service_provider_t`
- `service_scope_t`
- `service_lifetime_t::singleton`
- `service_lifetime_t::scoped`
- `service_lifetime_t::transient`
- `add_singleton<T>()`
- `add_singleton<T>(unique_ptr<T>)`
- `add_scoped<T>()`
- `add_transient<T>()`
- `add_factory<T>()`
- duplicate registration validation
- shutdown 중 resolve 금지
- handler invocation scope
- stream session scope
- spot activation scope
- entry spot scope
- actor creation scope

완료 기준:

- scoped service는 자기 scope 밖으로 재사용되지 않는다.
- stream session scope는 session close cleanup 뒤 닫힌다.
- Spot activation scope는 Spot cleanup 뒤 닫힌다.
- actor factory dependency는 actor creation 완료 뒤 정리된다.
- 외부 DI 타입이 public header에 드러나지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R DI
ctest --test-dir framework/languages/cpp/build -L framework-regression -R scope
```

### Goal 6. Runtime Integration And Dispatch Projection

목표는 zlink C++ binding과 CAPI dispatch callback을 framework runtime 내부로 숨기고,
사용자가 typed handler만 구현하게 만드는 것이다.

구현 항목:

- `zlink::context_t` lifecycle owner
- channel socket lifecycle owner
- stream socket lifecycle owner
- discovery lifecycle owner
- registry lifecycle owner
- spot node lifecycle owner
- CAPI dispatch callback 등록
- event kind별 recv 처리
- typed handler projection
- CAPI timer event projection
- core ordering 보존
- framework offload executor
- runtime drain
- native handle owner 숨김
- transport abstraction
- endpoint URI validation
- security option projection
- TCP, IPC, TLS, WebSocket transport support through zlink core
- framework core 안에 별도 network I/O stack을 추가하지 않음
- WebSocket 지원을 위해 `Boost.Beast`를 framework core에 직접 붙이지 않음
- PGM은 C++ framework 지원 범위에서 제외

완료 기준:

- public API는 native C handle, socket recv, poller slot을 노출하지 않는다.
- handler 등록 방식으로 channel, stream, spot, timer event를 처리한다.
- CPU-bound handler는 offload executor에서 실행 가능하다.
- offload executor는 shutdown에서 drain된다.
- 모든 channel handler, route handler, SPOT handler, stream session callback은 coroutine
  executor를 통과해 실행된다.
- coroutine executor는 Asio 기반 구현이지만 public API에는 Boost.Asio 타입을 노출하지 않는다.
- handler와 client 코드는 transport 종류를 직접 알 필요가 없다.
- framework core는 zlink core transport 의미를 감싸며 별도 event loop를 만들지 않는다.
- CAPI dispatch projection, recv/drain 순서, native handle owner는 `src/runtime/*`
  내부 구현으로만 존재한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R runtime
ctest --test-dir framework/languages/cpp/build -L framework-integration
```

### Goal 7. Handler Registry And Serializer

목표는 typed payload와 handler owner를 framework가 관리하고, 사용자는 member function
handler를 등록하는 표면을 제공하는 것이다.

구현 항목:

- `handler_registry_t`
- `on_request<THandler, TRequest, TReply>()`
- `on_send<THandler, TMessage>()`
- `on_event<THandler, TEvent>()`
- raw handler 확장 표면
- handler owner DI resolve
- packet name 기본값은 payload 타입 이름
- topic routing
- typed deserialize
- handler invoke
- handler exception mapping
- `serializer_registry_t`
- `serializer_t<T>`
- JSON serializer 기본값
- custom serializer 등록
- callback에서 받은 `message_t`와 payload view는 callback 동안만 유효한 borrowed value로 처리
- callback 밖 보관 시 copy 또는 move 정책 제공

완료 기준:

- `handler_registry_t`와 `serializer_registry_t` public header는 descriptor map,
  serializer map, DI resolve 순서, monitoring event 생성 구현을 노출하지 않는다.
- handler/serializer template 코드는 shape 검사와 type-erased runtime 호출로 제한된다.
- handler registry runtime state는 `src/runtime/handlers/*`, serializer registry runtime
  state는 `src/runtime/codecs/*`에 있다.
- 등록되지 않은 handler owner를 암묵 생성하지 않는다.
- decode 실패는 `payload_decode_failed`로 보고하고 runtime을 죽이지 않는다.
- handler 예외는 framework error와 monitoring event로 정리된다.
- public handler callback signature에 외부 library 타입을 강제하지 않는다.
- handler가 payload를 보관해야 하는 경우 framework가 copy/move boundary를 분명히 제공한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R handler
ctest --test-dir framework/languages/cpp/build -L framework-unit -R serializer
```

### Goal 8. Channel Messaging

목표는 request/reply, send, publish/subscribe, outbound client를 `.NET` framework와 같은
사용성으로 제공하는 것이다.

구현 항목:

- `channel_builder_t`
- capability builder:
  - server
  - client
  - publisher
  - subscriber
- `bind(...)`
- `connect(...)`
- `use_discovery(...)`
- same capability 안에서 discovery와 manual 연결 혼합 금지
- local server capability ingress 기준 request/send dispatch
- outbound dealer reply correlation
- outbound-only host
- manual capability connection sample path
- `message_bus_t`
- `request_client_t`
- `publisher_t`
- 일반 event publish
- request timeout
- pending queue
- send-ready drain
- disconnected result
- queue full result
- graceful drain

완료 기준:

- channel messaging 기본 호출은 channel name 기준이다.
- outbound receive path는 reply correlation으로만 처리한다.
- `ROUTER -> DEALER` 임의 push를 channel messaging 공용 계약에 넣지 않는다.
- pending queue 한도 초과는 `request_rejected`로 실패한다.
- outbound-only host는 server ingress 없이 client/publisher capability만으로 실행된다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-integration -R channel
ctest --test-dir framework/languages/cpp/build -L framework-regression -R channel
```

### Goal 9. Backpressure, Flow Control, Reliability

목표는 send readiness와 pending queue를 framework 내부에서 관리하고, 호출자에게 명확한
결과를 제공하는 것이다.

구현 항목:

- nonblocking send
- send-ready runtime integration
- HWM awareness
- queue depth 조회
- bounded pending queue
- timeout 처리
- disconnected 처리
- shutdown 처리
- explicit retry hook
- dead-letter extension point
- idempotency key hook
- lifecycle cancellation result mapping
- graceful close와 drain

완료 기준:

- public non-blocking 옵션으로 책임을 사용자에게 넘기지 않는다.
- timeout 전 send-ready가 오면 pending 작업을 drain한다.
- timeout까지 send-ready가 없으면 `timeout`으로 실패한다.
- shutdown 중 pending 작업은 graceful drain 또는 `shutdown` 실패로 닫힌다.
- target lifecycle 때문에 완료할 수 없는 작업은 `closed` 또는 `shutdown`으로 닫힌다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R backpressure
ctest --test-dir framework/languages/cpp/build -L framework-regression -R reliability
```

### Goal 10. SPOT Runtime

목표는 SPOT node, user Spot, Entry Spot, spot-to-spot messaging을 framework 표면으로
제공하는 것이다.

구현 항목:

- `spot_node_builder_t`
- `contracts/spots/*` public owner와 `src/runtime/spots/*` runtime owner 분리
- named spot factory 등록
- `spot_name` 기준 생성
- `spot_rid -> spot_name` 조회
- `spot_context_t`
- `spot_context_t::publish(...)`
- `spot_context_t::request_to(...)`
- spot-to-spot send/request
- Entry Spot
- `SpotRid`, `NodeRid` public view
- packet registry
- `spot_context_t::handlers()`
- `spot_handler_registry_t`
- `add_handler<THandler>()`
- `add_subscribe<THandler>(topic)`
- `spot_actor_send_context_t`
- `spot_actor_request_context_t`
- `spot_actor_reply_options_t`
- `add_actor_join<THandler>()`
- `add_actor_packet<THandler>()`
- `spot_actor_change_kind_t`
- `spot_actor_change_result_t`
- `add_post_actor_joined<THandler>()`
- `add_actor_left<THandler>()`
- `add_actor_disconnected<THandler>()`
- SPOT handler typed invocation
- SPOT handler DI resolve
- SPOT handler serializer decode/encode
- actor factory 기본 구조
- SPOT discovery 설정
- Registry-backed Spot lookup
- custom Spot resolver validation
- duplicate resolver validation
- ambiguous route channel validation

완료 기준:

- SPOT public header는 activation table, native dispatch router, subscription pump,
  spot packet dispatcher 구현 타입을 노출하지 않는다.
- 일반 application handler와 client는 channel name과 topic을 먼저 사용한다.
- `rid` 직접 지정은 spot-to-spot 경로와 Entry Spot join 같은 actor lifecycle 경로에 제한한다.
- SPOT node lifecycle은 app host가 관리한다.
- core SPOT dispatch ordering이 typed handler 표면에 유지된다.
- `.NET` `Context.Handlers.AddHandler`, `AddActorJoin`, `AddActorPacket`에 해당하는
  등록 표면이 C++ `spot_context_t::handlers()`에 있어야 한다. C++에는 assembly reflection이
  없으므로 handler type은 명시한다. 다만 spot, actor, request, reply type은 handler class의
  `spot_type`, `actor_type`, `request_type`, `reply_type` alias에서 framework가 읽는다.
  샘플 Spot 등록부가 `TSpot`, `TActor`, `TRequest`, `TReply`를 반복해서 나열하면 완료 기준을
  통과하지 못한다.
- 일반 Spot packet handler와 subscription handler도 `.NET`처럼 Spot instance와 DTO를 함께
  받아야 한다. 필요한 타입 정보는 handler class에 한 번만 선언하고, 등록부는
  `add_handler<THandler>()`, `add_subscribe<THandler>(topic)` 형태를 기본으로 한다.
- 등록된 SPOT handler는 descriptor 조회로 끝나면 안 된다. framework runtime이
  `serializer_registry_t`로 payload를 DTO로 바꾸고, `service_provider_t`에서 handler
  owner를 resolve한 뒤 typed handler를 호출해야 한다.
- actor lifecycle handler는 `.NET`의 `ZLinkSpotActorChangeResult`와 같은 의미의
  `spot_actor_change_result_t`를 받아야 한다. 단, actor disconnected handler는 `.NET`처럼
  Spot instance와 actor만 받으며 change result를 받지 않는다. C++은 reflection으로 handler
  interface의 `TSpot`을 자동 reflection으로 추론할 수 없으므로 handler class alias로
  보완한다.
- actor join/packet handler도 `.NET`처럼 Spot instance와 actor를 함께 받아야 한다.
  actor packet handler는 `spot_actor_send_context_t` 또는 `spot_actor_request_context_t`로
  packet name, content type, metadata, reply option을 확인할 수 있어야 한다. C++은
  reflection이 없으므로 handler class alias로 spot/actor/request/reply type을 명시하고,
  Spot 등록부에서는 handler type만 나열한다.
- Entry Spot에서 actor가 보낸 packet은 일반 Spot packet이 아니라 actor packet handler로
  등록한다. handler는 `EntrySpot`, actor, request context, DTO를 받고, framework는
  deserialize와 reply serialization을 내부에서 처리한다.
- Play sample smoke는 handler 객체를 직접 호출하는 확인만으로 완료로 보지 않는다.
  join/packet/lifecycle handler는 `spot_context_t::handlers()` dispatch 경로로 실행되어야 한다.
- stage 같은 상위 wrapper가 `SpotRid`, `NodeRid`, packet registry, outbound channel client,
  state와 domain method를 보관할 수 있다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-integration -R spot
ctest --test-dir framework/languages/cpp/build -L framework-regression -R spot
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_sample_parity|sample_smoke_sample_cpp_framework_(bingo|tictactoe)_play' --output-on-failure
```

### Goal 11. SPOT Timer

목표는 CAPI timer를 기반으로 `.NET` framework timer와 같은 기능성을 C++ framework에서
제공하는 것이다.

구현 항목:

- `timer_t`
- `contracts/timers/*` public owner와 `src/runtime/timers/*` runtime owner 분리
- `timer_options_t`
- `timer_overrun_policy_t`
- `timer_tick_t`
- `spot_context_t::add_timer<THandler>(...)`
- CAPI timer creation and lifecycle
- CAPI timer dispatch event projection
- `fire_count` 기반 skipped tick 계산
- `delivery_index`
- `scheduled_index`
- `scheduled_elapsed`
- `started_elapsed`
- `delay`
- `skipped_ticks`
- `skip_late_ticks`
- `catch_up_bounded`
- `delay_next_tick`
- same timer instance 재진입 금지
- user Spot timer는 core SPOT dispatch boundary 적용
- Entry Spot timer는 Entry Spot 전체 전역 직렬화 금지
- timer handler exception monitoring
- `stop_on_unhandled_exception`
- shutdown timer cancel/drain

완료 기준:

- timer public header는 native timer token, CAPI dispatch event, `fire_count` drain loop
  구현 타입을 노출하지 않는다.
- 사용자는 native timer handle, poller slot, timer recv 순서를 직접 다루지 않는다.
- `fire_count` 누적값으로 missed tick이 계산된다.
- user Spot timer는 같은 Spot의 packet/subscription/channel reply 순서 정책을 따른다.
- Entry Spot timer는 Entry Spot 전체를 막지 않는다.
- timer failure event는 snapshot interval을 기다리지 않고 발생한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R timer
ctest --test-dir framework/languages/cpp/build -L framework-integration -R timer
ctest --test-dir framework/languages/cpp/build -L framework-regression -R timer
```

### Goal 12. STREAM Framework

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
- `on_connected(...)`
- `on_disconnected(...)`
- `on_error(...)`
- packet callback
- `on_packet(...)`
- packet reply
- `stream_t::write_packet(...)`
- `stream_write_call_t`
- header encode/decode
- semantic validation
- request sequence
- packet name
- content type
- correlation id
- metadata
- codec field
- flags
- write backpressure
- session ordering
- close cleanup
- `on_error(...)` transport error projection

완료 기준:

- raw stream session, 사용자 정의 Header framing, 임의 byte stream dispatch는 core public 표면에 넣지 않는다.
- Header validation 실패 packet은 application handler로 넘기지 않는다.
- 같은 stream session lifecycle callback과 packet callback은 직렬이다.
- `on_error(...)`는 application handler 예외를 받지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-integration -R stream
ctest --test-dir framework/languages/cpp/build -L framework-regression -R stream
```

### Goal 13. ActorGateway Session Relay

목표는 STREAM session과 actor를 ActorGateway로 bind/relay하는 server-side runtime 기능을
구현하는 것이다.

구현 항목:

- `stream.attach_actor_gateway(spot_node_name)`
- `actor_ref_t`
- `session_actor_manager_t`
- `session_actor_t`
- `actor_context_t`
- `bound_session_t`
- `actor_context_t::join_spot<TRequest, TReply>(...)`
- `actor_context_t::join_entry_spot(...)`
- `.NET` `ZLinkActorJoinResult<TReply>`와 같은 의미의
  `actor_join_result_t<TReply>`
- actor factory 등록
- actor id/type 기반 create, find, get-or-create
- local actor handle bind
- remote actor ref bind
- session actor relay
- bound session push
- actor disconnect cleanup
- actor type mismatch error
- duplicate actor error
- remote ActorGateway locator codec 숨김
- session binding cleanup
- relay/send caller payload non-consuming policy
- remote frame이 필요할 때 framework runtime이 별도 buffer 생성

완료 기준:

- STREAM session에서 actor로 보내는 packet은 application route mesh channel을 만들지 않는다.
- session은 `actor_ref_t` 또는 logical actor handle을 bind한다.
- actor push는 `bound_session_t`를 통해 내려간다.
- actor가 Spot에 join하면 결과는 actor ref와 typed reply를 함께 돌려준다. C++에서는
  `.NET`의 `ZLinkActorJoinResult<TReply>`에 해당하는 `actor_join_result_t<TReply>`로
  표현한다.
- actor가 Entry Spot에 join하면 `.NET`처럼 actor ref만 돌려준다.
- stream close는 session binding cleanup만 수행하고 actor current Spot을 바꾸지 않는다.
- actor-session binding은 Registry나 sample-only metadata store에 저장하지 않는다.
- `relay(...)`와 `send(...)`는 caller payload를 소비하지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-integration -R ActorGateway
ctest --test-dir framework/languages/cpp/build -L framework-regression -R actor
```

### Goal 14. Registry And Topology

목표는 embedded registry, remote query, topology 조회를 framework runtime에 통합하는 것이다.

구현 항목:

- embedded registry bootstrap
- registry bind/peer 설정
- registry heartbeat/broadcast 설정
- registry query client
- topology query
- service summary
- Spot remote address lookup 기본값
- custom Spot resolver
- duplicate resolver rejection
- ambiguous route channel validation
- monitoring snapshot source

완료 기준:

- Registry는 Spot remote address 조회 기본값으로 사용한다.
- session actor relay hot path의 actor route store로 Registry를 쓰지 않는다.
- Spot discovery 없이 Registry Spot 기본값을 켜면 validation 오류다.
- route channel이 둘 이상이면 resolver channel 이름을 명시해야 한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-integration -R registry
ctest --test-dir framework/languages/cpp/build -L framework-regression -R registry
```

### Goal 15. Monitoring And Observability

목표는 runtime event를 typed event와 등록 표면으로 올리고, 운영자가 상태를 볼 수 있게
하는 것이다.

구현 항목:

- monitoring builder
- runtime event enum
- typed event payload structs
- socket monitor event projection
- discovery event projection
- registry snapshot diff
- spot snapshot diff
- stream events
- actor/session events
- spot timer immediate failure event
- source name
- timestamp
- severity
- node name
- correlation id
- health status
- tracing hook
- logging integration

완료 기준:

- socket/discovery/registry/spot runtime event를 typed event로 받을 수 있다.
- timer handler failure는 snapshot diff interval을 기다리지 않는다.
- handler exception과 transport error가 구분된다.
- public callback payload에 exception 객체 자체를 직접 싣지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R monitoring
ctest --test-dir framework/languages/cpp/build -L framework-integration -R monitoring
```

### Goal 16. Hosted Services And Module System

목표는 application 기능을 module과 hosted service로 구성하고, lifecycle을 host와
연동하는 것이다.

구현 항목:

- `module_t`
- `framework_module_contract_t`
- `app_t::add_zlink_framework(std::function<void(zlink_framework_options_t &)>)`
- `zlink_framework_options_t`
- `handler_options_builder_t`
- `codec_options_builder_t`
- `discovery_options_builder_t`
- `client_server_channel_builder_t`
- `publisher_channel_builder_t`
- `spot_node_options_builder_t`
- `stream_node_options_builder_t`
- module service registration
- module handler registration
- module runtime configuration
- module observability extension
- stage wrapper module pattern
- stage wrapper state와 domain method 보관
- stage wrapper packet registry
- stage wrapper outbound channel client 접근
- stage wrapper timer option mapping
- `hosted_service_t`
- start lifecycle
- stop lifecycle
- shutdown order
- subscriber loop
- heartbeat
- discovery maintenance
- retry worker
- periodic cleanup

완료 기준:

- module은 handler, service, runtime 구성, observability 확장을 한 곳에 묶을 수 있다.
- `.NET`의 `AddZLinkFramework(options => ...)`에 해당하는 C++ 고수준 진입점은
  `app_t::add_zlink_framework(options_callback)`다. 여기서 `options_callback`은
  `zlink_framework_options_t`를 받는다. C++에는 assembly reflection이 없으므로
  `.NET`의 `AddHandlersFromAssemblyOf(...)`만 그대로 옮기지 않고,
  `options.handlers().add<THandler>(group_name)`처럼 handler 타입을 명시해서 등록한다.
- `options.codecs().add_json()`은 JSON codec 사용만 선언한다. 사용자가 message type을 codec
  설정에 모두 나열하지 않는다. request/reply serializer 등록은 handler type의 `request_type`,
  `reply_type`에서 framework가 자동으로 처리한다.
- handler 생성자 의존성은 `dependency_list_t<Dep...>`와 DI 생성자 주입으로 처리한다.
  handler를 default constructor에 맞추기 위해 application service를 handler 내부에서 만들거나
  참조를 `shared_ptr`로 억지 변환하지 않는다.
- handler가 다른 channel로 request를 보낼 때는
  `co_await client.request<TReply>(channel_name, request).submit()` 형태를 사용한다.
  `request_to_channel<TRequest, TReply>(...).submit().result().value()`처럼 request/reply 타입을
  모두 드러내고 task 결과를 직접 꺼내는 코드는 샘플과 사용자 guide에 노출하지 않는다.
- application sample과 guide 예제에서는 샘플 namespace에
  `using zlink::framework::task_t;`를 두고 handler signature를
  `task_t<TReply> handle(...)`처럼 쓴다. `zlink::framework::task_t<TReply>`를 반복해서
  async 의미를 가리는 코드는 샘플 완료 기준에 맞지 않는다.
- 샘플의 framework 설정은 아래 수준으로 읽혀야 한다. 설정을 읽는 사람은 discovery를 쓰고,
  client-server channel 두 개를 구성하며, API channel에 handler group을 붙인다는 사실을
  바로 알 수 있어야 한다.

```cpp
app.add_zlink_framework ([&](zlink::framework::zlink_framework_options_t &options) {
  options.handlers ()
    .add<authenticate_player_handler_t> ("api")
    .add<match_bingo_api_handler_t> ("api");

  options.codecs ().add_json ();

  options.discovery ().add (topology.registry_router_endpoint);

  options.client_server_channel (sample_names_t::api_channel)
    .server (topology.api_channel_endpoint)
    .handler_group ("api");

  options.client_server_channel (sample_names_t::play_channel)
    .client ();
});
```

- `module_t`, `framework_module_contract_t`, handler용 DI factory, handler signature registration,
  monitoring channel 문자열, serializer smoke 검증, message type을 모두 나열하는 codec 등록은
  framework 내부 구현 또는 낮은 수준 확장 구현 세부다. 이것들이 샘플 `Program` 역할의
  `main.cpp`, role `*HostFactory`, 일반 사용자 설정 예제에 노출되면 완료가 아니다.
- sample `Program` 역할의 `main.cpp`와 role `*HostFactory`에는 handler registration,
  handler용 DI factory, logging sink, serializer smoke 검증을 두지 않는다. role `*HostFactory`는
  완성된 `app_t`를 반환하고, role별 세부 구성은 위 options builder 표면으로 표현한다.
- `zlink_framework_options_t`의 일반 사용자 표면에는 람다 기반 `enable_server`,
  `enable_client`, `channel(...)`, `use_zlink(...)` 우회 설정을 두지 않는다. 이런 API는
  framework 내부 runtime builder나 낮은 수준 확장 테스트에서만 사용한다. 샘플과 사용자 예제는
  `client_server_channel(...).server(...).client(...).handler_group(...)`,
  `publisher_channel(...).bind(...)`, `spot_node(...).bind(...)`,
  `stream_node(...).bind(...).packet_session(...)` 같은 fluent options builder만 사용한다.
- `stream_node` fluent builder는 `bind`와 `packet_session`이 모두 지정된 뒤에만 내부 stream
  builder에 반영한다. 체인 중간의 불완전한 stream 설정이 runtime에 등록되면, 사용자는 정상적인
  fluent 호출을 했는데도 `STREAM requires bind endpoint and packet session` 같은 저수준 오류를
  보게 되기 때문이다.
- hosted service는 app startup/shutdown과 함께 시작하고 종료한다.
- shutdown 중 hosted service가 새 submit을 무기한 만들지 않는다.
- stage wrapper는 게임 기능 전용 타입이 아니라 SPOT 위에 상위 모델을 감싸는 host/runtime 패턴이다.
- stage wrapper가 timer를 제공해도 tick metadata, overrun policy, handler exception monitoring을 숨기지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-unit -R module
ctest --test-dir framework/languages/cpp/build -L framework-integration -R hosted
```

### Goal 17. C++ Stream Connector

목표는 C++ framework와 별도 배포되는 client-side Stream Connector를 구현하는 것이다.

구현 항목:

- `zlink::stream_connector` namespace
- `zlink/stream_connector.hpp`
- `connector_t`
- `connector_factory_t`
- Asio 기반 TCP transport
- `codec_registry_t`
- explicit connect
- graceful close
- connection state event
- reconnect
- heartbeat
- packet send
- typed send
- typed request/reply
- callback submit
- coroutine submit
- request timeout
- pending request correlation
- packet callback receive
- manual dispatch mode
- immediate dispatch mode
- metadata
- payload compression flag 처리
- max send payload size
- max metadata size
- connector instance별 독립 실행
- JSON 기본 ON
- MessagePack 기본 OFF
- Protobuf 기본 OFF
- LZ4 기본 ON
- unsupported codec error

완료 기준:

- connector는 framework sample이나 framework target이 아니다.
- 기본 runtime은 `zlink::stream_connector` target으로 사용하고, typed auto codec helper는
  같은 배포물 안의 `zlink::stream_connector_codecs` target으로 선택해 사용한다.
- MessagePack, Protobuf, LZ4는 사용자가 켰을 때만 dependency를 요구한다.
- connector public contract header와 `connector/src/runtime/*` 구현이 물리적으로
  분리된다.
- connector public header는 receive loop, transport connection, pending request table,
  frame sender 구현 타입을 노출하지 않는다.
- connector runtime은 raw fd나 OS별 socket API가 아니라 Asio socket, resolver, timer를
  내부 구현으로 사용한다.
- manual dispatch에서는 callback이 `dispatch()` 호출 경로에서 실행된다.
- immediate dispatch에서는 별도 manual dispatch 없이 callback이 실행된다.
- `connect()`는 단순 상태 전환이 아니라 실제 endpoint 연결을 수행한다.
- `request(...).submit()`은 STREAM server response frame을 받아 pending request table의
  correlation을 해소한다.
- `send(...).submit()`은 실제 STREAM server로 frame을 기록하고, 전송 실패를 error로
  돌려준다.
- connector regression에는 local test runtime 검증과 별도로 실제 framework STREAM
  endpoint에 붙는 end-to-end test를 둔다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L connector-unit
ctest --test-dir framework/languages/cpp/build -L connector-integration
ctest --test-dir framework/languages/cpp/build -L connector-e2e
```

### Goal 18. Unreal Stream Connector

목표는 Unreal 프로젝트에서 바로 사용할 수 있는 Unreal 전용 connector를 구현하는 것이다.

구현 항목:

- Unreal plugin/module packaging
- `ZLinkStreamConnector.uplugin`
- `ZLinkStreamConnector.h`
- `UObject` 또는 subsystem lifecycle owner
- `FString`
- `FName`
- `TArray<uint8>`
- `TMap<FString, FString>`
- Blueprint callable connect
- Blueprint callable close
- Blueprint callable send
- Blueprint callable request
- Blueprint assignable connection state event
- Game Thread callback dispatch
- Tick 또는 subsystem update manual dispatch
- Unreal logging category
- Unreal build system dependency
- Unreal Automation Test `ZLink.StreamConnector.Loopback`
- Unreal `FSocket` loopback server 기반 send/request/push 검증
- PIE 종료 graceful close
- map unload graceful close
- game instance shutdown graceful close
- JSON 기본 포함
- MessagePack, Protobuf 선택 build option

완료 기준:

- Unreal connector는 일반 C++ connector public API를 그대로 노출하는 wrapper가 아니다.
- Unreal connector는 일반 C++ connector Asio runtime을 내부에서 감싸지 않고 Unreal의
  `Sockets`/`Networking` 모듈로 transport를 구현한다.
- public API는 Unreal 타입과 thread model을 따른다.
- callback은 Game Thread에서 실행된다.
- Unreal public API에는 `task_t`, `submit()`, `co_await` 기반 coroutine 표면을 두지 않고,
  Blueprint delegate와 native multicast delegate callback만 제공한다.
- Unreal 사용자가 codec 산출물을 따로 가져오지 않아도 JSON 기본 사용이 가능하다.
- Unreal `Public/` header는 Unreal 전용 contract와 facade만 노출하고, connection,
  codec registry, thread dispatch 구현은 `Private/`에 둔다.
- Unreal public API는 일반 C++ connector runtime 구현 타입을 그대로 노출하지 않는다.
- Unreal Automation Test는 실제 `FSocket` loopback server와 연결해 send/request/push
  callback을 검증한다.
- 일반 C++ CTest smoke는 Unreal Engine 없는 환경에서 public API shape와 source compile만
  확인하며, 실제 Unreal connector 완료 판정으로 쓰지 않는다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L unreal-connector-compile
ctest --test-dir framework/languages/cpp/build -L unreal-connector-smoke
UnrealEditor-Cmd <TestProject>.uproject -ExecCmds="Automation RunTests ZLink.StreamConnector; Quit" -unattended -nop4 -nosplash -NullRHI
```

### Goal 19. Review Samples

목표는 framework 전반 동작을 사용자가 리뷰하기 쉬운 샘플로 고정하는 것이다.

구현 항목:

- `Bingo`
  - `.NET` 샘플과 같은 `Shared`, `Client`, `Server/Registry`, `Server/Api`,
    `Server/Play`, `Server/Session` 역할 분리
  - `.NET` Bingo와 같은 packet 이름과 message contract 흐름
  - app/host
  - DI
  - channel request/reply
  - session packet dispatch
  - outbound-only host
  - manual connection
  - publish/subscribe
  - 일반 event publish
  - callback submit
  - coroutine submit
  - handler error
  - user Spot
  - SPOT timer
  - monitoring
  - graceful shutdown
  - offload handler
- `TicTacToe`
  - `.NET` 샘플과 같은 `Shared`, `Client`, `Server/Registry`, `Server/Api`,
    `Server/Play`, `Server/Session` 역할 분리
  - `.NET` TicTacToe와 같은 packet 이름과 message contract 흐름
  - STREAM endpoint
  - ActorGateway attach
  - Entry Spot
  - actor factory
  - session actor bind
  - relay
  - bound session push
  - actor join/move
  - disconnect cleanup

완료 기준:

- `Bingo`는 `.NET` Bingo와 같은 session stream 역할을 포함한다.
- `TicTacToe`가 STREAM과 ActorGateway 기반 actor/session relay 기준 샘플이다.
- `Server/Session` role은 `.NET`과 같이 `Sessions/*`와 `Sessions/Handlers/*` 아래에
  session class와 session packet handler를 둔다. `main.cpp` 안의 smoke 로직만으로
  session dispatch, authenticate, actor bind, relay를 대신하면 완료로 보지 않는다.
- 샘플 이름에 별도 접미사를 붙이지 않는다.
- 역할별 sample smoke test가 CTest에 등록된다.
- `.NET` 샘플과 맞춰야 하는 packet 이름과 핵심 handler 흐름이 contract test에 등록된다.
- `Client` 샘플은 서버 handler를 직접 호출하지 않고 `zlink::stream_connector`를 통해
  `connect -> request/send submit -> notification callback` 흐름을 사용한다.
- `Client` 샘플은 `zlink/stream_connector/codecs/auto_codec.hpp`와
  `zlink::stream_connector::codecs::{send,request,on}`을 사용한다. raw payload 조립,
  sample-only serializer helper, 직접 JSON parse로 connector codec을 우회하면 안 된다.
- sample contract DTO는 C++ JSON serializer hook인 `to_json`/`from_json`만 제공한다.
  application code가 `nlohmann::json::parse`로 field를 직접 읽거나
  `to_stream_payload`/`from_stream_payload` 같은 sample-only 변환 함수를 호출하면 안 된다.
- client sample smoke가 단순 컴파일/표면 검증으로만 통과하면 완료로 보지 않는다.
  Bingo와 TicTacToe client executable은 실제 server process와 붙어 request reply와 push
  notification을 검증해야 한다.
- 서버 쪽 로그는 파일로 남겨서 bind, receive, reply, push 흐름을 확인할 수 있어야 한다.
  CTest는 client executable 성공만 보지 않고, 로그 파일에 기대 packet 이름과 최소
  receive/reply/push 횟수가 기록됐는지도 검증해야 한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke
ctest --test-dir framework/languages/cpp/build -L framework-sample-parity
ctest --test-dir framework/languages/cpp/build -L framework-sample-client-e2e
ctest --test-dir framework/languages/cpp/build -L framework-sample-log
```

### Goal 20. Final Parity And Regression Gate

목표는 모든 draft 항목이 구현됐는지 최종 확인하고, `.NET` framework와 같은 기능 축을
회귀 테스트로 고정하는 것이다.

구현 항목:

- public header compile contract
- app/host regression
- DI/module regression
- channel messaging regression
- async surface regression
- handler execution regression
- STREAM regression
- SPOT regression
- timer regression
- ActorGateway relay regression
- Registry regression
- monitoring regression
- connector regression
- Unreal connector compile/smoke
- sample smoke
- public docs and sample code alignment
- extension regression

완료 기준:

- Goal 1부터 Goal 19까지의 완료 기준이 충족된다.
- Goal 21에서 다룰 extension boundary 항목을 제외하고 draft 추적표에 남은 구현 항목이 없다.
- CTest label 전체가 통과한다.
- Goal 1부터 Goal 20까지 각 goal에서 POSD 기반 리팩토링을 최소 한 번씩 수행했다.
- POSD 리팩토링 기록이 20개 이상 남아 있다.
- framework public header에 GoogleTest, GoogleMock, spdlog, fmt, codec 외부 타입이
  불필요하게 노출되지 않는다.
- framework, connector, Unreal connector public header가 runtime implementation header를
  include하지 않는다.
- `contracts/detail/*`에는 type trait, concept check, facade forwarding만 있고 queue,
  executor, dispatch projection, frame codec 구현이 없다.
- C++ framework 사용성은 `.NET` framework와 같은 application model로 설명된다.
- core framework 밖 extension은 Goal 21에서 dependency isolation과 extension point 기준을
  닫고, 그 뒤 전체 21개 goal 최종 감사를 다시 수행한다.

검증:

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

### Goal 21. Extension Boundaries

목표는 core framework 밖의 확장 영역을 독립 산출물이나 명확한 extension point로 닫는
것이다.

구현 항목:

- metrics extension
- tracing extension
- Kafka bridge
- gRPC bridge
- HTTP gateway
- advanced retry extension
- dead-letter storage extension
- FlatBuffers integration
- YAML configuration extension
- custom codec extension point
- custom transport integration boundary
- extension target naming
- extension dependency isolation
- extension test labels

완료 기준:

- core framework target은 Kafka, gRPC, HTTP, YAML, FlatBuffers dependency를 기본으로
  끌고 오지 않는다.
- 각 extension은 core framework public API 위에서 붙고 native handle 또는 internal
  runtime 구조에 의존하지 않는다.
- extension이 추가되어도 channel, SPOT, STREAM, ActorGateway, connector 기본 사용성은
  변하지 않는다.
- metrics와 tracing은 monitoring event와 logging 정책을 우회하지 않고 확장한다.
- advanced retry와 dead-letter는 ordering, 중복 처리, idempotency key 의미를 문서화한
  뒤 구현한다.

검증:

```bash
ctest --test-dir framework/languages/cpp/build -L framework-extension
git diff --check -- framework/languages/cpp
```

## 6. Draft 추적표

| Draft 문서 | 구현 goal |
|------------|-----------|
| [cpp-framework-implementation-plan.ko.md](./cpp-framework-implementation-plan.ko.md) | Goal 1-21 실행 추적 |
| [cpp-framework-posd-refactoring-log.ko.md](./cpp-framework-posd-refactoring-log.ko.md) | Goal 1-21 POSD 리팩토링 기록 |
| [README.ko.md](./README.ko.md) | Goal 1-21 |
| [cpp-framework-policy.ko.md](./cpp-framework-policy.ko.md) | Goal 1-21 |
| [cpp-framework-interfaces.ko.md](./cpp-framework-interfaces.ko.md) | Goal 1-16, Goal 20, Goal 21 |
| [handler-interfaces.ko.md](./handler-interfaces.ko.md) | Goal 7, Goal 8, Goal 10-13 |
| [cpp-channel-messaging.ko.md](./cpp-channel-messaging.ko.md) | Goal 8, Goal 9 |
| [channel-messaging-samples.ko.md](./channel-messaging-samples.ko.md) | Goal 8, Goal 19 |
| [cpp-spot.ko.md](./cpp-spot.ko.md) | Goal 10, Goal 11, Goal 13 |
| [spot-samples.ko.md](./spot-samples.ko.md) | Goal 10, Goal 11, Goal 13, Goal 19 |
| [stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md) | Goal 10, Goal 11, Goal 16 |
| [actor-gateway-session-relay.ko.md](./actor-gateway-session-relay.ko.md) | Goal 13, Goal 19 |
| [cpp-stream.ko.md](./cpp-stream.ko.md) | Goal 12, Goal 13 |
| [stream-open-items.ko.md](./stream-open-items.ko.md) | Goal 12 |
| [stream-samples.ko.md](./stream-samples.ko.md) | Goal 12, Goal 13, Goal 19 |
| [cpp-stream-connector.ko.md](./cpp-stream-connector.ko.md) | Goal 17, Goal 18 |
| [cpp-monitoring.ko.md](./cpp-monitoring.ko.md) | Goal 11, Goal 15 |
| [cpp-registry.ko.md](./cpp-registry.ko.md) | Goal 10, Goal 13, Goal 14 |

## 7. 기능 축 추적표

| 기능 축 | 구현 goal | 회귀 테스트 축 |
|---------|-----------|----------------|
| C++20 baseline | Goal 1, Goal 3 | contract compile |
| app/host lifecycle | Goal 4 | app/host |
| DI scope | Goal 5 | DI/module |
| callback submit | Goal 3, Goal 8, Goal 12, Goal 17, Goal 18 | 일반 C++는 call object callback, Unreal은 delegate callback |
| coroutine submit | Goal 3, Goal 8, Goal 12, Goal 17 | Unreal Connector에는 적용하지 않음 |
| channel request/reply | Goal 8 | channel messaging |
| send/event publish | Goal 8 | channel messaging |
| backpressure | Goal 9 | channel messaging, reliability |
| offload executor | Goal 6 | handler execution |
| serializer | Goal 7 | serializer |
| binding codec 정렬 | Goal 2 | contract compile, codec |
| SPOT lifecycle | Goal 10 | SPOT |
| SPOT timer | Goal 11 | timer |
| STREAM packet | Goal 12 | STREAM |
| ActorGateway relay | Goal 13 | ActorGateway relay |
| Registry | Goal 14 | Registry |
| monitoring | Goal 15 | monitoring |
| module/hosted service | Goal 16 | DI/module, hosted |
| transport abstraction | Goal 6, Goal 21 | runtime integration |
| lifecycle / ownership | Goal 4, Goal 6, Goal 7, Goal 9, Goal 13, Goal 20 | lifecycle, regression |
| stage wrapper | Goal 10, Goal 11, Goal 16 | SPOT, hosted |
| C++ Stream Connector | Goal 17 | connector |
| Unreal Stream Connector | Goal 18 | unreal connector |
| `Bingo` sample | Goal 19 | sample smoke |
| `TicTacToe` sample | Goal 19 | sample smoke |
| extensions | Goal 21 | framework-extension |
| POSD 리팩토링 | Goal 1-21 | POSD gate, regression |

## 8. Goal 실행용 문구

goal을 만들 때는 아래 문구를 objective로 사용할 수 있다.

```text
framework/languages/cpp/doc/draft/cpp-framework-implementation-plan.ko.md의 Goal N을
완료 기준까지 구현하고, POSD 기반 리팩토링을 이슈가 없을 때까지 수행한 뒤 해당
검증 명령을 실행하고 누락 항목을 보고한다.
```

여러 goal을 한 번에 묶어 실행할 때도 중간 goal의 완료 기준을 건너뛰지 않는다. 구현 중
draft 문서와 실제 코드가 충돌하면, 먼저 어떤 문서 항목과 충돌하는지 적고 문서 또는
구현 중 하나를 명시적으로 정리한 뒤 진행한다.

한 goal 안에서 POSD 위험 신호가 남아 있으면 다음 goal로 넘어가지 않는다. 21개 goal을
모두 끝낼 때까지 POSD 기반 리팩토링 기록은 최소 21개가 되어야 하며, 여러 번 반복한
goal은 반복 횟수를 모두 기록한다.
