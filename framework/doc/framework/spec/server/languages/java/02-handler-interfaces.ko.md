<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: Spring Boot Channel Messaging](01-system-structure.ko.md) | [다음: Spring Boot SPOT](01-system-structure.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](README.ko.md)

[Java 묶음](../../../../java/README.ko.md) | [channel](01-system-structure.ko.md) | [SPOT](01-system-structure.ko.md) | [Actor/session](01-system-structure.ko.md) | [STREAM](01-system-structure.ko.md) | [Monitoring](01-system-structure.ko.md) | [Registry](01-system-structure.ko.md)

# ZLink Framework Java Interface Catalog

본문 선언은 구현이 도달해야 하는 정식 목표 계약이다. 현재 배포 public surface와 다른
항목은 §8.1에 symbol 단위로 기록하며, gap 표의 존재가 본문 계약을 비규범으로 바꾸지
않는다.

## 0. 공통 정책 반영

이 문서는 [Framework Adapter 정책](../../../../common/README.ko.md)과
[bindings 공개 계약](../../../../../../../bindings/doc/spec/README.md)의
규칙을 그대로 따른다. 따라서 `Java` 문서에서는 아래를 기본으로 본다.

- 메서드는 `camelCase`, 클래스와 annotation은 `PascalCase`를 쓴다.
- 개념 이름은 공통 정책과 맞춘다. 예를 들어 `send`, `request`, `publish`,
  `sendTo`, `requestTo`, `sendChannel`, `requestChannel` 같은 action 이름을
  유지한다.
- blocking과 non-blocking을 별도 동사 이름으로 나누지 않는다.
- Java application handler와 lifecycle callback은 `CompletionStage<T>` 또는
  `CompletionStage<Void>`로 완료를 알린다. one-way send/push call은 호출자가 송신
  완료를 기다리는 객체를 반환하지 않는다.
- Kotlin `suspend fun`에 Java와 같은 ZLink annotation을 붙인 handler도 같은 계약으로
  본다. Spring bean scanner는 Kotlin suspend method를 별도 수동 등록 없이 발견해야
  하며, framework가 소유하는 coroutine adapter를 통해 실행해야 한다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다.

### 0.1 Handler 실행 executor

`ZLinkFrameworkOptions`는 handler 실행 executor를 설정할 수 있다.

정확한 두 member는 §4의 `ZLinkFrameworkOptions` 전체 선언에 포함한다.

기본값은 Java virtual thread per task executor다. framework의 receive loop와 backend
dispatch boundary는 native 또는 backend receive boundary를 담당하고, channel handler,
Spot lifecycle/packet/timer handler, stream session lifecycle/dispatch handler는 handler
executor 뒤에서 실행된다. 이 분리는 native wait 지점과 application handler 실행 지점이
같은 thread 정책에 묶이지 않게 하기 위한 것이다.

`useHandlerExecutor`로 application이 소유한 executor를 넘기면 framework는 그 executor를
종료하지 않는다. 기본 virtual thread executor 또는 `useVirtualThreadHandlers()`로 만든
executor는 framework host가 닫힐 때 함께 닫힌다.

Kotlin `suspend` handler의 coroutine dispatcher와 scope ownership은 Kotlin adapter가
담당한다. Java handler executor는 Kotlin coroutine dispatcher의 대체물이 아니며,
Kotlin adapter는 thread를 blocking하지 않고 suspend handler의 완료나 예외를 Java
`CompletionStage`로 전달한다.

Kotlin adapter는 Java core의 generic suspend invoker 주입 지점을 사용한다. Kotlin
application은 Kotlin extension으로 dispatcher 또는 scope를 설정한다.

```kotlin
options.useCoroutineHandlers(dispatcher)
options.useCoroutineHandlers(scope, dispatcher)
```

dispatcher만 넘기면 Kotlin adapter가 `CoroutineScope`를 만들고 framework handler
completion을 그 scope에서 실행한다. 외부 `CoroutineScope`를 넘기면 scope ownership은
application에 남고, framework는 해당 scope를 닫지 않는다. 두 경우 모두 Java core는
Kotlin handler의 결과를 Java sync handler 호출의 반환값 또는 예외로 관찰한다.

### 0.2 Spot Actor Join / Transfer 계약

Spot Actor Join / Transfer 관련 interface도 이 문서에 기록된 정식 계약이며,
그 동작 의미는 [공통 스펙](../../23-spot-actor.ko.md)을 따른다. 구현이나 regression test가
이 시그니처와 다르면 계약 불일치로 처리한다.

## 1. 인터페이스 전체 목록

| 분류 | 인터페이스 또는 타입 | 역할 |
|------|----------------------|------|
| context | `ZLinkHandlerContext` | 모든 handler context의 공통 기반 |
| handler | `ZLinkRequestHandler<TReq, TRep>` | request-response handler |
| handler | `ZLinkSendHandler<TMsg>` | one-way send handler |
| handler | `ZLinkPublishHandler<TMsg>` | pub/sub publish handler |
| handler | `ZLinkRouteSendHandler<TMsg>` | routed channel one-way handler |
| handler | `ZLinkRouteRequestHandler<TReq, TRep>` | routed channel request-response handler |
| handler | `ZLinkSession` | stream session lifecycle + framework header dispatch |
| context | `ZLinkSessionContext` | stream session identity, client 응답, actor binding |
| context | `ZLinkSessionClient` | session에서 client stream으로 send/reply |
| context | `ZLinkSessionActors` | session에서 actor handle bind와 lookup 수행 |
| value | `ZLinkSessionActor` | session이 들고 있는 actor dispatch handle |
| handler | `ZLinkTypedSessionPacketHandler<TContext, TMessage>` | message type으로 등록하는 typed stream handler |
| dispatcher | `ZLinkSessionPacketDispatcher<TContext>` | framework envelope을 typed handler로 dispatch하는 raw 경계 |
| handler | `ZLinkActor` | actor runtime 안에서 생성되는 application actor |
| factory | `ZLinkActorFactory` | actor type별 actor 생성 |
| management | `ZLinkActorManager` | actor id/type 기준 생성, 조회, 재사용 |
| context | `ZLinkActorContext` | actor 상태, Spot join, bound session 호출 표면 |
| client | `ZLinkBoundSession` | 현재 actor에서 현재 client session으로 보내는 표면 |
| stream | `ZLinkStreamConnector` | client 측 STREAM connector |
| value | `ZLinkStreamSessionError`, `ZLinkStreamError` | stream error kind + detail |
| handler | `ZLinkRuntimeEventHandler<TEvent>` | runtime monitoring event handler |
| options | `ZLinkMonitoringOptions` | runtime monitoring source 등록 |
| value | `ZLinkSocketEventKind`, `ZLinkSocketEvent` | socket runtime event |
| value | `ZLinkSpotEvent` sealed hierarchy | spot runtime event |
| serializer | `ZLinkMessageSerializer` | payload codec 추상화 |
| options | `ZLinkCodecRegistryBuilder` | JSON/MessagePack/Protobuf codec 등록 |
| options | `ZLinkDispatchOptions` | unhandled policy와 diagnostics |
| client | `ZLinkClient` | channel messaging outbound client |
| client | `ZLinkSpotOutbound` | `SPOT` outbound client |
| client | `ZLinkFanoutClient` | pub/sub fanout publisher |
| client | `ZLinkRouteClient` | route mesh channel target 호출 |
| client | `ZLinkSpotPublisherClient` | 외부 노드용 `SPOT` publish client |
| handler | `ZLinkActorTransferAdapter<TActor>` | remote actor transfer에서 actor state를 선택적으로 `ZLinkMessage`로 전달하고 target actor를 materialize |
| host | Spring host lifetime | Spring Boot `SmartLifecycle` 안에서 framework runtime을 시작하고 종료한다 |
| management | `ZLinkSpotManager` | Spot type 기준 생성, `spotRid` 기준 조회/삭제 |
| timer | `ZLinkTimer` | `SPOT` lifecycle timer handle |
| filter | `ZLinkHandlerFilter` | handler 전후 공통 처리 |
| marker | `ZLinkRequest<TReply>` | request/reply 타입 연결 marker |

## 2. Context

Java framework는 Spring Boot host lifetime 안에서 시작하고 종료한다. channel, Spot,
actor, session actor binding은 Spring bean으로 주입받는다. runtime을 직접 만들거나
시작하는 방법은 public contract로 노출하지 않는다.

```java
@Configuration
@EnableZLinkFramework
public class ZLinkApplicationConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions options) {
        options.addClientServerChannel("api")
            .enableClient();
    }
}
```

