# S5 Core 리뷰 finding ledger — iteration 13 병합

Snapshot: `7c7fb0feb` (631 files, aggregate `62999e63…91a3b1`).

## 1. 판정 요약

| 리뷰어 | 판정 | 축 |
|---|---|---|
| R1 Codex | **NOT CLEAN** | I1 NOT CLEAN(high 1) · I2 NOT CLEAN(high 1, 동일 family) · I3 CLEAN |
| R2 Claude Sonnet | **CLEAN** (`CORE REVIEW CLEAN`) | I1·I2·I3 CLEAN, low 0 |

수렴 상태: generation ruling은 **양 리뷰어 모두 spec 근거로 수용**
(01-mesh-node §4·§5가 duplicate/stale을 admission conflict로 정의, durable
발급 요구 없음 — S1 재개방 불필요로 종결). S5-12-02·03과 iteration 10 8건
전부 해소 판정. 잔여 root cause는 1개.

## 2. 병합 finding

| ID | 심각도 | 축 | 요약 | Family |
|---|---|---|---|---|
| S5-13-01 (Codex I1=I2) | high | I1·I2 | monitor handler 등록의 immediate dispatch task가 `dispatch_task_id` 저장 전에 첫 callback을 실행할 수 있고, 허용된 callback self-close와 교차하면 finalizer가 ID 0을 snapshot → task 미제거 → 다음 tick이 삭제된 state 재사용(UAF). `add_periodic_task`의 `_tasks` insert 후 `_schedule` insert bad_alloc 시 partial entry 잔존 | error atomicity (S5-12-02 family, 수정 commit의 새 반례) |

## 3. Coordinator 해소 기록 (2026-07-18)

- **S5-13-01a (등록 원자성)**: `set_monitor_handler_state`가
  `state->dispatch_sync`를 publication부터 `dispatch_task_id` 저장까지 보유.
  `monitor_handler_task`는 handler load 전에 같은 lock을 취하므로 immediate
  tick의 callback(및 self-close finalizer의 ID snapshot)은 등록 identity가
  완전히 commit된 뒤에만 진행된다. lock order 안전성 검증: scheduler는
  `_sync`를 해제한 뒤 task를 호출하므로 `dispatch_sync→_sync` 단방향.
- **S5-13-01b (scheduler strong rollback)**: `add_periodic_task`의
  `schedule_task_locked` bad_alloc 시 `_tasks` entry를 제거하고
  `ENOMEM`/0 반환.
- **S5-13-01c (회귀 테스트)**: `test_monitor_handler_attach_with_queued_events_and_self_close`
  — 이벤트가 큐에 쌓인 상태에서 handler attach → 첫 callback self-close를
  10회 반복, `ZLINK_CLOSE_OK`와 stray tick 부재 확인.

수정 후 일반 build·전체 테스트 → 새 commit → iteration 14 전체 재리뷰.
