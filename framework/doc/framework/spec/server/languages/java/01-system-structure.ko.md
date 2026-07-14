<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](README.ko.md)

# Java/Kotlin 시스템 구조 — 등록과 부트스트랩

> 이 문서는 **Spring Boot 위에서 ZLink framework를 어떻게 구성하는가**를 소유한다. 등록,
> DI, lifecycle, 그리고 각 기능(channel · SPOT · STREAM · monitoring · registry)의 **등록 표면**이다.
>
> **기능의 의미와 동작 규칙은 공통 스펙이 소유한다** — [channel-messaging](../../11-channel-messaging.ko.md),
> [spot-messaging](../../20-spot-messaging.ko.md), [spot-node](../../21-spot-node.ko.md),
> [stream-session](../../30-stream-session.ko.md), [actor-model](../../22-actor-model.ko.md),
> [session-actor-dispatch](../../31-session-actor-dispatch.ko.md),
> [runtime-monitoring](../../50-runtime-monitoring.ko.md),
> [location-runtime](../../40-location-runtime.ko.md),
> [stage-wrapper-on-spot](../../25-stage-wrapper-on-spot.ko.md).
>
> **public 타입과 시그니처는 [handler-interfaces](02-handler-interfaces.ko.md)가 소유한다.**
> client connector는 [stream-connector](../../../stream-connector/languages/java/03-stream-connector.ko.md)가 소유한다.


## 1. 패키지 구조

| 모듈 | 역할 | 의존 |
|---|---|---|
| `zlink-framework-core` | framework core — contract, runtime, dispatcher | core 바인딩 |
| `zlink-framework-spring-boot-starter` | Spring Boot host adapter — 자동 구성과 등록 표면 | `core` |
| `zlink-framework-kotlin` | coroutine · `Flow` · DSL extension | `core` |
| `zlink-framework-codec-protobuf` | Protobuf codec **extension** | `core` |
| `zlink-framework-codec-msgpack` | MessagePack codec **extension** | `core` |
| `zlink-framework-locations-redis` | Redis location store **extension** | `core` |
| `zlink-framework-testkit` | 테스트 지원 | `core` |
| `zlink-http-client` · `zlink-http-client-kotlin` | fluent HTTP/JSON client | — |
| `zlink-stream-connector` | **client** connector — 서버 framework에 의존하지 않는다 | 없음 |

**분리 원칙:**

- **codec 구현을 core에 섞지 않는다.** JSON은 기본 codec이고, Protobuf·MessagePack은 **extension
  모듈**로 분리한다. 같은 extension을 framework codec registry, HTTP client, stream connector가
  **공유한다**([channel-messaging §6](../../11-channel-messaging.ko.md)).
- **location store 구현도 extension이다.**
- **connector는 서버 framework 모듈을 참조하지 않는다.** 반대 방향도 같다.
- **host adapter(starter)와 core를 나눈다.** core는 Spring Boot에 의존하지 않는다.
- **Kotlin 표면은 별도 모듈이다.** core는 Kotlin에 의존하지 않는다.

## 2. 배포 계획

| 모듈 | 배포 채널 | 소비자 |
|---|---|---|
| `zlink-framework-core` · `zlink-framework-spring-boot-starter` | Maven | 서버 애플리케이션 |
| `zlink-framework-kotlin` | Maven | Kotlin 서버 |
| `zlink-framework-codec-*` | Maven | codec이 필요한 서버·client |
| `zlink-framework-locations-redis` | Maven | 다중 프로세스 배포 |
| `zlink-stream-connector` | Maven | **JVM 애플리케이션**(서버 도구·E2E·봇) |

**Java/Kotlin connector의 대상은 JVM 애플리케이션 하나뿐이다.** 게임 엔진과 브라우저는 담당하지
않으므로 엔진별 갈래가 없다([stream-connector 공통 스펙 §2](../../../stream-connector/32-stream-connector.ko.md)).

**미결정:** connector의 Maven 좌표 확정.

## Channel

### 계약 기준

SPOT route를 받는 channel은 local `ROUTER` receive loop 안에서 core
`SpotRouteBridge` handoff를 함께 사용한다. 일반 channel packet은 기존 channel
dispatcher가 처리하고, SPOT relay packet만 bridge가 소비한다. outbound `DEALER`나
route mesh `ROUTER` socket은 channel runtime 소유이며, `SpotNode`에 직접 attach하지
않는다.

### 1. 목표

`Spring Boot` 애플리케이션 안에서 아래 경험을 제공하는 것이 목표다.

- channel 이름 기준 direct call
- bean으로 주입되는 공용 outbound client
- bean으로 주입되는 `ZLinkFanoutClient`와 `ZLinkRouteClient`
- annotation 기반 request/send handler
- HTTP controller 안에서도 같은 `ZLinkClient` 사용

### 2. 등록 방식

같은 역할은 자동 연결과 수동 연결 중 하나만 선택한다.

```java
@Configuration
@EnableZLinkFramework
public class ZLinkConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions framework) {
        framework.addHandlersFromPackageOf(ZLinkConfig.class);

        framework.addClientServerChannel("api")
            .enableServer("tcp://0.0.0.0:7100")
            .addHandlerGroup("api");

        framework.addClientServerChannel("profile")
            .enableClient();

        framework.addClientServerChannel("account")
            .enableClient();

        framework.addFanoutChannel("profile-events")
            .enablePublisher("tcp://0.0.0.0:7200")
            .enableSubscriber();

        framework.useInMemoryLocationStores();
    }
}
```

수동 연결은 아래처럼 둔다.

```java
framework.addClientServerChannel("profile")
    .enableClient("tcp://10.0.10.15:7101");
```

앱 전체에서는 역할별로 방식을 나눠 쓸 수 있다.
예를 들어 `profile.client`는 discovery, `account.client`는 manual로 둘 수 있다.

