# C++ Framework spec gap audit ledger

## 문서 상태

- 기준 시점: 2026-08-03 07:17 KST. canonical `framework/languages/cpp/build`를 기준으로 build, focused CTest, full CTest, package consumer, sample serial smoke와 bounded common E2E를 다시 확인했다.
- 판정: 미완료. 현재 C++ Framework는 common Framework spec과 common E2E spec을 전체 충족한다고 판정할 수 없다.
- 변경 범위: 이 문서를 C++ Framework spec과 sample을 함께 관리하는 유일한 ledger로 유지한다. 이번 갱신에서는 이 문서와 실제 작업 진행 log만 수정한다. 기존의 별도 sample ledger는 중복 문서가 되므로 통합 후 제거한다. 구현 source, public header, test, E2E runner, 정식 spec과 기존 사용자 변경은 수정하지 않는다.
- 이 문서는 public contract가 아니다. 아래의 `contract 선행` 항목은 구현 전에 exact interface 또는 package ownership을 먼저 확정해야 한다.

## 공통 실행 규칙 — 네 ledger 동시 진행

이 문서의 C++ 작업은 `.NET`, Java/Kotlin, Node.js 작업과 동시에 진행한다. 현재 시스템 시각
`2026-08-03 07:17 KST (+09:00)` 기준 마감은 `2026-08-03 10:00 KST (+09:00)`이다. 마감 시점에
완료하지 못한 항목은 완료로 표시하지 않고, 현재 조건과 blocker를 기록한 뒤 다음 결정을 기다린다.

이 절에서 고정하는 것은 작업 간 경계, 하위 layer bug 처리, CPU·마감·log 위치처럼 지켜야 하는
조건이다. 구체적인 test 순서와 범위, review model·reasoning level, 도움 요청 시점, commit 단위와
push 시점은 진행 중 evidence와 dependency를 보고 workstream owner가 정한다. 처음 정한 방식이
맞지 않으면 작업을 멈추기보다 이유와 새 선택을 `log/`에 남기고 조정한다. 한 항목의 결정이
끝나지 않아도 독립적으로 진행할 수 있는 조사·재현·test 준비는 계속한다.

### 작업 경계와 CPU 제한

- 네 작업은 서로 독립된 workstream으로 진행한다. 이 C++ workstream은 이 ledger와 C++ Framework의
  source, test, E2E, package와 그에 대응하는 진행 기록만 수정한다.
- 다른 workstream의 source, test, E2E, 문서, package version, lockfile 또는 진행 기록을 수정하거나
  정리하지 않는다. 비교를 위한 read-only 확인은 가능하지만, 다른 작업의 내용을 이 candidate와
  commit에 섞지 않는다.
- Core 또는 bindings처럼 여러 workstream이 사용하는 공통 파일을 수정해야 하면 먼저 해당 하위
  layer의 owner와 변경 manifest를 정한다. owner가 하나의 candidate로 수정·검증하고, 다른
  workstream은 그 의존성만 자기 log에 기록한다. 같은 Core·bindings 변경을 각 작업이 중복 수정하지
  않는다.
- 시스템의 20 CPU를 네 작업이 나누어 사용하므로 작업 하나가 build, test, review agent와 보조
  process를 합쳐 점유하는 CPU는 최대 5개를 넘지 않는다. 병렬 실행 옵션도 작업당 `5` 이하로
  제한하고, 추가 worker를 생성해 이 제한을 우회하지 않는다.

### Core·bindings bug 처리와 local package 배포

Core 또는 bindings 수준의 bug를 발견하면 Framework나 sample에서 회피하지 않는다. 호출부의 raw
frame 해석, private/internal API 호출, test-only adapter, 상태 복제, 별도 retry 경로를 추가해
하위 layer의 실패를 숨기는 방식은 완료로 인정하지 않는다.

1. 최소 재현으로 원인을 소유한 layer를 확정한다. 원인과 재현 조건이 더 분명해지는 방식이라면
   test와 fix candidate를 함께 준비할 수 있지만, 최종 candidate에는 수정 전 동작을 잡는
   regression test가 남아 있어야 한다.
2. Core bug는 Core test에, bindings bug는 해당 bindings test에 regression test를 추가하고, 그
   test가 확인하는 책임을 해당 layer의 수정으로 해결한다.
3. test·fix·영향받는 gate의 실행 순서는 이슈의 재현 조건과 의존성에 맞춰 정한다. 순서를 바꾸면
   그 이유와 아직 닫히지 않은 조건을 `log/`에 남긴다.
4. 수정한 Core 또는 bindings를 local package로 배포할 때는 반드시 package version을 올린다.
   `scripts/local-package/README.ko.md`의 절차에 따라 새 runtime·archive를 만들고, 필요한 Core
   library 동기화와 stale cache 제거를 끝낸다.
5. Framework는 새 version의 local package를 실제로 resolve하는지 clean consumer, package contract와
   관련 process E2E로 확인한다. source tree나 이전 version cache를 사용한 결과는 새 package의
   증거로 인정하지 않는다.

원인 layer, regression test, fix, 올린 version, package 경로·hash, consumer 결과는 이 문서 본문에
진행 log로 나열하지 않고 아래 `log/` 규칙에 기록한다. 공통 Core·bindings candidate의 commit은
owner workstream에서만 만들며, 이 C++ workstream의 commit에는 다른 언어 작업의 변경을 포함하지
않는다.

### Review agent 선택

