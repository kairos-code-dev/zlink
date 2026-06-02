# Java/Kotlin Framework Draft 문서

이 디렉토리는 Java/Kotlin framework의 실행 계획, 후속 초안, 정식 계약으로 승격하지
않은 설계를 보관한다. 구현과 regression test로 확인된 사용자 문서는
[`../guide`](../guide/01-overview.ko.md), 공개 계약은 [`../spec`](../spec/README.ko.md),
내부 기준은 [`../internals`](../internals/regression-test-matrix.ko.md)를 우선해서
읽는다.

## 실행 계획

| 문서 | 범위 |
|------|------|
| [implementation-execution-plan](./implementation-execution-plan.ko.md) | phase, gate, evidence, completion prompt |
| [java-kotlin-framework-porting-plan](./java-kotlin-framework-porting-plan.ko.md) | 장기 포팅 범위와 package 계획 |
| [java-framework-completion-prompt](./java-framework-completion-prompt.ko.md) | 구현 실행용 프롬프트 |

## 후속 초안

| 문서 | 범위 |
|------|------|
| [stream-open-items](./stream-open-items.ko.md) | stream 후속 편의 기능 후보 |
| [stage-wrapper-on-spot](./stage-wrapper-on-spot.ko.md) | Spot 위 stage wrapper 설계 참고 |

## 승격된 문서의 원본

아래 문서는 정식 문서로 승격된 원본 초안이다. 새 작업에서는 정식 문서를 먼저 고치고,
draft 원본은 후속 설계 비교가 필요할 때만 참고한다.

| 승격 위치 | 원본 |
|-----------|------|
| [`../guide`](../guide/01-overview.ko.md) | [`guide/`](./guide/01-overview.ko.md) |
| [`../guide/samples`](../guide/samples/channel-messaging-samples.ko.md) | sample guide 초안 |
| [`../spec`](../spec/README.ko.md) | handler, Spring Boot, connector 계약 초안 |
| [`../internals`](../internals/regression-test-matrix.ko.md) | [`internals/`](./internals/regression-test-matrix.ko.md) |