```java
public interface ZLinkHandlerContext {
    Optional<String> channelName();
    Optional<String> packetName();
    Optional<String> contentType();
}
```

handler context는 Spring `ApplicationContext`를 노출하지 않는다. handler가 service를
필요로 하면 constructor injection으로 받는다. context를 service locator로 만들면
framework와 application 코드의 책임 경계가 흐려진다.

파생 context는 아래처럼 나눈다.

- `ZLinkRequestContext`
- `ZLinkSendContext`
- `ZLinkPublishContext` (`topic`, `source` 추가)
- `ZLinkRouteSendContext`
- `ZLinkRouteRequestContext`
- `ZLinkSpotActorSendContext`
- `ZLinkSpotActorRequestContext`

`ZLinkPublishContext`는 공통 필드에 더해 publish 고유 정보를 노출한다.

```java
public interface ZLinkPublishContext extends ZLinkHandlerContext {
    String topic();
    Optional<String> source();
}
```

## 3. Handler

아래 channel handler는 `systems.zlink.framework.channels` package가 소유한다. route,
Spot과 Spot actor handler는 각각 자신의 package와 역할별 이름을 사용하므로, 같은
단순 이름을 여러 package에 정의하지 않는다.

```java
public interface ZLinkRequestHandler<TRequest, TReply> {
    CompletionStage<TReply> handle(
        TRequest request,
        ZLinkRequestContext context);
}

public interface ZLinkSendHandler<TMessage> {
    CompletionStage<Void> handle(
        TMessage message,
        ZLinkSendContext context);
}

public interface ZLinkPublishHandler<TMessage> {
    CompletionStage<Void> handle(
        TMessage message,
        ZLinkPublishContext context);
}

public interface ZLinkRouteSendHandler<TMessage> {
    CompletionStage<Void> handle(
        TMessage message,
        ZLinkRouteSendContext context);
}

public interface ZLinkRouteRequestHandler<TRequest, TReply> {
    CompletionStage<TReply> handle(
        TRequest request,
        ZLinkRouteRequestContext context);
}
```

Kotlin adapter는 `suspend` handler를 위 Java `CompletionStage` handler interface로
변환한다. adapter는 thread를 blocking하지 않고 coroutine completion을
`CompletionStage`로 전달한다. Kotlin code 안에서는 `CompletionStage.await()`로
framework request 계열 API의 완료를 기다릴 수 있다.

annotation method handler dispatch도 같은 규칙을 따른다. Kotlin `suspend fun` handler는
scanner가 발견한 Java method handler catalog에 그대로 등록되고, invocation 시점에는
framework가 소유하는 coroutine adapter가 suspend function을 실행한다. fallback reflection
continuation은 Kotlin runtime provider가 없는 환경에서만 사용한다. Kotlin provider가
classpath에 있으면 handler 안의 `coroutineContext`는 framework가 만든 coroutine
context를 가진다.

Kotlin annotation handler는 Java annotation handler와 같은 discovery, validation,
dispatch 의미를 가진다. 예를 들어 Kotlin Spring bean에 `@ZLinkRequest`,
`@ZLinkSend`, `@ZLinkPublish`, `@ZLinkSpotActorRequest`, `@ZLinkSpotActorSend`,
timer annotation을 붙인 `suspend fun`은 Java method handler처럼 scanner catalog에
등록되어야 한다. SPOT actor lifecycle callback은 annotation handler가 아니라
Spot/Entry Spot member callback으로 작성한다. Kotlin
compiler가 suspend method에 추가하는 continuation parameter는 public handler
parameter로 노출하지 않는다. scanner와 adapter는 application이 작성한 request,
message, actor, context parameter만 계약으로 보아야 한다.

Kotlin suspend annotation handler는 아래 원칙을 지킨다.

- handler 실행은 framework가 소유한 `CoroutineScope`에서 시작한다.
- suspend function의 completion, exception, cancellation은 Java core의 내부
  completion 상태로 모인다.
- channel, Spot, actor, session dispatch ordering은 Java core의 serial execution
  queue를 따른다. Kotlin adapter가 별도 queue를 만들거나 callback 순서를 새로
  정의하지 않는다.
- Java handler와 Kotlin suspend handler가 같은 channel, packet, Spot actor mapping을
  등록하면 기존 duplicate registration validation으로 거부한다.
- Spring DI는 Java bean handler와 같은 방식으로 constructor injection을 사용한다.
  Kotlin handler가 `ApplicationContext`를 service locator로 직접 받도록 요구하지
  않는다.

SPOT actor packet handler는 annotation method와 interface 구현체를 모두 지원한다.
두 방식은 서로 다른 registry를 만들지 않고 같은 packet 이름과 actor mapping으로
정규화된다. 따라서 annotation handler와 interface handler가 같은 actor packet을
등록하면 startup validation에서 중복으로 거부된다. actor join admission,
post-join, left lifecycle은 handler registry에 등록하지 않고 Spot member callback으로
처리한다. 이 규칙은 Java와 Kotlin sample이 서로 다른 스타일을 보여 주더라도 runtime
의미가 갈라지지 않도록 하기 위한 것이다.

```java
public interface ZLinkEntrySpotActorRequestHandler<
    TEntrySpot extends ZLinkEntrySpot<?>,
    TActor extends ZLinkActor,
    TRequest,
    TReply> {
    CompletionStage<TReply> handle(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request);
}

public interface ZLinkSpotActorRequestHandler<
    TSpot extends ZLinkSpot<?>,
    TActor extends ZLinkActor,
    TRequest,
    TReply> {
    CompletionStage<TReply> handle(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request);
}

public interface ZLinkActorTransferAdapter<TActor extends ZLinkActor> {
    CompletionStage<ZLinkMessage> transferOut(TActor actor);

    CompletionStage<TActor> transferIn(
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state);
}
```

`ZLinkActorTransferAdapter<TActor>` 등록은 remote transfer에서 직접 옮길 actor state가 있음을 나타낸다.
등록이 없으면 framework는 빈 `ZLinkMessage`를 보내고, target에서 기존 actor factory 또는 public actor
생성 경로로 `TActor` instance를 만든다. 등록된 adapter는 target actor를 만들 때 framework가 준비한
`ZLinkActorContext`를 함께 받는다. 이 context는 actor 생성에만 사용하며 admission, routing, membership,
location 갱신은 framework가 계속 담당한다.

`ZLinkSpotActorSendHandler`, `ZLinkEntrySpotActorSendHandler`도 같은 패턴을 따른다.
Entry Spot actor request/send는 Entry Spot 전용 interface를 사용하고, user Spot actor
request/send는 Spot handler interface를 사용한다. actor join admission, post-join,
left, disconnected lifecycle은 위 member callback 표면만 사용한다.

stream은 header session 하나로 설명한다. callback으로 전달된 payload는 framework가
codec registry와 함께 감싼 `ZLinkMessage`다. session은 필요한 packet만 decode하고,
actor relay처럼 decode를 미룰 수 있는 경계에는 그대로 넘긴다.

```java
public interface ZLinkSession {
    ZLinkSessionContext context();

    CompletionStage<Void> onConnected();

    CompletionStage<Void> onDisconnected();

    CompletionStage<Void> onError(ZLinkStreamError error);

    default CompletionStage<Void> onDispatch(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload) {
        return CompletableFuture.completedFuture(null);
    }
}

public interface ZLinkSessionPacketDispatcher<TSessionContext extends ZLinkSessionContext> {
    CompletionStage<Boolean> tryHandle(
        TSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload);
}

public interface ZLinkSessionContext {
    String sessionId();

    Optional<RoutingId> routingId();

    Optional<String> localAddr();

    Optional<String> remoteAddr();

    ZLinkSessionClient client();

    ZLinkSessionActors actors();

    CompletionStage<Void> close();
}

public interface ZLinkSessionClient {
    <TMessage> ZLinkSessionSendCall send(TMessage message);

    <TMessage> ZLinkSessionReplyCall reply(TMessage message);
}

public interface ZLinkSessionActors {
    List<ZLinkSessionActor> bound();

    CompletionStage<ZLinkSessionActor> bind(ZLinkActor actor);

    CompletionStage<ZLinkSessionActor> bind(ActorRef actor);

    CompletionStage<ZLinkSessionActor> bindOrGet(ActorRef actor);

    Optional<ZLinkSessionActor> find(String actorId);
}

public enum ZLinkStreamSessionError {
    INTERNAL,
    TRANSPORT_ERROR,
    HANDSHAKE_FAILED
}

public record ZLinkStreamDiagnostic(
    int nativeCode,
    @Nullable String message) {
}

public record ZLinkStreamError(
    ZLinkStreamSessionError error,
    @Nullable ZLinkStreamDiagnostic diagnostic) {
}

public interface ZLinkSessionSendCall {
    ZLinkSessionSendCall metadata(String key, String value);
    ZLinkSessionSendCall compress();
    void submit();
}

public interface ZLinkSessionReplyCall {
    ZLinkSessionReplyCall metadata(String key, String value);
    ZLinkSessionReplyCall compress();
    void submit();
}

public interface ZLinkSessionActor {
    String actorId();
    ActorRef ref();
    CompletionStage<Void> relay(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload);
    CompletionStage<Void> notifyDisconnected();
}

public interface ZLinkActor {
    String actorId();
    ZLinkActorContext context();
    default void configure() {
    }
}

public interface ZLinkActorFactory {
    CompletionStage<ZLinkActor> create(
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

public interface ZLinkBoundSessionSendCall {
    ZLinkBoundSessionSendCall metadata(String key, String value);
    void submit();
}
```

