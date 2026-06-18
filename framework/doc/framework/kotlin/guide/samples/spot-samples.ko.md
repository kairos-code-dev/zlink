<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework Java Interface Catalog](../../../java/spec/handler-interfaces.ko.md) | [다음: ZLink Framework Spring Boot Channel Messaging](../../../java/spec/spring-boot-channel-messaging.ko.md)
<!-- framework-adapter-nav:end -->

[Kotlin 묶음](../../README.ko.md) | [SPOT](../../../java/spec/spring-boot-spot.ko.md) | [인터페이스](../../../java/spec/handler-interfaces.ko.md)

# ZLink Framework Kotlin SPOT Samples

## 1. 등록과 Spot type

```kotlin
@Configuration
@EnableZLinkFramework
class SpotConfig {
    @Bean
    fun framework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useCoroutineHandlers(Dispatchers.Default)
            val mesh = options.addSpotMesh("game.stage")
            mesh.useDiscovery().addRegistryEndpoint("tcp://registry1:5551")

            val spot = mesh.addNode("stage-node")
            spot.enableRouter("tcp://0.0.0.0:9000")
            spot.enablePubSub("tcp://0.0.0.0:9001")
            spot.attachChannelClient("profile")
            spot.attachSpotPublisherClient("game.stage")
            spot.addEntrySpot(GameEntrySpot::class.java)
            spot.addSpotFactory(StageSpot::class.java)
            spot.addSpotFactory(RoomSpot::class.java)
        }
}
```

같은 `SpotNode` 안에서 같은 Spot type을 다시 등록하면 조용히 덮어쓰지 않고
예외를 던지는 편을 기본으로 본다.

## 2. manager로 생성과 조회

```kotlin
@Component
class StageBootstrap(private val spotManager: ZLinkSpotManager) {
    suspend fun warmup() {
        val created = spotManager.create(StageSpot::class.java).await()
        val info = spotManager.find(created.spotRid()).await()
        val all = spotManager.list().await()
        // 운영 코드에서는 created.spotRid()를 다시 조회할 수 있어야 한다.
    }
}
```

## 3. spot 객체와 timer

`ZLinkSuspendingSpot<TActor>`을 상속하면 lifecycle 콜백을 `suspend`로 둔다.

```kotlin
class StageSpot(private val context: ZLinkSpotContext) :
    ZLinkSuspendingSpot<ZLinkActor>() {
    private var heartbeat: ZLinkTimer? = null

    override fun context(): ZLinkSpotContext = context

    override suspend fun onInitializeSuspending() {
        heartbeat = context.addTimer(
            "heartbeat",
            Duration.ofSeconds(1),
            StageHeartbeatHandler::class.java,
            null,
        ).await()
    }
}
```

timer는 공용 scheduler가 아니라 spot lifecycle 안에서 이름과 함께 등록하는 편을
기본으로 본다.

## 4. request, subscription, channel 호출

```kotlin
@Component
class StageHandlers {
    @ZLinkSpotRequest
    suspend fun getStageState(
        spot: StageSpot,
        request: GetStageStateRequest,
        context: ZLinkSpotRequestContext,
    ): GetStageStateReply {
        val profile: GetProfileReply =
            spot.context().outbound()
                .requestToChannel("profile", GetProfileRequest(request.accountId))
                .submit(GetProfileReply::class.java)
                .await()
        return GetStageStateReply(spot.context().spotRid(), profile.nickname)
    }

    @ZLinkSpotSubscription(spotNodeName = "stage-node", topic = "stage.state.updated")
    suspend fun onStageState(event: StageStateUpdated, context: ZLinkSpotSubscriptionContext) {
    }
}
```

`SPOT` 안에서 다른 channel을 호출할 때도 기본은 `channel name` 기준이다.
`targetRid + spotRid` direct routed 호출은 advanced surface로만 남긴다.

## 5. 외부 노드에서 `SPOT` publish

```kotlin
@RestController
@RequestMapping("/stage")
class StagePublishController(private val spotPublisherClient: ZLinkSpotPublisherClient) {
    @PostMapping("/publish")
    suspend fun publish(@RequestBody request: PublishStageStateHttpRequest): ResponseEntity<Void> {
        spotPublisherClient.publishSpot(
            "game.stage",
            "stage.state.updated",
            StageStateUpdated(request.stageRid, request.userCount),
        ).submit().await()
        return ResponseEntity.accepted().build()
    }
}
```

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework Java Interface Catalog](../../../java/spec/handler-interfaces.ko.md) | [다음: ZLink Framework Spring Boot Channel Messaging](../../../java/spec/spring-boot-channel-messaging.ko.md)
<!-- framework-adapter-nav:bottom:end -->
