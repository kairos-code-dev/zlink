# S5 Core 구현 리뷰 iteration 14 — Codex

## 1. Scope 확인

- 대상 commit: `26a4cbb81118f7aac7ea4c620a0a7a0e6bbae121`
- 시작: 631 files, aggregate SHA-256 `ba0b6c526ee2193046b0a0e78b2e5ace6b5ba384fd23eff7e1f43b5566327969`
- 종료: 631 files, aggregate SHA-256 `ba0b6c526ee2193046b0a0e78b2e5ace6b5ba384fd23eff7e1f43b5566327969`
- 시작과 종료 시 대상 checkout의 `git status --short`: 출력 없음.
- aggregate는 지정 scope의 `git ls-files` 결과를 `LC_ALL=C` 파일명 순으로 정렬하고 각 파일의 `sha256sum`을 만든 뒤 다시 `sha256sum`한 값이다.
- `core/doc/internals`는 hash 범위에만 포함했고 판정 근거와 수정 대상으로 사용하지 않았다.
- 검토 중 대상 checkout 파일은 수정하지 않았다. 산출물은 지정 review 디렉터리의 `progress.md`와 이 파일뿐이다.
- 절차 규정에 따라 build·테스트·sanitizer·package 생성은 실행하지 않았다. 실행 증거는 iteration 14 manifest §2의 일반 build 오류 0 및 CTest 85/85만 사용했다.

## 2. 직전 finding 해소 판정

### iteration 13 병합 finding 1건

1. **S5-13-01a 등록 원자성 — 요청된 반례는 해소됨.** `set_monitor_handler_state()`는 handler publication부터 `add_periodic_task()` 반환과 `dispatch_task_id` 저장까지 `dispatch_sync`를 보유한다(`monitor_api.cpp:344-390`). task는 queued event를 먼저 꺼낼 수 있지만 handler를 읽기 전에 같은 lock을 취하므로 callback과 self-close finalizer는 task ID commit 뒤에만 진행된다(`:138-179`). 외부 close도 같은 lock 아래 callback 상태를 바꾼다(`:429-456`). setter의 lock 순서는 `dispatch_sync`→service runtime `_sync`이고, scheduler loop는 `_sync` 범위를 끝낸 뒤 callback을 호출한다(`service_control_runtime.cpp:187-246`). 반대 중첩은 없다.
2. **S5-13-01b add-time strong rollback — 요청된 `_schedule` 실패 반례는 해소됨.** `_tasks` 삽입 뒤 `schedule_task_locked()`의 `bad_alloc`을 잡아 삽입 iterator를 erase하고 `ENOMEM`/0을 반환한다(`service_control_runtime.cpp:92-105`). 다만 같은 allocating primitive의 다른 caller에서 새 반례가 확인돼 family 전체는 재개방한다. 상세는 I1/I2 finding에 기록한다.
3. **S5-13-01c 회귀 테스트 — 해소됨.** test는 monitor handler 등록 전 50ms 동안 event를 queue하고, 등록 직후 첫 callback에서 monitor를 self-close하며, `ZLINK_CLOSE_OK`와 stray tick 관찰 구간을 10회 반복한다(`test_monitor_socket_contract.cpp:1461-1533`). CMake target에 포함돼 있고(`tests/CMakeLists.txt:82`, `:488-491`), manifest §2의 CTest 85/85에 포함됐다.

### 출력 계약의 iteration 10 finding 8건

