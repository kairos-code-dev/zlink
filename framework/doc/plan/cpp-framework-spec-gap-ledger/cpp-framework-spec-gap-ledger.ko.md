# C++ Framework spec gap audit ledger

## 문서 상태

- 기준 시점: 2026-08-02. 재검증 실행 구간은 08:46~08:48 KST다.
- 판정: 미완료. 현재 C++ Framework는 common Framework spec과 common E2E spec을 전체 충족한다고 판정할 수 없다.
- 변경 범위: 이번 갱신에서는 이 spec ledger와 후속 [`cpp-framework-sample-spec-gap-ledger.ko.md`](cpp-framework-sample-spec-gap-ledger.ko.md), 그리고 실제 작업 진행 log만 수정한다. 구현 source, public header, test, E2E runner, 정식 spec과 기존 사용자 변경은 수정하지 않는다.
- 이 문서는 public contract가 아니다. 아래의 `contract 선행` 항목은 구현 전에 exact interface 또는 package ownership을 먼저 확정해야 한다.

## 1. 목적과 완료 조건

이 ledger의 목적은 C++ Framework의 이름과 type이 존재하는지 확인하는 데 있지 않다. common Framework spec, C++ exact interface, public header와 export, 실제 production call path, process E2E의 role server 경계와 evidence를 같은 기준으로 비교해 현재 gap과 수정 순서를 고정한다.

완료는 다음 조건을 모두 만족할 때로 판정한다.

- C++ exact interface의 namespace, type, method, parameter, return type, ownership, lifetime, destructor, callback signature/lifetime, thread-safety, `noexcept`, exception과 error kind가 설치된 public header와 CMake package export에서 일치한다.
- `request`, `send`, `publish`, worker, actor, spot, stream, routing, admission, relocation과 shutdown의 실제 runtime path가 common spec의 state transition, deadline, cancellation, retry, cleanup과 일치한다.
- 14개 common E2E config의 374개 scenario ID가 C++ feature-map, selector, client dispatch, aggregate runner에서 일대일로 추적된다. `부분`, `미구현`, `diagnostic_only`, `N/A`, source-only와 historical log는 aggregate 성공으로 계산하지 않는다.
- client는 public HTTP client 또는 stream connector로 실제 role server endpoint를 호출하고, Framework 호출은 해당 role server에 있다. client-visible result와 role server evidence를 함께 확인한다.
- C++ Framework package가 지정한 `zlink_cpp`와 Core package에서 clean consumer를 compile하고 실행한다. install tree, target export, symbol visibility, C++ standard와 Debug/Release ABI를 확인한다.
- Core 전용 C ABI 또는 Framework 내부 ABI를 application/E2E 경로에 새로 노출하지 않는다.
- 현재 build, test, process E2E, package와 CI가 모두 재현 가능한 완료 evidence를 남긴다.
- 이 spec ledger의 선행 gate를 닫은 뒤 [`cpp-framework-sample-spec-gap-ledger.ko.md`](cpp-framework-sample-spec-gap-ledger.ko.md)의 sample contract, runner와 6개 process evidence도 완료한다. 두 ledger 중 하나라도 미완료이면 전체 C++ audit을 완료로 표시하지 않는다.

현재 source는 HTTP buffer access에 `mutable_buffer::data()`와 `const_buffer::data()`를 사용하지만, public contract mismatch, bounded CTest failure, aggregate 범위 누락, package verifier failure와 Framework/sample process evidence 부재가 남아 있으므로 완료 조건을 충족하지 않는다.

## 2. 조사 범위와 authoritative source

### 2.1 우선순위

계약의 우선순위는 다음과 같다.

1. `core/include/zlink.h`와 Core public package는 Core가 소유하는 binding 계약의 기준이다.
2. `framework/doc/framework/common/spec/`의 common Framework spec과 `framework/doc/framework/common/spec/server/languages/cpp/`, `http-client/languages/cpp/`, `stream-connector/languages/cpp/`의 C++ exact interface는 C++ Framework public contract의 기준이다.
3. `framework/doc/framework/common/e2e/`는 공통 동작을 검증하는 입력이다. E2E 문서나 다른 언어 구현만으로 새 C++ public API를 추가하지 않는다.
4. `framework/languages/cpp/framework/include/`의 설치 대상 public header, `framework/languages/cpp/CMakeLists.txt`의 target/export, package config와 clean consumer가 실제 공개 surface를 결정한다.
5. `framework/languages/cpp/framework/src/`는 production implementation을 확인하는 자료다. source에만 있는 기능은 public contract 충족으로 세지 않는다.

### 2.2 확인한 자료

- 저장소 규칙: `AGENTS.md`, `doc/principal/documentation/documentation-principles.ko.md`, `doc/principal/source-comment-principles.ko.md`.
- common Framework spec: `framework/doc/framework/common/spec/` 전체. 특히 `00-public-contract-governance.ko.md`, server 공통 문서와 runtime, channel, spot, actor, stream, monitoring interface를 확인했다.
- C++ exact interface: `framework/doc/framework/common/spec/server/languages/cpp/`의 `interfaces/01-common-runtime.ko.md`, `02-configuration-host.ko.md`, `03-channel-messaging.ko.md`, `04-spots.ko.md`, `05-actors.ko.md`, `06-stream-session.ko.md`, `07-location-store.ko.md`, `08-monitoring.ko.md`; HTTP client의 `framework/doc/framework/common/spec/http-client/languages/cpp/cpp-http-client.ko.md`; stream connector의 `framework/doc/framework/common/spec/stream-connector/languages/cpp/03-stream-connector.ko.md`.
- common E2E: `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md`부터 `config-14-instance-spot.ko.md`까지와 `framework/doc/framework/common/e2e/README.ko.md`.
- C++ production: `framework/languages/cpp/framework/include/`, `framework/languages/cpp/framework/src/`, `framework/languages/cpp/http-client/`, `framework/languages/cpp/connector/`.
- C++ E2E: `framework/languages/cpp/e2e/`의 feature-map, client dispatch, role server, `run_e2e.sh`, `run_e2e_all.sh`.
- build/package: `framework/languages/cpp/CMakeLists.txt`, `framework/languages/cpp/cmake/`, `framework/languages/cpp/scripts/verify_packaged_contract.sh`, `scripts/local-package/README.ko.md`, `scripts/v11/run-framework-runtime-regression.mjs`와 `.github/workflows/`.
- C++ sample 후속 범위: `framework/doc/plan/cpp-framework-spec-gap-ledger/cpp-framework-sample-spec-gap-ledger.ko.md`, `framework/languages/cpp/samples/`, C++ sample parity/layout/target test와 sample runner.
- historical reference: `framework/doc/plan/log/framework-public-contract-gap-implementation/cpp-g0-contract-ledger.ko.md`와 같은 디렉토리의 C++ 문서. historical log와 snapshot은 현재 완료 evidence로 사용하지 않았다.
- 진행 기록 정책: 실제 ledger 작업 중 각 조사·검증·판정 단계가 끝난 직후 `framework/doc/plan/cpp-framework-spec-gap-ledger/log/2026-08-02-progress.log`에 기록했다. 사후에 command 결과를 모아 만든 log는 완료 evidence로 사용하지 않는다.

### 2.3 판정 용어

- `충족(정적)`: 현재 source 또는 선언에서 좁은 조건을 확인했지만 build/runtime/process evidence까지 완료되었다는 뜻은 아니다.
- `gap`: common spec 또는 exact interface와 현재 실행 경로가 다르거나, 완료를 직접 증명할 gate가 없다.
- `contract 선행`: 구현 전에 public contract, exact C++ 표현 또는 package ownership을 먼저 확정해야 한다.
- `과거 evidence`: feature-map이나 log가 기록한 예전 실행 결과다. 현재 working tree의 증거로 승격하지 않는다.

### 2.4 Sample 후속 gate

이 문서는 Framework production·public contract·common E2E의 S0 gate를 소유한다. S0의 checklist가
완료된 뒤에만 sample ledger의 G2부터 시작한다. sample ledger는 공통 sample message와 실제 role
server call path를 검증하지만 public Framework API를 새로 정의하지 않는다.

S0 완료 후 S1에서 다음을 수행한다.

1. 6개 C++ sample의 exact message·field·transport inventory를 비교한다.
2. sample runner와 CMake/package provenance를 고정한다.
3. client-visible result, role-server evidence, owner/generation, callback, terminal reason과 cleanup을
   process에서 확인한다.
4. S1의 `CPP-SAMPLE-TEST-*`와 `CPP-SAMPLE-REG-*`가 통과한 뒤에만 S2 전체 C++ audit closure를
   기록한다.

S0의 Framework gap을 S1 sample code, private adapter 또는 raw route로 우회할 수 없다. S1의
`N/A` 범위인 ZoneWorld도 다른 언어 구현이나 common E2E 문서만으로 C++ public API를 추가하지 않는다.

## 3. 현재 검증 결과

### 3.1 Working tree와 범위

조사 시작 시점의 `git status --short`에는 이 문서와 별도의 사용자 변경이 함께 있었다. 당시 C++ 범위에는 다음 변경이 있었으며, 이 audit에서는 수정하지 않았다.

```text
 M framework/languages/cpp/framework/src/runtime/http/http_listener.cpp
```

초기 변경은 `asio::buffer_cast` 호출을 `mutable_buffer::data()`와 `const_buffer::data()`로 바꾼 내용이었다(`http_listener.cpp:94-97,137-138`). 이 변경은 이번 audit의 구현 수정이 아니다. 최종 status 재확인에서는 해당 C++ source가 더 이상 working-tree diff로 표시되지 않았고, 현재 source와 `HEAD`의 해당 경로가 일치했다. audit은 구현·test·E2E runner·정식 spec을 수정하지 않았으며, 상태 변화가 있었던 source를 되돌리거나 복원하지 않았다. 기존 historical C++ ledger가 있지만 requested full audit ledger와 같은 파일은 없으므로 이 경로의 ledger를 유지했다.

