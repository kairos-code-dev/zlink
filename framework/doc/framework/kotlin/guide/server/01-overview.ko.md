<!-- framework-adapter-nav:start -->
[가이드 홈](../../../index.ko.md) | [다음: 2. 시작하기](02-getting-started.ko.md)
<!-- framework-adapter-nav:end -->

# 1. 개요

> 이 문서는 Kotlin 가이드의 진입점이다. 언어 중립 정의는
> [공통 스펙 목차](../../../common/README.ko.md)가, Kotlin 전용 표면의 정확한 계약은
> [Kotlin 공개 계약](../../../common/spec/server/languages/kotlin/README.ko.md)이,
> 공유하는 표면은 [Java exact interface 목차](../../../common/spec/server/languages/java/interfaces/README.ko.md)가 소유한다.

## 1. Kotlin 가이드가 다루는 범위

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

## 2. Kotlin 레이어가 얹는 것

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

### 2.4 확장 함수와 `Flow`

| 확장 | 무엇을 바꾸나 |
| --- | --- |
| `ZLinkSpotHandlerRegistry.addHandler<T>()` | `addHandler(T::class.java)` 대신 reified 타입 |
| `ZLinkFrameworkOptions.routeMesh(...)` · `ZLinkMeshNodeBuilder.channelName(...)` | 등록을 람다 블록으로 |
| `ZLinkMessage.decode<T>()` · `messageOf(...)` | reified decode와 생성 |
| `ZLinkLocationRuntimeQuery`의 조회 | `Flow`로 페이지를 이어 받는다 |
| `Flow.Publisher.asFlow()` | 상태 stream을 `Flow`로 |

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
그대로 부르면 된다. 넣으면 `suspend`·`Flow`·reified 표면이 생긴다.

나머지 아티팩트 목록은 [Java 1. 개요](../../../java/guide/server/01-overview.ko.md) §3과 같다.

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

03~17장은 다섯 언어가 같은 정본을 공유한다. 예제는 `Kotlin` 탭을 고른다. 순서는
[Kotlin 가이드 진입점](README.ko.md)이 제시한다.

먼저 [3. 핵심 개념](../../../common/guide/server/03-concepts.ko.md)에서 channel · Spot ·
Actor · stream · relocation 다섯 개념을 잡는다.

## 6. 관련 문서

- 읽는 순서: [Kotlin 가이드 진입점](README.ko.md)
- Kotlin 전용 계약: [Kotlin 공개 계약](../../../common/spec/server/languages/kotlin/README.ko.md)
- 공유하는 계약: [Java exact interface 목차](../../../common/spec/server/languages/java/interfaces/README.ko.md)
- 다음 장: [2. 시작하기](02-getting-started.ko.md)
