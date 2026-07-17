# S5 Core 구현 리뷰 — iteration 13 — R2 (Claude Sonnet)

## 1. Scope 확인

- 검토 checkout: `/tmp/claude-1000/zlink-s5-it10-sonnet`, detached HEAD `7c7fb0feb6d042cc36c616e8b659543e41ae3c42` (`core(mesh): resolve S5 iteration-12 findings`). 시작·종료 시점 모두 `git status` clean, 검토 중 어떤 파일도 수정하지 않았다.
- 파일 수: 시작·종료 모두 **631** (`git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md`) — prompt 명시값과 일치.
- Aggregate SHA-256: 시작·종료 모두 `62999e63af011f587a8a228b6cb9f6ca55c0055bccc058383058c385b591a3b1` — prompt 명시값과 정확히 일치(재현 절차: 파일 목록을 `LC_ALL=C sort` 한 뒤 `sha256sum $(cat sorted-list)` — `xargs sha256sum | sort | sha256sum` 방식은 출력 라인을 해시값 기준으로 재정렬해 **다른** 값을 낸다는 점을 확인했으므로 이 방식은 피해야 한다).

## 2. iteration 12 병합 finding 3건 — 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| S5-12-01 (generation epoch anchor) | **해소, coordinator ruling 수용** | `mesh_runtime.cpp:82-108` `allocate_lifecycle_generation()`이 `std::chrono::system_clock::now().time_since_epoch()`의 **microseconds**로 앵커를 상향(기존 milliseconds 대비 1000× 충돌 저항)하고, CAS 루프(`compare_exchange_weak` + `candidate=previous+1` fallback)로 프로세스 내 강단조는 그대로 유지한다. 판정 타당성을 spec에서 직접 대조했다: `01-mesh-node.md` §5(peer connection and admission)는 "A duplicate RID/generation in one mesh is rejected. A higher generation drains and replaces the previous generation."라고 명시해 동일 RID/generation 충돌을 durable 순서가 아니라 admission 단계의 duplicate/stale 거부로 처리하도록 이미 정의하고 있다. `04-actor.md` §2("`new` commits a local Actor generation... An active generation for the same ID returns `ZLINK_REQUEST_CONFLICT`/`EEXIST`")도 동일 프로세스 내 활성 상태 충돌만 규정하며, 재부팅·프로세스 재시작 간 durable 순서 보장을 Core 발급자에게 요구하는 문구는 두 spec 어디에도 없다. `S0-21`/`FD-39`(Core가 durable state를 소유하지 않는다)와도 충돌하지 않는다. spec 재해석으로 이 ruling을 반박할 근거를 찾지 못했으므로 **coordinator 판정을 수용**한다. |
| S5-12-02 (monitor 등록 원자성) | **해소** | `monitor_api.cpp:304-385` `set_monitor_handler_state()`가 이전 4개 필드(`socket_handler`/`socket_handler_userdata`/`snapshot_provider`/`snapshot_subject`)를 publish 직전에 캡처 → publish(핸들러 선공개, 주기 tick이 핸들러 부재로 이미 소비한 event를 버리는 창을 막는 기존 계약 유지) → `add_periodic_task` 시도 → 실패(`runtime==NULL`→`ETERM`, `bad_alloc`→`ENOMEM` catch) 시 4개 필드 전부 원상복구 후 `-1` 반환하는 흐름을 라인 단위로 확인했다. `task_id==0`이면서 `failure_errno==0`인 경우(즉 `add_periodic_task` 내부가 예외 없이 0을 반환하는 경로, 예: `!_running \|\| _stopping`)도 `service_control_runtime.cpp:70-77`을 대조해 `add_periodic_task` 자신이 그 경로에서 이미 `errno=ETERM`(또는 `EINVAL`)을 설정한 뒤 0을 반환함을 확인했다 — 즉 `errno`는 어느 실패 경로에서도 정확히 설정되며 `bad_alloc` 봉인도 함수 내부에서 완결된다. |
| S5-12-03 (detach primitive) | **해소** | `mesh_runtime.hpp:697-706`에 선언된 `detach_pending_operation_locked()`가 terminal 6곳 전부에서 사용됨을 호출부 grep으로 전수 확인했다: runtime 3곳(`mesh_runtime.cpp:1107,1151,1281` — `complete_pending_operation_with_commit` 2곳, `commit_prepared_pending_operation` 1곳), actor 3곳(`mesh_actor_api.cpp:450,1433,1486`). 각 호출부 모두 `timeout_task`를 락 보유 구간 안에서 캡처하고, `lock_guard`/블록 스코프가 닫힌 뒤(`mesh_actor_api.cpp:1496-1497` 포함, 해당 함수는 주석으로 "Cancelled outside the node mutex (same contract as `complete_pending_operation_with_commit`)"를 명시)에만 `zlink::request_timeout::cancel()`을 호출함을 개별 확인했다. 잔여 `operations.erase` 호출은 정확히 3곳뿐이다: primitive 내부(`mesh_runtime.cpp:1020`) + `mesh_node_api.cpp:112`(`operation_timeout_guard_t` 생성 실패 시 pre-commit rollback, task가 존재하지 않는 경로) + `mesh_node_api.cpp:125`(`operation_submission_t` 소멸자, 미commit 시 guard 소유 rollback — `delete _timeout`도 같은 락 스코프 밖). 분모 3/3 일치, ledger §3 서술과 정확히 부합한다. |

