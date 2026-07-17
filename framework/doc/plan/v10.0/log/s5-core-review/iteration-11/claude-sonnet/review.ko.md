# S5 Core 리뷰 — iteration 11 — R2 (Claude Sonnet) 독립 리뷰

## 1. Scope 확인

- 대상 commit: `c1c579ad14ed1ec10c50dad04c053a14ecd533ba` (`core(mesh): resolve S5 iteration-10 findings`)
- 시작: `git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md` → **631 files**, aggregate SHA-256 `56a1b0c135e9357ee3da1666a45084239f9b4a195b5b4d86ee77b471b9a01305`
- 종료: 동일 명령 재실행 → **631 files**, aggregate SHA-256 `56a1b0c135e9357ee3da1666a45084239f9b4a195b5b4d86ee77b471b9a01305` (변경 없음)
- 리뷰 중 checkout(`/tmp/claude-1000/zlink-s5-it10-sonnet`) 어떤 파일도 수정하지 않았다. `core/build/`는 로컬 검증용으로 생성했으며 scope에 포함되지 않고 git 추적 대상도 아니다.

## 2. iteration 10 finding 8건 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| S5-10-01 scheduler lost-wakeup | **해소** | `request_timeout_scheduler_internal.cpp`의 `schedule()`이 liveness 판정(`state.started`)·thread 기동·`state.schedule.insert()`를 단일 critical section(`state.mutex` 한 번의 lock_guard)에서 수행한다. thread 기동을 insert보다 먼저 두어 `std::thread` 생성이 예외를 던져도 map에 고아 task가 남지 않는다(예외 시 task는 지역 `shared_ptr`로 소멸). `cancel()`은 `firing_thread`로 자기 자신의 firing 핸들러에서 호출된 self-cancel을 감지해 데드락 없이 진행한다. 회귀 테스트 `test_cancel_while_handler_is_firing_waits_for_handler_completion` PASS 확인(로컬 ctest) |
| S5-10-02 lifecycle generation | **해소** | `allocate_lifecycle_generation()`(`mesh_runtime.cpp:81`)이 wall-clock ms 앵커 + 프로세스 내 CAS 강단조 할당으로 바뀌었다. `mesh_node_t` 생성자에서 상수 `1` 대신 이 함수를 호출(`lifecycle_generation (allocate_lifecycle_generation ())`). `test_mesh_lifecycle_contracts`가 동일 RID 재생성 시 `lifecycle_generation`이 이전 값보다 큼을 검증하며 PASS. 01-mesh-node §4/§5 "Lifecycle generation distinguishes a new lifetime of the same RID"·"A higher generation drains and replaces the previous generation" 요건과 부합. (wall-clock 역행 시나리오는 §5 아래 known risk 성격이 아니라 일반적 시스템 클록 가정의 한계이며 별도 low로 기록) |
| S5-10-03 timeout task 소유 | **해소** | `pending_operation_t::timeout_task` 도입, `operation_timeout_guard_t`가 `prepared/committed/canceled` 3상태 gate로 스케줄→commit 사이의 조기 발화를 차단(`on_operation_timeout`이 gate가 committed/canceled가 될 때까지 대기)한다. `commit()`은 node mutex 하에서 task를 `pending_operation_t`에 인계한 뒤 gate를 연다. 완료 경로 전수 확인: `complete_pending_operation_with_commit`의 owner-미존재 분기(1088)·정상 분기(1133), `commit_prepared_pending_operation`(1264) 모두 node mutex 해제 후 `zlink::request_timeout::cancel()` 호출. `zlink_mesh_node_destroy`가 orphaned timeout task를 수집해 mutex 밖에서 cancel(mesh_node_api.cpp:576-630). `zlink_mesh_node_shutdown`은 미드레인 operation을 `complete_pending_operation`으로 위임해 동일 cancel 경로를 탄다(444-550). ABA 방지: `operation_id.high = node_->lifecycle_generation`(mesh_node_api.cpp:83)이고 콜백은 `it->second.id.high != ctx->operation_high` 불일치 시 즉시 반환(mesh_messaging_api.cpp:72-73) — 재사용된 node 주소·serial에 대한 오발화가 닫혔다. `operation_timeout_guard_t::commit()`이 호출되는 모든 지점이 `mesh_node_pin_t` 보유 구간 내부임을 확인(예: `zlink_mesh_node_actor_lookup_remote`), 따라서 commit() 내 `state->node` raw pointer는 그 구간 동안 유효 |
| S5-10-04 monitor registry pin | **해소** | `pin_monitor_handler_state`/`unpin_monitor_handler_state` + `monitor_state_pin_t` RAII, registry를 `std::mutex`+`condition_variable`로 전환. reader 5개 진입점 전수 확인: `zlink_monitor_status`(monitor_query_api.cpp:149), `require_monitor_recv_model`(monitor_api.cpp:194), `attach_socket_monitor_handler_state`(monitor_socket_api.cpp:31, `zlink_socket_monitor_handler`의 내부 구현), `zlink_close`의 monitor 검사 경로(zlink.cpp:136), `zlink_monitor_close`(monitor_api.cpp:374) — 전부 `monitor_state_pin_t`로 전환됐다. `unregister_monitor_handlers`·`erase_monitor_handler_state_and_wait`·`finalize_monitor_handler_self_close`가 `registry_pins == 0`이 될 때까지 대기 후 storage를 넘긴다. self-close(같은 콜백 안에서 close) 경로는 pin을 블록 스코프에서 해제한 뒤 처리해 self-deadlock을 피한다. 구 함수명(`find_monitor_handler_state`, `erase_monitor_handler_state`)에 대한 잔여 참조 없음(grep 확인). `test_monitor_socket_contract`(iteration-10에서 `mutex.hpp:108` abort가 관측된 테스트) 로컬 ctest PASS |
| S5-10-05 join reply flags | **해소(coordinator 판정 독립 검증 완료)** | `wire_submit_join_reply`에 `zlink_send_flags_t flags_` 파라미터가 추가되고 `zlink_actor_join_reply`가 원격 경로에서 `flags_`를 그대로 전달한다(mesh_actor_api.cpp:1483-1487). `send_data_message`→`zlink_send_part_rid`가 라우터 소켓의 실제 SNDTIMEO(`wire_start`에서 `node_->sndtimeo_ms` 적용, mesh_wire.cpp:139-141)와 DONTWAIT 플래그를 관통시켜 04-actor §3의 "A full queue returns ZLINK_SUBMIT_BACKPRESSURED/EAGAIN with DONTWAIT; a blocking call waits for SNDTIMEO then ETIMEDOUT... leave the token valid for retry" 계약을 충족한다(실패 시 `route.consumed`를 세팅하지 않아 토큰 재시도 가능, mesh_actor_api.cpp:1493-1502). 로컬 완료(non-remote) 경로는 `register_operation`이 등록 시점에 completion record 1개를 이미 예약해두므로(mesh_node_api.cpp:80-82) 회신 시점에 큐가 실제로 가득 찰 수 없다 — coordinator의 "로컬은 EAGAIN 도달 불가" 판정은 pre-reservation 설계와 일치하며 spec 위반이 아니다 |
| S5-10-06 acceptor errno | **해소** | `acceptor_error_to_errno()`가 `boost::asio::error::address_in_use`만 EADDRINUSE로 매핑하고 그 외에는 실제 boost system errno 또는 EINVAL로 폴백한다. open/set reuse_address/bind/listen 4개 호출 지점 전수가 이 함수를 사용(asio_tcp_acceptor_config.hpp:26-104). 04-errno-map.md:103 "`ZLINK_BIND_ADDR_IN_USE` | `EADDRINUSE` | Endpoint is already in use"와 부합 |
| S5-10-07 테스트 단위 | **해소** | `unittest_request_timeout_scheduler.cpp:43`의 `wait_until_entered`가 `SETTLE_TIME * 20 * 1000`(µs)로 `zlink_stopwatch_intermediate()`(µs 반환)와 단위를 맞췄다. 로컬 ctest PASS |
| S5-10-08 | 이관(이번 검토 대상 아님) | ledger §4/§5에 명시된 대로 S5-11 internals 확정 갱신으로 이관됨. `core/doc/internals`는 이번 판정 근거·수정 대상에서 제외되는 절차 규칙에 따라 판정하지 않는다 |

