# S3 문서 자동 검증 — iteration 5

## 1. 동결 전 검증

| 검증 | 결과 |
|---|---|
| Core formal 역방향 inventory | `S1 FORMAL REVERSE INVENTORY CLEAN`; target identifier 468개, 한국어·영문 25쌍 |
| Framework exact contract | `FRAMEWORK DOC CONTRACTS CLEAN`; exact 24개, formal 48개, 공개 선언 1,133개 |
| builder 전환 mapping | owner 20개, source member 263개 exact-once |
| E2E feature map | 55개 map, canonical scenario 전용 행 950개 |
| E2E·sample·runner·guide inventory | 55·32·4·96·81 전부 존재 |
| 상대 link·anchor·render 구조 | 227개, link 1,146개, table block 437개, fence 문서 122개, 오류 0 |
| 임시 plan 참조·문서 금지 표현 | 동결 scope no-hit |
| formal 내부 source·계획 표현 | formal scope no-hit |
| whitespace | `git diff --check` 통과 |

병렬 S4가 Core header를 변경하고 있으므로 현재-checkout 정방향 inventory는 S3의 계약 근거로 사용하지
않는다. S1 기준 commit과 현재 formal spec의 역방향 inventory를 대조한다.

```text
base_commit b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4
scope_file_count 227
scope_sha256 f1d9cdc5c2e18d79dbac1a68d2a501d66b39a920f731033b6b71f88091954d1c
formal_target_identifier_count 468
formal_target_identifier_sha256 2778708021ddee185cfba2c09f065f1a0c1156231b433e0d2148cb5e0a657b98
```

## 2. 리뷰 뒤 확인할 항목

- [x] 두 reviewer 시작·종료 시 scope hash 일치
- [x] reviewer process 정상 종료와 실제 provider·model·session ID
- [x] raw output SHA-256
- [x] finding별 1차 소스 검증 결과
- [x] 수정 여부와 다음 iteration 필요 여부

## 3. iteration 5 리뷰 결과

| reviewer | 범위 | 시작·종료 aggregate | 결과 |
|---|---:|---|---|
| Codex agent | `227/227` | `f1d9cdc5...` 동일 | finding 27건, clean 아님 |
| Claude CLI `claude-sonnet-5` | `227/227` | `f1d9cdc5...` 동일 | 형식화된 finding 39건, clean 아님 |

Claude session ID는 `0630768c-e1fe-4f7f-a546-807359ec159c`이며 정상 종료했다. Raw output SHA-256은
`7f99440e36a08f7eb8fcde189ae91bc8014a17189046cc90008393723a45b437`다. 두 결과의 finding을 개별
owner와 red gate에 연결한 뒤 수정하며, 다음 iteration은 새 scope hash를 사용한다.

Claude raw summary에는 40건이라고 적혀 있으나 `[축][severity]` 형식을 가진 finding line은 39건이다.
Ledger는 재현 가능한 원문 line 39건을 기준으로 삼고 이 집계 차이를 숨기지 않는다.

Codex raw output SHA-256은
`eb7833705aaa9dc7e15aed63bb7389a0d495a923b9b3e0843c6070f3e9acfa5a`다.

## 4. Closure

형식화된 원문 finding 66건을 모두 owner와 red gate에 연결해 수정했다. Java Stream Connector 표현
1건은 두 reviewer가 같은 문제를 보고한 중복이다. 수정 뒤 framework contract verifier, Core·C++·나머지
언어 scoped 구조 검사와 문서 범위 `git diff --check`가 통과했다. Iteration 6은 사용자 지시에 따라 Core
전수 리뷰를 제외한 framework 문서 177개를 새 aggregate hash로 동결한다.
