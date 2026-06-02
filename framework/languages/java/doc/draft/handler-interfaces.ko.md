<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework Java Channel Messaging Samples](./channel-messaging-samples.ko.md) | [다음: Draft -- ZLink Framework Java SPOT Samples](./spot-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Java 묶음](./README.ko.md) | [포팅 계획](./java-kotlin-framework-porting-plan.ko.md) | [channel](./spring-boot-channel-messaging.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT](./spring-boot-spot.ko.md) | [Actor/session](./spring-boot-actor-session.ko.md) | [STREAM](./spring-boot-stream.ko.md) | [Monitoring](./spring-boot-monitoring.ko.md) | [Registry](./spring-boot-registry.ko.md)

# Draft -- ZLink Framework Java Interface Catalog

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java`에서 `ZLink Framework`가 노출할 인터페이스와
> annotation을 한 곳에 모은 기준 문서다.

## 0. 공통 정책 반영

이 문서는 [Framework Adapter 정책](../../../../doc/spec/README.ko.md)과
[doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
규칙을 그대로 따른다. 따라서 `Java` 문서에서는 아래를 기본으로 본다.

- 메서드는 `camelCase`, 클래스와 annotation은 `PascalCase`를 쓴다.
- 개념 이름은 공통 정책과 맞춘다. 예를 들어 `send`, `request`, `publish`,
  `sendTo`, `requestTo`, `sendChannel`, `requestChannel` 같은 action 이름을
  유지한다.
- blocking과 non-blocking을 별도 동사 이름으로 나누지 않는다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다.

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
| host | `ZLinkFramework` | Spring Boot 밖에서 framework host를 시작하는 public facade |
| management | `ZLinkSpotManager` | Spot type 기준 생성, `spotRid` 기준 조회/삭제 |
| timer | `ZLinkTimer` | `SPOT` lifecycle timer handle |
| filter | `ZLinkHandlerFilter` | handler 전후 공통 처리 |
| marker | `ZLinkRequest<TReply>` | request/reply 타입 연결 marker |
| registry | `ZLinkRegistryQuery`, `ZLinkRegistryQueryClient` | registry 조회 |

## 2. Context

```java
public interface ZLinkHandlerContext {
    @Nullable String channelName();
    @Nullable String packetName();
    @Nullable String contentType();
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
    @Nullable String source();
}
```

## 3. Handler

```java
public interface ZLinkRequestHandler<TRequest, TReply> {
    CompletionStage<TReply> handleAsync(
        TRequest request,
        ZLinkRequestContext context);
}

public interface ZLinkSendHandler<TMessage> {
    CompletionStage<Void> handleAsync(
        TMessage message,
        ZLinkSendContext context);
}

public interface ZLinkPublishHandler<TMessage> {
    CompletionStage<Void> handleAsync(
        TMessage message,
        ZLinkPublishContext context);
}

public interface ZLinkRouteSendHandler<TMessage> {
    CompletionStage<Void> handleAsync(
        TMessage message,
        ZLinkRouteSendContext context);
}

public interface ZLinkRouteRequestHandler<TRequest, TReply> {
    CompletionStage<TReply> handleAsync(
        TRequest request,
        ZLinkRouteRequestContext context);
}
```

stream은 `.NET` 기준과 같이 header session 하나로 설명한다. 이전 초안의
`packet session`/`raw session` 분리는 현재 포팅 기준이 아니다. callback으로 전달된
payload는 framework가 빌려준 값이므로 callback 밖에서 보관해야 하면 별도 copy를
만든다.

```java
public interface ZLinkSession {
    ZLinkSessionContext context();

    CompletionStage<Void> onConnectedAsync();

    CompletionStage<Void> onDisconnectedAsync();

    CompletionStage<Void> onErrorAsync(ZLinkStreamError error);

    default CompletionStage<Void> onDispatchAsync(
        ZLinkStreamHeader header,
        Message payload) {
        return CompletableFuture.completedFuture(null);
    }
}

public interface ZLinkSessionContext {
    String sessionId();

    Optional<RoutingId> routingId();

    Optional<String> localAddr();

    Optional<String> remoteAddr();

    ZLinkSessionClient client();

    ZLinkSessionActors actors();

    CompletionStage<Void> closeAsync();
}

public interface ZLinkSessionClient {
    <TMessage> ZLinkSessionSendCall send(TMessage message);

    <TMessage> ZLinkSessionReplyCall reply(TMessage message);
}

public interface ZLinkSessionActors {
    List<ZLinkSessionActor> bound();

    CompletionStage<ZLinkSessionActor> bindAsync(ZLinkActor actor);

    CompletionStage<ZLinkSessionActor> bindAsync(ZLinkActorRef actor);

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
    CompletionStage<Void> submitAsync();
}

