<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](README.ko.md)

# Java/Kotlin 시스템 구조 — 등록과 부트스트랩

> 이 문서는 **Spring Boot 위에서 ZLink framework를 어떻게 구성하는가**를 소유한다. 등록,
> DI, lifecycle, 그리고 각 기능(channel · SPOT · STREAM · monitoring · registry)의 **등록 표면**이다.
>
> **기능의 의미와 동작 규칙은 공통 스펙이 소유한다** — [channel-messaging](../../11-channel-messaging.ko.md),
> [spot-messaging](../../20-spot-messaging.ko.md), [MeshNode](../../21-mesh-node.ko.md),
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

## 2. 배포 계약

| 모듈 | 배포 채널 | 소비자 |
|---|---|---|
| `zlink-framework-core` · `zlink-framework-spring-boot-starter` | Maven | 서버 애플리케이션 |
| `zlink-framework-kotlin` | Maven | Kotlin 서버 |
| `zlink-framework-codec-*` | Maven | codec이 필요한 서버·client |
| `zlink-framework-locations-redis` | Maven | 다중 프로세스 배포 |
| `zlink-stream-connector` | Maven | **JVM 애플리케이션**(서버 도구·E2E·봇) |

**Java/Kotlin connector의 대상은 JVM 애플리케이션 하나뿐이다.** 게임 엔진과 브라우저는 담당하지
않으므로 엔진별 갈래가 없다([stream-connector 공통 스펙 §2](../../../stream-connector/32-stream-connector.ko.md)).

## Channel

### Channel 계약 기준

Node direct, ChannelName, Spot과 Actor 메시지는 같은 MeshNode `ROUTER`를 사용한다.
ChannelName은 논리 membership과 handler namespace이며 별도 socket을 만들지 않는다.

### 1. 지원 기능

`Spring Boot` 애플리케이션에서 다음 기능을 제공한다.

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

        ZLinkMeshNodeBuilder mesh = framework.addRouteMesh("application")
            .listen("tcp://0.0.0.0:7100")
            .setRoutingId(RoutingId.from("app-1"));
        mesh.channelName("api").addHandlerGroup("api");
        mesh.channelName("profile");
        mesh.channelName("account");

        framework.addFanoutChannel("profile-events")
            .enablePublisher("tcp://0.0.0.0:7200")
            .enableSubscriber();

        framework.addLocationStore(redisLocationStore());
    }
}
```

수동 연결은 아래처럼 둔다.

```java
framework.addRouteMesh("application")
    .peerConnections()
    .connect("tcp://10.0.10.15:7101");
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
`event_id=zlink.dispatch_error`, `outcome=failed`, `action=reply_error` 메시지 흐름 이벤트로도 남긴다.

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
        ZLinkMeshNodeBuilder mesh = framework.addRouteMesh("application")
            .listen("tcp://0.0.0.0:7100")
            .setRoutingId(RoutingId.from("client-1"));
        mesh.channelName("profile");
        framework.addLocationStore(redisLocationStore());
    }
}
```

이 경우에도 MeshNode는 하나의 ROUTER endpoint와 RID를 가진다. Local handler 등록은 생략할 수 있다.

### 6. Route mesh

Java framework는 RouteMesh membership과 classic fanout을 구분한다.

| Builder | 용도 |
|---------|------|
| `addRouteMesh(...).channelName(...)` | ChannelName select-one request/send |
| `addFanoutChannel(...)` | pub/sub fanout |
| `addRouteMesh(...)`의 node client | target node `RoutingId` direct request/send |

route mesh는 session actor relay를 대체하지 않는다. application이 특정 node로
route send/request를 보내야 할 때 쓴다. 같은 runtime 안의 local managed actor
binding은 framework 내부 dispatch를 사용하고, remote actor binding은 stream node의

```java
ZLinkMeshNodeBuilder mesh = framework.addRouteMesh("play")
    .listen("tcp://0.0.0.0:7100")
    .setRoutingId(RoutingId.from("play-node"));
mesh.channelName("play-api");
mesh.peerConnections().connect(
    RoutingId.from("play-peer"),
    "tcp://10.0.10.15:7100");
