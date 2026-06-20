<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Java Channel Messaging Samples](../guide/samples/channel-messaging-samples.ko.md) | [다음: ZLink Framework Java SPOT Samples](../guide/samples/spot-samples.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](README.ko.md)

[Java 묶음](../README.ko.md) | [channel](spring-boot-channel-messaging.ko.md) | [channel 샘플](../guide/samples/channel-messaging-samples.ko.md) | [SPOT](spring-boot-spot.ko.md) | [Actor/session](spring-boot-actor-session.ko.md) | [STREAM](spring-boot-stream.ko.md) | [Monitoring](spring-boot-monitoring.ko.md) | [Registry](spring-boot-registry.ko.md)

# ZLink Framework Java Interface Catalog

## 0. 공통 정책 반영

이 문서는 [Framework Adapter 정책](../../common/README.ko.md)과
[doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
규칙을 그대로 따른다. 따라서 `Java` 문서에서는 아래를 기본으로 본다.

- 메서드는 `camelCase`, 클래스와 annotation은 `PascalCase`를 쓴다.
- 개념 이름은 공통 정책과 맞춘다. 예를 들어 `send`, `request`, `publish`,
  `sendTo`, `requestTo`, `sendChannel`, `requestChannel` 같은 action 이름을
  유지한다.
- blocking과 non-blocking을 별도 동사 이름으로 나누지 않는다.
- Java handler는 일반 Java 메서드처럼 값을 반환하거나 `void`로 끝난다.
  `CompletionStage`는 request, send, manager, connector처럼 호출자가 완료를 기다려야
  하는 API에서만 사용한다.
- Kotlin `suspend fun`에 Java와 같은 ZLink annotation을 붙인 handler도 같은 계약으로
  본다. Spring bean scanner는 Kotlin suspend method를 별도 수동 등록 없이 발견해야
  하며, framework가 소유하는 coroutine adapter를 통해 실행해야 한다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다.

### 0.1 Handler 실행 executor

`ZLinkFrameworkOptions`는 handler 실행 executor를 설정할 수 있다.

```java
public interface ZLinkFrameworkOptions {
    void useVirtualThreadHandlers();
    void useHandlerExecutor(Executor executor);
}
```

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
Kotlin adapter는 suspend handler가 끝날 때까지 기다린 뒤 Java handler와 같은 방식으로
값을 반환하거나 예외를 전달한다.

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
| handler | `ZLinkSessionPacketHandler<TContext>` | stream packet 이름별 session handler |
| dispatcher | `ZLinkSessionPacketDispatcher<TContext>` | 등록된 session packet handler 선택 실행 |
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
| value | `ZLinkRegistryEventKind`, `ZLinkRegistryEvent` | registry runtime event |
| value | `ZLinkSpotEventKind`, `ZLinkSpotEvent` | spot runtime event |
| serializer | `ZLinkMessageSerializer` | payload codec 추상화 |
| options | `ZLinkCodecRegistryBuilder` | JSON/MessagePack/Protobuf codec 등록 |
| options | `ZLinkDispatchOptions` | Spot/Stream dispatch mode, unhandled policy, diagnostics |
| client | `ZLinkClient` | channel messaging outbound client |
| client | `ZLinkSpotOutbound` | `SPOT` outbound client |
| client | `ZLinkFanoutClient` | pub/sub fanout publisher |
| client | `ZLinkRouteClient` | route mesh channel target 호출 |
| client | `ZLinkSpotPublisherClient` | 외부 노드용 `SPOT` publish client |
| host | Spring host lifetime | Spring Boot `SmartLifecycle` 안에서 framework runtime을 시작하고 종료한다 |
| management | `ZLinkSpotManager` | Spot type 기준 생성, `spotRid` 기준 조회/삭제 |
| timer | `ZLinkTimer` | `SPOT` lifecycle timer handle |
| filter | `ZLinkHandlerFilter` | handler 전후 공통 처리 |
| marker | `ZLinkRequest<TReply>` | request/reply 타입 연결 marker |
| registry | `ZLinkRegistryQuery`, `ZLinkRegistryQueryClient` | registry 조회 |

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
    CancellationToken cancellationToken();
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

```java
public interface ZLinkRequestHandler<TRequest, TReply> {
    TReply handle(
        TRequest request,
        ZLinkRequestContext context);
}

public interface ZLinkSendHandler<TMessage> {
    void handle(
        TMessage message,
        ZLinkSendContext context);
}

public interface ZLinkPublishHandler<TMessage> {
    void handle(
        TMessage message,
        ZLinkPublishContext context);
}

public interface ZLinkRouteSendHandler<TMessage> {
    void handle(
        TMessage message,
        ZLinkRouteSendContext context);
}

public interface ZLinkRouteRequestHandler<TRequest, TReply> {
    TReply handle(
        TRequest request,
        ZLinkRouteRequestContext context);
}
```

Kotlin adapter는 `suspend` handler를 위 Java handler interface로 변환한다. Java
interface가 sync 표면이므로 adapter는 suspend 블록이 끝날 때까지 기다린 뒤 값을
반환한다. Kotlin code 안에서는 `CompletionStage.await()`로 framework request 계열
API의 완료를 기다릴 수 있다.

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
    TReply handle(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface ZLinkSpotActorRequestHandler<
    TSpot extends ZLinkSpot<?>,
    TActor extends ZLinkActor,
    TRequest,
    TReply> {
    TReply handle(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface ZLinkSpot<TActor extends ZLinkActor> {
    ZLinkSpotActorJoinResponse onActorJoin(
        TActor actor,
        Message request,
        CancellationToken cancellationToken);

    void onJoinedActor(
        TActor actor,
        CancellationToken cancellationToken);

    void onLeaveActor(
        TActor actor,
        CancellationToken cancellationToken);

    void onDisconnectActor(
        TActor actor,
        CancellationToken cancellationToken);
}

public interface ZLinkEntrySpot<TActor extends ZLinkActor> {
    ZLinkSpotActorJoinResponse onActorJoin(
        TActor actor,
        Message request,
        CancellationToken cancellationToken);

    void onJoinedActor(
        TActor actor,
        CancellationToken cancellationToken);

    void onLeaveActor(
        TActor actor,
        CancellationToken cancellationToken);

    void onDisconnectActor(
        TActor actor,
        CancellationToken cancellationToken);
}
```

`ZLinkSpotActorSendHandler`, `ZLinkEntrySpotActorSendHandler`도 같은 패턴을 따른다.
Entry Spot actor request/send는 Entry Spot 전용 interface를 사용하고, user Spot actor
request/send는 Spot handler interface를 사용한다. actor join admission, post-join,
left, disconnected lifecycle은 위 member callback 표면만 사용한다.

stream은 header session 하나로 설명한다. callback으로 전달된 payload는 framework가
빌려준 값이므로 callback 밖에서 보관해야 하면 별도 copy를 만든다.

```java
public interface ZLinkSession {
    ZLinkSessionContext context();

    void onConnected();

    void onDisconnected();

    void onError(ZLinkStreamError error);

    default void onDispatch(
        ZLinkStreamHeader header,
        Message payload) {
    }
}

public interface ZLinkSessionPacketHandler<TSessionContext extends ZLinkSessionContext> {
    String packetName();

    void handle(
        TSessionContext context,
        ZLinkStreamHeader header,
        Message payload);
}

public interface ZLinkSessionPacketDispatcher<TSessionContext extends ZLinkSessionContext> {
    CompletionStage<Boolean> tryHandleAsync(
        TSessionContext context,
        ZLinkStreamHeader header,
        Message payload);
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

    CompletionStage<ZLinkSessionActor> bind(ZLinkActorRef actor);

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
    ZLinkSessionSendCall packetName(String messageName);
    ZLinkSessionSendCall compress();
    CompletionStage<Void> submit();
    void await();
}

public interface ZLinkSessionReplyCall {
    ZLinkSessionReplyCall metadata(String key, String value);
    ZLinkSessionReplyCall compress();
    CompletionStage<Void> submit();
    void await();
}

public interface ZLinkSessionActor {
    String actorId();
    ZLinkActorRef ref();
    CompletionStage<Void> relay(ZLinkStreamHeader header, Message payload);
    CompletionStage<Void> notifyDisconnected();
}

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
    CompletionStage<ZLinkActor> create(String actorId, String actorType);
    CompletionStage<Optional<ZLinkActor>> find(String actorId);
    CompletionStage<ZLinkActor> getOrCreate(String actorId, String actorType);
}

public interface ZLinkActorContext {
    Optional<RoutingId> spotRid();
    boolean isJoined();
    ZLinkBoundSession boundSession();
    ZLinkSpot getSpot();
    <TSpot extends ZLinkSpot<?>> TSpot getSpot(Class<TSpot> spotType);
    ZLinkActorJoinSpotCall joinSpot(RoutingId spotRid, Object request);
    ZLinkActorJoinEntrySpotCall joinEntrySpot(RoutingId spotNodeRid, Object request);
}

public interface ZLinkActorJoinEntrySpotCall {
    ZLinkActorJoinEntrySpotCall timeout(Duration timeout);
    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(
        Class<TReply> replyType);
    <TReply> ZLinkActorJoinResult<TReply> await(Class<TReply> replyType);
}

public interface ZLinkActorJoinSpotCall {
    ZLinkActorJoinSpotCall timeout(Duration timeout);
    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(
        Class<TReply> replyType);
    <TReply> ZLinkActorJoinResult<TReply> await(Class<TReply> replyType);
}

public record ZLinkActorJoinResult<TReply>(
    int resultCode,
    ZLinkActorRef actor,
    TReply reply) {
}

public interface ZLinkBoundSession {
    <TMessage> ZLinkBoundSessionSendCall send(TMessage message);
    CompletionStage<Void> disconnect();
}

public interface ZLinkBoundSessionSendCall {
    ZLinkBoundSessionSendCall packetName(String packetName);
    ZLinkBoundSessionSendCall metadata(String key, String value);
    CompletionStage<Void> submit();
    void await();
}
```

`ZLinkSessionClient.reply(...)`는 현재 session dispatch가 request packet을 처리하는
동안에만 사용할 수 있다. framework runtime은 inbound header의 request sequence를
보관했다가 response 전송에 다시 넣는다. request sequence가 없는 send packet에서
`reply(...)`를 호출하면 실패한 `CompletionStage`를 반환한다. 이 제한이 있어야 client
request/reply correlation이 packet 이름만으로 섞이지 않는다.

`onError(...)`는 application handler 내부 예외를 받는 callback이 아니다.
이 문서에서는 monitor에서 관찰 가능한 session-correlatable transport 오류만
`ZLinkStreamError`로 다시 올리는 용도로 제한한다.

## 4. Client 와 Options

```java
public interface ZLinkSpotNodeBuilder {
    ZLinkSpotNodeBuilder enableRouter(String endpoint);
    ZLinkSpotNodeBuilder connectRouter(String endpoint);
    ZLinkSpotNodeBuilder setRouterRoutingId(RoutingId routingId);

    ZLinkSpotNodeBuilder enablePubSub(String endpoint);
    ZLinkSpotNodeBuilder connectPeerPub(String endpoint);
    ZLinkSpotNodeBuilder connectPubSub(String endpoint); // compatibility alias
    ZLinkSpotNodeBuilder setPubSubRoutingId(RoutingId routingId);

    ZLinkSpotNodeBuilder attachChannelClient(String channelName);
    ZLinkSpotNodeBuilder attachChannelClient(String channelName, String endpoint);
    ZLinkSpotNodeBuilder attachSpotPublisherClient(String channelName);
    ZLinkSpotNodeBuilder attachSpotPublisherClient(String channelName, String endpoint);
    ZLinkSpotNodeBuilder acceptSpotRoutesFromChannel(String channelName);
    ZLinkSpotNodeBuilder acceptSpotRoutesFromChannel(String channelName, String endpoint);

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
    ZLinkStreamNodeBuilder attachActorGateway(String spotNodeName);
    ZLinkStreamNodeBuilder registerSession(Class<? extends ZLinkSession> sessionType);
    ZLinkStreamNodeBuilder addSessionPacketHandler(
        Class<? extends ZLinkSessionPacketHandler<?>> handlerType);
}

public interface ClientServerChannelBuilder {
    ClientServerChannelBuilder enableServer(String endpoint);
    ClientServerChannelBuilder enableClient();
    ClientServerChannelBuilder enableClient(String endpoint);
    ClientServerChannelBuilder addHandlerGroup(String groupName);
    <THandler extends ZLinkSendHandler<TMessage>, TMessage> void addSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType,
        @Nullable String packetName);
    <THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>
    void addRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType,
        @Nullable String packetName);
    ClientServerChannelBuilder enableSpotRouteEgress(String targetSpotNodeChannelName);
}

public interface FanoutChannelBuilder {
    FanoutChannelBuilder enablePublisher(String endpoint);
    FanoutChannelBuilder enableSubscriber();
    FanoutChannelBuilder enableSubscriber(String endpoint);
    FanoutChannelBuilder addHandlerGroup(String groupName);
    <THandler extends ZLinkPublishHandler<TMessage>, TMessage> void addPublishHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType,
        @Nullable String packetName);
    FanoutChannelBuilder addPublishHandler(Class<?> handlerType, @Nullable String packetName);
}

