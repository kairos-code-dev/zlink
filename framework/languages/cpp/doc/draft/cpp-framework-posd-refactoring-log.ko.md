<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ Implementation Plan](./cpp-framework-implementation-plan.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [구현 계획](./cpp-framework-implementation-plan.ko.md)

# Draft -- ZLink Framework C++ POSD Refactoring Log

> 이 문서는 **구현 전후 실행 기록**이다.
> 현재 공개 계약이 아니며, C++ framework 구현 goal마다 수행한 POSD 기반 리팩토링을
> 기록한다.

## Goal 1. Repository Skeleton And Build

### 발견한 위험 신호

- `zlink::stream_connector` target이 아직 public header에서 사용하지 않는 codec build
  option을 public compile definition으로 노출하고 있었다.
- 샘플 `main`이 version 값을 검사해 성공 여부를 정하고 있었다. include compile smoke에는
  필요 없는 조건이고, 나중에 version 정책이 바뀌면 샘플 smoke가 엉뚱한 이유로 실패할 수
  있었다.
- `.NET` 기준의 `Contracts/*`와 `Runtime/*` 분리를 문서에 추가한 뒤에도 skeleton은
  `include/zlink/framework/*.hpp` facade만 가지고 있었다. 이 상태에서는 public contract와
  runtime 구현 경계가 파일 구조로 검증되지 않는다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| public compile definition 유지 | 나중에 codec option을 header에서 바로 쓸 수 있다 | 아직 결정되지 않은 build 세부가 소비자 컴파일 표면에 노출된다 |
| generated config header로 숨김 | 필요해질 때 명시적인 public config 표면을 만들 수 있다 | Goal 1에서는 아직 실제 codec 구현이 없어 과하다 |
| compile definition 제거 | 빌드 경계만 남기고 불필요한 public 표면을 만들지 않는다 | 이후 codec goal에서 option 전달 방식을 다시 설계해야 한다 |
| facade header만 유지 | 기존 include 사용성이 가장 단순하다 | `.NET`식 contract/runtime 분리가 실제 구조에 반영되지 않는다 |
| contract header owner를 만들고 facade는 include wrapper로 유지 | 기존 include를 유지하면서 public 계약 owner를 분리한다 | Goal 1에서 디렉토리와 layout test가 추가된다 |

선택은 compile definition 제거다. Goal 1은 target 경계와 include compile 확인이 목적이므로,
아직 사용하지 않는 codec option을 public compile 표면에 올릴 이유가 없다.

샘플은 version 조건 검사를 제거하고, include 가능한지만 확인하는 형태로 유지했다.

contract/runtime 분리는 contract header owner를 새로 만들고 facade header는 wrapper로
남기는 방식을 선택했다. 이렇게 하면 기존 `#include <zlink/framework.hpp>` 사용성을
깨지 않으면서 `framework/include/zlink/framework/contracts/*`와
`framework/src/runtime/*` 경계를 실제 파일 구조로 검증할 수 있다.

### 적용한 리팩토링

- `zlink_stream_connector`의 public compile definitions를 제거했다.
- `Bingo`, `TicTacToe` sample smoke에서 version 값에 따른 조건부 실패를 제거했다.
- framework contract header owner를 `contracts/channels`, `contracts/dispatch`,
  `contracts/errors` 아래로 분리하고 기존 `zlink/framework/*.hpp`는 facade wrapper로
  정리했다.
- connector version contract를 `zlink/stream_connector/contracts/version.hpp`로 분리하고
  기존 `zlink/stream_connector/version.hpp`는 facade wrapper로 유지했다.
- `framework/src/runtime`, `connector/src/runtime`, Unreal `Private/` 경계를 물리적으로
  추가했다.
- public include가 `src/runtime/*`를 참조하지 않는지 확인하는
  `test_cpp_framework_layout_contract`를 추가했다.

### 남은 tradeoff

- connector codec build option은 Goal 17에서 실제 codec registry와 함께 다시 설계한다.
- Goal 1의 target은 INTERFACE target이다. runtime 구현은 Goal 6 이후에 붙인다.
- `contracts/detail/*`의 구체적 배치와 call object 내부 상태 은닉은 Goal 3에서 한 번 더
  점검한다. Goal 1에서는 runtime header가 public include로 새지 않는 구조 검증까지 닫았다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke --output-on-failure
git diff --check -- framework/languages/cpp
```

## Goal 2. Binding Codec Surface Alignment

### 발견한 위험 신호

- JSON, MessagePack, Protobuf 선택 codec target 생성 로직이 `bindings/cpp/CMakeLists.txt`
  안에 반복되어 있었다. target 이름, include directory, `zlink_cpp` link 규칙이 codec마다
  흩어지면 이후 dependency isolation 정책이 한 곳에서 유지되지 않는다.
- 기존 함수형 codec helper가 주 사용성처럼 남아 있었다. framework와 connector가 이
  표면을 따라가면 사용자는 `message_t`가 아니라 codec namespace를 먼저 알아야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 함수형 helper 유지 | 변경 범위가 작다 | framework/connector와 같은 `message_t` 중심 사용성을 만들 수 없다 |
| base `message_t`에 모든 codec 정의 포함 | 호출 표면이 가장 단순하다 | base binding이 JSON, MessagePack, Protobuf dependency를 끌고 온다 |
| base `message_t`에는 template 선언만 두고 선택 codec header에서 정의 | base binding dependency를 늘리지 않고 `message_t` 중심 API를 제공한다 | 사용자는 필요한 codec header와 target을 명시해야 한다 |

선택은 세 번째 방식이다. base binding은 raw message와 protocol enum만 제공하고, 각 codec
target이 자기 dependency와 `message_t::from_*`, `message.parse_*` 정의를 제공한다.

CMake target 생성은 `add_zlink_cpp_codec_target(...)` helper로 모았다. 이렇게 하면
선택 codec target의 include/link 규칙이 한 곳에 유지된다.

### 적용한 리팩토링

- `message_t`에 `from_json`, `parse_json`, `from_messagepack`, `parse_messagepack`,
  `from_protobuf`, `parse_protobuf` template 선언을 추가했다.
- 각 codec header가 해당 `message_t` helper 정의를 제공하게 바꿨다.
- 기존 codec namespace의 `encode`, `decode`, `parse`, `to_message`는 새 `message_t`
  helper를 호출하는 shim으로 정리했다.
- `zlink::cpp`, `zlink::cpp_codec_json`, `zlink::cpp_codec_messagepack`,
  `zlink::cpp_codec_protobuf` target을 추가했다.
- codec contract tests를 message 중심 API 기준으로 바꾸고 shim도 함께 검증했다.
- 반복되던 선택 codec target 생성 CMake를 `add_zlink_cpp_codec_target(...)` helper로
  모았다.

### 남은 tradeoff

- `message_t`의 codec helper는 base header에 선언만 있고 정의는 선택 codec header에 있다.
  필요한 header를 include하지 않으면 링크/컴파일 단계에서 사용할 수 없다. 이는 dependency
  isolation을 위한 의도된 tradeoff다.
- 기존 함수형 codec helper는 이행 기간 shim으로 남겼다. 제거 시점은 framework와 connector
  구현이 message 중심 API로 고정된 뒤 별도 정리한다.

### 재실행한 검증 명령

```bash
cmake -S bindings/cpp -B bindings/cpp/build -DZLINK_CPP_BUILD_TESTS=ON
cmake --build bindings/cpp/build --target test_cpp_contract_codec_json test_cpp_contract_codec_messagepack test_cpp_contract_codec_protobuf
ctest --test-dir bindings/cpp/build -R 'test_cpp_contract_codec_(json|messagepack|protobuf)' --output-on-failure
git diff --check -- bindings/cpp framework/languages/cpp
```

## Goal 3. Core Framework Types And Error Model

### 발견한 위험 신호

- call object 계약을 `contracts/channels/call.hpp`로 옮긴 뒤에도 callback/coroutine
  submit을 구현하는 작은 helper가 같은 public contract header 안에 남아 있었다. 이 상태는
  public 계약과 facade forwarding 구현이 한 파일에 섞여 보인다.
- `std::future`를 쓰지 않는다는 정책은 문서에 있었지만, public compile contract가
  blocking `wait()`나 future-style `get()` 부재를 직접 검증하지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| call helper를 그대로 contract header에 유지 | 파일 수가 적다 | contract owner와 facade forwarding이 한 파일에 섞인다 |
| call helper를 `src/runtime/*`로 이동 | public header가 가장 얇다 | template result type 때문에 Goal 3의 generic call surface를 구현하기 어렵다 |
| value-only call facade helper를 `contracts/detail/*`로 분리 | native/runtime 지식 없이 template forwarding만 숨길 수 있다 | runtime submitter가 붙기 전까지 즉시 완료 상태 helper가 public include tree에 남는다 |

선택은 세 번째 방식이다. Goal 3의 call object는 아직 runtime queue나 native submitter를
소유하지 않는다. `contracts/detail/call_facade.hpp`에는 result 값을 callback submit과
coroutine submit으로 같은 error kind에 연결하는 value-only helper만 둔다. pending queue,
executor, CAPI dispatch, native handle owner는 포함하지 않는다.

### 적용한 리팩토링

- `pending_operation_t`를 `contracts/channels/pending_operation.hpp`로 분리했다.
- `request_call_t`, `send_call_t`, `relay_call_t`, `stream_write_call_t`,
  `join_spot_call_t`의 반복 submit/timeout forwarding을
  `contracts/detail/call_facade.hpp`로 모았다.
- 기존 `zlink/framework/call.hpp`, `error.hpp`, `result.hpp`, `task.hpp`는 facade wrapper로
  정리하고, 실제 contract owner는 `contracts/*` 아래로 옮겼다.
- contract test에 `std::future`가 public async 타입이 아님을 확인하는 static assert와
  `wait()`/`get()` 부재 검사를 추가했다.
- timeout 실패와 shutdown 실패가 callback/coroutine submit에서 같은 error kind로 보이는지
  contract test로 확인했다.

### 남은 tradeoff

- `contracts/detail/call_facade.hpp`의 즉시 완료 helper는 template call object 계약을
  검증하기 위한 value-only helper다. runtime submitter, pending queue, shutdown drain은
  Goal 6과 Goal 9에서 `src/runtime/*` 구현으로 붙인다.
- `framework-unit` label은 아직 unit test executable이 없어 CTest가 "No tests were found"를
  출력한다. Goal 5 이후 DI/runtime 단위 테스트가 추가되면 이 label에 실제 테스트가 붙는다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-unit --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 4. App, Host, Configuration, Logging

### 발견한 위험 신호

- 첫 app/config/logging skeleton은 `config_builder_t::load_cli(...)` 같은 parser 구현을
  public header에 직접 둘 수 있는 모양이었다. 문서 기준상 CLI parser와 logging backend는
  framework 내부 구현이어야 하므로, public header에 구현이 커지면 얕은 모듈이 된다.
- `zlink::framework` target이 INTERFACE target으로 남으면 app/host 구현을
  `src/runtime/*`로 내릴 장소가 없다. 그러면 이후 signal handling, native context owner,
  graceful shutdown 구현이 public header로 밀려날 위험이 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| header-only app/config 유지 | CMake가 단순하고 초기 구현이 빠르다 | CLI parser, host state, logging 구현이 public header에 쌓인다 |
| 모든 app 상태를 public value type으로 유지 | 디버깅이 쉽다 | runtime owner와 shutdown state가 public layout에 고정된다 |
| `zlink::framework`를 실제 library target으로 만들고 구현을 `src/runtime/*`로 이동 | contract/runtime 분리를 강하게 유지한다 | Goal 4부터 build target이 static library가 된다 |

선택은 세 번째 방식이다. framework는 application host/runtime 계층이므로 처음부터
runtime 구현을 둘 수 있는 library target이어야 한다. public header는 facade와 계약을
보여 주고, parser와 host state는 `src/runtime/*`에 둔다.

### 적용한 리팩토링

- `zlink_framework` target을 INTERFACE에서 STATIC library로 바꿨다.
- `app_t`는 public concrete facade로 유지하되, 내부 상태는 `detail::app_state_t` PIMPL로
  숨겼다.
- `config_builder_t::load_json`, `load_env`, `load_cli` 구현을
  `framework/src/runtime/configuration/configuration.cpp`로 옮겼다.
- `logging_builder_t::use_console`, `set_level` 구현을
  `framework/src/runtime/diagnostics/logging.cpp`로 옮겼다.
- `app_t::create`, `services`, `handlers`, `config`, `logging`, `use_zlink`, `run`, `stop` 구현을
  `framework/src/runtime/host/app.cpp`로 옮겼다.
- `test_cpp_framework_app_host` unit test를 추가해 `run()` exit code, JSON/env/CLI가 같은
  configuration model에 합쳐지는지, logging facade가 외부 backend 타입 없이 동작하는지
  확인했다.

### 남은 tradeoff

- signal handling과 실제 graceful shutdown drain은 아직 native runtime이 없어서 Goal 6과
  Goal 11에서 구현한다. Goal 4에서는 public app/host 계약과 내부 구현 경계를 먼저 닫았다.
- `framework-regression` label은 아직 regression executable이 없어 CTest가
  "No tests were found"를 출력한다. Goal 20에서 전체 regression gate로 확장한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L framework-unit --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-regression --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 5. DI Container And Scope Lifetime

### 발견한 위험 신호

- `service_collection_t`가 빈 public 타입으로 남아 있어 `.NET`의 scope lifetime을 C++에서
  닫을 수 없었다.
- DI를 header-only로 구현하면 service registry, scope cache, shutdown resolve 금지 같은
  정책이 public header에 쌓인다. 이는 framework contract/runtime 분리 기준을 약하게 만든다.
- 첫 transient resolve 구현은 매번 새 객체를 만들지만, `get_required<T>()`가 reference를
  반환하기 때문에 temporary shared object가 바로 파괴될 수 있었다. 이는 API 의미와
  lifetime 구현이 맞지 않는 위험 신호다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| public header-only DI | template 구현이 단순하다 | registry/cache/shutdown 정책이 public header에 노출된다 |
| 외부 DI 라이브러리 사용 | 기능이 많다 | 외부 injector/scope 타입이 public API로 새기 쉽고 문서 정책과 맞지 않는다 |
| public header는 template forwarding만 두고 registry/scope 구현은 runtime으로 이동 | contract/runtime 분리를 유지한다 | type-erased factory와 scope state 구현이 필요하다 |

선택은 세 번째 방식이다. C++에서는 type 기반 등록 template이 필요하지만, 실제 descriptor
저장, duplicate validation, singleton/scoped/transient cache, shutdown resolve 금지는
`src/runtime/configuration/services.cpp`에 둔다.

### 적용한 리팩토링

- `service_lifetime_t`, `service_scope_kind_t`, `service_collection_t`,
  `service_provider_t`, `service_scope_t`를 추가했다.
- `add_singleton<T>()`, `add_singleton<T>(unique_ptr<T>)`, `add_scoped<T>()`,
  `add_transient<T>()`, `add_factory<T>()` public 등록 표면을 추가했다.
- service descriptor registry와 scope state는 runtime 구현 파일로 숨겼다.
- duplicate registration은 `request_protocol_error`로 실패하게 했다.
- provider close 이후 resolve와 scope 생성은 `shutdown`으로 실패하게 했다.
- scoped service는 root provider에서 직접 resolve할 수 없고, framework-owned scope 안에서만
  resolve되게 했다.
- transient instance는 현재 provider/scope가 닫힐 때까지 scope state가 보관하게 고쳤다.
  이렇게 해야 `get_required<T>()`가 반환한 reference가 같은 표현식 직후 dangling이 되지
  않는다.
- handler invocation, stream session, spot activation, entry spot, actor creation scope를
  `service_scope_kind_t`로 구분했다.

### 남은 tradeoff

- 실제 handler invocation, stream session close, Spot cleanup, actor creation lifecycle에
  scope를 연결하는 작업은 Goal 6, Goal 10, Goal 12에서 runtime integration과 함께 붙인다.
  Goal 5에서는 DI container와 scope lifetime 의미를 먼저 닫았다.
- 생성자 자동 wiring은 의도적으로 넣지 않았다. 의존성이 있는 타입은 `add_factory<T>()`로
  명시 등록한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_DI_scope
ctest --test-dir framework/languages/cpp/build -L framework-unit -R DI --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-regression -R scope --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 6. Runtime Integration And Dispatch Projection

### 발견한 위험 신호

- framework target이 binding을 사용하기 시작했지만, binding 타입이 public header에 노출되면
  사용자가 native context, socket, recv 순서를 알아야 하는 얕은 API가 된다.
- offload executor를 public contract로 만들면 handler 실행 정책이 runtime 구현 타입에
  묶인다.
- transport 지원을 각 feature 코드가 문자열로 직접 파싱하면 TCP, IPC, TLS, WebSocket,
  PGM 제외 정책이 여러 곳으로 흩어진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| public API가 binding socket/context를 직접 받음 | 구현이 빠르다 | native handle owner와 recv 순서가 사용자에게 노출된다 |
| framework core에 별도 event loop/transport stack 추가 | framework만으로 독립 실행 가능해 보인다 | zlink core transport 의미와 중복되고 shutdown/readiness가 복잡해진다 |
| binding은 `src/runtime/*` 내부에서만 link/include하고 public contract는 facade로 유지 | `.NET`식 contract/runtime 분리를 유지한다 | 내부 runtime owner와 projection test가 필요하다 |

선택은 세 번째 방식이다. framework target은 `zlink::cpp`를 private link하고,
`framework_runtime_t`가 `zlink::context_t`, channel socket, stream socket, discovery,
registry, spot node lifecycle을 내부에서 소유한다. public header는 이 타입들을 include하지
않는다.

### 적용한 리팩토링

- framework CMake가 `zlink::cpp`를 private dependency로 link하도록 바꿨다.
- `framework/src/runtime/host/framework_runtime.*`를 추가해 native context와 channel,
  stream, discovery, registry, spot node owner를 내부로 숨겼다.
- `framework/src/runtime/dispatch/offload_executor.*`를 추가해 CPU-bound handler offload와
  shutdown drain의 내부 기반을 만들었다.
- `framework/src/runtime/dispatch/projection.*`를 추가해 channel, stream, spot, timer event를
  typed runtime event로 투영하고, inline/offload 실행 정책을 적용했다.
- `transport_endpoint_t::parse(...)`를 추가해 TCP, IPC, TLS, WebSocket, WebSocket TLS를
  framework transport 의미로 받고 PGM을 거부하게 했다.
- `test_cpp_framework_runtime_integration`에서 native owner가 public header에 새지 않는 상태로
  context/socket/service owner 생성, offload drain, event projection ordering, endpoint
  validation을 검증했다.

### 남은 tradeoff

- 실제 CAPI dispatch callback 등록과 event kind별 `recv` 호출은 Goal 7-12의 handler,
  channel, SPOT, stream 구현에서 구체 타입과 연결한다. Goal 6에서는 public API 밖에 둘
  runtime owner와 projection 경계를 먼저 닫았다.
- DI public header에는 `detail::service_registry_t` 전방 선언과 shared state member가 남아
  있다. 이는 native runtime 지식은 아니지만, 이후 Goal 7 이후 public surface 정리에서 더
  얇게 만들 수 있는지 다시 점검한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L framework-unit -R runtime --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-integration --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 7. Handler Registry And Serializer

### 발견한 위험 신호

- 첫 handler registry 구현은 `topic`을 descriptor에 저장했지만 lookup key에는 쓰지 않았다.
  이 상태에서는 같은 channel과 packet name을 쓰는 다른 topic handler를 구분할 수 없다.
- sync request, coroutine request, send, event handler template마다 같은 예외 mapping 코드가
  반복됐다. decode 실패와 handler 예외 정책이 흩어지면 framework error 의미가 달라질 수
  있다.
- serializer registry가 custom serializer만 지원하면 Goal 7의 JSON 기본 serializer 조건을
  만족하지 못한다.
- handler 예외가 `result_t`로는 정리됐지만 monitoring runtime이 연결할 수 있는 실패 event
  경계가 없었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| channel + packet name만 key로 유지 | API 인자가 적다 | topic routing 요구와 맞지 않고 같은 packet name 재사용이 불가능하다 |
| channel + topic + packet name을 runtime key로 사용 | topic routing을 정확히 표현한다 | 기존 2인자 lookup은 빈 topic 호환 표면으로 제한해야 한다 |
| handler 예외 mapping을 각 template에 유지 | 구현이 바로 보인다 | 실패 정책이 public template 여러 곳에 중복된다 |
| `contracts/detail` helper로 예외 mapping을 모음 | native/runtime 지식 없이 forwarding 정책을 한곳에 둔다 | public include tree에 작은 helper가 남는다 |
| JSON serializer를 extension으로만 둠 | framework target dependency가 줄어든다 | C++ framework의 기본 JSON serializer 요구와 맞지 않는다 |
| framework target이 JSON codec include와 `nlohmann/json`을 기본 dependency로 가짐 | `.NET`과 같은 기본 JSON 사용성을 제공한다 | framework 사용자는 기본 JSON dependency를 함께 받는다 |

선택은 channel + topic + packet key, `contracts/detail` exception mapping helper, framework
기본 JSON serializer 방식이다. descriptor map, serializer map, failure observer 보관은
runtime 구현 파일에 두고, public header에는 template shape와 type-erased call boundary만
남겼다.

### 적용한 리팩토링

- `handler_registry_t`에 `on_request`, `on_send`, `on_event`, `send_raw`를 구현했다.
- request handler는 direct return과 `task_t<TReply>` return을 모두 지원한다.
- send/event handler는 direct `void`와 `task_t<void>` return을 모두 지원한다.
- handler lookup과 invoke key를 `channel_name + topic + packet_name`으로 바꿨다.
- 기존 2인자 `find`/`invoke`는 빈 topic 경로로만 남겨 호환 표면을 좁혔다.
- `handler_descriptor_t`에 `handler_execution_t`를 저장해 offload 정책이 registration
  boundary에서 사라지지 않게 했다.
- `serializer_registry_t`, `serializer_t<T>`, `payload_view_t`를 추가했고, 실제 serializer
  map은 `framework/src/runtime/codecs/serializer.cpp`에 숨겼다.
- `serializer_registry_t::add_json<T>()`를 추가해 framework 기본 JSON serializer를 제공했다.
- framework target이 JSON codec include와 `nlohmann_json::nlohmann_json`을 public
  dependency로 갖게 했다. base `zlink::cpp` binding은 JSON dependency를 끌고 오지 않는다.
- `payload_view_t::copy_message()`로 borrowed payload를 callback 밖에서 보관할 때의 copy
  boundary를 명확히 했다.
- handler 실패는 `framework_error_kind_t` result로 정리하고,
  `handler_failure_event_t`/`observe_failures(...)`로 monitoring runtime이 연결할 수 있는
  point-in-time failure event 경계를 만들었다.
- `test_cpp_framework_serializer_registry`와 `test_cpp_framework_handler_registry`를 추가해
  custom serializer, JSON serializer, decode failure, topic routing, DI owner resolve,
  task handler, raw handler, default packet name, duplicate registration, failure event를
  검증했다.

### 남은 tradeoff

- `observe_failures(...)`는 Goal 15의 전체 monitoring builder가 붙기 전까지 handler registry
  수준의 낮은 event hook이다. socket/discovery/registry/spot event와 같은 통합 monitoring
  표면은 Goal 15에서 닫는다.
- `typeid(T).name()` 기반 default packet name은 문서의 현재 기준을 따른다. 안정적인
  cross-compiler packet name 정책이 필요하면 serializer/packet naming 정책을 별도 goal에서
  확정해야 한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_framework_serializer_registry test_cpp_framework_handler_registry
ctest --test-dir framework/languages/cpp/build -L framework-unit -R handler --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-unit -R serializer --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 8. Channel Messaging

### 발견한 위험 신호

- 첫 channel runtime은 client/publisher 연결 여부와 pending queue 실패만 검증했고, local
  server capability ingress에서 handler registry로 dispatch되는 경로가 없었다.
- outbound request는 timeout result를 만들었지만 pending request table과 reply correlation
  경계가 없어 `ROUTER -> DEALER` 임의 push와 pending reply completion을 구분하기 어려웠다.
- `enable_server`, `enable_client`, `enable_publisher`, `enable_subscriber`가 같은 capability
  초기화 코드를 반복했다. 연결 정책이 늘어나면 네 곳이 따로 바뀔 위험이 있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| public `message_bus_t`에 raw dispatch API 추가 | 테스트가 쉽다 | raw message dispatch와 handler registry wiring이 사용자 표면으로 새어 나온다 |
| private `src/runtime/channels` runtime에서 dispatch/correlation을 검증 | public API는 channel name 기반으로 유지된다 | private runtime test hook이 필요하다 |
| outbound request를 즉시 timeout으로만 유지 | 구현이 작다 | reply correlation 완료 기준을 검증할 수 없다 |
| pending request table을 runtime state에 둠 | reply가 pending request에만 매칭되는 의미를 고정한다 | 실제 socket integration은 Goal 9 이후에 계속 붙여야 한다 |

선택은 private runtime hook 방식이다. public `message_bus_t`, `request_client_t`,
`publisher_t`는 channel name과 typed payload만 받는다. local server ingress dispatch,
pending request reservation, reply completion, drain은 `src/runtime/channels/*`에서 검증한다.

### 적용한 리팩토링

- `channel_builder_t`, `capability_builder_t`, `message_bus_t`, `request_client_t`,
  `publisher_t` public contract를 추가했다.
- `zlink_builder_t`에 `node`, `channel`, `max_pending`, `message_bus`, `request_client`,
  `publisher`, `channels`를 추가했다.
- server/client/publisher/subscriber capability에 `bind`, `connect`, `use_discovery`를
  추가하고, 같은 capability 안에서 manual endpoint와 discovery를 섞으면
  `request_protocol_error`로 실패하게 했다.
- `message_bus_t` outbound send/publish/request는 channel name 기준으로만 호출한다.
- missing client/publisher capability는 `disconnected`로 실패한다.
- pending queue 한도 초과는 `request_rejected`로 실패한다.
- `channel_runtime_t` private runtime을 추가해 local server capability ingress에서
  `handler_registry_t::invoke(...)`로 request/send를 dispatch하게 했다.
- outbound pending request table과 request sequence를 runtime state에 두고, 등록되지 않은
  reply completion은 `request_protocol_error`로 실패하게 했다.
- `drain()`은 pending request table과 pending count를 정리한다.
- capability enable 중복 코드는 `channel_builder_t::enable_capability(...)`로 모았다.
- `test_cpp_framework_channel_messaging`에서 manual path, outbound-only host,
  disconnected result, queue full result, local request/send dispatch, reply correlation,
  unmatched reply rejection을 검증했다.

### 남은 tradeoff

- 실제 zlink `ROUTER/DEALER/PUB/SUB` socket I/O와 send-ready drain은 Goal 9에서
  backpressure/reliability 구현과 함께 더 구체화한다. Goal 8에서는 public contract와
  runtime dispatch/correlation 경계를 먼저 닫았다.
- `message_bus_t::request(...)`는 현재 local test runtime에서 reply가 자동 완료되지 않으면
  `timeout`으로 닫는다. 실제 network reply completion은 Goal 9의 pending queue drain과
  함께 연결한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_channel_messaging
ctest --test-dir framework/languages/cpp/build -L framework-integration -R channel --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-regression -R channel --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 9. Backpressure, Flow Control, Reliability

### 발견한 위험 신호

- queue depth 조회를 public queue object로 노출하면 사용자가 pending queue와 send-ready
  drain 순서를 직접 다루게 된다.
- retry, dead-letter, idempotency hook을 call object마다 ad hoc으로 넣으면 reliability
  정책이 request/send/publish 호출부에 흩어진다.
- request reply correlation과 send-ready pending operation은 lifecycle 의미가 다르지만,
  같은 pending count를 공유한다. 이 차이를 숨기지 않으면 호출자가 내부 queue 구조를 알아야
  한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| public nonblocking/send-ready API 제공 | 호출자가 세밀하게 제어할 수 있다 | public non-blocking 옵션 금지 기준과 맞지 않고 recv/drain 순서가 노출된다 |
| call object마다 retry/dead-letter callback을 둠 | 호출 단위 커스터마이징이 쉽다 | reliability 정책이 여러 호출부로 흩어진다 |
| zlink builder에 retry/dead-letter hook을 두고 runtime이 pending queue를 소유 | public API가 단순하고 정책을 한곳에서 집약한다 | 세부 retry worker와 storage extension은 이후 goal에서 더 구현해야 한다 |

선택은 세 번째 방식이다. public surface는 queue depth 조회와 retry/dead-letter hook만
제공하고, nonblocking send, send-ready drain, timeout, shutdown/close mapping은
`src/runtime/channels/*`에서 처리한다.

### 적용한 리팩토링

- `channel_reliability_event_t`, `retry_hook_t`, `dead_letter_hook_t`를 추가했다.
- `zlink_builder_t::on_retry(...)`, `on_dead_letter(...)`를 추가했다.
- `message_bus_t::pending_count()`와 `pending_limit()`으로 queue depth만 조회하게 했다.
- `channel_runtime_t::queue_pending_send(...)`로 bounded pending operation을 등록한다.
- pending queue 한도 초과는 `request_rejected`로 실패한다.
- `mark_send_ready(...)`는 timeout 전에 pending operation을 drain하고 success로 닫는다.
- `expire_pending(...)`은 `timeout`으로 실패시키고 dead-letter hook에 idempotency key를
  전달한다.
- `retry_pending(...)`은 retry hook을 호출하되 public call object가 retry worker를 직접
  알지 않게 했다.
- `shutdown()`과 `close()`는 pending 작업을 drain하고 이후 새 작업을 각각 `shutdown`,
  `closed`로 실패시킨다.
- `test_cpp_framework_backpressure_reliability`에서 queue depth, queue full, send-ready
  drain, timeout, retry hook, dead-letter hook, idempotency key, shutdown, close mapping을
  검증했다.

### 남은 tradeoff

- 실제 native send-ready event와 zlink socket HWM 값 연결은 Goal 10 이후 SPOT/STREAM
  runtime과 함께 더 붙인다. Goal 9에서는 public 책임을 늘리지 않는 pending queue와
  lifecycle result mapping을 먼저 닫았다.
- retry hook은 현재 notification 경계다. 실제 retry worker, dead-letter storage, advanced
  retry policy는 Goal 21 extension boundary에서 확장한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_framework_backpressure_reliability
ctest --test-dir framework/languages/cpp/build -L framework-unit -R backpressure --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-regression -R reliability --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 10. SPOT Runtime

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | `.NET` `Contracts/Spots/*`의 Spot manager, remote address resolver, Spot context 개념과 `Runtime/Spots/*`의 activation, dispatch pump, outbound transport 분리를 기준으로 삼았다. |
| contract owner | `contracts/spots/spot.hpp`가 `node_rid_t`, `spot_rid_t`, `spot_route_t`, `spot_context_t`, `spot_node_builder_t`, snapshot, packet registry view를 소유한다. |
| runtime owner | `src/runtime/spots/spot_runtime.*`가 spot node state, factory map, RID directory, resolver map, ordering log를 소유한다. |
| public dependency | public header는 native SPOT node/socket/CAPI dispatch 타입을 노출하지 않는다. |
| native leakage | public method 인자와 반환값에는 `rid`, channel/topic, typed payload, call object만 있다. native handle, poller, callback userdata는 없다. |
| detail 사용 | private runtime test hook은 `src/runtime/spots/*`에 있고 `contracts/detail/*`에는 SPOT runtime 구현을 추가하지 않았다. |
| state hiding | `spot_context_t`와 `spot_node_builder_t`는 shared internal state를 PIMPL처럼 가리키며 state 정의는 runtime header에만 있다. |
| validation | `test_cpp_framework_spot_runtime`와 layout contract test로 public/runtime 분리와 SPOT observable contract를 확인한다. |

### 발견한 위험 신호

- SPOT node builder를 public named constructor로 직접 만들면 app host가 SPOT node lifecycle을
  관리한다는 완료 기준이 흐려질 수 있었다.
- registry 기반 remote address resolver와 custom resolver를 동시에 허용하면 같은 Spot RID
  lookup 정책이 두 곳에 흩어진다.
- ordering 검증을 public API로 노출하면 dispatch log라는 runtime 구현 세부가 사용자
  contract가 된다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `spot_node_builder_t("name")` public 생성자 유지 | 단위 테스트가 단순하다 | host lifecycle 관리 기준과 어긋나고 사용자가 builder를 독립 runtime처럼 오해할 수 있다 |
| `zlink_builder_t::spot_node(...)` 구성 경로만 사용 | SPOT node lifecycle이 app host 구성 아래에 남는다 | 테스트에서 builder state를 캡처해야 한다 |
| registry resolver와 custom resolver 동시 허용 | fallback 구성이 가능하다 | route 선택 정책이 모호해지고 충돌 검증이 늦어진다 |
| 둘 중 하나만 허용 | lookup owner가 명확하다 | 조합 정책은 이후 registry goal에서 명시적으로 확장해야 한다 |
| ordering log를 public `spot_context_t`에 노출 | 테스트가 쉽다 | runtime dispatch 순서가 public contract처럼 보인다 |
| private `detail::spot_node_runtime_t` test hook으로 검증 | public 표면을 깊게 유지한다 | unit test가 private runtime header를 include한다 |

선택은 host 구성 경로, resolver 단일 owner, private runtime test hook이다. public SPOT 표면은
Spot RID, Node RID, packet registry, typed send/request/publish call object만 제공하고,
activation table, native dispatch router, subscription pump, ordering log는 runtime에 둔다.

### 적용한 리팩토링

- `contracts/spots/spot.hpp`와 facade `zlink/framework/spots.hpp`로 SPOT public contract를
  분리했다.
- `src/runtime/spots/spot_runtime.*`에 SPOT node state, named spot factory, RID directory,
  resolver map, context state를 배치했다.
- `zlink_builder_t::spot_node(...)`와 `spot_nodes()`를 추가해 SPOT node lifecycle이 host
  구성 아래에서 관리되게 했다.
- `spot_context_t::publish(...)`, `send_to(...)`, `request_to(...)`, `register_packet(...)`을
  typed public 표면으로 추가했다.
- `use_registry_spot_remote_addresses(...)`를 추가하고 custom resolver와 동시에 등록하면
  `request_protocol_error`로 실패하게 했다.
- duplicate spot factory, duplicate resolver, empty discovery channel, empty registry route
  channel, duplicate packet 등록을 validation error로 닫았다.
- ordering 검증은 `detail::spot_node_runtime_t::from(builder).ordering_log(context)` private
  runtime hook으로만 확인하게 했다.
- `test_cpp_framework_spot_runtime`에서 named spot creation, `spot_rid -> spot_name` 조회,
  local/custom resolver lookup, registry resolver 설정, duplicate validation, publish/send/
  request result, route not found, ordering, stage wrapper 보관 가능성을 검증했다.

### 남은 tradeoff

- 실제 core `zlink::service::spot_node_t` dispatch, native route transport, Registry query
  client 연결은 이후 registry/stream/actor goal에서 더 붙인다. Goal 10은 public contract와
  runtime owner 경계, route resolver validation, typed call surface를 먼저 닫았다.
- `spot_node_builder_t` 기본 생성자는 기존 builder 패턴과 테스트 편의를 위해 남아 있다.
  정상 lifecycle은 `zlink_builder_t::spot_node(...)` 경로를 기준으로 검증했다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_framework_spot_runtime
ctest --test-dir framework/languages/cpp/build -L framework-integration -R spot --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-regression -R spot --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 11. SPOT Timer

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | `.NET` `Contracts/Timers/IZLinkTimer.cs`의 `IZLinkTimer`, `ZLinkTimerOptions`, `ZLinkTimerTick`, overrun policy와 `Runtime/Timers/ZLinkTimer.cs`의 pump/fire-count 계산 분리를 기준으로 삼았다. |
| contract owner | `contracts/timers/timer.hpp`가 `timer_t`, `timer_options_t`, `timer_overrun_policy_t`, `timer_tick_t`, failure event view를 소유한다. |
| runtime owner | `src/runtime/timers/timer_runtime.*`가 native timer wrapper, fire-count projection, reentry guard, failure event, cancel/drain state를 소유한다. |
| public dependency | public timer contract는 CAPI timer, poller, native callback userdata, binding timer type을 노출하지 않는다. |
| native leakage | `spot_context_t::add_timer<THandler>(...)`는 name, period, options만 받고 native timer token을 반환하지 않는다. |
| detail 사용 | timer runtime test hook은 private runtime header에만 있고 `contracts/detail/*`에는 timer 구현을 넣지 않았다. |
| state hiding | `timer_t`는 shared internal state를 들고, state 정의와 native timer owner는 runtime header에 있다. |
| validation | `test_cpp_framework_spot_timer`와 layout contract test가 timer public/runtime 경계를 확인한다. |

### 발견한 위험 신호

- fire-count 계산을 `timer_t` public class에 직접 넣으면 overrun policy와 native timer 상태가
  사용자 contract에 섞인다.
- native binding `zlink::timer_t`를 public timer header에 노출하면 사용자가 `recv()`,
  poller slot, native callback 순서를 직접 다루게 된다.
- same timer instance reentry를 handler 쪽 convention으로 남기면 호출자가 실행 순서를
  기억해야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| public `timer_t`가 binding timer를 직접 감쌈 | 구현이 작고 CAPI 기능 접근이 쉽다 | native lifecycle과 recv 순서가 public 표면으로 샌다 |
| public tick 계산 helper 제공 | 테스트와 디버깅이 쉽다 | fire-count drain loop가 public 계약이 된다 |
| private `timer_runtime_t`가 native timer와 fire-count projection을 소유 | public 표면이 `.NET` timer contract처럼 단순하다 | runtime test hook이 필요하다 |
| reentry를 handler 문서 규칙으로만 둠 | 구현이 작다 | 오류를 정의로 없애지 못하고 호출자 책임이 된다 |
| runtime state에서 reentry를 거부 | same timer instance 실행 규칙을 한곳에서 보장한다 | 실제 executor integration은 이후 dispatch goal과 연결해야 한다 |

선택은 private timer runtime과 runtime reentry guard다. public timer contract는 timer handle,
options, tick metadata만 제공하고, native timer start/stop, fire-count 해석, failure event,
shutdown drain은 runtime에 둔다.

### 적용한 리팩토링

- `contracts/timers/timer.hpp`와 facade `zlink/framework/timers.hpp`를 추가했다.
- `timer_t`, `timer_options_t`, `timer_overrun_policy_t`, `timer_tick_t`,
  `timer_failure_event_t`를 public contract로 정의했다.
- `spot_context_t::add_timer<THandler>(...)`를 추가하고 timer state는
  `src/runtime/timers/*`에서 소유하게 했다.
- private `timer_state_t`에 binding `zlink::timer_t`를 보관해 CAPI timer lifecycle을
  runtime 내부에서 시작/중지한다.
- `timer_runtime_t::dispatch_fire_count(...)`에서 `skip_late_ticks`,
  `catch_up_bounded`, `delay_next_tick`의 `delivery_index`, `scheduled_index`,
  `scheduled_elapsed`, `started_elapsed`, `delay`, `skipped_ticks`를 계산하게 했다.
- 같은 timer instance가 실행 중이면 `request_rejected`로 거부한다.
- handler 예외는 즉시 `timer_failure_event_t`로 기록하고,
  `stop_on_unhandled_exception`이면 timer를 disposed 상태로 닫는다.
- `cancel_all()`로 shutdown timer drain을 검증할 수 있게 했다.
- `test_cpp_framework_spot_timer`에서 fire-count projection, overrun policy, reentry 거부,
  handler failure monitoring, stop/continue policy, invalid option validation, cancel/drain,
  disposed timer result를 검증했다.

### 남은 tradeoff

- 현재 private runtime test는 `dispatch_fire_count(...)`로 CAPI dispatch event projection을
  직접 주입한다. 실제 SPOT dispatch pump에서 binding `spot_dispatch_event_t::timer_readable`
  이벤트를 받아 `recv()`를 호출하는 배선은 이후 SPOT/native dispatch 통합을 더 깊게 만들 때
  같은 runtime owner 안에서 붙인다. public API에는 변경이 필요 없다.
- Entry Spot timer의 전역 직렬화 금지는 public 계약과 runtime reentry guard 수준에서 먼저
  닫았다. 실제 Entry Spot executor 분리는 actor/stream relay goal에서 확장한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_spot_timer
ctest --test-dir framework/languages/cpp/build -L framework-unit -R timer --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-integration -R timer --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-regression -R timer --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 12. STREAM Framework

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | `.NET` `Contracts/Streams/*`의 stream/session/header/error 계약과 `Runtime/Streams/*`의 frame codec, session runtime, serial executor 분리를 기준으로 삼았다. |
| contract owner | `contracts/streams/stream.hpp`가 `stream_builder_t`, `stream_t`, `packet_stream_session_t`, `stream_header_t`, metadata, stream error, enum, snapshot을 소유한다. |
| runtime owner | `src/runtime/streams/stream_runtime.*`가 stream endpoint state, frame header encode/decode, semantic validation, session serial dispatch, written frame log를 소유한다. |
| public dependency | public STREAM contract는 raw stream socket, frame codec 구현, session table, request tracker, transport loop를 노출하지 않는다. |
| native leakage | public method는 endpoint name, session name, header view, payload, call object만 사용한다. native handle, poller slot, recv loop는 없다. |
| detail 사용 | frame codec과 serial dispatch 검증은 private runtime hook으로만 수행하고 `contracts/detail/*`에는 STREAM runtime 구현을 넣지 않았다. |
| state hiding | `stream_t`와 `stream_builder_t`는 shared internal state를 가리키며 state 정의는 runtime header에 있다. |
| validation | `test_cpp_framework_stream_framework`와 layout contract test가 public/runtime 경계, header validation, serial dispatch를 확인한다. |

### 발견한 위험 신호

- header encode/decode를 public helper로 제공하면 사용자가 framework Header framing을
  바꿀 수 있는 확장점처럼 오해할 수 있다.
- session lifecycle callback과 packet callback을 각각 직접 호출하면 같은 session 안의
  ordering 정책이 테스트마다 흩어진다.
- `on_error(...)`를 application handler 예외에도 호출하면 transport error projection과
  application failure mapping이 섞인다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| public header codec API 제공 | connector와 테스트에서 재사용하기 쉽다 | 사용자 정의 Header framing 가능성처럼 보인다 |
| private `stream_runtime_t`에서 header codec 소유 | core public 표면을 packet/session 계약으로 제한한다 | connector와 공유할 때 별도 내부 모듈 정리가 필요하다 |
| callback을 public session object에서 직접 호출 | 구현이 작다 | serial ordering과 validation 우회가 쉽다 |
| runtime serial dispatch를 통해 lifecycle/packet 호출 | 같은 session ordering과 validation 실패 drop을 한곳에서 보장한다 | private runtime test hook이 필요하다 |
| `on_error`를 모든 실패에 호출 | 사용자에게 실패 알림이 많다 | application handler 예외와 transport error가 구분되지 않는다 |
| `on_error`는 transport/session error만 호출 | `.NET` 계약처럼 오류 출처가 명확하다 | application handler failure는 result/monitoring으로 별도 처리해야 한다 |

선택은 private header codec, runtime serial dispatch, transport-only `on_error`다. public
STREAM 표면은 Header view와 packet session contract를 제공하고, frame codec, session table,
transport loop, validation drop은 runtime에 둔다.

### 적용한 리팩토링

- `contracts/streams/stream.hpp`와 facade `zlink/framework/streams.hpp`를 추가했다.
- `stream_builder_t`, `stream_t`, `packet_stream_session_t`, `stream_header_t`,
  `stream_metadata_t`, `stream_error_t`, STREAM enum, `stream_snapshot_t`를 public contract로
  정의했다.
- `zlink_builder_t::stream(...)`과 `streams()`를 추가해 STREAM endpoint 등록을 host 구성
  아래에 두었다.
- `src/runtime/streams/stream_runtime.*`에 header encode/decode, semantic validation,
  session open, lifecycle/packet/error serial dispatch를 배치했다.
- request sequence, packet name, codec, flags, correlation id, content type, metadata를
  header view와 runtime validation으로 검증했다.
- `stream_t::write_packet(...)`과 `reply_packet(...)`을 call object 표면으로 추가했다.
- Header validation 실패 packet은 session `on_packet(...)`으로 넘기지 않게 했다.
- application packet handler failure는 `on_error(...)`로 넘기지 않고 실패 result로 돌려준다.
- `test_cpp_framework_stream_framework`에서 stream bind/session registration,
  ActorGateway attach 이름 보관, header encode/decode, semantic validation, reserved prefix,
  session callback ordering, packet reply, invalid packet drop, transport error projection을
  검증했다.

### 남은 tradeoff

- 실제 binding `stream_socket_t` I/O, write-ready backpressure, request tracker storage는
  runtime owner 안에서 더 붙여야 한다. Goal 12에서는 public STREAM contract와 frame/session
  runtime 경계를 먼저 닫았다.
- `attach_actor_gateway(...)`는 Goal 13에서 actor/session relay runtime과 연결한다. Goal 12는
  route mesh channel로 우회하지 않는 구성 표면만 보관한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_framework_stream_framework
ctest --test-dir framework/languages/cpp/build -L framework-integration -R stream --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-regression -R stream --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 13. ActorGateway Session Relay

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | `.NET` `Contracts/Actors/*`, `Contracts/Streams/IZLinkSessionActor.cs`와 `Runtime/Actors/*`, `Runtime/Streams/*`의 binding table/relay sender 분리를 기준으로 삼았다. |
| contract owner | `contracts/actors/actor.hpp`가 `actor_ref_t`, `session_actor_manager_t`, `session_actor_t`, `actor_context_t`, `bound_session_t`를 소유한다. |
| runtime owner | `src/runtime/actors/actor_gateway_runtime.*`가 actor record, session binding table, relayed frame copy, bound session push 기록을 소유한다. |
| public dependency | public contract는 remote ActorGateway locator codec, relay packet dispatcher, registry store, route mesh channel 구현을 노출하지 않는다. |
| native leakage | public relay/send는 `actor_ref_t`, `stream_header_t`, borrowed `message_t`, call object만 사용한다. native handle과 internal frame buffer는 없다. |
| detail 사용 | ActorGateway runtime 검증은 private runtime hook으로만 수행하고 `contracts/detail/*`에는 relay 구현을 넣지 않았다. |
| state hiding | actor/session public facade는 shared internal state를 가리키며 binding table 정의는 runtime header에 있다. |
| validation | `test_cpp_framework_ActorGateway_actor_session_relay`가 actor create/find/bind/relay/push/cleanup과 payload non-consuming 정책을 확인한다. |

### 발견한 위험 신호

- session actor relay를 channel message bus나 Registry lookup으로 우회하면 ActorGateway hot
  path가 application route mesh 또는 운영 registry state에 묶인다.
- `relay(...)`와 `bound_session_t::send(...)`가 caller payload를 move로 소비하면 session
  callback의 borrowed payload lifetime 규칙이 깨진다.
- remote locator codec이나 relay frame 구조를 public type으로 만들면 ActorGateway 내부 wire
  형식이 application contract가 된다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| route mesh channel로 actor relay 구현 | 기존 channel runtime을 재사용할 수 있다 | 완료 기준과 달리 STREAM session actor relay가 application route mesh에 의존한다 |
| Registry에 session actor binding 저장 | remote lookup이 단순해 보인다 | hot path가 registry state에 묶이고 stream close cleanup 의미가 흐려진다 |
| private actor gateway binding table 사용 | session binding cleanup과 relay owner가 명확하다 | 실제 remote transport codec은 runtime에서 계속 구현해야 한다 |
| payload를 move해서 relay frame 생성 | 복사가 줄어든다 | caller payload non-consuming 정책을 위반한다 |
| runtime이 별도 relay frame payload를 복사 | caller lifetime을 보존한다 | relay path에서 필요한 복사 비용이 생긴다 |

선택은 private actor gateway binding table과 runtime-owned relay frame copy다. public 표면은
`actor_ref_t`, `session_actor_t`, `bound_session_t`만 제공하고 route mesh, registry store,
remote locator codec은 숨긴다.

### 적용한 리팩토링

- `contracts/actors/actor.hpp`와 facade `zlink/framework/actors.hpp`를 추가했다.
- `actor_ref_t`, `session_actor_manager_t`, `session_actor_t`, `actor_context_t`,
  `bound_session_t`를 public contract로 정의했다.
- `src/runtime/actors/actor_gateway_runtime.*`에 actor binding table, relayed frame copy,
  bound session push 기록을 배치했다.
- `session_actor_manager_t::create`, `find`, `get_or_create`, `bind`, `unbind_session`을
  추가했다.
- duplicate actor는 `actor_already_exists`, actor id/type 불일치는 `actor_type_mismatch`,
  unbound actor relay는 `actor_route_not_found`, unbound session push는
  `actor_session_not_bound`로 닫았다.
- `session_actor_t::relay(...)`와 `bound_session_t::send_raw(...)`는 caller payload를 소비하지
  않고 runtime-owned frame copy를 만든다.
- `session_actor_t::notify_disconnected()`와 manager `unbind_session(...)`으로 stream close 시
  session binding cleanup만 수행할 수 있게 했다.
- `test_cpp_framework_ActorGateway_actor_session_relay`에서 ActorGateway attach 구성, actor
  create/find/get-or-create, local/remote ref bind, relay, bound session push, disconnect
  cleanup, payload non-consuming 정책을 검증했다.

### 남은 tradeoff

- 실제 remote ActorGateway transport와 locator codec은 private runtime owner 안에서 더
  붙여야 한다. Goal 13에서는 public API와 session binding/relay semantics를 먼저 닫았다.
- actor factory DI resolve는 SpotNode의 `add_actor_factory(...)` 등록 표면과 actor manager
  contract를 분리해 둔 상태다. 실제 factory 생성 coordinator는 이후 integration goal에서
  DI scope와 연결한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_framework_ActorGateway_actor_session_relay
ctest --test-dir framework/languages/cpp/build -L framework-integration -R ActorGateway --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-regression -R actor --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 14. Registry And Topology

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | `.NET` `Contracts/Registry/*`, `Contracts/Configuration/Builders.cs`와 `Runtime/Registry/*`, `Runtime/Configuration/*`의 query model, route resolver, registration validator 분리를 기준으로 삼았다. |
| contract owner | `contracts/registry/registry.hpp`가 registry builder, discovery builder, query model, status, service summary, topology, member peer, monitoring snapshot을 소유한다. |
| runtime owner | `src/runtime/registry/registry_runtime.*`가 embedded registry option state, discovery endpoints, route channel validation, topology projection, Spot route lookup cache를 소유한다. |
| public dependency | public registry contract는 backend query transport, registry payload codec, route resolver cache, topology cache 구현을 노출하지 않는다. |
| native leakage | public API는 endpoint string, route channel name, query result value type, `spot_rid_t`와 `spot_route_t`만 사용한다. native registry handle, discovery owner, socket, poller는 없다. |
| detail 사용 | 테스트용 private hook은 `detail::registry_runtime_t`에 두고, `contracts/detail/*`에는 registry cache나 query 구현을 넣지 않았다. |
| state hiding | `registry_builder_t`, `discovery_builder_t`, `registry_query_t`는 shared opaque runtime state를 가리키고 state 정의는 runtime header에 있다. |
| validation | `test_cpp_framework_registry_topology`와 framework contract/layout test가 public header, registry validation, query, topology, monitoring snapshot, Spot lookup, ActorGateway relay 분리를 확인한다. |

### 발견한 위험 신호

- Registry validation, topology projection, Spot route lookup을 한 함수에 절차적으로 몰면
  registry runtime이 얕은 모듈이 되고 완료 기준별 정책 이름이 코드에 드러나지 않는다.
- Spot remote address lookup 기본값을 `spot_node_builder_t` 안의 custom resolver와 같은
  저장소로 처리하면 custom resolver conflict와 route channel ambiguity validation이
  흩어진다.
- ActorGateway session relay가 Registry lookup count나 registry route cache에 의존하면
  hot path가 운영 query state에 묶인다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `spot_node_builder_t`에 모든 registry lookup state를 둔다 | Spot API 근처에서 설정이 보인다 | discovery endpoints, route channel, topology query owner가 Spot builder로 새어 나온다 |
| `zlink_builder_t`에 registry 설정과 query cache를 직접 둔다 | 구현이 단순하다 | host builder가 topology cache, resolver validation, query model을 모두 알게 된다 |
| `registry_runtime_state_t`와 `registry_runtime_t`가 validation/query/cache를 소유한다 | public builder는 설정만 받고 runtime owner가 정책을 닫는다 | private runtime hook이 테스트에서 필요하다 |
| ActorGateway relay에서 Registry actor route lookup을 사용한다 | remote actor routing을 하나의 저장소로 볼 수 있다 | 완료 기준과 달리 session relay hot path가 registry에 의존한다 |
| ActorGateway relay와 Registry를 완전히 분리한다 | stream close cleanup과 actor binding table 의미가 안정적이다 | remote ActorGateway locator 구현은 actor runtime에서 별도로 유지해야 한다 |

선택은 registry runtime owner와 ActorGateway hot path 분리다. public registry 표면은
configuration/query value type만 제공하고, topology cache, route channel validation,
Spot route cache는 `src/runtime/registry/*`가 소유한다.

### 적용한 리팩토링

- `contracts/registry/registry.hpp`와 facade `zlink/framework/registry.hpp`를 추가했다.
- `registry_builder_t`, `discovery_builder_t`, `registry_query_t`, registry status,
  service summary, topology, member peer, monitoring snapshot model을 public contract로
  정의했다.
- `zlink_builder_t::registry(...)`, `discovery(...)`, `route_channel(...)`,
  `validate_registry()`, `registry_query()`를 추가했다.
- `spot_node_builder_t::use_registry_spot_remote_addresses()` no-arg overload를 추가해
  route channel이 하나일 때 명시 이름 없이 Registry 기본 lookup을 켤 수 있게 했다.
- 기존 explicit overload는 route channel 이름을 보관하고 custom resolver와의 충돌을
  그대로 validation한다.
- `src/runtime/registry/registry_runtime.*`에 embedded registry option validation,
  discovery endpoint validation, route channel ambiguity validation, topology projection,
  Spot remote route cache, monitoring snapshot source를 배치했다.
- POSD 리팩토링으로 `validate_embedded_registry()`,
  `validate_spot_remote_lookup()`, `project_channel()`, `project_spot_node()`를 분리해
  validation 정책과 topology projection 정책이 한 함수에 섞이지 않게 했다.
- `test_cpp_framework_registry_topology`에서 embedded registry bootstrap, peer/heartbeat
  설정, query status, topology/service summary/member peers, Registry-backed Spot lookup,
  custom resolver conflict, duplicate/ambiguous route validation, discovery 없는 Registry
  Spot default 오류, ActorGateway relay가 registry lookup count를 증가시키지 않는 점을
  검증했다.

### 남은 tradeoff

- 실제 CAPI registry service와 remote query transport 연결은 private runtime owner 안에서
  더 붙여야 한다. Goal 14에서는 public contract, validation, query/topology semantics,
  Spot lookup 기본값 경계를 먼저 닫았다.
- route channel은 C++ framework에 아직 전용 route mesh runtime이 없으므로
  `zlink_builder_t::route_channel(...)`의 public 설정으로 시작했다. 이후 channel route
  runtime이 구체화되면 같은 public 설정을 runtime channel owner에 연결한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_framework_registry_topology
ctest --test-dir framework/languages/cpp/build -L framework-integration -R registry --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-regression -R registry --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 15. Monitoring And Observability

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | `.NET` `Contracts/Eventing/Contracts.cs`와 `Runtime/Diagnostics/*`, 기능별 snapshot provider 분리를 기준으로 삼았다. |
| contract owner | `contracts/eventing/events.hpp`가 monitoring builder, runtime event enum, severity, health status, typed payload structs를 소유한다. |
| runtime owner | `src/runtime/diagnostics/monitoring_runtime.*`가 handler table, tracing hook, event publish/projection, timer failure event 생성 경로를 소유한다. |
| public dependency | public payload는 string, enum, value type, registry/spot/stream public model만 사용하고 telemetry backend나 logger backend 타입을 노출하지 않는다. |
| native leakage | socket native event는 안정적인 diagnostic 숫자로만 노출하고 native handle, monitor socket, poller slot, exception object를 싣지 않는다. |
| detail 사용 | private runtime hook은 `detail::monitoring_runtime_t`에만 두고, `contracts/detail/*`에는 event table이나 publisher 구현을 넣지 않았다. |
| state hiding | `monitoring_builder_t`는 opaque state를 가리키며 handler table과 source registration 정의는 diagnostics runtime header에 있다. |
| validation | `test_cpp_framework_monitoring`과 framework contract/layout test가 typed event delivery, tracing hook, timer immediate failure summary, handler exception/transport error 구분을 확인한다. |

### 발견한 위험 신호

- 처음 구현한 public `monitoring_builder_t::emit()`은 사용자가 runtime event 발행 경로를
  우회하게 만들 수 있었다. 이 경우 timestamp, tracing hook, severity 정책이 diagnostics
  runtime을 거치지 않아 monitoring builder가 얕은 event bus가 된다.
- timer handler failure payload에 exception 객체를 직접 싣거나 exception type을 runtime
  객체로 노출하면 public callback payload가 직렬화 가능한 운영 event가 아니게 된다.
- stream handler exception과 transport error를 같은 event kind로 합치면 운영자가
  application handler 실패와 연결 실패를 구분할 수 없다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| public builder가 `emit()`까지 제공 | 테스트와 수동 event 발행이 쉽다 | runtime projection, tracing, timestamp 정책을 우회한다 |
| public builder는 등록만 하고 runtime이 publish 소유 | event source와 runtime projection 책임이 명확하다 | 테스트는 private diagnostics hook을 사용해야 한다 |
| exception 객체를 payload에 직접 싣기 | 원본 정보를 모두 볼 수 있다 | public payload가 안정적인 value model이 아니고 언어/ABI 경계를 오염시킨다 |
| exception summary만 싣기 | 운영 event가 직렬화 가능하고 안전하다 | stack trace 같은 상세 정보는 logging/tracing backend에서 별도로 다뤄야 한다 |
| stream failure를 단일 error event로 둔다 | enum이 단순하다 | handler exception과 transport error 완료 기준을 만족하지 못한다 |
| stream event kind를 분리한다 | 운영자가 원인별로 대응할 수 있다 | enum 값이 조금 늘어난다 |

선택은 public registration-only builder와 diagnostics runtime-owned publish 경로다. public
payload는 안정적인 value type만 담고, event projection과 tracing/logging integration은
`src/runtime/diagnostics/*`에 둔다.

### 적용한 리팩토링

- `contracts/eventing/events.hpp`와 facade `zlink/framework/monitoring.hpp`를 추가했다.
- `monitoring_builder_t`, runtime event severity, health status, socket/discovery/registry/
  spot/stream/actor event enum과 payload struct를 public contract로 정의했다.
- `app_t::monitoring()`을 추가해 monitoring 등록 표면을 app host 구성에 붙였다.
- `src/runtime/diagnostics/monitoring_runtime.*`에 typed handler table, tracing hook,
  socket/discovery/registry/spot/stream/actor event publish, timer failure immediate event
  projection을 배치했다.
- POSD 리팩토링으로 public `emit()` 경로를 제거하고, event 발행은 diagnostics runtime
  owner만 수행하게 좁혔다.
- timer failure event는 timer name, handler type name, delivery index, scheduled index,
  exception type string, exception message만 담고 exception 객체는 싣지 않는다.
- stream event kind를 `transport_error`와 `handler_exception`으로 분리했다.
- `test_cpp_framework_monitoring`에서 socket/discovery/registry/spot typed event,
  stream transport error와 handler exception 구분, actor/session event, timer failure
  immediate summary, tracing hook 호출을 검증했다.

### 남은 tradeoff

- 실제 CAPI socket monitor, discovery monitor, registry/spot snapshot diff scheduler와
  logging backend 연결은 diagnostics runtime owner 안에서 더 붙여야 한다. Goal 15에서는
  public event contract와 projection 경계를 먼저 닫았다.
- tracing hook은 value event base만 받는다. vendor tracing backend 연결은 Goal 21
  extension boundary에서 별도 target으로 붙인다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_framework_monitoring
ctest --test-dir framework/languages/cpp/build -L framework-unit -R monitoring --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-integration -R monitoring --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 16. Hosted Services And Module System

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | `.NET` host/service registration과 C++ draft의 `module_t`, `hosted_service_t` 표면을 기준으로 삼고, lifecycle scheduler는 runtime owner에 숨겼다. |
| contract owner | `contracts/configuration/module.hpp`가 `module_t`와 `hosted_service_t` public contract를 소유한다. |
| runtime owner | `src/runtime/host/app.cpp`의 app state가 module 적용 순서, hosted service start/stop, reverse stop order를 소유한다. |
| public dependency | module contract는 services, zlink builder, handlers, monitoring public contract만 받는다. runtime scheduler나 worker queue 타입을 노출하지 않는다. |
| native leakage | hosted service는 `service_provider_t`만 받고 native context, socket, poller, dispatch token을 받지 않는다. |
| detail 사용 | module과 hosted service 실행 순서는 host runtime 구현에만 있고 `contracts/detail/*`에는 lifecycle 구현을 넣지 않았다. |
| state hiding | app state가 hosted service ownership과 started-service list를 숨긴다. public `app_t`는 add/run/stop 표면만 제공한다. |
| validation | `test_cpp_framework_module_hosted`와 framework contract/layout test가 module 구성, stage wrapper pattern, hosted lifecycle, reverse stop order를 확인한다. |

### 발견한 위험 신호

- `app_t::run()` 안에 hosted service start/stop 루프가 직접 들어가면 shutdown order 정책이
  CLI loading과 exit code 처리 사이에 섞인다.
- module이 runtime private 타입을 받으면 service/handler/runtime/observability 확장을
  묶는 public contract가 host 구현을 알게 된다.
- stage wrapper를 framework core concrete type으로 만들면 게임 또는 stage 전용 추상이 core
  public API가 된다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| module을 함수 callback 묶음으로만 제공 | 구현이 작다 | module이 기능 단위 타입으로 재사용되기 어렵고 extension point가 흐려진다 |
| `module_t` virtual contract 제공 | 서비스, runtime, handler, monitoring 구성을 한 기능 단위로 묶을 수 있다 | module type을 public contract로 추가해야 한다 |
| hosted service start/stop을 `app_t::run()`에 직접 구현 | 코드가 짧다 | lifecycle 정책이 run 절차에 섞이고 stop order가 재사용되지 않는다 |
| app state helper가 hosted lifecycle을 소유 | start/stop 순서와 예외 cleanup이 한 owner에 모인다 | app state 내부 메서드가 늘어난다 |
| stage wrapper를 core 타입으로 제공 | 샘플 작성이 쉽다 | stage가 framework core 개념처럼 보인다 |
| stage wrapper를 module pattern으로 검증 | SPOT 위의 상위 host/runtime 패턴임을 유지한다 | 사용자가 wrapper 타입을 직접 작성해야 한다 |

선택은 `module_t`/`hosted_service_t` public contract와 host runtime-owned lifecycle helper다.
stage wrapper는 core type이 아니라 module이 구성하는 상위 패턴으로 테스트했다.

### 적용한 리팩토링

- `contracts/configuration/module.hpp`와 facade `zlink/framework/module.hpp`를 추가했다.
- `module_t`에 `configure_services`, `configure_zlink`, `configure_handlers`,
  `configure_monitoring` extension point를 정의했다.
- `hosted_service_t`에 `start(service_provider_t&)`와 `stop() noexcept` lifecycle contract를
  정의했다.
- `app_t::add_module(...)`, `add_hosted_service(...)`, `monitoring()`을 host 표면에 추가했다.
- app runtime은 module을 서비스, zlink runtime, handler, monitoring 순서로 적용한다.
- hosted service는 `run()`에서 시작하고 reverse order로 정지하며, start 중 예외가 나면
  이미 시작한 service를 reverse order로 정리한다.
- POSD 리팩토링으로 hosted service start/stop 루프를 app state helper로 분리해 lifecycle
  정책이 `run()` 본문에 섞이지 않게 했다.
- `test_cpp_framework_module_hosted`에서 module service/runtime/handler/monitoring 구성,
  stage wrapper state/domain method/packet registry/outbound publisher/timer option mapping,
  hosted service start와 reverse stop order, null hosted service validation을 검증했다.

### 남은 tradeoff

- hosted service의 background worker loop, subscriber loop, heartbeat, retry worker,
  periodic cleanup은 이후 실제 runtime worker와 연결해야 한다. Goal 16에서는 lifecycle
  contract와 host-owned start/stop order를 먼저 닫았다.
- stage wrapper는 의도적으로 core concrete type으로 만들지 않았다. 재사용 helper가 필요하면
  module/extension으로 제공하고 SPOT timer metadata와 monitoring failure event를 숨기지
  않는 조건을 유지한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_framework_module_hosted
ctest --test-dir framework/languages/cpp/build -L framework-unit -R module --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-integration -R hosted --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 17. C++ Stream Connector

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | `.NET` `Systems.Zlink.Stream.Connector/Contracts/*`와 `Runtime/*`의 connector/call/options/model과 receive loop/pending request/frame sender 분리를 기준으로 삼았다. |
| contract owner | `connector/include/zlink/stream_connector/contracts/connector.hpp`가 connector options, state/error enum, metadata, packet, result/task, call object, codec registry, connector facade를 소유한다. |
| runtime owner | `connector/src/runtime/connector_runtime.*`가 connection state storage, dispatch queue, pending request table, sent frame log, packet handler table, state/error callbacks를 소유한다. |
| public dependency | connector public header는 server framework를 include하지 않고, payload boundary로 C++ binding `message_t`만 사용한다. MessagePack, Protobuf, LZ4 dependency는 기본 public dependency가 아니다. |
| native leakage | public connector contract에는 receive loop, transport connection, pending request table, frame sender, heartbeat scheduler, compression worker 타입이 없다. |
| detail 사용 | template request call은 opaque state와 type-erased submit forwarding만 갖고, pending table과 dispatch 구현은 runtime owner에 있다. |
| state hiding | `connector_t`, `codec_registry_t`, call object는 opaque connector state를 가리키며 state 정의는 connector runtime header에 있다. |
| validation | `test_cpp_stream_connector`와 layout/contract test가 connector target 독립성, connect/close, codec option, send/request, callback/coroutine submit, manual/immediate dispatch를 확인한다. |

### 발견한 위험 신호

- connector를 `INTERFACE` target으로만 두면 별도 배포 라이브러리라고 보기 어렵고, receive
  loop나 pending request owner를 둘 private runtime 경계도 없다.
- public connector header에 dispatch queue나 pending request table을 넣으면 manual dispatch와
  request correlation 구현이 사용자 계약이 된다.
- MessagePack, Protobuf, LZ4를 기본 target에 강제로 붙이면 사용하지 않는 codec dependency를
  사용자가 설치해야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| connector를 header-only facade로 유지 | 구현이 작고 빌드가 단순하다 | 별도 runtime owner가 없어 완료 기준의 contract/runtime 분리를 만족하기 어렵다 |
| connector를 `zlink::framework`에 포함 | 기존 framework runtime을 재사용할 수 있다 | connector가 서버 framework package에 묶여 별도 배포 기준을 위반한다 |
| connector를 별도 static target으로 구현 | public contract와 private runtime owner를 분리할 수 있다 | CMake target과 테스트 구성이 늘어난다 |
| codec dependency를 모두 기본 ON | typed helper 구현이 단순하다 | 사용하지 않는 Protobuf/MessagePack/LZ4 설치를 강제한다 |
| raw/JSON 기본, 나머지는 build option | 기본 사용은 하나의 target으로 가능하고 optional dependency를 격리한다 | optional codec path 검증을 별도로 유지해야 한다 |

선택은 별도 `zlink::stream_connector` static target과 private runtime owner다. connector는
server framework target을 의존하지 않고 C++ binding payload boundary만 사용한다.

### 적용한 리팩토링

- `connector/include/zlink/stream_connector/contracts/connector.hpp`와 umbrella
  `zlink/stream_connector.hpp`를 추가했다.
- `connector_t`, `connector_factory_t`, `connector_options_t`, `codec_registry_t`,
  `send_call_t`, `request_call_t<T>`, connector `result_t<T>`, `task_t<T>`를 public
  contract로 정의했다.
- transport, codec, compression, dispatch mode, message kind, header flags, error code,
  connection state enum과 metadata/packet/state changed model을 추가했다.
- `zlink_stream_connector`를 `INTERFACE`에서 별도 static library로 바꾸고
  `connector/src/runtime/connector_runtime.cpp`를 private runtime 구현으로 연결했다.
- `connector_runtime.*`에 connection state, state/error/disconnected callback, pending request
  correlation, manual dispatch queue, immediate dispatch, sent packet storage, codec feature
  flag validation을 배치했다.
- JSON은 기본 지원으로 두고 MessagePack, Protobuf, LZ4는 build option이 켜진 경우에만
  runtime feature flag가 활성화되게 했다.
- `test_cpp_stream_connector`에서 explicit connect, graceful close, connection state event,
  send callback submit, request timeout/correlation, packet callback receive, manual dispatch,
  immediate dispatch, metadata, JSON codec registry, unsupported optional codec error,
  connector instance별 독립 실행을 검증했다.

### 남은 tradeoff

- 실제 TCP/TLS/WebSocket transport, reconnect loop, heartbeat scheduler, frame codec,
  compression worker는 connector runtime owner 안에서 더 붙여야 한다. Goal 17에서는 public
  contract, dispatch mode, pending correlation, codec dependency isolation 경계를 닫았다.
- typed codec encode/decode는 현재 `message_t` boundary로 사상한다. JSON helper의 실제
  encode/decode는 bindings/cpp codec target과 연결된 뒤 connector typed path에서 확장한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector
ctest --test-dir framework/languages/cpp/build -L connector-unit --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-integration --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 18. Unreal Stream Connector

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | 일반 C++ connector contract/runtime 분리와 Unreal plugin public/private 분리를 함께 기준으로 삼았다. Unreal public API는 일반 connector wrapper가 아니라 Unreal 타입과 thread model 표면이다. |
| contract owner | `unreal-connector/Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h`가 `UZLinkStreamConnector`, Unreal-facing state enum, packet value type, Blueprint-callable method를 소유한다. |
| runtime owner | `unreal-connector/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp`가 일반 C++ connector instance, Game Thread dispatch forwarding, lifecycle shutdown mapping을 소유한다. |
| public dependency | Unreal public header는 일반 C++ connector runtime header를 include하지 않는다. Unreal 엔진이 없는 CTest compile에서는 shim 타입으로 public API shape만 검증한다. |
| native leakage | public API는 `FString`, `FName`, `TArray<uint8>`, `TMap<FString,FString>` 기반 표면만 노출하고 receive loop, pending request table, frame sender, thread queue를 노출하지 않는다. |
| detail 사용 | 일반 C++ connector runtime include는 Unreal `Private/` 구현에만 있다. |
| state hiding | `UZLinkStreamConnector`는 opaque private runtime을 `unique_ptr`로 소유한다. |
| validation | `test_unreal_stream_connector`가 plugin public header compile, connect/close/send/request/dispatch, PIE/map unload/game instance shutdown smoke를 확인한다. |

### 발견한 위험 신호

- Unreal public header가 일반 C++ connector runtime class를 멤버나 base class로 노출하면
  Unreal 사용자가 C++ connector 내부 lifecycle과 dispatch 모델을 알아야 한다.
- 초기 구현에서 `UZLinkStreamConnector`가 private runtime을 raw pointer로 소유했다. public
  facade 내부 구현이라도 소유권이 명시되지 않으면 shutdown/cleanup 의미가 흐려진다.
- Unreal callback을 내부 receive 흐름에서 바로 실행하면 Game Thread dispatch 완료 기준을
  지키기 어렵다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 일반 C++ connector API를 Unreal public header에 그대로 노출 | 중복 API가 줄어든다 | Unreal 타입, Blueprint, Game Thread 모델과 맞지 않는다 |
| Unreal 전용 facade를 제공하고 Private에서 일반 connector 사용 | Unreal 사용성에 맞고 runtime 구현을 숨길 수 있다 | public/private adapter 코드가 필요하다 |
| private runtime raw pointer 소유 | 구현이 짧다 | ownership, destructor, shutdown 책임이 명확하지 않다 |
| private runtime `unique_ptr` 소유 | RAII로 lifetime이 닫힌다 | public header에 `<memory>`와 전방 선언이 필요하다 |
| immediate callback 실행 | callback path가 단순하다 | Game Thread 실행 보장이 약해진다 |
| `Dispatch()`/`Tick()` manual dispatch 표면 제공 | Game Thread에서 명시적으로 callback을 처리할 수 있다 | 사용자가 frame loop에서 dispatch를 호출해야 한다 |

선택은 Unreal 전용 public facade와 `Private/` runtime adapter다. public header는 Unreal 타입과
Blueprint-callable method만 제공하고, 일반 connector runtime과 transport/codec/thread
dispatch 구현은 Private에 숨긴다.

### 적용한 리팩토링

- `ZLinkStreamConnector.uplugin` 골격을 유지하고 Unreal module `ZLinkStreamConnector.Build.cs`
  를 추가했다.
- `Public/ZLinkStreamConnector.h`에 `UZLinkStreamConnector`, `EZLinkStreamConnectionState`,
  `FZLinkStreamPacket`을 정의했다.
- Unreal 엔진이 없는 CTest 환경에서도 compile smoke가 가능하도록 public header에 최소 shim
  타입과 macro fallback을 두었다.
- `Connect`, `Close`, `SendJson`, `RequestJson`, `Dispatch`, `Tick`, `ShutdownForPie`,
  `ShutdownForMapUnload`, `ShutdownForGameInstanceShutdown` 표면을 추가했다.
- `Private/ZLinkStreamConnector.cpp`에서 일반 `zlink::stream_connector::connector_t`를
  사용하되 public header에는 일반 connector runtime 타입을 노출하지 않았다.
- POSD 리팩토링으로 private runtime 소유권을 raw pointer에서 `std::unique_ptr`로 바꾸었다.
- CMake smoke용 `zlink_unreal_stream_connector` target과 `test_unreal_stream_connector`를
  추가해 Unreal public header compile과 lifecycle smoke를 CTest에 등록했다.

### 남은 tradeoff

- 실제 Unreal `DECLARE_DYNAMIC_MULTICAST_DELEGATE` 기반 Blueprint assignable event와 Unreal
  logging category는 Unreal 엔진 빌드 환경에서 확장해야 한다. Goal 18에서는 public/private
  경계, Blueprint-callable shape, Game Thread manual dispatch path를 먼저 닫았다.
- JSON은 Unreal connector public API에 기본 `SendJson`/`RequestJson`으로 노출했지만 실제
  JSON serializer wiring은 일반 connector codec runtime과 함께 더 연결해야 한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_unreal_stream_connector
ctest --test-dir framework/languages/cpp/build -L unreal-connector-compile --output-on-failure
ctest --test-dir framework/languages/cpp/build -L unreal-connector-smoke --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 19. Review Samples

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | `.NET` Bingo와 TicTacToe 샘플의 역할 분리를 기준으로 삼되, C++ 샘플은 C++20 public framework API만 사용한다. |
| contract owner | 샘플은 새 public contract를 만들지 않고 `zlink/framework.hpp` public umbrella와 public contract 타입만 사용한다. |
| runtime owner | 샘플 전용 runtime store나 metadata store를 만들지 않는다. Actor/session relay는 public ActorGateway contract를 사용한다. |
| public dependency | 샘플은 framework public target만 링크하고 private runtime header를 include하지 않는다. |
| native leakage | 샘플은 native context, socket, poller, dispatch callback, stream frame codec을 직접 다루지 않는다. |
| detail 사용 | 샘플 코드에는 `detail::*` 사용이 없다. |
| state hiding | 샘플 state는 application domain object와 local variable에만 있고 framework runtime state를 직접 보관하지 않는다. |
| validation | `framework-sample-smoke` CTest label이 Bingo와 TicTacToe 실행을 확인한다. README도 샘플 역할과 포함 범위를 설명한다. |

### 발견한 위험 신호

- Goal 1 placeholder README가 남아 있으면 샘플을 열어도 현재 리뷰 범위를 알기 어렵다.
- Bingo에 STREAM ActorGateway relay 변형을 넣으면 TicTacToe와 역할이 겹치고 샘플 경계가
  흐려진다.
- TicTacToe에서 ActorGateway 대신 route mesh channel이나 sample-only metadata store를 쓰면
  Goal 13의 relay 기준을 우회하게 된다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 빈 샘플 유지 | smoke가 단순하다 | framework 전반 동작 리뷰에 쓸 수 없다 |
| 모든 기능을 한 샘플에 넣기 | 실행 대상이 하나다 | channel/SPOT과 STREAM/ActorGateway 역할이 섞인다 |
| Bingo와 TicTacToe로 역할 분리 | 각 샘플의 리뷰 목적이 명확하다 | 두 샘플을 모두 유지해야 한다 |
| 샘플에서 private runtime hook 사용 | 내부 상태 검증이 쉽다 | 사용자가 보는 public API 리뷰 샘플이 아니게 된다 |
| public API만 사용 | 실제 사용자 관점의 사용성을 확인할 수 있다 | 내부 상태 검증은 unit test에 맡겨야 한다 |

선택은 Bingo와 TicTacToe 역할 분리와 public API-only 샘플이다. Bingo는 channel/SPOT 중심,
TicTacToe는 STREAM/ActorGateway 중심으로 둔다.

### 적용한 리팩토링

- `samples/Bingo/main.cpp`를 app/host, DI, hosted service, channel request/reply,
  outbound-only host, manual connection, publish/subscribe, 일반 event publish, callback
  submit, coroutine submit, handler error, user Spot, SPOT timer, monitoring, graceful
  shutdown, offload handler option을 확인하는 샘플로 채웠다.
- `samples/TicTacToe/main.cpp`를 STREAM endpoint, ActorGateway attach, Entry Spot,
  actor factory, session actor bind, relay, bound session push, actor join/move,
  disconnect cleanup을 확인하는 샘플로 채웠다.
- 샘플 이름은 `Bingo`, `TicTacToe` 그대로 유지하고 별도 접미사를 붙이지 않았다.
- Bingo에는 STREAM ActorGateway relay 변형을 넣지 않았다.
- POSD 리팩토링으로 placeholder README를 제거하고 각 샘플의 리뷰 목적과 포함 범위를
  현재 코드와 맞게 갱신했다.

### 남은 tradeoff

- 샘플은 smoke와 사용성 리뷰 목적이므로 private runtime 내부 상태를 검증하지 않는다.
  내부 relay frame copy, pending queue, timer fire-count 같은 세부는 unit/integration test가
  검증한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target sample_cpp_framework_bingo sample_cpp_framework_tictactoe
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 20. Final Parity And Regression Gate

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | Goal 1-19에서 만든 C++ framework, connector, Unreal connector 표면을 `.NET` framework 기능 축과 대조했다. |
| contract owner | 새 public contract를 추가하지 않고 기존 installed public headers와 sample/connector contract를 회귀 대상으로 삼았다. |
| runtime owner | 각 기능의 runtime owner는 Goal별 owner matrix를 따른다. Goal 20은 owner를 새로 만들지 않고 전체 build/CTest로 경계를 검증한다. |
| public dependency | public header에서 GoogleTest, GoogleMock, spdlog, fmt, Kafka, gRPC, YAML, FlatBuffers, runtime include 누출을 검색했다. |
| native leakage | layout contract test와 public header 검색으로 runtime implementation header include 누출이 없음을 확인했다. |
| detail 사용 | `contracts/detail/*`에는 call object forwarding helper만 있고 queue, executor, frame codec, dispatch projection 구현은 없다. |
| state hiding | framework, connector, Unreal connector facade는 private/opaque state 또는 private implementation owner를 사용한다. |
| validation | full build, 전체 CTest 20개, public header dependency 검색, `git diff --check`를 실행했다. |

### 발견한 위험 신호

- draft의 Goal 20은 Goal 21보다 앞에 있으면서 완료 기준에 “Goal 1부터 Goal 21까지”를
  요구하고 있었다. 순서대로 진행하면 Goal 20에서 Goal 21 완료를 증명할 수 없어 실행
  계획 자체가 시간적 모순을 가진다.
- 전체 회귀 게이트가 extension boundary까지 요구하면 Goal 21의 extension owner를 만들기
  전에 Goal 20을 닫을 수 없다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| Goal 20을 건너뛰고 Goal 21을 먼저 진행 | 최종 감사 의미와 맞는다 | 사용자가 요청한 순서 진행을 깬다 |
| Goal 20 완료 기준을 그대로 두고 보류 | 문서를 바꾸지 않는다 | 다음 Goal로 넘어갈 수 없어 실행 계획이 멈춘다 |
| Goal 20을 Goal 1-19 회귀 게이트로 정리하고 Goal 21 뒤 최종 감사를 다시 수행 | 순서대로 진행하면서 충돌을 없앤다 | Goal 20 문서 완료 기준을 수정해야 한다 |

선택은 Goal 20을 Goal 1-19 회귀 게이트로 정리하는 것이다. Goal 21에서 extension boundary를
닫은 뒤 전체 21개 goal 최종 감사와 commit/push를 수행한다.

### 적용한 리팩토링

- `cpp-framework-implementation-plan.ko.md`의 Goal 20 완료 기준을 Goal 1-19 회귀 게이트로
  정리했다.
- Goal 21 extension boundary 항목은 Goal 21에서 닫고 그 뒤 전체 21개 goal 최종 감사를 다시
  수행한다고 문서화했다.
- `cmake --build framework/languages/cpp/build`로 framework, connector, Unreal connector,
  samples, tests 전체 target을 빌드했다.
- `ctest --test-dir framework/languages/cpp/build --output-on-failure`로 현재 등록된 20개
  테스트 전체를 실행했다.
- public header에서 테스트 라이브러리, 외부 extension dependency, runtime/private include
  누출을 검색했다.
- `contracts/detail/call_facade.hpp`를 확인해 현재 detail 영역이 call forwarding helper에
  머물고 있음을 확인했다.

### 남은 tradeoff

- Goal 21 extension boundary가 아직 남아 있다. 따라서 active thread goal은 완료가 아니며,
  Goal 21 이후 전체 21개 goal 최종 감사, 변경 범위 리뷰, 커밋/푸시가 필요하다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
rg -n "gtest|gmock|spdlog|fmt::|boost|kafka|grpc|yaml|flatbuffers|src/runtime|connector/src/runtime|Private/" framework/languages/cpp/framework/include framework/languages/cpp/connector/include framework/languages/cpp/unreal-connector/Source/ZLinkStreamConnector/Public
find framework/languages/cpp/framework/include/zlink/framework/contracts/detail -type f -maxdepth 1 -print -exec sed -n '1,220p' {} \;
git diff --check -- framework/languages/cpp bindings/cpp
```

## Goal 21. Extension Boundaries

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | extension은 core framework public API 위에 붙는 별도 산출물이다. `.NET` framework core와 같은 channel, monitoring, logging 계약을 사용하되 Kafka, gRPC, HTTP, YAML, FlatBuffers 구현 dependency는 core target에 넣지 않는다. |
| contract owner | extension public contract는 `extensions/include/zlink/framework/extensions/*`가 소유한다. core framework contract owner를 새로 만들지 않는다. |
| runtime owner | Goal 21은 implementation boundary를 닫는 goal이므로 extension target은 `INTERFACE` target으로 시작한다. 실제 bridge runtime이 필요해질 때는 extension별 private runtime owner를 둔다. |
| public dependency | extension public header는 framework public contract header만 include하고 Kafka, gRPC, HTTP, YAML, FlatBuffers header를 include하지 않는다. |
| native leakage | extension public signature는 endpoint string, channel name, callback, policy value만 사용하고 native handle, socket, poller, dispatch token을 노출하지 않는다. |
| detail 사용 | `contracts/detail/*`을 사용하지 않는다. extension boundary는 core public contract 위에서만 붙는다. |
| state hiding | extension target은 외부 system client나 bridge runtime state를 public member로 노출하지 않는다. 현재 public object는 option/policy/builder boundary만 표현한다. |
| validation | `framework-extension` CTest label과 CMake extension target link를 추가했다. |

### 발견한 위험 신호

- extension header가 umbrella `zlink/framework.hpp`를 include하면 필요한 계약보다 넓은
  public 표면을 끌고 온다.
- metrics와 tracing이 monitoring event만 직접 hook하고 logging 정책을 지나가지 않으면
  core diagnostics 정책을 우회하는 확장처럼 보일 수 있다.
- advanced retry와 dead-letter가 ordering, duplicate delivery, idempotency key 의미를
  public policy에 드러내지 않으면 handler 작성자가 중복 처리 기준을 알기 어렵다.
- extension public header가 실제 Kafka, gRPC, HTTP, YAML, FlatBuffers header를 include하면
  core framework를 쓰는 사용자에게 불필요한 dependency가 번진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| extension을 core framework target에 직접 구현 | 사용자가 target 하나만 링크한다 | Kafka, gRPC, HTTP, YAML, FlatBuffers dependency가 core에 섞인다 |
| extension을 별도 target으로 두고 core public API만 의존 | core 기본 사용성이 변하지 않는다 | extension target 목록과 install 경계를 관리해야 한다 |
| extension header에서 umbrella framework header include | include가 단순하다 | extension boundary가 필요한 contract보다 넓어진다 |
| extension header에서 필요한 contract header만 include | public dependency와 의도를 좁게 유지한다 | include 목록을 명시적으로 관리해야 한다 |
| retry/dead-letter 의미를 runtime 문서에만 둔다 | public type이 짧다 | handler 작성자가 ordering과 idempotency 의미를 놓치기 쉽다 |
| retry/dead-letter policy value에 의미를 드러낸다 | public 계약만 보고도 중복 처리 기준을 알 수 있다 | policy surface가 조금 늘어난다 |

선택은 extension별 `INTERFACE` target과 좁은 public contract include다. retry/dead-letter는
policy value에 ordering, duplicate delivery, idempotency key 의미를 명시한다.

### 적용한 리팩토링

- `zlink::framework_extension_*` target들을 추가하고 각 target이 `zlink::framework`만
  의존하게 했다.
- extension target과 include directory를 별도로 설치/export 대상으로 추가했다.
- `test_cpp_framework_extensions`를 CTest `framework-extension` label로 등록했다.
- extension public header의 include를 `zlink/framework.hpp`에서 필요한 public contract
  header로 좁혔다.
- metrics와 tracing extension이 `monitoring_builder_t`와 `logging_builder_t`를 통해
  core diagnostics 정책 위에서 동작하도록 표면을 보강했다.
- advanced retry와 dead-letter policy에 ordering, duplicate delivery, idempotency key
  의미를 public value로 드러냈다.

### 남은 tradeoff

- Goal 21은 extension boundary를 닫는 단계이므로 Kafka, gRPC, HTTP, YAML, FlatBuffers의
  실제 외부 library 호출 구현은 포함하지 않는다. 이후 구현하더라도 각 extension private
  runtime target 안에 숨겨야 하며 core framework target에는 추가하지 않는다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_framework_extensions
ctest --test-dir framework/languages/cpp/build -L framework-extension --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```
