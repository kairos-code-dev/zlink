# S5 Core 구현 리뷰 — iteration 14 — R2 (Claude Sonnet)

## 1. Scope 확인

- 검토 checkout: `/tmp/claude-1000/zlink-s5-it10-sonnet`, detached HEAD `26a4cbb81118f7aac7ea4c620a0a7a0e6bbae121` (`core(monitor): resolve S5 iteration-13 finding`). 시작·종료 시점 모두 `git status` clean, 검토 중 어떤 파일도 수정하지 않았다.
- 파일 수: 시작·종료 모두 **631** (`git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md`) — prompt 명시값과 일치.
- Aggregate SHA-256: 시작·종료 모두 `ba0b6c526ee2193046b0a0e78b2e5ace6b5ba384fd23eff7e1f43b5566327969` — prompt 명시값과 정확히 일치. 재현 절차: `git ls-files ...`가 이미 내는 순서 그대로 각 파일의 `sha256sum` 출력을 그 순서대로 이어 붙인 뒤 그 전체를 다시 `sha256sum`(`LC_ALL=C sort`는 파일 목록 자체가 이미 그 순서이므로 추가 재정렬을 하지 않는 것이 핵심 — 개별 해시 라인을 값 기준으로 재정렬하면 다른 값이 나옴을 직접 확인했다).

## 2. iteration 10 finding 8건 / iteration 13 병합 finding 1건 — 해소 판정

이번 회차 prompt의 "우선 검증" 절은 iteration 10의 8건이 아니라 **iteration 13 병합 finding 1건(S5-13-01, root-cause family: S5-12-02와 동일 계열의 새 반례)**을 수정 commit `26a4cbb81`로 명시했다. iteration 10의 8건은 iteration-13 finding-ledger §1에 "iteration 10 8건 전부 해소 판정"으로 이미 양 리뷰어 합의 종결되어 있고, 이번 prompt는 그 8건에 대한 새로운 반례나 세부 항목을 제시하지 않는다. 재지적 규칙(이전 iteration에서 resolved 판정된 finding은 이전 finding ID·수정 commit·새 반례 없이 재론하지 않음)에 따라 iteration 10의 8건은 재검토 대상에서 제외하고, 이번 회차에서 실제로 명시된 S5-13-01a/b/c만 소스 대조로 검증했다.

| ID | 판정 | 근거 |
|---|---|---|
| S5-13-01a (등록 원자성 / lock order) | **해소** | `monitor_api.cpp:354` `set_monitor_handler_state()`가 `state->dispatch_sync`를 4개 필드(`socket_handler`/`socket_handler_userdata`/`snapshot_provider`/`snapshot_subject`) publish(361-364행)부터 `state->dispatch_task_id = task_id`(388행) 저장까지 보유한다. `monitor_handler_task`(154행)도 handler를 load하기 전에 동일 락을 취한다 — 따라서 immediate tick(및 self-close finalizer의 ID snapshot)은 등록 identity가 완전히 commit된 뒤에만 진행 가능하다. Lock order 안전성: `_slot_sync`류와 달리 `dispatch_sync`는 `add_periodic_task` 내부의 `_sync`(`service_control_runtime.cpp`)와 중첩되는데, `set_monitor_handler_state`가 `dispatch_sync`를 쥔 채 `add_periodic_task`를 호출(373행)해 `_sync`를 안쪽에서 취하는 한편, scheduler `loop()`는 `call.fn(call.arg)`(`service_control_runtime.cpp:246`)를 **`_sync`를 해제한 뒤**(240-244행 블록이 닫히고 246행에서 호출) 실행하므로 `monitor_handler_task`가 `dispatch_sync`를 취하는 시점에 `_sync`는 이미 풀려 있다. 즉 lock order는 `dispatch_sync → _sync` 단방향이며 역방향(`_sync`를 쥔 채 `dispatch_sync`를 기다리는) 경로는 코드에 없다 — 데드락 불가능함을 직접 확인했다. `finalize_monitor_handler_self_close`가 `dispatch_task_id`를 락 없이 읽는 지점(116행)도, mutex의 unlock-happens-before-lock 관계로 `set_monitor_handler_state`의 저장(388행)이 `monitor_handler_task`의 락 획득(154행)보다 먼저 일어났음이 보장되므로 정합적이다. |
| S5-13-01b (scheduler strong rollback) | **해소** | `service_control_runtime.cpp:94-103` `add_periodic_task`가 `schedule_task_locked`의 `bad_alloc`을 catch해 `_tasks.erase(inserted.first)`로 방금 삽입한 entry를 되돌리고 `errno=ENOMEM`, `0`을 반환한다. 스케줄 슬롯 없이 `_tasks`에만 남는 반쪽 entry(루프에서 보이지 않지만 ID는 여전히 점유)가 사라졌다. |
| S5-13-01c (회귀 테스트) | **해소** | `test_monitor_socket_contract.cpp:1482-1533` `test_monitor_handler_attach_with_queued_events_and_self_close`가 이벤트를 큐에 미리 쌓은 뒤(50ms 대기) handler를 attach하고, 첫 콜백에서 `self_close_monitor_handler`가 `zlink_monitor_close`를 호출(1471-1479행)하는 self-close를 10회 반복한다. `probe.close_rc == ZLINK_CLOSE_OK`를 확인하고, close 후 stray tick이 있다면 crash할 여유를 주기 위해 50ms 추가 대기(1528행)한다. `main()`의 `RUN_TEST` 목록(1552행)에 등록되어 실제로 실행됨을 확인했다. |