중요한 점은 수동 연결이 `channel` 전체 설정이 아니라 `channel + capability`
설정이라는 점이다. 예를 들어 같은 `profile` channel이라도 `profile.client`와
`profile.subscriber`는 다른 연결 집합이다.

여기서 client manual 연결은 remote `RoutingId`를 따로 받지 않는다. channel client는
하부 `DEALER(client)`가 connect된 peer 집합으로 요청을 보내는 모델이므로,
startup과 런타임 제어 모두 endpoint 집합만 관리하면 된다.

manual 역할은 startup 시점에 endpoint 집합을 등록한다.

일반 `PUB/SUB` event publish는 `ZLinkFanoutClient` 같은 별도 surface로 설명한다.
이 표면도 `channel name + topic` 기준으로 동작한다.

### 3. Handler 모델

```java
@Component
public final class UserHandlers {
    private final ZLinkClient client;

    public UserHandlers(ZLinkClient client) {
        this.client = client;
    }

    @ZLinkRequest
    public GetUserReply getUser(
        GetUserRequest request,
        ZLinkRequestContext context) {
        GetAccountReply account = client.requestToChannel(
            "account",
            new GetAccountRequest(request.accountId())
        ).submit(GetAccountReply.class).toCompletableFuture().join();
        return new GetUserReply(request.accountId(), account.nickname());
    }
}
```

기본 packet key는 `GetUserRequest` 같은 payload 타입 이름을 쓴다.
외부 계약 때문에 다른 이름이 필요할 때만 annotation 또는 options에서 override한다.

### 4. Dispatch 기준

- 일반 request/send handler dispatch는 local `ROUTER(server)`가 받은 메시지 기준이다.
- outbound `DEALER(client)`가 받은 메시지는 reply correlation 경로로 본다.
- 따라서 `ROUTER -> DEALER` 임의 push는 현재 channel messaging 공용 계약에 넣지 않는다.

등록된 request handler 가 없거나 request payload decode, handler 실행 중 예외, invalid request frame 이
발생하면 server runtime 은 error reply 를 반환한다. 같은 사건은 Error 로그, counter,
`outcome=ERROR` 메시지 흐름 이벤트로도 남긴다.

send 또는 publish 에서 handler 를 찾지 못하면 reply 를 만들지 않고 drop 한다. send 는 Warning 로그와
counter, publish 는 Debug 로그 또는 counter 와 message-flow event 를 남긴다. observer 가 없더라도
기본 로그와 counter 는 생략하지 않는다. observer callback 실패는 dispatch 결과를 바꾸지 않는다.

### 5. Outbound-only 앱

local handler 없이 client만 쓰는 앱도 가능해야 한다.

```java
@Configuration
@EnableZLinkFramework
public class OutboundOnlyConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions framework) {
        framework.addClientServerChannel("profile")
            .enableClient();
        framework.useInMemoryLocationStores();
    }
}
```

이 경우 local `ROUTER(server)`는 열지 않고 outbound `DEALER(client)`만 만든다.

### 6. Route mesh

Java framework도 `.NET`과 같이 channel을 세 종류로 나눈다.

| Builder | 용도 |
|---------|------|
| `addClientServerChannel(...)` | 일반 server/client request-send |
| `addFanoutChannel(...)` | pub/sub fanout |
| `addRouteMeshChannel(...)` | target node `RoutingId` 또는 spot handle을 대상으로 하는 routed channel |

route mesh는 session actor relay를 대체하지 않는다. application이 특정 node로
route send/request를 보내야 할 때 쓴다. 같은 runtime 안의 local managed actor
binding은 framework 내부 dispatch를 사용하고, remote actor binding은 stream node의

```java
RouteMeshChannelBuilder route = framework.addRouteMeshChannel("play-route")
    .enableClient();
route.setRoutingId(RoutingId.from("play-node"));
```

route mesh는 server 역할과 client 역할을 따로 선언한다. 들어오는 route handler나
SPOT route ingress를 받아야 하는 runtime은 `enableServer(endpoint)`로 local ROUTER
endpoint를 연다. 다른 node로만 request/send를 보내는 runtime은 bind endpoint 없이
`enableClient()`를 선언하고 Discovery로 peer를 찾거나, `enableClient(endpoint)`로
수동 peer에 연결한다.

Location store 기반 Spot remote ref 기본 구현을 쓰려면 route mesh channel이 필요하다.
spot mesh 이름과 route mesh channel 이름이 다르면 `configureLocations()`에서
spot router channel 매핑을 명시해야 한다.

## SPOT

### 계약 기준

외부 route channel에서 특정 Spot으로 들어오는 send/request는 framework가 core
`SpotRouteBridge`를 내부에서 사용해 자동으로 연결한다. Java framework runtime은
`bindings/java`의 public `createRouteBridge()` / `SpotRouteBridge` 표면으로
같은 프로세스의 RouteMesh channel socket을 bridge에 연결한다. channel socket은
channel runtime이 계속 소유하며, bridge는 SPOT relay packet만 분류한다. local
`SpotNode` topic plane으로 외부 publish가 필요하면 raw `PUB` attach가 아니라 public
publisher handle을 사용한다.

> 이 문서는 [SPOT 메시징 공통 스펙](../../20-spot-messaging.ko.md)의 **투영**이다. SPOT의 개념
> 위치, outbound 세 축, publish·subscribe 모델, dispatch 실패 정책, route ingress 규칙,
> startup validation은 공통 스펙이 소유한다. 이 문서는 **언어 표면**만 고정한다.

### 1. 방향

`SPOT`은 별도 raw runtime으로 노출하기보다, `Spring Boot` bean lifecycle 안에서
등록하고 관리하는 편을 기본으로 본다.