3건 모두 해소로 판정. 새로 연 항목 없음(반례 없음).

## 3. I1 / I2 / I3 — 전체 scope 재검토

### I1 — 계약 구현 일치

Finding: 없음.

Evidence: 위 3건 재검증에 더해 `mesh_runtime.cpp` 전체(1293줄)를 처음부터 재독했다 — 핸들 레지스트리 pin/unpin(`pin_node_lifecycle`/`pin_node_data_path`/`unpin_node_lifecycle`/`claim_node_destroy`/`unregister_node_and_wait_lifecycle_quiesced`), `admit_record`(백프레셔·infrastructure 무제한 admission·`bad_alloc` 봉인), `emit_monitor_event`(monitor pin 카운트로 close-race 방어), `complete_pending_operation_with_commit`/`prepare_pending_operation_completion`/`commit_prepared_pending_operation`(2-phase 커밋과 committing 플래그) 모두 관찰 가능한 오류 코드·상태 전이가 `01-mesh-node.md`/`04-actor.md`와 충돌하지 않는다. `reply_routes`(join/transfer-relay 라우팅)는 `timeout_task`를 갖지 않는 별도 구조체(`mesh_runtime.hpp:459-`)이므로 S5-12-03과 같은 detach 누락 위험이 구조적으로 없다 — 4곳의 `reply_routes.erase` 호출부(`mesh_node_api.cpp`, `mesh_actor_api.cpp`, `mesh_wire_ingress.cpp` 3곳, `mesh_stream_session_api.cpp`)를 대조해 확인. `mesh_transfer_api.cpp`(1267줄, actor transfer 상태 머신)와 `core/src/runtime/sockets/`(레거시 소켓 계층) 전체는 iteration 9(전자)·iteration 1(후자) 이후 이번 commit까지 **한 줄도 변경되지 않았다**(`git log`로 확인) — 즉 각각 3회·11회 연속 clean 재검토를 통과한 영역이며 이번 diff와 무관하다.

Verdict: **CLEAN**

### I2 — POSD·DDD

Finding: 없음(blocker/high/medium).

Evidence: `detach_pending_operation_locked()` 도입은 iteration-12 low finding(수동 반복 패턴)을 정확히 해소하는 방향의 리팩토링으로, "timeout_task는 락 아래서만 다뤄지고 캡처된 핸들만 락 밖에서 cancel된다"는 기존 정보 은닉 경계를 단일 지점으로 응집시켰다 — 책임(operation map 소유권)이 `mesh_node_t`에, 취소 시점 지식(락 밖)이 caller에 남아있는 기존 분할이 유지된다. monitor 등록의 이전 값 캡처/복원은 registry 락이 아니라 `state`의 원자적 필드 스냅샷/복원으로 처리되며, 이는 handler 필드가 애초에 `std::atomic`으로 선언된 기존 설계(다중 reader, 단일 writer 가정)와 일치한다.

Verdict: **CLEAN**

### I3 — 정리 완결성

Finding: 없음.

