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

### 추가 리뷰. Unreal Connector async 표면 분리

#### 발견한 위험 신호

- 일반 C++ Stream Connector의 `task_t`, `submit()`, `co_await` 표면과 Unreal Connector의
  delegate callback 표면이 같은 connector 설명 안에서 함께 읽혔다. 이 상태에서는 Unreal
  public header에도 coroutine API가 필요하다고 오해할 수 있다.
- Unreal public header가 일반 C++ connector runtime을 감싸지 않는다는 검사는 있었지만,
  `task_t`나 coroutine header가 Unreal public 표면에 새지 않는다는 회귀 검사는 없었다.

#### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| Unreal에도 일반 C++ connector와 같은 coroutine 표면 제공 | C++ connector와 이름이 같아진다 | Unreal thread model, Blueprint delegate, Game Thread dispatch와 맞지 않는다 |
| Unreal은 delegate callback만 제공 | Unreal 사용성이 자연스럽고 Game Thread 규칙을 분명히 한다 | 일반 C++ connector와 async 표현이 다르다 |

선택은 두 번째 방식이다. wire protocol과 codec 의미는 같게 유지하되, Unreal public API는
Unreal 타입과 delegate callback을 기준으로 둔다. coroutine은 일반 C++ connector와
framework server runtime의 C++20 표면에만 둔다.

#### 적용한 리팩토링

- `cpp-stream-connector.ko.md`에 Unreal Connector는 `task_t`, `submit()`, `co_await` 표면을
  public API로 제공하지 않는다고 명시했다.
- `cpp-framework-implementation-plan.ko.md`의 Unreal Goal 완료 기준과 feature index를
  delegate callback 기준으로 보정했다.
- `test_cpp_framework_layout_contract`에 Unreal public header가 일반 connector include,
  `task_t`, `<coroutine>`, `co_await`, `submit`을 노출하지 않는지 확인하는 검사를 추가했다.

#### 남은 tradeoff

- Unreal public API는 일반 C++ connector와 같은 wire 의미를 쓰지만 async 표현은 다르다.
  이는 Unreal lifecycle과 Blueprint/Game Thread 규칙을 지키기 위한 의도된 차이다.

#### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_layout_contract
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_layout_contract --output-on-failure
git diff --check -- framework/languages/cpp
```

## 반복 POSD 재리뷰. Sample connector 호출 정책 공통화

### 발견한 위험 신호

- Bingo와 TicTacToe player client는 connector 생성 option은 공통 helper를 쓰지만,
  `connect().result()`, `close().result()`, `dispatch().result()` 호출 방식은 각 client에
  직접 남아 있었다. 이는 sample client가 connector task 처리 세부를 계속 알아야 하는
  정보 누출이다.
- typed request와 fire-and-forget send도 각 sample client가 `codecs::request/send`,
  `submit()`, `result()`, sample call result 변환 순서를 직접 조립했다. 게임별 client가
  보여줘야 할 것은 packet 흐름인데, connector 호출 절차가 같이 섞여 있었다.
- `connector_runtime_t::complete_next_request(...)`는 실제 호출자가 없는 내부 hook으로
  남아 있었다. pending request table을 직접 지우고 fake reply packet을 queue에 넣기 때문에,
  유지하면 connector request 완료 경로가 두 곳에 있는 것처럼 보인다.
- `connector_callbacks_t`는 error handler loop를 한 번 감싸는 class였지만 실제 호출자가
  없었다. connector error publish 정책은 실제 send/request 호출 경로의 helper가 이미
  소유하므로, 별도 source는 얕은 모듈로 남아 있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현재 구조 유지 | 변경이 작다 | connector task/result 처리 규칙이 sample마다 반복된다 |
| player client 공통 base class 추가 | 반복을 많이 줄일 수 있다 | 게임별 client 상속 구조가 생겨 sample이 더 무거워진다 |
| 기존 sample helper에 connector 호출 정책만 추가 | public API를 바꾸지 않고 반복 지식만 숨긴다 | helper 함수 수가 조금 늘어난다 |
| 미사용 runtime hook 유지 | 테스트용 확장 가능성이 남는다 | 실제 완료 경로가 아닌 API가 pending request 정책을 노출한다 |

선택은 기존 sample helper 확장이다. sample public surface와 게임별 client 구조는 유지하고,
connector task 완료와 result 변환 규칙만 `samples/Shared/client_connector_helpers.hpp`가
소유하게 한다.

### 적용한 리팩토링

- `connect_client_connector`, `close_client_connector`, `dispatch_client_connector`를 추가해
  sample client가 connector task result를 직접 풀지 않게 했다.
- `request_client_packet`과 `send_client_packet`을 추가해 typed packet 호출과 sample call
  result 변환을 한 helper로 모았다.
- Bingo/TicTacToe player client에서 직접 `codecs::request/send`, `submit()`, `result()`를
  조립하던 코드를 공통 helper 호출로 바꿨다.
- 호출자가 없던 `connector_runtime_t::complete_next_request(...)`를 제거했다.
- 호출자가 없던 `connector_callbacks_t`와 source 등록을 제거했다.

### 수정 후 점검

- Bingo/TicTacToe player client는 actor id, notification registration, game packet method만
  드러낸다.
- connector option, task 완료, error string 변환 정책은 sample shared helper에 모였다.
- framework, http-client, connector public API는 바꾸지 않았다.
- connector request 완료는 `submit_request(...)`의 실제 frame receive 경로만 남았다.
- connector callback task 완료는 public `task_t`가 소유하고, runtime source에는 사용하지
  않는 callback wrapper가 남지 않는다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target sample_cpp_framework_bingo_client sample_cpp_framework_tictactoe_client test_cpp_framework_sample_parity
ctest --test-dir framework/languages/cpp/build -R 'sample_smoke_sample_cpp_framework_bingo_client|sample_smoke_sample_cpp_framework_tictactoe_client|sample_e2e_log_sample_cpp_framework_bingo_client|sample_e2e_log_sample_cpp_framework_tictactoe_client|test_cpp_framework_sample_parity' --output-on-failure
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_cpp_stream_connector --output-on-failure
```

## 반복 POSD 재리뷰. HTTP client test builder 정책 공통화

### 발견한 위험 신호

- `test_cpp_http_client`의 각 test가 `client_t::create().base_url(...).json().timeout(...)`
  builder chain을 직접 반복했다. HTTPS trust test는 여기에 certificate trust 설정까지 더해,
  테스트가 검증하려는 HTTP 동작보다 client 구성 절차가 더 크게 보였다.
- `test_cpp_framework_app_host`도 HTTP host와 HTTPS host 검증에서 같은 JSON client 구성과
  500ms timeout 정책을 직접 반복했다.
- timeout 기본값과 JSON mode 선택이 테스트마다 흩어져 있어, HTTP client test 기본 정책을
  바꿀 때 여러 test body를 함께 수정해야 했다.
- implementation plan의 구조 표는 HTTP client에도 backend contract 디렉터리가 있는 것처럼
  적고 있었지만, 실제 HTTP client는 별도 backend adapter가 없는 단일 runtime owner 구조다.
  없는 디렉터리를 맞추기 위해 placeholder를 만들면 얕은 모듈만 늘어난다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현재 구조 유지 | helper가 늘지 않는다 | client 구성 정책이 테스트마다 반복된다 |
| fixture class 도입 | shared setup을 강하게 묶을 수 있다 | 각 test가 독립 server를 갖는 현재 구조보다 무거워진다 |
| 작은 `make_json_client(...)` helper 추가 | 반복 정책만 숨기고 test 독립성은 유지한다 | optional trust 인자가 하나 늘어난다 |

선택은 작은 helper 추가다. HTTP/HTTPS server 생명주기는 각 test가 계속 소유하고, JSON mode,
timeout, optional trust certificate 구성만 helper가 소유한다.

### 적용한 리팩토링

- `make_json_client(...)`를 추가해 HTTP client test의 JSON mode, timeout, trust certificate
  설정을 한 곳으로 모았다.
- `make_app_host_test_client(...)`를 추가해 app host HTTP/HTTPS e2e test의 JSON mode,
  timeout, optional trust certificate 설정을 한 곳으로 모았다.
- HTTP/HTTPS client tests가 검증하려는 request/response expectation만 직접 드러내도록
  builder chain 반복을 제거했다.
- implementation plan의 HTTP client backend contract 칸을 `현재 없음`으로 바꾸고, 별도
  backend adapter가 생기기 전에는 placeholder 디렉터리를 만들지 않는다고 명시했다.

### 수정 후 점검

- HTTP client public API는 바꾸지 않았다.
- HTTPS trust와 hostname mismatch test는 여전히 explicit trust certificate를 검증한다.
- timeout test는 기존 50ms timeout을 유지한다.
- HTTP client runtime 구조 문서는 실제 파일 구조와 맞고, 사용하지 않는 backend contract
  placeholder를 요구하지 않는다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_http_client
