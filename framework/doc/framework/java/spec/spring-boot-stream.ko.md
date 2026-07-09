<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Spring Boot SPOT](spring-boot-spot.ko.md) | [다음: Java Stage Wrapper On SPOT](stage-wrapper-on-spot.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](README.ko.md)

[Java 묶음](../README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [Actor/session](spring-boot-actor-session.ko.md) | [STREAM 샘플](../guide/samples/stream-samples.ko.md)

# ZLink Framework Spring Boot STREAM

## 1. 방향

`STREAM`은 일반 request handler와 다른 전용 session 모델로 설명한다. 현재
포팅 기준은 `.NET` framework와 같은 header 기반 session 하나다.

- stream node는 bind endpoint와 session type을 등록한다.
- session callback은 `ZLinkSessionContext`를 통해 peer 정보, client 응답,
  actor binding, close 제어를 사용한다.
- inbound payload는 framework `ZLinkMessage`다.
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
            options.useDiscovery().addRegistryEndpoint("tcp://registry1:5551");
            ZLinkSpotMeshBuilder node = options.addSpotMesh("game.stage");
            node.enableRouter("tcp://0.0.0.0:9001");
            node.addEntrySpot(GameEntrySpot.class);
            node.addSpotFactory(GameRoomSpot.class);

            ZLinkStreamNodeBuilder stream = options.addStreamNode("gateway");
            stream.bind("tcp://0.0.0.0:7201");
            stream.registerSession(GameStreamSession.class);
        };
    }
}
```

한 stream node에는 session type을 하나만 등록한다. 여러 session type을 같은 node에
나란히 붙이는 방식은 기본 표면으로 두지 않는다.

## 2.1 STREAM compression 설정

framework runtime은 stream compression codec을 하나만 활성화한다. 기본값은 LZ4다.
이 기본값은 모든 frame을 자동 압축한다는 뜻이 아니다. session send/reply call에서
`compress()`를 호출한 frame만 압축한다.

```java
@Bean
ZLinkFrameworkConfigurer compressionOptions() {
    return options -> {
        options.configureStreamCompression()
            .useLz4(); // compressed frame을 보낼 때와 받을 때 사용할 codec
    };
}
```

custom compression codec도 같은 builder 경로로 설정한다. server와 connector가 같은
codec을 사용해야 compressed frame을 복원할 수 있다.

```java
ZLinkStreamCompressionCodec codec = new MyStreamCompressionCodec();

options.configureStreamCompression()
    .use(codec); // 이 framework runtime의 활성 compression codec
```

`disable()`을 호출한 상태에서 `compress()`로 송신을 요청하면 송신 단계 오류가 난다.
같은 상태에서 compressed frame을 받으면 복원 오류가 나며, 오류 메시지는 compression
codec이 설정되지 않았다는 뜻을 드러낸다. 복원된 payload가 runtime 수신 한도를 넘는
경우에도 framework가 최종 길이를 다시 검사해 거부한다.

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
    public void onConnected() {
    }

    @Override
    public void onDispatch(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload) {
        actors.getOrCreate("player-42", "player")
            .thenCompose(actor -> context.actors().bind(actor))
            .thenCompose(bound -> bound.relay(payload))
            .toCompletableFuture()
            .join();
    }
}
```

session 객체는 stream 객체를 직접 인자로 받지 않는다. session 정보와 client
응답은 `context()`로 접근한다.

```java
context.client()
    .send(new Welcome("player-42"))
    .submit();
```

## 4. session relay

local managed actor instance를 bind해서 같은 runtime 안에서 actor로 relay하는 경우
stream node는 session relay 없이 동작한다. session gateway처럼 remote
`ActorRef`를 bind하거나 actor 위치를 core SessionRelay가 해석해야 하는 경우에는
같은 프로세스의 router-capable SpotNode가 session relay 입구가 되며, 별도 attach 없이
framework가 자동으로 연결한다.

```java
ZLinkStreamNodeBuilder stream = options.addStreamNode("gateway");
stream.bind("tcp://0.0.0.0:7201");
stream.registerSession(GameStreamSession.class);
```

이 설정은 session relay용 route mesh channel을 만든다는 뜻이 아니다. application
Spot route egress가 필요하면 별도 route channel을 등록한다. local managed actor
binding은 framework 내부 dispatch를 사용하고, remote actor binding은 SessionRelay
경로로 보낸다.

## 5. Client Connector

client 측 STREAM connector는 Spring server session과 별도 모듈이다. Java 포팅은
`.NET`의 `Systems.Zlink.Stream.Connector` 역할을 아래 표면으로 제공한다.

```java
// transport(TCP/TLS/WS/WSS)는 endpoint URI scheme(`ws://`/`wss://`/`tls://`/`tcp://`)으로 선택된다.
ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(
    ZLinkStreamConnectorOptions.createDefault(URI.create("ws://127.0.0.1:7201")));

connector.on("MatchFound", (message) -> {
    return CompletableFuture.completedFuture(null);
});

connector.connect().submit()
    .thenCompose(ignored -> connector.send(encodedPayload).submit());
```

connector는 heartbeat, reconnect, manual dispatch, request timeout, compression,
packet name resolver를 option으로 받는다. Kotlin 표면은 이 Java connector 위에
`suspend`와 `Flow` extension을 얹는다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Spring Boot SPOT](spring-boot-spot.ko.md) | [다음: Java Stage Wrapper On SPOT](stage-wrapper-on-spot.ko.md)
<!-- framework-adapter-nav:bottom:end -->