```

MeshNode는 `listen(endpoint)`로 local ROUTER endpoint를 열고 같은 socket으로 inbound와
outbound traffic을 처리한다. 자동 peer는 Redis descriptor로 찾고 수동 peer는
`peerConnections()`에 endpoint 또는 expected RID와 endpoint를 등록한다.

분산 Spot·Actor 주소를 사용하면 owner MeshName과 함께 Redis location store를 등록한다.

## SPOT

### SPOT 계약 기준

Spot direct와 Logical Multicast는 owner MeshNode ROUTER를 사용한다. Logical Multicast는
remote MeshNode마다 한 번 route하고 수신 MeshNode가 node-local subscription만 검사한다.
classic fanout 이외에는 PUB/SUB socket을 만들지 않는다.

> 이 문서는 [SPOT 메시징 공통 스펙](../../20-spot-messaging.ko.md)의 **투영**이다. SPOT의 개념
> 위치, outbound 세 축, publish·subscribe 모델, dispatch 실패 정책, route ingress 규칙,
> startup validation은 공통 스펙이 소유한다. 이 문서는 **언어 표면**만 고정한다.

### 1. SPOT 방향

`SPOT`은 별도 raw runtime으로 노출하기보다, `Spring Boot` bean lifecycle 안에서
등록하고 관리하는 편을 기본으로 본다.

- root location store 등록과 역할별 discovery 활성화
- MeshNode 설정 등록과, 그에 따른 `ZLinkSpotManager`/`ZLinkSpotOutbound` 등 capability bean 조건부 노출
- ChannelName 기준 Logical Multicast와 Node·Channel client 경로
- local Spot 인스턴스가 없는 node의 publisher client 경로
- Entry Spot과 user Spot factory
- 같은 MeshNode의 Node·Channel·Spot·Actor route 공유
- 필요할 때만 spot-to-spot routed 호출 허용

공통 정책 기준으로 아래를 함께 지킨다.

- MeshNode는 하나 이상의 immutable ChannelName membership을 소유하고 active
  channel 범위를 정한다.
- MeshNode ROUTER가 Node·Channel·Spot·Actor traffic을 함께 처리하고 Logical Multicast
  publisher client도 같은 MeshName context를 사용한다.
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
        framework.addLocationStore(redisLocationStore());
        ZLinkMeshNodeBuilder node = framework.addRouteMesh("game")
            .listen("tcp://0.0.0.0:9000")
            .setRoutingId(RoutingId.from("game-1"));
        node.channelName("game.stage");
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
- ChannelName과 topic을 받는 Logical Multicast
- MeshNode client를 통한 Node direct와 ChannelName send/request
- local Spot 인스턴스가 없는 node의 `ZLinkSpotPublisherClient`
- 필요할 때만 `spot-to-spot` routed send/request

즉 high-level `SPOT` 표면은 `rid` 직접 지정보다 ChannelName publish와
channel client를 먼저 설명한다. 실제 운영 코드가 Spot type으로
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

actor를 지원하려면 MeshNode에는 Entry Spot과 user Spot factory가 함께 있어야 한다.
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
`event_id=zlink.dispatch_error`, `outcome=failed`, `action=drop` 메시지 흐름 이벤트를 남긴다.
observer 실패는 dispatch loop 나 shutdown 을 깨지 않는다.

## STREAM

### 1. STREAM 방향

`STREAM`은 일반 request handler와 다른 전용 session 모델로 설명한다. 정식 계약은
공통 stream header를 사용하는 session 하나다.

- stream node는 bind endpoint와 session type을 등록한다.
- session callback은 `ZLinkSessionContext`를 통해 peer 정보, client 응답,
  actor binding, close 제어를 사용한다.
- inbound payload는 framework `ZLinkMessage`다.
- 같은 session 안의 callback은 직렬로 실행한다.
- 서로 다른 session은 독립적으로 진행될 수 있다.

recv loop를 application 표면에 직접 노출하지 않는 편을 기본으로 본다.

### 2. STREAM 등록

```java
@Configuration
public class StreamConfig {
    @Bean
    ZLinkFrameworkConfigurer streamOptions() {
        return options -> {
            options.addLocationStore(redisLocationStore());
            ZLinkMeshNodeBuilder node = options.addRouteMesh("game")
                .listen("tcp://0.0.0.0:9001")
                .setRoutingId(RoutingId.from("game-1"));
            node.channelName("game.stage");
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
        actors.getOrCreate("game", "player-42", "player")
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
같은 process의 owner MeshNode가 session relay 입구가 되며, 별도 attach 없이
framework가 자동으로 연결한다.

```java
ZLinkStreamNodeBuilder stream = options.addStreamNode("gateway");
stream.bind("tcp://0.0.0.0:7201");
stream.registerSession(GameStreamSession.class);
```

`EnableActorDispatch(meshName)`에 대응하는 설정은 session relay가 사용할 기존 MeshName을
명시한다. 별도 transport나 논리 channel을 만들지 않는다. Local managed actor binding은
framework 내부 dispatch를 사용하고, remote actor binding은 같은 MeshNode route를 사용한다.

### 5. Client Connector

Client 측 STREAM connector는 Spring server session과 별도 모듈이다. Java와 Kotlin의 정확한
client connector 선언, lifecycle과 coroutine·`Flow` 투영은
[Stream Connector 계약](../../../stream-connector/languages/java/03-stream-connector.ko.md)이 소유한다.
이 server package 계약에서는 client connector 타입을 재선언하지 않는다.

## Actor session

### 1. Actor session 방향

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

### 2. Actor session 등록

```java
@Configuration
@EnableZLinkFramework
public class ActorConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions framework) {
        framework.addLocationStore(redisLocationStore());
        ZLinkMeshNodeBuilder node = framework.addRouteMesh("game")
            .listen("tcp://0.0.0.0:9001")
            .setRoutingId(RoutingId.from("game-1"));
        node.channelName("game.stage");
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
stream node가 해당 MeshNode의 session relay에 연결되어야 한다. Java framework는 이
의미를 별도 channel transport로 대신 구현하지 않는다.

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
    CompletionStage<ActorRef> create(String meshName, String actorId, String actorType);
    CompletionStage<ActorRef> create(
        String meshName,
        String actorId,
        String actorType,
        ZLinkMessage createRequest);
    CompletionStage<Optional<ActorRef>> find(String meshName, String actorId);
    CompletionStage<ActorRef> getOrCreate(
        String meshName,
        String actorId,
        String actorType);
    CompletionStage<ActorRef> getOrCreate(
        String meshName,
        String actorId,
        String actorType,
        ZLinkMessage createRequest);
}
```

`actorType`은 application이 정하는 짧은 문자열 키다. 같은 actor id를 다른
actorType으로 다시 쓰면 설정 또는 런타임 오류로 실패해야 한다.
manager 는 actor 객체를 직접 반환하지 않고 `ActorRef`를 반환한다. Spot 밖
public handler는 이 ref로 actor join/admission을 직접 수행하지 않는다. Actor 초기 상태는 create request와
Actor factory에서 설정하고, Entry Spot의 control callback에는 immutable membership snapshot만 전달한다.

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

`ZLinkActorContext`, `ZLinkActorJoinCall`, `ZLinkActorJoinResult`와 `ZLinkBoundSession`의 정확한
선언은 [Java interface catalog §3](02-handler-interfaces.ko.md#3-handler)가 단독으로 소유한다.
Join call은 `submit(...)`과 `yield(...)`를 제공한다. Framework는
보호 중인 actor/Spot 상태의 직렬성을 유지하면서 join에 필요한 독립 실행을 진행한다.

`joinSpot(...)`은 actor가 Entry Spot 이후 실제 user Spot으로 들어가는 요청이다. 호출은
`CompletionStage`로 완료되며 framework는 MeshNode actor join 결과를
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

actor가 참여한 Spot 상태가 필요하면 Actor context의 membership snapshot을 확인한다. Spot 소유 상태를
바꿔야 하면 `SpotHandle`을 대상으로 명시적인 send/request를 제출한다.

session actor의 `notifyDisconnected()`는 backend actor binding을 해제한 뒤,
그 binding이 actor context의 현재 bound session과 일치할 때만 disconnected lifecycle을
실행한다. 오래된 session binding에서 disconnect 알림이 늦게 도착해도 현재 bound
session과 disconnected lifecycle callback을 건드리지 않는다.
`relay(payload)`는 session이 받은 actor packet을 bound actor route로
전달한다. `payload`는 framework `ZLinkMessage`이며, session은 이 값을 decode 하거나
relay API에 그대로 넘긴다.

### 6. Handler

Actor payload handler는 Actor queue에서 실행하며 mutable Actor 하나만 소유한다. Entry Spot과 user Spot은
membership control callback을 제공하지만 Actor payload handler에 mutable Spot instance를 전달하지 않는다.

```java
public interface ZLinkEntrySpotActorRequestHandler<
    TActor extends ZLinkActor,
    TRequest,
    TReply> {
    CompletionStage<TReply> handle(
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request);
}

public interface ZLinkSpotActorSendHandler<
    TActor extends ZLinkActor,
    TMessage> {
    CompletionStage<Void> handle(
        TActor actor,
        ZLinkSpotActorSendContext context,
        TMessage message);
}
```

### 7. Runtime 규칙

- actor 생성은 `ZLinkActorManager`를 통해 명시적으로 수행한다.
- actor packet handler는 Actor context의 registry에 등록한다.
- session callback에서 받은 payload는 framework `ZLinkMessage`다. session은 이 값을
  decode 하거나 relay API에 그대로 넘긴다.
- client close는 session binding cleanup만 수행한다. actor disconnect callback이
  필요하면 application이 `notifyDisconnected()`를 호출한다.
- remote actor로 relay할 때 Java framework는 backend stream의 bound actor send를
  사용한다.
- Node direct, ChannelName과 Spot route는 같은 MeshNode ROUTER를 사용한다. Session actor relay는
  명시한 MeshName을 사용하며 별도 연결망을 만들지 않는다.

### 8. Kotlin 사용 표면

Kotlin은 Java contract에 coroutine extension을 추가한다.

```kotlin
val actor = actorManager.getOrCreate("game", "player-42", "player").await()
session.context.actors.bind(actor).await()

actor.context.boundSession.send(PlayerJoined(...)).submit()
```

Kotlin DSL은 등록 코드를 짧게 만들 뿐이고, actor lifecycle 의미는 Java contract와
같다.

## Monitoring

### 1. Monitoring 방향

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
| mesh | `addMeshNodeEvents(...)` | MeshName |

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

- socket/MeshNode monitoring source 이름이 runtime source name과 맞지 않으면
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
| 로그 모드 | `ZLinkMessageFlowLogMode` { `OFF`, `ERRORS_ONLY`(기본), `KEY_TRANSITIONS`, `VERBOSE` } |
| outcome | `ZLinkMessageFlowEvent.outcome()`의 닫힌 문자열 `succeeded`, `failed`, `backpressured`, `dropped`, `cancelled`, `shutdown` |
| flow·dispatch event | `ZLinkMessageFlowEvent` — 공통 §4 field를 같은 이름으로 투영. dispatch error는 `outcome=failed`와 `reason`·`action`을 함께 제공 |
| observer | `ZLinkMessageFlowObserver.onMessageFlow(ZLinkMessageFlowEvent)` → `CompletionStage<Void>` |
| runtime error sink | `ZLinkRuntimeErrorSink.onRuntimeError(ZLinkRuntimeErrorEvent)` → `CompletionStage<Void>` |
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
- runtime error sink는 `setRuntimeErrorSink(MySink.class)` 또는 인스턴스/람다로 등록한다.
  Observer 실패는 `observer_failed`/`message_flow_observer` event로 sink에 전달되며 exception object는 노출하지 않는다.
- `OFF`는 기본 로그 출력만 끄며 명시적으로 등록한 observer·runtime error sink event를 끄지 않는다.

#### 7.3 런타임 토글

`ZLinkMessageFlowControl`은 Spring `ZLinkFrameworkLifecycle` 빈이 구현하므로 주입받아 재시작 없이
모드를 바꾼다. 공유 live cell을 모든 surface가 읽어 즉시 반영된다.

```java
flowControl.setMessageFlowMode(ZLinkMessageFlowLogMode.KEY_TRANSITIONS);  // off→on
```

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
| event 필드(추가) | `Optional<String> ZLinkMessageFlowEvent.flowId()`, `Optional<String> flowOrigin()` — 둘은 함께 있거나 함께 없음 |

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
이 절은 drain 등록, 명시적 제어와 상태 관측의 Java 사용법을 설명한다.

> **설계 원칙(복잡도 하향): 공통 케이스는 무설정.** framework가 Spring `SmartLifecycle`로 graceful
> shutdown에 자동 참여해 drain한다. 앱은 코드를 쓰지 않는다.

#### 10.1 표면

```java
public enum ZLinkFlowOrigin { INBOUND, TIMER, APPLICATION, LIFECYCLE }
public enum ZLinkMeshNodeDrainPolicy { DRAIN_NATURAL, RELEASE_AND_RECREATE }
public enum ZLinkDrainForceReason {
    DEADLINE_EXCEEDED, DRAINING_STATE_PUBLISH_FAILED, OWNER_CLEANUP_FAILED, TEARDOWN_FAILED
}
```

MeshName별 drain 제어와 terminal result의 정확한 interface는
[Java handler interface §4.2](02-handler-interfaces.ko.md#42-runtime-monitoring)의
`ZLinkRouteMeshRuntime`과 `ZLinkMeshDrainResult`가 소유한다.

| 공통 개념 | Java |
|-----------|------|
| 자동 drain(기본) | framework `SmartLifecycle` 빈이 shutdown에서 drain — 앱 코드 0 |
| MeshNode drain 정책 | MeshNode 등록의 `useDrainPolicy(ZLinkMeshNodeDrainPolicy.{DRAIN_NATURAL(기본)/RELEASE_AND_RECREATE})` |
| terminal result | sealed `ZLinkMeshDrainResult` permits `ZLinkMeshDrained`, `ZLinkMeshForceStopped(ZLinkDrainForceReason reason)`; reason은 `DEADLINE_EXCEEDED`, `DRAINING_STATE_PUBLISH_FAILED`, `OWNER_CLEANUP_FAILED`, `TEARDOWN_FAILED` |
| 명시 제어(선택) | `ZLinkRouteMeshRuntime.drain(meshName, deadline)`과 `awaitDrained(meshName)`이 같은 `CompletionStage<ZLinkMeshDrainResult>`를 반환한다. deadline을 생략한 자동 drain은 30초를 사용한다. |
| readiness probe | starter가 `ZLinkDrainReadinessContributor`를 자동 등록해 Actuator readiness group에 반영(무설정). 또는 `ZLinkRouteMeshRuntime.isReady(meshName)`을 직접 조회한다. |
| 상태 관측 | `ZLinkRouteMeshRuntime.observe(meshName, capacity)`가 `zlink.runtime.mesh_node.drain_changed` identifier와 `ZLinkMeshNodeState`를 가진 `ZLinkMeshRuntimeEvent`를 게시한다. |

```java
options.addRouteMesh("orders-mesh")
    .channelName("orders")
    .useDrainPolicy(ZLinkMeshNodeDrainPolicy.RELEASE_AND_RECREATE);
```

- 비동기 반환에 `Async` 접미사를 쓰지 않는 이 코드베이스 관례에 맞춰 `drain`으로 둔다.
- Spring `SmartLifecycle`은 shutdown에서 등록된 MeshNode를 각각 drain한다. 애플리케이션이 특정
  MeshName의 drain을 시작하거나 기다릴 때만 `ZLinkRouteMeshRuntime`을 사용한다.
- drain 상태 관측은 별도 source나 event handler를 등록하지 않고 mesh별 `observe(...)` publisher를
  사용한다. Kotlin도 같은 runtime과 result를 사용하고 `CompletionStage.await()`로 기다린다(§8).

## Stage wrapper

### 1. 기본 생각

playhouse `Stage` 같은 상위 객체는 `SPOT` 자체를 대체하는 것이 아니라, `SPOT`을 사용하는
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