### 3.2 새로 실행한 검증

| 검증 | 실행 결과 | 해석 |
|---|---|---|
| CMake configure, `build-v11-tests`, Release, E2E OFF, `zlink_cpp=11.1.0`, Core `11.0.0`, vcpkg dependency prefix 사용 | 08:46:51 성공 | pinned dependency가 지정되면 configure는 완료된다. 실제 작업 재개 시 command와 결과를 step별 log로 남긴다. |
| dependency prefix 없이 별도 clean configure | 실패 | `protobufConfig.cmake`를 찾지 못했다. dependency 설치 누락이며 C++ source gap과 분리한다. |
| `cmake --build ... --target zlink_framework zlink_http_client zlink_stream_connector -j2` | 08:46:54 성공 | current source의 supported buffer access로 세 production target이 build된다. 이전 `buffer_cast` failure는 현재 tree의 실행 결과가 아니다. |
| `ctest --test-dir framework/languages/cpp/build-v11-tests -N` | 성공, 49 tests 수집 | inventory만 확인한다. test body 결과는 다음 bounded 실행과 구분한다. |
| `timeout 180s ctest --test-dir ... --output-on-failure --timeout 30` | 41 passed, 8 failed, exit 8 | `m6a` timeout, `m6b` abort, mesh-node vertical failure, app-host/http integration/store-resolver timeout, stream framework segfault, stream connector abort가 발생했다. target/layout/raw-route/install-consumer와 `test_cpp_http_client`는 통과했지만 전체 runtime gate는 실패했다. |
| `framework/languages/cpp/scripts/verify_packaged_contract.sh framework/languages/cpp/build-v11-tests` | 실패 | temporary install 뒤 `include/zlink/framework/contracts/locations/spot_handle.hpp`가 없어서 중단했다. exact spec와 verifier manifest 기준이 다르다. |
| `framework/languages/cpp/e2e/run_e2e_all.sh --max-attempts=1 --scenario-timeout-seconds=20` | exit 124 | 12-config aggregate가 시작됐지만 `RegistrationCodec` E2E target build가 19초 뒤 bounded timeout으로 종료됐다. 전체 process E2E PASS로 해석하지 않는다. |

현재 실행 결과와 실패 ID는 ledger 본문과 실제 작업 중 갱신한 [`log/2026-08-02-progress.log`](log/2026-08-02-progress.log)에 반영했다. CTest failure의 root cause는 이 audit에서 추가 진단하지 않았으며, 관찰된 timeout/abort/segfault와 sample process의 `invalid application payload version` abort 자체를 current evidence로 보존한다.

### 3.3 package와 cache 관찰

- source CMake는 `ZLINK_FRAMEWORK_CPP_ZLINK_CPP_VERSION=11.1.0`, `ZLINK_FRAMEWORK_CPP_ZLINK_CORE_VERSION=11.0.0`을 사용하고 `find_package(zlink_cpp 11.1.0 EXACT CONFIG REQUIRED)`를 호출한다(`framework/languages/cpp/CMakeLists.txt:41-87`).
- `framework/languages/cpp/build-v11-tests/CMakeCache.txt`는 Release, `11.1.0/11.0.0`이다.
- 별도 기존 `framework/languages/cpp/build/CMakeCache.txt`는 Debug, `ZLINK_FRAMEWORK_CPP_ZLINK_CPP_VERSION=11.0.2`이다. 이 build 결과를 현재 `11.1.0` package evidence로 사용하지 않는다.
- `framework/languages/cpp/e2e/RegistrationCodec/run_e2e.sh:5-6`은 E2E build directory를 `framework/languages/cpp/build`로 고정한다. 따라서 aggregate E2E는 `build-v11-tests`의 pinned package evidence와 다른 cache를 사용할 수 있다.
- C++ binding package config는 `find_dependency(zlink 11 CONFIG)`로 Core major만 요구한다. Framework source pin의 Core `11.0.0`과 clean package provenance를 exact하게 확인하는 단계가 남아 있다.
- `scripts/local-package/README.ko.md:204-219`는 C++ HTTP client를 별도 local package에서 제외하고 in-tree public header와 static library 정책으로 설명한다. 현재 CMake는 `zlink_http_client`를 별도 `HttpClient` install component와 `zlink_http_client_cpp` export로 설치한다(`framework/languages/cpp/CMakeLists.txt:627-630,665-668,730-733`). 이 ownership 차이는 package gap으로 남긴다.

## 4. 현재 충족 판정

다음은 전체 완료가 아니라 현재 확인 가능한 좁은 조건이다.

| 항목 | 현재 판정 | 근거와 제한 |
|---|---|---|
| Framework CMake target | 부분 충족 | `zlink_framework`와 `zlink::framework` alias, C++20, public include와 `zlink::cpp` link가 `CMakeLists.txt:158-230`에 있고 현재 세 production target build도 성공했다. 전체 CTest와 process E2E는 실패 또는 bounded timeout이다. |
| exact public header 위치 | 충족(정적) | `framework/include/zlink/framework/`가 설치 source tree에 있고 layout gate가 통과했다. exact method와 error enum은 아래 gap과 같이 다르다. |
| Core/Framework link boundary | 충족(정적) | Framework target은 `zlink::cpp`를 public dependency로 사용한다. public E2E client source에서 Core C ABI 또는 Framework runtime header 직접 호출은 확인하지 못했다. CMake가 client에 private include path를 제공하므로 clean consumer와 include-boundary gate가 필요하다. |
| async submit ownership skeleton | 부분 확인 | `async_submit_runtime_t`는 owner epoch, reservation, pending/ready queue와 shutdown completion을 구현한다(`async_submit_runtime.cpp:117-211,268-300`). callback count, payload lifetime, late signal, process recovery를 current process에서 확인하지 못했다. |
| client HTTP role 호출 | 충족(정적) | 여러 C++ client는 `zlink::http_client`로 role endpoint를 호출한다. `test_cpp_http_client`는 통과했지만 HTTP integration timeout과 aggregate E2E build timeout 때문에 role evidence까지 검증하지 못했다. |
| historical feature-map | 과거 evidence | 일부 문서는 예전 log를 `implemented`로 기록하지만 현재 aggregate 범위와 source tree가 common E2E 전체와 다르다. historical log만으로 충족 판정하지 않는다. |

## 5. `CPP-IMP-*` production implementation gap

### CPP-IMP-001 — HTTP buffer access compile blocker는 current tree에서 해소되었지만 runtime/process evidence가 남음

- 상태: `부분 충족`; 이전 build blocker는 current working tree에서 해소되었고 HTTP runtime/process gap은 남아 있다.
- common/exact 근거: `framework/doc/framework/common/spec/server/languages/cpp/60-http-hosting.ko.md`, `framework/doc/framework/common/spec/http-client/languages/cpp/cpp-http-client.ko.md:45-90`, `framework/languages/cpp/CMakeLists.txt:158-230`.
- C++ 경로: `framework/languages/cpp/framework/src/runtime/http/http_listener.cpp:85-145`, `framework/languages/cpp/CMakeLists.txt:195`.
- 확인한 동작과 기대 동작: 이전 source는 `asio::buffer_cast`를 사용해 build가 실패했지만, 현재 working tree는 `mutable_buffer::data()`와 `const_buffer::data()`를 사용한다(`http_listener.cpp:94-97,137-138`). 현재 `zlink_framework`, `zlink_http_client`, `zlink_stream_connector` build와 `test_cpp_http_client`는 통과했다. 기대 동작은 선택한 Boost tree의 supported buffer access와 synchronous buffer lifetime을 유지하면서 HTTP listener, typed client call과 role server process가 끝까지 동작하는 것이다.
- 판정 근거: 이전 `buffer_cast` compile failure는 현재 tree의 production build 결과가 아니므로 현재 compile blocker로 계속 표시할 수 없다. 그러나 `test_cpp_framework_http_integration`은 30초 timeout을 기록했고, aggregate E2E는 `RegistrationCodec` target build 중 bounded timeout으로 종료됐다. 따라서 HTTP role server와 client-visible/role-server evidence까지 완료되었다고 판정할 수 없다.
- 수정 목록: current `buffer.data()` 변경의 Boost version·buffer lifetime 계약을 source review와 compile probe로 고정한다. Framework와 HTTP client가 같은 Boost include tree를 사용하도록 CMake dependency를 고정한다. HTTP integration timeout의 원인을 별도 runtime test로 진단하고, public HTTP client의 typed `submit<T>()`, deadline/status mapping, role server startup과 cleanup을 process evidence로 닫는다.
- 필요한 회귀 검증: `CPP-REG-006`의 HTTP compile, typed submit, timeout, status/error mapping, HTTPS 조건부 test, role server startup과 shutdown.
- Core·bindings·package 선행 조건: Core와 `zlink_cpp` package를 바꾸지 않고 Framework의 Boost dependency만 고친다. package consumer가 source tree의 Boost header를 우연히 가져오지 않아야 한다.
- 완료 evidence: clean Release configure/build와 installed package consumer가 성공하고, HTTP integration과 aggregate role server process E2E가 bounded timeout 없이 통과한다. client-visible result, role evidence, buffer lifetime, terminal cleanup을 함께 확인하며, `buffer_cast` compile error가 없는 current log를 `log/`에 남긴다.