public interface ZLinkSessionReplyCall {
    ZLinkSessionReplyCall metadata(String key, String value);
    ZLinkSessionReplyCall compress();
    CompletionStage<Void> submitAsync();
}

public interface ZLinkSessionActor {
    String actorId();
    ZLinkActorRef ref();
    CompletionStage<Void> relayAsync(ZLinkStreamHeader header, Message payload);
    CompletionStage<Void> notifyDisconnectedAsync();
}

public interface ZLinkActor {
    String actorId();
    ZLinkActorContext context();
    default void configure() {
    }
}

public interface ZLinkActorFactory {
    CompletionStage<ZLinkActor> createAsync(
        String actorId,
        ZLinkActorContext context);
}

public interface ZLinkActorManager {
    CompletionStage<ZLinkActor> createAsync(String actorId, String actorType);
    CompletionStage<Optional<ZLinkActor>> findAsync(String actorId);
    CompletionStage<ZLinkActor> getOrCreateAsync(String actorId, String actorType);
}

public interface ZLinkActorContext {
    Optional<RoutingId> spotRid();
    boolean isJoined();
    ZLinkBoundSession boundSession();
    ZLinkSpot getSpot();
    <TSpot extends ZLinkSpot> TSpot getSpot(Class<TSpot> spotType);
    ZLinkActorJoinSpotCall joinSpot(RoutingId spotRid, Object request);
    ZLinkActorJoinEntrySpotCall joinEntrySpot(RoutingId spotNodeRid);
}

public interface ZLinkBoundSession {
    <TMessage> ZLinkBoundSessionSendCall send(TMessage message);
    CompletionStage<Void> disconnectAsync();
}

public interface ZLinkBoundSessionSendCall {
    ZLinkBoundSessionSendCall packetName(String packetName);
    ZLinkBoundSessionSendCall metadata(String key, String value);
    CompletionStage<Void> submitAsync();
}
```

`onErrorAsync(...)`는 application handler 내부 예외를 받는 callback이 아니다.
이 초안에서는 monitor에서 관찰 가능한 session-correlatable transport 오류만
`ZLinkStreamError`로 다시 올리는 용도로 제한한다.

## 4. Client 와 Options

```java
public interface ManualEndpointListBuilder {
    void connect(String endpoint);
}

public interface ManualRouterPeerListBuilder {
    void connect(String endpoint);
}

public interface ClientCapabilityBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}

public interface SubscriberCapabilityBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}

public interface SpotRouterCapabilityBuilder {
    void setRouterBind(String endpoint);
    void useManualConnections(Consumer<ManualRouterPeerListBuilder> configure);
}

public interface SpotPubSubCapabilityBuilder {
    void setPubBind(String endpoint);
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}

public interface SpotChannelClientCapabilityBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}

public interface SpotPublisherClientCapabilityBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}

public interface ZLinkSpotNodeBuilder {
    void enableRouter();
    void enableRouter(Consumer<SpotRouterCapabilityBuilder> configure);
    void enablePubSub();
    void enablePubSub(Consumer<SpotPubSubCapabilityBuilder> configure);
    void attachChannelClient(String channelName);
    void attachChannelClient(
        String channelName,
        Consumer<SpotChannelClientCapabilityBuilder> configure);
    void attachSpotPublisherClient(String channelName);
    void attachSpotPublisherClient(
        String channelName,
        Consumer<SpotPublisherClientCapabilityBuilder> configure);
    void acceptSpotRoutesFromChannel(String channelName);
    void acceptSpotRoutesFromChannel(
        String channelName,
        Consumer<ZLinkSpotRouteChannelAcceptanceBuilder> configure);
    void configureEntrySpot(Consumer<ZLinkEntrySpotOptions> configure);
    void addSpotFactory(Class<? extends ZLinkSpot> spotType);
    void addEntrySpot(Class<? extends ZLinkEntrySpot> entrySpotType);
}

public interface ZLinkSpotRouteChannelAcceptanceBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}

public interface ZLinkEntrySpotOptions {
    RoutingId routingId();
    void setRoutingId(RoutingId routingId);
}

public interface ZLinkStreamNodeBuilder {
    void bind(String endpoint);
    void attachActorGateway(String spotNodeName);
    void registerSession(Class<? extends ZLinkSession> sessionType);
}

public interface ChannelServerCapabilityBuilder {
    void bind(String endpoint);
}

public interface ChannelPublisherCapabilityBuilder {
    void bind(String endpoint);
}

public interface ClientServerChannelBuilder {
    void enableServer();
    void enableServer(Consumer<ChannelServerCapabilityBuilder> configure);
    void enableClient();
    void enableClient(Consumer<ClientCapabilityBuilder> configure);
    void addHandlerGroup(String groupName);
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
    void enableSpotRouteEgress(String targetSpotNodeChannelName);
}

