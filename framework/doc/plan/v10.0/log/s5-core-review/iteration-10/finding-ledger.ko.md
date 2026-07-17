# S5 Core 리뷰 finding ledger — iteration 10 병합

Snapshot: `a4e91c01d` (631 files, aggregate `536d62e8…489d3c`). 연장 6회차 전체 pass.

## 1. 판정 요약

| 리뷰어 | 판정 | 축 |
|---|---|---|
| R1 Codex | **NOT CLEAN** | I1 NOT CLEAN(F1·F2·F3·F4·F5) · I2 NOT CLEAN(F2·F3) · I3 NOT CLEAN(F6·F7) |
| R2 Claude Sonnet | **NOT CLEAN** | I1 NOT CLEAN(high 1) · I2 CLEAN · I3 CLEAN |

원본: `codex-review.ko.md`(SHA-256 `c30efe36…f155d`),
`claude-sonnet-review.ko.md`(SHA-256 `d8e5401f…fc4b7`),
`codex-raw-output.log`(SHA-256 `e461da94…3095d3`).

두 결과를 병합하면 유효 finding은 8건이다. root-cause family 규칙에 따라
같은 계열은 family로 묶고, 수정은 인스턴스가 아니라 family의 전체 호출
표면을 inventory한 뒤 일괄 반영한다.

## 2. 병합 finding

| ID | 심각도 | 축 | 요약 | Family |
|---|---|---|---|---|
| S5-10-01 (Sonnet high) | high | I1 | `request_timeout_scheduler` idle-exit과 `ensure_started`가 별도 critical section이라 task 고아화 (lost wakeup TOCTOU) | operation lifecycle |
| S5-10-02 (Codex F1) | high | I1 | MeshNode lifecycle generation이 모든 새 node에서 1로 고정 — 같은 RID 재시작 수명 구분·교체 계약(01-mesh-node §4·§5) 미구현 | identity/generation |
| S5-10-03 (Codex F2) | high | I1·I2 | committed timeout task가 stale raw node pointer+low serial만 보관 — node 재생성 주소/serial 재사용 시 ABA 조기 timeout | operation lifecycle |
| S5-10-04 (Codex F3) | high | I1·I2 | monitor registry가 pin 없는 raw state pointer 반환 — 동시 close와 UAF/파괴된 mutex lock (`mutex.hpp:108` abort의 유력 가설) | registry lifecycle pinning |
| S5-10-05 (Codex F4) | medium | I1 | `zlink_actor_join_reply`가 flags·mailbox budget·SNDTIMEO backpressure 계약(04-actor §3) 미적용 | admission/backpressure |
| S5-10-06 (Codex F5) | medium | I1 | acceptor open·option·bind·listen 모든 오류를 EADDRINUSE로 변환 — errno map 위반 | errno mapping |
| S5-10-07 (Codex F6) | medium | I3 | scheduler 회귀 테스트 대기 상한 단위 오류: ms 값 6000을 µs 반환값과 비교해 실효 상한 6ms | test correctness |
| S5-10-08 (Codex F7) | low | I3 | `multipart-atomicity.ko.md`가 제거된 SPOT PUB/SUB 구현·삭제 파일 링크를 current로 설명 | stale docs |

주: 관찰된 두 테스트 실패의 인과가 정리됐다. `unittest_request_timeout_scheduler`
부하 실패의 1차 원인은 S5-10-07(실효 상한 6ms)이고, S5-10-01은 그와 별개로
소스에 실재하는 lost-wakeup race다(부하 A/B에서 구버전 1/19 재현).
`test_monitor_socket_contract`의 `mutex.hpp:108` abort는 S5-10-04가 유력
가설이며 Sonnet의 "iteration-4 기존 계열" 판정과 Codex의 UAF 소스 확정이
양립한다 — 수정은 S5-10-04를 family로 처리한다.

## 3. Known risk 판정 (두 리뷰어 합치)

1. TSAN auto-HWM lock-order — 미해소·추적 유지 (양측 일치, 신규 증거 없음)
2. raw command mailbox ypipe — 미해소·추적 유지 (양측 일치)
3. raw socket teardown 관찰 — 미해소·추적 유지 (양측 일치)
4. `ctx_term` linger — 계약 일치, finding 아님 (Codex 명시 판정)

## 4. Coordinator 처리 방침

