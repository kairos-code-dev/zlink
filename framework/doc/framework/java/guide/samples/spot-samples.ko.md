<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework Java Interface Catalog](../../spec/handler-interfaces.ko.md) | [다음: ZLink Framework Spring Boot Channel Messaging](../../spec/spring-boot-channel-messaging.ko.md)
<!-- framework-adapter-nav:end -->

[Java 문서](../../README.ko.md)

[Java 묶음](../../README.ko.md) | [SPOT](../../spec/spring-boot-spot.ko.md) | [인터페이스](../../spec/handler-interfaces.ko.md)

# ZLink Framework Java SPOT Samples

## 1. 등록과 Spot type

```java
@Configuration
@EnableZLinkFramework
public class SpotConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions framework) {
        framework.useDiscovery().addRegistryEndpoint("tcp://registry1:5551");
        ZLinkSpotMeshBuilder spot = framework.addSpotMesh("game.stage");
        spot.enableRouter("tcp://0.0.0.0:9000");
        spot.enablePubSub("tcp://0.0.0.0:9001");
        spot.addEntrySpot(GameEntrySpot.class);
        spot.addSpotFactory(StageSpot.class);
        spot.addSpotFactory(RoomSpot.class);

        options.addClientServerChannel("profile").enableClient();
    }
}
```

같은 `SpotNode` 안에서 같은 Spot type을 다시 등록하면 조용히 덮어쓰지 않고
예외를 던지는 편을 기본으로 본다.

## 2. manager로 생성과 조회

```java
@Component
public final class StageBootstrap {
    private final ZLinkSpotManager spotManager;

    public StageBootstrap(ZLinkSpotManager spotManager) {
        this.spotManager = spotManager;
    }

    public CompletionStage<Void> warmupAsync() {
        return spotManager.create(StageSpot.class)
            .thenCompose(created -> spotManager.find(created.spotRid())
                .thenCompose(info -> spotManager.list()
                    .thenAccept(all -> {
                        // 운영 코드에서는 created.spotRid()를 다시 조회할 수 있어야 한다.
                    })));
    }
}
```

## 3. spot 객체와 timer

```java
public final class StageSpot implements ZLinkSpot<ZLinkActor> {
    private final ZLinkSpotContext context;
    private ZLinkTimer heartbeat;

    public StageSpot(ZLinkSpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public void onInitialize() {
        this.heartbeat = ZLinkAwait.await(context.addTimer(
            "heartbeat",
            Duration.ofSeconds(1),
            StageHeartbeatHandler.class,
            null));
    }
}
```

timer는 공용 scheduler가 아니라 spot lifecycle 안에서 이름과 함께 등록하는 편을
기본으로 본다.

## 4. request, subscription, channel 호출

```java
@Component
public final class StageHandlers {
    @ZLinkSpotRequest
    public GetStageStateReply getStageState(
        StageSpot spot,
        GetStageStateRequest request) {
        GetProfileReply profile = ZLinkAwait.await(spot.context().outbound().requestToChannel(
            "profile",
            new GetProfileRequest(request.accountId())
        ).submit(GetProfileReply.class));
        return new GetStageStateReply(spot.context().spotRid(), profile.nickname());
    }

    @ZLinkSpotSubscription(spotNodeName = "stage-node", topic = "stage.state.updated")
    public void onStageState(
        StageSpot spot,
        StageStateUpdated event) {
    }
}
```

`SPOT` 안에서 다른 channel을 호출할 때도 기본은 `channel name` 기준이다. 다른 spot으로 보내는
outbound 는 `sendToSpot/requestToSpot(SpotRef spotRef, ...)` 로 전송 대상을 받는다. `spotRid`만으로
전송하지 않고, 조회 단계에서 얻은 `SpotRef`를 전송 입력으로 넘긴다.

## 5. 외부 노드에서 `SPOT` publish

```java
@RestController
@RequestMapping("/stage")
public final class StagePublishController {
    private final ZLinkSpotPublisherClient spotPublisherClient;

    public StagePublishController(ZLinkSpotPublisherClient spotPublisherClient) {
        this.spotPublisherClient = spotPublisherClient;
    }

    @PostMapping("/publish")
    public CompletionStage<ResponseEntity<Void>> publish(
        @RequestBody PublishStageStateHttpRequest request) {
        return spotPublisherClient.publishSpot(
            "game.stage",
            "stage.state.updated",
            new StageStateUpdated(request.stageRid(), request.userCount())
        ).submit().thenApply(submitted -> ResponseEntity.accepted().build());
    }
}
```

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework Java Interface Catalog](../../spec/handler-interfaces.ko.md) | [다음: ZLink Framework Spring Boot Channel Messaging](../../spec/spring-boot-channel-messaging.ko.md)
<!-- framework-adapter-nav:bottom:end -->
