# S1 Core 정식 스펙 자동 검증 — iteration 4

## 1. 동결 전 결과

| 검증 | 결과 |
|---|---|
| 현재 header 정방향 inventory | `PUBLIC API INVENTORY CLEAN` |
| formal spec 역방향 inventory | `S1 FORMAL REVERSE INVENTORY CLEAN` |
| formal 한·영 C block | 25쌍 일치 |
| public 함수 선언 export | 392개 위치·175개 고유 함수·누락 0개 |
| formal target | FUNC 89·TYPE 31·ENUM_TYPE 16·ENUMERATOR 100·FIELD 213·MACRO 12, 전체 461개 |
| local link와 code fence | 동결 범위 52개 오류 0개 |
| plan 참조·SpotNode·금지 문구 | formal scope no-hit |
| whitespace | `git diff --check` 통과 |

## 2. 동결 식별자

```text
formal_target_identifier_sha256 8eb6071a0aad8851170b6a56e9c2f5622b086144ff90a8dcfcc62a658b004924
frozen_52_file_sha256 a47810550e250538d9fcb2c6a6f530927b4a0112b58d33881f4ae1a03805b3ac
```

독립 리뷰가 끝난 뒤 같은 hash 유지 여부와 두 reviewer 결과를 이 문서에 추가한다.

## 3. 리뷰 결과와 원본 출력

| Reviewer | 결과 | 원본 출력 SHA-256 |
|---|---|---|
| Codex | finding 10건, clean 아님 | `70c666403f9630fbf2335d190f45d071e2d1ff4456c1b95f1c5f4fa22bf2033e` |
| Claude Sonnet | finding 2건, clean 아님 | `ec07556190e99bafd31ff216eca45b94595f5d928b55a8c1e8a668f68ba80f6b` |

두 reviewer 모두 시작·종료 hash가 동결 hash와 일치했고 repository를 수정하지 않았다.

## 4. Finding 수정 뒤 구현 기준선

두 reviewer의 finding 12건을 모두 수정한 뒤 정방향·역방향 inventory, 한·영 C block 25쌍,
52개 local link·fence, 금지 문구와 `git diff --check`가 통과했다.

```text
final_file_count 52
final_frozen_sha256 6cd163bf7fa4b010e3ddac02ea0c6e9cc90fe58622a29f0cc1a50bf85451ea71
formal_target_identifier_count 461
formal_target_identifier_sha256 8eb6071a0aad8851170b6a56e9c2f5622b086144ff90a8dcfcc62a658b004924
```

수정본에 대한 iteration 5 clean 재리뷰는 실행하지 않았다. 사용자가 이 hash를 Core 구현 기준선으로
승인했으며, 구현 중 공개 계약 변경이 필요하면 spec을 먼저 수정하고 문서 리뷰를 다시 연다.