1. **S5-10-01 scheduler lost-wakeup — 해소됨.** idle-exit의 `started=false` commit과 liveness 판정·thread 기동·task 삽입이 같은 mutex로 직렬화된다(`request_timeout_scheduler_internal.cpp:95-104`, `:171-193`). thread 생성은 map 삽입보다 먼저이고, self-cancel은 firing thread 자신을 기다리지 않는다(`:197-221`).
2. **S5-10-02 lifecycle generation — 해소됨.** allocator는 epoch microseconds를 앵커로 하고 process 안에서는 CAS로 강단조다(`mesh_runtime.cpp:82-107`). 정식 spec은 같은 RID의 duplicate/stale generation을 admission conflict로 거부하고 higher generation만 교체한다고 규정한다(`service/01-mesh-node.ko.md:242-248`). Core 발급자에게 durable cross-process storage를 요구하는 절은 없으므로 종결된 generation ruling을 재개방하지 않는다.
3. **S5-10-03 timeout task 소유 — 해소됨.** pending operation이 task를 소유하고 guard가 gate commit 전에 full operation ID를 확인해 인계한다(`mesh_runtime.hpp:197-207`, `mesh_messaging_api.cpp:179-201`). terminal 경로는 `detach_pending_operation_locked()`로 erase와 task 인계를 묶고 mutex 밖에서 cancel한다(`mesh_runtime.cpp:1015-1021`, `:1086-1155`, `:1241-1285`; `mesh_actor_api.cpp:382-456`, `:1413-1497`). 잔여 직접 erase는 primitive 내부 1곳과 pre-commit rollback 2곳뿐이다(`mesh_node_api.cpp:91-130`).
4. **S5-10-04 monitor registry pin — 해소됨.** registry reader는 RAII pin을 사용하고 unregister/finalizer는 pin drain 뒤 삭제한다(`monitor_api_internal.hpp:68-99`, `monitor_api.cpp:86-135`, `:183-229`, `:278-302`). status, recv-model, handler 등록, close 검사와 monitor close의 state dereference entry가 pin 계약을 사용하며, callback self-close는 pin을 해제한 뒤 finalizer로 넘어간다.
5. **S5-10-05 join reply flags — 해소됨.** local completion은 선예약된 record를 splice하고, remote path는 public `flags_`를 `wire_submit_join_reply()`에 전달한다(`mesh_actor_api.cpp:1461-1505`). wire 함수는 그 flags를 최종 `send_data_message()`까지 전달하므로 DONTWAIT/SNDTIMEO 실패가 token retry 상태를 보존한다(`mesh_wire.cpp:476-504`).
6. **S5-10-06 acceptor errno — 해소됨.** `address_in_use`만 `EADDRINUSE`로 변환하고 다른 system error는 실제 errno 또는 Winsock 변환값을 보존한다. open/reuse/bind/listen 네 실패 지점이 이 mapper를 사용한다(`asio_tcp_acceptor_config.hpp:24-40`, `:52-109`).
7. **S5-10-07 테스트 단위 — 해소됨.** stopwatch의 microsecond 반환 단위에 맞춰 상한을 `SETTLE_TIME * 20 * 1000`으로 계산한다(`unittest_request_timeout_scheduler.cpp:38-50`).
8. **S5-10-08 — 이번 판정 대상 아님.** 지시대로 internals를 근거 또는 수정 대상으로 사용하지 않았다.

## 3. I1 — 계약 구현 일치

### Finding