3건 모두 해소로 판정. 새로 연 항목 없음(반례 없음).

부가 검증(이번 회차 자체 조사): `zlink_mesh_node_monitor_handler`(`mesh_monitor_api.cpp`)는 periodic dispatch task가 아니라 emit 시점에 `monitor->mutex` 아래서 동기적으로 핸들러를 호출하는 별개 아키텍처이므로, S5-13-01과 같은 "immediate tick이 등록 커밋보다 먼저 실행" 클래스의 결함이 구조적으로 성립하지 않는다.

## 3. I1 / I2 / I3 — 전체 scope 재검토

이번 iteration에서 이전 검토 기준선(iteration 13이 검토한 `7c7fb0feb`) 이후 core/src에 반영된 commit은 `26a4cbb81` 단 1개뿐이며(`git log 7c7fb0feb..26a4cbb81 --oneline`으로 확인), diff는 `monitor_api.cpp`(+6)·`service_control_runtime.cpp`(+10/-1)·`test_monitor_socket_contract.cpp`(+76)뿐이다(`git show --stat`). Known risk 4곳(`auto_hwm`/`ypipe`·`mailbox`/소켓 teardown/`ctx_term`)과 mesh 런타임 핵심 파일(`mesh_runtime.cpp/hpp`, `mesh_actor_api.cpp`, `mesh_transfer_api.cpp` 등)은 이 diff에 포함되지 않는다.

### I1 — 계약 구현 일치

Finding: 없음.

Evidence: §2의 diff 3파일을 라인 단위로 재검증했고, `07-monitoring.md` §1("Handler mode and receive mode are mutually exclusive")과 대조해 `set_monitor_handler_state`의 수정이 기존 공개 계약(핸들러 우선 공개 후 실패 시 원상복구, `EBUSY`/`ENOMEM`/`ETERM` 분기)을 바꾸지 않고 원자성만 강화했음을 확인했다. `set_monitor_handler_state`의 유일한 호출부는 `monitor_socket_api.cpp`(raw socket monitor 전용)이며, MeshNode monitor(`mesh_monitor_api.cpp`, spec §2·§3)는 별도의 emit-time 동기 디스패치 구조라 이번 수정과 무관함을 호출부 grep으로 확인했다. `add_periodic_task`의 반환 계약(성공 시 0이 아닌 ID, 실패 시 0+errno)은 강화된 rollback 이후에도 동일하게 유지된다(공개 API가 아니라 내부 서비스 계약이지만, 유일한 소비자인 monitor 등록 경로의 오류 매핑이 변하지 않았다). Package metadata(§6)와 known risk(§5)도 diff 범위 밖이라 이전 iteration의 CLEAN 판정이 그대로 유지된다.

Verdict: **CLEAN**

### I2 — POSD·DDD

Finding: 없음(blocker/high/medium).

Evidence: `dispatch_sync`의 보유 구간을 "publish 시작"부터 "task ID 저장"까지로 넓힌 것은 `monitor_handler_state_t`가 이미 소유하고 있던 락을 사용해 "핸들러 registration의 완전한 identity"라는 하나의 불변식을 한 곳에서 강제한 것이며, 새로운 락이나 공유 상태를 추가하지 않았다. `service_control_runtime_t::add_periodic_task`의 rollback도 이미 존재하던 strong-guarantee 패턴(실패 시 caller가 관찰할 수 있는 흔적을 남기지 않음)을 스케줄러 내부에 국한해 완결시켰다 — `_tasks`/`_schedule` 두 자료구조의 정합성은 `service_control_runtime_t` 자신의 책임 범위 안에서만 복구되고, 호출자(monitor_api.cpp)는 여전히 "0 반환 시 아무 것도 등록되지 않았다"는 단순 계약만 본다. 책임 경계 이동이나 정보 은닉 위반 없음.

Verdict: **CLEAN**

### I3 — 정리 완결성

Finding: 없음.

