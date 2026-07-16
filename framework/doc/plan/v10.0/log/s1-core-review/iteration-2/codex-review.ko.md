# Codex S1 Core 정식 스펙 리뷰 — iteration 2

## 1. 실행 증거

| 항목 | 값 |
|---|---|
| Reviewer | Codex agent `/root/codex_plan_review_r1` |
| 입력 파일 수 | 52 |
| 입력 SHA-256 | `d0b550591986c1950d1903b3ea3fc0be21bb9c29a32a356de8425892a0c7b731` |
| 입력 hash 검증 | 일치 |
| 결과 | clean 아님 |

Reviewer는 파일을 수정하지 않고 동결 범위 전체를 다시 읽었다.

## 2. Finding

| ID | 심각도 | 요약 | 대표 근거 |
|---|---|---|---|
| C2-01 | HIGH | socket 문서의 complete-message 함수가 header와 inventory에 없고 retained `*_part` 계약과 충돌 | `socket/README.ko.md:45`, `01-pair.ko.md:18`, `02-pub.ko.md:84`, `03-sub.ko.md:119`, `08-stream.ko.md:67` |
| C2-02 | HIGH | STREAM raw·part·packet 계약이 모순되고 complete-message 경계를 구현할 수 없음 | `socket/08-stream.ko.md:17`, `socket/README.ko.md:601` |
| C2-03 | HIGH | Actor join이 Spot lifecycle generation을 지정·보존하지 않음 | `service/03-spot.ko.md:53`, `service/04-actor.ko.md:49`, `service/04-actor.ko.md:181` |
| C2-04 | HIGH | Context 정식 spec에 제거된 SpotNode 이중 socket 구조가 남음 | `01-context.ko.md:188`, `service/03-spot.ko.md:13` |
| C2-05 | HIGH | atomic counter export 설명이 header·inventory와 반대 | `08-utilities.ko.md:34`, `core/include/zlink/core/api.h:148` |
| C2-06 | HIGH | 존재하지 않는 `zlink_subscribe`에는 output capacity가 없어 안전하게 구현할 수 없음 | `socket/03-sub.ko.md:119` |
| C2-07 | MEDIUM | 길이 없는 subscription setter를 binary-safe라고 잘못 규정 | `socket/03-sub.ko.md:67` |
| C2-08 | MEDIUM | option owner, retry profile, sentinel과 lock 구현 같은 내부 구조가 public spec에 노출 | `socket/README.ko.md:422`, `07-monitoring.ko.md:186`, `08-utilities.ko.md:29` |
| C2-09 | MEDIUM | Utilities 한영 heading 순서가 다름 | `08-utilities.ko.md:11`, `08-utilities.md:12` |
| C2-10 | MEDIUM | socket index가 번호 없는 STREAM 파일명을 표시 | `socket/README.ko.md:580` |

## 3. 판정

중요 finding이 있으므로 `DOC REVIEW CLEAN`이 아니다. 수정 뒤 새 hash를 동결하고 Codex와 Claude
Sonnet 리뷰를 모두 다시 실행한다.