public interface DealerMeshChannelBuilder {
    DealerMeshChannelBuilder enableClient();
    DealerMeshChannelBuilder enableClient(String endpoint);
    DealerMeshChannelBuilder addHandlerGroup(String groupName);
}

public interface RouteMeshChannelBuilder {
    RouteMeshChannelBuilder enableServer(String endpoint);
    ZLinkRouteConfigBuilder configureRouting();
    RouteMeshChannelBuilder enableClient();
    RouteMeshChannelBuilder enableClient(String endpoint);
    RouteMeshChannelBuilder addHandlerGroup(String groupName);
    <THandler extends ZLinkRouteSendHandler<TMessage>, TMessage> void addSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType,
        @Nullable String packetName);
    <THandler extends ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply>
    void addRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType,
        @Nullable String packetName);
    RouteMeshChannelBuilder enableSpotRouteEgress(String targetSpotNodeChannelName);
}

public interface ZLinkRouteConfigBuilder {
    void setRoutingId(RoutingId routingId);
}

public interface ZLinkFrameworkOptions {
    Duration defaultTimeout();
    void setDefaultTimeout(Duration timeout);
    ZLinkCodecRegistryBuilder codecs();
    void addHandlersFromPackageOf(Class<?> markerType);
    ZLinkMetadataPolicyBuilder configureMetadata();
    ZLinkDiscoveryBuilder useDiscovery();
    ClientServerChannelBuilder addClientServerChannel(String channelName);
    FanoutChannelBuilder addFanoutChannel(String channelName);
    DealerMeshChannelBuilder addDealerMeshChannel(String channelName);
    RouteMeshChannelBuilder addRouteMeshChannel(String channelName);
    ZLinkSpotMeshBuilder addSpotMesh(String channelName);
    ZLinkStreamNodeBuilder addStreamNode(String streamNodeName);
    void addActorFactory(
        String actorType,
        Class<? extends ZLinkActorFactory> factoryType);
    void addSpotRemoteAddressResolver(
        Class<? extends ZLinkSpotRemoteAddressResolver> resolverType);
    ZLinkRegistrySpotRemoteAddressesOptions useRegistrySpotRemoteAddresses(String namespaceName);
    void useFilter(Class<? extends ZLinkHandlerFilter> filterType);
    ZLinkDispatchOptions configureDispatch();
    ZLinkWorkerOptions configureWorkers();
}

