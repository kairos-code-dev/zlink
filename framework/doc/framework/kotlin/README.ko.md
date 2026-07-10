# ZLink Framework for Kotlin -- 문서

> 이 묶음은 `Kotlin`(Spring Boot) 사용자를 위한 ZLink Framework 문서다.
> `zlink-framework-kotlin`은 Java `zlink-framework` 런타임을 그대로 재사용하는
> 얇은 coroutine idiom 레이어라, **공개 계약(spec)과 내부 기준(internals)은
> [Java/Kotlin 문서](../java/README.ko.md)를 정본으로 공유**한다. 반면 `guide/`는
> Kotlin 사용자가 `suspend` 함수, coroutine handler, `Flow`만으로 따라 쓸 수 있도록
> **Kotlin 전용으로** 작성한다. 공통 의미는
> [공통 스펙](../common/README.ko.md)을 따른다.

비동기 실행, `CompletionStage`, Kotlin coroutine wrapper의 공통 의미는
[비동기 실행과 coroutine 정책](../common/spec/async-execution-policy.ko.md)을 따른다.

## 0. Kotlin 표면 한눈에

`zlink-framework-kotlin`은 새 transport를 만들지 않는다. Java framework가 노출하는
같은 channel·Spot·actor·stream 위에 coroutine 표면만 얹는다.

| Java 표면 | Kotlin 표면 |
|-----------|-------------|
| `ZLinkRequestHandler<T, R>` (plain `TReply` 반환) | `ZLinkSuspendingRequestHandler<T, R>` (`suspend fun handle`) |
| `ZLinkSendHandler` / `ZLinkPublishHandler` | `ZLinkSuspendingSendHandler` / `ZLinkSuspendingPublishHandler` |
| `ZLinkSpot<TActor>` (override `onCreate`, `onActorJoin`) | `ZLinkSuspendingSpot<TActor>` (`onCreateSuspending`, `onActorJoinSuspending`) |
| `ZLinkSession` | `ZLinkSuspendingSession` (`onConnectedSuspending` 등) |
| `client.requestToChannel(...).submit(R::class.java)` | `client.request<R>(channel, msg)` / `call.awaitReply<R>()` |
| `connector.on(name) { ... }` callback | `connector.kotlin().messages(name): Flow<...>` |

coroutine handler를 켜려면 framework 옵션에서 `useCoroutineHandlers(dispatcher)`를
호출한다. 자세한 건 [02-getting-started](guide/02-getting-started.ko.md)를 본다.

## 1. 사용자 guide (Kotlin 전용)

Kotlin/Spring Boot 개발자가 읽고 바로 따라 쓸 수 있도록 기능과 사용법을 Kotlin
관용구로 설명한다. 내부 backend adapter나 binding wrapper 구조는 guide에서 설명하지
않고, 필요하면 공유 `internals/`(아래 §3)로 연결한다.

| 문서 | 범위 |
|------|------|
| [01-overview](guide/01-overview.ko.md) | 한 줄 정의, 아키텍처, coroidiom 통합 축 |
| [02-getting-started](guide/02-getting-started.ko.md) | 첫 channel request까지 (`suspend` handler) |
| [03-concepts](guide/03-concepts.ko.md) | channel, 역할, Spring DI, coroutine 멘탈 모델 |
| [04-channel-messaging](guide/04-channel-messaging.ko.md) | request/send/pub-sub (`suspend`/`Flow`) |
| [05-spot](guide/05-spot.ko.md) | Spot 생성, 조회, timer (`ZLinkSuspendingSpot`) |
| [06-actor-session](guide/06-actor-session.ko.md) | actor lifecycle, session actor dispatch |
| [07-stream](guide/07-stream.ko.md) | STREAM server session과 connector (`Flow`) |
| [08-registry](guide/08-registry.ko.md) | Registry 구동과 query |
| [09-monitoring](guide/09-monitoring.ko.md) | runtime event 관찰 (`suspend` event handler) |
| [10-feature-map](guide/10-feature-map.ko.md) | 기능별 사용 시점과 구현 문서 연결 |
| [11-interface-catalog](guide/11-interface-catalog.ko.md) | 주요 Kotlin public interface |
| [12-grpc-alternative](guide/12-grpc-alternative.ko.md) | gRPC/HTTP 대비 도입 판단 |

## 2. 공개 계약 spec — Java/Kotlin 공유

Kotlin은 같은 Spring Boot 계약 위에 coroutine 확장만 더하므로, 정식 spec은
**Java/Kotlin 문서를 그대로 공유**한다. 시그니처는 Java contract를 정본으로 보고,
Kotlin은 그 위의 `suspend` 표면(아래 §0)을 더한다.

| 문서 | 범위 |
|------|------|
| [spec 목차](../java/spec/README.ko.md) | Java/Kotlin 공개 계약 문서 목록 |
| [handler-interfaces](../java/spec/handler-interfaces.ko.md) | interface, annotation, context, options |
| [spring-boot-channel-messaging](../java/spec/spring-boot-channel-messaging.ko.md) | channel 등록, outbound client, dispatch |
| [spring-boot-spot](../java/spec/spring-boot-spot.ko.md) | Spot lifecycle, Entry Spot, timer |
| [spring-boot-actor-session](../java/spec/spring-boot-actor-session.ko.md) | actor factory, SessionRelay, bound session |
| [spring-boot-stream](../java/spec/spring-boot-stream.ko.md) | stream node, header session |
| [stream-connector](../java/spec/stream-connector.ko.md) | Java/Kotlin Stream Connector |
| [spring-boot-registry](../java/spec/spring-boot-registry.ko.md) | embedded registry, remote query |
| [spring-boot-monitoring](../java/spec/spring-boot-monitoring.ko.md) | runtime event, typed handler |

## 3. 내부 기준 — Java/Kotlin 공유

구현 구조, lifecycle, regression 기준은 같은 런타임을 쓰므로 **Java/Kotlin
`internals/`를 공유**한다.

| 문서 | 범위 |
|------|------|
| [backend-dependency-policy](../java/internals/backend-dependency-policy.ko.md) | Java binding 의존 격리 |
| [runtime-lifecycle](../java/internals/runtime-lifecycle.ko.md) | Spring lifecycle과 Java/Kotlin 공유 runtime 소유권 |
| [regression-test-matrix](../java/internals/regression-test-matrix.ko.md) | `.NET` 동등성 회귀 테스트 기준 |

## 4. 샘플 (Kotlin)

샘플은 Java와 같은 scenario set을 Kotlin coroutine 구현으로 제공한다. 정본 6종은
per-app 문서로, 기능 축 샘플은 별도 문서로 둔다.

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
| [gamequest-sample](guide/samples/gamequest-sample.ko.md) | event-sourced quest Spot, fanout owner routing, projection rebuild, snapshot 재동기화 (JSON, Kotlin) |
| [channel-messaging-samples](guide/samples/channel-messaging-samples.ko.md) | channel 등록, handler, outbound client 샘플 |
| [spot-samples](guide/samples/spot-samples.ko.md) | room/stage/zone 기준 Spot 등록과 publish/request 샘플 |
| [stream-samples](guide/samples/stream-samples.ko.md) | stream 등록, header session, actor relay 샘플 |