- root location store 등록과 역할별 discovery 활성화
- spot node 설정 등록과, 그에 따른 `ZLinkSpotManager`/`ZLinkSpotOutbound` 등 capability bean 조건부 노출
- current channel publish/subscribe와 route bridge channel socket 경로
- local spot 인스턴스가 없는 외부 노드용 publisher client 경로
- Entry Spot과 user Spot factory
- 같은 프로세스의 RouteMesh channel과 SpotNode 자동 연결
- 필요할 때만 spot-to-spot routed 호출 허용

현재 공통 정책 기준으로는 아래를 같이 지켜야 한다.

- `SpotNode`는 channel 이름을 직접 소유하지 않고, attach된 discovery view가 active
  channel 범위를 정한다.
- 역할은 `router`, `pub/sub`, route bridge channel socket, attach된 spot
  publisher client로 나눠서 설명한다.
- spot factory는 Spot type 기준으로 등록하고, 같은 Spot type 재등록은 덮어쓰지 않고
  예외로 본다.
- spot 생성은 Spot type 기준으로 설명하고, 운영 코드는 `spotRid`로 생성된 Spot을
  다시 조회할 수 있어야 한다.
- timer는 공용 scheduler보다 spot lifecycle registration 표면으로 두는 편이
  자연스럽다.
- 같은 runtime 안의 local managed session actor dispatch는 framework 내부 dispatch를
  사용한다. remote session actor dispatch는 Spot route channel이 아니라 SessionRelay
  attach를 사용한다.

### 2. 기본 등록

```java
@Configuration
@EnableZLinkFramework
public class SpotConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions framework) {
        framework.useInMemoryLocationStores();
        ZLinkSpotNodeBuilder node = framework.addSpotMesh("game.stage");
        node.enableRouter("tcp://0.0.0.0:9000");
        node.enablePubSub("tcp://0.0.0.0:9001");
        node.configureEntrySpot()
            .setRoutingId(RoutingId.from("play.entry"));
        node.addEntrySpot(GameEntrySpot.class);
        node.addSpotFactory(GameRoomSpot.class);
    }
}
```

### 3. Public surface

- `ZLinkSpotManager`
- `ZLinkEntrySpot`
- `ZLinkSpotActor*Handler`
- current channel publish/subscribe
- route bridge channel socket을 통한 다른 channel send/request
- local spot 인스턴스가 없는 외부 노드용 `ZLinkSpotPublisherClient`
- 필요할 때만 `spot-to-spot` routed send/request

즉 high-level `SPOT` 표면은 `rid` 직접 지정보다 current channel publish와
cross-channel client를 먼저 설명하는 편이 맞다. 다만 실제 운영 코드가 Spot type으로
생성하고 `spotRid`로 다시 조회해야 하므로, `ZLinkSpotManager`도 public surface에
함께 둬야 한다.

### 4. Spot-to-spot

spot-to-spot routed 호출은 남긴다. 다만 일반 channel messaging과 섞지 않고,
advanced surface로 설명한다. 현재 SPOT 문맥의 `context.outbound()`(`ZLinkSpotOutbound`)
가 target SPOT `RoutingId` 하나만 받아 routed request 를 보낸다. target node 와 route
channel 해소는 `SpotRemoteRefResolver` 가 맡는다.

```java
context.outbound()
    .requestToSpot(targetSpotRid, request)
    .submit(StageReply.class);
```

### 5. Entry Spot과 user Spot

actor를 지원하려면 SpotNode에는 Entry Spot과 user Spot factory가 함께 있어야 한다.
Entry Spot은 actor 생성 직후의 기본 위치이며, 인증이나 target user Spot 선택 같은
입구 로직을 맡는다. user Spot은 room, stage, zone 같은 도메인 상태를 보관한다.

Entry Spot actor packet은 대상 actor의 mailbox에서 순서대로 처리된다. 같은 actor의
packet은 겹치지 않지만, 서로 다른 actor의 Entry Spot actor packet은 Entry Spot 하나의
실행 줄 때문에 서로 기다리지 않는다. Entry Spot lifecycle callback은 Entry Spot 자체의
입구 정책을 다루므로 actor mailbox로 옮기지 않는다.

user Spot의 message dispatch는 Spot 단위 실행 문맥 하나를 기준으로 직렬화한다.
route packet, subscription packet, user Spot actor packet, actor lifecycle callback,
framework managed timer callback은 같은 Spot 안에서 동시에 실행되지 않는다. handler가
`CompletionStage`를 반환하면 framework는 그 stage가 끝난 뒤 같은 Spot의 다음 dispatch를
시작한다. 이 규칙은 Java handler와 Kotlin `suspend fun` annotation handler에 같이
적용된다. Kotlin handler는 framework 소유 coroutine adapter에서 실행되고, adapter가
반환한 `CompletionStage`가 Spot serial queue의 완료 기준이 된다.

Entry Spot actor handler는 Entry Spot 인자를 받지만 Entry Spot 전체 실행 줄에 들어가지
않는다. handler는 actor별 상태를 다루는 곳이며, Entry Spot 객체의 가변 필드를 여러
actor가 공유하는 동기화 수단으로 쓰면 안 된다. Entry Spot lifecycle callback과 route 같은
Entry Spot 자체 상태 흐름은 별도 Entry Spot 실행 문맥에서 처리한다.

request, join과 worker는 `CompletionStage` 완료 표면 하나만 제공한다. framework는
보호 중인 Spot/actor 상태의 직렬성을 유지하면서 완료에 필요한 독립 실행을 진행하고,
continuation을 원래 실행 문맥에서 재개한다.
Java public handler에는 별도 cancellation token을 전달하지 않는다. request, actor join과
worker completion은 timeout, host shutdown과 `CompletionStage` 완료 규칙을 따른다.

