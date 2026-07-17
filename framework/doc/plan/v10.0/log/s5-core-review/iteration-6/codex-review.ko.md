# S5 Core 구현 독립 리뷰 R1 — iteration 6 전체 pass

## 결론

**CORE REVIEW NOT CLEAN**

- iteration 5 병합 finding 4건: **해소 3건, 부분 해소 1건**
- 전체 scope 신규 finding: **4건** (`high 1`, `medium 2`, `low 1`)
- I1 계약 구현 일치: **NOT CLEAN**
- I2 POSD·DDD: **CLEAN**
- I3 정리 완결성: **NOT CLEAN**

## 실행 증거

| 항목 | 결과 |
|---|---|
| snapshot | `HEAD == b1e6c81fb5de1d28000bbb9ffaf6750169393067`, manifest candidate와 일치 |
| scope | 631 files, manifest와 일치 |
| 시작 scope hash | `6fa7e8c66ef878cff76356c9208dbf194a3dffc7f494948d91b87f51cd7656d8` — manifest와 일치 |
| 종료 scope hash | `6fa7e8c66ef878cff76356c9208dbf194a3dffc7f494948d91b87f51cd7656d8` — 시작값·manifest와 일치 |
| iteration 6 delta | `c8d567c64..b1e6c81fb`, scope 안 5 files, +48/−19, `git diff --check` clean |
| 전체 S5 campaign delta | `8206fd44d..b1e6c81fb`, scope 안 44 files, +3735/−1825, `git diff --check` clean |
| 공개 surface | `check_public_surface.py`: **PASS**, formal function 196개와 export 일치, 제거 identifier 없음 |
| configured suite | `ctest --test-dir core/build -N`: **85 targets**. 실행은 하지 않았고 manifest의 기존 85/85 결과를 재사용했다. |
| runner inventory | peer admission 12, monitor matrix 6, stress 3, lifecycle contracts 9 case |
| 정적 hygiene | 631 files 전부 목록화. 0-byte·merge marker·금지 문구·문서 tab 없음. Conan YAML parse PASS. |
| 동적 제약 | 이번 pass의 주 증거는 정적 대조다. TCP bind가 필요한 test를 재실행하지 않았으며 sandbox 705를 finding으로 사용하지 않았다. |

scope hash는 시작과 종료에 지시된 다음 명령을 그대로 사용했다.

```bash
git ls-files core/include core/src core/tests core/packaging \
  core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md \
  | xargs sha256sum | sha256sum
```

## Iteration 5 finding 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| F-I1-01 잔여 | **해소** | §5는 drain peer를 새 snapshot에서 제외하고 commit된 message를 취소하지 않는다(`core/doc/spec/core/service/01-mesh-node.ko.md:254-255`). §7은 all-or-none을 capacity admission으로 한정하고 reserve~commit peer 이탈을 unreachable 성공으로 정의한다(`:381-394`). §9는 원자성을 명시적으로 §7 capacity 보장에 종속하고 같은 unreachable 규칙을 참조한다(`:467-471`). 영문도 같은 뜻이다(`core/doc/spec/core/service/01-mesh-node.md:276-278`, `:413-431`, `:512-518`). 따라서 §5·§7·§9에서 같은 관찰 가능한 delivery·회계 결과가 도출된다. |
| N5-I1-01 | **부분 해소** | 지적된 `slot_base.resize`는 `ready_added.reserve`, deque placeholder, ready-index insert와 함께 `bad_alloc` catch 안에 들어갔고 실패 시 speculative state를 rollback한 뒤 `OUT_OF_MEMORY/ENOMEM`을 반환한다(`core/src/api/mesh/mesh_messaging_api.cpp:873-906`). 그러나 그 앞의 record 선준비 storage는 catch 밖이다. 잔여는 `N6-I1-01`로 계속한다. |
| N5-I3-01 | **해소** | configured CTest inventory는 85이고, runner의 실제 `RUN_TEST` 수는 peer admission 12, monitor 6, stress 3, lifecycle 9다. CHANGELOG가 같은 수치와 신규 unreachable accounting·MIXED merge·bind/destroy hammer를 기록한다(`CHANGELOG.md:56-70`). 기계 관찰 4건은 아래 known risk 4건과 일치한다. |
| N5-I3-02 | **해소** | 두 binder 결과를 atomic에 저장하고(`core/tests/integration/test_mesh_lifecycle_contracts.cpp:661-672`), 합법 집합 `{OK, INVALID_STATE, NOT_FOUND, BACKPRESSURED}`을 단정한다(`:679-692`). destroy 뒤 bind 실패와 destroyed generation binding 0도 확인한다(`:694-717`). 주석은 성공-후-rollback 자체에 외부 관측점이 없고 설계 논증으로 닫는 범위를 명시한다(`:603-610`). coordinator가 정한 관측 범위와 일치한다. |

