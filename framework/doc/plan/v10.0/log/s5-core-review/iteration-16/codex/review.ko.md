# S5 Core 구현 리뷰 — iteration 16 — R1 Codex

## 1. Scope 확인

- 대상 commit: `1f247af7ae3946b74945ff96aa1462d1a984a3ac` (`core(control): resolve S5 iteration-15 finding`).
- 시작 snapshot: **632 files**, aggregate SHA-256 `398ee290bd39d1fb2070b9585cfb86bf63646f2407600c68ca26a4070e7fa993`.
- 종료 snapshot: **632 files**, aggregate SHA-256 `398ee290bd39d1fb2070b9585cfb86bf63646f2407600c68ca26a4070e7fa993`.
- 시작·종료 모두 checkout의 `git status --short`는 비어 있었다. 검토 checkout의 파일은 수정하지 않았다.
- prompt와 manifest가 적은 631개와 달리 실제 scope는 632개다. 직전 candidate의 631개에 `core/tests/unittest/unittest_service_control_runtime.cpp`가 추가된 결과이며, 제시된 aggregate는 실제 632개 집합과 정확히 일치하므로 검토 대상 자체의 모호성은 없다.
- build·테스트·sanitizer·package 생성은 실행하지 않았다. 실행 증거는 coordinator manifest §2의 일반 build 오류 0 및 CTest **86/86**만 사용했다.
- `core/doc/internals`는 snapshot 계산에만 포함하고 판정 근거나 수정 대상으로 사용하지 않았다.

## 2. 우선 검증

### 2.1 iteration 15 병합 finding 2건

1. **S5-15-01 — 해소.**
   - 각 callback은 `service_control_runtime.cpp:270-279`에서 `std::bad_alloc`을 개별 봉인한다. task는 호출 전에 재예약되고 실패한 tick만 폐기되며, 공통 epilogue(`:281-287`)가 active ID를 해제하고 broadcast하므로 이전의 영구 대기 반례가 닫혔다.
   - 최후 방어 catch는 `:161-176`에서 `_stopping=true`, `_active_task_id=0`, broadcast를 같은 critical section에서 commit한다. 따라서 `remove_task`의 대기(`:114-135`)와 context teardown의 task 제거가 죽은 worker의 stale active ID에 머물지 않고, 죽은 worker에 새 task도 수락되지 않는다.
   - 신규 `unittest_service_control_runtime.cpp:18-23,48-78`은 첫 tick의 bad_alloc 뒤 3 tick 관찰, `remove_task` 반환, `zlink_ctx_term` 종료를 검증하며 `core/tests/unittest/CMakeLists.txt:21`에 등록됐다. coordinator manifest §2는 추가 후 전체 CTest 86/86을 기록한다.
2. **S5-15-02 — 해소.** `schedule_task_locked`는 `service_control_runtime.cpp:179-193`에서 add 시 확보한 multimap node를 extract/rekey/move-insert하고, 도달 불가 fallback 대신 `zlink_assert(!cached_node.empty())`로 invariant를 고정한다. wakeup과 periodic 경로는 각각 선행 deschedule/extract를 수행하고 add는 최초 node를 직접 삽입하므로 caller 계약과 일치하며 steady-state 무할당 계약을 훼손하지 않는다.

### 2.2 iteration 10 finding 8건 재확인

1. **S5-10-01 scheduler lost-wakeup — 해소 유지.** `request_timeout_scheduler_internal.cpp:95-104,171-193`에서 liveness 판정·worker 기동·task 삽입이 동일 mutex에 있고, task publish 뒤 thread 기동 실패 rollback 순서가 보존된다. firing thread의 self-cancel은 `:197-221`에서 대기하지 않는다. 새 반례 없음.
2. **S5-10-02 lifecycle generation — 해소 유지.** `mesh_runtime.cpp:82-107`은 epoch microseconds wall-clock anchor를 사용한다. Core가 durable 순서 상태를 소유해야 한다는 계약은 spec에 없고, 동일 RID의 duplicate/stale 판정은 `01-mesh-node.ko.md:242-248`의 admission 규칙으로 표면화된다. 이전 해소 판정을 뒤집을 새 반례 없음.
3. **S5-10-03 timeout task 소유 — 해소 유지.** `pending_operation_t`가 task를 소유하고 `detach_pending_operation_locked`(`mesh_runtime.cpp:1015-1021`)가 terminal detach를 집중시킨다. runtime terminal 경로(`:1086-1155,1241-1285`)와 actor terminal 경로(`mesh_actor_api.cpp:382-456,1413-1497`)는 mutex 밖에서 cancel한다. 잔여 직접 erase는 primitive 내부 및 commit 전 submission rollback뿐이며 full operation ID 검증을 우회하는 ABA 반례가 없다.
4. **S5-10-04 monitor registry pin — 해소 유지.** `monitor_api.cpp:86-135`의 unregister/finalizer와 `:138-229,278-302,344-390`의 handler·recv model·close/registration 진입점은 pin 획득·drain 계약을 공유한다. reader 누락, double delete, self-close drain deadlock의 새 반례 없음.
5. **S5-10-05 join reply flags — 해소 유지.** local completion의 선예약 수락 경로와 wire 경로를 재대조했다. `mesh_wire.cpp:476-504`가 flags를 wire submit에 전달하며 `04-actor` §3의 관찰 가능한 reply flags 계약을 충족한다.
6. **S5-10-06 acceptor errno — 해소 유지.** `asio_tcp_acceptor_config.hpp:29-40`의 mapper는 address-in-use만 `EADDRINUSE`로 변환하고 open/option/bind/listen 실패 지점(`:52-109`)의 원인을 보존한다.
7. **S5-10-07 테스트 단위 — 해소 유지.** `unittest_request_timeout_scheduler.cpp:38-50`의 stopwatch 상한은 microseconds 단위와 일치한다.
8. **S5-10-08 — 이번 판정 대상 아님.** S5-11 이관 항목이고 `core/doc/internals` 제외 규칙을 적용했다.

