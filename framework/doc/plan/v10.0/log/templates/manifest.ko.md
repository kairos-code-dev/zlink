# RouteMesh 10.0.0 Review Manifest Template

## 1. Review identity

| 항목 | 값 |
|---|---|
| Stage | `<S3, S5, S7, S8, S10-C, S10-J, S10-N 또는 S11>` |
| Review kind | `<문서 리뷰 또는 구현 리뷰>` |
| Iteration | `<N>` |
| Base commit | `<commit SHA>` |
| Working tree diff | `<path 또는 명령>` |
| Scope file hashes | `<sha256sum 결과>` |
| Reviewer | `<Codex agent, Claude Sonnet 문서 리뷰 또는 Claude Fable 코드 리뷰>` |
| Provider/model | `<provider와 실제 model identifier/version>` |
| Invocation/session ID | `<ID>` |
| Fable fallback | `<사용하지 않음 또는 Fable invocation과 차단 근거>` |
| Started at | `<ISO-8601>` |
| Finished at | `<ISO-8601>` |
| Exit status | `<status>` |

한 iteration에는 Codex agent용 manifest·raw output과 해당 review 유형의 R2 manifest·raw output을
각각 보존한다. S3 문서 리뷰의 R2는 Claude Sonnet이다. Claude Fable은 문서 리뷰에 사용하지 않으며,
S5·S7·S8·S10·S11에서 source·test·build·package 코드와 구현 결과를 검토할 때만 R2로 호출한다.
구현 리뷰에서는 Fable을 먼저 호출한다. Fable이 제공되지 않거나 quota·capacity, 인증·도구 차단 또는 정상 종료 실패로
리뷰를 완료할 수 없을 때만 같은 manifest로 Claude Sonnet을 호출한다. fallback을 사용하면 Fable
invocation과 차단 근거, Sonnet session을 모두 기록한다. 두 리뷰는 같은 frozen scope를 독립 검토한다.
어느 한쪽 결과로 문서, 코드, 테스트, 설정 또는 검증 증거가 수정되면 새 revision을 고정하고 두 리뷰를
모두 다시 실행한다. 구현 리뷰에서 어느 축이 수정되면 두 리뷰어가 I1·I2·I3 전체를 다시 검토한다. 두
결과가 모두 이 stage의 exact clean 문구로 끝나야 review gate를 통과한다.

## 2. Required input

- `framework/doc/plan/v10.0/s0-scope-baseline.ko.md`
- `framework/doc/plan/v10.0/framework-route-mesh-messaging-consolidation.ko.md`
- `framework/doc/plan/v10.0/mesh-node-core-api-review.ko.md`
- `framework/doc/plan/v10.0/mesh-node-framework-dispatch-design.ko.md`
- `core/doc/spec/core/service/01-mesh-node.ko.md`와 영문 문서
- `core/doc/spec/core/service/02-dispatch.ko.md`와 영문 문서
- `core/doc/spec/core/service/03-spot.ko.md`와 영문 문서
- `core/doc/spec/core/service/04-actor.ko.md`, `05-stream-session.ko.md`와 영문 문서
- `core/doc/spec/core/socket/07-router.ko.md`, `08-stream.ko.md`와 영문 문서
- `core/doc/spec/core/06-polling.ko.md`, `07-monitoring.ko.md`, `04-errno-map.ko.md`, `03-errors.ko.md`와 영문 문서
- `core/doc/spec/core/00-public-contract-governance.ko.md`와 검토 stage가 변경한 한국어·영문 정식 owner 문서
- 임시 구현 차이 추적이 필요하면 `framework/doc/plan/v10.0/s1-core-implementation-tracking.ko.md`
- S2에서 변경한 framework 공통·server 정식 spec 전부
- `framework/doc/framework/spec/server/languages/` 아래 .NET·C++·Java·Kotlin·Node exact interface 전부
- 공통·언어별 E2E 문서와 public 예제, 공통·언어별 sample 문서와 public 예제 및 임시 영향 inventory 전부
- `core/include/zlink.h`와 include되는 public header 전부
- `doc/principal/documentation/documentation-principles.ko.md`
- `doc/principal/software-design-principles.md`

## 3. Review kind별 질문

