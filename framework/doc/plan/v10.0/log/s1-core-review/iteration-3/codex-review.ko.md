# Codex S1 Core 정식 스펙 리뷰 — iteration 3

## 실행 증거

| 항목 | 값 |
|---|---|
| Reviewer | Codex agent `/root/codex_plan_review_r1` |
| 입력 파일 수 | 52 |
| 입력 SHA-256 | `cb6a9e554176c62645974a3c61661e92ed7b0f5cacb83fe8f0345b5ece942873` |
| hash 검증 | 시작·종료 모두 일치 |
| repository 수정 | 없음 |
| 결과 | clean 아님 |

| ID | 심각도 | Finding | 대표 근거 |
|---|---|---|---|
| C3-01 | HIGH | 정식 C block의 public 함수 65개씩에서 `ZLINK_EXPORT`가 누락되고 validator가 일부 문서만 검사 | `01-context.ko.md:104`, `02-message.ko.md:89`, `06-polling.ko.md:63`, `08-utilities.ko.md:154`, `socket/06-dealer.ko.md:47` |
| C3-02 | HIGH | SUB/XSUB topic buffer 실패가 payload를 이전한 뒤 발생해 result·errno·ownership을 일관되게 표현할 수 없음 | `socket/03-sub.ko.md:130`, `socket/05-xsub.ko.md:124`, `04-errno-map.ko.md:73` |
| C3-03 | MEDIUM | raw publish `topic_id_`의 NUL, embedded NUL, 길이와 상한 계약이 없음 | `socket/02-pub.ko.md:83`, `socket/04-xpub.ko.md:76` |
| C3-04 | MEDIUM | Spot 정식 spec에 refcount·match index·원격 submit 전략과 framework 언어별 timer 구현이 노출 | `service/03-spot.ko.md:209`, `service/03-spot.ko.md:232`, `service/03-spot.ko.md:271` |

중요 finding이 있으므로 `DOC REVIEW CLEAN`이 아니다.
