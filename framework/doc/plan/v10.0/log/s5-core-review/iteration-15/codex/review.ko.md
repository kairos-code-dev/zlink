# S5 Core 구현 리뷰 iteration 15 — Codex

## 1. Scope 확인

- 대상 commit: `7b580a52062af34fb47dc5e8cae349b936d925ee`
- 시작: 631 files, aggregate SHA-256 `cf26306098cb19579b2759b3c82df89809ece3ddb5d4aebac23372da02eefdc5`
- 종료: 631 files, aggregate SHA-256 `cf26306098cb19579b2759b3c82df89809ece3ddb5d4aebac23372da02eefdc5`
- 시작과 종료 시 대상 checkout의 `git status --short`: 출력 없음.
- aggregate는 지정 scope의 `git ls-files` 결과를 `LC_ALL=C` 파일명 순으로 정렬하고 각 파일의 `sha256sum`을 만든 뒤 다시 `sha256sum`한 값이다.
- `core/doc/internals`는 hash 범위에만 포함했고 판정 근거와 수정 대상으로 사용하지 않았다.
- 검토 중 대상 checkout 파일은 수정하지 않았다. 산출물은 지정 review 디렉터리의 `progress.md`와 이 파일뿐이다.
- 절차 규정에 따라 build·테스트·sanitizer·package 생성은 실행하지 않았다. 실행 증거는 iteration 15 manifest §2의 일반 build 오류 0 및 CTest 85/85만 사용했다.

## 2. 직전 finding 해소 판정

### iteration 14 병합 finding 1건

1. **S5-14-01.1 cached schedule node — 요청된 재스케줄 allocation 반례는 해소됨.** `task_entry_t`가 multimap node handle을 소유한다(`service_control_runtime.hpp:39-57`). deschedule은 iterator를 `extract()`해 `cached_node`에 보존하고, schedule은 key를 바꾼 뒤 같은 container에 move-insert한다(`service_control_runtime.cpp:174-200`). 현재 wakeup과 loop caller는 schedule 전에 항상 기존 node를 extract하므로 steady-state 재삽입은 allocation-free다(`:138-154`, `:220-242`).
2. **S5-14-01.2 add transaction — 해소됨.** `_tasks.emplace`부터 최초 `_schedule.insert`와 `scheduled=true`까지 한 try 범위이고, 어느 allocation이 실패해도 생성된 task entry를 erase한 뒤 `ENOMEM`/0을 반환한다(`service_control_runtime.cpp:83-111`). 최초 schedule node가 성공한 뒤에는 throw 지점이 없으므로 catch 시 schedule orphan도 남지 않는다.
3. **S5-14-01.3 단일 due call과 active 의미 — 해소됨.** vector가 제거되고 pass당 stack의 `due_call_t` 하나만 사용한다(`service_control_runtime.cpp:202-242`). `_active_task_id`는 task 존재 확인·다음 tick 재예약과 같은 `_sync` 구간에서 callback 직전에 설정된다(`:215-265`). 정상 callback 반환 뒤 같은 ID를 해제하고 broadcast하며(`:267-275`), 외부 remove는 schedule/task entry를 제거한 뒤 active callback 종료를 기다리고 self-remove는 현재 worker를 인식해 기다리지 않는다(`:114-135`). 정상 실행에서 기존 대기 계약과 동등하다.
4. **S5-14-01.4 예외 barrier — C ABI barrier는 해소됐으나 `run()` 최후 방어에서 새 반례로 최종 해소 안 됨.** `zlink_ctx_set_ext()`와 `zlink_ctx_auto_hwm_recalculate()`는 `bad_alloc`을 `ENOMEM`과 config failure로 봉인한다(`context_api.cpp:149-165`, `:192-206`). 그러나 `run()`의 catch가 worker lifecycle cleanup 없이 예외만 삼킨다. 상세 반례는 I1/I2 finding에 기록한다.
5. **S5-14-01.5 node handle 소유권 — 해소됨.** `extract()` 뒤 node key 변경은 C++17 node-handle 계약 안에서 유효하고 같은 multimap으로 move-insert하므로 allocator가 일치한다. insert 뒤 handle은 empty이고 iterator는 새 위치를 가리킨다(`service_control_runtime.cpp:182-189`). remove는 scheduled node를 먼저 extract한 뒤 task entry를 erase하므로 entry의 `cached_node` destructor가 node를 정확히 한 번 해제한다(`:114-135`, `:192-200`). loop의 orphan schedule entry는 node handle로 만들지 않고 직접 erase한다(`:225-230`).

