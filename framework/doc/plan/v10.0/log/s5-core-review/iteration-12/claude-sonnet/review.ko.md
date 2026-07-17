# S5 Core 구현 리뷰 — iteration 12 — R2 (Claude Sonnet)

## 1. Scope 확인

- 검토 checkout: `/tmp/claude-1000/zlink-s5-it10-sonnet`, detached HEAD `7f9d3e3153e95917e3ad113c3f0fe4b193975445` (`core(mesh): resolve S5 iteration-11 findings`). `git status` clean, 검토 중 파일 수정 없음.
- 파일 수: 시작·종료 모두 **631** (`git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md`) — prompt 명시값과 일치.
- Aggregate SHA-256: 세 가지 방식(파일경로 정렬 후 `sha256sum` per-file → 그 출력 정렬 → 재 `sha256sum`; 동일 파이프라인을 `git show HEAD:<path>` blob 기준으로 재계산; per-file 해시값만 정렬해 재해시)으로 재현했으나 모두 `661109305d977570103c7196aa5fd3a255fb96bc6f8f3c976779aae216f9acbb`로 수렴했고, prompt에 기재된 `539d94abe13a30064208dbf0ac254bfb8b0347242682bac485aff865b0efcce7`와 일치하지 않았다. 파일 수는 정확히 일치하고, 디스크 내용과 git blob 내용이 동일함을 별도로 확인했으므로(작업 트리 drift 아님) 이는 coordinator 측 해시 산출 파이프라인 차이로 보이며 checkout 훼손의 증거는 아니다. **재현 가능한 사실로 기록**하되 finding으로는 등록하지 않는다(요청받은 절차상 사실 기재).
- 검토 중 파일 수정 없음(작업 종료 시점도 시작 시점과 동일한 631 / 위 aggregate 값 유지).

## 2. iteration 11 병합 finding 5건 — 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| S5-11-01 (generation 앵커) | **해소** | `mesh_runtime.cpp` `allocate_lifecycle_generation()`이 `std::chrono::system_clock::now().time_since_epoch()` epoch ms를 앵커로 사용하도록 교체됨. 기존 `now_ms()`(`thread_local clock_t` — boot-relative `CLOCK_MONOTONIC` 캐시)와 명확히 분리. CAS 루프(`compare_exchange_weak` + `candidate = previous + 1` fallback)로 프로세스 내 강단조 유지 로직은 그대로 보존. |
| S5-11-02 (actor join terminal task 회수) | **해소** | `operations.erase` 전체 8개 지점을 전수 대조(분모 확인: `mesh_runtime.cpp` 3곳 — `:1095,:1140,:1271`; `mesh_actor_api.cpp` 3콜(2개 함수) — `:451,:1435,:1488`; `mesh_node_api.cpp` submission rollback 2곳 — `:112,:125`). 새로 손댄 `mesh_actor_api.cpp` 두 함수 모두 erase 직전에 `timeout_task`를 캡처하고, `lock_guard` 스코프 종료(락 해제) 이후 `zlink::request_timeout::cancel()`을 호출하는, 기존 `complete_pending_operation_with_commit`과 동일한 계약을 따른다. `EFAULT`/`ENOMEM` 오류 반환 경로(reservation 비어있음, `ready.insert` `bad_alloc`)는 캡처된 `join_timeout_task`를 취소하지 않고 조기 반환하므로 task가 armed 상태로 유지된다 — 확인됨. `mesh_node_api.cpp`의 두 rollback 지점(`operation_timeout_guard_t` 생성 실패 시, 소멸자에서 미commit 시)은 각각 task가 아예 존재하지 않거나(`_valid==false` → `_state==NULL`) 소멸자가 락 밖에서 호출되어(`delete _timeout`이 `lock_guard` 스코프 밖) 동일 데드락 회피 계약을 지킨다. |
| S5-11-03 (monitor 등록 원자화) | **해소** | `set_monitor_handler_state()`에 `expected_state_` 파라미터 추가. non-NULL(update, 호출자=`attach_socket_monitor_handler_state`가 `monitor_state_pin_t`로 pin한 state)일 때 registry lock 아래 `it->second != expected_state_ \|\| expected_state_->unregistered`를 검증해 불일치 시 `ESHUTDOWN` 반환 및 재생성 안 함. 생성 경로(`open_socket_monitor_with_handler_internal` → `expected_state_=NULL`)와 분리됨. |
| S5-11-04 (Windows errno) | **해소** | `acceptor_error_to_errno()`가 `ZLINK_HAVE_WINDOWS` 분기에서 `zlink::wsa_error_to_errno(ec_.value())`를 사용. coordinator 메모는 `_WIN32`를 언급했으나 실제 구현은 codebase 전역에서 이미 표준으로 쓰이는 `ZLINK_HAVE_WINDOWS`(CMake `check_include_files(windows.h ...)`로 정의, 52개 파일에서 사용) 매크로를 사용해 더 일관적 — 편차이나 결함 아님. |
| S5-11-05 (idle-exit 회귀 테스트) | **해소** | `test_schedule_across_idle_exit_boundary_fires_every_task`가 25회 반복, 매회 1ms 만료 task를 스케줄 → 발화 대기(`wait_until_entered`, 타임아웃 시 명시적 assert 실패) → cancel → `90 + (i % 21)`ms(90~110ms) sleep으로 스케줄러의 100ms idle-exit 경계를 반복 교차시키며 매회 정확히 1회 발화(`entered == 1`)를 검증한다. 기존 `test_cancel_while_handler_is_firing_waits_for_handler_completion`과 동일 단언 패턴을 반복 구조로 확장한 것으로, S5-10-01 lost-wakeup 회귀를 stall 없이 검출 가능. |