## 3. I1 계약 구현 일치

### Finding

없음.

### Evidence

- `core/doc/spec/core`의 공통 8개, service 5개, socket 9개 계약 묶음과 한·영 대응 문서 전체를 header/source의 반환값, errno, ownership, 수명 및 동시성 경로와 정적 대조했다.
- 이번 수정은 callback bad_alloc을 worker 밖으로 누출하지 않고 task의 다음 주기 의미를 유지하며, 모든 종료 경로에서 active-ID 대기자를 깨운다. 공개 context control API의 오류 봉인과 teardown 계약에 반하는 경로를 찾지 못했다.
- iteration 10 종결 항목의 scheduler, generation, timeout ownership, monitor pin, join flags, acceptor errno 경로에도 새 반례가 없다.
- 공개 surface 검사는 `core/tests/CMakeLists.txt:915-923`, 누락 테스트 검사는 `:903` 및 `core/tests/unittest/CMakeLists.txt:78`에 계속 등록되어 있다. 실행은 하지 않았고 coordinator manifest의 build 오류 0·CTest 86/86만 보조 증거로 사용했다.
- package metadata는 `core/CMakeLists.txt:11`의 10.0.0, `:1370`의 SOVERSION 10, Debian changelog/dsc, Red Hat spec, NuGet config/nuspec의 10.0.0이 일치한다.

### Verdict

**CLEAN** — blocker/high/medium finding 0건.

## 4. I2 POSD·DDD

### Finding

없음.

### Evidence

- callback 예외 정책과 worker terminal lifecycle은 `service_control_runtime_t` 안에서 흡수된다. caller가 scheduler node, active-ID 정리 순서 또는 예외 복구를 알아야 하지 않으며 복잡성이 상위 context API로 누출되지 않는다.
- cached multimap node의 할당·extract·rekey·재삽입과 rollback은 control runtime 내부에 봉인되어 깊은 모듈 경계를 유지한다.
- MeshNode pending-operation detach, Actor terminal completion, wire reply 전달 및 monitor registry pin 경계를 다시 추적했다. MeshNode·Spot·Actor·session 간에 transport/registry/lifecycle 책임이 새로 역류하거나 병렬 추상화가 생긴 정황이 없다.

### Verdict

**CLEAN** — blocker/high/medium finding 0건.

## 5. I3 정리 완결성

### Finding

없음.

### Evidence

- 이번에 추가된 `unittest_service_control_runtime` source는 unittest CMake 목록에 등록되어 있으며, 구현 수정과 회귀 검증이 함께 추적된다.
- scope의 source/CMake inventory, 선언·정의·호출, 테스트 target 및 unregistered-test gate를 정적 대조했다. orphan source, 죽은 선언, 빠진 build target 또는 RouteMesh 이전 호환 잔재로 인해 관찰 가능한 계약이나 artifact가 어긋나는 사례를 찾지 못했다.
- core spec 한·영 파일 대응과 Debian/Red Hat/NuGet 버전 및 SOVERSION 정적 메타데이터가 일치한다.

### Verdict

**CLEAN** — blocker/high/medium finding 0건.

## 6. Low finding

- `[artifact][low] iteration-16/prompt.md:9,24 및 manifest.ko.md:7,10,13-14 — snapshot·실행 메타데이터 일부가 직전 iteration 값으로 남아 있다 — 실제 scope는 신규 unittest 포함 632개이고 aggregate는 그 집합과 일치하며, manifest는 S5/15·이전 checkout을 적고 prompt는 CTest 85/85를 적지만 manifest §2의 실제 기록은 86/86이다 — scope 파일 수를 632로, stage/checkout을 iteration 16 candidate로, prompt의 suite 수를 86/86으로 갱신하라.`

## 7. Known risk 4건 판정

1. **TSAN auto-HWM lock-order — 추적 유지, 새 finding 없음.** `ctx_auto_hwm_recalc.cpp:50,80-116,125`의 `_slot_sync` 구간과 `socket_base.cpp`의 socket/monitor 관련 동기화 경로를 대조했으며 반대 순서 중첩의 정적 반례를 찾지 못했다. 동적 TSAN 판정은 coordinator 종료 gate 범위다.
2. **raw command mailbox ypipe — 추적 유지, 새 finding 없음.** `mailbox.cpp:39-57,64-98,138-167`의 send/recv/reschedule/detach가 `_sync`와 signal 상태 아래 직렬화된다. raw command 수명 또는 ypipe 경쟁의 새 정적 반례가 없다.
3. **raw socket teardown 관찰 — 추적 유지, 새 finding 없음.** `socket_lifecycle_runtime.cpp`의 close/reaper coordinator와 `ctx.cpp:147-170`의 reaper 완료 대기를 대조했다. raw socket이 teardown 관찰을 우회해 남는 새 경로를 찾지 못했다.
4. **ctx_term linger — 계약 일치, 추적 유지.** `01-context.ko.md:123-144`의 blocking/linger 설명과 `ctx.cpp:147-170`의 socket 종료·reaper 완료 대기가 일치한다. 무한 linger 가능성은 명시된 계약이며 새 구현 결함의 반례가 없다.

CORE REVIEW CLEAN