Review agent의 model과 reasoning level은 언어별로 고정하지 않고, review를 요청하는 시점에
[OpenAI 공식 model guidance](https://developers.openai.com/api/docs/guides/latest-model)를 확인해
review 위험도에 따라 결정한다. 이 문서 갱신 시점의 guide는 `gpt-5.6-sol`을 frontier capability,
`gpt-5.6-terra`를 intelligence와 cost의 균형, `gpt-5.6-luna`를 효율적인 high-volume 작업의
예시로 설명한다. 이 model ID는 영구 pin이 아니며, guide가 바뀌면 새 선택을 따른다.

- heading·link·manifest 같은 기계 검사는 guide가 정한 balanced 또는 efficient model을 출발점으로
  삼는다. 실제 범위가 달라지면 더 적절한 model과 level을 선택할 수 있다.
- public contract, ABI, runtime semantics, lifecycle, concurrency, package와 process E2E를 판단하는
  review는 guide의 frontier model을 우선 검토한다. `high`, `xhigh`, `max` 중 어느 수준이 필요한지는
  candidate의 위험도와 실제 evidence에 따라 진행 중 정한다.
- 전체 closure와 어려운 cross-layer race의 최종 audit은 충분한 capability의 독립 reviewer가
  수행해야 한다. 특정 model ID나 level을 형식적으로 채우는 것보다 review 범위와 결과의 충분성을
  우선하며, 선택을 조정한 근거를 `log/`에 남긴다.
- 실제 model ID, guide 확인 URL와 날짜, 선택 근거, reasoning level과 결과는 이 문서의 `log/`에
  기록한다. 필요한 reviewer를 바로 사용할 수 없으면 해당 review만 pending으로 두고 독립적으로
  진행할 수 있는 작업을 계속한다. review가 필요한 완료 판정은 reviewer 결과 전까지 완료로
  표시하지 않는다.

### 미해결 이슈의 독립 도움 요청

작업 중 같은 이슈가 재현된 뒤에도 원인이나 수정 방향이 닫히지 않으면, 최신 guide가 정한 높은
수준의 Codex model 또는 `Claude Fable`에 독립 도움을 요청해 다음 선택을 정한다. 어느 시점에
어떤 경로로 요청할지는 재현 가능성, 영향 범위와 진행 dependency를 보고 owner가 정한다. 요청에는
판단에 필요한 candidate manifest, 재현 명령과 결과, 현재 가설, 책임 경계와 남은 제약을 포함한다.
도움은 설계·진단 입력이며 해결 판정이 아니다. 실제 owner layer의 regression test, fix, 새 package와
관련 gate가 닫힌 뒤에만 이슈를 해결로 표시한다. 도움 요청과 응답, 선택한 조치, 미해결 조건은
이 문서의 `log/`에만 기록한다. 지원 model을 사용하더라도 workstream 경계와 작업당 5 CPU 제한을
유지한다.

본문에 남은 model ID, level, round 순서와 표는 과거 evidence 또는 시작 profile이다. 별도 계약으로
고정하지 않은 세부 선택은 진행 중 candidate의 위험도와 evidence에 맞춰 조정할 수 있다. 이 공통
규칙과 review 요청 시점의 공식 guide를 기준으로 선택하고, 변경 이유만 `log/`에 남긴다.

### Commit·push와 진행 기록

- 하나의 bounded card 또는 하위 layer 수정의 commit 경계는 책임 범위, rollback 가능성과 review
  흐름을 보고 owner가 정한다. 필요한 검증을 마친 단위는 path-limited commit으로 남기고 적절한
  branch로 push한 뒤 다음 card로 진행한다. 중간 commit이 필요하면 candidate로 표시하고 최종
  완료와 구분한다.
- 최종·고위험 변경은 가능하면 push 전에 독립 review를 거치지만, review와 무관한 조사·재현·준비
  작업까지 기다리게 하지는 않는다.
- `git add -A`나 unrelated 변경을 포함한 commit은 금지한다. commit과 push 전후의 SHA, branch,
  package version과 gate 결과는 `log/`에 기록한다.
- 진행 log는 이 ledger 본문에 절대 남기지 않는다. 명령, exit code, review finding, commit·push,
  package 배포와 blocker는 이 문서가 있는 디렉토리의 `log/` 안에만 기록하고, 본문에는 현재 판정과
  필요한 log 링크만 둔다.

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
- S0 Framework spec gate를 먼저 닫고, 이 문서의 11장 이후 S1 sample phase에서 sample contract, runner와 6개 process evidence를 완료한다. S0 또는 S1이 미완료이면 전체 C++ audit을 완료로 표시하지 않는다.

현재 runtime 작업으로 HTTP buffer access, Boost include order, mesh admission, submit retry, worker scheduling·shutdown, worker options, owner-managed deadline, blocking request offload, ClientServer hello readiness와 pinned TicTacToe process 경로를 수정했다. M6A liveness regression의 logical-clock 사용도 정렬했고, HWM exact-boundary와 application mailbox의 한 dispatch turn 한 건 제한, STREAM reply-token terminal-consumption 회귀를 추가했다. 2026-08-03 initial canonical full CTest는 sample process를 포함해 55개 중 50개 통과였고, 이후 runtime bounded-shutdown 수정과 unit regression을 반영한 non-sample CTest는 49/49로 통과했다. exact public contract, common E2E 전체 분모, package ownership/ABI, CI와 S1 sample aggregate는 아직 닫히지 않았으므로 이 ledger 전체를 완료로 판정하지 않는다.

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
- C++ sample 후속 범위: 이 문서의 11~20장, `framework/languages/cpp/samples/`, C++ sample parity/layout/target test와 sample runner.
- historical reference: `framework/doc/plan/log/framework-public-contract-gap-implementation/cpp-g0-contract-ledger.ko.md`와 같은 디렉토리의 C++ 문서. historical log와 snapshot은 현재 완료 evidence로 사용하지 않았다.
- 진행 기록 정책: 실제 ledger 작업 중 각 조사·검증·판정 단계가 끝난 직후 이 디렉토리의 날짜별 `log/`에 기록한다. `2026-08-02-progress.log`는 이전 snapshot이고, 이번 재검증은 [`log/2026-08-03-progress.log`](log/2026-08-03-progress.log)에 기록했다. 사후에 command 결과를 모아 만든 log는 완료 evidence로 사용하지 않는다.

### 2.3 판정 용어

- `충족(정적)`: 현재 source 또는 선언에서 좁은 조건을 확인했지만 build/runtime/process evidence까지 완료되었다는 뜻은 아니다.
- `gap`: common spec 또는 exact interface와 현재 실행 경로가 다르거나, 완료를 직접 증명할 gate가 없다.
- `contract 선행`: 구현 전에 public contract, exact C++ 표현 또는 package ownership을 먼저 확정해야 한다.
- `과거 evidence`: feature-map이나 log가 기록한 예전 실행 결과다. 현재 working tree의 증거로 승격하지 않는다.

### 2.4 Sample 후속 gate

이 문서의 1~10장은 Framework production·public contract·common E2E의 S0 gate를 소유하고,
11장 이후는 C++ sample의 S1 gate를 소유한다. S0의 checklist가 완료된 뒤에만 같은 문서의
S1 작업 순서 G2부터 시작한다. S1은 공통 sample message와 실제 role server call path를 검증하지만
public Framework API를 새로 정의하지 않는다.

S0 완료 후 이 문서의 11장 이후에서 S1으로 다음을 수행한다.

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

현재 `git status --short`에는 이 문서와 별도의 사용자 변경이 함께 있다. 이번 runtime phase에서 수정한 C++ 범위에는 Framework production source, worker/submit regression test, CMake test scheduling, sample runner와 sample readiness path가 포함된다. 다른 언어의 사용자 변경은 범위 밖으로 두었다.

주요 현재 변경은 `framework/languages/cpp/framework/src/runtime/`, `framework/languages/cpp/framework/include/zlink/framework/contracts/workers/`, C++ regression tests, `framework/languages/cpp/CMakeLists.txt`, sample runner와 이 ledger/log에 있다. 기존 dirty worktree의 다른 변경은 되돌리거나 덮어쓰지 않았다.

### 3.2 2026-08-02 `build-v11-tests` snapshot

이 subsection은 2026-08-02에 별도 `build-v11-tests`에서 얻은 historical runtime snapshot이다. 현재
checkout에는 그 build directory가 없으므로, 현재 판정은 아래 3.4의 canonical `build` 결과를 우선한다.

| 검증 | 실행 결과 | 해석 |
|---|---|---|
| `cmake --build framework/languages/cpp/build-v11-tests --parallel 1` | 성공 | Release pinned build에서 Framework, HTTP client, stream connector와 regression targets가 다시 compile/link됐다. |
| `ctest --test-dir framework/languages/cpp/build-v11-tests -R 'test_cpp_framework_execution|test_cpp_framework_contract_headers|test_cpp_framework_submit_admission|test_cpp_framework_store_location_resolvers' --output-on-failure --timeout 60` | 4/4 passed | Worker options/sealing, owner-managed deadline, stop/timeout/IO queue, submit retry와 ClientServer location readiness 회귀를 확인했다. |
| worker/admission executable 반복 | 100회 모두 passed | `test_cpp_framework_execution`과 `test_cpp_framework_submit_admission`을 각각 반복해 stop token, timeout, queue boundary와 late-success terminal mapping을 확인했다. |
| `test_cpp_framework_m6a_runtime` executable 반복 | 100회 모두 passed | synthetic liveness tick 이후에도 logical probe interval이 유지되는 M6A node send/liveness 경로를 확인했다. |
| M6A/STREAM runtime 회귀 executable 반복 | 변경 후 20회 모두 passed | M6A의 HWM exact-boundary pause/resume와 STREAM의 transport failure 뒤 reply token 재사용 금지를 확인했다. 첫 transport failure는 한 번만 시도되고 후속 reply는 `request_protocol_error`로 거부된다. |
| M6B HWM mailbox runtime 회귀 executable 반복 | 최종 20회 모두 passed | 두 application record를 mailbox에 준비하고 첫 `dispatch_ready()` 뒤 잔여 record가 1개인지 확인했다. user-spot replay retention test는 request deadline 기준으로 만료를 기다리도록 정렬했다. |
| 변경 후 full CTest | 최종 `-j2`, 49/49 passed | HWM mailbox dispatch regression과 replay-expiry test 정렬을 포함한 현재 tree에서 전체 49개 target을 다시 실행했다. |
| `ctest --test-dir framework/languages/cpp/build-v11-tests --quiet --timeout 60 -j2` | post-fix 5회 연속 passed, 49/49 each | 이전 반복에서 location resolver 단독 failure와 shared vcpkg mutation failure를 관찰했다. tooling test의 `RESOURCE_LOCK`과 worker lifetime 수정 후 5회 연속 결과를 current regression evidence로 기록한다. |
| `framework/languages/cpp/scripts/verify_packaged_contract.sh framework/languages/cpp/build-v11-tests` | passed | installed library hash, 110 headers와 clean packaged consumer를 확인했다. HTTP package ownership 차이는 별도 gap이다. |
| `ZLINK_CPP_AUTO_CONNECT_TRACE=1 timeout 300s framework/languages/cpp/samples/TicTacToe/run_sample.sh` | exit 0 | pinned `build-v11-samples`에서 preflight 3/3, `PASS TicTacToe.Cpp`, full client/server self-check를 확인했다. six-sample aggregate는 별도 미완료다. |
| common C++ aggregate E2E | 미완료 | 14-config/374 scenario 전체 selector와 role evidence가 아직 닫히지 않았다. |

2026-08-02 snapshot의 실행 결과와 실패 ID는 [`log/2026-08-02-progress.log`](log/2026-08-02-progress.log)에
반영되어 있다. 이 snapshot은 exact contract나 common E2E 전체 완료를 의미하지 않는다.

### 3.3 package와 cache 관찰

- source CMake는 `ZLINK_FRAMEWORK_CPP_ZLINK_CPP_VERSION=11.1.0`, `ZLINK_FRAMEWORK_CPP_ZLINK_CORE_VERSION=11.1.0`을 기본값으로 사용하고 `find_package(zlink_cpp 11.1.0 EXACT CONFIG REQUIRED)`를 호출한다(`framework/languages/cpp/CMakeLists.txt:41-89`).
- 2026-08-02 historical `build-v11-tests` cache는 Release, `11.1.0/11.0.0`이었지만 현재 checkout에는 해당 directory가 없다.
- 현재 canonical `framework/languages/cpp/build/CMakeCache.txt`는 Release, `11.1.0/11.1.0`이며 `CMAKE_PREFIX_PATH`가 `.artifacts/wsl/install/zlink-cpp/11.1.0`과 `.artifacts/wsl/install/zlink-core/11.1.0`을 가리킨다. 이 cache를 2026-08-03 live evidence에 사용했다.
- `framework/languages/cpp/e2e/RegistrationCodec/run_e2e.sh:5-6`은 E2E build directory를 `framework/languages/cpp/build`로 고정한다. 따라서 aggregate E2E는 `build-v11-tests`의 pinned package evidence와 다른 cache를 사용할 수 있다. 이 경로는 common aggregate closure 전에 별도로 고정해야 한다.
- C++ binding package config는 `find_dependency(zlink 11 CONFIG)`로 Core major만 요구한다. 2026-08-02 historical sample snapshot은 Core `11.0.0`을 사용했지만, current canonical build의 Core `11.1.0` exact provenance와 ABI를 별도로 확인해야 한다.
- `framework/languages/cpp/samples/sample-build-common.sh`와 각 shell runner가 사용하는 `build-v11-samples`는 historical Release `zlink_cpp=11.1.0`, Core `11.0.0` cache다. current live evidence는 Release `framework/languages/cpp/build`의 `11.1.0/11.1.0` cache를 사용했으며, PowerShell과 aggregate runner의 provenance 정렬은 남아 있다.
- `framework/languages/cpp/scripts/verify_packaged_contract.sh`는 current installed header 기준으로 required/forbidden manifest와 clean consumer를 통과한다. 다만 `scripts/local-package/README.ko.md:204-219`는 C++ HTTP client를 별도 local package에서 제외한다고 설명하고, CMake는 `zlink_http_client`를 별도 `HttpClient` component/export로 설치한다(`framework/languages/cpp/CMakeLists.txt:627-630,665-668,730-733`). 이 ownership 차이는 package gap으로 남긴다.

### 3.4 2026-08-03 canonical build live recheck

아래 결과가 현재 checkout의 우선 evidence다. focused unit·contract와 package 결과는 통과했지만,
sample process와 common aggregate가 닫히지 않아 전체 판정은 `미완료`다. 상세 command와 exit code는
[`log/2026-08-03-progress.log`](log/2026-08-03-progress.log)에 기록했다.

| 검증 | 실행 결과 | 현재 판정 |
|---|---|---|
| `cmake --build framework/languages/cpp/build --parallel 1` | exit 0 | canonical Release build의 Framework, HTTP client, stream connector, E2E와 sample target이 compile/link됐다. |
| focused CTest: contract/runtime/channel/stream/HTTP/connector 12개 | 12/12 passed, exit 0 | public header·target·sample parity, worker/admission, channel/stream, HTTP와 connector의 좁은 회귀 조건을 확인했다. |
| `ctest --test-dir framework/languages/cpp/build --output-on-failure --timeout 180 -j2` | 50/55 passed, exit 8 | initial parallel run이다. non-sample gate와 TicTacToe는 통과했고, DeliveryDispatch/SupportChat은 FetchContent/ExternalProject shared build race, Bingo/GameQuest/ShoppingMall은 sample process failure로 분류됐다. |
| `ctest --test-dir framework/languages/cpp/build -E '^sample_smoke_' --output-on-failure --timeout 120 -j1` | 49/49 passed, exit 0 | bounded shutdown regression을 포함해 actor gateway, worker execution, channel/messaging, stream identity, HTTP, connector와 install consumer를 serial로 재검증했다. sample process 6개와 common aggregate는 포함하지 않았다. |
| bounded host shutdown regression: `test_cpp_framework_app_host` | 1/1 passed, exit 0 | 실제 `hosted_service_t::stop()`을 100ms 이상 block한 상태에서 `app_t::shutdown(100ms)`가 1초 이내 `force_stopped/deadline_exceeded`를 반환하고, service release 후 run thread가 종료되는지 확인했다. |
| 5개 실패 sample serial 재실행 | 3/5 passed, exit 8 | DeliveryDispatch와 SupportChat은 serial에서 통과해 병렬 build race로 분리했다. Bingo는 actor destroy cleanup timeout, GameQuest는 first hunt progress wait failure, ShoppingMall은 client/evidence 검증 failure가 재현됐다. |
| `framework/languages/cpp/scripts/verify_packaged_contract.sh framework/languages/cpp/build` | `PASS`, exit 0 | installed library 3개 hash, 110 headers, required/forbidden manifest와 clean consumer를 확인했다. HTTP ownership과 full ABI matrix는 여전히 gap이다. |
| `timeout -k 5s 120s ./run_e2e_all.sh --max-attempts=1 --scenario-timeout-seconds=20` | exit 124 | RegistrationCodec는 PASS, RegistryMessaging는 RM-A1~A3까지 진행한 뒤 bounded timeout으로 종료됐다. 12-config aggregate와 374 scenario evidence는 미완료다. |

현재 canonical evidence를 합치면 non-sample 49개 gate와 sample serial 3/6은 확인되지만, 이를 하나의
55개 serial full CTest PASS로 합산하지 않는다. Bingo, GameQuest, ShoppingMall의 process blocker와
common aggregate의 범위·cleanup·role evidence를 먼저 닫아야 한다.

## 4. 현재 충족 판정

다음은 전체 완료가 아니라 현재 확인 가능한 좁은 조건이다.

| 항목 | 현재 판정 | 근거와 제한 |
|---|---|---|
| Framework CMake target | 부분 충족 | `zlink_framework`와 `zlink::framework` alias, C++20, public include와 `zlink::cpp` link가 `CMakeLists.txt:158-230`에 있고 현재 세 production target build도 성공했다. 전체 CTest와 process E2E는 실패 또는 bounded timeout이다. |
| exact public header 위치 | 충족(정적) | `framework/include/zlink/framework/`가 설치 source tree에 있고 layout gate가 통과했다. exact method와 error enum은 아래 gap과 같이 다르다. |
| Core/Framework link boundary | 충족(정적) | Framework target은 `zlink::cpp`를 public dependency로 사용한다. public E2E client source에서 Core C ABI 또는 Framework runtime header 직접 호출은 확인하지 못했다. CMake가 client에 private include path를 제공하므로 clean consumer와 include-boundary gate가 필요하다. |
| async submit ownership skeleton | 부분 확인 | `async_submit_runtime_t`는 owner epoch, reservation, pending/ready queue와 shutdown completion을 구현한다(`async_submit_runtime.cpp:117-211,268-300`). callback count, payload lifetime, late signal, process recovery를 current process에서 확인하지 못했다. |
| client HTTP role 호출 | 충족(정적) | 여러 C++ client는 `zlink::http_client`로 role endpoint를 호출한다. 2026-08-03 `test_cpp_framework_http_integration`, `test_cpp_http_client`와 `test_cpp_framework_app_host`는 focused CTest에서 통과했지만 aggregate role evidence와 sample HTTP process closure는 남아 있다. |
| historical feature-map | 과거 evidence | 일부 문서는 예전 log를 `implemented`로 기록하지만 현재 aggregate 범위와 source tree가 common E2E 전체와 다르다. historical log만으로 충족 판정하지 않는다. |

## 5. `CPP-IMP-*` production implementation gap

### CPP-IMP-001 — HTTP buffer access와 HTTP runtime blocker는 해소되었지만 process 범위가 남음

- 상태: `runtime 부분 충족`; compile blocker와 current CTest HTTP runtime failure는 해소되었고 common role-process evidence는 남아 있다.
- common/exact 근거: `framework/doc/framework/common/spec/server/languages/cpp/60-http-hosting.ko.md`, `framework/doc/framework/common/spec/http-client/languages/cpp/cpp-http-client.ko.md:45-90`, `framework/languages/cpp/CMakeLists.txt:158-230`.
- C++ 경로: `framework/languages/cpp/framework/src/runtime/http/http_listener.cpp:85-145`, `framework/languages/cpp/CMakeLists.txt:195`.
- 확인한 동작과 기대 동작: 이전 source는 `asio::buffer_cast`를 사용해 build가 실패했지만, 현재 working tree는 `mutable_buffer::data()`와 `const_buffer::data()`를 사용한다(`http_listener.cpp:94-97,137-138`). 현재 `zlink_framework`, `zlink_http_client`, `zlink_stream_connector` build와 `test_cpp_http_client`는 통과했다. 기대 동작은 선택한 Boost tree의 supported buffer access와 synchronous buffer lifetime을 유지하면서 HTTP listener, typed client call과 role server process가 끝까지 동작하는 것이다.
- 판정 근거: 이전 `buffer_cast` compile failure는 current production build에서 재현되지 않는다. Framework와 HTTP client의 bundled Boost include order를 `BEFORE`로 고정한 뒤 `test_cpp_framework_http_integration`, `test_cpp_http_client`, `test_cpp_framework_app_host`와 2026-08-03 focused CTest가 통과했다. 다만 common aggregate가 전체 role evidence를 아직 완료하지 않았고 ShoppingMall HTTP process가 실패했으므로 public HTTP process closure로 판정하지 않는다.
- 수정 목록: current `buffer.data()`와 동일 Boost tree 선택을 유지한다. remaining work는 public HTTP client의 typed `submit<T>()`, deadline/status mapping, HTTPS 조건부 path, role server startup과 cleanup을 common E2E evidence로 연결하는 것이다.
- 필요한 회귀 검증: `CPP-REG-006`의 HTTP compile, typed submit, timeout, status/error mapping, HTTPS 조건부 test, role server startup과 shutdown.
- Core·bindings·package 선행 조건: Core와 `zlink_cpp` package를 바꾸지 않고 Framework의 Boost dependency만 고친다. package consumer가 source tree의 Boost header를 우연히 가져오지 않아야 한다.
- 완료 evidence: clean Release configure/build와 installed package consumer가 성공하고, HTTP integration과 aggregate role server process E2E가 bounded timeout 없이 통과한다. client-visible result, role evidence, buffer lifetime, terminal cleanup을 함께 확인하며, `buffer_cast` compile error가 없는 current log를 `log/`에 남긴다.

### CPP-IMP-002 — Worker public contract candidate와 runtime scheduling/cancellation은 연결됨

- 상태: `runtime 부분 충족`; `worker_options_t`와 owner-managed deadline은 candidate에 반영했지만 exact governance, caller cancellation 표현과 process evidence는 남아 있다.
- common/exact 근거: `framework/doc/framework/common/spec/server/languages/cpp/interfaces/01-common-runtime.ko.md:305-340`, `framework/doc/framework/common/e2e/config-8-execution-turn.ko.md`의 TD-C 계열.
- C++ 경로: `framework/languages/cpp/framework/include/zlink/framework/contracts/workers/worker.hpp:64-118`, `framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp:1013-1145`, `framework/languages/cpp/framework/src/runtime/dispatch/offload_executor.*`.
- 확인한 동작과 기대 동작: current `worker_call_t` executor는 `std::stop_token`을 받고, CPU body와 IO returned task 생성 모두 worker scheduler queue에서 수행된다(`worker.hpp`, `spot.hpp:1013-1150`, `offload_executor.*`). `worker_options_t`는 min/max thread, idle timeout과 queue length를 제공하고 `options.apply()` 뒤 변경을 거부한다. deadline은 detached thread가 아니라 `worker_control_t`가 join하는 owner-managed state로 취소·정리하며, host stop source는 runtime shutdown completion과 worker cancellation으로 연결되고 timeout과 queue full은 typed terminal result로 매핑된다. 기대 동작은 Spot/session 실행 문맥 밖에서 worker를 실행하고 host shutdown과 caller cancellation을 합친 token, queue/thread boundary와 exactly-once completion을 지키는 것이다.
- 판정 근거: `test_cpp_framework_contract_headers`와 `test_cpp_framework_execution`은 worker options surface/sealing, CPU/IO queue, stop token, timeout, host shutdown, queue full과 deadline owner destruction을 확인했고 focused·반복·full CTest가 통과했다. 다만 common exact interface governance의 caller cancellation 표현, installed exact compile probe와 process role evidence는 남아 있다.
- 수정 목록: exact interface governance에서 worker options와 cancellation ownership을 최종 확정한다. 현재 owner-managed timer/completion barrier를 유지하고 timeout·queue full·caller cancellation·host shutdown mapping을 installed contract test와 process evidence에 연결한다.
- 필요한 회귀 검증: `CPP-REG-001` public compile, `CPP-REG-003` worker thread/cancellation/timeout/queue, `CPP-REG-004` same Spot/Actor lane order.
- Core·bindings·package 선행 조건: worker contract는 Framework 소유다. Core C ABI나 binding raw operation을 worker API로 노출하지 않는다.
- 완료 evidence: exact header compile, worker body가 caller serial thread 밖에서 실행되는 thread-id evidence, stop token 관찰, timeout/shutdown 중 completion count 1, queue full terminal, `yield()` 허용 문맥과 `invalid_operation`을 unit와 process E2E에서 확인한다.

### CPP-IMP-003 — Actor와 Spot public declaration이 C++ exact interface와 다름

- 상태: `contract 선행`, public contract gap.
- common/exact 근거: `framework/doc/framework/common/spec/server/languages/cpp/interfaces/04-spots.ko.md:196-229,383-417,853-899`, `framework/doc/framework/common/spec/server/languages/cpp/interfaces/05-actors.ko.md:149-245,278-310`, `framework/doc/framework/common/spec/server/languages/cpp/interfaces/06-stream-session.ko.md:55-94`.
- C++ 경로: `framework/languages/cpp/framework/include/zlink/framework/contracts/actors/actor.hpp:30-82,330-369,378-451,641-760`, `framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp:283-314,957-1015,1320-1355,1787-1844`, `framework/languages/cpp/framework/include/zlink/framework/contracts/streams/stream.hpp:79-84,188-274`.
- 확인한 동작과 기대 동작: 현재 `actor_client_t`는 `send_to_actor(actor_ref_t, ...)`와 `request_to_actor(actor_ref_t, ...)`를 제공하지만 exact interface는 global `actor_id_t`를 받는 `send`와 `request`를 고정하고 ActorRef/owner overload를 제공하지 않는다. 현재 `actor_id_t`도 exact wrapper class가 아니라 `std::string` alias이며, `actor_ref_t`의 인자와 getter도 exact의 `(actor_id, generation, mesh_name, node_rid)`와 다르다. `actor_ref_snapshot_t`는 exact interface에 없는 별도 public surface다. 반면 current `actor_request_call_t`는 metadata와 `yield()`를, `actor_create_call_t`는 `yield()`를 이미 제공하므로 이 두 항목은 gap으로 계산하지 않는다. `spot_create_call_t`에는 exact `yield()`가 없고, 두 manager는 concrete copyable value class인 반면 exact interface는 virtual destructor와 virtual operations를 요구한다. `session_actor_t`는 `session_message_context_t` overload 대신 packet-name overload를 제공한다. `actor_context_t::actor_id()`는 `std::string_view`이고 exact interface는 `const actor_id_t &`다. 현재 `spot_context_t::publish`는 topic만 받으며, 별도의 `spot_publisher_client_t`가 channel과 topic을 받지만 exact interface가 요구하는 common context의 public owner와 다르다. STREAM도 current header의 `stream_dispatch_context_t`와 `on_packet` signature가 exact의 `session_message_context_t`와 다르고, exact stream에 필요한 `routing_id`, local address, remote address가 current public `stream_t`에 없다.
- 판정 근거: source에 관련 class와 method가 있다는 사실은 parameter, const/reference/value, move/copy, virtual/lifetime 계약의 불일치를 해결하지 못한다. 현재 public header를 exact interface 충족으로 세지 않는다.
- 수정 목록: contract governance에 따라 exact C++ declaration을 먼저 확정하고 public header, internal adapter, ownership과 destructor를 같은 방향으로 맞춘다. global ID lookup과 exact ActorRef routing을 서로 다른 operation 의미로 분리한다. Spot publish target ChannelName, Spot create `yield`, session message context와 metadata, STREAM route/address field를 runtime path까지 연결한다. 기존 호출부에 raw ref/handle workaround를 추가하지 않는다.
- 필요한 회귀 검증: `CPP-REG-001`의 positive/negative header compile, move/copy/noexcept/virtual ABI probe, actor/spot manager call, session context relay, publish parameter compile과 installed consumer compile.
- Core·bindings·package 선행 조건: Actor/Spot public contract는 Framework가 소유한다. Core 전용 service ABI, internal `spot_handle_t` 또는 source-only resolver를 public 대체 표면으로 추가하지 않는다.
- 완료 evidence: exact interface와 public header의 declaration diff가 0이고, installed package에서 동일 compile probe가 성공한다. actor/spot create, request, relay, publish가 local/remote/reject/moving generation에서 같은 public contract와 terminal mapping을 process evidence로 남긴다.

### CPP-IMP-004 — Error terminal mapping과 installed contract evidence가 아직 닫히지 않음

- 상태: `runtime 부분 충족`; public enum declaration은 exact 값 집합과 맞지만 operation별 runtime mapping과 installed ABI evidence가 남아 있다.
- common/exact 근거: `framework/doc/framework/common/spec/server/languages/cpp/interfaces/03-channel-messaging.ko.md:564-585`, `framework/doc/framework/common/spec/server/languages/cpp/interfaces/01-common-runtime.ko.md:342-347`, `framework/doc/framework/common/spec/server/languages/cpp/interfaces/05-actors.ko.md:189-191`.
- C++ 경로: `framework/languages/cpp/framework/include/zlink/framework/contracts/errors/error.hpp:13-177`, `framework/languages/cpp/framework/src/runtime/messaging/request_failure_mapper.cpp`, `framework/languages/cpp/framework/src/runtime/messaging/async_submit_runtime.cpp:139-140,298-299`.
- 확인한 동작과 기대 동작: exact C++ interface가 정의한 13개 `framework_error_kind_t` 값과 현재 public header의 enum 이름·numeric value는 일치한다. 현재 header의 `detail::boundary_error_t`와 `failure_origin_t`는 private detail이며 public error kind의 추가 enumerator로 계산하지 않는다. runtime은 request failure, timeout, shutdown과 route 오류를 common kind로 변환하는 mapper를 갖지만, 기대 동작인 timeout, route, shutdown, stale generation의 operation별 terminal mapping과 installed ABI가 동일하다는 증거는 아직 없다. application은 닫힌 common kind로 분기하고 platform `error_code`는 진단 정보로만 사용해야 한다.
- 판정 근거: public enum declaration 자체는 current source에서 gap이 아니다. 그러나 actor/spot/stream/worker/submit의 모든 operation family에서 `kind()`, `code()`, exception과 callback terminal reason이 exact table대로 전달되는지 process evidence가 없고, clean installed package에서 numeric ABI를 확인하는 probe도 남아 있다.
- 수정 목록: 내부 세부 원인은 private mapping에 두고 public `kind()`와 optional diagnostic `code()`를 분리한다. request/send/publish/actor/spot/stream/worker의 error table을 하나의 mapper와 contract test로 통합하고, stale generation을 exact contract가 정한 `invalid_operation` 또는 해당 route reason으로 일관되게 매핑한다. public error enum에 private detail name을 추가하지 않는다.
- 필요한 회귀 검증: `CPP-REG-001` enum value compile, `CPP-REG-005` operation별 kind/code/retriable/exception, shutdown·timeout·stale generation terminal exactly-once process test.
- Core·bindings·package 선행 조건: Core errno와 Framework error kind를 같은 enum으로 재사용하지 않는다. binding package의 error code가 Framework public ABI를 결정하지 않도록 package boundary를 확인한다.
- 완료 evidence: exact 13개 numeric value compile probe, installed symbol/package probe, 각 common E2E의 expected terminal reason, `framework_exception_t::kind()`와 `code()` assertion, callback count 1을 clean runtime에서 확인한다.

### CPP-IMP-005 — Submit/admission runtime regression은 안정화되었지만 전체 process semantics는 남음

- 상태: `contract 선행`, runtime semantics `부분 충족`; focused regression은 통과했지만 common process matrix는 남아 있다.
- common/exact 근거: `framework/doc/framework/common/spec/server/languages/cpp/interfaces/03-channel-messaging.ko.md:927-968`, `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md`, `config-2-spot-service.ko.md`, `config-5-resilience-lifecycle.ko.md`, `config-10-spot-actor-relocation.ko.md`, `config-13-submit-admission.ko.md`.
- C++ 경로: `framework/languages/cpp/framework/include/zlink/framework/contracts/channels/call.hpp:77-127,169-252,298-317`, `framework/languages/cpp/framework/src/runtime/messaging/async_submit_runtime.cpp:144-211,268-300`, `framework/languages/cpp/framework/src/runtime/messaging/submit_queue.*`, actor/spot/stream runtime callers.
- 확인한 동작과 기대 동작: current request call은 serial turn plan이 있을 때 Framework offload executor에 `_submit(...).result()`를 예약하고 결과를 completion source에 넣는다. channel request도 bounded offload executor에서 `blocking_submit()`을 실행한다. one-way runtime은 첫 attempt 뒤 backpressure를 pending queue와 owner reservation에 넣고 deadline 또는 shutdown에서 completion한다. application receive는 HWM을 넘는 batch dispatch를 만들지 않고 한 dispatch turn에서 application record 하나만 시작한다. 기대 동작은 source-local queue admission, bounded deadline, no automatic resubmit after terminal, owner/generation fencing, payload lifetime, retry/replay order와 callback exactly-once가 operation family별로 동일하게 지켜지는 것이다.
- 판정 근거: request/channel call surface에서 unmanaged `std::thread(...).detach()`를 제거하고 runtime offload executor rejection을 typed request failure로 처리했다. owner epoch와 retry admission 뒤 shutdown을 다시 검사해 late success를 `runtime_shutdown`으로 종결하는 회귀를 추가했다. HWM은 exact-boundary pause/resume와 mailbox batch 제한, STREAM reply token은 transport failure 뒤 one-shot 소비를 focused test에서 확인했다. `offload_executor_t::drain_until()`과 cooperative/non-cooperative worker 회귀는 `test_cpp_framework_execution`에서 통과했다. `app_t::run_shared_shutdown()`의 active-run teardown barrier는 `wait_until(deadline_at)`으로 바뀌었고, blocking hosted service 회귀가 `force_stopped/deadline_exceeded`와 run-thread cleanup을 확인한다. 따라서 이 bounded barrier runtime gap은 현재 충족 범위로 이동했지만, normal finalizer의 unbounded `drain()`은 cooperative teardown 뒤에 사용하는 내부 cleanup이고 caller cancellation, relocation backlog와 common process matrix는 아직 남아 있다. initial canonical full CTest 50/55와 latest non-sample 49/49를 합산하지 않는다. narrow green test를 full semantic compliance로 판정하지 않는다.
- 수정 목록: operation별 admission state machine과 terminal reason을 유지하고, pending reservation/owner epoch/late signal/retry credit, callback lifetime, moved payload와 shutdown cleanup을 common E2E에서 확인한다. queue replay와 relocation backlog가 location commit보다 앞서거나 뒤지는 순서를 role evidence로 고정한다. application caller에 raw frame, `parse`, `decode`, internal helper를 추가하지 않는다.
- 필요한 회귀 검증: `CPP-REG-002` admission queue/retry/deadline/owner epoch, `CPP-REG-004` callback/queue order, `CPP-REG-005` terminal mapping, process E2E `SA-E2E-01~20`, `ST-*`, `RM-*`, `SM-*`의 required evidence.
- Core·bindings·package 선행 조건: Core HWM와 native route readiness는 Core/binding package의 실제 candidate runtime으로 검증한다. Framework는 Core service C ABI를 새로 만들지 않고 existing public `zlink::cpp` operation만 사용한다. package version drift가 없는 build를 사용해야 한다.
- 완료 evidence: local/remote/self/unknown/known-disconnected target, ready/pending/timeout/shutdown, owner restart, stream reply token, relocation backlog와 late signal에 대해 attempt count, reservation count, payload value, generation, callback count, terminal reason을 role server evidence와 client result로 모두 확인한다.

### CPP-IMP-006 — Package verifier는 통과했지만 ownership/provenance/ABI gap이 남음

- 상태: `contract 선행`, package/ABI gap.
- common/exact 근거: C++ exact interface의 public include 목록, `framework/doc/framework/common/spec/http-client/languages/cpp/cpp-http-client.ko.md:45-70`, `scripts/local-package/README.ko.md:204-240`.
- C++ 경로: `framework/languages/cpp/CMakeLists.txt:41-87,158-230,272-305,618-735`, `framework/languages/cpp/cmake/zlink_framework_cppConfig.cmake.in:8-32`, `bindings/cpp/cmake/zlink_cppConfig.cmake.in:1-6`, `framework/languages/cpp/scripts/verify_packaged_contract.sh:31-76`.
- 확인한 동작과 기대 동작: current verifier는 installed library hash 3개, 110 headers와 clean packaged consumer를 통과한다. 2026-08-02 historical sample runner는 Release `zlink_cpp=11.1.0`, Core `11.0.0`의 `build-v11-samples`를 사용했고, 2026-08-03 canonical build는 `11.1.0/11.1.0`이다. exact C++ interface 문서는 Framework `11.0.0`을 표기하고, current public `framework/version.hpp`는 `0.1.0`을 표기하므로 문서·public version·package pin도 하나의 provenance가 아니다. C++ HTTP package policy와 CMake export의 ownership 차이는 남아 있다. 기대 동작은 exact C++ contract에 맞는 header/target만 install하고 Framework, sample과 E2E가 같은 version·package·ABI provenance를 사용하는 것이다.
- 판정 근거: stale verifier manifest는 current tree에 맞춰 수정되어 통과했지만, HTTP ownership, Core exact version/ABI, clean consumer의 source-tree negative boundary와 compiler/configuration matrix는 전체 확인하지 않았다. target alias와 library hash만으로 install tree, E2E runtime과 ABI가 완전히 일치한다고 판정하지 않는다.
- 수정 목록: HTTP client ownership을 common package policy와 C++ exact guide에 맞춰 하나로 결정한다. Core dependency exact provenance, target export dependency closure, header include, `nm`/symbol visibility, C++20, static/shared와 Debug/Release/compiler matrix를 gate에 추가한다.
- 필요한 회귀 검증: `CPP-TEST-001`, `CPP-REG-008` clean install consumer와 version/ABI matrix, `CPP-REG-009` Core/binding provenance와 private include negative scan.
- Core·bindings·package 선행 조건: current canonical evidence가 사용하는 `.artifacts/wsl/install/zlink-cpp/11.1.0`과 Core `11.1.0`의 hash와 path를 기록하고, full ABI matrix에서 다시 확인해야 한다. stale package와 다른 cache를 재사용하지 않는다. Framework는 bindings source를 직접 참조하지 않고 local package만 사용한다.
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
- 확인한 동작과 기대 동작: aggregate `all`은 RegistrationCodec부터 SubmitAdmission까지 12개 directory만 `all` selector로 실행하고 exit code 0을 config PASS로 집계한다. common은 14개 config와 374개 ID를 요구한다. Config 12와 14는 C++ selector, role target, client dispatch 모두 없다. 2026-08-02 bounded probe는 첫 RegistrationCodec target build에서 timeout으로 종료됐고, 2026-08-03 probe는 RegistrationCodec PASS와 RegistryMessaging RM-A1~A3 뒤 bounded timeout으로 종료됐다.
- 판정 근거: aggregate 성공은 common 전체의 실행 결과가 아니며, 현재 bounded run도 전체 config에 도달하지 못했다. 없는 두 config를 단순 skip하거나 성공으로 표시하면 scenario inventory gap을 숨긴다. current aggregate는 canonical `framework/languages/cpp/build`를 사용하지만 exact package/role evidence와 full selector mapping은 남아 있다.
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
- 확인한 동작과 기대 동작: static include scan에서 C++ client source가 Framework `src/runtime` 또는 Core C ABI를 직접 include하는 사례는 확인하지 못했다. 그러나 CMake는 여러 client target에 `${ZLINK_FRAMEWORK_CPP_DIR}/framework/src`를 private include path로 제공한다. 이 설정은 client가 private header를 사용할 수 있는 compile boundary를 열어 둔다. aggregate는 child runner exit code와 `PASS` text를 중심으로 집계하고 exact role evidence manifest를 공통 inventory와 대조하지 않는다. `.github/workflows/`에는 C++ Framework workflow가 없고, v11 regression plan은 C++ E2E를 `BUILD_E2E=OFF`로 configure한다. 2026-08-02 bounded aggregate는 RegistrationCodec target build에서 timeout으로 종료됐고, 2026-08-03 bounded aggregate는 RegistrationCodec PASS 뒤 RegistryMessaging RM-A1~A3에서 timeout으로 종료됐다.
- 판정 근거: public include를 실제로 사용했다는 source scan은 좁은 충족 evidence다. private include permission, E2E의 stale build cache, undefined old role server, current CTest failure, historical feature-map와 no-C++-workflow를 함께 보면 process E2E와 CI가 common rule을 enforce한다고 판정할 수 없다.
- 수정 목록: client target에서 `framework/src` include path를 제거하고 public installed package만으로 compile한다. Framework call은 role server endpoint 내부에만 두며 client는 HTTP/stream connector로 result와 role evidence를 읽는다. aggregate에 exact ID/evidence manifest parser, partial/N/A policy, child cleanup와 bounded timeout을 추가한다. C++ Framework CI job을 추가하거나 existing gate가 C++ source/common spec path를 실제로 trigger하도록 연결한다.
- 필요한 회귀 검증: `CPP-REG-007` inventory/aggregate, `CPP-REG-008` clean consumer, `CPP-REG-009` private include/Core boundary and CI path filter, process role/evidence gate.
- Core·bindings·package 선행 조건: package mode에서 client와 role server 모두 pinned local `zlink_cpp`/Core package를 사용한다. Framework internal ABI와 Core service C ABI는 include/link target에 나타나지 않아야 한다. bindings source reference와 stale cache를 금지한다.
- 완료 evidence: client-only compile fails if an internal header is included; all role server target process paths compile from installed public headers; each scenario has client result plus role evidence, aggregate counts exact IDs, CI runs the C++ job on C++/common-spec/package changes and records full logs.

## 7. Core·bindings·package 선행 조건

| 영역 | 현재 사실 | 완료 전 조건 |
|---|---|---|
| Core | Framework CMake는 `zlink::cpp`를 link하고 Framework source에는 `raw_mesh_node_owner`, service wire와 stateful runtime 같은 private implementation이 있다. public E2E client에서 Core C ABI 직접 사용은 정적 scan에서 확인하지 못했다. | Core service 의미를 Framework public API로 우회하지 않는다. current Core `11.1.0` runtime과 binding `11.1.0` package의 hash, install path, ABI를 기록하고, Framework production call path가 public binding operation에만 의존하는지 negative scan과 clean consumer로 확인한다. |
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
8. S0 Framework gate가 닫힌 뒤 이 문서의 18장 G2~G8을 실행한다. sample contract, runner,
   CMake/package provenance와 6개 process evidence를 S0의 선행 결과와 연결한다.
9. 이 문서의 S0와 S1 모든 gap에 current source line, current test output, current process log와
   package hash를 연결하고, 아래의 Codex model·agent 수준 규칙에 맞는 reviewer의 re-review 결과를
   기록한다. 문서 원칙에 따라 결과·조건·다음 작업을 다시 검토한다. S1이 닫히기 전에는 이
   문서의 최종 완료를 표시하지 않는다.

### 8.1 리뷰 요청과 Codex model·agent 수준

리뷰 요청은 작업의 위험도에 맞는 Codex model과 reasoning level을 지정해 전달한다. 5.6 계열
model은 `gpt-5.6-sol`과 `gpt-5.6-terra` 중 목적에 맞춰 선택한다. [OpenAI Model guidance](https://developers.openai.com/api/docs/guides/latest-model)에 따라 복잡한 contract·runtime·E2E 판정에는
frontier model인 `sol`을 사용하고, 기계 검증과 구현 후속 작업에는 비용·성능 균형을 고려해 `terra`를
사용한다. 저장소 규모나 예상 소요 시간만으로 model·level을 낮추지 않으며, 좁은 test가 통과했다는
이유로 public contract 또는 runtime 완료 판정을 내리지 않는다.

| 리뷰 범위 | Codex model | reasoning level | 필요한 결과 |
|---|---|---|---|
| 문서 heading, link, ID 중복, `git diff --check`, 금지 표현과 기계적인 manifest 대조 | `gpt-5.6-terra` | `medium` | 누락·형식 오류 목록과 실행 command/result |
| public API·exact interface·ABI, ownership/lifetime, callback, exception/error mapping, runtime lifecycle·concurrency, package/export·CI, E2E selector·role evidence와 Core·bindings 책임 경계 | `gpt-5.6-sol` | `xhigh` | authoritative source별 판정, 실제 call path와 반례, gap별 수정 순서와 회귀 evidence |
| S0+S1 전체 closure, 서로 충돌하는 build/test/E2E/package 결과, terminal·cleanup·recovery를 포함한 최종 audit | `gpt-5.6-sol` | `max` | cross-layer compliance verdict, 미해결 blocker, 완료 checklist별 직접 증거 |

각 리뷰 요청에는 당시 선택한 model, reasoning level, 검토 범위, authoritative source, 대상 경로,
current 실행 결과와 제외 범위를 적는다. reviewer 결과에는 실제 evidence와 `충족`·`gap`·
`contract 선행` 판정을 구분해 기록한다. 선택한 reviewer를 바로 사용할 수 없으면 해당 review를
`대기`로 남기고, 독립적으로 가능한 문서 정리·증거 수집·test 준비를 계속한다. review 수준을
조정할 때는 current guide와 candidate 위험도를 근거로 삼으며, review 전에는 closure를 표시하지
않는다.

#### 8.1.1 단계별 review request profile

리뷰 요청을 만들 때는 아래 profile 중 하나를 선택해 model과 level을 요청에 그대로 기록한다.
`model`을 비워 두거나 reviewer가 임의로 선택하게 하지 않는다.

| 단계 | 요청 목적 | 선택할 model | 선택할 reasoning level | 요청 record 예시 |
|---|---|---|---|---|
| S0 contract/runtime review | public API·ABI·ownership·lifecycle·concurrency·package·common E2E | `gpt-5.6-sol` | `xhigh` | `model: gpt-5.6-sol; level: xhigh` |
| S1 sample review | 6개 sample의 message contract·production call path·runner·role evidence | `gpt-5.6-sol` | `xhigh` | `model: gpt-5.6-sol; level: xhigh` |
| 기계적 문서 검증 | heading·link·ID·manifest·diff·금지 표현 | `gpt-5.6-terra` | `medium` | `model: gpt-5.6-terra; level: medium` |
| 구현 후속 작업 | review에서 확정한 수정 사항의 code·test 구현 | `gpt-5.6-terra` | `high` | `model: gpt-5.6-terra; level: high` |
| 최종 S0+S1 closure | cross-layer verdict·충돌 evidence·terminal·recovery·cleanup | `gpt-5.6-sol` | `max` | `model: gpt-5.6-sol; level: max` |

S0·S1 review와 최종 closure에 적절한 reviewer를 바로 사용할 수 없으면 `대기`로 남기고
독립적으로 진행할 수 있는 작업을 계속한다. 각 review record에는 실제 사용 model·level과 선택
근거를 기록한다. `max`가 필요한지는 최종 candidate의 위험도와 evidence를 보고 정하며, phase
review도 동일하게 형식적인 level보다 충분한 판단 근거를 우선한다.

## 9. 기존 회귀 test의 유지·변경·추가 목록

### 9.1 기존 test와 script의 유지/변경

#### CPP-TEST-001 — packaged contract verifier의 manifest 기준이 현재 exact spec과 다름

- 상태: `부분 충족`; current installed manifest와 clean consumer는 통과했지만 exact public contract·HTTP ownership 정렬은 남아 있다.
- 근거: `framework/languages/cpp/scripts/verify_packaged_contract.sh:31-76`, `framework/doc/framework/common/spec/server/languages/cpp/interfaces/04-spots.ko.md:855-899`, `framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp:283-310`.
- C++ 경로: current verifier required/forbidden list와 temporary install source.
- 확인한 동작과 기대 동작: verifier required header를 current install tree와 맞추고 retired private token만 forbidden으로 유지했다. 2026-08-03 canonical `build`에서 installed library hash 3개, 110 headers와 clean packaged consumer가 통과했다. 기대 동작은 exact spec, installed public header, target export와 verifier manifest가 하나의 contract를 공유하는 것이다.
- 판정 근거: verifier 자체는 통과했지만 HTTP package ownership, exact declaration과 full ABI matrix를 아직 닫지 않았다.
- 수정 목록: exact public interface와 package policy를 contract review로 확정한 뒤 HTTP ownership, target export dependency closure와 ABI matrix를 함께 검증한다.
- 필요한 회귀 검증: installed header positive/negative compile, `find_package` clean consumer, target export와 no-source-tree check.
- Core·bindings·package 선행 조건: exact `spot_ref_t` package header와 pinned `zlink_cpp`/Core package를 사용한다. old handle source를 package에 복사하지 않는다.
- 완료 evidence: verifier가 current exact manifest와 clean consumer compile/run을 모두 통과하고, package header에 forbidden old surface가 없다는 결과를 남긴다.

#### CPP-TEST-002 — textual target gate가 runtime/E2E completion을 증명하지 못함

- 상태: audit/test gap.
- 근거: `framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_target_contract.cpp:3-8,24-68`, `framework/languages/cpp/CMakeLists.txt:1021-1031`, `scripts/v11/run-framework-runtime-regression.mjs:292-320`.
- C++ 경로: `test_cpp_framework_target_contract`, `test_cpp_framework_layout_contract`, `test_cpp_framework_contract_headers`, CTest inventory.
- 확인한 동작과 기대 동작: target contract test는 public header와 E2E wiring text를 filesystem scan하고 current CTest에서 PASS했다. `test_cpp_framework_install_consumer`와 `test_cpp_http_client`도 PASS했고, 2026-08-03 focused CTest 12/12와 package verifier가 통과했다. 이후 bounded host shutdown regression을 포함한 latest non-sample CTest도 49/49로 통과했다. initial parallel full CTest는 sample process 5개 failure를 포함해 50/55였으며, common 완료는 built declaration, actual runtime, package consumer와 process E2E evidence까지 요구하므로 aggregate process gate와 exact contract는 아직 남아 있다.
- 판정 근거: static gate PASS와 production/runtime/E2E PASS 사이에 증명 단계가 없다. 좁은 target gate를 전체 compliance verdict로 사용하면 false positive가 된다.
- 수정 목록: textual scan은 early diagnostic으로 유지하되 completion gate에서 분리한다. exact header compile, installed package consumer, common ID/evidence manifest, process role server assertion과 aggregate result를 별도 required gate로 추가한다. CTest timeout/abort/segfault를 별도 failure record로 남기고, test가 historical ledger의 문자열 상태에 의존하지 않게 한다.
- 필요한 회귀 검증: `CPP-REG-001`, `CPP-REG-007`, `CPP-REG-008`, `CPP-REG-009`와 current `ctest` result classification.
- Core·bindings·package 선행 조건: gate는 source tree private header와 stale cache를 사용할 수 없다. pinned local package와 clean install prefix를 input으로 받는다.
- 완료 evidence: static gate, built public compile, runtime unit, clean package consumer, exact 14-config process aggregate가 각각 PASS하고 한 단계의 PASS가 다른 단계를 대신하지 않는다.

### 9.2 추가 또는 변경해야 할 regression ID

아래 9개 ID는 이 ledger의 regression 추적 ID다. runtime phase에서 `CPP-REG-002`와 `CPP-REG-003`의 핵심 focused test를 추가했으며, 나머지는 exact contract·process·package 범위가 남아 있다.

| ID | 대상 | 현재 상태 | 필요한 assertion과 완료 evidence |
|---|---|---|---|
| `CPP-REG-001` | exact public header/ABI | 미착수 | worker/actor/spot/error의 namespace, method, parameter, `const`/reference/value, move/copy, destructor, `noexcept`, enum numeric value와 installed consumer compile을 positive/negative probe로 확인한다. |
| `CPP-REG-002` | submit admission | focused 구현·검증 완료 | first attempt, HWM/backpressure, HWM exact-boundary pause/resume, 한 dispatch turn 한 application record, retry 1회, owner reservation, owner epoch, shutdown 중 late success, pending cleanup과 STREAM reply token one-shot terminal consumption을 unit에서 확인했다. deadline·process matrix는 남아 있다. |
| `CPP-REG-003` | worker execution | focused 구현·검증 완료 | CPU/IO queue boundary, `std::stop_token`, queue full, timeout, host shutdown, worker options/sealing, owner-managed deadline과 completion exactly-once를 unit에서 확인하고 worker/admission 100회와 M6A 100회, 2026-08-03 non-sample CTest 49/49가 통과했다. exact caller cancellation/process evidence는 남아 있다. |
| `CPP-REG-004` | lifecycle/queue/relocation | focused 부분 검증 | M6B runtime에서 actor join/spot create, authority commit, owner/generation, callback order, bounded Message Follow와 relocation backlog/replay, stale route cleanup을 확인했다. common process matrix와 전체 `submit/yield` family는 남아 있다. |
| `CPP-REG-005` | error mapping | 추가 예정 | operation family별 `kind`, diagnostic `code`, retriable flag, terminal reason, duplicate/late completion과 shutdown mapping을 확인한다. |
| `CPP-REG-006` | HTTP production path | CTest 핵심 경로 통과 | supported Boost buffer API, typed async submit, blocking boundary와 HTTP integration/client test를 통과했다. HTTPS 조건부 path와 common role evidence는 남아 있다. |
| `CPP-REG-007` | E2E inventory/aggregate | 추가 예정 | common 374 ID와 C++ selector/dispatch set difference, Config 12/14 existence, partial/N/A/diagnostic_only exclusion, exact scenario PASS line을 확인한다. |
| `CPP-REG-008` | package/install/ABI | verifier·consumer 부분 통과 | Framework/StreamConnector/Dependency components, target export, clean consumer와 current header manifest를 확인했다. no source-tree include, exact version/hash와 static/shared・compiler matrix는 남아 있다. |
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
- [ ] `CPP-TEST-001~002`: verifier와 textual gate가 exact contract와 actual runtime/E2E gate를 서로 모순 없이 검사한다. package verifier와 focused CTest 12/12는 통과했지만 current canonical full CTest는 50/55이며 exact contract와 common E2E gate는 남아 있다.
- [ ] `CPP-REG-001~009`: `CPP-REG-002/003` focused regression과 반복은 통과했으며, 나머지 contract/process/package/CI 범위가 구현·검증되어야 한다.
- [ ] 이 문서 11~20장의 `CPP-SAMPLE-IMP-*`, `CPP-SAMPLE-E2E-IMP-*`, `CPP-SAMPLE-TEST-*`,
      `CPP-SAMPLE-REG-*`가 current sample source와 process evidence로 완료됐다.
- [ ] S1의 6개 C++ sample aggregate가 완료되었고, `ZoneWorld`는 C++ 계약 대상이 아니라는 범위
      근거가 유지된다.
- [ ] S1 완료 전에는 이 문서와 전체 C++ audit을 `완료`로 표시하지 않는다.
- [ ] C++ E2E client는 public HTTP client/stream connector만 사용하고 Framework call은 role server에 있다.
- [ ] role server evidence와 client-visible result가 scenario별로 모두 확인된다. historical log, source type 존재, unit test만으로 PASS를 만들지 않는다.
- [ ] Core 전용 C ABI, Framework internal ABI, private header, raw frame, test-only adapter와 reflection 우회가 없다.
- [ ] C++ Framework CI가 C++ source, common spec, package policy와 E2E path change에서 required gates를 실행한다.
- [ ] final re-review request에 `model: gpt-5.6-sol`과 `level: max`가 선택되어 기록됐고, 실제
      사용 model·level이 요청과 일치한다.
- [ ] final re-review가 선택된 Codex model·level로 수행되었고, 요청 범위·authoritative source·
      실행 evidence·판정이 기록됐다.
- [ ] final re-review에서 현재 `git status`, build/test/E2E/package log와 이 문서의 gap 상태가 같은
      working tree를 가리킨다.

### 현재 미해결 blocker 요약

- C++ Framework production/runtime: canonical build와 latest non-sample CTest 49/49가 통과했다. initial parallel full CTest는 50/55였고, sample process 3건은 serial에서도 실패했다. 이전 failure와 현재 sample 결과는 progress log에 분리해 기록했다.
- Regression stability: worker/admission 100회, bounded host shutdown regression과 2026-08-03 non-sample CTest 49/49가 통과했다. DeliveryDispatch와 SupportChat의 병렬 실패는 FetchContent/ExternalProject shared build race로 분리했으며, Bingo/GameQuest/ShoppingMall process failure는 남아 있다.
- Public contract: worker, actor/spot declaration과 error enum mismatch.
- Runtime semantics: admission late-success, worker scheduling/cancellation, worker options, owner-managed deadline, ClientServer hello readiness, HWM exact-boundary·mailbox batch dispatch와 STREAM reply-token 핵심 회귀는 통과했다. caller cancellation, relocation의 common process matrix와 role evidence는 남아 있다.
- E2E: Config 12/14 부재, Config 1~11/13 scenario 누락 또는 partial aggregate, Config 10 migration host, role evidence와 private include boundary. 2026-08-03 bounded 12-config aggregate는 RegistrationCodec PASS 뒤 RegistryMessaging RM-A1~A3에서 timeout으로 종료됐다.
- Package: `zlink_cpp`/Core provenance drift, HTTP client ownership 문서와 CMake export 불일치, stale packaged verifier manifest.
- CI: C++ Framework 전용 GitHub workflow와 common 14-config process gate 부재.
- Sample 후속: TicTacToe는 2026-08-02 pinned runner와 2026-08-03 canonical CTest에서 통과했다. 5개 실패 sample의 serial 결과 중 DeliveryDispatch와 SupportChat은 통과했지만 Bingo actor destroy cleanup, GameQuest progress observation, ShoppingMall client/evidence 검증은 남아 있다. S1 six-sample aggregate, PowerShell parity, exact message contract와 role-server evidence도 남아 있다.
- 다른 언어 구현은 위 gap을 해결하는 public API 근거로 사용하지 않았다.

---

## 11. S1 Sample phase — 목적과 완료 조건

이 장부터 20장까지는 S0 Framework spec·public contract·common E2E gate가 완료된 뒤 진행하는
C++ sample audit이다. sample은 사용자가 따라 하는 public API 경로와 role server의 실제 호출 예시를
보여 주므로, source 파일이나 public class의 존재만으로 완료하지 않는다. 공통 sample 계약, 실제
Domain → Application → Infrastructure 호출 경로, client-visible 결과, role-server evidence와
process cleanup을 함께 확인한다.

S1에서 확인하는 C++ sample 범위는 다음 6개다.

- `Bingo`
- `TicTacToe`
- `SupportChat`
- `DeliveryDispatch`
- `ShoppingMall`
- `GameQuest`

`ZoneWorld`는 `framework/doc/framework/common/sample/README.ko.md:12-16`에서 .NET과 Node.js
범위로만 정의되어 있다. 따라서 C++ sample 계약 대상에 포함하지 않으며, 다른 언어 구현이나
common E2E 문서만으로 C++ public API를 추가하지 않는다.

S1 완료는 다음 조건을 모두 만족할 때로 판정한다.

- 6개 sample의 message name, field type, optional/null 표현, transport와 one-way/reply 의미가
  공통 sample 문서와 C++ shared contract에서 일치한다.
- C++의 value/reference/move, `shared_ptr`·`unique_ptr`, callback capture와 buffer lifetime가
  공통 ownership 계약을 위반하지 않는다. actor·session·connection identity 같은 Framework
  내부 식별자가 Domain contract로 노출되지 않는다.
- `Client`, `Shared`, 각 role server의 책임 경계와 실제 production call path가 공통 sample의
  Domain → Application → Infrastructure 구조와 일치한다.
- shell과 PowerShell individual/aggregate runner가 같은 6개 inventory를 실행하며, selector,
  readiness, 전용 Redis, child process 상태, log, terminal reason과 cleanup을 직접 확인한다.
- 각 sample은 public HTTP client 또는 stream connector를 통해 실제 role server endpoint를 호출하고,
  client-visible 결과와 role-server owner/generation/callback/terminal/cleanup evidence를 같은 run
  단위로 확인한다.
- `부분`, `미구현`, `diagnostic_only`, `N/A`, source-only, historical log를 aggregate 성공으로
  계산하지 않는다.
- CMake target, install/export, package version과 clean consumer가 같은 package provenance를
  가리킨다. S0 blocker를 sample code나 private ABI로 우회하지 않는다.

S0가 닫히기 전에는 이 장의 implementation을 완료로 표시하지 않는다. S1이 닫히기 전에는 문서
전체의 C++ audit closure를 기록하지 않는다.

## 12. S1 Sample phase — 조사 범위와 authoritative source

S1의 계약 우선순위는 S0와 같다. 공통 sample 문서와 공통 Framework spec이 목표 계약이며, C++ exact
interface와 설치된 public package가 언어별 표현을 결정한다. 다른 언어 sample source는 계약의 출처가
아니라 해석을 비교하는 참고 자료다.

확인한 자료는 다음과 같다.

- 공통 sample index와 규칙: `framework/doc/framework/common/sample/README.ko.md`.
- sample 계약: `framework/doc/framework/common/sample/bingo/README.ko.md`,
  `tictactoe/README.ko.md`, `supportchat/README.ko.md`, `deliverydispatch/README.ko.md`,
  `event/shoppingmall.ko.md`, `event/gamequest.ko.md`.
- 공통 Framework spec과 C++ exact interface: 이 문서 2.2에 기록한 `common/spec/`와
  `common/e2e/`의 authoritative source.
- C++ sample production: `framework/languages/cpp/samples/` 아래의 `Client/`, `Shared/`,
  `Server/` 및 각 sample `CMakeLists.txt`.
- C++ sample test: `framework/languages/cpp/tests/Zlink.Framework.ContractTests/`의 sample
  parity, layout, target, install consumer test.
- C++ runner: `framework/languages/cpp/samples/{run_samples.sh,run_samples.ps1}`와 각 sample의
  `run_sample.sh`, `run_sample.ps1`.
- package/build: `framework/languages/cpp/CMakeLists.txt`, `framework/languages/cpp/cmake/`,
  `framework/languages/cpp/scripts/verify_packaged_contract.sh`,
  `scripts/local-package/README.ko.md`.

S1의 완료 근거는 다음 세 종류를 분리한다.

1. static contract 및 source 구조 확인은 public boundary와 inventory의 조기 진단이다.
2. unit/integration 및 package consumer 결과는 compile·install·serialization 계약의 근거다.
3. process E2E 결과는 실제 client 호출, role server 상태, callback 순서와 cleanup의 근거다.

한 종류의 결과를 다른 종류의 완료 근거로 대체하지 않는다.

## 13. S1 Sample phase — 현재 검증 결과

### 13.1 Working tree와 실행 범위

runtime phase에서 C++ sample runner, pinned build helper와 TicTacToe readiness path를 수정했다.
공통 sample 문서와 다른 언어의 사용자 변경은 범위 밖으로 두었으며, 실제 진행 기록은
`log/2026-08-02-progress.log`와 `log/2026-08-03-progress.log`에 단계가 끝날 때마다 추가했다.

### 13.2 새로 실행한 결과

| 검증 | 현재 결과 | 판정 |
|---|---|---|
| sample static contract CTest (`test_cpp_framework_sample_parity`, `test_cpp_framework_target_contract`, `test_cpp_framework_layout_contract`, `test_cpp_framework_install_consumer`) | 4/4 passed | static parity/layout/target/install 조건만 확인했다. process completion은 증명하지 않는다. |
| `ZLINK_CPP_AUTO_CONNECT_TRACE=1 timeout 300s framework/languages/cpp/samples/TicTacToe/run_sample.sh` | exit 0 | pinned `build-v11-samples`에서 preflight 3/3, `PASS TicTacToe.Cpp`, full client/server self-check를 확인했다. |
| `timeout 180s framework/languages/cpp/samples/TicTacToe/run_sample.sh` | exit 124 | 같은 runner의 첫 incremental build가 actor gateway compile 중 bounded limit에 도달했다. application process failure로 계산하지 않는다. |
| `framework/languages/cpp/samples/run_samples.sh --max-attempts=1` | 전체 미실행 | six-sample aggregate와 PowerShell parity는 아직 process evidence가 없다. |
| `framework/languages/cpp/scripts/verify_packaged_contract.sh framework/languages/cpp/build` | passed | 2026-08-03 canonical installed manifest, library hash 3개, 110 headers와 clean packaged consumer가 통과했다. |
| S0 full CTest와 C++ common E2E aggregate | initial parallel full CTest 50/55, latest non-sample 49/49 / bounded aggregate 미완료 | runtime non-sample gate와 package gate는 통과했지만 sample process 3건과 common 14-config process aggregate는 별도 blocker다. |

`TicTacToe` pinned 실행은 실제 role server 호출과 client/server self-check까지 통과했다. 이 결과는
6개 sample aggregate, PowerShell parity, common E2E 전체와 exact message contract를 대신하지 않는다.

## 14. S1 Sample phase — 현재 충족 판정

현재 다음 항목은 정적 범위에서만 확인되었다.

| 항목 | 현재 판정 | 제한 |
|---|---|---|
| C++ sample 범위 | `Bingo`, `TicTacToe`, `SupportChat`, `DeliveryDispatch`, `ShoppingMall`, `GameQuest`의 6개 목록이 source와 shell aggregate에 존재한다. | exact common inventory와 PowerShell aggregate는 미완료다. |
| `ZoneWorld` 범위 | C++ 대상이 아님을 공통 index에서 확인했다. | 다른 언어 parity를 근거로 C++ API를 추가하지 않는다. |
| compile-time message registration | 6개 sample이 `Shared/Contracts`와 registration 경로를 사용한다. | field·optional·codec 전체 diff를 증명하지 않는다. |
| public client boundary | client source에서 public Framework 호출 경계를 정적으로 확인했다. | 실제 process 호출과 role-server evidence가 없다. |
| static parity/layout/target gate | 4/4 narrow CTest가 통과했다. | static green은 전체 계약 완료가 아니다. |
| role executable 분리와 Redis cleanup | source/runner 구조에 정적 경로가 있다. | 각 process 실행에서 cleanup과 terminal evidence를 확인하지 못했다. |
| exact message/field/transport parity | 미충족 | `CPP-SAMPLE-IMP-001`을 닫아야 한다. |
| 6개 process와 aggregate | 미충족 | serial sample smoke는 3/6만 통과했다. Bingo, GameQuest, ShoppingMall이 실패했고 common E2E aggregate도 bounded timeout으로 종료됐다. |

## 15. `CPP-SAMPLE-IMP-*` production implementation gap

### CPP-SAMPLE-IMP-001 — 공통 message·field·transport inventory와 C++ shared contract가 다름

- 상태: `gap`, 일부 항목은 `contract 선행`.
- 공통 spec/E2E 경로: `framework/doc/framework/common/sample/{bingo,tictactoe,supportchat,deliverydispatch}/README.ko.md`, `event/{shoppingmall,gamequest}.ko.md`의 message inventory와 업무 흐름, 해당 공통 E2E의 routing·lifecycle·failure scenario.
- C++ 경로: `framework/languages/cpp/samples/{Bingo,TicTacToe,SupportChat,DeliveryDispatch,ShoppingMall,GameQuest}/Shared/Contracts/messages.hpp`, 각 `Client/*_scenario.hpp`, 각 role `main.cpp`와 compile-time registration.
- 확인한 실제 동작과 기대 동작: C++ source는 sample별 packet declaration과 registration을 제공하지만 공통 계약과 다음 차이가 있다. `SupportChat`은 `OpenConversationApiRes`에 `conversationId/status`를 두고 `ConversationCreateReq`에 `createdAtUnixMs`가 없으며 `SetTypingReq`를 사용한다. 공통 계약은 `SetTypingMsg` one-way message와 공통 state 표현을 요구한다. `TicTacToe`는 `CreateGameHttpReq.game_name`을 non-optional로 두고 `CreateGameHttpRes.owner_play_endpoint`를 추가하며 `LeaveGameReq`를 사용한다. 공통 계약은 optional `gameName`, `LeaveGameMsg` one-way message를 요구하고 current shared contract에서 `JoinGameFailedNotify`가 보이지 않는다. `DeliveryDispatch`는 `occurred_at` 문자열과 빈 문자열 기반 optional courier/reason을 사용하지만 공통 계약은 `occurredAtUnixMs`와 nullable 표현을 요구한다. `GameQuest`는 `JoinSessionRes.playerId`가 없고 `targetConnectionId`를 push에 노출하며 `ClosePlayerQuestMsg`가 없고 추가 action/projection message가 있다. `ShoppingMall`은 `StartOrderRes.orderId/status`를 사용하지만 공통 계약은 `orderId/state`를 요구하고, `StartOrderWorkflowReq.sourceCommandId`가 없다. optional `OrderState` field도 빈 문자열로 표현한다. `Bingo`는 대부분 대응하지만 `lastDrawnNumber`의 optional 값 표현과 `EnsurePlayerActorRes`의 추가 field를 internal/public 범위로 분리해야 한다. 기대 동작은 공통 6개 inventory가 name·field·type·optional/null·transport·reply semantics까지 일대일로 대응하는 것이다.
- gap 판정 근거: compile-time registration과 일부 parity assertion은 source shape의 존재만 확인한다. 위의 message 이름, field type, optional/null, extra message와 one-way/reply 차이는 현재 4/4 static gate와 sample process failure만으로 해소되지 않는다. 공통 문서에 없는 extra를 다른 언어 구현만으로 public C++ API로 승격할 수 없다.
- 구체적인 수정 목록: 6개 공통 문서에서 machine-readable inventory 또는 동일한 contract source를 정하고 C++ shared contract를 그 기준에 맞춘다. one-way message와 reply API를 분리하고, timestamp·nullable·optional field를 typed JSON/Protobuf 표현으로 고정한다. extra field/message는 internal 또는 test/evidence-only로 명시하며 public contract에서 제거하거나 contract review를 먼저 수행한다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-001`~`008`과 negative extra-message case. 각 sample의 serialized payload fixture, field omission/null, packet kind, response absence를 확인한다.
- Core·bindings·package 선행 조건: S0 exact Framework interface와 pinned `zlink_cpp`/Core package가 먼저 고정되어야 한다. Core C ABI, Framework private header와 raw frame으로 message 차이를 보정하지 않는다.
- 완료 evidence: 6개 inventory diff가 0이고, 설치된 public package에서 compile/runtime serialization fixture가 통과하며, extra public surface와 공통 계약 외의 API가 남지 않는다.

### CPP-SAMPLE-IMP-002 — C++ ownership·routing identity와 codec 책임이 sample contract 밖으로 노출됨

- 상태: `gap`, `contract 선행`.
- 공통 spec/E2E 경로: 6개 공통 sample 문서의 client/session/actor 흐름과 `common/spec`의 message ownership, channel, actor, spot contract.
- C++ 경로: `framework/languages/cpp/samples/GameQuest/Shared/Contracts/messages.hpp`, `GameQuest/Client/*`, `GameQuest/Server/*`, sample 공통 session/actor binding과 `nlohmann::json` serialization 경로.
- 확인한 실제 동작과 기대 동작: `GameQuest` payload가 `std::vector<uint8_t>`와 `nlohmann::json::to_msgpack/from_msgpack`으로 처리되고, `targetConnectionId`와 `actor_ref_snapshot_t` 계열 별칭이 message 또는 projection 경계에 나타난다. 공통 계약은 typed JSON object와 Framework가 관리하는 session/actor routing을 사용하며, client가 connection identity와 borrowed buffer lifetime을 관리하지 않도록 한다. raw pointer, borrowed reference, move된 message와 callback capture가 process lifetime을 넘는지 직접 증명하는 evidence도 없다.
- gap 판정 근거: payload codec과 route identity가 public sample contract에 섞이면 Framework의 ownership·routing 책임이 호출자와 Domain으로 이동한다. source에 serializer가 있다는 사실은 공통 codec과 lifetime 계약을 충족한다는 증거가 아니다.
- 구체적인 수정 목록: typed JSON serializer 경로를 Framework 책임으로 고정하고 호출부 MessagePack encode/decode를 제거하거나 contract review 대상으로 분리한다. connection id와 actor snapshot은 internal projection으로 격리하고 public sample message에는 typed business field만 둔다. callback capture, buffer ownership, move/copy와 shutdown 중 in-flight operation을 명시한다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-002`, `006`, `009`, `010`, `017`, `021`~`024`.
- Core·bindings·package 선행 조건: Framework public actor/session/channel API와 package header가 exact interface와 일치해야 한다. Core internal identity와 binding raw operation을 sample path에 사용하지 않는다.
- 완료 evidence: client와 Domain source에 private identity·raw payload codec·borrowed buffer가 없고, typed serialization, reconnect/replay, exactly-once terminal과 cleanup process evidence가 통과한다.

### CPP-SAMPLE-IMP-003 — role·layering 구조와 production call path를 공통 sample 책임으로 고정하지 않음

- 상태: `gap`.
- 공통 spec/E2E 경로: 각 공통 sample 문서의 Domain → Application → Infrastructure 설명, Client/Shared/Server 분리 규칙과 role server self-check 기준.
- C++ 경로: `framework/languages/cpp/samples/{DeliveryDispatch,GameQuest,ShoppingMall}/Server/main.cpp`, 각 sample의 `Domain/`, `Application/`, `Infrastructure/`, `Client/`, `Shared/`와 `CMakeLists.txt`.
- 확인한 실제 동작과 기대 동작: directory와 executable 분리는 정적으로 보이지만 DeliveryDispatch, GameQuest, ShoppingMall의 업무 흐름이 role `main.cpp`와 shared contract에 직접 집중된 경로가 남아 있다. 기대 동작은 client가 business command를 보내고, role server의 Application이 Domain 상태 전이를 수행하며, Infrastructure가 Framework transport·persistence를 담당하는 실제 process call path다.
- gap 판정 근거: directory 존재와 historical inventory는 runtime call order, owner 변경, actor join/leave, rollback·cleanup을 증명하지 않는다. client가 private runtime 정보를 조립하거나 role server가 Framework internal ABI를 호출하면 공통 책임 경계를 위반한다.
- 구체적인 수정 목록: 6개 sample마다 public client call → role server endpoint → Application handler → Domain transition → Infrastructure result의 trace를 고정한다. owner/generation, actor join, relocation, HWM/stream과 cleanup은 role server evidence로 남기고, client에서 raw frame·Core operation·Framework private adapter를 제거한다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-009`, `013`, `017`, `021`~`024`.
- Core·bindings·package 선행 조건: S0 public actor/spot/channel/stream contract와 package export가 먼저 완료되어야 한다. 역할 경계 문제를 Core C ABI 추가로 해결하지 않는다.
- 완료 evidence: 각 sample의 process trace와 role evidence가 동일 run id로 연결되고, Domain/Application/Infrastructure include·호출 경계 및 owner/generation 순서가 직접 assertion된다.

### CPP-SAMPLE-IMP-004 — shell·PowerShell runner inventory와 완료 판정이 일치하지 않음

- 상태: `gap`.
- 공통 spec/E2E 경로: `framework/doc/framework/common/sample/README.ko.md`의 runner, readiness, Redis, retry 금지, aggregate completion 규칙.
- C++ 경로: `framework/languages/cpp/samples/run_samples.sh`, `run_samples.ps1`, 각 sample `run_sample.sh`와 `run_sample.ps1`.
- 확인한 실제 동작과 기대 동작: shell aggregate는 6개 sample을 나열하지만 bind failure를 재시도한다. PowerShell aggregate는 `TicTacToe`와 `Bingo` runner만 호출하며 다른 4개 sample의 Windows 경로가 없다. 현재 shell aggregate는 첫 `TicTacToe` failure 뒤 중단했고, 단일 `sample all result=passed` 표식은 role evidence와 cleanup을 표현하지 않는다. 기대 동작은 양쪽 runner가 같은 6개 inventory, selector, retry policy, bounded timeout, result schema와 cleanup을 사용하고 모든 선택 sample의 결과를 수집하는 것이다.
- gap 판정 근거: shell 목록과 CTest 등록 수는 PowerShell parity, actual process success, callback/owner/terminal evidence를 보장하지 않는다. bind failure retry는 공통 규칙과 다른 결과를 만들 수 있다.
- 구체적인 수정 목록: shell·PowerShell individual runner를 6개로 맞추고 aggregate manifest에서 중복·누락을 machine-check한다. bind failure retry를 제거하고, sample별 exit status와 evidence schema를 수집한 뒤 하나라도 실패하면 aggregate를 실패시킨다. readiness, Redis, child process와 log cleanup을 bounded timeout으로 판정한다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-011`, `012`, `015`, `016`, `025`, `027`, `028`.
- Core·bindings·package 선행 조건: runner는 pinned package와 명시된 build directory만 사용해야 한다. 다른 cache나 package로 자동 전환하지 않는다.
- 완료 evidence: Linux와 Windows runner가 같은 6개 sample을 실행하고, 실패·timeout·partial 결과를 성공으로 표시하지 않으며, 각 process log·exit status·cleanup 결과를 보존한다.

### CPP-SAMPLE-IMP-005 — sample package·build cache·문서 version이 현재 CMake pin과 다름

- 상태: `gap`.
- 공통 spec/E2E 경로: 공통 sample 실행 규칙, `scripts/local-package/README.ko.md`, S0의 package/ABI contract와 CMake install/export 규칙.
- C++ 경로: `framework/languages/cpp/CMakeLists.txt`, `framework/languages/cpp/cmake/`, `framework/languages/cpp/samples/*/run_sample.sh`, sample README와 `framework/languages/cpp/scripts/verify_packaged_contract.sh`.
- 확인한 실제 동작과 기대 동작: sample README는 Framework `10.0.0`을 설명하지만 current CMake는 `zlink_cpp=11.1.0`, Core `11.1.0`을 pin한다. 2026-08-02 historical `build-v11-samples`는 Core `11.0.0`이었고, 2026-08-03 canonical `build`는 Release `11.1.0/11.1.0`이다. `sample-build-common.sh`는 shell runner에 명시적 dependency prefix를 제공하며 verifier는 current install manifest와 clean consumer를 통과한다. 기대 동작은 sample documentation, CMake pin, binary, local package, install tree와 clean consumer가 동일 version·configuration·ABI provenance를 가리키는 것이다.
- gap 판정 근거: shell runner provenance는 고정했지만 README version, PowerShell runner, Core exact hash와 full install/ABI matrix는 아직 정렬되지 않았다.
- 구체적인 수정 목록: sample documentation과 package policy를 contract review 뒤 CMake pin과 맞춘다. PowerShell runner에도 같은 pinned build provenance와 six-sample inventory를 연결하고, install/export consumer와 ABI matrix를 추가한다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-014`, `018`~`020`, `019`와 clean consumer/install verifier.
- Core·bindings·package 선행 조건: Core runtime과 C++ binding package의 version, hash, install path와 compiler/standard를 먼저 기록한다. Framework가 binding source나 stale package를 직접 참조하지 않는다.
- 완료 evidence: 6개 runner binary가 같은 pinned package를 가리키고, clean Release/Debug·compiler standard matrix와 install/export consumer, symbol/ABI 검증이 통과하며 `spot_handle.hpp` manifest 판정도 exact contract와 일치한다.

## 16. `CPP-SAMPLE-E2E-IMP-*` sample runner·process evidence gap

### CPP-SAMPLE-E2E-IMP-001 — client-visible 결과와 role-server evidence를 한 process 실행에서 완료하지 못함

- 상태: `gap`, 현재 process blocker.
- 공통 spec/E2E 경로: 각 공통 sample 문서의 업무 흐름·Client self-check·smoke 실행과 `framework/doc/framework/common/e2e/`의 routing, lifecycle, failure scenario.
- C++ 경로: `samples/TicTacToe/Client/tictactoe_client_scenario.hpp`, 각 sample의 `Client/*_scenario.hpp`, 각 role `main.cpp`의 self-check/evidence handler, `framework/languages/cpp/samples/run_samples.sh`.
- 확인한 실제 동작과 기대 동작: pinned `TicTacToe` 실행은 preflight 3/3, `PASS TicTacToe.Cpp`와 full client/server self-check를 통과했다. 이전 service-wire decode failure는 `mesh_node_host_service.cpp` inbound budget 경계 수정으로 해소됐다. 기대 동작은 client가 public HTTP/STREAM API로 role server를 호출하고, 같은 run에서 server-side evidence가 상태 전이, owner, generation, callback exactly-once, cleanup과 terminal reason을 검증하는 것이다.
- gap 판정 근거: 한 sample의 process PASS는 나머지 5개 sample과 common E2E ID의 process evidence를 증명하지 않는다. six-sample aggregate PASS 결과가 없다.
- 구체적인 수정 목록: S0 package/runtime blocker를 먼저 닫고 6개 sample별 process sequence와 evidence contract를 고정한다. client에 private status query나 raw frame을 추가하지 않으며, role server의 public sample HTTP/typed message evidence 경계를 먼저 확인한다. process failure·timeout·child status·cleanup을 bounded log로 남긴다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-013`, `021`~`025`.
- Core·bindings·package 선행 조건: S0 `CPP-IMP-*`, `CPP-E2E-IMP-*`와 package verifier가 먼저 닫혀야 한다. Core recovery나 Framework internal runtime을 sample client가 직접 호출하지 않는다.
- 완료 evidence: 6개 sample role server가 readiness를 통과하고 client-visible response/push와 server evidence가 같은 run id로 연결된다. owner·generation·terminal reason·callback count·cleanup을 직접 assertion하며 aggregate가 bounded timeout 안에 통과한다.

### CPP-SAMPLE-E2E-IMP-002 — sample 이름과 common E2E scenario ID를 완료로 혼동할 수 있음

- 상태: `test gap`, `contract 선행`.
- 공통 spec/E2E 경로: `framework/doc/framework/common/e2e/README.ko.md`, `config-*.ko.md` 전체와 6개 공통 sample 문서.
- C++ 경로: `framework/languages/cpp/e2e/`의 12 config feature-map, `run_e2e_all.sh`, `framework/languages/cpp/samples/`의 6개 runner와 CMake CTest 등록.
- 확인한 실제 동작과 기대 동작: C++ E2E aggregate는 Framework scenario ID를 관리하고 sample aggregate는 sample 이름만 관리한다. 현재 6개 sample과 12 config를 하나의 completion count로 연결하는 inventory가 없고 `ZoneWorld`는 C++ sample 범위에 없다. 기대 동작은 두 namespace를 분리하되, 각 C++ 대상 scenario ID가 어느 feature-map·selector·role server·client assertion으로 검증되는지 추적하는 것이다.
- gap 판정 근거: source file 존재나 sample 이름은 common E2E ID의 process evidence가 아니다. `부분`, `diagnostic_only`, `N/A`, historical 항목을 성공 수에 포함하면 누락을 숨긴다.
- 구체적인 수정 목록: sample inventory와 Framework E2E inventory를 분리한 manifest를 만들고, C++ 요구 scenario만 exact ID로 연결한다. `ZoneWorld`는 N/A 이유를 유지한다. selector가 실제 client/role runner dispatch에 연결되는지 machine-check한다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-026`~`028`.
- Core·bindings·package 선행 조건: common E2E가 요구하는 Framework public contract와 package gate를 S0에서 먼저 닫는다. sample 이름만으로 새 API를 만들지 않는다.
- 완료 evidence: 6개 sample inventory와 C++ common E2E feature-map cross-reference가 모두 존재하고 각 row가 실제 runner, selector, role server와 client assertion으로 실행된다. N/A와 partial 항목은 aggregate 성공에서 제외된다.

## 17. S1의 Core·bindings·package 선행 조건

| 영역 | 현재 사실 | S1 완료 전 조건 |
|---|---|---|
| C++ Framework public contract | S0 `CPP-IMP-*`와 `CPP-E2E-IMP-*`가 미완료다. | sample에 private helper, raw frame, internal ABI를 추가하지 않고 S0 checklist를 먼저 닫는다. |
| C++ runtime test | bounded host shutdown regression과 latest non-sample CTest 49/49, worker/admission 100회가 통과했다. | current sample smoke와 common E2E가 runtime gate를 대신하지 않도록 별도 evidence를 유지한다. |
| package install/export | verifier current manifest와 clean consumer가 통과했다. | HTTP ownership, exact ABI/provenance와 source-tree negative boundary를 추가로 확인한다. |
| local package version | historical shell sample runner는 CMake 11.1.0/11.0.0 pin의 `build-v11-samples`를 사용했고, current canonical build는 11.1.0/11.1.0이다. | PowerShell/aggregate와 documentation/version/hash를 current provenance로 정렬한다. |
| Core·bindings handoff | clean provenance가 current evidence로 확정되지 않았다. | Core C ABI, binding raw operation, Framework private ABI를 sample 업무 경로에서 사용하지 않는다. |
| common sample contract | 공통 sample 문서에 기존 사용자 변경이 있다. | C++ sample이 다른 언어 source를 계약 근거로 사용하지 않고 common 문서 review 결과를 기준으로 한다. |

Core 또는 bindings 변경이 필요한 경우 S1에서 임의로 수정하지 않고 해당 모듈의 ledger와 package
handoff를 먼저 완료한다. Framework sample은 공개 package를 통해서만 Core·bindings 기능을 사용한다.

## 18. S1 작업 순서

S1은 S0 완료 checklist를 통과한 뒤에만 G2부터 진행한다. G0와 G1은 S0와 연결된 선행 확인이며,
G2~G8이 실제 sample 작업이다.

| 단계 | 작업 | 완료 gate |
|---|---|---|
| G0 | working tree와 공통 sample 문서 기준을 보존하고 각 실제 단계 직후 progress log를 추가한다. | 사용자 변경 보존, source/public header/test/runner/formal spec 미수정 |
| G1 | 이 문서 1~10장의 S0 public contract, runtime, package/ABI와 common E2E gap을 닫는다. | S0 checklist와 current evidence가 complete |
| G2 | 6개 sample message inventory를 공통 문서와 exact diff하고 internal/test/evidence-only 범위를 분리한다. | `CPP-SAMPLE-IMP-001`과 contract 선행 항목 종료 |
| G3 | codec, ownership, session binding, Actor/Spot routing과 Domain/Application/Infrastructure call path를 고정한다. | raw/private/connection identity 위반 0 |
| G4 | CMake target, install/export와 package version을 고정하고 clean consumer를 통과시킨다. | `CPP-SAMPLE-IMP-005` 종료 |
| G5 | shell·PowerShell individual/aggregate runner의 inventory, readiness, Redis, marker, evidence와 cleanup을 정렬한다. | `CPP-SAMPLE-IMP-004` 종료 |
| G6 | 6개 sample을 individual로 실행한 뒤 aggregate `all`을 실행한다. 실패 sample을 성공으로 계산하지 않는다. | `CPP-SAMPLE-E2E-IMP-001` 종료 |
| G7 | static contract, unit/integration, package consumer, process E2E와 CI path filter를 재검증한다. | `CPP-SAMPLE-TEST-*`와 `CPP-SAMPLE-REG-*` 종료 |
| G8 | `gpt-5.6-sol`의 `xhigh` reasoning level로 독립 review를 요청해 공통 message, 실제 call path, owner/generation, evidence, cleanup과 N/A 범위를 다시 확인한다. | unresolved S1 gap 0, S0와 S1 모두 완료 |

G2에서 공통 문서에 없는 기능을 발견하면 바로 C++ public API를 추가하지 않는다. 필요한 계약 변경은
`contract 선행`으로 남기고 review 결과를 기다린다.

## 19. S1 기존 회귀 test의 유지·변경·추가 목록

### 19.1 기존 test gap

#### CPP-SAMPLE-TEST-001 — 기존 sample parity test가 전체 common inventory를 비교하지 않음

- 상태: `test gap`.
- 공통 spec/E2E 경로: 12.1의 6개 sample 문서와 `framework/doc/framework/common/e2e/`.
- C++ 경로: `framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_sample_parity.cpp`.
- 확인한 실제 동작과 기대 동작: 현재 test는 6개 sample의 일부 packet name·field·source pattern과 layering을 검사하며 current build에서 통과했다. 전체 message set, JSON null/optional, serialized payload, process owner와 공통 문서의 모든 field는 비교하지 않는다. 기대 동작은 공통 inventory 변경이 C++ test failure로 드러나는 것이다.
- gap 판정 근거: narrow static test가 green이어도 `SetTypingReq`, `LeaveGameReq`, GameQuest MessagePack payload, ShoppingMall `StartOrderRes`와 같은 차이를 전체적으로 판정하지 못한다.
- 구체적인 수정 목록: 공통 sample inventory를 중복 선언하지 않는 manifest 또는 generated check로 연결하고 packet kind, field type, optional/null과 codec을 compile/runtime serialization test로 비교한다. source string search만으로 완료를 표시하지 않는다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-001`~`008`과 negative extra-message case.
- Core·bindings·package 선행 조건: installed public C++ header와 pinned package를 사용하며 Core private header를 include하지 않는다.
- 완료 evidence: 6개 inventory diff와 serialization fixture가 current package test에서 실패·복구를 직접 드러내고 packet kind assertion이 통과한다.

#### CPP-SAMPLE-TEST-002 — 기존 CTest sample gate가 process evidence와 aggregate completeness를 판정하지 않음

- 상태: `test gap`.
- 공통 spec/E2E 경로: `framework/doc/framework/common/sample/README.ko.md:375-449`와 6개 sample smoke 기준.
- C++ 경로: `framework/languages/cpp/CMakeLists.txt:2028-2060`, `framework/languages/cpp/samples/run_samples.sh`, `run_samples.ps1`, 각 runner.
- 확인한 실제 동작과 기대 동작: CTest sample smoke target은 6개 shell script를 등록한다. 2026-08-03 initial parallel full CTest는 50/55였고, 실패 sample 5개를 serial로 재실행하면 3/5가 통과한다. latest non-sample CTest 49/49에는 sample smoke가 포함되지 않는다. PowerShell aggregate는 2개만 실행한다. static 4/4 결과는 role server evidence, callback count, terminal reason, owner/generation, cleanup과 6개 aggregate를 직접 판정하지 않는다. 기대 동작은 sample별 process 결과와 aggregate completeness를 실제 실행으로 확인하는 것이다.
- gap 판정 근거: CTest 등록 수가 process 성공을 뜻하지 않으며 runner exit code만으로 client-visible/role evidence completeness를 보장하지 않는다.
- 구체적인 수정 목록: process gate를 sample별 result schema와 run id로 확장하고 aggregate가 선택 sample 전체 결과를 수집한다. bounded timeout, child status, Redis cleanup과 log path를 실패 assertion에 포함한다. sample source·common sample 문서·runner 변경이 CI에서 skip되지 않도록 path filter를 연결한다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-011`, `013`, `015`~`017`, `021`~`028`.
- Core·bindings·package 선행 조건: pinned package와 clean build를 사용하며 Core/bindings blocker를 sample result로 성공 처리하지 않는다.
- 완료 evidence: 6개 individual CTest와 aggregate selection/all runner가 bounded timeout 안에 client·role evidence·cleanup을 확인하고 CI에서 skip되지 않는다.

### 19.2 유지할 기존 test

- `test_cpp_framework_sample_parity`: static ownership/layering, Protobuf, DTO serializer, deferred join과 timeout rule assertion을 유지한다. 전체 inventory gate를 추가해도 private/raw route 금지 assertion을 삭제하지 않는다.
- `test_cpp_framework_layout_contract`: `Client`, `Shared`, server role 구조와 client/server link 경계를 유지한다. directory 존재를 process completion으로 해석하지 않는다.
- `test_cpp_framework_target_contract`: public target, sample target과 private API 금지를 유지한다.
- `test_cpp_framework_install_consumer`: clean install consumer를 유지하고 package verifier와 같은 header/target manifest를 비교한다.
- 각 sample domain/unit test와 CTest smoke: 상태 전이, timeout, retry와 cleanup assertion을 유지한다.

### 19.3 추가·변경할 regression ID

아래 ID는 S1 회귀 계획이며 구현 code와 test code에는 아직 추가하지 않았다.

| ID | 대상 | 완료 assertion |
|---|---|---|
| `CPP-SAMPLE-REG-001` | 6개 common message name inventory | 공통 문서와 C++ packet name one-to-one |
| `CPP-SAMPLE-REG-002` | JSON/Protobuf field type와 optional/null | `int64`, nullable string, optional omission과 serialized payload |
| `CPP-SAMPLE-REG-003` | TicTacToe `LeaveGameMsg` | one-way send, reply 없음, Entry Spot 복귀와 destroy |
| `CPP-SAMPLE-REG-004` | SupportChat `SetTypingMsg` | one-way send, typing push와 response 미대기 |
| `CPP-SAMPLE-REG-005` | DeliveryDispatch status | `occurredAtUnixMs`, optional courier/reason, attempt와 late decision |
| `CPP-SAMPLE-REG-006` | GameQuest action/payload | typed JSON object, `ClosePlayerQuestMsg`, playerId와 projection |
| `CPP-SAMPLE-REG-007` | ShoppingMall workflow | `StartOrderRes.state`, sourceCommandId, event order와 compensation |
| `CPP-SAMPLE-REG-008` | Bingo Protobuf | schema tag/cardinality, optional `lastDrawnNumber`, reward publish |
| `CPP-SAMPLE-REG-009` | Domain/Application/Infrastructure boundary | Domain이 Framework type와 route identity를 include하지 않음 |
| `CPP-SAMPLE-REG-010` | session reconnect routing | client가 connection id를 지정하지 않고 binding이 current target을 선택 |
| `CPP-SAMPLE-REG-011` | aggregate inventory | shell·PowerShell 6종 목록, selector와 duplicate 방지 |
| `CPP-SAMPLE-REG-012` | per-run resource cleanup | Redis, child process, temp config와 log cleanup |
| `CPP-SAMPLE-REG-013` | role-server evidence | client result와 owner/generation/terminal/cleanup evidence 연결 |
| `CPP-SAMPLE-REG-014` | package/version provenance | CMake pin, README, binary, local package와 clean consumer 일치 |
| `CPP-SAMPLE-REG-015` | shell/PowerShell parity | Windows와 Linux runner가 같은 sample 책임 수행 |
| `CPP-SAMPLE-REG-016` | no retry/false success | bind failure retry 금지, partial failure는 aggregate failure |
| `CPP-SAMPLE-REG-017` | public boundary | HTTP/STREAM public API만 사용하고 Core/private/raw 우회 없음 |
| `CPP-SAMPLE-REG-018` | clean sample build | Debug/Release, compiler standard matrix와 old cache 혼입 방지 |
| `CPP-SAMPLE-REG-019` | install/export consumer | public header, target, package config와 symbol visibility |
| `CPP-SAMPLE-REG-020` | ABI matrix | shared/static, C++ standard, package version과 compiler ABI |
| `CPP-SAMPLE-REG-021` | lifecycle evidence | owner, generation, actor/Spot join/leave/destroy 순서 |
| `CPP-SAMPLE-REG-022` | exactly-once terminal | callback count, terminal reason, duplicate completion 차단 |
| `CPP-SAMPLE-REG-023` | shutdown cleanup | in-flight operation, worker, stream close와 child process cleanup |
| `CPP-SAMPLE-REG-024` | reconnect/replay | session binding 변경 뒤 push와 event/projection replay |
| `CPP-SAMPLE-REG-025` | aggregate all result | 6개 모두 실행되고 result·log·cleanup status 수집 |
| `CPP-SAMPLE-REG-026` | sample/E2E manifest | sample 이름과 common E2E scenario ID를 별도로 추적 |
| `CPP-SAMPLE-REG-027` | selector dispatch | selector가 실제 client/role runner에 연결 |
| `CPP-SAMPLE-REG-028` | status exclusion | `부분`, `diagnostic_only`, `N/A`, historical을 성공 수에서 제외 |

`CPP-SAMPLE-REG-*`는 구현 완료를 뜻하지 않는다. G2~G8의 current test와 process evidence가
확인된 뒤에만 각 ID를 `충족`으로 변경한다.

## 20. S1 완료 판정 checklist

### S0 선행 gate

- [ ] 이 문서 1~10장의 public contract, exact C++ interface, runtime, common E2E, package와 CI
      gate가 모두 완료됐다.
- [ ] 14개 common E2E config와 374개 scenario ID가 C++ feature-map, selector, client dispatch와
      aggregate에서 추적되고 partial/source-only/historical 항목이 성공으로 계산되지 않는다.
- [ ] CMake package, install/export, local package, Core/bindings version과 clean consumer가 같은
      provenance로 통과한다.

### S1 sample contract와 call path

- [ ] 6개 sample의 message·field·transport inventory가 공통 문서와 exact하게 대응한다.
- [ ] `SetTypingMsg`, `LeaveGameMsg`, Delivery timestamp/null, GameQuest typed payload와
      ShoppingMall `OrderState` 차이가 해소됐다.
- [ ] extra message와 field가 internal-only 또는 test/evidence-only로 명시됐고 unresolved public
      extra가 없다.
- [ ] C++ value/reference/move ownership, optional lifetime, callback capture와 buffer lifetime가
      공통 계약을 위반하지 않는다.
- [ ] client와 Domain에 actor ref, node rid, session route, connection id, raw frame과 Core C ABI가
      없다.
- [ ] Domain/Application/Infrastructure와 role owner의 실제 production call path가 evidence로
      확인된다.

### S1 runner와 process E2E

- [ ] shell·PowerShell individual/aggregate runner가 같은 6개 inventory와 selector 의미를 제공한다.
- [ ] build directory와 package version이 명시되고 old Debug/Release cache가 혼입되지 않는다.
- [ ] 각 실행이 전용 Redis, readiness, role start, public client call, client self-check, server
      evidence, terminal reason, owner/generation, callback count와 cleanup을 확인한다.
- [ ] bind failure를 retry하지 않으며 한 sample failure가 aggregate 성공으로 계산되지 않는다.
- [ ] 6개 aggregate가 bounded timeout 안에 완료되고 각 sample log path와 process exit status를
      보존한다.

### S1 test·package·CI

- [ ] `CPP-SAMPLE-TEST-001`, `CPP-SAMPLE-TEST-002`가 닫혔다.
- [ ] `CPP-SAMPLE-REG-001`~`028` 결과가 current source와 current package 실행으로 증명된다.
- [ ] static parity green과 process E2E green이 별도 결과로 기록된다.
- [ ] install header, CMake target export, package config, ABI/symbol visibility와 clean consumer가
      통과한다.
- [ ] sample source, common sample doc, runner와 CMake path 변경이 CI에서 skip되지 않는다.
- [ ] 구현 code·public header·test·E2E runner·정식 spec을 수정하지 않았고 working tree의 사용자
      변경을 보존했다.
- [ ] S1 contract/runtime/process E2E/package review request와 S0+S1 최종 closure request에
      당시 OpenAI guide와 candidate 위험도에 맞는 실제 model·level·선택 근거가 기록됐다.
      reviewer를 바로 사용할 수 없으면 대기 상태와 독립적으로 진행한 작업이 구분되어 있다.
- [ ] S0와 S1의 unresolved gap, Core·bindings·package blocker가 0이다.

S0와 S1 checklist가 모두 완료될 때만 이 문서의 문서 상태를 `완료`로 바꾼다. 별도 sample ledger는
사용하지 않으며, 모든 후속 sample 작업·검증·progress log는 이 문서와 `log/` 아래 기록을 기준으로
진행한다.