### 출력 계약의 iteration 10 finding 8건

1. **S5-10-01 scheduler lost-wakeup — 해소됨.** idle-exit의 `started=false` commit과 liveness 판정·thread 기동·task 삽입이 같은 mutex로 직렬화된다(`request_timeout_scheduler_internal.cpp:95-104`, `:171-193`). thread 생성은 map 삽입보다 먼저이고 self-cancel은 firing thread 자신을 기다리지 않는다(`:197-221`).
2. **S5-10-02 lifecycle generation — 해소됨.** allocator는 epoch microseconds를 앵커로 하고 process 안에서는 CAS로 강단조다(`mesh_runtime.cpp:82-107`). 정식 spec은 같은 RID의 duplicate/stale generation을 admission conflict로 거부하고 higher generation만 교체한다고 규정한다(`service/01-mesh-node.ko.md:242-248`). Core 발급자에게 durable cross-process storage를 요구하는 절은 없으므로 종결된 generation ruling을 재개방하지 않는다.
3. **S5-10-03 timeout task 소유 — 해소됨.** pending operation이 task를 소유하고 guard가 gate commit 전에 full operation ID를 확인해 인계한다(`mesh_runtime.hpp:197-207`, `mesh_messaging_api.cpp:179-201`). terminal 경로는 `detach_pending_operation_locked()`로 erase와 task 인계를 묶고 mutex 밖에서 cancel한다(`mesh_runtime.cpp:1015-1021`, `:1086-1155`, `:1241-1285`; `mesh_actor_api.cpp:382-456`, `:1413-1497`). 잔여 직접 erase는 primitive 내부 1곳과 pre-commit rollback 2곳뿐이다(`mesh_node_api.cpp:91-130`).
4. **S5-10-04 monitor registry pin — 해소됨.** registry reader는 RAII pin을 사용하고 unregister/finalizer는 pin drain 뒤 삭제한다(`monitor_api_internal.hpp:68-99`, `monitor_api.cpp:86-135`, `:183-229`, `:278-302`). handler publication부터 dispatch task ID 저장까지 같은 `dispatch_sync`를 사용해 immediate callback self-close도 완전한 task identity 뒤에 진행된다(`monitor_api.cpp:138-179`, `:344-390`).
5. **S5-10-05 join reply flags — 해소됨.** local completion은 선예약된 record를 splice하고 remote path는 public `flags_`를 `wire_submit_join_reply()`에 전달한다(`mesh_actor_api.cpp:1461-1505`). wire 함수는 flags를 최종 `send_data_message()`까지 전달한다(`mesh_wire.cpp:476-504`).
6. **S5-10-06 acceptor errno — 해소됨.** `address_in_use`만 `EADDRINUSE`로 변환하고 다른 system error는 실제 errno 또는 Winsock 변환값을 보존한다. open/reuse/bind/listen 네 실패 지점이 mapper를 사용한다(`asio_tcp_acceptor_config.hpp:24-40`, `:52-109`).
7. **S5-10-07 테스트 단위 — 해소됨.** stopwatch의 microsecond 반환 단위에 맞춰 상한을 `SETTLE_TIME * 20 * 1000`으로 계산한다(`unittest_request_timeout_scheduler.cpp:38-50`).
8. **S5-10-08 — 이번 판정 대상 아님.** 지시대로 internals를 근거 또는 수정 대상으로 사용하지 않았다.

## 3. I1 — 계약 구현 일치

### Finding