### CPP-IMP-002 — Worker public contract와 실제 scheduling/cancellation이 다름

- 상태: `contract 선행`, production gap.
- common/exact 근거: `framework/doc/framework/common/spec/server/languages/cpp/interfaces/01-common-runtime.ko.md:305-340`, `framework/doc/framework/common/e2e/config-8-execution-turn.ko.md`의 TD-C 계열.
- C++ 경로: `framework/languages/cpp/framework/include/zlink/framework/contracts/workers/worker.hpp:64-118`, `framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp:1013-1145`, `framework/languages/cpp/framework/src/runtime/dispatch/offload_executor.*`.
- 확인한 동작과 기대 동작: exact interface는 `worker_call_t::executor_t(std::stop_token)`와 `worker_options_t`의 thread/queue 설정을 요구한다. 현재 public header는 `std::optional<std::chrono::milliseconds>`를 executor argument로 사용하고 `worker_options_t`를 제공하지 않는다. CPU worker는 timeout마다 detached thread를 만들고, IO worker는 `work()`를 `run_io_worker` 호출 경로에서 바로 실행한 뒤 returned task를 관찰한다(`spot.hpp:1083-1144`). 기대 동작은 Spot/session 실행 문맥 밖에서 worker를 실행하고 host shutdown과 caller cancellation을 합친 `std::stop_token`을 전달하며, queue와 thread boundary를 지키는 것이다.
- 판정 근거: 선언의 parameter type만으로도 exact C++ contract와 다르다. IO worker의 호출 위치와 detached timeout thread 때문에 caller thread, timeout, shutdown, exactly-once completion의 runtime semantics도 process evidence가 없다.
- 수정 목록: exact interface를 먼저 확정한다. `std::stop_token` 전달과 worker options를 public contract에 맞추고, CPU/IO executor의 scheduling boundary를 분리한다. detached timeout thread가 host lifetime을 넘지 않게 owner-managed cancellation과 completion barrier를 사용한다. timeout, queue full, caller cancellation, host shutdown의 terminal mapping을 한 곳에서 처리한다.
- 필요한 회귀 검증: `CPP-REG-001` public compile, `CPP-REG-003` worker thread/cancellation/timeout/queue, `CPP-REG-004` same Spot/Actor lane order.
- Core·bindings·package 선행 조건: worker contract는 Framework 소유다. Core C ABI나 binding raw operation을 worker API로 노출하지 않는다.
- 완료 evidence: exact header compile, worker body가 caller serial thread 밖에서 실행되는 thread-id evidence, stop token 관찰, timeout/shutdown 중 completion count 1, queue full terminal, `yield()` 허용 문맥과 `invalid_operation`을 unit와 process E2E에서 확인한다.

### CPP-IMP-003 — Actor와 Spot public declaration이 C++ exact interface와 다름

- 상태: `contract 선행`, public contract gap.
- common/exact 근거: `framework/doc/framework/common/spec/server/languages/cpp/interfaces/04-spots.ko.md:196-229,383-417,853-899`, `framework/doc/framework/common/spec/server/languages/cpp/interfaces/05-actors.ko.md:149-245,278-310`.
- C++ 경로: `framework/languages/cpp/framework/include/zlink/framework/contracts/actors/actor.hpp:269-447,637-675,773-877`, `framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp:948-1005,1774-1825`.
- 확인한 동작과 기대 동작: 현재 `actor_client_t`는 `send_to_actor(actor_ref_t, ...)`와 `request_to_actor(actor_ref_t, ...)`를 제공하지만 exact interface는 global `actor_id_t`를 받는 `send`와 `request`를 고정하고 ActorRef/owner overload를 제공하지 않는다. `actor_request_call_t`에는 exact actor interface의 metadata method가 없고, `actor_create_call_t`와 `spot_create_call_t`에는 exact `yield()`가 없다. 현재 manager는 concrete copyable value class이고 exact interface는 virtual destructor와 virtual operations를 요구한다. `session_actor_t`는 `session_message_context_t` overload 대신 packet-name overload를 제공한다. `actor_context_t::actor_id()`는 `std::string_view`이고 exact interface는 `const actor_id_t &`다. 현재 `spot_context_t::publish`는 topic만 받고 exact interface는 `channel_name`과 topic을 함께 받는다.
- 판정 근거: source에 관련 class와 method가 있다는 사실은 parameter, const/reference/value, move/copy, virtual/lifetime 계약의 불일치를 해결하지 못한다. 현재 public header를 exact interface 충족으로 세지 않는다.
- 수정 목록: contract governance에 따라 exact C++ declaration을 먼저 확정하고 public header, internal adapter, ownership과 destructor를 같은 방향으로 맞춘다. global ID lookup과 exact ActorRef routing을 서로 다른 operation 의미로 분리한다. Spot publish target ChannelName, Actor/Spot create `yield`, session message context와 metadata를 runtime path까지 연결한다. 기존 호출부에 raw ref/handle workaround를 추가하지 않는다.
- 필요한 회귀 검증: `CPP-REG-001`의 positive/negative header compile, move/copy/noexcept/virtual ABI probe, actor/spot manager call, session context relay, publish parameter compile과 installed consumer compile.
- Core·bindings·package 선행 조건: Actor/Spot public contract는 Framework가 소유한다. Core 전용 service ABI, internal `spot_handle_t` 또는 source-only resolver를 public 대체 표면으로 추가하지 않는다.
- 완료 evidence: exact interface와 public header의 declaration diff가 0이고, installed package에서 동일 compile probe가 성공한다. actor/spot create, request, relay, publish가 local/remote/reject/moving generation에서 같은 public contract와 terminal mapping을 process evidence로 남긴다.

### CPP-IMP-004 — Public error enum과 terminal error mapping이 common contract와 다름

- 상태: `contract 선행`, public ABI gap.
- common/exact 근거: `framework/doc/framework/common/spec/server/languages/cpp/interfaces/03-channel-messaging.ko.md:564-585`, `framework/doc/framework/common/spec/server/languages/cpp/interfaces/01-common-runtime.ko.md:342-347`, `framework/doc/framework/common/spec/server/languages/cpp/interfaces/05-actors.ko.md:189-191`.
- C++ 경로: `framework/languages/cpp/framework/include/zlink/framework/contracts/errors/error.hpp:13-177`, `framework/languages/cpp/framework/src/runtime/messaging/request_failure_mapper.cpp`, `framework/languages/cpp/framework/src/runtime/messaging/async_submit_runtime.cpp:139-140,298-299`.
- 확인한 동작과 기대 동작: exact C++ interface는 안정적인 13개 `framework_error_kind_t` 값인 `not_found`부터 `internal_failure`까지를 정의한다. 현재 header는 actor/spot/worker/relocation/runtime별 40개 public enumerator를 제공하고, `runtime_shutdown`과 `request_failed`를 detail boundary로 조합해 `std::errc`를 만든다. 기대 동작은 application이 닫힌 common kind로 분기하고 platform `error_code`는 진단 정보로만 사용하며, timeout, route, shutdown, stale generation이 exact kind로 일관되게 전달되는 것이다.
- 판정 근거: public enum의 이름과 값 집합이 exact declaration과 직접 다르다. 일부 runtime source가 shutdown과 owner cleanup을 처리하는 것은 확인했지만 모든 operation family의 mapping과 installed ABI가 exact kind를 사용한다는 증거는 없다.
- 수정 목록: common 13-value enum과 numeric ABI를 먼저 고정한다. 내부 세부 원인은 private mapping에 두고 public `kind()`와 optional diagnostic `code()`를 분리한다. request/send/publish/actor/spot/stream/worker의 error table을 하나의 mapper와 contract test로 통합한다. `runtime_shutdown`, `worker_timed_out` 같은 public extra name을 호환 surface로 남길지는 governance에서 명시적으로 결정한다.
- 필요한 회귀 검증: `CPP-REG-001` enum value compile, `CPP-REG-005` operation별 kind/code/retriable/exception, shutdown·timeout·stale generation terminal exactly-once process test.
- Core·bindings·package 선행 조건: Core errno와 Framework error kind를 같은 enum으로 재사용하지 않는다. binding package의 error code가 Framework public ABI를 결정하지 않도록 package boundary를 확인한다.
- 완료 evidence: exact 13개 numeric value compile probe, installed symbol/package probe, 각 common E2E의 expected terminal reason, `framework_exception_t::kind()`와 `code()` assertion, callback count 1을 clean runtime에서 확인한다.

### CPP-IMP-005 — Submit/admission call path의 ownership과 lifecycle semantics가 process evidence로 닫히지 않음