public interface ZLinkSpotMeshBuilder {
    ZLinkDiscoveryBuilder useDiscovery();
    ZLinkSpotNodeBuilder addNode(String spotNodeName);
}

public interface ZLinkDiscoveryBuilder {
    void addRegistryEndpoint(String endpoint);
}

public interface ZLinkCodecRegistryBuilder {
    void use(ZLinkCodecExtension extension);
    void addJson();
    void addSerializer(String contentType, ZLinkMessageSerializer serializer);
}

public interface ZLinkMetadataPolicyBuilder {
    void addForwardedMetadataKey(String key);
}

public interface ZLinkRegistrySpotRemoteAddressesOptions {
    void setRouterChannelId(String routerChannelId);
}

public interface ZLinkSpotRemoteAddressResolver {
    CompletionStage<ZLinkSpotRemoteAddress> resolveSpotRemoteAddressAsync(
        RoutingId spotRid);
}

public record ZLinkSpotRemoteAddress(
    String routerChannelId,
    RoutingId targetNodeRid,
    RoutingId spotRid,
    ZLinkSpotKind spotKind) {
}

public enum ZLinkSpotKind {
    INVALID,
    ENTRY,
    USER
}

public interface ZLinkDispatchOptions {
    ZLinkDispatchMode spotDispatchMode();
    void setSpotDispatchMode(ZLinkDispatchMode mode);
    ZLinkDispatchMode streamDispatchMode();
    void setStreamDispatchMode(ZLinkDispatchMode mode);
    ZLinkUnhandledDispatchOptions unhandled();
    ZLinkDiagnosticsOptions diagnostics();
}