- `[I1][high] core/src/runtime/services/control/service_control_runtime.cpp:92 — S5-13-01을 수정 commit 26a4cbb81에서 재지적: add-time의 두 번째 container 삽입만 봉인됐고 같은 scheduler의 다른 allocation 실패는 공개 C 호출 밖으로 예외를 내보내거나 worker thread에서 std::terminate를 유발함 — 이전에 없던 구체적 반례는 세 경로다. 첫째 `_tasks.insert()` 자체가 새 try 범위 밖이다(:92-97). auto-HWM은 이 함수를 catch 없이 호출하고(ctx_auto_hwm_recalc.cpp:16-27), runtime option 변경은 `ctx_t::set()`에서 이를 호출한 뒤 성공을 반환하며(ctx_options.cpp:105-107), `zlink_ctx_set()`/`zlink_ctx_set_ext()`도 예외 봉인이 없다(context_api.cpp:128-155). 따라서 허용된 runtime auto-HWM 설정이 allocation 실패 시 `zlink_config_result_t`가 아니라 C ABI 밖으로 `std::bad_alloc`을 전파할 수 있다. 둘째 기존 task의 `wakeup_task()`는 schedule entry를 지운 뒤 allocating `schedule_task_locked()`를 catch 없이 호출하고(:132-147), auto-HWM public option 경로가 이를 호출한다(ctx_auto_hwm_recalc.cpp:40-59). 셋째 scheduler loop는 due schedule node를 지운 뒤 매 tick 같은 helper로 재삽입하며(:192-214), thread entry `run()`에는 catch가 없어(:155-159) bad_alloc이 worker 밖으로 벗어나 process를 종료시킨다. manifest의 85/85는 allocation-failure injection이 없는 정상 실행 증거이므로 이 정적 반례를 닫지 않는다 — service runtime 안에서 모든 task/schedule container mutation을 하나의 non-throwing transaction으로 캡슐화하고, add/wakeup은 실패 시 이전 상태를 복원해 `ENOMEM`을 반환하며, periodic reschedule은 이미 할당된 schedule node 재사용 등 tick마다 allocation하지 않는 구조로 바꾼다. 최소한 thread entry와 모든 C 도달 caller에서 예외를 봉인하되, registered task가 조용히 사라지지 않도록 task lifecycle 결과도 명시한다.`

### Evidence

- 이 항목은 S5-13-01의 scheduler error-atomicity root-cause family를 새 ID로 분할하지 않고 재개방한 것이다. 이전 finding의 반례는 `_tasks` 성공 뒤 최초 `_schedule` 실패에 따른 partial entry였고, 이번 반례는 수정 commit 뒤에도 남은 첫 container 실패, wakeup 재삽입과 periodic worker 재삽입이다.
- `zlink_ctx_set()`은 runtime 중 auto-HWM profile과 policy 변경을 허용하고 result enum으로 성공/실패를 반환한다고 규정한다(`01-context.ko.md:175-201`). 공통 오류 규칙도 공개 함수의 주 제어 흐름을 result enum으로 반환한다고 정한다(`03-errors.ko.md:11-15`). C++ 예외나 process termination은 이 관찰 가능한 계약을 충족하지 않는다.
- 공통 spec 8개, service spec 5개, socket spec 9개와 영문 대응본, modular public header, errno mapper와 source entry point를 정적으로 대조했다. 위 family 외의 blocker/high/medium 계약 불일치는 찾지 못했다.
- public surface checker는 한국어/영문 C block, formal spec/header, removed identifier, package metadata와 export를 대조하며 CTest에 등록돼 있다(`tests/contract/check_public_surface.py:200-276`, `tests/CMakeLists.txt:910-923`). 이번 리뷰에서는 실행하지 않았고 manifest §2의 85/85만 실행 증거로 사용했다.
- package metadata는 CMake project 10.0.0과 SOVERSION 10(`core/CMakeLists.txt:11`, `:1369-1370`), Debian 10.0.0/`libzlink10`(`packaging/debian/changelog:1`, `zlink.dsc:5`, `control:34,57`), RPM 10.0.0(`packaging/redhat/zlink.spec:13`), NuGet 10.0.0/`10_0_0`(`packaging/nuget/package.config:4`, `package.nuspec:10`)으로 일치했다.

### Verdict

**NOT CLEAN** — blocker 0, high 1, medium 0.

## 4. I2 — POSD·DDD

### Finding

- `[I2][high] core/src/runtime/services/control/service_control_runtime.cpp:161 — S5-13-01과 같은 family: task lifecycle의 allocation·schedule rollback 정책이 scheduler module 내부에 완전히 숨겨지지 않아 caller와 worker가 서로 다른 실패 의미를 가짐 — monitor caller만 add 예외를 catch하지만 auto-HWM caller는 catch하지 않고, add는 부분 rollback을 하지만 wakeup과 periodic reschedule은 같은 helper 실패를 처리하지 않는다. 이 때문에 scheduler의 container 선택과 allocation 시점이 C API caller 및 process 생존 조건으로 누출된다 — task entry와 schedule slot을 한 lifecycle owner가 reserve/activate/reschedule/remove하도록 만들고, 공개 caller가 scheduler의 예외 정책을 알 필요가 없는 non-throwing interface와 일관된 rollback을 제공한다.`