- S5-10-01: **수정 완료**(리뷰 병행 중 선반영) — liveness 판정과 task 삽입을
  같은 critical section으로 병합, 예외 안전 순서(기동→삽입) 적용. 검증:
  빌드 부하 A/B 구버전 1/19 실패·수정본 0/19, CPU 포화 15/15, 전체 85/85.
- S5-10-02: generation allocator 도입 — node 시작 시각 기반 단조 값. 관련
  contract test 추가.
- S5-10-03: pending operation이 timeout task handle을 소유하고 terminal
  completion·destroy에서 cancel. callback은 full operation ID(high+low)와
  node lifecycle generation을 검증. operation lifecycle family의 전체 표면
  (등록·완료·destroy·타이머) inventory 후 일괄 반영.
- S5-10-04: monitor registry 조회를 pin 계약으로 전환 — socket monitor와
  mesh monitor의 모든 reader 진입점 inventory 후 일괄 반영. close는 active
  reader 종료 대기.
- S5-10-05: join reply local completion에 mailbox budget admission
  (DONTWAIT→EAGAIN, blocking→SNDTIMEO→ETIMEDOUT, 실패 시 token retry 유지).
- S5-10-06: Boost error code→실제 errno 매핑, EADDRINUSE는 실제 collision만.
- S5-10-07: 테스트 대기 상한을 µs 단위로 통일(6초 = 6,000,000µs).
- S5-10-08: low — ledger §2의 internals 규칙(구현 리뷰 중 internals 수정
  금지, clean·종료 검증 뒤 S5-11에서 최종 반영)에 따라 S5-11 internals
  확정 갱신으로 이관. 후속 정리 목록에 기록.

수정 후 일반 build와 전체 테스트를 통과하면 새 commit을 고정하고, 두
리뷰어가 byte 동일 prompt로 stage 전체를 다시 리뷰한다(iteration 11, 새
§2 절차). sanitizer·공개 API·package 검증은 clean 이후 종료 검증에서 실행한다.

## 5. 수정 반영 결과 (2026-07-18)

| ID | 반영 내용 |
|---|---|
| S5-10-01 | `schedule()`가 liveness 판정·thread 기동·삽입을 한 critical section에서 수행 (`request_timeout_scheduler_internal.cpp`). 기동을 삽입보다 먼저 두어 thread 생성 예외 시 고아 task가 map에 남지 않음 |
| S5-10-02 | `allocate_lifecycle_generation()` — wall-clock ms 앵커 + 프로세스 내 강단조 CAS. node 생성자에서 할당 (`mesh_runtime.cpp:73`, `mesh_runtime.hpp`) |
| S5-10-03 | `pending_operation_t::timeout_task` 소유 도입. guard commit이 gate 개방 전에 task를 op에 인계, 완료 3경로(`complete_pending_operation_with_commit` owner-미존재/정상, `commit_prepared_pending_operation`)와 destroy가 node mutex 밖에서 cancel. scheduler에 firing_thread 기반 self-cancel 지원. callback은 full operation ID(high=lifecycle generation) 검증 |
| S5-10-04 | monitor registry pin 계약: `pin/unpin_monitor_handler_state` + `monitor_state_pin_t`, registry를 std::mutex+cv로 전환, `unregister`·`erase…_and_wait`·self-close finalizer가 reader pin drain을 대기. reader 5개 진입점 전수 전환(zlink_monitor_status, require_monitor_recv_model, socket_monitor_handler 등록, zlink_close 검사 경로, zlink_monitor_close) — 분모: registry state deref 지점 5/5. 부수: 제거된 spot provider dead 선언 3개 삭제 |
| S5-10-05 | 판정 조정: 로컬 completion은 iteration 9 선예약 설계상 수락이 항상 성공하므로 EAGAIN 도달 불가(계약 위반 아님). 실결함은 wire 경로의 flags 무시 — `wire_submit_join_reply`에 `flags_` 관통, `LIBZLINK_UNUSED` 제거. 리뷰어는 iteration 11에서 이 판정을 재검토한다 |
| S5-10-06 | `acceptor_error_to_errno()` — `address_in_use`만 EADDRINUSE, 그 외 실제 errno 보존 (open/reuse/bind/listen 4곳 전수) |
| S5-10-07 | 대기 상한을 µs 단위로 통일 (`SETTLE_TIME*20*1000`) |
| S5-10-08 | S5-11 internals 확정 갱신으로 이관 (후속 정리 목록) |

검증: 일반 build 오류 0, 전체 CTest 85/85 (아래 verification 기록).
