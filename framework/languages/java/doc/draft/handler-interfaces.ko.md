<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework Java Channel Messaging Samples](./channel-messaging-samples.ko.md) | [다음: Draft -- ZLink Framework Java SPOT Samples](./spot-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Java 묶음](./README.ko.md) | [channel](./spring-boot-channel-messaging.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT](./spring-boot-spot.ko.md) | [STREAM](./spring-boot-stream.ko.md) | [Monitoring](./spring-boot-monitoring.ko.md) | [Registry](./spring-boot-registry.ko.md)

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
| handler | `ZLinkEventHandler<TEvent>` | event handler |
| handler | `ZLinkPacketStreamSession` | packet stream session lifecycle + packet callback |
| handler | `ZLinkRawStreamSession` | raw stream session lifecycle + raw callback |
| stream | `ZLinkStream` | stream I/O와 peer 식별 |
| value | `ZLinkStreamSessionError`, `ZLinkStreamError` | stream error kind + detail |
| handler | `ZLinkRuntimeEventHandler<TEvent>` | runtime monitoring event handler |
| options | `ZLinkMonitoringOptions` | runtime monitoring source 등록 |
| value | `ZLinkSocketEventKind`, `ZLinkSocketEvent` | socket runtime event |
| value | `ZLinkDiscoveryEventKind`, `ZLinkDiscoveryEvent` | discovery runtime event |
| value | `ZLinkRegistryEventKind`, `ZLinkRegistryEvent` | registry runtime event |
| value | `ZLinkSpotEventKind`, `ZLinkSpotEvent` | spot runtime event |
| serializer | `ZLinkMessageSerializer` | payload codec 추상화 |
| client | `ZLinkClient` | channel messaging outbound client |
| client | `ZLinkSpotClient` | `SPOT` outbound client |
| client | `ZLinkEventPublisher` | event publisher |
| client | `ZLinkSpotPublisherClient` | 외부 노드용 `SPOT` publish client |
| management | `ZLinkChannelConnectionManager` | channel capability별 수동 연결 제어 |
| management | `ZLinkSpotManager` | `spotName` 기준 생성/조회/삭제 |
| management | `ZLinkSpotConnectionManager` | `SPOT` capability별 수동 연결 제어 |
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
    @Nullable String correlationId();
    @Nullable Instant deadline();
    ApplicationContext services();
}
```

파생 context는 아래처럼 나눈다.

- `ZLinkRequestContext`
- `ZLinkSendContext`
- `ZLinkEventContext`
- `ZLinkSpotRequestContext`
- `ZLinkSpotSubscriptionContext`

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

public interface ZLinkEventHandler<TEvent> {
    CompletionStage<Void> handleAsync(
        TEvent message,
        ZLinkEventContext context);
}
```

stream은 packet path와 raw path를 나눌 수 있지만, 둘 다 session lifecycle 위에서
설명하는 방향을 기본으로 본다.