- 상태: `contract 선행`, runtime semantics gap.
- common/exact 근거: `framework/doc/framework/common/spec/server/languages/cpp/interfaces/03-channel-messaging.ko.md:927-968`, `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md`, `config-2-spot-service.ko.md`, `config-5-resilience-lifecycle.ko.md`, `config-10-spot-actor-relocation.ko.md`, `config-13-submit-admission.ko.md`.
- C++ 경로: `framework/languages/cpp/framework/include/zlink/framework/contracts/channels/call.hpp:77-127,169-252,298-317`, `framework/languages/cpp/framework/src/runtime/messaging/async_submit_runtime.cpp:144-211,268-300`, `framework/languages/cpp/framework/src/runtime/messaging/submit_queue.*`, actor/spot/stream runtime callers.
- 확인한 동작과 기대 동작: 현재 request call은 serial turn plan이 있을 때 detached thread에서 `_submit(...).result()`를 실행하고 결과를 completion source에 넣는다. channel request도 `blocking_submit().result()`를 detached thread에서 실행한다. one-way runtime은 첫 attempt 뒤 backpressure를 pending queue와 owner reservation에 넣고 deadline 또는 shutdown에서 completion한다. 기대 동작은 source-local queue admission, bounded deadline, no automatic resubmit after terminal, owner/generation fencing, payload lifetime, retry/replay order와 callback exactly-once가 operation family별로 동일하게 지켜지는 것이다.
- 판정 근거: detached bridge가 serial turn을 점유하지 않도록 의도된 주석은 있으나, 현재 source와 process E2E에서 detached capture, moved message, late readiness, shutdown race, HWM, stream token, relocation queue와 role evidence를 함께 확인하지 못했다. 이번 bounded CTest에서도 `m6a`, app-host, HTTP integration, store resolver와 stream 관련 timeout/abort/segfault가 관찰되었다. SubmitAdmission feature-map도 20개 중 일부만 process evidence를 제공한다. 따라서 narrow source inspection이나 일부 unit PASS를 full semantic compliance로 판정하지 않는다.
- 수정 목록: operation별 admission state machine과 terminal reason을 명시하고, pending reservation/owner epoch/late signal/retry credit를 하나의 runtime path로 검증한다. callback signature와 실행 thread, callback lifetime을 exact contract에 맞춰 확인하고, detached thread가 task와 payload를 보유하는 lifetime을 owner-managed completion으로 바꿀 필요가 있는지 확인한다. queue replay와 relocation backlog가 location commit보다 앞서거나 뒤지는 순서를 evidence로 고정한다. application caller에 raw frame, `parse`, `decode`, internal helper를 추가하지 않는다.
- 필요한 회귀 검증: `CPP-REG-002` admission queue/retry/deadline/owner epoch, `CPP-REG-004` callback/queue order, `CPP-REG-005` terminal mapping, process E2E `SA-E2E-01~20`, `ST-*`, `RM-*`, `SM-*`의 required evidence.
- Core·bindings·package 선행 조건: Core HWM와 native route readiness는 Core/binding package의 실제 candidate runtime으로 검증한다. Framework는 Core service C ABI를 새로 만들지 않고 existing public `zlink::cpp` operation만 사용한다. package version drift가 없는 build를 사용해야 한다.
- 완료 evidence: local/remote/self/unknown/known-disconnected target, ready/pending/timeout/shutdown, owner restart, stream reply token, relocation backlog와 late signal에 대해 attempt count, reservation count, payload value, generation, callback count, terminal reason을 role server evidence와 client result로 모두 확인한다.

### CPP-IMP-006 — Package ownership, install/export, version provenance와 ABI 검증이 서로 다름

- 상태: `contract 선행`, package/ABI gap.
- common/exact 근거: C++ exact interface의 public include 목록, `framework/doc/framework/common/spec/http-client/languages/cpp/cpp-http-client.ko.md:45-70`, `scripts/local-package/README.ko.md:204-240`.
- C++ 경로: `framework/languages/cpp/CMakeLists.txt:41-87,158-230,272-305,618-735`, `framework/languages/cpp/cmake/zlink_framework_cppConfig.cmake.in:8-32`, `bindings/cpp/cmake/zlink_cppConfig.cmake.in:1-6`, `framework/languages/cpp/scripts/verify_packaged_contract.sh:31-76`.
- 확인한 동작과 기대 동작: source CMake는 `zlink::framework`와 `zlink::http_client`를 각각 export하고 Framework package에 `zlink_cpp` include, static library와 CMake targets를 복사한다. local package policy는 C++ HTTP client를 별도 package에서 제외한다고 설명한다. verifier는 존재하지 않는 `locations/spot_handle.hpp`를 required path로 요구하고 `spot_ref_t` token을 forbidden으로 둔다. existing Debug cache는 binding `11.0.2`를 사용하고, E2E runner도 그 `build` cache를 사용한다. 기대 동작은 exact C++ contract에 맞는 header/target만 install하고, Framework와 E2E가 exact `zlink_cpp=11.1.0`과 Core `11.0.0` provenance를 사용하며, clean consumer가 source tree 없이 compile하는 것이다.
- 판정 근거: package verifier가 temporary install에서 stale manifest 때문에 실패했고, HTTP package ownership 문서와 CMake export가 다르며, `build`와 `build-v11-tests` 사이에 cache version drift가 확인되었다. target alias가 source에 있다는 것만으로 install tree, E2E runtime과 ABI가 일치한다고 판정할 수 없다.
- 수정 목록: HTTP client ownership을 common package policy와 C++ exact guide에 맞춰 하나로 결정한다. verifier manifest를 current exact header와 target으로 재작성한다. Core dependency를 required exact provenance로 확인하고 binding config의 major-only dependency를 package policy에 맞춘다. clean install consumer, target export, header include, `nm`/symbol visibility, C++20, static/shared와 compiler matrix를 gate에 추가한다.
- 필요한 회귀 검증: `CPP-TEST-001`, `CPP-REG-008` clean install consumer와 version/ABI matrix, `CPP-REG-009` Core/binding provenance와 private include negative scan.
- Core·bindings·package 선행 조건: `.artifacts/wsl/install/zlink-cpp/11.1.0`과 Core `11.0.0`을 새로 만들어 hash와 path를 기록해야 한다. stale `framework/languages/cpp/build`를 재사용하지 않는다. Framework는 bindings source를 직접 참조하지 않고 local package만 사용한다.
- 완료 evidence: Framework/StreamConnector/FrameworkDependency component install, `find_package(zlink_framework_cpp CONFIG REQUIRED)` clean consumer compile/run, required/forbidden manifest pass, exported target dependency closure, library/header hash와 version print, Debug/Release ABI probe가 모두 통과한다.

## 6. `CPP-E2E-IMP-*` C++ E2E implementation, runner와 process evidence gap

### Common E2E inventory 요약

common E2E 문서에서 추출한 ID는 14개 config, 총 374개다. C++ aggregate는 현재 12개 config만 선택한다.

| Config | common ID 수 | 현재 C++ selector/dispatch | 현재 확인된 누락 또는 비정식 상태 |
|---|---:|---|---|
| Config 1 Location messaging | 17 | 16 | `RM-A7`가 aggregate와 RegistryMessaging runner에 없다. |
| Config 2 Spot service | 66 | 51 exact ID와 composite selector | `SM-A9~A13`, `SM-B0`, `SM-B0A`, `SM-B10`, `SM-B11`, `SM-C6`, `SM-D4A`, `SM-D4B`, `SM-D5A`, `SM-G5A`, `SM-G5B`가 없다. |
| Config 3 PubSub | 24 | 7 | `PS-D1~D6`, `PS-D7A`, `PS-D7B`, `PS-E1`, `PS-E2A~E2C`, `PS-F1~F5`가 없다. |
| Config 4 Registration codec | 12 | 11 | `RC-B6`가 없다. `RC-A1/A2`는 C++ exact contract의 reflection/attribute 예외로 명시적 N/A 또는 alternative evidence가 필요하다. |
| Config 5 Resilience lifecycle | 39 | 19와 `RL-consumer` | `RL-D5`, `RL-E1~E5`, `RL-F1~F14`가 `all`에서 실행되지 않는다. |
| Config 6 Store failure recovery | 28 | 10 | `SF-B3`, `SF-C3~C5`, `SF-F1~F11`, `SF-G1~G3`가 없다. |
| Config 7 Monitoring | 12 | 7 exact와 비정식 `MON-A4`, `MON-D1` | `MON-A4A`, `MON-A4B`, `MON-A6`, `MON-D1A`, `MON-D1B`가 없다. `A4`, `D1`은 exact ID의 대체가 아니다. |
| Config 8 Execution turn | 32 | `ATD-*`와 legacy `TD-C*`, `TD-E*` | common `TD-*` 32개와 일대일 selector/dispatch가 없다. feature-map도 전체를 `전환 필요`로 표시한다. |
| Config 9 ToActor messaging | 7 | `TA-A1~B3` | current server source는 public `mesh.peer_connections().disconnect/connect`를 사용한다. stale feature-map의 `router_connections()` 주장은 현재 source와 다르므로 구현 gap이 아니라 map/evidence drift다. |
| Config 10 Spot/Actor relocation | 43 | A~F 일부 | `ST-E1A`, `ST-E1B`, `ST-E1C`, `ST-F3A`, `ST-G1~G6`, `ST-H1~H5`, `ST-I1~I6`가 full process gate에 없다. |
| Config 11 Observability | 22 | `OBS-A1~A4`, `OBS-B1~B4`, `OBS-C1~C5` | `OBS-A5`, `OBS-C6~C12`가 없다. |
| Config 12 Channel egress routing | 16 | C++ directory/target 없음 | 전체 config가 aggregate에서 빠졌다. |
| Config 13 Submit admission | 20 + 4 regression | process 5개, regression 3개 | `all`은 implemented subset만 실행한다. partial/unimplemented/`SA-REG-03 N/A`/`SA-REG-04`를 완료 분모에서 분리하지 않는다. |
| Config 14 Instance Spot | 36 | C++ directory/target 없음 | 전체 config가 aggregate에서 빠졌다. |

### CPP-E2E-IMP-001 — Aggregate가 common 14-config 전체를 실행하지 않음