public enum ZLinkDispatchMode {
    COMPILED,
    DYNAMIC
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
    ZLinkSendCall packetName(String messageName);
    CompletionStage<Void> submit();
    void await();
}

public interface ZLinkRequestCall {
    ZLinkRequestCall packetName(String messageName);
    ZLinkRequestCall timeout(Duration timeout);
    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);
    <TReply> TReply await(Class<TReply> replyType);
}

public interface ZLinkSpotOutbound {
    <TMessage> ZLinkSendCall sendToSpot(
        RoutingId spotRid,
        TMessage message);

    <TMessage> ZLinkRequestCall requestToSpot(
        RoutingId spotRid,
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

// 코드 기준: .NET `IZLinkActorHandlerRegistry`는 actor packet/lifecycle 등록을
// actor-type 바인딩은 handler가 구현한 generic 인터페이스
// (`ZLinkSpotActorRequestHandler<TSpot, TActor, ...>` 등)에서 가져오므로 별도 actorType
// 인자를 받지 않는다.
public interface ZLinkSpotHandlerRegistry {
    void addPacket(Class<?> handlerType);
    void addSubscribe(String topic, Class<?> handlerType);
    void addHandler(Class<?> handlerType);
    void addActorPacket(Class<?> handlerType);
    void addActorSend(Class<?> handlerType);
    void addActorRequest(Class<?> handlerType);
}

public interface ZLinkSpotContext {
    RoutingId spotRid();
    RoutingId nodeRid();
    ZLinkSpotOutbound outbound();
    CompletionStage<Void> leaveActor(ZLinkActor actor);
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
    CompletionStage<Void> destroyActor(ZLinkActor actor);
    CompletionStage<ZLinkTimer> addTimer(
        String name,
        Duration period,
        Class<?> handlerType,
        @Nullable ZLinkTimerOptions options);
}

public interface ZLinkRouteClient {
    <TMessage> ZLinkSendCall sendTo(
        String routerChannelId,
        RoutingId targetNodeRid,
        TMessage message);

    <TMessage> ZLinkRequestCall requestTo(
        String routerChannelId,
        RoutingId targetNodeRid,
        TMessage request);
}

public interface ZLinkSpotPublisherClient {
    <TEvent> ZLinkPublishCall publishSpot(
        String channelName,
        String topic,
        TEvent message);
}

public interface ZLinkFanoutClient {
    <TEvent> ZLinkPublishCall publish(
        String channelName,
        String topic,
        TEvent message);
}

public interface ZLinkPublishCall {
    ZLinkPublishCall packetName(String messageName);
    CompletionStage<Void> submit();
    void await();
}

public record ZLinkSpotCreateResult(
    RoutingId spotRid,
    ZLinkSpotCreateState state,
    Message reply) {
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

    CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid);

    CompletionStage<Optional<ZLinkSpotInfo>> find(RoutingId spotRid);
    CompletionStage<List<ZLinkSpotInfo>> list();
    CompletionStage<Boolean> close(RoutingId spotRid);
}

public interface ZLinkSpot<TActor extends ZLinkActor> {
    ZLinkSpotContext context();