SPOT route request 에 handler 가 없거나 payload decode, handler 예외, invalid frame 이 발생하면 reply
path 가 있는 경우 error reply 를 반환한다. actor request 도 같은 원칙을 따른다. 같은 process 안의
local actor call 처럼 reply frame 이 없는 경로는 `CompletionStage` 를 framework error 로 완료한다.

SPOT route send, subscription, actor send 는 reply 를 만들 수 없으므로 실패한 메시지를 drop 한다.
route send 와 actor send 는 Warning 로그와 counter, subscription 은 Debug 로그 또는 counter 와
`outcome=ERROR` 메시지 흐름 이벤트를 남긴다. observer 실패는 dispatch loop 나 shutdown 을 깨지 않는다.

## STREAM

### 1. 방향

`STREAM`은 일반 request handler와 다른 전용 session 모델로 설명한다. 정식 계약은
공통 stream header를 사용하는 session 하나다.

- stream node는 bind endpoint와 session type을 등록한다.
- session callback은 `ZLinkSessionContext`를 통해 peer 정보, client 응답,
  actor binding, close 제어를 사용한다.
- inbound payload는 framework `ZLinkMessage`다.
- 같은 session 안의 callback은 직렬로 실행한다.
- 서로 다른 session은 독립적으로 진행될 수 있다.

recv loop를 application 표면에 직접 노출하지 않는 편을 기본으로 본다.

### 2. 등록

```java
@Configuration
public class StreamConfig {
    @Bean
    ZLinkFrameworkConfigurer streamOptions() {
        return options -> {
            options.useInMemoryLocationStores();
            ZLinkSpotNodeBuilder node = options.addSpotMesh("game.stage");
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

### 2.1 STREAM compression 설정

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

### 3. Session 계약

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

### 4. session relay

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

### 5. Client Connector

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

## Actor session

### 1. 방향

Actor는 stateful application object다. 일반 handler처럼 메시지마다 새 객체로
처리하지 않고, actor id로 식별되는 같은 인스턴스가 여러 메시지를 받는다.

Actor 상태는 두 축으로 나눈다.

| 축 | 의미 |
|----|------|
| 위치 | Entry Spot 또는 user Spot 중 어디에서 실행되는가 |
| binding | STREAM session에 묶였는가 |

session binding은 client relay 경로일 뿐이다. actor가 실제로 어느 Spot에 있는지는
Entry Spot/user Spot 위치 축이 결정한다. session close가 actor를 자동으로 user Spot
밖으로 이동시키지 않는다.

### 2. 등록

```java
@Configuration
@EnableZLinkFramework
public class ActorConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions framework) {
        framework.useInMemoryLocationStores();
        ZLinkSpotNodeBuilder node = framework.addSpotMesh("game.stage");
        node.enableRouter("tcp://0.0.0.0:9001");
        node.addEntrySpot(GameEntrySpot.class);
        node.addSpotFactory(GameRoomSpot.class);
        node.addActorFactory("player", PlayerActorFactory.class);

        ZLinkStreamNodeBuilder stream = framework.addStreamNode("gateway");
        stream.bind("tcp://0.0.0.0:7201");
        stream.registerSession(ClientSession.class);
    }
}
```

local managed actor instance를 `bind(ZLinkActor)`로 bind하는 경우 stream node는
`.NET` direct stream과 같이 session relay 없이 actor packet을 framework 내부
dispatch 경로로 전달한다. remote `ActorRef`를 bind하거나 별도 session gateway
stream node가 해당 SpotNode의 SessionRelay에 연결되어야 한다. Java framework는 이
의미를 route mesh channel packet으로 대신 구현하지 않는다.

### 3. Actor 계약

```java
public interface ZLinkActor {
    String actorId();
    ZLinkActorContext context();
    default void configure() {
    }
}

public interface ZLinkActorFactory {
    ZLinkActor create(
        String actorId,
        ZLinkActorContext context);
}

public interface ZLinkActorManager {
    CompletionStage<ActorRef> create(String actorId, String actorType);
    CompletionStage<ActorRef> create(
        String actorId,
        String actorType,
        ZLinkMessage createRequest);
    CompletionStage<Optional<ActorRef>> find(String actorId);
    CompletionStage<ActorRef> getOrCreate(String actorId, String actorType);
    CompletionStage<ActorRef> getOrCreate(
        String actorId,
        String actorType,
        ZLinkMessage createRequest);
}
```

`actorType`은 application이 정하는 짧은 문자열 키다. 같은 actor id를 다른
actorType으로 다시 쓰면 설정 또는 런타임 오류로 실패해야 한다.
manager 는 actor 객체를 직접 반환하지 않고 `ActorRef`를 반환한다. Spot 밖
public handler는 이 ref로 actor join/admission을 직접 수행하지 않는다. actor 초기
상태는 create request와 Entry Spot의 create callback에서 설정한다.

### 4. Session Binding

```java
public interface ZLinkSessionActors {
    List<ZLinkSessionActor> bound();

    CompletionStage<ZLinkSessionActor> bind(ZLinkActor actor);

    CompletionStage<ZLinkSessionActor> bind(ActorRef actor);

    CompletionStage<ZLinkSessionActor> bindOrGet(ActorRef actor);

    Optional<ZLinkSessionActor> find(String actorId);
}

public interface ZLinkSessionActor {
    String actorId();
    ActorRef ref();

    CompletionStage<Void> relay(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload);

