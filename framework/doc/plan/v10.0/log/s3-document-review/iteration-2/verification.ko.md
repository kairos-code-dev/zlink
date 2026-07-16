# S3 문서 자동 검증 — iteration 2

## 1. 동결 전 검증

| 검증 | 결과 |
|---|---|
| Core 공개 API 정방향 inventory | `PUBLIC API INVENTORY CLEAN` |
| Core formal 역방향 inventory | `S1 FORMAL REVERSE INVENTORY CLEAN`; target identifier 468개 |
| Core 한국어·영문 C block | 역방향 validator에서 25쌍 일치 |
| Framework exact contract | `FRAMEWORK DOC CONTRACTS CLEAN`; 5개 언어, exact 문서 23개, formal 문서 47개, 공개 선언 1,157개 |
| builder 전환 mapping | owner 20개, 현재 source member 263개에 exact-once 결정 |
| E2E 영향 inventory | 55/55 lane, 누락·중복 0 |
| sample 영향 inventory | 32/32 lane과 언어별 루트 안내 4/4, 누락·중복 0 |
| runner 영향 inventory | 96/96 파일, 누락·중복 0 |
| guide·internals 영향 inventory | 81/81 파일, 누락·중복 0 |
| 상대 link·anchor·render | 221개 render, link 1,048개, table 문서 154개, fence 문서 115개, 오류 0 |
| 제거 topology·stale location API | 동결 scope no-hit |
| 임시 plan 참조 | 동결 scope no-hit |
| formal current-state marker | 정식 계약 47개 no-hit |
| whitespace | `git diff --check` 통과 |

## 2. 동결 식별자

```text
base_commit b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4
scope_file_count 221
scope_sha256 12d908d39baa0a6bbaaaddbdedc465f464dc9bc3e8dc1c03494b0be49b36d749
formal_target_identifier_count 468
formal_target_identifier_sha256 2778708021ddee185cfba2c09f065f1a0c1156231b433e0d2148cb5e0a657b98
```

## 3. 리뷰 뒤 확인할 항목

- 두 reviewer 시작·종료 시 scope hash 일치
- reviewer process 정상 종료와 실제 provider·model·session ID
- raw output SHA-256
- finding별 1차 소스 검증 결과
- 수정 여부와 다음 iteration 필요 여부

## 4. 리뷰 결과

| reviewer | 실행 결과 | 판정 |
|---|---|---|
| Codex agent | 시작·종료 scope hash 일치, 원칙 9건·1차 소스 14건 | 수정 및 전체 재리뷰 필요 |
| Claude Sonnet | session `d4b9ecda-b23e-4b16-99a4-d20aaae7f96e`, 비용 `$33.98180385000003`, 예산 초과 종료 | 유효한 판정 없음 |

iteration 2는 clean이 아니다. 23개 finding을 수정한 뒤 새 revision을 동결하고 두 reviewer를 모두 다시
실행해야 한다.
