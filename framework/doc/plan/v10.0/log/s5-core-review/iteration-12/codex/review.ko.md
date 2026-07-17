# S5 Core 구현 리뷰 iteration 12 — Codex

## 1. Scope 확인

- 대상 commit: `7f9d3e3153e95917e3ad113c3f0fe4b193975445`
- 시작: 631 files, aggregate SHA-256 `539d94abe13a30064208dbf0ac254bfb8b0347242682bac485aff865b0efcce7`
- 종료: 631 files, aggregate SHA-256 `539d94abe13a30064208dbf0ac254bfb8b0347242682bac485aff865b0efcce7`
- 종료 시 대상 checkout의 scope diff와 `git status --short`: 출력 없음.
- aggregate는 scope의 `git ls-files` 결과에 대해 각 파일의 `sha256sum`을 파일명 순으로 정렬한 뒤 다시 `sha256sum`한 값이다.
- `core/doc/internals`는 hash 범위에만 포함했고 판정 근거와 수정 대상으로 사용하지 않았다.
- 검토 중 대상 checkout 파일은 수정하지 않았다. 지정된 review 디렉터리의 `progress.md`와 이 파일만 작성했다.
- 사용자 지시에 따라 이번 잔여 검토에서는 빌드·테스트를 실행하지 않았다. 실행 증거는 iteration 12 manifest §2의 일반 build 오류 0, CTest 85/85, 신규 idle-exit sweep 포함 기록을 사용했다.

## 2. 직전 finding 해소 판정

### iteration 11 병합 finding 5건

1. **S5-11-01 — 해소 안 됨.** `allocate_lifecycle_generation()`은 boot-relative clock 대신 epoch millisecond를 사용하고 process 안에서는 CAS로 강단조다(`mesh_runtime.cpp:82-103`). 그러나 상태는 여전히 process-local이고 입력 해상도는 millisecond이므로 별도 process가 같은 millisecond에 같은 RID로 재시작하면 같은 generation을 발급할 수 있다. wall clock rollback 뒤에는 새 수명이 더 낮은 값을 발급할 수도 있다. 이는 같은 RID의 새 수명을 generation으로 구분하고 higher generation만 기존 수명을 교체한다는 계약(`service/01-mesh-node.ko.md:212`, `:242-248`)을 보장하지 못한다. 기존 test도 한 process 안의 Spot 재생성만 검증한다(`test_mesh_lifecycle_contracts.cpp:209-261`).
2. **S5-11-02 — 관찰 가능한 timeout-task 누락은 해소, I2 책임 경계는 미해소.** operations erase 분모 8곳을 재확인했다. runtime terminal 3곳은 task를 캡처해 mutex 밖에서 cancel하고(`mesh_runtime.cpp:1073-1144`, `:1230-1275`), Actor terminal 3곳도 같은 순서를 지킨다(`mesh_actor_api.cpp:382-457`, `:1342-1501`). submission rollback 2곳은 아직 guard가 task를 소유한 pre-commit 경로라 guard destructor가 cancel한다(`mesh_node_api.cpp:91-130`, `mesh_messaging_api.cpp:157-201`). EFAULT/ENOMEM 반환에서는 operation을 erase하지 않아 task가 armed로 남는다(`mesh_actor_api.cpp:1443-1461`). 다만 직접 erase/capture 지식이 여섯 terminal 경로에 계속 분산된 설계 문제는 I2 finding으로 남긴다.
3. **S5-11-03 — 원래 registry replacement race는 해소됨.** handler 등록은 pin한 state를 expected state로 넘기고(`monitor_socket_api.cpp:31-45`), registry lock 아래 현재 entry와 expected state의 동일성 및 `unregistered`를 검사한다. 불일치하면 `ESHUTDOWN`이고 새 state를 만들지 않는다(`monitor_api.cpp:304-341`). open의 생성 경로는 expected `NULL`로 분리돼 있다(`monitor_socket_api.cpp:88-102`). 다만 task 등록 실패의 rollback 누락은 이 수정에서 확인한 별도 I1/I2 finding이다.
4. **S5-11-04 — 해소됨.** 실제 address collision만 `EADDRINUSE`로 고정하고, Windows system-category 오류는 `wsa_error_to_errno()`로 POSIX errno에 변환한다(`asio_tcp_acceptor_config.hpp:24-40`; 변환표 `err.cpp:201-314`).
5. **S5-11-05 — 해소됨.** 회귀 테스트는 100ms idle-exit 경계 주변 90~110ms를 25회 sweep하고, 매 task가 상한 안에 진입하며 정확히 한 번 관찰되는지 확인한다(`unittest_request_timeout_scheduler.cpp:38-50`, `:98-128`). 실행 결과는 manifest §2의 전체 CTest 85/85 통과 기록에 포함돼 있다.

### 출력 계약의 iteration 10 finding 8건