ctest --test-dir framework/languages/cpp/build -R test_cpp_http_client --output-on-failure
cmake --build framework/languages/cpp/build --target test_cpp_framework_app_host
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_app_host --output-on-failure
```

### 발견한 위험 신호

- `zlink::stream_connector` target이 당시 public header에서 사용하지 않는 codec build
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
| public compile definition 유지 | 나중에 codec option을 header에서 바로 쓸 수 있다 | 당시 결정되지 않은 build 세부가 소비자 컴파일 표면에 노출된다 |
| generated config header로 숨김 | 필요해질 때 명시적인 public config 표면을 만들 수 있다 | Goal 1에서는 당시 실제 codec 구현이 없어 과하다 |
| compile definition 제거 | 빌드 경계만 남기고 불필요한 public 표면을 만들지 않는다 | 이후 codec goal에서 option 전달 방식을 다시 설계해야 한다 |
| facade header만 유지 | 기존 include 사용성이 가장 단순하다 | `.NET`식 contract/runtime 분리가 실제 구조에 반영되지 않는다 |
| contract header owner를 만들고 facade는 include wrapper로 유지 | 기존 include를 유지하면서 public 계약 owner를 분리한다 | Goal 1에서 디렉토리와 layout test가 추가된다 |

선택은 compile definition 제거다. Goal 1은 target 경계와 include compile 확인이 목적이므로,
당시 사용하지 않는 codec option을 public compile 표면에 올릴 이유가 없다.

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

선택은 세 번째 방식이다. Goal 3의 call object는 당시 runtime queue나 native submitter를
소유하지 않는다. `contracts/detail/call_facade.hpp`에는 result 값을 callback submit과
coroutine submit으로 같은 error kind에 연결하는 value-only helper만 둔다. pending queue,
executor, CAPI dispatch, native handle owner는 포함하지 않는다.

### 적용한 리팩토링

- `pending_operation_t`를 `contracts/channels/pending_operation.hpp`로 분리했다.
- `request_call_t`, `send_call_t`, `relay_call_t`, `stream_write_call_t`의 반복
  submit/timeout forwarding을 `contracts/detail/call_facade.hpp`로 모았다.
- 기존 `zlink/framework/call.hpp`, `error.hpp`, `result.hpp`, `task.hpp`는 facade wrapper로
  정리하고, 실제 contract owner는 `contracts/*` 아래로 옮겼다.
- contract test에 `std::future`가 public async 타입이 아님을 확인하는 static assert와
  `wait()`/`get()` 부재 검사를 추가했다.
- timeout 실패와 shutdown 실패가 callback/coroutine submit에서 같은 error kind로 보이는지
  contract test로 확인했다.
- handler registry 단위 테스트에 `task_t<T>` 다중 await와 first-complete-wins 회귀를
  추가했다. 같은 task를 여러 coroutine이 await할 수 있고, 완료 상태와 callback은 첫 완료로만
  확정된다.
- layout contract에 handler dispatch가 coroutine executor와 `await_task_result`를 통과하는지
  검사하고, route handler dispatch가 `.result()` blocking bridge로 후퇴하지 않도록 고정했다.

### 남은 tradeoff

- `contracts/detail/call_facade.hpp`의 즉시 완료 helper는 template call object 계약을
  검증하기 위한 value-only helper다. runtime submitter, pending queue, shutdown drain은
  Goal 6과 Goal 9에서 `src/runtime/*` 구현으로 붙인다.
- `framework-unit` label은 당시 unit test executable이 없어 CTest가 "No tests were found"를
  출력한다. Goal 5 이후 DI/runtime 단위 테스트가 추가되면 이 label에 실제 테스트가 붙는다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_handler_registry test_cpp_framework_layout_contract
ctest --test-dir framework/languages/cpp/build -R "handler_registry|layout_contract" --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-unit -R async --output-on-failure
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
  `framework/src/runtime/configuration/builders/configuration_builder.cpp`로 옮겼다.
- `logging_builder_t::use_console`, `set_level` 구현을
  `framework/src/runtime/diagnostics/logging.cpp`로 옮겼다.
- `app_t::create`, `advanced().services`, `advanced().handlers`, `config`, `logging`,
  `advanced().use_zlink`, `run`, `stop` 구현을
  `framework/src/runtime/host/app.cpp`로 옮겼다.
- `test_cpp_framework_app_host` unit test를 추가해 `run()` exit code, JSON/env/CLI가 같은
  configuration model에 합쳐지는지, logging facade가 외부 backend 타입 없이 동작하는지
  확인했다.

### 남은 tradeoff

- signal handling과 실제 graceful shutdown drain은 당시 native runtime이 없어서 Goal 6과
  Goal 11에서 구현한다. Goal 4에서는 public app/host 계약과 내부 구현 경계를 먼저 닫았다.
- `framework-regression` label은 당시 regression executable이 없어 CTest가
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
  반환하기 때문에 임시 공유 객체가 바로 파괴될 수 있었다. 이는 API 의미와
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

### 추가 리뷰. SPOT Context handler registry 보정

#### 발견한 위험 신호

- `.NET` sample은 Spot 안의 `Configure()`에서 `Context.Handlers.AddHandler`,
  `AddActorJoin`, `AddActorPacket`, `AddActorLeft`로 메시지 handler를 등록한다. C++에는
  `spot_context_t::register_packet`만 있어 packet 이름은 기록할 수 있었지만 실제 handler
  등록 owner가 없었다.
- 샘플이 handler 객체를 직접 생성해 호출하면 framework가 handler registration/dispatch를
  소유한다는 점을 확인하기 어렵다. 이 상태에서는 sample logic이 framework 밖에 남는다.
- C++에는 `.NET` assembly reflection이 없는데 무리하게 자동 추론을 복제하면 숨은 타입 규칙이
  생긴다.

#### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `register_packet`만 유지 | 변경이 작다 | `.NET Context.Handlers`와 다르고 handler owner가 없다 |
| reflection 유사 매크로로 handler 자동 수집 | 호출은 짧다 | C++ 타입 규칙과 빌드 의존성이 복잡해진다 |
| `spot_context_t::handlers()`와 typed registry 제공 | `.NET` 등록 모델을 유지하면서 C++ 타입을 명시할 수 있다 | template 인자가 더 필요하다 |

선택은 typed registry다. C++ 표면은 `.NET` 이름과 역할을 따르되, handler type, actor type,
message type, reply type을 template 인자로 명시한다.

#### 적용한 리팩토링

- `spot_handler_kind_t`, `spot_handler_descriptor_t`, `spot_handler_registry_t`를
  `contracts/spots/spot.hpp`에 추가했다.
- `spot_context_t::handlers()`를 추가하고 registry descriptor storage는
  `src/runtime/spots/spot_runtime.*`의 context state에 숨겼다.
- `add_handler`, `add_subscribe`, `add_actor_join`, `add_actor_packet`,
  `add_post_actor_joined`, `add_actor_left`, `add_actor_disconnected` 등록 표면을 추가했다.
- duplicate handler registration은 같은 kind, packet/topic, actor 조합 기준으로
  `request_protocol_error`를 낸다.
- Bingo/TicTacToe Spot sample에 `.NET Configure()`에 해당하는 `configure(context)`를 추가해
  handler를 직접 등록하게 했다.
- play sample executable과 sample parity test가 handler descriptor kind와 packet name을
  확인하도록 보강했다.

#### 남은 tradeoff

- C++는 `.NET`처럼 `AddHandler<THandler>()`만으로 handler interface generic argument를
  reflection으로 읽지 않는다. 자동 수집은 추후 macro/codegen 또는 explicit registration
  policy가 필요할 때 별도 goal에서 다룬다.

#### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_spot_runtime test_cpp_framework_sample_parity sample_cpp_framework_bingo_play sample_cpp_framework_tictactoe_play
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(spot_runtime|sample_parity)|sample_smoke_sample_cpp_framework_(bingo|tictactoe)_play' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. SPOT actor join/packet context shape 보정

### 발견한 위험 신호

- actor lifecycle handler는 Spot instance와 change result를 받도록 보정됐지만,
  actor join/packet handler는 여전히 `handle(request)` 또는 `handle(actor, message)`처럼
  일부 정보만 받는 형태가 남아 있었다.
- `.NET`의 `IZLinkSpotActorSendHandler<TSpot,TActor,TMessage>`는 `spot`, `actor`,
  `ZLinkSpotActorSendContext`, `message`를 받고,
  request handler는 `ZLinkSpotActorRequestContext`와 reply option을 받는다. C++에서
  context가 빠지면 packet name, content type, metadata, reply compression 같은 정책을
  application이 확인하거나 조정할 수 없다.
- 샘플과 parity test 일부가 handler를 1-인자로 직접 호출하고 있어 framework dispatch
  경로와 handler signature가 어긋났다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 `handle(message)` 호환 유지 | 변경이 작다 | `.NET` handler 계약과 다르고 context 정책이 새어 나온다 |
| context를 raw map/string 인자로 넘김 | 타입 수가 적다 | packet metadata와 reply option 의미가 흐려진다 |
| `spot_actor_send_context_t`, `spot_actor_request_context_t`를 추가 | `.NET` 의미를 C++ 타입으로 보존한다 | public contract 타입이 늘어난다 |

선택은 context 타입 추가다. C++은 reflection이 없으므로 actor join/packet 등록 API에
`TSpot`을 명시하고, handler 실행 시 typed Spot, actor, context, DTO를 함께 전달한다.

### 적용한 리팩토링

- `spot_actor_message_metadata_t`, `spot_actor_reply_options_t`,
  `spot_actor_send_context_t`, `spot_actor_request_context_t`를 추가했다.
- `add_actor_join`과 `add_actor_packet` 등록 API를 `THandler, TSpot, TActor, ...`
  형태로 보정했다.
- actor join invoker가 `handle(spot, actor, request)` shape를 우선 호출한다.
- actor packet invoker가 `handle(spot, actor, request_context, message)` 또는
  `handle(spot, actor, send_context, message)` shape를 우선 호출한다.
- `test_cpp_framework_spot_runtime`이 Spot state와 packet context가 handler에 실제로
  전달되는지 검증하도록 보강했다.
- Bingo/TicTacToe Play handler와 sample parity의 직접 handler 호출을 Spot/context-aware
  호출로 교체했다.

### 남은 tradeoff

- 일반 SPOT packet handler와 Entry Spot actor packet handler의 shape는 후속 리뷰에서
  `.NET`과 맞췄다. 이 절은 actor join/packet context 보정의 근거로 남긴다.
- actor context가 실제 actor gateway runtime과 결합되는 전체 e2e는 계속 확장해야 한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_spot_runtime test_cpp_framework_sample_parity sample_cpp_framework_bingo_play sample_cpp_framework_tictactoe_play
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_spot_runtime|test_cpp_framework_sample_parity|sample_smoke_sample_cpp_framework_(bingo|tictactoe)_play' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. Spot 일반 handler와 EntrySpot actor packet shape 보정

### 발견한 위험 신호

- C++ `add_handler`와 `add_subscribe`는 handler에 DTO만 넘겼다. `.NET`의
  `IZLinkSpotPacketHandler<TSpot,TMessage>`와
  `IZLinkSpotSubscriptionHandler<TSpot,TEvent>`는 Spot instance와 DTO를 함께 받는다.
  Spot을 받지 않으면 handler가 외부 참조나 전역 상태로 Spot state를 찾아야 한다.
- Bingo/TicTacToe Entry Spot 샘플은 actor가 보낸 request를 일반 Spot packet처럼
  `add_handler`로 등록했다. `.NET` sample은 Entry Spot에서도 actor request handler로
  등록하고, handler가 `EntrySpot`, actor, request context, DTO를 받는다.
- 일부 sample smoke는 예전 `handle(request)` 직접 호출을 유지했다. 이 상태에서는
  framework dispatch 경로와 sample 검증 경로가 서로 다른 API를 사용한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `handle(message)` fallback 유지 | 기존 sample 수정이 작다 | `.NET` 동일 contract가 강제되지 않고 얕은 handler가 계속 남는다 |
| 일반 packet에도 별도 context 타입 추가 | 확장 여지가 크다 | `.NET` 일반 Spot packet handler보다 표면이 넓어진다 |
| 일반 packet은 `handle(spot, message)`, EntrySpot actor packet은 `handle(spot, actor, context, message)`로 분리 | `.NET` handler 의미와 일치한다 | C++ 등록 API에 `TSpot` template 인자가 필요하다 |

선택은 세 번째 방식이다. C++은 reflection으로 handler interface의 `TSpot`을 읽을 수 없으므로
`add_handler`, `add_subscribe`, `add_actor_packet`에서 `TSpot`을 명시한다.

### 적용한 리팩토링

- `add_handler<THandler, TSpot, TMessage>`와
  `add_subscribe<THandler, TSpot, TEvent>`로 일반 Spot packet/subscription 등록 표면을
  보정했다.
- 일반 Spot packet invoker가 `handle(spot, message)`만 호출하도록 바꿨다. 기존
  `handle(message)` fallback은 남기지 않았다.
- actor join invoker의 `handle(actor, request)`, `handle(request)` fallback과 actor packet
  invoker의 `handle(actor, message)`, `handle(message)` fallback을 제거했다.
- post-joined/left lifecycle은 `handle(spot, actor, result)`, disconnected lifecycle은
  `.NET`처럼 `handle(spot, actor)`만 호출하도록 분리했다.
- `test_cpp_framework_spot_runtime`이 일반 Spot packet handler에서 Spot state를 수정하는지
  검증하도록 보강했다.
- Bingo Entry Spot의 `MatchBingoReq`와 TicTacToe Entry Spot의 `JoinMatchReq`를
  `add_actor_packet` 등록으로 바꿨다.
- Bingo `player_actor_t`에 `.NET` `PlayerActor.DisplayName`에 해당하는 display name을
  추가하고, match handler가 actor state를 기준으로 room join을 수행하게 했다.
- TicTacToe `join_match_handler_t`가 `EntrySpot`, actor, request context, DTO를 받도록
  변경했다. actor id는 actor object를 기준으로 결정한다.
- sample parity test와 Play smoke의 직접 handler 호출도 새 actor packet shape로 바꿨다.

### 남은 tradeoff

- 일반 Spot subscription의 실제 publish pump e2e는 당시 별도 확장 검증이 필요하다. 이번
  변경은 public registration과 typed invocation shape를 맞춘 범위다.
- EntrySpot actor packet handler는 sample-local room directory/room 객체를 DI로 받는다.
  실제 actor context의 `JoinSpot`까지 포함한 완전한 gateway e2e는 다음 actor gateway
  정렬 단계에서 더 보강해야 한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_spot_runtime test_cpp_framework_sample_parity sample_cpp_framework_bingo_play sample_cpp_framework_tictactoe_play
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_spot_runtime|test_cpp_framework_sample_parity|sample_smoke_sample_cpp_framework_(bingo|tictactoe)_play' --output-on-failure
```

## 추가 리뷰. SPOT actor lifecycle handler shape 보정

### 발견한 위험 신호

- C++ SPOT lifecycle handler는 `actor`만 받거나 sample-local 문자열/notification DTO를
  받는 형태가 섞여 있었다. `.NET`의 `IZLinkSpotPostActorJoinedHandler<TSpot,TActor>`와
  `IZLinkSpotActorLeftHandler<TSpot,TActor>`는 handler가 `spot`, `actor`,
  `ZLinkSpotActorChangeResult`를 함께 받는다.
- lifecycle handler가 Spot instance를 받지 못하면 handler 안에서 actor가 어떤 Spot에
  붙었는지 확인하거나, leave 원인이 `JoinSpot`인지 `JoinEntrySpot`인지 구분하는 정책이
  application 코드 밖으로 새어 나온다.
- C++은 `.NET`처럼 handler interface를 reflection으로 읽어 `TSpot`을 추론할 수 없다.
  `add_post_actor_joined<THandler,TActor>()` 표면을 유지하면 Spot instance를 typed로
  전달할 방법이 없다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| actor-only lifecycle handler 유지 | 등록 API가 짧다 | `.NET` lifecycle 의미와 다르고 Spot state 접근이 불가능하다 |
| handler가 void pointer Spot을 받음 | template 인자를 줄인다 | 호출자가 cast를 알아야 하므로 복잡성이 위로 올라간다 |
| lifecycle 등록에 `TSpot`을 명시 | typed Spot, actor, result를 모두 전달한다 | C++ template 인자가 하나 늘어난다 |

선택은 lifecycle 등록에 `TSpot`을 명시하는 방식이다. 언어 특성상 reflection이 없는 부분만
template 인자로 드러내고, dispatch/DI/serializer 실행은 framework 내부에 둔다.

### 적용한 리팩토링

- `spot_actor_change_kind_t`, `spot_actor_change_result_t`를 SPOT public contract에
  추가했다.
- `add_post_actor_joined`, `add_actor_left`, `add_actor_disconnected` 등록 API를
  `THandler, TSpot, TActor` template 형태로 보정했다.
- lifecycle invoker가 `spot`, `actor`, `spot_actor_change_result_t`를 typed handler에
  전달하도록 바꿨다.
- `test_cpp_framework_spot_runtime`이 joined, left, disconnected handler dispatch를
  실제로 호출하고 change kind 전달을 검증하도록 보강했다.
- Bingo/TicTacToe Play sample이 lifecycle handler까지 framework dispatch 경로로 실행하게
  보강했다.
- sample lifecycle handler는 notification DTO를 직접 받지 않고 Spot snapshot과 actor,
  change result에서 필요한 DTO를 만든다.

### 남은 tradeoff

- actor packet/request handler도 `.NET`은 Spot instance와 send/request context를 받는다.
  현재 C++ 보정은 lifecycle에 집중했다. 다음 리뷰에서는 actor packet/request handler의
  `TSpot`과 context 표면도 같은 기준으로 맞춰야 한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_spot_runtime test_cpp_framework_sample_parity sample_cpp_framework_bingo_play sample_cpp_framework_tictactoe_play
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_spot_runtime|test_cpp_framework_sample_parity|sample_smoke_sample_cpp_framework_(bingo|tictactoe)_play' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
git diff --check -- framework/languages/cpp
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
- route channel은 이제 `src/runtime/channels/route_channel_runtime.*`의 private runtime
  owner가 맡는다. 실제 CAPI router socket adapter 연결은 이후 backend substrate와 묶는다.

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
| runtime owner | `unreal-connector/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp`가 Unreal `Sockets`/`Networking` 기반 connection, Game Thread dispatch forwarding, lifecycle shutdown mapping을 소유한다. |
| public dependency | Unreal public header는 일반 C++ connector runtime header를 include하지 않는다. Unreal 엔진이 없는 CTest compile에서는 shim 타입으로 public API shape만 검증한다. |
| native leakage | public API는 `FString`, `FName`, `TArray<uint8>`, `TMap<FString,FString>` 기반 표면만 노출하고 receive loop, pending request table, frame sender, thread queue를 노출하지 않는다. |
| detail 사용 | 일반 C++ connector runtime include는 Unreal 구현에 두지 않는다. Unreal connector는 wire 의미만 공유하고 transport는 Unreal networking API로 구현한다. |
| state hiding | `UZLinkStreamConnector`는 opaque private runtime을 `unique_ptr`로 소유한다. |
| validation | `test_unreal_stream_connector`가 Engine 없는 compile smoke를 담당하고, `ZLink.StreamConnector.Loopback` Unreal Automation Test가 실제 `FSocket` loopback send/request/push를 확인한다. |

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
| Unreal 전용 facade를 제공하고 Private에서 Unreal runtime 구현 | Unreal 사용성에 맞고 transport 구현을 숨길 수 있다 | public/private adapter 코드가 필요하다 |
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
- `Private/ZLinkStreamConnector.cpp`에서 Unreal `Sockets` 기반 runtime을 소유하고, public
  header에는 일반 connector runtime 타입을 노출하지 않았다.
- POSD 리팩토링으로 private runtime 소유권을 raw pointer에서 `std::unique_ptr`로 바꾸었다.
- CMake smoke용 `zlink_unreal_stream_connector` target과 `test_unreal_stream_connector`를
  추가해 Unreal public header compile과 lifecycle smoke를 CTest에 등록했다.

### 남은 tradeoff

- 실제 Unreal `DECLARE_DYNAMIC_MULTICAST_DELEGATE` 기반 Blueprint assignable event와 Unreal
  logging category는 Unreal 엔진 빌드 환경에서 확장해야 한다. Goal 18에서는 public/private
  경계, Blueprint-callable shape, Game Thread manual dispatch path를 먼저 닫았다.
- JSON은 Unreal connector public API에 기본 `SendJson`/`RequestJson`으로 노출했고, 현재
  표면은 사용자가 넘긴 JSON payload를 STREAM `codec=json` frame으로 전송한다. typed UObject
  serializer helper는 다음 확장 대상이다.

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
| validation | `framework-sample-smoke` CTest label이 Bingo와 TicTacToe의 역할별 실행 파일을 확인한다. README도 샘플 역할과 포함 범위를 설명한다. |

### 발견한 위험 신호

- Goal 1 초기 README가 남아 있으면 샘플을 열어도 현재 리뷰 범위를 알기 어렵다.
- Bingo에서 `.NET` Bingo의 session stream 역할을 빼면 처리 packet 수와 역할 구성이 달라져
  sample parity를 검토할 수 없다.
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

- `samples/Bingo`를 `Shared`, `Client`, `Server/Registry`, `Server/Api`, `Server/Play`
  역할로 나누고 app/host, DI, hosted service, channel request/reply, publish/subscribe,
  callback submit, coroutine submit, user Spot, SPOT timer, monitoring, graceful shutdown,
  offload handler option을 확인하는 샘플로 채웠다.
- `samples/TicTacToe`를 `Shared`, `Client`, `Server/Registry`, `Server/Api`,
  `Server/Play`, `Server/Session` 역할로 나누고 STREAM endpoint, ActorGateway attach,
  Entry Spot, actor factory, session actor bind, relay, bound session push,
  actor join/move, disconnect cleanup을 확인하는 샘플로 채웠다.
- 샘플 이름은 `Bingo`, `TicTacToe` 그대로 유지하고 별도 접미사를 붙이지 않았다.
- Bingo에도 `.NET` Bingo와 같은 `Server/Session` 역할을 두고 session stream 흐름을
  포함했다.
- POSD 리팩토링으로 초기 README를 제거하고 각 샘플의 리뷰 목적과 포함 범위를
  현재 코드와 맞게 갱신했다.

### 남은 tradeoff

- 샘플은 smoke와 사용성 리뷰 목적이므로 private runtime 내부 상태를 검증하지 않는다.
  내부 relay frame copy, pending queue, timer fire-count 같은 세부는 unit/integration test가
  검증한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

### 추가 리뷰. Client E2E 서버 로그 검증

#### 발견한 위험 신호

- Bingo/TicTacToe client sample은 실제 STREAM server를 띄우고 connector로 request/reply와
  push notification을 주고받았지만, CTest는 executable exit code만 확인했다. 서버 로그가
  비어 있거나 기대 packet 흐름과 달라도 client result가 우연히 성공하면 놓칠 수 있었다.
- 서버 로그 파일 이름은 README에 적혀 있었지만, bind, receive, reply, push 흐름이
  회귀 테스트의 observable contract로 고정되어 있지 않았다.
- 전체 CTest 순서에서 Bingo loopback server가 같은 inbound connection에 reply frame과
  push frame을 연속 `submit()`하다가 `EAGAIN`을 만났다. 이는 샘플의 논리 흐름 문제가
  아니라 STREAM socket write readiness를 샘플 서버가 직접 떠안은 문제다.

#### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 client smoke만 유지 | 테스트가 빠르고 단순하다 | 서버 쪽 실제 frame 흐름을 증명하지 못한다 |
| 샘플 코드 안에서 로그 문자열을 직접 assert | 실행 파일 하나로 끝난다 | 샘플 사용성 코드에 테스트 전용 검증 로직이 섞인다 |
| CTest wrapper가 샘플을 실행한 뒤 로그 파일을 검증 | 샘플 코드는 사용자 흐름으로 유지하고 회귀 검증을 분리한다 | CMake script test가 하나 더 필요하다 |
| reply와 push를 별도 public 동작으로 바꿈 | 연속 submit 문제가 줄어든다 | `.NET` 샘플의 reply 후 bound-session push 논리 흐름과 달라질 수 있다 |
| 같은 STREAM write에 reply frame과 push frame을 순서대로 담음 | `.NET`과 같은 reply 후 push frame 순서를 유지하면서 write readiness 문제를 숨기지 않는다 | loopback server helper가 frame batching을 알아야 한다 |

선택은 CTest wrapper 방식이다. 샘플 executable은 계속 public API 사용성만 보여 주고,
회귀 테스트는 실행 뒤 서버 로그를 읽어 실제 bind, receive, reply, push 흐름과 packet
이름, 최소 처리 횟수를 검증한다.

연속 submit 실패는 frame batching으로 정리했다. `.NET` 샘플처럼 request reply와
bound-session push는 논리적으로 분리되어 있고, 로그도 `reply` 다음 `push`를 그대로 남긴다.
다만 C++ loopback server는 같은 STREAM connection에 두 frame을 순서대로 쓰는 transport
세부를 한 번의 write로 처리한다. 이는 public framework/connector API 차이가 아니라
STREAM wire가 여러 frame을 한 byte stream에 실을 수 있다는 구현 세부다.

#### 적용한 리팩토링

- `tests/Zlink.Framework.E2ETests/Samples/verify_sample_client_log.cmake`를 추가해 샘플 executable 실행, 서버 로그
  생성 여부, 기대 문자열, 최소 receive/reply/push line 수를 검증하게 했다.
- Bingo client E2E 로그 테스트가 `AuthenticateReq`, `MatchBingoReq`,
  `StartBingoGameReq`, `LeaveRoomReq`, `PlayerJoinedNotify`, `BingoGameEndedNotify`와
  10개 receive, 9개 reply, 10개 push 흐름을 확인하도록 등록했다.
- TicTacToe client E2E 로그 테스트가 `AuthenticateReq`, `JoinMatchReq`, `PlaceMarkReq`,
  `TurnChangedNotify`와 9개 receive, 9개 reply, 9개 push 흐름을 확인하도록 등록했다.
- `framework-sample-log` CTest label을 추가해 로그 검증만 따로 실행할 수 있게 했다.
- sample loopback server helper에 reply frame과 push frame을 같은 STREAM write에 담는
  `send_stream_reply_and_push`를 추가하고, Bingo/TicTacToe client sample server가 이를
  사용하도록 바꿨다.

#### 남은 tradeoff

- 로그 검증은 샘플 loopback server가 본 STREAM frame 흐름을 검증한다. 별도 OS process
  orchestration은 `.NET` MultiProcess 테스트의 역할이고, C++ 샘플에서는 connector와 실제
  TCP server 사이의 observable packet flow를 우선 고정한다.
- loopback server의 frame batching은 샘플 서버 전용 transport helper다. framework public
  API와 샘플 client의 호출 흐름은 `.NET`과 같이 request reply와 notification push를
  별도 의미로 유지한다.

#### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target sample_cpp_framework_bingo_client sample_cpp_framework_tictactoe_client
ctest --test-dir framework/languages/cpp/build -L framework-sample-client-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build -R sample_e2e_log_sample_cpp_framework_bingo_client --output-on-failure --repeat until-fail:10
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Connector E2E 검증 라벨 보정

### 발견한 위험 신호

- 구현 계획의 Goal 17 검증 명령은 `connector-e2e` 라벨을 요구하지만, CTest 라벨 목록에는
  해당 라벨이 없었다. 이 상태에서는 connector unit/integration과 실제 sample client E2E를
  분리해서 재실행할 수 없다.
- 실제 connector E2E 증거는 Bingo/TicTacToe client sample의 서버 로그 검증에 있었지만,
  라벨이 `framework-sample-client-e2e`와 `framework-sample-log`에만 묶여 있어 Goal 17의
  connector 검증 경로와 연결되지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `test_cpp_stream_connector`에만 `connector-e2e` 라벨 추가 | 라벨 추가가 작다 | local test runtime 검증과 실제 sample endpoint E2E를 구분하지 못한다 |
| sample client 로그 검증 테스트에 `connector-e2e` 라벨 추가 | 실제 connector가 sample STREAM endpoint와 메시지를 주고받는 증거에 라벨이 붙는다 | 하나의 테스트가 sample과 connector 라벨을 함께 가진다 |
| 별도 connector E2E executable 작성 | 책임이 가장 분명하다 | 이미 같은 흐름을 검증하는 sample E2E와 중복된다 |

선택은 sample client 로그 검증 테스트에 `connector-e2e` 라벨을 추가하는 것이다. 같은 실행이
사용자 리뷰용 sample E2E이면서 connector가 실제 endpoint에 붙는 회귀 증거이므로, 라벨을
겹쳐 두는 편이 중복 테스트보다 단순하다.

### 적용한 리팩토링

- `sample_e2e_log_sample_cpp_framework_bingo_client`에 `connector-e2e` 라벨을 추가했다.
- `sample_e2e_log_sample_cpp_framework_tictactoe_client`에 `connector-e2e` 라벨을 추가했다.
- `ctest --print-labels`에서 Goal 17 검증 라벨이 보이도록 CTest 표면을 맞췄다.

### 남은 tradeoff

- `connector-e2e` 라벨은 sample client E2E와 같은 테스트를 가리킨다. 이는 connector가
  framework sample endpoint에 붙는 실제 흐름을 검증하기 위한 의도된 중복 라벨이다.

### 재실행할 검증 명령

```bash
ctest --test-dir framework/languages/cpp/build -L connector-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build --print-labels
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. Connector runtime option 실제 동작 보정

### 발견한 위험 신호

- Stream Connector의 `request_timeout` option이 public contract에 있었지만 runtime에서는
  `(void) timeout`으로 무시되고 있었다. 서버가 response frame을 보내지 않으면 request
  호출자가 무기한 block될 수 있어 실시간 messaging client로 사용할 수 없다.
- reconnect와 heartbeat는 options와 파일만 있었고 실제 connect retry나 frame 전송으로
  연결되지 않았다. 이는 파일 분리는 되어 있지만 구현체가 없는 얕은 모듈이다.
- request read를 단순 blocking read로 두면 timeout과 dispatch responsiveness를 보장하기
  어렵다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| background receive thread를 추가 | heartbeat와 timeout 처리가 명확하다 | 현재 connector의 manual dispatch 모델과 thread 소유권이 커진다 |
| request path만 timed non-blocking read로 보정 | public API를 바꾸지 않고 무기한 block을 제거한다 | background receive loop 수준의 완전한 async runtime은 아니다 |
| heartbeat 전용 thread 추가 | 주기 전송이 정확하다 | 사용자가 쓰지 않는 connector에도 thread 비용이 생긴다 |
| `dispatch()` 경로에서 interval이 지난 heartbeat만 전송 | background thread 없이 manual dispatch 모델과 맞는다 | dispatch가 호출될 때 heartbeat가 진행된다 |
| reconnect를 connect retry loop로 구현 | 현재 sync connect contract 안에서 재시도 의미를 닫는다 | async backoff cancellation은 당시 없다 |

선택은 timed non-blocking request read, connect retry loop, opportunistic heartbeat다. 이렇게 하면
현재 connector의 sync/coroutine facade와 manual dispatch 모델을 유지하면서 무기한 block과
옵션 무시 문제를 없앨 수 있다.

### 적용한 리팩토링

- `submit_request(...)`가 `request_timeout` deadline을 기준으로 socket `available()`과
  `read_some()`을 사용해 frame prefix/header/payload를 읽도록 바꿨다.
- timeout이 지나면 pending request를 제거하고 `request_timeout` error를 돌려준다.
- `connect()`가 `reconnect.max_attempts`, `initial_delay`, `max_delay`, `backoff_factor`에 따라
  실패한 endpoint에 재시도하고, 두 번째 시도부터 `reconnecting` state를 발행하게 했다.
- `dispatch()` 경로에서 heartbeat interval이 지난 경우 `$zlink.heartbeat.ping` control frame을
  전송하도록 했다. 별도 background thread는 만들지 않았다.
- connector unit test에 request timeout, heartbeat control frame, reconnecting state 검증을
  추가했다.

### 남은 tradeoff

- heartbeat는 background scheduler가 아니라 `dispatch()`에 묶인 opportunistic heartbeat다.
  이는 현재 manual dispatch connector 모델에서 thread 비용을 만들지 않기 위한 선택이다.
- connect timeout은 Boost.Asio sync connect 실패를 error로 매핑한다. 세밀한 per-attempt
  async timeout은 이후 async connector runtime이 필요할 때 private runtime 안에서 확장한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_cpp_stream_connector --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-unit --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-integration --output-on-failure
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. .NET 대응 테스트 폴더 분류 보정

### 발견한 위험 신호

- .NET framework는 `Zlink.Framework.UnitTests`, `Zlink.Framework.ContractTests`,
  `Zlink.Framework.E2ETests`, `Systems.Zlink.Stream.Connector.Tests`처럼 테스트 프로젝트
  단위가 기능과 검증 성격을 드러낸다. C++은 `tests/unit`, `tests/contract`, `tests/samples`,
  `tests/package`에 섞여 있어 .NET 기준으로 어느 회귀 축을 비교해야 하는지 바로 보이지
  않았다.
- CTest label은 충분히 세분화되어 있었지만, 폴더 구조가 label 의미를 따라가지 못했다.
  파일 분류만 보고는 framework contract test와 connector transport test, sample e2e test의
  책임 경계가 약했다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 `unit/contract` 디렉터리 유지 | 변경이 작다 | `.NET` 테스트 프로젝트 구조와 직접 비교하기 어렵다 |
| CTest label만 보강 | 실행 필터는 충분하다 | 파일 분류 요구를 만족하지 못한다 |
| `.NET` 테스트 프로젝트 이름에 맞춰 C++ 테스트 디렉터리 재분류 | 구조 비교와 파일 책임이 명확하다 | CMake 경로와 layout contract를 함께 바꿔야 한다 |

선택은 테스트 디렉터리를 `.NET` 테스트 프로젝트 이름에 맞춰 재분류하는 것이다. C++은
프로젝트 파일 대신 CMake target과 CTest label을 쓰지만, 디렉터리 이름은 `.NET`의 검증 축을
그대로 따라가게 한다.

### 적용한 리팩토링

- `tests/Zlink.Framework.ContractTests`에 public contract/layout/sample parity 검증을 옮겼다.
- `tests/Zlink.Framework.UnitTests`에 framework runtime 단위 테스트를 옮겼다.
- `tests/Zlink.Framework.E2ETests/Samples`에 sample client/server log e2e verifier를 옮겼다.
- `tests/Systems.Zlink.Stream.Connector.Tests`에 C++ Stream Connector test를 옮겼다.
- `tests/Zlink.Unreal.Stream.Connector.Tests`에 Unreal connector compile/smoke test를 옮겼다.
- `tests/Zlink.Framework.PackageTests`에 install consumer package 검증을 옮겼다.
- CMake test source 경로와 layout contract의 구조 검증을 새 디렉터리로 맞췄다.

### 남은 tradeoff

- C++은 .NET처럼 test project 파일을 만들지 않고, CMake target과 CTest label로 실행 단위를
  관리한다. 디렉터리 이름은 `.NET`과 맞추되 build system은 C++ 방식으로 유지한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_layout_contract
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_layout_contract --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. Configuration builder owner 분리

### 발견한 위험 신호

- .NET framework는 configuration runtime 안에서도 `Runtime/Configuration/Builders`를
  별도 하위 owner로 둔다. C++은 JSON/env/CLI configuration builder 구현이
  `src/runtime/configuration/configuration.cpp`에 바로 있었기 때문에, 파일 이름만 보면
  configuration model 구현인지 builder/parser 구현인지 구분하기 어려웠다.
- configuration builder는 app/host public configuration surface의 핵심 진입점이다. 이
  구현이 flat하게 남아 있으면 이후 route/spot/stream builder 구현이 같은 디렉터리에 섞일
  가능성이 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 `configuration.cpp` 유지 | 변경이 작다 | `.NET`의 builder owner 분리와 다르고 역할이 모호하다 |
| 모든 configuration 파일을 `builders`로 이동 | 폴더 이름은 맞는다 | DI service container 같은 비-builder 구현까지 잘못 분류된다 |
| configuration builder 구현만 `configuration/builders`로 이동 | 역할이 정확하고 .NET 구조와 가까워진다 | CMake와 layout contract를 갱신해야 한다 |

선택은 configuration builder 구현만 이동하는 것이다. `services.cpp`는 DI container runtime
구현이므로 builder owner가 아니다.

### 적용한 리팩토링

- `framework/src/runtime/configuration/configuration.cpp`를
  `framework/src/runtime/configuration/builders/configuration_builder.cpp`로 옮겼다.
- CMake source path를 새 위치로 갱신했다.
- layout contract가 `configuration/builders`와 `configuration_builder.cpp` 존재를 검증하게
  보강했다.

### 남은 tradeoff

- C++은 .NET처럼 channel/route/spot/stream builder 파일이 모두 별도 구현 파일로 분리되어
  있지는 않다. 현재 실제 구현 owner가 있는 configuration builder부터 분리하고, 새로운
  runtime builder 구현이 생기면 같은 `configuration/builders` 아래에 둔다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_layout_contract
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_layout_contract --output-on-failure
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. TicTacToe sample discovery/topology 정렬

### 발견한 위험 신호

- `.NET` TicTacToe Api/Play/Session sample의 host factory는 모두
  `UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint))`를 사용한다.
  C++ TicTacToe sample은 registry endpoint가 topology에 없고, Api host가 Play channel client를
  직접 endpoint로 연결했다. 이 상태에서는 C++ sample이 discovery 기반 channel 구성이라는
  `.NET` 샘플의 핵심 흐름을 보여 주지 못한다.
- Bingo C++ sample은 `api_server_framework.hpp`로 framework 설정을 host factory 밖에
  분리했지만, TicTacToe API는 설정이 `api_server_host_factory.hpp` 안에 남아 있었다. 같은
  sample family 안에서도 파일 분류 수준이 달랐다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현재 direct endpoint client 유지 | 단순하고 smoke가 빠르다 | `.NET` discovery/channel 사용 흐름과 다르다 |
| route mesh 전체를 즉시 재현 | `.NET` sample과 가장 가깝다 | 당시 C++ high-level options가 route mesh 전체를 한 번에 표현하지 못했다 |
| registry/discovery/channel client 흐름부터 맞춤 | 당시 public C++ API로 구현 가능하고 사용성 차이를 줄인다 | route mesh/reconnect endpoint 전체 parity가 다음 반복 리뷰 대상이었다 |

선택은 registry/discovery/channel client 흐름부터 맞추는 것이다. 이는 framework public API를
늘리지 않고 `.NET` 샘플의 가장 눈에 보이는 host configuration 흐름을 맞추는 변경이다.

### 적용한 리팩토링

- TicTacToe `sample_topology_t`에 `registry_pub_endpoint`와 `registry_router_endpoint`를
  추가했다.
- TicTacToe registry host factory가 hard-coded endpoint 대신 topology 값을 받도록 바꿨다.
- TicTacToe API 설정을 `api_server_framework.hpp`로 분리해 Bingo와 같은 host factory 깊이를
  유지했다.
- TicTacToe Api/Play/Session host factory가 `options.discovery().add(...)`를 사용하게 했다.
- TicTacToe Api/Session client channels는 `.NET`처럼 discovery 기반 client 표면으로 보이게
  직접 play endpoint 연결을 제거했다.
- sample parity test가 TicTacToe host factory의 discovery 사용과 registry topology 사용을
  검증하게 했다.

### 후속 보정

이후 반복 리뷰에서 `options.route_mesh_channel(...)`,
`options.use_registry_spot_remote_addresses(...)`, `options.spot_mesh(...)`를 추가해
`.NET`의 route mesh, registry backed Spot remote address, Spot mesh 설정을 C++ framework
options 표면에서도 표현하도록 보정했다. 그래서 이 단계의 남은 tradeoff였던 route mesh/
spot mesh builder 부재는 현재 draft 기준으로 해소된 상태다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_sample_parity
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_sample_parity --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke --output-on-failure
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. Framework options route/spot mesh 표면 보정

### 발견한 위험 신호

- `.NET` framework options에는 `UseRegistrySpotRemoteAddresses`,
  `AddRouteMeshChannel`, `AddSpotMesh`가 있어 host factory가 route mesh와 Spot mesh를
  낮은 수준 socket 설정 없이 표현한다. C++는 같은 기능이 `zlink_builder_t`와
  `spot_node_builder_t` 쪽에 흩어져 있어 샘플 설정이 `.NET`보다 얕고 직접적이었다.
- TicTacToe Session/Play sample은 registry 기반 Spot remote address와 route mesh channel을
  보여 줘야 하지만, C++ sample은 stream/client channel 중심으로만 구성되어 framework 검증
  샘플로서 약했다.
- fluent builder가 내부적으로 여러 번 `route_channel(...)`을 적용하면 registry route channel
  이름이 중복될 수 있었다. 이는 builder pattern을 쓰는 사용자가 호출 순서를 알아야 하는
  얕은 모듈 신호다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 낮은 수준 `zlink_builder_t` 사용을 가이드 | public 변경이 작다 | `.NET`과 같은 host factory 가독성을 만들 수 없다 |
| 샘플에 helper 함수만 추가 | 샘플 코드는 짧아진다 | framework API 자체의 얕은 표면은 그대로 남는다 |
| framework options에 route/spot mesh builder 추가 | 호출자가 보는 설정이 `.NET`과 같은 수준으로 올라간다 | builder 상태 재적용과 회귀 테스트가 필요하다 |

선택은 framework options builder 추가다. route mesh, registry Spot remote address, Spot mesh는
애플리케이션 설정자가 이해해야 하는 개념이고, native socket 조립은 framework 내부로 내려야
한다.

### 적용한 리팩토링

- `zlink_framework_options_t::route_mesh_channel(name)`을 추가해 route channel bind와 manual
  connection, routing id를 framework options 표면에서 설정하게 했다.
- `zlink_framework_options_t::use_registry_spot_remote_addresses(...)`를 추가해 Spot remote
  address resolver 기본값을 framework options에서 켤 수 있게 했다.
- `zlink_framework_options_t::spot_mesh(name).node(nodeName)`을 추가해 Spot node discovery
  channel과 node 설정을 한 곳에서 표현하게 했다.
- `spot_node_options_builder_t`에 `accept_routes_from_channel(...)`과
  `attach_channel_client(...)`를 추가해 `.NET` sample의 `AcceptSpotRoutesFromChannel`과
  `AttachChannelClient` 의미를 C++ native style로 표현했다.
- `spot_node_options_builder_t::enable_router(...)`와 `enable_pub_sub(...)`를 추가해
  `.NET` sample의 `EnableRouter(...)`, `EnablePubSub(...)` 역할 구분을 C++ host factory에도
  드러나게 했다.
- `enable_router(endpoint, routing_id)`와 `enable_pub_sub(endpoint, routing_id)` overload를
  추가해 `.NET` sample topology의 `PlayRid`, `SessionRouterRid`, `SessionPubRid` 역할을 C++
  sample에서도 보존하게 했다.
- `route_mesh_channel(...).routing_id(...)`와 route channel runtime routing id snapshot을
  추가해 `.NET`의 `ConfigureRouting(routing => routing.RoutingId = ...)` 정보를 C++ route
  mesh registration에서도 잃지 않게 했다.
- `zlink_builder_t::route_channel(...)` 재적용 시 registry route channel 이름이 중복되지 않게
  보정했다.
- Bingo/TicTacToe Play/Session sample이 새 framework options 표면을 사용하도록 바꿨고,
  sample parity와 registry topology test가 이 표면의 snapshot 의미를 검증하게 했다.

### 남은 tradeoff

- 현재 C++ Spot runtime snapshot은 router/pubsub endpoint와 routing id 역할을 구분하지만,
  socket 세부 option builder까지 별도 객체로 나누지는 않는다. socket option 세부 builder는
  실제 framework runtime에서 native SPOT socket 초기화를 확장할 때 추가한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_registry_topology test_cpp_framework_sample_parity
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(sample_parity|registry_topology|layout_contract)' --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-client-e2e --output-on-failure
git diff --check -- framework/languages/cpp
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
| validation | full build, 당시 전체 CTest, public header dependency 검색, `git diff --check`를 실행했다. |

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
닫은 뒤 당시 계획의 전체 goal 최종 감사와 commit/push를 수행한다.

### 적용한 리팩토링

- `cpp-framework-implementation-plan.ko.md`의 Goal 20 완료 기준을 Goal 1-19 회귀 게이트로
  정리했다.
- Goal 21 extension boundary 항목은 Goal 21에서 닫고 그 뒤 당시 계획의 전체 goal 최종 감사를 다시
  수행한다고 문서화했다.
- `cmake --build framework/languages/cpp/build`로 framework, connector, Unreal connector,
  samples, tests 전체 target을 빌드했다.
- `ctest --test-dir framework/languages/cpp/build --output-on-failure`로 당시 등록된
  테스트 전체를 실행했다.
- public header에서 테스트 라이브러리, 외부 extension dependency, runtime/private include
  누출을 검색했다.
- `contracts/detail/call_facade.hpp`를 확인해 현재 detail 영역이 call forwarding helper에
  머물고 있음을 확인했다.

### 남은 tradeoff

- 이 항목은 Goal 20 실행 시점의 회귀 게이트 기록이다. 당시에는 Goal 21 extension
  boundary가 남아 있었고, 이후 Goal 21과 추가 리뷰 항목에서 extension boundary와 최종
  감사 보정을 이어서 닫았다.

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
| `.NET` 대응 확인 | extension은 core framework public API 위에 붙는 별도 산출물이다. `.NET` framework core와 같은 channel, monitoring, logging 계약을 사용하되 Kafka, gRPC, YAML, FlatBuffers 구현 dependency는 core target에 넣지 않는다. HTTP hosting은 이후 application framework 기준에서 core 기능으로 재분류했다. |
| contract owner | extension public contract는 `extensions/include/zlink/framework/extensions/*`가 소유한다. core framework contract owner를 새로 만들지 않는다. |
| runtime owner | Goal 21은 implementation boundary를 닫는 goal이므로 extension target은 `INTERFACE` target으로 시작한다. 실제 bridge runtime이 필요해질 때는 extension별 private runtime owner를 둔다. |
| public dependency | extension public header는 framework public contract header만 include하고 Kafka, gRPC, YAML, FlatBuffers header를 include하지 않는다. HTTP hosting public header도 Beast/Asio 타입을 노출하지 않는다. |
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
- extension public header가 실제 Kafka, gRPC, YAML, FlatBuffers header를 include하면
  core framework를 쓰는 사용자에게 불필요한 dependency가 번진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| extension을 core framework target에 직접 구현 | 사용자가 target 하나만 링크한다 | Kafka, gRPC, YAML, FlatBuffers dependency가 core에 섞인다 |
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

- Goal 21은 extension boundary를 닫는 단계이므로 Kafka, gRPC, YAML, FlatBuffers의
  실제 외부 library 호출 구현은 포함하지 않는다. 이후 구현하더라도 각 extension private
  runtime target 안에 숨겨야 하며 core framework target에는 추가하지 않는다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_framework_extensions
ctest --test-dir framework/languages/cpp/build -L framework-extension --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## 추가 리뷰. 문서 반영 누락 보정

### 발견한 위험 신호

- `contracts/assembly/*`와 `src/runtime/backend/contracts/*`가 implementation plan의
  interface/implementation owner matrix에는 있었지만 실제 tree에는 없었다.
- framework와 connector export target은 build tree alias 이름과 install tree target 이름이
  달라질 수 있었다.
- 테스트 정책은 GoogleTest/GoogleMock을 기본 harness로 정했지만 CMake test target은 일반
  executable만 링크하고 있었다.
- `load_json()`과 `load_env()`가 실제 값을 읽지 않고 경로와 prefix만 저장하고 있었다.
- `app_t::run()`이 hosted service를 시작한 뒤 바로 stop해서 long-running host lifecycle을
  표현하지 못했다.
- Unreal connector public header에 Blueprint assignable connection state event가 없었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 문서를 현재 skeleton 수준으로 낮추기 | 구현 변경이 적다 | 사용자가 요구한 완성 스펙과 어긋난다 |
| 실제 코드와 package/test 경계를 문서 수준으로 올리기 | draft 기준과 구현이 맞는다 | CMake, host lifecycle, tests를 함께 고쳐야 한다 |

선택은 코드를 문서 수준으로 올리는 것이다. draft는 구현 범위를 줄이지 않는다는 기준을
이미 명시하고 있으므로, 누락된 owner와 runtime behavior를 추가 구현으로 닫는다.

### 적용한 리팩토링

- `contracts/assembly/assembly.hpp`, `framework/assembly.hpp`, `src/runtime/host/assembly.cpp`,
  `src/runtime/backend/contracts/backend_runtime_contract.hpp`,
  `connector/src/runtime/backend/contracts/connector_backend_contract.hpp`를 추가했다.
- framework package와 stream connector package의 install/export config를 분리하고,
  installed consumer가 `zlink::framework`, `zlink::stream_connector`,
  `zlink::framework_extension_*` target을 그대로 사용할 수 있게 `EXPORT_NAME`과 config
  파일을 추가했다.
- framework public serializer가 요구하는 JSON codec header와 native runtime을 install
  package에 포함했다.
- GoogleTest/GoogleMock을 CMake test harness로 연결하고, GoogleMock boundary를 확인하는
  `test_cpp_framework_gtest_harness`를 추가했다.
- `load_json()`은 JSON 파일을 읽어 flat configuration model로 합치고, `load_env()`는
  prefix가 맞는 환경 변수를 model에 반영하도록 수정했다.
- `app_t::run()`은 signal handler를 등록하고 stop 요청이 들어올 때까지 hosted service를
  유지한 뒤 역순 stop을 수행하도록 수정했다.
- Unreal connector에 Blueprint assignable connection state delegate를 추가하고 state 변경
  callback에서 broadcast하도록 수정했다.

### 남은 tradeoff

- GoogleTest가 시스템에 없으면 CMake configure 단계에서 FetchContent로 GoogleTest를
  가져온다. 이는 개발 의존성에만 적용되며 public framework header와 install runtime
  dependency에는 노출하지 않는다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
cmake --install framework/languages/cpp/build --prefix /tmp/zlink_framework_cpp_install
cmake -S /tmp/zlink_framework_cpp_consumer -B /tmp/zlink_framework_cpp_consumer/build -DCMAKE_PREFIX_PATH=/tmp/zlink_framework_cpp_install
cmake --build /tmp/zlink_framework_cpp_consumer/build
git diff --check -- framework/languages/cpp
# draft 금지 표현 검색과 public header dependency 누출 검색을 실행했다.
```

## 추가 리뷰. 샘플 품질 보정

### 발견한 위험 신호

- 기존 C++ `Bingo`와 `TicTacToe` 샘플은 CTest smoke 성격이 강해서 `.NET` 샘플처럼
  registry/API/play/session/client 역할과 domain flow를 리뷰하기 어려웠다.
- Bingo 샘플은 channel/SPOT 기능을 나열했지만 authenticate, match allocation, room state,
  notification inbox 같은 실제 메시징 프로그램의 흐름이 약했다.
- TicTacToe 샘플은 ActorGateway relay API 호출은 있었지만 create match, join, place mark,
  turn changed, game ended 같은 게임 흐름이 거의 없었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 smoke 샘플 유지 | 테스트가 짧다 | `.NET` 샘플과 비교하면 리뷰 가치가 낮다 |
| `.NET`처럼 여러 실행 파일로 즉시 분리 | 구조가 가장 비슷하다 | sample smoke target이 늘어난다 |
| 단일 실행 파일 안에서 역할을 명확히 분리 | target 수가 적다 | 실제 프로세스 역할 분리가 보이지 않는다 |

선택은 `.NET`처럼 여러 실행 파일로 즉시 분리하는 것이다. 샘플은 private runtime을 쓰지
않고 public API와 샘플 domain code만 사용한다.

### 적용한 리팩토링

- `Bingo`를 `Shared`, `Client`, `Server/Registry`, `Server/Api`, `Server/Play`로 나누고
  topology, authenticate handler, match allocation, room join/start/draw, notification
  inbox, publish callback/coroutine, SPOT timer 흐름이 보이도록 재작성했다.
- `TicTacToe`를 `Shared`, `Client`, `Server/Registry`, `Server/Api`, `Server/Play`,
  `Server/Session`으로 나누고 session stream host, ActorGateway attach, authenticate
  actor, create match, join, place mark, turn changed, game ended, bound session push,
  disconnect cleanup 흐름이 보이도록 재작성했다.
- CMake sample smoke를 역할별 실행 파일로 등록했다.
- 두 샘플 README를 실제 샘플 구조와 맞게 갱신했다.

### 남은 tradeoff

- 현재 샘플 smoke는 각 역할 실행 파일을 독립 실행해 public API 사용성과 domain flow를
  확인한다. 여러 프로세스를 동시에 띄워 실제 네트워크 통합 흐름을 확인하는 run script는
  후속 통합 테스트 단계에서 추가한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke --output-on-failure
```

## 추가 리뷰. 샘플 parity 보정

### 발견한 위험 신호

- C++ 샘플은 `.NET` 샘플과 폴더 역할은 맞췄지만 packet 이름, request/response 계약,
  notification 수, handler 수가 부족했다.
- Bingo C++ 샘플에는 `.NET` Bingo에 있는 `Server/Session` 역할이 없어서 session packet
  dispatch 흐름을 리뷰할 수 없었다.
- 샘플 parity를 고정하는 테스트가 없어서 다시 단순 smoke 수준으로 퇴행할 수 있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| README만 `.NET` 수준이라고 설명 | 수정이 작다 | 실제 샘플이 다르면 리뷰할 수 없다 |
| 샘플 안에서만 메시지를 흉내 | 빠르게 맞출 수 있다 | framework 표면 누락을 숨길 수 있다 |
| 샘플 계약과 framework 표면을 함께 보강 | 실제 개발 기준이 된다 | 샘플과 CMake, 테스트를 함께 수정해야 한다 |

선택은 샘플 계약과 framework 표면을 함께 보강하는 것이다. 현재 필요한 stream, handler,
ActorGateway 표면은 framework public API로 존재하므로 샘플은 private runtime을 우회하지
않고 public API만 사용한다.

### 적용한 리팩토링

- Bingo shared contract를 `.NET` Bingo의 `AuthenticateReq/Res`,
  `AuthenticatePlayerReq/Res`, `EnsurePlayerActorReq/Res`, `MatchBingoReq/Res`,
  `MatchBingoApiReq/Res`, `AllocateBingoRoomReq/Res`, `BingoRoomJoinReq/Res`,
  `StartBingoGameReq/Res`, `LeaveRoomReq/Res`, notification 5종으로 확장했다.
- Bingo에 `Server/Session` sample executable을 추가하고 CTest sample smoke에 등록했다.
- TicTacToe shared contract를 `.NET` TicTacToe의 `AuthenticateReq/Res`,
  `AuthenticateActorReq/Res`, `EnsurePlayerActorReq/Res`, `CreateMatchReq/Res`,
  `CreateMatchRoomReq/Res`, `JoinMatchReq/Res`, `PlaceMarkReq/Res`, notification 3종으로
  확장했다.
- `test_cpp_framework_sample_parity` contract test를 추가해 `.NET` 샘플과 맞춰야 하는
  packet 이름과 핵심 handler 흐름을 고정했다.
- 샘플 README와 draft 문서를 실제 역할 수와 packet/handler parity 기준에 맞게 수정했다.

### 남은 tradeoff

- 현재 sample smoke는 역할별 executable을 독립 실행한다. 여러 프로세스를 동시에 띄워
  실제 네트워크 통합 흐름까지 확인하는 run script는 별도 통합 테스트 단계에서 추가한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Route public client facade 연결

### 발견한 위험 신호

- `.NET`은 `IZLinkRouteClient`와 `ZLinkRouteClient`가 routed send/request public 표면을
  제공하지만, C++는 테스트와 샘플이 `route_channel_runtime_t` detail 타입을 직접 써야 했다.
- route channel builder가 public으로 연결된 뒤에도 outbound call 표면이 없으면 사용자는
  route runtime manager, serializer registry, envelope codec의 조합을 알아야 한다.
- 기존 `call_facade_t`는 즉시 result를 담는 얕은 helper라 `.packet_name().timeout().submit()`
  순서가 실제 route runtime 호출에 반영되기 어렵다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| detail `route_channel_runtime_t`를 public에 노출 | 구현이 빠르다 | route connection set과 envelope codec 지식이 public으로 샌다 |
| 기존 `send_call_t`/`request_call_t`만 재사용 | 파일 수가 적다 | call 생성 시점에 이미 submit이 끝나 lazy option 변경을 반영하지 못한다 |
| route 전용 public call object와 pimpl state 추가 | `.NET` route client 책임과 맞고 내부 runtime을 숨긴다 | native router adapter 연결은 backend seam 뒤에서 별도 구현해야 한다 |

선택은 route 전용 public call object다. `route_client_t`는 builder에서 만들고,
serializer registry와 route runtime state를 내부 state로 들고 있다. public template method는
typed payload만 받고, envelope encode와 route runtime lookup은 `.cpp` 구현으로 내려간다.

### 적용한 리팩토링

- public `route_client_t`, `route_send_call_t`, `route_request_call_t`,
  `typed_route_request_call_t<TReply>`를 추가했다.
- `zlink_builder_t::route_client(serializer_registry_t&)`가 route channel registration을
  초기화하고 public client를 반환하도록 연결했다.
- route send call은 `.packet_name().submit()` 시점에 command envelope를 만들고
  `route_channel_runtime_t::submit_send_parts`로 내려간다.
- route request call은 `.packet_name().timeout().submit()` 시점에 request envelope와
  deadline을 만들고 request sequence를 등록한다.
- typed route request call은 backend seam에서 받은 reply envelope body를 serializer로
  `TReply`로 복원한다.
- `test_cpp_framework_channel_messaging`이 public route client로 send/request를 호출한 뒤
  outbound packet, target node routing id, envelope kind/name/deadline, typed reply
  deserialization을 검증한다.

### 남은 tradeoff

- 현재 typed request public call은 route runtime backend seam으로 reply parts를 받아
  `TReply`를 완성한다. native route backend adapter가 이 seam에 연결됐고, 남은 작업은
  runtime manager가 실제 router socket lifecycle과 discovery attach 단계에서 adapter를
  자동으로 붙이는 것이다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_channel_messaging
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_channel_messaging --output-on-failure
```

## 추가 리뷰. Route typed request reply completion seam

### 발견한 위험 신호

- public route request가 request sequence만 반환하면 `.NET`의 `SubmitAsync<TReply>` 의미와
  다르다.
- reply completion을 public call object가 직접 알면 route backend, envelope decode,
  serializer registry 지식이 public 표면으로 새어 나온다.
- native router socket lifecycle 연결이 없는 상태에서 typed reply API를 억지로 성공시키면
  실제 backend attach 때 다시 API를 바꿔야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| request sequence 반환만 유지 | 구현이 작다 | `.NET` route request 사용성과 맞지 않는다 |
| public call object에 reply map을 둔다 | public API에서 completion을 볼 수 있다 | pending request table과 envelope 지식이 public으로 샌다 |
| route runtime에 backend seam을 두고 typed call은 reply body만 deserialize | public 표면은 typed reply만 보고 backend는 숨긴다 | router socket lifecycle attach는 별도 단계로 남는다 |

선택은 route runtime backend seam이다. native router adapter는 이 seam 뒤에 붙이고, public
typed route request는 reply envelope body를 `TReply`로 복원하는 역할만 가진다.

### 적용한 리팩토링

- `route_channel_runtime_t`에 request backend seam과 `request_reply_parts`를 추가했다.
- `route_client_t::request<TRequest, TReply>`와 `typed_route_request_call_t<TReply>`를
  추가해 `.packet_name().timeout().submit()`이 `task_t<TReply>`를 반환하게 했다.
- backend가 없을 때는 timeout 성격의 실패로 반환해 미완성 backend를 성공처럼 숨기지 않는다.
- `test_cpp_framework_channel_messaging`이 backend seam에서 reply envelope를 만들고 public
  typed route request가 `reply_t`로 복원하는지 검증한다.

### 남은 tradeoff

- backend seam은 `native_route_backend_t`로 C++ binding `router_socket_t::send/request`에
  연결됐다. 남은 작업은 runtime manager가 실제 router socket owner를 만들고 route channel
  초기화 시 adapter를 자동 attach하는 것이다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_channel_messaging
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_channel_messaging --output-on-failure
```

## 추가 리뷰. Native route backend adapter 연결

### 발견한 위험 신호

- route runtime이 send/request envelope를 만들지만 native `router_socket_t` 호출과 연결되지
  않으면 `.NET`의 `IZLinkBackendRouterSocket` 역할이 C++에 없다.
- route runtime이 직접 binding operation builder를 알면 routing id, multipart ownership,
  async request result 처리 지식이 channel runtime으로 새어 나온다.
- reply parts를 2-part로만 보관하면 native backend가 돌려주는 multipart reply를 손실할 수
  있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `route_channel_runtime_t`에서 직접 `router_socket_t` 호출 | 구현이 빠르다 | backend 지식이 route runtime에 섞인다 |
| public route client에서 native socket 호출 | public API가 바로 동작한다 | public 표면에 binding socket lifecycle이 샌다 |
| private native route backend adapter 추가 | `.NET` backend substrate와 책임이 맞다 | runtime manager가 adapter lifecycle을 자동 연결하는 단계가 남는다 |

선택은 private native route backend adapter다. route runtime은 send/request backend seam만
알고, adapter가 C++ binding `router_socket_t::send/request` operation builder와
`message_parts_t` 변환을 담당한다.

### 적용한 리팩토링

- `src/runtime/backend/native_route_backend.*`를 추가했다.
- `native_route_backend_t`가 route send parts를 `router_socket_t::send(...).message(...).submit()`
  경로로 내려보내도록 구현했다.
- `native_route_backend_t`가 route request parts를
  `router_socket_t::request(...).message(...).timeout(...).submit_async().get()` 경로로 보내고
  reply vector를 framework `message_parts_t`로 복원하도록 구현했다.
- `route_channel_runtime_t`에 send backend seam과 `attach_native_backend(...)`를 추가했다.
- `message_parts_t`가 native multipart reply를 보존할 수 있도록 vector 생성자를 추가했다.
- layout contract가 native route backend adapter 파일을 요구하도록 확장했다.
- `test_cpp_framework_channel_messaging`이 route runtime에 native backend를 attach할 수 있고,
  fake send backend seam이 public route send에서 실제 호출되는지 검증한다.

### 남은 tradeoff

- native adapter는 구현됐지만 runtime manager가 route channel별 router socket owner를 만들고
  discovery attach까지 자동 연결하는 단계는 당시 남아 있다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_channel_messaging test_cpp_framework_layout_contract
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(channel_messaging|layout_contract)' --output-on-failure
```

## 추가 리뷰. Public route channel builder 연결

### 발견한 위험 신호

- C++ public `zlink_builder_t::route_channel(name)`은 route channel 이름만 registry runtime에
  저장했다. 사용자는 별도 `channel(name)` 호출로 endpoint를 설정해야 했고 route handler는
  public 표면에서 등록할 수 없었다.
- `.NET`의 `AddRouteMeshChannel(..., builder)`는 bind, manual connection, handler group,
  typed route handler registration을 한 builder에서 처리한다. C++가 이름만 받으면 파일
  구조는 맞아도 사용성이 같은 기능 수준에 도달하지 못한다.
- public template handler registration이 private state를 직접 만지면 public header가
  runtime 구현 세부를 알아야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 `route_channel(name)`만 유지 | 변경이 작다 | `.NET` route builder 기능과 맞지 않는다 |
| public builder template이 private registration을 직접 조작 | 구현이 빠르다 | public header가 private runtime 타입에 의존한다 |
| public typed registration value를 만들고 non-template method로 runtime state에 전달 | public API와 runtime 구현을 분리한다 | route handler registration value type이 필요하다 |

선택은 public typed registration value다. `route_channel_builder_t`는 public contract에서
handler invoker value를 만들고, 내부 `route_channel_registration_t`가 그 값을 runtime
registry로 변환한다.

### 적용한 리팩토링

- public `route_handler_context_t`, `route_handler_kind_t`,
  `route_handler_registration_t`, `route_channel_builder_t`를 추가했다.
- `zlink_builder_t::route_channel(name, configure)` overload를 추가해 bind, manual
  connection, handler group, typed routed send/request handler 등록을 한 곳에서 처리하게
  했다.
- `zlink_builder_state_t`가 route channel builder state map을 보관하고,
  `channel_runtime_manager_t`가 builder에서 route runtime을 초기화할 수 있게 했다.
- private `route_handler_registry_t`와 `route_channel_registration_t`가 public route handler
  context/registration value를 사용하도록 보정했다.
- `test_cpp_framework_channel_messaging`이 public route builder에서 route runtime connection
  두 개와 handler registration을 만든 뒤 manager가 route channel을 초기화하는 경로를
  검증하도록 확장했다.

### 남은 tradeoff

- public route client facade는 추가됐지만 request는 당시 remote `TReply` completion까지
  가지 않고 request sequence submission을 반환한다. native router socket adapter와 reply
  completion 연결이 끝나면 `.NET`의 `ZLinkRouteRequestCall<TRequest>.SubmitAsync<TReply>`
  의미로 확장한다.
- native router socket과 discovery attach는 backend substrate 단계에서 initializer 뒤에
  연결한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(channel_messaging|layout_contract)' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Route channel registration collector와 initializer 분리

### 발견한 위험 신호

- `.NET`은 `ZLinkRouteChannelBuilder`가 route handler registration을 모으고
  `ZLinkRouteChannelInitializer`가 descriptor를 만들어 runtime에 넘기지만, C++에는
  registration에서 route handler registry로 이어지는 owner가 없었다.
- C++에서 route handler를 테스트나 dispatcher에 직접 넣으면 host 구성, route runtime,
  handler registry 생성 규칙이 서로 다른 위치에 흩어진다.
- `.NET`의 reflection scanner를 그대로 흉내 내면 C++ 언어 스타일과 맞지 않고, 실제 타입
  안정성도 떨어진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| route handler registry를 직접 생성해 runtime에 전달 | 구현이 단순하다 | registration 단계와 runtime 단계가 분리되지 않는다 |
| `.NET`처럼 reflection scanner를 흉내 낸다 | 파일 이름 대응이 쉽다 | C++에 맞지 않는 런타임 타입 탐색을 만들게 된다 |
| typed handler installer를 registration에 모으고 initializer가 registry로 변환 | C++ 스타일을 유지하면서 `.NET` 책임 경계를 맞춘다 | public builder 노출은 별도 단계가 필요하다 |

선택은 typed registration collector다. C++는 compile-time 타입 정보와 멤버 함수 포인터를
사용하고, scanner가 하던 descriptor 생성 책임은 `route_channel_registration_t`의 installer와
`route_channel_initializer_t`가 나눠 맡는다.

### 적용한 리팩토링

- `route_channel_registration.*`를 추가해 route channel id, bind endpoint, manual
  connection, handler group, typed send/request handler installer를 모으게 했다.
- `route_channel_initializer_t`가 registration에서 `route_channel_runtime_t`를 만들고
  manual connection을 연결한 뒤 `route_handler_registry_t`를 생성하도록 했다.
- layout contract가 route channel registration 파일을 요구하도록 확장했다.
- `test_cpp_framework_channel_messaging`이 registration -> initializer -> route runtime ->
  handler registry -> routed request reply 흐름을 검증하도록 확장했다.

### 남은 tradeoff

- registration collector는 public `zlink_builder_t::route_channel(name, configure)` 표면과
  public `route_client_t` send/request submission facade까지 연결됐다. 남은 차이는 native
  backend 연결과 typed request reply completion이다.
- native router socket과 discovery attach는 backend substrate 단계에서 initializer 뒤에
  연결한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(channel_messaging|layout_contract)' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Route handler registry/invoker와 internal dispatcher 분리

### 발견한 위험 신호

- route receive dispatcher가 request에 항상 `route_handler_not_found`를 반환하던 상태라,
  `.NET`의 routed handler registry/invoker 기능과 맞지 않았다.
- 일반 `handler_registry_t`를 route handler에 재사용하면 source routing id, route channel
  id, routed packet context가 일반 channel handler 표면과 섞인다.
- framework 내부 routed packet을 사용자 handler보다 먼저 처리하는 dispatcher seam이 없으면
  ActorGateway/SPOT/registry internal packet이 route handler registry와 뒤섞일 위험이 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 일반 `handler_registry_t`에 route option 추가 | 코드 재사용이 쉽다 | route context와 일반 channel context가 한 public 표면에 섞인다 |
| route packet dispatcher에 lambda map만 둔다 | 구현이 작다 | descriptor, duplicate detection, typed serializer 책임이 얕게 흩어진다 |
| `.NET`처럼 route registry, invoker, internal dispatcher를 private runtime으로 분리 | route 의미를 깊은 내부 모듈에 숨긴다 | private 파일과 테스트가 늘어난다 |

선택은 private route handler runtime 분리다. public channel handler 표면은 유지하고,
route 전용 context와 internal packet dispatch는 `src/runtime/channels/*` 안에 둔다.

### 적용한 리팩토링

- `route_handler_registry.*`를 추가해 route handler descriptor, duplicate detection,
  typed send/request registration, serializer 기반 payload decode/encode를 구현했다.
- `route_handler_invoker.*`를 추가해 route send/request invocation을 route registry 뒤에
  숨겼다.
- `route_internal_packet_dispatcher.*`를 추가해 no-op internal dispatcher와 composite
  internal dispatcher를 구현했다.
- `route_packet.hpp`를 추가해 route receive/reply DTO를 dispatcher와 internal dispatcher가
  순환 include 없이 공유하게 했다.
- `route_packet_dispatcher.*`가 internal dispatcher 우선 처리, route handler send/request
  dispatch, missing handler error envelope, handler failure error envelope를 처리하도록
  확장했다.
- `test_cpp_framework_channel_messaging`이 routed request handler reply, routed send handler
  context, composite internal request reply를 검증하도록 확장했다.

### 남은 tradeoff

- route handler registry는 이제 typed registration collector와 public
  `route_channel_builder_t`를 통해 runtime registry로 변환된다.
- 실제 native router socket reply/send 연결은 backend substrate가 붙는 단계에서
  `route_receive_pump_t`와 `route_channel_runtime_t` 뒤에 연결한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(channel_messaging|layout_contract)' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Channel manager/factory와 route receive dispatch 분리

### 발견한 위험 신호

- C++에 `channel_runtime_bundle_t`와 `route_channel_runtime_t`는 생겼지만, `.NET`의
  `ZLinkChannelBundleFactory`, `ZLinkChannelRuntimeManager`처럼 capability bundle 생성과
  조회를 소유하는 내부 모듈이 없었다.
- manager가 없으면 client/publisher lazy creation, inbound 초기화, monitoring source
  parsing, route channel lookup이 각각 call object, monitoring runtime, registry runtime에
  흩어질 수 있다.
- route receive path도 `route_channel_runtime_t`에 직접 넣으면 route outbound와 inbound
  dispatch가 한 파일에 섞이고, 이후 route handler registry가 붙을 때 파일이 얕고 넓어진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `channel_runtime_t`에 bundle map과 helper를 직접 추가 | 호출 지점이 적다 | 기존 outbound pending/reliability runtime과 capability owner가 섞인다 |
| monitoring/registry/SPOT 쪽에서 필요한 bundle을 직접 만든다 | 당장 필요한 경로에 맞출 수 있다 | 같은 capability 생성 규칙이 여러 모듈로 새어 나온다 |
| `.NET`처럼 bundle factory, runtime manager, route dispatcher/pump를 private runtime으로 분리 | 책임이 깊고 파일 분류가 대응된다 | 내부 파일과 테스트가 늘어난다 |

선택은 private runtime 분리다. bundle 생성 규칙은 factory가, state map과 lookup은 manager가,
route inbound dispatch는 dispatcher/pump가 맡는다.

### 적용한 리팩토링

- `channel_runtime_state_t`에 server/client/publisher/subscriber bundle map과 route channel
  runtime map을 추가했다.
- `channel_bundle_factory.*`를 추가해 capability snapshot에서 runtime bundle을 만들고
  manual endpoint attachment를 처리하게 했다.
- `channel_runtime_manager.*`를 추가해 client/publisher lazy creation, inbound/client/
  publisher 초기화, route channel 초기화와 lookup, monitoring source parsing을 담당하게
  했다.
- `route_packet_dispatcher.*`와 `route_receive_pump.*`를 추가해 route packet queue drain과
  envelope dispatch를 분리했다. route handler registry가 붙기 전까지 request는
  `route_handler_not_found` error envelope로 응답하고 command는 drop 처리한다.
- `channel_reply_writer_t`가 `route_handler_not_found`를 stable error code로 기록하도록
  보정했다.
- layout contract와 `test_cpp_framework_channel_messaging`이 manager/factory, monitoring
  source parsing, managed route initialization, route receive error reply를 검증하도록
  확장했다.

### 남은 tradeoff

- route handler registry/invoker와 internal packet dispatcher는
  `route_handler_registry.*`, `route_handler_invoker.*`,
  `route_internal_packet_dispatcher.*`로 구현됐다. 남은 차이는 public registration builder와
  native backend 연결이다.
- channel manager는 현재 snapshot 기반 bundle을 만든다. 실제 CAPI backend socket attach는
  `src/runtime/backend/*` substrate가 붙을 때 같은 manager/factory 뒤로 연결한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(channel_messaging|layout_contract)' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Route channel runtime과 connection set 분리

### 발견한 위험 신호

- `.NET Runtime/Channels`에는 `ZLinkRouteConnectionSet`과 `ZLinkRouteChannelRuntime`이
  route channel 연결 목록, outbound envelope, request sequence correlation을 소유하지만
  C++에는 같은 owner가 없었다.
- route channel 설정은 registry/SPOT 쪽에 이미 존재했지만, route outbound parts와 SPOT
  routed request를 담는 runtime owner가 없으면 registry, SPOT, channel 모듈이 같은
  routing state를 각자 알게 된다.
- route send/request를 public builder나 sample helper에 직접 구현하면 native router
  socket, request sequence, envelope header 작성 지식이 사용자 표면으로 새어 나온다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| registry runtime에 route outbound 상태를 추가 | 기존 route channel 설정과 가깝다 | registry가 transport와 request correlation까지 알게 된다 |
| SPOT runtime에 route send/request를 직접 구현 | actor route 호출을 빨리 연결할 수 있다 | channel route와 actor route 상태가 중복된다 |
| `.NET`처럼 route connection set과 route channel runtime을 private channel runtime으로 분리 | route transport 지식이 한 모듈에 모인다 | backend adapter 연결 전에도 내부 runtime 테스트가 필요하다 |

선택은 private channel runtime 분리다. route channel은 channel runtime의 특수 capability로
보고, registry와 SPOT은 route channel id와 resolved routing id만 넘기도록 유지한다.

### 적용한 리팩토링

- `route_connection_set.*`를 추가해 route manual connection의 중복 제거, disconnect,
  정렬 snapshot을 구현했다.
- `route_channel_runtime.*`를 추가해 route channel id, running state, connection set,
  outbound command/request parts, SPOT routed parts, request sequence correlation을
  소유하게 했다.
- typed route send/request는 `client_call_codec_t`를 사용해 `.NET`과 같은 envelope
  header와 body parts를 만든다.
- layout contract가 새 route runtime 파일들을 요구하도록 확장했다.
- `test_cpp_framework_channel_messaging`이 route connection set, route send/request
  envelope, deadline, SPOT routed request, pending completion, stop drain을 검증하도록
  확장했다.

### 남은 tradeoff

- 이번 구현은 route runtime owner와 correlation semantics를 닫았고, 실제 CAPI
  `router_socket_t` send/request 호출은 당시 backend substrate 뒤에 붙여야 한다.
- 다음 반복에서는 `.NET`의 `ZLinkChannelBundleFactory`, `ZLinkChannelRuntimeManager`,
  `ZLinkRouteReceivePump`, `ZLinkRoutePacketDispatcher`와 대조해 runtime manager와 route
  receive dispatch 파일 분리를 추가로 맞춘다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(channel_messaging|layout_contract)' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Runtime/Channels bundle과 receive pump 분리

### 발견한 위험 신호

- C++ channel runtime은 `.NET Runtime/Channels`의 packet dispatcher, reply writer,
  pending request table만 분리되어 있었고 runtime bundle, receive loop, message pump
  owner가 없었다.
- manual connection set, receive gate, dealer-mesh pending request owner가 capability
  단위 내부 상태로 묶이지 않으면 이후 route runtime이나 native adapter가 붙을 때 같은
  상태 지식이 여러 모듈에 흩어질 위험이 있었다.
- receive pump를 public call object나 builder 쪽으로 노출하면 사용자가 수신 순서와
  재진입 제어를 알아야 하므로 얕은 public API가 된다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `channel_runtime_state_t`에 필드만 추가 | 변경 파일이 적다 | capability 단위 상태와 receive gate 책임이 큰 상태 객체에 섞인다 |
| receive pump를 public helper로 제공 | 테스트에서 직접 호출하기 쉽다 | raw 수신 루프가 public 계약처럼 보인다 |
| `.NET`처럼 bundle, message pump, receive loop를 private runtime으로 분리 | public API를 유지하면서 내부 책임이 깊어진다 | 내부 파일과 테스트가 늘어난다 |

선택은 private runtime 분리다. public channel API는 그대로 두고, 수신 루프와 manual
connection set은 `src/runtime/channels/*` 내부 모듈이 맡는다.

### 적용한 리팩토링

- `channel_runtime_bundle.*`를 추가해 manual connection set, receive gate,
  dealer-mesh pending request owner를 capability runtime 상태로 묶었다.
- `channel_message_pump.*`를 추가해 server ingress envelope dispatch를 packet dispatcher
  뒤에 숨겼다.
- `channel_receive_loop.*`를 추가해 queued server message drain, reply 수집, receive
  gate 재진입 거부를 구현했다.
- layout contract가 새 `Runtime/Channels` owner 파일들을 요구하도록 확장했다.
- `test_cpp_framework_channel_messaging`이 manual connection snapshot, receive loop drain,
  reply envelope 생성, re-entrant gate rejection을 검증하도록 확장했다.

### 남은 tradeoff

- 현재 receive loop는 테스트 가능한 queued message drain 형태다. 다음 반복에서는
  `.NET`의 `ZLinkChannelRuntimeManager`, `ZLinkChannelBundleFactory`,
  `ZLinkRouteChannelRuntime`, `ZLinkRouteReceivePump`와 대조해 channel manager와 route
  channel owner를 같은 방식으로 보강해야 한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(channel_messaging|layout_contract)' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Channel Pending Request와 Reply Dispatcher 분리

### 발견한 위험 신호

- C++ channel runtime은 `.NET Runtime/Channels`의 pending request table, packet dispatcher,
  reply writer 책임을 `channel_runtime.*` 안에 함께 담고 있었다.
- reply correlation map이 runtime state에 직접 노출되어 있어 request sequence 발급,
  reply match, drain 정책이 channel runtime의 여러 메서드에 흩어질 수 있었다.
- request handler reply와 error reply를 envelope로 감싸는 책임이 분리되어 있지 않아,
  이후 receive pump나 route dispatcher가 붙으면 같은 header/error 작성 지식이 반복될 수
  있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `channel_runtime.*`에 계속 추가 | 변경 범위가 작다 | runtime 파일이 얕고 커져 `.NET` 책임 분리와 멀어진다 |
| public channel API에 pending request object 노출 | 테스트가 쉽다 | 사용자가 correlation table을 직접 알게 된다 |
| private channel runtime owner로 pending requests, reply writer, packet dispatcher 분리 | `.NET` 구조와 맞고 내부 지식을 숨긴다 | private 파일과 layout/test가 추가된다 |

선택은 세 번째 방식이다. public call object는 pending table과 reply envelope를 모르고,
`src/runtime/channels/*`가 request sequence, reply match, server ingress envelope dispatch를
담당한다.

### 적용한 리팩토링

- `channel_pending_requests.*`를 추가해 request sequence 발급, pending 등록, reply remove,
  drain을 담당하게 했다.
- `channel_runtime_state_t`에서 raw `pending_request_channels` map을 제거하고
  `channel_pending_requests_t`를 사용하게 했다.
- `channel_reply_writer.*`를 추가해 response/error header 생성과 raw envelope reply 생성을
  담당하게 했다.
- `channel_packet_dispatcher.*`를 추가해 request/command envelope dispatch를 처리하게 했다.
- error reply code를 enum 숫자가 아니라 stable string으로 기록하도록 보정했다.
- layout contract가 새 channel runtime 파일을 요구하도록 갱신했다.
- `test_cpp_framework_channel_messaging`이 envelope request dispatch, response envelope,
  handler-not-found error envelope를 검증하도록 확장했다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Runtime/Messaging Envelope와 Failure Mapping 보강

### 발견한 위험 신호

- C++ `Runtime/Messaging`에는 submit queue와 pending operation은 있었지만, `.NET`의
  `ZLinkEnvelopeCodec`, `ZLinkClientCallCodec`, `ZLinkRequestFailureMapper`에 해당하는
  책임이 없었다.
- header/body envelope, correlation id, deadline, error reply 해석을 channel이나 sample이
  직접 알게 되면 messaging wire 지식이 여러 곳으로 새게 된다.
- request 결과와 error envelope code를 framework error kind로 사상하는 정책이 한곳에
  고정되어 있지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| channel runtime에 직접 encode/decode 구현 | 파일 수가 적다 | channel dispatch와 wire envelope 정책이 섞인다 |
| public API로 envelope 타입을 노출 | 테스트와 샘플 작성이 쉽다 | 사용자가 내부 wire format을 직접 다루게 된다 |
| private `src/runtime/messaging` codec/mapper로 분리 | `.NET Runtime/Messaging` 구조와 맞고 wire 지식을 숨긴다 | private test hook과 파일이 추가된다 |

선택은 private runtime 분리다. public channel call object는 envelope JSON을 몰라도 되고,
runtime messaging이 header/body/error mapping을 담당한다.

### 적용한 리팩토링

- `envelope_codec.*`를 추가해 message kind, envelope header, header/body 2-part encode,
  header/body decode를 구현했다.
- `client_call_codec.*`를 추가해 request envelope 생성, correlation id, deadline, typed
  body encode, reply decode, error reply 해석을 구현했다.
- `request_failure_mapper.*`를 추가해 request result와 error code를
  `framework_error_kind_t`와 retriable 여부로 사상하게 했다.
- layout contract가 새 `src/runtime/messaging/*` 파일들을 요구하도록 갱신했다.
- `test_cpp_framework_messaging`이 envelope roundtrip, typed reply decode, error reply
  mapping, busy retry mapping을 검증하도록 확장했다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. 타입 기반 Packet Name Resolver 보강

### 발견한 위험 신호

- handler 등록과 connector client 호출에서 같은 packet name 문자열을 DTO, handler,
  client에 반복해서 적고 있었다.
- framework handler 기본 packet name이 `typeid(T).name()`에 기대고 있어 compiler와 ABI에
  따라 값이 달라질 수 있었다.
- connector의 `packet_name_resolver.cpp`는 layout contract에는 있었지만 실제 packet 생성
  경로에서 사용되지 않았다.
- connector public template이 codec lookup을 위해 private `connector_state_t` 내부 map을
  직접 보려고 했다. 이는 public header에 runtime state 지식을 새는 얕은 모듈이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 문자열 override 유지 | 변경 범위가 작다 | packet name 지식이 샘플과 handler에 반복된다 |
| C++ type name만 사용 | 사용자가 이름을 적지 않아도 된다 | 안정적인 wire contract가 되지 않는다 |
| DTO의 `static constexpr packet_name`을 우선 사용하고 fallback만 type name으로 둔다 | `.NET`의 타입 기반 이름 해석과 역할이 같고 C++ 스타일에도 맞다 | DTO에 packet name 상수를 추가해야 한다 |

선택은 세 번째 방식이다. 샘플과 정식 DTO는 packet name을 명시하고, framework와 connector는
그 값을 기본 이름으로 사용한다. public template은 이름 계산과 forwarding만 하고, codec
lookup과 packet 생성은 connector runtime `.cpp`로 내린다.

### 적용한 리팩토링

- `contracts/detail/message_name.hpp`를 추가해 framework handler 기본 packet name이 DTO
  `packet_name`을 우선 사용하게 했다.
- Stream Connector `stream_payload.hpp`에 같은 방식의 packet name helper를 추가했다.
- `connector_t::send`, `request`, `on`이 명시 packet name 없이 DTO 이름을 사용하게 했다.
- connector runtime의 `make_packet(type, name)`이 `packet_name_resolver_t`를 실제로
  호출하도록 연결했다.
- Bingo와 TicTacToe 샘플 DTO에 `.NET` record 이름과 같은 packet name 상수를 추가하고,
  client/handler의 반복 문자열을 제거했다.
- contract/unit/sample smoke 테스트가 DTO packet name 기반 등록과 connector frame 이름을
  검증하도록 갱신했다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Runtime/Messaging Submit 상태 보강

### 발견한 위험 신호

- `pending_operation_t`가 실제 완료 상태 없이 항상 유효한 것처럼 보이는 얕은 모듈이었다.
- callback 기반 `submit(callback)`은 콜백을 즉시 호출한 뒤 빈 pending operation을
  반환했다. 이 상태에서는 사용자가 완료, 취소, 만료 상태를 확인할 수 없다.
- `.NET`의 `PendingSubmit`, `ZLinkSubmitQueue`에 해당하는 runtime owner가 C++ 파일
  구조에 없어서 `Runtime/Messaging` 대응이 문서와 구현 사이에서 갈라져 있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `pending_operation_t`에 bool 필드만 추가 | 변경 범위가 작다 | 완료/취소/실패 전이가 public layout에 고정되고 runtime queue와 연결되지 않는다 |
| public header가 runtime submit class를 직접 friend로 안다 | 구현은 쉽다 | public 계약이 private runtime class 이름을 알게 되어 interface 분리가 약해진다 |
| public type은 type-erased state만 들고, 전이는 `contracts/detail` helper와 private runtime에서 처리 | public 표면이 작고 runtime state를 숨길 수 있다 | helper 함수와 private state 파일이 추가된다 |

선택은 세 번째 방식이다. 호출자는 `valid`, `completed`, `cancelled`, `cancel`만 알면 되고,
deadline, queue slot, failure exception은 `src/runtime/messaging`이 관리한다.

### 적용한 리팩토링

- `pending_operation_t`를 type-erased state 기반으로 바꾸고 `completed`, `cancelled`,
  `cancel`을 추가했다.
- callback `submit(callback)`은 완료된 pending operation을 반환하도록 바꿨다.
- `src/runtime/messaging/pending_operation_state.hpp`,
  `pending_operation.cpp`, `pending_submit.*`, `submit_queue.*`를 추가했다.
- `pending_submit_t`는 command accepted completion, request explicit completion,
  deadline failure, wake callback을 처리한다.
- `submit_queue_t`는 bounded FIFO, expected dequeue, dispose-all을 처리한다.
- layout contract가 `src/runtime/messaging/*` 파일을 요구하도록 보강했다.
- `test_cpp_framework_messaging`을 추가해 callback submit, command/request submit,
  capacity, FIFO, deadline, dispose 동작을 검증했다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Execution Queue와 Runtime Event Publisher 보강

### 발견한 위험 신호

- `.NET` `Runtime/Execution`에는 serial execution queue와 runtime task runner가 있지만,
  C++는 offload executor만 있고 ordered drain을 소유하는 runtime owner가 없었다. handler
  offload는 있어도 session/spot 내부 작업 순서를 보장하는 queue가 따로 검증되지 않았다.
- `.NET` `Contracts/Eventing`에는 runtime event publisher가 있지만, C++는 monitoring
  builder의 handler 등록과 detail monitoring runtime의 publish method만 있었다. 사용자가
  framework 표면에서 typed runtime event를 직접 publish하는 계약이 빠져 있었다.
- layout contract가 `src/runtime/execution/*`를 요구하지 않아 구조가 다시 빠져도 테스트가
  잡지 못했다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| offload executor만 계속 사용 | 파일 수가 적다 | serial ordering, capacity, close/drain 의미를 호출자가 조합해야 한다 |
| public execution queue 노출 | 테스트와 사용이 쉽다 | runtime work item 저장 구조가 public 계약으로 새기 쉽다 |
| private runtime serial queue 구현 | `.NET Runtime/Execution` 축과 맞고 구현 세부를 숨긴다 | private runtime test hook이 필요하다 |
| monitoring detail runtime만 유지 | 기존 테스트가 작다 | `.NET` event publisher 대응 public 표면이 없다 |
| monitoring builder가 publisher를 반환 | handler map은 숨기고 typed publish 계약만 제공한다 | event publisher가 monitoring state를 공유하는 owner가 추가된다 |

선택은 private runtime serial queue와 public runtime event publisher다. serial queue는
`src/runtime/execution/*`에 두어 work item storage, capacity, drain state를 숨긴다.
publisher는 `contracts/eventing/events.hpp`에 두되, state와 handler map은 diagnostics
runtime 안에 둔다.

### 적용한 리팩토링

- `runtime::serial_execution_queue_t`를 추가했다. queue는 offload executor를 사용하지만
  한 번에 하나의 drain loop만 실행하고, capacity, close, drain, handler exception reporting을
  내부에서 처리한다.
- `runtime_event_publisher_t`를 public eventing contract에 추가했다.
  `monitoring_builder_t::publisher()`가 같은 monitoring state를 공유하는 publisher를 반환한다.
- detail `monitoring_runtime_t`의 typed publish도 같은 publisher 경로를 사용하게 정리했다.
- layout contract가 `framework/src/runtime/execution/serial_execution_queue.*`를 요구한다.
- `test_cpp_framework_execution`이 ordering, exception reporting, close, capacity validation을
  검증한다.
- `test_cpp_framework_monitoring`이 public publisher로 올린 actor event가 typed handler와
  trace hook까지 전달되는지 검증한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. `.NET` 구조 parity 반복 보정

### 발견한 위험 신호

- C++ connector public 계약이 `connector.hpp` 한 파일에 모여 있어 `.NET`의
  `Contracts/Calls`, options, metadata, models, factory 분리와 맞지 않았다.
- C++ connector runtime이 `connector_runtime.cpp` 중심이라 `.NET`의 `Runtime/Calls`,
  `Runtime/Protocol`, `Runtime/Transport`, lifecycle, callbacks, receive loop 분리와
  비교하기 어려웠다.
- Bingo/TicTacToe 샘플은 폴더 이름은 나뉘었지만 `.NET` 샘플의 `*HostFactory`,
  Actors, room/game model, publisher, spot, handler 하위 파일 분류가 부족했다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 파일 유지 후 문서에만 대응표 작성 | 변경이 작다 | 사용자가 실제 파일을 보고 `.NET`과 비교할 수 없다 |
| wrapper 파일만 추가 | 빠르다 | 책임 owner가 여전히 모호하다 |
| public/runtime/sample 파일을 역할 단위로 분리하고 layout test로 강제 | 비교와 리뷰가 쉽다 | 파일 수와 CMake 관리가 늘어난다 |

선택은 역할 단위 분리와 layout test 강제다. C++ 이름은 snake_case를 사용하지만, 책임 경계는
`.NET`의 `Contracts/*`, `Runtime/*`, sample role file과 같은 뜻으로 맞춘다.

### 적용한 리팩토링

- connector public 계약을 `contracts/calls/zlink_stream_calls.hpp`,
  `zlink_stream_connector_options.hpp`, `zlink_stream_models.hpp`,
  `zlink_stream_connector.hpp`, `zlink_stream_connector_factory.hpp`,
  `codec_registry.hpp`, `result.hpp`, `task.hpp`로 분리했다.
- connector runtime을 `src/runtime/calls`, `src/runtime/protocol`,
  `src/runtime/protocol/compression`, `src/runtime/protocol/framing`,
  `src/runtime/transport`, `connector_lifecycle`, `connector_runtime`,
  `heartbeat_monitor`로 분리했다. 이후 반복 POSD 리뷰에서
  사용되지 않는 얕은 wrapper 파일은 제거했다.
- Bingo/TicTacToe 샘플에 role별 `*host_factory.hpp`, actor, room/game model,
  publisher, spot, handler 하위 파일을 추가했다.
- sample parity test가 새 role header를 include하고 주요 타입을 실제로 사용하도록
  확장했다.
- layout contract가 connector runtime의 실제 owner와 sample role 파일을 필수 경로로
  검증하도록 확장했다.
- draft 문서의 connector owner 표와 sample 구조 설명을 실제 파일 배치와 맞췄다.

### 남은 tradeoff

- 일부 새 파일은 기존 구현을 감싸는 얇은 role header다. 이는 `.NET`과 같은 리뷰 단위를
  먼저 만들기 위한 단계이며, 다음 구현 goal에서 내부 로직을 해당 owner 파일로 더 옮긴다.
- connector transport 경로는 당시 `.NET` frame protocol로 전환되지 않았다. 다만
  connector private runtime에 `.NET` byte layout과 같은 header/frame codec owner를 두고,
  다음 반복에서 전송 경로를 그 codec으로 교체한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
git diff --check -- framework/languages/cpp
rg -n '<금지 표현 패턴>' framework/languages/cpp/doc/draft framework/languages/cpp/samples framework/languages/cpp/connector framework/languages/cpp/framework || true
```

## 추가 리뷰. 샘플 파일 분리 보정

### 발견한 위험 신호

- C++ 샘플은 역할별 executable로는 나뉘었지만 `Shared/sample.hpp` 하나가 configuration,
  contracts, domain model, handler, inbox, host wiring을 모두 담고 있었다.
- `.NET` 샘플은 `Shared/Configuration`, `Shared/Contracts`, `Server/*/Handlers`,
  `Server/Play/*Spots`, `Client/*Inbox`처럼 기능 단위 파일을 나누는데 C++ 샘플은 이
  수준을 따라가지 못했다.
- 파일 분리 자체를 검증하는 contract test가 없어서 다시 큰 umbrella 파일로 퇴행할 수
  있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| umbrella header 유지 | include가 단순하다 | `.NET` 샘플과 같은 수준의 리뷰가 어렵다 |
| main만 include를 나눔 | 변경이 작다 | 실제 구현은 여전히 한 파일에 모인다 |
| `.NET` 역할과 같은 파일 계층으로 분리 | 비교와 리뷰가 쉽다 | include 경로와 layout contract를 함께 관리해야 한다 |

선택은 `.NET` 역할과 같은 파일 계층으로 분리하는 것이다. `Shared/sample.hpp`는 기존 include
호환을 위한 umbrella로만 남기고, 실제 정의는 역할별 헤더로 이동했다.

### 적용한 리팩토링

- Bingo를 `Shared/Configuration`, `Shared/Contracts`, `Client/bingo_notification_inbox`,
  `Server/Api/Handlers`, `Server/Play/Handlers`, `Server/Play/BingoRoomSpots`,
  `Server/Play/EntrySpot` 파일로 분리했다.
- TicTacToe를 `Shared/Actors`, `Shared/Configuration`, `Shared/Contracts`,
  `Client/session_actor_notification_inbox`, `Server/Api/Handlers`,
  `Server/Play/EntrySpot`, `Server/Play/GameSpots`, `Server/Play/Handlers` 파일로 분리했다.
- `test_cpp_framework_layout_contract`가 샘플 파일 분리 경로를 필수로 확인하도록 확장했다.
- 샘플 README와 draft 샘플 배치 문서를 실제 파일 구조와 맞게 수정했다.

### 남은 tradeoff

- `Shared/sample.hpp`는 기존 sample main과 contract test include를 유지하기 위한 umbrella다.
  실제 계약과 handler 구현은 더 이상 이 파일에 두지 않는다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Connector frame/header codec parity 보정

### 발견한 위험 신호

- `.NET` connector는 frame prefix를 `u16 header length + u32 payload length` big-endian으로
  쓰고, header에 kind, codec, flags, request sequence, name, metadata를 담는다.
- C++ connector runtime에는 `protocol/framing/frame_codec`과 `protocol/header_codec` 파일은
  있었지만 실제 구현은 frame size 검증과 packet name 문자열 반환 수준이었다.
- 이 상태에서는 connector 폴더 구조가 `.NET`과 비슷해 보여도 wire protocol parity를
  증명할 수 없었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| transport 경로를 한 번에 `.NET` frame protocol로 교체 | 최종 상태에 가장 가깝다 | 현재 샘플 smoke 서버와 connector test를 동시에 크게 바꿔야 한다 |
| private codec만 먼저 구현하고 테스트로 고정 | 회귀 위험이 작고 다음 transport 전환의 기반이 된다 | 이 단계만으로는 실제 socket 전송까지 증명하지 못한다 |
| public header codec API를 추가 | 테스트와 재사용이 쉽다 | 사용자가 wire 세부를 직접 다루는 API처럼 보인다 |

선택은 private codec 구현을 먼저 완료하는 것이다. frame/header/metadata byte layout은
runtime 내부에 숨기고, public connector API는 send/request/dispatch 표면만 유지한다.

### 적용한 리팩토링

- `metadata_codec_t`에 `.NET`과 같은 metadata payload encode/decode를 구현했다.
- `header_codec_t`에 kind, codec, flags, request sequence, packet name, metadata
  encode/decode와 control/request/send/error semantic validation을 구현했다.
- `frame_codec_t`에 `.NET`과 같은 6-byte frame prefix와 frame encode를 구현했다.
- `test_cpp_stream_connector`가 header roundtrip, unknown flag rejection, control packet
  contract, frame prefix layout을 검증하도록 확장했다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -R test_cpp_stream_connector --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Unreal Stream Connector frame protocol 전환

### 발견한 위험 신호

- 일반 C++ connector는 STREAM frame prefix와 header codec으로 전환됐지만 Unreal connector는
  별도 텍스트 명령 문자열을 직접 읽고 썼다.
- `.NET`과 같은 wire protocol을 사용하지 않으면 Unreal client만 별도 서버 adapter가
  필요해지고, connector가 언어별로 같은 방식이라는 기준을 만족하지 못한다.
- Unreal connector가 일반 C++ connector runtime을 그대로 감싸면 Asio transport와 Unreal
  `FSocket`/Game Thread lifecycle이 섞인다. 이는 Unreal 전용 connector를 별도 배포한다는
  정책과 충돌한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 라인 프로토콜 유지 | 구현이 작다 | 서버와 wire protocol parity가 깨진다 |
| 일반 C++ connector runtime 재사용 | protocol 중복이 줄어든다 | Unreal network module과 lifecycle 기준을 잃는다 |
| Unreal `Private/`에서 같은 frame byte layout을 구현 | Unreal transport를 유지하면서 wire 의미를 맞춘다 | protocol byte layout을 계속 contract test로 고정해야 한다 |

선택은 Unreal `Private/` runtime이 Unreal `Sockets`를 계속 사용하되, 송수신 바이트는
일반 C++ connector와 같은 STREAM frame 구조로 만드는 방식이다. public header에는 frame
세부를 노출하지 않는다.

### 적용한 리팩토링

- `SendJson`은 `kind=send`, `codec=json`, request sequence 없음, packet name, payload로
  STREAM frame을 만들어 `FSocket::Send`에 전달한다.
- `RequestJson`은 `kind=request`, `codec=json`, `has_request_seq` flag와 sequence를 가진
  STREAM frame을 만든다.
- `Dispatch`는 socket에서 받은 bytes를 6-byte prefix, header, payload 순서로 읽고
  `kind=send` packet을 Unreal packet callback으로 변환한다.
- Unreal Automation Test `ZLink.StreamConnector.Loopback`은 더 이상 라인 문자열을 보지
  않고, frame kind, codec, flags, request sequence, packet name, payload를 검증한다.

### 남은 tradeoff

- Unreal Engine 없는 CTest는 public shape와 source compile만 확인한다. 실제 `FSocket`
  loopback 검증은 Unreal Automation Test에서 실행해야 한다.
- Game Thread enqueue 정책은 다음 반복에서 일반 connector의 dispatch 규칙과 다시 대조한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_unreal_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_unreal_stream_connector --output-on-failure
```

## 추가 리뷰. Unreal Stream Connector metadata parity 보정

### 발견한 위험 신호

- Unreal public packet에는 `Metadata` 필드가 있었지만 `SendJson`과 `RequestJson`에서
  metadata를 전달할 방법이 없었다.
- 일반 C++ connector는 metadata를 STREAM header에 encode/decode하지만 Unreal connector는
  수신 frame의 metadata를 callback packet에 채우지 않았다.
- compression 같은 당시 구현되지 않은 option을 public options에 섞으면 호출자가 실제로
  동작하지 않는 기능을 믿게 된다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 API 유지 | 변경이 작다 | Unreal connector만 metadata 기능이 빠진다 |
| `SendJson` 인자를 계속 늘림 | Blueprint에서 바로 보인다 | timeout, metadata, 이후 option이 함수 시그니처를 계속 키운다 |
| Unreal 전용 options 구조 추가 | public API를 깊게 유지하고 metadata를 실제 구현과 연결한다 | 기존 API와 options overload를 같이 관리해야 한다 |

선택은 Unreal 전용 `FZLinkStreamSendOptions` 구조다. 기존 `SendJson`/`RequestJson`은 빈
options로 동작하고, metadata가 필요한 호출만 `WithOptions` overload를 쓴다.

### 적용한 리팩토링

- `FZLinkStreamSendOptions`를 public Unreal contract에 추가하고 `Metadata`만 노출했다.
  당시 구현하지 않은 compression option은 넣지 않았다.
- `SendJsonWithOptions`와 `RequestJsonWithOptions`를 Blueprint-callable API로 추가했다.
- Unreal private frame encoder가 metadata를 일반 C++ connector와 같은 metadata payload
  구조로 header에 싣고 `has_metadata` flag를 설정하게 했다.
- Unreal private frame decoder가 metadata payload를 `FZLinkStreamPacket.Metadata`에 채운다.
- Engine 없는 CTest smoke가 새 options API를 컴파일하고, Unreal Automation Test가
  metadata send/request frame과 metadata push callback을 검증하도록 확장됐다.

### 남은 tradeoff

- Engine 없는 CTest는 metadata wire bytes를 실행 검증하지 못한다. 실제 socket metadata
  검증은 Unreal Automation Test에서 담당한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_unreal_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_unreal_stream_connector --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. 22개 Goal 계획과 HTTP 검증 label 보정

### 발견한 위험 신호

- 최신 실행 계획은 Goal 18 `ZLink HTTP Client`, Goal 19 `HTTP Hosting`, Goal 22
  `Final Regression`까지 총 22개 goal을 기준으로 한다. 이 로그의 앞선 Goal 20/21 항목은
  HTTP client goal을 추가하기 전의 이력이라, 현재 실행 순서로 읽으면 최종 gate 범위를
  잘못 이해할 수 있다.
- 실행 계획은 `http-client-contract`, `http-client-unit`, `http-client-e2e`,
  `http-client-https`, `http-client-regression`, `framework-http-e2e` label을 요구하지만
  CMake test 등록에는 해당 label이 없었다. 이 상태에서는 계획 문서의 CTest 명령이 0개
  테스트로 통과하거나 실행 가드 역할을 하지 못한다.
- HTTP hosting draft의 결정 전 항목처럼 보이는 표현은 이미 선택이 끝난 구현 기준을 결정 전
  항목처럼 보이게 만든다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| label을 Goal 18 구현 때까지 비워 둠 | 현재 코드 변경이 없다 | 계획 문서의 회귀 명령이 empty test selection를 잡을 수 있다 |
| 지금 별도 dummy HTTP client test target을 만듦 | label별 test target이 생긴다 | 실제 client 구현 없이 얕은 테스트 target만 늘어난다 |
| 현재 contract/host test에 초기 실행 label을 연결 | 계획 명령이 즉시 실행 가드를 갖는다 | Goal 18 구현 때 실제 client test로 확장해야 한다 |

선택은 현재 contract/host test에 label을 연결하는 방식이다. 당시 `zlink::http_client` 구현이
없으므로 contract header test는 HTTP client 계획 label의 최소 실행 가드로만 사용한다. Goal 18
구현이 들어오면 실제 client unit/e2e/HTTPS/regression test target을 추가하고 이 초기 label
분배를 더 세분화해야 한다.

### 적용한 리팩토링

- `test_cpp_framework_contract_headers`에 `http-client-*` label을 붙여 Goal 18 계획의
  CTest 명령이 현재 빌드에서도 테스트를 찾도록 했다.
- `test_cpp_framework_app_host`에 `framework-http-e2e` label을 붙여 HTTP hosting e2e
  gate가 기존 HTTP route/TLS validation smoke를 실행하도록 했다.
- `cpp-http-hosting.ko.md`의 결정 표를 “결정된 구현 기준”으로 바꿔 결정 전 항목처럼
  읽히지 않게 했다.
- 이 로그의 앞선 Goal 20/21 항목은 과거 실행 기록으로 유지하고, 현재 기준은
  `cpp-framework-implementation-plan.ko.md`의 22개 goal이라고 명시했다.

### 재실행할 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L http-client-contract --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-unit --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-https --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-regression --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-http-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
git diff --check -- framework/languages/cpp
```

## Goal 18. ZLink HTTP Client 실제 산출물 추가

### 발견한 위험 신호

- plan과 HTTP client draft는 `framework/languages/cpp/http-client`,
  `zlink/http_client.hpp`, `zlink::http_client` target을 요구했지만 실제 산출물이 없었다.
  label만 contract test에 붙이면 CTest는 실행되지만 샘플과 HTTP handler e2e가 사용할
  public client가 없다.
- 샘플마다 Boost.Beast wrapper를 만들면 URL parsing, TLS trust, timeout, JSON decode,
  HTTP status mapping 지식이 여러 곳으로 새어 나간다.
- HTTPS 지원을 public API에서 OpenSSL context나 SSL stream으로 노출하면 사용자가 낮은
  수준 TLS 설정을 알아야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 샘플별 Beast helper를 둠 | 구현을 빠르게 시작할 수 있다 | HTTP/TLS/JSON 정책이 샘플마다 흩어진다 |
| framework HTTP hosting target에 client를 붙임 | HTTP 기능이 한 target에 모인다 | server core가 client-side dependency까지 떠안는다 |
| 별도 `zlink::http_client` target을 둠 | client 정책을 한 모듈에 숨기고 framework core dependency를 늘리지 않는다 | package/export/test target을 추가해야 한다 |

선택은 별도 `zlink::http_client` target이다. public header는 `client_t`, builder, request
call object만 보여 주고, Boost.Beast와 OpenSSL 타입은 `http-client/src/runtime/*` 안에
둔다.

### 적용한 리팩토링

- `http-client/include/zlink/http_client.hpp`와
  `http-client/include/zlink/http_client/contracts/client.hpp`를 추가했다.
- `zlink_http_client` static target과 `zlink::http_client` alias를 추가하고 install/export에
  포함했다.
- `client_t::create().base_url(...).json().timeout(...).trust_certificate_file(...).build()`
  fluent builder를 추가했다.
- `get`, `post`, `put`, `delete_` request builder와 typed JSON `body(...)`,
  `submit<T>()`, callback `submit<T>(...)`, `submit_raw()`를 추가했다.
- private runtime은 Boost.Beast로 HTTP를 처리하고, OpenSSL이 있으면 HTTPS handshake,
  certificate verification, hostname verification, explicit test certificate trust를 처리한다.
- `test_cpp_http_client`가 HTTP JSON request/response, callback submit, GET/POST/PUT/DELETE,
  404 status mapping, decode error, timeout, HTTPS success, untrusted certificate failure를
  검증한다.
- install consumer test가 `zlink::http_client` target과 `<zlink/http_client.hpp>`를 함께
  소비하도록 확장했다.

### 수정 후 점검

- public HTTP client header에는 Boost.Beast, Boost.Asio, OpenSSL, socket, resolver, SSL
  stream 타입이 노출되지 않는다.
- HTTPS trust는 `trust_certificate_file(...)`로만 전달한다. verification을 묵시적으로 끄는
  public option은 추가하지 않았다.
- `http-client-*` label은 실제 `test_cpp_http_client`와 contract header test를 실행한다.

### 재실행할 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_http_client test_cpp_framework_contract_headers
ctest --test-dir framework/languages/cpp/build -L http-client-contract --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-unit --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-https --output-on-failure
ctest --test-dir framework/languages/cpp/build -L http-client-regression --output-on-failure
```

## Goal 19. HTTP Hosting runtime과 HTTP client e2e 연결

### 발견한 위험 신호

- `options.http().listen(...).map_*<THandler>(...)` public API와 route metadata는 있었지만
  app lifecycle에서 실제 HTTP listener를 시작하지 않았다. 이 상태에서는 HTTP hosting 문서의
  handler e2e 요구를 충족할 수 없다.
- HTTP handler e2e가 framework 내부 metadata만 확인하면 실제 사용자가 호출하는 client path를
  검증하지 못한다.
- 첫 구현에서 route invoker 함수를 `http_route_t` public field로 노출하면 handler invocation
  결정이 public metadata에 새어 나간다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| test-local HTTP server로 hosting을 흉내 냄 | 테스트를 빠르게 만들 수 있다 | framework hosting runtime이 여전히 없다 |
| app host 밖에 별도 `start_http(...)` API를 둠 | lifecycle 제어가 명시적이다 | 사용자가 app lifecycle과 HTTP lifecycle을 따로 알아야 한다 |
| `options.http()` snapshot을 app hosted service로 자동 등록 | 기존 fluent API를 유지하고 app lifecycle에 묶인다 | internal listener/runtime owner가 필요하다 |

선택은 `hosted_service_t` 기반 HTTP host service다. 사용자는 기존처럼
`add_zlink_framework(... options.http() ...)`만 호출하고, runtime은 app start/stop에 맞춰
listener를 관리한다.

### 적용한 리팩토링

- `framework/src/runtime/http/http_host_service.*`를 추가해 HTTP endpoint별 Beast listener를
  app lifecycle에 묶었다.
- `add_zlink_framework`가 HTTP endpoint snapshot을 가진 경우 `http_host_service_t`를
  hosted service로 자동 등록하게 했다.
- route matching은 exact path와 `{id}` 같은 단일 segment parameter pattern을 지원한다.
- handler request body는 typed JSON으로 decode하고, handler reply는 JSON response로 encode한다.
- `test_cpp_framework_app_host`가 `zlink::http_client`로 `POST /games`, `GET /games/{id}`,
  `PUT /games/{id}`, `DELETE /games/{id}`를 실제 HTTP listener에 호출한다.
- route invoker는 `http_route_t` private member와 runtime friend accessor 뒤에 숨겼다.
  public metadata에는 method, path, handler name만 남긴다.

### 수정 후 점검

- HTTP hosting public header에는 Boost.Beast, Boost.Asio, OpenSSL socket/stream 타입이
  노출되지 않는다.
- `framework-http`와 `framework-http-e2e` label은 실제 app host HTTP listener와
  `zlink::http_client` 호출을 실행한다.
- app 재실행 smoke는 HTTP listener가 없는 별도 app으로 분리해 HTTP e2e lifecycle과 섞지
  않았다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_app_host
ctest --test-dir framework/languages/cpp/build -L framework-http --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-http-e2e --output-on-failure
```

## 추가 리뷰. 이전 계획 검증 label과 HTTP/sample e2e 보정

### 발견한 위험 신호

- 당시 implementation plan의 검증 명령은 `framework-zlink-*`, `framework-http`,
  `framework-config`, `framework-host`, `framework-observability`,
  `framework-sample-e2e` label을 기준으로 한다. 하지만 CTest 등록은 일반
  `framework-integration`, `framework-regression`, `channel`, `spot` 같은 label에만
  걸려 있어서 문서 기준 검증 명령이 empty test selection를 반환할 수 있었다.
- HTTP hosting draft에는 `options.http().listen(...).map_post<T>()`와 HTTPS TLS 검증
  표면이 있는데 public contract owner가 없었다. 이 상태에서는 TicTacToe HTTP 시작 흐름을
  C++ framework 표면으로 검증할 수 없다.
- sample e2e 로그 검증은 request, reply, push만 확인했다. ActorGateway relay, monitoring
  event, disconnect, shutdown을 보지 않으면 client 성공만으로 e2e를 완료 처리할 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 label을 유지하고 문서 검증 명령을 바꾼다 | 코드 변경이 적다 | goal별 검증 명령과 CTest taxonomy가 계속 어긋난다 |
| 기존 테스트에 goal label을 추가한다 | 기능별 테스트를 재사용하면서 plan 검증 명령이 실제 테스트를 실행한다 | label 추가가 CMake에 반영된다 |
| HTTP를 extension boundary에만 둔다 | core dependency를 줄일 수 있다 | Goal 18의 core HTTP hosting 표면과 TicTacToe `POST /games` 기준을 충족하지 못한다 |
| HTTP contract owner를 core framework에 만들고 runtime 구현은 private owner에 붙인다 | public 표면과 startup validation을 먼저 닫고 Boost.Beast 타입 노출을 막을 수 있다 | 실제 socket accept loop는 runtime owner의 후속 구현 책임으로 남는다 |

선택은 기존 테스트에 goal label을 추가하고, HTTP public contract owner를
`contracts/http/http.hpp`로 두는 방식이다. 이렇게 하면 검증 명령은 문서와 일치하고,
HTTP 세부 구현은 public header가 아니라 runtime owner에 붙일 수 있다.

### 적용한 리팩토링

- `framework-http`, `framework-host`, `framework-config`, `framework-observability`,
  `framework-zlink-*`, `framework-sample-e2e`, `connector-e2e` label을 실제 CTest에
  연결했다.
- `zlink/framework/contracts/http/http.hpp`와 `zlink/framework/http.hpp`를 추가하고,
  `zlink_framework_options_t::http()`에서 `listen`, `tls`, `map_get`, `map_post`,
  `map_put`, `map_delete`, `use<TMiddleware>()` contract를 제공하게 했다.
- HTTPS endpoint는 certificate/private key가 없으면 startup validation에서
  `request_protocol_error`로 실패하게 했다.
- app host test에 HTTP route 등록과 HTTPS TLS validation 회귀를 추가했다.
- Bingo와 TicTacToe sample e2e log assertion에 monitoring marker, actor relay,
  disconnect, shutdown 확인을 추가했다.

### 남은 tradeoff

- HTTP public surface와 TLS startup validation은 닫았지만, 실제 Boost.Beast accept loop와
  request dispatcher는 runtime private owner에 붙을 후속 구현 영역으로 남아 있다. public
  header에는 Boost.Beast, Boost.Asio, OpenSSL 타입이 노출되지 않는다.
- sample e2e는 local stream server file log를 검증한다. 실제 다중 process server log와
  monitoring sink 통합은 같은 log vocabulary를 유지하면서 별도 e2e runner로 확장해야 한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-zlink --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-http --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-e2e --output-on-failure
ctest --test-dir bindings/cpp/build -R codec --output-on-failure
git diff --check -- framework/languages/cpp bindings/cpp
```

## 추가 리뷰. Framework options builder action 누적 제거

### 발견한 위험 신호

- `client_server_channel`, `route_mesh_channel`, `publisher_channel`, `stream_node` builder가
  체인 호출마다 runtime action을 vector에 추가하면 같은 channel 또는 stream 등록이 여러 번
  실행될 수 있다.
- 테스트가 마지막 등록 결과만 보게 되면 중복 등록 비용과 호출 순서 의존성이 숨어 남는다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| vector 누적을 유지 | 구현이 단순하다 | 중간 상태가 runtime에 반복 적용된다 |
| builder별 최종 snapshot 객체를 따로 둠 | 상태 표현이 명확하다 | 작은 builder마다 별도 snapshot 타입이 늘어난다 |
| key 기반 applier를 갱신 | 기존 builder 구조를 유지하면서 최종 선언만 적용한다 | action key 규칙을 state 내부에서 관리해야 한다 |

선택은 key 기반 applier 갱신이다. 이름이 있는 channel, route channel, publisher channel,
stream node 설정은 같은 key의 applier를 덮어써서 최종 상태만 한 번 적용한다.

### 적용한 POSD 원칙

- **정보 은닉**: builder 내부의 중간 상태와 호출 순서는 사용자에게 의미가 없어야 한다.
- **복잡성을 아래로**: 사용자는 `server().client().handler_group(...)` 같은 선언만 하고,
  framework가 최종 runtime 등록을 한 번으로 합쳐야 한다.

### 적용한 리팩토링

- `framework_options_state_t`에 key 기반 runtime action map을 추가했다.
- `client_server_channel`, `route_mesh_channel`, `publisher_channel`, `stream_node`는 mutation마다
  action을 추가하지 않고 같은 key의 applier를 갱신한다.
- `apply()`는 일반 deferred action을 먼저 실행한 뒤 key 기반 applier를 한 번씩 실행한다.
- `test_cpp_framework_module_hosted`에서 같은 `api-channel`에 server와 client를 함께 선언해도
  최종 channel snapshot이 하나만 생기고 양쪽 capability가 모두 남는지 확인한다.

### 재실행한 검증 명령

```bash
cmake --build --preset linux-ninja-debug
ctest --preset linux-ninja-debug --output-on-failure
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. Bingo Api Program/HostFactory 책임 분리 보정

### 발견한 위험 신호

- Bingo C++ `Server/Api/main.cpp`가 `.NET` `Program.cs`와 달리 host factory, DI, logging,
  monitoring, handler registration, serializer smoke 검증을 모두 직접 수행했다.
- `app.logging().use_callback_sink(...)`는 샘플 앱 설정이 아니라 로그 검증용 in-memory sink다.
  이 코드가 `main.cpp`에 있으면 사용자가 실제 API server에 필요한 설정으로 오해한다.
- `app.advanced().handlers().on_request<...>(...)`는 낮은 수준 extension/test 표면으로는 남을 수
  있지만, 샘플 host factory에 있으면 handler discovery/group 구성이 application entry point로
  새어 나온다.
- `api_server_host_factory_t::build(...)`가 완성된 host가 아니라 `zlink_builder_t`만 반환해
  `.NET`의 `ApiServerHostFactory.Build(topology).RunAsync()` 구조와 달랐다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `main.cpp`에 smoke 검증 유지 | 현재 CTest 유지가 쉽다 | 샘플 entry point가 실제 앱 코드처럼 보이지 않는다 |
| `main.cpp`에서 helper 함수만 호출 | 파일은 짧아진다 | 책임은 여전히 entry point에 남는다 |
| `api_server_host_factory_t`가 `app_t`를 완성해서 반환 | `.NET` Program/HostFactory 구조와 맞다 | factory 안에 C++ typed registration이 들어간다 |

선택은 세 번째 방식이다. C++은 assembly scan이 없으므로 typed handler registration은 factory
내부에 두되, `main.cpp`는 topology 생성과 `run(...)`만 남긴다.

### 적용한 리팩토링

- `sample_cpp_framework_bingo_api`의 `main.cpp`를 `ApiServerHostFactory.Build(...).RunAsync()`
  에 해당하는 구조로 줄였다.
- `api_server_host_factory_t::build(...)`가 `zlink_builder_t`가 아니라 완성된 `app_t`를
  반환하게 했다.
- API channel 구성, logging, monitoring, hosted service, service factory, handler
  registration을 `api_server_host_factory_t` 안으로 이동했다.
- 로그 검증용 callback sink, handler 직접 invoke, sample-local serializer smoke를
  `main.cpp`에서 제거했다.
- `AuthenticatePlayer`와 `MatchBingo` handler registration을 factory 내부 private 함수로
  숨겼다.

### 남은 tradeoff

- C++ framework에는 당시 `.NET`의 `AddHandlersFromAssemblyOf(...)`와 같은 자동 discovery가
  없다. C++에서는 reflection이 없으므로 typed registration helper나 module 단위 registration
  표면을 더 정리해야 한다.
- TicTacToe API와 다른 role sample도 같은 기준으로 다시 리뷰해야 한다. 이번 항목은 Bingo
  API entry point 누수를 먼저 막은 단계다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target sample_cpp_framework_bingo_api
ctest --test-dir framework/languages/cpp/build -R sample_smoke_sample_cpp_framework_bingo_api --output-on-failure
```

## 추가 리뷰. AddZLinkFramework 대응 C++ module API 보정

### 발견한 위험 신호

- Bingo API의 설정을 `main.cpp`에서 `api_server_host_factory_t`로 옮긴 뒤에도 factory 내부가
  services, logging, monitoring, handler registration, zlink runtime 구성을 순서대로 나열하는
  스크립트처럼 보였다.
- `.NET` sample은 `AddZLinkFramework(options => ...)` 안에서 handler discovery, codec,
  channel, discovery 구성을 한 고수준 진입점으로 묶는다. C++에는 이에 대응하는 public API가
  없어서 sample role factory가 framework 내부 구성 순서를 직접 보여 줬다.
- C++에는 assembly reflection이 없기 때문에 `.NET`의 `AddHandlersFromAssemblyOf(...)`를 그대로
  옮길 수 없다. 그렇다고 handler registration을 role factory에 직접 나열하면 Program/HostFactory
  구조가 계속 얕아진다.
- `app_t::add_zlink_framework<TModule>(...)`처럼 module type을 직접 넘기는 방식도 충분하지
  않다. 사용자가 봐야 하는 것은 module 생명주기나 DI factory가 아니라, `.NET` sample처럼
  discovery를 쓰고 client-server channel 두 개를 구성하며 handler group에 handler를 붙인다는
  의도다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| factory 안에서 private helper 함수로만 분리 | 구현이 작다 | framework 사용 표면 자체는 계속 거칠다 |
| 기존 `module_t` 인스턴스를 사용자가 직접 만들고 `add_module(...)` 호출 | 기존 API를 재사용한다 | `.NET AddZLinkFramework`처럼 role type을 넘기는 구성 진입점이 없다 |
| `app_t::add_zlink_framework<TModule>(...)` 추가 | module 단위로 세부 구성을 묶을 수 있다 | 사용자가 읽는 설정 수준이 여전히 module/DI/handler signature 중심이 된다 |
| `app_t::add_zlink_framework(options_callback)` 추가 | `.NET AddZLinkFramework(options => ...)`와 같은 읽기 수준을 제공한다 | options builder 계층을 새로 구현해야 한다 |

선택은 `add_zlink_framework(options_callback)`를 주 표면으로 두는 것이다. C++ role module은
내부 확장 단위로 유지할 수 있지만, 샘플과 사용자 문서에는 options builder를 노출한다. C++에서
reflection이 없다는 차이는 `AddHandlersFromAssemblyOf(...)`를
`options.handlers().add<THandler>(group_name)`으로 바꾸는 데서만 드러나야 한다.

### 적용한 리팩토링

- 문서 기준을 `app_t::add_zlink_framework<TModule>(...)` 중심에서
  `app_t::add_zlink_framework(options_callback)` 중심으로 수정했다.
- C++ sample API 설정의 목표 형태를 `.NET` `ApiServerHostFactory`와 같은 수준의 예제로 명시했다.
- handler 자동 검색은 C++에서 제공하지 않고,
  `options.handlers().add<authenticate_player_handler_t>("api")`처럼 handler 타입을 명시하는 것으로
  차이를 제한한다고 정리했다.
- `options.codecs().add_json()`은 codec 사용 선언만 맡기고, request/reply message type은 handler
  registration에서 framework가 읽어 serializer를 자동 등록하는 방향으로 낮췄다.
- C++에서는 람다 중첩이 `.NET`보다 장황해지므로 channel 설정은
  `options.client_server_channel(name).server(endpoint).handler_group(group)`처럼 fluent builder로
  표현한다고 정리했다.
- DI 생성자 주입을 추가해 `add_singleton<T, Dep...>()`, `add_scoped<T, Dep...>()`,
  `add_transient<T, Dep...>()`가 `service_provider_t`에서 의존성을 resolve한 뒤 생성자를 호출하게
  했다.
- handler는 `dependency_list_t<Dep...>`로 생성자 의존성을 명시한다. handler 등록은 이 목록을
  읽어 DI에 owner를 등록하므로, sample 설정에 handler용 factory lambda가 노출되지 않는다.
- module, handler용 DI factory, handler member function pointer, monitoring channel 문자열은
  낮은 수준 구현 세부로 분류했다. 이것들이 `main.cpp`, role `*HostFactory`, 일반 사용자 설정
  예제에 노출되면 당시 목표 수준에 도달하지 못한 것으로 본다.
- Bingo API `main.cpp`는 topology 생성과 `run(...)`만 남긴 상태를 유지했다.

### 남은 tradeoff

- Bingo와 TicTacToe의 Registry, API, Play, Session role factory는 모두
  `zlink_framework_options_t` 기반 options builder를 사용한다. 샘플 `main.cpp`는 topology 생성과
  `run(...)`만 수행한다.
- 샘플에서 낮은 수준 `use_zlink`, `channel`, `enable_server`, `enable_client`, handler용 DI
  factory, serializer registry 직접 설정이 다시 나타나지 않도록 sample parity 계약 테스트가
  source pattern을 확인한다.
- `zlink_builder_t`의 낮은 수준 API는 framework 내부 runtime과 단위 테스트용 확장 표면으로 남아
  있다. 일반 사용자 설정 표면인 `zlink_framework_options_t`에서는 람다 기반
  `add_client_server_channel(...)`과 `client_server_channel_options_t`를 제거했다.
- `stream_node` fluent builder는 `bind`와 `packet_session`이 모두 지정된 뒤에만 내부 stream
  builder에 반영한다. 이렇게 해야 체인 중간 상태가 runtime에 등록되어 저수준 검증 오류를 만드는
  문제를 막을 수 있다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target sample_cpp_framework_bingo_registry sample_cpp_framework_bingo_api sample_cpp_framework_bingo_play sample_cpp_framework_bingo_session sample_cpp_framework_tictactoe_registry sample_cpp_framework_tictactoe_api sample_cpp_framework_tictactoe_play sample_cpp_framework_tictactoe_session test_cpp_framework_sample_parity test_cpp_framework_contract_headers test_cpp_framework_module_hosted test_cpp_framework_DI_scope
ctest --test-dir framework/languages/cpp/build -R 'sample_smoke_sample_cpp_framework_(bingo|tictactoe)_(registry|api|play|session)|test_cpp_framework_sample_parity|test_cpp_framework_contract_headers|test_cpp_framework_module_hosted|test_cpp_framework_DI_scope' --output-on-failure
```

## 추가 리뷰. ActorContext JoinSpot 결과 구조 보정

### 발견한 위험 신호

- `.NET`의 `IZLinkActorContext.JoinSpot(...)`은 submit 결과로
  `ZLinkActorJoinResult<TReply>`를 돌려준다. 이 결과에는 result code, join 이후 actor ref,
  typed reply가 함께 있다. C++에는 `actor_context_t::join_spot(...)` 표면이 없었고,
  기존 `join_spot_call_t<TActor>`는 결과 타입이 actor 하나로 고정되어 있었다.
- `join_spot_call_t<TActor>`라는 이름은 JoinSpot 결과를 표현하는 것처럼 보이지만 typed reply를
  담을 자리가 없었다. 이 상태에서 `TActor` 자리에 join result를 넣으면 template 이름과 의미가
  어긋난다.
- actor gateway test는 session bind, relay, bound session push만 확인했다. actor context에서
  Spot/Entry Spot join을 submit하는 경로는 검증하지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `join_spot_call_t<TActor>` 유지 | 변경이 작다 | `.NET`의 actor ref + reply 결과 구조를 표현하지 못한다 |
| `join_spot_call_t<T>`의 `T`를 join result로 재해석 | 파일 변경이 적다 | 타입 이름이 거짓말을 하고 호출자 의미가 흐려진다 |
| `actor_join_result_t<TReply>`, `actor_join_spot_call_t<TReply>`, `actor_join_entry_spot_call_t` 추가 | `.NET` 결과 의미를 그대로 보존한다 | public 타입이 늘어난다 |

선택은 세 번째 방식이다. C++의 template 인자는 reply 타입을 표현해야 하며, actor ref와 result
code는 `actor_join_result_t<TReply>` 안에 함께 둔다.

### 적용한 리팩토링

- 사용되지 않던 `join_spot_call_t<TActor>`를 제거했다.
- `actor_join_result_t<TReply>`를 추가했다. 이 타입은 `.NET`의
  `ZLinkActorJoinResult<TReply>`와 같은 의미로 result code, actor ref, typed reply를 가진다.
- `actor_join_spot_call_t<TReply>`와 `actor_join_entry_spot_call_t`를 추가했다.
- `actor_context_t::join_spot<TRequest, TReply>(...)`와
  `actor_context_t::join_entry_spot(...)` public 표면을 추가했다.
- actor gateway runtime state에 Spot join dispatcher와 Entry Spot join dispatcher seam을
  추가했다. 실제 native/core transport 연결은 이 seam 안으로 들어갈 수 있다.
- `test_cpp_framework_ActorGateway_actor_session_relay`가 actor context에서 JoinSpot request
  payload를 보내고, typed reply와 actor ref를 함께 받는지 검증하도록 확장했다.
- 같은 test가 Entry Spot join에서 actor ref만 반환되는지도 검증한다.

### 남은 tradeoff

- 이번 변경은 actor context public contract와 actor gateway runtime seam을 닫은 단계다.
  실제 core ActorGateway transport, actor current Spot 저장, `GetSpot()` 표면은 다음 반복에서
  더 맞춰야 한다.
- JoinSpot request/reply codec은 현재 `message_t::from_json(...)`과 `parse_json<T>()`를
  사용한다. framework serializer registry와 결합할지는 sample/runtime 통합 단계에서 다시
  검토한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_ActorGateway_actor_session_relay
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_ActorGateway_actor_session_relay --output-on-failure
```

## 추가 리뷰. SPOT handler registry typed dispatch 보정

### 발견한 위험 신호

- `spot_context_t::handlers()`가 `.NET` `Context.Handlers`와 비슷한 등록 표면을
  제공했지만, 기존 확인은 descriptor 개수와 packet 이름에 머물렀다. 이 상태는 얕은
  모듈 신호다. 사용자는 handler를 등록했다고 생각하지만 framework가 실제 payload decode,
  DI resolve, handler 호출을 책임지는지 확인할 수 없었다.
- Play sample은 일부 도메인 handler를 직접 생성해 `handle(...)`을 호출했다. 그러면
  framework의 SPOT dispatch boundary가 샘플에서 검증되지 않고, application 코드가
  runtime 실행 순서를 알고 있어야 한다.
- Bingo join DTO는 JSON round-trip hook이 빠져 있어 serializer registry를 통과하는
  typed dispatch를 샘플에서 사용할 수 없었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| descriptor registry만 유지 | 구현이 작다 | `.NET` handler dispatch와 같은 기능이라고 보기 어렵다 |
| 샘플에서 직접 handler 호출 유지 | smoke가 단순하다 | framework가 메시지를 처리하는지 확인하지 못한다 |
| registry가 typed invoker를 저장하고 DI/serializer를 통해 실행 | 등록 표면과 실행 의미가 연결된다 | C++ template helper가 늘어난다 |

선택은 typed invoker 저장 방식이다. C++은 reflection이 없으므로 handler, actor, message,
reply type을 template 인자로 받되, payload decode와 owner resolve는 runtime으로 숨긴다.

### 적용한 리팩토링

- `spot_handler_registry_t`가 descriptor와 함께 erased invoker를 저장하도록 보강했다.
- `invoke_packet`, `invoke_actor_join`, `invoke_actor_packet`, actor lifecycle invoke
  표면을 추가했다.
- SPOT handler dispatch는 `serializer_registry_t`로 `message_t`를 DTO로 바꾸고,
  `service_provider_t`에서 handler owner를 resolve한 뒤 typed `handle(...)`을 호출한다.
- `test_cpp_framework_spot_runtime`이 descriptor 확인뿐 아니라 실제 packet, actor join,
  actor packet dispatch를 검증하도록 보강했다.
- Bingo/TicTacToe Play smoke가 등록된 join/packet handler를 framework dispatch 경로로
  실행하도록 보강했다.
- Bingo `bingo_room_join_req_t`, `bingo_room_join_res_t`에 JSON hook을 추가해 application
  layer 직접 field parsing 없이 serializer registry를 사용할 수 있게 했다.

### 남은 tradeoff

- 일부 sample lifecycle handler는 당시 `.NET`처럼 Spot instance와 actor change result를
  모두 받는 완성형 시그니처가 아니다. 현재 registry는 실행 가능한 handler shape는 실제
  호출하고, 불일치 shape는 protocol error로 보고한다. 다음 단계에서는 lifecycle handler
  shape와 actor context/result 모델을 `.NET`과 더 맞춰야 한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_spot_runtime test_cpp_framework_sample_parity sample_cpp_framework_bingo_play sample_cpp_framework_tictactoe_play
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_spot_runtime|test_cpp_framework_sample_parity|sample_smoke_sample_cpp_framework_(bingo|tictactoe)_play' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. Install consumer regression 고정

### 발견한 위험 신호

- framework와 connector package를 수동으로 설치한 뒤 consumer를 빌드하는 검증은 있었지만
  CTest 회귀에 고정되어 있지 않았다. export target이나 native runtime 설치 규칙이 다시
  깨져도 일반 unit test만으로는 잡기 어렵다.
- `zlink::stream_connector` install export는 static library link interface에
  `Threads::Threads`를 포함하지만 package config가 `find_dependency(Threads)`를 호출하지
  않으면 설치 consumer configure 단계에서 실패한다.
- Linux native runtime은 SONAME이 `libzlink.so.6`인데 install 규칙이 `libzlink.so`만 복사하면
  consumer 실행 단계에서 loader가 runtime을 찾지 못한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 수동 install 검증만 유지 | CTest 시간이 짧다 | 배포 회귀가 자동으로 잡히지 않는다 |
| header compile test만 추가 | 빠르다 | export config와 runtime loader 문제를 검증하지 못한다 |
| CTest에서 install, consumer configure/build/run까지 수행 | 실제 배포 경계를 검증한다 | package test 시간이 조금 늘어난다 |

선택은 CTest package consumer 검증이다. `.NET`의 contract/e2e test project처럼 C++도
배포 경계를 테스트 목록에 고정한다.

### 적용한 리팩토링

- `tests/Zlink.Framework.PackageTests/install_consumer.cmake`를 추가했다.
- 새 CTest `test_cpp_framework_install_consumer`가 현재 build tree를 격리 prefix에 설치하고,
  별도 consumer project를 생성해 `find_package(zlink_framework_cpp)`와
  `find_package(zlink_stream_connector_cpp)`를 실행한다.
- consumer는 `zlink::framework`, `zlink::framework_extension_metrics`,
  `zlink::stream_connector`, `zlink::stream_connector_codecs`를 링크하고
  `auto_codec.hpp`를 실제로 include한다.
- package config에 `find_dependency(Threads)`를 추가했다.
- Linux install 규칙은 `libzlink.so*` 파일을 함께 설치해 SONAME이 요구하는
  `libzlink.so.6`도 배포 prefix에 들어가게 했다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build -DZLINK_STREAM_CONNECTOR_WITH_LZ4=ON
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_install_consumer --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Public contract header coverage 보정

### 발견한 위험 신호

- `.NET` framework contract tests는 reflection으로 모든 public contract interface가 scenario
  example에 포함되는지 검증한다. C++는 reflection이 없어서 새 public contract header가
  생겨도 compile coverage 없이 지나갈 수 있었다.
- facade header만 include하면 개별 contract header가 독립적으로 compile되는지 보장하지
  못한다.
- layout test가 주요 파일 존재 여부는 보지만, `contracts/*.hpp`가 contract compile test에
  포함되는지까지는 확인하지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| facade include만 유지 | 테스트가 짧다 | 개별 public header 독립성이 깨져도 숨는다 |
| 모든 public header를 install consumer에서만 확인 | 배포 경계는 본다 | source tree에서 coverage 누락이 바로 드러나지 않는다 |
| contract header test가 모든 `contracts/*.hpp`를 직접 include하고 layout test가 coverage를 스캔 | C++ 방식으로 surface coverage를 고정한다 | include 목록을 명시적으로 관리해야 한다 |

선택은 direct include coverage다. C++에는 `.NET` reflection이 없으므로 파일 시스템과
compile test를 결합해 같은 목적을 달성한다.

### 적용한 리팩토링

- `test_cpp_framework_contract_headers.cpp`가 framework와 stream connector의 모든
  public `contracts/*.hpp`를 직접 include하도록 확장했다.
- `test_cpp_framework_layout_contract.cpp`가 public contract header tree를 스캔하고, 각
  header가 contract header compile test에 직접 include되어 있지 않으면 실패하게 했다.
- 이 검증은 `framework/include/zlink/framework/contracts`와
  `connector/include/zlink/stream_connector/contracts` 양쪽에 적용된다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_contract_headers test_cpp_framework_layout_contract
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(contract_headers|layout_contract)' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Stream Connector auto codec helper 보정

### 발견한 위험 신호

- `.NET`에는 `Systems.Zlink.Stream.Connector.Codecs`가 있어 typed send/request/on에서
  JSON, MessagePack, Protobuf 선택을 호출자 코드 밖으로 숨긴다. C++ connector는 codec enum과
  registry는 있었지만 auto codec helper target이 없어 사용자가 payload encode와 codec id를
  직접 맞춰야 했다.
- typed auto codec을 `zlink::stream_connector` 기본 target에 직접 붙이면 사용하지 않는
  codec dependency가 기본 connector 사용자에게 전파된다.
- C++에는 `.NET` attribute reflection이 없으므로 MessagePackObject, Protobuf IMessage 같은
  런타임 자동 선택을 그대로 복제하면 언어 특성과 맞지 않는다.
- 샘플 DTO에 sample-only `to_stream_payload`/`from_stream_payload` helper나 직접 JSON field
  parser가 들어가면 application code가 serializer를 알아야 한다. 이는 `.NET` 샘플의
  `connector.Request(dto).SubmitAsync<T>()` 흐름과 다르고, codec 선택 지식이 샘플로
  새어 나오는 얕은 모듈이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 codec registry만 유지 | 변경이 작다 | `.NET Codecs` 패키지 역할이 비어 있다 |
| 기본 connector target에 모든 codec helper 포함 | 사용법이 단순하다 | MessagePack/Protobuf dependency를 강제한다 |
| 같은 배포물 안에 별도 `stream_connector_codecs` target 제공 | 의존성을 선택적으로 유지하면서 auto helper를 제공한다 | 사용자가 helper target을 명시적으로 링크해야 한다 |
| 샘플 DTO별 payload helper 유지 | 샘플 loopback 작성이 쉽다 | serializer 정책이 application sample에 퍼지고 `.NET` connector 사용성과 달라진다 |

선택은 별도 helper target이다. C++ auto codec 선택은 기본 JSON으로 두고, MessagePack과
Protobuf는 `codec_traits<T>` 특수화로 명시한다.

샘플 DTO는 C++ serializer가 찾을 수 있는 `to_json`/`from_json` hook만 제공한다. client
application code는 `.NET`과 같은 수준에서 `codecs::request<TReply>`, `codecs::send`,
`codecs::on<T>`만 호출한다. sample-only payload 변환 함수나 직접 JSON parser는 두지 않는다.

### 적용한 리팩토링

- `zlink/stream_connector/codecs/auto_codec.hpp`를 추가했다.
- `codec_traits<T>` 기본 구현은 `message_t::from_json`과 `message.parse_json<T>()`를 사용한다.
- `codecs::send`, `codecs::request`, `codecs::on` helper를 추가해 encoded packet 생성,
  codec id 지정, typed callback decode를 한 곳에 모았다.
- connector public call object에 `codec(codec_t)` setter를 추가하고, `packet_t` 기반
  `send`/`request` overload를 추가해 encoded payload가 public API로 이동할 수 있게 했다.
- CMake에 `zlink::stream_connector_codecs` interface target을 추가했다. 이 target은
  `zlink::stream_connector`와 `zlink::cpp_codec_json`을 링크하고, MessagePack/Protobuf는
  build option과 binding codec target이 있을 때만 연결한다.
- `test_cpp_stream_connector`가 auto codec send frame의 `codec=json`, packet name, JSON
  payload를 실제 loopback에서 확인하고, `codecs::on<T>`가 dispatch 전에 JSON payload를 DTO로
  복원하는지 검증한다.
- Bingo/TicTacToe client sample이 `zlink::stream_connector::codecs::{send,request,on}`을
  사용하도록 보정했다. 샘플 application code는 raw STREAM payload 조립이나 직접 JSON parse를
  하지 않는다.
- Bingo/TicTacToe DTO contract header에서 sample-only `to_stream_payload`와
  `from_stream_payload` helper를 제거하고, `to_json`/`from_json` hook만 남겼다.
- sample loopback server는 reply/push frame payload를 만들 때 `message_t::from_json`을
  내부 helper에서 사용한다. 이 코드는 샘플 server transport adapter의 세부이며 client
  application API가 아니다.
- layout contract test가 client sample의 connector codec helper include와
  `codecs::request`, `codecs::on`, `.submit()` 사용을 확인하도록 보강했다.

### 남은 tradeoff

- C++ auto codec은 `.NET`처럼 attribute reflection으로 MessagePack/Protobuf를 자동 선택하지
  않는다. 해당 codec을 쓰는 타입은 `codec_traits<T>` 특수화로 선택한다.
- DTO별 `to_json`/`from_json` hook은 C++의 ADL 기반 serializer 연결점이다. 이것은 `.NET`
  record/attribute metadata에 해당하는 계약 선언이지, application layer serializer 호출이
  아니다.

### 재실행할 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build -DZLINK_STREAM_CONNECTOR_WITH_LZ4=ON
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_cpp_stream_connector --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
rg -n "to_stream_payload|from_stream_payload|json_value|json_field|nlohmann::json::parse" framework/languages/cpp/samples
```

## 추가 리뷰. Unreal Stream Connector pending dispatch 보정

### 발견한 위험 신호

- Unreal connector public API에는 `PendingDispatchCount()`가 있었지만 runtime이 항상 0을
  반환했다. 이 상태에서는 manual dispatch mode에서 처리할 callback이 쌓였는지 확인할 수
  없다.
- `.NET` connector는 manual dispatch에서 callback queue count를 노출하고, dispatch 전에는
  handler가 실행되지 않는 것을 테스트한다. Unreal connector도 같은 의미를 Unreal delegate
  모델에 맞게 제공해야 한다.
- Unreal connector에 coroutine surface까지 추가하면 Unreal 사용 모델과 맞지 않고 public
  API가 불필요하게 넓어진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 항상 0 유지 | 구현이 짧다 | public API가 거짓 상태를 노출한다 |
| background receive thread 추가 | `.NET`과 queue 시점이 가장 비슷하다 | Unreal Game Thread/lifecycle 정책과 충돌하기 쉽다 |
| `PendingDispatchCount()`에서 socket bytes를 private queue로만 pump | callback 실행을 지연하면서 manual dispatch 의미를 유지한다 | query가 receive buffer를 갱신한다 |

선택은 private queue pump 방식이다. `PendingDispatchCount()`는 socket에서 완성 frame을 읽어
private dispatch queue에 넣지만 callback은 실행하지 않는다. `Dispatch()`는 같은 queue를
drain하면서 delegate를 호출한다.

### 적용한 리팩토링

- Unreal private runtime에 `DispatchQueue`를 추가했다.
- socket receive와 frame decode를 `PumpIncomingFrames()`로 분리했다.
- `PendingDispatchCount()`가 `PumpIncomingFrames()`를 호출한 뒤 queue size를 반환하게 했다.
- `Dispatch()`는 queue를 drain하며 push는 `OnPacketReceived`, response는 pending request와
  match될 때 `OnRequestCompleted`로 보낸다.
- Unreal Automation Test가 response/push frame 수신 뒤 `PendingDispatchCount() == 1`,
  callback 미실행, `Dispatch()` 후 count 0과 callback 실행을 검증하도록 확장됐다.
- Unreal connector에는 coroutine API를 추가하지 않는다. Unreal 표면은 Blueprint/native
  delegate와 Game Thread dispatch를 기준으로 유지한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_unreal_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_unreal_stream_connector --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Unreal Stream Connector request completion 보정

### 발견한 위험 신호

- Unreal `RequestJson`은 request frame을 보내지만 response frame을 받을 public 표면이
  없었다. 이 상태에서는 요청/응답 connector가 아니라 send-only client처럼 보인다.
- response completion을 `OnPacketReceived`와 섞으면 push notification과 request reply가
  같은 callback으로 들어와 호출자가 request sequence 구분을 직접 해야 한다.
- pending request sequence table을 public API로 노출하면 `.NET`과 일반 C++ connector가
  숨기는 correlation 세부가 Unreal 사용자 표면으로 새어 나온다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `RequestJson`을 fire-and-forget으로 유지 | 변경이 작다 | request/reply 기능성이 빠진다 |
| response를 `OnPacketReceived`로 보냄 | callback 수가 적다 | push와 reply 구분을 사용자에게 떠넘긴다 |
| 별도 request completed delegate 제공 | Unreal event 모델에 맞고 reply 의미가 분리된다 | pending request table을 private runtime에서 관리해야 한다 |

선택은 별도 request completed delegate다. public API는 response packet만 받고, request
sequence 발급과 match 여부는 Unreal private runtime이 소유한다.

### 적용한 리팩토링

- `FZLinkStreamRequestCompleted`와 `FZLinkStreamRequestCompletedNative` delegate를 추가했다.
- `UZLinkStreamConnector`에 `OnRequestCompleted`와 `OnRequestCompletedNative` event를
  추가했다.
- `RequestJson`과 `RequestJsonWithOptions`가 sequence를 발급하고 private pending request
  table에 등록하게 했다.
- `Dispatch`가 `kind=response` frame을 만나면 request sequence가 pending table에 있을 때만
  `OnRequestCompleted`로 전달하고 pending entry를 제거한다.
- Unreal Automation Test가 request frame의 sequence를 읽고 response frame을 보낸 뒤
  request completed callback의 packet name과 metadata를 검증하도록 확장됐다.

### 남은 tradeoff

- 현재 Unreal request completion은 response event surface를 제공한다. typed UObject
  serializer와 per-request callback/future style helper는 다음 API 확장 대상이다.
- Engine 없는 CTest는 event surface compile smoke만 확인한다. 실제 response frame matching은
  Unreal Automation Test가 담당한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_unreal_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_unreal_stream_connector --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Logging 표면 보정

### 발견한 위험 신호

- `app.logging().use_console().set_level()`은 상태만 저장하고, handler가 실제 log를 남길
  public logger 표면이 없었다.
- `.NET` 샘플은 `ILogger<T>`를 handler에 주입해 actor join, room event, packet dispatch를
  기록하지만 C++ 샘플은 handler logging을 보여 주지 못했다.
- `spdlog`를 내부 구현으로 쓰겠다는 정책은 있었지만 public API에서 어떤 sink와 level을
  지원하는지 고정되어 있지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `spdlog::logger`를 public API로 노출 | 구현이 빠르다 | backend 교체가 public breaking change가 된다 |
| console level만 유지 | 작다 | 운영 logging과 handler logging을 지원하지 못한다 |
| ZLink logging abstraction 제공 | `.NET`의 `ILogger<T>`와 역할이 맞다 | 내부 logging runtime 구현이 필요하다 |

선택은 ZLink 자체 logging abstraction이다. public API는 `logger_t<TCategory>`와
`logger_factory_t`가 소유하고, backend와 sink 구현은 runtime 안에 숨긴다.

### 적용한 리팩토링

- `log_level_t`, `logging_backend_t`, `logging_overflow_policy_t`, `log_record_t`,
  `logger_t<TCategory>`, `logger_factory_t`를 public contract에 추가했다.
- `logging_builder_t`에 console, file, rotating file, callback sink, async option,
  backend selection, captured records 조회를 추가했다.
- diagnostics logging runtime에 level filtering, console/file/rotating/callback sink 호출을
  구현했다.
- Bingo/TicTacToe 핵심 handler가 logger를 받아 실제 log를 남기도록 샘플을 보강했다.
- app host regression test가 callback sink, file sink, level filtering, backend selection을
  검증하도록 확장했다.

### 남은 tradeoff

- 현재 backend selection은 public 계약으로 `spdlog`를 선택할 수 있게 고정했지만 public
  header에는 `spdlog` 타입을 노출하지 않는다. 실제 spdlog sink 최적화는 이 abstraction
  뒤에서 확장한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L framework-unit --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Stream Connector LZ4 실제 구현

### 발견한 위험 신호

- `lz4_compression_codec_t`가 payload를 그대로 돌려주는 no-op이었다. public option과
  compression flag가 있어도 실제 압축이 일어나지 않아 테스트가 성공해도 운영 동작을
  검증할 수 없었다.
- 시스템에 `liblz4.so.1`만 있고 개발 header가 없으면 system dependency만으로는 현재
  workspace에서 재현 가능한 빌드를 만들 수 없었다.
- 압축 flag는 frame header에 기록되지만 send/read 경로에서 payload 변환과 decompression
  실패 처리가 연결되어 있지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| no-op 유지 | 빌드는 쉽다 | compression 계약이 거짓 동작이 된다 |
| system LZ4 dev package만 요구 | 배포 의존성이 명확하다 | 개발 환경마다 헤더 설치가 필요하고 현재 workspace에서 바로 깨진다 |
| system LZ4를 우선 찾고 없으면 source fallback 포함 | 재현 가능한 빌드와 실제 압축을 모두 얻는다 | CMake가 C 언어와 FetchContent fallback을 관리해야 한다 |

선택은 system 우선, source fallback 방식이다. connector 사용자는 여전히
`zlink::stream_connector` 하나만 링크하고, LZ4 dependency는 connector target 내부 구현으로
숨긴다.

### 적용한 리팩토링

- `ZLINK_STREAM_CONNECTOR_WITH_LZ4` 기본값을 ON으로 바꿨다.
- CMake가 system `lz4.h`/`liblz4`를 먼저 찾고, 없으면 LZ4 1.10.0 source를 받아
  `zlink_stream_connector` private source로 포함하게 했다.
- `lz4_compression_codec_t`가 `LZ4_compress_default`와 `LZ4_decompress_safe`를 호출하도록
  구현했다. compressed payload는 원본 크기 4바이트와 LZ4 bytes로 저장한다.
- send/request frame 작성 경로가 `.compress()`와 `compression_t::lz4`를 만나면 실제
  payload를 압축하고 `payload_compressed` flag를 기록한다.
- read/dispatch 경로가 `payload_compressed` flag를 만나면 payload를 해제하고 실패 시
  `decompression_failed`로 반환한다.
- `test_cpp_stream_connector`가 LZ4 roundtrip, compressed send frame, compressed server push
  수신과 해제를 실제 TCP stream 경로에서 검증한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build -DZLINK_STREAM_CONNECTOR_WITH_LZ4=ON
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## 추가 리뷰. Sample handler 파일 분류 보정

### 발견한 위험 신호

- `Bingo/Server/Play/EntrySpot/match_bingo_actor_handler.hpp`,
  `TicTacToe/Server/Play/EntrySpot/join_match_handler.hpp`,
  `TicTacToe/Server/Play/GameSpots/place_mark_handler.hpp`는 실제 구현 없이 `Handlers/*`
  header만 include하는 wrapper였다. `.NET` 샘플은 handler 구현체가 `Handlers/` 아래에
  직접 있으므로, wrapper는 파일 구조 parity를 보여 주지 못한다.
- `BingoRoomSpots/bingo_room_handlers.hpp`는 여러 handler를 묶는 aggregate header였고,
  `bingo_room_timer_handler.hpp`가 다시 이 aggregate를 include했다. 이 구조는 실제
  의존성보다 include 편의가 앞서며, 어떤 파일이 handler owner인지 흐리게 만든다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| wrapper 유지 | include 경로가 짧다 | 구현 없는 파일이 구조 parity처럼 보이고 owner가 흐려진다 |
| wrapper에 실제 구현을 옮김 | 파일 수는 유지된다 | `.NET`의 `Handlers/*` owner와 달라진다 |
| wrapper 제거, 실제 `Handlers/*` 구현체를 직접 include | `.NET` 파일 분류와 맞고 얕은 모듈이 사라진다 | include 사용처를 갱신해야 한다 |

선택은 wrapper 제거다. C++ 샘플은 언어 스타일상 `.hpp`를 쓰지만, handler 구현의 소유 위치는
`.NET`과 같은 `Handlers/*` 아래로 고정한다.

### 적용한 리팩토링

- 실제 구현 없는 handler wrapper header 3개를 삭제했다.
- `BingoRoomSpots/bingo_room_handlers.hpp` aggregate header를 삭제했다.
- `Bingo`와 `TicTacToe` sample include를 실제 `Handlers/*` 구현체 경로로 바꿨다.
- `bingo_room_timer_handler.hpp`는 aggregate header 대신 실제 의존성인 `bingo_room.hpp`를
  include하도록 보정했다.
- layout contract test가 삭제된 wrapper/aggregate header가 다시 생기면 실패하도록
  `require_absent` 검사를 추가했다.

### 남은 tradeoff

- `Shared/sample.hpp`는 sample parity test 편의를 위한 include surface다. 다만 이제 구현 없는
  wrapper를 통하지 않고 실제 role/handler owner를 직접 include한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_sample_parity test_cpp_framework_layout_contract sample_cpp_framework_bingo_client sample_cpp_framework_tictactoe_client
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(sample_parity|layout_contract)' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. Sample Session role 구현 분리

### 발견한 위험 신호

- `.NET` Bingo와 TicTacToe는 `Server/Session/Sessions/*`에 session class를 두고,
  `Sessions/Handlers/*`에 authenticate/create-match packet handler를 둔다. C++ 샘플은
  Session role의 핵심 흐름이 `main.cpp` smoke 로직에만 있었기 때문에 실제 framework 사용
  구조를 리뷰하기 어려웠다.
- `main.cpp` 안에서 actor bind와 relay를 직접 실행하면 session dispatch의 순서가 sample
  executable 구현 지식으로 남는다. `.NET`의 `IZLinkSession.OnDispatchAsync`처럼 handler를
  먼저 시도하고, 처리되지 않은 packet을 bound actor에게 relay하는 정책이 별도 모듈로
  보이지 않는다.
- 요청 DTO에는 `to_json` hook만 있고 일부 `from_json` hook이 빠져 있었다. session handler가
  typed request를 `message.parse_json<T>()`로 읽으려면 serializer hook이 DTO contract에
  있어야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `main.cpp` smoke 유지 | 변경이 작다 | Session role 파일 분류와 dispatch 정책이 드러나지 않는다 |
| session logic을 framework runtime test로만 검증 | 테스트는 강해진다 | 샘플이 `.NET` sample 구조와 계속 다르다 |
| `Sessions/*`와 `Sessions/Handlers/*`에 실제 session/handler 구현 추가 | `.NET` 구조와 같고 리뷰 대상이 분명하다 | sample header가 늘어난다 |

선택은 session/handler 구현 추가다. C++에서는 header-only sample class로 두지만, 역할 분류는
`.NET`과 같은 `Server/Session/Sessions`와 `Sessions/Handlers`로 맞춘다.

### 적용한 리팩토링

- Bingo에 `Sessions/bingo_session.hpp`와
  `Sessions/Handlers/authenticate_session_handler.hpp`를 추가했다.
- TicTacToe에 `Sessions/session_relay_session.hpp`,
  `Sessions/Handlers/authenticate_session_packet_handler.hpp`,
  `Sessions/Handlers/create_match_session_packet_handler.hpp`를 추가했다.
- session class는 `.NET`처럼 packet handler를 먼저 시도하고, 처리되지 않은 packet은 인증
  뒤 bound actor로 relay한다.
- session handler는 `message.parse_json<T>()`와 `message_t::from_json(...)`만 사용한다.
  application layer의 직접 JSON field parsing은 추가하지 않았다.
- Bingo/TicTacToe request DTO에 빠져 있던 `from_json` hook을 추가했다.
- Session role `main.cpp`는 새 session/handler class를 실제로 생성해 authenticate, actor
  bind, create-match reply, relay, disconnect cleanup을 smoke한다.
- layout contract test가 `Server/Session/Sessions/*`와 `Sessions/Handlers/*` 파일 존재를
  확인하도록 보강했다.

### 남은 tradeoff

- C++ sample handler는 `.NET`의 `IZLinkChannelClient` 대신 sample-local handler 객체를
  주입받는다. 현재 C++ framework sample runtime에서는 실제 channel client DI를 모두 띄우는
  e2e가 아니라 role별 smoke로 검증하기 때문이다. public 흐름은 `message_t` codec,
  session packet handler, actor bind, relay 순서로 맞췄다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target sample_cpp_framework_bingo_session sample_cpp_framework_tictactoe_session test_cpp_framework_layout_contract
ctest --test-dir framework/languages/cpp/build -R 'sample_smoke_sample_cpp_framework_(bingo|tictactoe)_session|test_cpp_framework_layout_contract' --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. Unreal Stream Connector LZ4 frame parity 보정

### 발견한 위험 신호

- Unreal connector에는 `payload_compressed` flag enum이 있었지만 public option과 payload
  변환 경로가 없었다. 이 상태에서는 일반 C++ connector가 보낸 compressed frame을 Unreal
  callback에서 그대로 binary payload로 받게 된다.
- Unreal plugin public header나 Build.cs에 일반 connector runtime 또는 system LZ4 의존성을
  노출하면 Unreal 전용 배포 단위가 깨진다.
- compressed send/request를 automation에서 읽지 않으면 flag만 맞고 payload가 실제로
  복원되는지 확인할 수 없다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| compression option을 계속 숨김 | Unreal public 변경이 없다 | 일반 connector와 기능 격차가 남는다 |
| 일반 C++ connector runtime을 Unreal module에 링크 | codec 중복이 줄어든다 | Asio/runtime dependency가 Unreal plugin 내부로 새어 들어온다 |
| Unreal `Private/`에 wire-format LZ4 helper를 둠 | public 의존성을 숨기고 같은 frame 형식을 처리한다 | LZ4 helper를 contract test로 계속 고정해야 한다 |

선택은 Unreal `Private/` helper 방식이다. 일반 C++ connector와 같은 payload 형식인
`원본 크기 4바이트 + LZ4 block`을 사용하되, public 표면은 `FZLinkStreamSendOptions`의
`bCompress` 하나로 유지한다.

### 적용한 리팩토링

- `FZLinkStreamSendOptions::bCompress`를 Unreal public contract에 추가했다.
- Unreal frame encoder가 `bCompress`를 받으면 payload를 LZ4 block payload로 바꾸고
  `payload_compressed` flag를 기록하게 했다.
- Unreal frame decoder가 `payload_compressed` flag를 만나면 callback 전에 payload를
  해제하고, `FZLinkStreamPacket::bCompressed`에 원래 frame 상태를 기록하게 했다.
- Unreal Automation Test loopback 서버가 compressed send/request를 읽어 원본 JSON으로
  복원하고, compressed push/response가 native callback에서 복원되는지 검증하도록 확장했다.
- Engine 없는 CTest smoke가 `bCompress` option을 사용하는 코드까지 컴파일한다.

### 남은 tradeoff

- CMake 검증 경로에서는 Unreal static target도 system LZ4 또는 FetchContent fallback을
  private로 받아 실제 `LZ4_compress_default`와 `LZ4_decompress_safe`를 호출한다. Unreal
  Engine Build.cs 배포에서는 같은 방식을 plugin `ThirdParty/LZ4` source vendoring으로
  닫아야 한다.
- Engine 없는 CTest는 Unreal socket automation을 실행하지 못한다. 실제 compressed frame
  loopback은 `ZLink.StreamConnector.Loopback` Automation Test에서 담당한다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_unreal_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_unreal_stream_connector --output-on-failure
ctest --test-dir framework/languages/cpp/build --output-on-failure
```

## Goal 22. Runtime coverage regression gate 추가

### 발견한 위험 신호

- CTest 전체가 통과해도 framework runtime 소스가 충분히 실행됐는지 수치로 확인하는 gate가
  없었다. 이 상태에서는 regression test가 많아 보여도 HTTP client, HTTP hosting,
  connector runtime의 누락 경로가 조용히 남을 수 있다.
- coverage 계산에 tests, samples, generated install consumer를 섞으면 실제 runtime 품질보다
  테스트 코드 실행량이 숫자를 올린다.
- 현재 개발 환경에는 `lcov`나 `gcovr`가 없었다. 특정 외부 도구에만 의존하면 coverage
  regression이 환경마다 실행되지 않을 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| CTest 통과만 최종 gate로 둠 | 추가 도구가 필요 없다 | 테스트가 runtime을 얼마나 덮는지 모른다 |
| `lcov`나 `gcovr`를 필수 도구로 요구 | 보고서 생성이 편하다 | 현재 workspace에서 바로 실행되지 않고 환경 의존성이 늘어난다 |
| CMake coverage build와 `gcov` 기반 threshold script를 둠 | 기본 toolchain만으로 CTest gate를 만들 수 있다 | HTML 보고서는 별도 도구가 있을 때 추가해야 한다 |

선택은 CMake coverage build와 `gcov` 기반 threshold script다. coverage 대상은
`framework/src`, `http-client/src`, `connector/src`, Unreal connector private source로
제한하고, 테스트와 샘플은 계산에서 제외한다.

### 적용한 리팩토링

- `ZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE`와 `ZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD` CMake
  option을 추가했다.
- coverage build에서 `zlink_framework`, `zlink_http_client`, `zlink_stream_connector`,
  `zlink_unreal_stream_connector` runtime target에 coverage compile/link option을 붙였다.
- `tests/Zlink.Framework.Coverage/coverage_threshold.cmake`를 추가해 `.gcda`를 `gcov`로
  읽고 runtime source line coverage를 계산하게 했다.
- `test_cpp_framework_coverage_threshold` CTest를 추가하고 기존 regression test 뒤에 실행되게
  했다.
- Goal 22 완료 기준과 검증 명령에 runtime line coverage 70% 이상 기준을 추가했다.

### 수정 후 점검

- coverage script는 tests, samples, external dependency, build generated source를 coverage
  분모에 넣지 않는다.
- 현재 coverage build 기준 runtime line coverage는 77.69%다.
- 기준값 70% 미만이면 CTest가 실패하므로 이후 회귀 테스트 추가/삭제가 숫자로 검증된다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build-coverage -DZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE=ON -DZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD=70
cmake --build framework/languages/cpp/build-coverage
ctest --test-dir framework/languages/cpp/build-coverage --output-on-failure
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. Goal 검증 명령 empty-selection gate 제거

### 발견한 위험 신호

- plan의 Goal 3, Goal 6, Goal 19 검증 명령은 각각 `-R async`, `-R parity`, `-R http`를
  함께 사용한다. CTest label은 존재했지만 test name이 해당 정규식과 맞지 않아 조합 실행 시
  empty test selection가 선택됐다.
- 이미 같은 executable이 async contract, sample parity, HTTP hosting integration을 검증하고
  있었지만 CTest 이름으로 그 의미가 드러나지 않았다. 이 상태는 테스트 의도가 CMake 내부
  label 지식에 숨어 있는 정보 은닉 실패다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| plan의 `-R` 조건을 제거 | 문서 수정만으로 empty selection을 없앨 수 있다 | Goal별로 어떤 테스트를 의도했는지 약해진다 |
| 기존 executable 이름을 바꿈 | 이름과 의도가 맞아진다 | 기존 test name을 쓰는 회귀 명령과 로그가 흔들린다 |
| 같은 executable을 의미 있는 CTest alias로 추가 | 기존 이름을 유지하면서 Goal별 gate가 실제 테스트를 실행한다 | CTest 항목 수가 늘어난다 |

선택은 CTest alias 추가다. 새 binary를 만들지 않고 기존 executable을 다른 test name으로
등록해 plan의 검증 명령이 empty selection 없이 실행되게 했다.

### 적용한 리팩토링

- `test_cpp_framework_async_contract`를 추가해 `test_cpp_framework_contract_headers`를
  `framework-unit;async` gate로도 실행하게 했다.
- `test_cpp_framework_parity_contract`를 추가해 `test_cpp_framework_sample_parity`를
  `framework-regression;parity` gate로도 실행하게 했다.
- `test_cpp_framework_http_integration`을 추가해 `test_cpp_framework_app_host`를
  `framework-integration;http` gate로도 실행하게 했다.

### 수정 후 점검

- plan에 적힌 `ctest -L framework-unit -R async`,
  `ctest -L framework-regression -R parity`,
  `ctest -L framework-integration -R http`는 모두 1개 이상의 테스트를 선택한다.
- alias는 기존 executable을 재사용하므로 테스트 구현 지식은 한 곳에 남는다.

### 재실행할 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -N -L framework-unit -R async
ctest --test-dir framework/languages/cpp/build -N -L framework-regression -R parity
ctest --test-dir framework/languages/cpp/build -N -L framework-integration -R http
ctest --test-dir framework/languages/cpp/build --output-on-failure
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. HTTP client public boundary contract 보강

### 발견한 위험 신호

- Goal 18과 Goal 22는 HTTP client public header가 runtime 구현과 Boost.Beast, Boost.Asio,
  OpenSSL 타입을 노출하지 않아야 한다고 요구한다. 하지만 layout contract는 framework와
  connector public include만 검사했고, HTTP client public include tree는 같은 강도로 보지
  않았다.
- `test_cpp_framework_contract_headers`는 `<zlink/http_client.hpp>` facade만 include했다.
  `http-client/include/zlink/http_client/contracts/*`가 직접 include 가능한지 검증하지
  않으면 contract owner가 facade 뒤에 숨어도 테스트가 통과할 수 있다.
- public dependency 검색이 수동 `rg`에만 의존하면 이후 header 추가 때 회귀가 자동으로
  잡히지 않는다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 수동 `rg` 검증만 유지 | 구현 변경이 없다 | 새 public header 추가 시 자동 회귀가 없다 |
| HTTP client 전용 새 테스트 executable 추가 | 책임이 분리된다 | layout boundary 검사가 여러 파일로 흩어진다 |
| 기존 layout/contract test에 HTTP client public tree를 포함 | 같은 public surface gate에서 framework, connector, HTTP client를 함께 본다 | layout contract가 조금 커진다 |

선택은 기존 layout/contract test 확장이다. public surface gate는 산출물별로 같은 규칙을
적용해야 하므로, HTTP client도 같은 테스트에서 확인한다.

### 적용한 리팩토링

- `test_cpp_framework_contract_headers`가
  `<zlink/http_client/contracts/client.hpp>`를 직접 include하도록 보강했다.
- `test_cpp_framework_layout_contract`가 `http-client/include`, `http-client/src/runtime`,
  `http_client_runtime.*` 존재를 확인하게 했다.
- layout contract가 framework, connector, HTTP client, Unreal public header에서
  Boost/Beast/Asio/OpenSSL/GoogleTest/GoogleMock/spdlog/fmt runtime/test 타입 노출을
  자동 검색하게 했다.
- HTTP client public headers도 runtime include 금지와 contract compile coverage 검사를
  통과해야 한다.

### 수정 후 점검

- `logging_backend_t::spdlog`는 public 설정 enum 값이므로 `spdlog::` 타입이나
  `<spdlog/...>` header 노출로 보지 않는다.
- JSON DTO 변환을 위해 사용하는 선택 codec 또는 `nlohmann::json` template helper는 이번
  runtime dependency 검색 대상이 아니다. codec dependency 분리는 Goal 2와 HTTP client JSON
  계약의 별도 기준으로 유지한다.
- 새 boundary 검사는 `framework-contract` label에서 실행된다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_layout_contract test_cpp_framework_contract_headers
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(layout_contract|contract_headers|async_contract)' --output-on-failure
```

## 추가 리뷰. HTTP hosting HTTPS e2e와 병렬 포트 충돌 보정

### 발견한 위험 신호

- HTTP hosting draft는 HTTPS loopback에서 test certificate로 JSON request/response가 성공해야
  한다고 적고 있었다. 하지만 app host test는 HTTPS endpoint의 TLS option validation만
  확인했고, 실제 HTTPS listener를 시작해 `zlink::http_client`로 호출하지 않았다.
- `test_cpp_framework_app_host`와 CTest alias인 `test_cpp_framework_http_integration`은 같은
  executable과 같은 fixed port를 사용한다. 별도 build tree나 `ctest -j`가 동시에 실행되면
  bind 충돌로 실패할 수 있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| HTTPS validation만 유지 | 테스트가 빠르다 | TLS handshake와 HTTP client trust path를 검증하지 못한다 |
| HTTPS 전용 새 executable 추가 | 포트 분리가 쉽다 | HTTP hosting lifecycle 테스트가 둘로 나뉜다 |
| 기존 app host test에 HTTPS loopback을 추가하고 CTest resource lock/포트 분리를 적용 | HTTP/HTTPS hosting lifecycle을 한 곳에서 검증하고 병렬 실행도 안정화한다 | test compile definition이 늘어난다 |

선택은 기존 app host test 보강이다. HTTP hosting lifecycle은 같은 app host runtime owner를
통과하므로 하나의 e2e 안에서 HTTP와 HTTPS를 함께 검증한다.

### 적용한 리팩토링

- OpenSSL과 test certificate가 있는 빌드에서 `test_cpp_framework_app_host`가 HTTPS listener를
  시작하고, `zlink::http_client`의 `trust_certificate_file(...)`로 `GET` readiness와
  `POST /games` JSON response를 검증하게 했다.
- normal build와 coverage build가 동시에 실행되어도 충돌하지 않도록 app host test endpoint를
  CMake compile definition으로 주입하고 coverage build에는 별도 port를 사용하게 했다.
- `test_cpp_framework_app_host`와 `test_cpp_framework_http_integration`에 같은 CTest
  `RESOURCE_LOCK`을 걸어 한 CTest 프로세스 안의 병렬 실행에서도 같은 port를 동시에 잡지
  않게 했다.

### 수정 후 점검

- HTTP handler e2e는 HTTP와 HTTPS 모두 `zlink::http_client`를 사용한다.
- TLS certificate/private key 누락 validation과 HTTPS listener handshake/request/response를
  같은 `framework-http`/`framework-http-e2e` label에서 검증한다.
- normal build와 coverage build의 `framework-http` label을 동시에 실행해도 port 충돌 없이
  통과한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake -S framework/languages/cpp -B framework/languages/cpp/build-coverage -DZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE=ON -DZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD=70
cmake --build framework/languages/cpp/build --target test_cpp_framework_app_host
cmake --build framework/languages/cpp/build-coverage --target test_cpp_framework_app_host
ctest --test-dir framework/languages/cpp/build -L framework-http -j2 --output-on-failure
ctest --test-dir framework/languages/cpp/build-coverage -L framework-http -j2 --output-on-failure
```

## Goal 1. Tooling contract smoke 보강

### 발견한 위험 신호

- Goal 1은 CMake presets, vcpkg manifest, CLion/Visual Studio configure 가능성을 완료 조건으로
  둔다. 파일은 있었지만 `framework-contract` label이 해당 파일의 필수 preset과 dependency를
  자동 검증하지 않았다.
- tooling 조건을 수동 확인에만 맡기면 preset 이름 변경이나 Visual Studio generator 누락이
  plan 검증에서 빠질 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 문서 검증 명령만 유지 | 추가 테스트가 없다 | CMakePresets/vcpkg drift를 자동으로 잡지 못한다 |
| 실제 Windows Visual Studio configure를 Linux CTest에서 실행 | 가장 직접적인 검증이다 | 현재 Linux/WSL 환경에서는 Visual Studio generator를 실행할 수 없다 |
| preset/manifest contract와 `cmake --list-presets=all` smoke를 CTest로 추가 | 현재 환경에서 실행 가능하고 CLion/Visual Studio/WSL preset drift를 잡는다 | 플랫폼별 실제 generator configure는 해당 플랫폼 CI가 담당해야 한다 |

선택은 tooling contract smoke다. Linux/WSL에서도 preset 파일의 구조와 CMake preset 인식은
검증할 수 있고, Windows generator 실제 configure는 Windows 환경에서 같은 preset을 사용한다.

### 적용한 리팩토링

- `tests/Zlink.Framework.ContractTests/verify_tooling_contract.cmake`를 추가했다.
- tooling contract가 `CMakePresets.json`의 Linux Ninja, Linux vcpkg, Windows MSVC preset과
  `Visual Studio 17 2022` generator, C++20/test/sample 기본 option을 확인한다.
- `vcpkg.json`의 `boost-asio`, `gtest`, `lz4`, `nlohmann-json` dependency를 확인한다.
- `cmake --list-presets=all`이 주요 configure/build/test preset을 실제로 나열하는지
  확인한다.
- `test_cpp_framework_tooling_contract`를 `framework-contract;framework-package` label로
  등록했다.

### 수정 후 점검

- Goal 1의 tooling 조건이 `framework-contract` label 안에서 실행된다.
- Goal 22의 package/tooling regression도 `framework-package` label에서 같은 smoke를 포함한다.
- Linux CTest에서 Visual Studio generator 자체를 실행하지는 않는다. 대신 preset 존재와 CMake
  인식 여부를 고정하고, Windows 실제 configure는 Windows 환경의 같은 preset으로 실행한다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
cmake -S framework/languages/cpp -B framework/languages/cpp/build-coverage -DZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE=ON -DZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD=70
ctest --test-dir framework/languages/cpp/build-coverage -L framework-contract --output-on-failure
```

## Goal 2. Codec boundary와 sample JSON 사용 gate 보강

### 발견한 위험 신호

- Goal 2는 base C++ binding이 JSON, MessagePack, Protobuf dependency를 끌고 오지 않아야
  한다고 요구한다. codec roundtrip test는 있었지만, base `zlink_cpp` target의 link interface가
  codec dependency를 갖지 않는다는 gate는 없었다.
- framework sample application code가 직접 JSON parser로 field를 꺼내지 않는다는 조건도 수동
  검색에 가까웠다. DTO serializer hook은 `nlohmann::json`을 사용할 수 있지만, handler/client
  application code가 `nlohmann::json::parse`나 `json.at(...)`를 쓰면 Goal 2 사용성 기준을
  우회한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 codec roundtrip test만 유지 | 테스트 수가 늘지 않는다 | base target dependency 누출과 sample 직접 JSON parsing을 잡지 못한다 |
| sample 코드 리뷰를 수동으로 반복 | 의도를 사람이 판단하기 쉽다 | 회귀 자동화가 없다 |
| target boundary report와 layout contract 검색을 추가 | CTest가 dependency와 sample 사용 규칙을 자동으로 검증한다 | contract test가 파일 구조를 더 많이 읽는다 |

선택은 자동 gate 추가다. Goal 2는 dependency isolation과 사용성 규칙이 핵심이므로, 테스트가
둘 다 직접 확인해야 한다.

### 적용한 리팩토링

- `bindings/cpp/tests/contract/verify_codec_target_contract.cmake`를 추가했다.
- `bindings/cpp/CMakeLists.txt`가 configure 단계에서 `codec-target-contract.txt`를 만들고,
  base `zlink_cpp` link interface와 선택 codec target link interface를 기록하게 했다.
- `test_cpp_contract_codec_target_boundary`가 base target에 `nlohmann_json`, `msgpack`,
  `protobuf` dependency가 없는지 확인한다. 선택 codec target은 base `zlink_cpp`를 링크해야
  한다.
- `test_cpp_framework_layout_contract`가 sample application code에서
  `nlohmann::json::parse`와 DTO serializer hook 외부 `json.at(...)` 사용을 금지하게 했다.

### 수정 후 점검

- `ctest --test-dir bindings/cpp/build -R codec`은 JSON, MessagePack, Protobuf roundtrip과
  codec target boundary를 함께 실행한다.
- sample DTO `to_json`/`from_json` hook은 허용된다. handler/client application code는
  `message_t::from_json`, `message.parse_json<T>()`, stream connector codec helper를 사용한다.
- framework sample layout contract가 이 규칙을 `framework-contract` label 안에서 고정한다.

### 재실행한 검증 명령

```bash
cmake -S bindings/cpp -B bindings/cpp/build -DZLINK_CPP_BUILD_TESTS=ON
cmake --build bindings/cpp/build --target test_cpp_contract_codec_json test_cpp_contract_codec_messagepack test_cpp_contract_codec_protobuf
ctest --test-dir bindings/cpp/build -R codec --output-on-failure
cmake --build framework/languages/cpp/build --target test_cpp_framework_layout_contract
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_layout_contract --output-on-failure
```

## Goal 16. Health readiness/liveness 표면 보강

### 발견한 위험 신호

- Goal 16은 monitoring, health, readiness, liveness를 함께 완료 조건으로 둔다. 기존 구현은
  typed runtime event와 `health_status_t` enum은 제공했지만, 사용자가 runtime 상태를
  health/readiness/liveness report로 읽는 public 표면이 없었다.
- health를 monitoring event payload의 필드로만 두면 호출자는 health check 등록, 상태 갱신,
  readiness/liveness 집계를 직접 반복해야 한다. 이는 복잡성을 호출자에게 올리는 얕은 표면이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| monitoring event의 `health` 필드만 사용 | 새 API가 없다 | readiness/liveness 집계 책임이 사용자에게 넘어간다 |
| HTTP `/health` route만 추가 | HTTP sample에서 확인하기 쉽다 | health가 HTTP 전용처럼 보이고 zlink/registry/stream/hosted service 상태를 담기 어렵다 |
| `health_builder_t`를 별도 contract로 추가 | health/readiness/liveness 집계를 하나의 깊은 모듈에 숨긴다 | 작은 public API가 추가된다 |

선택은 `health_builder_t` 추가다. health는 HTTP endpoint보다 넓은 운영 표면이므로 app host의
독립 contract로 두고, HTTP endpoint 매핑은 후속 확장으로 연결할 수 있게 둔다.

### 적용한 리팩토링

- `contracts/eventing/health.hpp`를 추가하고 `app_t::health()`에서 접근하게 했다.
- `health_builder_t`가 zlink runtime, channel, registry, stream endpoint, hosted service check를
  등록하고 `health_report_t`로 전체 status, readiness, liveness를 집계한다.
- 집계 구현은 `src/runtime/diagnostics/health.cpp`에 숨겨 public header가 runtime 자료구조를
  노출하지 않게 했다.
- `test_cpp_framework_monitoring`에 healthy, channel unhealthy readiness, hosted service
  unhealthy liveness 회귀 테스트를 추가했다.

### 수정 후 점검

- Goal 16의 health/readiness/liveness 완료 조건은 `framework-observability` label에서 실행된다.
- health check는 HTTP에 종속되지 않고 zlink channel, registry, STREAM endpoint, hosted service를
  같은 report에 담는다.
- HTTP `map_health`/`map_readiness`/`map_liveness` route sugar는 Goal 19 pass에서
  `app.health()` report를 읽는 system route로 연결했다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_monitoring test_cpp_framework_contract_headers
ctest --test-dir framework/languages/cpp/build -L framework-observability --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-unit -R monitoring --output-on-failure
git diff --check -- framework/languages/cpp
```

## Goal 19. HTTP route/query binding 보강

### 발견한 위험 신호

- HTTP host runtime은 `"/games/{id}"` 같은 route pattern을 매칭할 수 있었지만, `{id}` 값을
  handler `request_type` DTO에 전달하지 않았다. 이 상태는 route parameter binding 완료 조건을
  매칭 성공과 혼동하게 만든다.
- query string은 route 선택에서 제거만 되고 DTO에 반영되지 않았다. 사용자는 handler 안에서
  raw target을 다시 파싱해야 하므로 HTTP parser 세부 지식이 호출자에게 새는 얕은 모듈이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| handler에 raw HTTP target/context를 넘김 | 모든 정보를 직접 볼 수 있다 | Beast/HTTP parser 지식이 handler 표면으로 올라온다 |
| route/query 전용 request context를 별도 인자로 추가 | 타입이 명확하다 | 기존 `handle(const request_type&)` 규칙이 흔들린다 |
| route/query/body를 DTO JSON으로 병합 | handler 규칙을 유지한다 | 문자열 기반 query 값은 DTO serializer가 최종 타입 변환을 맡는다 |

선택은 DTO JSON 병합이다. Goal 19는 message handler와 같은 `request_type`/`reply_type` 규칙을
요구하므로, route/query binding은 runtime 아래에서 흡수한다.

### 적용한 리팩토링

- HTTP runtime이 route match 결과로 route parameter map과 query map을 함께 만든다.
- request body JSON, query string, route parameter를 하나의 JSON object로 병합한 뒤
  `request_type` deserialize에 넘긴다. 우선순위는 route, query, body 순서다.
- `test_cpp_framework_app_host`가 `GET /games/1?filter=active`와 `PUT /games/1` body를
  ZLink HTTP client로 호출해 route/query/body binding을 확인한다.

### 수정 후 점검

- handler는 `Boost.Beast` request나 raw target을 보지 않는다.
- route/query binding은 HTTP hosted service 내부에서 처리되고, public HTTP API는
  `map_get<THandler>`, `map_post<THandler>` 형태를 유지한다.
- middleware/filter는 당시 type registration과 pipeline extension point 수준이다. 실제 auth/
  validation middleware chaining은 후속 HTTP extension에서 넓히되, Beast/Asio 타입을 public
  표면에 올리지 않는 원칙은 layout contract가 고정한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_app_host
ctest --test-dir framework/languages/cpp/build -L framework-http-e2e --output-on-failure
```

## Goal 19. HTTP middleware/correlation pipeline 보강

### 발견한 위험 신호

- `use<TMiddleware>()`가 middleware type 이름만 snapshot에 저장하고 request 처리 경로에서는
  호출되지 않았다. 이 상태는 middleware/filter pipeline 완료 조건을 등록 API 존재와 혼동하게
  만든다.
- correlation id는 문서에서 cross-cutting 처리 대상으로 언급됐지만 request header에서 읽어
  handler와 response로 전파하는 runtime 경로가 없었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| raw Beast request/response를 middleware에 넘김 | 구현이 직접적이다 | Boost.Beast 타입이 public 표면으로 새고 Goal 19 public 경계와 충돌한다 |
| middleware를 이름 등록만 하는 extension point로 유지 | API가 작다 | 실제 pipeline 회귀 테스트를 만들 수 없다 |
| `http_context_t`를 추가하고 before/after hook만 노출 | HTTP 문맥을 framework 타입으로 숨긴다 | 작은 public context 타입이 추가된다 |

선택은 `http_context_t` 기반 pipeline이다. middleware가 필요한 correlation/header/status 정보만
보고, socket/parser/runtime 타입은 HTTP hosted service 내부에 둔다.

### 적용한 리팩토링

- `contracts/http/http.hpp`에 `http_context_t`와 `http_middleware_t`를 추가했다.
- `use<TMiddleware>()`가 `before(http_context_t&)`, `after(http_context_t&)` 또는
  service-aware overload를 request 전후에 호출하도록 했다.
- HTTP runtime이 `X-Correlation-Id` 또는 `X-Request-Id`를 `http_context_t::correlation_id`로
  읽고 response `X-Correlation-Id`에 전파한다.
- handler가 `handle(request, http_context_t&)`를 제공하면 context를 받고, 기존
  `handle(request)` handler는 그대로 지원한다.
- `test_cpp_framework_app_host`가 ZLink HTTP client로 correlation header와 middleware response
  header를 확인한다.

### 수정 후 점검

- middleware/filter는 Beast/Asio/OpenSSL 타입을 받지 않는다.
- 기존 HTTP handler signature와 sample은 깨지지 않는다.
- auth/validation/logging middleware의 구체 정책은 이 pipeline 위의 사용자 middleware로
  확장할 수 있다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_app_host test_cpp_framework_contract_headers
ctest --test-dir framework/languages/cpp/build -L framework-http --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-http-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-integration -R http --output-on-failure
```

## Goal 16/19. HTTP health route와 middleware short-circuit 보강

### 발견한 위험 신호

- `app.health()`는 readiness/liveness report를 제공했지만 HTTP app 사용자가 `/health`,
  `/ready`, `/live` endpoint를 만들려면 별도 handler와 DTO를 직접 작성해야 했다. 이는 운영
  endpoint라는 반복 규칙을 호출자에게 넘기는 얕은 모듈이다.
- HTTP middleware는 before/after hook과 correlation propagation을 제공했지만, validation/auth
  같은 middleware가 handler 실행을 멈추고 JSON response를 반환하는 경로가 없었다. 이 상태는
  cross-cutting concern을 handler 안에 다시 넣게 만든다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 사용자 handler로 health endpoint를 만들게 함 | framework API가 늘지 않는다 | 모든 app이 health DTO와 status mapping을 반복한다 |
| HTTP runtime이 고정 경로 `/health`만 자동 노출 | 설정이 가장 작다 | app별 route 정책과 충돌하고 readiness/liveness 분리가 어렵다 |
| `map_health`/`map_readiness`/`map_liveness`를 system route로 제공 | route 정책은 사용자가 정하고 집계 규칙은 framework가 숨긴다 | 작은 HTTP builder API가 추가된다 |

선택은 system route builder 추가다. health 집계는 `app.health()`가 계속 소유하고, HTTP는 같은
report를 JSON으로 노출하는 얇은 transport adapter만 맡는다.

middleware short-circuit은 raw Beast response를 넘기는 대신 `http_context_t::json_response`로
표현했다. 이렇게 하면 middleware가 socket/parser 타입을 모르면서도 auth/validation 실패
응답을 만들 수 있다.

### 적용한 리팩토링

- `http_options_builder_t`에 `map_health`, `map_readiness`, `map_liveness`를 추가했다.
- `http_host_service_t`가 app의 `health_builder_t`를 받아 system health route를 먼저 처리한다.
- readiness/liveness가 `unhealthy`이면 HTTP status를 `503 Service Unavailable`로 반환한다.
- `http_context_t::json_response(...)`를 추가하고 middleware before hook에서 설정하면 handler
  호출을 건너뛰도록 했다.
- `test_cpp_framework_app_host`가 ZLink HTTP client로 `/ready`, `/health`, `/live`와
  middleware short-circuit response를 검증한다.

### 수정 후 점검

- health route는 Beast/Asio/OpenSSL 타입을 public header에 노출하지 않는다.
- health 집계 규칙은 `contracts/eventing/health.hpp`와 diagnostics runtime에 남아 있고,
  HTTP runtime은 JSON 노출만 담당한다.
- route별 filter 타입은 별도 public API로 만들지 않고, middleware가 method/path를 읽어
  필요한 route에만 적용한다. 현재 Goal 19 범위에서 남은 POSD 리팩토링 이슈는 0개다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_app_host
ctest --test-dir framework/languages/cpp/build -L framework-http --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-http-e2e --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-observability --output-on-failure
```

## 전체 POSD 재리뷰. Connector, HTTP transport, sample client 정책 정리

### 발견한 위험 신호

- `auto_codec.hpp`가 typed request 결과를 얻기 위해 `on_completed` callback을 호출하고
  local `optional`에 결과를 다시 담았다. 이는 connector `task_t`가 즉시 완료된다는 내부
  구현 사실을 auto codec이 알아야 하는 back-door leakage다.
- HTTP host는 HTTP와 HTTPS handler에서 request를 읽고 framework response를 쓰는 규칙을
  각각 반복했다. transport 차이는 handshake와 shutdown인데, dispatch 규칙까지 두 곳에
  퍼져 있었다.
- HTTP client는 HTTP와 HTTPS 분기에서 `write`, `read`, raw response 변환, timeout 판정을
  반복했다. status/timeout 정책 변경 시 두 분기를 함께 고쳐야 하는 change amplification이다.
- Bingo와 TicTacToe client sample은 connector option 구성과 connector `result_t`를 sample
  call result로 바꾸는 규칙을 각자 반복했다. 샘플 사용법 정책이 게임별 client에 새어 있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현재 구조 유지 | 코드 이동이 없다 | task completion, HTTP exchange, sample connector 정책이 여러 곳에 반복된다 |
| 큰 공통 runtime 모듈로 통합 | 중복이 가장 많이 줄어든다 | framework, HTTP client, connector target 경계를 흐릴 수 있다 |
| 각 target 안에서 정보 owner만 좁게 추출 | public 표면과 target 경계를 유지하면서 반복 지식을 줄인다 | 작은 helper가 몇 개 추가된다 |

선택은 target 안의 좁은 owner 추출이다. POSD 관점에서 이번 문제는 public API를 새로 늘릴
문제가 아니라, 이미 존재하는 task/result/transport 정책이 다른 파일에 새지 않게 하는 문제다.

### 적용한 리팩토링

- connector `task_t`에 `consume_result()`를 추가해 coroutine handle 저장과 direct result 저장
  차이를 `task_t` 내부로 숨겼다.
- `auto_codec.hpp`의 typed request 변환은 `on_completed` capture 대신 `consume_result()`를
  사용한다.
- layout contract가 auto codec에서 `.result`, immediate callback capture, local
  `optional<result_t>` 패턴이 다시 들어오지 못하게 검사한다.
- HTTP host listener에 `serve_request(...)`를 추가해 HTTP/HTTPS transport가 같은 request
  dispatch 규칙을 공유하게 했다.
- HTTP client runtime에 `exchange_request(...)`와 `finish_response(...)`를 추가해
  HTTP/HTTPS 분기의 response 변환과 timeout 판정을 한 곳으로 모았다.
- sample 공통 `client_connector_helpers.hpp`를 추가해 immediate connector option과 sample
  call result 변환 규칙을 Bingo/TicTacToe client 밖으로 옮겼다.
- channel runtime의 pending admission 검사를 `ensure_pending_admission(...)`으로 모아
  shutdown, closed, pending capacity 정책을 request/send 경로가 공유하게 했다.
- connector inbound packet dispatch 정책을 `deliver_received_packet(...)`으로 모아
  direct receive 경로와 request 중 push packet 수신 경로가 같은 dispatch mode 규칙을 쓰게 했다.

### 수정 후 점검

- connector auto codec은 task completion 저장 방식이나 callback 즉시 실행 여부를 알 필요가
  없다.
- HTTP host와 HTTP client는 transport별 setup만 분기하고 request/response 처리 정책은 각
  target 안의 한 helper가 소유한다.
- sample client는 게임별 packet 흐름과 notification registration만 드러내고, connector
  option 구성과 error string 변환은 공통 helper를 사용한다.
- channel request/send admission은 연결 필요 여부 같은 경로별 차이만 남고, lifecycle과
  capacity 거절 정책은 한 helper가 소유한다.
- connector는 push packet을 즉시 handler로 넘길지 dispatch queue에 넣을지 결정하는 규칙을
  한 내부 helper가 소유한다.
- 이번 재리뷰 패스에서 추가로 남은 POSD 리팩토링 이슈는 발견하지 못했다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_layout_contract test_cpp_stream_connector sample_cpp_framework_bingo_client sample_cpp_framework_tictactoe_client
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_layout_contract|test_cpp_stream_connector|sample_smoke_sample_cpp_framework_bingo_client|sample_smoke_sample_cpp_framework_tictactoe_client' --output-on-failure
cmake --build framework/languages/cpp/build --target test_cpp_framework_app_host test_cpp_http_client
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_app_host|test_cpp_framework_http_integration|test_cpp_http_client' --output-on-failure
cmake --build framework/languages/cpp/build --target test_cpp_framework_channel_messaging test_cpp_framework_backpressure_reliability
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_channel_messaging|test_cpp_framework_backpressure_reliability' --output-on-failure
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_cpp_stream_connector --output-on-failure
```

## 반복 POSD 재리뷰. Connector receive dispatch 정책 단일화

### 발견한 위험 신호

- `deliver_received_packet(...)`은 connector가 받은 push packet을 즉시 handler로 넘길지
  dispatch queue에 쌓을지 결정했다. 그러나 socket에서 남은 push frame을 drain하는
  `drain_available_pushes(...)`도 같은 `dispatch_mode` 분기를 직접 가지고 있었다. 이는
  dispatch mode 정책이 두 파일에 퍼진 정보 누출이다.
- `receive_dispatcher_t`는 `dispatch_queue.push_back(...)`만 감싸는 내부 클래스였고 실제
  호출자가 없었다. 인터페이스가 구현보다 얕은 dead module이므로, 유지하면 connector
  receive 경로를 읽을 때 존재하지 않는 추상화를 따라가게 만든다.
- `pending_requests_t`, `receive_loop_t`, `task_runner_t`, `typed_handler_registry_t`도
  connector runtime에서 호출되지 않는 내부 wrapper였다. 대부분 상태 container 크기를 읽거나
  함수를 바로 실행하는 수준이라, 유지할수록 실제 connector 경로보다 파일 구조가 더 복잡해진다.
- HTTP app host 테스트는 HTTP host와 HTTPS host가 준비될 때까지 `/ready`를 ZLink HTTP
  client로 polling하는 절차를 두 번 반복했다. 이 절차는 테스트 안정화 정책이므로 각
  시나리오 본문에 직접 남아 있으면 readiness 판정 변경 시 두 곳을 함께 고쳐야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현재 구조 유지 | 코드 이동이 없다 | dispatch mode 정책 변경 시 `deliver_received_packet`과 drain 경로를 함께 고쳐야 한다 |
| `drain_available_pushes`에 작은 private helper 추가 | 파일 내부 중복은 줄어든다 | 이미 있는 `deliver_received_packet`과 정책 owner가 둘로 남는다 |
| 모든 inbound push packet을 `deliver_received_packet`으로 통과시킴 | dispatch mode 정책 owner가 하나가 된다 | framing 코드가 runtime helper를 호출한다 |

선택은 모든 inbound push packet을 `deliver_received_packet`으로 통과시키는 것이다. frame을
읽는 책임은 framing module에 남기고, 읽은 뒤 connector가 packet을 사용자 handler로 보낼지
queue에 둘지는 connector runtime helper가 소유한다.

### 적용한 리팩토링

- `drain_available_pushes(...)`가 `dispatch_mode`를 직접 읽지 않고
  `deliver_received_packet(...)`을 호출하도록 바꿨다.
- 사용되지 않던 `receive_dispatcher_t`와 source 등록을 제거했다.
- 사용되지 않던 connector 내부 wrapper header 네 개를 제거했다.
- layout contract에서 삭제된 내부 파일 존재 요구를 제거했다.
- `test_cpp_framework_app_host`의 HTTP/HTTPS readiness polling을 `wait_for_ready(...)`로
  모아, host 준비 판정은 한 helper가 소유하고 테스트 본문은 시나리오 검증에 집중하게 했다.

### 수정 후 점검

- connector의 push packet dispatch mode 결정은 `deliver_received_packet(...)` 한 곳에 남았다.
- framing module은 stream frame 읽기와 packet 구성만 담당한다.
- 삭제한 dispatcher와 wrapper header들은 public header가 아니며, connector public 계약에는
  영향을 주지 않는다.
- app host 테스트는 여전히 ZLink HTTP client로 `/ready`를 확인하지만, polling loop와 대기
  간격은 테스트 helper 하나에만 남았다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector test_cpp_framework_layout_contract
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_stream_connector|test_cpp_framework_layout_contract' --output-on-failure
cmake --build framework/languages/cpp/build --target test_cpp_framework_app_host
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_app_host --output-on-failure
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
cmake --build framework/languages/cpp/build-coverage
ctest --test-dir framework/languages/cpp/build-coverage --output-on-failure
cmake -DZLINK_FRAMEWORK_CPP_BUILD_DIR=/home/hep7/project/kairos/zlink/framework/languages/cpp/build-coverage -DZLINK_FRAMEWORK_CPP_SOURCE_DIR=/home/hep7/project/kairos/zlink/framework/languages/cpp -DZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD=70 -P framework/languages/cpp/tests/Zlink.Framework.Coverage/coverage_threshold.cmake
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. HTTP Client 문서 탐색 표면 보정

### 발견한 위험 신호

- Goal 18에서 `zlink::http_client`가 별도 산출물로 승격됐지만, 일부 C++ draft 문서의
  상단 묶음 링크에는 `HTTP Client`가 빠져 있었다. 독자가 HTTP hosting 문서에서만 client
  표면을 찾게 되면 plan의 산출물 경계와 문서 탐색 경계가 어긋난다.
- `cpp-http-client.ko.md`는 HTTP hosting만 되돌아갈 수 있고 framework interface 문서로 바로
  이어지지 않았다. public surface를 검토하는 흐름에서 client 계약을 interface 문맥과 대조하기
  어렵다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| README 링크만 유지 | 한 곳에서 전체 목록을 볼 수 있다 | 개별 문서에서 HTTP client 산출물이 보이지 않는다 |
| 모든 문서에 긴 전체 링크 목록을 복제 | 탐색 누락이 줄어든다 | 문서마다 유지보수 비용과 소음이 늘어난다 |
| HTTP와 interface 인접 문서의 묶음 링크만 보강 | Goal 18/19 흐름에서 필요한 탐색 경계를 닫는다 | 전체 문서 내비게이션 생성기는 별도 개선 과제로 남는다 |

선택은 HTTP와 interface 인접 문서의 묶음 링크 보강이다. HTTP client는 HTTP hosting,
application framework, framework interface, policy를 읽을 때 함께 확인해야 하는 산출물이므로
그 경로에서만 링크를 추가해 문서 소음을 늘리지 않는다.

### 적용한 리팩토링

- `cpp-framework-policy.ko.md`, `cpp-application-framework.ko.md`,
  `cpp-framework-interfaces.ko.md`의 묶음 링크에 `HTTP Client`를 추가했다.
- `cpp-http-client.ko.md`의 묶음 링크에 `Framework 인터페이스`를 추가해 public surface
  검토 흐름으로 돌아갈 수 있게 했다.

### 수정 후 점검

- Goal 18 `ZLink HTTP Client`는 README, implementation plan, policy, application framework,
  framework interface, HTTP client, HTTP hosting 문서에서 탐색 가능하다.
- 문서 본문에 남은 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 추가 리뷰. HTTP Client contract label 보강

### 발견한 위험 신호

- Goal 18 검증 명령의 `http-client-contract` label은 실제 빌드에서 비어 있지는 않았지만
  `test_cpp_framework_contract_headers`만 실행했다. 이 상태에서는 HTTP client public header
  compile smoke는 보지만 typed JSON request/response, status mapping, timeout, HTTPS trust 같은
  실제 client 계약 테스트는 contract gate에 포함되지 않는다.
- `test_cpp_http_client`는 `http-client-unit`, `http-client-e2e`, `http-client-regression`,
  OpenSSL 사용 시 `http-client-https`에는 들어가지만 contract label에는 빠져 있었다. 검증
  label 의미가 test target의 책임과 어긋난다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현재 label 유지 | 테스트 선택 수는 이미 0이 아니다 | contract gate가 실제 HTTP client 계약을 실행하지 않는다 |
| header smoke에서 unit/e2e label 제거 | label 의미를 더 좁힐 수 있다 | 기존 non-empty gate와 과거 검증 명령의 기대 선택 수를 갑자기 줄인다 |
| `test_cpp_http_client`에 `http-client-contract`를 추가 | 실제 client 계약 테스트가 contract gate에 포함된다 | contract label 선택 수가 늘어난다 |

선택은 `test_cpp_http_client`에 contract label을 추가하는 것이다. Header compile smoke는 public
include 경계를 지키고, 실제 HTTP client 테스트는 동작 계약을 지키므로 둘 다 contract gate에
속하는 편이 plan의 완료 기준에 맞다.

### 적용한 리팩토링

- `test_cpp_http_client` 기본 label에 `http-client-contract`를 추가했다.
- OpenSSL 사용 시 재설정되는 `test_cpp_http_client` label에도 `http-client-contract`를
  유지하도록 맞췄다.

### 수정 후 점검

- `ctest --test-dir framework/languages/cpp/build -L http-client-contract -N`은 header smoke와
  실제 HTTP client test를 함께 선택해야 한다.
- Goal 18 HTTP client contract gate에 남은 label/테스트 매핑 이슈는 0개다.

## 추가 리뷰. TicTacToe HTTP 시작 handler 경계 보정

### 발견한 위험 신호

- Goal 21과 HTTP hosting draft는 TicTacToe client가 HTTP `POST /games`로 시작하고
  `Server/Api` role의 handler 흐름을 지나야 한다고 적는다. 그러나 client e2e는 client 파일
  안에 둔 임시 HTTP handler를 route에 직접 붙여 `Server/Api/Handlers/create_match_handler_t`
  경계를 우회하고 있었다.
- 임시 handler는 고정된 `"tictactoe-game"` 응답만 만들었다. 이 상태에서는 sample API role의
  DI handler 구성과 create-match handler가 깨져도 client e2e가 통과할 수 있다.
- STREAM mock server가 HTTP 응답으로 받은 match id를 보존하지 않고 thread 시작 시점의 기본
  `"tictactoe-game"`을 reply state에 넣었다. 또한 모든 request에 `place_mark_res_t` 형태로
  답해 typed reply 계약이 약하게 검증됐다.
- HTTP hosting draft는 `POST /games` 응답의 `play_endpoint`로 stream connector가 연결한다고
  설명하지만, 샘플 DTO와 API handler 응답에는 endpoint가 없었다. 이 상태에서는 문서의 시작
  흐름과 실제 connector 입력이 서로 다른 지식을 갖게 된다.
- TicTacToe client e2e가 API HTTP endpoint를 고정 port로 열어 반복 실행이나 외부 프로세스와
  충돌할 수 있었다. 샘플 회귀 테스트가 환경 상태에 흔들리면 실제 샘플 문제와 포트 점유 문제를
  구분하기 어렵다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 임시 handler 유지 | e2e 구성이 단순하다 | 문서의 `Server/Api` handler 흐름을 검증하지 않는다 |
| 실제 Api server executable을 별도 process로 띄움 | role 분리가 가장 명확하다 | registry/play server까지 orchestration해야 해서 이번 보정 범위를 크게 넘는다 |
| client e2e의 HTTP app에 `create_match_handler_t`를 연결 | HTTP route가 실제 API handler와 DI 구성을 검증한다 | process 분리는 후속 샘플 orchestration 과제로 남는다 |

선택은 client e2e HTTP app에 실제 API handler를 연결하는 것이다. 이렇게 하면 HTTP client,
HTTP hosting, API handler DI 경계가 한 테스트에서 검증되고, process orchestration은 별도
샘플 실행기 개선으로 남길 수 있다.

### 적용한 리팩토링

- TicTacToe client e2e에서 임시 `sample_create_game_http_handler_t`를 제거했다.
- HTTP route는 `create_match_handler_t`를 사용하고, 그 의존성인 `create_match_room_handler_t`를
  sample app service로 등록한다.
- STREAM mock server는 `AuthenticateReq`, `JoinMatchReq`, `PlaceMarkReq` payload를 읽고 각
  요청에 맞는 reply DTO를 반환한다.
- `JoinMatchReq`와 `PlaceMarkReq`의 `match_id`를 reply/push state에 반영해 HTTP `POST /games`
  결과로 받은 match id가 stream 흐름까지 이어지게 했다.
- `CreateMatchRes`에 `play_endpoint`를 추가하고, API handler가 sample topology의
  `stream_endpoint`를 응답에 담도록 했다.
- TicTacToe client는 HTTP create-game 응답의 `play_endpoint`로 stream connector를 연결한다.
- TicTacToe client executable은 HTTP create-game 결과가 `match-1`이고 play endpoint가 loopback
  stream endpoint인지 확인한다.
- TicTacToe client e2e의 API HTTP endpoint는 zlink stream socket의 loopback port allocation을
  이용해 고정 port 충돌을 피한다.

### 수정 후 점검

- TicTacToe client e2e의 `POST /games`는 `Server/Api/Handlers/create_match_handler_t`를 통과한다.
- Stream connector request는 요청별 reply DTO와 HTTP create-match 결과의 match id를 함께
  검증한다.
- Stream connector endpoint는 샘플의 초기 옵션값이 아니라 HTTP `POST /games` 응답의
  `play_endpoint`에서 온다.
- 샘플 e2e label은 같은 workspace에서 반복 실행해도 고정 API HTTP port에 의존하지 않는다.
- client sample은 여전히 `zlink::http_client`와 stream connector를 사용하고 server handler를
  직접 호출하지 않는다.
- 이번 보정 뒤 Goal 21 샘플 handler 경계에서 남은 즉시 수정 이슈는 0개다.

## 추가 리뷰. Connector label taxonomy 공백 보정

### 발견한 위험 신호

- Implementation plan의 산출물 경계는 connector tests가 contract, protocol, transport,
  typed 흐름을 CTest label로 드러내야 한다고 적는다. 그러나 현재 `ctest -N -L` 감사에서는
  `connector-contract`, `connector-protocol`, `connector-transport`, `connector-typed`가 모두
  0개를 선택했다.
- `test_cpp_stream_connector`는 header/frame protocol, TCP endpoint transport, typed codec send와
  dispatch를 이미 검증한다. 문제는 테스트 부재가 아니라 테스트 의미가 `connector-unit`,
  `connector-integration`, `connector-e2e`에만 접혀 있어 final audit에서 문서 요구사항을 직접
  증명할 수 없다는 점이다.
- Unreal connector test도 public compile/smoke 계약을 확인하지만 `unreal-connector-contract`
  label이 없어 plan의 Unreal public API compile 계약을 라벨로 선택할 수 없었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| plan에서 세분 label 제거 | CMake 변경이 없다 | 산출물 경계의 검증 의도를 약화한다 |
| 별도 connector 테스트를 네 개로 분리 | label 의미가 가장 좁아진다 | 현재 단일 e2e가 공유하는 server setup을 중복한다 |
| 기존 connector 테스트에 세분 label 추가 | 현재 검증 범위를 유지하면서 final audit 선택자가 비지 않는다 | 하나의 테스트가 여러 의미 label을 가진다 |

선택은 기존 connector 테스트에 세분 label을 추가하는 것이다. 현재 테스트가 이미 protocol,
transport, typed 흐름을 함께 지나므로, 라벨을 추가하는 편이 중복 테스트를 만드는 것보다
복잡성을 덜 늘린다.

### 적용한 리팩토링

- `test_cpp_stream_connector` label에 `connector-contract`, `connector-protocol`,
  `connector-transport`, `connector-typed`를 추가했다.
- `test_unreal_stream_connector` label에 `unreal-connector-contract`를 추가했다.

### 수정 후 점검

- `ctest --test-dir framework/languages/cpp/build -N -L connector-contract`는
  `test_cpp_stream_connector`를 포함해야 한다. CTest label 선택은 정규식이므로
  `unreal-connector-contract`도 같은 명령에 함께 선택될 수 있다.
- `connector-protocol`, `connector-transport`, `connector-typed` label도 같은 connector
  회귀 테스트를 선택해야 한다.
- `unreal-connector-contract` label은 Unreal connector compile/smoke 테스트를 선택해야 한다.
- 이번 보정 뒤 connector label taxonomy에서 남은 즉시 수정 이슈는 0개다.

## 추가 리뷰. TicTacToe HTTP 문서 예시와 구현 계약 정렬

### 발견한 위험 신호

- HTTP hosting, HTTP client, application framework, interface draft 일부가 C++ TicTacToe
  HTTP 시작 예시를 `CreateGameHttpReq/Res`, `game_id`, `game_name`으로 설명했다.
- 현재 C++ TicTacToe sample의 shared contract와 handler는 `CreateMatchReq/Res`,
  `match_id`, `owner_actor_id`, `play_endpoint`를 사용한다. 문서와 코드가 서로 다른 DTO
  이름을 갖고 있으면 사용자는 어떤 요청을 보내야 하는지 알기 어렵다.
- 일부 설명은 API handler가 Play channel로 `CreateGameReq`를 보낸다고 단정했지만, 현재
  C++ sample의 검증된 HTTP path는 `create_match_handler_t`가 DI로 match room allocator와
  topology를 받아 `CreateMatchRes`를 반환하는 흐름이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 문서의 `CreateGameHttp*` 흐름을 유지 | `.NET` TicTacToe 일반 sample과 이름이 가깝다 | 현재 C++ sample과 테스트가 검증하는 계약을 설명하지 못한다 |
| C++ sample을 `CreateGameHttp*`로 전면 rename | `.NET` 일반 sample과 이름을 맞춘다 | SessionGateway 기반 match contract와 기존 회귀 테스트를 크게 흔든다 |
| draft 예시를 현재 C++ `CreateMatch*` 계약으로 정렬 | 구현과 테스트 증거가 같은 계약을 가리킨다 | `.NET` 일반 sample과 C++ match 용어 차이를 문서에 명시해야 한다 |

선택은 draft 예시를 현재 C++ `CreateMatch*` 계약으로 정렬하는 것이다. 이 문서들은 정식 spec이
아니라 현재 C++ framework 구현 범위를 추적하는 draft이므로, 검증 가능한 구현 계약을 우선한다.

### 적용한 리팩토링

- `cpp-http-client.ko.md`의 `/games` POST 예시를 `create_match_req_t`와
  `create_match_res_t`로 바꿨다.
- `cpp-http-hosting.ko.md`의 기준 흐름, handler shape, TicTacToe 반영 항목을
  `create_match_handler_t`, `CreateMatchReq/Res`, `match_id`, `owner_actor_id`,
  `play_endpoint` 기준으로 바꿨다.
- `cpp-application-framework.ko.md`, `cpp-framework-interfaces.ko.md`,
  `cpp-framework-policy.ko.md`의 TicTacToe HTTP 예시도 같은 계약으로 맞췄다.

### 수정 후 점검

- C++ draft 문서의 TicTacToe HTTP sample 설명은 `CreateMatchReq/Res`와
  `play_endpoint`를 중심으로 읽혀야 한다.
- 남아 있는 `create_game_http_handler_t` 이름은 HTTP hosting unit test fixture에 한정되어야
  하며, TicTacToe sample 계약 설명에는 남기지 않는다.
- 이번 보정 뒤 TicTacToe HTTP 문서 예시와 현재 구현 계약 사이의 즉시 수정 이슈는 0개다.

## 추가 리뷰. HTTP route/query/body binding 우선순위 회귀 보강

### 발견한 위험 신호

- HTTP hosting draft는 request DTO 병합 우선순위를 route parameter, query string, body
  순서로 고정한다. 현재 runtime은 body를 읽은 뒤 query, route 값을 차례로 덮어써 이 규칙을
  구현하고 있다.
- 기존 HTTP app host 테스트는 GET query binding과 route parameter가 body id를 덮는 경로를
  검증했다. 그러나 query string이 body field를 덮는 경로는 직접 검증하지 않아, runtime 병합
  순서가 바뀌어도 테스트가 놓칠 수 있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| runtime 유지, 테스트 추가 없음 | 코드 변경이 없다 | 문서의 우선순위 계약을 간접 증거에 맡긴다 |
| runtime에 별도 우선순위 helper를 추가 | 의도가 이름으로 드러난다 | 현재 병합 로직이 짧아 abstraction 이득이 작다 |
| 기존 HTTP e2e에 query-over-body 케이스 추가 | 문서 계약을 실제 public HTTP 호출로 검증한다 | 테스트 요청이 하나 늘어난다 |

선택은 기존 HTTP e2e에 query-over-body 케이스를 추가하는 것이다. runtime 구현은 이미
문서 방향과 맞고, 부족한 것은 회귀 증거다.

### 적용한 리팩토링

- `test_cpp_framework_app_host`에 `PUT /games/1?filter=query-filter` 요청을 추가했다.
- 요청 body에는 `filter=body-filter`를 넣고, 응답이 `filter=query-filter`와 route id `1`을
  반환하는지 확인한다.

### 수정 후 점검

- HTTP binding 우선순위는 body 값을 query가 덮고, route parameter가 body id를 덮는 방식으로
  public HTTP e2e에서 검증되어야 한다.
- 이번 보정 뒤 HTTP route/query/body binding 우선순위 회귀 공백은 0개다.

## 추가 리뷰. HTTP system route 충돌 validation 보강

### 발견한 위험 신호

- HTTP hosting draft는 같은 method/path 중복 등록과 system route 충돌이 startup validation에서
  실패해야 한다고 적는다.
- 현재 validation은 user route가 health/readiness/liveness path와 충돌하는 경우를 막았다.
  그러나 `map_health("/status")`와 `map_readiness("/status")`처럼 system route끼리 같은 path를
  쓰는 경우는 막지 않았다.
- system route 간 path가 같으면 request 처리 순서에 따라 health/readiness/liveness 중 어느
  의미가 반환되는지 결정된다. 이는 API 정의가 아니라 구현 순서에 의존하는 얕은 계약이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현재 동작 유지 | 코드 변경이 없다 | 같은 path에 여러 system 의미가 붙는 모호성을 허용한다 |
| request 처리에서 우선순위를 문서화 | runtime 동작은 명확해진다 | 사용자가 잘못된 route 구성을 startup에서 알 수 없다 |
| startup validation에서 system route path 중복을 거부 | 모호한 route 구성을 실행 전에 제거한다 | validation 코드가 조금 늘어난다 |

선택은 startup validation에서 system route path 중복을 거부하는 것이다. HTTP route table의
의미를 하나의 path당 하나로 유지해 호출자가 내부 match 순서를 알 필요가 없게 한다.

### 적용한 리팩토링

- `http_options_builder_t::validate()`가 health/readiness/liveness path 중복을
  `request_protocol_error`로 거부하도록 했다.
- `test_cpp_framework_app_host`에 `map_health("/status")`와 `map_readiness("/status")`가 함께
  등록될 때 startup validation이 실패하는 회귀 테스트를 추가했다.

### 수정 후 점검

- user route와 system route 충돌뿐 아니라 system route 간 path 충돌도 startup validation에서
  실패해야 한다.
- 이번 보정 뒤 HTTP system route 충돌 validation의 즉시 수정 이슈는 0개다.

## 전체 POSD 재리뷰. 현재 Goal 1-22 기록 매핑 감사

### 발견한 위험 신호

- implementation plan은 현재 22개 goal을 기준으로 각 goal마다 POSD 기반 리팩토링 기록이
  최소 하나 필요하다고 적는다.
- 이 로그는 구현 중 goal 재배치가 여러 번 있었기 때문에 과거 section 번호와 현재 plan의
  goal 번호가 항상 1:1로 읽히지 않는다. 기록 수는 충분하지만, final audit에서 현재 Goal
  1-22 각각의 대표 증거를 바로 찾기 어렵다.
- 증거 탐색이 어렵다는 것은 구현 문제가 아니라 검증 가능성 문제다. completion gate는 사람이
  의도를 추정하지 않아도 현재 plan 항목과 기록 항목을 대조할 수 있어야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 로그만 유지 | 새 문서 변경이 없다 | 현재 22개 goal 기준 증거를 매번 재검색해야 한다 |
| 과거 section 번호를 모두 현재 번호로 재작성 | 번호가 깔끔해진다 | 과거 실행 기록의 시간 순서와 commit 맥락이 흐려진다 |
| 현재 plan 기준 evidence mapping table 추가 | 기존 기록을 보존하면서 final audit 증거를 고정한다 | 표를 최신 상태로 유지해야 한다 |

선택은 evidence mapping table을 추가하는 것이다. POSD 로그는 실행 기록이므로 과거 번호를
무리하게 다시 쓰지 않고, 현재 plan 기준 대표 증거를 별도 표로 연결한다.

### 현재 Goal 1-22 대표 POSD 기록

| 현재 goal | 대표 POSD 기록 |
|-----------|----------------|
| Goal 1. Repository Skeleton And Tooling | `Goal 1. Tooling contract smoke 보강`, `Goal 1. Repository Skeleton And Build` |
| Goal 2. Binding Codec Surface Alignment | `Goal 2. Codec boundary와 sample JSON 사용 gate 보강`, `Goal 2. Binding Codec Surface Alignment` |
| Goal 3. Core Async, Task, Error Model | `Goal 3. Core Framework Types And Error Model`, `Runtime/Messaging Submit 상태 보강` |
| Goal 4. App Host, Configuration, Logging | `Goal 4. App, Host, Configuration, Logging`, `Logging 표면 보정` |
| Goal 5. DI Container And Scope Lifetime | `Goal 5. DI Container And Scope Lifetime`, `Configuration builder owner 분리` |
| Goal 6. Application Framework Parity Model | `Application Framework` 문서 예시 보정, `.NET 구조 parity 반복 보정` |
| Goal 7. Runtime Integration And Execution | `Goal 6. Runtime Integration And Dispatch Projection`, `Execution Queue와 Runtime Event Publisher 보강` |
| Goal 8. Handler Registry And Serializer | `Goal 7. Handler Registry And Serializer`, `SPOT handler registry typed dispatch 보정` |
| Goal 9. Channel Messaging | `Goal 8. Channel Messaging`, `Route public client facade 연결` |
| Goal 10. Backpressure And Reliability | `Goal 9. Backpressure, Flow Control, Reliability`, `Channel Pending Request와 Reply Dispatcher 분리` |
| Goal 11. SPOT Runtime | `Goal 10. SPOT Runtime`, `SPOT actor lifecycle handler shape 보정` |
| Goal 12. SPOT Timer | `Goal 11. SPOT Timer`, `Goal 16/19. HTTP health route와 middleware short-circuit 보강` |
| Goal 13. STREAM Framework | `Goal 12. STREAM Framework`, `Sample Session role 구현 분리` |
| Goal 14. ActorGateway Session Relay | `Goal 13. ActorGateway Session Relay`, `ActorContext JoinSpot 결과 구조 보정` |
| Goal 15. Registry And Topology | `Goal 14. Registry And Topology`, `TicTacToe sample discovery/topology 정렬` |
| Goal 16. Monitoring, Health, Observability | `Goal 16. Health readiness/liveness 표면 보강`, `Logging 표면 보정` |
| Goal 17. Module System And Hosted Services | `Goal 16. Hosted Services And Module System`, `AddZLinkFramework 대응 C++ module API 보정` |
| Goal 18. ZLink HTTP Client | `Goal 18. ZLink HTTP Client 실제 산출물 추가`, `HTTP Client contract label 보강` |
| Goal 19. HTTP Hosting | `Goal 19. HTTP Hosting runtime과 HTTP client e2e 연결`, `HTTP system route 충돌 validation 보강` |
| Goal 20. Stream Connectors | `Goal 17. C++ Stream Connector`, `Connector label taxonomy 공백 보정` |
| Goal 21. Review Samples | `Goal 19. Review Samples`, `TicTacToe HTTP 시작 handler 경계 보정` |
| Goal 22. Final Regression, Package, Extension Boundary | `Goal 22. Runtime coverage regression gate 추가`, `Goal 검증 명령 empty-selection gate 제거` |

### 수정 후 점검

- 현재 implementation plan의 Goal 1-22는 모두 이 로그 안의 대표 POSD 기록과 연결된다.
- 과거 section 번호는 실행 당시 기록으로 유지하고, 현재 plan 기준 대조는 위 표를 사용한다.
- 이번 보정 뒤 POSD 기록 매핑 감사의 즉시 수정 이슈는 0개다.

## 추가 리뷰. Public facade compile coverage와 native leakage gate 보강

### 발견한 위험 신호

- implementation plan의 public surface gate는 public header include와 runtime include 금지 규칙을
  contract/layout test가 확인해야 한다고 적는다.
- 기존 compile smoke는 contract header를 직접 include했고, top-level facade 일부는
  `zlink/framework.hpp` 같은 aggregate include를 통해 간접으로만 검증됐다. facade header는
  사용자가 직접 include하는 표면이므로 직접 compile coverage가 약하면 public header 누락을
  놓칠 수 있다.
- layout contract는 Boost/OpenSSL/gtest/spdlog 같은 외부 runtime dependency 누출을 막았지만,
  C++ binding의 concrete socket/context header와 타입 누출은 명시적으로 막지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| aggregate header compile만 유지 | 테스트가 작다 | 개별 facade include 누락을 직접 증명하지 못한다 |
| 모든 facade를 별도 테스트 파일로 분리 | 실패 위치가 좁다 | 테스트 파일 수가 늘어난다 |
| 기존 contract smoke에 facade include를 추가하고 layout coverage 범위를 include tree 전체로 확장 | 테스트 구조를 유지하면서 public include 증거를 넓힌다 | smoke 파일 include 목록이 길어진다 |

선택은 기존 contract smoke와 layout contract를 확장하는 것이다. public header 검증 책임을 한
곳에 유지하면서, 사용자가 직접 include할 수 있는 facade header까지 같은 gate에 넣는다.

### 적용한 리팩토링

- `test_cpp_framework_contract_headers`가 framework facade header, stream connector facade/header
  helper를 직접 include하도록 보강했다.
- layout contract의 compile coverage 검사를 `contracts/*` 하위가 아니라 public include tree
  전체로 넓혔다.
- public header 금지어에 binding concrete socket/context include와 타입을 추가했다.

### 수정 후 점검

- framework, connector, HTTP client public include tree의 `.hpp` 파일은 direct compile smoke
  include 목록에 있어야 한다.
- public header에는 `zlink::context_t`, concrete socket 타입, binding socket/service contract
  include가 나타나면 안 된다.
- 이번 보정 뒤 public facade compile coverage와 native leakage gate의 즉시 수정 이슈는 0개다.