public interface FanoutChannelBuilder {
    void enablePublisher();
    void enablePublisher(Consumer<ChannelPublisherCapabilityBuilder> configure);
    void enableSubscriber();
    void enableSubscriber(Consumer<SubscriberCapabilityBuilder> configure);
    void addHandlerGroup(String groupName);
    <THandler extends ZLinkPublishHandler<TMessage>, TMessage> void addPublishHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType,
        @Nullable String packetName);
    void addPublishHandler(Class<?> handlerType, @Nullable String packetName);
}

public interface DealerMeshChannelBuilder {
    void enableClient();
    void enableClient(Consumer<ClientCapabilityBuilder> configure);
    void addHandlerGroup(String groupName);
}

public interface RouteMeshChannelBuilder {
    void bind(String endpoint);
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
    void addHandlerGroup(String groupName);
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
    void enableSpotRouteEgress(String targetSpotNodeChannelName);
}

public interface RegistryBuilder {
    void add(String endpoint);
}

public interface ZLinkFrameworkOptions {
    Duration defaultTimeout();
    void setDefaultTimeout(Duration timeout);
    ZLinkCodecRegistryBuilder codecs();
    void addHandlersFromPackageOf(Class<?> markerType);
    void configureMetadata(Consumer<ZLinkMetadataPolicyBuilder> configure);
    void useDiscovery(Consumer<RegistryBuilder> configure);
    void addClientServerChannel(
        String channelName,
        Consumer<ClientServerChannelBuilder> configure);
    void addFanoutChannel(
        String channelName,
        Consumer<FanoutChannelBuilder> configure);
    void addDealerMeshChannel(
        String channelName,
        Consumer<DealerMeshChannelBuilder> configure);
    void addRouteMeshChannel(
        String channelName,
        Consumer<RouteMeshChannelBuilder> configure);
    void addSpotMesh(
        String channelName,
        Consumer<ZLinkSpotMeshBuilder> configure);
    void addStreamNode(
        String streamNodeName,
        Consumer<ZLinkStreamNodeBuilder> configure);
    void addActorFactory(
        String actorType,
        Class<? extends ZLinkActorFactory> factoryType);
    void addSpotRemoteAddressResolver(
        Class<? extends ZLinkSpotRemoteAddressResolver> resolverType);
    void useRegistrySpotRemoteAddresses(String namespaceName);
    void useRegistrySpotRemoteAddresses(
        String namespaceName,
        Consumer<ZLinkRegistrySpotRemoteAddressesOptions> configure);
    void useFilter(Class<? extends ZLinkHandlerFilter> filterType);
    void configureDispatch(Consumer<ZLinkDispatchOptions> configure);
}

public interface ZLinkSpotMeshBuilder {
    void useDiscovery(Consumer<RegistryBuilder> configure);
    void addNode(String spotNodeName, Consumer<ZLinkSpotNodeBuilder> configure);
}

public interface ZLinkCodecRegistryBuilder {
    void addJson();
    void addMessagePack();
    void addProtobuf();
}

public interface ZLinkMetadataPolicyBuilder {
    void forward(String key);
}

public interface ZLinkRegistrySpotRemoteAddressesOptions {
    void setRouterChannelId(String routerChannelId);
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
    CompletionStage<Void> submitAsync();
}

public interface ZLinkRequestCall {
    ZLinkRequestCall packetName(String messageName);
    ZLinkRequestCall timeout(Duration timeout);
    <TReply> CompletionStage<TReply> submitAsync(Class<TReply> replyType);
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
// `<THandler, TActor>` 제네릭 쌍으로 묶는다. Java는 타입 소거 때문에 `TActor`를
// `Class<? extends ZLinkActor> actorType` 인자로 받아 같은 actor-type 바인딩을 유지한다.
public interface ZLinkActorHandlerRegistry {
    void addHandler(Class<?> handlerType);
    void addHandler(Class<?> handlerType, String packetName);
    void addActorPacket(Class<?> handlerType, Class<? extends ZLinkActor> actorType);
    void addActorPacket(
        Class<?> handlerType,
        Class<? extends ZLinkActor> actorType,
        String packetName);
    void addPostActorJoined(Class<?> handlerType, Class<? extends ZLinkActor> actorType);
    void addActorLeft(Class<?> handlerType, Class<? extends ZLinkActor> actorType);
    void addActorDisconnected(Class<?> handlerType, Class<? extends ZLinkActor> actorType);
}

public interface ZLinkSpotHandlerRegistry extends ZLinkActorHandlerRegistry {
    void addPacket(Class<?> handlerType);
    void addSubscribe(Class<?> handlerType, String topic);
    void addActorJoin(Class<?> handlerType, Class<? extends ZLinkActor> actorType);
    void addActorJoin(Class<?> handlerType);
}

public interface ZLinkSpotContext {
    RoutingId spotRid();
    RoutingId nodeRid();
    ZLinkSpotHandlerRegistry handlers();
    ZLinkSpotOutbound outbound();
    CompletionStage<Void> leaveActorAsync(ZLinkActor actor);
    CompletionStage<ZLinkTimer> addTimer(
        String name,
        Duration period,
        Class<?> handlerType,
        @Nullable ZLinkTimerOptions options);
}

public interface ZLinkEntrySpotContext {
    RoutingId spotRid();
    RoutingId nodeRid();
    ZLinkSpotHandlerRegistry handlers();
    ZLinkSpotOutbound outbound();
    CompletionStage<ZLinkTimer> addTimer(
        String name,
        Duration period,
        Class<?> handlerType,
        @Nullable ZLinkTimerOptions options);
}

public interface ZLinkRouteClient {
    <TMessage> ZLinkSendCall send(
        String routerChannelId,
        RoutingId targetNodeRid,
        TMessage message);