`submit(...)` 은 send 계열 메시지를 맡기는 terminator다. 완료 객체를 반환하지 않으며,
송신 수락과 backpressure 처리는 framework 내부 책임이다. 응답을 기다리는 call도
완료 terminator 하나만 제공하며 실행 줄 관리는 framework가 맡는다.
`spotRid()`의 `Optional`이 join 상태의 단일 기준이다. join 결과는 sealed interface로
승인과 거절만 허용하므로 승인 결과에는 항상 actor ref가 존재한다.

`ZLinkSessionClient.reply(...)`는 현재 session dispatch가 request packet을 처리하는
동안에만 사용할 수 있다. framework runtime은 inbound header의 request sequence를
보관했다가 response 전송에 다시 넣는다. request sequence가 없는 send packet에서
`reply(...).submit()`을 호출하면 전송 전에 `IllegalStateException`을 던진다. one-way
reply가 비동기 완료 객체를 반환하지 않더라도 잘못된 request sequence를 조용히
전송하지 않아야 client request/reply correlation이 packet 이름만으로 섞이지 않는다.

`onError(...)`는 application handler 내부 예외를 받는 callback이 아니다.
이 문서에서는 monitor에서 관찰 가능한 session-correlatable transport 오류만
`ZLinkStreamError`로 다시 올리는 용도로 제한한다.

## 4. Client 와 Options