- 상태: gap, runner 범위 blocker.
- common/E2E 근거: `framework/doc/framework/common/e2e/README.ko.md:10-20,163-172,203-240`, `config-12-channel-egress-routing.ko.md`, `config-14-instance-spot.ko.md`.
- C++ 경로: `framework/languages/cpp/e2e/run_e2e_all.sh:28-41,117-121`, `framework/languages/cpp/CMakeLists.txt:1428-1809`; C++ E2E directory 목록에 `ChannelEgressRouting`과 `InstanceSpot`이 없다.
- 확인한 동작과 기대 동작: aggregate `all`은 RegistrationCodec부터 SubmitAdmission까지 12개 directory만 `all` selector로 실행하고 exit code 0을 config PASS로 집계한다. common은 14개 config와 374개 ID를 요구한다. Config 12와 14는 C++ selector, role target, client dispatch 모두 없다. 현재 bounded probe는 12-config 실행을 시작했지만 첫 RegistrationCodec target build에서 19초 뒤 timeout으로 종료됐다.
- 판정 근거: 현재 aggregate 성공은 common 전체의 실행 결과가 아니며, 이번 실행도 전체 config에 도달하지 못했다. 없는 두 config를 단순 skip하거나 성공으로 표시하면 scenario inventory gap을 숨긴다. aggregate E2E가 사용하는 build directory도 pinned `build-v11-tests`가 아니라 기존 Debug `build`다.
- 수정 목록: 먼저 각 missing scenario에 C++ exact public contract 근거가 있는지 확인한다. 근거가 있으면 실제 role server/client와 exact ID selector를 추가한다. 근거가 없으면 public API를 만들지 않고 feature-map에 contract gap과 설계 이슈를 남긴다. aggregate는 required inventory와 executed inventory를 비교해 누락을 실패로 표시한다.
- 필요한 회귀 검증: `CPP-REG-007` common ID inventory, selector dispatch, aggregate denominator, Config 12/14 absence negative test.
- Core·bindings·package 선행 조건: Config 12/14 구현은 Core service C ABI나 raw frame 우회가 아니다. 필요한 public contract와 package version을 먼저 확정하고 current runtime package로 실행한다.
- 완료 evidence: `run_e2e_all.sh all`이 14개 config를 출력하고 각 config에서 exact ID 목록과 role evidence manifest를 생성한다. 374개 ID와 executed ID set의 차이가 0이며, 누락 config는 PASS로 표시되지 않는다.

### CPP-E2E-IMP-002 — Config 1과 Config 2의 scenario ID, selector와 dispatch가 common inventory보다 작음

- 상태: gap.
- common/E2E 근거: `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md:87-412`, `config-2-spot-service.ko.md:42-1004`.
- C++ 경로: `framework/languages/cpp/e2e/RegistryMessaging/run_e2e.sh:635-665`, `framework/languages/cpp/e2e/SpotService/Client/main.cpp:3-56`, `framework/languages/cpp/e2e/SpotService/run_e2e.sh:3867-3881`, `framework/languages/cpp/e2e/SpotService/feature-map.ko.md`.
- 확인한 동작과 기대 동작: RegistryMessaging `all`은 `RM-A1,A2,A3,A4,A6,B1,B2,C1~C5,C7~C9`만 실행해 `RM-A7`을 제외한다. SpotService `all`은 여러 scenario를 child로 실행하지만 A1/A2/A4/F1/F2를 composite selector로 묶고, 위 표의 15개 common ID를 dispatch하지 않는다. common은 global Actor/Spot identity, explicit create/find, stream, binding, placement와 capacity matrix까지 요구한다.
- 판정 근거: current runner가 `spot-service e2e result=passed`를 출력해도 common ID set 전체가 실행된 것이 아니다. composite selector는 exact scenario evidence의 일대일 추적을 막고, historical feature-map의 source type 존재를 process completion으로 바꿀 수 없다.
- 수정 목록: selector를 common ID로 정규화하고 composite selector에는 포함된 exact ID별 독립 marker를 남긴다. missing ID는 role server endpoint, client result, source/target evidence와 terminal reason을 갖는 실제 process scenario로 분리한다. `SM-A9~A13` 등 public contract가 없는 항목은 contract 선행으로 분리한다.
- 필요한 회귀 검증: `CPP-REG-007` inventory/selector, `CPP-REG-009` role endpoint와 evidence, Config 1/2 process matrix의 owner/generation/callback/cleanup assertion.
- Core·bindings·package 선행 조건: Spot/Actor routing은 Framework public API와 `zlink::cpp` package만 사용한다. `spot_handle_t` 또는 Core internal API를 missing scenario를 채우는 우회로로 사용하지 않는다.
- 완료 evidence: Registry/Spot runner가 exact common ID를 각각 선택할 수 있고 unknown ID를 거부하며, `all` output의 scenario count와 evidence manifest가 common list와 일치한다. local/remote, stream, admission, placement, cleanup 결과가 client와 role server에서 각각 확인된다.

### CPP-E2E-IMP-003 — Config 3과 Config 4의 PubSub/Registration codec 범위와 상태 분류가 불완전함

- 상태: gap; `RC-A1/A2`는 contract 선행이 아니라 C++ exact exception을 명시해야 한다.
- common/E2E 근거: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md:52-452`, `config-4-registration-codec.ko.md:58-266`, common README의 role server/evidence 규칙 `:40-71`.
- C++ 경로: `framework/languages/cpp/e2e/PubSub/run_e2e.sh:397-531`, `framework/languages/cpp/e2e/PubSub/Client/main.cpp:18-47`, `framework/languages/cpp/e2e/RegistrationCodec/run_e2e.sh:8-17`, `RegistrationCodec/Client/main.cpp:22-87`, 각 feature-map.
- 확인한 동작과 기대 동작: PubSub runner는 `PS-A1~A4`, `PS-B1~B2`, `PS-C1`만 selector로 받으며 D/E/F topology, discovery, store, observer와 liveness matrix를 받지 않는다. Registration runner는 `RC-A1~A6`, `RC-B1~B5`를 받지만 `RC-B6`이 없다. Feature-map은 C++ RC-A1/A2를 reflection/attribute 자동 등록의 `not-supported`로 설명하면서 runner client는 해당 source를 include한다. common 완료는 실제 subscriber role evidence, packet-name dispatch, reconnect/non-replay와 five-language JSON result parity를 요구한다.
- 판정 근거: PubSub feature-map의 historical/transition log와 current runner PASS는 Config 3 전체의 process evidence가 아니다. RC-A1/A2는 다른 언어의 attribute 기능을 C++ public API로 추가하는 근거가 아니며, explicit C++ exception 또는 N/A evidence가 필요하다.
- 수정 목록: PubSub D/E/F scenario의 contract 근거와 exact C++ public path를 먼저 확정하고, publisher/subscriber role server의 bounded `/evidence/wait`와 client-visible result를 추가한다. RC-B6 cross-language JSON parity를 C++ exact serialization path로 검증한다. RC-A1/A2는 C++ reflection을 새로 만들지 않고 exact interface의 exception을 feature-map/runner output에서 명시한다.
- 필요한 회귀 검증: `CPP-REG-007` ID/selector, `CPP-REG-009` role evidence/public client, codec content-type/error and subscriber exactly-once process checks.
- Core·bindings·package 선행 조건: typed JSON/codec 책임은 Framework serializer와 registered extension이 소유한다. client에 `encode/decode/parse` 우회와 raw frame 처리를 추가하지 않는다. package에는 matching codec extension target만 포함한다.
- 완료 evidence: every exact ID has a client dispatch, actual role server endpoint, subscriber/provider evidence and terminal assertion; `RC-B6` passes; RC-A1/A2 output is explicit C++ N/A/exception and is not counted as an implemented feature.

### CPP-E2E-IMP-004 — Config 5와 Config 6은 A~D/E 일부만 실행하고 resilience/store-failure 전체를 증명하지 않음

- 상태: gap.
- common/E2E 근거: `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md` 전체, `config-6-store-failure-recovery.ko.md:53-509`.
- C++ 경로: `framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh:32-48`, `ResilienceLifecycle/feature-map.ko.md:13-32`, `framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh:40-70`, `DiscoveryRegistryHa/feature-map.ko.md:15-30`.
- 확인한 동작과 기대 동작: Resilience `all`은 `RL-consumer`, A1~A5, B1~B6, C1~C4, D1~D4만 실행하고 D5와 E/F track을 실행하지 않는다. common missing set은 `RL-D5`, `RL-E1~E5`, `RL-F1~F14`다. StoreFailure `all`은 SF-A1,A2,B1,B2,C1,C2,D1,D2,D3,E1만 실행하고 B3, C3~C5, F1~F11, G1~G3를 제외한다.
- 판정 근거: runner의 final PASS는 provider crash/restart, lease, placement, relocation, cancellation과 latency isolation의 일부 subset만 확인한다. feature-map도 missing rows를 명시한다.
- 수정 목록: common ID를 runner의 canonical selector와 feature-map에 일치시킨다. provider/consumer role process에서 owner lease, generation, ready/degraded/ready sequence, shutdown drain, unrelated request latency와 cleanup을 직접 assert한다. 긴 sleep이나 기존 120회 burst를 missing soak contract의 대체로 사용하지 않는다.
- 필요한 회귀 검증: `CPP-REG-007` inventory, `CPP-REG-009` role evidence, resilience/store process matrix와 bounded timeout/cleanup.
- Core·bindings·package 선행 조건: Redis/local package와 Core runtime candidate를 hash로 고정한다. Core failure/recovery 의미를 Framework public status로 대체하지 않고 boundary를 분리한다.
- 완료 evidence: each missing ID has exact role topology, fault injection, client result, provider/consumer evidence, owner/generation/cleanup assertion; aggregate does not print PASS while D5/E/F or F/G rows are absent.

### CPP-E2E-IMP-005 — Config 7, Config 8과 Config 9의 exact ID와 public execution semantics가 일치하지 않음

- 상태: gap; Config 8은 `contract 선행`.
- common/E2E 근거: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md:61-271`, `config-8-execution-turn.ko.md:53-605`, `config-9-to-actor-messaging.ko.md:52-167`.
- C++ 경로: `framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh:8-16,451-570`, `RuntimeMonitoring/Client/main.cpp:18-75`, `AutomaticTurnDispatch/run_e2e.sh:8-16`, `AutomaticTurnDispatch/Client/main.cpp`, `AutomaticTurnDispatch/feature-map.ko.md`, `ToActorMessaging/Client/main.cpp:18-47`, `ToActorMessaging/Server/Caller/main.cpp:57-85`.
- 확인한 동작과 기대 동작: Monitoring runner와 client는 `MON-A4`, `MON-D1`이라는 비정식 ID를 사용하고 common의 `MON-A4A/A4B`, `MON-D1A/D1B`, `MON-A6`를 사용하지 않는다. AutomaticTurnDispatch는 common `TD-*` 대신 `ATD-*`와 일부 legacy `TD-C*`, `TD-E*`를 받아 32개 exact scenario와 일대일 mapping이 없다. ToActor client는 TA-A1~B3를 dispatch하고 caller role server는 `mesh.peer_connections().disconnect/connect`를 사용한다. 이 마지막 경로는 public API 사용 측면에서 current source가 feature-map의 stale `router_connections()` 설명보다 앞서 있으며, 현재는 static satisfied로 기록한다.
- 판정 근거: custom name의 source scenario가 존재해도 common `TD-*` contract를 검증했다는 뜻은 아니다. Monitoring alias도 exact status field와 scenario ID를 대체하지 않는다. TA-B3는 반대로 current source와 feature-map이 달라 feature-map drift가 있으며, 이를 public API gap으로 잘못 분류하지 않아야 한다.
- 수정 목록: Config 7의 canonical ID와 evidence field를 맞춘다. Config 8은 exact TD contract를 먼저 확정하고 ATD 이름을 그대로 공통 ID로 취급하지 않는다. Config 9 feature-map의 stale claim을 current source evidence로 정정하는 후속 문서 작업을 ledger에 남기고 public `peer_connections()` 경로와 route failure result를 유지한다.
- 필요한 회귀 검증: `CPP-REG-001` worker/turn declarations, `CPP-REG-004` turn lane/order, `CPP-REG-007` ID alias rejection, `CPP-REG-009` client public include/role server route. TA-B3 positive source scan은 false gap을 방지한다.
- Core·bindings·package 선행 조건: turn semantics는 Framework execution queue와 public HTTP/stream connector가 소유한다. route disconnect는 public Framework mesh peer API로 수행하며 Core internal endpoint helper를 client에 노출하지 않는다.
- 완료 evidence: common ID 하나를 지정하면 동일 ID의 client, role server, evidence marker가 실행되고, `TD-A1~G1` 전체 mapping과 turn/cancellation order가 process에서 확인된다. TA-B3는 public peer API와 `route_not_connected`/restored evidence를 직접 확인한다.

