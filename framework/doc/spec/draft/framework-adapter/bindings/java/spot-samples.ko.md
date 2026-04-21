[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [SPOT](./spring-boot-spot.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework Java SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java` `SPOT` 초안을 코드 흐름으로 보기 위한 샘플 문서다.

## 1. 등록과 `spotName`

```java
@Configuration
@EnableZLinkFramework
public class SpotConfig implements ZLinkFrameworkOptionsCustomizer {
    @Override
    public void customize(ZLinkFrameworkOptions options) {
        options.useSpotDiscovery("game.stage", registry -> {
            registry.add("tcp://registry1:5551");
        });

        options.addSpotNode("stage-node", spot -> {
            spot.bind("tcp://0.0.0.0:9000");
            spot.enableRouter();
            spot.enablePubSub();
            spot.attachChannelClient("profile");
            spot.attachSpotPublisherClient("game.stage");
            spot.addSpotFactory("stage", StageSpot.class);
            spot.addSpotFactory("room", RoomSpot.class);
        });
    }
}
```

같은 `SpotNode` 안에서 같은 `spotName`을 다시 등록하면 조용히 덮어쓰지 않고
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
        return spotManager.createAsync("stage")
            .thenCompose(created -> spotManager.getAsync(created.spotRid())
                .thenCompose(info -> spotManager.listAsync()
                    .thenAccept(all -> {
                        // 운영 코드에서는 created.spotRid()가 어떤 spotName으로
                        // 생성됐는지 다시 확인할 수 있어야 한다.
                    })));
    }
}
```

## 3. spot 객체와 timer

```java
public final class StageSpot extends ZLinkSpot {
    private RoutingId spotRid;
    private ZLinkTimer heartbeat;

    @Override
    public RoutingId spotRid() {
        return spotRid;
    }

    public CompletionStage<Void> initializeAsync() {
        return addTimer(
            "heartbeat",
            Duration.ofSeconds(1),
            StageHeartbeatHandler.class
        ).thenAccept(timer -> this.heartbeat = timer);
    }
}
```

timer는 공용 scheduler가 아니라 spot lifecycle 안에서 이름과 함께 등록하는 편을
기본으로 본다.

## 4. request, subscription, channel 호출

```java
@Component
public final class StageHandlers {
    private final ZLinkSpotClient spotClient;

    public StageHandlers(ZLinkSpotClient spotClient) {
        this.spotClient = spotClient;
    }

    @ZLinkSpotRequestMapping
    public CompletionStage<GetStageStateReply> getStageStateAsync(
        GetStageStateRequest request,
        ZLinkSpotRequestContext context) {
        return spotClient.requestChannelAsync(
            "profile",
            new GetProfileRequest(request.accountId()),
            null
        ).thenApply(profile -> new GetStageStateReply(
            context.self().spotRid(),
            profile.nickname()
        ));
    }

    @ZLinkSpotSubscription(topic = "stage.state.updated")
    public CompletionStage<Void> onStageStateAsync(
        StageStateUpdated event,
        ZLinkSpotSubscriptionContext context) {
        return CompletableFuture.completedFuture(null);
    }
}
```

`SPOT` 안에서 다른 channel을 호출할 때도 기본은 `channel name` 기준이다.
`targetRid + spotRid` direct routed 호출은 advanced surface로만 남긴다.

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
        return spotPublisherClient.publishAsync(
            "game.stage",
            "stage.state.updated",
            new StageStateUpdated(request.stageRid(), request.userCount()),
            null
        ).thenApply(submitted -> ResponseEntity.accepted().build());
    }
}
```