```java
public interface ZLinkSpotNodeBuilder {
    ZLinkSpotNodeBuilder setRoutingId(RoutingId routingId);
    ZLinkSpotNodeBuilder enableRouter(String endpoint);
    ZLinkSpotNodeBuilder connectRouter(String endpoint);

    ZLinkSpotNodeBuilder enablePubSub(String endpoint);
    ZLinkSpotNodeBuilder connectPeerPub(String endpoint);

    ZLinkEndpointConnections routerConnections();
    ZLinkEndpointConnections pubSubConnections();
    ZLinkEndpointConnections channelClientConnections();
    ZLinkEndpointConnections publisherConnections();

    ZLinkEntrySpotOptions configureEntrySpot();
    ZLinkSpotNodeBuilder addSpotFactory(Class<? extends ZLinkSpot> spotType);
    ZLinkSpotNodeBuilder addEntrySpot(Class<? extends ZLinkEntrySpot> entrySpotType);
}

public interface ZLinkEntrySpotOptions {
    RoutingId routingId();
    void setRoutingId(RoutingId routingId);
}

public interface ZLinkStreamNodeBuilder {
    ZLinkStreamNodeBuilder bind(String endpoint);
    ZLinkStreamNodeBuilder setTlsServer(String certificatePath, String keyPath);
    ZLinkStreamNodeBuilder setTlsServer(
        String certificatePath,
        String keyPath,
        boolean requireClientCertificate);
    ZLinkStreamNodeBuilder registerSession(Class<? extends ZLinkSession> sessionType);
    ZLinkStreamNodeBuilder addSessionPacketHandler(
        Class<? extends ZLinkTypedSessionPacketHandler<?, ?>> handlerType);
}

public interface ClientServerChannelBuilder {
    ClientServerChannelBuilder enableServer(String endpoint);
    ClientServerChannelBuilder setRoutingId(RoutingId routingId);
    ClientServerChannelBuilder enableClient();
    ClientServerChannelBuilder enableClient(String endpoint);
    ZLinkEndpointConnections clientConnections();
    ClientServerChannelBuilder setDefaultRequestTimeout(Duration timeout);
    ClientServerChannelBuilder addHandlerGroup(String groupName);
    <THandler extends ZLinkSendHandler<TMessage>, TMessage> void addSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType);
    <THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>
    void addRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType);
}

public interface FanoutChannelBuilder {
    FanoutChannelBuilder enablePublisher(String endpoint);
    FanoutChannelBuilder enableSubscriber();
    FanoutChannelBuilder enableSubscriber(String endpoint);
    ZLinkEndpointConnections subscriberConnections();
    FanoutChannelBuilder addHandlerGroup(String groupName);
    <THandler extends ZLinkPublishHandler<TMessage>, TMessage> void addPublishHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType);
}

public interface RouteMeshChannelBuilder {
    RouteMeshChannelBuilder enableServer(String endpoint);
    RouteMeshChannelBuilder setRoutingId(RoutingId routingId);
    RouteMeshChannelBuilder enableClient();
    RouteMeshChannelBuilder enableClient(String endpoint);
    ZLinkEndpointConnections clientConnections();
    RouteMeshChannelBuilder setDefaultRequestTimeout(Duration timeout);
    RouteMeshChannelBuilder addHandlerGroup(String groupName);
    <THandler extends ZLinkRouteSendHandler<TMessage>, TMessage> void addSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType);
    <THandler extends ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply>
    void addRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType);
}

public interface ZLinkFrameworkOptions {
    void useVirtualThreadHandlers();
    void useHandlerExecutor(Executor executor);
    Duration defaultRequestTimeout();
    void setDefaultRequestTimeout(Duration timeout);
    ZLinkCodecRegistryBuilder codecs();
    void addHandlersFromPackageOf(Class<?> markerType);
    ZLinkMetadataPolicyBuilder configureMetadata();
    void useInMemoryLocationStores();
    void addLocationStore(ZLinkLocationStore store);
    ClientServerChannelBuilder addClientServerChannel(String channelName);
    FanoutChannelBuilder addFanoutChannel(String channelName);
    RouteMeshChannelBuilder addRouteMeshChannel(String channelName);
    ZLinkSpotNodeBuilder addSpotMesh(String channelName);
    ZLinkStreamNodeBuilder addStreamNode(String streamNodeName);
    void addActorFactory(
        String actorType,
        Class<? extends ZLinkActorFactory> factoryType);
    void addActorTransferAdapter(
        String actorType,
        Class<? extends ZLinkActorTransferAdapter<?>> adapterType);
    void useFilter(Class<? extends ZLinkHandlerFilter> filterType);
    ZLinkDispatchOptions configureDispatch();
    ZLinkWorkerOptions configureWorkers();
}

public interface ZLinkCodecRegistryBuilder {
    void use(ZLinkCodecExtension extension);
}

public interface ZLinkMetadataPolicyBuilder {
    void addForwardedMetadataKey(String key);
}

public enum ZLinkSpotKind {
    INVALID,
    ENTRY,
    USER
}

public interface ZLinkDispatchOptions {
    ZLinkUnhandledDispatchOptions unhandled();
    ZLinkDiagnosticsOptions diagnostics();
    ZLinkDispatchOptions setMessageFlowObserver(
        Class<? extends ZLinkMessageFlowObserver> observerType);
    ZLinkDispatchOptions setMessageFlowObserver(
        ZLinkMessageFlowObserver observer);
}

public interface ZLinkUnhandledDispatchOptions {
    void setRequest(ZLinkUnhandledDispatchAction action);
    void setSend(ZLinkUnhandledDispatchAction action);
    void setPublish(ZLinkUnhandledDispatchAction action);
    void setSendLogLevel(LogLevel level);
    void setPublishLogLevel(LogLevel level);
}

public enum ZLinkUnhandledDispatchAction {
    REPLY_ERROR,
    LOG_AND_DROP,
    DROP,
    THROW
}

public interface ZLinkDiagnosticsOptions {
    void setMessageFlow(ZLinkMessageFlowLogMode mode);
    void setSampleRate(double sampleRate);
    void setIncludeMessageSizes(boolean enabled);
    void setIncludeNativeDiagnostics(boolean enabled);
}

public enum ZLinkMessageFlowLogMode {
    OFF,
    ERRORS_ONLY,
    KEY_TRANSITIONS,
    VERBOSE,
    DIAGNOSTIC
}

public interface ZLinkClient {
    <TMessage> ZLinkSendCall sendToChannel(
        String channelName,
        TMessage message);

    <TMessage> ZLinkRequestCall requestToChannel(
        String channelName,
        TMessage request);
}

public interface ZLinkSendCall {
    void submit();
}

public interface ZLinkRequestCall {
    ZLinkRequestCall timeout(Duration timeout);
    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);
}

public interface ZLinkSpotOutbound {
    <TMessage> ZLinkSendCall sendToSpot(
        SpotHandle target,
        TMessage message);

    <TMessage> ZLinkRequestCall requestToSpot(
        SpotHandle target,
        TMessage request);

    <TEvent> ZLinkPublishCall publish(
        String topic,
        TEvent message);

    <TMessage> ZLinkSendCall sendToChannel(
        String channelName,
        TMessage message);

    <TMessage> ZLinkRequestCall requestToChannel(
        String channelName,
        TMessage request);
}

// actor packet/lifecycle 등록은 하나의 actor handler registry가 소유한다.
// actor-type 바인딩은 handler가 구현한 generic 인터페이스
// (`ZLinkSpotActorRequestHandler<TSpot, TActor, ...>` 등)에서 가져오므로 별도 actorType
// 인자를 받지 않는다.
public interface ZLinkSpotHandlerRegistry {
    void addHandler(Class<?> handlerType);
}

public interface ZLinkSpotContext {
    RoutingId spotRid();
    RoutingId nodeRid();
    ZLinkSpotOutbound outbound();
    ZLinkSpotHandlerRegistry handlers();
    <T> ZLinkWorkerCall<T> runWorker(ZLinkWorkerTask<T> work);
    CompletionStage<Void> leaveActor(ZLinkActor actor);
    CompletionStage<Boolean> close();
    CompletionStage<ZLinkTimer> addTimer(
        String name,
        Duration period,
        Class<?> handlerType,
        @Nullable ZLinkTimerOptions options);
}

public interface ZLinkEntrySpotContext {
    RoutingId spotRid();
    RoutingId nodeRid();
    ZLinkSpotOutbound outbound();
    ZLinkSpotHandlerRegistry handlers();
    <T> ZLinkWorkerCall<T> runWorker(ZLinkWorkerTask<T> work);
    CompletionStage<Void> destroyActor(ZLinkActor actor);
    CompletionStage<ZLinkTimer> addTimer(
        String name,
        Duration period,
        Class<?> handlerType,
        @Nullable ZLinkTimerOptions options);
}

public interface ZLinkWorkerCall<T> {
    ZLinkWorkerCall<T> timeout(Duration timeout);
    CompletionStage<T> submit();
}

public interface ZLinkRouteClient {
    ZLinkSendCall sendToNode(
        String channelName,
        RoutingId target,
        Object message);

    ZLinkRequestCall requestToNode(
        String channelName,
        RoutingId target,
        Object message);

    // spot 대상은 handle 하나만 받는다. handle이 owner node와 전송 mesh를 소유하므로
    // caller가 route channel을 함께 고르지 않는다(공통 스펙 24 §3).
    ZLinkSendCall sendToSpot(
        SpotHandle spot,
        Object message);

    ZLinkRequestCall requestToSpot(
        SpotHandle spot,
        Object message);
}

public interface ZLinkSpotPublisherClient {
    ZLinkPublishCall publish(
        String channelName,
        String topic,
        Object message);
}

public interface ZLinkFanoutClient {
    <TEvent> ZLinkPublishCall publish(
        String channelName,
        String topic,
        TEvent message);
}

public interface ZLinkPublishCall {
    void submit();
}

public record ZLinkSpotCreateResult(
    RoutingId spotRid,
    ZLinkSpotCreateState state,
    ZLinkMessage reply) {
}

public enum ZLinkSpotCreateState {
    EXISTING,
    CREATED,
    REJECTED
}

public record ZLinkSpotInfo(
    RoutingId spotRid) {
}

public interface ZLinkSpotManager {
    CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot> spotType);

    CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid);

    CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot> spotType,
        ZLinkMessage request);

    CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid);

    CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid,
        ZLinkMessage request);

    CompletionStage<Optional<ZLinkSpotInfo>> find(RoutingId spotRid);
    CompletionStage<List<ZLinkSpotInfo>> list();
    CompletionStage<Boolean> close(RoutingId spotRid);
}

public interface ZLinkSpotActorLifecycle<TActor extends ZLinkActor> {
    CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
        String actorId,
        ZLinkMessage request);
    CompletionStage<Void> onJoinedActor(TActor actor);
    CompletionStage<Void> onLeaveActor(TActor actor);
    CompletionStage<Void> onDisconnectActor(TActor actor);
}

public record ZLinkSpotActorJoinResponse(
    boolean accepted,
    ZLinkMessage reply) {
}

public interface ZLinkSpot<TActor extends ZLinkActor>
    extends ZLinkSpotActorLifecycle<TActor> {
    ZLinkSpotContext context();

    default void configure() {
    }

    default CompletionStage<ZLinkSpotCreateResponse> onCreate(ZLinkMessage request) {
        return CompletableFuture.completedFuture(ZLinkSpotCreateResponse.accept());
    }

    default CompletionStage<Void> onInitialize() {
        return CompletableFuture.completedFuture(null);
    }

    default CompletionStage<Void> onClosing() {
        return CompletableFuture.completedFuture(null);
    }
}

public interface ZLinkEntrySpot<TActor extends ZLinkActor>
    extends ZLinkSpotActorLifecycle<TActor> {
    ZLinkEntrySpotContext context();

    default void configure() {
    }

    default CompletionStage<Void> onInitialize() {
        return CompletableFuture.completedFuture(null);
    }

    default CompletionStage<Void> onClosing() {
        return CompletableFuture.completedFuture(null);
    }

    default CompletionStage<Void> onCreateActor(
        TActor actor,
        ZLinkMessage createRequest) {
        return CompletableFuture.completedFuture(null);
    }
}

public interface ZLinkEndpointConnections {
    void connect(String endpoint);
    void disconnect(String endpoint);
    List<String> listConnections();
}

public interface ZLinkTimer extends AutoCloseable {
    boolean isDisposed();
    CompletionStage<Void> cancel();
}

public record ZLinkTimerOptions(
    ZLinkTimerOverrunPolicy overrunPolicy,
    int maxCatchUpTicks,
    boolean stopOnUnhandledException) {
}

public enum ZLinkTimerOverrunPolicy {
    SKIP_LATE_TICKS,
    CATCH_UP_BOUNDED,
    DELAY_NEXT_TICK
}

public record ZLinkTimerTick(
    String name,
    long deliveryIndex,
    long scheduledIndex,
    Duration period,
    Instant scheduledAt,
    Instant startedAt,
    Duration scheduledElapsed,
    Duration startedElapsed,
    Duration delay,
    long skippedTicks) {
}
```

같은 stream node가 평문 endpoint와 TLS endpoint를 함께 열어야 할 때는 `bind(...)`를
반복 호출한다. 이 경우 하나의 session relay 상태를 공유하므로 같은 session gateway의
평문 연결과 TLS 연결이 서로 다른 server role처럼 분리되지 않는다.

SPOT handler가 다른 client/server channel로 `sendToChannel` 또는
`requestToChannel`을 보내려면 `addClientServerChannel(...).enableClient(...)`로
그 channel의 client 역할을 켠다. SPOT node builder는 별도 channel client socket을
부착하지 않는다.

`destroyActor(actor)`는 Entry Spot context 전용 API이다. user Spot context에는
actor destroy API가 없다. actor가 user Spot에 있으면 먼저 `leaveActor(...)` 또는
actor context의 Entry Spot join 흐름으로 Entry Spot에 돌아와야 한다. destroy가 성공하는
호출은 lifecycle callback을 호출하지 않고 native actor ref와 framework registry를 정리한다.
같은 actor instance에 대한 중복 destroy는 성공으로 끝난다.

등록된 Entry Spot은 framework startup에서 native Entry Spot 위에 activation으로
생성된다. 생성된 activation은 `configure()`를 먼저 호출한 뒤 `onInitialize()`를
실행하고, framework shutdown에서는 `onClosing()`를 호출한 뒤 timer와 native
Entry Spot을 정리한다. actor join lifecycle과 entry actor packet dispatch는 별도 runtime
경로로 검증해야 한다. Entry Spot actor packet dispatch는 actor별 mailbox를 사용한다.

client 측 STREAM connector는 server session과 별도 모듈로 둔다.

