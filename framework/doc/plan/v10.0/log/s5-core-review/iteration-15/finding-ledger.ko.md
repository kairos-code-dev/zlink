# S5 Core 리뷰 finding ledger — iteration 15 병합

Snapshot: `7b580a520` (631 files, aggregate `cf263060…eefdc5`).

## 1. 판정 요약

| 리뷰어 | 판정 | 축 |
|---|---|---|
| R1 Codex | **NOT CLEAN** | I1 NOT CLEAN(high 1) · I2 NOT CLEAN(동일 family 병합) · I3 CLEAN |
| R2 Claude Sonnet | **CLEAN** (`CORE REVIEW CLEAN`) | I1·I2·I3 CLEAN, low 1 |

S5-14-01의 5개 세부는 양 리뷰어 모두 해소 판정(무할당 재스케줄·sealed add·
단일 due call·C ABI barrier·node handle 소유권). 잔여 1건은 수정 commit이
새로 만든 `run()` seal의 lifecycle 누락.

## 2. 병합 finding

| ID | 심각도 | 축 | 요약 | Family |
|---|---|---|---|---|
| S5-15-01 (Codex I1=I2) | high | I1·I2 | callback bad_alloc 시 `_active_task_id` 해제·broadcast epilogue를 건너뛰고 `run()` 빈 catch가 cleanup 없이 종료 → `remove_task`·`zlink_ctx_term`(auto-HWM task 제거 경유) 영구 대기. auto-HWM recalc callback은 실제로 할당함 | error atomicity (worker lifecycle) |
| S5-15-02 (Sonnet low) | low | I2 | `schedule_task_locked`의 도달 불가 allocating fallback 분기 | 정리 (편승 반영) |

## 3. Coordinator 해소 기록 (2026-07-18)

- **S5-15-01a**: 각 `call.fn` 호출을 bad_alloc catch로 봉인 — 실패 tick은
  버리고 task는 다음 주기 재시도(이미 재예약됨), epilogue(active 해제·
  broadcast)는 무조건 실행.
- **S5-15-01b**: `run()` 최후 catch가 terminal lifecycle을 commit —
  `_stopping=true`·`_active_task_id=0`·broadcast를 한 critical section에서
  수행해 waiter가 깨어나고 죽은 worker가 새 task를 받지 않음.
- **S5-15-01c**: 회귀 unittest `unittest_service_control_runtime` —
  첫 tick에서 bad_alloc을 던지는 periodic task로 worker 생존(3 tick 관찰)·
  `remove_task` 즉시 반환·ctx_term 정상 종료 검증.
- **S5-15-02**: fallback 분기를 `zlink_assert`로 교체(모든 caller가
  deschedule 선행 — 무할당 계약 명시).

수정 후 일반 build·전체 테스트 → 새 commit → iteration 16 전체 재리뷰.
