# S5 Core 리뷰 finding ledger — iteration 12 병합

Snapshot: `7f9d3e315` (631 files, aggregate `539d94ab…cce7`).

## 1. 판정 요약

| 리뷰어 | 판정 | 축 |
|---|---|---|
| R1 Codex | **NOT CLEAN** | I1 NOT CLEAN(high 1·medium 1) · I2 NOT CLEAN(medium 2) · I3 **CLEAN** |
| R2 Claude Sonnet | **CLEAN** (`CORE REVIEW CLEAN`) | I1·I2·I3 CLEAN, low 1 |

원본: `codex/review.ko.md`(SHA-256 `08f903be…ed2d`), `claude-sonnet/review.ko.md`.
Codex 세션은 07:39에 provider 필터로 turn이 실패해 §2.2에 따라 같은 thread의
다음 session이 미검토 범위를 정적 대조로 이어받아 완료했다(빌드·테스트
실행 금지 지시 반영). 진행 iteration 11의 5건 중 4건 해소, 1건(generation)
재반례 유지 + 신규 2 family.

## 2. 병합 finding

| ID | 심각도 | 축 | 요약 | Family |
|---|---|---|---|---|
| S5-12-01 (Codex I1-1, S5-11-01 재반례) | high | I1 | epoch ms + process-local atomic은 같은 ms 타 process 동일 RID 재시작·클록 역행에서 순서 보장 실패 | identity/generation |
| S5-12-02 (Codex I1-2=I2-2) | medium | I1·I2 | monitor handler 등록이 handler publish 후 task 확보 — 실패 시 partial state(다음 등록 EBUSY)·`add_periodic_task` bad_alloc이 C 표면 탈출 가능 | error atomicity |
| S5-12-03 (Codex I2-1) | medium | I2 | operation erase/task 인계 지식이 6개 terminal 경로에 분산 — iteration 11 누락의 구조적 원인 | operation lifecycle |

Sonnet low 1 (terminal cancel 패턴 중복)은 S5-12-03과 동일 root cause로 병합.

## 3. Coordinator 해소 기록 (2026-07-18)

- **S5-12-01**: 앵커를 epoch **microseconds**로 상향(충돌 저항 1000×)하고
  다음 판정을 기록한다 — Core는 durable 상태를 소유하지 않는다(S0-21·FD-39:
  location/authority는 framework 소관, "Core는 store를 조회하지 않는다"). 같은
  RID의 동시/동일-µs 수명과 클록 역행은 01-mesh-node §5가 이미 정의한
  duplicate/stale generation admission 충돌로 표면화되며(무단 교체가 아님),
  이는 wall-clock 기반 발급의 문서화된 한계다. durable per-RID 발급자를
  Core에 넣는 제안은 기존 아키텍처 결정과 충돌해 채택하지 않는다. 이 판정은
  code 주석과 이 ledger가 소유하며, iteration 13 리뷰어는 판정 자체의 타당성
  (spec §4·§5 해석)을 검토한다. spec 해석이 리뷰어 간 계속 갈리면 §0.1에 따라
  S1 재개방 여부를 사용자에게 회부한다.
- **S5-12-02**: 이전 등록값 4종 캡처 → publish → task 확보 실패
  시(ETERM·task_id 0·bad_alloc) 이전 값 원상복구 + `ENOMEM`/`ETERM` errno.
  handler 선공개 순서는 유지(주기 tick이 handler 부재 시 소비한 event를
  버리는 창 방지). bad_alloc은 함수 내부에서 봉인.
- **S5-12-03**: `detach_pending_operation_locked()` 깊은 primitive 도입 —
  terminal 6곳(runtime 3, actor 3) 전부 primitive 경유로 전환, cancel은 공통
  계약대로 caller가 mutex 밖 수행. submission rollback 2곳은 pre-commit이라
  guard 소유(예외로 문서화). 잔여 `operations.erase`는 primitive 내부 1곳 +
  rollback 2곳뿐(no-hit 분모 3/3).

수정 후 일반 build·전체 테스트 → 새 commit → iteration 13 전체 재리뷰.