```java
public interface ZLinkStreamConnector {
    boolean isConnected();
    ZLinkStreamConnectionState state();
    ZLinkStreamConnectorOptions options();
    int pendingDispatchCount();

    ZLinkStreamLifecycleCall connect();
    ZLinkStreamLifecycleCall disconnect();
    ZLinkStreamLifecycleCall reconnect();
    ZLinkStreamLifecycleCall close();
    ZLinkStreamLifecycleCall dispatch();

    ZLinkStreamSendCall send(ZLinkStreamEncodedPayload payload);
    ZLinkStreamRequestCall request(ZLinkStreamEncodedPayload payload);
    ZLinkTypedStreamSendCall send(Object payload);
    ZLinkTypedStreamRequestCall request(Object payload);
    ZLinkStreamWaitCall waitFor(String name);
    ZLinkStreamWaitCall waitFor(Class<?> payloadType);

    AutoCloseable on(
        String name,
        ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload> handler);
    AutoCloseable onDisconnected(ZLinkStreamDisconnectedHandler handler);
    AutoCloseable onConnectionStateChanged(ZLinkStreamConnectionStateHandler handler);
}
```

## 4.1 runtime monitoring

runtime monitoring은 socket의 하부 monitor와 registry/spot의 snapshot
diff를 함께 감싸는 운영 표면이다.

```java
public interface ZLinkMonitoringOptions {
    void addSocketEvents(String sourceName, ZLinkSocketEventKind... events);
    void addSpotEvents(String sourceName, Duration interval);
    void addLocationRuntimeEvents(String sourceName, Duration interval);
    void addLocationPeerEvents(String sourceName);
    void addLocationSpotEvents(String sourceName);
    void addLocationActorEvents(String sourceName);
    void addLocationRouteEvents(String sourceName);
}

public interface ZLinkRuntimeEvent {
    String sourceName();
    Instant timestamp();
}

public interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
    CompletionStage<Void> handle(TEvent event);
}

public enum ZLinkSocketEventKind {
    CONNECTED,
    CONNECTION_READY,
    DISCONNECTED,
    HANDSHAKE_FAILED,
    PEER_ADMISSION_CHANGED,
    CLOSED,
    INTERNAL
}

public record ZLinkSocketDiagnostic(
    ZLinkSocketNativeEventType nativeEvent,
    long nativeValue) {
}

public record ZLinkSocketEvent(
    String sourceName,
    Instant timestamp,
    ZLinkSocketEventKind event,
    @Nullable RoutingId routingId,
    String localAddr,
    String remoteAddr,
    @Nullable ZLinkSocketDiagnostic diagnostic) implements ZLinkRuntimeEvent {
}

public sealed interface ZLinkLocationRuntimeEvent extends ZLinkRuntimeEvent
    permits ZLinkLocationStatusChanged, ZLinkLocationTopologyChanged,
            ZLinkLocationServiceSummaryChanged, ZLinkLocationStoreFailure,
            ZLinkLocationStoreRecovered {
}
public record ZLinkLocationStatusChanged(String sourceName, Instant timestamp,
    ZLinkLocationRuntimeStatus status) implements ZLinkLocationRuntimeEvent {}
public record ZLinkLocationTopologyChanged(String sourceName, Instant timestamp,
    List<ZLinkLocationTopologyEntry> topology) implements ZLinkLocationRuntimeEvent {}
public record ZLinkLocationServiceSummaryChanged(String sourceName, Instant timestamp,
    List<ZLinkLocationServiceSummary> serviceSummary) implements ZLinkLocationRuntimeEvent {}
public record ZLinkLocationStoreFailure(String sourceName, Instant timestamp)
    implements ZLinkLocationRuntimeEvent {}
public record ZLinkLocationStoreRecovered(String sourceName, Instant timestamp)
    implements ZLinkLocationRuntimeEvent {}

public sealed interface ZLinkSpotEvent extends ZLinkRuntimeEvent
    permits ZLinkSpotStatusChanged, ZLinkSpotPeersChanged,
            ZLinkSpotSubjectsChanged, ZLinkSpotTimerHandlerFailed,
            ZLinkSpotTimerStoppedAfterUnhandledException {
}
public record ZLinkSpotStatusChanged(String sourceName, Instant timestamp,
    ZLinkSpotNodeStatus status) implements ZLinkSpotEvent {}
public record ZLinkSpotPeersChanged(String sourceName, Instant timestamp,
    List<ZLinkSpotNodePeerEntry> peers) implements ZLinkSpotEvent {}
public record ZLinkSpotSubjectsChanged(String sourceName, Instant timestamp,
    List<ZLinkSpotNodeSubjectEntry> subjects) implements ZLinkSpotEvent {}
public record ZLinkSpotTimerHandlerFailed(String sourceName, Instant timestamp,
    ZLinkSpotTimerDiagnostic diagnostic) implements ZLinkSpotEvent {}
public record ZLinkSpotTimerStoppedAfterUnhandledException(
    String sourceName, Instant timestamp, ZLinkSpotTimerDiagnostic diagnostic)
    implements ZLinkSpotEvent {}
```

각 permitted record는 event 종류에 필요한 payload만 필수 constructor 인자로 가진다.
예를 들어 `ZLinkLocationStatusChanged`는 status를, `ZLinkSpotPeersChanged`는 peer 목록을,
두 timer failure record는 `ZLinkSpotTimerDiagnostic`을 가진다. kind와 여러 nullable
payload를 독립 필드로 두지 않는다.

위 registration 타입들이 `system-structure.ko.md`,
`system-structure.ko.md`와 `system-structure.ko.md`에서 쓰는 기준 표면이다.

수동 연결은 `channel` 전체가 아니라 `channel + capability` 또는
`spot node + capability` 기준으로 관리해야 한다. 예를 들면 `profile.client`와
`profile.subscriber`는 서로 다른 연결 집합이고, `stage-node.router`와
`stage-node.pubsub`도 서로 다른 집합이다.

일반 channel client manual 연결은 endpoint만 받는 편이 맞다. 하부 `DEALER(client)`
가 connect된 peer 집합으로 요청을 보내므로, startup과 런타임 제어 모두 endpoint
집합만 다루면 된다. `SPOT` router manual 연결도 같은 방식으로 endpoint 집합만
등록하고, 이 문서에서는 `connect(...)` 호출 시 remote router id를 따로 받지
않는다.

`ZLinkSpotManager`는 등록된 Spot type으로 factory를 고르고, 생성 결과와 조회
표면에서 `spotRid`를 다시 볼 수 있게 한다. 같은 `SpotNode` 안에서 이미 등록된
Spot type을 다시 등록하면 조용히 덮어쓰지 않고 예외를 던지는 편을 기본으로 본다.

send/publish는 one-way submit이다. 송신 수락과 backpressure 처리는 framework
내부 책임이며, 호출자가 완료값을 기다리는 흐름으로 설명하지 않는다.
공통 의미는 [framework 공통 정책](../../../04-async-execution-policy.ko.md)을 따른다.
request도 request packet을 보내는 단계에서는 같은 async submit 경로를 사용하고,
reply 대기는 request timeout이 따로 정한다.
호출별 `timeout(...)`이 가장 먼저 적용되고, 그 다음 채널별
`setDefaultRequestTimeout(...)`, 마지막으로 framework 전역
`setDefaultRequestTimeout(...)` 값이 적용된다. 전역 기본값은 30초다.

typed packet key는 registration 시 descriptor에서 확정한다. payload 타입의
`@ZLinkPacket` 값을 사용하고, annotation이 없으면 `SimpleName`을 사용한다. typed
call options는 packet name을 다시 받지 않는다. handler annotation의 packet name은
raw/untyped extension 등록에만 사용한다.

## 5. Annotation

```java
@Target(ElementType.TYPE)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkPacket {
    String value();
}

@Target(ElementType.TYPE)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkHandlerGroup {
    String value();
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkRequest {
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSend {
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkPublish {
}
```

`SPOT`, `STREAM`용 annotation도 `.NET` `[ZLinkX]` 이름에 맞춰 `Mapping` 접미사 없이 둔다.

```java
@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSpotRequest {
}

@Target({ElementType.METHOD, ElementType.TYPE})
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSpotSubscription {
    String spotNodeName() default "";
    String topic();
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSpotActorRequest {
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSpotActorSend {
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkStreamPacket {
}
```

