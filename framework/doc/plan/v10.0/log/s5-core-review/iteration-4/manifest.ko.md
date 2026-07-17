# S5 Core 구현 리뷰 manifest — iteration 4 (최종 전체 pass, P4)

## 1. 목적

iteration 3의 유효 finding 5건 수정 + F-I1-01 rejected 판정을 반영한 snapshot의
마지막 전체 pass다. campaign 수정·재리뷰 3회차(기본 4회 예산 내). 두 리뷰어는
delta(10 files, +742/−21)와 F-I1-01 rejected 근거를 재검토한 뒤 전체 scope를
처음부터 재검토하고 I1·I2·I3를 재판정한다.

리뷰어 계약: finding은 이슈·근거(file:line)·영향·수정 범위·검증 방향만 제시.
해결 설계 선택·구현은 coordinator 책임.

## 2. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S5 / 4 (최종 전체 pass) |
| Acceptance candidate commit | `59b3ea9400327e74e80b0f8d74763c89ccfdf141` |
| 직전 candidate | `25617130eee` (iteration 3) |
| Delta | 10 files, +742/−21 |
| Scope 파일 수 | 631 |
| Scope aggregate SHA-256 | `d9621658e16a04c53c646286f3386d99f6ac28faf3196616eec4d71e51a8e413` |

## 3. 반영 사항

[iteration-3 finding ledger](../iteration-3/finding-ledger.ko.md) 참조.

- F-I1-03(재재): bind가 binding 락 획득 뒤 actor를 재검증하고 stale 삽입을
  롤백(`mesh_stream_session_api.cpp` bind 경로)
- N3-I1-01: multicast local record를 remote commit **이전에** 전부
  선구축(`mesh_messaging_api.cpp` publish_common) — commit 후 무실패 move만
- N5: destroy drain의 lock 재획득 직후 `owner_it` 재조회(`mesh_actor_api.cpp`)
- N6: 대상 잃은 주석 제거(`mesh_c_internal.hpp`)
- **F-I1-01: rejected** — spec §5 "이미 commit한 message를 취소하지 않는다"
  조항과 reserve의 capacity-차원 all-or-none 보장을 근거로 코디네이터가
  기각(ledger에 상세). §2.4에 따라 이번 pass에서 두 리뷰어의 재검토 대상.

## 4. 기존 검증 (2026-07-17)

전체 suite 85/85, ASAN 5바이너리 clean, TSAN 잔여=기존 2계열.

## 5. 리뷰어 지시

- 실행 계약 동일(읽기 전용, scope hash 시작·종료, 결과는 `iteration-4/`).
- iteration 3 수정 4건 해소 판정 + F-I1-01 rejected 근거의 수용/반박 판정.
- 전체 scope 처음부터 재검토(P4). 세 축 각각 finding 또는 `없음` +
  `CLEAN`/`NOT CLEAN`. known risk(TSAN 기존 2계열, ctx_term linger) 명시 판정.
- blocker·high 없음 + 세 축 CLEAN이면 마지막 줄 정확히 `CORE REVIEW CLEAN`.
