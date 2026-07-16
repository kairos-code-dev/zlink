# RouteMesh 10.0.0 문서 Review Manifest Template

## 1. Review identity

| 항목 | 값 |
|---|---|
| Stage | `S3` |
| Iteration | `<N>` |
| Base commit | `<commit SHA>` |
| Working tree diff | `<path 또는 명령>` |
| Scope file hashes | `<sha256sum 결과>` |
| Reviewer | `<Codex agent 또는 Claude Fable>` |
| Provider/model | `<provider와 실제 model identifier/version>` |
| Invocation/session ID | `<ID>` |
| Started at | `<ISO-8601>` |
| Finished at | `<ISO-8601>` |
| Exit status | `<status>` |

## 2. Required input

- `framework/doc/plan/v10.0/s0-scope-baseline.ko.md`
- `framework/doc/plan/v10.0/framework-route-mesh-messaging-consolidation.ko.md`
- `framework/doc/plan/v10.0/mesh-node-core-api-review.ko.md`
- `framework/doc/plan/v10.0/mesh-node-framework-dispatch-design.ko.md`
- `core/doc/spec/core/service/mesh-node.ko.md`와 영문 문서
- `core/doc/spec/core/service/dispatch.ko.md`와 영문 문서
- `core/doc/spec/core/service/spot.ko.md`와 영문 문서
- `core/doc/spec/core/service/actor.ko.md`, `stream-session.ko.md`와 영문 문서
- `core/doc/spec/core/socket/router.ko.md`, `stream.ko.md`와 영문 문서
- `core/doc/spec/core/polling.ko.md`, `monitoring.ko.md`, `errno-map.ko.md`, `errors.ko.md`와 영문 문서
- `core/doc/spec/core/00-public-contract-governance.ko.md`와 검토 stage가 변경한 한국어·영문 정식 owner 문서
- 임시 구현 차이 추적이 필요하면 `framework/doc/plan/v10.0/s1-core-implementation-tracking.ko.md`
- S2에서 변경한 framework 공통·server 정식 spec, .NET exact interface와 임시 영향 inventory 전부
- `core/include/zlink.h`와 include되는 public header 전부
- `doc/principal/documentation/documentation-principles.ko.md`
- `doc/principal/software-design-principles.md`

## 3. Review questions

1. Core 정식 spec의 함수, type, result, ownership, timeout과 close 계약만으로 구현할 수 있는가?
2. Core 정식 spec의 한국어·영문 의미가 같은가?
3. framework 정식 spec과 언어별 interface가 Core 계약을 확대하거나 축소하지 않는가?
4. E2E, sample, package consumer와 runner 영향이 파일·scenario 단위로 빠짐없이 기록됐는가?
5. 삭제 API, bridge, SpotNode PUB/SUB plane, Core worker pool과 production in-memory fallback이 현재 목표
   계약에 남아 있지 않은가?
6. 기술문서 작성 원칙의 독자·질문·근거·검증·문체·current-state 경계를 지키는가?
7. POSD의 깊은 모듈과 정보 은닉, DDD의 MeshNode·Spot·Actor·session 책임 경계를 지키는가?

## 4. Required verification

```text
<link, heading, signature parity, stale symbol, duplicate와 formatting 명령>
```

## 5. Output contract

각 finding은 다음 형식을 사용한다.

```text
[축][심각도] file:line — 문제 — 1차 소스 근거 — 수정 제안
```

finding이 없으면 마지막 줄에 정확히 다음 문구를 쓴다.

```text
DOC REVIEW CLEAN
```

## 6. Raw output evidence

| 항목 | 값 |
|---|---|
| Raw output path | `<path>` |
| Raw output SHA-256 | `<checksum>` |
| Process completed | `<yes/no>` |