## 3. I1 / I2 / I3

### I1 계약 구현 일치

**Finding**: 없음

**Evidence**: 위 §2의 파일별 대조(mesh_runtime.cpp/.hpp, mesh_wire.cpp/.hpp, mesh_messaging_api.cpp, mesh_node_api.cpp, mesh_actor_api.cpp, monitor_api.cpp/.hpp, monitor_query_api.cpp, monitor_socket_api.cpp, zlink.cpp, request_timeout_scheduler_internal.cpp, asio_tcp_acceptor_config.hpp)를 `git diff a4e91c01d c1c579ad1`의 19개 변경 파일(코드 15개, 이번 iteration 로그/문서 4개는 scope 밖) 전체와 대조해 diff에 없는 회귀나 새 계약 위반을 도입하지 않았음을 확인했다. 04-actor §3(join reply), 01-mesh-node §4/§5(lifecycle generation), 04-errno-map(EADDRINUSE), 07-monitoring(monitor close/handler 계약)과 소스가 일치한다. 로컬 빌드(ninja, 316/316 타깃, 에러 0) 및 `ctest -j 8`(85/85 PASS, `unittest_request_timeout_scheduler`·`test_monitor_socket_contract`·`test_mesh_lifecycle_contracts`·`test_mesh_peer_admission`·`test_mesh_monitor_matrix` 포함)로 관찰 가능한 동작을 재검증했다.