Evidence: `core/src` 전역에서 mesh/monitoring 관련 TODO/FIXME/XXX/HACK 주석, `#if 0` 블록을 검색했으나 0건. `core/tests/CMakeLists.txt`는 `file(GLOB_RECURSE ...)` + `zlink_warn_unregistered_tests()`(CMake 설정 단계에서 CMakeLists.txt 본문에 이름이 등장하지 않는 테스트 소스를 `AUTHOR_WARNING`으로 자동 검출)로 죽은/미등록 테스트 target을 구조적으로 방지하고 있음을 확인했다 — 이번 diff는 이 메커니즘을 우회하지 않는다. iteration-12 커밋(7c7fb0feb)의 diff 자체(4개 소스 파일 + 문서)에 불필요한 변경이나 orphan 코드가 없다.

Verdict: **CLEAN**

## 4. low finding 목록

없음. iteration-12 low finding 1건(수동 반복 패턴)은 이번 commit의 `detach_pending_operation_locked()` 도입으로 해소되었다.

## 5. Known risk 4건 판정

`git show --stat 7c7fb0feb`로 이번 commit이 정확히 4개 소스 파일(`mesh_actor_api.cpp`, `monitor_api.cpp`, `mesh_runtime.cpp`, `mesh_runtime.hpp`)만 건드렸음을 확인했다 — 아래 4개 영역 어디와도 겹치지 않는다.

1. **TSAN auto-HWM lock-order** — 관련 파일(`auto_hwm_policy.cpp/hpp`, `ctx_auto_hwm_recalc.cpp`, `ctx_auto_hwm_state.cpp/hpp`, `socket_base.cpp`의 auto-HWM 경로) 전부 이번 commit에서 미변경. 직접 재독: `ctx_auto_hwm_recalc.cpp`의 `auto_hwm_recalculate_now()`는 `ctx_t::_slot_sync`를 보유한 채 `socket_base_t::prepare_auto_hwm_socket_plan()`을 호출하고, 그 함수는 내부에서 소켓 자신의 `monitor_runtime().sync`를 추가로 획득한다(`socket_base.cpp:224-238`) — 즉 `_slot_sync → monitor sync` 순서의 중첩 락이 실재한다. 이 순서를 역전시켜 획득하는 경로가 정적 검토만으로 코드베이스 전역에서 완전히 배제되지는 않으므로, 이전 iteration들과 동일하게 "정적 검토로 완전 배제 불가, 신규 반례 없음, 추적 유지"로 판정한다. 신규 counter-evidence는 발견하지 못했다.
2. **raw command mailbox ypipe** — `ypipe.hpp`/`ypipe_base.hpp`/`ypipe_conflate.hpp`/`mailbox.hpp`/`pipe.cpp` 전부 미변경(`core/src/runtime/sockets/` 전체가 iteration-1 이후 무변경). 레거시 lock-free 단일-writer/단일-reader 큐 설계(`ypipe.hpp:11-16` 주석)로, S5 mesh 작업과 무관. 추적 유지, 신규 반례 없음.
3. **raw socket teardown 관찰** — 관련 소켓/teardown 경로(소켓 계층 전체) 미변경. 추적 유지, 신규 반례 없음.
4. **`ctx_term` linger** — 관련 경로(`ctx.cpp`/`own.cpp`/`session_base.cpp`) 미변경. 계약 일치, finding 아님(이전 판정 유지).

## 6. Package metadata 정적 대조

- `core/CMakeLists.txt`: `project(zlink VERSION 10.0.0 ...)`, `SOVERSION "10"`(`:1370`), `ZLINK_VERSION_MAJOR/MINOR/PATCH`는 `core/include/zlink.h`·`core/include/zlink/common.h`에서 `10`/`0`/`0`으로 정의되고 `TestZLINKVersion.cmake`가 헤더에서 파싱.
- `core/packaging/debian/control`: 패키지명 `libzlink10`/`libzlink10-dev`/`libzlink10-dbg`(SOVERSION 10과 일치), `Conflicts`/`Replaces`에 구버전(`libzlink6-dev`/`libzlink5-dev`) 명시.
- `core/packaging/debian/changelog`: `zlink (10.0.0-0.1) UNRELEASED`.
- `core/packaging/redhat/zlink.spec`: `Version: 10.0.0`.
- `core/packaging/nuget/package.nuspec`: `<version>10.0.0</version>`.
- 전부 상호 일치. 불일치 없음.

## 7. 최종 판정

CORE REVIEW CLEAN
