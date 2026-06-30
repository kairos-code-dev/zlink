<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Draft -- ZLink Framework C++ Implementation Plan](cpp-framework-implementation-plan.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[C++ 묶음](../README.ko.md) | [구현 계획](cpp-framework-implementation-plan.ko.md)

# Draft -- ZLink Framework C++ POSD Refactoring Log

> 이 문서는 **구현 전후 실행 기록**이다.
> 현재 공개 계약이 아니며, C++ framework 구현 goal마다 수행한 POSD 기반 리팩토링을
> 기록한다.

## 현재 기준 정정. 이전 로그 항목의 stale 경로

이 문서는 시간순 실행 기록이므로 과거 항목에는 현재 구조와 맞지 않는 파일명이 남아 있을 수 있다.
현재 구조를 확인할 때는 아래 기준을 우선한다.

- C++ framework draft/spec 추적 contract는 더 이상 `doc/draft/*.ko.md`와
  `## 6. Draft 추적표`를 기준으로 삼지 않는다. 현재 contract는
  `framework/doc/framework/cpp/spec`와 `framework/doc/framework/cpp/internals` 아래 문서를
  `cpp-framework-implementation-plan.ko.md`의 `## 6. 참고 문서 추적표`와 대조한다.
- `route_receive_pump.*`, `channel_message_pump.*`, `channel_receive_loop.*`는 이후 정리에서
  production runtime에서 제거됐다. 현재 production route/channel 수신 책임은
  `route_packet_dispatcher.*`, `route_channel_runtime.*`, `channel_runtime_bundle.*`,
  `channel_runtime_manager.*`가 맡고, 테스트에서 필요한 queued drain helper만
  `test_cpp_framework_channel_messaging.cpp` 안에 남아 있다.

## 반복 POSD 재리뷰. Final tooling configure smoke 보강

### 발견한 위험 신호

- Goal 22는 CLion/Visual Studio configure smoke를 완료 항목으로 둔다. 기존 tooling contract는
  CMake preset 이름과 vcpkg manifest를 확인했지만, 현재 host에서 CLion-style Ninja configure가
  실제로 성공하는지는 확인하지 않았다.
- IDE 설정은 preset 문법과 실제 configure 가능성이 따로 깨질 수 있다. 정적 확인만 남기면
  include path, target graph, generated compile database 문제를 놓칠 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| preset 목록 확인만 유지한다 | 빠르다 | configure smoke 완료 조건을 직접 증명하지 못한다 |
| 모든 OS/IDE preset을 현재 CI에서 configure한다 | 가장 넓다 | Linux host에서 Visual Studio generator를 실행할 수 없다 |
| 현재 host에서는 Ninja configure를 실제 실행하고, Visual Studio는 preset parse/static contract로 고정한다 | 실행 가능한 범위를 직접 검증한다 | Windows generator 실행은 별도 환경에 남는다 |

선택은 세 번째 방식이다. CLion 계열 configure는 Ninja와 `compile_commands.json`으로 현재
host에서 검증할 수 있고, Visual Studio는 generator preset이 깨지지 않도록 static contract로
남긴다.

### 적용한 리팩토링

- `test_cpp_framework_tooling_contract`에 `framework-tooling` label을 추가했다.
- tooling contract가 build tree 내부 임시 디렉터리에 Ninja configure를 실행하게 했다.
- configure 결과의 `compile_commands.json`, test/sample build option, compile command export
  cache를 확인하게 했다.
- Goal 22 검증 명령에 `framework-tooling` label을 추가했다.

### 수정 후 점검

- public API와 sample 동작은 바꾸지 않았다.
- tooling smoke는 source tree 밖 build tree에 산출물을 만들고, package/install 검증과 분리된다.
- Visual Studio preset은 `cmake --list-presets=all`과 generator string contract로 계속 고정된다.

### 재실행한 검증 명령

```bash
ctest --test-dir framework/languages/cpp/build -L framework-tooling --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-contract --output-on-failure
```

## 반복 POSD 재리뷰. Goal 22 parity e2e command gate 보강

### 발견한 위험 신호

- Goal 22는 `.NET` parity e2e regression을 완료 기준으로 둔다.
- `parity` label은 정적 sample parity와 sample e2e/process e2e 테스트를 함께 선택하지만,
  Goal 22 검증 명령에는 `ctest -L parity`가 직접 들어 있지 않았다.
- final audit 문서가 parity 축을 설명하면서 실행 명령으로는 드러내지 않으면, 검증자가 full
  CTest를 생략하고 축별 명령만 실행할 때 parity e2e 증거를 놓칠 수 있다.

### 위반한 POSD 원칙

- 정보 은닉: parity e2e 선택 기준이 CMake label에만 숨고 plan 검증 명령에는 드러나지 않았다.
- 복잡성을 아래로: 검증자가 어떤 label 조합이 `.NET` parity e2e를 증명하는지 추론해야 했다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| full CTest 명령에만 의존 | 문서가 짧다 | Goal 22의 독립 parity 축이 명령 목록에서 보이지 않는다 |
| 별도 `framework-parity-e2e` label을 추가 | 의미가 가장 좁다 | 기존 `parity` label과 sample e2e label이 중복된다 |
| Goal 22 명령에 `ctest -L parity`를 추가하고 layout contract로 고정 | 현재 label 의미를 재사용하면서 final audit 명령이 명확해진다 | 검증 명령이 한 줄 늘어난다 |

선택은 세 번째 방식이다. `parity` label은 이미 정적 parity와 sample e2e/process e2e를 함께
선택하므로, Goal 22 검증 명령이 해당 label을 직접 실행하게 하는 편이 가장 단순하다.

### 적용한 리팩토링

- Goal 22 검증 명령에 `ctest --test-dir framework/languages/cpp/build -L parity
  --output-on-failure`를 추가했다.
- layout contract가 Goal 22 검증 블록에 parity label 명령이 있는지 확인하게 했다.

### 수정 후 점검

- Goal 22를 문서대로 실행하면 `.NET` parity e2e regression 축도 독립 명령으로 확인된다.
- parity label의 실제 선택 범위는 CTest label contract와 sample e2e/process e2e label에서
  함께 검증된다.

## 반복 POSD 재리뷰. Goal 4 typed configuration binding 보강

### 발견한 위험 신호

- Goal 4와 application framework draft는 JSON/env/CLI merge와 typed options binding을 완료
  항목으로 둔다.
- 기존 configuration model은 JSON, env, CLI 값을 읽었지만 env와 CLI 값을 source별 namespace에만
  저장해 같은 key 공간에서 override되는지 검증하기 어려웠다.
- `config_builder_t`에는 draft 예시의 `bind_required<T>("server")`에 해당하는 public API가
  없어 typed options binding 완료 기준을 만족하지 못했다.

### 위반한 POSD 원칙

- 깊은 모듈: 호출자가 직접 문자열 key를 여러 번 읽고 parsing해야 해서 configuration module이
  얕게 남아 있었다.
- 복잡성을 아래로: 필수 값 누락과 typed options 조립 책임이 application code로 새어 나갔다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 문자열 model만 유지 | 변경이 작다 | typed options binding 완료 기준이 계속 미구현이다 |
| reflection-like 자동 field binding을 도입 | 호출 코드는 짧다 | C++에서 숨은 naming 규칙과 macro가 필요해 public surface가 무거워진다 |
| `T::bind(configuration_section_t)` 계약으로 typed binding을 제공 | API가 작고 parsing 지식이 option type 안에 모인다 | option type이 static bind 함수를 제공해야 한다 |

선택은 세 번째 방식이다. C++에는 `.NET` reflection 기반 binder가 없으므로, option type이 자기
section을 해석하게 하면 public API는 작게 유지하면서 필수 값 검증을 framework가 한곳에서
제공할 수 있다.

### 적용한 리팩토링

- `configuration_section_t`, `config_builder_t::bind<T>()`,
  `config_builder_t::bind_required<T>()`를 추가했다.
- `optional_t::yes/no`와 `load_json(path, optional_t)` overload를 추가해 profile JSON 파일이
  없을 때 허용할지 명시하게 했다.
- env `__` key와 CLI `--key=value`를 source별 key와 canonical key 양쪽에 저장해 JSON/env/CLI
  호출 순서대로 같은 key 공간에서 merge되게 했다.
- app host regression이 JSON 기본값, env override, CLI override, typed binding, 필수 값 누락
  실패, optional JSON 파일 허용, required JSON 파일 누락 실패를 확인하게 했다.

### 수정 후 점검

- application code는 `app.config().bind_required<T>("server")`만 호출하면 된다.
- 필수 값 누락은 `framework_exception_t(request_protocol_error)`로 한곳에서 보고된다.
- 기존 `env.*`, `cli.*` source-specific key는 유지되어 디버깅과 기존 테스트 표면을 보존한다.

## 반복 POSD 재리뷰. Goal 4 environment profile selection 보강

### 발견한 위험 신호

- application framework draft는 `development`, `production`, `test` 같은 environment/profile
  selection을 필수 configuration 기능으로 둔다.
- 기존 configuration API는 profile JSON을 직접 optional load할 수는 있었지만, 현재 app이 어떤
  environment로 실행되는지 framework 표면에서 표현하지 못했다.
- profile 이름이 application code의 임의 문자열로 흩어지면 sample과 테스트가 같은 의미로
  환경을 선택하는지 검증하기 어렵다.

### 위반한 POSD 원칙

- 정보 은닉: environment 이름과 기본값 결정이 framework가 아니라 호출자 관례로 새어 나갔다.
- 오류를 정의로 없애라: 기본 environment가 없으면 호출자가 매번 fallback을 정해야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 문서의 profile 문구를 제거 | 구현 변경이 없다 | application framework 목표를 축소한다 |
| environment를 env var parser에만 맡긴다 | 입력 source가 하나다 | 명시 profile 선택과 테스트 기본값을 표현하기 어렵다 |
| `use_environment`, `environment`, `is_environment`를 configuration API에 둔다 | 표면이 작고 profile 선택 의미가 한곳에 모인다 | profile별 파일 로딩은 호출자가 명시해야 한다 |

선택은 세 번째 방식이다. C++에서는 파일 로딩 순서를 명시하는 편이 단순하므로, framework는
environment 이름과 기본값을 제공하고 profile JSON은 기존 `load_json(..., optional_t::yes)`와
조합하게 한다.

### 적용한 리팩토링

- `config_builder_t::use_environment(std::string)`, `environment()`,
  `is_environment(std::string_view)`를 추가했다.
- environment 기본값은 `.NET Generic Host` 관례와 맞춰 `production`으로 둔다.
- app host regression이 기본 production, explicit development, 대소문자 무시 비교를 확인하게
  했다.

### 수정 후 점검

- application code는 현재 profile을 `app.config().environment()`로 읽을 수 있다.
- profile 비교는 `is_environment()`에 모여 호출자가 casing 규칙을 반복하지 않는다.
- optional profile JSON은 기존 optional loader와 조합된다.

## 반복 POSD 재리뷰. Goal 19 HTTP DTO validation hook 보강

### 발견한 위험 신호

- Goal 19와 application framework draft는 HTTP request validation과 DTO validation failure를
  필수 테스트 축으로 둔다.
- 기존 HTTP hosting은 invalid JSON과 exception mapping은 처리했지만, DTO가 역직렬화된 뒤
  handler 실행 전에 사용자 정의 validation hook을 호출하지 않았다.
- validation을 handler 내부에만 두면 handler마다 같은 `400` 매핑을 반복하거나 validation 실패를
  business failure와 섞어 처리하게 된다.

### 위반한 POSD 원칙

- 깊은 모듈: HTTP route module이 binding과 error mapping을 맡으면서 validation 단계는 호출자에
  떠넘겨져 있었다.
- 복잡성을 아래로: handler 작성자가 validation 실패의 HTTP status mapping까지 알아야 했다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| handler 내부 validation만 문서화 | 구현 변경이 없다 | 공통 validation/error mapping 완료 기준이 미구현으로 남는다 |
| 별도 validator registry를 추가 | 확장성이 크다 | 초기 표면이 커지고 route 등록과 validator 등록 순서가 생긴다 |
| DTO의 `validate(context)` 또는 `validate()` hook을 route invocation에서 호출 | 표면이 작고 DTO가 자기 규칙을 가진다 | 복잡한 validation 조합은 후속 extension이 필요하다 |

선택은 세 번째 방식이다. C++ DTO가 자기 validation 함수를 제공하면 framework는 호출 시점과
HTTP error mapping만 책임지고, annotation이나 별도 validator는 후속 extension으로 남길 수 있다.

### 적용한 리팩토링

- HTTP route invocation이 request DTO 역직렬화 뒤 `validate(context)` 또는 `validate()`가
  있으면 handler 호출 전에 실행하게 했다.
- validation hook이 `framework_exception_t(request_protocol_error)`를 던지면 기존 error mapping을
  통해 `400` 응답으로 고정된다.
- app host regression이 missing required field와 DTO validation failure를 `zlink::http_client`
  raw response로 확인하게 했다.

### 수정 후 점검

- validation 실패는 handler 실행 전에 멈춘다.
- HTTP validation 실패는 기존 middleware `after`와 correlation id 흐름을 그대로 통과한다.
- 복잡한 validator registry 없이 draft의 초기 사용자 정의 `validate()` hook 요구를 만족한다.

## 반복 POSD 재리뷰. Goal 19 HTTP unsupported content type 보강

### 발견한 위험 신호

- Goal 19와 application framework draft는 HTTP request validation 축에 unsupported content type을
  포함한다.
- 기존 HTTP host는 body가 있으면 `Content-Type`을 확인하지 않고 JSON parse를 시도했다.
- 이 상태에서는 `text/plain` 같은 요청도 JSON처럼 처리되거나 JSON parse error로만 보고되어,
  content negotiation 책임이 route handler 주변에 흩어질 수 있다.

### 위반한 POSD 원칙

- 깊은 모듈: HTTP binding module이 JSON body를 담당하면서 media type 정책을 숨기지 못했다.
- 오류를 정의로 없애라: 지원하지 않는 media type을 parse error로 늦게 발견하면 원인이 흐려진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| JSON parse 실패에 계속 의존 | 변경이 없다 | unsupported content type 완료 기준을 만족하지 못한다 |
| route handler마다 content type을 검사 | route별 자유도가 크다 | 같은 validation과 error mapping이 반복된다 |
| body가 있는 framework JSON route에서 `application/json`만 허용 | 정책이 한곳에 모이고 테스트가 단순하다 | 다른 media type은 후속 extension이 필요하다 |

선택은 세 번째 방식이다. 현재 HTTP route는 JSON DTO binding을 core 표면으로 제공하므로,
framework가 `application/json` 여부를 먼저 확인하고 지원하지 않는 media type은 공통 error
mapping으로 닫는 것이 가장 단순하다.

### 적용한 리팩토링

- HTTP host request handling에서 body가 있는 route 호출 전에 `Content-Type`을 검사한다.
- `application/json`은 parameter가 붙어도 허용하고, 그 외 media type이나 누락된 content type은
  `framework_exception_t(request_protocol_error, "unsupported content type")`로 실패한다.
- app host regression이 `text/plain` body 요청을 `zlink::http_client` raw response로 확인하게
  했다.

### 수정 후 점검

- unsupported content type은 handler 호출 전에 `400`으로 매핑된다.
- invalid JSON과 DTO validation failure는 기존 경로로 계속 분리된다.
- JSON media type 정책은 Beast request 타입을 public header에 노출하지 않고 runtime에 머문다.

## 반복 POSD 재리뷰. Sample client process e2e 분리

### 발견한 위험 신호

- Goal 21은 Bingo와 TicTacToe client executable이 실제 server process와 붙어 request/reply와
  push를 검증해야 한다고 명시한다. 기존 client e2e는 client process 안에서 loopback server
  thread를 띄워 같은 흐름을 흉내냈다.
- client가 server loop와 server log 작성까지 직접 소유하면 sample의 역할 경계가 흐려진다.
  이는 `Client`가 connector/http client 사용법만 보여야 한다는 목표와 맞지 않는다.
- CTest label에는 sample e2e가 있었지만, process topology를 직접 드러내는 label이 없어
  단일 process e2e와 multi-process e2e를 구분하기 어려웠다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 내장 server e2e를 유지한다 | 변경이 작고 빠르다 | "실제 server process" 완료 조건을 만족하지 못한다 |
| role server 전체를 한 번에 orchestration한다 | 운영 형태와 가장 가깝다 | registry/discovery/readiness까지 한 번에 묶여 변경 범위가 크다 |
| 기존 e2e server loop를 독립 server executable로 분리하고 client가 외부 process에 붙게 한다 | 완료 조건을 직접 검증하면서 변경 범위를 줄인다 | role server 전체 orchestration은 별도 단계로 남는다 |

선택은 세 번째 방식이다. 먼저 process 경계를 테스트로 고정해야 이후 role server orchestration을
넓힐 때도 client가 server 구현을 다시 품는 퇴행을 막을 수 있다.

### 적용한 리팩토링

- Bingo/TicTacToe client에서 내장 server 분기를 제거했다.
- 샘플 실행 확인은 현재 유지되는 public role executable의 smoke와 sample-local runner로
  한정했다.
- 별도 process 사이의 full client/server 검증은 현재 C++ sample channel request 구조가
  지원하지 않으므로 성공한 실행처럼 표시하지 않는다.

### 수정 후 점검

- client standalone smoke와 role executable smoke를 유지한다.
- sample-local runner는 full client/server 검증을 가장하지 않고 현재 가능한 smoke 범위를
  출력한다.
- 문서의 검증 명령과 label taxonomy를 현재 유지되는 sample smoke label과 맞췄다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target sample_cpp_framework_bingo_client sample_cpp_framework_tictactoe_client
ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke --output-on-failure
./framework/languages/cpp/samples/run_samples.sh
```

## 반복 POSD 재리뷰. Stream Connector compression opt-in 회귀 보강

### 발견한 위험 신호

- Goal 20은 LZ4 build feature는 기본 ON이고, packet 압축은 opt-in이라고 명시한다. 기존
  connector 테스트는 `.compress()`를 호출한 packet이 압축되는지는 확인했지만, 기본 send가
  압축되지 않는지는 직접 확인하지 않았다.
- 기본값 검증 없이 압축 성공만 보면, 나중에 connector 기본 option이 `lz4`로 바뀌거나 send
  경로가 모든 packet을 압축해도 테스트가 완료 조건 위반을 놓칠 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| LZ4 codec round-trip만 유지한다 | 테스트가 작다 | opt-in 의미를 증명하지 못한다 |
| public option에서 compression setter를 제거한다 | 오용 표면이 줄어든다 | 사용자가 packet 단위 압축을 선택할 수 없어진다 |
| connector e2e에서 기본 option과 무압축 send frame을 함께 확인한다 | 문서 완료 조건을 wire 수준에서 고정한다 | server loop가 한 frame 더 처리해야 한다 |

선택은 세 번째 방식이다. 압축 여부는 header flag와 payload wire 형태로 드러나는 동작이므로,
테스트도 connector server가 받은 frame에서 직접 확인해야 한다.

### 적용한 리팩토링

- `connector_options_t` 기본 compression이 `none`인지 확인했다.
- `.compress()`를 호출한 send는 compressed flag와 복원된 payload로 검증하고, 별도의 기본
  send는 compressed flag가 없는 frame으로 검증했다.
- 같은 TCP connector e2e 안에서 압축 opt-in과 무압축 기본 경로를 함께 확인하게 했다.

### 수정 후 점검

- public connector API는 바꾸지 않았다.
- LZ4 build feature 기본 ON 검증은 유지하고, packet 단위 opt-in 의미를 wire 회귀로 보강했다.
- compression 설정 지식은 caller가 아닌 connector option과 send call 경계에 남는다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_cpp_stream_connector --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-protocol --output-on-failure
```

## 반복 POSD 재리뷰. TicTacToe HTTP sample parity 회귀 보강

### 발견한 위험 신호

- Goal 19는 TicTacToe sample이 HTTP `POST /games`로 시작해야 한다고 명시한다. 샘플 구현과
  e2e log 검증은 해당 흐름을 사용하지만, sample parity 테스트는 API role이 play endpoint에
  직접 연결하지 않는다는 우회 조건만 확인하고 있었다.
- `POST /games` route와 `zlink::http_client` 호출을 직접 고정하지 않으면, 샘플이 다시
  channel-only 시작 흐름으로 돌아가도 parity 테스트가 놓칠 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| e2e log 검증만 신뢰한다 | 실행 흐름을 본다 | route 등록과 client API 선택을 정확히 지목하지 못한다 |
| 샘플 구현을 더 추상화한다 | 중복 문자열을 줄일 수 있다 | 이미 동작하는 샘플에 불필요한 구조 변경이 생긴다 |
| sample parity 테스트가 route와 client 호출 표면을 직접 검사한다 | 문서 완료 조건을 좁고 빠르게 고정한다 | 파일 문자열 계약을 추가로 유지해야 한다 |

선택은 세 번째 방식이다. 샘플 시작 흐름은 public sample contract에 가까우므로, route와 client
표면을 계약 테스트에서 직접 확인하는 것이 더 명확하다.

### 적용한 리팩토링

- TicTacToe API role이 `topology.api_http_endpoint`를 listen하고
  `map_post<create_game_http_handler_t>("/games")`를 등록하는지 고정했다.
- TicTacToe client가 `zlink::http_client`를 include하고, API HTTP endpoint를 base URL로 둔 뒤
  `POST /games`를 `create_game_http_res_t`로 받는지 고정했다.
- client 결과의 `http_game_created`가 HTTP readiness 결과로 채워지는지도 검증했다.

### 수정 후 점검

- sample 구현과 public API는 바꾸지 않았다.
- HTTP 시작 흐름은 e2e log와 sample parity 테스트 양쪽에서 확인된다.
- API role이 play endpoint에 직접 연결하지 않는 기존 검증은 유지했다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_sample_parity
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_sample_parity --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-sample-e2e --output-on-failure
```

## 반복 POSD 재리뷰. HTTP hosting TLS startup validation 회귀 보강

### 발견한 위험 신호

- Goal 19는 `https://` endpoint가 TLS certificate와 private key를 모두 요구한다고 명시한다.
  구현은 두 값을 모두 검사하지만, 회귀 테스트는 TLS 설정이 전혀 없는 경우만 고정하고 있었다.
- certificate만 누락되거나 private key만 누락되는 부분 설정은 사용자가 실제로 만들 수 있는
  오류다. 이 케이스가 테스트에 없으면 완료 증거가 구현 세부에 의존하게 된다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 테스트를 유지한다 | 변경이 없다 | 문서의 "둘 다 필요" 조건을 테스트가 직접 증명하지 못한다 |
| TLS builder setter에서 즉시 검증한다 | 오류 위치가 빠르다 | fluent builder 작성 순서 중간 상태까지 오류로 만들 수 있다 |
| startup validation 테스트에 부분 TLS 설정 케이스를 추가한다 | 문서의 완료 조건을 그대로 고정한다 | 테스트 케이스가 몇 개 늘어난다 |

선택은 세 번째 방식이다. `listen(...).configure_tls(...)`는 fluent builder이므로 중간 상태를
허용하고, framework 설정 완료 시점의 startup validation에서 certificate/private key 쌍을 검증하는 것이
호출자 관점에서 단순하다.

### 적용한 리팩토링

- `test_cpp_framework_app_host`에 HTTPS TLS validation helper를 추가했다.
- TLS 설정 없음, certificate 누락, private key 누락, 빈 TLS callback을 모두 startup validation
  실패로 검증했다.
- `http://` endpoint에 TLS option이 붙는 경우는 실패시키지 않아 scheme별 검증 범위를 분리했다.

### 수정 후 점검

- public HTTP API는 바꾸지 않았다.
- HTTPS endpoint의 필수 TLS 쌍 검증은 문서 문구와 테스트가 같은 단위로 맞는다.
- OpenSSL runtime availability와 관계없이 startup validation 의미를 확인한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_app_host
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_app_host --output-on-failure
ctest --test-dir framework/languages/cpp/build -L framework-http --output-on-failure
```

## 반복 POSD 재리뷰. Stream Connector WebSocket TLS transport 추가

### 발견한 위험 신호

- TCP, TLS, WebSocket은 구현됐지만 공통 초안과 `.NET` connector의 최종 transport 범위에는
  WebSocket over TLS도 포함된다. WSS만 명시 실패로 남기면 Goal 20의 transport parity가
  완료되지 않는다.
- WebSocket over TLS를 별도 호출 경로로 처리하면 TLS handshake와 WebSocket binary message
  경계가 connector lifecycle, frame codec, request read에 섞인다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| WSS를 계속 unsupported로 둔다 | 변경이 작다 | 공통 transport 완료 기준이 닫히지 않는다 |
| TLS connection 위에 별도 WebSocket 호출 경로를 둔다 | 구현 위치가 명시적이다 | packet 호출자가 secure WebSocket 세부를 알게 된다 |
| Beast `websocket::stream<ssl::stream<tcp::socket>>`를 `stream_connection_t` 구현체로 둔다 | 기존 frame read/write 경로를 그대로 쓴다 | Beast SSL teardown include와 buffering 처리가 필요하다 |

선택은 세 번째 방식이다. WSS도 connector 입장에서는 byte frame을 읽고 쓰는 connection이다.
TLS handshake와 WebSocket binary message는 transport owner가 흡수한다.

### 적용한 리팩토링

- `wss://host:port/path` endpoint parser를 추가했다.
- OpenSSL이 있는 빌드에서 `connect_websocket_secure(...)`를 추가하고, TLS handshake 뒤
  WebSocket handshake를 수행하게 했다.
- WebSocket connection 구현을 template로 정리해 plain TCP WebSocket과 TLS WebSocket이 같은
  buffering/read/write/close 구현을 공유하게 했다.
- connector lifecycle과 transport factory가 `transport_t::websocket_secure`를 OpenSSL 빌드에서
  지원하게 했다.
- connector e2e 테스트에 self-signed WSS loopback server를 추가해 binary WebSocket frame에
  STREAM connector frame이 실리는지 검증했다.

### 수정 후 점검

- public Stream Connector API는 바꾸지 않았다.
- TCP, TLS, WebSocket, WebSocket over TLS가 모두 같은 send/request frame 경로를 사용한다.
- OpenSSL 없는 빌드에서는 secure transport가 명시 실패로 남는다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_cpp_stream_connector --output-on-failure
```

## 반복 POSD 재리뷰. Stream Connector TLS transport 추가

### 발견한 위험 신호

- `.NET` connector는 `tls://` endpoint를 TLS over TCP로 연결하고 certificate validation
  정책을 option으로 제공한다. C++ connector에는 같은 public enum과
  `skip_server_certificate_validation` option이 있었지만 runtime은 TLS를 unsupported로
  처리했다.
- TLS를 request/write 경로에 직접 넣으면 handshake, certificate verification, SNI 설정이
  connector lifecycle과 frame codec에 흩어진다. 이는 transport 보안 세부가 packet 호출
  경로로 새는 정보 은닉 위반이다.
- OpenSSL이 없는 빌드에서도 public header가 OpenSSL 타입을 노출하면 dependency isolation
  원칙이 깨진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| TLS를 계속 unsupported로 둔다 | 변경이 없다 | `.NET` parity와 공통 transport 완료 기준을 만족하지 못한다 |
| connector lifecycle에서 SSL stream을 직접 소유한다 | 구현 파일 수가 적다 | read/write/close 경로가 TLS 세부를 알게 된다 |
| OpenSSL이 있는 빌드에서만 `stream_connection_t` TLS 구현체를 추가한다 | public API를 바꾸지 않고 transport 세부를 숨긴다 | OpenSSL 없는 빌드는 TLS를 명시 실패로 유지한다 |

선택은 세 번째 방식이다. TLS는 private runtime dependency이며, public option은 endpoint와
certificate validation 정책만 표현한다.

### 적용한 리팩토링

- `tls://host:port` endpoint parser를 추가했다.
- OpenSSL이 있는 빌드에서 `tls_stream_connection_t`를 `stream_connection_t` 구현체로 추가했다.
- TLS client handshake, SNI, hostname verification, `skip_server_certificate_validation`
  처리를 transport owner에 모았다.
- CMake가 OpenSSL을 찾으면 `zlink_stream_connector`에 private OpenSSL dependency와
  `ZLINK_STREAM_CONNECTOR_WITH_OPENSSL` compile definition을 붙이게 했다.
- connector e2e 테스트에 self-signed TLS loopback server를 추가하고, 테스트 인증서 검증
  생략 option으로 `tls://localhost:<port>` 전송을 검증했다.

### 수정 후 점검

- public Stream Connector header는 OpenSSL 타입을 노출하지 않는다.
- TCP, TLS, WebSocket은 같은 send/request frame 경로를 사용한다.
- WebSocket over TLS도 같은 `stream_connection_t` abstraction 아래에서 구현됐다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_cpp_stream_connector --output-on-failure
```

## 반복 POSD 재리뷰. Stream Connector WebSocket transport 추가

### 발견한 위험 신호

- `.NET` connector와 공통 초안은 WebSocket binary transport를 지원하지만, C++ connector는
  `transport_t::websocket`을 public enum에 두고도 connect 단계에서 실패시켰다. 이 상태는
  public surface와 실제 동작이 맞지 않는 오구현이다.
- WebSocket을 호출 경로에 직접 분기로 넣으면 request read, push drain, heartbeat, close가
  WebSocket frame 세부를 알게 된다. 앞서 분리한 `stream_connection_t` 경계를 실제 transport
  구현으로 검증해야 했다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| WebSocket을 계속 unsupported로 둔다 | 변경이 작다 | `.NET` parity와 공통 초안 완료 기준을 만족하지 못한다 |
| request/write 경로에서 WebSocket 분기 처리 | 빠르게 frame을 보낼 수 있다 | transport 세부가 호출 경로로 퍼진다 |
| `stream_connection_t` 구현체로 WebSocket connection 추가 | 기존 packet frame 호출자는 그대로 둔다 | WebSocket buffering 구현이 필요하다 |

선택은 세 번째 방식이다. connector의 상위 호출자는 byte frame read/write만 알아야 하며,
WebSocket binary message 경계는 transport 구현이 흡수한다.

### 적용한 리팩토링

- `websocket_connection.cpp`를 placeholder에서 Boost.Beast 기반 WebSocket binary connection
  구현으로 바꿨다.
- `ws://host:port/path` endpoint parser와 WebSocket connect factory를 추가했다.
- WebSocket binary message를 내부 byte buffer로 풀어 기존 frame decoder가 그대로 읽게 했다.
- `transport_t::websocket`을 supported transport로 전환하고, connector lifecycle이
  WebSocket factory를 호출하게 했다.
- connector e2e 테스트에 Boost.Beast loopback server를 추가해 WebSocket binary frame으로
  STREAM connector frame이 도착하는지 검증했다.

### 수정 후 점검

- public Stream Connector API는 바꾸지 않았다.
- TCP와 WebSocket은 같은 `send(packet)` / typed send 호출 표면을 사용한다.
- WebSocket over TLS도 같은 `stream_connection_t` abstraction 아래에서 구현됐다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_cpp_stream_connector --output-on-failure
```

## 반복 POSD 재리뷰. Stream Connector transport 경계 분리

### 발견한 위험 신호

- 공통 Stream Connector 초안과 `.NET` connector는 TCP, TLS, WebSocket, WebSocket over TLS를
  같은 packet API로 지원한다. C++ connector는 public enum에는 네 transport가 있지만 runtime은
  `tcp::socket`을 state에 직접 들고 있어 transport를 추가할 때 request, dispatch, heartbeat,
  close 경로마다 분기가 번질 수 있었다.
- `connector_runtime.hpp`가 구체 socket 타입을 소유하면 public contract에는 새지 않더라도
  runtime 내부 호출자가 TCP connection 세부를 계속 알아야 한다. 이는 transport 결정이 여러
  모듈로 새는 정보 은닉 위반이다.
- unsupported transport를 명시 실패로 고정한 회귀는 있었지만, Goal 20 계획 문서는
  TCP/TLS/WebSocket/WebSocket over TLS parity 범위를 직접 나열하지 않아 남은 미구현 범위를
  완료 기준이 드러내지 못했다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 각 호출 경로에서 transport별 분기 추가 | 빠르게 한 transport를 붙일 수 있다 | heartbeat, request read, push drain, close 의미가 반복된다 |
| `connector_state_t`에 TCP/TLS/WS 멤버를 모두 둔다 | type별 상태를 직접 볼 수 있다 | 상태 layout이 transport matrix를 노출하고 얕아진다 |
| transport별 connection을 `stream_connection_t` runtime interface 뒤에 둔다 | 호출자는 read/write/close 의미만 보고 transport 세부가 숨겨진다 | transport 구현 class가 추가된다 |

선택은 세 번째 방식이다. connector 호출 경로는 packet frame을 읽고 쓰는 의미만 필요하다.
TLS handshake, WebSocket binary message, TCP socket details는 transport owner가 흡수해야
한다.

### 적용한 리팩토링

- `transport/transport_connection.hpp`를 추가해 runtime 전용 `stream_connection_t` interface를
  만들었다.
- `connector_state_t`가 구체 `tcp::socket` 대신 `std::unique_ptr<stream_connection_t>`를
  소유하게 바꿨다.
- 기존 TCP socket read/write/available/shutdown 구현은 `tcp_stream_connection_t`로 숨겼다.
- request read, push drain, heartbeat timeout close, connector close 경로가
  `state.connection` helper만 사용하게 정리했다.
- Goal 20 계획 문서에 TCP, TLS, WebSocket, WebSocket over TLS와 unsupported transport
  validation을 완료 범위로 명시했다.
- C++ Stream Connector 초안에 당시 TCP-only 상태와 최종 parity 전까지 추가 transport
  connection 구현이 남아 있음을 분명히 적었다.

### 수정 후 점검

- public Stream Connector API는 바꾸지 않았다.
- TCP 동작은 기존 connector e2e 테스트로 유지된다.
- WebSocket over TLS parity도 같은 abstraction 아래에서 닫혔다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_cpp_stream_connector --output-on-failure
```

## 반복 POSD 재리뷰. Stream Connector heartbeat 의미 정렬

### 발견한 위험 신호

- 공통 Stream Connector 초안은 heartbeat ping/pong과 timeout을 완료 기준으로 둔다. C++
  connector에는 `heartbeat.timeout` option이 있었지만, runtime은 ping 전송만 하고 timeout
  값을 사용하지 않았다.
- `$zlink.heartbeat.pong` 같은 control frame을 application callback으로 전달하지 않는다는
  공통 규칙을 C++ runtime 경계에서 직접 막지 않았다. 사용자가 `$zlink.` 예약 이름 handler를
  등록하면 내부 control frame이 application packet처럼 보일 수 있었다.
- heartbeat 판단을 별도 background thread로 옮기면 manual dispatch 모델과 충돌한다. C++
  초안은 game/client loop가 `dispatch()`를 호출하는 모델을 전제로 하므로, heartbeat도 같은
  경로에서 처리해야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| ping 전송만 유지 | 구현이 가장 작다 | public timeout option과 공통 완료 기준이 맞지 않는다 |
| heartbeat 전용 background thread 추가 | 자동으로 timeout을 감지할 수 있다 | manual dispatch/game loop 모델과 thread ownership이 복잡해진다 |
| `dispatch()`에서 inbound drain, timeout 판정, ping 전송을 처리 | 기존 dispatch 모델과 맞고 thread를 늘리지 않는다 | 사용자가 dispatch를 주기적으로 호출해야 한다 |

선택은 세 번째 방식이다. connector는 사용자가 정한 loop에서 callback을 실행하는 것이 기본
정책이므로, heartbeat도 같은 loop의 `dispatch()` 호출에서 처리한다.

### 적용한 리팩토링

- connector state에 마지막 inbound frame 시각을 추가하고, connect 성공 시 기준 시각을
  초기화했다.
- request read와 dispatch push read 경로가 frame을 읽으면 마지막 inbound 시각을 갱신하게
  했다.
- `heartbeat_monitor_t`가 timeout 판단도 소유하게 해 heartbeat option 의미를 한 runtime
  모듈로 모았다.
- `dispatch()`가 도착한 frame을 먼저 비운 뒤 heartbeat timeout을 판정하고, timeout이면
  socket을 닫고 `disconnected` 상태와 오류를 발행하게 했다.
- `$zlink.` 예약 control packet은 application dispatch queue나 immediate callback으로
  전달하지 않게 했다.
- `test_cpp_stream_connector`에 heartbeat pong control frame 필터와 heartbeat timeout
  회귀를 추가했다.

### 수정 후 점검

- heartbeat 처리는 별도 thread를 만들지 않는다.
- server pong이 이미 도착한 경우 먼저 drain해서 마지막 inbound 시각을 갱신하므로 잘못된
  timeout으로 끊지 않는다.
- application callback은 `$zlink.` 내부 control packet을 받지 않는다.
- timeout error code는 현재 public enum을 늘리지 않고 `disconnected`를 사용한다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_cpp_stream_connector --output-on-failure
```

## 반복 POSD 재리뷰. Stream Connector 경계 조건 회귀 고정

### 발견한 위험 신호

- Stream Connector 초안은 지원하지 않는 secure transport를 조용히 TCP처럼 처리하지
  않는다고 설명하지만, connector 회귀 테스트는 이 실패 정책을 직접 검증하지 않았다.
- `max_send_payload_size`와 `max_metadata_size`는 public option으로 제공되지만, send 전에
  차단된다는 회귀 테스트가 없었다. 이 상태에서는 frame encoder나 call object 변경 중 제한
  검사가 transport write 뒤로 밀려도 테스트가 놓칠 수 있다.
- reconnect 실패 뒤 새 request가 queue에 쌓이지 않고 disconnected 오류로 실패한다는
  테스트 항목도 명시적인 회귀가 약했다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 구현 코드만 유지 | 변경이 없다 | public option의 실패 의미를 테스트가 증명하지 못한다 |
| 별도 executable로 경계 테스트 분리 | label을 더 세밀하게 나눌 수 있다 | connector runtime setup이 중복된다 |
| 기존 connector e2e 테스트에 경계 조건 추가 | 실제 연결 상태에서 send/request 경계를 검증한다 | 단일 테스트 본문이 조금 길어진다 |

선택은 기존 connector 회귀에 추가하는 방식이다. 같은 connector instance에서 성공 경로와
실패 경계를 함께 검증해야, 제한 검사가 실제 transport write 전에 실행되는지 확인할 수
있다.

### 적용한 리팩토링

- `test_cpp_stream_connector`에 raw `packet_t` send payload가 `max_send_payload_size`를
  넘으면 `frame_too_large`로 실패하는 검사를 추가했다.
- metadata 합산 크기가 `max_metadata_size`를 넘으면 `validation_failed`로 실패하는 검사를
  추가했다.
- `transport_t::tls` connect가 `configuration_error`로 실패하고 연결 상태로 전환되지
  않는지 검증했다.
- reconnect 시도 실패 뒤 새 request가 pending queue에 쌓이지 않고 `disconnected`로 실패하는
  회귀를 추가했다.

### 수정 후 점검

- Stream Connector public API는 바꾸지 않았다.
- 경계 조건은 DTO fallback payload가 아니라 raw `packet_t` overload를 통해 검증한다. 이
  방식이 payload 크기 제한의 실제 입력을 가장 직접적으로 드러낸다.
- unsupported transport 정책은 문서처럼 명시 실패로 고정된다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_stream_connector
ctest --test-dir framework/languages/cpp/build -R test_cpp_stream_connector --output-on-failure
```

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
ctest --test-dir framework/languages/cpp/build -R 'sample_smoke_sample_cpp_framework_(bingo|tictactoe)|test_cpp_framework_sample_parity' --output-on-failure
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
- `framework/src/runtime`, `connector/core/src/runtime`, Unreal `Private/` 경계를 물리적으로
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
- C++ binding codec target을 framework build graph에 두지 않는다. JSON은 framework 기본
  header로 제공하고, Protobuf/MessagePack은 framework codec extension target으로 분리한다.
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
  `advanced().zlink`, `run`, `stop` 구현을
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
- scoped service는 root provider에서 직접 resolve할 수 없고, framework가 소유하는 scope 안에서만
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
  server 역할 ingress에서 handler registry로 dispatch되는 경로가 없었다.
- outbound request는 timeout result를 만들었지만 pending request table과 reply correlation
  경계가 없어 `ROUTER -> DEALER` 임의 push와 pending reply completion을 구분하기 어려웠다.
- `enable_server`, `enable_client`, `enable_publisher`, `enable_subscriber`가 같은 역할
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
- server/client/publisher/subscriber 역할에 `bind`, `connect`, `use_discovery`를
  추가하고, 같은 역할 안에서 manual endpoint와 discovery를 섞으면
  `request_protocol_error`로 실패하게 했다.
- `message_bus_t` outbound send/publish/request는 channel name 기준으로만 호출한다.
- missing client/publisher 역할은 `disconnected`로 실패한다.
- pending queue 한도 초과는 `request_rejected`로 실패한다.
- `channel_runtime_t` private runtime을 추가해 local server 역할 ingress에서
  `handler_registry_t::invoke(...)`로 request/send를 dispatch하게 했다.
- outbound pending request table과 request sequence를 runtime state에 두고, 등록되지 않은
  reply completion은 `request_protocol_error`로 실패하게 했다.
- `drain()`은 pending request table과 pending count를 정리한다.
- 역할 enable 중복 코드는 `channel_builder_t::enable_capability(...)`로 모았다.
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
  `add_post_actor_joined`, `add_actor_left`, `onDisconnectActor` 등록 표면을 추가했다.
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
- `add_post_actor_joined`, `add_actor_left`, `onDisconnectActor` 등록 API를
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
- Entry Spot actor packet은 대상 actor mailbox에서 처리한다. Entry Spot timer 실행 줄
  정합성은 actor packet dispatch 계약과 분리해서 다룬다. 실제 Entry Spot executor 배선은
  actor/stream relay goal에서 확장한다.

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
  session relay 이름 보관, header encode/decode, semantic validation, reserved prefix,
  session callback ordering, packet reply, invalid packet drop, transport error projection을
  검증했다.

### 남은 tradeoff

- 실제 binding `stream_socket_t` I/O, write-ready backpressure, request tracker storage는
  runtime owner 안에서 더 붙여야 한다. Goal 12에서는 public STREAM contract와 frame/session
  runtime 경계를 먼저 닫았다.
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
- `test_cpp_framework_ActorGateway_actor_session_relay`에서 session relay 구성, actor
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

## 이전 계획 Goal 17. C++ Stream Connector

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | `.NET` `Systems.Zlink.Stream.Connector/Contracts/*`와 `Runtime/*`의 connector/call/options/model과 receive loop/pending request/frame sender 분리를 기준으로 삼았다. |
| contract owner | `connector/core/include/zlink/stream_connector/contracts/connector.hpp`가 connector options, state/error enum, metadata, packet, result/task, call object, codec registry, connector facade를 소유한다. |
| runtime owner | `connector/core/src/runtime/connector_runtime.*`가 connection state storage, dispatch queue, pending request table, sent frame log, packet handler table, state/error callbacks를 소유한다. |
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

- `connector/core/include/zlink/stream_connector/contracts/connector.hpp`와 umbrella
  `zlink/stream_connector.hpp`를 추가했다.
- `connector_t`, `connector_factory_t`, `connector_options_t`, `codec_registry_t`,
  `send_call_t`, `request_call_t<T>`, connector `result_t<T>`, `task_t<T>`를 public
  contract로 정의했다.
- transport, codec, compression, dispatch mode, message kind, header flags, error code,
  connection state enum과 metadata/packet/state changed model을 추가했다.
- `zlink_stream_connector`를 `INTERFACE`에서 별도 static library로 바꾸고
  `connector/core/src/runtime/connector_runtime.cpp`를 private runtime 구현으로 연결했다.
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

## 이전 계획 Goal 18. Unreal Stream Connector

### Interface Separation Review

| 확인 항목 | 결정 |
|-----------|------|
| `.NET` 대응 확인 | 일반 C++ connector contract/runtime 분리와 Unreal plugin public/private 분리를 함께 기준으로 삼았다. Unreal public API는 일반 connector type을 노출하지 않고 Unreal 타입과 thread model 표면만 제공한다. |
| contract owner | `connector/engines/unreal/Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h`가 `UZLinkStreamConnector`, Unreal-facing state enum, packet value type, Blueprint-callable method를 소유한다. |
| runtime owner | `connector/engines/unreal/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp`가 기본 C++ connector 호출, Game Thread dispatch forwarding, lifecycle shutdown mapping을 소유한다. |
| public dependency | Unreal public header는 일반 C++ connector runtime header를 include하지 않는다. Unreal 엔진이 없는 CTest compile에서는 shim 타입으로 public API shape만 검증한다. |
| native leakage | public API는 `FString`, `FName`, `TArray<uint8>`, `TMap<FString,FString>` 기반 표면만 노출하고 receive loop, pending request table, frame sender, thread queue를 노출하지 않는다. |
| detail 사용 | 일반 C++ connector의 public header만 Unreal private 구현에서 include한다. `connector/core/src/runtime/*` private header와 자체 STREAM frame 구현은 Unreal adapter에 두지 않는다. |
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
| Unreal 전용 facade를 제공하고 Private에서 기본 connector 호출 | Unreal 사용성에 맞고 core 구현 복제를 피한다 | public/private adapter 코드가 필요하다 |
| private runtime raw pointer 소유 | 구현이 짧다 | ownership, destructor, shutdown 책임이 명확하지 않다 |
| private runtime `unique_ptr` 소유 | RAII로 lifetime이 닫힌다 | public header에 `<memory>`와 전방 선언이 필요하다 |
| immediate callback 실행 | callback path가 단순하다 | Game Thread 실행 보장이 약해진다 |
| `Dispatch()`/`Tick()` manual dispatch 표면 제공 | Game Thread에서 명시적으로 callback을 처리할 수 있다 | 사용자가 frame loop에서 dispatch를 호출해야 한다 |

현재 기준의 선택은 Unreal 전용 public facade와 `Private/` adapter다. public header는 Unreal
타입과 Blueprint-callable method만 제공하고, 기본 connector 호출과 Game Thread dispatch 구현은
Private에 숨긴다. STREAM frame, reconnect, heartbeat, pending request correlation은 기본
connector core에 둔다.

### 적용한 리팩토링

- `ZLinkStreamConnector.uplugin` 골격을 유지하고 Unreal module `ZLinkStreamConnector.Build.cs`
  를 추가했다.
- `Public/ZLinkStreamConnector.h`에 `UZLinkStreamConnector`, `EZLinkStreamConnectionState`,
  `FZLinkStreamPacket`을 정의했다.
- Unreal 엔진이 없는 CTest 환경에서도 compile smoke가 가능하도록 public header에 최소 shim
  타입과 macro fallback을 두었다.
- `Connect`, `Close`, `SendJson`, `RequestJson`, `Dispatch`, `Tick`, `ShutdownForPie`,
  `ShutdownForMapUnload`, `ShutdownForGameInstanceShutdown` 표면을 추가했다.
- `Private/ZLinkStreamConnector.cpp`는 기본 connector public API를 호출하는 adapter가 되어야
  한다. 과거 Unreal `Sockets` 기반 runtime 구현은 현재 기준에서 제거 대상이다.
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
ctest --test-dir framework/languages/cpp/build -L connector-unreal-compile --output-on-failure
ctest --test-dir framework/languages/cpp/build -L connector-unreal-smoke --output-on-failure
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
  `Server/Play`, `Server/Session` 역할로 나누고 STREAM endpoint, session relay,
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
| CTest wrapper가 server/client 샘플 프로세스를 실행한 뒤 로그 파일을 검증 | 샘플 코드는 사용자 흐름으로 유지하고 회귀 검증을 분리한다 | process orchestration helper가 필요하다 |
| reply와 push를 별도 public 동작으로 바꿈 | 연속 submit 문제가 줄어든다 | `.NET` 샘플의 reply 후 bound-session push 논리 흐름과 달라질 수 있다 |
| 같은 STREAM write에 reply frame과 push frame을 순서대로 담음 | `.NET`과 같은 reply 후 push frame 순서를 유지하면서 write readiness 문제를 숨기지 않는다 | loopback server helper가 frame batching을 알아야 한다 |

선택은 CTest wrapper 방식이다. Client 샘플 executable은 계속 public API 사용성만 보여 주고,
회귀 테스트는 별도 server process와 client process를 함께 실행한 뒤 서버 로그를 읽어 실제 bind,
receive, reply, push 흐름과 packet 이름, 최소 처리 횟수를 검증한다.

연속 submit 실패는 frame batching으로 정리했다. `.NET` 샘플처럼 request reply와
bound-session push는 논리적으로 분리되어 있고, 로그도 `reply` 다음 `push`를 그대로 남긴다.
다만 C++ loopback server는 같은 STREAM connection에 두 frame을 순서대로 쓰는 transport
세부를 한 번의 write로 처리한다. 이는 public framework/connector API 차이가 아니라
STREAM wire가 여러 frame을 한 byte stream에 실을 수 있다는 구현 세부다.

#### 적용한 리팩토링

- sample-local runner가 현재 유지되는 public role executable의 smoke를 실행하게 했다.
- C++ sample channel request는 별도 process 사이의 full client/server 검증을 완료하지
  않으므로, runner는 그 범위를 명시적으로 출력한다.
- `framework-sample-smoke` CTest label로 role executable smoke를 확인한다.
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
ctest --test-dir framework/languages/cpp/build -R sample_process_e2e_sample_cpp_framework_bingo_client --output-on-failure --repeat until-fail:10
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

- `sample_process_e2e_sample_cpp_framework_bingo_client`에 `connector-e2e` 라벨을 추가했다.
- `sample_process_e2e_sample_cpp_framework_tictactoe_client`에 `connector-e2e` 라벨을 추가했다.
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
  `Systems.Zlink.Stream.Connector.Tests`처럼 테스트 프로젝트
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
  `AddRegistryEndpoint(topology.RegistryRouterEndpoint)`를 사용한다.
  C++ TicTacToe sample은 registry endpoint가 topology에 없고, Api host가 Play channel client를
  직접 endpoint로 연결했다. 이 상태에서는 C++ sample이 discovery 기반 channel 구성이라는
  `.NET` 샘플의 핵심 흐름을 보여 주지 못한다.
- `.NET` TicTacToe API sample은 host factory가 framework 설정을 직접 보여 준다. C++도 같은
  독자 경험을 유지해야 하므로 TicTacToe API 설정을 helper header로 다시 분리하지 않는다.

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
- TicTacToe API 설정은 `.NET`처럼 `api_server_host_factory.hpp` 안에서 직접 보이게 유지했다.
- TicTacToe Api/Play/Session host factory가 `options.use_discovery().add_registry_endpoint (...)`를 사용하게 했다.
- TicTacToe Api/Session client channels는 `.NET`처럼 discovery 기반 client 표면으로 보이게
  직접 play endpoint 연결을 제거했다.
- sample parity test가 TicTacToe host factory의 discovery 사용과 registry topology 사용을
  검증하게 했다.

### 후속 보정

이후 반복 리뷰에서 `options.add_route_mesh(...)`,
`options.use_registry_spot_remote_addresses(...)`, `options.add_spot_mesh(...)`를 추가해
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

- `.NET` framework options에는 registry-backed Spot remote address 설정,
  route mesh 등록, Spot mesh 등록이 있어 host factory가 route mesh와 Spot mesh를
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

- `zlink_framework_options_t::add_route_mesh(name)`을 추가해 route channel bind와 manual
  connection, routing id를 framework options 표면에서 설정하게 했다.
- `zlink_framework_options_t::use_registry_spot_remote_addresses(...)`를 추가해 Spot remote
  address resolver 기본값을 framework options에서 켤 수 있게 했다.
- `zlink_framework_options_t::add_spot_mesh(name).add_node(nodeName)`을 추가해 Spot node discovery
  channel과 node 설정을 한 곳에서 표현하게 했다.
- `spot_node_options_builder_t`에 route 수신 설정과 예전 channel client attach 설정을 추가해
  `.NET` sample의 route 수신과 channel client attach 의미를 C++ native style로 표현했다.
- `spot_node_options_builder_t::enable_router(...)`와 `enable_pub_sub(...)`를 추가해
  `.NET` sample의 `EnableRouter(...)`, `EnablePubSub(...)` 역할 구분을 C++ host factory에도
  드러나게 했다.
- `set_routing_id (routing_id).enable_router (endpoint)`와 `set_routing_id (routing_id).enable_pub_sub (endpoint)` overload를
  추가해 `.NET` sample topology의 `PlayRid`, `SessionRouterRid`, `SessionPubRid` 역할을 C++
  sample에서도 보존하게 했다.
- `add_route_mesh(...).set_routing_id(...)`와 route channel runtime routing id snapshot을
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

## 이전 계획 Goal 20. Final Parity And Regression Gate

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
rg -n "gtest|gmock|spdlog|fmt::|boost|kafka|grpc|yaml|flatbuffers|src/runtime|connector/core/src/runtime|Private/" framework/languages/cpp/framework/include framework/languages/cpp/connector/core/include framework/languages/cpp/connector/engines/unreal/Source/ZLinkStreamConnector/Public
find framework/languages/cpp/framework/include/zlink/framework/contracts/detail -type f -maxdepth 1 -print -exec sed -n '1,220p' {} \;
git diff --check -- framework/languages/cpp bindings/cpp
```

## 이전 계획 Goal 21. Extension Boundaries

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
  `connector/core/src/runtime/backend/contracts/connector_backend_contract.hpp`를 추가했다.
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
  `Server/Session`으로 나누고 session stream host, session relay, authenticate
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

- public `route_client_t`, `route_send_call_t`, `route_request_call_t`를 추가했다.
- `zlink_builder_t::route_client(serializer_registry_t&)`가 route channel registration을
  초기화하고 public client를 반환하도록 연결했다.
- route send call은 `.packet_name().submit()` 시점에 command envelope를 만들고
  `route_channel_runtime_t::submit_send_parts`로 내려간다.
- route request call은 `.packet_name().timeout().submit()` 시점에 request envelope와
  deadline을 만들고 request sequence를 등록한다.
- route request call의 `async<TReply>()`는 backend seam에서 받은 reply envelope body를
  serializer로 `TReply`로 복원한다.
- `test_cpp_framework_channel_messaging`이 public route client로 send/request를 호출한 뒤
  outbound packet, target node routing id, envelope kind/name/deadline, typed reply
  deserialization을 검증한다.

### 남은 tradeoff

- 현재 typed request public call은 route runtime backend seam으로 reply parts를 받아
  `TReply`를 완성한다. native route backend adapter가 이 seam에 연결됐고, public route client
  표면은 router socket lifecycle이나 discovery attach 세부를 노출하지 않는다.

### 재실행한 검증 명령

```bash
cmake --build framework/languages/cpp/build --target test_cpp_framework_channel_messaging
ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_channel_messaging --output-on-failure
```

## 추가 리뷰. Route typed request reply completion seam

### 발견한 위험 신호

- public route request가 request sequence만 반환하면 `.NET`의 `Async<TReply>` 의미와
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
- `route_client_t::request(...)`에 typed terminator를 추가해
  `.packet_name().timeout().async<TReply>()`가 `task_t<TReply>`를 반환하게 했다.
- backend가 없을 때는 timeout 성격의 실패로 반환해 미완성 backend를 성공처럼 숨기지 않는다.
- `test_cpp_framework_channel_messaging`이 backend seam에서 reply envelope를 만들고 public
  typed route request가 `reply_t`로 복원하는지 검증한다.

### 남은 tradeoff

- backend seam은 `native_route_backend_t`로 C++ binding `router_socket_t::send/request`에
  연결됐다. router socket owner나 route channel 초기화 정책이 바뀌어도 public route client는
  같은 seam 아래에 머문다.

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
  `router_socket_t::request(...).message(...).timeout(...).async().get()` 경로로 보내고
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
- `.NET`의 route mesh builder는 bind, manual connection, handler group,
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
  completion 연결이 끝나면 `.NET`의 `ZLinkRouteRequestCall<TRequest>.Async<TReply>`
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
  `ZLinkChannelBundleFactory`, `ZLinkChannelRuntimeManager`처럼 역할 bundle 생성과
  조회를 소유하는 내부 모듈이 없었다.
- manager가 없으면 client/publisher lazy creation, inbound 초기화, monitoring source
  parsing, route channel lookup이 각각 call object, monitoring runtime, registry runtime에
  흩어질 수 있다.
- route receive path도 `route_channel_runtime_t`에 직접 넣으면 route outbound와 inbound
  dispatch가 한 파일에 섞이고, 이후 route handler registry가 붙을 때 파일이 얕고 넓어진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `channel_runtime_t`에 bundle map과 helper를 직접 추가 | 호출 지점이 적다 | 기존 outbound pending/reliability runtime과 역할 owner가 섞인다 |
| monitoring/registry/SPOT 쪽에서 필요한 bundle을 직접 만든다 | 당장 필요한 경로에 맞출 수 있다 | 같은 역할 생성 규칙이 여러 모듈로 새어 나온다 |
| `.NET`처럼 bundle factory, runtime manager, route dispatcher/pump를 private runtime으로 분리 | 책임이 깊고 파일 분류가 대응된다 | 내부 파일과 테스트가 늘어난다 |

선택은 private runtime 분리다. bundle 생성 규칙은 factory가, state map과 lookup은 manager가,
route inbound dispatch는 dispatcher/pump가 맡는다.

### 적용한 리팩토링

- `channel_runtime_state_t`에 server/client/publisher/subscriber bundle map과 route channel
  runtime map을 추가했다.
- `channel_bundle_factory.*`를 추가해 역할 snapshot에서 runtime bundle을 만들고
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

선택은 private channel runtime 분리다. route channel은 channel runtime의 특수 역할로
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
- manual connection set, receive gate, dealer-mesh pending request owner가 역할
  단위 내부 상태로 묶이지 않으면 이후 route runtime이나 native adapter가 붙을 때 같은
  상태 지식이 여러 모듈에 흩어질 위험이 있었다.
- receive pump를 public call object나 builder 쪽으로 노출하면 사용자가 수신 순서와
  재진입 제어를 알아야 하므로 얕은 public API가 된다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `channel_runtime_state_t`에 필드만 추가 | 변경 파일이 적다 | 역할 단위 상태와 receive gate 책임이 큰 상태 객체에 섞인다 |
| receive pump를 public helper로 제공 | 테스트에서 직접 호출하기 쉽다 | raw 수신 루프가 public 계약처럼 보인다 |
| `.NET`처럼 bundle, message pump, receive loop를 private runtime으로 분리 | public API를 유지하면서 내부 책임이 깊어진다 | 내부 파일과 테스트가 늘어난다 |

선택은 private runtime 분리다. public channel API는 그대로 두고, 수신 루프와 manual
connection set은 `src/runtime/channels/*` 내부 모듈이 맡는다.

### 적용한 리팩토링

- `channel_runtime_bundle.*`를 추가해 manual connection set, receive gate,
  dealer-mesh pending request owner를 역할 runtime 상태로 묶었다.
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
rg -n '<금지 표현 패턴>' framework/doc/framework/cpp framework/languages/cpp/samples framework/languages/cpp/connector framework/languages/cpp/framework || true
```

## 추가 리뷰. 샘플 파일 분리 보정

### 발견한 위험 신호

- C++ 샘플은 역할별 executable로는 나뉘었지만 공유 umbrella header 하나가 configuration,
  contracts, domain model, handler, inbox, host wiring을 모두 담고 있었다.
- `.NET` 샘플은 공유 설정 디렉터리, `Shared/Contracts`, `Server/*/Handlers`,
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

선택은 `.NET` 역할과 같은 파일 계층으로 분리하는 것이다. 공유 umbrella header는 기존 include
호환을 위한 umbrella로만 남기고, 실제 정의는 역할별 헤더로 이동했다.

### 적용한 리팩토링

- Bingo를 공유 설정 디렉터리, `Shared/Contracts`, `Server/Api/Handlers`,
  `Server/Play/Infrastructure/ZLink/Handlers`,
  `Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot`,
  `Server/Play/Infrastructure/ZLink/Spots/EntrySpot` 파일로 분리했다.
- TicTacToe를 공유 actor 디렉터리, 공유 설정 디렉터리, `Shared/Contracts`,
  `Server/Api/Handlers`, `Server/Play/Infrastructure/ZLink/Spots/EntrySpot`,
  `Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot`,
  `Server/Play/Infrastructure/ZLink/Handlers` 파일로 분리했다.
- `test_cpp_framework_layout_contract`가 샘플 파일 분리 경로를 필수로 확인하도록 확장했다.
- 샘플 README와 draft 샘플 배치 문서를 실제 파일 구조와 맞게 수정했다.

### 남은 tradeoff

- 공유 umbrella header는 기존 sample main과 contract test include를 유지하기 위한 umbrella다.
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
- `zlink/framework/contracts/http/http.hpp`를 추가하고,
  `zlink_framework_options_t::http()`에서 `listen`, `configure_tls`, `map_get`, `map_post`,
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

- `add_client_server_channel`, `add_route_mesh`, `add_fanout_channel`, `add_stream_node` builder가
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
- **복잡성을 아래로**: 사용자는 `enable_server().enable_client().use_handler_group(...)` 같은 선언만 하고,
  framework가 최종 runtime 등록을 한 번으로 합쳐야 한다.

### 적용한 리팩토링

- `framework_options_state_t`에 key 기반 runtime action map을 추가했다.
- `add_client_server_channel`, `add_route_mesh`, `add_fanout_channel`, `add_stream_node`는 mutation마다
  action을 추가하지 않고 같은 key의 applier를 갱신한다.
- `apply()`는 일반 deferred action을 먼저 실행한 뒤 key 기반 applier를 한 번씩 실행한다.
- `test_cpp_framework_module_hosted`에서 같은 `api-channel`에 server와 client를 함께 선언해도
  최종 channel snapshot이 하나만 생기고 양쪽 역할이 모두 남는지 확인한다.

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
- 샘플 실행용 logging 정책은 `api_server_host_factory_t`에 두고, `add_bingo_api_server(...)`는
  API channel, discovery, codec, handler registration만 구성하게 했다. 그래야 재사용 가능한
  framework 구성 함수가 console/file/callback 같은 host-level logging sink를 강제로 정하지
  않는다.
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
`options.handlers().group(group_name).add<THandler>()`로 바꾸는 데서만 드러나야 한다.

### 적용한 리팩토링

- 문서 기준을 `app_t::add_zlink_framework<TModule>(...)` 중심에서
  `app_t::add_zlink_framework(options_callback)` 중심으로 수정했다.
- C++ sample API 설정의 목표 형태를 `.NET` `ApiServerHostFactory`와 같은 수준의 예제로 명시했다.
- handler 자동 검색은 C++에서 제공하지 않고,
  `options.handlers().group("api").add<authenticate_player_handler_t>()`처럼 handler 타입을 명시하는 것으로
  차이를 제한한다고 정리했다.
- `options.codecs().add_json()`은 codec 사용 선언만 맡기고, request/reply message type은 handler
  registration에서 framework가 읽어 serializer를 자동 등록하는 방향으로 낮췄다.
- C++에서는 람다 중첩이 `.NET`보다 장황해지므로 channel 설정은
  `options.add_client_server_channel(name).enable_server(endpoint).use_handler_group(group)`처럼 fluent builder로
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
- 샘플에서 낮은 수준 `use_zlink`, `advanced().zlink`, `channel`, `enable_server`,
  `enable_client`, handler용 DI
  factory, serializer registry 직접 설정이 다시 나타나지 않도록 sample parity 계약 테스트가
  source pattern을 확인한다.
- `zlink_builder_t`의 낮은 수준 API는 framework 내부 runtime과 단위 테스트용 확장 표면으로 남아
  있다. 일반 사용자 설정 표면인 `zlink_framework_options_t`에서는 람다 기반
  `add_client_server_channel(...)`과 `client_server_channel_options_t`를 제거했다.
- `add_stream_node` fluent builder는 `bind`와 `packet_session`이 모두 지정된 뒤에만 내부 stream
  builder에 반영한다. 이렇게 해야 체인 중간 상태가 runtime에 등록되어 저수준 검증 오류를 만드는
  문제를 막을 수 있다.

### 재실행할 검증 명령

```bash
cmake --build framework/languages/cpp/build --target sample_cpp_framework_bingo_registry sample_cpp_framework_bingo_api sample_cpp_framework_bingo_play sample_cpp_framework_bingo_session sample_cpp_framework_tictactoe_api sample_cpp_framework_tictactoe_play test_cpp_framework_sample_parity test_cpp_framework_contract_headers test_cpp_framework_module_hosted test_cpp_framework_DI_scope
ctest --test-dir framework/languages/cpp/build -R 'sample_smoke_sample_cpp_framework_(bingo|tictactoe)_(registry|api|play|session)|sample_smoke_sample_cpp_framework_tictactoe_(api|play)|test_cpp_framework_sample_parity|test_cpp_framework_contract_headers|test_cpp_framework_module_hosted|test_cpp_framework_DI_scope' --output-on-failure
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
  `connector/core/include/zlink/stream_connector/contracts` 양쪽에 적용된다.

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
  `connector.Request(dto).Async<T>()` 흐름과 다르고, codec 선택 지식이 샘플로
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
application code는 `.NET`과 같은 수준에서 `codecs::request(...).async<TReply>()`, `codecs::send`,
`codecs::on<T>`만 호출한다. sample-only payload 변환 함수나 직접 JSON parser는 두지 않는다.

### 적용한 리팩토링

- `zlink/stream_connector/codecs/auto_codec.hpp`를 추가했다.
- `codec_traits<T>` 기본 구현은 `message_t::from_json`과 `message.parse_json<T>()`를 사용한다.
- `codecs::send`, `codecs::request`, `codecs::on` helper를 추가해 encoded packet 생성,
  codec id 지정, typed callback decode를 한 곳에 모았다.
- connector public call object에 `codec(codec_t)` setter를 추가하고, `packet_t` 기반
  `send`/`request` overload를 추가해 encoded payload가 public API로 이동할 수 있게 했다.
- CMake의 stream connector codec helper는 connector 전용 Protobuf/MessagePack package를
  만들지 않는다. JSON은 framework 기본 codec을 쓰고, Protobuf/MessagePack은 framework
  codec extension target이 connector adapter를 함께 제공한다.
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

- 현재 backend selection은 public 계약으로 구조화된 backend를 선택할 수 있게 고정했지만 public
  header에는 `spdlog` 이름이나 타입을 노출하지 않는다. 실제 logging sink 최적화는 이 abstraction
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

- `Bingo/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/match_bingo_actor_handler.hpp`,
  `TicTacToe/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/join_match_handler.hpp`,
  `TicTacToe/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/place_mark_handler.hpp`는 실제 구현 없이 `Handlers/*`
  header만 include하는 wrapper였다. `.NET` 샘플은 handler 구현체가 `Handlers/` 아래에
  직접 있으므로, wrapper는 파일 구조 parity를 보여 주지 못한다.
- `Spots/BingoRoomSpot/bingo_room_handlers.hpp`는 여러 handler를 묶는 aggregate header였고,
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
- `Spots/BingoRoomSpot/bingo_room_handlers.hpp` aggregate header를 삭제했다.
- `Bingo`와 `TicTacToe` sample include를 실제 `Handlers/*` 구현체 경로로 바꿨다.
- `bingo_room_timer_handler.hpp`는 aggregate header 대신 실제 의존성인 `bingo_room.hpp`를
  include하도록 보정했다.
- layout contract test가 삭제된 wrapper/aggregate header가 다시 생기면 실패하도록
  `require_absent` 검사를 추가했다.

### 남은 tradeoff

- 공유 umbrella header는 sample parity test 편의를 위한 include surface다. 다만 이제 구현 없는
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
cmake --build framework/languages/cpp/build --target sample_cpp_framework_bingo_session test_cpp_framework_layout_contract
ctest --test-dir framework/languages/cpp/build -R 'sample_smoke_sample_cpp_framework_bingo_session|test_cpp_framework_layout_contract' --output-on-failure
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
`framework/src`, `http-client/src`, `connector/core/src`, Unreal connector private source로
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
- Goal 22 완료 기준과 검증 명령에 runtime line coverage 80% 이상 기준을 추가했다.

### 수정 후 점검

- coverage script는 tests, samples, external dependency, build generated source를 coverage
  분모에 넣지 않는다.
- 현재 coverage build 기준 runtime line coverage는 80.21%다.
- 기준값 80% 미만이면 CTest가 실패하므로 이후 회귀 테스트 추가/삭제가 숫자로 검증된다.

### 재실행한 검증 명령

```bash
cmake -S framework/languages/cpp -B framework/languages/cpp/build-coverage -DZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE=ON -DZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD=80
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

- `logging_backend_t::structured`는 public 설정 enum 값이지만 구체 logging library 이름을
  노출하지 않는다. layout contract는 `spdlog` 이름, `spdlog::` 타입, `<spdlog/...>` header가
  public header에 다시 들어오면 실패한다.
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
cmake -S framework/languages/cpp -B framework/languages/cpp/build-coverage -DZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE=ON -DZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD=80
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
cmake -S framework/languages/cpp -B framework/languages/cpp/build-coverage -DZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE=ON -DZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD=80
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
ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_layout_contract|test_cpp_stream_connector|sample_smoke_sample_cpp_framework_(bingo|tictactoe)' --output-on-failure
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
cmake -DZLINK_FRAMEWORK_CPP_BUILD_DIR=/home/hep7/project/kairos/zlink/framework/languages/cpp/build-coverage -DZLINK_FRAMEWORK_CPP_SOURCE_DIR=/home/hep7/project/kairos/zlink/framework/languages/cpp -DZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD=80 -P framework/languages/cpp/tests/Zlink.Framework.Coverage/coverage_threshold.cmake
git diff --check -- framework/languages/cpp
```

## 추가 리뷰. Stream Connector explicit receive 검증 보강

### 위험 신호

- 공통 Stream Connector 초안은 callback receive와 explicit receive API를 모두 완료 기준으로
  둔다. C++ connector public API에는 `receive()`가 추가됐지만, loopback 서버가 보낸
  packet을 callback 없이 직접 꺼내는 회귀 테스트가 없었다. 이는 테스트가 public 계약을
  충분히 증명하지 못하는 정보 은닉 약화다.
- close 이후 request 실패는 async result 경로로만 일부 확인됐고 callback submit 경로는
  직접 검증하지 않았다. callback과 task가 같은 오류 의미를 공유한다는 지식이 테스트 밖에
  남는 상태였다.
- `zlink_stream_connector.hpp`는 `std::chrono::milliseconds`를 public method 시그니처에
  쓰면서 직접 `<chrono>`를 include하지 않았다. public header가 transitive include에 의존하면
  호출자가 숨은 include 순서를 알아야 한다.

### 대안 검토

| 대안 | 장점 | 문제 |
|------|------|------|
| 문서에서 explicit receive 기준 제거 | 구현 변경이 작다 | 공통 완료 기준을 낮추고 언어별 connector 의미가 어긋난다 |
| runtime hook으로 queue에 packet을 넣어 `receive()`만 검증 | 테스트가 단순하다 | 실제 transport read, frame decode, multi-packet read를 증명하지 못한다 |
| loopback STREAM 서버가 두 frame을 한 번에 보내고 public `receive()`로 읽음 | public API와 wire frame 경계를 함께 검증한다 | 테스트 fixture가 조금 길어진다 |
| raw TCP 서버가 frame prefix를 나눠 보내 partial read를 재현 | read loop의 frame 복원 책임을 직접 검증한다 | STREAM helper 대신 raw socket fixture가 필요하다 |

선택은 세 번째 방식이다. explicit receive는 사용자에게 보이는 public API이므로 runtime 내부
hook보다 실제 transport를 통과한 packet으로 검증해야 한다.

### 수정

- `connector_t` public header가 `<chrono>`를 직접 include하도록 했다.
- `test_cpp_stream_connector`에 callback 없이 `receive(timeout)`를 두 번 호출해 서버가 보낸
  두 packet을 순서대로 꺼내는 loopback 검증을 추가했다.
- close 이후 request callback이 `disconnected` 오류를 받는지 확인해 task/callback 오류
  의미를 같은 회귀 테스트에 묶었다.
- request callback response와 timeout도 별도 loopback 서버로 검증해 공통 초안의
  request callback 완료 기준을 직접 증명하게 했다.
- raw TCP 서버가 frame을 두 번에 나눠 보내도 `receive(timeout)`이 하나의 packet으로 복원하는
  partial read 회귀 테스트를 추가했다.
- 기본 송신 제한보다 큰 서버 payload를 `receive(timeout)`로 받아 large payload 수신 처리가
  송신 제한과 섞이지 않는지 검증했다.
- reconnect 성공 경로는 첫 연결 실패 뒤 지연 시작한 TCP 서버로 재시도가 성공하고, 이후
  public send가 가능한지 확인하도록 추가했다.

### 재점검

- explicit receive 계약은 public API와 실제 STREAM frame read 경로로 검증된다.
- callback request response, timeout, close는 callback submit 경로에서 직접 검증된다.
- partial read는 raw TCP wire split을 통해 frame codec과 read loop 경계에서 검증된다.
- large payload receive는 송신 제한 검증과 별도로 inbound frame 복원 경로에서 검증된다.
- reconnect 성공과 실패는 각각 상태 전이와 실패 후 request 거부로 검증된다.
- public header는 public 시그니처에 필요한 표준 header를 직접 include한다.

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
  안에 둔 임시 HTTP handler를 route에 직접 붙여 `Server/Api/Handlers/create_game_http_handler_t`
  경계를 우회하고 있었다.
- 임시 handler는 고정된 `"tictactoe-game"` 응답만 만들었다. 이 상태에서는 sample API role의
  DI handler 구성과 create-game HTTP handler가 깨져도 client e2e가 통과할 수 있다.
- STREAM mock server가 HTTP 응답으로 받은 room id를 보존하지 않고 thread 시작 시점의 기본
  `"tictactoe-game"`을 reply state에 넣었다. 또한 모든 request에 `place_mark_res_t` 형태로
  답해 typed reply 계약이 약하게 검증됐다.
- HTTP hosting draft는 `POST /games` 응답의 Play stream endpoint로 stream connector가 연결한다고
  설명하지만, 당시 샘플 DTO와 API handler 응답에는 endpoint 목록이 없었다. 이 상태에서는 문서의
  시작 흐름과 실제 connector 입력이 서로 다른 지식을 갖게 된다.
- TicTacToe client e2e가 API HTTP endpoint를 고정 port로 열어 반복 실행이나 외부 프로세스와
  충돌할 수 있었다. 샘플 회귀 테스트가 환경 상태에 흔들리면 실제 샘플 문제와 포트 점유 문제를
  구분하기 어렵다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 임시 handler 유지 | e2e 구성이 단순하다 | 문서의 `Server/Api` handler 흐름을 검증하지 않는다 |
| 실제 Api server executable을 별도 process로 띄움 | role 분리가 가장 명확하다 | registry/play server까지 orchestration해야 해서 이번 보정 범위를 크게 넘는다 |
| client e2e의 HTTP app에 `create_game_http_handler_t`를 연결 | HTTP route가 실제 API handler와 DI 구성을 검증한다 | process 분리는 후속 샘플 orchestration 과제로 남는다 |

선택은 client e2e HTTP app에 실제 API handler를 연결하는 것이다. 이렇게 하면 HTTP client,
HTTP hosting, API handler DI 경계가 한 테스트에서 검증되고, process orchestration은 별도
샘플 실행기 개선으로 남길 수 있다.

### 적용한 리팩토링

- TicTacToe client e2e에서 임시 `sample_create_game_http_handler_t`를 제거했다.
- HTTP route는 `create_game_http_handler_t`를 사용하고, 그 의존성인 `channel_client_t`와
  sample topology를 sample app service로 등록한다.
- STREAM mock server는 `AuthenticateReq`, `JoinGameReq`, `PlaceMarkReq` payload를 읽고 각
  요청에 맞는 reply DTO를 반환한다.
- `JoinGameReq`와 `PlaceMarkReq`의 room id를 reply/push state에 반영해 HTTP `POST /games`
  결과로 받은 room id가 stream 흐름까지 이어지게 했다.
- `CreateGameHttpRes`에 Play stream endpoint 목록을 담고, API handler가 Play channel의
  `CreateGameRes` 결과를 HTTP 응답으로 변환하도록 했다.
- TicTacToe client는 HTTP create-game 응답의 `owner_play_endpoint`로 owner stream connector를
  연결하고, `play_endpoints`에서 observer endpoint를 고른다.
- TicTacToe client executable은 HTTP create-game 결과가 expected room id이고 owner Play stream
  endpoint가 loopback endpoint인지 확인한다.
- TicTacToe client e2e의 API HTTP endpoint는 zlink stream socket의 loopback port allocation을
  이용해 고정 port 충돌을 피한다.

### 수정 후 점검

- TicTacToe client e2e의 `POST /games`는 `Server/Api/Handlers/create_game_http_handler_t`를 통과한다.
- Stream connector request는 요청별 reply DTO와 HTTP create-game 결과의 room id를 함께
  검증한다.
- Stream connector endpoint는 샘플의 초기 옵션값이 아니라 HTTP `POST /games` 응답의
  `owner_play_endpoint`와 `play_endpoints`에서 온다.
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
- Unreal connector test도 public compile/smoke 계약을 확인하지만 `connector-unreal-contract`
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
- `test_unreal_stream_connector` label에 `connector-unreal-contract`를 추가했다.

### 수정 후 점검

- `ctest --test-dir framework/languages/cpp/build -N -L connector-contract`는
  `test_cpp_stream_connector`를 포함해야 한다. CTest label 선택은 정규식이므로
  `connector-unreal-contract`도 같은 명령에 함께 선택될 수 있다.
- `connector-protocol`, `connector-transport`, `connector-typed` label도 같은 connector
  회귀 테스트를 선택해야 한다.
- `connector-unreal-contract` label은 Unreal connector compile/smoke 테스트를 선택해야 한다.
- 이번 보정 뒤 connector label taxonomy에서 남은 즉시 수정 이슈는 0개다.

## 추가 리뷰. TicTacToe HTTP 문서 예시와 현재 구현 계약 정렬

### 발견한 위험 신호

- HTTP hosting, HTTP client, application framework, interface draft 일부가 C++ TicTacToe
  HTTP 시작 예시를 과거 `CreateMatchReq/Res` 흐름으로 설명했다.
- 현재 C++ TicTacToe sample의 shared contract와 handler는 `CreateGameHttpReq/Res`,
  `room_id`, `game_name`, Play stream endpoint 목록을 사용한다. 문서와 코드가 서로 다른 DTO
  이름을 갖고 있으면 사용자는 어떤 요청을 보내야 하는지 알기 어렵다.
- 현재 C++ sample의 검증된 HTTP path는 `create_game_http_handler_t`가 Play channel로
  `CreateGameReq`를 보내고 `CreateGameHttpRes`를 반환하는 흐름이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 문서의 과거 `CreateMatch*` 흐름을 유지 | 변경이 작다 | 현재 C++ sample과 테스트가 검증하는 계약을 설명하지 못한다 |
| C++ sample을 다시 `CreateMatch*`로 rename | 과거 로그와 이름이 맞는다 | 현재 Play channel과 HTTP DTO 경계를 되돌리는 큰 변경이다 |
| 문서 예시를 현재 C++ `CreateGameHttp*` 계약으로 정렬 | 구현과 테스트 증거가 같은 계약을 가리킨다 | 과거 로그의 결론을 현재 기준으로 보정해야 한다 |

선택은 문서 예시를 현재 C++ `CreateGameHttp*` 계약으로 정렬하는 것이다. 현재 C++ framework
구현 범위를 추적하는 문서는 검증 가능한 구현 계약을 우선한다.

### 적용한 리팩토링

- `cpp-http-client.ko.md`의 `/games` POST 예시를 `create_game_http_req_t`와
  `create_game_http_res_t`로 바꿨다.
- `cpp-http-hosting.ko.md`의 기준 흐름, handler shape, TicTacToe 반영 항목을
  `create_game_http_handler_t`, `CreateGameHttpReq/Res`, `room_id`, `game_name`,
  Play stream endpoint 기준으로 바꿨다.
- `cpp-application-framework.ko.md`, `cpp-framework-interfaces.ko.md`,
  `cpp-framework-policy.ko.md`의 TicTacToe HTTP 예시도 같은 계약으로 맞췄다.

### 수정 후 점검

- C++ 문서의 TicTacToe HTTP sample 설명은 `CreateGameHttpReq/Res`와 Play stream endpoint를
  중심으로 읽혀야 한다.
- 남아 있는 `CreateMatch*` 이름은 과거 로그나 별도 game-domain 용어가 필요한 위치에만 남기고,
  현재 HTTP 시작 계약 설명에는 남기지 않는다.
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

| 현재 goal | 대표 POSD 기록 | 재점검 상태 |
|-----------|----------------|-------------|
| Goal 1. Repository Skeleton And Tooling | `Goal 1. Tooling contract smoke 보강`, `Goal 1. Repository Skeleton And Build` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 2. Binding Codec Surface Alignment | `Goal 2. Codec boundary와 sample JSON 사용 gate 보강`, `Goal 2. Binding Codec Surface Alignment` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 3. Core Async, Task, Error Model | `Goal 3. Core Framework Types And Error Model`, `Runtime/Messaging Submit 상태 보강` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 4. App Host, Configuration, Logging | `Goal 4. App, Host, Configuration, Logging`, `Logging 표면 보정` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 5. DI Container And Scope Lifetime | `Goal 5. DI Container And Scope Lifetime`, `Configuration builder owner 분리` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 6. Application Framework Parity Model | `Application Framework` 문서 예시 보정, `.NET 구조 parity 반복 보정` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 7. Runtime Integration And Execution | `Goal 6. Runtime Integration And Dispatch Projection`, `Execution Queue와 Runtime Event Publisher 보강` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 8. Handler Registry And Serializer | `Goal 7. Handler Registry And Serializer`, `SPOT handler registry typed dispatch 보정` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 9. Channel Messaging | `Goal 8. Channel Messaging`, `Route public client facade 연결` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 10. Backpressure And Reliability | `Goal 9. Backpressure, Flow Control, Reliability`, `Channel Pending Request와 Reply Dispatcher 분리` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 11. SPOT Runtime | `Goal 10. SPOT Runtime`, `SPOT actor lifecycle handler shape 보정` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 12. SPOT Timer | `Goal 11. SPOT Timer`, `Goal 16/19. HTTP health route와 middleware short-circuit 보강` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 13. STREAM Framework | `Goal 12. STREAM Framework`, `Sample Session role 구현 분리` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 14. ActorGateway Session Relay | `Goal 13. ActorGateway Session Relay`, `ActorContext JoinSpot 결과 구조 보정` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 15. Registry And Topology | `Goal 14. Registry And Topology`, `TicTacToe sample discovery/topology 정렬` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 16. Monitoring, Health, Observability | `Goal 16. Health readiness/liveness 표면 보강`, `Logging 표면 보정` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 17. Module System And Hosted Services | `Goal 16. Hosted Services And Module System`, `AddZLinkFramework 대응 C++ module API 보정` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 18. ZLink HTTP Client | `Goal 18. ZLink HTTP Client 실제 산출물 추가`, `HTTP Client contract label 보강` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 19. HTTP Hosting | `Goal 19. HTTP Hosting runtime과 HTTP client e2e 연결`, `HTTP system route 충돌 validation 보강` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 20. Stream Connectors | `Goal 17. C++ Stream Connector`, `Unreal Stream Connector general connector dependency 제거`, `Connector label taxonomy 공백 보정` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 21. Review Samples | `Goal 19. Review Samples`, `TicTacToe client server handler include 제거`, `TicTacToe HTTP 시작 handler 경계 보정` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |
| Goal 22. Final Regression, Package, Extension Boundary | `Goal 22. Runtime coverage regression gate 추가`, `Parity label e2e coverage 보강`, `Goal 검증 명령 empty-selection gate 제거` | 잔여 POSD 위험 신호와 리팩토링 이슈 0개 |

### 수정 후 점검

- 현재 implementation plan의 Goal 1-22는 모두 이 로그 안의 대표 POSD 기록과 연결된다.
- 과거 section 번호는 실행 당시 기록으로 유지하고, 현재 plan 기준 대조는 위 표를 사용한다.
- 위 표의 각 row는 해당 goal의 대표 POSD 기록과 잔여 이슈 0개 상태를 함께 고정한다.

## 추가 리뷰. POSD 기록 매핑 contract gate 보강

### 발견한 위험 신호

- Goal 22는 Goal 1부터 Goal 22까지 각 goal에서 POSD 기반 리팩토링을 최소 한 번씩 수행하고,
  POSD 리팩토링 기록이 22개 이상 남아 있어야 한다고 적는다.
- POSD 로그에는 현재 Goal 1-22 대표 기록 표가 있지만, 테스트가 이 표와 기록 수를 검사하지
  않으면 표가 낡아져도 회귀에서 드러나지 않는다.
- 문서 증거가 테스트와 떨어져 있으면 final audit이 다시 수동 추정에 의존하게 된다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 수동 리뷰만 유지 | 코드 변경이 없다 | POSD 기록 누락이 자동 회귀에 잡히지 않는다 |
| 별도 문서 검사 스크립트 추가 | 책임이 분리된다 | 현재 layout/public surface gate와 중복된다 |
| layout contract가 POSD 기록 수와 현재 Goal 1-22 row를 검사 | Goal 22 final audit 증거를 기존 contract label에 묶는다 | layout contract가 문서 검사도 포함한다 |

선택은 세 번째 방식이다. Goal 22의 final audit은 public surface와 문서 증거를 함께 확인해야
하므로 기존 layout contract에 POSD 기록 매핑 검사를 붙인다.

### 적용한 리팩토링

- layout contract가 `cpp-framework-posd-refactoring-log.ko.md`의 `### 적용한 리팩토링`
  개수가 22개 이상인지 검사하게 했다.
- layout contract가 현재 Goal 1-22 대표 POSD 기록 표의 각 row를 검사하게 했다.

### 수정 후 점검

- POSD 기록 수나 현재 Goal 1-22 매핑 row가 빠지면 `test_cpp_framework_layout_contract`가
  실패한다.

## 추가 리뷰. CTest label empty-selection contract 보강

### 발견한 위험 신호

- implementation plan의 각 Goal 검증 명령은 CTest label을 기준으로 실행한다.
- 현재 수동 감사에서는 주요 label이 모두 test를 선택하지만, CMake가 새로 바뀌면 label이
  비어도 문서 명령 자체는 남아 있을 수 있다.
- 빈 label selection은 테스트 실패가 아니라 "No tests were found" 형태로 지나갈 수 있어,
  final audit이 실제 회귀 증거 없이 완료된 것처럼 보일 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 수동 `ctest -N -L` 감사 유지 | 새 test가 없다 | label drift가 자동으로 잡히지 않는다 |
| plan 문서에서 label 명령 제거 | empty selection 위험이 줄어든다 | Goal별 검증 표면이 약해진다 |
| CTest label contract가 주요 label의 non-empty selection을 검사 | 문서 명령과 CTest taxonomy를 자동으로 묶는다 | CTest 안에서 CTest dry-run을 한 번 더 호출한다 |

선택은 세 번째 방식이다. `ctest -N -L`은 실행하지 않는 dry-run이므로 빠르고, label taxonomy
drift를 기존 contract/regression label에서 바로 잡을 수 있다.

### 적용한 리팩토링

- `verify_ctest_label_contract.cmake`를 추가해 plan에 등장하는 주요 CTest label이 하나 이상의
  test를 선택하는지 검사하게 했다.
- `test_cpp_framework_label_contract`를 `framework-contract;framework-regression` label로
  등록했다.

### 수정 후 점검

- Goal 검증 label이 비면 `test_cpp_framework_label_contract`가 실패한다.

## 추가 리뷰. Core framework extension dependency boundary gate 보강

### 발견한 위험 신호

- Goal 22는 core framework target이 Kafka, gRPC, YAML, FlatBuffers dependency를 기본으로
  끌고 오지 않아야 한다고 적는다.
- extension unit test는 extension descriptor와 API shape를 확인하지만, `zlink_framework`
  target이 실수로 extension target이나 외부 package를 링크하는 회귀는 CMake 파일 기준으로
  직접 검사하지 않았다.
- dependency boundary가 CMake에만 숨어 있으면 public header 검사는 통과해도 build graph가
  무거워질 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| extension unit test만 유지 | 기존 동작을 유지한다 | core target 링크 누출을 직접 잡지 못한다 |
| CMake graph를 외부 도구로 출력해 검사 | 더 일반적이다 | 환경 의존성과 출력 파싱이 커진다 |
| layout contract가 CMake target/link/package 문자열을 검사 | 빠르고 현재 boundary를 직접 고정한다 | CMake 구조가 크게 바뀌면 검사도 갱신해야 한다 |

선택은 세 번째 방식이다. 현재 extension boundary는 CMake target taxonomy로 표현되므로,
layout contract에서 target 수와 core link 금지 규칙을 같이 확인하는 것이 가장 직접적이다.

### 적용한 리팩토링

- layout contract가 framework extension target 등록 수를 11개로 검사하게 했다.
- extension target helper가 core framework에만 의존하는지 확인하게 했다.
- `zlink_framework` target이 extension target이나 Kafka/gRPC/YAML/FlatBuffers package를
  기본 링크/탐색하지 않는지 검사하게 했다.

### 수정 후 점검

- core framework target dependency boundary가 깨지면 `test_cpp_framework_layout_contract`가
  실패한다.

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

## 추가 리뷰. HTTP client default header 회귀 보강

### 발견한 위험 신호

- HTTP client draft는 초기 구현 범위에 default header를 포함한다.
- 구현은 `client_builder_t::default_header(...)` 값을 runtime option에 저장하고, request별
  header를 나중에 적용해 같은 이름의 default header를 override할 수 있게 한다.
- 기존 `test_cpp_http_client`는 typed JSON, method, callback, status, timeout, HTTPS를
  검증했지만 default header와 request-level override 순서는 직접 검증하지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 구현만 신뢰 | 변경이 없다 | default header 계약이 테스트 없이 남는다 |
| runtime 옵션 accessor 추가 | 내부 상태를 직접 확인하기 쉽다 | public/test hook이 runtime 세부를 노출한다 |
| loopback server가 수신 header를 echo하는 public HTTP 테스트 추가 | 실제 wire request로 default/override 순서를 검증한다 | 테스트 endpoint 분기가 하나 늘어난다 |

선택은 loopback server echo 테스트다. default header는 사용자가 관찰하는 wire 계약이므로,
runtime 내부 option을 직접 읽지 않고 HTTP request 결과로 검증한다.

### 적용한 리팩토링

- `test_cpp_http_client`에 `/headers` echo path를 추가했다.
- `default_header("From", ...)`가 request에 실리는지 확인했다.
- 같은 이름의 default header를 `.header(...)`가 request 단위로 덮는지 확인했다.

### 수정 후 점검

- `zlink::http_client`의 default header와 request header override 순서는 public HTTP client
  test에서 검증되어야 한다.
- 이번 보정 뒤 HTTP client default header 회귀 공백은 0개다.

## 추가 리뷰. TicTacToe sample HTTP 시작 흐름 로그 gate 보강

### 발견한 위험 신호

- Goal 19와 Goal 21은 TicTacToe client가 HTTP `POST /games`로 시작하고, sample e2e가 server
  file log의 request/reply 흐름을 검증해야 한다고 적는다.
- client sample은 실제로 `zlink::http_client`로 `/games`를 호출했지만, e2e log assertion은
  stream server packet만 확인했다. 이 상태에서는 HTTP 시작 흐름이 나중에 빠져도 log gate가
  통과할 수 있다.
- HTTP handler와 stream client가 같은 sample log file을 다루면서 파일 이름과 초기화 정책이
  각 파일에 흩어지면 로그 생산 순서에 따라 한쪽 로그가 지워질 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| client result의 `http_game_created`만 신뢰 | 이미 smoke에서 확인한다 | server file log gate 요구를 직접 증명하지 못한다 |
| CTest에서 stdout을 추가 확인 | 구현 변경이 작다 | sample server file log 기준과 분리된다 |
| HTTP handler가 sample log에 request/reply를 남기고 CTest expected를 강화 | HTTP 시작 흐름을 같은 e2e log gate로 고정한다 | sample log helper가 필요하다 |

선택은 세 번째 방식이다. HTTP 시작 흐름은 sample server가 처리한 request/reply이므로,
server file log에서 stream request와 함께 검증하는 것이 plan의 완료 기준과 가장 직접 맞는다.

### 적용한 리팩토링

- TicTacToe 공유 sample log header에 sample log file 이름, reset, append helper를 모았다.
- `create_game_http_handler_t`가 HTTP `POST /games`, `recv CreateGameHttpReq`, `reply CreateGameHttpRes`를
  sample log에 남기게 했다.
- TicTacToe sample e2e CTest expected와 최소 request/reply count를 HTTP 시작 흐름까지
  포함하도록 올렸다.

### 수정 후 점검

- sample log file 초기화와 append 정책은 shared helper 한 곳이 소유한다.
- TicTacToe sample e2e log gate는 HTTP `POST /games`와 stream connector request/reply/push를
  모두 고정한다.

## 추가 리뷰. App Host rotating file logging 회귀 보강

### 발견한 위험 신호

- Goal 4는 console, file, rotating file, callback sink, async logging option을 구현 항목으로
  둔다.
- 기존 app host 테스트는 console/file/callback/async/backend/min-level은 검증했지만,
  rotating file sink가 실제 파일 회전을 수행하는지는 직접 확인하지 않았다.
- rotating file 동작을 builder 내부 상태만 확인하면 파일 크기, rename 순서, 새 로그 쓰기 같은
  사용자가 실제로 관찰하는 계약을 놓칠 수 있다.
- 일반 file sink와 rotating file sink를 함께 쓰면 file path와 rotation option이 서로 다른
  vector에 저장되어 순서가 어긋날 수 있었다. 이는 같은 sink 설정 지식이 두 자료구조에 나뉜
  정보 은닉 약화다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `file_paths()`에 path가 추가되는지만 확인 | 테스트가 작다 | 회전 동작을 전혀 증명하지 못한다 |
| runtime helper를 public/test hook으로 노출 | 세부 상태 확인이 쉽다 | logging 구현 세부를 테스트 표면으로 끌어올린다 |
| 기존 app host 테스트에서 작은 max size로 실제 파일 회전을 확인 | public logging API와 파일 결과만 본다 | 임시 파일 검증이 조금 늘어난다 |

선택은 세 번째 방식이다. rotating file은 파일 시스템 결과가 public 관찰 지점이므로 runtime
내부 helper를 노출하지 않고 실제 회전 결과로 검증한다.

### 적용한 리팩토링

- `test_cpp_framework_app_host`가 기존 rotating log file을 만든 뒤 `use_rotating_file(...)`을
  작은 `max_file_size`로 설정한다.
- logger write 후 기존 파일이 `.1`로 이동하고 새 rotating log file에 현재 record가 쓰였는지
  확인한다.
- `use_file(...)`도 non-rotating option slot을 함께 추가해 file path와 rotation option의
  index가 항상 1:1로 맞게 했다.

### 수정 후 점검

- Goal 4 logging sink 중 console/file/rotating/callback/async/backend/min-level은 같은 app host
  regression에서 모두 검증된다.
- 일반 file sink와 rotating file sink를 함께 등록해도 rotating sink가 자기 option을 사용한다.

## 추가 리뷰. HTTPS/TLS toolchain manifest 보강

### 발견한 위험 신호

- Goal 18, Goal 19, Goal 20은 HTTPS, TLS transport, WebSocket over TLS를 완료 기준으로 둔다.
- CMake runtime은 OpenSSL을 찾으면 secure 기능과 테스트를 켜지만, vcpkg manifest에는 OpenSSL
  dependency가 없었다. vcpkg preset을 쓰는 clean 환경에서는 secure 기능이 조용히 빠질 수 있다.
- tooling contract는 vcpkg manifest의 Boost, GTest, LZ4, JSON만 확인해 HTTPS/TLS dependency
  누락을 잡지 못했다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| OpenSSL을 optional로 계속 둠 | OpenSSL 없는 환경도 configure가 쉽다 | HTTPS/TLS 완료 기준과 vcpkg preset 기대가 어긋난다 |
| CMake에서 OpenSSL 없으면 항상 실패 | secure 기능 누락을 즉시 잡는다 | system package 없이 HTTP-only 개발도 막는다 |
| vcpkg manifest와 tooling contract에 OpenSSL을 명시 | vcpkg preset은 secure 기능을 재현 가능하게 한다 | non-vcpkg 환경은 여전히 system OpenSSL에 의존한다 |

선택은 세 번째 방식이다. plan은 vcpkg manifest를 tooling 산출물로 두므로, vcpkg 경로에서는
HTTPS/TLS dependency를 명시해 secure 기능이 빠지지 않게 해야 한다.

### 적용한 리팩토링

- `framework/languages/cpp/vcpkg.json`에 `openssl` dependency를 추가했다.
- tooling contract가 vcpkg manifest에서 `openssl`을 요구하도록 보강했다.

### 수정 후 점검

- vcpkg preset은 HTTP client HTTPS, HTTP hosting HTTPS, connector TLS/WSS 구현에 필요한
  OpenSSL dependency를 manifest에서 제공한다.

## 추가 리뷰. Hosted service start failure lifecycle gate 보강

### 발견한 위험 신호

- Goal 17은 hosted service가 app startup/shutdown과 함께 시작하고 종료해야 한다고 적는다.
- 기존 module/hosted test는 정상 시작과 역순 종료는 검증했지만, 여러 hosted service 중 하나가
  `start()`에서 실패할 때 이미 시작된 service가 정리되는지는 직접 확인하지 않았다.
- 실패 경로의 정리 책임이 caller에게 새면 lifecycle 지식이 app host 밖으로 새는 얕은 모듈이
  된다. POSD 관점에서는 host가 start/stop 순서와 실패 정리를 한 곳에서 숨겨야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 정상 lifecycle test만 유지 | 테스트가 작다 | start 실패 회귀를 잡지 못한다 |
| app host 내부 helper를 public test hook으로 노출 | started 목록을 직접 볼 수 있다 | 내부 순서 자료구조를 공개 표면으로 끌어올린다 |
| public hosted service로 start 실패를 만들고 관찰 가능한 start/stop event만 확인 | public lifecycle 계약만 본다 | test helper class가 하나 늘어난다 |

선택은 세 번째 방식이다. hosted service lifecycle은 public `hosted_service_t`의 `start()`와
`stop()` 호출 결과로 증명되어야 하며, app host 내부 started list를 노출할 필요가 없다.

### 적용한 리팩토링

- `test_cpp_framework_module_hosted`에 `failing_start_hosted_service_t`를 추가했다.
- 두 번째 hosted service가 `start()`에서 실패할 때 첫 번째 service만 `stop()`되고 세 번째
  service는 시작되지 않는지 검증했다.

### 수정 후 점검

- Goal 17 hosted service lifecycle은 정상 start/stop과 start 실패 cleanup을 모두 회귀 테스트로
  고정한다.

## 추가 리뷰. STREAM disconnected write gate 보강

### 발견한 위험 신호

- Goal 13은 pending write 중 disconnect가 발생하면 caller가 disconnected 계열 error를 받아야
  한다고 적는다.
- `dispatch_disconnected()`는 session을 closed 상태로 표시했지만, `stream_t::write_packet()`은
  그 상태를 확인하지 않고 항상 성공으로 write record를 추가했다.
- disconnect 상태 지식이 dispatch 경로와 write 경로에 나뉘면 caller가 close 이후 write 성공을
  관찰할 수 있다. 이는 session lifecycle 정보를 `stream_t`가 숨기지 못한 정보 은닉 약화다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| test에서 serial log만 확인 | 기존 구조를 건드리지 않는다 | write caller가 받는 error를 증명하지 못한다 |
| runtime에 별도 pending write table을 공개 | pending 상태를 직접 볼 수 있다 | 아직 필요 없는 세부 구현을 public/test 표면으로 끌어올린다 |
| `stream_t::write_packet()`이 session closed 상태를 보고 `disconnected`를 반환 | caller 계약을 가장 가까운 write 표면에서 지킨다 | 현재 구현의 pending write 모델은 즉시 call object에 머문다 |

선택은 세 번째 방식이다. 현재 runtime은 write call object를 즉시 완료하므로, disconnect 이후 write를
가장 가까운 public write 표면에서 실패시키는 것이 완료 기준과 가장 직접 맞는다.

### 적용한 리팩토링

- `stream_t::write_packet()`이 closed session이면 `framework_error_kind_t::disconnected` 실패를
  반환하도록 했다.
- `test_cpp_framework_stream_framework`가 disconnect 이후 write 실패와 write record 미증가를
  검증하도록 보강했다.

### 수정 후 점검

- STREAM session lifecycle 상태는 `stream_t` 내부 state가 소유한다.
- close 이후 write 성공으로 보이는 회귀는 `framework-zlink-stream` test가 잡는다.

## 추가 리뷰. HTTP error pipeline middleware gate 보강

### 발견한 위험 신호

- Goal 19와 HTTP hosting 문서는 exception, logging, validation, auth, correlation id 처리를
  middleware/filter extension point로 둔다고 적는다.
- `cpp-http-hosting.ko.md`는 short-circuit 경로도 `after(...)` middleware를 거쳐 logging과
  correlation 처리를 한 곳에 둘 수 있어야 한다고 설명한다.
- 기존 runtime은 정상 응답에서만 `after(...)`를 실행했고, JSON binding 실패나 handler
  exception이 발생하면 `after(...)`가 실행되지 않았다.
- `cpp-application-framework.ko.md`의 기본 HTTP error response에는 `correlationId`가 포함되지만,
  실제 error JSON은 `error`와 `message`만 반환했다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 오류 응답 테스트만 body/status로 유지 | 기존 구현을 보존한다 | logging/correlation middleware 계약을 증명하지 못한다 |
| middleware에 별도 `on_error(...)` hook 추가 | 오류 전용 처리가 명확하다 | 초기 core API 범위를 넓히고 route filter 설계를 앞당긴다 |
| 현재 `before/after` hook을 유지하되 오류 경로에서도 `after`를 한 번 실행 | public API를 늘리지 않고 문서의 cross-cutting 위치를 지킨다 | runtime의 try/catch 구조를 정리해야 한다 |

선택은 세 번째 방식이다. 초기 core 범위는 `before/after` hook이므로, 오류 경로도 같은
pipeline으로 닫아야 logging/correlation 지식이 handler마다 반복되지 않는다.

### 적용한 리팩토링

- HTTP route 처리에서 request scope와 middleware invocation 목록을 try/catch 바깥의 route 처리
  상태로 올렸다.
- 정상, binding 실패, handler failure 경로 모두 `after(...)` middleware를 한 번 실행하게 했다.
- framework error JSON과 일반 exception error JSON에 `correlationId`를 포함했다.
- app host HTTP e2e가 invalid JSON과 handler timeout 오류에서 `X-Middleware-After`와
  correlation id 보존을 검증하게 했다.

### 수정 후 점검

- HTTP 오류 응답도 middleware/filter pipeline의 관찰 지점을 지난다.
- correlation id는 success DTO, response header, error JSON에 모두 남는다.

## 추가 리뷰. Registry stale Spot route cleanup gate 보강

### 발견한 위험 신호

- Goal 15는 Registry와 Topology 완료 기준에 stale address cleanup을 포함한다.
- registry runtime은 remote Spot route를 추가하고 조회할 수 있었지만, 오래된 route를 제거하는
  경로와 회귀 테스트가 없었다.
- stale route가 계속 lookup에 남으면 Registry가 Spot remote address 기본값으로 동작할 때
  caller가 이미 사라진 route를 정상 route처럼 받을 수 있다.
- stale 판단과 route table 소유권이 caller 쪽으로 새면 Registry lookup 정책이 얕은 helper로
  흩어진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 조회 실패 테스트만 유지 | 기존 구현을 유지한다 | stale route 누적을 잡지 못한다 |
| public `registry_query_t`에 remove API를 추가 | application code에서 정리할 수 있다 | cleanup 정책을 caller에게 노출한다 |
| detail registry runtime이 active RID set 기준으로 stale route를 제거 | route table 소유자가 cleanup도 맡는다 | 현재 테스트는 detail runtime hook을 사용한다 |

선택은 세 번째 방식이다. Spot route table은 registry runtime 내부 상태이므로, stale cleanup도
같은 owner 안에 두는 것이 정보 은닉에 맞다. public query 표면은 lookup 계약만 유지한다.

### 적용한 리팩토링

- `registry_runtime_t::cleanup_stale_spot_routes(...)`를 추가했다.
- registry topology test가 active route와 stale route를 함께 등록한 뒤, cleanup 이후 stale
  route lookup이 `spot_route_not_found`로 실패하는지 검증하게 했다.

### 수정 후 점검

- stale Spot remote address는 registry runtime 내부 cleanup으로 제거된다.
- Goal 15의 stale address cleanup 항목은 `framework-zlink-registry` 회귀 테스트가 잡는다.

## 추가 리뷰. ActorGateway disconnected session relay gate 보강

### 발견한 위험 신호

- Goal 14와 ActorGateway session relay 문서는 session disconnect cleanup 뒤 bound session push와
  relay가 실패해야 한다고 적는다.
- `bound_session_t::send_raw(...)`은 bound 상태를 확인했지만, `session_actor_t::relay(...)`는
  disconnect된 actor record를 보지 않고 stale actor ref로 relay frame을 계속 기록했다.
- disconnect 상태 지식이 push와 relay 경로에 다르게 적용되면 caller는 같은 session actor에
  대해 push는 실패하고 relay는 성공하는 모순을 관찰한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| actor bound 상태 테스트만 유지 | 기존 relay 구현을 유지한다 | cleanup 뒤 stale relay를 잡지 못한다 |
| `notify_disconnected()`에서 actor ref를 비움 | relay 실패를 쉽게 만든다 | 기존 `session_actor_t` 복사본의 상태가 여전히 남을 수 있다 |
| push와 relay가 모두 actor gateway state의 bound/disconnected record를 확인 | 상태 소유자를 한 곳으로 유지한다 | relay마다 map lookup이 추가된다 |

선택은 세 번째 방식이다. actor-session binding 상태는 ActorGateway state가 소유하므로,
push와 relay 모두 같은 state를 확인해야 cleanup 의미가 한 곳에 모인다.

### 적용한 리팩토링

- `bound_session_t::send_raw(...)`이 disconnected record에는 `disconnected` error를 반환하도록
  했다.
- `session_actor_t::relay(...)`가 actor record의 bound/disconnected 상태를 확인하도록 했다.
- ActorGateway regression test가 `notify_disconnected()` 이후 push와 relay가 모두
  `disconnected`로 실패하고 payload가 소비되지 않는지 검증하게 했다.

### 수정 후 점검

- disconnect cleanup 뒤 stale `session_actor_t`로 relay frame이 추가되지 않는다.
- Goal 14의 disconnected session push/relay 실패 계약은 `framework-zlink-actor-gateway`
  test가 잡는다.

## 추가 리뷰. Unreal Stream Connector general connector dependency 제거

> 이 절의 결론은 이후 `cpp-stream-connector.ko.md`의 adapter 기준 보강으로 대체되었다.
> Unreal public API가 일반 C++ connector type을 노출하면 안 된다는 판단은 유지한다.
> 그러나 Unreal private 구현까지 기본 connector를 사용하지 말아야 한다는 판단은 폐기한다.
> 현재 기준은 Unreal adapter가 기본 connector를 private dependency로 소유하고, Unreal
> public API와 Game Thread dispatch만 adapter에서 책임지는 것이다.

### 발견한 위험 신호

- Goal 20은 Unreal connector가 일반 C++ connector wrapper가 아니라 Unreal network library 기반
  구현이어야 한다고 적는다.
- Unreal private source는 자체 frame codec과 Unreal `Sockets` 경로를 사용하지만, CMake target은
  `zlink::stream_connector`를 public link하고 `connector/core/src`를 private include로 열어 두었다.
- 구현은 독립인데 build graph가 일반 connector runtime을 끌고 오면, 나중에 wrapper 의존성이
  조용히 생겨도 public surface gate가 놓칠 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 link 유지 | 빌드 변경이 없다 | Goal 20의 독립 connector 기준을 증명하지 못한다 |
| link를 PRIVATE로 낮춤 | public 전이는 줄어든다 | 일반 connector runtime 의존성 자체는 남는다 |
| 일반 connector link와 runtime include를 제거 | Unreal connector가 자체 Unreal transport만 의존한다 | 필요한 공유 코드가 생기면 별도 protocol contract로 분리해야 한다 |

당시 선택은 세 번째 방식이었다. 이후 Stream Connector draft를 재정리하면서 이 선택은
잘못된 방향으로 판정했다. 자체 frame/metadata/LZ4 경로를 Unreal adapter에 두면 기본 connector
구현을 엔진별로 복제하게 된다. 정보 은닉 기준에서 숨겨야 할 것은 기본 connector type을 Unreal
public header에 노출하지 않는 것이지, private 구현에서 기본 connector를 의존하지 않는 것이
아니다.

### 적용한 리팩토링

- `zlink_unreal_stream_connector` target에서 `zlink::stream_connector` link를 제거했다.
- 같은 target의 `connector/core/src` private include도 제거했다.
- layout contract가 Unreal connector target이 일반 connector target이나 runtime include를 다시
  참조하지 않는지 검사하게 했다.

### 현재 기준

- Unreal public header는 Unreal type과 delegate만 노출한다.
- Unreal private source는 `zlink::stream_connector` public API를 사용해 connect, send, request,
  dispatch, close를 위임한다.
- Unreal adapter는 `connector/core/src/runtime/*` private header나 자체 STREAM frame codec을 소유하지
  않는다.
- CMake 검증 target은 `zlink::stream_connector`와 `zlink::stream_connector_codecs`를 `PRIVATE`
  dependency로 링크한다.

## 추가 리뷰. TicTacToe client server handler include 제거

### 발견한 위험 신호

- Goal 21은 `Client` 샘플이 서버 handler를 직접 호출하지 않고 HTTP client와 connector를
  사용해야 한다고 적는다.
- layout contract는 `Client/main.cpp`만 검사했기 때문에 `TicTacToe/Client/tictactoe_client.hpp`가
  `Server/Api/Handlers/create_game_http_handler.hpp`를 include해도 잡지 못했다.
- client e2e가 HTTP handler 구현 타입을 직접 알면 HTTP 경계 검증과 server handler 소유권이
  같은 파일에 섞인다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 검사 유지 | 변경이 없다 | client subtree의 server include를 놓친다 |
| client가 server host factory를 직접 사용 | 실제 server handler를 재사용한다 | server 구현 지식이 client에 계속 남는다 |
| client e2e용 HTTP fixture handler를 client 내부에 두고 server include를 금지 | HTTP client 경계는 유지하고 server handler 소유권을 분리한다 | fixture handler가 sample e2e 전용 코드로 남는다 |

선택은 세 번째 방식이다. TicTacToe client e2e는 HTTP `POST /games`와 stream connector 연결을
검증하면 충분하고, Play/API server handler class 자체를 알 필요는 없다.

### 적용한 리팩토링

- `tictactoe_client.hpp`에서 server API handler include를 제거했다.
- client e2e용 `tictactoe_client_e2e_create_game_handler_t`를 추가해 공유 DTO와 topology만으로
  `/games` 응답을 만든다.
- layout contract가 `samples/*/Client` 전체에서 server implementation include를 금지하게 했다.

### 수정 후 점검

- TicTacToe client sample은 서버 handler를 include하지 않고 `zlink::http_client`로 `/games`를
  호출한다.
- server handler include 회귀는 `test_cpp_framework_layout_contract`가 client subtree 전체에서
  잡는다.

## 추가 리뷰. Parity label e2e coverage 보강

### 발견한 위험 신호

- Goal 22는 `.NET` parity e2e regression을 완료 기준에 둔다.
- `parity` CTest label은 `test_cpp_framework_sample_parity`의 중복 실행만 선택했고, 실제
  client/server request, reply, push, log 검증을 수행하는 sample e2e log 테스트는 선택하지
  않았다.
- label 의미가 정적 parity 검사에만 묶이면 final audit에서 `ctest -L parity`가 e2e 증거를
  제공하지 못한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 label 유지 | 변경이 없다 | parity e2e 요구가 label로 증명되지 않는다 |
| 별도 parity e2e 테스트를 새로 추가 | label 의미가 가장 명확하다 | 기존 sample e2e log test와 setup이 중복된다 |
| 기존 sample e2e log 테스트에 `parity` label을 추가 | 실제 e2e 증거를 재사용하고 label 선택이 강해진다 | 한 테스트가 여러 목적 label을 가진다 |

선택은 세 번째 방식이다. sample e2e log 테스트는 이미 client 성공, server file log, push,
disconnect, shutdown을 확인하므로 parity e2e 증거로 재사용할 수 있다.

### 적용한 리팩토링

- Bingo와 TicTacToe sample e2e log 테스트에 `parity` label을 추가했다.

### 수정 후 점검

- `ctest -L parity`는 정적 sample parity와 sample e2e log regression을 함께 선택한다.
- parity audit은 packet/handler 구조뿐 아니라 실제 client/server 흐름도 확인한다.

## 추가 리뷰. Plan label과 POSD goal heading drift 보강

### 발견한 위험 신호

- 실행 계획의 기능 축 추적표는 SPOT timer 검증 축에 `timer` label을 적지만, label
  empty-selection contract는 이 label을 직접 검사하지 않았다.
- POSD 기록에는 현재 계획으로 재번호화되기 전의 `Goal 17`, `Goal 18`, `Goal 20`,
  `Goal 21` heading이 그대로 남아 있었다.
- 현재 계획에서 Goal 18은 `ZLink HTTP Client`, Goal 20은 `Stream Connectors`, Goal 21은
  `Review Samples`이므로, 예전 heading을 현재 heading처럼 두면 문서 대조 때 같은 번호가
  서로 다른 기능을 뜻한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 로그 heading을 그대로 둠 | 과거 기록을 손대지 않는다 | 현재 goal 번호와 충돌한다 |
| 예전 로그 섹션을 삭제 | 현재 plan만 남는다 | 어떤 리뷰에서 해당 변경이 들어왔는지 추적하기 어렵다 |
| 예전 heading을 `이전 계획 Goal N`으로 표시하고 contract로 stale heading을 금지 | 기록은 보존하고 현재 plan과 구분한다 | heading 검사 목록을 plan 재번호화 때 갱신해야 한다 |

선택은 세 번째 방식이다. POSD 기록은 변경 이력으로 보존해야 하지만, 현재 실행 계획과 같은
heading 형식으로 남아 있으면 완료 audit의 기준이 흐려진다.

### 적용한 리팩토링

- label contract가 `timer` label도 비어 있지 않은지 확인하게 했다.
- 예전 plan 번호로 작성된 connector/final/extension heading을 `이전 계획 Goal N`으로 바꾸었다.
- layout contract가 현재 plan과 충돌하는 예전 goal heading이 다시 들어오면 실패하게 했다.

### 수정 후 점검

- `timer` label이 비면 `test_cpp_framework_label_contract`가 실패한다.
- POSD 로그는 과거 변경 이력과 현재 Goal 18/20/21 의미를 서로 구분한다.

## 추가 리뷰. Coverage 전용 label contract 보강

### 발견한 위험 신호

- Goal 22는 coverage build의 runtime line coverage 80% 이상을 완료 기준으로 둔다.
- `framework-coverage` label은 coverage build에서만 생기므로 normal build의 label contract에
  무조건 넣으면 정상 build가 실패한다.
- 반대로 label contract가 `framework-coverage`를 전혀 보지 않으면 coverage threshold test가
  label에서 빠져도 normal build 검증만으로는 드러나지 않는다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `framework-coverage`를 항상 필수 label로 둠 | 규칙이 단순하다 | coverage option이 꺼진 normal build가 실패한다 |
| coverage label은 coverage threshold test 자체에만 맡김 | 변경이 적다 | label drift를 별도 contract가 잡지 못한다 |
| label contract에 coverage build 여부를 넘겨 조건부 필수 label로 검사 | normal build와 coverage build 의미를 모두 보존한다 | CTest command에 옵션 전달이 필요하다 |

선택은 세 번째 방식이다. coverage label은 coverage build의 공개 검증 축이므로, coverage build
안에서는 empty-selection contract가 직접 확인해야 한다.

### 적용한 리팩토링

- `test_cpp_framework_label_contract` 실행 시
  `ZLINK_FRAMEWORK_CPP_EXPECT_COVERAGE_LABEL` 값을 CMake option에서 넘기게 했다.
- label contract는 이 값이 켜진 build에서만 `framework-coverage` label을 필수로 검사한다.

### 수정 후 점검

- normal build에서는 coverage test가 없어도 label contract가 통과한다.
- coverage build에서는 `framework-coverage` label이 비면 label contract가 실패한다.

## 추가 리뷰. Draft 추적표 파일 목록 contract 보강

### 발견한 위험 신호

- 실행 계획은 `Draft 추적표`에서 C++ framework draft 문서와 구현 goal의 대응을 관리한다.
- 실제 `doc/draft/*.ko.md` 파일과 추적표 항목이 어긋나도 기존 contract는 이 차이를 직접
  검사하지 않았다.
- 새 draft 문서를 추가하거나 문서를 삭제한 뒤 추적표가 따라오지 않으면 final audit에서 어떤
  문서가 구현 계획 범위인지 판단하기 어려워진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 사람이 plan 하단을 눈으로 대조 | 별도 코드가 없다 | 반복 리뷰 때 누락을 놓치기 쉽다 |
| 추적표 문서 목록을 contract에 하드코딩 | 검사 의도가 명확하다 | 새 draft 추가 때 plan과 contract를 모두 수정해야 한다 |
| layout contract가 실제 draft 파일 목록과 추적표 링크를 비교 | 파일 추가/삭제 drift를 직접 잡는다 | 추적표 마크다운 구조가 바뀌면 검사도 갱신해야 한다 |

선택은 세 번째 방식이다. 추적표는 실제 draft 파일의 공개 인덱스 역할을 하므로, 파일 시스템과
계획 문서의 링크 목록이 1:1인지 검사하는 것이 가장 직접적이다.

### 적용한 리팩토링

- layout contract가 `## 6. Draft 추적표`와 `## 7. 기능 축 추적표` 사이의 표를 읽게 했다.
- `doc/draft/*.ko.md` 파일마다 추적표에 `./파일명` 링크가 정확히 한 번 있는지 검사하게 했다.
- 추적표의 `.ko.md` 링크 개수와 실제 draft 파일 개수가 다르면 실패하게 했다.

### 수정 후 점검

- draft 파일이 추가되거나 삭제됐는데 실행 계획 추적표가 따라오지 않으면
  `test_cpp_framework_layout_contract`가 실패한다.

## 추가 리뷰. Draft README 역할 표 HTTP Client 누락 보강

### 발견한 위험 신호

- Goal 18은 `ZLink HTTP Client`를 별도 산출물과 별도 draft 문서로 둔다.
- `doc/draft/README.ko.md` 상단 링크에는 `cpp-http-client.ko.md`가 있었지만,
  `문서 구조와 역할 분담`의 주제 문서 표에는 HTTP Client 역할 설명이 없었다.
- 사용자가 README 본문에서 문서 역할을 찾으면 HTTP client draft가 어떤 범위인지 알 수 없다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 상단 링크만 유지 | 문서 이동은 가능하다 | 역할 표가 Goal 18 산출물을 설명하지 않는다 |
| HTTP Client 행만 수동 추가 | 즉시 누락을 고친다 | 다음 draft 문서 누락을 자동으로 잡지 못한다 |
| HTTP Client 행을 추가하고 README 역할 표가 모든 draft를 덮는지 contract로 검사 | 현재 누락과 재발을 같이 막는다 | README 섹션 경계가 바뀌면 검사도 갱신해야 한다 |

선택은 세 번째 방식이다. README의 역할 표는 draft 독자가 각 문서의 범위를 파악하는 첫
본문 인덱스이므로, 실제 draft 파일과 같은 범위를 설명해야 한다.

### 적용한 리팩토링

- README 주제 문서 표에 `cpp-http-client.ko.md` 행을 추가했다.
- layout contract가 README의 `문서 구조와 역할 분담` 구간을 읽고, `README.ko.md`를 제외한
  모든 draft 문서가 이 구간에서 설명되는지 검사하게 했다.

### 수정 후 점검

- 새 draft 문서가 생겼는데 README 역할 표에 빠지면 `test_cpp_framework_layout_contract`가
  실패한다.

## 추가 리뷰. Feature axis wildcard label 확장표 보강

### 발견한 위험 신호

- 실행 계획의 기능 축 추적표는 `http-client-*`, `connector-*`, `connector-unreal-*`,
  `framework-sample-*`처럼 wildcard label을 사용한다.
- 실제 CTest gate는 concrete label을 검사하지만, plan 안에서 wildcard가 어떤 concrete label을
  뜻하는지 한곳에 고정하지 않았다.
- wildcard 의미가 사람 머릿속에만 있으면 label을 추가하거나 이름을 바꿀 때 기능 축 추적표와
  `test_cpp_framework_label_contract`가 서로 다른 범위를 가리킬 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| wildcard 표현만 유지 | 표가 짧다 | concrete label 범위가 불명확하다 |
| 기능 축 표에서 wildcard를 모두 concrete label로 펼침 | 한 표만 보면 된다 | 표가 길어져 기능 축을 읽기 어렵다 |
| 기능 축 표는 유지하고 별도 wildcard 확장표를 둔 뒤 contract로 검사 | 표의 가독성과 exact label 증거를 모두 유지한다 | label 추가 때 확장표도 갱신해야 한다 |

선택은 세 번째 방식이다. 기능 축 표는 큰 범위를 보여 주고, wildcard 확장표는 CTest label
contract와 같은 concrete label 집합을 보여 주는 역할로 나누는 것이 더 명확하다.

### 적용한 리팩토링

- 실행 계획의 기능 축 추적표 아래에 wildcard label 확장표를 추가했다.
- layout contract가 `http-client-*`, `connector-*`, `connector-unreal-*`,
  `framework-sample-*` 확장 행을 검사하게 했다.

### 수정 후 점검

- wildcard label 의미가 plan에서 빠지거나 concrete label 목록과 어긋나면
  `test_cpp_framework_layout_contract`가 실패한다.

## 추가 리뷰. Wildcard label prefix coverage 보정

### 발견한 위험 신호

- `ctest --print-labels` 기준 실제 `connector-*` label에는 `connector-package`도 포함된다.
- 실제 `framework-sample-*` label에는 smoke/parity/e2e/log 외에도 sample 영역별
  `framework-sample-api`, `framework-sample-client`, `framework-sample-bingo` 같은 label이
  있다.
- wildcard 확장표가 prefix label 일부만 적으면 기능 축 추적표의 `*-*` 표현이 실제 CTest
  label prefix 전체가 아니라 임의의 부분집합처럼 보인다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| regression에 쓰는 일부 label만 확장표에 둠 | 표가 짧다 | wildcard가 prefix 전체를 뜻한다는 직관과 어긋난다 |
| wildcard 표현을 더 좁은 이름으로 바꿈 | 부분집합 의미가 분명해진다 | 기존 기능 축 표와 label taxonomy를 다시 바꿔야 한다 |
| 실제 prefix label 전체를 확장표와 label contract에 포함 | 문서와 CTest label taxonomy가 일치한다 | 표와 required label 목록이 길어진다 |

선택은 세 번째 방식이다. `*-*` wildcard를 쓰는 이상 실제 prefix label 전체를 문서화하고
non-empty contract로 고정하는 편이 오해가 적다.

### 적용한 리팩토링

- `connector-*` 확장표와 label contract에 `connector-package`를 추가했다.
- `framework-sample-*` 확장표와 label contract에 sample 영역별 concrete label을 추가했다.
- layout contract의 wildcard 확장 행도 같은 목록으로 갱신했다.

### 수정 후 점검

- 실제 sample/connector prefix label이 비거나 확장표에서 빠지면 label/layout contract가
  함께 실패한다.

## 추가 리뷰. Wildcard prefix label 자동 감시 보강

### 발견한 위험 신호

- `required_labels`는 concrete label을 직접 나열하므로 현재 목록은 정확하지만, 새
  `http-client-*`, `connector-*`, `connector-unreal-*`, `framework-sample-*` label이
  추가되어도 누락을 자동으로 발견하지 못했다.
- 실행 계획의 wildcard 확장표가 prefix 전체를 뜻한다면, CTest에 실제로 존재하는 prefix label도
  모두 non-empty contract에 들어와야 한다.
- 수동 목록만 유지하면 이전처럼 prefix label 일부가 문서와 contract에서 빠질 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 수동 목록만 유지 | 스크립트가 단순하다 | 새 prefix label 누락을 다시 놓칠 수 있다 |
| plan 문서를 파싱해 required label을 만든다 | 문서가 단일 기준이 된다 | CMake markdown parsing이 복잡해진다 |
| CTest `--print-labels`에서 wildcard prefix label을 수집해 required 목록 포함 여부를 검사 | 실제 빌드 label taxonomy를 직접 감시한다 | prefix 목록을 스크립트에 유지해야 한다 |

선택은 세 번째 방식이다. 이 contract의 목적은 CTest label이 비거나 required 목록에서 빠지는
회귀를 잡는 것이므로, 실제 CTest label 출력과 required 목록을 직접 비교하는 편이 가장 강하다.

### 적용한 리팩토링

- label contract가 `ctest --print-labels` 출력을 읽게 했다.
- `http-client-*`, `connector-*`, `connector-unreal-*`, `framework-sample-*` prefix label이
  `required_labels`에 없으면 실패하게 했다.

### 수정 후 점검

- 새 wildcard prefix label이 추가됐는데 label contract와 plan 확장표를 갱신하지 않으면
  `test_cpp_framework_label_contract`가 실패한다.

## 추가 리뷰. Unreal public header runtime include gate 보강

### 발견한 위험 신호

- Goal 22는 Unreal connector public header도 runtime implementation header를 include하지
  않아야 한다고 적는다.
- 기존 `public_headers_do_not_include_runtime` 검사는 `.hpp`만 순회했기 때문에 Unreal public
  header인 `ZLinkStreamConnector.h`를 일반 runtime include 검사에서 놓쳤다.
- 별도 문자열 검사로 일부 connector include는 막고 있었지만, Unreal `Private/` 구현 header를
  public header에서 include하는 회귀는 일반 규칙으로 잡지 못했다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| Unreal 전용 금지 문자열만 계속 추가 | 변경 범위가 작다 | public header 공통 규칙과 중복된다 |
| Unreal public header를 `.hpp`로 바꿈 | 기존 검사에 들어온다 | Unreal 관례와 generated header 흐름에 맞지 않는다 |
| public header runtime include 검사를 `.h`까지 확장하고 Unreal Public에도 적용 | Goal 22 문구와 검사 범위가 일치한다 | 검사 함수가 Unreal `Private/` 경로도 알아야 한다 |

선택은 세 번째 방식이다. public header 경계는 framework/connector/http-client/Unreal 모두에
공통으로 적용되는 규칙이므로, 파일 확장자 차이 때문에 검사 범위가 갈라지면 안 된다.

### 적용한 리팩토링

- `public_headers_do_not_include_runtime`가 `.hpp`와 `.h`를 모두 검사하게 했다.
- runtime include 금지 패턴에 `Private/` 구현 경로를 추가했다.
- Unreal connector `Public` 디렉터리에도 같은 검사를 적용했다.

### 수정 후 점검

- Unreal public header가 `Private/` 구현이나 `src/runtime` 구현을 include하면
  `test_cpp_framework_layout_contract`가 실패한다.

## 추가 리뷰. IDE configure preset contract 보강

### 발견한 위험 신호

- Goal 22는 CLion/Visual Studio configure smoke를 완료 기준에 둔다.
- tooling contract는 Linux Ninja와 Windows MSVC preset을 확인했지만, 실제 preset 파일에 있는
  `macos-ninja-debug`를 필수 목록으로 보지 않았다.
- CLion configure에 중요한 `CMAKE_EXPORT_COMPILE_COMMANDS`도 preset에는 있지만 contract에서
  고정하지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 preset 검사 유지 | 변경이 없다 | IDE configure에 필요한 일부 preset/cache drift를 놓친다 |
| 각 플랫폼 generator configure를 모두 실행 | 가장 직접적이다 | 현재 Linux 환경에서 Visual Studio/macOS generator를 실행할 수 없다 |
| preset 파일과 `cmake --list-presets=all` smoke에서 macOS preset과 compile commands option을 고정 | 현재 환경에서 실행 가능하고 IDE 진입점을 넓게 감시한다 | 실제 macOS/Windows configure는 해당 플랫폼 CI가 맡아야 한다 |

선택은 세 번째 방식이다. 이 저장소의 contract test는 현재 플랫폼에서 실행 가능한 smoke를
제공해야 하므로, cross-platform generator 실행 대신 preset 존재와 CMake 인식 여부를 강하게
고정한다.

### 적용한 리팩토링

- tooling contract가 `macos-ninja-debug` preset을 필수로 확인하게 했다.
- tooling contract가 `CMAKE_EXPORT_COMPILE_COMMANDS` 기본 ON 설정을 확인하게 했다.
- `cmake --list-presets=all` 출력에도 `macos-ninja-debug`가 있는지 확인하게 했다.

### 수정 후 점검

- macOS/CLion preset 또는 compile commands 설정이 빠지면 `test_cpp_framework_tooling_contract`가
  실패한다.

## 추가 리뷰. Installed extension target consumer coverage 보강

### 발견한 위험 신호

- Goal 22는 extension target naming과 dependency isolation을 완료 기준에 둔다.
- install consumer는 `zlink::framework_extension_metrics`만 링크했기 때문에, 나머지 extension
  alias target이 설치 package에서 소비 가능한지는 직접 확인하지 않았다.
- extension target이 모두 INTERFACE로 core에만 의존해야 한다면, 설치 consumer가 전부 링크해도
  Kafka/gRPC/YAML/FlatBuffers dependency를 끌어오지 않아야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| metrics extension만 유지 | consumer가 짧다 | extension package surface 대부분을 확인하지 못한다 |
| extension별 consumer test를 따로 만든다 | 실패 원인이 세분화된다 | package install/configure 시간이 늘고 중복이 크다 |
| 기존 install consumer가 모든 extension alias target을 링크 | 하나의 package smoke로 전체 installed target surface를 확인한다 | consumer CMakeLists가 길어진다 |

선택은 세 번째 방식이다. Goal 22의 package gate는 설치된 산출물을 소비하는 증거여야 하므로,
모든 extension target을 한 consumer에서 링크해 export와 dependency boundary를 함께 확인한다.

### 적용한 리팩토링

- install consumer가 11개 framework extension alias target을 모두 링크하게 했다.
- consumer 실행 코드가 `known_extensions()` 개수 11개를 확인하게 했다.

### 수정 후 점검

- extension alias target이 export에서 빠지거나 불필요한 외부 dependency를 요구하면
  `test_cpp_framework_install_consumer`가 configure/build 단계에서 실패한다.

## 추가 리뷰. Installed package export target file gate 보강

### 발견한 위험 신호

- install consumer는 target을 링크하므로 큰 누락은 configure 단계에서 잡지만, 어떤 export
  target 파일이 어떤 target을 제공해야 하는지 직접 설명하지 않았다.
- `zlink_framework_cppTargets.cmake`와 `zlink_stream_connector_cppTargets.cmake` 중 하나가
  설치되지 않거나, target 일부가 잘못된 export에 들어가도 실패 메시지가 간접적일 수 있다.
- Goal 22는 framework, connector, HTTP client, extension boundary를 package 사용성까지
  고정해야 하므로 export 파일 자체의 expected target 목록도 증거가 되어야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| consumer link 실패에만 의존 | 중복 검사가 없다 | 어떤 export 파일이 깨졌는지 알기 어렵다 |
| CMake export 파일 전체를 snapshot으로 비교 | drift를 가장 강하게 잡는다 | CMake 버전 차이에 취약하다 |
| export target 파일 존재와 expected target 문자열만 검사 | 산출물 경계를 직접 확인하고 CMake 생성 세부에는 덜 민감하다 | link consumer와 일부 중복된다 |

선택은 세 번째 방식이다. package gate는 설치 산출물의 공개 target taxonomy를 확인해야 하지만,
CMake가 생성하는 파일 전체 형식까지 고정할 필요는 없다.

### 적용한 리팩토링

- install consumer가 framework/stream connector package target 파일 존재를 검사하게 했다.
- framework package export에 framework, HTTP client, 11개 extension target이 있는지 확인하게 했다.
- stream connector package export에 connector와 codec helper target이 있는지 확인하게 했다.

### 수정 후 점검

- export target 파일이 빠지거나 기대 target이 누락되면 consumer configure 전에
  `test_cpp_framework_install_consumer`가 명확한 메시지로 실패한다.

## 추가 리뷰. Installed package config boundary gate 보강

### 발견한 위험 신호

- install consumer는 `find_package`와 target link를 실행하지만, package config 파일이
  어떤 dependency와 target export를 포함해야 하는지 직접 검증하지 않았다.
- framework package config가 stream connector target export를 포함하거나, stream connector
  package config가 framework target export를 포함하면 두 package의 독립 배포 경계가 흐려진다.
- `Threads`와 JSON dependency는 installed target link interface가 기대하는 공개 dependency다.
  config 파일에서 빠지면 consumer configure 실패가 link 단계의 간접 오류로 드러난다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| consumer configure 실패에만 의존 | 검사가 짧다 | dependency 누락과 package 경계 섞임의 원인이 불명확하다 |
| generated config 파일 전체를 snapshot으로 비교 | drift를 강하게 잡는다 | CMake package boilerplate 변경에 취약하다 |
| config 파일의 필수 dependency와 금지된 target include만 검사 | 공개 package 계약을 직접 고정하면서 생성 세부에는 덜 민감하다 | 검사 문자열을 package 구조 변경 때 갱신해야 한다 |

선택은 세 번째 방식이다. package config는 소비자 진입점이므로 dependency와 package 경계를
명확히 증명해야 하지만, CMake가 생성하는 부수 형식 전체를 계약으로 만들 필요는 없다.

### 적용한 리팩토링

- install consumer가 framework와 stream connector `Config.cmake` 파일 존재를 검사하게 했다.
- framework config가 `Threads`, `nlohmann_json CONFIG`, core C++ target export,
  framework target export를 포함하는지 확인하게 했다.
- stream connector config가 `Threads`, `nlohmann_json`, core C++ target export,
  stream connector target export를 포함하는지 확인하게 했다.
- framework config에는 stream connector target export가 없고, stream connector config에는
  framework target export가 없는지 확인하게 했다.

### 수정 후 점검

- package config dependency가 누락되거나 두 package target export가 서로 섞이면
  `test_cpp_framework_install_consumer`가 consumer configure 전에 실패한다.

## 추가 리뷰. CTest known label taxonomy gate 보강

### 발견한 위험 신호

- label contract는 plan에 등장하는 주요 label이 비어 있지 않은지는 확인했다.
- 그러나 CTest에 새 label이 추가될 때 그 label이 의도된 taxonomy인지 직접 제한하지 않았다.
  오타 label이나 임시 label이 추가되어도 기존 required label만 살아 있으면 통과할 수 있다.
- plan의 검증 명령은 label을 실행 단위로 사용하므로, label 이름 자체가 drift하면 문서와
  테스트 선택자의 연결이 약해진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| required label non-empty 검사만 유지 | 유지 비용이 없다 | 새 오타 label이나 임시 label 유입을 잡지 못한다 |
| CTest 출력 전체를 snapshot으로 비교 | label drift를 가장 강하게 잡는다 | test 순서와 CTest 출력 형식 변화에 취약하다 |
| 알려진 label taxonomy allowlist를 두고 실제 label 전체를 검사 | 의도하지 않은 label 유입을 잡고 출력 형식에는 덜 민감하다 | 새 label을 의도적으로 추가할 때 allowlist도 갱신해야 한다 |

선택은 세 번째 방식이다. CTest label은 plan의 검증 언어이므로 알려진 taxonomy에 속해야
하지만, CTest 출력 전체 형식까지 문서 계약으로 만들 필요는 없다.

### 적용한 리팩토링

- `verify_ctest_label_contract.cmake`에 `known_labels` allowlist를 추가했다.
- required label과 기능별 세부 label을 모두 알려진 label로 등록했다.
- coverage build에서만 나타나는 `framework-coverage`는 coverage label 기대값이 켜졌을 때만
  allowlist와 required 목록에 포함되게 했다.
- `ctest --print-labels`의 모든 실제 label이 `known_labels` 안에 없으면 실패하게 했다.

### 수정 후 점검

- CTest에 오타 label이나 문서화되지 않은 임시 label이 추가되면
  `test_cpp_framework_label_contract`가 실패한다.

## 추가 리뷰. Goal 20 connector package command gate 보강

### 발견한 위험 신호

- implementation plan의 wildcard 표와 실제 CTest label에는 `connector-package`가 있다.
- 그러나 Goal 20 검증 명령 블록은 connector unit, integration, e2e, contract, protocol,
  transport, typed label만 실행하고 package label을 빠뜨렸다.
- connector package는 framework package와 독립 배포 경계를 검증하므로 Goal 20 완료 기준의
  일부다. 검증 명령에서 빠지면 문서를 따라 실행해도 package 회귀를 놓칠 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| wildcard 표만 유지 | 표는 정확하다 | Goal 20 실행 명령을 그대로 따르는 사용자는 package 검증을 놓친다 |
| 최종 Goal 22 package 검증에만 의존 | 중복 실행이 줄어든다 | connector 자체 goal 완료 기준과 package 경계가 분리된다 |
| Goal 20 명령에 `connector-package`를 추가하고 layout contract로 고정 | goal별 검증 명령과 label taxonomy가 일치한다 | plan 명령 목록이 한 줄 늘어난다 |

선택은 세 번째 방식이다. Goal 20은 connector 자체의 public/package 경계를 다루므로,
해당 goal의 검증 명령이 connector package label을 직접 포함해야 한다.

### 적용한 리팩토링

- Goal 20 검증 명령 블록에 `ctest --test-dir framework/languages/cpp/build -L connector-package`
  명령을 추가했다.
- layout contract가 Goal 20 검증 블록 안에 모든 connector/unreal connector concrete label
  명령이 있는지 확인하게 했다.

### 수정 후 점검

- Goal 20 검증 명령에서 connector package label이 빠지면
  `test_cpp_framework_layout_contract`가 실패한다.

## 추가 리뷰. Goal 21 sample role command gate 보강

### 발견한 위험 신호

- implementation plan의 `framework-sample-*` wildcard 표는 sample role label을 모두
  concrete label로 나열한다.
- Goal 21 검증 명령은 smoke, parity, e2e, log만 실행했다. 실제 CTest에는
  `framework-sample-api`, `framework-sample-play`, `framework-sample-registry`,
  `framework-sample-session`, `framework-sample-client-e2e` 같은 역할별 label이 존재한다.
- Goal 21 완료 기준은 샘플의 역할 분리와 client/server e2e를 검증해야 하므로, 검증 명령이
  역할별 label을 직접 드러내야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 smoke/e2e/log 명령만 유지 | 중복 실행이 적다 | 역할별 label이 문서 검증 명령에서 보이지 않는다 |
| wildcard 표만 근거로 둔다 | 표는 간결하다 | Goal 21 명령을 따라 실행하면 역할별 selector를 놓친다 |
| Goal 21 명령에 모든 sample concrete label을 추가하고 layout contract로 고정 | 문서 명령과 sample label taxonomy가 일치한다 | 검증 명령 목록이 길어진다 |

선택은 세 번째 방식이다. Goal 21은 샘플 역할 분리 자체를 완료 기준으로 삼으므로,
검증 명령도 역할별 CTest label을 명시해야 한다.

### 적용한 리팩토링

- Goal 21 검증 명령 블록에 sample role concrete label 명령을 추가했다.
- layout contract가 Goal 21 검증 블록에 모든 sample concrete label 명령이 있는지 확인하게 했다.

### 수정 후 점검

- Goal 21 검증 명령에서 sample role label이 빠지면
  `test_cpp_framework_layout_contract`가 실패한다.

## 추가 리뷰. Goal 22 final label axis command gate 보강

### 발견한 위험 신호

- Goal 22 완료 기준은 CTest label 전체 통과와 Goal 1-22 완료 기준 충족을 요구한다.
- 최종 검증 명령은 full CTest를 실행하지만, 기능 축 추적표에서 독립 축으로 둔
  connector, Unreal connector, sample role label 일부를 직접 명령으로 드러내지 않았다.
- full CTest가 통과해도 문서 명령 블록만 보면 어떤 독립 label 축을 최종 감사에서 확인해야
  하는지 약해진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| full CTest 명령만 최종 근거로 둔다 | 명령 목록이 짧다 | 기능 축별 audit 증거가 문서에서 보이지 않는다 |
| 각 Goal의 검증 명령에만 의존한다 | 중복을 줄인다 | Goal 22 단독 실행 시 독립 축 명령을 놓칠 수 있다 |
| Goal 22 명령에 독립 label 축을 명시하고 layout contract로 고정 | 최종 audit 문서와 CTest taxonomy가 일치한다 | 검증 명령 목록이 길어진다 |

선택은 세 번째 방식이다. Goal 22는 최종 audit이므로 full CTest 외에도 독립 public/package
축을 명시적으로 실행할 수 있어야 한다.

### 적용한 리팩토링

- Goal 22 검증 명령에 sample role, connector, Unreal connector concrete label 명령을 추가했다.
- layout contract가 Goal 22 검증 블록에 최종 label 축 명령이 있는지 확인하게 했다.

### 수정 후 점검

- Goal 22 검증 명령에서 connector, Unreal connector, sample role, package, coverage 축이
  빠지면 `test_cpp_framework_layout_contract`가 실패한다.

## 추가 리뷰. Stream Connector JSON helper option 제거

### 발견한 위험 신호

- CMake에는 `ZLINK_STREAM_CONNECTOR_WITH_JSON` option이 있었지만 JSON helper target과
  install include는 항상 켜져 있었다.
- 옵션을 OFF로 바꿔도 public helper 표면이 실제로 사라지지 않으면 사용자는 설정 의미를
  잘못 이해하게 된다.
- POSD 관점에서는 동작을 바꾸지 않는 설정 파라미터가 호출자 복잡성만 늘리는 얕은 표면이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 옵션을 유지하고 문서만 둔다 | 변경이 작다 | 가짜 설정 표면이 계속 남는다 |
| 옵션 OFF일 때 JSON helper target과 install include까지 조건부로 만든다 | option 의미가 생긴다 | 기본 helper를 끄는 경로까지 package/test matrix가 늘어난다 |
| JSON helper는 항상 포함되는 기본 helper로 정리하고 option을 제거한다 | 실제 동작과 문서가 일치하고 설정 표면이 줄어든다 | 기존 preset의 JSON cache variable을 삭제해야 한다 |

선택은 세 번째 방식이다. 현재 connector 완료 기준은 JSON helper 기본 포함을 요구하며,
사용하지 않는 dependency 분리는 `zlink::stream_connector_codecs` target과 MessagePack,
Protobuf option으로 충분하다.

### 적용한 리팩토링

- `ZLINK_STREAM_CONNECTOR_WITH_JSON` CMake option과 preset cache variable을 제거했다.
- connector draft는 JSON helper가 별도 option 없이 기본 포함된다고 설명하게 했다.
- layout contract가 제거된 JSON option이 CMakeLists나 preset에 다시 들어오면 실패하게 했다.

### 수정 후 점검

- 의미 없는 JSON helper option이 재도입되면 `test_cpp_framework_layout_contract`가 실패한다.

## 추가 리뷰. Stream Connector optional codec effective flag 보강

### 발견한 위험 신호

- MessagePack과 Protobuf helper는 build option이 켜지고 해당 C++ binding codec target이
  있을 때만 연결된다고 문서화되어 있다.
- CMake link 조건은 option과 target 존재를 함께 봤지만, runtime compile definition은 option만
  봤다. option이 ON이고 target이 없으면 runtime은 codec을 enabled로 볼 수 있었다.
- 이 상태는 public `supports(codec_t::message_pack)` 의미와 실제 helper dependency 상태가
  갈라지는 정보 누출이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| option만 runtime enabled 조건으로 유지 | 설정이 단순하다 | target이 없어도 enabled로 보이는 오구현이 남는다 |
| option ON인데 target이 없으면 configure 실패 | 누락 의존성을 즉시 알린다 | optional helper를 끈 기본 개발 흐름과 다르게 패키지 설치를 강제할 수 있다 |
| effective flag를 만들어 option과 target 존재가 모두 참일 때만 runtime/link를 켠다 | 문서 의미와 runtime 의미가 일치한다 | CMake 변수가 두 개 늘어난다 |

선택은 세 번째 방식이다. MessagePack과 Protobuf는 선택 helper이므로 의존 target이 실제로
있을 때만 connector runtime과 helper target이 같은 enabled 상태를 가져야 한다.

### 적용한 리팩토링

- `ZLINK_STREAM_CONNECTOR_MESSAGEPACK_ENABLED`와
  `ZLINK_STREAM_CONNECTOR_PROTOBUF_ENABLED` effective flag를 추가했다.
- runtime compile definition과 `zlink::stream_connector_codecs` link 조건을 같은 effective
  flag로 묶었다.
- layout contract가 optional codec effective flag 패턴을 확인하게 했다.

### 수정 후 점검

- option만 켜고 binding codec target이 없는 상태에서 runtime codec support가 켜지는 회귀는
  layout contract가 CMake 조건 drift로 잡는다.

## 추가 리뷰. Optional codec package export dependency 보강

### 발견한 위험 신호

- `ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK=ON`과
  `ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF=ON` 구성에서 binding codec target은 build tree에
  생기지만 `zlink_cppTargets` export set에는 들어가지 않았다.
- 그 상태에서 `zlink::stream_connector_codecs`가 optional codec target을 참조하면 install
  export 생성 또는 installed consumer configure가 실패한다.
- 설치된 config 파일은 optional codec dependency와 stream connector OpenSSL dependency를
  `find_dependency`로 복원하지 않았다.
- `find_dependency`가 다른 package config를 읽은 뒤 `PACKAGE_PREFIX_DIR`를 바꿀 수 있어,
  `zlink-native` imported location이 설치 prefix가 아니라 `/usr` 같은 다른 prefix를 가리킬
  수 있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| optional codec target link를 제거한다 | install export 오류가 사라진다 | helper target이 optional codec dependency를 제공한다는 문서 의미가 약해진다 |
| optional codec option ON이면 install packaging을 지원하지 않는다고 문서화한다 | 구현 변경이 작다 | Goal 20 package boundary 완료 기준과 충돌한다 |
| binding codec targets를 export/install하고 package config dependency를 복원한다 | optional codec helper가 build/install consumer 모두에서 같은 의미를 가진다 | binding CMake와 framework config template을 함께 고쳐야 한다 |

선택은 세 번째 방식이다. optional codec은 connector package 표면의 일부이므로 build tree에서만
동작하고 install consumer에서 깨지는 상태를 허용하면 안 된다.

### 적용한 리팩토링

- binding C++ codec interface target을 `zlink_cppTargets` export set에 포함하고 codec include를
  install하도록 했다.
- framework와 stream connector package config가 optional MessagePack/Protobuf dependency를
  `find_dependency`로 복원하게 했다.
- stream connector package config가 OpenSSL dependency를 복원하게 했다.
- framework와 stream connector package config가 native runtime prefix를 별도 변수로 저장해
  `find_dependency` 이후에도 같은 설치 prefix를 사용하게 했다.
- layout contract가 optional codec export/config dependency 패턴을 확인하게 했다.

### 수정 후 점검

- optional codec option을 켠 build에서 `test_cpp_stream_connector`, install, installed
  consumer configure/build가 모두 통과해야 한다.

## 추가 리뷰. Stream Connector endpoint scheme mismatch coverage 보강

### 발견한 위험 신호

- Goal 20은 unsupported transport와 endpoint scheme mismatch validation을 완료 기준으로 둔다.
- connector runtime은 transport별 endpoint scheme을 검증하지만, 테스트는 WSS transport에 TCP
  endpoint를 넣는 한 조합만 확인했다.
- TCP, TLS, WebSocket, WebSocket over TLS가 같은 packet API로 동작하려면 각 transport가
  자기 endpoint scheme만 받아들이는 계약도 같은 수준으로 고정되어야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 WSS mismatch 테스트만 유지 | 변경이 작다 | 다른 transport의 scheme validation drift를 놓친다 |
| parser private 함수를 직접 테스트 | 실패 원인이 세분화된다 | runtime connect 계약보다 내부 함수에 테스트가 묶인다 |
| public connector connect 결과로 모든 transport mismatch를 표 기반 검증 | public 계약을 직접 검증하고 중복을 줄인다 | OpenSSL 없는 build의 TLS/WSS 기대 메시지를 분기해야 한다 |

선택은 세 번째 방식이다. 사용자는 parser가 아니라 connector connect 결과를 보므로,
public result의 configuration error와 message fragment를 transport별로 확인한다.

### 적용한 리팩토링

- `test_cpp_stream_connector`가 TCP, TLS, WebSocket, WebSocket over TLS scheme mismatch를
  표 기반으로 검증하게 했다.
- OpenSSL 없는 build에서는 TLS/WSS가 unsupported transport 메시지를 먼저 반환하는 점을
  테스트가 반영하게 했다.

### 수정 후 점검

- transport별 endpoint scheme validation이 빠지거나 잘못된 메시지를 반환하면
  `test_cpp_stream_connector`가 실패한다.

## 반복 POSD 재리뷰. HTTP route/query parse failure 회귀 테스트 보강

### 발견한 위험 신호

- HTTP 적용 가이드는 request validation 항목에 route parameter parse failure와 query parse
  failure를 포함한다.
- `test_cpp_framework_app_host`는 route parameter와 query string이 handler DTO로 들어가는 성공
  경로를 검증했지만, 잘못된 route/query 값이 decode 실패로 매핑되는지는 고정하지 않았다.
- 이 상태에서는 Goal 19의 HTTP handler e2e 테스트가 `zlink::http_client`를 사용하더라도
  request validation 실패 경로가 조용히 빠질 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 문자열 DTO 성공 테스트만 유지 | 변경이 없다 | parse failure 완료 기준을 회귀 테스트가 지키지 못한다 |
| runtime에서 route/query 값을 숫자와 boolean으로 전역 변환 | handler DTO가 typed scalar를 바로 받을 수 있다 | 기존 string DTO에서 `/games/1` 같은 값의 의미가 바뀔 수 있다 |
| typed DTO가 현재 문자열 binding을 명시적으로 파싱하게 하고 실패 매핑을 e2e로 검증 | 기존 public binding 의미를 유지한다 | 테스트 DTO에 파싱 코드가 조금 추가된다 |

선택은 세 번째 방식이다. framework runtime은 DTO 필드 타입을 알지 못하므로 route/query 문자열을
전역으로 추측 변환하면 호출자 관점의 예측 가능성이 낮아진다. 현재 contract는 serializer decode
단계의 실패를 `payload_decode_failed`로 모으는 것이므로, typed DTO에서 잘못된 route/query 값을
던지고 HTTP e2e가 400 응답을 확인하는 편이 더 작은 인터페이스를 유지한다.

### 적용한 리팩토링

- `number_http_handler_t`를 app host e2e에 추가해 `/numbers/{id}?page=...` 경로에서 route와
  query 값을 정수로 파싱하게 했다.
- `/numbers/not-a-number?page=2`와 `/numbers/41?page=bad` 호출이 `zlink::http_client` raw
  응답에서 400과 `payload_decode_failed`를 반환하는지 검증하게 했다.
- 정상 숫자 route/query 값도 typed reply로 확인해 성공 경로와 실패 경로가 같은 binding 흐름을
  지나가게 했다.

### 수정 후 점검

- route/query parse failure가 `payload_decode_failed` HTTP 400 매핑에서 벗어나면
  `test_cpp_framework_app_host`가 실패한다.

## 반복 POSD 재리뷰. HTTP middleware DI auth extension 회귀 테스트 보강

### 발견한 위험 신호

- application framework 문서는 middleware/filter가 DI를 사용할 수 있어야 하고, 초기 security
  범위는 auth filter extension point까지 둔다고 설명한다.
- HTTP runtime은 default 생성 middleware와 DI resolve middleware를 모두 지원하지만,
  app host e2e는 default 생성 middleware만 검증했다.
- 이 상태에서는 non-default middleware가 request scope에서 resolve되지 않거나 short-circuit
  response를 만들지 못해도 문서의 auth extension point 요구를 테스트가 놓칠 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 middleware 테스트만 유지 | 변경이 없다 | DI 기반 filter 요구가 증거 없이 남는다 |
| JWT/OAuth provider를 framework core에 추가 | 실제 auth 기능처럼 보인다 | 초기 범위가 아닌 provider 정책을 core에 끌어들인다 |
| auth-style middleware를 DI로 등록하고 token 정책 서비스 기반 short-circuit을 e2e로 검증 | extension point를 public API 증가 없이 고정한다 | 테스트용 middleware 타입이 하나 늘어난다 |

선택은 세 번째 방식이다. 문서가 요구하는 것은 구체 auth provider가 아니라 extension point이므로,
core는 middleware DI와 `http_context_t::json_response(...)` short-circuit 의미를 깊은 모듈로
제공하고 auth 정책 자체는 사용자 코드에 남겨야 한다.

### 적용한 리팩토링

- `auth_policy_t`와 non-default `auth_middleware_t`를 app host e2e에 추가했다.
- middleware를 `add_scoped<auth_middleware_t, auth_policy_t>()`로 등록하고
  `options.http().use<auth_middleware_t>()`가 request scope에서 resolve되게 했다.
- `/secure-games/{id}` 요청에서 올바른 `authorization` header는 handler까지 통과하고,
  누락된 token은 middleware가 401 JSON response로 short-circuit하는지 검증했다.

### 수정 후 점검

- HTTP middleware가 DI에서 resolve되지 않거나 auth-style middleware가 handler 호출을 건너뛰지
  못하면 `test_cpp_framework_app_host`가 실패한다.

## 반복 POSD 재리뷰. Goal 16 metrics hook public surface 보강

### 발견한 위험 신호

- Goal 16은 metrics/tracing hook을 구현 항목에 포함한다.
- tracing hook은 `monitoring_builder_t::on_trace(...)`와 monitoring 회귀 테스트가 고정하고
  있었지만, `metrics_builder_t::add_runtime_metrics()`는 문서 인터페이스 초안에만 있고 실제
  public surface에는 없었다.
- 이 상태에서는 health와 runtime event는 구현되어 있어도 metrics hook 요구가 문서에만 남아
  Goal 16 완료 증거가 약해진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| metrics 문구를 문서에서 제거 | 구현 변경이 없다 | Goal 16과 인터페이스 초안의 관찰 표면 요구를 축소한다 |
| exporter와 label schema까지 한 번에 추가 | 완성형 metrics 시스템에 가깝다 | 초기 core 범위를 넘어 provider 정책과 backend 결정을 끌어들인다 |
| `metrics_builder_t`와 typed `metric_event_payload_t`를 monitoring state 위에 얹는다 | public metrics hook을 제공하면서 exporter 결정은 숨긴다 | runtime metric publish helper가 최소 기능으로 시작한다 |

선택은 세 번째 방식이다. metrics는 운영 event stream의 일부이므로 monitoring handler와 trace
hook 순서를 재사용하면 public API를 작게 유지하면서도 문서의 core 관찰 표면 요구를 충족한다.
exporter, label schema, backend adapter는 extension이 맡을 수 있게 남겨 둔다.

### 적용한 리팩토링

- `metric_event_payload_t`와 `metrics_builder_t`를 public eventing contract에 추가했다.
- `app.metrics().add_runtime_metrics()`와 `record_runtime_metric(...)`가 monitoring state를
  공유해 typed handler와 trace hook을 통과하게 했다.
- `test_cpp_framework_monitoring`이 runtime metric event payload, tag, trace 호출을 함께
  검증하게 했다.

### 수정 후 점검

- metrics hook이 public app surface에서 사라지거나 metric event가 monitoring handler/trace
  hook을 통과하지 않으면 `test_cpp_framework_monitoring`이 실패한다.

## 반복 POSD 재리뷰. Goal 17 JSON codec 선언 순서 회귀 테스트 보강

### 발견한 위험 신호

- Goal 17 완료 기준은 `options.codecs().add_json()`이 JSON codec 사용만 선언하고 message type을
  모두 나열하지 않아야 한다고 요구한다.
- 기존 module/hosted service 테스트는 handler를 먼저 등록하고 나중에 `add_json()`을 호출하는
  순서만 검증했다.
- 이 상태에서는 사용자가 JSON codec을 먼저 선언한 뒤 handler를 추가할 때 request/reply
  serializer가 자동 설치되지 않는 회귀를 놓칠 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 단일 순서 테스트만 유지 | 변경이 없다 | 선언형 codec 의미의 절반만 검증한다 |
| handler type마다 serializer를 직접 등록하도록 문서화 | 구현이 단순하다 | Goal 17의 "message type을 모두 나열하지 않는다" 기준과 충돌한다 |
| `add_json()` 전후 handler 등록 순서를 모두 e2e로 검증 | public 사용 의미를 고정한다 | 테스트 DTO와 handler가 하나 늘어난다 |

선택은 세 번째 방식이다. JSON codec 선언은 handler 등록 순서와 독립적이어야 호출자가 serializer
등록 순서를 외우지 않아도 된다. 이는 codec builder가 복잡성을 아래로 숨긴다는 POSD 기준에도
맞다.

### 적용한 리팩토링

- `test_cpp_framework_module_hosted`에 `add_json()` 이후 등록되는 late handler를 추가했다.
- 같은 channel/group에서 기존 handler와 late handler를 모두 invoke해 request/reply JSON
  serializer가 자동 설치되는지 검증했다.

### 수정 후 점검

- `options.codecs().add_json()` 호출 뒤 추가된 handler의 serializer가 자동 설치되지 않으면
  `test_cpp_framework_module_hosted`가 실패한다.

## 반복 POSD 재리뷰. Goal 18 coroutine/callback submit 회귀 테스트 보강

### 발견한 위험 신호

- Goal 18과 HTTP client 문서는 public 호출이 call object를 만든 뒤 `submit(callback)` 또는
  `co_await submit<T>()`으로 실행되어야 한다고 요구한다.
- 기존 HTTP client 테스트는 `submit<T>().result()`와 callback 성공 경로를 검증했지만,
  실제 coroutine 안에서 `co_await submit<T>()`를 사용하는 경로는 직접 고정하지 않았다.
- callback도 성공 경로만 확인해 decode error가 callback result로 전달되는지 증거가 약했다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 `.result()` 테스트만 유지 | 변경이 없다 | coroutine submit 완료 기준을 간접 증거에 의존한다 |
| 별도 async runtime을 도입해 HTTP client 테스트를 재구성 | runtime thread blocking 여부까지 더 강하게 볼 수 있다 | 현재 `task_t` contract보다 큰 테스트 인프라를 만든다 |
| 기존 loopback HTTP server 위에 작은 coroutine helper와 callback failure 검증을 추가 | public API 요구를 좁게 고정한다 | 내부 nonblocking 구현까지 증명하지는 않는다 |

선택은 세 번째 방식이다. Goal 18의 public contract는 call object가 coroutine과 callback 양쪽에서
같은 result surface를 제공하는 것이다. 이를 기존 HTTP client regression suite 안에서 직접
검증하면 테스트 범위를 키우지 않고 문서 요구를 더 강하게 고정할 수 있다.

### 적용한 리팩토링

- `test_cpp_http_client`에 `co_await client.post(...).submit<T>()`를 사용하는 coroutine helper를
  추가했다.
- callback submit이 invalid JSON decode failure를 `payload_decode_failed` result로 전달하는지
  검증했다.

### 수정 후 점검

- typed submit이 coroutine await에서 깨지거나 callback failure result가 누락되면
  `test_cpp_http_client`가 실패한다.

## 반복 POSD 재리뷰. Goal 20 Stream Connector coroutine submit 회귀 테스트 보강

### 발견한 위험 신호

- Goal 20은 Stream Connector call object가 callback submit과 coroutine submit을 모두 제공해야
  한다고 요구한다.
- 기존 connector 테스트는 `submit().result()`와 callback request/send 경로를 넓게 검증했지만,
  실제 coroutine 안에서 `co_await send.submit()`와 `co_await request.submit()`를 사용하는
  public 경로는 직접 고정하지 않았다.
- 이 상태에서는 connector `task_t` await contract가 깨져도 대부분의 regression이 blocking
  result 호출로만 통과할 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 `.result()`와 callback 테스트만 유지 | 변경이 없다 | coroutine submit 완료 기준을 간접 증거에 의존한다 |
| connector task 구현을 framework `task_t`로 합친다 | coroutine 모델이 하나로 줄어든다 | connector 독립 산출물 경계와 public task contract를 크게 흔든다 |
| 기존 loopback stream socket 위에 send/request coroutine helper를 추가 | public coroutine 경로를 좁게 고정한다 | 내부 scheduler까지 검증하지는 않는다 |

선택은 세 번째 방식이다. Goal 20의 요구는 connector public call object가 coroutine 문법으로도
같은 packet API를 제공하는 것이므로, 기존 transport regression 안에 실제 `co_await` 호출을
넣는 것이 가장 작은 증거 보강이다.

### 적용한 리팩토링

- `test_cpp_stream_connector`에 `connector.send(...).submit()` helper와
  `co_await connector.request(...).submit<T>()` helper를 추가했다.
- loopback stream socket이 coroutine send와 request frame을 모두 수신하고 request reply를
  반환하는지 검증했다.

### 수정 후 점검

- Stream Connector send submit 또는 request submit await 경로가 깨지면 `test_cpp_stream_connector`가
  실패한다.

## 반복 POSD 재리뷰. Goal 21 sample parity 회귀 테스트 보강

### 발견한 위험 신호

- Goal 21은 샘플 클라이언트가 실제 서버 프로세스와 HTTP client, Stream Connector를 사용하고
  서버 handler를 직접 호출하지 않아야 한다고 요구한다.
- 기존 parity 테스트는 TicTacToe client가 `zlink::http_client`로 `POST /games`를 호출하는지는
  확인했지만, client 디렉터리가 나중에 server handler header를 직접 include하는 회귀는 막지
  못했다.
- DTO serializer hook 밖에서 JSON field를 직접 읽는 코드도 자동으로 막지 못했다. 이 경우
  샘플 애플리케이션이 contract type 대신 JSON 구조에 직접 결합되어 호출자 복잡성이 다시
  올라간다.
- public sample 이름에 variant suffix가 다시 붙는 회귀도 CMake target smoke만으로는 드러나지
  않는다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 수동 리뷰만 유지 | 코드 변경이 없다 | 문서 완료 기준을 매번 사람이 기억해야 한다 |
| 샘플 client 구현을 더 감싸는 facade를 추가 | include 경로를 줄일 수 있다 | 현재 클라이언트 구조보다 깊은 모듈이 되지 않고 표면만 늘어난다 |
| parity 테스트가 client/server 경계와 JSON 접근 위치를 스캔 | 문서 요구를 자동 회귀 조건으로 만든다 | 문자열 기반 구조 검증이므로 의도적인 새 예외가 생기면 테스트를 갱신해야 한다 |

선택은 세 번째 방식이다. 샘플의 핵심 요구는 새 abstraction보다 public 경계가 흐려지지 않는지를
지속적으로 검증하는 것이다. 테스트가 client와 server handler 경계, JSON serializer 경계, sample
이름 경계를 고정하면 호출자 관점의 단순한 샘플 구조가 유지된다.

### 적용한 리팩토링

- `test_cpp_framework_sample_parity`에 sample source 순회 helper를 추가했다.
- client sample 파일이 `Server` 또는 `Handlers` 경로를 직접 참조하지 않는지 검증했다.
- `Shared/Contracts` 아래 DTO serializer hook을 제외한 sample code에서 `nlohmann::json::parse`,
  `json.at`, `json[]` 접근이 나오지 않도록 검증했다.
- public sample directory가 `Bingo`, `TicTacToe` 이름만 유지하는지 검증했다.

### 수정 후 점검

- sample client가 server handler를 직접 include하거나 DTO serializer 밖에서 JSON field에 결합되면
  `test_cpp_framework_sample_parity`가 실패한다.

## 반복 POSD 재리뷰. Goal 22 extension public header boundary 보강

### 발견한 위험 신호

- Goal 22는 extension target naming, dependency isolation, Kafka/gRPC bridge boundary,
  FlatBuffers/YAML/custom codec boundary를 완료 기준에 둔다.
- 기존 layout contract는 CMake target이 extension dependency를 core framework에 링크하지 않는지
  검사했지만, `extensions/include` public header 자체는 runtime include와 외부 dependency 노출
  검사 범위에 들어 있지 않았다.
- extension public header가 나중에 Kafka, gRPC, YAML, FlatBuffers header나 runtime detail을 직접
  include해도 core target link 검사는 통과할 수 있다. 이 경우 extension boundary가 public
  contract에서 새어 나가고, 사용자는 선택하지 않은 외부 SDK를 include 단계에서 요구받는다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| CMake target 경계 검사만 유지 | 현재 테스트가 짧다 | public header dependency 누출을 놓친다 |
| extension header를 전부 opaque forward declaration으로 바꿈 | 외부 dependency 노출 가능성이 작다 | 현재 policy/value type 사용성을 불필요하게 줄인다 |
| 기존 public header layout 검사와 compile coverage 범위에 `extensions/include`를 추가 | core와 extension 경계를 같은 규칙으로 검증한다 | forbidden dependency 목록을 extension SDK까지 명시해야 한다 |

선택은 세 번째 방식이다. extension은 별도 target이지만 사용자가 include하는 public contract이므로
framework, connector, HTTP client public header와 같은 leakage gate를 통과해야 한다. 이렇게 하면
확장 기능의 선택 dependency가 core나 다른 extension 사용자에게 전파되지 않는다.

### 적용한 리팩토링

- contract header smoke가 `zlink/framework/extensions.hpp`와
  `zlink/framework/extensions/extension_boundaries.hpp`를 직접 include하게 했다.
- contract header smoke target이 extension public include directory를 실제로 compile하도록
  framework extension target을 링크하게 했다.
- layout contract의 public header runtime include 검사 범위에 `extensions/include`를 추가했다.
- public header dependency 금지 목록에 Kafka, gRPC, YAML, FlatBuffers SDK include/type 패턴을
  추가하고, 같은 검사를 `extensions/include`에도 적용했다.
- extension public headers도 direct compile coverage 검사 대상에 넣었다.

### 수정 후 점검

- extension public header가 runtime detail이나 Kafka/gRPC/YAML/FlatBuffers SDK 타입을 노출하거나
  contract compile smoke에서 빠지면 `test_cpp_framework_layout_contract` 또는
  `test_cpp_framework_contract_headers`가 실패한다.

## 반복 POSD 재리뷰. Goal 22 sample monitoring event evidence 보강

### 발견한 위험 신호

- Goal 22 완료 기준은 sample e2e가 server file log와 monitoring event를 확인해야 한다고
  요구한다.
- 기존 sample e2e는 `monitor stream ready` 로그를 readiness 대기 조건과 검증 항목으로 함께
  사용했다. 이 문자열은 프로세스가 준비됐다는 신호인지 monitoring event 증거인지 의미가
  섞여 있었다.
- 의미가 섞인 로그는 샘플 검증을 통과시키지만, 문서의 monitoring event 완료 기준을 강하게
  증명하지 못한다. 호출자는 sample log에서 어떤 항목이 관찰 이벤트인지 구분하기 어렵다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 `monitor stream ready`만 유지 | 변경이 없다 | readiness와 monitoring event 의미가 계속 섞인다 |
| sample e2e 서버를 full framework monitoring runtime으로 재구성 | 실제 runtime monitoring과 가장 가깝다 | client/process e2e의 목적보다 큰 서버 구성이 된다 |
| readiness 로그와 별도 `monitor event` 로그를 남기고 CTest가 둘 다 확인 | 완료 기준을 명확히 고정한다 | 로그 항목이 하나 늘어난다 |

선택은 세 번째 방식이다. sample e2e의 목적은 실제 client/server 흐름을 검증하면서 사용자가
로그로 동작을 리뷰할 수 있게 하는 것이다. readiness와 monitoring event를 분리하면 테스트
증거가 명확해지고, 샘플 서버 구현은 여전히 작게 유지된다.

### 적용한 리팩토링

- Bingo와 TicTacToe sample e2e stream server가 `monitor event stream_ready` 로그를 남기게 했다.
- process e2e log 검증의 `EXPECTED_CONTAINS`에 `monitor event stream_ready`를 추가했다.
- 샘플 README의 server log 설명을 monitoring event까지 포함하도록 맞췄다.

### 수정 후 점검

- sample e2e server log에서 monitoring event 항목이 빠지면 `framework-sample-log`,
  `framework-sample-e2e`, `framework-sample-process-e2e` 라벨이 실패한다.

## 반복 POSD 재리뷰. Goal 22 sample README target alignment 보강

### 발견한 위험 신호

- Goal 22는 public docs and sample code alignment를 완료 기준에 둔다.
- 기존 layout contract는 draft 문서 추적표와 draft README 역할표를 검사하지만, sample README가
  CMake의 public sample executable 목록과 계속 맞는지는 직접 확인하지 않았다.
- 이 상태에서는 sample target 이름이나 역할이 바뀌어도 README가 낡은 실행 파일 이름을
  설명하거나, 내부 e2e server target을 public sample처럼 설명하는 회귀를 놓칠 수 있다.
- 최근 sample log 검증은 monitoring event까지 강해졌지만 README가 그 증거를 계속 설명하는지도
  자동으로 고정되어 있지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| README는 수동 리뷰로만 확인 | 테스트 변경이 없다 | public docs/sample alignment 완료 기준이 약하다 |
| README를 CMake에서 생성 | drift를 없앨 수 있다 | 문서 설명 문맥과 한국어 작성 품질을 템플릿에 묶는다 |
| sample parity test가 README의 public target 목록과 log evidence 문구를 검사 | 현재 문서 구조를 유지하면서 drift를 막는다 | target 목록을 테스트에 명시해야 한다 |

선택은 세 번째 방식이다. sample README는 사용자가 직접 읽는 public 문서이므로 생성물보다
설명형 문서로 유지하는 편이 낫다. 대신 parity test가 CMake target과 README target 설명,
server log evidence 문구를 함께 확인해 문서와 샘플 코드가 같은 표면을 가리키도록 고정한다.

### 적용한 리팩토링

- `test_cpp_framework_sample_parity`에 sample README alignment 테스트를 추가했다.
- Bingo와 TicTacToe README가 public sample executable target 5개를 모두 설명하는지 확인한다.
- README가 내부 `_e2e_server` target을 public sample executable로 설명하지 않는지 확인한다.
- README가 server log 파일과 monitoring event, receive/reply/push 증거를 설명하는지 확인한다.

### 수정 후 점검

- sample target 이름, README 실행 파일 목록, server log evidence 설명이 서로 어긋나면
  `test_cpp_framework_sample_parity`가 실패한다.

## 반복 POSD 재리뷰. Goal 22 public codec SDK leakage gate 보강

### 발견한 위험 신호

- Goal 22는 public header에 codec 외부 타입이 불필요하게 노출되지 않아야 한다고 요구한다.
- 기존 public header dependency gate는 Boost, OpenSSL, test/logging library, Kafka/gRPC/YAML/
  FlatBuffers 노출을 막았지만 nlohmann/json, MessagePack, Protobuf SDK 타입 노출은 직접 금지하지
  않았다.
- JSON 구현은 `zlink/codec/json.hpp`가 소유하지만, framework, HTTP client, connector,
  extension public header가 `nlohmann::json`, `msgpack::...`, `google::protobuf` 타입을 직접
  signature나 include로 노출하면 codec 경계가 새어 나간다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현재 금지 목록만 유지 | 변경이 작다 | codec SDK 타입 public 노출 회귀를 놓친다 |
| JSON helper에서도 nlohmann include를 제거 | public dependency가 가장 작아진다 | 현재 template JSON encode/decode 표면과 binding codec 구조를 크게 바꾼다 |
| 상위 public header에서 codec SDK 타입 직접 노출만 금지 | codec helper 소유권을 유지하면서 framework 표면을 보호한다 | dedicated codec header 자체는 별도 소유자로 남는다 |

선택은 세 번째 방식이다. 현재 구조에서 JSON codec helper는 `message_t::from_json`과
`parse_json<T>()` template 구현을 맡는 명확한 owner다. POSD 기준으로는 이 helper 바깥의 public
framework 표면이 외부 codec SDK 타입에 결합되지 않는지를 검증하는 것이 더 직접적인 경계다.

### 적용한 리팩토링

- layout contract의 public header dependency 금지 목록에 `#include <nlohmann`, `nlohmann::`,
  `#include <msgpack`, `msgpack::`, `#include <google/protobuf`, `google::protobuf`를 추가했다.
- 기존 검사 범위인 framework, connector, HTTP client, extension, Unreal public header에 같은
  codec SDK leakage gate가 적용된다.

### 수정 후 점검

- 상위 public header가 nlohmann/json, MessagePack, Protobuf SDK 타입을 직접 include하거나
  signature에 노출하면 `test_cpp_framework_layout_contract`가 실패한다.

## 반복 POSD 재리뷰. Goal 22 package consumer 실행 격리 보강

### 발견한 위험 신호

- Goal 22는 CTest label 전체 통과와 install/package consumer test를 완료 기준에 둔다.
- `test_cpp_framework_install_consumer`는 고정된 `package-consumer-src`,
  `package-consumer-build`, `package-consumer-install` 경로를 사용했다.
- 같은 build tree에서 package label과 full suite를 별도 `ctest` 프로세스로 동시에 실행하면
  한쪽 테스트가 다른 쪽 consumer build directory를 지워 dependency file 생성 실패가 날 수
  있었다.
- 이는 package 기능 자체의 실패가 아니라 test isolation 실패다. 하지만 최종 회귀 gate를 반복
  실행하는 상황에서는 테스트 신뢰성을 떨어뜨리는 실제 오구현이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 병렬로 같은 label을 실행하지 않는 규칙만 둔다 | 코드 변경이 없다 | 테스트 사용자가 실행 순서를 기억해야 한다 |
| CTest `RESOURCE_LOCK`만 추가 | 한 CTest 프로세스 안에서는 충돌을 줄인다 | 서로 다른 CTest 프로세스 간 충돌은 막지 못한다 |
| install consumer가 실행마다 고유 작업 디렉터리를 사용 | 별도 CTest 프로세스끼리도 격리된다 | 임시 run directory가 생긴다 |

선택은 세 번째 방식이다. package consumer는 배포 산출물을 소비하는 독립 검증이어야 하므로,
호출자가 실행 순서를 알아야 하는 설계는 얕은 테스트다. 실행마다 install/source/build directory를
분리하면 테스트가 자기 상태를 내부에 숨기고 최종 gate를 반복 실행해도 안정적으로 동작한다.

### 적용한 리팩토링

- `install_consumer.cmake`가 random run id 아래에 `install`, `src`, `build` directory를 만들게 했다.
- package config 검사, consumer configure/build/run 모두 해당 고유 install prefix를 사용하게 했다.
- 기존 `ZLINK_FRAMEWORK_CPP_INSTALL_PREFIX`는 test 호출 contract로 유지하되, 개별 실행의 base
  directory는 script 내부에서 격리한다.

### 수정 후 점검

- 같은 build tree에서 package consumer가 중복 실행되어도 서로의 source/build/install directory를
  지우지 않는다.

## 반복 POSD 재리뷰. Goal 22 tooling smoke 실행 격리 보강

### 발견한 위험 신호

- `test_cpp_framework_tooling_contract`는 `framework-package` label에도 포함되어 install/package
  gate와 함께 실행된다.
- 해당 스크립트는 고정된 `tooling-smoke/linux-ninja-debug` build directory를 지우고 다시
  configure했다.
- 같은 build tree에서 package label을 별도 `ctest` 프로세스로 동시에 실행하면 한쪽 tooling smoke가
  다른 쪽 configure directory를 지워 CMake target import 상태가 깨질 수 있었다.
- 이는 tooling contract의 본질인 preset/configure 검증과 무관한 공유 상태 누출이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| package label을 동시에 실행하지 않는다 | 변경이 없다 | 최종 gate 반복 실행 조건을 테스트 사용자가 기억해야 한다 |
| tooling smoke를 package label에서 제거 | 충돌 가능성이 줄어든다 | package gate가 tooling contract를 덜 검증한다 |
| tooling smoke가 실행마다 고유 build directory를 사용 | 별도 `ctest` 프로세스끼리도 격리된다 | 임시 run directory가 생긴다 |

선택은 세 번째 방식이다. tooling smoke는 CLion-style configure가 가능한지를 독립적으로 확인하는
깊은 테스트여야 한다. 고정 directory를 공유하면 호출자가 실행 순서를 알아야 하므로 테스트가
자기 상태를 숨기지 못한다.

### 적용한 리팩토링

- `verify_tooling_contract.cmake`가 random run id 아래에 tooling smoke build directory를 만들게 했다.
- 기존 preset, vcpkg, compile commands, cache 검증은 그대로 유지했다.

### 수정 후 점검

- 같은 build tree에서 tooling contract가 중복 실행되어도 서로의 configure directory를 지우지 않는다.

## 반복 POSD 재리뷰. Goal 1-22 POSD zero-issue gate 보강

### 발견한 위험 신호

- implementation plan은 각 goal의 POSD 리팩토링 뒤 잔여 위험 신호와 리팩토링 이슈가 0개여야
  다음으로 진행할 수 있다고 명시한다.
- 기존 layout contract는 POSD 기록 수와 현재 Goal 1-22 대표 mapping row 존재를 검사했지만,
  각 row가 잔여 이슈 0개 상태까지 명시하는지는 확인하지 않았다.
- 이 상태에서는 대표 기록 표가 남아 있어도 final audit에서 "기록이 있다"와 "재점검 결과가
  0 이슈로 닫혔다"를 사람이 다시 구분해야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현재 mapping row 검사만 유지 | 변경이 작다 | 0 이슈 완료 조건은 문서 독자가 다시 확인해야 한다 |
| 모든 POSD section을 정규화한다 | 가장 엄격하다 | 과거 실행 기록 전체를 대규모로 고쳐야 한다 |
| 현재 Goal 1-22 대표 표에 재점검 상태를 추가하고 contract가 검사한다 | 현재 plan 완료 조건을 직접 고정한다 | 표 row 형식을 유지해야 한다 |

선택은 세 번째 방식이다. POSD 로그는 실행 기록이므로 과거 section을 강제로 재작성하지 않고,
현재 plan 기준 대표 표에서 각 goal의 0 이슈 재점검 상태를 명시한다. contract test는 이 대표
표를 final audit 증거로 사용한다.

### 적용한 리팩토링

- 현재 Goal 1-22 대표 POSD 기록 표에 `재점검 상태` column을 추가했다.
- 각 goal row가 `잔여 POSD 위험 신호와 리팩토링 이슈 0개` 상태를 명시하게 했다.
- `test_cpp_framework_layout_contract`가 각 대표 mapping row에서 0 이슈 재점검 상태를 함께
  검사하게 했다.

### 수정 후 점검

- POSD 대표 mapping row가 기록만 남기고 0 이슈 상태를 빠뜨리면
  `test_cpp_framework_layout_contract`가 실패한다.

## 반복 POSD 재리뷰. Goal 18 HTTP client deferred feature boundary 보강

### 발견한 위험 신호

- Goal 18은 retry, redirect, cookie, proxy, multipart, streaming download를 초기 core 범위에
  넣지 않고 후속 extension point로 남긴다고 명시한다.
- 현재 public header에는 해당 API가 없지만, layout contract는 이 제외 범위를 직접 검사하지
  않았다.
- 이 상태에서는 HTTP client public surface에 후속 범위 API가 섞여도 public dependency gate나
  compile contract만으로는 plan과의 충돌을 잡기 어렵다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 수동 리뷰만 유지 | 테스트 변경이 없다 | public surface drift를 반복 리뷰 때 놓칠 수 있다 |
| HTTP client feature를 모두 구현한다 | 기능 표면이 넓어진다 | Goal 18의 초기 core 범위를 넘어서고 extension boundary가 흐려진다 |
| layout contract가 HTTP client public include에서 deferred feature API를 금지한다 | plan의 범위 제한을 자동으로 고정한다 | 후속 goal에서 기능을 열 때 contract와 문서를 함께 바꿔야 한다 |

선택은 세 번째 방식이다. HTTP client는 framework HTTP hosting 검증용 core 소비자이므로 초기
표면은 typed JSON request/response, timeout, status/TLS mapping에 집중해야 한다. 후속 기능은
별도 extension point가 생길 때 plan과 contract를 함께 갱신한다.

### 적용한 리팩토링

- `test_cpp_framework_layout_contract`에 HTTP client public include를 스캔하는
  deferred feature boundary 검사를 추가했다.
- public header에 retry, redirect, cookie, proxy, multipart, streaming download 계열 이름이
  들어오면 contract가 실패하게 했다.

### 수정 후 점검

- HTTP client public surface가 Goal 18 초기 core 범위를 넘어서면
  `test_cpp_framework_layout_contract`가 실패한다.

## 반복 POSD 재리뷰. Goal 19 HTTP hosting non-goal boundary 보강

### 발견한 위험 신호

- Goal 19는 HTTP hosting 범위를 Minimal API style route handler로 제한하고, MVC controller,
  template rendering, Razor page, WebSocket transport는 포함하지 않는다고 명시한다.
- public dependency gate는 Beast/Asio/OpenSSL 같은 낮은 수준 타입 노출을 막지만, HTTP hosting
  public surface가 non-goal 기능 이름을 직접 추가하는 회귀는 별도로 고정하지 않았다.
- 이 상태에서는 HTTP route handler 표면이 framework core 안에서 MVC/WebSocket 계열 표면과
  섞여도 final audit이 문서 대조를 다시 수동으로 해야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 수동 리뷰만 유지 | 테스트 변경이 없다 | HTTP hosting 범위가 넓어지는 회귀를 놓칠 수 있다 |
| non-goal 기능을 즉시 구현한다 | 기능은 많아진다 | Goal 19의 Minimal API 중심 범위와 충돌한다 |
| layout contract가 HTTP hosting public include에서 non-goal feature API를 금지한다 | plan의 범위 제한을 자동으로 고정한다 | 후속 기능을 열 때 문서와 contract를 함께 갱신해야 한다 |

선택은 세 번째 방식이다. HTTP hosting은 route handler, middleware/filter, TLS, health 표면을
core로 닫고, MVC/Razor/WebSocket 계열은 별도 goal이나 extension owner가 생길 때 분리해야 한다.

### 적용한 리팩토링

- `test_cpp_framework_layout_contract`에 HTTP hosting public include를 스캔하는 non-goal
  boundary 검사를 추가했다.
- public HTTP hosting header에 MVC/controller/Razor/WebSocket/template rendering 계열 이름이
  들어오면 contract가 실패하게 했다.

### 수정 후 점검

- HTTP hosting public surface가 Goal 19 Minimal API 범위를 넘어서면
  `test_cpp_framework_layout_contract`가 실패한다.

## 반복 POSD 재리뷰. Goal 19 HTTP e2e process port isolation 보강

### 발견한 위험 신호

- `test_cpp_framework_app_host`는 HTTP/HTTPS listener endpoint를 고정 port로 사용했다.
- 같은 executable이 `framework-http`, `framework-http-e2e`, full suite 같은 서로 다른 CTest
  프로세스에서 동시에 실행되면 한쪽 process가 다른 쪽 process의 port bind를 실패시킬 수 있다.
- 이는 HTTP hosting 기능 실패가 아니라 test isolation 실패지만, Goal 19/22 검증 명령을 반복해서
  병렬 실행하는 상황에서는 완료 증거를 흔든다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| HTTP 관련 label을 동시에 실행하지 않는다 | 코드 변경이 없다 | 테스트 사용자가 실행 순서를 기억해야 한다 |
| CTest `RESOURCE_LOCK`만 추가 | 한 CTest 프로세스 안에서는 충돌을 줄인다 | 별도 CTest 프로세스끼리는 막지 못한다 |
| 테스트 process별로 listener port를 파생한다 | 별도 CTest 프로세스끼리도 격리된다 | test endpoint 계산 helper가 필요하다 |

선택은 세 번째 방식이다. HTTP hosting e2e는 route/middleware/TLS 의미를 검증해야 하므로,
고정 port라는 외부 공유 상태 때문에 실패하면 테스트가 얕아진다. process별 endpoint를 만들면
검증자가 label 실행 순서를 알 필요가 없다.

### 적용한 리팩토링

- `test_cpp_framework_app_host`가 compile-time 기본 endpoint에서 process id 기반 port offset을
  계산하게 했다.
- HTTP listener, HTTPS listener, HTTPS client base URL이 같은 process 안에서는 같은 파생 port를
  공유하고, 다른 process와는 겹치지 않게 했다.
- TLS startup validation과 HTTPS e2e test도 파생 endpoint를 사용하게 했다.

### 수정 후 점검

- 별도 CTest 프로세스에서 HTTP 관련 label이 동시에 실행되어도 같은 고정 listener port를
  공유하지 않는다.

## 반복 POSD 재리뷰. Goal 20 Stream Connector public state hiding 보강

### 발견한 위험 신호

- Goal 20 완료 조건은 connector public header가 receive loop, transport connection, pending
  request table, frame sender 같은 runtime 구현 타입을 노출하지 않아야 한다고 명시한다.
- 기존 public header는 실제 transport나 pending table 필드를 직접 노출하지는 않았지만,
  `detail::connector_state_t`와 `detail::connector_runtime_t` forward declaration을 통해 내부
  상태 owner 이름을 public contract에 남겼다.
- 이 상태에서는 호출자가 내부 상태 구조를 알아야 할 필요는 없는데도 public header가 구현의
  이름을 암시하므로 정보 은닉이 약해진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 수동 리뷰만 유지 | 코드 변경이 작다 | 내부 상태 타입명이 다시 public header에 들어와도 자동으로 잡지 못한다 |
| 모든 builder 상태를 값 타입으로 복사한다 | 내부 pointer가 사라진다 | connector/call/codec registry가 같은 runtime 상태를 공유해야 하는 현재 fluent API와 맞지 않는다 |
| public header는 opaque handle만 보관하고 runtime `.cpp`에서 내부 상태로 복원한다 | 호출자 표면에서 구현 타입명을 숨기고 기존 fluent API를 유지한다 | runtime 구현에서 handle 복원 helper가 필요하다 |

선택은 세 번째 방식이다. connector 상태 공유는 구현 세부 사항이므로 public header에는 의미 없는
opaque handle로만 남기고, 실제 상태 타입과 runtime helper는 `connector/core/src/runtime` 안에서만
다룬다.

### 적용한 리팩토링

- `connector_t`, `codec_registry_t`, `send_call_t`, `request_call_t` public header의 내부 상태
  저장소를 `std::shared_ptr<void>` opaque handle로 바꾸었다.
- `detail::connector_state_t`와 `detail::connector_runtime_t` forward declaration을 public
  interface header에서 제거했다.
- runtime 구현은 내부 helper로 opaque handle을 `connector_state_t`로 복원한 뒤 기존 동작을
  수행하게 했다.
- `test_cpp_framework_layout_contract`가 Stream Connector public include에서 runtime 내부 타입명
  노출을 검사하게 했다.

### 수정 후 점검

- connector public header에 `connector_state_t`, `connector_runtime_t`, pending request table,
  transport connection, frame/header/metadata/LZ4 codec 구현 타입명이 들어오면
  `test_cpp_framework_layout_contract`가 실패한다.

## 반복 POSD 재리뷰. Goal 18 HTTP client HTTPS label evidence 보강

### 발견한 위험 신호

- Goal 18은 HTTPS request, TLS verification 실패, test certificate trust 성공을
  `http-client-https` 축으로 검증해야 한다고 명시한다.
- 기존 CTest label은 public header compile smoke에도 `http-client-https`를 붙였다. 이 상태에서는
  HTTPS e2e가 비활성화되어도 label non-empty 검사가 compile smoke 때문에 통과할 수 있다.
- label이 실제 동작 검증이 아니라 compile smoke로 만족되면 plan의 HTTPS 완료 기준을 증명하지
  못한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현 상태 유지 | 테스트 수가 변하지 않는다 | HTTPS label의 증거 의미가 약하다 |
| OpenSSL이 없으면 configure를 즉시 실패시킨다 | HTTPS 지원을 강하게 요구한다 | dependency discovery 정책까지 넓게 바꾼다 |
| `http-client-https`를 실제 HTTP client regression test에만 붙이고 label contract로 고정한다 | label 의미와 plan 검증 축이 일치한다 | OpenSSL 없는 빌드에서는 label contract가 실패한다 |

선택은 세 번째 방식이다. plan은 HTTPS를 완료 기준으로 둔다. 따라서 `http-client-https` label은
public header compile 여부가 아니라 HTTPS request와 TLS verification을 실행하는 test를
가리켜야 한다.

### 적용한 리팩토링

- `test_cpp_framework_contract_headers`에서 HTTP client unit/e2e/https/regression label을 제거하고
  framework public header compile contract로만 남겼다.
- `verify_ctest_label_contract.cmake`가 `http-client-https` label이
  `test_cpp_http_client`를 선택하고 public header compile smoke로 만족되지 않는지 검사하게 했다.

### 수정 후 점검

- `http-client-https` label이 실제 HTTP client HTTPS regression test 없이 통과하면
  `test_cpp_framework_label_contract`가 실패한다.

## 반복 POSD 재리뷰. Goal 3 public async surface gate 보강

### 발견한 위험 신호

- Goal 3은 public async 표면에서 `std::future`와 `boost::asio::awaitable`을 사용하지 않는다고
  명시한다.
- 기존 contract header test는 대표 타입 일부를 compile-time assert로 확인했지만, 모든 public
  header를 스캔해 future/awaitable/cancellation token 계열 이름이 새로 들어오는 회귀를 막지는
  않았다.
- async primitive가 public header에 섞이면 호출자는 framework의 `task_t<T>` 의미 대신 낮은
  수준 실행 모델을 알아야 하므로 public surface가 얕아진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 대표 static assert만 유지 | 테스트가 이미 있다 | 새 public header에 future/awaitable이 들어오는 회귀를 놓칠 수 있다 |
| public async API를 전부 수동 리뷰한다 | 유연하다 | 반복 리뷰 때 같은 증거를 다시 모아야 한다 |
| layout contract가 public include 전체에서 forbidden async primitive를 스캔한다 | 완료 기준을 자동으로 고정한다 | 후속 정책 변경 시 contract와 문서를 함께 바꿔야 한다 |

선택은 세 번째 방식이다. public async model은 framework 전체의 사용성 기준이므로 특정 대표 타입만
검사하기보다 public include 전체를 gate로 닫는 편이 더 깊은 모듈을 유지한다.

### 적용한 리팩토링

- `test_cpp_framework_layout_contract`의 public dependency scanner에 `std::future`,
  `std::promise`, `<future>`, `boost::asio::awaitable`, cancellation token/source 계열 이름을
  금지 항목으로 추가했다.

### 수정 후 점검

- framework, HTTP client, connector, extension public header에 future/awaitable/cancellation
  primitive가 노출되면 `test_cpp_framework_layout_contract`가 실패한다.

## 반복 POSD 재리뷰. Goal 21 client sample embedded harness 제거

### 발견한 위험 신호

- Goal 21은 Client 샘플이 서버 handler를 직접 호출하지 않고 HTTP client와 Stream Connector로
  실제 server process와 붙어 검증해야 한다고 명시한다.
- 기존 Bingo/TicTacToe Client 코드는 standalone smoke를 위해 `zlink::context_t`,
  `zlink::stream_socket_t`, `Shared/E2E` server harness를 직접 포함했다.
- 이 구조에서는 사용자에게 보여야 할 Client 샘플과 테스트용 server harness가 한 모듈에 섞인다.
  Client가 낮은 수준 zlink socket 준비 순서를 알아야 하므로 public sample 사용성이 얕아진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| embedded harness 유지 | client executable 하나만 실행해도 통과한다 | Client 샘플이 server/test harness와 낮은 수준 socket을 직접 안다 |
| Client 내부 분기를 유지하되 파일만 나눈다 | 일부 include는 줄일 수 있다 | Client API에 embedded server 옵션이 남아 책임 경계가 흐리다 |
| Client는 실제 client 역할만 수행하고 process e2e test가 server executable을 띄운다 | 샘플 역할과 테스트 harness 책임이 분리된다 | standalone client smoke test를 process e2e로 대체해야 한다 |

선택은 세 번째 방식이다. Goal 21의 완료 증거는 client 단독 smoke가 아니라 실제 server process와
붙은 request/reply, push, disconnect, shutdown log 검증이어야 한다.

### 적용한 리팩토링

- Bingo/TicTacToe Client에서 embedded server option, raw zlink context/stream socket, `Shared/E2E`
  harness include를 제거했다.
- Client executable은 더 이상 standalone smoke test로 등록하지 않고, process e2e test에서 server
  executable과 함께 실행하게 했다.
- 기존 standalone client-log verifier를 제거하고, process e2e test가 server file log의 request,
  reply, push, disconnect, shutdown evidence를 계속 검증하게 했다.
- process e2e runner는 runner source 경로 기준의 process-wide lock을 잡은 뒤 로그를 초기화하고
  server/client를 실행한다. 같은 고정 sample endpoint를 쓰는 label이나 normal/coverage build
  tree를 별도 CTest 프로세스에서 동시에 실행해도 서로 충돌하지 않게 하기 위해서다.
- sample parity/layout contract가 Client 디렉터리에서 server/test harness include와 낮은 수준
  zlink socket/context 사용을 금지하게 했다.

### 수정 후 점검

- Client 샘플에 `Shared/E2E`, `zlink/Contracts/Sockets`, `zlink::context_t`,
  `zlink::stream_socket_t`, embedded server option이 다시 들어오면 contract test가 실패한다.
- `framework-sample-client-e2e`와 `framework-sample-log` label, normal/coverage build tree를 별도
  CTest 프로세스로 동시에 실행해도 process e2e runner lock 때문에 sample server port 충돌이
  재발하지 않는다.

## 반복 POSD 재리뷰. Goal 20 Unreal connector plugin metadata 정합성

### 발견한 위험 신호

- Goal 20은 Unreal Stream Connector를 별도 plugin/module 산출물로 구현한다고 명시한다.
- 실제 `ZLinkStreamConnector.uplugin`은 runtime module과 public/private 구현을 갖고 있었지만,
  description은 여전히 `plugin skeleton`이라고 설명했다.
- 사용자가 plugin metadata를 먼저 보면 구현된 runtime 산출물을 빈 뼈대처럼 오해할 수 있다.
  이는 구현 상태 지식이 코드와 metadata에 다르게 저장된 정보 누출이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| description 유지 | 변경이 없다 | plan 완료 상태와 plugin metadata가 계속 충돌한다 |
| plugin metadata를 제거 | 불일치가 사라진다 | Unreal package 표면 정보가 빈약해진다 |
| description을 runtime plugin으로 갱신 | 산출물 상태를 정확히 드러낸다 | metadata 문구를 유지해야 한다 |

선택은 세 번째 방식이다. Unreal plugin metadata는 public package surface이므로 구현 상태를 정확히
말해야 한다.

### 적용한 리팩토링

- `ZLinkStreamConnector.uplugin` description을 `runtime plugin`으로 갱신했다.

### 수정 후 점검

- Unreal connector contract/compile/smoke label이 계속 통과한다.

## 반복 POSD 재리뷰. Goal 4/22 logging backend public dependency 은닉

### 발견한 위험 신호

- Goal 4와 Goal 22는 `spdlog` 같은 logging runtime dependency가 public header에 불필요하게
  노출되지 않아야 한다고 요구한다.
- `logging_backend_t` public enum은 `spdlog` 값을 직접 노출했다. `spdlog::logger`나
  `<spdlog/...>` header를 노출하지는 않았지만, public API가 내부 logging library 이름을
  호출자에게 알리는 형태였다.
- 정책 문서도 `spdlog`는 내부 구현 세부라고 설명하면서 public 계약 예시는
  `logging_backend_t::spdlog`로 남아 있었다. 이는 정보 은닉 위반이며, 문서와 코드가 서로
  다른 설계 의도를 말하는 위험 신호다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 enum 유지 | 코드 변경이 적다 | 내부 구현 이름이 public API에 계속 남는다 |
| `use_backend(...)`를 제거 | 호출자 표면이 가장 작다 | 현재 regression이 검증하는 backend 선택 기능까지 한 번에 제거한다 |
| enum 값을 구현 중립 이름으로 변경 | 기능은 유지하고 implementation name 누출만 제거한다 | public enum 이름 변경이 필요하다 |

선택은 세 번째 방식이다. backend 선택 기능 자체는 유지하되, 호출자가 특정 logging library를
알아야 하는 구조는 제거한다.

### 적용한 리팩토링

- `logging_backend_t::spdlog`를 `logging_backend_t::structured`로 바꿨다.
- app host regression test와 정책 문서의 public 계약 예시를 같은 이름으로 맞췄다.
- public header dependency contract가 bare `spdlog` 문자열도 금지하도록 보강했다.

### 수정 후 점검

- public header에는 `spdlog` 이름, `spdlog::` 타입, `<spdlog/...>` include가 남아 있지 않다.
- 남은 `spdlog` 참조는 내부 구현 정책 설명, plan의 금지 기준, POSD 기록, contract test의
  금지 문자열에 한정된다.
- 이번 보정 뒤 logging backend public dependency 경계의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 22 implementation plan CTest command gate 보강

### 발견한 위험 신호

- implementation plan은 각 goal과 최종 회귀 단계에 CTest 명령을 직접 적는다.
- 기존 label contract는 required label이 현재 build에서 비어 있지 않은지만 확인했다.
  하지만 plan에 적힌 `-R` selector나 build directory가 잘못되어도, 해당 명령 전체를
  자동으로 훑는 gate는 없었다.
- 이 상태에서는 문서 명령이 실제로는 0개 테스트를 선택하는데도 label taxonomy만 통과할 수
  있다. 검증 지식이 문서와 수동 스크립트에 나뉘어 있는 정보 은닉 위반이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 수동 스크립트만 유지 | 구현 변경이 없다 | plan 명령 drift가 회귀 테스트에 남지 않는다 |
| 모든 plan 명령을 별도 테스트 executable로 실행 | 표현력이 높다 | CTest 호출을 다시 구현해야 한다 |
| label contract CMake script가 plan의 CTest 명령을 `ctest -N`으로 스캔 | 기존 label gate와 책임이 같다 | 외부 build dir이 없을 때 skip 기준이 필요하다 |

선택은 세 번째 방식이다. plan 검증 명령은 CTest label/selectors의 계약이므로 기존
`test_cpp_framework_label_contract` 안에서 함께 검사한다.

### 적용한 리팩토링

- `test_cpp_framework_label_contract`에 framework source dir을 전달하게 했다.
- label contract script가 `cpp-framework-implementation-plan.ko.md`의 `ctest --test-dir`
  명령을 읽고, 현재 framework build 명령은 현재 build dir으로 `ctest -N` 스캔한다.
- coverage build 명령은 coverage label contract에서 현재 build dir으로 스캔하고, 외부
  `bindings/cpp/build`처럼 현재 configure 범위 밖의 build dir은 존재할 때만 스캔한다.

### 수정 후 점검

- plan에 적힌 CTest 명령이 현재 build tree에서 0개 테스트를 선택하면
  `test_cpp_framework_label_contract`가 실패한다.
- 이번 보정 뒤 implementation plan CTest command drift의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 16/19 health HTTP unhealthy status 회귀 보강

### 발견한 위험 신호

- `cpp-http-hosting.ko.md`는 readiness 또는 liveness가 `unhealthy`이면 해당 HTTP endpoint가
  `503 Service Unavailable`을 반환한다고 명시한다.
- runtime 구현은 이 mapping을 갖고 있었지만, app host HTTP e2e는 healthy `/health`와
  `/live` 응답만 확인했다.
- 이 상태에서는 health aggregate 구현이 바뀌어 unhealthy readiness가 `200 OK`로 회귀해도
  문서 조건을 직접 깨는 테스트가 없었다. 문서 조건과 검증 지식이 분리된 위험 신호다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| monitoring unit test만 유지 | 테스트가 빠르다 | HTTP status mapping을 검증하지 않는다 |
| 기존 running app health 상태를 중간에 바꾼다 | 같은 endpoint를 재사용한다 | test thread와 server thread 사이 상태 변경이 섞인다 |
| 별도 unhealthy health app을 띄워 `/ready`, `/live` status를 검증한다 | HTTP mapping을 직접 고정하고 기존 happy path와 격리된다 | app host test가 한 시나리오 더 실행된다 |

선택은 세 번째 방식이다. health 상태를 server 시작 전에 설정하면 테스트가 실행 순서나 공유 상태
변경 타이밍을 알아야 하지 않는다.

### 적용한 리팩토링

- app host e2e test에 `wait_for_raw_status(...)` helper를 추가했다.
- 별도 health app에서 readiness 전용 channel check를 `unhealthy`로 설정하고 `/ready`는 `503`,
  `/live`는 `200`을 반환하는지 `zlink::http_client` raw response로 검증하게 했다.
- liveness까지 포함되는 hosted service check가 `unhealthy`일 때는 `/ready`와 `/live`가 모두
  `503`을 반환하는지도 별도 app으로 검증하게 했다.

### 수정 후 점검

- health HTTP route가 aggregate status body만이 아니라 documented HTTP status mapping까지
  지키는지 `framework-http-e2e` label에서 확인한다.
- 이번 보정 뒤 health HTTP unhealthy status mapping의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 21 Shared sample aggregate 경계 보강

### 발견한 위험 신호

- Goal 21은 `Shared`, `Client`, `Server/Registry`, `Server/Api`, `Server/Play`,
  `Server/Session` 역할 분리를 완료 기준으로 둔다.
- `Bingo 공유 umbrella header`와 `TicTacToe 공유 umbrella header`가 Client header와 Server handler
  header를 함께 include하고 있었다.
- 이 구조에서는 server host factory가 필요한 타입을 명시하지 않고 Shared aggregate에 기대게 된다.
  Shared 역할이 DTO와 공통 설정을 넘어 role-specific 구현을 끌어안는 얕은 모듈이 되는 위험 신호다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 aggregate를 유지하고 문서에 예외로 적는다 | 코드 변경이 작다 | Goal 21의 역할 분리 기준을 약화한다 |
| Client만 aggregate에서 제거한다 | client/server 직접 결합은 줄어든다 | Server handler가 Shared 표면에 계속 섞인다 |
| Shared aggregate와 host support를 공통 설정, 계약, lifecycle helper로 줄이고 role 파일이 직접 include한다 | include owner가 명확하고 역할 분리가 테스트로 고정된다 | 각 host/test 파일의 include가 늘어난다 |

선택은 세 번째 방식이다. 각 role 파일이 자신이 사용하는 handler와 runtime 타입을 직접 include하면
Shared header를 읽는 사용자가 server 내부 구조를 함께 배워야 하지 않는다.

### 적용한 리팩토링

- `Bingo 공유 umbrella header`와 `TicTacToe 공유 umbrella header`에서 Client/Server 구현 include를
  제거했다.
- 공유 host support header는 sample auto-stop hosted service만 제공하도록 축소했다.
- server host factory와 sample parity test는 필요한 handler/header를 직접 include하게 했다.
- sample parity test에 Shared sample header가 Client/Server role code를 aggregate하지 않는
  회귀 테스트를 추가했다.

### 수정 후 점검

- Shared sample header는 DTO, 공통 설정, lifecycle helper만 노출한다.
- Client sample은 여전히 HTTP client와 Stream Connector를 통해 server process와 통신한다.
- 이번 보정 뒤 Goal 21 Shared sample aggregate 경계의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 21 TicTacToe README HTTP 시작 흐름 보강

### 발견한 위험 신호

- Goal 18, Goal 19, Goal 21은 TicTacToe client sample이 `zlink::http_client`로
  HTTP `POST /games`를 호출해 match를 시작해야 한다고 둔다.
- 실제 `sample_cpp_framework_tictactoe_client`는 HTTP client로 `/games`를 호출한 뒤
  Stream Connector로 session flow를 진행한다.
- 하지만 `samples/TicTacToe/README.ko.md`의 client 실행 파일 설명은 Stream Connector flow만
  언급했다. 문서를 보고 sample을 검토하는 사용자가 HTTP 시작 경계를 놓칠 수 있는 문서 drift다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| README를 그대로 둔다 | 변경이 없다 | Goal 21의 HTTP 시작 흐름이 sample 문서에서 보이지 않는다 |
| implementation plan만 근거로 둔다 | 상위 계획은 이미 맞다 | 샘플을 직접 여는 독자에게는 증거가 약하다 |
| TicTacToe README와 sample parity test가 HTTP client 시작 흐름을 함께 고정한다 | 문서와 회귀 테스트가 실제 client 경계를 같이 설명한다 | README test 문구가 조금 더 구체적이다 |

선택은 세 번째 방식이다. Review sample 문서는 사용자가 실제 실행 파일을 열기 전에 보는 문서이므로
HTTP client와 Stream Connector의 역할 분담을 바로 드러내야 한다.

### 적용한 리팩토링

- TicTacToe README의 client 실행 파일 설명을 HTTP client `POST /games` 시작 요청과
  Stream Connector flow로 고쳤다.
- client smoke 설명에 실제 HTTP API server, `zlink::http_client`, `POST /games`,
  HTTP request log evidence를 추가했다.
- sample parity test가 TicTacToe README에 HTTP client 시작 흐름과 log evidence가 있는지
  검증하게 했다.

### 수정 후 점검

- TicTacToe sample 문서는 plan의 HTTP 시작 요구와 실제 client 구현을 같은 방향으로 설명한다.
- README에서 HTTP 시작 흐름이 빠지면 `test_cpp_framework_sample_parity`가 실패한다.
- 이번 보정 뒤 Goal 21 TicTacToe README HTTP 시작 흐름의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 1/22 non-empty directory placeholder 제거

### 발견한 위험 신호

- Goal 1과 Goal 22는 framework, connector, HTTP client, Unreal connector가 실제 산출물과
  public/runtime 경계로 분리되어야 한다고 둔다.
- 실제 파일이 들어찬 `framework/include`, `framework/src/runtime`, `connector/core/src/runtime`,
  Unreal `Private` 하위 디렉터리에 `.gitkeep` placeholder가 계속 남아 있었다.
- 비어 있지 않은 디렉터리에 placeholder 파일이 남으면 현재 구조가 실제 구현 소유자인지,
  빈 디렉터리를 맞추기 위한 자리표시자인지 구분이 흐려진다. 이는 산출물 경계 감사에서 잡음이 된다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `.gitkeep`를 그대로 둔다 | 변경이 없다 | placeholder와 실제 구현 디렉터리의 의미가 섞인다 |
| 모든 `.gitkeep`를 일괄 금지한다 | 규칙이 단순하다 | 진짜 빈 디렉터리를 보존해야 할 때 사용할 수 없다 |
| 비어 있지 않은 C++ framework 산출물 디렉터리에서만 `.gitkeep`를 금지한다 | 구현 소유자가 생긴 디렉터리의 placeholder 잔재를 막는다 | layout contract가 파일 시스템 구조를 더 본다 |

선택은 세 번째 방식이다. `.gitkeep` 자체를 금지하지 않고, 실제 파일이 들어찬 산출물 디렉터리에
남은 placeholder만 제거하면 디렉터리 의도가 명확해진다.

### 적용한 리팩토링

- 이미 실제 파일이 있는 C++ framework, connector, Unreal connector public/runtime 디렉터리의
  `.gitkeep` 파일을 제거했다.
- layout contract가 non-empty framework 산출물 디렉터리에 `.gitkeep` placeholder가 남으면
  실패하게 했다.

### 수정 후 점검

- public/runtime 디렉터리는 실제 header/source 파일로 존재가 증명된다.
- 향후 구현이 들어간 디렉터리에 placeholder가 남으면 `test_cpp_framework_layout_contract`가
  실패한다.
- 이번 보정 뒤 non-empty directory placeholder 잔재의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 22 final concrete label command 보강

### 발견한 위험 신호

- Goal 22는 CTest label 전체 통과와 Goal 1-22 완료 기준 충족을 최종 완료 조건으로 둔다.
- Goal 22 검증 블록에는 full CTest가 있었지만, 일부 concrete label이 독립 명령으로
  드러나지 않았거나 layout contract의 필수 명령 목록에 고정되지 않았다.
- 빠져 있던 축은 HTTP client contract/unit/HTTPS, framework zlink 세부 label,
  config/observability, connector unit/integration, sample smoke/parity/log, 샘플별 role label
  일부, framework coverage였다.
- 이 상태에서는 전체 CTest를 실행하면 통과하더라도, Goal 22 문서만 보고 최종 감사 축을
  하나씩 재실행하는 사용자가 concrete label 일부를 놓칠 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| full CTest 명령만 유지한다 | 명령 목록이 짧다 | 최종 audit 축이 문서에 모두 보이지 않는다 |
| 각 Goal 18-21 검증 명령에만 맡긴다 | 중복을 줄인다 | Goal 22 단독 실행 시 concrete label 일부를 놓칠 수 있다 |
| Goal 22 명령에 final audit concrete label 축을 모두 드러내고 contract로 고정한다 | 최종 감사 명령과 label taxonomy가 일치한다 | 명령 목록이 길어진다 |

선택은 세 번째 방식이다. Goal 22는 최종 감사 문서이므로, full CTest와 별도로 독립 label 축을
명령 목록에서 바로 확인할 수 있어야 한다.

### 적용한 리팩토링

- Goal 22 검증 명령에 누락된 HTTP client contract/unit/HTTPS, connector unit/integration,
  sample smoke/parity/log, sample bingo/client/tictactoe, framework coverage concrete label 명령을 추가했다.
- layout contract가 framework zlink 세부 label, config/observability, HTTP client, connector,
  Unreal connector, sample, coverage concrete label 명령을 Goal 22 검증 블록에서 찾도록 보강했다.

### 수정 후 점검

- Goal 22 검증 블록에서 final audit concrete label 축이 빠지면
  `test_cpp_framework_layout_contract`가 실패한다.
- 이번 보정 뒤 Goal 22 final concrete label command의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 22 final non-CTest gate 보강

### 발견한 위험 신호

- Goal 22 검증 블록은 full build, coverage configure/build, `git diff --check`까지 포함한다.
- 기존 layout contract는 Goal 22의 `ctest` 명령만 필수로 고정했다.
- 이 상태에서는 coverage threshold 명령은 남아 있어도 coverage build configure가 빠지거나,
  최종 whitespace gate가 문서에서 사라지는 퇴행을 contract가 잡지 못한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `ctest` 명령만 계속 검사한다 | contract가 단순하다 | Goal 22의 build/hygiene gate 누락을 놓친다 |
| shell script로 Goal 22 명령 전체를 실행한다 | 문서와 실행이 강하게 묶인다 | layout contract가 느려지고 환경 의존성이 커진다 |
| layout contract가 비-CTest gate 문자열도 검사한다 | 빠르고 Goal 22 문서 퇴행을 잡는다 | 명령 문자열을 유지해야 한다 |

선택은 세 번째 방식이다. Goal 22 문서의 비-CTest gate는 실행 자체보다 존재 여부를 빠르게
고정하는 것이 목적에 맞다.

### 적용한 리팩토링

- layout contract가 Goal 22 검증 블록에서 normal build, coverage configure/build,
  `git diff --check -- framework/languages/cpp bindings/cpp` 명령을 찾도록 보강했다.

### 수정 후 점검

- Goal 22 검증 블록에서 build, coverage configure/build, diff check gate가 빠지면
  `test_cpp_framework_layout_contract`가 실패한다.
- 이번 보정 뒤 Goal 22 final non-CTest gate의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 22 framework package optional connector dependency 제거

### 발견한 위험 신호

- Goal 22는 framework, connector, HTTP client, extension boundary가 독립 package 경계를
  유지해야 한다고 둔다.
- `zlink_framework_cppConfig.cmake.in`은 framework package config인데도 Stream Connector의
  MessagePack/Protobuf optional dependency placeholder를 포함했다.
- 기본 옵션에서는 빈 문자열이라 드러나지 않지만, connector optional codec을 켠 build에서는
  framework package만 찾는 소비자도 connector codec dependency를 요구받을 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 그대로 둔다 | 현재 기본 build에는 변화가 없다 | optional connector dependency가 framework package 경계로 샌다 |
| framework package가 connector config를 함께 포함한다 | consumer가 한 config만 찾으면 된다 | connector 별도 배포물 기준과 충돌한다 |
| connector optional dependency 복원은 stream connector config에만 둔다 | package 경계가 명확하다 | consumer가 connector를 쓰려면 connector package도 찾아야 한다 |

선택은 세 번째 방식이다. framework package는 framework와 HTTP client dependency만 복원하고,
Stream Connector optional codec dependency는 connector package config가 복원해야 한다.

### 적용한 리팩토링

- `zlink_framework_cppConfig.cmake.in`에서 Stream Connector MessagePack/Protobuf dependency
  placeholder를 제거했다.
- install consumer regression이 framework package config 안에 connector targets, MessagePack,
  Protobuf dependency 복원이 들어오면 실패하게 했다.

### 수정 후 점검

- framework package config는 `zlink_stream_connector_cppTargets.cmake`,
  `find_dependency(msgpack-cxx CONFIG)`, `find_dependency(Protobuf)`를 포함하지 않는다.
- Stream Connector package config는 connector를 사용할 때 필요한 optional dependency 복원을
  계속 소유한다.
- 이번 보정 뒤 framework package optional connector dependency 잔재의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 9 route client metadata parity 보강

### 발견한 위험 신호

- `.NET` framework의 channel/route client는 gRPC metadata/trailer 대체 흐름을 위해
  metadata 정책과 전달 가능한 key 개념을 공개 기대값으로 둔다.
- C++ framework는 STREAM header와 SPOT actor reply에는 metadata 표면이 있었지만,
  route client가 만드는 framework envelope에는 application metadata를 넣을 fluent 표면이 없었다.
- 이 상태에서는 trace id나 request context를 route request/send와 함께 보존하는 사용자 기대값이
  C++ route client에서 끊긴다. metadata 지식이 STREAM/SPOT에만 있고 channel envelope에는 없는
  정보 누출이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 최상위 metadata forwarding policy 전체를 즉시 구현한다 | `.NET` 표면과 가장 가깝다 | filter, policy, inbound propagation까지 한 번에 커져 Goal 9 경계가 흐려진다 |
| route call object에 `.metadata(key, value)`를 추가하고 envelope가 값을 보존한다 | 호출자 표면이 단순하고 route envelope owner 한 곳에 지식이 모인다 | forwarding policy와 inbound context policy는 후속 반복에서 더 닫아야 한다 |
| metadata를 C++ 초기 범위에서 제외한다고 문서화한다 | 변경이 작다 | `.NET` 사용자 기대값을 축소해 parity 목표와 맞지 않는다 |

선택은 두 번째 방식이다. route call object는 이미 `packet_name`, `timeout`, `submit`을 소유하는
실행 옵션 표면이므로 metadata도 같은 call object에 두는 것이 깊은 모듈에 가깝다. envelope JSON
구조는 `envelope_codec_t`가 계속 소유하고, 사용자는 header JSON이나 runtime backend seam을
직접 알 필요가 없다.

### 적용한 리팩토링

- `envelope_header_t`에 metadata map을 추가하고 `envelope_codec_t` encode/decode가 보존하게 했다.
- `route_send_call_t`, `route_request_call_t`에
  `.metadata(key, value)` fluent API를 추가했다.
- route send, request, typed request submit 경로가 metadata를 envelope header에 담도록 연결했다.
- public contract test가 세 route call object의 metadata fluent surface를 컴파일 계약으로 고정한다.
- channel messaging regression이 route send/request/typed request envelope의 `trace-id` metadata
  보존을 검증한다.

### 수정 후 점검

- 관련 빌드 target `test_cpp_framework_channel_messaging`, `test_cpp_framework_contract_headers`가 통과했다.
- `framework-zlink-channel`, `framework-contract` label이 통과했다.
- 이번 보정 뒤 route client metadata 전달의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 8 handler filter parity 보강

### 발견한 위험 신호

- `.NET` framework는 handler filter를 public contract로 두어 handler 호출 앞뒤의 인증,
  감사, short-circuit 처리를 한 곳에서 연결할 수 있다.
- C++ framework는 HTTP middleware/filter는 갖고 있었지만 일반 channel handler registry에는
  filter 등록 표면이 없었다.
- 이 상태에서는 사용자가 각 handler 안에 공통 처리를 반복하거나, runtime dispatch 세부를
  우회해야 한다. 이는 공통 정책이 handler method마다 새는 정보 은닉 위반이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 각 handler method 안에서 공통 함수를 직접 호출한다 | 구현이 가장 작다 | 공통 정책이 모든 handler에 반복되고 누락을 잡기 어렵다 |
| handler registry 밖에 별도 dispatcher wrapper를 둔다 | 기존 registry 변경이 작다 | descriptor lookup, serializer, DI resolve 순서가 wrapper로 샌다 |
| `handler_registry_t`가 filter chain을 소유하고 `use_filter<TFilter>()`만 공개한다 | 호출자 표면이 단순하고 dispatch 세부가 registry 내부에 남는다 | registry invoke 경로가 filter chain을 구성해야 한다 |

선택은 세 번째 방식이다. handler registry는 이미 handler descriptor와 erased invoker를
소유하므로 filter chain도 같은 모듈 안에 두는 것이 깊은 모듈에 가깝다. 사용자는 filter
타입과 `next()` 호출 여부만 알면 되고, serializer나 DI resolve 순서는 알 필요가 없다.

### 적용한 리팩토링

- `handler_invocation_context_t`, `handler_next_t`, `handler_registry_t::use_filter<TFilter>()`를
  public contract에 추가했다.
- `zlink_framework_options_t::use_filter<TFilter>()`를 추가해 일반 application 설정에서도
  `.NET`처럼 top-level options에서 filter를 연결할 수 있게 했다.
- `handler_registry_t`가 등록 순서대로 filter chain을 실행하고, 마지막 단계에서 기존 erased
  handler invoker를 호출하도록 했다.
- filter는 `next()`를 호출해 계속 진행하거나 reply message를 직접 반환해 short-circuit할 수
  있다.
- unit regression이 filter before/after 호출, descriptor context 전달, short-circuit 시 handler
  미호출을 검증한다.
- module/hosted regression이 `options.use_filter<TFilter>()`로 등록한 filter가 handler group
  dispatch에 적용되는지 검증한다.
- public contract test가 `use_filter<TFilter>()` fluent surface를 컴파일 계약으로 고정한다.

### 수정 후 점검

- handler filter chain은 registry 내부 상태에만 저장되고 public header에는 구현 자료구조를
  노출하지 않는다.
- 이번 보정 뒤 일반 channel handler filter parity의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 8 handler context parity 보강

### 발견한 위험 신호

- `.NET` request/send/publish handler는 payload와 함께 typed context를 받을 수 있다.
- C++ 일반 `handler_registry_t`는 payload-only method만 지원했고, route handler만 별도 context를
  받았다.
- 이 상태에서는 일반 channel handler가 channel 이름, packet 이름, topic 같은 호출 정보를 알기
  위해 raw envelope나 외부 상태에 의존해야 한다. 이는 dispatch 세부가 사용자 handler로 새는
  정보 은닉 위반이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| payload-only handler만 유지한다 | API가 작다 | `.NET` handler contract와 어긋나고 공통 정책 구현이 어렵다 |
| 모든 handler에 raw envelope를 넘긴다 | 정보가 많다 | binding payload와 runtime frame 구조가 public handler 표면으로 샌다 |
| request/send/publish별 typed context overload를 추가한다 | 필요한 정보만 공개하고 기존 handler shape를 유지한다 | overload 수가 늘어난다 |

선택은 세 번째 방식이다. context는 handler registry가 descriptor에서 만들고, 사용자는
`request_context_t`, `send_context_t`, `publish_context_t`만 본다. raw multipart, descriptor map,
serializer 선택은 계속 registry 내부 구현으로 남긴다.

### 적용한 리팩토링

- `handler_context_t`, `request_context_t`, `send_context_t`, `publish_context_t`를 public
  contract에 추가했다.
- `on_request`, `on_send`, `on_event`가 payload-only method와 payload+context method를 모두
  받을 수 있도록 overload를 추가했다.
- context handler 공통 invoker helper를 두어 serializer, DI resolve, error mapping 로직이
  request/send/event별로 흩어지지 않게 했다.
- unit regression이 request/send/event context에 channel, packet, topic 값이 들어오는지 검증한다.
- public contract test가 context overload method pointer shape를 컴파일 계약으로 고정한다.

### 수정 후 점검

- 일반 channel handler는 raw frame 없이 호출 정보를 읽을 수 있다.
- 기존 payload-only handler surface는 그대로 유지된다.
- 이번 보정 뒤 일반 channel handler context parity의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 14 actor metadata forwarding policy 보강

### 발견한 위험 신호

- `.NET` framework는 `ConfigureMetadata(...AddForwardedMetadataKey(...))`로 stream/session metadata 중
  application handler에 넘길 key를 명시한다.
- C++ framework는 `spot_actor_send_context_t`와 `spot_actor_request_context_t`에 metadata
  필드는 있었지만, actor packet dispatch가 항상 빈 metadata context를 만들었다.
- 이 상태에서는 trace id 같은 application metadata가 ActorGateway에서 actor handler로 이어지지
  않고, 사용자가 stream header 전체를 직접 해석하려는 압력이 생긴다. 이는 framework 내부 frame
  구조가 handler로 새는 정보 은닉 위반이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| stream header metadata 전체를 actor context에 넣는다 | 구현이 단순하다 | internal/control metadata까지 handler에 새고 policy가 없다 |
| ActorGateway relay API가 metadata key를 직접 고른다 | relay 경로만 빨리 닫힌다 | metadata 정책이 relay 호출자마다 흩어진다 |
| `message_metadata_policy_t`와 `options.metadata().add_forwarded_metadata_key(...)`를 두고 허용 key만 project한다 | `.NET` 사용 흐름과 맞고 policy 지식이 한곳에 모인다 | runtime bridge가 policy projection을 호출해야 한다 |

선택은 세 번째 방식이다. C++에서는 reflection이나 ASP.NET Core middleware를 복제하지 않고,
명시적인 fluent options builder와 value object로 같은 사용자 경험을 제공한다. actor handler는
raw stream header가 아니라 `spot_actor_message_metadata_t`만 본다.

### 적용한 리팩토링

- `message_metadata_policy_t`를 추가하고 `forward(key)`, `can_forward(key)`,
  `project(metadata)`를 제공했다.
- `zlink_framework_options_t::metadata().add_forwarded_metadata_key(key)`가 metadata policy를 구성하고 DI에서
  `message_metadata_policy_t` singleton으로 resolve될 수 있게 했다.
- `spot_handler_registry_t::invoke_actor_packet(...)`에 metadata overload를 추가해 ActorGateway/
  stream bridge가 policy projection 결과를 actor context로 전달할 수 있게 했다.
- SPOT regression이 허용된 `trace-id`만 actor context에 들어오고 `tenant-id`는 제외되는지
  검증한다.
- module/hosted regression이 options metadata policy가 service provider를 통해 resolve되는지
  검증한다.

### 수정 후 점검

- actor handler는 stream header 전체를 알 필요 없이 허용된 application metadata만 읽는다.
- 기존 metadata 없는 actor packet dispatch는 빈 context를 유지한다.
- 이번 보정 뒤 actor metadata forwarding policy의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 14 actor metadata 조회 표면 보강

### 발견한 위험 신호

- `.NET` framework의 `ZLinkMessageMetadata`는 `Find(key)`로 값을 조회하게 해 handler가
  dictionary 구현 세부에 직접 묶이지 않는다.
- C++ `spot_actor_message_metadata_t`는 forwarding policy를 갖췄지만 handler 예제가
  `values.find(...)`를 직접 호출했다.
- 이 상태에서는 metadata value object가 얕아지고, 호출자가 `std::map` 선택을 public contract로
  받아들이게 된다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `values` 직접 접근만 유지 | 변경이 없다 | metadata container 구현 지식이 handler로 샌다 |
| `values`를 private으로 바꾸고 조회 API만 남긴다 | 정보 은닉이 가장 강하다 | 기존 테스트와 호출자 호환을 깨뜨린다 |
| `values`는 유지하되 `find`, `contains`, `empty`를 추가한다 | 호환을 유지하면서 handler가 value object API를 사용할 수 있다 | map 자체는 아직 public에 남는다 |

선택은 세 번째 방식이다. C++20 포팅 표면에서 `.NET`의 `Find` 사용성을 제공하면서도 기존
호출자를 깨지 않는다. 이후 breaking 변경이 허용되는 시점에는 `values` private 전환을 별도로
검토할 수 있다.

### 적용한 리팩토링

- `spot_actor_message_metadata_t::find`, `contains`, `empty`를 추가했다.
- `message_metadata_policy_t::forward("")`가 직접 사용 경로에서도
  `framework_exception_t(request_protocol_error)`로 실패하게 했다.
- SPOT regression이 조회 API, 허용 key projection, 빈 key 거부를 확인하게 했다.
- contract header regression이 새 조회 API shape을 compile-time으로 고정하게 했다.

### 수정 후 점검

- actor handler는 `std::map` 반복이 필요 없는 단일 key 조회를 value object에 맡긴다.
- `options.metadata().add_forwarded_metadata_key(...)`와 직접 `message_metadata_policy_t::forward(...)`가 같은
  validation 규칙을 쓴다.
- 기존 `values` 반복 사용자는 그대로 컴파일된다.

## 반복 POSD 재리뷰. Goal 21 sample e2e port isolation 보강

### 발견한 위험 신호

- C++ sample process e2e는 Bingo/TicTacToe 기본 포트 대역을 그대로 사용했다.
- 같은 checkout에서 Node sample regression이 동시에 실행되면 동일한 sample port를 선점해
  C++ sample e2e가 `Address already in use`로 실패할 수 있다.
- 테스트가 외부 실행 순서에 의존하면 sample parity evidence가 불안정해지고, 검증자가 unrelated
  프로세스 상태를 알아야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| Node sample이 끝날 때까지 기다린다 | 코드 변경이 없다 | 검증 안정성이 외부 프로세스에 계속 의존한다 |
| 샘플 기본 포트를 완전히 바꾼다 | 즉시 충돌을 피할 수 있다 | 다른 언어 샘플과 문서의 기본 topology 비교가 어려워진다 |
| process e2e runner가 port offset을 주입하고 sample topology가 opt-in으로 적용한다 | 기본 샘플 topology는 유지하면서 테스트 실행만 격리된다 | topology에 작은 env override hook이 생긴다 |

선택은 세 번째 방식이다. 사용자 샘플 기본값은 유지하고, CTest process e2e만 고유 offset을
주입하면 sample 구조 parity와 테스트 격리를 동시에 만족한다.

### 적용한 리팩토링

- Bingo/TicTacToe `sample_topology_t`가 `ZLINK_CPP_SAMPLE_PORT_OFFSET`이 있을 때 endpoint port에
  offset을 적용하게 했다.
- sample-local runner는 현재 full client/server 검증을 수행하지 않으므로 port offset
  후보나 readiness 재시도 로직을 갖지 않는다.
- role executable smoke는 CTest의 sample smoke label로 확인한다.

### 수정 후 점검

- 기본 샘플 실행에는 기존 포트가 그대로 남는다.
- process e2e는 같은 runner 안의 server와 client가 같은 offset topology를 사용한다.
- 다른 언어 샘플이 기본 포트를 사용 중이거나 직전 실패 포트가 `TIME_WAIT` 상태여도 C++
  process e2e가 다른 포트 대역으로 재시도한다.

## 반복 POSD 재리뷰. Goal 19 HTTP app_host port isolation 보강

### 발견한 위험 신호

- `test_cpp_framework_app_host`는 PID 기반 offset으로 HTTP/HTTPS test endpoint를 만들었다.
- 같은 테스트를 짧은 간격으로 반복하면 직전 실행의 `TIME_WAIT` 포트를 다시 선택해
  `Address already in use`로 실패할 수 있었다.
- 테스트 검증자가 OS socket 상태를 알아야 하면 HTTP hosting regression 증거가 불안정해진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 실패하면 수동 재실행한다 | 구현 변경이 없다 | 회귀 gate가 외부 상태에 계속 흔들린다 |
| HTTP host가 port 0을 받고 실제 port를 노출하게 한다 | 가장 근본적이다 | public/runtime API 변경이 커지고 이번 테스트 보정 범위를 넘는다 |
| 테스트 endpoint 생성 시 실제 bind 가능한 port를 probe한다 | public API 변경 없이 gate 안정성을 높인다 | probe와 실제 bind 사이의 짧은 race는 남는다 |

선택은 세 번째 방식이다. HTTP hosting public 표면을 늘리지 않고, 테스트가 스스로 현재 host에서
사용 가능한 port를 고르게 해서 반복 검증 안정성을 높인다.

### 적용한 리팩토링

- `process_unique_port(...)`가 Linux host에서 loopback TCP socket bind probe를 수행해 bind 가능한
  port를 선택하게 했다.
- 기존 PID 기반 salt는 첫 후보 계산에만 사용하고, 충돌하면 다음 후보로 이동한다.

### 수정 후 점검

- HTTP app_host 테스트의 client endpoint와 server listen endpoint는 같은 helper 결과를 사용한다.
- public HTTP hosting API와 sample code는 변경하지 않았다.

## 반복 POSD 재리뷰. Goal 16 registry query filter parity 보강

### 발견한 위험 신호

- `.NET` framework의 registry query는 service summary와 topology 조회에 filter value object를
  받는다.
- C++ `registry_query_t`는 전체 snapshot만 반환해서, 특정 channel/service/state만 보려면
  application code가 직접 반복문과 비교 규칙을 가져야 했다.
- 이 상태에서는 registry query module이 얕아지고, channel 이름과 topology state 비교 지식이
  호출자마다 반복된다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 전체 snapshot API만 유지한다 | 변경이 없다 | `.NET` query 사용성보다 얕고 호출자 필터 반복이 생긴다 |
| SQL-like predicate callback을 받는다 | 유연하다 | public API가 runtime row 구조에 강하게 묶인다 |
| C++ value filter struct와 overload를 추가한다 | `.NET` 사용 흐름과 맞고 비교 규칙을 query module에 모은다 | filter field가 늘면 struct도 확장해야 한다 |

선택은 세 번째 방식이다. C++에서는 nullable record 대신 `std::optional` field를 가진 value
struct가 가장 단순하다. query module이 filter 의미를 소유하므로 호출자는 필요한 조건만 채우면
된다.

### 적용한 리팩토링

- `service_summary_filter_t`와 `topology_filter_t`를 public registry contract에 추가했다.
- `registry_query_t::service_summary(filter)`와 `registry_query_t::topology(filter)` overload를
  추가했다.
- registry runtime query가 name, kind, role, source, state 조건을 한곳에서 적용하게 했다.
- contract header regression이 filter overload shape를 고정하게 했다.
- registry topology regression이 service summary filter, topology filter, 빈 결과를 검증하게 했다.

### 수정 후 점검

- 기존 무인자 `service_summary()`와 `topology()`는 전체 snapshot API로 유지된다.
- filter는 runtime/backend row 타입을 노출하지 않고 public value object만 사용한다.

## 반복 POSD 재리뷰. Goal 9 역할 builder draft surface 보정

### 발견한 위험 신호

- `cpp-framework-interfaces.ko.md`는 `client_capability_builder_t`,
  `publisher_capability_builder_t` 같은 별도 타입과 per-역할 timeout/pending option을
  public pseudo API로 적고 있었다.
- 실제 C++ public header는 `.NET` channel 역할 builder와 같은 수준으로
  `capability_builder_t::bind/connect/use_discovery`만 제공한다.
- 문서가 구현되지 않은 세부 option을 정식 표면처럼 보여 주면 사용자가 얕은 wrapper와 중복
  timeout 정책을 요구하게 된다. 이는 timeout/backpressure 지식이 call object, runtime queue,
  역할 builder로 흩어지는 change amplification이다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 문서에 적힌 per-역할 timeout API를 구현한다 | 문서와 코드는 맞는다 | `.NET` builder에도 없는 표면을 추가하고 timeout 정책이 분산된다 |
| pseudo API를 삭제한다 | 거짓 표면이 사라진다 | channel 역할의 실제 public shape가 문서에서 약해진다 |
| pseudo API를 실제 `capability_builder_t`와 call/runtime timeout 정책으로 보정한다 | 문서와 코드가 맞고 public 표면을 늘리지 않는다 | per-역할 timeout이 없다는 설명을 유지해야 한다 |

선택은 세 번째 방식이다. C++ channel 역할 builder는 endpoint/discovery만 소유하고,
request timeout은 call object가, pending queue 한도는 runtime builder가 소유한다.

### 적용한 리팩토링

- `cpp-framework-interfaces.ko.md`의 channel 역할 pseudo API를 실제
  `capability_builder_t` 표면으로 정리했다.
- 문서에 per-역할 timeout/pending option을 만들지 않는 이유와 실제 소유자를 명시했다.

### 수정 후 점검

- draft 문서는 존재하지 않는 `client_capability_builder_t::send_timeout(...)` 같은 public API를
  더 이상 정식 표면처럼 보여 주지 않는다.
- 이번 보정 뒤 channel 역할 builder 문서/구현 불일치의 즉시 수정 이슈는 0개다.

## 반복 POSD 재리뷰. Goal 16 remote registry query client parity 보강

### 발견한 위험 신호

- `.NET` framework는 `IZLinkRegistryQueryClient`와
  `IZLinkRegistryQueryClientOptions.Endpoint`로 remote registry topology snapshot을 조회한다.
- C++ draft도 remote registry query client를 필수 표면으로 적고 있었지만, framework public
  header에는 in-process `registry_query_t`만 있었다.
- 사용자가 C++ binding의 `zlink::service::registry_query_client_t`, native context, native
  topology filter/model을 직접 조합해야 하면 framework registry module이 얕아지고 backend
  transport 지식이 application code로 새어 나온다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| binding query client를 그대로 문서에 안내한다 | 구현이 없다 | framework 사용자가 native context와 binding model을 알아야 한다 |
| `registry_query_t`에 remote endpoint option을 섞는다 | 타입 수가 늘지 않는다 | in-process snapshot query와 remote transport lifecycle이 한 타입에 섞인다 |
| 별도 `registry_query_client_t`와 options value를 둔다 | `.NET` query client 역할과 맞고 native transport를 숨긴다 | 작은 public 타입이 추가된다 |

선택은 세 번째 방식이다. in-process query와 remote client는 lifecycle과 실패 방식이 다르므로
분리한다. C++에서는 `registry_query_client_options_t`와 RAII client가 `.NET` options/service
역할을 대신한다.

### 적용한 리팩토링

- `registry_query_client_options_t`와 `registry_query_client_t`를 public registry contract에
  추가했다.
- client 구현은 native context와 `zlink::service::registry_query_client_t`를 pimpl 내부에
  숨긴다.
- native topology entry/filter는 framework `topology_entry_t`, `topology_filter_t`로 변환한다.
- 연결되지 않은 client는 `disconnected`, 빈 endpoint는 `request_protocol_error` result로
  닫히게 했다.
- contract header regression이 query client shape를 고정하고, registry topology regression이
  실제 native registry/discovery/provider를 framework query client로 조회한다.
- `cpp-registry.ko.md`에 remote query client 사용 흐름과 실패 의미를 추가했다.

### 수정 후 점검

- application code는 remote registry 조회를 위해 native context, native filter, binding
  exception type을 알 필요가 없다.
- in-process `registry_query_t`는 기존 snapshot API로 유지되고, remote transport lifecycle은
  `registry_query_client_t`가 소유한다.

## 반복 POSD 재리뷰. Framework dispatch options parity 보강

### 발견한 위험 신호

- `.NET` framework의 top-level options는 `ConfigureDispatch(...)`로 Spot/STREAM dispatch mode,
  unhandled request/send/publish 정책, message flow diagnostics 설정을 받는다.
- C++ `zlink_framework_options_t`에는 handler coroutine worker 수는 있었지만, dispatch 정책을
  사용자 관점에서 한 곳에 모아 두는 public value가 없었다.
- 이 상태에서는 사용자가 unhandled dispatch 정책과 diagnostics sampling 의미를 handler, logging,
  runtime 내부 동작에 흩어 이해해야 한다. 이는 호출자 부담 증가와 정보 누수다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 구현이 연결될 때까지 문서에서만 보류한다 | public API 증가가 없다 | `.NET` top-level configuration parity가 계속 비어 있다 |
| `.NET`처럼 여러 interface 객체를 만든다 | 원본 구조와 이름이 가깝다 | C++에서 불필요한 얕은 wrapper와 lifetime 관리가 늘어난다 |
| `dispatch_options_t` value를 `configure_dispatch(...)` 람다에 넘긴다 | C++20 idiom에 맞고 호출자 표면이 작다 | runtime 연결이 늘면 value field를 확장해야 한다 |

선택은 세 번째 방식이다. dispatch 설정은 application configuration value이므로 C++에서는
interface graph보다 value snapshot이 더 깊은 표면이다. native dispatch token, queue slot,
handler lookup table은 계속 runtime owner 안에 숨긴다.

### 적용한 리팩토링

- `dispatch_mode_t`, `unhandled_dispatch_action_t`, `message_flow_log_mode_t`,
  `unhandled_dispatch_options_t`, `dispatch_diagnostics_options_t`,
  `dispatch_options_t`를 public dispatch contract에 추가했다.
- `zlink_framework_options_t::configure_dispatch(...)`와 `dispatch_options()` snapshot getter를
  추가했다.
- diagnostics sample rate는 `0.0`에서 `1.0` 사이로 검증한다.
- contract header regression이 `configure_dispatch(...)`와 snapshot 반환형을 고정한다.
- module/options regression이 Spot/STREAM mode, unhandled policy, diagnostics options와 invalid
  sample rate 실패를 검증한다.
- `cpp-framework-interfaces.ko.md`의 high-level options 예제에 dispatch 설정을 추가했다.

### 수정 후 점검

- application code는 dispatch 옵션을 설정하기 위해 runtime dispatcher, native callback,
  queue internals를 알 필요가 없다.
- 현재 runtime 동작에 연결되지 않은 lower-level dispatch implementation detail은 public API로
  노출하지 않았다.

## 반복 POSD 재리뷰. High-level send/publish handler options parity 보강

### 발견한 위험 신호

- `.NET` channel builders는 request handler뿐 아니라 send handler와 publish handler도 같은
  application configuration 흐름에서 등록한다.
- C++ high-level `options.handlers()`는 request/reply handler만 group에 설치했다. send/publish
  handler는 낮은 수준 `handler_registry_t::on_send(...)`, `on_event(...)`를 직접 사용해야 했다.
- 이 상태에서는 샘플과 사용자 설정이 handler kind별 등록 위치를 기억해야 하므로 configuration
  표면이 얕아지고, serializer 자동 등록 규칙도 request handler에만 보이는 불일치가 생긴다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| channel builder에 `add_send_handler`/`add_publish_handler`를 직접 추가한다 | `.NET` 이름과 가장 가깝다 | C++의 group 기반 설치 규칙과 handler registry 설치 로직이 channel builder로 퍼진다 |
| 사용자가 낮은 수준 `handler_registry_t`를 직접 쓰게 둔다 | 변경이 없다 | high-level options가 request-only가 되어 `.NET` 사용 흐름과 어긋난다 |
| `handler_options_builder_t::group(...)` 아래에 `add_send`/`add_publish`를 둔다 | group 기반 깊은 모듈을 유지하고 serializer 자동 등록을 공유한다 | method 이름이 `.NET`과 1:1은 아니다 |

선택은 세 번째 방식이다. C++ high-level configuration은 handler group을 기준으로 channel에
연결하므로, handler kind별 설치도 같은 builder 안에 모으는 편이 정보 은닉에 맞다.

### 적용한 리팩토링

- `handler_options_builder_t::group(group_name).add_send<THandler>()`를 추가했다.
- `handler_options_builder_t::group(group_name).add_publish<THandler>()`를 추가했다.
- send handler는 `message_type`, publish handler는 `event_type`을 읽어 JSON serializer 자동
  등록과 handler registry 설치를 수행한다.
- topic 이름은 handler의 `topic_name`이 있으면 사용하고, 없으면 payload type의 message name을
  사용하도록 helper를 일반화했다.
- contract header regression이 새 high-level handler options 표면을 고정한다.
- module/options regression이 request/send/publish handler가 같은 group/channel/filter 경로로
  호출되는지 확인한다.
- `cpp-framework-interfaces.ko.md`의 high-level options 설명과 예제를 request/send/publish
  등록 흐름으로 갱신했다.

### 수정 후 점검

- application code는 send/publish handler를 등록하기 위해 낮은 수준 handler registry 설치 순서나
  serializer 등록 순서를 알 필요가 없다.
- handler registry 자체의 request/send/event public contract는 그대로 재사용하고, 중복 dispatcher
  abstraction은 추가하지 않았다.

## 반복 POSD 재리뷰. High-level fanout channel options parity 보강

### 발견한 위험 신호

- `.NET`의 `AddFanoutChannel(...)`은 publisher, subscriber, publish handler group을 같은 사용자
  설정 흐름에서 표현한다.
- C++ high-level options에는 `add_fanout_channel(...).enable_publisher(...)`만 있어 publisher-only 채널은
  쉽게 만들 수 있었지만, subscriber role과 publish handler group 연결은 낮은 수준
  `channel_builder_t`와 handler group 규칙을 함께 알아야 했다.
- 이 상태는 fanout channel이라는 개념을 사용자에게 충분히 숨기지 못하고, publish handler는
  `add_client_server_channel`에 억지로 붙여도 테스트가 통과하는 얕은 표면을 만든다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 `publisher_channel_builder_t`에 subscriber와 handler group을 추가한다 | API 변경이 작다 | 이름이 publisher-only라 fanout 의도가 흐려진다 |
| `.NET`처럼 `EnablePublisher`, `EnableSubscriber` 람다 builder를 그대로 옮긴다 | 원본과 이름이 가깝다 | C++ options layer에 역할 pass-through 메서드가 늘어난다 |
| `fanout_channel_builder_t`를 추가하고 `add_fanout_channel()`만 남긴다 | fanout 의도가 드러나고 이름이 하나로 모인다 | 기존 publisher-only 호출을 모두 바꿔야 한다 |

선택은 세 번째 방식이다. C++ options layer는 낮은 수준 역할 builder를 그대로 노출하지 않고,
fanout channel의 publisher/subscriber/handler group 연결을 하나의 깊은 builder에 모은다.

### 적용한 리팩토링

- `fanout_channel_builder_t`를 추가했다.
- `add_fanout_channel(...).enable_publisher(...)`, `.enable_subscriber()`, `.enable_subscriber(endpoint)`,
  `.use_handler_group(...)`을 제공한다.
- publisher-only 별칭은 두지 않는다. publisher 역할은
  `add_fanout_channel(...).enable_publisher(...)`로 표현한다.
- contract header regression이 fanout builder 반환형과 fluent 메서드를 고정한다.
- module/options regression이 event channel의 publisher bind endpoint, subscriber connect endpoint,
  publish handler group 호출을 함께 검증한다.
- `cpp-framework-interfaces.ko.md`의 high-level options 예제를 fanout channel과 events handler
  group 기준으로 갱신했다.

### 수정 후 점검

- application code는 publish handler를 fanout 채널에 연결하기 위해 낮은 수준
  `channel_builder_t::enable_subscriber(...)` 호출 순서를 알 필요가 없다.
- publisher-only 샘플도 `add_fanout_channel(...).enable_publisher(...)`를 사용한다.
  일반 fanout 예시는 같은 builder에서 subscriber와 handler group을 함께 보여준다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. High-level dealer mesh channel options parity 보강

### 발견한 위험 신호

- 과거 `.NET` dealer-mesh builder는 client bind, manual connection, handler group을
  dealer mesh channel이라는 사용자 개념 안에서 표현했다.
- C++ runtime은 client 역할의 bind/connect endpoint와 dealer mesh pending owner를 이미
  가지고 있었지만, high-level `zlink_framework_options_t`에는 이 의도를 드러내는
  `add_dealer_mesh_channel(...)` 표면이 없었다.
- 사용자가 낮은 수준 `channel_builder_t::enable_client(...)`를 직접 조합해야 하면 dealer mesh와
  일반 client/server channel의 차이가 options layer 밖으로 새어 나간다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `add_client_server_channel(...).enable_client(endpoint)`를 dealer mesh 용도로 재사용한다 | public 타입이 늘지 않는다 | 이름과 의미가 맞지 않아 client/server와 dealer mesh 의도가 섞인다 |
| 낮은 수준 `zlink_builder_t::channel(...)` 사용을 문서화한다 | 구현 변경이 없다 | high-level options가 `.NET` configuration parity를 제공하지 못한다 |
| `dealer_mesh_channel_builder_t`를 추가한다 | dealer mesh 의도가 드러나고 server/client/handler group을 한 곳에 모은다 | public builder 타입이 하나 늘어난다 |

선택은 세 번째 방식이다. dealer mesh는 `.NET`에서도 별도 channel kind이므로 C++ options layer도
같은 사용자 개념을 제공하되, socket/pending request 구현은 runtime 내부에 숨긴다.

### 적용한 리팩토링

- `dealer_mesh_channel_builder_t`를 추가했다.
- `add_dealer_mesh_channel(...).enable_server(...)`, `.enable_client(...)`, `.use_handler_group(...)`을 제공한다.
- builder는 낮은 수준 `channel.enable_client(...)`에 client bind/connect endpoint를 사상한다.
- contract header regression이 dealer mesh builder 반환형과 fluent 메서드를 고정한다.
- module/options regression이 dealer mesh client bind/connect snapshot과 handler group 연결 호출을
  검증한다.
- `cpp-framework-interfaces.ko.md`의 high-level options 예제에 dealer mesh channel을 추가했다.

### 수정 후 점검

- application code는 dealer mesh channel을 만들기 위해 client 역할의 bind/connect 조합이나
  pending request owner를 알 필요가 없다.
- client/server, fanout, dealer mesh, route mesh channel 의도가 options layer에서 서로 분리된다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Discovery-backed client validation parity 보강

### 발견한 위험 신호

- `.NET` registration validation은 channel client role이 discovery 또는 manual connection 같은
  peer 획득 경로 없이 등록되면 startup 단계에서 실패시킨다.
- C++ high-level `add_client_server_channel(...).enable_client()`는 client 역할만 enabled로 만들고
  discovery/manual endpoint를 명시하지 않았다. 이 경우 오류가 설정 시점이 아니라 실제 send/request
  호출 시점의 disconnected 결과로 밀린다.
- fanout `subscriber()`도 endpoint 없이 role만 enabled로 만들 수 있었다. 이는 호출자가 설정 오류와
  런타임 연결 오류를 구분해야 하는 부담을 만든다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현재처럼 호출 시 disconnected를 반환한다 | 구현 변경이 작다 | 설정 오류를 늦게 발견하고 `.NET` startup validation 기대와 다르다 |
| `enable_client()`/`enable_subscriber()`를 금지하고 endpoint 인자만 허용한다 | 모호함이 없다 | registry discovery 기반 샘플 설정이 장황해지고 `.NET EnableClient()` 경험과 멀어진다 |
| 인자 없는 `enable_client()`/`enable_subscriber()`를 discovery-backed로 정의하고 discovery가 없으면 `apply()`에서 실패시킨다 | 사용자 의도가 분명하고 설정 오류를 startup에서 잡는다 | options state가 discovery-backed 역할을 추적해야 한다 |

선택은 세 번째 방식이다. 인자 없는 role 활성화는 registry discovery 기반 연결이라는 의미로 닫고,
manual 연결은 endpoint 인자를 받는 overload로 분리한다.

### 적용한 리팩토링

- `discovery_options_builder_t::add(...)`가 registry discovery endpoint를 options state에도 기록한다.
- `add_client_server_channel(...).enable_client()`는 root discovery endpoint가 있으면 client 역할에 자동 연결을 적용한다.
- `add_fanout_channel(...).enable_subscriber()`는 root discovery endpoint가 있으면 subscriber 역할에 자동 연결을 적용한다.
- discovery-backed 역할이 있는데 registry discovery endpoint가 없으면 `zlink_framework_options_t::apply()`가
  `request_protocol_error`로 실패한다.
- module/options regression이 정상 `.enable_client()` snapshot의 discovery flag와 discovery 없는 `.enable_client()` 실패를
  검증한다.
- `cpp-framework-interfaces.ko.md`와 `cpp-channel-messaging.ko.md`에 discovery-backed role 규칙을 적었다.

### 수정 후 점검

- application code는 endpoint 없는 client/subscriber가 어떤 연결 방식을 의미하는지 따로 추론하지 않아도 된다.
- 설정 오류는 send/request 호출 시점이 아니라 framework options apply 단계에서 드러난다.
- manual endpoint와 discovery-backed role은 public API에서 분리된다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Stream packet session 중복 등록 검증 보강

### 발견한 위험 신호

- `.NET` registration validation은 하나의 stream node가 session을 두 번 등록하면 startup 전에
  설정 오류로 실패시킨다.
- C++ high-level `add_stream_node(...).register_session(...)`은 여러 번 호출하면 마지막 값으로 덮어쓸 수
  있었다. 이 동작은 사용자가 실수로 중복 등록한 경우를 조용히 숨긴다.
- 설정 실수를 덮어쓰는 방식은 오류를 늦게 발견하게 하고, stream node의 public 의미를 얕게 만든다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 마지막 `register_session(...)` 호출이 이기게 둔다 | 기존 동작을 유지한다 | 중복 session 등록 실수를 숨기고 `.NET` validation parity와 다르다 |
| `apply()`에서 stream snapshot을 검사한다 | 모든 stream 설정을 한 번에 검증할 수 있다 | 중복 호출 위치와 원인을 늦게 알려준다 |
| 두 번째 `register_session(...)` 호출에서 바로 실패시킨다 | 오류 위치가 명확하고 stream node의 단일 session 규칙을 표면에서 닫는다 | builder가 session 설정 여부를 추적해야 한다 |

선택은 세 번째 방식이다. stream node는 packet session을 하나만 가진다는 의미를 builder가 직접
지키는 편이 호출자에게 가장 분명하다.

### 적용한 리팩토링

- `stream_node_options_builder_t`가 packet session 설정 여부를 추적한다.
- 같은 builder에서 `register_session(...)`을 두 번 호출하면 `request_protocol_error`로 실패한다.
- module/options regression이 중복 packet session 등록 실패를 검증한다.
- `cpp-stream.ko.md`와 `cpp-framework-interfaces.ko.md`에 단일 packet session 규칙을 적었다.

### 수정 후 점검

- application code는 중복 session 등록이 마지막 값으로 조용히 덮이는지 걱정하지 않아도 된다.
- 오류는 stream runtime 시작 뒤가 아니라 options 작성 단계에서 드러난다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Dealer mesh peer path validation 보강

### 발견한 위험 신호

- `.NET` registration validation은 client role이 실제 peer 획득 경로 없이 등록되면 startup 단계에서
  설정 오류로 실패시킨다.
- C++ high-level `add_dealer_mesh_channel(...)`은 생성자에서 channel client role을 등록하지만,
  `bind(...)`나 `connect(...)` 없이도 options 적용이 가능했다.
- 이 상태는 사용자가 설정 실수와 실제 연결 실패를 send/request 시점까지 구분해야 하게 만든다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현재처럼 runtime 호출 시점의 disconnected 결과에 맡긴다 | 구현 변경이 작다 | 설정 오류가 늦게 드러나고 `.NET` startup validation 기대와 다르다 |
| 생성자에서 channel 등록을 하지 않고 `bind(...)`/`connect(...)`가 있을 때만 등록한다 | peer path 없는 channel이 만들어지지 않는다 | `add_dealer_mesh_channel(...)` 호출 자체가 조용히 무시될 수 있다 |
| 선언된 dealer mesh channel과 peer path 보유 channel을 options state에서 추적하고 `apply()`에서 검증한다 | 사용자 의도를 보존하면서 설정 오류를 startup에서 잡는다 | options state가 validation bookkeeping을 조금 더 가진다 |

선택은 세 번째 방식이다. channel 선언 의도는 유지하고, `bind(...)` 또는 `connect(...)`가 없는
선언은 framework options 적용 시점에 명확히 실패시킨다.

### 적용한 리팩토링

- `add_dealer_mesh_channel(...)` 호출은 dealer mesh 선언을 options state에 기록한다.
- `bind(...)`와 `connect(...)`는 해당 channel이 peer path를 가진 것으로 기록한다.
- peer path 없는 dealer mesh channel이 있으면 `zlink_framework_options_t::apply()`가
  `request_protocol_error`로 실패한다.
- module/options regression이 `add_dealer_mesh_channel(...)`만 선언한 뒤 `apply()`하는 경우의 실패를
  검증한다.
- `cpp-framework-interfaces.ko.md`와 `cpp-channel-messaging.ko.md`에 dealer mesh peer path 규칙을
  적었다.

### 수정 후 점검

- application code는 dealer mesh channel 선언이 실제 연결 경로를 갖는지 별도로 추적하지 않아도 된다.
- 설정 오류는 send/request 호출 시점이 아니라 framework options apply 단계에서 드러난다.
- channel 선언과 peer path 검증 책임이 options layer에 모여 public API가 얕아지지 않는다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Dispatch option validation parity 보강

### 발견한 위험 신호

- `.NET` registration validation은 send와 publish의 unhandled 정책에 `ReplyError`를 금지한다.
  두 메시지 종류에는 reply path가 없기 때문이다.
- C++ `configure_dispatch(...)`는 diagnostics sample rate 범위만 확인했고, send/publish에
  `reply_error`를 설정해도 통과했다.
- C++ sample rate 검증은 NaN을 별도로 막지 않아 `NaN < 0.0`과 `NaN > 1.0`이 모두 false인
  상태를 허용할 수 있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| runtime dispatch에서 의미 없는 `reply_error`를 drop으로 해석한다 | 실행은 계속된다 | 설정 오류를 숨기고 사용자가 정책 의미를 잘못 이해할 수 있다 |
| enum에서 `reply_error`를 request 전용 타입으로 분리한다 | 타입으로 오류를 막을 수 있다 | public API 변경 폭이 크고 기존 options 구조와 맞지 않는다 |
| `configure_dispatch(...)` validation에서 send/publish `reply_error`와 NaN sample rate를 거부한다 | `.NET` startup validation과 맞고 변경 범위가 작다 | enum 자체는 여전히 공통 타입이다 |

선택은 세 번째 방식이다. C++ public enum 구조를 유지하면서 의미 없는 조합은 설정 단계에서
명확히 실패시킨다.

### 적용한 리팩토링

- `validate_dispatch_options(...)`가 send/publish `reply_error`를 `request_protocol_error`로
  거부한다.
- diagnostics sample rate가 NaN이면 `request_protocol_error`로 실패한다.
- module/options regression이 send `reply_error`, publish `reply_error`, NaN sample rate 실패를
  검증한다.
- `cpp-framework-interfaces.ko.md`에 reply path가 없는 메시지 종류의 정책 제한을 적었다.

### 수정 후 점검

- application code는 send/publish에서 응답을 보낼 수 없는 정책 조합을 runtime까지 가져가지 않는다.
- validation 책임은 dispatch options layer에 모여 handler/runtime 호출자가 방어 코드를 알 필요가 없다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Handler group kind validation 보강

### 발견한 위험 신호

- `.NET` registration validation은 client/server channel에 publish handler group을 매핑하거나
  fanout channel에 send/request handler group을 매핑하는 설정을 startup 단계에서 실패시킨다.
- C++ high-level options는 handler group 이름만 추적했고, group 안의 handler kind와 channel kind의
  호환성을 검증하지 않았다.
- 이 구조는 fanout, client/server, dealer mesh의 의미 차이를 handler registry/runtime 쪽으로
  밀어 사용자가 잘못된 group 매핑을 늦게 발견하게 만든다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| runtime dispatch에서 맞지 않는 handler는 호출되지 않게 둔다 | 변경이 작다 | 설정 실수를 숨기고 `.NET` startup validation 기대와 다르다 |
| 각 channel builder가 group 이름 규칙을 강제한다 | 구현이 단순하다 | group 이름에 의미가 새고 사용자 naming에 불필요한 제약을 만든다 |
| handler group state가 group의 handler kind와 channel의 허용 kind를 함께 검증한다 | 이름 규칙 없이 의미를 검증하고 지연 설치 순서를 유지한다 | handler group state가 작은 compatibility metadata를 가진다 |

선택은 세 번째 방식이다. group 이름은 사용자 의도 표현으로 남기고, 실제 호환성은 options layer가
handler kind 정보로 판단한다.

### 적용한 리팩토링

- handler group installer에 request/send/publish kind를 기록한다.
- channel builder의 `handler_group(...)`은 해당 channel이 허용하는 handler kind를 함께 등록한다.
- group을 먼저 등록한 뒤 channel에 매핑하는 경우와 channel에 먼저 매핑한 뒤 group을 등록하는 경우를
  모두 같은 compatibility check로 막는다.
- module/options regression이 fanout-send group, client/server-publish group 오매핑 실패를 검증한다.
- `cpp-framework-interfaces.ko.md`에 channel 종류별 handler group 규칙을 적었다.

### 수정 후 점검

- application code는 group 이름에 숨은 규칙을 맞추지 않아도 되고, 잘못된 handler kind 조합은
  options 작성 시점에 드러난다.
- 지연 설치 구조는 유지하되 validation 지식이 handler group state에 모여 있다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Inbound channel handler exposure validation 보강

### 발견한 위험 신호

- `.NET` registration validation은 client/server server가 handler group이나 typed handler 없이
  등록되면 startup 단계에서 실패시킨다. 단, SPOT route channel로 accept된 channel은 route ingress로
  쓰일 수 있어 예외다.
- `.NET` fanout subscriber도 publish handler group이나 typed publish handler 없이 등록되면
  startup 단계에서 실패한다.
- C++ high-level options는 server/subscriber role만 켜고 handler group 없이 `apply()`할 수 있었다.
  이 경우 설정 오류가 실제 메시지 dispatch 시점까지 밀린다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| runtime dispatch에서 handler not found로 처리한다 | 구현 변경이 작다 | inbound role 설정 오류가 늦게 드러나고 `.NET` startup validation과 다르다 |
| channel builder가 `server(endpoint, group)`처럼 group을 필수 인자로 받게 한다 | type surface에서 누락을 줄인다 | fluent builder가 장황해지고 SPOT route 예외를 표현하기 어렵다 |
| options state가 inbound role과 accepted SPOT route channel을 추적하고 `apply()`에서 handler exposure를 검증한다 | 기존 fluent 표면을 유지하고 예외 규칙을 한 곳에 모은다 | options state와 handler group state 사이의 검증 연결이 필요하다 |

선택은 세 번째 방식이다. role 선언과 handler exposure는 options layer의 같은 startup validation으로
닫고, public API에는 추가 인자 부담을 만들지 않는다.

### 적용한 리팩토링

- client/server `server(...)` 선언을 options state에 기록한다.
- fanout `subscriber(...)` 선언을 options state에 기록한다.
- SPOT `accept_routes_from_channel(...)` 선언을 accepted route channel로 기록한다.
- `zlink_framework_options_t::apply()`가 client/server server의 request/send exposure와 fanout
  subscriber의 publish exposure를 검증한다.
- module/options regression이 handler group 없는 server 실패, SPOT route accept 예외, handler group 없는
  subscriber 실패를 검증한다.
- `cpp-framework-interfaces.ko.md`에 inbound role handler exposure 규칙을 적었다.

### 수정 후 점검

- application code는 inbound role만 켜고 handler를 빼먹은 상태를 runtime까지 가져가지 않는다.
- SPOT route ingress 예외는 options state에서 명시적으로 표현되어 호출자가 내부 route dispatch를 알 필요가 없다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Route mesh bind validation 보강

### 발견한 위험 신호

- `.NET` registration validation은 route mesh channel이 bind endpoint 없이 등록되면 startup 단계에서
  실패시킨다.
- C++ high-level `add_route_mesh(...)`은 생성자에서 low-level route channel action을 등록하지만,
  `bind(...)` 없이 routing id나 manual connection만 설정해도 options 적용이 가능했다.
- route mesh channel은 local route endpoint를 열어야 하는 surface인데, bind 누락을 runtime 초기화나
  send/request 시점으로 미루면 사용자가 설정 오류와 연결 오류를 구분해야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| runtime route channel 초기화에서 bind 누락을 실패시킨다 | low-level validation만 추가하면 된다 | high-level options 오류가 늦게 드러나고 `.NET` startup validation과 다르다 |
| `add_route_mesh(...)` 생성자에서 action을 등록하지 않고 `bind(...)`가 있을 때만 등록한다 | bind 없는 route channel은 만들어지지 않는다 | 사용자 선언이 조용히 무시될 수 있어 설정 실수를 숨긴다 |
| 선언된 route mesh channel과 bind 보유 channel을 options state에서 추적하고 `apply()`에서 검증한다 | 사용자 의도를 보존하면서 startup validation으로 닫는다 | options state가 route mesh validation metadata를 가진다 |

선택은 세 번째 방식이다. route mesh 선언은 그대로 유지하고, bind endpoint가 없는 선언은 framework
options 적용 시점에 명확히 실패시킨다.

### 적용한 리팩토링

- `add_route_mesh(...)` 호출은 route mesh 선언을 options state에 기록한다.
- `bind(...)` 호출은 해당 route mesh channel이 bind endpoint를 가진 것으로 기록한다.
- bind 없는 route mesh channel이 있으면 `zlink_framework_options_t::apply()`가
  `request_protocol_error`로 실패한다.
- module/options regression이 routing id만 설정한 route mesh channel의 apply 실패를 검증한다.
- `cpp-framework-interfaces.ko.md`와 `cpp-channel-messaging.ko.md`에 route mesh bind 필수 규칙을 적었다.

### 수정 후 점검

- application code는 route mesh channel이 local route endpoint를 여는지 별도로 추적하지 않아도 된다.
- 설정 오류는 route runtime 초기화 뒤가 아니라 framework options apply 단계에서 드러난다.
- route mesh 선언과 validation 책임이 options layer에 모여 public API가 얕아지지 않는다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. SPOT node 역할 validation 보강

### 발견한 위험 신호

- `.NET` registration validation은 SPOT node가 router 또는 pub/sub 역할 없이 등록되면 startup
  단계에서 실패시킨다.
- C++ high-level `add_spot_mesh(...).add_node(...)`는 discovery view를 자동으로 붙이지만, discovery는 실행
  역할이 아니다. `enable_router(...)`와 `enable_pub_sub(...)`가 모두 빠져도 options 적용이
  가능했다.
- 역할 없는 SPOT node는 실제 메시지 ingress/egress 역할이 불분명해지고, 사용자가 discovery
  설정과 runtime 역할을 혼동하게 만든다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| runtime spot initializer에서 역할 없는 node를 실패시킨다 | runtime snapshot 기준으로 판단할 수 있다 | high-level 설정 오류가 늦게 드러난다 |
| `add_spot_mesh(...).add_node(...)`가 기본 router 역할을 자동으로 켠다 | 사용자 코드가 짧다 | endpoint를 추측할 수 없어 호출자에게 숨은 default를 만든다 |
| options state가 SPOT node 선언과 router/pub-sub 역할 보유 여부를 추적하고 `apply()`에서 검증한다 | `.NET` startup validation과 맞고 discovery와 역할 의미를 분리한다 | options state가 작은 validation metadata를 가진다 |

선택은 세 번째 방식이다. discovery는 peer discovery 의미로 유지하고, runtime 역할은
`enable_router(...)` 또는 `enable_pub_sub(...)`로 명시하게 한다.

### 적용한 리팩토링

- SPOT node 선언을 options state에 기록한다.
- `enable_router(...)`와 `enable_pub_sub(...)` 호출은 해당 node가 runtime 역할을 가진 것으로
  기록한다.
- runtime 역할 없는 SPOT node가 있으면 `zlink_framework_options_t::apply()`가
  `request_protocol_error`로 실패한다.
- module/options regression이 역할 없이 spot만 등록한 node의 apply 실패를 검증한다.
- `cpp-framework-interfaces.ko.md`에 SPOT node 역할 필수 규칙을 적었다.

### 수정 후 점검

- application code는 discovery view와 SPOT runtime 역할을 혼동하지 않아도 된다.
- 설정 오류는 SPOT runtime 시작 뒤가 아니라 framework options apply 단계에서 드러난다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Accepted SPOT route channel validation 보강

### 발견한 위험 신호

- 과거 `.NET` registration validation은 명시적으로 accepted SPOT route channel이
  router-capable ingress인지 startup 단계에서 검증했다.
- C++ high-level `accept_routes_from_channel(...)`은 channel 이름을 handler exposure 예외로만
  기록했다. 그래서 router 역할 누락, fanout/dealer mesh 오용, 미등록 channel, 모호한
  channel 이름, registry discovery 누락이 더 늦은 단계로 흘러갈 수 있었다.
- route relay는 registry snapshot과 route channel 의미에 의존한다. 이 지식을 호출자나 runtime
  실패 메시지에 흩어 두면 정보 은닉이 깨진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| low-level `spot_node_builder_t`에서만 실패시킨다 | runtime snapshot과 가깝다 | high-level fluent options에서 생긴 오류를 늦게 보고한다 |
| `accept_routes_from_channel(...)`이 channel 종류를 직접 조회하지 않고 모든 channel을 허용한다 | public API가 느슨하다 | fanout/dealer mesh를 route ingress로 쓰는 오구성을 숨긴다 |
| options state가 channel 종류와 node별 accepted route channel을 추적하고 `apply()`에서 검증한다 | `.NET` startup validation과 맞고 오류 정의가 한 곳에 모인다 | options state가 validation metadata를 더 가진다 |

선택은 세 번째 방식이다. route ingress 정책은 framework options layer의 설정 의미이므로,
runtime socket 오류보다 먼저 사용자 설정 오류로 닫는 편이 호출자 부담을 줄인다.

### 적용한 리팩토링

- client/server channel, fanout channel, route mesh channel, dealer mesh channel 선언을
  options state에서 구분해 추적한다.
- `accept_routes_from_channel(...)`은 node별 accepted route channel로 기록한다.
- accepted route channel을 가진 node가 router 역할을 켜지 않았거나, 대상 channel이
  client/server 또는 route mesh가 아니거나, registry discovery가 없으면 `apply()`가
  `request_protocol_error`로 실패한다.
- module/options regression이 router 역할 누락, unknown channel, fanout channel 오용,
  ambiguous channel 이름, registry discovery 누락을 검증한다.
- `cpp-framework-interfaces.ko.md`와 `cpp-channel-messaging.ko.md`에 accepted route channel
  validation 규칙을 적었다.

### 수정 후 점검

- SPOT route ingress 정책이 fluent options validation에 모여 호출자가 내부 relay 실패를 해석하지
  않아도 된다.
- fanout/dealer mesh를 route ingress로 재사용하는 얕은 표면을 막았다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Fluent options 입력 guard 보강

### 발견한 위험 신호

- `.NET` builder는 channel 이름, handler group 이름, endpoint, SPOT/STREAM node 이름 같은
  public 설정 입력이 비어 있으면 즉시 configuration error로 실패한다.
- C++ high-level fluent options 일부는 빈 문자열이나 공백 문자열을 low-level builder까지 넘길 수
  있었다. low-level에서 일부를 다시 막더라도, 오류 위치가 API 표면마다 달라져 호출자가 원인을
  추적해야 한다.
- public 설정 값 검증이 흩어지면 정보 은닉이 약해지고, endpoint가 비어 있는 상태 같은 오류가
  socket/runtime 실패처럼 보일 수 있다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| low-level builder 검증에만 의존한다 | 중복 검증이 적다 | high-level fluent API 사용자는 오류를 늦게 보거나 표면별로 다른 메시지를 본다 |
| 각 method에 직접 `empty()` 검사를 반복한다 | 구현이 단순하다 | 검증 기준이 반복되어 whitespace 처리 같은 정책이 새기 쉽다 |
| high-level options 내부 helper로 blank 검증을 모으고 각 fluent method가 호출한다 | 검증 정책이 한 곳에 모이고 caller 오류를 이른 시점에 닫는다 | method별 호출 지점이 늘어난다 |

선택은 세 번째 방식이다. 입력 정책은 public fluent layer의 계약이므로, 작은 helper로 기준을 모으고
각 method는 자신의 의미에 맞는 오류 메시지만 제공한다.

### 적용한 리팩토링

- `framework_options.hpp` 내부에 blank 문자열 검증 helper를 추가했다.
- client/server, fanout, dealer mesh, route mesh, SPOT, STREAM, registry spot remote address
  fluent options에서 빈 이름과 빈 endpoint를 거부한다.
- handler group 이름도 handler group options state에서 공통으로 검증한다.
- module/options regression이 빈 channel 이름, server endpoint, handler group, SPOT mesh 이름,
  STREAM node 이름, registry route channel 이름을 검증한다.
- `cpp-framework-interfaces.ko.md`에 high-level fluent options 입력 guard 규칙을 적었다.

### 수정 후 점검

- 호출자는 빈 endpoint나 이름 때문에 low-level runtime 오류를 해석하지 않아도 된다.
- 검증 기준은 helper 한 곳에 모이고, public method는 domain-specific 메시지만 가진다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Attached SPOT client validation 보강

### 발견한 위험 신호

- `.NET` registration validation은 SPOT node가 attach한 channel client와 publisher client가
  실제 등록된 역할인지 startup 단계에서 검증한다.
- C++ high-level channel client attach는 이름만 low-level snapshot으로 넘겼고,
  `attach_publisher(...)`는 low-level builder에는 있지만 high-level fluent options 표면에는 없었다.
- attach 대상 channel이 없거나 역할이 맞지 않는 오류가 runtime 내부까지 흘러가면,
  호출자는 SPOT node 설정 오류와 channel 연결 오류를 구분해야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| low-level `spot_node_builder_t` snapshot만 유지한다 | public options state가 작다 | high-level sample과 `.NET` startup validation의 오류 위치가 어긋난다 |
| attached client를 일반 `channel_client_t` 주입으로만 대체한다 | surface가 줄어든다 | SPOT node별 attach 의도를 표현하지 못하고 `.NET` sample 구조와 달라진다 |
| high-level options가 node별 attach 의도와 channel 역할을 추적하고 `apply()`에서 검증한다 | attach 정책이 설정 layer에 모이고 오류가 이른 시점에 드러난다 | options state가 validation metadata를 더 가진다 |

선택은 세 번째 방식이다. attached client는 SPOT node configuration의 의미이므로, runtime bundle
생성 실패가 아니라 framework options validation으로 닫는 편이 호출자 부담을 줄인다.

### 적용한 리팩토링

- `spot_node_options_builder_t::attach_publisher(...)`를 high-level fluent options에 추가했다.
- client/server channel 등록 여부와 fanout channel의 publisher 역할을 options state에서
  추적한다.
- node별 attached channel client와 attached publisher를 options state에 기록한다.
- attached channel client는 등록된 client/server channel과 registry discovery를 요구한다.
- attached publisher는 등록된 fanout publisher 역할과 SPOT node pub/sub 역할을
  요구하고, publisher channel 중복 attach를 거부한다.
- module/options regression이 정상 publisher attach와 missing channel, missing 역할,
  missing discovery, duplicate publisher attach를 검증한다.
- `cpp-framework-interfaces.ko.md`에 attached client/publisher validation 규칙을 적었다.

### 수정 후 점검

- SPOT node attach 오류는 runtime 내부 bundle 생성 뒤가 아니라 options 적용 시점에 드러난다.
- low-level builder 표면과 high-level fluent options 표면의 역할 차이가 줄었다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Channel 역할 shape validation 보강

### 발견한 위험 신호

- `.NET` `ValidateChannelShape`는 client/server channel이 server 또는 client 역할을 하나도
  켜지 않았거나, fanout channel이 publisher 또는 subscriber 역할을 하나도 켜지 않으면
  startup 단계에서 실패시킨다.
- C++ high-level `add_client_server_channel(name)`과 `add_fanout_channel(name)`은 선언만 해도 action을
  등록할 수 있었다. 아무 역할도 없는 channel은 public API에서 의미가 없고, 이후 runtime snapshot
  해석으로 오류가 미뤄질 수 있다.
- 역할 없는 channel 선언을 허용하면 사용자가 channel kind와 역할을 별도로 추적해야 하므로
  호출자 부담이 늘어난다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| low-level `zlink_builder_t`가 빈 channel snapshot을 무시한다 | 실행 오류는 줄어든다 | 사용자 설정 실수를 조용히 숨긴다 |
| builder 생성자에서 즉시 실패시킨다 | 가장 이른 실패다 | fluent builder에서 나중에 `.enable_server(...)`, `.enable_client(...)`를 붙이는 정상 사용을 막는다 |
| options state가 channel 선언과 역할 보유 여부를 추적하고 `apply()`에서 검증한다 | fluent chaining을 보존하면서 `.NET` startup validation과 맞춘다 | options state가 validation metadata를 가진다 |

선택은 세 번째 방식이다. channel builder는 단계적으로 구성되므로 생성자에서 실패시키지 않고,
최종 options 적용 시점에 의미 없는 선언을 닫는다.

### 적용한 리팩토링

- client/server channel 선언과 server/client 역할 보유 여부를 options state에서 대조한다.
- fanout channel 선언과 publisher/subscriber 역할 보유 여부를 options state에서 대조한다.
- 역할이 하나도 없는 client/server 또는 fanout channel은 `apply()`가
  `request_protocol_error`로 실패한다.
- module/options regression이 역할 없는 client/server channel과 fanout channel의 apply 실패를
  검증한다.
- `cpp-framework-interfaces.ko.md`와 `cpp-channel-messaging.ko.md`에 channel 역할 shape
  validation 규칙을 적었다.

### 수정 후 점검

- application code는 channel kind 선언과 실제 역할이 맞는지 별도로 추적하지 않아도 된다.
- 의미 없는 channel snapshot이 runtime까지 전달되지 않는다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. High-level handler duplicate regression 보강

### 발견한 위험 신호

- `.NET` channel registration validation은 channel에 노출되는 handler packet이 중복되면
  startup 단계에서 configuration error로 실패시킨다.
- C++ `handler_registry_t`도 중복 packet 등록을 막지만, high-level fluent options의 handler
  group 경로에서 같은 보장이 회귀 테스트로 고정되어 있지 않았다.
- 테스트 공백이 있으면 사용자는 handler group 연결 순서에 따라 중복 노출이 허용되는지 별도로
  추론해야 한다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| low-level `handler_registry_t` 테스트만 유지한다 | 중복 테스트가 적다 | fluent options 사용자 관점의 parity 증거가 약하다 |
| handler group state에 별도 packet index를 추가한다 | options state에서 더 이른 메시지를 줄 수 있다 | 같은 정보를 registry와 options가 중복 보유한다 |
| group 내부 중복은 options state에서, channel 단위 packet 충돌은 registry에서 검증한다 | 설정 순서별 오류 위치가 명확하고 최종 channel 노출 기준도 한 곳에서 닫힌다 | options state와 registry가 서로 다른 수준의 검증을 나눠 가진다 |

선택은 세 번째 방식이다. 같은 group에 같은 packet을 두 번 넣는 오류는 DI 등록보다 먼저 options
state가 닫고, 서로 다른 group이 같은 channel에 같은 packet을 노출하는 오류는 최종 노출 표면을
소유한 `handler_registry_t`가 닫는다.

### 적용한 리팩토링

- module/options regression에 channel이 group을 먼저 매핑한 뒤 같은 send handler가 두 번
  등록되는 경우를 추가했다.
- module/options regression에 handler가 먼저 두 번 등록되고 나중에 channel이 group을 매핑하는
  경우를 추가했다.
- module/options regression에 서로 다른 handler group이 같은 channel에 같은 send packet을 노출하는
  경우를 추가했다.
- `handler_registry_t`가 `channel + kind + packet` 기준 duplicate를 거부하도록 보강했다.
- `handler-interfaces.ko.md`와 `cpp-framework-interfaces.ko.md`에 high-level handler duplicate
  validation 규칙을 적었다.

### 수정 후 점검

- group 내부 duplicate와 channel 노출 duplicate가 각각 가장 가까운 owner에서 닫힌다.
- fluent options 사용자는 group/handler 등록 순서와 무관하게 같은 configuration error를 받는다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Accepted route manual connection parity 보강

### 발견한 위험 신호

- 과거 `.NET` 표면은 accepted SPOT route peer를 discovery 없이 직접 지정할 수 있었다.
- C++ high-level `accept_routes_from_channel(name)`은 channel 이름만 받았고, validation도
  accepted route마다 registry discovery를 항상 요구했다.
- accepted route ingress와 registry Spot remote address resolver가 같은 snapshot field를 공유하면
  서로 다른 설계 결정을 한 값으로 표현하게 되어 정보 은닉이 약해진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| registry discovery만 허용한다 | 구현이 작다 | `.NET` manual accepted route 사용 흐름과 맞지 않는다 |
| accepted route manual endpoint를 client/server channel client endpoint로 합친다 | 기존 channel snapshot을 재사용한다 | route ingress 설정이 channel client 역할로 새어 나간다 |
| accepted route channel snapshot과 manual endpoint builder를 별도로 둔다 | accepted route ingress 의도가 분리되고 `.NET` peer source 정책과 맞는다 | snapshot type과 options state가 늘어난다 |

선택은 세 번째 방식이다. accepted route ingress는 SPOT node의 설정이며, registry remote address
resolver와 같은 필드에 섞지 않는다. peer source는 discovery 또는 route별 manual endpoint 중
하나로 검증한다.

### 적용한 리팩토링

- `accepted_spot_route_channel_t` snapshot을 추가해 accepted route channel과 manual endpoint 목록을
  별도 값으로 보존한다.
- low-level `spot_node_builder_t::accept_routes_from_channel(...)`을 추가했다.
- high-level `spot_node_options_builder_t::accept_routes_from_channel(name, configure)` overload와
  `accepted_spot_route_channel_builder_t::connect(...)`를 추가했다.
- accepted route validation은 registry discovery 또는 manual endpoint 중 하나가 있으면 통과하도록
  바꿨다.
- module/options regression이 discovery 없이 manual accepted route를 허용하고 snapshot에 endpoint가
  남는지 검증한다.
- registry topology regression이 discovery 기반 accepted route와 manual 기반 accepted route snapshot을
  검증한다.
- `cpp-framework-interfaces.ko.md`, `cpp-channel-messaging.ko.md`, `cpp-registry.ko.md`에 manual
  accepted route 설정을 적었다.

### 수정 후 점검

- accepted route ingress와 registry remote resolver가 public snapshot에서 분리됐다.
- 사용자는 registry가 없는 테스트/샘플 topology에서도 accepted route peer를 명시할 수 있다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Attached client manual connection parity 보강

### 발견한 위험 신호

- `.NET`은 channel client attach와 publisher attach에 configure callback을
  제공하고, attached channel client는 discovery 또는 manual connection으로 peer를 얻을 수 있다.
- C++ high-level attach 표면은 channel 이름만 받았고 attached channel client에 registry discovery를
  항상 요구했다.
- C++ validation은 attached channel client 대상이 client/server client 역할을 가져야 한다고
  보았지만, `.NET`은 server-only client/server channel에도 SPOT node가 자체 outbound dealer를
  attach할 수 있다.
- attach 호출마다 low-level action을 쌓는 방식은 같은 attach를 다시 configure할 때 duplicate
  snapshot을 만들 수 있어 attach 의도가 options state에 모이지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 이름-only attach만 유지한다 | 표면이 작다 | `.NET` manual attach flow와 맞지 않는다 |
| channel client의 `client(endpoint)` 설정을 attach manual endpoint로 재사용한다 | 새 builder가 없다 | channel 역할 설정과 SPOT attach 설정이 섞인다 |
| attach별 builder와 상세 snapshot을 추가하고 apply 시점에 한 번 materialize한다 | attach 의도와 peer source가 분리되고 duplicate action을 피한다 | snapshot type과 options state가 늘어난다 |

선택은 세 번째 방식이다. attached client/publisher는 SPOT node configuration의 일부이므로, channel
역할 설정과 섞지 않고 attach별 endpoint 목록을 별도 값으로 둔다.

### 적용한 리팩토링

- `attached_channel_client_t`, `attached_publisher_t` snapshot을 추가하고 기존 이름 목록은 유지했다.
- low-level spot node builder의 channel client attach와 `attach_publisher(...)`가 manual
  endpoint 목록을 받을 수 있게 했다.
- high-level channel client attach와 `attach_publisher(name, configure)` overload를
  추가했다.
- attach 호출은 action list가 아니라 options state에 기록하고, `apply()`에서 한 번 low-level snapshot으로
  materialize하도록 정리했다.
- attached channel client validation은 registry discovery 또는 attach별 manual endpoint 중 하나가
  있으면 통과하도록 바꿨고, 대상 channel은 client 역할이 아니라 client/server channel
  등록 여부만 요구한다.
- module/options regression이 discovery 없는 attached channel client manual endpoint와 attached
  publisher manual endpoint snapshot을 검증한다.
- low-level SPOT runtime regression이 attach 상세 snapshot과 빈 manual endpoint rejection을 검증한다.
- contract header test와 SPOT 관련 draft 문서를 갱신했다.

### 수정 후 점검

- attached client peer source 정책이 `.NET`과 같은 수준으로 맞춰졌다.
- attach configuration은 options state가 소유하고 low-level snapshot은 중복 없이 생성된다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Sample process e2e port probe 단순화

### 발견한 위험 신호

- sample process e2e harness가 포트를 고르기 전에 Python socket bind로 사용 가능 여부를 미리
  판단했다.
- 이 사전 검사는 실제 sample server의 bind 정책과 별도로 유지되어야 하므로, TIME_WAIT 같은 커널
  상태를 실제 서버보다 보수적으로 해석할 수 있다.
- 테스트 harness가 runtime bind 의미를 중복 구현하면 실패 원인이 sample 동작인지 harness 추정인지
  구분하기 어려워진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| Python bind 사전 검사를 유지한다 | 서버 실행 전 빠르게 스킵할 수 있다 | 실제 서버 bind 의미와 다른 판단이 생긴다 |
| Python socket에 `SO_REUSEADDR`를 추가한다 | TIME_WAIT 오탐을 일부 줄인다 | 여전히 서버 bind 정책을 harness가 복제한다 |
| listener 점유만 사전 스킵하고 실제 bind 가능성은 서버 실행 결과로 판단한다 | bind 의미의 owner가 sample server 하나로 모인다 | 실패 offset마다 서버를 실행해 보는 비용이 있다 |

선택은 세 번째 방식이다. 사전 검사는 이미 listen 중인 포트만 피하고, 나머지는 sample server가 실제로
bind하면서 성공 여부를 결정하게 둔다.

### 적용한 리팩토링

- sample-local runner에서 오래된 port preflight 설명을 제거했다.
- 현재 C++ runner는 full client/server 검증을 수행하지 않고 role executable smoke 범위를
  명확히 출력한다.

### 수정 후 점검

- 테스트 harness가 sample server의 bind 의미를 중복 구현하지 않는다.
- 포트 선택 실패는 listener 점유와 실제 server startup failure로 구분된다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Channel manual endpoint collection parity 보강

### 발견한 위험 신호

- `.NET` channel client와 fanout subscriber manual endpoint는 `EnableClient(endpoint)`와
  `EnableSubscriber(endpoint)` 같은 fluent 메서드 인자로 지정한다.
- C++ high-level `add_client_server_channel(...).enable_client(endpoint)`와
  `add_fanout_channel(...).enable_subscriber(endpoint)`는 마지막 endpoint만 low-level snapshot에 남겨
  manual connection collection 의미를 잃었다.
- low-level `capability_builder_t::connect(...)`는 이미 여러 endpoint를 보존하므로, high-level
  builder가 더 얕은 wrapper처럼 동작하고 있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 단일 endpoint 정책을 문서화한다 | 구현 변경이 작다 | `.NET` manual connection collection 기대와 맞지 않는다 |
| `clients({ ... })`, `subscribers({ ... })` 같은 새 API를 추가한다 | 복수 endpoint 의도가 드러난다 | 기존 fluent chain과 다르고 public 표면이 불필요하게 늘어난다 |
| 기존 `client(endpoint)`와 `subscriber(endpoint)` 반복 호출을 endpoint 추가로 정의한다 | C++ fluent style을 유지하고 low-level 역할 의미와 맞다 | discovery mode 전환 시 manual 목록을 명확히 비워야 한다 |

선택은 세 번째 방식이다. C++에서는 `Connect(...)` 컬렉션 builder 대신 같은 fluent method를 반복
호출하는 것이 자연스럽고, low-level 역할 snapshot도 이미 목록을 소유한다.

### 적용한 리팩토링

- `client_server_channel_builder_t`가 manual client endpoint를 vector로 보존하게 했다.
- `fanout_channel_builder_t`가 manual subscriber endpoint를 vector로 보존하게 했다.
- endpoint 인자 없는 `enable_client()`와 `enable_subscriber()`는 discovery mode로 전환하면서 manual endpoint
  목록을 비운다.
- module/options regression이 client/server client와 fanout subscriber의 복수 manual endpoint
  snapshot을 검증한다.
- channel messaging, sample, framework interface draft 문서에 반복 호출 의미를 적었다.

### 수정 후 점검

- high-level builder가 low-level 역할의 manual connection collection 의미를 잃지 않는다.
- C++ 사용자는 새 public 타입 없이 `.NET`의 manual connection collection과 같은 사용자 흐름을
  갖는다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. TicTacToe e2e readiness gate 보강

### 발견한 위험 신호

- TicTacToe process e2e server는 HTTP API app을 별도 thread로 시작한 뒤 stream socket을 bind하면
  readiness log를 기록했다.
- client는 readiness 직후 `zlink::http_client`로 `POST /games`를 보내므로, HTTP listener bind가
  아직 끝나지 않았거나 bind 실패가 늦게 드러나면 offset retry 없이 client 실패로 끝날 수 있다.
- readiness가 실제 의존 서비스 준비 상태가 아니라 실행 순서에 묶여 있어 시간적 분해 위험 신호가
  있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| client retry 횟수를 늘린다 | 구현 변경이 작다 | HTTP bind 실패와 늦은 readiness를 구분하지 못한다 |
| shell script에서 HTTP port를 별도로 probe한다 | sample 코드 변경이 작다 | sample의 준비 조건을 test harness가 중복해서 알아야 한다 |
| e2e server가 HTTP listener TCP readiness를 확인한 뒤 stream readiness를 기록한다 | 준비 조건 owner가 sample server 안에 모인다 | server main에 작은 readiness helper가 필요하다 |

선택은 세 번째 방식이다. sample server가 자신이 제공하는 HTTP와 stream endpoint의 준비 조건을
모두 확인한 뒤 readiness를 기록해야 test harness가 내부 port 목록을 더 알 필요가 없다.

### 적용한 리팩토링

- TicTacToe e2e server main에 HTTP endpoint host/port parser와 TCP connect readiness probe를
  추가했다.
- HTTP API thread가 먼저 종료되면 readiness 전에 실패하도록 했다.
- stream socket bind와 readiness log는 HTTP listener가 실제로 연결 가능해진 뒤 실행된다.

### 수정 후 점검

- client가 HTTP listener bind 완료 전에 시작하지 않는다.
- HTTP bind 실패는 readiness 전 server startup failure로 드러나므로 process e2e offset retry와
  결합된다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. SPOT router/pub-sub manual peer parity 보강

### 발견한 위험 신호

- `.NET`의 SpotNode fluent builder는 router/pub-sub endpoint와 peer endpoint를 메서드 인자로
  받아 registry discovery 없이 역할 peer를 직접 지정할 수 있다.
- C++ high-level `enable_router(...)`와 `enable_pub_sub(...)`는 bind endpoint와 routing id만
  보존했고, manual peer를 표현할 방법이 없었다.
- attach channel client, attached publisher, accepted route의 manual endpoint를 역할 peer로
  재사용하면 서로 다른 설계 결정을 한 필드에 섞어 정보 은닉이 약해진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| router/pub-sub manual peer를 지원하지 않는다 | public 표면이 작다 | `.NET`의 고정 endpoint SPOT topology 흐름과 맞지 않는다 |
| attach/accepted route manual endpoint를 역할 peer로 재사용한다 | 새 builder가 적다 | attach, route ingress, SPOT 역할 의도가 섞인다 |
| router/pub-sub 역할별 configure builder와 snapshot 필드를 추가한다 | peer source 책임이 분리되고 `.NET` configure 흐름과 맞다 | public builder 타입과 snapshot 필드가 늘어난다 |

선택은 세 번째 방식이다. SPOT router/pub-sub manual peer는 역할 자체의 연결 정책이며,
attached channel client나 accepted route ingress의 peer와 다른 정보다. C++에서는
`enable_router(endpoint).connect_router(peer)`처럼 같은 Spot node builder에서 이어지는
fluent 호출로 표현한다.

### 적용한 리팩토링

- `spot_node_snapshot_t`에 router/pub-sub manual connection 목록을 추가했다.
- low-level `spot_node_builder_t::connect_router(...)`,
  `spot_node_builder_t::connect_pub_sub(...)`를 추가하고 빈 endpoint를 거부하게 했다.
- high-level Spot node builder에 역할별 manual peer를 표현하는 fluent 호출을 추가했다.
- `spot_node_options_builder_t::set_routing_id (routing_id)`를 추가해 routing id는 node에 한 번만
  지정하고, 역할별 endpoint는 `enable_router(endpoint)`와 `enable_pub_sub(endpoint)`에서 설정하게 했다.
- contract header, SPOT runtime, registry topology regression과 draft 문서를 갱신했다.

### 수정 후 점검

- SPOT 역할 peer, attached client peer, accepted route peer가 public snapshot에서 분리된다.
- registry discovery 없이 고정 endpoint SPOT topology를 `.NET`과 같은 사용자 흐름으로 구성할 수
  있다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. discovery/metadata 공백 입력 검증 보강

### 발견한 위험 신호

- `.NET` framework는 `AddRegistryEndpoint(endpoint)`와 metadata forwarding key에서 빈 문자열과
  공백만 있는 문자열을 설정 오류로 거부한다.
- C++ high-level `options.use_discovery().add_registry_endpoint (...)`는 endpoint를 검증하지 않았고,
  low-level `message_metadata_policy_t::forward(...)`는 빈 문자열만 거부했다.
- 잘못된 값이 설정 경계를 지나 native 연결이나 actor dispatch 시점까지 내려가면 호출자가 원인을
  늦게 파악해야 하므로, 오류 처리를 하위 정의로 없애는 POSD 원칙과 맞지 않았다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `apply()` 단계에서만 검증한다 | validation 위치가 한 곳이다 | 어떤 builder 입력이 문제였는지 늦게 드러난다 |
| native 연결 실패에 맡긴다 | C++ 코드 변경이 가장 작다 | 공백 endpoint와 실제 연결 실패가 섞이고 사용자 경험이 `.NET`과 달라진다 |
| 입력을 받는 public builder와 policy 경계에서 즉시 거부한다 | 실패 위치가 명확하고 `.NET` 의미와 맞다 | 작은 검증 코드가 추가된다 |

선택은 세 번째 방식이다. discovery endpoint와 metadata key는 framework 설정의 public 계약이므로,
유효하지 않은 값을 받은 자리에서 바로 거부해야 호출자 부담이 줄어든다.

### 적용한 리팩토링

- `discovery_options_builder_t::add(...)`가 빈 endpoint와 공백 endpoint를 거부하게 했다.
- `message_metadata_policy_t::forward(...)`가 공백 key도 거부하게 했다.
- module hosted 회귀 테스트에 discovery/metadata 공백 입력 검증을 추가했다.
- SPOT runtime 회귀 테스트에 low-level metadata policy 공백 key 검증을 추가했다.
- draft interface 문서에 공백 입력 거부 규칙을 명시했다.

### 수정 후 점검

- 설정 경계에서 잘못된 discovery endpoint와 metadata key가 즉시 실패한다.
- `.NET`의 `string.IsNullOrWhiteSpace` 기준과 사용자 관점의 검증 의미가 맞는다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. routed SPOT egress configuration parity 보강

### 발견한 위험 신호

- 과거 `.NET` framework는 client/server channel과 route mesh channel builder에서
  routed SPOT egress 대상을 명시하게 했다.
- C++ fluent options에는 같은 사용자 의도를 표현하는 public 메서드가 없어서, routed SPOT ingress
  설정인 `accept_routes_from_channel(...)`만으로는 outbound relay 의도를 문서와 코드에서 맞출 수
  없었다.
- egress target을 handler group이나 accepted route 설정에 섞으면 방향이 다른 설계 결정이 한
  필드에 섞여 정보 은닉이 약해진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 문서에서 C++ non-goal로 남긴다 | 구현 변경이 없다 | `.NET` sample architecture parity가 깨진다 |
| accepted route 설정을 egress target으로 재사용한다 | 새 public 메서드가 적다 | ingress와 egress 책임이 섞인다 |
| egress 전용 builder 메서드와 route registration 필드를 추가한다 | 방향별 책임이 분리되고 `.NET` 사용자 흐름과 맞다 | relay 실행 경로와 registration 보존을 단계적으로 검증해야 한다 |

선택은 세 번째 방식이다. 이번 반복에서는 public configuration 계약과 registration 보존을 먼저
닫고, 실제 routed relay 실행 경로는 같은 target 정보를 사용하는 후속 단계로 분리한다. 이렇게 해야
얕은 wrapper를 만들지 않고 egress 설정의 owner를 분명히 둘 수 있다.

### 적용한 리팩토링

- low-level `route_channel_builder_t::enable_spot_route_egress(...)`를 추가했다.
- `route_channel_registration_t`와 `route_channel_runtime_t`가 target SPOT node channel 이름을
  보존하고 조회할 수 있게 했다.
- high-level `client_server_channel_builder_t::enable_spot_route_egress(...)`와
  `route_mesh_channel_builder_t::enable_spot_route_egress(...)`를 추가했다.
- client/server channel egress는 client 역할이 없으면 options 적용 시점에 실패하게 했다.
- contract header, channel messaging, registry topology, module hosted regression과 draft 문서를
  갱신했다.

### 수정 후 점검

- egress target은 handler group, accepted route ingress, manual peer 목록과 분리된다.
- route mesh egress target은 `options.apply()` 이후 route runtime까지 보존된다.
- client/server egress는 local client 역할이 없으면 즉시 설정 오류로 드러난다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. STREAM typed session registration parity 보강

### 발견한 위험 신호

- `.NET` framework의 stream node builder는 `RegisterSession<TSession>()`으로 session 타입을
  등록하고, runtime이 DI scope에서 session을 만든다.
- C++ high-level stream node builder는 `register_session(name)`만 제공해 사용자가 native packet
  session 이름을 직접 골라야 했다.
- 문자열 session 이름만 public 표면으로 두면 session handler 타입과 native session 이름의 연결
  지식이 호출자에게 새고, `.NET` 샘플의 타입 중심 사용 흐름과 달라진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `register_session(name)`만 유지한다 | 변경이 없다 | session handler 타입과 이름 연결을 호출자에게 맡긴다 |
| `register_session<T>()`를 단순 alias로 추가한다 | public 표면이 맞아 보인다 | DI 등록 기대를 충족하지 못하는 얕은 wrapper다 |
| `register_session<T>()`가 scoped service 등록과 session 이름 결정을 함께 맡는다 | 타입 중심 사용자 흐름과 `.NET` 의미가 맞고 이름 지식이 builder 안에 숨는다 | builder가 service collection 참조를 가져야 한다 |

선택은 세 번째 방식이다. C++에서는 `packet_stream_session_t` 파생 타입을 session handler 계약으로
삼고, `TSession::session_name`이 있으면 그 값을, 없으면 타입 기반 message name을 native packet
session 이름으로 사용한다.

### 적용한 리팩토링

- `stream_node_options_builder_t::register_session<TSession>()`를 추가했다.
- `TSession`은 `packet_stream_session_t`를 상속해야 하며, framework service collection에 scoped
  service로 등록된다.
- 기존 `register_session(name)`과 같은 단일 session 규칙을 공유하게 했다.
- contract header, module hosted regression, STREAM draft와 sample draft 문서를 갱신했다.

### 수정 후 점검

- typed session 등록이 `options.apply()` 이후 stream snapshot에 반영된다.
- typed session 타입은 provider에서 resolve 가능하다.
- `register_session<T>()`와 `register_session(...)`을 중복 호출하면 설정 오류가 난다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. STREAM write fluent call parity 보강

### 발견한 위험 신호

- `.NET` framework의 session send/reply call은 `Metadata(...)`, `PacketName(...)`,
  `Compress()`를 submit 전에 설정할 수 있다.
- C++ `stream_t::write_packet(...)`은 header를 미리 직접 만들어야 했고, 기존 구현은 call object를
  반환하기 전에 written header를 기록했다.
- 이 구조에서는 metadata나 compression 같은 header 정책을 호출자가 직접 조립해야 하며,
  `submit()` 전에 call을 구성한다는 framework 공통 call 의미도 약해진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| header 직접 생성 표면만 유지한다 | 변경이 없다 | `.NET` session call 사용자 경험과 다르고 header 조립 부담이 호출자에게 남는다 |
| `stream_t`에 `send(...)`, `reply(...)` 별도 고수준 API를 추가한다 | `.NET` 이름과 가깝다 | typed serializer/codec 정책까지 한 번에 끌어와 변경 범위가 커진다 |
| 기존 `stream_write_call_t`를 lazy fluent call로 바꾼다 | public call object 하나가 metadata, packet name, compression, submit을 소유한다 | write 실행 시점이 submit으로 이동하므로 회귀 테스트가 필요하다 |

선택은 세 번째 방식이다. C++는 이미 `stream_header_t`와 payload를 받는 명시적 표면을 가지고
있으므로, 그 위에 call object가 submit 전 header mutation을 흡수하게 하면 새 얕은 wrapper 없이
사용자 부담을 줄일 수 있다.

### 적용한 리팩토링

- `stream_write_call_t`에 `metadata(...)`, `packet_name(...)`, `compress()`를 추가했다.
- `stream_write_call_t`를 pimpl 기반 lazy call로 바꿔 실제 write가 `submit()`에서 실행되게 했다.
- `stream_t::write_packet(...)`은 call object를 만들고, submit 시점에 disconnected 상태와 최종
  header를 확인하게 했다.
- contract header, stream unit regression, STREAM draft와 interface draft 문서를 갱신했다.

### 수정 후 점검

- submit 전에는 stream written header가 증가하지 않는다.
- metadata, packet name override, compression flag가 submit 시점에 header에 반영된다.
- disconnected 상태는 submit 결과로 유지된다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. STREAM close와 bound session disconnect parity 보강

### 발견한 위험 신호

- `.NET` framework의 `IZLinkStream`은 `CloseAsync()`를 제공하고, `IZLinkBoundSession`은
  `DisconnectAsync()`를 제공한다.
- C++ draft의 ActorGateway relay 문서는 `bound_session_t::disconnect()`와 stream close cleanup을
  언급했지만, 실제 public header에는 `stream_t::close()`와 `bound_session_t::disconnect()`가
  없었다.
- 호출자가 session close를 흉내 내려면 runtime test helper나 actor manager cleanup 경로를 알아야
  하므로, lifecycle 결정이 public 표면 아래에 숨겨지지 않는 위험 신호가 있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 `notify_disconnected()`와 runtime dispatch만 사용한다 | 코드 변경이 없다 | actor push 쪽 `.NET` disconnect 사용자 흐름과 맞지 않고 문서 불일치가 남는다 |
| `stream_runtime_t`와 `session_actor_manager_t` helper를 public으로 노출한다 | 테스트 구현이 쉽다 | runtime cleanup 세부가 public API로 새어 나간다 |
| `stream_t::close()`와 `bound_session_t::disconnect()`를 public lifecycle 표면으로 둔다 | 호출자는 stream/bound session만 알고 close 의미는 runtime state가 숨긴다 | close와 disconnect 결과를 회귀 테스트로 고정해야 한다 |

선택은 세 번째 방식이다. C++ connector에도 `close()` task 표면이 있으므로 stream close는
`task_t<void>`로 두고, bound session disconnect는 기존 call object 규칙에 맞춰
`send_call_t`를 반환하게 했다.

### 적용한 리팩토링

- `stream_t::close()`를 추가해 session을 closed 상태로 만들고 이후 write submit이
  `disconnected`를 반환하게 했다.
- `bound_session_t::disconnect()`를 추가해 bound flag를 내리고 disconnected 상태를 기록하게
  했다.
- contract header, stream runtime regression, actor gateway regression, STREAM draft와
  ActorGateway relay draft를 갱신했다.

### 수정 후 점검

- `stream_t::close()` 뒤 새 write submit은 추가 frame을 쓰지 않고 `disconnected`를 반환한다.
- `bound_session_t::disconnect()` 뒤 actor는 bound 상태가 아니며, push와 relay는
  `disconnected`를 반환한다.
- 기존 `session_actor_t::notify_disconnected()` 경로도 회귀 테스트에 남아 있다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. 일반 channel call lazy submit parity 보강

### 발견한 위험 신호

- `.NET` framework의 `IZLinkSendCall`과 `IZLinkRequestCall`은 `PacketName(...)`,
  `Timeout(...)`, `Submit(...)` 순서로 호출을 구성한다.
- C++ 일반 `message_bus_t::send(...)`, `publish(...)`, `request(...)`는 call object를 반환했지만
  생성 시점에 이미 runtime submit 결과를 계산했다.
- 이 구조에서는 `packet_name`과 metadata를 submit 전에 바꿀 수 없고, plan의 “public 호출은 call
  object를 만들고 마지막 submit에서 실행한다” 규칙과 다르다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 즉시 완료 call에 `packet_name(...)` no-op만 추가한다 | 변경이 작다 | public 표면만 맞고 submit 의미는 여전히 어긋나는 얕은 wrapper다 |
| route 전용 call만 일반 channel에도 재사용한다 | packet name과 metadata 전달 구조가 이미 있다 | route target node, router channel 개념이 일반 channel API로 새어 나온다 |
| `request_call_t`와 `send_call_t`를 lazy fluent call로 바꾼다 | 일반 channel도 `.NET`과 같은 submit 경계를 가지며 route 세부를 노출하지 않는다 | 즉시 실패/성공 call 생성자와 lazy submit 경로를 함께 유지해야 한다 |

선택은 세 번째 방식이다. 즉시 result 기반 call은 spot, actor, 테스트 helper에서 계속 쓸 수 있게
두고, message bus가 만든 call만 packet name, metadata, timeout을 submit 시점에 runtime으로
넘기게 했다.

### 적용한 리팩토링

- `channel_request_call_t`와 `send_call_t`에 `packet_name(...)`과 `metadata(...)`를 추가했다.
- 일반 channel `message_bus_t::request/send/publish`가 call 생성 시점에는 side effect를 만들지
  않고, `submit()`에서 runtime submit을 실행하게 했다.
- internal channel runtime에 outbound submit 기록을 두어 회귀 테스트가 packet name, metadata,
  timeout 전달을 검증할 수 있게 했다.
- contract header, channel messaging regression, channel messaging draft와 interface draft를
  갱신했다.

### 수정 후 점검

- call object 생성만으로 outbound submit 기록이 생기지 않는다.
- submit 이후 request/send/publish 기록에 packet name, metadata, timeout이 보존된다.
- route call의 target node/routing 세부는 일반 channel public API로 노출되지 않는다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Handler filter invocation context parity 보강

### 발견한 위험 신호

- `.NET` framework의 `ZLinkHandlerInvocation`은 filter에 message, handler context, channel name,
  packet name을 제공한다.
- C++ `handler_invocation_context_t`는 descriptor만 제공해 filter가 실제 payload나 dispatch
  context를 보고 감사, 차단, 대체 응답을 결정할 수 없었다.
- filter가 descriptor map만 알면 application-level 정책을 handler 내부로 밀어 넣게 되어
  cross-cutting concern이 흩어진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| descriptor만 유지한다 | 변경이 없다 | `.NET` filter 사용성과 다르고 payload/context 기반 정책을 handler에 반복해야 한다 |
| typed object를 `std::any`로 제공한다 | `.NET`의 object message와 비슷하다 | serializer type erasure와 lifetime 규칙이 public API로 새어 나온다 |
| immutable `message_t`와 `handler_context_t`를 invocation context에 추가한다 | C++ message boundary를 유지하면서 filter가 payload와 dispatch metadata를 읽을 수 있다 | filter가 typed DTO를 직접 받지는 않는다 |

선택은 세 번째 방식이다. C++ framework는 codec boundary를 `message_t`로 닫고 있으므로 filter도
같은 immutable payload를 읽게 하면 typed serializer 세부를 public filter 계약에 노출하지 않는다.

### 적용한 리팩토링

- `handler_invocation_context_t`에 `handler_context_t context`와
  `std::shared_ptr<const zlink::message_t> message`를 추가했다.
- handler dispatch가 filter chain을 만들 때 owned message와 dispatch context를 함께 전달하게
  했다.
- contract header, handler registry regression, handler/interface draft 문서를 갱신했다.

### 수정 후 점검

- 기존 descriptor 기반 filter는 그대로 동작한다.
- filter는 channel name, packet name, immutable raw message payload를 읽을 수 있다.
- typed DTO lifetime이나 serializer 내부 캐시는 public filter 계약으로 노출되지 않는다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Monitoring socket event filter parity 보강

### 발견한 위험 신호

- `.NET` framework의 monitoring options는 socket source와 event kind 목록을 함께 등록하고,
  등록되지 않은 source나 허용되지 않은 event kind는 handler로 올리지 않는다.
- C++ monitoring builder는 source 이름만 저장했고, runtime은 publish 시점에 source나 event kind
  허용 여부를 확인하지 않았다.
- 이 상태에서는 handler마다 source/event filtering을 반복해야 하므로 monitoring policy가 handler
  구현으로 새어 나가는 정보 은닉 위반 위험이 있었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 기존 source-only 등록을 유지하고 handler에서 직접 걸러 낸다 | runtime 변경이 없다 | source policy가 handler마다 반복되고 `.NET` 사용 흐름과 다르다 |
| global socket event allow-list를 둔다 | 구현이 단순하다 | source별 정책을 표현할 수 없어 여러 channel을 가진 app에서 호출자 부담이 커진다 |
| source별 socket event kind 목록을 monitoring state에 저장하고 publish 경계에서 걸러 낸다 | `.NET`과 같은 source별 filtering 의미를 제공하고 handler 복잡성을 줄인다 | 등록 validation과 회귀 테스트가 필요하다 |

선택은 세 번째 방식이다. source별 event filter는 monitoring runtime 내부 정책이므로 public handler
표면에 누출하지 않고, 빈 event 목록은 해당 source의 모든 socket event를 의미하게 했다.

### 적용한 리팩토링

- `monitoring_builder_t::add_socket_events(source, events)` overload를 추가했다.
- monitoring runtime state가 socket source 이름과 허용 event kind 목록을 함께 보관하게 했다.
- `publish_socket(...)`이 등록되지 않은 source와 허용되지 않은 event kind를 publish 전에 차단하게
  했다.
- 중복 source와 빈 source 이름을 설정 오류로 고정하고 회귀 테스트를 보강했다.
- contract header와 monitoring draft 예시를 실제 public API에 맞췄다.

### 수정 후 점검

- event 목록이 없는 source는 기존처럼 모든 socket event를 받을 수 있다.
- event 목록이 있는 source는 허용된 kind만 typed handler와 trace hook으로 전달된다.
- 등록되지 않은 socket source는 publish 경계에서 차단된다.
- source validation은 builder가 담당하므로 handler가 설정 오류를 나중에 해석하지 않는다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Monitoring source registration policy parity 보강

### 발견한 위험 신호

- `.NET` framework는 registry/spot monitoring source에서 빈 이름, 중복 source, 0 이하 interval을
  설정 오류로 막는다.
- C++ monitoring builder는 socket 외 source를 그대로 저장했고, runtime publish 경계도 등록된
  source인지 확인하지 않았다.
- 이 구조에서는 handler가 source filtering과 잘못된 polling 설정을 각자 해석해야 하므로 monitoring
  등록 정책이 public handler 구현으로 새어 나간다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| registry/spot interval만 검사한다 | `.NET` polling validation 일부를 맞출 수 있다 | 등록되지 않은 source event가 계속 handler로 전달된다 |
| 각 handler 예시에 source guard를 넣는다 | runtime 변경이 작다 | source policy가 모든 handler에 반복되어 정보 은닉을 해친다 |
| builder에서 source validation을 통합하고 runtime publish 경계에서 등록 source만 통과시킨다 | source policy를 한 곳에 숨기고 `.NET` polling validation과 C++ 확장 event source를 함께 정리한다 | monitoring runtime helper가 조금 늘어난다 |

선택은 세 번째 방식이다. monitoring source 등록 정책은 handler의 관심사가 아니라 runtime event
pipeline의 책임이므로, builder와 runtime publish 경계에 모으는 편이 호출자 복잡성을 줄인다.

### 적용한 리팩토링

- source name blank check와 duplicate check를 monitoring builder 내부 helper로 통합했다.
- registry/spot polling interval이 0 이하이면 설정 오류를 반환하게 했다.
- discovery, registry, spot, spot timer, stream, actor runtime publish 경계가 등록 source만
  통과시키게 했다.
- 등록되지 않은 source가 handler와 trace hook으로 새지 않는지 monitoring regression test를
  보강했다.
- monitoring draft에 source registration policy와 interval validation 설명을 추가했다.

### 수정 후 점검

- builder가 잘못된 source 이름, 중복 source, 잘못된 polling interval을 조기에 거부한다.
- handler는 source 등록 여부를 반복해서 검사하지 않아도 된다.
- direct publisher는 사용자가 명시적으로 event를 올리는 표면이므로 기존처럼 typed event를 그대로
  전달한다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. HTTP client fluent validation parity 보강

### 발견한 위험 신호

- HTTP client draft는 timeout, base URL, HTTPS trust 설정, request path를 public fluent builder
  표면에서 다룬다고 명시한다.
- 기존 C++ HTTP client는 잘못된 `base_url`이나 request path를 `std::invalid_argument` 또는
  generic `request_failed`로 흘릴 수 있었다.
- 이 상태에서는 호출자가 설정 오류와 transport 실패를 같은 방식으로 해석해야 하므로 error model
  정책이 client 사용자 코드로 새어 나간다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| `std::invalid_argument`를 그대로 둔다 | 구현 변경이 작다 | framework 공통 error model과 다르고 샘플 handler에서 예외 종류를 별도로 알아야 한다 |
| 모든 오류를 `request_failed` result로 바꾼다 | result 표면만 보면 된다 | 잘못된 fluent 입력과 실제 transport 실패가 구분되지 않는다 |
| fluent 입력은 즉시 `request_protocol_error`로 검증하고, transport 실패는 기존 request result로 둔다 | 설정 오류와 실행 오류가 분리되고 call object가 잘못된 상태로 만들어지지 않는다 | validation helper와 회귀 테스트가 필요하다 |

선택은 세 번째 방식이다. URL, timeout, header name, trust file, request path는 호출자가 즉시
고칠 수 있는 설정 입력이므로 request submit 경로로 미루지 않고 builder/call 생성 경계에서 닫는다.

### 적용한 리팩토링

- HTTP client builder가 빈 base URL, 빈 header name, 빈 trust certificate path, 0 이하 timeout을
  `framework_exception_t(request_protocol_error)`로 거부하게 했다.
- runtime URL parsing에서 나온 `std::invalid_argument`도 build 경계에서
  `request_protocol_error`로 변환하게 했다.
- request path와 request header name validation을 request builder 경계에 추가했다.
- HTTP client regression test와 draft 문서를 갱신했다.

### 수정 후 점검

- 잘못된 fluent 입력은 transport 실행 전에 protocol/configuration 오류로 닫힌다.
- HTTP status, timeout, TLS 검증 실패 같은 실행 결과 mapping은 기존 public result 표면을 유지한다.
- public header에 Beast, Asio, OpenSSL 타입을 노출하지 않는다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Registry monitoring draft drift 보강

### 발견한 위험 신호

- Registry draft는 monitoring 통합 항목을 아직 별도 regression 전 상태처럼 설명했다.
- 실제 C++ monitoring regression은 등록된 source만 Registry snapshot event를 받고,
  topology와 service summary 변화가 typed event로 올라오는 경로를 이미 검증한다.
- 문서가 구현된 동작을 pending처럼 남기면 다음 구현자가 이미 닫힌 public 기대값을 다시 해석해야
  하므로 문서와 코드 사이의 정보 불일치가 생긴다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 문서만 현재 상태에 맞춘다 | 변경이 작다 | 같은 stale 문구가 다시 들어와도 자동으로 잡지 못한다 |
| Registry monitoring을 별도 public API로 확장한다 | 문구를 큰 기능으로 닫을 수 있다 | 현재 요구보다 넓고 runtime polling worker 설계를 섞게 된다 |
| 구현된 snapshot event 계약은 현재 구현 항목으로 옮기고, 아직 없는 polling/log worker 정책만 후속으로 남긴다 | 문서가 현재 코드와 같은 말을 하고 scope가 명확하다 | layout contract에 문구 검사가 필요하다 |

선택은 세 번째 방식이다. 이미 구현된 monitoring event projection은 현재 계약으로 설명하고,
아직 없는 runtime polling worker와 log correlation 정책은 후속 확장으로 분리했다.

### 적용한 리팩토링

- `cpp-registry.ko.md`에서 Registry snapshot event의 현재 구현 상태를 완료 항목으로 옮겼다.
- remote query timeout과 polling worker/log correlation은 후속 확장 항목으로 좁혀 적었다.
- layout contract가 registry draft에서 구현된 monitoring 계약 문구와 stale pending 문구를 함께
  검사하게 했다.

### 수정 후 점검

- registry draft는 현재 monitoring regression과 같은 범위를 설명한다.
- 아직 구현되지 않은 polling worker/log correlation은 정식 공개 계약처럼 쓰지 않는다.
- 같은 drift가 다시 들어오면 layout contract에서 실패한다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Request failure mapper regression parity 보강

### 발견한 위험 신호

- `.NET` unit test는 request completion의 `NotConnected`, `NotFound`, `TimedOut`과 error
  envelope code가 어떤 framework error로 바뀌는지 직접 고정한다.
- C++에는 `request_failure_mapper_t`가 있었지만 회귀 테스트는 `busy`와
  `route_not_connected` error header만 확인했다.
- 매핑 표가 테스트와 문서에 없으면 channel, route, HTTP bridge가 각자 오류를 해석하게 되어
  request failure policy가 여러 곳으로 흩어진다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 현재 mapper 구현을 신뢰하고 테스트를 늘리지 않는다 | 변경이 없다 | `.NET`과 같은 오류 의미가 regression으로 고정되지 않는다 |
| 각 channel/route 테스트에서 개별 실패를 추가한다 | end-to-end 의미에 가깝다 | 같은 mapping 지식을 여러 테스트에 반복한다 |
| mapper unit test와 interface draft에 매핑 표를 추가한다 | 오류 정책 owner가 명확하고 작은 테스트로 drift를 잡는다 | mapper와 상위 e2e를 함께 유지해야 한다 |

선택은 세 번째 방식이다. 오류 사상은 `request_failure_mapper_t`가 소유하고, 상위 runtime은 이
정책을 재해석하지 않게 한다.

### 적용한 리팩토링

- `test_cpp_framework_messaging`에 `not_connected`, `not_found`, `timed_out`,
  `request_rejected`, `request_protocol_error`, `handler_not_found` mapping 검증을 추가했다.
- `cpp-framework-interfaces.ko.md`에 native result/error code와 C++ error kind, retriable 여부
  표를 추가했다.

### 수정 후 점검

- `.NET` request failure mapping의 핵심 기대값이 C++ mapper unit test에 대응된다.
- channel, route, connector sample은 error code mapping을 직접 반복하지 않아도 된다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Channel request decode failure reply parity 보강

### 발견한 위험 신호

- `.NET` channel dispatcher test는 request payload decode failure를 caller에게 error envelope로
  돌려주는 정책을 검증한다.
- C++ channel dispatcher는 envelope header를 이미 읽은 뒤 body part가 없으면 dispatcher
  failure로 반환했다. 이 경우 reply 가능한 request 오류가 transport/pump 상위 계층으로 새어
  caller failure result 계약이 흐려진다.
- 문서도 payload decode failure가 caller failure result에 반영된다고 적고 있었으나, body
  decode 실패는 회귀 테스트가 없었다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| 상위 receive loop에서 dispatcher failure를 error reply로 바꾼다 | transport에 가까운 곳에서 처리한다 | header/correlation 정보를 다시 해석해야 하고 reply 정책이 분산된다 |
| dispatcher가 request body decode failure만 error envelope로 바꾼다 | header를 가진 계층이 correlation과 error code를 한 곳에서 처리한다 | reply path가 없는 command는 기존 failure 의미를 유지해야 한다 |
| body decode failure를 handler deserialize failure처럼 handler registry로 넘긴다 | 기존 handler 오류 경로를 재사용한다 | body가 없으면 typed handler 호출 전제 자체가 깨진다 |

선택은 두 번째 방식이다. reply 가능한 request protocol error는 dispatcher가 error envelope로
흡수하고, header decode failure나 command body failure처럼 reply path가 불명확한 경우는 기존
failure 결과로 둔다.

### 적용한 리팩토링

- `channel_packet_dispatcher_t`가 request header를 읽은 뒤 body decode에 실패하면
  `request_protocol_error` error envelope를 생성하게 했다.
- `test_cpp_framework_channel_messaging`에 body 없는 request envelope가 dispatcher failure가
  아니라 correlation id를 유지한 error envelope로 돌아오는 regression을 추가했다.
- `cpp-channel-messaging.ko.md`에 request decode failure는 error envelope로 caller에게
  반환한다는 구체 규칙을 적었다.

### 수정 후 점검

- reply 가능한 request 오류는 상위 receive loop에 누수되지 않는다.
- send/command처럼 reply path가 없는 메시지의 기존 failure 의미는 바꾸지 않았다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

## 반복 POSD 재리뷰. Spot actor change kind membership guard 보강

### 발견한 위험 신호

- `.NET` contract test는 actor change kind가 `JoinSpot`, `JoinEntrySpot`, `LeaveSpot`
  membership 변화만 담고, 잘못된 enum 값은 생성 시 거부한다고 고정한다.
- C++ enum은 동일한 세 값만 선언했지만 `static_cast`로 정의되지 않은 값을 넣어
  `spot_actor_change_result_t`를 만들 수 있었다.
- lifecycle handler가 의미 없는 change kind를 받으면 handler 쪽에서 방어해야 하므로 오류 처리가
  호출자에게 새어 나간다.

### 비교한 대안

| 대안 | 장점 | 단점 |
|------|------|------|
| enum 선언만 믿고 추가 검사를 하지 않는다 | 변경이 없다 | C++ enum cast 입력을 막지 못한다 |
| handler invocation 때마다 change kind를 검사한다 | runtime 경계에서 막을 수 있다 | 같은 membership 지식이 여러 dispatch 경로에 반복된다 |
| `spot_actor_change_result_t` 생성자에서 허용 값을 검증한다 | lifecycle 의미를 값 객체가 소유하고 호출자는 잘못된 상태를 만들 수 없다 | 생성자가 예외를 던질 수 있다 |

선택은 세 번째 방식이다. membership change의 유효성은 `spot_actor_change_result_t`가 소유하고,
handler dispatch는 이미 검증된 값을 받는다.

### 적용한 리팩토링

- `spot_actor_change_result_t`가 `join_spot`, `join_entry_spot`, `leave_spot` 외 값을
  `request_protocol_error`로 거부하게 했다.
- `test_cpp_framework_spot_runtime`에 invalid enum cast 값이 거부되는 regression을 추가했다.
- `cpp-framework-interfaces.ko.md`에 세 membership change 외 값은 생성 시 거부된다고 명시했다.

### 수정 후 점검

- actor disconnected는 `.NET`처럼 change result 없이 별도 handler로 남는다.
- lifecycle handler는 정의된 membership change만 받는다.
- 잔여 POSD 위험 신호와 리팩토링 이슈는 0개다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Draft -- ZLink Framework C++ Implementation Plan](cpp-framework-implementation-plan.ko.md)
<!-- framework-adapter-nav:bottom:end -->