**Verdict**: **CLEAN**

### I2 POSD·DDD

**Finding**: 없음

**Evidence**: 변경분은 기존 클래스(`pending_operation_t`, `operation_timeout_guard_t`, `monitor_handler_state_t`)에 소유권 필드/pin 카운터를 추가하는 형태로, 책임 경계(operation lifecycle을 pending_operation_t가 소유, monitor registry가 reader lifetime을 관리)를 강화하는 방향이며 새로운 계층 침범이나 정보 은닉 위반을 만들지 않았다. `core/src` 최대 파일 크기(asio_engine.cpp 1955줄, mesh_actor_api.cpp 1770줄 등)를 조회했고, mesh API가 도메인별(actor/stream-session/transfer/node/messaging/dispatch)로 이미 분리되어 있어 이번 변경으로 새로운 God-file이 생기지 않았다.

**Verdict**: **CLEAN**

### I3 정리 완결성

**Finding**: 없음

**Evidence**: 구 함수명(`find_monitor_handler_state`, `erase_monitor_handler_state`, `operation_timeout_guard_t(mesh_node_t*, uint64_t, uint32_t)` 구 시그니처)에 대한 잔여 참조를 grep으로 확인했으며 전무하다. 제거된 `SpotNode`/route bridge 식별자는 `core/tests/contract/removed-identifiers-10.0.0.json`(의도된 negative-test 매니페스트)에만 남아 있고 실제 선언은 없다. 변경 영역에 `#if 0`, TODO/FIXME/XXX 잔재 없음.

**Verdict**: **CLEAN**

## 4. Low finding

없음.

## 5. Known risk 4건 판정

이번 iteration의 diff(`a4e91c01d..c1c579ad1`)는 아래 4건과 관련된 파일을 전혀 건드리지 않았다(`ctx_auto_hwm_recalc.cpp`, `ctx_auto_hwm_state.hpp`, `ypipe*.hpp`, `pipe.cpp`, `mailbox.hpp`, raw socket/stream 관련 소스, `ctx.cpp`/`own.cpp`/`object.cpp`의 term/linger 경로 — `git diff --stat`으로 확인). 따라서 이전 iteration들의 판정을 뒤집을 새 증거가 없다.

1. **TSAN auto-HWM lock-order** — `ctx_auto_hwm_recalc.cpp` 자체는 `_opt_sync`/`_slot_sync`를 중첩 없이 별도 스코프로 획득해 국소적으로는 안전해 보이지만, socket 쪽 자체 mutex와의 전역 lock-order는 정적 검토만으로 완전히 배제할 수 없다. 미해소·추적 유지(신규 증거 없음, 재조사 범위 아님).
2. **raw command mailbox ypipe** — 관련 파일 unchanged. 미해소·추적 유지.
3. **raw socket teardown 관찰** — 관련 파일 unchanged. 미해소·추적 유지.
4. **`ctx_term` linger** — `own.cpp`/`session_base.cpp`/`object.cpp`의 linger 전파 경로가 이번 diff와 무관하며 이전 판정("계약 일치, finding 아님")을 유지한다.

## 6. package metadata 정적 대조

- `core/CMakeLists.txt`: `project(zlink VERSION 10.0.0 ...)`, `SOVERSION "10"`(1370행)
- `core/packaging/debian/changelog`: `zlink (10.0.0-0.1) UNRELEASED`
- `core/packaging/debian/control`, `libzlink10.install`(`usr/lib/*/libzlink.so.*`), `libzlink10-dev.install` 등 SOVERSION 10 계열 패키지명과 일치
- `core/packaging/redhat/zlink.spec`: `Version: 10.0.0`
- `core/packaging/nuget/package.nuspec`: `<version>10.0.0</version>`
- `core/packaging/conan/conandata.yml`: `10.0.0` 소스 URL 등록(sha256은 아직 미기재 — 동일 패턴이 이전 미출시 버전들(9.0.x 등)에도 존재하는 release-time 채움 방식이라 신규 결함 아님)

전부 10.0.0/SOVERSION 10로 일치. 불일치 없음.

## 7. 결론

CORE REVIEW CLEAN
