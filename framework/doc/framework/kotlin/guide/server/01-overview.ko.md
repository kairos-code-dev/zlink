---
title: "1. 개요 · Kotlin"
---

<!-- generated:start -->
<!-- 이 파일은 `common/guide/server/01-overview.ko.md`에서 생성한다. 직접 고치지 않는다.
     고칠 곳은 공통 소스이고, `python3 doc/site/scripts/generate_language_guides.py`로 다시 만든다. -->
<!-- generated:end -->

<!-- framework-adapter-nav:start -->
[가이드 홈](README.ko.md) | [다음: 2. 시작하기](02-getting-started.ko.md)
<!-- framework-adapter-nav:end -->

<!-- language-switch:start -->
다른 언어로 보기 — [C#/.NET](../../../dotnet/guide/server/01-overview.ko.md) · [C++](../../../cpp/guide/server/01-overview.ko.md) · [Java](../../../java/guide/server/01-overview.ko.md) · **Kotlin** · [Node/TypeScript](../../../node/guide/server/01-overview.ko.md)
<!-- language-switch:end -->

# 1. 개요

> **이 장의 계약 소유 문서** — [Framework 개요](../../../common/spec/02-overview.ko.md)가
> 무엇을 제공하는지를, [언어별 공개 계약 목차](../../../common/spec/server/languages/README.ko.md)가
> 각 언어 표면의 정확한 계약을 소유한다. 이 문서는 그 가운데 **어디서 시작하는지**를
> 정리한다.

## 1. 무엇을 만드는가

실시간 메시징이 중요한 서버 시스템을 여러 프로세스로 나눠 만든다. 서버 간 typed
메시징, 상태 단위(Spot)의 직렬 실행, 외부 client 실시간 연결, 무중단 이전을 한 선언
모델 위에서 조합한다.

Kotlin에서는 **Spring Boot 애플리케이션 안에 얹는다.** 별도 프로세스가 아니라 같은
JVM에서 Spring의 DI·설정·수명주기를 그대로 쓴다.

### 이 가이드가 다루는 범위

**Kotlin은 Java 런타임을 그대로 쓴다.** `zlink-framework-kotlin`은 별도 구현이 아니라
그 위에 coroutine idiom을 얹는 얇은 레이어다. 그래서 이 가이드는 **Java와 다른 지점만**
설명하고 나머지는 Java 문서를 가리킨다.

| 장 | Kotlin 전용 문서 | 이유 |
| --- | --- | --- |
| 1 · 2 | 이 가이드가 쓴다 | 의존성과 등록 코드 모양이 다르다 |
| 3 ~ 10 · 12 · 14 · 15 · 17 | 공통 정본. `Kotlin` 탭을 본다 | 개념과 동작이 같다 |
| 11 · 13 · 16 | 이 가이드가 **차이만** 쓴다 | 표면은 Java와 같고 idiom만 다르다 |

같은 내용을 두 벌로 두지 않는 것이 목적이다. Java 문서가 바뀌면 Kotlin 독자도 같은
문서를 본다.

## 2. 무엇을 대체하나

| 지금 쓰는 것 | ZLink가 대신하는 부분 |
| --- | --- |
| 서비스 간 gRPC · REST 호출 | host · port · stub 대신 **ChannelName**으로 부른다 |
| 방·세션 상태를 담는 분산 락 | **Spot**의 직렬 실행 — 같은 상태에 두 요청이 겹치지 않는다 |
| WebSocket 세션 관리 코드 | **STREAM session**과 Actor binding |
| 배포 시 세션 드레이닝 스크립트 | **relocation** — 상태를 다른 node로 옮기고 내린다 |

HTTP는 대체하지 않는다. 외부 진입은 Spring MVC·WebFlux가 그대로 맡는다.

### Kotlin 레이어가 얹는 것

네 가지다. 이것이 Java와 다른 전부다.

### 2.1 suspend handler 계약

Java handler는 `CompletionStage`를 돌려주고, Kotlin은 `suspend`로 쓴다. 같은 자리마다
`ZLinkSuspending*` 짝이 있다.

| Java | Kotlin |
| --- | --- |
| `ZLinkRequestHandler` | `ZLinkSuspendingRequestHandler` |
| `ZLinkSendHandler` | `ZLinkSuspendingSendHandler` |
| `ZLinkFanoutHandler` | `ZLinkSuspendingPublishHandler` |
| `ZLinkSpotPacketHandler` · `ZLinkSpotRequestHandler` | `ZLinkSuspendingSpot*Handler` |
| `ZLinkSpotSubscriptionHandler` · `ZLinkSpotTimerHandler` | `ZLinkSuspendingSpot*Handler` |
| `ZLinkSpotActorSendHandler` · `...RequestHandler` | `ZLinkSuspendingSpotActor*Handler` |
| `ZLinkEntrySpotActorSendHandler` · `...RequestHandler` | `ZLinkSuspendingEntrySpotActor*Handler` |
| `ZLinkRouteSendHandler` · `ZLinkRouteRequestHandler` | `ZLinkSuspendingRoute*Handler` |
| `ZLinkTypedSessionPacketHandler` | `ZLinkSuspendingTypedSessionPacketHandler` |

**둘을 섞어 등록해도 된다.** 등록 쪽이 어느 계약인지 보고 맞게 호출한다.

### 2.2 `.kotlin()` wrapper

Java client는 `CompletionStage`를 돌려준다. `.kotlin()`을 부르면 같은 호출이 suspend
표면으로 바뀐다.

```kotlin
// Java 표면 그대로 — CompletionStage를 await로 받는다.
val reply = client.requestToChannel("orders", request)
    .submit(OrderPlaced::class.java)
    .await()

// Kotlin wrapper — 호출 자체가 suspend다.
val reply = client.kotlin().requestToChannel("orders", request).submit<OrderPlaced>()
```

wrapper가 있는 표면은 `ZLinkClient` · `ZLinkRouteClient` · `ZLinkFanoutClient` ·
`ZLinkActorClient` · `ZLinkActorManager`다.

### 2.3 `CompletionStage.await()`

wrapper가 없는 자리에서는 확장 함수 하나로 받는다.

```kotlin
val status = runtime.relocate(options).await()
```

**이 `await()`가 turn을 안다.** Spot이나 Actor의 turn 안에서 불러도 그 turn의 실행
보장을 깨지 않는다. `kotlinx.coroutines`의 일반 `await`와 바꿔 쓰지 않는다.

### 2.4 확장 함수와 `flow`

| 확장 | 무엇을 바꾸나 |
| --- | --- |
| `ZLinkSpotHandlerRegistry.addHandler<T>()` | `addHandler(T::class.java)` 대신 reified 타입 |
| `ZLinkFrameworkOptions.routeMesh(...)` · `ZLinkMeshNodeBuilder.channelName(...)` | 등록을 람다 블록으로 |
| `ZLinkMessage.decode<T>()` · `messageOf(...)` | reified decode와 생성 |
| `ZLinkLocationRuntimeQuery`의 조회 | `flow`로 페이지를 이어 받는다 |
| `Flow.Publisher.asFlow()` | 상태 stream을 `flow`로 |

**어떤 상황에서 후보가 되는지**와 gRPC · Orleans · Akka와의 비교는
[17. ZLink를 어디에 쓰나](17-alternative.ko.md)가 다룬다.

## 3. 산출물

Java 아티팩트에 `zlink-framework-kotlin` 하나를 더한다.

```kotlin
dependencies {
    implementation("systems.zlink:zlink-framework-core:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-spring-boot-starter:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-kotlin:0.1.0-SNAPSHOT")  // coroutine idiom
    implementation("systems.zlink:zlink-framework-locations-redis:0.1.0-SNAPSHOT")
}
```

**`zlink-framework-kotlin`은 선택이다.** 빼도 Kotlin에서 쓸 수 있다 — Java 표면을
그대로 부르면 된다. 넣으면 `suspend`·`flow`·reified 표면이 생긴다.

나머지 아티팩트 목록은 [Java 1. 개요](../../../java/guide/server/01-overview.ko.md) §3과 같다.

설치 절차와 최소 예제는 [2. 시작하기](02-getting-started.ko.md)가 다룬다.

## 4. 등록 진입점

Java와 같다. `@EnableZLinkFramework`와 `ZLinkFrameworkConfigurer` bean이다.

```kotlin
@EnableZLinkFramework
@SpringBootApplication
class PlayServerApplication {

    @Bean
    fun zlink(settings: PlaySettings): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.addHandlersFromPackageOf(PlayServerApplication::class.java)

            val mesh = options.addRouteMesh("play")
            mesh.listen(settings.meshEndpoint)
                .setRoutingIdPrefix("play")
            mesh.objects().server()
                .addEntrySpot(PlayEntrySpot::class.java)
        }
}
```

`ZLinkFrameworkConfigurer`는 Java의 functional interface라 Kotlin에서 SAM 변환으로
람다를 넘긴다.

## 5. 읽는 순서

이 가이드의 03~17장은 **다섯 언어가 같은 정본에서 생성된다.** 예제는 이 언어의 코드만
담기며 다른 언어 코드가 섞이지 않는다. 읽는 순서는 이 언어의 가이드 진입점이 제시한다.

먼저 [3. 핵심 개념](03-concepts.ko.md)에서 channel · Spot · Actor · stream ·
relocation 다섯 개념을 잡는다. 나머지 장은 그 조합이다.

## 6. 도입 순서 고르기

전부 한 번에 쓰지 않는다. 지금 겪는 문제부터 고른다.

| 지금 겪는 문제 | 먼저 볼 장 |
| --- | --- |
| 서비스가 어디 있는지 관리하기 번거롭다 | [5. Channel Messaging](05-channel-messaging.ko.md) |
| 방·세션 상태에 락이 얽힌다 | [6. Spot](06-spot.ko.md) |
| client 실시간 연결을 직접 관리한다 | [9. STREAM](09-stream.ko.md) |
| 배포할 때 세션이 끊긴다 | [10. Location](10-location.ko.md) · [7. Actor와 Spot](07-actor-spot.ko.md) |
| 부하가 몰릴 때 동작을 모르겠다 | [4. Backpressure](04-backpressure.ko.md) |

## 7. 관련 문서

- 읽는 순서: [Kotlin 가이드 진입점](README.ko.md)
- Kotlin 전용 계약: [Kotlin 공개 계약](../../../common/spec/server/languages/kotlin/README.ko.md)

- 언어 중립 정의: [공통 스펙 목차](../../../common/README.ko.md)
- 다음 장: [2. 시작하기](02-getting-started.ko.md)