5건 모두 해소로 판정. 새로 연 항목 없음.

## 3. I1 / I2 / I3

### I1 — 계약 구현 일치

Finding: 없음.

Evidence: 위 5건 재검증에서 관찰 가능한 동작(오류 코드, 상태 전이, task 수명)이 스펙과 충돌하지 않음을 확인. `01-mesh-node.md` §4는 lifecycle generation의 의미(동일 RID의 새 lifetime 구분, weight 변경과 무관)만 규정하고 특정 clock 소스나 재부팅 순서 보장을 요구하지 않아 wall-clock 앵커 교체와 충돌하지 않음. `07-monitoring.md`/errno map 대조에서 `ESHUTDOWN` 사용이 기존 monitor 오류 분류와 충돌하지 않음(신규 등록 실패를 close-race로 분류하는 것은 상태 오류 계열). Windows errno 매핑은 04-errno-map 계약(POSIX errno 노출)과 일치.

Verdict: **CLEAN**

### I2 — POSD·DDD

Finding: 없음(blocker/high/medium).

Evidence: 새 코드는 기존 `complete_pending_operation_with_commit`/`commit_pending_operation_locked`가 이미 확립한 "timeout_task를 락 아래 캡처하고 락 밖에서 cancel"이라는 계약을 그대로 따라 정보 은닉 경계(누가 task를 소유·취소하는지)를 어기지 않는다. monitor 등록의 expected-state 검증은 registry lock 안에서만 판단해 pin 소유권 경계를 지킨다.

Verdict: **CLEAN**

### I3 — 정리 완결성

Finding: 없음.

Evidence: 변경분에 죽은 코드·미사용 선언·orphan 테스트가 없음. `mesh_node_api.cpp`의 두 rollback 지점은 새 계약과 이미 정합적이라 손대지 않았고 이는 올바른 판단(불필요한 변경 없음).

Verdict: **CLEAN**

## 4. low finding 목록

- **[I2][low]** `core/src/runtime/services/mesh/mesh_runtime.cpp:1077-1141,1230-1272`, `core/src/api/mesh/mesh_actor_api.cpp:381-452,1345-1493` — "`timeout_task`를 락 아래 캡처 → `operations.erase` → 락 해제 후 `cancel()`" 패턴이 이제 두 파일에 걸쳐 5곳(erase 콜 기준)에서 수동 반복된다. iteration-11 병합 ledger의 coordinator 처리 방침은 공통 primitive(`detach_pending_operation_locked()`) 추출을 제안했으나 이번 수정은 기존 `mesh_runtime.cpp` 스타일을 그대로 복제하는 방식으로 반영됐다. 정확성에는 영향 없음(각 사이트 개별 대조 완료) — 향후 유사 terminal 경로 추가 시 반복 실수 위험을 낮추는 리팩토링 후보로 남겨둔다. CLEAN 판정을 막지 않음.

## 5. Known risk 4건 판정

1. **TSAN auto-HWM lock-order** — `auto_hwm_policy.cpp`/관련 소켓 파일은 이번 커밋(`c1c579ad1`→`7f9d3e315`)에서 미변경. iteration-11 판정(추적 유지, 신규 확정 반례 없음) 계승.
2. **raw command mailbox ypipe** — `ypipe.hpp`/`ypipe_base.hpp`/`ypipe_conflate.hpp`/`mailbox.hpp`/`pipe.cpp` 등 미변경. 동일하게 추적 유지, 신규 반례 없음.
3. **raw socket teardown 관찰** — 관련 소켓/teardown 경로 미변경. 추적 유지, 신규 반례 없음.
4. **ctx_term linger** — 계약 일치, finding 아님(iteration-11과 동일).

이번 커밋의 변경분(7개 파일)이 위 4개 known-risk 영역 어디에도 겹치지 않음을 diff로 확인했으므로, 새로운 반례를 만들 수 있는 변경이 없었다.

## 6. 최종 판정

CORE REVIEW CLEAN
