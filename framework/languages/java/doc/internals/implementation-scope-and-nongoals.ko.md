<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [Regression Matrix](./regression-test-matrix.ko.md)
<!-- framework-adapter-nav:end -->

# Java Implementation Scope And Non-Goals

## 1. 구현 범위

아래 항목은 첫 Java/Kotlin 포팅의 기본 범위다.

- `@EnableZLinkFramework`
- `ZLinkFrameworkConfigurer`
- channel builder 4종: client/server, fanout, dealer mesh, route mesh
- `ZLinkClient`, `ZLinkFanoutClient`, `ZLinkRouteClient`
- handler interface와 annotation mapping
- global discovery와 capability별 manual connection
- backend adapter layer
- runtime building block: `Runtime/Execution/`의 serial executor(session/spot
  콜백 직렬 dispatch), polling backoff, bounded task set, runtime task runner
- codec registry와 JSON 기본 codec
- embedded registry와 `ZLinkRegistryQuery`
- remote `ZLinkRegistryQueryClient`
- monitoring source와 typed runtime event handler
- Spot mesh, SpotNode, Entry Spot, user Spot factory
- `ZLinkSpotManager`, `ZLinkSpotOutbound`, `ZLinkSpotPublisherClient`
- Spot timer와 timer monitoring event
- actor factory, `ZLinkActorManager`, actor context
- STREAM node, header 기반 `ZLinkSession`
- ActorGateway 기반 session actor relay
- `ZLinkBoundSession` push와 disconnect
- Java Stream Connector
- connector codec helper: JSON, MessagePack, Protobuf, auto codec
- Kotlin coroutine/DSL wrapper
- testkit: fake backend, in-process host, sample fixture
- samples: `samples/java/*`와 `samples/kotlin/*` 아래의 `TicTacToe`,
  `TicTacToe.SessionGateway`, `Bingo`, `StreamingClient`, `Async`

구현은 단계적으로 나눌 수 있다. 그러나 위 항목을 기본 범위 밖으로 밀어 두면
`.NET`과 같은 수준의 포팅으로 보지 않는다.

## 2. 비목표

아래 항목은 첫 구현의 framework core 범위가 아니다.

- channel별 generated typed client wrapper
- scatter-gather aggregate helper
- workflow orchestration metadata와 compensation model
- health check actuator 자동 통합
- topology event를 Reactor/RxJava stream으로 자동 노출
- Spring Boot starter의 connector client bean 자동 구성
- Kotlin `Flow` backpressure 세부 정책 확정
- preview Java/Kotlin 언어 기능 의존
- framework core 안에 protobuf/msgpack 구현체 직접 포함

필요하면 별도 extension package나 후속 문서에서 다룬다.

## 3. 판정 기준

이 구현 범위는 아래를 모두 만족할 때 끝난 것으로 본다.

- 구현 범위 항목이 코드와 테스트로 존재한다.
- `regression-test-matrix.ko.md`의 release gate를 통과한다.
- sample이 framework/connector public API만 사용한다.
- backend concrete type이 public API에 새지 않는다.
- Java binding public API 부족분은 public API로 추가되어 있다.
- Kotlin wrapper가 Java runtime과 다른 의미를 만들지 않는다.

## 4. Scope 회귀 테스트

| 테스트 | 확인 기준 |
|--------|-----------|
| public surface scope | 비목표 API가 public surface에 없다 |
| connector separate module | Stream Connector가 server framework module에 의존하지 않는다 |
| sample public API only | sample이 framework/connector public API만 사용한다 |
| no backend leakage | binding concrete type이 public surface에 없다 |