SPOT actor lifecycle callback은 actor만 받는다. join admission callback은
framework 공통 `ZLinkMessage` request를 받고 `ZLinkSpotActorJoinResponse`를 반환한다.
accepted가 `true`일 때만 actor 위치가 target Spot으로 commit되고
`onJoinedActor`가 호출된다. accepted가 `false`이면 actor 위치는 바뀌지 않고
post-join callback도 실행되지 않는다. Entry Spot도 같은 admission callback을 갖고,
user Spot에서 Entry Spot으로 돌아오는 명시적 join 요청을 여기서 accept/reject한다.
disconnected callback은 actor만 받는다. session actor의 현재 binding이 끊어졌거나
application이 actor disconnected 알림을 명시적으로 보낼 때 실행되며, actor가 들어오거나
나간 Spot 변경 결과를 만들지 않기 때문이다.

```java
public final class MatchSpot implements ZLinkSpot<MatchActor> {
    @Override
    public CompletionStage<Void> onDisconnectActor(MatchActor actor) {
        // session binding cleanup or domain notification
        return CompletableFuture.completedFuture(null);
    }
}
```

## 6. Filter

`Spring`의 `HandlerInterceptor`와 비슷한 공통 처리 층을 둔다.

```java
public interface ZLinkHandlerFilter {
    <T> CompletionStage<T> invoke(
        ZLinkInvocationContext context,
        ZLinkNext<T> next);
}
```

## 7. 중요한 규칙

- 일반 channel messaging의 request/send dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)` 수신은 reply correlation 경로로 본다.
- 같은 역할은 자동 연결 또는 수동 연결 중 하나만 선택한다.
- 수동 연결은 `channel + capability` 단위로 관리한다.
- 수동 연결 역할은 startup 등록뿐 아니라 런타임 `connect`, `disconnect`,
  `listConnections`도 지원해야 한다.
- `ROUTER -> DEALER` 임의 push는 channel messaging 공용 계약에 넣지 않는다.
- stream session은 header 기반 `ZLinkSession` 하나로 둔다.
- actor/session relay는 SessionRelay와 `ZLinkBoundSession`을 기준으로 구현한다.

### 7.1 message-flow dispatch error event

미등록 메시지와 dispatch 실패 관측은 메시지 흐름 observer의 `outcome=ERROR` event 로 처리한다.
channel 별, spot 별 observer 등록은 이 버전의 공개 계약이 아니다. request 실패는 reply path 가 있으면
error reply 로 끝나고, local actor call 처럼 reply frame 이 없는 경로는 `CompletionStage` 를 framework
error 로 완료한다. one-way 실패는 drop 되지만 기본 로그, counter, message-flow event 를 남긴다.

두 observer overload는 §4의 `ZLinkDispatchOptions` 전체 선언에 포함되어 있다.

`ZLinkMessageFlowEvent` 의 error event 는 `surface`, `messageKind`, `errorReason`, `errorAction`,
`packetName`, `channelName`, `topic`, `spotRid`, `actorId`, `sourceRid`, `correlationId`,
`exception` 을 담는 record 다. native message 소유권이나 frame 참조는 포함하지 않는다.

```java
@Override
public void configure(ZLinkFrameworkOptions options) {
    options.configureDispatch()
        .setMessageFlowObserver(MyMessageFlowObserver.class);
}
```

## 8. 전체 public interface inventory와 구현 차이

이 inventory는 공통 기능을 Spring과 Java 관례로 제공하기 위한 목표 계약이다.
`.NET` interface 이름을 복사하지 않고 `I` prefix를 제거하며, 비동기 완료는
`CompletionStage`로 표현한다. framework `CancellationToken`은 사용하지 않는다.

| 기능 | Java public interface |
|------|-----------------------|
| handler와 context | `ZLinkHandlerContext`, request/send/publish/route handler와 context, `ZLinkHandlerFilter` |
| channel call | `ZLinkClient`, `ZLinkRouteClient`, `ZLinkFanoutClient`, `ZLinkSendCall`, `ZLinkRequestCall`, `ZLinkPublishCall` |
| actor | `ZLinkActor`, `ZLinkActorContext`, `ZLinkActorFactory`, `ZLinkActorManager`, `ZLinkActorDirectory`, `ZLinkActorClient`, actor send/request/join call interface |
| Spot | `ZLinkSpot`, `ZLinkEntrySpot`, `ZLinkSpotActorLifecycle`, `ZLinkActorTransferAdapter`, handler registry, outbound, common/Spot/Entry Spot context |
| Spot handler와 관리 | Spot/Entry Spot actor send/request handler, packet/request/subscription/timer handler, `ZLinkSpotManager`, publisher client와 resolver |
| stream과 session | `ZLinkSession`, typed session packet handler, session context/client/actors/actor, bound session과 send/reply call |
| location | 통합 location store, peer/Spot/actor/route/owner lease store, watch/change-stamp, readiness, runtime query와 resolver |
| codec | serializer, codec extension/registrar/registry builder, stream compression builder |
| configuration | framework options, channel/route mesh/fanout/stream/Spot builder, runtime options와 socket/route config |
| dispatch와 monitoring | dispatch/unhandled/diagnostics options, message-flow observer/control, monitoring options와 typed runtime event handler |
| timer와 worker | `ZLinkTimer`, `ZLinkWorkerCall<T>`, `ZLinkWorkerOptions` |

기존 절에서 전체 member를 고정하지 않았던 application interface의 목표 시그니처는
다음과 같다. 이 선언도 위 본문 선언과 같은 정식 계약이다.

```java
public interface ZLinkActorClient {
    ZLinkActorSendCall sendToActor(ActorRef actorRef, Object message);
    ZLinkActorRequestCall requestToActor(ActorRef actorRef, Object request);
}

public interface ZLinkActorSendCall {
    void submit();
}

public interface ZLinkActorRequestCall {
    ZLinkActorRequestCall timeout(Duration timeout);
    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);
}

public interface ZLinkActorDirectory {
    CompletionStage<Optional<ActorRef>> find(String actorId);
    CompletionStage<ActorRef> ensure(
        String actorId,
        ZLinkMessage createRequest,
        ZLinkActorPlacement placement);
    CompletionStage<ActorRef> ensure(String actorId, ZLinkMessage createRequest);
    CompletionStage<ActorRef> ensure(String actorId, Object createRequest);
}

```

Java call 객체에는 framework `CancellationToken` overload를 두지 않는다. one-way
`submit()`은 완료 객체를 반환하지 않고, reply가 있는 request와 join만
`CompletionStage`를 반환한다.

```java
public interface ManualEndpointListBuilder {
    void connect(String endpoint);
}

public interface ZLinkChannelRuntimeOptions {
    ZLinkClientServerChannelRuntimeOptions clientServerChannel(String channelName);
    ZLinkRouteMeshChannelRuntimeOptions routeMeshChannel(String channelName);
}

public interface ZLinkClientServerChannelRuntimeOptions {
    ZLinkSocketRuntimeOptions configureServerSocket();
}

public interface ZLinkRouteMeshChannelRuntimeOptions {
    ZLinkSocketRuntimeOptions configureServerSocket();
}

public interface ZLinkSocketRuntimeOptions {
    long maxMessageSize();
    void maxMessageSize(long value);
    int weight();
    void weight(int value);
}

public interface ZLinkCodecRegistrar {
    void addSerializer(String contentType, ZLinkMessageSerializer serializer);
    void addSerializer(
        String contentType,
        ZLinkMessageSerializer serializer,
        Predicate<Class<?>> canSerialize);
    void addStreamCodec(String contentType, ZLinkStreamCodec codec);
}

public interface ZLinkStreamCompressionBuilder {
    ZLinkStreamCompressionBuilder useDefault();
    ZLinkStreamCompressionBuilder useLz4();
    ZLinkStreamCompressionBuilder use(ZLinkStreamCompressionCodec codec);
    ZLinkStreamCompressionBuilder disable();
}
```

```java
public interface ZLinkPeerLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updatePeer(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteResult> removePeer(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner);
    CompletionStage<List<ZLinkPeerLocation>> listPeerLocations(
        ZLinkPeerLocationFilter filter);
}

public interface ZLinkSpotLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateSpot(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteResult> removeSpot(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner);
    CompletionStage<ZLinkSpotLocation> resolveSpot(ZLinkSpotLocationKey key);
    CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocations(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page);
}

public interface ZLinkActorLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateActor(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteResult> removeActor(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner);
    CompletionStage<ZLinkActorLocation> resolveActor(ZLinkActorLocationKey key);
    CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocations(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page);
}

public interface ZLinkRouteLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateRoute(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteResult> removeRoute(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner);
    CompletionStage<ZLinkRouteLocation> resolveRoute(ZLinkRouteLocationKey key);
    CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocations(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page);
}