    CompletionStage<Void> notifyDisconnected();
}
```

session은 local actor instance 또는 framework actor locator인 `ActorRef`에
bind할 수 있다. local actor instance binding은 같은 framework runtime 안에서 handler
annotation catalog를 사용해 dispatch한다. remote binding은 actor 위치를 담은 `ActorRef`를
session public 입력으로 받고, core SessionRelay와 logical actor handle을 사용한다.

### 5. Bound Session

actor에서 현재 client session으로 보내는 표면은 `ZLinkBoundSession`이다.
STREAM session은 framework가 만든 `ZLinkSessionContext`를 constructor로 받을 수 있다.
application session은 이 context의 `actors()`로 actor binding을 만들고, `client()`로
client reply 또는 push를 보낸다. sample 안에서 별도 session context나 bound session
stand-in을 만들어 이 경로를 대체하지 않는다.

```java
public interface ZLinkActorContext {
    Optional<RoutingId> spotRid();
    ZLinkBoundSession boundSession();

    ZLinkActorJoinCall joinSpot(RoutingId spotRid, Object request);
    ZLinkActorJoinCall joinEntrySpot(RoutingId spotNodeRid, Object request);
}

public interface ZLinkActorJoinCall {
    ZLinkActorJoinCall timeout(Duration timeout);
    CompletionStage<ZLinkActorJoinResult<Void>> submit();
    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(
        Class<TReply> replyType);
}

public sealed interface ZLinkActorJoinResult<TReply>
    permits ZLinkActorJoinResult.Accepted, ZLinkActorJoinResult.Rejected {
    TReply reply();

    record Accepted<TReply>(ActorRef actor, TReply reply)
        implements ZLinkActorJoinResult<TReply> {
    }

    record Rejected<TReply>(TReply reply)
        implements ZLinkActorJoinResult<TReply> {
    }
}

public interface ZLinkBoundSession {
    <TMessage> ZLinkBoundSessionSendCall send(TMessage message);
    CompletionStage<Void> disconnect();
}
```

`submit(...)`은 `CompletionStage`를 반환하는 유일한 완료 terminator다. framework는
보호 중인 actor/Spot 상태의 직렬성을 유지하면서 join에 필요한 독립 실행을 진행한다.

`joinSpot(...)`은 actor가 Entry Spot 이후 실제 user Spot으로 들어가는 요청이다. 호출은
`CompletionStage`로 완료되며 framework는 backend `SpotNode.joinActor(...)` 결과를
받은 뒤 actor context의 `spotRid()` 상태를 갱신한다. nullable Spot 식별자가 join
상태의 단일 기준이다. Kotlin에서는 같은
Java `CompletionStage`를 `suspend` wrapper로 감싸서 사용한다.

`joinSpot(...)`/`joinEntrySpot(...)` 도 `timeout(Duration)` override 를 갖는다. 생략하면
기본 timeout 을 쓰고, join 대기가 기본과 달라야 할 때만 지정한다(샘플은 기본값).

`ZLinkBoundSession`은 server-to-client request API를 제공하지 않는다. client
request에 대한 응답은 actor request handler의 반환값과 원래 request correlation으로
처리한다.
`disconnect()`는 현재 actor에 묶인 client session을 backend binding에서 해제하고
actor context의 bound session을 비운다. 이 호출은 server가 session을 닫는 의미이므로
Spot actor disconnected callback을 대신 실행하지 않는다.

actor가 join한 Spot 상태가 필요하면 Spot handler가 받은 `spot` 인자를 사용한다.

session actor의 `notifyDisconnected()`는 backend actor binding을 해제한 뒤,
그 binding이 actor context의 현재 bound session과 일치할 때만 disconnected lifecycle을
실행한다. 오래된 session binding에서 disconnect 알림이 늦게 도착해도 현재 bound
session과 disconnected lifecycle callback을 건드리지 않는다.
`relay(payload)`는 session이 받은 actor packet을 bound actor route로
전달한다. `payload`는 framework `ZLinkMessage`이며, session은 이 값을 decode 하거나
relay API에 그대로 넘긴다.

### 6. Handler

Entry Spot actor handler와 user Spot actor handler는 분리한다. Entry Spot은 인증,
초기 actor 생성, target Spot 선택 같은 입구 로직을 맡고, user Spot은 실제 room,
stage, zone 안의 domain packet을 처리한다.

```java
public interface ZLinkEntrySpotActorRequestHandler<
    TEntrySpot extends ZLinkEntrySpot,
    TActor extends ZLinkActor,
    TRequest,
    TReply> {
    CompletionStage<TReply> handle(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request);
}

public interface ZLinkSpotActorSendHandler<
    TSpot extends ZLinkSpot,
    TActor extends ZLinkActor,
    TMessage> {
    CompletionStage<Void> handle(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorSendContext context,
        TMessage message);
}
```

### 7. Runtime 규칙

- actor 생성은 `ZLinkActorManager`를 통해 명시적으로 수행한다.
- actor packet handler는 actor class가 아니라 Entry Spot 또는 user Spot registry에
  등록한다.
- session callback에서 받은 payload는 framework `ZLinkMessage`다. session은 이 값을
  decode 하거나 relay API에 그대로 넘긴다.
- client close는 session binding cleanup만 수행한다. actor disconnect callback이
  필요하면 application이 `notifyDisconnected()`를 호출한다.
- remote actor로 relay할 때 Java framework는 backend stream의 bound actor send를
  사용한다.
- route mesh channel은 application Spot route egress용이다. session actor relay
  설정으로 해석하지 않는다.

### 8. Kotlin 사용 표면

Kotlin은 Java contract 위에 coroutine extension을 얹는다.

```kotlin
val actor = actorManager.getOrCreate("player-42", "player").await()
session.context.actors.bind(actor).await()

