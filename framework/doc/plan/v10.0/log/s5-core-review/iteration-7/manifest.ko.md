# S5 Core 구현 리뷰 manifest — iteration 7 (연장 3회차, 전체 pass)

## 1. 목적

iteration 6의 병합 finding 4건(shutdown/destroy 수명 high 포함)을 전부
수정했다. 이 iteration은 그 수정을 반영한 snapshot의 전체 pass다. 미해결
medium+ 0건 + 세 축 CLEAN까지 반복한다.

리뷰어 계약: finding은 이슈·근거(file:line)·영향·수정 범위·검증 방향만 제시.
해결 설계 선택·구현은 coordinator 책임.

## 2. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S5 / 7 (연장 전체 pass) |
| Acceptance candidate commit | `f8c35e6fe` |
| 직전 candidate | `b1e6c81fb` (iteration 6) |
| Scope 파일 수 | 631 |
| Scope aggregate SHA-256 | `cdbc1b1053c4931d0610968d640c133b5ea9b07964a2a57cd6d379c6f2478af6` |
| Scope 정의 | `git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md` 각 파일 sha256의 aggregate |

## 3. 반영 사항 (iteration 6 병합 finding 4건)

[iteration-6 finding ledger](../iteration-6/finding-ledger.ko.md) 참조.

1. **N6-I1-02(high)**: destroy가 `shutdown_active` 검사로 §11 재진입을
   `ZLINK_CLOSE_BUSY`+`EDEADLK`로 거부. shutdown은 unlock 후 꼬리(이벤트
   방출·완료 전달·wire_stop)까지 `shutdown_active` 유지 후 해제. destroy는
   child 검사와 강제 종료를 한 lock 보유로 병합하고 `unregister_node`를
   teardown 앞으로 이동. errno map(ko/en) close 표에 EDEADLK 재진입 항목
   추가. 신규 test `test_destroy_during_shutdown_wait_is_deadlock_error`.
2. **N6-I1-01+CS6-I1-01(medium)**: submit-family 전반 bad_alloc 장벽 —
   publish_common(검증·snapshot·record 선구축·슬롯 선예약·remote envelope
   leg 롤백), submit_local_record, actor send/request, stream session
   submit, part-copy helper 2종(ENOMEM 반환+호출자 매핑), admit_record
   (mutation 선행+원상 복구), monitor emit queue push(overflow drop과 동일
   행동).
3. **N6-I1-03(medium)**: monitor active-callback close → `EBUSY`(공식 close
   매핑).
4. **N6-I3-01(low)**: services-internals(ko/en)에 `mesh_api.cpp` seam의
   Spot timer registry·turn admission 소유 명시.

## 4. 기존 검증 (2026-07-17, candidate `f8c35e6fe`)

- 전체 suite 85/85 (lifecycle 10/10 — 신규 shutdown/destroy test 포함).
- ASAN 5 mesh 바이너리(10·3·6·8·12 = 39 case) 리포트 0.
- TSAN 단독: lifecycle 10/10(경고 13=auto-HWM 11+mailbox 1+pipe teardown 1,
  전부 기존 계열), stress 3/3(경고 3). 신규 mesh race 0.
- TSAN 2-process admission 3건은 baseline 재현으로 delta-무관 확정 유지
  (iteration-5 manifest §4).

## 5. Known risk (명시 판정 대상, 동일 4건)

1. TSAN auto-HWM lock-order 계열(기존).
2. TSAN raw command mailbox ypipe 계열(기존).
3. raw socket teardown 관찰(pipe_t::detach_peer_backref·asio blob_t) — 9.x
   raw 기계 계열.
4. ctx_term linger(기존).

## 6. 리뷰어 지시

- 실행 계약 동일: core/ 읽기 전용, scope hash 시작·종료 기록, 결과는
  `iteration-7/`에 R1=`codex-review.ko.md`, R2=`claude-sonnet-review.ko.md`.
- iteration 6 수정 4건의 해소 판정 + 전체 scope 재검토 + I1·I2·I3 축별 판정
  + known risk 4건 명시 판정.
- blocker·high·medium 없음 + 세 축 CLEAN이면 마지막 줄 정확히
  `CORE REVIEW CLEAN`.
