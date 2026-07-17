# S5 Core 구현 리뷰 iteration 11 — Codex

## 1. Scope 확인

- 대상 commit: `c1c579ad14ed1ec10c50dad04c053a14ecd533ba`
- 시작: 631 files, aggregate SHA-256 `56a1b0c135e9357ee3da1666a45084239f9b4a195b5b4d86ee77b471b9a01305`
- 종료: 631 files, aggregate SHA-256 `56a1b0c135e9357ee3da1666a45084239f9b4a195b5b4d86ee77b471b9a01305`
- 종료 시 검토 checkout `git status --short`: 출력 없음
- aggregate 명령: scope의 `git ls-files` 출력을 `xargs sha256sum | sha256sum`으로 집계했다.
- `core/doc/internals`는 파일 hash 범위에만 포함했고 판정 근거와 수정 대상으로 사용하지 않았다.
- 검토 중 checkout 파일은 수정하지 않았다. 지정된 review 디렉터리의 `progress.md`와 이 파일만 작성했다.

## 2. iteration 10 finding 8건 해소 판정

1. **S5-10-01 — 해소됨.** `request_timeout_scheduler_internal.cpp:171-193`에서 `started` 판정, thread 기동, task 삽입이 `state.mutex` 한 구간에 있다. thread 생성은 삽입보다 먼저이며, idle thread도 같은 mutex 아래 `started=false`를 commit한다(`:95-104`). firing handler가 자기 task를 cancel할 때는 `firing_thread` 비교로 자기 자신을 기다리지 않는다(`:133-151`, `:217-221`). 국소 단위 테스트는 2/2 통과했다. 다만 lost-wakeup 자체를 재현하는 회귀 테스트 부재는 I3 finding으로 별도 기록한다.
2. **S5-10-02 — 해소 안 됨.** 수정 commit `c1c579ad1`의 새 반례다. allocator는 wall-clock 앵커라고 설명하지만(`mesh_runtime.cpp:81-97`) 실제 `now_ms()`는 `zlink::clock_t`이고, Linux에서는 `CLOCK_MONOTONIC`, Windows에서는 부팅 후 tick을 사용한다(`clock.cpp:117-159`, `:177-205`). 따라서 재부팅 뒤 값이 낮아지고, 별도 process가 같은 millisecond에 같은 RID를 재시작하면 process-local atomic도 같은 generation을 만들 수 있다. 이는 더 높은 generation만 이전 수명을 교체한다는 계약(`01-mesh-node.ko.md:242-248`)을 충족하지 않는다.
3. **S5-10-03 — 해소 안 됨.** 수정 commit `c1c579ad1`의 이전 inventory에 없던 구체적 반례다. 공통 completion과 destroy는 task를 node mutex 밖에서 cancel하지만(`mesh_runtime.cpp:1067-1138`, `:1224-1269`, `mesh_node_api.cpp:617-630`), Actor join 전용 completion은 operation을 직접 erase하면서 task를 인계받아 cancel하지 않는다(`mesh_actor_api.cpp:447`, `:1421-1426`, `:1474-1479`). scheduler map이 `shared_ptr`를 계속 보유하므로 성공한 operation마다 callback context와 raw node address가 원래 timeout까지 남는다. full operation ID 검증은 ABA 실행을 막지만, 긴 timeout과 높은 완료율에서 누적 resource retention은 닫히지 않았다.
4. **S5-10-04 — 해소 안 됨.** 수정 commit `c1c579ad1`의 새 interleaving 반례다. 다섯 reader는 pin을 사용하지만 handler 등록은 pin한 state를 직접 갱신하지 않고 `set_monitor_handler_state()`에서 registry를 다시 조회한다(`monitor_socket_api.cpp:31-44`). 그 사이 close가 기존 entry를 erase하고 pin drain을 기다리면(`monitor_api.cpp:278-301`), setter는 entry 부재를 보고 같은 socket key에 새 state를 생성한다(`:304-344`). close는 old state만 삭제하고 socket을 닫으므로 새 state와 periodic task가 닫힌 socket을 참조한다. pin 계약이 registration mutation까지 원자적으로 보호하지 못한다.
5. **S5-10-05 — 해소됨.** local completion은 이미 예약한 completion record를 splice하므로 reply 시점에 새 mailbox capacity admission이 없다(`mesh_actor_api.cpp:1431-1458`). remote path는 public `flags_`를 `wire_submit_join_reply()`에 전달하고(`:1483-1504`), wire가 이를 최종 `zlink_send_part_rid`까지 관통시킨다(`mesh_wire.cpp:24-42`, `:476-504`). 실패 시 `in_flight`만 해제하고 `consumed`를 설정하지 않아 token retry도 유지된다.
6. **S5-10-06 — 부분 해소, 최종적으로 해소 안 됨.** 실제 `address_in_use`만 `EADDRINUSE`로 분류하는 Linux 경로는 맞다(`asio_tcp_acceptor_config.hpp:26-32`). 그러나 Windows system-category 값은 Winsock code(예: `WSAEACCES == 10013`)인데 나머지 오류를 `ec.value()` 그대로 errno에 넣는다. 저장소에는 이를 POSIX errno로 바꾸는 `wsa_error_to_errno()`가 이미 있다(`err.cpp:201-314`). 따라서 Windows open/option/bind/listen 실패는 공개 errno map의 보존된 POSIX errno가 아니다. 이는 수정 commit `c1c579ad1`에 대한 새 platform 반례다.
7. **S5-10-07 — 해소됨.** 상한은 stopwatch의 microsecond 반환 단위에 맞춘 `SETTLE_TIME * 20 * 1000`이다(`unittest_request_timeout_scheduler.cpp:40-48`). 해당 실행 파일은 2/2 통과했다.
8. **S5-10-08 — 이번 판정 대상 아님.** 지시대로 internals를 근거 또는 수정 대상으로 사용하지 않았으며 S5-11 이관 상태만 확인했다.