actor.context.boundSession.send(PlayerJoined(...)).submit()
```

Kotlin DSL은 등록 코드를 짧게 만들 뿐이고, actor lifecycle 의미는 Java contract와
같다.

## Monitoring

### 1. 방향

운영 이벤트는 일반 request/send handler와 다르다. 이 문서는 아래를 기본으로 본다.

- event kind는 enum으로 둔다.
- 실제 callback payload는 record로 둔다.
- socket은 하부 monitor를 감싼다.
- registry/spot는 snapshot diff 기반으로 다시 올린다.
- stream session lifecycle로 매핑 가능한 transport 오류만 `ZLinkStreamError`로
  session callback에 올린다.
- timer handler 실패는 spot runtime event로도 볼 수 있어야 한다.

### 2. 등록 예시

Java monitoring 구성은 monitoring source만 등록한다. 실제 socket, registry, spot source는 같은 application 안에 이미
framework나 registry 등록으로 만들어져 있어야 한다.

```java
@Configuration
public class MonitoringConfig {
    @Bean
    ZLinkMonitoringOptionsCustomizer zlinkMonitoringOptionsCustomizer() {
        return options -> {
            options.addSocketEvents(
                "profile.server",
                ZLinkSocketEventKind.CONNECTION_READY,
                ZLinkSocketEventKind.DISCONNECTED);
            options.addSpotEvents("stage-node", Duration.ofSeconds(1));
        };
    }
}
```

Spring Boot starter는 configurer가 있으면 monitoring hosted lifecycle을 등록한다.
configurer가 없으면 monitoring runner를 만들지 않는다.

등록 가능한 source는 아래로 제한한다.

| Source | 등록 메서드 | source name 기준 |
|--------|-------------|------------------|
| socket | `addSocketEvents(...)` | channel 역할 logical name |
| spot | `addSpotEvents(...)` | SpotNode name |

socket과 spot source 이름은 startup 시점에 실제 runtime source와 대조한다. 이름이
맞지 않으면 startup validation 오류다. registry source 이름은 embedded registry에서
발생한 event의 label로 쓰이며, registry monitoring을 쓰려면 embedded registry가 같은
application 안에 있어야 한다. registry와 spot polling interval은 `Duration.ZERO`보다
커야 한다.

### 3. Handler 예시

```java
@Component
public final class ProfileSocketMonitor
    implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {

    @Override
    public void handle(ZLinkSocketEvent event) {
    }
}

@Component
public final class RegistryMonitor

    @Override
    }
}
```

source 이름은 logical name을 쓰는 편이 자연스럽다.

- socket: `profile.server`, `profile.client`
- registry: `registry`, `ops-registry`
- spot: `stage-node`

Spring adapter는 typed event handler를 호출한다. public 계약은
`ZLinkRuntimeEventHandler<T>`가 기준이다.

### 4. Event 모델

event payload는 source별 record로 둔다. application handler는 native event mask나
registry snapshot 형식을 직접 해석하지 않는다.

```java
public interface ZLinkRuntimeEvent {
    String sourceName();
    Instant timestamp();
}

