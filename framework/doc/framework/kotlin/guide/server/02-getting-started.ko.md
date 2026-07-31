<!-- framework-adapter-nav:start -->
[가이드 홈](../../../index.ko.md) | [이전: 1. 개요](01-overview.ko.md) | [다음: 3. 핵심 개념](03-concepts.ko.md)
<!-- framework-adapter-nav:end -->

# 2. 시작하기

> 이 장이 따라가는 코드는 저장소의 `framework/languages/java/samples/kotlin/TicTacToe`다.
> 등록 표면의 정식 계약은
> [Java configuration과 host 공개 계약](../../../common/spec/server/languages/java/interfaces/configuration-host.ko.md)이
> 다룬다 — Kotlin은 같은 표면을 쓴다.

두 프로세스가 서버 간 channel로 대화하는 최소 구성을 만든다. **등록은 Java와 같고
handler와 호출만 coroutine 모양이다.**

## 1. 의존성 추가

```kotlin
dependencies {
    implementation("systems.zlink:zlink-framework-core:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-spring-boot-starter:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-kotlin:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-locations-redis:0.1.0-SNAPSHOT")
}
```

`zlink-framework-kotlin`이 `suspend`·`Flow`·reified 표면을 얹는다. 빼면 Java 표면을
그대로 쓰게 된다.

## 2. 애플리케이션에 얹기

```kotlin
@EnableZLinkFramework
@SpringBootApplication
class ApiServerApplication {

    @Bean
    fun zlink(settings: ApiSettings): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.addHandlersFromPackageOf(ApiServerApplication::class.java)

            val mesh = options.addRouteMesh("play")
            mesh.listen(settings.routeEndpoint)
                .setRoutingIdPrefix("tictactoe-api")
            mesh.channelName("play.game").client()
            mesh.peerConnections().connect(settings.playEndpoint)
        }
}

fun main(args: Array<String>) {
    runApplication<ApiServerApplication>(*args)
}
```

`ZLinkFrameworkConfigurer`는 Java의 functional interface다. Kotlin에서는 SAM 변환으로
람다를 넘긴다. **Java 코드와 같은 표면이므로 등록 부분에는 Kotlin 전용 API가 없다.**

## 3. 받는 쪽 — suspend handler

Java는 `CompletionStage`를 돌려주고, Kotlin은 `suspend`로 쓴다.

```kotlin
@ZLinkHandlerGroup("play")
class CreateGameHandler(
    private val games: GameStore,   // 생성자 주입 — Spring 컨테이너에서 온다.
) : ZLinkSuspendingRequestHandler<CreateGameReq, CreateGameRes> {

    override suspend fun handle(
        request: CreateGameReq,
        context: ZLinkMessageContext,
    ): CreateGameRes {
        val game = games.create(request.gameName)
        return CreateGameRes(game.id)
    }
}
```

**`ZLinkSuspendingRequestHandler`가 Kotlin 짝이다.** Java의 `ZLinkRequestHandler`를
구현해도 되고, 둘을 한 프로젝트에 섞어도 등록이 알아서 구분한다.

handler class에 `@Component`를 붙이지 않는 규칙은 Java와 같다 —
`addHandlersFromPackageOf(...)`가 찾아 등록하고 생성자 인자만 주입된다.

## 4. 보내는 쪽 — 두 가지 방법

Java 표면을 그대로 쓰고 `await()`로 받는 방법과, `.kotlin()` wrapper로 suspend 호출을
쓰는 방법이 있다.

```kotlin
@RestController
class GameController(private val client: ZLinkRouteClient) {

    // ① Java 표면 + await 확장
    @PostMapping("/games")
    suspend fun create(@RequestBody request: CreateGameReq): CreateGameRes =
        client.requestToChannel("play.game", request)
            .timeout(Duration.ofSeconds(3))
            .submit(CreateGameRes::class.java)
            .await()

    // ② Kotlin wrapper — 호출 자체가 suspend다.
    @PostMapping("/games/v2")
    suspend fun createV2(@RequestBody request: CreateGameReq): CreateGameRes =
        client.kotlin()
            .requestToChannel("play.game", request)
            .timeout(Duration.ofSeconds(3))
            .submit()
}
```

**이 `await()`는 `zlink-framework-kotlin`이 제공하는 확장이다.** Spot이나 Actor의 turn
안에서 불러도 그 turn의 실행 보장을 깨지 않는다. `kotlinx.coroutines`의 일반 `await`와
바꿔 쓰지 않는다.

## 5. 실행과 확인

```bash
ZLINK_SAMPLE_LANGUAGES=kotlin \
  framework/languages/java/samples/run_samples.sh TicTacToe
```

java와 kotlin 샘플이 같은 runner를 쓰고 언어만 골라 준다. 확인 순서는 Java와 같다 —
기동 로그 · client exit code · 서버 로그의 dispatch 오류다.

## 6. 다음에 볼 것

| 하려는 것 | 볼 장 |
| --- | --- |
| 개념을 먼저 잡기 | [3. 핵심 개념](03-concepts.ko.md) |
| 요청 방식 세 가지 | [5. Channel Messaging](05-channel-messaging.ko.md) |
| 방·세션 상태를 담기 | [6. Spot](06-spot.ko.md) |
| Kotlin 레이어가 얹는 것 전체 | [1. 개요](01-overview.ko.md) §2 |
| 설정 값 목록 | [Java 16. Options](../../../java/guide/server/16-options.ko.md) |

## 7. 관련 문서

- 등록 표면의 정식 계약: [Java configuration과 host 공개 계약](../../../common/spec/server/languages/java/interfaces/configuration-host.ko.md)
- Kotlin 전용 계약: [Kotlin 공개 계약](../../../common/spec/server/languages/kotlin/README.ko.md)
- 이전 장: [1. 개요](01-overview.ko.md)
- 다음 장: [3. 핵심 개념](03-concepts.ko.md)
