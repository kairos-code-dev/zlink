# S3 문서 자동 검증 — iteration 4

| 검증 | 결과 |
|---|---|
| Core formal 역방향 inventory | `S1 FORMAL REVERSE INVENTORY CLEAN`; target identifier 468개, 한국어·영문 25쌍 |
| Framework exact contract | `FRAMEWORK DOC CONTRACTS CLEAN`; exact 23개, formal 47개, 공개 선언 1,153개 |
| builder 전환 mapping | owner 20개, source member 263개 exact-once |
| E2E feature map | 55개 map, canonical scenario 전용 행 950개 |
| E2E·sample·runner·guide inventory | 55·32·4·96·81 전부 존재 |
| 상대 link·anchor·render | 221개, link 1,050개, table 문서 155개, fence 문서 115개, 오류 0 |
| 임시 plan 참조·문서 금지 표현 | 동결 scope no-hit |
| formal 내부 source·계획 표현 | formal scope no-hit |
| whitespace | `git diff --check` 통과 |

병렬 S4가 Core header를 변경하고 있으므로 현재-checkout 정방향 inventory는 S3의 계약 근거로 사용하지
않는다. S1 기준 commit과 현재 formal spec의 역방향 inventory를 대조한다.

```text
base_commit b0e4af22652b60831e6ba5c4daec4fdcdaa7fce4
scope_file_count 221
scope_sha256 f55b2bffe92f9576d5f330c62696a44c0ab277d0ff5a7421fbadf460852b9306
formal_target_identifier_count 468
formal_target_identifier_sha256 2778708021ddee185cfba2c09f065f1a0c1156231b433e0d2148cb5e0a657b98
```

## 리뷰 결과

| reviewer | scope 시작·종료 hash | 결과 | 판정 |
|---|---|---|---|
| Codex agent | `f55b2b…9306` / `f55b2b…9306` | 원칙·1차 소스 finding 29건 | 수정과 전체 재리뷰 필요 |
| Claude Sonnet | `f55b2b…9306` / `f55b2b…9306` | session `88b071c4-655e-4dfd-adeb-6a5262457422`, finding 71건 | 수정과 전체 재리뷰 필요 |

두 reviewer의 raw output은 보존했고 [`finding-ledger.ko.md`](./finding-ledger.ko.md)가 Codex 29/29와
Claude Sonnet 71/71을 owner와 red gate에 연결한다. iteration 4는 finding이 있었으므로 clean iteration이
아니다.

## 수정 뒤 closure

- coordinator가 현재 checkout에서 100건을 전부 다시 대조했고 terminal 판정은 `I4 CLOSURE CLEAN`이다.
- Core formal reverse inventory, Framework contract verifier, full-scope link·anchor·table·fence 검사와
  `git diff --check`가 통과했다.
- Connector reconnect metric의 package owner와 Actor destroy lifecycle처럼 수정 과정에서 드러난 파생
  계약도 정식 owner와 언어별 투영에 반영했다.
- 수정된 문서는 iteration 4 hash와 다르므로 새 iteration에서 두 reviewer 전체 재리뷰를 실행한다.