### CPP-E2E-IMP-006 — Config 10 `all`이 A~F subset을 full completion처럼 표시함

- 상태: gap; 여러 row는 `contract 선행` 또는 runtime/process blocker다.
- common/E2E 근거: `framework/doc/framework/common/e2e/config-10-spot-actor-relocation.ko.md:51-648`, 특히 G/H/I와 H4A/H4B.
- C++ 경로: `framework/languages/cpp/e2e/SpotActorTransfer/Client/main.cpp:3-103`, `SpotActorTransfer/run_e2e.sh`, `SpotActorTransfer/feature-map.ko.md:8-58`, `framework/languages/cpp/e2e/run_e2e_all.sh:38-40`, `framework/languages/cpp/CMakeLists.txt:1672-1709,1823-1847`.
- 확인한 동작과 기대 동작: feature-map은 `run_e2e.sh all`이 A~F track만 실행한다고 명시하고, current client dispatch도 A1~F6 범위다. common missing set은 `ST-E1A`, `ST-E1B`, `ST-E1C`, `ST-F3A`, `ST-G1~G6`, `ST-H1~H5`, `ST-I1~I6`다. CMake는 여러 SpotService/ATD/SpotActor host target을 removed SpotMesh/SpotNode migration target으로 설명하고 `EXCLUDE_FROM_ALL`로 설정한다. SpotService/ATD source는 현재 installed public header에 없는 `spot_handle_resolver_t`와 old handle path를 참조한다(`SpotService/Server/Play/play_host_factory.hpp:156-205`, `AutomaticTurnDispatch/Server/Session/Support/await_session.hpp:21-31,324-339`).
- 판정 근거: `spot-actor-transfer e2e partial result=passed`와 A~F child PASS는 common Config 10 전체 완료가 아니다. H/I process path는 feature-map이 component-only, blocked 또는 미구현으로 기록하고, migration host compile path도 current build에서 independently 증명되지 않았다.
- 수정 목록: exact public Actor/Spot/relocation contract를 먼저 확정한다. old `spot_handle`/resolver를 public API 또는 Core ABI로 되살리지 않고 role server를 current public `spot_ref_t`, manager, actor and context surface로 전환한다. A~I exact selector와 role evidence를 분리하고 partial subset은 aggregate PASS가 아니라 incomplete로 표시한다.
- 필요한 회귀 검증: `CPP-REG-001` actor/spot/relocation headers, `CPP-REG-004` queue/authority/callback order, `CPP-REG-005` terminal mapping, `CPP-REG-007` full Config 10 inventory and partial rejection, process fault/recovery tests.
- Core·bindings·package 선행 조건: relocation wire and location store implementation remains Framework/Core boundary; no private Core service C ABI or test-only handle adapter. Current Core/binding package must be installed before role server build.
- 완료 evidence: every A-I scenario has compiled role server using installed public headers, source/target owner and generation evidence, backlog/replay order, callback count and cleanup. Aggregate output refuses to label A-F-only execution as Config 10 PASS.

### CPP-E2E-IMP-007 — Config 11 observability client dispatch가 common C track의 절반만 가짐

- 상태: gap.
- common/E2E 근거: `framework/doc/framework/common/e2e/config-11-observability-ops.ko.md:53-403`.
- C++ 경로: `framework/languages/cpp/e2e/ObservabilityOps/Client/main.cpp:3-50`, `ObservabilityOps/run_e2e.sh:240-705`, `ObservabilityOps/feature-map.ko.md:13-35`.
- 확인한 동작과 기대 동작: C++ client dispatch는 `OBS-A1~A4`, `OBS-B1~B4`, `OBS-C1~C5`만 포함한다. common은 `OBS-A5`와 `OBS-C6~C12`까지 요구한다. runner의 `all`은 flow/metrics/fanout/drain/handoff/force/offnode subset을 PASS로 출력하지만 feature-map은 C1~C5를 이전 contract 흔적으로 설명하고 C4/C5 등 일부를 deferred로 둔다.
- 판정 근거: current client source의 `else` unknown scenario path와 feature-map의 missing/deferred rows가 exact common inventory gap을 직접 보여 준다. flow log와 marker가 있어도 tracing level change, rolling update, planned maintenance, shutdown deadline과 relocation cancellation 전체를 증명하지 않는다.
- 수정 목록: exact OBS-A5/C6~C12 role topology와 runtime observation fields를 구현하거나, public contract 근거가 없는 항목은 contract gap으로 분리한다. client-visible result, actor/session owner evidence, metric label, cancellation/shutdown reason을 ID별로 assertion한다. `all` output은 실제 실행 ID를 출력한다.
- 필요한 회귀 검증: `CPP-REG-007` inventory, `CPP-REG-009` role evidence, observation sequence/snapshot resync, metric label and shutdown/relocation process tests.
- Core·bindings·package 선행 조건: monitoring API는 Framework public snapshot/event contract를 사용한다. Core internal trace 또는 driver-only evidence를 client-visible completion으로 사용하지 않는다.
- 완료 evidence: OBS-A1~C12 each has public observation request, actual role server evidence, sequence/field assertions and terminal cleanup; no deferred row is included in aggregate PASS.

### CPP-E2E-IMP-008 — Config 13 `all`이 partial, unimplemented와 N/A를 성공으로 포장할 위험이 있음

- 상태: gap.
- common/E2E 근거: `framework/doc/framework/common/e2e/config-13-submit-admission.ko.md:63-406`, common README `:72-77,163-201`.
- C++ 경로: `framework/languages/cpp/e2e/SubmitAdmission/run_e2e.sh:29-57,222-229,433`, `SubmitAdmission/feature-map.ko.md:21-57`.
- 확인한 동작과 기대 동작: runner의 `IMPLEMENTED_PROCESS`는 SA-E2E-01, 08, 09, 14, 20이고 `IMPLEMENTED_REGRESSION`은 SA-REG-01, 02, 03이다. `all`은 이 subset만 실행하고 `SubmitAdmission PASS`를 출력한다. feature-map은 나머지 대부분을 `부분 구현` 또는 `미구현`으로 기록하고 SA-REG-03을 C++ contract상 N/A, SA-REG-04를 미구현으로 기록한다. common completion은 SA-E2E-01~20과 SA-REG-01~04를 모두 요구한다.
- 판정 근거: `N/A`를 명시적으로 제외한 것이 아니라 `all`의 성공 경로에 섞으면 사용자는 common 전체가 통과했다고 해석할 수 있다. partial row의 missing observer와 process topology는 feature-map에 이미 적혀 있다.
- 수정 목록: aggregate를 `implemented subset PASS`와 `common completion INCOMPLETE`로 분리한다. `all`은 common required set에 대해 missing/partial/N/A manifest를 출력하고 full PASS를 금지한다. process scenario는 caller, target, ClientServer, Spot, Actor, Session, Stream family의 role evidence를 추가한다.
- 필요한 회귀 검증: `CPP-REG-007` aggregate denominator and N/A policy, `CPP-REG-002` admission semantics, `CPP-REG-009` role evidence and package provenance.
- Core·bindings·package 선행 조건: feature-map이 요구하는 Core readiness/HWM observer가 Core public package에 없으면 새 Core service ABI를 만들지 않고 contract gap으로 남긴다. C++ package version/hash를 log에 남긴다.
- 완료 evidence: all output에 24개 required selector의 state가 `passed`, `partial`, `unimplemented`, `N/A`로 분리되고 full completion은 모든 applicable row가 evidence를 가질 때만 0 exit/PASS가 된다.

