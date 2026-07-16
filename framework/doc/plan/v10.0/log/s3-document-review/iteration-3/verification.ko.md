# S3 문서 자동 검증 — iteration 3

## 1. 동결 전 검증

| 검증 | 결과 |
|---|---|
| Core formal 역방향 inventory | `S1 FORMAL REVERSE INVENTORY CLEAN`; target identifier 468개, 한국어·영문 25쌍 일치 |
| Framework exact contract | `FRAMEWORK DOC CONTRACTS CLEAN`; 5개 언어, exact 문서 23개, formal 문서 47개, 공개 선언 1,157개 |
| builder 전환 mapping | owner 20개, 현재 source member 263개에 exact-once 결정 |
| E2E feature map | 55개 map, canonical scenario 전용 행 950개 |
| E2E 영향 inventory | 55/55 lane, 누락·중복 0 |
| sample 영향 inventory | 32/32 lane과 sample 루트 안내 4/4 |
| runner 영향 inventory | 96/96 파일 |
| guide·internals 영향 inventory | 81/81 파일 |
| 상대 link·anchor·render | 221개 render, link 1,051개, table 문서 155개, fence 문서 115개, 오류 0 |
| 임시 plan 참조 | 동결 scope no-hit |
| 문서 금지 표현 | 동결 scope no-hit |
| whitespace | `git diff --check` 통과 |

병렬 S4가 현재 Core header를 변경하고 있으므로 S1의 현재-checkout 정방향 inventory는 S3의 동결 근거로
다시 사용하지 않는다. S1에서 고정한 기준 commit과 현재 정식 spec의 역방향 inventory를 대조한다.

## 2. 동결 식별자

```text
base_commit b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4
scope_file_count 221
scope_sha256 fa592ab14863f414080303881e1c3a31dcecdac8e2f2946ddee642c479c24f9a
formal_target_identifier_count 468
formal_target_identifier_sha256 2778708021ddee185cfba2c09f065f1a0c1156231b433e0d2148cb5e0a657b98
```

## 3. 리뷰 뒤 확인할 항목

- 두 reviewer 시작·종료 scope hash 일치
- reviewer process 정상 종료와 실제 provider·model·session ID
- raw output SHA-256
- finding별 1차 소스 검증 결과
- 수정 여부와 다음 iteration 필요 여부

## 4. 리뷰 중단 결과

동결 뒤 coordinator가 C++ exact interface와 HTTP hosting 정식 spec에서 내부 source 경로와 구현 책임을
발견했다. Codex와 Claude Sonnet은 유효한 최종 판정을 내기 전에 중단했으며 iteration 3은 clean이 아니다.
수정 범위는 [`finding-ledger.ko.md`](./finding-ledger.ko.md)에 기록한다.
