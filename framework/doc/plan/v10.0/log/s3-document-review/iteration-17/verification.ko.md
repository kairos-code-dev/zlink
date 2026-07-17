# S3 문서 자동 검증 — iteration 17

## 1. 동결 전 검증

| 검증 | 결과 |
|---|---|
| Framework exact contract | `FRAMEWORK DOC CONTRACTS CLEAN`; exact 24, connector exact 4, formal 53, 공개 선언 1,164 |
| builder 전환 mapping | owner 20, source member 263 exact-once |
| E2E feature map | 55개 map, canonical scenario 전용 행 955개 |
| 실제 Markdown render | Python Markdown Unicode slug로 202개 문서의 local Markdown 링크 1,786개 검사, file·anchor 오류 0 |
| JSON | `framework/doc` 아래 JSON 15개 parse 통과 |
| whitespace | 관련 변경의 `git diff --check` 통과 |
| scope 안정성 | 동시 scope 수정이 멈춘 뒤 현재 202개 파일별 hash와 aggregate를 계산해 동결 |

```text
base_commit 169c458ed238228d7a23cea089c8c467c96b953c
scope_file_count 202
scope_sha256 dd8d4039c8a5397900a561776053d1f26803925a8b7ea3e961a28c62410c9371
scope_list_sha256 dfabc81f2c0ec7403683bc54ab3f350d9cf6565aac60a3baf9579192c513642f
```

## 2. 리뷰 뒤 확인할 항목

- [x] 두 reviewer 시작 뒤 scope hash가 달라져 iteration을 무효 처리
- [x] Claude Sonnet의 실제 provider·model·session ID와 중단 결과 기록
- [x] 무효 처리한 두 review output SHA-256 기록
- [ ] 새 안정 revision에서 두 reviewer 시작·종료 hash 일치
- [ ] 새 안정 revision에서 두 reviewer의 마지막 줄이 각각 `DOC REVIEW CLEAN`

```text
review_start_scope_sha256 dd8d4039c8a5397900a561776053d1f26803925a8b7ea3e961a28c62410c9371
drift_scope_sha256 8c0bfa0459629f8c3e5874025994aa8349f762275e27570c4b98438f5598c5d4
drift_files 4
```