Evidence: diff 3파일에 TODO/FIXME/XXX/HACK/`#if 0` 없음. scope 전체(631개 파일, internals 제외)에서 동일 마커를 재검색해 6건을 확인했으나 전부 `mktemp`/`mkstemp` 템플릿 문자열("...XXXXXX")이며 실제 마커가 아니다. `core/tests/CMakeLists.txt:82,488-490`에서 `test_monitor_socket_contract`가 `INTEGRATION_TEST_ROOT`에 명시적으로 등록되어 있고, `zlink_warn_unregistered_tests()`(15-26행, `GLOB_RECURSE` 기반 미등록 테스트 자동 경고)가 새 테스트 함수 추가와 무관하게 여전히 작동함을 확인했다. 새 테스트는 기존 파일에 함수 추가 + `RUN_TEST` 등록뿐이라 orphan build target이 생기지 않는다.

Verdict: **CLEAN**

## 4. low finding 목록

없음.

## 5. Known risk 4건 판정

`git log 7c7fb0feb..26a4cbb81`가 core/src 전역에서 정확히 이번 diff 3파일만 건드렸음을 확인했으므로 아래 4개 영역은 모두 이전 판정에서 변경이 없다. 이번 회차에 각 risk를 처음부터 다시 소스로 추적해 독립 재확인했다:

1. **TSAN auto-HWM lock-order** — `ctx_auto_hwm_recalc.cpp:80` `auto_hwm_recalculate_now()`가 `_slot_sync`(ctx 레벨)를 쥔 채 `socket->prepare_auto_hwm_socket_plan()`(103행)을 호출하고, 그 함수(`socket_base.cpp:225`)가 내부에서 `monitor_runtime().sync`(소켓 레벨)를 추가로 취한다 — `_slot_sync → monitor sync` 중첩이 실재한다. 역방향 경로(소켓 레벨 monitor sync를 쥔 채 `_slot_sync`를 기다리는 경로)를 찾기 위해 `monitor_runtime ().sync`를 취하는 6개 파일의 모든 획득 지점(`socket_base_monitor.cpp`/`socket_base_api.cpp`/`socket_base_lifecycle.cpp`/`socket_base.cpp`)을 grep했고, `_slot_sync`는 `ctx.cpp`/`ctx_auto_hwm_recalc.cpp`/`ctx_bootstrap.cpp`/`ctx_termination.cpp` 밖에서는 획득되지 않음을 확인했다 — 직접 호출 그래프상 역방향 경로는 없다. 다만 가상 호출·콜백 경유 간접 경로까지는 정적 검토만으로 완전히 배제할 수 없으므로, 이전 iteration들과 동일하게 "정적 검토로 완전 배제 불가, 신규 반례 없음, 추적 유지"로 판정한다.
2. **raw command mailbox ypipe** — `ypipe.hpp`/`ypipe_base.hpp`/`ypipe_conflate.hpp`/`mailbox.hpp`/`mailbox.cpp`/`i_mailbox.hpp`의 최근 커밋 이력을 확인한 결과 마지막 변경은 `078bc238d`(refactor core source layout, S5 캠페인 이전)이다. 단일 writer/단일 reader 락-프리 큐 설계(`ypipe.hpp:11-16` 주석)이며 S5 mesh/monitor 작업과 무관. 추적 유지, 신규 반례 없음.
3. **raw socket teardown 관찰** — 관련 소켓 계층(`core/src/runtime/sockets/`) 전체가 이번 diff에 포함되지 않는다. 추적 유지, 신규 반례 없음.
4. **`ctx_term` linger** — `ctx.cpp`/`own.cpp`/`session_base.cpp`의 마지막 변경은 `52fe28fa1`(router pipe direction fix, S5 캠페인 이전)로 이번 diff와 무관하다. 계약 일치, finding 아님(이전 판정 유지).

## 6. Package metadata 정적 대조

- `core/CMakeLists.txt`: `project(zlink VERSION 10.0.0 ...)`(11행), `SOVERSION "10"`(1370행).
- `core/packaging/debian/control`: `libzlink10`/`libzlink10-dev`/`libzlink10-dbg`(SOVERSION 10과 일치).
- `core/packaging/debian/changelog`: `zlink (10.0.0-0.1) UNRELEASED`.
- `core/packaging/redhat/zlink.spec`: `Version: 10.0.0`(13행).
- `core/packaging/nuget/package.nuspec`: `<version>10.0.0</version>`(10행).
- `CHANGELOG.md`: 이번 diff에 포함되지 않음(`## 10.0.0 (release candidate)` 헤더 유지, 196개 공개 함수·`contract_public_surface` 게이트 서술과 모순 없음).
- 전부 상호 일치. 불일치 없음.

## 7. 최종 판정

CORE REVIEW CLEAN