- `[I1][high] core/src/runtime/services/control/service_control_runtime.cpp:167 — S5-14-01을 수정 commit 7b580a520에서 재지적: worker의 최후 bad_alloc seal이 active-task lifecycle을 완료하지 않고 thread만 종료해 이후 remove와 context 종료가 영구 대기함 — 이전에 없던 구체적 반례는 periodic auto-HWM callback이다. loop는 callback 직전에 _active_task_id를 설정하고(:264) call.fn()을 실행하지만(:267), callback이 bad_alloc을 던지면 정상 epilogue의 ID 해제와 broadcast(:269-275)를 건너뛴다. 바깥 run() catch는 아무 cleanup 없이 반환한다(:167-171). 이 예외는 가상이 아니다. auto_hwm_recalc_task()는 periodic task callback이고(ctx_auto_hwm_recalc.cpp:11-26, :121-131), 재계산은 socket/plan vector 수집·reserve·push_back을 수행한다(:67-117). 따라서 allocation failure 뒤 worker는 종료됐지만 _active_task_id는 해당 task로 남는다. remove_task()는 task/schedule을 지운 뒤 active ID가 같으면 무기한 wait한다(service_control_runtime.cpp:128-135). zlink_ctx_term()의 synchronous delete는(ctx.cpp:147-170; heap_owner.hpp:8-11) destructor에서 runtime teardown보다 먼저 stop_auto_hwm_recalc_task()를 호출하고(ctx.cpp:89-95), 이 함수가 바로 remove_task()를 호출하므로(ctx_auto_hwm_recalc.cpp:29-38) socket registry가 이미 비어 있어도 context 종료가 반환하지 않는다. manifest의 85/85는 allocation-failure injection이 없는 정상 실행 증거여서 이 반례를 닫지 않는다 — 각 call.fn 실행에 try/catch와 무조건 실행되는 active-ID 해제·broadcast epilogue를 두고, bad_alloc task를 다음 tick에 재시도할지 제거할지를 scheduler 정책으로 명시한다. 외곽 최후 catch도 worker를 끝낼 경우 active ID를 해제하고 waiters를 깨우며 runtime을 더 이상 task를 받지 않는 상태로 원자적으로 전환해야 한다.`

### Evidence

- 이 항목은 S5-14-01의 scheduler allocation/liveness root-cause family를 새 ID로 분할하지 않고 재개방했다. 이전 반례는 container allocation이 C ABI를 탈출하거나 worker에서 `std::terminate`를 일으키는 경로였고, 수정 commit이 새로 추가한 empty catch가 process terminate를 막는 대신 active callback 종료 계약을 누락시킨 새 interleaving이다.
- `zlink_ctx_term()`은 모든 socket이 닫힐 때까지만 block될 수 있고 종료 뒤 context resource를 해제한다고 규정한다(`01-context.ko.md:123-144`). 위 경로는 socket 유무와 관계없이 scheduler의 stale active ID 때문에 무기한 block하므로 공개 lifecycle 계약 위반이다.
- 공통 spec 8개, service spec 5개, socket spec 9개와 영문 대응본, modular public header, errno mapper와 source entry point를 정적으로 대조했다. 위 family 외의 blocker/high/medium 계약 불일치는 찾지 못했다.
- public surface checker는 한국어/영문 C block, formal spec/header, removed identifier, package metadata와 export를 대조하며 CTest에 등록돼 있다(`tests/contract/check_public_surface.py:200-276`, `tests/CMakeLists.txt:910-923`). 이번 리뷰에서는 실행하지 않았고 manifest §2의 85/85만 실행 증거로 사용했다.
- package metadata는 CMake project 10.0.0과 SOVERSION 10(`core/CMakeLists.txt:11`, `:1369-1370`), Debian 10.0.0/`libzlink10`(`packaging/debian/changelog:1`, `zlink.dsc:5`, `control:34,57`), RPM 10.0.0(`packaging/redhat/zlink.spec:13`), NuGet 10.0.0/`10_0_0`(`packaging/nuget/package.config:4`, `package.nuspec:10`)으로 일치했다.

### Verdict

**NOT CLEAN** — blocker 0, high 1, medium 0.

## 4. I2 — POSD·DDD

### Finding