```java
public interface ZLinkStream {
    String sessionId();

    @Nullable RoutingId routingId();

    @Nullable String localAddr();

    @Nullable String remoteAddr();

    boolean write(Message payload, SendFlags flags);

    boolean write(Message header, Message payload, SendFlags flags);
}

public enum ZLinkStreamSessionError {
    INTERNAL,
    TRANSPORT_ERROR,
    HANDSHAKE_FAILED
}

public record ZLinkStreamError(
    ZLinkStreamSessionError error,
    int internalErrno) {

    public ErrorCode getErrorCode() {
        return ZlinkException.mapErrorCode(internalErrno);
    }

    public String getErrorMessage() {
        return Zlink.strerror(internalErrno);
    }
}

public interface ZLinkPacketStreamSession {
    CompletionStage<Void> onConnectedAsync(ZLinkStream stream);

    CompletionStage<Void> onDisconnectedAsync(ZLinkStream stream);

    CompletionStage<Void> onErrorAsync(
        ZLinkStream stream,
        ZLinkStreamError error);

    CompletionStage<Void> onPacketAsync(
        ZLinkStream stream,
        Message header,
        Message payload);
}

public interface ZLinkRawStreamSession {
    CompletionStage<Void> onConnectedAsync(ZLinkStream stream);

    CompletionStage<Void> onDisconnectedAsync(ZLinkStream stream);

    CompletionStage<Void> onErrorAsync(
        ZLinkStream stream,
        ZLinkStreamError error);

    CompletionStage<Void> onRawAsync(
        ZLinkStream stream,
        Message payload);
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
    void useManualConnections(Consumer<List<String>> configure);
}

public interface SpotRouterCapabilityBuilder {
    void useManualConnections(Consumer<ManualRouterPeerListBuilder> configure);
}

public interface SpotPubSubCapabilityBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}

public interface SpotChannelClientCapabilityBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}

public interface SpotPublisherClientCapabilityBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}

public interface ZLinkSpotNodeBuilder {
    void bind(String endpoint);
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
    void addSpotFactory(String spotName, Class<? extends ZLinkSpot> spotType);
}

public interface ChannelBuilder {
    void enableServer();
    void enableClient();
    void enableClient(Consumer<ClientCapabilityBuilder> configure);
    void enablePublisher();
    void enableSubscriber();
    void enableSubscriber(Consumer<SubscriberCapabilityBuilder> configure);
}

public interface RegistryBuilder {
    void add(String endpoint);
}

public interface ZLinkFrameworkOptions {
    void addChannel(String channelName, Consumer<ChannelBuilder> configure);
    void useDiscovery(Consumer<RegistryBuilder> configure);
    void useSpotDiscovery(
        String channelName,
        Consumer<RegistryBuilder> configure);
    void addSpotNode(
        String spotNodeName,
        Consumer<ZLinkSpotNodeBuilder> configure);
}

public final class ZLinkSendOptions {
    @Nullable private String packetName;

    public ZLinkSendOptions setPacketName(String packetName);
}

public final class ZLinkRequestOptions {
    @Nullable private String packetName;
    @Nullable private Duration timeout;

    public ZLinkRequestOptions setPacketName(String packetName);
    public ZLinkRequestOptions setTimeout(Duration timeout);
}

public interface ZLinkClient {
    <TMessage> CompletionStage<Void> sendAsync(
        String channelName,
        TMessage message,
        @Nullable ZLinkSendOptions options);

    <TReply> CompletionStage<TReply> requestAsync(
        String channelName,
        ZLinkRequest<TReply> request,
        @Nullable ZLinkRequestOptions options);
}

public interface ZLinkSpotClient {
    <TMessage> CompletionStage<Void> sendChannelAsync(
        String channelName,
        TMessage message,
        @Nullable ZLinkSendOptions options);

    <TReply> CompletionStage<TReply> requestChannelAsync(
        String channelName,
        ZLinkRequest<TReply> request,
        @Nullable ZLinkRequestOptions options);

    <TMessage> CompletionStage<Void> sendToAsync(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message,
        @Nullable ZLinkSendOptions options);

    <TReply> CompletionStage<TReply> requestToAsync(
        RoutingId targetRid,
        RoutingId spotRid,
        ZLinkRequest<TReply> request,
        @Nullable ZLinkRequestOptions options);

    <TEvent> CompletionStage<Void> publishAsync(
        String topic,
        TEvent message,
        @Nullable ZLinkSendOptions options);
}

public interface ZLinkSpotPublisherClient {
    <TEvent> CompletionStage<Void> publishAsync(
        String channelName,
        String topic,
        TEvent message,
        @Nullable ZLinkSendOptions options);
}

public interface ZLinkEventPublisher {
    <TEvent> CompletionStage<Void> publishAsync(
        String channelName,
        String topic,
        TEvent message,
        @Nullable ZLinkSendOptions options);
}

public record ZLinkSpotCreateResult(
    RoutingId spotRid,
    String spotName,
    boolean created) {
}

public record ZLinkSpotInfo(
    RoutingId spotRid,
    String spotName) {
}

public interface ZLinkSpotManager {
    CompletionStage<ZLinkSpotCreateResult> createAsync(String spotName);
    CompletionStage<ZLinkSpotCreateResult> createAsync(
        String spotName,
        RoutingId spotRid);
    CompletionStage<Optional<ZLinkSpotInfo>> getAsync(RoutingId spotRid);
    CompletionStage<List<ZLinkSpotInfo>> listAsync();
    CompletionStage<Boolean> removeAsync(RoutingId spotRid);
}

public abstract class ZLinkSpot {
    public abstract RoutingId spotRid();

    public abstract CompletionStage<ZLinkTimer> addTimer(
        String name,
        Duration period,
        Class<?> handlerType);
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

public interface ZLinkChannelConnectionManager {
    ChannelClientConnections getClient(String channelName);
    ChannelSubscriberConnections getSubscriber(String channelName);
}

public interface ZLinkSpotConnectionManager {
    SpotRouterConnections getRouter(String spotNodeName);
    SpotPubSubConnections getPubSub(String spotNodeName);
    SpotChannelClientConnections getChannelClient(
        String spotNodeName,
        String channelName);
    SpotPublisherClientConnections getSpotPublisherClient(
        String spotNodeName,
        String channelName);
}

public interface ZLinkTimer extends AutoCloseable {
    boolean isDisposed();
    CompletionStage<Void> cancelAsync();
}
```