`Review kind`에서 하나만 선택한다. S3은 문서 리뷰이고 S5·S7·S8·S10·S11은 구현 리뷰다. S3의
`DOC REVIEW CLEAN`은 구현 리뷰 세 축의 판정을 대신하지 않으며, 구현 리뷰 결과도 S3 문서 리뷰를
대신하지 않는다.

### 3.1 S3 문서 리뷰 질문

1. Core 정식 spec의 함수, type, result, ownership, timeout과 close 계약만으로 구현할 수 있는가?
2. Core 정식 spec의 한국어·영문 의미가 같은가?
3. framework 정식 spec과 언어별 interface가 Core 계약을 확대하거나 축소하지 않는가?
4. E2E, sample, package consumer와 runner 영향이 파일·scenario 단위로 빠짐없이 기록됐는가?
5. 삭제 API, bridge, SpotNode PUB/SUB plane, Core worker pool과 production in-memory fallback이 현재 목표
   계약에 남아 있지 않은가?
6. 기술문서 작성 원칙의 독자·질문·근거·검증·문체·current-state 경계를 지키는가?
7. POSD의 깊은 모듈과 정보 은닉, DDD의 MeshNode·Spot·Actor·session 책임 경계를 지키는가?

### 3.2 S5·S7·S8·S10·S11 구현 리뷰 필수 축

세 축은 독립된 필수 판정이다. POSD·DDD 한 줄이나 stage 전체 clean 문구로 다른 축을 대체하지 않는다.

| 축 | 질문 | Finding | Evidence | Verdict |
|---|---|---|---|---|
| **I1 계약 구현 일치** | frozen spec에서 누락되거나 잘못 구현된 항목, 관찰 가능한 동작·오류·수명·동시성 불일치가 없는가? | `<ID 목록 또는 없음>` | `<file:line, test, package, 실행 log>` | `<CLEAN/NOT CLEAN>` |
| **I2 POSD·DDD 리팩터링** | POSD·DDD 관점에서 의미 있는 리팩터링 요소가 남아 있지 않은가? | `<ID 목록 또는 없음>` | `<위험 신호, 대안 비교, file:line>` | `<CLEAN/NOT CLEAN>` |
| **I3 정리 완결성** | 불필요·죽은 code·file·test·build target·generated artifact와 alias·adapter·forwarder 등 호환 잔재가 남아 있지 않은가? | `<ID 목록 또는 없음>` | `<scoped no-hit, package 내용, file:line>` | `<CLEAN/NOT CLEAN>` |

각 리뷰어는 세 행을 모두 채운다. 한 축이라도 `NOT CLEAN`이면 stage clean 문구를 출력하지 않는다.
어느 축의 수정이라도 발생하면 새 frozen revision에서 Codex agent와 선택된 R2가 세 축 전체를 다시
검토한다. 구현 리뷰의 R2 선택은 위 Fable 우선 규칙을 매 iteration 적용한다. 두 리뷰어의 세 축이 모두
`CLEAN`이어야 stage exact clean 문구를 인정한다.

## 4. Required verification

```text
<link, heading, signature parity, stale symbol, duplicate와 formatting 명령>
```

## 5. Output contract

각 finding은 다음 형식을 사용한다.

```text
[축][심각도] file:line — 문제 — 1차 소스 근거 — 수정 제안
```

S3 문서 리뷰에서 finding이 없으면 마지막 줄에 정확히 다음 문구를 쓴다.

```text
DOC REVIEW CLEAN
```

구현 리뷰는 먼저 I1·I2·I3 각각의 Finding, Evidence와 Verdict를 출력한다. 세 축이 모두 `CLEAN`일
때만 마지막 줄에 stage에 맞는 exact clean 문구 하나를 쓴다.

| Stage | Exact clean 문구 |
|---|---|
| S5 | `CORE REVIEW CLEAN` |
| S7 | `BINDINGS REVIEW CLEAN` |
| S8 | `DOTNET REVIEW CLEAN` |
| S10-C | `CPP REVIEW CLEAN` |
| S10-J | `JVM REVIEW CLEAN` |
| S10-N | `NODE REVIEW CLEAN` |
| S11 | `FINAL REVIEW CLEAN` |

## 6. Raw output evidence

| 항목 | 값 |
|---|---|
| Raw output path | `<path>` |
| Raw output SHA-256 | `<checksum>` |
| Process completed | `<yes/no>` |