1. **S5-10-01 — 해소됨.** scheduler liveness 판정, thread 기동, task 삽입과 idle-exit commit이 같은 mutex로 직렬화되고 self-cancel도 firing thread를 기다리지 않는다(`request_timeout_scheduler_internal.cpp:95-104`, `:171-193`, `:197-221`). 신규 idle-exit sweep도 등록·통과했다.
2. **S5-10-02 — 해소 안 됨.** boot-relative anchor는 고쳤지만 S5-11-01의 same-ms cross-process와 clock rollback 반례가 남는다.
3. **S5-10-03 — 관찰 가능한 ABA·task retention은 해소됨.** full operation ID 검증, guard 인계, 모든 terminal/destroy 경로의 mutex 밖 cancel을 확인했다. I2의 분산 책임만 별도 finding이다.
4. **S5-10-04 — 원래 close/registration replacement race는 해소됨.** 다섯 reader pin과 expected-state 갱신을 확인했다. task 등록 실패 rollback은 별도 root-cause다.
5. **S5-10-05 — 해소됨.** local completion은 선예약 storage를 사용하고 remote join reply는 `flags_`를 wire submit에 전달하며 실패한 token은 retry 가능 상태로 돌아간다(`mesh_actor_api.cpp:1463-1518`).
6. **S5-10-06 — 해소됨.** 실제 충돌만 `EADDRINUSE`이고 Windows Winsock errno도 변환한다.
7. **S5-10-07 — 해소됨.** stopwatch microsecond 단위에 맞춘 상한을 유지한다(`unittest_request_timeout_scheduler.cpp:38-50`).
8. **S5-10-08 — 이번 판정 대상 아님.** 지시대로 internals를 판정 근거로 사용하지 않았다.

## 3. I1 — 계약 구현 일치

### Finding

- `[I1][high] core/src/runtime/services/mesh/mesh_runtime.cpp:82 — lifecycle generation 발급이 같은 RID의 process 재시작 수명을 확실히 구분하지 못함 — epoch millisecond와 process-local atomic 조합은 같은 millisecond에 시작한 별도 process에 같은 값을 발급하고 wall-clock rollback 뒤 더 낮은 값을 발급할 수 있다. 중복 RID/generation은 거부하고 higher generation만 교체하는 01-mesh-node §4·§5 계약 때문에 정상 재시작이 stale/duplicate lifecycle로 거부될 수 있다 — RID별 마지막 generation을 재시작 사이에 보존하는 durable 발급자 또는 순서와 유일성을 함께 보장하는 authority를 사용하고, 별도 process same-ms restart와 clock rollback contract test를 추가한다.`
- `[I1][medium] core/src/api/monitoring/monitor_api.cpp:344 — socket monitor handler 등록 실패가 원자적으로 rollback되지 않고 C API 밖으로 allocation 예외를 전파할 수 있음 — setter는 handler·userdata를 먼저 publish한 뒤 periodic task를 등록한다(:344-357). runtime 종료 시 `ETERM`을 반환하거나 task 등록이 실패해도 handler를 지우지 않아 다음 등록은 `EBUSY`가 된다(`monitor_socket_api.cpp:31-40`). 또한 `add_periodic_task()`의 map/multimap allocation은 예외를 던질 수 있지만(`service_control_runtime.cpp:77-96`) 공개 handler API까지 catch가 없다 — fallible task storage를 먼저 예약한 뒤 registry state를 한 번에 commit하고, 모든 실패에서 handler/userdata/task ID를 원상복구하며 `std::bad_alloc`을 `ZLINK_HANDLER_INTERNAL_ERROR`/`ENOMEM`으로 변환한다.`

### Evidence

- 공통 spec 8개, service spec 5개, socket spec 9개와 영문 대응본, modular public header, 구현 entry point를 정적 대조했다. `core/doc/internals`는 제외했다.
- operation ID의 high 값과 node/Spot lifecycle generation, admission의 duplicate/higher-generation 분기를 caller와 sink까지 추적했다. process-local test는 allocator의 cross-process 계약을 증명하지 않는다.
- monitor expected-state check는 close와 registry identity race를 닫지만, registry update 뒤 scheduler 등록이라는 두 단계 commit은 실패 sink를 갖는다. 이 finding은 S5-11-03을 재개방한 것이 아니라 수정된 함수에서 확인한 별도 error-atomicity family다.
- errno 변환 helper와 public result mapper를 대조했다. Windows acceptor는 `WSAEACCES` 등 system error를 POSIX errno로 바꾸고 bind mapper는 실제 `EADDRINUSE`만 `ZLINK_BIND_ADDR_IN_USE`로 분류한다(`bind_result_internal.hpp:12-29`).
- package metadata는 CMake 10.0.0/SOVERSION 10(`core/CMakeLists.txt:11`, `:1368-1370`), Debian 10.0.0 및 `libzlink10`(`packaging/debian/changelog:1-5`, `zlink.dsc:1-5`, `control:31-57`), RPM 10.0.0/`libzlink10`(`packaging/redhat/zlink.spec:11-14`), NuGet 10.0.0과 `10_0_0` library 이름(`packaging/nuget/package.config:1-4`, `package.nuspec:7-10`, `package.targets:29-35`)으로 일치했다.
- 실행 증거는 coordinator manifest §2만 사용했다: 일반 build 오류 0, CTest 85/85, 신규 idle-exit sweep 포함. sanitizer·공개 API surface gate·package 실물 생성은 종료 뒤 coordinator gate이므로 실행하지 않았다.