## 4.1 runtime monitoring

runtime monitoring은 socket/discovery의 하부 monitor와 registry/spot의 snapshot
diff를 함께 감싸는 운영 표면이다.

```java
public interface ZLinkMonitoringOptions {
    void addSocketEvents(String sourceName, SocketEvent events);
    void addDiscoveryEvents(
        String sourceName,
        ServiceMonitorEventMask... events);
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

public record ZLinkSocketEvent(
    String sourceName,
    Instant timestamp,
    ZLinkSocketEventKind event,
    MonitorEventType nativeEvent,
    long value,
    @Nullable RoutingId routingId,
    String localAddr,
    String remoteAddr) implements ZLinkRuntimeEvent {
}

public enum ZLinkDiscoveryEventKind {
    SERVICE_UP,
    SERVICE_DOWN,
    PROVIDERS_CHANGED,
    PEER_ADMISSION_CHANGED,
    ERROR,
    CLOSED,
    INTERNAL
}

public record ZLinkDiscoveryEvent(
    String sourceName,
    Instant timestamp,
    ZLinkDiscoveryEventKind event,
    ServiceEventType nativeEventType,
    long status,
    long errorCode,
    long value,
    long detailFlags,
    String serviceName,
    String endpoint,
    @Nullable RoutingId routingId,
    String subject) implements ZLinkRuntimeEvent {
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
    @Nullable RegistryStatus status,
    @Nullable List<RegistryTopologyEntry> topology) implements ZLinkRuntimeEvent {
}

public enum ZLinkSpotEventKind {
    STATUS_CHANGED,
    PEERS_CHANGED,
    SUBJECTS_CHANGED
}

public record ZLinkSpotEvent(
    String sourceName,
    Instant timestamp,
    ZLinkSpotEventKind event,
    @Nullable SpotNodeStatus status,
    @Nullable List<SpotNodePeerEntry> peers,
    @Nullable List<SpotNodeSubjectEntry> subjects) implements ZLinkRuntimeEvent {
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

`ZLinkSpotManager`는 등록된 `spotName`으로 factory를 고르고, 생성 결과와 조회
표면에서 `spotRid -> spotName` 매핑을 다시 볼 수 있게 한다. 같은 `SpotNode`
안에서 이미 등록된 `spotName`을 다시 등록하면 조용히 덮어쓰지 않고 예외를
던지는 편을 기본으로 본다.

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

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkRequestMapping {
    String packetName() default "";
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSendMapping {
    String packetName() default "";
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkEventMapping {
    String packetName() default "";
}
```

`SPOT`, `STREAM`용 annotation도 같은 방식으로 분리한다.

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