### CPP-E2E-IMP-009 — role server evidence, private include boundary와 C++ CI enforcement가 aggregate semantics를 보장하지 않음

- 상태: gap; package/build 선행 조건과 연결됨.
- common/E2E 근거: `framework/doc/framework/common/e2e/README.ko.md:40-71,163-172,174-240`, `framework/doc/framework/common/e2e/config-*.ko.md`의 role/evidence 조건.
- C++ 경로: `framework/languages/cpp/CMakeLists.txt:1435-1440,1478-1485,1550-1556,1591-1598,1616-1621,1665-1670`, C++ E2E client directories, `framework/languages/cpp/e2e/run_e2e_all.sh:158-220`, `.github/workflows/build.yml`, `.github/workflows/framework-dotnet.yml`, `.github/workflows/framework-node.yml`, `scripts/v11/run-framework-runtime-regression.mjs:276-320`.
- 확인한 동작과 기대 동작: static include scan에서 C++ client source가 Framework `src/runtime` 또는 Core C ABI를 직접 include하는 사례는 확인하지 못했다. 그러나 CMake는 여러 client target에 `${ZLINK_FRAMEWORK_CPP_DIR}/framework/src`를 private include path로 제공한다. 이 설정은 client가 private header를 사용할 수 있는 compile boundary를 열어 둔다. aggregate는 child runner exit code와 `PASS` text를 중심으로 집계하고 exact role evidence manifest를 공통 inventory와 대조하지 않는다. `.github/workflows/`에는 C++ Framework workflow가 없고, v11 regression plan은 C++ E2E를 `BUILD_E2E=OFF`로 configure한다. 실제 bounded aggregate는 `build` cache에서 RegistrationCodec target build 중 timeout으로 종료됐다.
- 판정 근거: public include를 실제로 사용했다는 source scan은 좁은 충족 evidence다. private include permission, E2E의 stale build cache, undefined old role server, current CTest failure, historical feature-map와 no-C++-workflow를 함께 보면 process E2E와 CI가 common rule을 enforce한다고 판정할 수 없다.
- 수정 목록: client target에서 `framework/src` include path를 제거하고 public installed package만으로 compile한다. Framework call은 role server endpoint 내부에만 두며 client는 HTTP/stream connector로 result와 role evidence를 읽는다. aggregate에 exact ID/evidence manifest parser, partial/N/A policy, child cleanup와 bounded timeout을 추가한다. C++ Framework CI job을 추가하거나 existing gate가 C++ source/common spec path를 실제로 trigger하도록 연결한다.
- 필요한 회귀 검증: `CPP-REG-007` inventory/aggregate, `CPP-REG-008` clean consumer, `CPP-REG-009` private include/Core boundary and CI path filter, process role/evidence gate.
- Core·bindings·package 선행 조건: package mode에서 client와 role server 모두 pinned local `zlink_cpp`/Core package를 사용한다. Framework internal ABI와 Core service C ABI는 include/link target에 나타나지 않아야 한다. bindings source reference와 stale cache를 금지한다.
- 완료 evidence: client-only compile fails if an internal header is included; all role server target process paths compile from installed public headers; each scenario has client result plus role evidence, aggregate counts exact IDs, CI runs the C++ job on C++/common-spec/package changes and records full logs.

## 7. Core·bindings·package 선행 조건

| 영역 | 현재 사실 | 완료 전 조건 |
|---|---|---|
| Core | Framework CMake는 `zlink::cpp`를 link하고 Framework source에는 `raw_mesh_node_owner`, service wire와 stateful runtime 같은 private implementation이 있다. public E2E client에서 Core C ABI 직접 사용은 정적 scan에서 확인하지 못했다. | Core service 의미를 Framework public API로 우회하지 않는다. Core `11.0.0` candidate runtime과 binding `11.1.0` package의 hash, install path, ABI를 기록하고, Framework production call path가 public binding operation에만 의존하는지 negative scan과 clean consumer로 확인한다. |
| C++ binding | `.artifacts/wsl/install/zlink-cpp/11.1.0`이 존재하고 source CMake pin은 11.1.0이다. 기존 Debug cache는 11.0.2다. | `scripts/local-package/README.ko.md` 정책대로 binding package를 새로 만든 뒤 Framework를 exact version으로 configure한다. old cache/runtime을 실행 evidence로 사용하지 않는다. |
| HTTP client | C++ local package policy는 별도 HTTP package를 제외한다고 설명하지만 CMake는 `zlink_http_client_cpp` component/export를 만든다. | HTTP client ownership을 common spec, C++ exact interface와 package policy 중 하나로 명시적으로 맞춘다. 별도 package를 유지한다면 policy와 verifier를 갱신하는 contract review가 선행되어야 한다. |
| Package verifier | `verify_packaged_contract.sh`는 `spot_handle.hpp`를 required로 하고 `spot_ref_t`를 forbidden으로 한다. exact Spot spec는 `spot_ref_t`를 요구하고 public list/resolver/handle을 제공하지 않는다고 한다. | verifier manifest, installed header set, CMake target export와 exact interface를 같은 contract source로 재정렬한다. verifier를 통과했다고 public semantic compliance로 해석하지 않는다. |
| CI와 다른 언어 | C++ Framework 전용 GitHub workflow는 확인하지 못했다. .NET/Node workflow의 path filter는 각 언어에 한정된다. | 다른 언어 구현을 C++ API 출처로 사용하지 않는다. common spec 변경, C++ source, C++ package script와 E2E 변경을 C++ gate가 실제로 실행하도록 CI ownership을 정한다. |

선행 조건을 해결하는 과정에서도 Core 전용 C ABI, Framework private target, test-only adapter, raw frame parser를 C++ public contract의 대체 수단으로 추가하지 않는다.

## 8. 작업 순서

1. `CPP-IMP-003`, `CPP-IMP-004`, `CPP-IMP-002`의 exact C++ declaration과 error/worker ownership을 contract governance에서 확정한다. Config 8의 `TD-*` mapping과 RC-A1/A2의 C++ exception도 이 단계에서 고정한다.
2. `CPP-IMP-001`의 current `buffer.data()` 변경을 pinned Boost dependency와 clean Release에서 재검증하고, HTTP integration timeout과 role-server process evidence를 닫는다.
3. actor/spot/worker/error declaration에 맞춰 production runtime의 task, ownership, terminal mapping을 정리한다. `CPP-IMP-005`의 admission, HWM, stream, actor join, relocation과 concurrent shutdown을 deterministic unit/integration gate로 닫는다.
4. SpotService, AutomaticTurnDispatch와 SpotActorTransfer role server를 current public header로 compile한다. `spot_handle_t`, `spot_handle_resolver_t`와 retired host surface를 public/API 우회로 되살리지 않는다.
5. `CPP-E2E-IMP-001~009` 순서로 common ID inventory, selector, client dispatch, role server evidence와 aggregate policy를 연결한다. partial/N/A/source-only/historical status는 full PASS에서 제외한다.
6. `CPP-IMP-006`의 package ownership과 verifier를 exact contract에 맞추고 clean consumer, install tree, target export, symbol/ABI matrix를 실행한다.
7. C++ Framework CI가 build, contract, unit, package, aggregate process E2E와 common-spec/path change를 실제로 실행하도록 gate를 연결한다.
8. S0 Framework gate가 닫힌 뒤 `cpp-framework-sample-spec-gap-ledger.ko.md`의 G2~G8을 실행한다. sample contract, runner, CMake/package provenance와 6개 process evidence를 이 ledger의 선행 결과와 연결한다.
9. 두 ledger의 모든 gap에 current source line, current test output, current process log와 package hash를 연결하고, 문서 원칙에 따라 결과·조건·다음 작업을 다시 검토한다. sample ledger가 닫히기 전에는 이 문서의 최종 완료를 표시하지 않는다.

## 9. 기존 회귀 test의 유지·변경·추가 목록

### 9.1 기존 test와 script의 유지/변경

#### CPP-TEST-001 — packaged contract verifier의 manifest 기준이 현재 exact spec과 다름

- 상태: audit/test gap.
- 근거: `framework/languages/cpp/scripts/verify_packaged_contract.sh:31-76`, `framework/doc/framework/common/spec/server/languages/cpp/interfaces/04-spots.ko.md:855-899`, `framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp:283-310`.
- C++ 경로: verifier의 required `include/zlink/framework/contracts/locations/spot_handle.hpp`, forbidden `spot_ref_t`와 current install source.
- 확인한 동작과 기대 동작: current verifier는 temporary install 뒤 `spot_handle.hpp` 누락으로 실패했다. exact spec는 `spot_ref_t`를 public exact ref로 사용하고 public handle/resolver/list를 제공하지 않는다고 한다. verifier는 current contract와 반대 기준을 사용한다.
- 판정 근거: verifier failure를 C++ implementation failure로만 해석할 수 없다. 먼저 verifier ownership과 exact header list를 정리해야 한다.
- 수정 목록: contract review 뒤 required/forbidden manifest, clean consumer source, target names, HTTP ownership을 한 번에 맞춘다. 현재 작업 범위에서는 script를 수정하지 않고 gap만 기록한다.
- 필요한 회귀 검증: installed header positive/negative compile, `find_package` clean consumer, target export와 no-source-tree check.
- Core·bindings·package 선행 조건: exact `spot_ref_t` package header와 pinned `zlink_cpp`/Core package를 사용한다. old handle source를 package에 복사하지 않는다.
- 완료 evidence: verifier가 current exact manifest와 clean consumer compile/run을 모두 통과하고, package header에 forbidden old surface가 없다는 결과를 남긴다.