## I1 계약 구현 일치 — NOT CLEAN

### N6-I1-01 — Logical Multicast record 선준비 allocation이 public OOM result로 매핑되지 않음 (medium)

- 이슈·근거: `publish_common`은 local record 선준비에서 `prepared.reserve`, 문자열·metadata storage 대입과 part vector `resize`를 수행한다(`core/src/api/mesh/mesh_messaging_api.cpp:844-870`, `:52-64`). 이 allocation들은 `bad_alloc` catch가 시작되는 local slot block보다 앞에 있다(`:878-899`). 두 공개 publish 함수도 outer exception mapping 없이 `publish_common`을 직접 반환한다(`:1001-1016`, `:1019-1044`). 정본은 Mesh publisher와 Spot publish를 포함한 submit family의 필요한 storage 실패를 `ZLINK_SUBMIT_OUT_OF_MEMORY`, `errno == ENOMEM`으로 규정한다(`core/doc/spec/core/04-errno-map.ko.md:24-45`).
- 영향: record 선준비 allocation 실패가 typed C result로 끝나지 않고 C ABI 밖으로 예외를 전파하거나 process 종료를 일으킬 수 있다. remote commit 전 실패이므로 delivery는 시작되지 않지만 공개 오류 계약이 깨진다.
- 수정 범위: `publish_common`의 snapshot·record 선준비부터 local slot 선예약까지 필요한 allocation과 public submit error mapping.
- 검증 방향: 각 선준비 allocation 실패를 주입해 remote/local delivery 0, speculative mailbox·ready state 0, `ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM`, C ABI 밖 예외 0을 확인한다.

### N6-I1-02 — 대기 중 shutdown과 concurrent destroy가 같은 MeshNode 수명을 동시에 소유함 (high)

- 이슈·근거: `shutdown`은 `shutdown_active = true` 뒤 active claim/operation을 기다리며 condition-variable wait에서 node mutex를 놓는다(`core/src/api/mesh/mesh_node_api.cpp:318-353`). 그러나 `destroy`의 child 검사와 강제 종료 경로는 `shutdown_active`를 검사하지 않고(`:407-452`) wire·registry 정리 뒤 node를 삭제한다(`:454-460`). 정본은 같은 handle의 shutdown과 destroy 재진입을 `EDEADLK`로 끝내도록 규정한다(`core/doc/spec/core/service/01-mesh-node.ko.md:506-509`).
- 영향: child handle이 없고 claim 또는 operation 때문에 shutdown이 대기하는 동안 destroy가 node를 삭제할 수 있다. shutdown thread가 깨어난 뒤 해제된 mutex와 node state를 다시 사용하므로 use-after-free, hang 또는 process corruption 가능성이 있다.
- 수정 범위: MeshNode shutdown/destroy의 동시 lifecycle admission, node 수명 소유권과 public result/errno mapping.
- 검증 방향: active claim과 timeoutless operation 각각으로 shutdown을 wait 상태에 두고 concurrent destroy를 호출해, 정본의 `EDEADLK` 결과, node 미삭제, shutdown 종료 뒤 단일 destroy 성공을 검증한다. ASAN과 TSAN에서 같은 두 interleaving을 반복한다.

### N6-I1-03 — active monitor callback의 close가 formal close errno 집합 밖의 EDEADLK를 반환함 (medium)

- 이슈·근거: Mesh monitor close는 `handler_active`이면 `ZLINK_CLOSE_BUSY`를 반환하면서 `errno = EDEADLK`로 설정한다(`core/src/api/mesh/mesh_monitor_api.cpp:136-155`). 정본 errno map은 `ZLINK_CLOSE_BUSY`를 active callback/API의 `EBUSY`로만 정의하며 `EDEADLK`는 handler 등록·해제의 `ZLINK_HANDLER_DEADLOCK`에 속한다(`core/doc/spec/core/04-errno-map.ko.md:88-101`).
- 영향: 같은 active-callback 조건을 bindings가 formal close mapping으로 분류할 수 없고, C result와 errno의 조합이 정본에 없는 값이 된다.
- 수정 범위: MeshNode monitor active-callback close의 result/errno mapping과 해당 contract test.
- 검증 방향: handler 실행 중 같은 monitor에 close를 호출해 result·errno 조합, handle 보존, callback 종료 뒤 close 성공을 정본 errno table과 대조한다.

