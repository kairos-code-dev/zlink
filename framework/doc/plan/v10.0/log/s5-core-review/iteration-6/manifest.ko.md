# S5 Core 구현 리뷰 manifest — iteration 6 (연장 2회차, 전체 pass)

## 1. 목적

iteration 5에서 두 리뷰어(Codex·Claude Sonnet)가 동일 4지점으로 수렴했고
(§9 잔여 문구·slot_base OOM 매핑·CHANGELOG 수치·race test 관측), 4건 전부
수정했다. 이 iteration은 그 수정을 반영한 snapshot의 전체 pass다. 미해결
medium+ 0건 + 세 축 CLEAN까지 반복한다.

리뷰어 계약: finding은 이슈·근거(file:line)·영향·수정 범위·검증 방향만 제시.
해결 설계 선택·구현은 coordinator 책임.

## 2. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S5 / 6 (연장 전체 pass) |
| Acceptance candidate commit | `b1e6c81fb` |
| 직전 candidate | `c8d567c64` (iteration 5) |
| Scope 파일 수 | 631 |
| Scope aggregate SHA-256 | `6fa7e8c66ef878cff76356c9208dbf194a3dffc7f494948d91b87f51cd7656d8` |
| Scope 정의 | `git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md` 각 파일 sha256의 aggregate |

## 3. 반영 사항 (iteration 5 병합 finding 4건)

[iteration-5 finding ledger](../iteration-5/finding-ledger.ko.md) 참조.

1. **F-I1-01 잔여 / CS-I1-02** — spec §9(ko/en)의 Logical Multicast 원자성
   문구를 §7 capacity admission 보장에 종속시키고 reserve~commit 사이 peer
   이탈이 §7 unreachable 규칙을 따름을 명시
   (`core/doc/spec/core/service/01-mesh-node.{ko.md,md}` §9).
2. **N5-I1-01 / CS-I1-01** — `slot_base` storage 확보를 try 안으로 이동
   (`mesh_messaging_api.cpp` publish_common): 선예약 경로의 모든 할당이
   `ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM`으로 매핑.
3. **N5-I3-01 / CS-I3-01** — CHANGELOG 검증 수치 최신화(85/85, peer admission
   12 case, lifecycle contracts 9 case, 기계 관찰 4건).
4. **N5-I3-02 / CS-I3-02** — race test가 두 binder의 반환값을 캡처해 합법
   결과 집합 {OK, INVALID_STATE, NOT_FOUND, BACKPRESSURED}을 단정하고, 주석을
   실제 단정 범위로 정합화(성공-후-rollback은 외부 관측점이 없어 설계
   논증으로 닫힘을 명시 — F-I1-03 해소는 iteration 5에서 양측 확정).

## 4. 기존 검증 (2026-07-17, candidate `b1e6c81fb`)

- 전체 suite 85/85 (iteration 5의 Sonnet 독립 재실행 + 수정 후 재실행).
- ASAN: iteration 5에서 5 mesh 바이너리 clean, 수정 후 lifecycle 재실행 clean
  (이번 delta의 코드 변경은 publish 선예약 try 범위와 test 단정뿐).
- TSAN: iteration 5 단독 실행 lifecycle 9/9·stress 3/3, mesh 신규 race 0.
  2-process admission 3건은 baseline 재현으로 delta-무관 확정(iteration-5
  manifest §4).

## 5. Known risk (명시 판정 대상, iteration 5와 동일 4건)

1. TSAN auto-HWM lock-order 계열(기존).
2. TSAN raw command mailbox ypipe 계열(기존).
3. raw socket teardown 관찰(pipe_t::detach_peer_backref·asio blob_t) — 9.x
   raw 기계 계열.
4. ctx_term linger(기존).

## 6. 리뷰어 지시

- 실행 계약 동일: core/ 읽기 전용, scope hash 시작·종료 기록, 결과는
  `iteration-6/`에 R1=`codex-review.ko.md`, R2=`claude-sonnet-review.ko.md`.
- iteration 5 수정 4건의 해소 판정 + 전체 scope 재검토 + I1·I2·I3 축별 판정
  + known risk 4건 명시 판정.
- blocker·high·medium 없음 + 세 축 CLEAN이면 마지막 줄 정확히
  `CORE REVIEW CLEAN`.
