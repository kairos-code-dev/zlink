# S5 Core 구현 리뷰 manifest — iteration 8 (연장 4회차, 전체 pass)

## 1. 목적

iteration 7의 병합 finding 3건(역순 lifecycle 창 high 포함)과 검증 중 발견한
spec 미달 1건(N7-C1)을 전부 수정했다. 이 iteration은 그 수정을 반영한
snapshot의 전체 pass다. 미해결 medium+ 0건 + 세 축 CLEAN까지 반복한다.

리뷰어 계약: finding은 이슈·근거(file:line)·영향·수정 범위·검증 방향만 제시.
해결 설계 선택·구현은 coordinator 책임.

## 2. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S5 / 8 (연장 전체 pass) |
| Acceptance candidate commit | `ee8036a09` |
| 직전 candidate | `f8c35e6fe` (iteration 7) |
| Scope 파일 수 | 631 |
| Scope aggregate SHA-256 | `269b6c1b17aab31c4b74979d2eb8c61482ce102d49da63de3f06c2cba7c632ff` |
| Scope 정의 | `git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md` 각 파일 sha256의 aggregate |

## 3. 반영 사항

[iteration-7 finding ledger](../iteration-7/finding-ledger.ko.md) 참조.

1. **N7-I1-01/CS7-I1-02(high)** — registry lifecycle pinning:
   `pin_node_lifecycle`(shutdown, RAII unpin, destroy claim 시 EDEADLK) +
   `claim_node_destroy`(배타 점유, 이중 destroy ESTALE) +
   `unregister_node_and_wait_lifecycle_quiesced`(pin 0까지 대기 후 teardown)
   (`mesh_runtime.{hpp,cpp}`, `mesh_node_api.cpp`). 신규 결정적 test
   `test_destroy_waits_for_pinned_shutdown`(pause hook).
2. **N7-I1-02/CS7-I1-01(medium/high)** — OOM/예외 매핑 전면화: 공개
   submit-family 25 진입점 function-try-block 외곽 장벽,
   `complete_operation` nothrow+장벽(ingress/timer 스레드 terminate 불가),
   ingress dispatch 메시지 단위 장벽, ENOMEM 오매핑 2건 교정. 신규 test
   `test_submit_alloc_failure_maps_to_out_of_memory`(alloc fault hook,
   기록 준비·publish 준비·operation 등록 3지점).
3. **N7-C1(coordinator 발견)** — ready handler 해제의 spec 미달 정정:
   같은 스레드 재진입만 EDEADLK, 타 스레드 (해)등록은 진행 중 callback
   반환까지 대기(spec 02-dispatch:176-177). 이전 "churn EDEADLK 부하 오염"
   분류를 이 결함의 간헐 발현으로 정정.
4. **N7-I3-01/CS7-I3-01(low)** — CHANGELOG lifecycle 12 case 갱신.

## 4. 기존 검증 (2026-07-18, candidate `ee8036a09`)

- 전체 suite 85/85 (lifecycle 12/12 — 신규 2 test 포함).
- ASAN 5 mesh 바이너리(12·3·6·8·12 = 41 case) 리포트 0.
- TSAN 단독: lifecycle 12/12(경고 17=auto-HWM 13+mailbox 1+pipe teardown
  1+asio blob 2, 전부 기존 계열), stress 3/3 **2회 연속**(경고 3). N7-C1
  수정 전 간헐 실패의 재현 소멸 확인. 신규 mesh race 0.
- TSAN 2-process admission 3건은 baseline 재현으로 delta-무관 확정 유지.

## 5. Known risk (명시 판정 대상, 동일 4건)

1. TSAN auto-HWM lock-order 계열(기존).
2. TSAN raw command mailbox ypipe 계열(기존).
3. raw socket teardown 관찰(pipe_t::detach_peer_backref·asio blob_t) — 9.x
   raw 기계 계열.
4. ctx_term linger(기존).

## 6. 리뷰어 지시

- 실행 계약 동일: core/ 읽기 전용, scope hash 시작·종료 기록, 결과는
  `iteration-8/`에 R1=`codex-review.ko.md`, R2=`claude-sonnet-review.ko.md`.
- iteration 7 수정 4건(N7-C1 포함)의 해소 판정 + 전체 scope 재검토 +
  I1·I2·I3 축별 판정 + known risk 4건 명시 판정.
- blocker·high·medium 없음 + 세 축 CLEAN이면 마지막 줄 정확히
  `CORE REVIEW CLEAN`.
