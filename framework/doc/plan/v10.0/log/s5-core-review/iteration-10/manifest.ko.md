# S5 Core 구현 리뷰 manifest — iteration 10 (연장 6회차, 전체 pass)

## 1. 목적

iteration 9의 두 독립 리뷰를 병합해 확인한 finding 7건을 수정한 commit
`a4e91c01d`를 대상으로, 최신 Core 전체 scope를 처음부터 다시 검토하는 전체
pass다. 이전 리뷰 결과를 clean 근거로 재사용하지 않으며, blocker·high·medium
finding이 0건이고 두 리뷰어의 I1·I2·I3가 모두 `CLEAN`일 때만 S5를 종료한다.

리뷰어는 finding마다 이슈, `file:line` 근거, 영향, 수정 범위와 검증 방향을
기록한다. 해결 설계와 구현 선택은 coordinator가 담당한다.

## 2. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S5 / 10 (연장 전체 pass) |
| Review kind | 구현 리뷰 |
| Campaign / pass | S4+S5 / P4 최종 적대 검토 |
| Acceptance candidate commit | `a4e91c01d` (`core(mesh): resolve S5 iteration-9 findings`) |
| 직전 candidate | `f5000d2fe7d2f9f7bc50eaa9ae23c978d3b54e85` |
| Scope 파일 수 | 631 |
| Scope aggregate SHA-256 | `536d62e84fb7f00df811098da619697481ac717e9d626fdb08e27412dc489d3c` |
| Scope 정의 | `git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md` 각 파일 SHA-256의 aggregate |
| R1 | Codex agent |
| R2 | Claude Sonnet (ledger §2.1 R2-CODE) |
| Coordinator | Claude Fable (리뷰어 아님) |

## 3. iteration 9 finding과 반영 사항

[iteration 9 finding ledger](../iteration-9/finding-ledger.ko.md)의 병합 7건을
다음과 같이 반영했다.

1. request transaction(`operation_submission_t`, `operation_timeout_guard_t`)이
   operation·reply route·timeout 준비를 소유하고 commit 전 return은 소멸자
   rollback을 거친다.
2. `completion_reservation_t`가 terminal completion storage를 operation 등록
   시점에 선예약하고, Actor·STREAM domain 전이는 terminal admission과 같은
   lock의 non-allocating callback으로 commit한다.
3. Debian·RPM·NuGet recipe를 10.0.0/SOVERSION 10에 맞추고
   `contract_public_surface`가 세 recipe의 version·ABI 이름을 검사한다.
4. dead forward declaration 4개와 `pending_operation_t::deadline_ms` 필드
   제거, 제거 식별자 재사용 gate(`REUSED_IDENTIFIER`) 추가, C ABI OOM 정책을
   `submit_out_of_memory_result()` 한 곳으로 이동.

## 4. 리뷰 snapshot 사전 검증 결과 (coordinator 실행, 2026-07-18)

reviewer는 이 결과를 자신의 판정으로 재사용하지 않으며, 각자 별도의 detached
worktree에서 같은 commit과 scope hash를 확인한 뒤 전체 scope를 독립 검토한다.

- `cmake --build core/build -j20`: 성공.
- `ctest --test-dir core/build -j8`: **실패 4건** 발생. 직렬 재실행에서
  `test_multi_socket_contract_regressions`, `unittest_poller` 2건은 통과
  (병렬 부하 flake로 추정)했으나 다음 2건은 재현성 있게 실패했다.
  - `unittest_request_timeout_scheduler`
    `test_cancel_while_handler_is_firing_waits_for_handler_completion`:
    3/3 실패. 1ms deadline handler가 6초 안에 진입하지 않음
    (`Expected 7820 to be less than or equal to 6000`).
  - `test_monitor_socket_contract`: 반복 실행에서 다수 Subprocess aborted,
    `core/src/runtime/utils/mutex.hpp:108` `Invalid argument`.
- **정정 (2026-07-18T06:03+09:00)**: 위 실패·재실행은 모두 coordinator의
  sanitizer `-j20` 병렬 빌드가 실행 중인 고부하 환경(load average ~43)에서
  수행된 것으로 확인됐다. 빌드 종료 후 유휴 상태(load ~1)에서 두 테스트를
  각 5회 재실행한 결과 10/10 전부 통과했다. 두 실패는 소스 결함이 아니라
  부하 유발 timing flake로 판정한다. `unittest_request_timeout_scheduler`는
  1ms deadline handler 진입 대기(상한 6초)가 부하로 지연된 것이고,
  `test_monitor_socket_contract`의 mutex abort는 부하 환경에서만 재현됐다.
  리뷰어는 이 판정을 그대로 수용하지 말고 두 테스트의 부하 내성(테스트
  상한의 타당성 포함)과 mutex.hpp:108 경로가 계약 위반인지 여부를
  독립적으로 판정해 I1에 반영한다.
- 유휴 상태 전체 재실행 (2026-07-18T06:07~06:39+09:00, 2회):
  `ctest --test-dir core/build -j4` → **`100% tests passed, 0 tests failed
  out of 85`**. flake 정정 판정과 일치한다.
- ASAN/TSAN 빌드 갱신: 성공 (test 실행은 위 실패 원인 확인 뒤 수행).
- `git diff --check`: 성공, working tree clean.

## 5. Known risk

다음 네 항목은 이전 pass와 동일하게 각 리뷰어가 현재 source와 실행 증거에
대조해 명시적으로 판정한다.

1. TSAN auto-HWM lock-order 계열.
2. TSAN raw command mailbox ypipe 계열.
3. raw socket teardown 관찰(`pipe_t::detach_peer_backref`, Asio `blob_t`).
4. `ctx_term` linger.

## 6. 리뷰어 지시

- 고정 commit `a4e91c01d`를 checkout한 read-only detached worktree만 검토한다.
- 시작과 종료에 scope 파일 수와 aggregate SHA-256을 기록한다.
- iteration 9의 모든 finding 해소 여부를 먼저 판정한 뒤, 최신 Core
  source·test·spec·internals·package 범위 전체를 처음부터 재검토한다.
- §4의 재현 실패 2건을 독립 재현하고 원인·심각도를 판정한다.
- I1 계약 구현 일치, I2 POSD·DDD, I3 정리 완결성을 각각 판정한다.
- 자동 test 성공만으로 clean을 선언하지 않고, public contract,
  concurrency·lifecycle·ownership·오류 원자성, 제거 범위와 package
  metadata를 source에서 대조한다.
- blocker·high·medium finding이 없고 세 축이 모두 `CLEAN`이면 결과 마지막
  줄을 정확히 `CORE REVIEW CLEAN`으로 쓴다. 그 외에는
  `CORE REVIEW NOT CLEAN`으로 쓴다.

## 7. Session 기록

| 항목 | R1 Codex | R2 Claude Sonnet |
|---|---|---|
| 시작 | 2026-07-18T06:00+09:00 | 2026-07-18T06:00+09:00 |
| 종료 | 기록 예정 | 기록 예정 |
| Invocation/session ID | 기록 예정 | 기록 예정 |
| 결과 경로 | `codex-review.ko.md` | `claude-sonnet-review.ko.md` |
| Exit status | 기록 예정 | 기록 예정 |