### Verdict

**NOT CLEAN** — blocker 0, high 1, medium 1.

## 4. I2 — POSD·DDD

### Finding

- `[I2][medium] core/src/runtime/services/mesh/mesh_runtime.cpp:1095 — pending operation의 erase/task 인계/cancel 책임이 공통 owner에 모이지 않음 — runtime terminal 세 곳과 Actor terminal 세 곳이 모두 timeout task capture와 직접 erase를 반복하고, submission rollback 두 곳은 별도 guard 규칙에 의존한다. 바로 이 분산이 iteration 11의 Actor 세 경로 누락을 만들었고 수정 뒤에도 새 terminal 경로가 같은 invariant를 빠뜨릴 구조다 — `detach_pending_operation_locked()`처럼 operation identity 검증, task 인계와 erase를 소유하는 한 깊은 primitive를 두고 각 service는 terminal payload와 state commit만 제공하며 cancel은 공통 caller가 mutex 밖에서 수행한다.`
- `[I2][medium] core/src/api/monitoring/monitor_api.cpp:344 — monitor registry mutation과 dispatch task 확보가 시간 순서로 분해됨 — expected-state identity는 registry 모듈이 확인하지만 실제 handler 활성화에 필요한 scheduler resource는 state를 publish한 뒤 확보해 실패 시 partial state가 외부에 남는다. 이는 I1 monitor error-atomicity finding과 같은 root-cause family다 — registry와 scheduler 준비를 하나의 transaction으로 감싸 fallible 준비, identity 재검증, state commit 순서로 책임을 하향 이동한다.`

### Evidence

- operation erase 분모는 정확히 8곳(runtime 3, submission rollback 2, Actor 3)이며 공통 detach primitive는 없다. 현재 관찰 가능한 cancel 누락은 없지만 lifecycle invariant가 여섯 terminal caller에 노출돼 있다.
- monitor의 expected-state 변경은 lookup identity를 올바르게 숨겼다. 반면 handler publication과 task ID publication은 같은 함수에서도 rollback 계약 없이 분리돼 있어 호출자가 실패 뒤 handle 상태를 복구할 방법이 없다.
- MeshNode, Spot, Actor, STREAM session의 public handle·strong child·claim 경계에서는 위 두 family 외의 blocker/high/medium 책임 경계 위반을 찾지 못했다.

### Verdict

**NOT CLEAN** — blocker 0, high 0, medium 2.

## 5. I3 — 정리 완결성

### Finding

없음.

### Evidence

- `core/src`의 `.cpp` 174개와 `core/CMakeLists.txt`의 literal source 174개가 일치하며 누락 source가 없다.
- unit/integration test CMake는 등록 목록과 `zlink_warn_unregistered_tests()` 검사를 유지하고, scheduler regression target도 unit test 목록과 CTest에 등록돼 있다(`tests/unittest/CMakeLists.txt:4-39`, `:75-78`; `tests/CMakeLists.txt:15-38`, `:325-372`, `:903`).
- removed identifier contract, public modular header, build target와 compatibility 검색에서 blocker/high/medium dead declaration·orphan test·stale target을 찾지 못했다. internals 문서는 제외했다.
- coordinator manifest §2의 CTest 85/85는 등록된 최신 test lane이 실행됐다는 증거로 사용했다.

### Verdict

**CLEAN** — blocker 0, high 0, medium 0.

## 6. Low finding 목록

없음.

## 7. Known risk 4건 판정

1. **TSAN auto-HWM lock-order — 추적 유지, 새 확정 static finding 없음.** context recalc는 `_slot_sync`를 잡고 socket plan을 준비하며(`ctx_auto_hwm_recalc.cpp:80-116`), plan 준비는 socket monitor sync를 잡는다(`socket_base.cpp:214-238`). 반대 순서의 확정 경로는 찾지 못했다. sanitizer는 coordinator 종료 gate이므로 여기서 닫지 않는다.
2. **raw command mailbox ypipe — source상 직렬화 확인, sanitizer 전 추적 유지.** `_cpipe.write/flush`, read, `check_read`는 `_sync` 아래 있다(`mailbox.cpp:39-57`, `:64-98`, `:138-167`). 정적 반례는 없다.
3. **raw socket teardown 관찰 — 추적 유지, 새 확정 static finding 없음.** public API inflight/closing bit와 callback deferred-close handoff는 `socket_lifecycle_runtime.cpp:20-149`, async mailbox quiesce와 mailbox reference 경계는 `:210-373`에 있다. Context는 reaper 완료와 빈 socket registry를 확인한다(`ctx.cpp:147-170`). 동적 검증은 이번 지시 범위에서 실행하지 않았다.
4. **`ctx_term` linger — 계약 일치, finding 아님.** spec은 모든 socket이 닫힐 때까지 term이 block될 수 있다고 명시한다(`01-context.ko.md:123-144`). 구현도 shutdown 뒤 reaper 완료와 빈 registry를 기다린다(`ctx.cpp:147-170`).

CORE REVIEW NOT CLEAN
