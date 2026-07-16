# Codex S1 Core 정식 스펙 리뷰 — iteration 4

## 실행 증거

| 항목 | 값 |
|---|---|
| Reviewer | Codex agent `/root/codex_core_review_r4` |
| 입력 파일 수 | 52 |
| 입력 SHA-256 | `a47810550e250538d9fcb2c6a6f530927b4a0112b58d33881f4ae1a03805b3ac` |
| hash 검증 | 시작·종료 모두 일치 |
| 범위 diff SHA-256 | 시작·종료 모두 `b4bfbaa77d0025ac0d498a4337ab7faab1d0714ac099805adecb014f9e742953` |
| repository 수정 | 없음 |
| 결과 | clean 아님 |

| ID | 심각도 | Finding | 동결본 대표 근거 |
|---|---|---|---|
| C4-01 | HIGH | `zlink_request_result_t`가 socket 공통과 errors에서 `BACKPRESSURED=113` 포함 여부 불일치 | `socket/README.md:299`, `03-errors.md:63` |
| C4-02 | HIGH | MeshName admission 실패가 `CONFLICT`와 `AUTH_FAILED`로 충돌 | `service/01-mesh-node.md:246`, `03-errors.md:150` |
| C4-03 | HIGH | MeshNode `ZLINK_OPT_MAXMSGSIZE` 지원 여부가 socket 공통과 MeshNode에서 반대 | `socket/README.md:421`, `service/01-mesh-node.md:438` |
| C4-04 | HIGH | caller output capacity 부족을 canonical `CONFIG_BUFFER_TOO_SMALL/ENOBUFS`와 다르게 정의 | `socket/README.md:761`, `socket/03-sub.md:174`, `04-errno-map.md:120` |
| C4-05 | HIGH | Actor transfer에서 frozen backlog·post-barrier traffic 이동, high-water ACK와 peer failure 계약 누락 | `service/04-actor.md:344`, `service/04-actor.md:404` |
| C4-06 | HIGH | part 기반 multipart submit 실패 뒤 sequence abort·staging 폐기·재시작 상태 누락 | `socket/01-pair.md:24`, `socket/02-pub.md:114`, `socket/06-dealer.md:132` |
| C4-07 | MEDIUM | claim이 단일 domain을 소유하면서 recv 호출자가 같은 domain을 다시 전달 | `service/02-dispatch.md:249`, `service/02-dispatch.md:268` |
| C4-08 | MEDIUM | service가 peer 목록을 숨긴다는 설명이 공개 peer snapshot API와 충돌 | `service/README.md:7`, `service/01-mesh-node.md:497` |
| C4-09 | MEDIUM | local multicast의 storage 재사용·재인코딩 금지가 formal spec에 구현 전략으로 노출 | `service/01-mesh-node.md:418` |
| C4-10 | MEDIUM | `zlink_msg_gets()`가 모든 호출에서 실패하는 얕은 public API로 남음 | `02-message.md:331` |

중요 finding이 있으므로 `DOC REVIEW CLEAN`이 아니다.