## I2 POSD·DDD — CLEAN

- finding 없음.
- public contract/runtime/transport 분리는 유지되고, wire format·admission·ingress·outbound 책임은 실제 네 구현 모듈과 하나의 private shared contract로 나뉜다(`core/src/runtime/services/mesh/mesh_wire_internal.hpp:12-18`, `core/CMakeLists.txt:886-890`). 공개 surface를 늘리지 않고 node-owned raw ROUTER의 비소비 capacity probe만 내부 확장했다(`core/src/runtime/sockets/common/socket_base.hpp:137-141`).
- I1의 lifecycle/OOM/result 결함은 독립 I2 finding으로 중복 계산하지 않았다.

## I3 정리 완결성 — NOT CLEAN

### N6-I3-01 — internals의 API/runtime 책임 경계 설명이 현재 Spot timer 구현과 불일치 (low)

- 이슈·근거: internals는 `api/mesh/*`가 공개 signature 검증과 result mapping만 소유하고 모든 상태 변경을 `mesh_runtime`/`mesh_wire`로 내린다고 기록한다(`core/doc/internals/services-internals.ko.md:23-28`; 영문 `core/doc/internals/services-internals.md:24-30`). 실제 `mesh_api.cpp`는 Spot timer registry와 cancellation state를 소유하고(`core/src/api/mesh/mesh_api.cpp:223-246`), `timer_count`와 `timer_turn_active`를 직접 변경하며 lifecycle 종료를 호출한다(`:250-276`, `:280-325`, `:349-418`).
- 영향: 유지보수자가 Spot timer 수명과 lock/state owner를 runtime 쪽에서 찾게 되어 실제 registry·turn admission·종료 책임과 잠금 관계를 놓칠 수 있다.
- 수정 범위: 한·영 service internals의 `api/mesh`/mesh runtime 책임 표와 Spot timer lifecycle·locking 설명.
- 검증 방향: Spot timer 생성·turn 진입/해제·cancel·close의 모든 state write와 lock owner를 실제 파일별로 목록화해 한·영 internals의 책임 경계와 일치시키고, 문서에 없는 owner가 남지 않는지 확인한다.

그 밖의 정리 gate는 clean이다. 공개 C block/header/export 196개 일치, 제거 identifier no-hit, CMake의 wire 4 TU·mesh test 5개 연결, package YAML, 0-byte/merge marker, 한·영 §9 의미와 diff hygiene에서 추가 finding이 없다.

## Known risk 판정

| 항목 | 판정 | 근거 |
|---|---|---|
| TSAN auto-HWM lock-order | **기존 risk 수용·추적 유지, 신규 S5 finding 없음** | `_slot_sync`를 잡은 상태에서 socket plan prepare/apply를 호출한다(`core/src/runtime/core/ctx_auto_hwm_recalc.cpp:80-116`). 이번 정적 pass로 반대 lock order를 확정하지 못했으며 S5 delta와 직접 연결되지 않는다. |
| TSAN raw command mailbox ypipe | **기존 risk 수용·추적 유지, 신규 S5 finding 없음** | command pipe write/flush와 read가 `_sync`로 감싸져 있다(`core/src/runtime/core/mailbox.cpp:39-56`, `:64-97`). 기존 TSAN 관찰의 원인·해소를 현재 정적 코드만으로 확정하지 않는다. |
| raw socket teardown (`pipe_t::detach_peer_backref`·asio `blob_t`) | **9.x raw 기계 risk 수용·추적 유지, mesh finding 아님** | pipe term ack는 peer backref를 끊은 뒤 sink 종료를 알린다(`core/src/runtime/core/pipe.cpp:704-730`). asio error 경로는 session routing-id `blob_t` view로 disconnect event를 낸 뒤 engine error와 unplug를 진행한다(`core/src/runtime/engine/asio/asio_engine.cpp:1845-1855`). 동적 수명 관찰을 정적으로 해소됐다고 판정하지 않는다. |
| ctx_term linger | **기존 risk 수용·추적 유지** | blocky context의 socket 기본 linger는 `-1`이다(`core/src/runtime/sockets/common/socket_base.cpp:129-134`). termination은 attached pipe를 종료하고 ack를 기다리는 구조다(`core/src/runtime/sockets/common/socket_base_lifecycle.cpp:137-161`). bounded 종료 증거가 없어 기존 risk로 유지한다. |

## 최종 판정

blocker는 없지만 high 1건, medium 2건, low 1건이 남았다. I1과 I3가 `NOT CLEAN`이므로 clean gate를 충족하지 못한다.

CORE REVIEW NOT CLEAN