public interface ZLinkRuntimeEventHandler<T extends ZLinkRuntimeEvent> {
    CompletionStage<Void> handle(T event);
}
```

handler 실패는 monitoring runner를 중단하지 않는다. 실패 event를 내부 logger와
diagnostic counter에 기록하고 다음 event dispatch를 계속한다.

### 5. Lifecycle

monitoring lifecycle은 source runtime이 준비된 뒤 attach된다.

1. framework와 registry option validation
2. framework와 registry runtime start
3. monitoring source validation
4. socket monitor attach
5. registry/spot polling task start

shutdown은 polling task와 monitor attach를 먼저 해제한 뒤 framework와 registry
runtime을 멈춘다. 일시적인 registry query 실패나 spot snapshot 실패는 startup 실패가
아니라 runtime event 또는 diagnostic failure로 다룬다.

### 6. 검증 기준

- socket/SpotNode monitoring source 이름이 runtime source name과 맞지 않으면
  startup validation 오류다. registry source는 embedded registry 존재 여부로 매핑되어
  event label로 쓰인다.
- registry/spot polling interval이 `0` 이하이면 startup validation 오류다.
- socket native event는 typed event로 변환된다.
- registry/spot snapshot diff는 typed event로 변환된다(발행 spot event는 status/peers/subjects).
- `ZLinkRuntimeEventHandler<T>` 실패는 monitoring runner를 중단하지 않는다.

### 7. 메시지 흐름 추적 (dispatch 관측)

monitoring 이 socket/registry/spot **runtime 변화**를 다룬다면, 메시지 흐름 추적은 한 메시지의
생애주기(왔나/처리됐나/응답됐나/보냈나/응답받았나)를 dispatch 길목에서 관측한다. 공통 의미는
[공통 스펙 — 메시지 흐름 추적](../../52-message-flow-tracing.ko.md)이 소유하고, 이 절은
Java 표면만 적는다. dispatch 제어가 아니라 관측이며, observer 실패가 처리나 응답을 깨지 않는다.

#### 7.1 표면

| 공통 개념 | Java 타입 / 멤버 |
|-----------|------------------|
| 로그 모드 | `ZLinkMessageFlowLogMode` { `OFF`, `ERRORS_ONLY`(기본), `KEY_TRANSITIONS`, `VERBOSE`, `DIAGNOSTIC` } |
| outcome | `ZLinkMessageFlowOutcome` { `RECEIVED`, `DISPATCHED`, `REPLIED`, `DROPPED`, `SENT`, `REPLY_RECEIVED`, `ERROR` } |
| event | `ZLinkMessageFlowEvent`(record): `outcome()`, `surface()`, `messageKind()`, `packetName()`, `channelName()`, `topic()`, `correlationId()`, `sourceRid()`, `spotRid()`, `actorId()`, `messageSize()`, `errorReason()`, `errorAction()`, `exception()` |
| observer | `ZLinkMessageFlowObserver.onMessageFlow(ZLinkMessageFlowEvent)` → `CompletionStage<Void>` |
| 진단 옵션(read-only) | `configureDispatch().diagnostics()` → `ZLinkDiagnosticsOptions` { `messageFlow()`, `effectiveMessageFlow()`, `sampleRate()`, `includeMessageSizes()`, `logFile()`, `label()` } |
| 런타임 토글 | `ZLinkMessageFlowControl.setMessageFlowMode(...)` / `messageFlowMode()` (Spring `ZLinkFrameworkLifecycle` 빈이 구현·위임) |

게이팅(공통 규칙): `DROPPED`·에러는 `ERRORS_ONLY` 이상, 성공 전이는 `KEY_TRANSITIONS` 이상에서
발화한다. `sampleRate<1`은 성공 전이만 thinning하고 `DROPPED`·에러는 항상 통과한다.

#### 7.2 설정 (builder 전용)

framework options 등록(`ZLinkFrameworkConfigurer`)에서 `configureDispatch()` 체인으로만 설정한다.
진단 필드는 read-only다.

```java
@Bean
ZLinkFrameworkConfigurer dispatchTracing() {
    return options -> options.configureDispatch()
        .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
        .traceLogFile("logs/flow-api.log")   // 지정=전용 파일(앱 로그와 분리)
        .traceLabel("api")                   // 구조화 필드 label=
        .includeMessageSizes(true);          // VERBOSE에서 size=
}
```

- `traceLogFile` 지정 시 트레이싱/에러는 전용 파일로만, 미지정 + 앱 로거 sink 있으면 통합, 둘 다
  없으면 표준 에러스트림 폴백. 출력은 key=value 구조화(JUL/파일) + `label=`로 콜렉터 ingest 가능.
- observer는 `setMessageFlowObserver(MyObserver.class)` 또는 인스턴스/람다로 등록한다(단일 메서드
  인터페이스라 람다 호환). 콜렉터/OTel 어댑터는 앱 레이어 책임이다(공통 스펙 §6).
- `OFF`일 때는 이벤트를 생성조차 하지 않아(호출부 가드 + lazy) 운영 성능에 영향이 없다.

#### 7.3 런타임 토글

`ZLinkMessageFlowControl`은 Spring `ZLinkFrameworkLifecycle` 빈이 구현하므로 주입받아 재시작 없이
모드를 바꾼다. 공유 live cell을 모든 surface가 읽어 즉시 반영된다.

```java
flowControl.setMessageFlowMode(ZLinkMessageFlowLogMode.KEY_TRANSITIONS);  // off→on
```

#### 7.4 샘플

Java/Kotlin Bingo 3노드는 각자 `messageFlow(KEY_TRANSITIONS)` +
`traceLogFile(.../flow-<role>.log)` + `traceLabel(role)`로 분리 파일 로깅을 시연한다
(`BINGO_LOG_DIR` override). Kotlin은 같은 Java 런타임을 공유하며 `configureDispatch { }` DSL과
`onMessageFlow { }` 람다 옵저버(에르고노믹스)를 추가로 제공한다.

### 8. 런타임 메트릭 (runtime metrics)

공통 의미는 [공통 스펙 — 런타임 메트릭](../../51-runtime-metrics.ko.md)이 소유한다. 이 절은 Java 표면만
적는다.

> **설계 원칙(깊은 모듈): 공통 케이스는 무설정.** Spring Boot 앱에 Micrometer `MeterRegistry` 빈이
> 있으면 framework가 자동으로 바인딩해 카탈로그 계기를 방출한다. 앱은 계기를 하나도 선언하지 않는다.

#### 8.1 표면

| 공통 개념 | Java |
|-----------|------|
| 계기 이름 접두 | `zlink.` (Micrometer meter name, 공통 §4.0 바이트 동일) |
| 계기 방출 | 앰비언트 `MeterRegistry`에 `Counter`/`Gauge`/`Timer`(histogram) 등록 |
| 앱 연결(공통 케이스) | Spring Boot Actuator + Micrometer registry 자동 구성 — 별도 zlink 설정 없음 |
| meter/scope 이름 | 계기 접두 `zlink.` (Micrometer는 scope 개념이 없어 접두가 바이트 동일 식별자, 공통 §11) |
| 커스텀 조정(선택) | `ZLinkMetricsCustomizer { void customize(MeterRegistry registry); }`(공통 태그 추가·필터 등) — `ZLinkMonitoringOptionsCustomizer` 선례와 동형 |

- 공통 §3 `updown`=Micrometer `Gauge`(등록 시 상태 참조), `observable`=`Gauge` 콜백. histogram은 **duration
  계기(`.duration`/`.latency`)=`Timer`, 그 외 분포=`DistributionSummary`**로 고정한다. framework는
  duration을 Micrometer `Timer`로 기록하고 표준 export의 초 단위를 사용한다. contract/E2E evidence도
  `Timer` snapshot을 초로 읽어 공통 단위를 유지한다.
- registry가 없으면(예: 테스트) 계기는 no-op registry로 접혀 성능 영향이 없다(공통 §7.2).
- 대시보드·exporter(Prometheus/OTLP)는 앱 몫이다(공통 §6).

### 9. 메시지 흐름 상관관계 (flow correlation)

공통 의미는 [공통 스펙 — 메시지 흐름 상관관계](../../53-flow-correlation.ko.md)가 소유한다. §7(메시지
흐름 추적)의 additive 확장이며 새 최상위 표면을 만들지 않는다.

#### 9.1 표면

| 공통 개념 | Java |
|-----------|------|
| 생성 gate | 기존 message-flow mode가 `OFF`가 아니면 create-if-absent 자동 생성 |
| event 필드(추가) | `String ZLinkMessageFlowEvent.flowId()`, `ZLinkFlowOrigin flowOrigin()` — 오류 이벤트에도 동일 |

```java
ZLinkFrameworkConfigurer dispatchTracing() {
    return configurer -> configurer.configureDispatch()
        .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS); // flow id 자동 생성·전파
}
```

- 생성은 모드 게이트, 전파는 무조건(공통 §2.2). stream/actor gateway 로거 자동 배선(공통 §7),
  게이팅 불변이다. Kotlin도 같은 설정을 사용하며 flow id 전용 DSL을 추가하지 않는다.

### 10. Graceful Drain & Handoff

공통 의미는 [공통 스펙 — Graceful Drain & Handoff](../../54-graceful-drain-handoff.ko.md)가 소유한다.
lifecycle 제어 표면(관측 아님)의 Java 투영이다.

> **설계 원칙(복잡도 하향): 공통 케이스는 무설정.** framework가 Spring `SmartLifecycle`로 graceful
> shutdown에 자동 참여해 drain한다. 앱은 코드를 쓰지 않는다.

#### 10.1 표면

```java
public enum ZLinkFlowOrigin { INBOUND, TIMER, APPLICATION, LIFECYCLE }
public enum ZLinkSpotDrainPolicy { DRAIN_NATURAL, RELEASE_AND_RECREATE }
public enum ZLinkDrainForceReason {
    DEADLINE_EXCEEDED, DRAINING_STATE_PUBLISH_FAILED, OWNER_CLEANUP_FAILED, TEARDOWN_FAILED
}
public sealed interface ZLinkDrainResult permits Drained, ForceStopped {}
public record Drained() implements ZLinkDrainResult {}
public record ForceStopped(ZLinkDrainForceReason reason) implements ZLinkDrainResult {}
public interface ZLinkDrainControl {
    CompletionStage<ZLinkDrainResult> drain();
    CompletionStage<ZLinkDrainResult> drain(Duration deadline);
    CompletionStage<ZLinkDrainResult> awaitDrained();
    boolean isReady();
}
```

| 공통 개념 | Java |
|-----------|------|
| 자동 drain(기본) | framework `SmartLifecycle` 빈이 shutdown에서 drain — 앱 코드 0 |
| SPOT drain 정책 | spot mesh 등록의 `useDrainPolicy(ZLinkSpotDrainPolicy.{DRAIN_NATURAL(기본)/RELEASE_AND_RECREATE})` |
| terminal result | sealed `ZLinkDrainResult` permits `Drained`, `ForceStopped(ZLinkDrainForceReason reason)`; reason은 `DEADLINE_EXCEEDED`, `DRAINING_STATE_PUBLISH_FAILED`, `OWNER_CLEANUP_FAILED`, `TEARDOWN_FAILED` |
| 명시 제어(선택) | `ZLinkDrainControl` { `drain(Duration deadline)`/`drain()`(30초)/`awaitDrained()` → `CompletionStage<ZLinkDrainResult>`, `boolean isReady()` } (빈) |
| readiness probe | starter가 `ZLinkDrainReadinessContributor`를 자동 등록해 Actuator readiness group에 반영(무설정). 또는 `ZLinkDrainControl.isReady()` 직접 조회 |
| 상태 관측 | 기존 `ZLinkRuntimeEventHandler<ZLinkDrainEvent>` 재사용. `ZLinkDrainEvent.state()` { `SERVING`/`DRAINING`/`DRAINED`/`FORCE_STOPPING` }, `sourceName()` = 고정값 `"drain"` |

```java
options.addSpotMesh("orders")
    .useDrainPolicy(ZLinkSpotDrainPolicy.RELEASE_AND_RECREATE);
