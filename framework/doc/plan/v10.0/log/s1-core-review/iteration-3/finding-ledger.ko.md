# S1 Core 정식 스펙 finding ledger — iteration 3

| ID | 병합 finding | 출처 | 상태 |
|---|---|---|---|
| F3-01 | 모든 formal public 함수에 exact `ZLINK_EXPORT`를 넣고 전체 25쌍 validator로 누락 차단 | C3-01 | 수정 완료 — 392개 선언 위치·175개 고유 함수에서 누락 0개 |
| F3-02 | SUB/XSUB buffer 부족에서 record를 보존하고 required size·재호출·ownership을 result map과 통일 | C3-02 | 수정 완료 — `03-sub.*`, `05-xsub.*`, errors·errno map 일치 |
| F3-03 | raw publish topic 문자열·길이·오류 계약 확정 | C3-03 | 수정 완료 — `02-pub.*`, `04-xpub.*`에 입력·크기·allocation 실패 계약 명시 |
| F3-04 | Spot spec에서 내부 자료구조·전송 전략·framework timer backend 설명 제거 | C3-04 | 수정 완료 — `service/03-spot.*`에서 구현 세부 no-hit |
| F3-05 | PUB/SUB option enum의 exact type·숫자 값 추가 | S3-01 | 수정 완료 — 네 socket 문서의 enum이 공개 header와 일치 |
| F3-06 | 생성 경로 없는 Actor transfer operation kind 제거 | S3-02 | 수정 완료 — formal scope에서 orphan enumerator no-hit |
| F3-07 | inbound Node/Channel application claim 수신 계약 추가 | S3-03 | 수정 완료 — `service/01-mesh-node.*`에 claim owner·record 계약 명시 |
| F3-08 | raw ROUTER receive의 항상 빈 Spot output 제거 | S3-04 | 수정 완료 — `socket/07-router.*` signature와 inventory disposition 일치 |

자동 검증은 정방향·역방향 inventory, 한·영 C block 25쌍, export 전수 검사, 52개 local link·fence와
`git diff --check`를 통과했다. 다음 판정은 iteration 4에서 동결한 52개 전체를 다시 읽는 Codex와
Claude Sonnet 독립 리뷰가 내린다.