- `[I2][high] core/src/runtime/services/control/service_control_runtime.cpp:161 — S5-14-01과 같은 family: worker 예외 봉인 책임과 active-task 완료 책임이 run()과 loop()로 시간적으로 분리돼 예외 경로가 scheduler 불변식을 복구하지 않음 — 정상 경로만 loop가 active ID를 해제하고 최후 catch는 예외 종류만 알아 상태 cleanup을 하지 않는다. 그 결과 task callback의 내부 allocation 결정이 remove caller와 Context destructor의 무기한 대기로 누출된다 — callback invoke와 active-ID cleanup을 하나의 scheduler-owned execution primitive로 묶어 모든 반환·예외에서 같은 epilogue를 실행하고, 최후 worker 종료도 running/stopping 상태와 waiter notification을 한 critical section에서 commit한다.`

### Evidence

- cached node는 schedule allocation 정보를 task entry 안에 숨기고 wakeup/periodic caller를 무할당화했으며, add transaction도 두 container의 생성 책임을 한 모듈에 모았다. 따라서 요청된 구조 방향은 적절하고 남은 finding은 worker execution lifecycle의 예외 경계 하나다.
- Mesh operation은 task 인계와 erase를 `detach_pending_operation_locked()`에 모아 terminal caller가 container/timeout 결합 지식을 반복하지 않는다. MeshNode·Spot·Actor·STREAM session의 handle, strong child, claim과 transfer 책임 경계에서 이 scheduler family 외의 blocker/high/medium POSD·DDD finding을 찾지 못했다.

### Verdict

**NOT CLEAN** — blocker 0, high 1, medium 0.

## 5. I3 — 정리 완결성

### Finding

없음.

### Evidence

- `core/src`의 tracked `.cpp` 174개와 `core/CMakeLists.txt`의 literal source 174개가 일치하며 양쪽 차집합이 없다.
- 한국어/영문 spec은 25/25 pair다. unit/integration CMake는 unregistered-test 경고 gate를 유지하고 scheduler regression, monitor contract와 public surface contract target을 등록한다(`tests/unittest/CMakeLists.txt:19,77`; `tests/CMakeLists.txt:82,488-491,903-923`).
- `cached_node`는 deschedule/schedule/remove/loop에서 모두 사용되고 dead member가 아니다. 수정 범위와 전체 scope의 removed identifier, compatibility, 선언·정의·build target 정적 검색에서 blocker/high/medium 정리 finding을 찾지 못했다.
- allocation-failure worker 회귀 검증 누락은 I1/I2의 동일 root-cause family에 포함했고 별도 I3 ID로 분할하지 않았다.

### Verdict

**CLEAN** — blocker 0, high 0, medium 0.

## 6. Low finding 목록

없음.

## 7. Known risk 4건 판정

1. **TSAN auto-HWM lock-order — 추적 유지, 새 확정 static finding 없음.** context recalc는 `_slot_sync`를 보유한 채 socket plan을 준비하고(`ctx_auto_hwm_recalc.cpp:80-116`), plan 준비는 socket monitor sync를 취한다(`socket_base.cpp:214-238`). 반대 순서의 확정 경로는 찾지 못했다. sanitizer는 실행하지 않았다.
2. **raw command mailbox ypipe — source상 직렬화 확인, sanitizer 전 추적 유지.** `_cpipe.write/flush`, read와 `check_read`는 `_sync` 아래 있다(`mailbox.cpp:39-57`, `:64-98`, `:138-167`). 새 정적 반례는 없다.
3. **raw socket teardown 관찰 — 추적 유지, 새 확정 static finding 없음.** public API inflight/closing과 callback deferred-close gate는 `socket_lifecycle_runtime.cpp:20-100`, async mailbox quiesce와 mailbox refcount는 `:249-315`, `:351-373`에 있다. Context는 reaper 완료와 빈 socket registry를 확인한다(`ctx.cpp:147-170`). 동적 검증은 실행하지 않았다.
4. **`ctx_term` linger — 일반 계약은 일치하나 S5-14-01 예외 경로는 I1 finding.** 정상 구현은 shutdown 뒤 reaper 완료와 빈 registry를 기다리고(`ctx.cpp:147-170`), spec은 모든 socket이 닫힐 때까지 block될 수 있다고 명시한다(`01-context.ko.md:123-144`). 정상 linger 의미는 finding이 아니며, socket과 무관한 scheduler stale-active hang만 위 동일 family로 기록했다.

CORE REVIEW NOT CLEAN
