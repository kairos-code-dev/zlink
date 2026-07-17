# S5 Core 구현 리뷰 manifest — iteration 3 (최종 전체 pass, P4)

## 1. 목적

iteration 2의 유효 finding 11건(중복 1 병합)을 모두 수정한 snapshot의 마지막
전체 pass다. campaign 수정·재리뷰 2회차(기본 4회 예산 내)다. 두 리뷰어는
delta(30 files, +983/−66)와 직접 영향 범위를 깊게 본 뒤 전체 scope를 처음부터
재검토하고 I1·I2·I3를 재판정한다.

**리뷰어 계약(갱신)**: 리뷰어는 이슈, 근거, 영향, 수정 범위와 검증 방향까지만
제시한다. 구체적인 해결 설계의 선택과 구현은 coordinator·구현 담당자의
책임이다(예: iteration 2의 N-I2-01은 per-Spot backend 제안 대신 per-MeshNode
scheduler로 구현됨 — 이런 설계 대안 선택 자체는 finding 사유가 아니며, 선택된
설계가 계약을 위반할 때만 finding이다).

## 2. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S5 / 3 (최종 전체 pass) |
| Acceptance candidate commit | `25617130eeeb1dc464aec6eca1a8378888aee42a` |
| 직전 candidate | `a01b537f8ce` (iteration 2) |
| Delta | 30 files, +983/−66 (`git diff a01b537f8..25617130e`) |
| Scope 파일 수 | 631 |
| Scope aggregate SHA-256 | `1ca3763f80f568890e2e3e888b4b1dd93e2d15dab9deb9125c12b316fa1f6598` |

## 3. 반영된 finding

[iteration-2 finding ledger](../iteration-2/finding-ledger.ko.md)의 11건 전부
(fixed). 핵심 delta:

- `timer_api.cpp`/`timer_scheduler_backend.cpp`/`timer_api_internal.hpp`:
  destroy 데드락 해소(scheduler lock 해제 후 busy 대기), per-MeshNode spot
  scheduler(`zlink_timer_new_for_spot_node`/`release`), 9.x 잔재 제거
- `mesh_api.cpp`: registry cancelled flag+`spot_timer_cancel`, enter_turn 50ms
  timed wait+재조회
- `mesh_runtime.{hpp,cpp}`/`mesh_monitor_api.cpp`: `monitor_emit_refs` 핀과
  close 대기
- `mesh_wire.{hpp,cpp}`: NODROP commit의 unreachable 제외 회계
  (`unreachable_out_`), `mesh_messaging_api.cpp` snapshot 차감
- `mesh_actor_api.cpp`: destroy의 session control drain+binding 제거,
  iterator 선캡처; `mesh_stream_session_{api.cpp,internal.hpp}`:
  `session_bindings_pending/remove_actor`
- `mesh_wire_ingress.cpp`: `handle_actor_left` maybe_end
- `.github/workflows/core-conan-release.yml`: sha256 필수 gate;
  `core/packaging/conan/`: pycache untrack·README 갱신
- test: `test_timer_destroy_overlapping_fire_completes`(overlap destroy +
  claim-held cancel) 추가 — lifecycle 8 case

## 4. 기존 검증 결과 (2026-07-17)

전체 suite 85/85, ASAN clean(모든 mesh 바이너리), TSAN 잔여는 기존 2계열
(auto-HWM lock-order, raw socket command mailbox ypipe)뿐.

## 5. 리뷰어 지시

- iteration 1·2와 같은 실행 계약(읽기 전용, scope hash 시작·종료, 결과 파일
  2개는 `iteration-3/`에).
- iteration 2 finding 11건의 해소를 각각 판정한 뒤 전체 scope를 처음부터
  재검토한다(P4).
- 세 축 각각 finding 또는 `없음` + `CLEAN`/`NOT CLEAN`. finding에는 이슈·
  근거(file:line)·영향·수정 범위·검증 방향만 담는다.
- 남은 known risk(기존 기계 TSAN 2계열, ctx_term linger)를 명시 판정한다.
- blocker·high가 없고 세 축 모두 CLEAN이면 마지막 줄에 정확히
  `CORE REVIEW CLEAN`.