```

- 비동기 반환에 `Async` 접미사를 쓰지 않는 이 코드베이스 관례(`transferOut`, `onMessageFlow`)에 맞춰
  `drain`으로 둔다.
- drain 상태 관측은 monitoring의 `ZLinkRuntimeEventHandler<T>`를 그대로 쓴다(같은 개념 → 같은
  메커니즘). **drain 이벤트는 source 등록이 필요 없다** — 저빈도 lifecycle 이벤트라 handler 빈 존재만으로
  monitoring configurer 유무와 무관하게 수신한다(공통 §9, 조용한 무관측 없음). Kotlin은
  `drainControl.drain(deadline).await()`와 `onDrain { }` 람다를 제공한다(§8).

## Registry

> **제거된 기능이다.** core의 Discovery/Registry 표면이 사라지면서 `ZLinkEmbeddedRegistryOptions`와
> `systems.zlink.framework.registry` 패키지도 제거됐다. **공개 계약이 아니다.**
>
> 위치 해석은 [location runtime](../../40-location-runtime.ko.md)과 location store extension을
> 사용한다. contract test가 sample에서 Registry 역할 사용을 금지한다
> (`SampleReleaseGateContractTest`: "Java Bingo roles must use the Redis location store extension
> instead of a Registry role").

## Stage wrapper

### 1. 기본 생각

playhouse `Stage` 같은 상위 객체는 `SPOT` 자체를 대체하는 것이 아니라, `SPOT` 위에 얹는
도메인 모델로 보는 편이 맞다. Java에서는 사용자가 만든 그 도메인 객체가
`ZLinkSpot<TActor>` 를 구현한다(별도 framework `Stage` 타입은 없다).

이 상위 도메인 객체(`Stage` wrapper)는 아래 역할을 가져야 한다.

- 현재 spot rid·node rid 노출(`context.spotRid()`/`context.nodeRid()`, 반환 `RoutingId`)
- packet handler registry
- timer 등록
- outbound channel client 접근

### 2. 분리 기준

- 상태와 도메인 메서드: `Stage`
- packet handler: 별도 bean
- 다른 channel 호출: `ZLinkSpotOutbound`

즉 `Stage` 안에 모든 packet 처리와 외부 호출을 몰아 넣지 않는 편이 맞다.