### Evidence

- S5-13-01a 수정은 monitor handler state와 dispatch task identity의 publication을 `dispatch_sync` 아래 하나의 commit으로 묶어 monitor 쪽 시간적 분해를 해소했다. 남은 finding은 그 아래 scheduler module 자체의 실패 원자성 경계다.
- Mesh operation 쪽은 task 인계와 erase를 `detach_pending_operation_locked()`에 모아 terminal caller가 container/timeout 결합 지식을 반복하지 않는다. MeshNode·Spot·Actor·STREAM session의 handle, strong child, claim과 transfer 책임 경계에서 이 scheduler family 외의 blocker/high/medium POSD·DDD finding을 찾지 못했다.

### Verdict

**NOT CLEAN** — blocker 0, high 1, medium 0.

## 5. I3 — 정리 완결성

### Finding

없음.

### Evidence

- `core/src`의 tracked `.cpp` 174개와 `core/CMakeLists.txt`의 literal source 174개가 일치하며 양쪽 차집합이 없다.
- unit/integration CMake는 unregistered-test 경고 gate를 유지하고 scheduler regression, monitor contract와 public surface contract target을 등록한다(`tests/unittest/CMakeLists.txt:19,77`; `tests/CMakeLists.txt:82,488-491,903-923`). 신규 queued-event self-close test는 target의 `main()`에 실제 등록돼 있다(`test_monitor_socket_contract.cpp:1535-1553`).
- `operations.erase` 정적 분모, 새 monitor synchronization member와 scheduler rollback 코드의 선언/사용, removed identifier와 compatibility 검색에서 blocker/high/medium dead code·선언·test·build target finding을 찾지 못했다.
- scheduler의 잔여 exception-safety 검증 누락은 I1/I2의 동일 root-cause family에 포함했고 별도 I3 ID로 분할하지 않았다.

### Verdict

**CLEAN** — blocker 0, high 0, medium 0.

## 6. Low finding 목록

없음.

## 7. Known risk 4건 판정

1. **TSAN auto-HWM lock-order — 추적 유지, 새 확정 static finding 없음.** context recalc는 `_slot_sync`를 보유한 채 socket plan을 준비하고(`ctx_auto_hwm_recalc.cpp:80-116`), plan 준비는 socket monitor sync를 취한다(`socket_base.cpp:214-238`). 반대 순서의 확정 경로는 찾지 못했다. sanitizer는 실행하지 않았다.
2. **raw command mailbox ypipe — source상 직렬화 확인, sanitizer 전 추적 유지.** `_cpipe.write/flush`, read와 `check_read`는 `_sync` 아래 있다(`mailbox.cpp:39-57`, `:64-98`, `:138-167`). 새 정적 반례는 없다.
3. **raw socket teardown 관찰 — 추적 유지, 새 확정 static finding 없음.** public API inflight/closing과 callback deferred-close gate는 `socket_lifecycle_runtime.cpp:20-100`, async mailbox quiesce와 mailbox refcount는 `:210-315`, `:351-373`에 있다. Context는 reaper 완료와 빈 socket registry를 확인한다(`ctx.cpp:147-170`). 동적 검증은 실행하지 않았다.
4. **`ctx_term` linger — 계약 일치, finding 아님.** spec은 모든 socket이 닫힐 때까지 term이 block될 수 있다고 명시하고(`01-context.ko.md:123-144`), 구현은 shutdown 뒤 reaper 완료와 빈 registry를 기다린다(`ctx.cpp:147-170`).

CORE REVIEW NOT CLEAN
