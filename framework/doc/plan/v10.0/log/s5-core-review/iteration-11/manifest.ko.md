# S5 Core 구현 리뷰 manifest — iteration 11 (전체 pass, 새 §2 절차 1회차)

## 1. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S5 / 11 |
| Review kind | 구현 리뷰 (ledger §2 개정 절차) |
| Acceptance candidate commit | `c1c579ad1` (`core(mesh): resolve S5 iteration-10 findings`) |
| 직전 candidate | `a4e91c01d` |
| Scope 파일 수 | 631 |
| Scope aggregate SHA-256 | `56a1b0c135e9357ee3da1666a45084239f9b4a195b5b4d86ee77b471b9a01305` |
| Scope 정의 | `git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md` 각 파일 SHA-256의 aggregate |
| 공통 prompt | `prompt.md` (SHA-256 `735d401b8deddf75d47588697ef4f42831a2ba038746c5ee5ad032fa16751c6c`) — 두 리뷰어에 byte 동일 전달 |
| R1 | Codex agent — checkout `/tmp/claude-1000/zlink-s5-it10-codex` (local clone, detached `c1c579ad1`) |
| R2 | Claude Sonnet — worktree `/tmp/claude-1000/zlink-s5-it10-sonnet` (detached `c1c579ad1`) |
| internals 규칙 | `core/doc/internals`는 판정 근거·수정 대상에서 제외 (S5-11에서 확정) |
| 종료 조건 | 4회차 이후 규칙: 축별 blocker·high·medium 0건이면 CLEAN, low는 후속 정리 목록 |
| 시작 시각 | 2026-07-18T07:04+09:00 |

## 2. 직전 finding 수정 뒤 통과한 일반 build·전체 테스트

- `cmake --build core/build -j16`: 오류 0.
- `ctest --test-dir core/build -j4`: `100% tests passed, 0 tests failed out of 85` (2026-07-18T07:00 전후 2회).
- 수정 내역과 검증 상세: [iteration 10 finding ledger](../iteration-10/finding-ledger.ko.md) §5.

## 3. 리뷰 clean 뒤 실행할 종료 검증 목록 (coordinator)

1. ASAN/UBSAN mesh 5개 target + scheduler unittest
2. TSAN lifecycle·stress·scheduler (기존 auto-HWM·mailbox·pipe 계열 외 신규 0 확인)
3. `contract_public_surface` (공개 표면 196·package metadata gate)
4. shared library SONAME `libzlink.so.10`·package recipe 정적 검사
5. `git diff --check`·0-byte·merge marker·금지 문구 no-hit

## 4. Session 기록

| 항목 | R1 Codex | R2 Claude Sonnet |
|---|---|---|
| Invocation/session ID | 기록 예정 | 기록 예정 |
| 종료 시각 / exit status | 기록 예정 | 기록 예정 |
| progress | `codex/progress.md` | `claude-sonnet/progress.md` |
| 결과 | `codex/review.ko.md` | `claude-sonnet/review.ko.md` |
| Raw output | `codex/raw-output.log` | `claude-sonnet/raw-output.log` |
