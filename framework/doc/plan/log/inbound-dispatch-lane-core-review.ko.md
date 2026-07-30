# Core clean review — byte HWM과 Application·Completion pair

이 문서는 [inbound dispatch lane 설계](../inbound-dispatch-lane-design.ko.md) §8.2의 2단계
Core clean review 기록이다. Round마다 candidate, reviewer, finding과 처리 결과를 남긴다.
판정 규칙은 설계 문서 §8.2를 따른다. 같은 candidate에서 두 reviewer가 모두 `Medium` 이상
finding을 0건으로 보고할 때만 `CLEAN`이다.

## Round 1

### 검토 입력 (CR-01)

| 항목 | 값 |
| --- | --- |
| Candidate commit | `d7d682bb1f` |
| 비교 기준 commit | `8bc2aa6786` (count 기반 HWM 의미) |
| Candidate worktree | `/tmp/zlink-core-candidate-d7d682bb1f` (읽기 전용) |
| 전체 diff | `git diff 8bc2aa6786..d7d682bb1f -- core bindings/c` (128 file, +4,086 / −2,534) |
| 공통 review 입력 | `framework/doc/plan/inbound-dispatch-lane-design.ko.md`, `AGENTS.md`, spec 작성 지침, source comment 원칙, software design 원칙, `core/doc` Core 정식 spec, C public header, Core source·test·benchmark·monitoring, 위 diff, [reqrep multipart rollback review](inbound-dispatch-lane-reqrep-multipart-rollback-review.ko.md) §9 |
| 공통 prompt | `core_review_prompt.md` rubric v1 (설계 문서 §8.2의 다섯 질문과 severity 표를 그대로 사용) |

두 reviewer에게 같은 prompt와 같은 candidate를 주고, 한 reviewer의 결과를 다른 reviewer에게
제공하지 않았다. Candidate는 별도 worktree로 고정했으므로 review 중 main 작업 tree의 변경이
검토 대상에 섞이지 않는다.

Candidate에 포함된 stage 1 수정 이력은 다음과 같다.

| commit | 내용 |
| --- | --- |
| `563e11d614` | reply submit의 completion credit 회복, lb·DEALER·dist multipart rollback |
| `58aa55df8b` | multipart byte admission을 per-call HWM writer로 한정 |
| `0830b29317` | inproc transport pair readiness 발행과 pair readiness key 정리 |
| `af2ef1e558` | perf fixture reply retry를 측정 경로 밖으로 이동 |
| `d7d682bb1f` | memory amplification 하네스와 C-07 증거 기록 |

### 실행 기록 (CR-02, CR-03)

| Reviewer | Model | Reasoning | 실행 | 결과 |
| --- | --- | --- | --- | --- |
| Codex 5.6 High | `gpt-5.6-sol` | high | 2026-07-30 진행 | 기록 대기 |
| Claude Fable | 보고서 기록값 | 보고서 기록값 | 2026-07-30 진행 | 기록 대기 |

### Finding (CR-04)

기록 대기.

### 판정 (CR-07)

기록 대기.