    <TMessage> ZLinkRequestCall request(
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
    CompletionStage<Void> submitAsync();
}

public record ZLinkSpotCreateResult(
    RoutingId spotRid,
    boolean created) {
}

public record ZLinkSpotInfo(
    RoutingId spotRid) {
}

public interface ZLinkSpotManager {
    CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType);

    CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid);

    CompletionStage<ZLinkSpotCreateResult> getOrCreateAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid);

    CompletionStage<Optional<ZLinkSpotInfo>> findAsync(RoutingId spotRid);
    CompletionStage<List<ZLinkSpotInfo>> listAsync();
    CompletionStage<Boolean> removeAsync(RoutingId spotRid);
}

public interface ZLinkSpot {
    ZLinkSpotContext context();

    default void configure() {
    }

    default CompletionStage<Void> onCreateAsync(List<Message> createParts) {
        return CompletableFuture.completedFuture(null);
    }

    default CompletionStage<Void> onInitializeAsync() {
        return CompletableFuture.completedFuture(null);
    }

    default CompletionStage<Void> onClosingAsync() {
        return CompletableFuture.completedFuture(null);
    }
}

public interface ZLinkEntrySpot {
    ZLinkEntrySpotContext context();

    default void configure() {
    }

    default CompletionStage<Void> onInitializeAsync() {
        return CompletableFuture.completedFuture(null);
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

client 측 STREAM connector는 server session과 별도 모듈로 둔다.

```java
public interface ZLinkStreamConnector extends AutoCloseable {
    boolean isConnected();
    ZLinkStreamConnectionState state();
    ZLinkStreamConnectorOptions options();
    int pendingDispatchCount();

    CompletionStage<Void> connectAsync();
    CompletionStage<Void> disconnectAsync();
    CompletionStage<Void> reconnectAsync();
    CompletionStage<Void> closeAsync();
    CompletionStage<Void> dispatchAsync();

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
    CompletionStage<Void> handleAsync(TEvent event);
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
등록하고, 이 초안에서는 `connect(...)` 호출 시 remote router id를 따로 받지
않는다.

`ZLinkSpotManager`는 등록된 Spot type으로 factory를 고르고, 생성 결과와 조회
표면에서 `spotRid`를 다시 볼 수 있게 한다. 같은 `SpotNode` 안에서 이미 등록된
Spot type을 다시 등록하면 조용히 덮어쓰지 않고 예외를 던지는 편을 기본으로 본다.

send/publish는 기본 async submit이다. blocking send를 async 호출로 감싸는 것이
아니라, nonblocking send와 ready notification을 이용해 backpressure 동안 호출
thread가 막히지 않게 해야 한다. request도 request packet을 보내는 단계에서는
같은 async submit 경로를 사용하고, reply 대기는 request timeout이 따로 정한다.

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

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSpotSubscription {
    String spotNodeName();
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
public @interface ZLinkSpotActorJoin {
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSpotPostActorJoined {
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSpotActorLeft {
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSpotActorDisconnected {
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkStreamPacket {
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
- 같은 capability는 자동 연결 또는 수동 연결 중 하나만 선택한다.
- 수동 연결은 `channel + capability` 단위로 관리한다.
- manual capability는 startup 등록뿐 아니라 런타임 `connect`, `disconnect`,
  `listConnections`도 지원해야 한다.
- `ROUTER -> DEALER` 임의 push는 channel messaging 공용 계약에 넣지 않는다.
- stream session은 header 기반 `ZLinkSession` 하나로 둔다.
- actor/session relay는 ActorGateway와 `ZLinkBoundSession`을 기준으로 구현한다.
