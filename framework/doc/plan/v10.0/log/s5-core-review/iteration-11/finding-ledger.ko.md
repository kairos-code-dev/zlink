# S5 Core 리뷰 finding ledger — iteration 11 병합

Snapshot: `c1c579ad1` (631 files, aggregate `56a1b0c1…a01305`). 새 §2 절차 1회차.

## 1. 판정 요약

| 리뷰어 | 판정 | 축 |
|---|---|---|
| R1 Codex | **NOT CLEAN** | I1 NOT CLEAN(high 3·medium 1) · I2 NOT CLEAN(high 2, I1과 동일 root cause) · I3 NOT CLEAN(medium 1) |
| R2 Claude Sonnet | **CLEAN** (`CORE REVIEW CLEAN`) | I1·I2·I3 모두 CLEAN, low 0 |

원본: `codex/review.ko.md`, `claude-sonnet/review.ko.md`, raw log 각 디렉터리.
두 판정이 갈렸으나 Codex finding은 전부 수정 commit `c1c579ad1`에 대한
이전에 없던 구체적 반례를 갖추고 있어(재지적 규칙 충족) 유효 finding으로
채택한다. gate는 두 리뷰어 모두 clean을 요구하므로 수정 후 iteration 12
전체 재리뷰를 연다.

## 2. 병합 finding (root-cause family 기준)

| ID | 심각도 | 축 | 요약 | Family |
|---|---|---|---|---|
| S5-11-01 (Codex I1-1) | high | I1 | generation allocator의 앵커가 wall clock이 아니라 boot-relative `CLOCK_MONOTONIC`(`clock_t::now_ms`) — 재부팅 후 역행, 같은-ms 타 process 재시작 시 동일 값 가능. 01-mesh-node §4·§5 위반 지속 | identity/generation |
| S5-11-02 (Codex I1-2=I2-1) | high | I1·I2 | Actor join 계열 terminal 경로 3곳(`mesh_actor_api.cpp:447`·`:1424`·`:1476`)이 operation 직접 erase — timeout task 미회수로 deadline까지 ctx·raw node pointer 누적. terminal lifecycle 지식이 공통 owner 밖으로 누출 | operation lifecycle |
| S5-11-03 (Codex I1-3=I2-2) | high | I1·I2 | monitor handler 등록이 pin한 state를 쓰지 않고 `set_monitor_handler_state()`에서 registry 재조회·부재 시 재생성 — 동시 close와 교차하면 닫힌 socket에 새 state·periodic task 생성 | registry lifecycle pinning |
| S5-11-04 (Codex I1-4) | medium | I1 | Windows acceptor 오류가 Winsock 코드(예: 10013)를 그대로 errno로 노출 — 기존 `wsa_error_to_errno()` 미사용 | errno mapping |
| S5-11-05 (Codex I3-1) | medium | I3 | S5-10-01 lost-wakeup 수정의 회귀 테스트 부재 — idle-exit 경계 교차 stress 단위 테스트 필요 | test coverage |

low: 없음 (양측 모두 0).

## 3. Known risk 4건

양측 일치: 1~3 추적 유지(신규 확정 반례 없음, sanitizer 종료 gate에서 재확인),
4 `ctx_term` linger는 계약 일치로 finding 아님.

## 4. Coordinator 처리 방침

- S5-11-01: 앵커를 epoch wall clock(`std::chrono::system_clock`) ms로 교체,
  프로세스 내 CAS 강단조 유지. 같은 RID 연속 재생성 시 generation 강증가
  contract test 추가. (클록 역행 하의 재부팅 순서는 wall-clock 방식의 일반
  한계로 문서화 — durable 발급은 Core 밖 authority 소관)
- S5-11-02: 공통 primitive `detach_pending_operation_locked()`(erase하며 task
  반환)를 mesh_runtime에 추가하고, `core/src/api`+runtime 전체의
  `operations.erase` 전 지점을 분모 있는 inventory로 전수 전환. cancel은
  항상 node mutex 밖에서.
- S5-11-03: 등록 경로에 expected-state 원자 갱신
  (`update_monitor_handler_state(socket, expected, …)` — registry lock 아래
  entry==expected && !unregistered일 때만 갱신, 아니면 실패) 도입. 생성
  경로(open)와 갱신 경로(handler 등록)를 분리.
- S5-11-04: `_WIN32`에서 `wsa_error_to_errno(ec.value())` 사용.
- S5-11-05: idle-exit 경계(100ms)를 반복 교차하는 stress-shaped 단위 회귀
  추가 — 매 회 1ms task가 상한 내 정확히 한 번 fire.

수정 후 일반 build·전체 테스트 → 새 commit 동결 → iteration 12 전체 재리뷰.