public interface ZLinkOwnerLeaseStore {
    CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLease(
        String ownerId,
        RoutingId nodeRid,
        Duration leaseTtl);
    CompletionStage<Boolean> removeOwnerLease(String ownerId);
    CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeases();
}

public interface ZLinkLocationStore extends
    ZLinkPeerLocationStore,
    ZLinkSpotLocationStore,
    ZLinkActorLocationStore,
    ZLinkRouteLocationStore,
    ZLinkOwnerLeaseStore {
    CompletionStage<Long> removeAllByOwner(String ownerId);
}

public interface ZLinkLocationChangeStampStore {
    CompletionStage<Long> getChangeStamp(ZLinkLocationChangeStampScope scope);
}

public interface ZLinkLocationWatchStore {
    Flow.Publisher<ZLinkLocationChanged> watch(ZLinkLocationWatchFilter filter);
}
```

```java
public interface ZLinkPeerLocationResolver {
    CompletionStage<List<ZLinkPeerLocation>> listLivePeers(
        ZLinkPeerLocationFilter filter);
}

public sealed interface SpotHandle permits FrameworkSpotHandle {
    RoutingId spotRid();
}

public interface SpotHandleResolver {
    CompletionStage<Optional<SpotHandle>> resolveSpotHandle(
        RoutingId spotRid);
}

public interface ActorSpotHandleResolver {
    CompletionStage<Optional<SpotHandle>> resolveActorSpotHandle(String actorId);
}

public interface ZLinkLocationReadiness {
    CompletionStage<Boolean> isPeerReady(
        String meshName,
        ZLinkLocationRole role,
        @Nullable RoutingId nodeRid);
}

public interface ZLinkLocationRuntimeQuery {
    CompletionStage<ZLinkLocationRuntimeStatus> getStatus();
    CompletionStage<List<ZLinkPeerLocation>> listPeerLocations(
        ZLinkPeerLocationFilter filter);
    CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocations(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page);
    CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocations(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page);
    CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocations(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page);
    CompletionStage<ZLinkLocationPage<ZLinkLocationTopologyEntry>> listTopology(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page);
    CompletionStage<List<ZLinkLocationServiceSummary>> listServiceSummaries(
        ZLinkLocationServiceSummaryFilter filter);
}

public sealed interface ZLinkLocationKey
    permits ZLinkLocationKey.Peer,
            ZLinkLocationKey.Spot,
            ZLinkLocationKey.Actor,
            ZLinkLocationKey.Route {
    ZLinkLocationKind kind();
    record Peer(ZLinkPeerLocationKey key) implements ZLinkLocationKey {
        @Override public ZLinkLocationKind kind() { return ZLinkLocationKind.PEER; }
    }
    record Spot(ZLinkSpotLocationKey key) implements ZLinkLocationKey {
        @Override public ZLinkLocationKind kind() { return ZLinkLocationKind.SPOT; }
    }
    record Actor(ZLinkActorLocationKey key) implements ZLinkLocationKey {
        @Override public ZLinkLocationKind kind() { return ZLinkLocationKind.ACTOR; }
    }
    record Route(ZLinkRouteLocationKey key) implements ZLinkLocationKey {
        @Override public ZLinkLocationKind kind() { return ZLinkLocationKind.ROUTE; }
    }
}
```

`FrameworkSpotHandle`은 package-private runtime 구현이며 package root에서 export하지 않는다.

```java
public interface ZLinkSpotPacketHandler<TSpot, TMessage> {
    CompletionStage<Void> handle(TSpot spot, TMessage message);
}

public interface ZLinkSpotRequestHandler<TSpot, TRequest, TReply> {
    CompletionStage<TReply> handle(TSpot spot, TRequest request);
}

public interface ZLinkSpotSubscriptionHandler<TSpot, TEvent> {
    CompletionStage<Void> handle(TSpot spot, TEvent message);
}

public interface ZLinkSpotTimerHandler<TSpot extends ZLinkSpot<?>> {
    CompletionStage<Void> handle(TSpot spot, ZLinkTimerTick tick);
}

public interface ZLinkTypedSessionPacketHandler<
    TSessionContext extends ZLinkSessionContext,
    TMessage> {
    Class<TMessage> messageType();
    CompletionStage<Void> handle(
        TSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        TMessage message);
}
```

application용 session handler는 이 typed interface 하나만 사용한다. packet identity는
`messageType()`의 descriptor에서 registry가 정하며 handler가 같은 이름을 반복하지 않는다.
raw `ZLinkMessage` 경계는 framework의 `ZLinkSessionPacketDispatcher`가 소유한다.

```java
public interface ZLinkStreamCompressionCodec {
    byte[] compress(byte[] payload);
    byte[] decompress(byte[] payload, int maxDecompressedSize);
}

@FunctionalInterface
public interface ZLinkStreamErrorHandler {
    CompletionStage<Void> handle(ZLinkStreamError error);
}

@FunctionalInterface
public interface ZLinkStreamInboundObserver {
    CompletionStage<Void> observe(ZLinkStreamInboundObservation observation);
}

@FunctionalInterface
public interface ZLinkStreamPacketNameResolver {
    String resolve(Class<?> payloadType);
}

public interface ZLinkStreamTypedCodec {
    <T> ZLinkStreamEncodedPayload encode(String packetName, T value);
    <T> T decode(ZLinkStreamEncodedPayload payload, Class<T> type);
}

public interface ZLinkStreamWaitCall {
    ZLinkStreamWaitCall timeout(Duration timeout);
    ZLinkStreamWaitCall where(
        Predicate<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> predicate);
    <TPayload> ZLinkStreamWaitCall where(
        Class<TPayload> payloadType,
        Predicate<ZLinkStreamMessage<TPayload>> predicate);
    CompletionStage<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> submit();
    <TPayload> CompletionStage<ZLinkStreamMessage<TPayload>> submit(
        Class<TPayload> payloadType);
}

public interface ZLinkMessageFlowControl {
    void setMessageFlowMode(ZLinkMessageFlowLogMode mode);
    ZLinkMessageFlowLogMode messageFlowMode();
}

public interface ZLinkMonitoringOptionsCustomizer {
    void customize(ZLinkMonitoringOptions options);
}
```

```java
public interface ZLinkInvocationContext extends ZLinkHandlerContext {
    Optional<Object> request();
}
public interface ZLinkRequestContext extends ZLinkHandlerContext {}
public interface ZLinkSendContext extends ZLinkHandlerContext {}
public interface ZLinkRouteRequestContext extends ZLinkHandlerContext {
    RoutingId routingId();
}
public interface ZLinkRouteSendContext extends ZLinkHandlerContext {
    RoutingId routingId();
}
public interface ZLinkSpotActorRequestContext extends ZLinkHandlerContext {}
public interface ZLinkSpotActorSendContext extends ZLinkHandlerContext {}

@FunctionalInterface
public interface ZLinkNext<T> {
    CompletionStage<T> invoke();
}

public interface ZLinkMessageSerializer {
    <T> ZLinkEncodedPayload serialize(T value);
    <T> T deserialize(ZLinkEncodedPayload payload, Class<T> type);
    default void prepare(Class<?> type) {}
}

@FunctionalInterface
public interface ZLinkCodecExtension {
    void register(ZLinkCodecRegistrar codecs);
}

public interface ZLinkMessageFlowObserver {
    CompletionStage<Void> onMessageFlow(ZLinkMessageFlowEvent flow);
}

@FunctionalInterface
public interface ZLinkFrameworkConfigurer {
    void configure(ZLinkFrameworkOptions framework);
}
```

```java
public interface ZLinkEntrySpotActorSendHandler<
    TEntrySpot extends ZLinkEntrySpot<?>,
    TActor extends ZLinkActor,
    TMessage> {
    CompletionStage<Void> handle(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorSendContext context,
        TMessage message);
}

public interface ZLinkSpotActorSendHandler<
    TSpot extends ZLinkSpot<?>,
    TActor extends ZLinkActor,
    TMessage> {
    CompletionStage<Void> handle(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorSendContext context,
        TMessage message);
}

@FunctionalInterface
public interface ZLinkWorkerTask<T> {
    T run() throws Exception;
}

public interface ZLinkWorkerOptions {
    ZLinkWorkerOptions minThreads(int minThreads);
    ZLinkWorkerOptions maxThreads(int maxThreads);
    ZLinkWorkerOptions idleTimeout(Duration idleTimeout);
    ZLinkWorkerOptions maxQueueLength(int maxQueueLength);
}
```

```java
@FunctionalInterface
public interface ZLinkStreamConnectionStateHandler {
    CompletionStage<Void> handle(ZLinkStreamConnectionState state);
}
@FunctionalInterface
public interface ZLinkStreamDisconnectedHandler {
    CompletionStage<Void> handle(ZLinkStreamDisconnected event);
}
public record ZLinkStreamDisconnected(ZLinkStreamCloseReason closeReason) {}
@FunctionalInterface
public interface ZLinkStreamMessageHandler<TPayload> {
    CompletionStage<Void> handle(ZLinkStreamMessage<TPayload> message);
}