## 3. I1 — 계약 구현 일치

### Finding

- `[I1][high] core/src/runtime/services/mesh/mesh_runtime.cpp:81 — S5-10-02 lifecycle generation이 process 재시작 수명을 엄격히 순서화하지 못함 — allocator의 입력은 wall clock이 아니라 부팅 기준 monotonic millisecond이고 CAS 상태도 process-local이라 재부팅 또는 같은-ms 재시작에서 새 generation이 이전 값보다 크다는 보장이 없다. 01-mesh-node §4·§5의 같은 RID 재시작 구분과 higher-generation replacement를 위반한다 — 재시작 사이에도 보존되는 단조 epoch를 사용하거나, 계약이 요구하는 순서를 보장하는 durable generation 발급 방식을 도입하고 reboot·즉시 process restart contract test를 추가한다.`
- `[I1][high] core/src/api/mesh/mesh_actor_api.cpp:447 — S5-10-03 operation-owned timeout task가 Actor join 전용 terminal path에서 cancel되지 않음 — 같은 직접 erase가 owner-missing(:1424)과 정상 local reply(:1476)에도 있고 scheduler가 task shared_ptr를 deadline까지 보존한다. 완료 operation의 callback context/raw node pointer가 대량 누적될 수 있다 — 모든 terminal erase를 timeout task를 반환하는 한 completion primitive로 모으고 node mutex 밖에서 cancel한다. owner-missing과 정상 join reply, wire join completion 각각을 긴 timeout으로 검증한다.`
- `[I1][high] core/src/api/monitoring/monitor_api.cpp:321 — S5-10-04 concurrent handler registration/close가 닫히지 않은 registry replacement race를 가짐 — registration caller가 old state를 pin해도 setter가 registry를 재조회하며, closer가 entry를 지운 뒤에는 새 state를 생성한다. closer는 old state만 drain/delete하고 socket close를 계속한다 — pinned expected state를 setter에 넘겨 동일 entry일 때만 원자 갱신하거나 registry lock 아래 registration claim을 두고, concurrent close가 시작됐으면 handler 등록을 실패시킨다.`
- `[I1][medium] core/src/runtime/transports/asio/asio_tcp_acceptor_config.hpp:30 — S5-10-06 Windows acceptor 오류가 Winsock 숫자를 공개 errno로 노출함 — Windows용 기존 변환 함수는 `err.cpp:201-314`에 있지만 helper는 `ec.value()`를 그대로 반환한다. 예를 들어 permission failure가 `EACCES`가 아니라 10013이 되어 errno map과 typed-result 분류가 흔들린다 — Windows에서는 `wsa_error_to_errno(ec.value())`를 사용하고 open/set-option/bind/listen 오류별 platform test를 추가한다.`

