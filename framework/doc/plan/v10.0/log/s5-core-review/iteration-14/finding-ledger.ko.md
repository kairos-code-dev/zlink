# S5 Core 리뷰 finding ledger — iteration 14 병합

Snapshot: `26a4cbb81` (631 files, aggregate `ba0b6c52…327969`).

## 1. 판정 요약

| 리뷰어 | 판정 | 축 |
|---|---|---|
| R1 Codex | **NOT CLEAN** | I1 NOT CLEAN(high 1) · I2 NOT CLEAN(high 1, 동일 family) · I3 CLEAN |
| R2 Claude Sonnet | **CLEAN** (`CORE REVIEW CLEAN`) | I1·I2·I3 CLEAN, low 0 |

S5-13-01a/b/c는 양 리뷰어 모두 해소 판정. 잔여 root cause 1개 — S5-13-01b가
연 scheduler allocation 경계의 더 깊은 층위(수정 commit의 새 반례).

## 2. 병합 finding

| ID | 심각도 | 축 | 요약 | Family |
|---|---|---|---|---|
| S5-14-01 (Codex I1=I2) | high | I1·I2 | scheduler allocation 실패의 3개 미봉인 경로: ① `_tasks.insert` 자체가 try 밖 + auto-HWM C 경로(`zlink_ctx_set`→`ctx_t::set`→`add_periodic_task`)에 예외 봉인 없음 → bad_alloc이 C ABI 탈출, ② `wakeup_task`가 deschedule 후 allocating 재삽입을 catch 없이 수행, ③ loop가 매 tick allocating 재삽입 + thread entry catch 없음 → worker bad_alloc 시 std::terminate | error atomicity (scheduler allocation) |

## 3. Coordinator 해소 기록 (2026-07-18)

구조적 해법: **task의 schedule node를 add 시점에 1회 할당하고, 이후 모든
재스케줄은 `multimap::extract`/node 재삽입으로 무할당화**.

- `task_entry_t::cached_node` 도입 — deschedule은 extract로 node를 보존,
  `schedule_task_locked`는 rekey 후 move 재삽입(무할당·no-throw).
- `add_periodic_task`: emplace부터 최초 schedule insert까지 전 구간을 하나의
  sealed transaction으로 — 실패 시 완전 rollback + `ENOMEM`/0. 이로써
  wakeup·periodic reschedule·remove는 task 존재 후 allocation-free.
- loop를 pass당 단일 due call 처리로 재구성해 `due_calls` vector 할당 제거.
  thread entry `run()`에 최후 방어 bad_alloc seal.
- C ABI 봉인: `zlink_ctx_set_ext`(zlink_ctx_set/set_data 공용)와
  `zlink_ctx_auto_hwm_recalculate`에 bad_alloc→`ENOMEM` barrier.

수정 후 일반 build·전체 테스트 → 새 commit → iteration 15 전체 재리뷰.
