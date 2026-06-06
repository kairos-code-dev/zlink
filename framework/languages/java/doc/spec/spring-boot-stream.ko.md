<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework Spring Boot SPOT](./spring-boot-spot.ko.md) | [다음: Java Stage Wrapper On SPOT](./stage-wrapper-on-spot.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](./README.ko.md)

[Java 묶음](../README.ko.md) | [포팅 계획](../draft/java-kotlin-framework-porting-plan.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [Actor/session](./spring-boot-actor-session.ko.md) | [STREAM 샘플](../guide/samples/stream-samples.ko.md) | [STREAM open items](../draft/stream-open-items.ko.md)

# ZLink Framework Spring Boot STREAM

## 1. 방향

`STREAM`은 일반 request handler와 다른 전용 session 모델로 설명한다. 현재
포팅 기준은 `.NET` framework와 같은 header 기반 session 하나다.

- stream node는 bind endpoint와 session type을 등록한다.
- session callback은 `ZLinkSessionContext`를 통해 peer 정보, client 응답,
  actor binding, close 제어를 사용한다.
- inbound payload는 callback 동안 framework가 빌려준 값이다.
- 같은 session 안의 callback은 직렬로 실행한다.
- 서로 다른 session은 독립적으로 진행될 수 있다.

recv loop를 application 표면에 직접 노출하지 않는 편을 기본으로 본다.

## 2. 등록

```java
@Configuration
public class StreamConfig {
    @Bean
    ZLinkFrameworkConfigurer streamOptions() {
        return options -> {
            options.addSpotMesh("game.stage", mesh -> {
                mesh.useDiscovery(discovery -> discovery.addRegistryEndpoint("tcp://registry1:5551"));
                mesh.addNode("play", node -> {
                    node.enableRouter();
                    node.addEntrySpot(GameEntrySpot.class);
                    node.addSpotFactory(GameRoomSpot.class);
                });
            });

            options.addStreamNode("gateway", stream -> {
                stream.bind("tcp://0.0.0.0:7201");
                stream.attachActorGateway("play");
                stream.registerSession(GameStreamSession.class);
            });
        };
    }
}
```

한 stream node에는 session type을 하나만 등록한다. 여러 session type을 같은 node에
나란히 붙이는 방식은 기본 표면으로 두지 않는다.

## 3. Session 계약

```java
@Component
public final class GameStreamSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkActorManager actors;

    public GameStreamSession(
        ZLinkSessionContext context,
        ZLinkActorManager actors) {
        this.context = context;
        this.actors = actors;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public CompletionStage<Void> onConnectedAsync() {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onDispatchAsync(
        ZLinkStreamHeader header,
        Message payload) {
        return actors.getOrCreateAsync("player-42", "player")
            .thenCompose(actor -> context.actors().bindAsync(actor))
            .thenCompose(bound -> bound.relayAsync(header, payload));
    }
}
```

session 객체는 stream 객체를 직접 인자로 받지 않는다. session 정보와 client
응답은 `context()`로 접근한다.

```java
context.client()
    .send(new Welcome("player-42"))
    .packetName("Welcome")
    .submitAsync();
```

## 4. ActorGateway attach

local managed actor instance를 bind해서 같은 runtime 안에서 actor로 relay하는 경우
stream node는 ActorGateway attach 없이 동작한다. session gateway처럼 remote
`ZLinkActorRef`를 bind하거나 actor 위치를 core ActorGateway가 해석해야 하는 경우에는
stream node가 SpotNode의 ActorGateway에 attach되어 있어야 한다.

```java
options.addStreamNode("gateway", stream -> {
    stream.bind("tcp://0.0.0.0:7201");
    stream.attachActorGateway("play");
    stream.registerSession(GameStreamSession.class);
});
```

이 설정은 session relay용 route mesh channel을 만든다는 뜻이 아니다. application
Spot route egress가 필요하면 별도 route channel을 등록한다. local managed actor
binding은 framework 내부 dispatch를 사용하고, remote actor binding은 ActorGateway
경로로 보낸다.

## 5. Client Connector

client 측 STREAM connector는 Spring server session과 별도 모듈이다. Java 포팅은
`.NET`의 `Systems.Zlink.Stream.Connector` 역할을 아래 표면으로 제공한다.

```java
ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(
    ZLinkStreamConnectorOptions.builder()
        .endpoint(URI.create("ws://127.0.0.1:7201"))
        .transport(ZLinkStreamTransport.WEB_SOCKET)
        .requestTimeout(Duration.ofSeconds(30))
        .build());

connector.on("MatchFound", (message) -> {
    return CompletableFuture.completedFuture(null);
});

connector.connectAsync()
    .thenCompose(ignored -> connector.send(encodedPayload).submitAsync());
```

connector는 heartbeat, reconnect, manual dispatch, request timeout, compression,
packet name resolver를 option으로 받는다. Kotlin 표면은 이 Java connector 위에
`suspend`와 `Flow` extension을 얹는다.