    default void configure() {
    }

    default ZLinkSpotCreateResponse onCreate(Message request) {
        return ZLinkSpotCreateResponse.accept();
    }

    default void onInitialize() {
    }

    default void onClosing() {
    }

    default ZLinkSpotActorJoinResponse onActorJoin(
        TActor actor,
        Message request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.reject();
    }

    default void onJoinedActor(
        TActor actor,
        CancellationToken cancellationToken) {
    }

    default void onLeaveActor(
        TActor actor,
        CancellationToken cancellationToken) {
    }
}

public interface ZLinkEntrySpot<TActor extends ZLinkActor> {
    ZLinkEntrySpotContext context();

    default void configure() {
    }

    default void onInitialize() {
    }

    default void onClosing() {
    }

    default void onJoinedActor(
        TActor actor,
        CancellationToken cancellationToken) {
    }

    default void onLeaveActor(
        TActor actor,
        CancellationToken cancellationToken) {
    }
}

public interface ChannelClientConnections {
    void connect(String endpoint);
    void disconnect(String endpoint);
    List<String> listConnections();
}

public interface ChannelSubscriberConnections {
    void connect(String endpoint);
    void disconnect(String endpoint);
    List<String> listConnections();
}

public interface SpotRouterConnections {
    void connect(String endpoint);
    void disconnect(String endpoint);
    List<String> listConnections();
}

public interface SpotPubSubConnections {
    void connect(String endpoint);
    void disconnect(String endpoint);
    List<String> listConnections();
}

public interface SpotChannelClientConnections {
    void connect(String endpoint);
    void disconnect(String endpoint);
    List<String> listConnections();
}

public interface SpotPublisherClientConnections {
    void connect(String endpoint);
    void disconnect(String endpoint);
    List<String> listConnections();
}

public interface ZLinkTimer extends AutoCloseable {
    boolean isDisposed();
    CompletionStage<Void> cancelAsync();
}

public final class ZLinkTimerOptions {
    ZLinkTimerOverrunPolicy overrunPolicy();
    int maxCatchUpTicks();
    boolean stopOnUnhandledException();
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

`destroyActor(actor)`는 Entry Spot context 전용 API이다. user Spot context에는
actor destroy API가 없다. actor가 user Spot에 있으면 먼저 `leaveActor(...)` 또는
actor context의 Entry Spot join 흐름으로 Entry Spot에 돌아와야 한다. destroy가 성공하는
호출은 lifecycle callback을 호출하지 않고 native actor ref와 framework registry를 정리한다.
같은 actor instance에 대한 중복 destroy는 성공으로 끝난다.

등록된 Entry Spot은 framework startup에서 native Entry Spot 위에 activation으로
생성된다. 생성된 activation은 `configure()`를 먼저 호출한 뒤 `onInitialize()`를
실행하고, framework shutdown에서는 `onClosing()`를 호출한 뒤 timer와 native
Entry Spot을 정리한다. actor join과 entry actor packet dispatch는 별도 runtime 경로로
검증해야 한다.

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
    void addRegistryEvents(String sourceName, Duration interval);
    void addSpotEvents(String sourceName, Duration interval);
}

public interface ZLinkRuntimeEvent {
    String sourceName();
    Instant timestamp();
}

public interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
    void handle(TEvent event);
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

public enum ZLinkRegistryEventKind {
    STATUS_CHANGED,
    TOPOLOGY_CHANGED,
    SERVICE_SUMMARY_CHANGED
}

public record ZLinkRegistryEvent(
    String sourceName,
    Instant timestamp,
    ZLinkRegistryEventKind event,
    @Nullable ZLinkRegistryStatus status,
    @Nullable List<ZLinkRegistryTopologyEntry> topology,
    @Nullable List<ZLinkRegistryServiceSummaryEntry> serviceSummary) implements ZLinkRuntimeEvent {
}

public enum ZLinkSpotEventKind {
    STATUS_CHANGED,
    PEERS_CHANGED,
    SUBJECTS_CHANGED,
    TIMER_HANDLER_FAILED,
    TIMER_STOPPED_AFTER_UNHANDLED_EXCEPTION
}

public record ZLinkSpotEvent(
    String sourceName,
    Instant timestamp,
    ZLinkSpotEventKind event,
    @Nullable ZLinkSpotNodeStatus status,
    @Nullable List<ZLinkSpotNodePeerEntry> peers,
    @Nullable List<ZLinkSpotNodeSubjectEntry> subjects,
    @Nullable ZLinkSpotTimerDiagnostic timerDiagnostic) implements ZLinkRuntimeEvent {
}
```

위 registration 타입들이 `spring-boot-channel-messaging.ko.md`,
`spring-boot-spot.ko.md`, `channel-messaging-samples.ko.md`,
`spot-samples.ko.md`에서 쓰는 기준 표면이다.

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

send/publish는 기본 async submit이다. async submit과 backpressure의 공통 의미는
[framework 공통 정책](../../common/spec/async-execution-policy.ko.md)을 따른다.
request도 request packet을 보내는 단계에서는 같은 async submit 경로를 사용하고,
reply 대기는 request timeout이 따로 정한다.

packet key 해석 규칙은 아래 순서를 기본으로 본다.

1. `options.packetName`
2. payload 타입의 `@ZLinkPacket`
3. payload 타입 `SimpleName`

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
    String packetName() default "";
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSend {
    String packetName() default "";
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkPublish {
    String packetName() default "";
}
```

`SPOT`, `STREAM`용 annotation도 `.NET` `[ZLinkX]` 이름에 맞춰 `Mapping` 접미사 없이 둔다.

```java
@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSpotRequest {
    String packetName() default "";
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
    String packetName() default "";
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSpotActorSend {
    String packetName() default "";
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkStreamPacket {
}
```

SPOT actor lifecycle callback은 actor만 받는다. join admission callback은
framework 공통 `Message` request를 받고 `ZLinkSpotActorJoinResponse`를 반환한다.
accepted가 `true`일 때만 actor 위치가 target Spot으로 commit되고
`onJoinedActor`가 호출된다. accepted가 `false`이면 actor 위치는 바뀌지 않고
post-join callback도 실행되지 않는다. Entry Spot도 같은 admission callback을 갖고,
user Spot에서 Entry Spot으로 돌아오는 명시적 join 요청을 여기서 accept/reject한다.
disconnected callback은 actor만 받는다. session actor의 현재 binding이 끊어졌거나
application이 actor disconnected 알림을 명시적으로 보낼 때 실행되며, actor가 들어오거나
나간 Spot 변경 결과를 만들지 않기 때문이다.

public final class MatchSpot implements ZLinkSpot<MatchActor> {
    @Override
    public void onDisconnectActor(
        MatchActor actor,
        CancellationToken cancellationToken) {
        // session binding cleanup or domain notification
    }
}
```

## 6. Filter

`Spring`의 `HandlerInterceptor`와 비슷한 공통 처리 층을 둔다.

```java
public interface ZLinkHandlerFilter {
    <T> CompletionStage<T> invokeAsync(
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
- actor/session relay는 ActorGateway와 `ZLinkBoundSession`을 기준으로 구현한다.

### 7.1 message dispatch error observer

미등록 메시지와 dispatch 실패 관측은 전역 `ZLinkMessageDispatchErrorObserver` 로 처리한다.
channel 별, spot 별 observer 등록은 이 버전의 공개 계약이 아니다. request 실패는 reply path 가 있으면
error reply 로 끝나고, local actor call 처럼 reply frame 이 없는 경로는 `CompletionStage` 를 framework
error 로 완료한다. one-way 실패는 drop 되지만 기본 로그, counter, observer event 를 남긴다.

```java
public interface ZLinkDispatchOptions {
    ZLinkDispatchOptions setMessageDispatchErrorObserver(
        Class<? extends ZLinkMessageDispatchErrorObserver> observerType);

    ZLinkDispatchOptions setMessageDispatchErrorObserver(
        ZLinkMessageDispatchErrorObserver observer);
}

public interface ZLinkMessageDispatchErrorObserver {
    CompletionStage<Void> onDispatchError(
        ZLinkMessageDispatchErrorEvent error);
}
```

`ZLinkMessageDispatchErrorEvent` 는 `surface`, `messageKind`, `reason`, `action`,
`packetName`, `channelName`, `topic`, `spotRid`, `actorId`, `sourceRid`, `correlationId`,
`exception` 을 담는 record 다. native message 소유권이나 frame 참조는 포함하지 않는다.

```java
@Override
public void configure(ZLinkFrameworkOptions options) {
    options.configureDispatch()
        .setMessageDispatchErrorObserver(MyDispatchErrorObserver.class);
}
```

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Java Channel Messaging Samples](../guide/samples/channel-messaging-samples.ko.md) | [다음: ZLink Framework Java SPOT Samples](../guide/samples/spot-samples.ko.md)
<!-- framework-adapter-nav:bottom:end -->