### Evidence

- 공통 spec 8개, service spec 5개, socket spec 8개와 각 영문 대응본, modular public header, 구현 entry point를 정적 대조했다. prompt의 “socket 9”는 `socket/README`를 공통 socket 계약으로 더해 확인했다.
- operation ID의 high 값은 node lifecycle generation으로 등록된다(`mesh_node_api.cpp:78-88`), timeout callback도 high와 low를 모두 확인한다(`mesh_messaging_api.cpp:27-49`). 따라서 S5-10-03의 ABA 실행 방지는 해소됐지만 task 수명 누락은 별개로 남는다.
- monitor status, recv-model, handler registration, generic close 검사와 monitor close의 다섯 reader는 pin을 취한다. finding은 여섯 번째 raw reader가 아니라 pin과 mutation 사이의 재조회/재생성 interleaving이다.
- join reply 계약의 `DONTWAIT`/`SNDTIMEO`와 token retry 조건은 `04-actor.ko.md:227-232`, 구현의 remote flags 전달과 consume 조건은 `mesh_actor_api.cpp:1483-1504`에서 일치한다.
- package metadata는 `core/CMakeLists.txt:11`, `:1366-1370`, Debian changelog/dsc/control, Red Hat spec, NuGet config/nuspec/targets에서 10.0.0 및 SOVERSION/package major 10으로 일치했다.
- 국소 검증: `unittest_request_timeout_scheduler` 2/2 통과. `test_monitor_socket_contract`는 두 번 모두 첫 wildcard TCP bind가 `EADDRINUSE`로 실패한 뒤 `mutex.hpp:108` 진단을 냈으므로 green 근거로 사용하지 않았다. 이 실행만으로 registry race의 인과를 주장하지 않고 위 finding은 source interleaving으로 입증했다.

### Verdict

**NOT CLEAN** — blocker 0, high 3, medium 1.

## 4. I2 — POSD·DDD

### Finding

- `[I2][high] core/src/api/mesh/mesh_actor_api.cpp:447 — operation terminal lifecycle 지식이 공통 completion owner 밖으로 누출됨 — 공통 helper는 timeout task 회수·mutex 밖 cancel invariant를 소유하지만 Actor join의 세 직접 erase가 이를 복제하지 않아 S5-10-03이 남았다 — operation erase/cancel/monitor observation을 하나의 깊은 terminal-completion 모듈이 소유하게 하고 특수 Actor membership commit만 callback으로 주입한다.`
- `[I2][high] core/src/api/monitoring/monitor_socket_api.cpp:42 — monitor pin의 lifetime 보장과 registry mutation 책임이 두 함수 사이에 분리됨 — caller는 old state를 pin하지만 setter는 그 state를 버리고 key로 다시 찾아 새 state까지 만들 수 있어 S5-10-04 race가 생긴다 — registration claim 또는 pinned state update 한 인터페이스가 lookup, liveness 확인, mutation을 함께 소유하도록 합친다.`

### Evidence