public interface ZLinkStreamLifecycleCall {
    CompletionStage<Void> submit();
}

public interface ZLinkStreamSendCall {
    ZLinkStreamSendCall metadata(String key, String value);
    ZLinkStreamSendCall metadata(Map<String, String> metadata);
    ZLinkStreamSendCall compress();
    void submit();
}

public interface ZLinkStreamRequestCall {
    ZLinkStreamRequestCall metadata(String key, String value);
    ZLinkStreamRequestCall metadata(Map<String, String> metadata);
    ZLinkStreamRequestCall compress();
    ZLinkStreamRequestCall timeout(Duration timeout);
    CompletionStage<ZLinkStreamEncodedPayload> submit();
    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);
}

public interface ZLinkTypedStreamSendCall {
    ZLinkTypedStreamSendCall metadata(String key, String value);
    ZLinkTypedStreamSendCall metadata(Map<String, String> metadata);
    ZLinkTypedStreamSendCall compress();
    void submit();
}

public interface ZLinkTypedStreamRequestCall {
    ZLinkTypedStreamRequestCall metadata(String key, String value);
    ZLinkTypedStreamRequestCall metadata(Map<String, String> metadata);
    ZLinkTypedStreamRequestCall compress();
    ZLinkTypedStreamRequestCall timeout(Duration timeout);
    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);
}
```

encoded/raw payload의 packet identity는 `ZLinkStreamEncodedPayload.packetName()`에
명시한다. typed object call의 identity는 payload type descriptor가 정한다. 두 call
builder 모두 호출별 identity override를 제공하지 않는다.

handler 등록과 stream packet 이름을 고정하는 public annotation도 계약에 포함한다.

```java
@Target(ElementType.TYPE)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSpotTimer {
    String name();
    long periodMillis();
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkStreamRaw {
}

@Target(ElementType.TYPE)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkHandlerGroups {
    ZLinkHandlerGroup[] value();
}

@Target(ElementType.TYPE)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkStreamPacketName {
    String value();
}
```

### 8.1 목표 interface와 현재 구현

| 대상 symbol | 목표 Java 계약 | 현재 public 선언 | 판정과 구현 작업 |
|-------------|----------------|------------------|------------------|
| application handler와 actor factory | `CompletionStage<T>` 또는 `CompletionStage<Void>` | channel, route, Spot, actor, session handler와 `ZLinkActorFactory.create`가 `CompletionStage`를 반환한다. | 충족 — exact 반환형 contract test와 automatic-turn Config 8 전체 실행으로 확인했다. |
| Spot, entry Spot, actor lifecycle과 transfer | lifecycle은 `CompletionStage`, token 인자 없음 | lifecycle과 transfer adapter가 `CompletionStage`를 반환하며 framework cancellation token을 노출하지 않는다. | 충족 — reflection contract와 production symbol no-hit로 확인했다. |
| one-way call의 `submit` | `void` | send, publish, session reply와 bound-session send의 `submit()`은 `void`다. | 충족 — `ZLinkSubmitStage`와 `ZLinkAwait`는 production symbol에 없고 exact return contract가 통과한다. **단 `yield` terminator는 제공해야 하는데 없다** — [구현 차이 §12.21](../../../90-implementation-gap.ko.md) |
| `ZLinkTypedSessionPacketHandler` | typed `CompletionStage<Void> handle(...)` | raw application handler를 상속하지 않는 typed interface와 framework dispatcher로 분리되어 있다. | 충족 — exact hierarchy contract와 fake backend typed dispatch test로 확인했다. |
| actor join | request를 받는 `joinSpot`/`joinEntrySpot`, 단일 `ZLinkActorJoinCall`, sealed 결과 | 요청 없는 overload와 default throw가 없고 `Accepted`/`Rejected` 결과를 사용한다. | 충족 — reflection 및 implementor contract가 통과한다. |
| route-mesh와 manual connection | route-mesh 조회 facade와 공통 `ZLinkEndpointConnections` | `ZLinkChannelRuntimeOptions.routeMeshChannel`과 역할별 builder의 공통 endpoint 계약이 존재한다. | 충족 — exact facade contract가 통과한다. |
| worker 완료와 취소 | worker request는 `CompletionStage`, task는 token 없이 실행 | `ZLinkWorkerCall.submit()`은 `CompletionStage<T>`이고 framework cancellation token은 없다. | 충족 — legacy terminator와 token production symbol no-hit로 확인했다. |
| 비동기 Java 이름 | `CompletionStage` method에 불필요한 `Async` 접미사 없음 | application 공개 계약은 Java 이름을 사용한다. | 충족 — public method contract가 통과한다. transport와 runtime internal method는 application 계약이 아니다. |
| location key와 runtime facet | sealed `ZLinkLocationKey`와 store/query/readiness facet | 네 key record가 sealed key를 구현하며 정식 location facet이 존재한다. | 충족 — exact location contract와 Config 6 SF-A1~E1 전체 실행으로 확인했다. |
| Spot messaging target | `SpotHandleResolver`, `ActorSpotHandleResolver`, `SpotHandle` | opaque handle과 두 resolver를 제공하고 legacy remote ref를 노출하지 않는다. | 충족 — exact symbol contract와 production symbol no-hit로 확인했다. |
| dispatch와 typed packet identity | dispatch 전략은 내부에 두고 typed identity는 type descriptor가 결정 | public dispatch mode와 typed call packet-name override가 없다. | 충족 — public method 및 symbol absence contract가 통과한다. |
| actor membership과 Spot 접근 | `Optional<RoutingId> spotRid()` 하나, generic Spot getter 없음 | `spotRid()`가 단일 membership 값이며 `isJoined`와 `getSpot`은 없다. | 충족 — exact method contract와 production symbol no-hit로 확인했다. |
| Spot context와 manager | handler registry, close, request-bearing create/get-or-create | 정식 member와 overload를 공개한다. | 충족 — exact reflection contract가 통과한다. |
| 관측·운영 public declaration | monitoring, flow, metrics, drain과 location event의 유효 상태를 타입으로 표현 | 정식 public type과 member가 구현되어 있다. | 선언 충족 — `JavaTargetContractGapTest`의 관측·운영 계약이 통과한다. 전체 운영 동작은 완료된 Config만 증거로 사용하며 미실행 Config는 이 표에서 완료로 판정하지 않는다. |
| application export 경계 | backend와 handler runtime SPI를 application package에 노출하지 않음 | runtime SPI는 internal package로 이동했고 application public surface에서 제외됐다. | 충족 — package와 modifier contract가 통과한다. |
| Kotlin suspend invocation 소유권 | Kotlin module provider 한 곳이 소유 | Java core에 Kotlin reflection fallback이 없다. | 충족 — fallback symbol absence contract가 통과한다. |
| 이 절의 exact declaration | 이름, member, 반환형과 generic 제약을 고정 | `JavaTargetContractGapTest`가 target contract 14개 영역을 직접 검증한다. | 충족 — 2026-07-13 전체 test가 통과했다. |

`systems.zlink.framework.runtime.*`와 backend adapter package에 선언된 public interface는
application contract가 아니다. `ZLinkHandlerFactory`, `ZLinkSuspendHandlerInvoker`와
`ZLinkBackend*`, `*BackendAdapter` 타입은 module 내부 extension 경계로 내리거나 외부
application이 import하지 못하게 해야 한다. 이 타입을 위 inventory에 추가해 public
contract로 고정하지 않는다.

관측·운영 public inventory에는 `ZLinkFlowOrigin`, `ZLinkSpotDrainPolicy`,
`ZLinkDrainForceReason`, `ZLinkDrainResult`, `Drained`, `ForceStopped`, `ZLinkDrainControl`,
`ZLinkStreamCloseReason`과 disconnect event type도 포함한다. [Spring Boot Monitoring §8~10](01-system-structure.ko.md)과
[Stream Connector](../../../stream-connector/languages/java/03-stream-connector.ko.md)의 전체 declaration이 정확한 member와 반환형을 고정한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md) | [이전: Spring Boot Channel Messaging](01-system-structure.ko.md) | [다음: Spring Boot SPOT](01-system-structure.ko.md)
<!-- framework-adapter-nav:bottom:end -->
