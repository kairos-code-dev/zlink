# S5 Core 구현 리뷰 manifest — iteration 9 (연장 5회차, 전체 pass)

## 1. 목적

iteration 8의 두 독립 리뷰를 병합해 확인한 finding을 수정했다. 이 iteration은
수정된 최신 Core 전체 scope를 처음부터 다시 검토하는 최종 후보 pass다. 이전
리뷰 결과를 clean 근거로 재사용하지 않으며, blocker·high·medium finding이
0건이고 두 리뷰어의 I1·I2·I3가 모두 `CLEAN`일 때만 S5를 종료한다.

리뷰어는 finding마다 이슈, `file:line` 근거, 영향, 수정 범위와 검증 방향을
기록한다. 해결 설계와 구현 선택은 coordinator가 담당한다.

## 2. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S5 / 9 (연장 전체 pass) |
| Acceptance candidate commit | `f5000d2fe7d2f9f7bc50eaa9ae23c978d3b54e85` |
| 직전 candidate | `ee8036a09e951e89db5730426d6a91a44afdac85` |
| Scope 파일 수 | 631 |
| Scope aggregate SHA-256 | `5eeb7c9010200c38c933d86ad9fa7a8d99d80e16495a0f3015cc74dcbf516255` |
| Scope 정의 | `git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md` 각 파일 SHA-256의 aggregate |

## 3. iteration 8 finding과 반영 사항

[iteration 8 finding ledger](../iteration-8/finding-ledger.ko.md)를 기준으로
다음 변경을 반영했다.

1. 공개 MeshNode 진입점과 timer·claim 내부 callback의 raw handle 검증을
   registry pin으로 통합했다. registry membership과 tag를 같은 lock에서
   확인하고, 이미 진입한 호출이 반환할 때까지 destroy가 storage 해제를
   기다린다. 새 public API나 호출자 설정은 추가하지 않았다.
2. 줄바꿈된 두 공개 submit 함수에도 C ABI `bad_alloc` 장벽을 적용해
   27개 submit 진입점 전체가 `ZLINK_SUBMIT_OUT_OF_MEMORY`와 `ENOMEM`으로
   실패하도록 맞췄다.
3. reply bookkeeping을 소비한 뒤 reply tail 구성에서 storage 부족이
   발생하면 operation completion이 유실되지 않도록
   `ZLINK_REQUEST_INTERNAL_ERROR`와 `ENOMEM` terminal completion으로
   강등했다.
4. 동시 submit/destroy hammer를 추가하고 lifecycle runner와
   `CHANGELOG.md`의 case 수를 13개로 일치시켰다.

## 4. 리뷰 snapshot 사전 검증 증거

아래 결과는 reviewer 실행 전에 coordinator가 candidate
`f5000d2fe7d2f9f7bc50eaa9ae23c978d3b54e85`에서 수행했다. reviewer는 이
결과를 자신의 판정으로 재사용하지 않으며, 각자 별도의 detached worktree에서
같은 commit과 scope hash를 확인한 뒤 전체 scope를 독립 검토한다. 리뷰가
진행되는 동안 coordinator 작업 공간의 이후 변경은 이 snapshot에 반영되지
않는다.

- `cmake --build core/build -j20`: 성공.
- `ctest --test-dir core/build -j8 --output-on-failure`: 85/85 성공.
- `contract_public_surface`: 전체 suite 안에서 성공, formal public surface
  196개와 제거 identifier 부재 유지.
- ASAN mesh 5개 target: lifecycle 13, peer admission 12, stress 3,
  monitor matrix 6, node basic 8의 총 42개 case가 모두 성공하고 report 0.
- TSAN lifecycle: 13/13 성공. warning 18건은 auto-HWM lock-order 14,
  mailbox 1, pipe teardown 1, Asio `blob_t` 2의 기존 계열이며 신규 mesh
  frame은 없다.
- TSAN stress: 3/3 성공. warning 3건은 기존 auto-HWM lock-order 계열이다.
- `git diff --check`: 성공.
- scope에서 0-byte file, merge marker와 금지 문구가 없다.

## 5. Known risk

다음 네 항목은 이전 pass와 동일하게 각 리뷰어가 현재 source와 실행 증거에
대조해 명시적으로 판정한다.

1. TSAN auto-HWM lock-order 계열.
2. TSAN raw command mailbox ypipe 계열.
3. raw socket teardown 관찰(`pipe_t::detach_peer_backref`, Asio `blob_t`).
4. `ctx_term` linger.

## 6. 리뷰어 지시

- 고정 commit을 checkout한 read-only snapshot만 검토한다.
- 시작과 종료에 scope 파일 수와 aggregate SHA-256을 기록한다.
- iteration 8의 모든 finding 해소 여부를 먼저 판정한 뒤, 최신 Core
  source·test·spec·internals·package 범위 전체를 처음부터 재검토한다.
- I1 계약 구현 일치, I2 POSD·DDD, I3 정리 완결성을 각각 판정한다.
- 자동 test 성공만으로 clean을 선언하지 않고, public contract,
  concurrency·lifecycle·ownership·오류 원자성, 제거 범위와 package
  metadata를 source에서 대조한다.
- blocker·high·medium finding이 없고 세 축이 모두 `CLEAN`이면 결과 마지막
  줄을 정확히 `CORE REVIEW CLEAN`으로 쓴다. 그 외에는
  `CORE REVIEW NOT CLEAN`으로 쓴다.