#### CPP-TEST-002 — textual target gate가 runtime/E2E completion을 증명하지 못함

- 상태: audit/test gap.
- 근거: `framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_target_contract.cpp:3-8,24-68`, `framework/languages/cpp/CMakeLists.txt:1021-1031`, `scripts/v11/run-framework-runtime-regression.mjs:292-320`.
- C++ 경로: `test_cpp_framework_target_contract`, `test_cpp_framework_layout_contract`, `test_cpp_framework_contract_headers`, CTest inventory.
- 확인한 동작과 기대 동작: target contract test는 public header와 E2E wiring text를 filesystem scan하고 current CTest에서 PASS했다. `test_cpp_framework_install_consumer`와 `test_cpp_http_client`도 PASS했지만, bounded 전체 CTest는 49개 중 8개가 실패했다. common 완료는 built declaration, actual runtime, package consumer와 process E2E evidence까지 요구하며, aggregate probe는 RegistrationCodec build timeout으로 종료됐다.
- 판정 근거: static gate PASS와 production/runtime/E2E PASS 사이에 증명 단계가 없다. 좁은 target gate를 전체 compliance verdict로 사용하면 false positive가 된다.
- 수정 목록: textual scan은 early diagnostic으로 유지하되 completion gate에서 분리한다. exact header compile, installed package consumer, common ID/evidence manifest, process role server assertion과 aggregate result를 별도 required gate로 추가한다. CTest timeout/abort/segfault를 별도 failure record로 남기고, test가 historical ledger의 문자열 상태에 의존하지 않게 한다.
- 필요한 회귀 검증: `CPP-REG-001`, `CPP-REG-007`, `CPP-REG-008`, `CPP-REG-009`와 current `ctest` result classification.
- Core·bindings·package 선행 조건: gate는 source tree private header와 stale cache를 사용할 수 없다. pinned local package와 clean install prefix를 input으로 받는다.
- 완료 evidence: static gate, built public compile, runtime unit, clean package consumer, exact 14-config process aggregate가 각각 PASS하고 한 단계의 PASS가 다른 단계를 대신하지 않는다.

### 9.2 추가 또는 변경해야 할 regression ID

아래 9개 ID는 이 ledger에 추가한 regression 계획이다. 구현 code와 test code에는 아직 추가하지 않았다.

| ID | 대상 | 현재 상태 | 필요한 assertion과 완료 evidence |
|---|---|---|---|
| `CPP-REG-001` | exact public header/ABI | 추가 예정 | worker/actor/spot/error의 namespace, method, parameter, `const`/reference/value, move/copy, destructor, `noexcept`, enum numeric value와 installed consumer compile을 positive/negative probe로 확인한다. |
| `CPP-REG-002` | submit admission | 추가 예정 | first attempt, HWM/backpressure, signal별 retry 1회, owner reservation, deadline, late signal, owner epoch, shutdown cleanup, callback count를 unit와 process evidence로 확인한다. |
| `CPP-REG-003` | worker execution | 추가 예정 | CPU/IO thread boundary, `std::stop_token`, queue full, timeout, caller cancellation, host shutdown과 completion exactly-once를 확인한다. |
| `CPP-REG-004` | lifecycle/queue/relocation | 추가 예정 | actor join/spot create `submit/yield`, authority commit, owner/generation, callback order, backlog/replay와 stale route cleanup을 확인한다. |
| `CPP-REG-005` | error mapping | 추가 예정 | operation family별 `kind`, diagnostic `code`, retriable flag, terminal reason, duplicate/late completion과 shutdown mapping을 확인한다. |
| `CPP-REG-006` | HTTP production path | 추가 예정 | supported Boost buffer API, typed async submit, blocking boundary, deadline/status mapping, HTTPS conditional path와 role server startup/cleanup을 확인한다. |
| `CPP-REG-007` | E2E inventory/aggregate | 추가 예정 | common 374 ID와 C++ selector/dispatch set difference, Config 12/14 existence, partial/N/A/diagnostic_only exclusion, exact scenario PASS line을 확인한다. |
| `CPP-REG-008` | package/install/ABI | 추가 예정 | Framework/StreamConnector/Dependency components, target export, clean consumer, no source-tree include, `zlink_cpp`/Core version/hash, static/shared and Debug/Release/compiler matrix를 확인한다. |
| `CPP-REG-009` | responsibility boundary/CI | 추가 예정 | client internal-header/Core ABI negative scan, role server Framework-call location, client-visible plus role evidence, C++ CI path filter와 bounded aggregate execution을 확인한다. |

## 10. 완료 판정 checklist

현재 checklist는 모두 미완료다. `충족(정적)`은 완료 checkbox를 대체하지 않는다.

- [ ] `CPP-IMP-001`: clean Release production build와 HTTP role server compile이 통과했다.
- [ ] `CPP-IMP-002`: worker exact interface, stop token, options, scheduling, cancellation과 timeout semantics가 일치한다.
- [ ] `CPP-IMP-003`: actor/spot/session public header가 exact interface의 signature와 ownership을 일치시킨다.
- [ ] `CPP-IMP-004`: common error enum numeric ABI와 operation별 mapping이 installed package와 runtime에서 일치한다.
- [ ] `CPP-IMP-005`: admission, HWM, stream, routing, actor join, relocation, retry, replay, cleanup과 concurrent shutdown이 current process evidence로 증명된다.
- [ ] `CPP-IMP-006`: local package version, Core provenance, install tree, CMake target, symbol visibility와 ABI가 clean consumer에서 증명된다.
- [ ] `CPP-E2E-IMP-001~009`: common 14 config와 374 scenario ID의 selector, dispatch, role process, evidence와 aggregate policy가 일치한다.
- [ ] `CPP-TEST-001~002`: verifier와 textual gate가 exact contract와 actual runtime/E2E gate를 서로 모순 없이 검사하고, 현재 CTest의 8개 failure가 해소되었다.
- [ ] `CPP-REG-001~009`: 회귀 test가 구현되고 current working tree에서 통과했다.
- [ ] S0 완료 뒤 [`cpp-framework-sample-spec-gap-ledger.ko.md`](cpp-framework-sample-spec-gap-ledger.ko.md)의 `CPP-SAMPLE-IMP-*`, `CPP-SAMPLE-E2E-IMP-*`, `CPP-SAMPLE-TEST-*`, `CPP-SAMPLE-REG-*`가 current sample source와 process evidence로 완료됐다.
- [ ] sample ledger의 6개 C++ sample aggregate가 완료되었고, `ZoneWorld`는 C++ 계약 대상이 아니라는 범위 근거가 유지된다.
- [ ] sample ledger 완료 전에는 이 spec ledger와 전체 C++ audit을 `완료`로 표시하지 않는다.
- [ ] C++ E2E client는 public HTTP client/stream connector만 사용하고 Framework call은 role server에 있다.
- [ ] role server evidence와 client-visible result가 scenario별로 모두 확인된다. historical log, source type 존재, unit test만으로 PASS를 만들지 않는다.
- [ ] Core 전용 C ABI, Framework internal ABI, private header, raw frame, test-only adapter와 reflection 우회가 없다.
- [ ] C++ Framework CI가 C++ source, common spec, package policy와 E2E path change에서 required gates를 실행한다.
- [ ] final re-review에서 현재 `git status`, build/test/E2E/package log와 이 문서의 gap 상태가 같은 working tree를 가리킨다.

### 현재 미해결 blocker 요약

- C++ Framework production build: current `zlink_framework`, `zlink_http_client`, `zlink_stream_connector` build는 통과했다. 이전 `buffer_cast` failure는 current working tree 변경 전 evidence로 남기고, HTTP integration과 role process evidence는 미완료다.
- CTest: bounded 49개 실행에서 8개 failure가 발생했다(`m6a`/app-host/http/store resolver timeout, `m6b`/stream connector abort, mesh-node vertical failure, stream framework segfault).
- Public contract: worker, actor/spot declaration과 error enum mismatch.
- Runtime semantics: submit/admission, worker cancellation, ownership과 lifecycle process evidence 부족.
- E2E: Config 12/14 부재, Config 1~11/13 scenario 누락 또는 partial aggregate, Config 10 migration host, role evidence와 private include boundary. Current 12-config aggregate probe도 RegistrationCodec target build timeout으로 종료됐다.
- Package: `zlink_cpp`/Core provenance drift, HTTP client ownership 문서와 CMake export 불일치, stale packaged verifier manifest.
- CI: C++ Framework 전용 GitHub workflow와 common 14-config process gate 부재.
- Sample 후속: `cpp-framework-sample-spec-gap-ledger.ko.md`는 작성되었지만 아직 S1 process gate가 완료되지 않았다. C++ sample aggregate는 현재 `TicTacToe`의 `invalid application payload version` abort에서 중단되었고, PowerShell aggregate는 2개 sample만 호출한다. 6개 sample의 exact contract, package provenance와 role-server evidence가 남아 있다.
- 다른 언어 구현은 위 gap을 해결하는 public API 근거로 사용하지 않았다.