- 첫 대안인 “각 직접 erase 옆에 cancel 추가”는 Actor별 누락을 다시 만들고 operation lifecycle 지식을 확산한다. 선택할 대안은 공통 terminal primitive가 task ownership과 cancel 순서를 흡수하는 방식이다.
- monitor의 첫 대안인 “setter 내부에서 임시 pin 추가”는 old pin과 재조회 state가 달라질 수 있어 identity 문제를 없애지 못한다. 선택할 대안은 caller가 얻은 registration claim/state 자체를 원자적으로 갱신하는 방식이다.
- MeshNode, Spot, Actor, session의 public handle과 owner/claim 경계 자체에서는 이번 전체 pass에서 별도 blocker·high·medium POSD/DDD finding을 찾지 못했다.

### Verdict

**NOT CLEAN** — blocker 0, high 2, medium 0.

## 5. I3 — 정리 완결성

### Finding

- `[I3][medium] core/tests/unittest/unittest_request_timeout_scheduler.cpp:62 — S5-10-01 lost-wakeup 수정에 대응하는 회귀 테스트가 없음 — 현재 두 test는 deadline 전 cancel과 firing 중 cancel만 실행하고(:62-105), scheduler가 idle exit를 commit하는 순간의 concurrent schedule 또는 반복 idle restart를 검증하지 않는다. 이번 수정의 핵심 race가 되돌아가도 이 target은 통과한다 — scheduler idle-exit 경계와 schedule을 반복 교차시키고 모든 task가 deadline 내 정확히 한 번 fire하는 stress-shaped 단위 회귀를 추가한다.`

### Evidence

- scope의 test `.cpp` basename과 CMake source 목록을 대조했으며 orphan test source를 찾지 못했다.
- scope의 source `.cpp`는 CMake source inventory에 모두 포함됐다.
- 제거 identifier contract와 `libzlink.vers`, public modular headers를 정적 검색했으며 이번 범위에서 새 blocker·high·medium 호환 잔재나 dead declaration을 찾지 못했다.
- S5-10-08 internals 문서는 명시적으로 제외했다.

### Verdict

**NOT CLEAN** — blocker 0, high 0, medium 1.

## 6. Low finding 목록

없음.

## 7. Known risk 4건 판정

1. **TSAN auto-HWM lock-order — 추적 유지, 이번 source pass에서 새 확정 finding 없음.** context recalc가 `_slot_sync`를 보유한 채 socket plan을 만들고(`ctx_auto_hwm_recalc.cpp:80-116`) 그 안에서 monitor sync를 취한다(`socket_base.cpp:222-238`). 반대 순서의 확정 deadlock 실행 경로는 찾지 못했지만 sanitizer gate가 연기된 상태이므로 risk를 닫지 않는다.
2. **raw command mailbox ypipe — source상 endpoint 직렬화 확인, sanitizer 확인 전 추적 유지.** send/recv와 `check_read` 사용은 `_sync` 아래 있다(`mailbox.cpp:39-57`, `:64-98`, `:138-167`). 정적 반례는 찾지 못했으나 coordinator의 sanitizer 종료 gate 전에는 최종 해소로 승격하지 않는다.
3. **raw socket teardown 관찰 — 추적 유지, 새 확정 finding 없음.** public API inflight/closing state와 deferred close/mailbox refcount 경계는 `socket_lifecycle_runtime.cpp:20-149`, `:210-309`에 있고 Context는 reaper가 socket registry를 비울 때까지 기다린다(`ctx.cpp:147-170`). 이번 국소 monitor test가 green하지 않았으므로 동적 종료 증거는 확보하지 못했다.
4. **`ctx_term` linger — 계약 일치, finding 아님.** spec은 socket이 닫힐 때까지 term이 block될 수 있다고 명시한다(`01-context.ko.md:123-144`); 구현은 shutdown을 시작하고 reaper 완료와 빈 socket registry를 기다린다(`ctx.cpp:147-170`). linger 대기는 이 계약 안에 있다.

CORE REVIEW NOT CLEAN
