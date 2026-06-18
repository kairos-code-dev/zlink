# ZLink Framework for Java/Kotlin -- 문서

> 이 묶음은 `Java`, `Kotlin`, `Spring Boot`용 ZLink Framework 문서다. 문서는
> `guide/`(사용법·케이스 스터디), `spec/`(공개 계약), `internals/`(구현·검증 기준)로
> 나뉜다. 공통 의미는
> [공통 스펙](../common/README.ko.md)을 따르며, 여기서는 그 의미를
> Java/Kotlin 표면으로 구체화한다.

비동기 실행, `CompletionStage`, Kotlin coroutine wrapper의 공통 의미는
[비동기 실행과 coroutine 정책](../common/spec/async-execution-policy.ko.md)을 따른다.

> **Kotlin 사용자**는 [Kotlin 전용 guide](../kotlin/README.ko.md)를 본다.
> `zlink-framework-kotlin`은 이 런타임을 공유하는 얇은 coroutine idiom 레이어라,
> `spec/`·`internals/`는 이 문서를 정본으로 공유하고 guide만 `suspend`/`Flow`
> 표면으로 따로 작성한다. 아래 guide는 Java(blocking/`CompletionStage`) 표면이다.

## 1. 사용자 guide

Spring Boot 개발자가 읽고 바로 따라 쓸 수 있도록 기능과 사용법을 설명한다. 내부
backend adapter나 binding wrapper 구조는 guide에서 설명하지 않고, 필요하면
`internals/` 문서로 연결한다.

| 문서 | 범위 |
|------|------|
| [01-overview](guide/01-overview.ko.md) | 한 줄 정의, 아키텍처, 통합 축 |
| [02-getting-started](guide/02-getting-started.ko.md) | 첫 channel request까지 |
| [03-concepts](guide/03-concepts.ko.md) | channel, 역할, Spring DI 멘탈 모델 |
| [04-channel-messaging](guide/04-channel-messaging.ko.md) | request/send/pub-sub |
| [05-spot](guide/05-spot.ko.md) | Spot 생성, 조회, timer |
| [06-actor-session](guide/06-actor-session.ko.md) | actor lifecycle, session actor dispatch |
| [07-stream](guide/07-stream.ko.md) | STREAM server session과 connector |
| [08-registry](guide/08-registry.ko.md) | Registry 구동과 query |
| [09-monitoring](guide/09-monitoring.ko.md) | runtime event 관찰 |
| [10-feature-map](guide/10-feature-map.ko.md) | 기능별 사용 시점과 구현 문서 연결 |
| [11-interface-catalog](guide/11-interface-catalog.ko.md) | 주요 public interface |
| [12-grpc-alternative](guide/12-grpc-alternative.ko.md) | gRPC/HTTP 대비 도입 판단 |

## 2. 공개 계약 spec

정식 spec은 현재 Java 코드와 regression test에 존재하는 public API만 설명한다.

| 문서 | 범위 |
|------|------|
| [spec 목차](spec/README.ko.md) | Java/Kotlin 공개 계약 문서 목록 |
| [handler-interfaces](spec/handler-interfaces.ko.md) | interface, annotation, context, options |
| [spring-boot-channel-messaging](spec/spring-boot-channel-messaging.ko.md) | channel 등록, outbound client, dispatch |
| [spring-boot-spot](spec/spring-boot-spot.ko.md) | Spot lifecycle, Entry Spot, timer |
| [spring-boot-actor-session](spec/spring-boot-actor-session.ko.md) | actor factory, ActorGateway, bound session |
| [spring-boot-stream](spec/spring-boot-stream.ko.md) | stream node, header session |
| [stream-connector](spec/stream-connector.ko.md) | Java/Kotlin Stream Connector |
| [spring-boot-registry](spec/spring-boot-registry.ko.md) | embedded registry, remote query |
| [spring-boot-monitoring](spec/spring-boot-monitoring.ko.md) | runtime event, typed handler |

## 3. 내부 기준

`internals/`는 유지보수자를 위한 구현 구조, lifecycle, regression 기준을 설명한다.

| 문서 | 범위 |
|------|------|
| [dotnet-to-java-surface-mapping](internals/dotnet-to-java-surface-mapping.ko.md) | 이식 기준과 번역 규칙 |
| [backend-dependency-policy](internals/backend-dependency-policy.ko.md) | Java binding 의존 격리 |
| [di-capability-exposure-policy](internals/di-capability-exposure-policy.ko.md) | 역할별 Spring bean 노출 규칙 |
| [lifecycle-and-failure-semantics](internals/lifecycle-and-failure-semantics.ko.md) | 시동, 종료, 실패 의미 |
| [behavior-matrix](internals/behavior-matrix.ko.md) | 기능별 validation/runtime 판정 |
| [implementation-scope-and-nongoals](internals/implementation-scope-and-nongoals.ko.md) | 구현 범위와 비목표 |
| [regression-test-matrix](internals/regression-test-matrix.ko.md) | `.NET` 동등성 회귀 테스트 기준 |

## 4. 샘플

샘플은 Java/Kotlin 양쪽에서 같은 scenario set을 제공한다. 정본 6종은 per-app 문서로,
기능 축 샘플은 별도 문서로 둔다.

Bingo와 TicTacToe를 제외한 정본 샘플(SupportChat, DeliveryDispatch,
ShoppingMall, GameQuest)은 공통 샘플 기준에 따라 JSON codec, Registry/Discovery
자동 연결, Spring component scan 기반 handler 자동 등록을 사용한다. 이 기준은
[공통 샘플 포팅 기준](../common/sample/README.ko.md#샘플-포팅-기준)을 따른다.

| 문서 | 범위 |
|------|------|
| [samples README](../../../languages/java/samples/README.md) | Java/Kotlin sample 구조와 실행 방법 |
| [bingo-game-sample](guide/samples/bingo-game-sample.ko.md) | Session/Api/Play/Registry, Entry Spot, room Spot, timer, bound push (Protobuf) |
| [tictactoe-game-sample](guide/samples/tictactoe-game-sample.ko.md) | Api/Play 두 서버, 수동 연결, typed session dispatch |
| [supportchat-sample](guide/samples/supportchat-sample.ko.md) | conversation Spot, idle timer, reconnect, 양방향 push (JSON) |
| [deliverydispatch-sample](guide/samples/deliverydispatch-sample.ko.md) | 배차, timeout 재배정, 상태 fanout, 고객 stream push |
| [shoppingmall-sample](guide/samples/shoppingmall-sample.ko.md) | event-sourced workflow Spot, projection, scale-out |
| [gamequest-sample](guide/samples/gamequest-sample.ko.md) | event-sourced quest Spot, fanout owner routing, projection rebuild, snapshot 재동기화 (JSON, Java) |
| [channel-messaging-samples](guide/samples/channel-messaging-samples.ko.md) | channel 등록, handler, outbound client 샘플 |
| [spot-samples](guide/samples/spot-samples.ko.md) | room/stage/zone 기준 Spot 등록과 publish/request 샘플 |
| [stream-samples](guide/samples/stream-samples.ko.md) | stream 등록, header session, actor relay 샘플 |

## 5. 케이스 스터디

도입 판단과 아키텍처 매핑을 위한 도메인별 케이스 스터디는
[12-grpc-alternative](guide/12-grpc-alternative.ko.md)에서 진입하며,
[guide/case-studies/](guide/case-studies/13-case-ecommerce-checkout.ko.md) 13–18에 둔다.
