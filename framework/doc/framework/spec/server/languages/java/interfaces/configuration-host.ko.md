# Java 구성과 host 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Transport liveness](../../../55-transport-liveness.ko.md)

```java
public interface ZLinkFrameworkOptions {
    Duration defaultRequestTimeout();
    void setDefaultRequestTimeout(Duration timeout);
    ZLinkCodecRegistryBuilder codecs();
    void addHandlersFromPackageOf(Class<?> markerType);
    ZLinkMetadataPolicyBuilder configureMetadata();
    void addLocationStore(ZLinkLocationStore store);
    void addRelocationStore(ZLinkRelocationStore store);
    void setApplicationVersion(long version);
    void setMaintenanceWave(String waveId);
    ZLinkLocationOptions configureLocations();
    ZLinkNetworkOptions configureNetwork();
    ZLinkMeshNodeBuilder addRouteMesh(String meshName);
    ClientServerChannelBuilder addClientServerChannel(String channelName);
    FanoutChannelBuilder addFanoutChannel(String channelName);
    ZLinkStreamNodeBuilder addStreamNode(String name);
    void useFilter(Class<? extends ZLinkHandlerFilter> filterType);
    ZLinkDispatchOptions configureDispatch();
    ZLinkStreamCompressionBuilder configureStreamCompression();
    ZLinkWorkerOptions configureWorkers();
    void useVirtualThreadHandlers();
    void useHandlerExecutor(Executor executor);
}

public enum ZLinkFlowOrigin {
    INBOUND, TIMER, APPLICATION, LIFECYCLE
}

public interface ZLinkNetworkOptions {
    String bindHost();
    void setBindHost(String host);
    Optional<String> advertiseHost();
    void setAdvertiseHost(String host);
}

public interface ZLinkMeshNodeSocketConfig {
    long maxMessageSize();
    void setMaxMessageSize(long value);
    int sendHighWaterMark();
    void setSendHighWaterMark(int value);
    int receiveHighWaterMark();
    void setReceiveHighWaterMark(int value);
    long mailboxMessageBudget();
    void setMailboxMessageBudget(long value);
    long mailboxByteBudget();
    void setMailboxByteBudget(long value);
    Optional<Duration> receiveTimeout();
    void setReceiveTimeout(Duration value);
    Optional<Duration> sendTimeout();
    void setSendTimeout(Duration value);
}

@FunctionalInterface
public interface ZLinkFrameworkConfigurer {
    void configure(ZLinkFrameworkOptions framework);
}

public interface ZLinkMeshNodeBuilder {
    ZLinkMeshChannelBuilder channel(String channelName);
    ZLinkMeshNodeBuilder listen(String endpoint);
    ZLinkMeshNodeBuilder listen();
    ZLinkMeshNodeBuilder listen(int port);
    ZLinkMeshNodeBuilder setBindHost(String host);
    ZLinkMeshNodeBuilder setAdvertiseHost(String host);
    ZLinkMeshNodeBuilder setRoutingId(RoutingId routingId);
    ZLinkMeshNodeBuilder setRoutingIdPrefix(String prefix);
    ZLinkMeshNodeBuilder setPlacementWeight(int weight);
    ZLinkMeshNodeBuilder setObjectCapacity(int maxActiveObjects, int maxPendingActivations);
    ZLinkMeshObjectRoleBuilder objects();
    ZLinkMeshNodeSocketConfig configureRouterSocket();
    ZLinkSpotPublisherConfig configureSpotPublisher();
    ZLinkMeshPeerConnections peerConnections();
    ZLinkMeshNodeBuilder setDefaultRequestTimeout(Duration timeout);

    <THandler extends ZLinkRouteSendHandler<TMessage>, TMessage>
    ZLinkMeshNodeBuilder addRouteSendHandler(
        Class<THandler> handlerType,
        Class<TMessage> messageType);
    <THandler extends ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply>
    ZLinkMeshNodeBuilder addRouteRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType);

}

public interface ZLinkMeshObjectRoleBuilder {
    ZLinkMeshObjectClientBuilder client();
    ZLinkMeshObjectServerBuilder server();
}

public interface ZLinkMeshObjectClientBuilder {}

public interface ZLinkMeshObjectServerBuilder {
    ZLinkMeshObjectServerBuilder addEntrySpot(Class<? extends ZLinkEntrySpot> entrySpotClass);
    <TSpot extends ZLinkSpot> ZLinkMeshObjectServerBuilder addSpotFactory(
        String spotType, Class<TSpot> spotClass,
        ZLinkObjectPlacementOptions placement, ZLinkRelocationPolicy<TSpot> relocation);
    <TSpot extends ZLinkInstanceSpot> ZLinkMeshObjectServerBuilder addInstanceSpotFactory(
        String instanceSpotType, Class<TSpot> spotClass,
        ZLinkObjectPlacementOptions placement, ZLinkRelocationPolicy<TSpot> relocation);
    <TActor extends ZLinkActor> ZLinkMeshObjectServerBuilder addActorFactory(
        String actorType,
        Class<TActor> actorClass,
        Class<? extends ZLinkActorFactory> factoryClass,
        ZLinkObjectPlacementOptions placement,
        ZLinkRelocationPolicy<TActor> relocation);
}

public record ZLinkObjectPlacementOptions(
    Set<String> placementProfiles,
    Integer maxActiveObjects,
    Integer maxPendingActivations) {}

public interface FanoutChannelBuilder {
    FanoutChannelBuilder enablePublisher(String endpoint);
    FanoutChannelBuilder enablePublisher();
    FanoutChannelBuilder enablePublisher(int port);
    FanoutChannelBuilder setBindHost(String host);
    FanoutChannelBuilder setAdvertiseHost(String host);
    FanoutChannelBuilder setRoutingId(RoutingId publisherRoutingId);
    FanoutChannelBuilder setRoutingIdPrefix(String prefix);
    FanoutChannelBuilder enableSubscriber();
    FanoutChannelBuilder enableSubscriber(String endpoint);
    ZLinkEndpointConnections subscriberConnections();
    FanoutChannelBuilder addHandlerGroup(String groupName);
}
```

Automatic RouteMesh는 RID를 canonical byte order로 비교하고 더 작은 RID의 MeshNode만 상대 endpoint로
connect한다. Manual topology는 application endpoint 구성에 따라 한쪽 또는 양쪽에서 connect할 수 있다.
양쪽 연결이나 automatic discovery 경합·오래된 snapshot으로 중복 후보가 생기면 handshake와 admission이
같은 RID와 lifecycle generation을 확인해 하나만 ready 상태로 유지한다.

ClientServer는 manual endpoint와 location store automatic discovery를 함께 사용할 수 있다. 두 source가 같은
Server RID와 lifecycle generation을 가리키면 connection intent와 ready target을 하나로 합친다. Automatic과
manual 모두 Client만 server로 connect하며 Server는 client endpoint를 찾거나 outbound connect를 시작하지
않는다.

Fanout에서는 Publisher가 descriptor만 게시하고 outbound connect를 시작하지 않는다. Subscriber만 publisher
endpoint로 connect하며 automatic subscriber는 Publisher RID와 lifecycle generation마다 connection intent
하나를 만든다. 한 ChannelName에 automatic subscriber와 manual subscriber endpoint를 함께 구성하면 startup이
실패한다.

Object role을 생략하면 `None`이다. `client()`는 global object operation만 제공하고 placement target이 되지
않으며 `server()`는 Client capability와 Entry Spot·factory registration을 제공한다. Client와 Server는
Location Store가 필수다. Actor·User Spot·Instance Spot factory는 stable type과 explicit relocation policy를
반드시 받으며 policy를 생략하는 overload는 없다.

`ZLinkRelocationPolicy.snapshot(adapterClass)`의 `Class<?>`는 factory kind에 따라 socket bind 전에 검증한다.
Actor factory에는 같은 Actor type의 `ZLinkActorRelocationAdapter`, User·Instance Spot factory에는 같은 Spot
type의 `ZLinkSpotRelocationAdapter`가 필요하다. `Disabled`와 `Recreate`에는 adapter class를 연결하지 않는다.
Type mismatch는 startup configuration error이며 application traffic을 받기 전에 끝난다.

Node placement weight는 0..100이고 기본값은 100이다. Node capacity 기본값은 active 10,000, pending 128이다.
Type별 limit은 `null`이면 node limit을 공유하고 명시하면 1..`Integer.MAX_VALUE`이며 node limit보다 작은 값을
적용한다. Placement profile은 UTF-8 1..255 bytes다. Capacity filter를 weight보다 먼저 적용한다.
`enableActorDispatch()`는 인자가 없으며 global ActorId가 Mesh를 resolve한다.

Location provider는 `ZLinkLocationStore`를 통해 descriptor·location 기능과 authority CAS capability를 함께
제공한다. 별도 `ZLinkAuthorityStore` instance를 host에 등록하지 않는다. `Recreate` 또는 `Snapshot` policy를
하나라도 등록했거나 Instance Spot factory를 하나라도 등록한 host는 `ZLinkRelocationStore`를 정확히 하나 등록한다.
Instance Spot factory가 없고 `Disabled` factory와 same-node join만 사용하는 host는 Relocation Store가 없어도 된다.
Missing 또는 duplicate Store registration은 socket bind 전에 startup
configuration error다. Location과 Relocation capability를 함께 등록하는 API와 Redis 전용 registration helper는
제공하지 않는다.

`ApplicationVersion`은 `0..Long.MAX_VALUE` 범위의 배포 순번이다. 음수는 startup validation에서 거부한다.
Application traffic과 무관한 5초 periodic probe와 같은 current connection의 matching ACK 15초 deadline은
JVM service runtime의 고정 liveness profile이다. 다른 inbound frame은 deadline을 충족하지 않는다. 이 값을
Channel·handler·peer별 public option으로 노출하지 않는다. Location owner lease option은 별도 store 계약이며
transport liveness를 대신하지 않는다.

`maxMessageSize`는 startup 전에만 설정하며 runtime setter를 제공하지 않는다. `0`은 bindings 또는 transport가
수신할 수 있는 최대 complete message 크기로 정규화한다. Transport가 unlimited이면 service wire의 `uint32`
표현 한계에서 envelope overhead를 뺀 값을 사용한다. 양수는 그 표현 한계를 넘을 수 없으며 넘으면 startup
설정 오류로 거부한다. Peer는 정규화한 값을 내부 handshake로 교환하고 sender와 receiver는 두 값 중 작은
effective bound를 complete message allocation 전에 적용한다. 이 negotiation을 위한 public option은 제공하지
않는다.

`mailboxMessageBudget`와 `mailboxByteBudget`은 owner별 application mailbox의 메시지 수와 payload byte 수
상한이다. 두 값은 startup 전에만 설정한다. `0`은 unlimited가 아니라 Framework profile의 유한 기본값을
선택한다. 음수는 startup 설정 오류다. Logical Multicast의 local target도 이 용량 제한으로 admission을
판단한다.

Automatic RID는 `prefix-<32 lowercase hex>` 형식이다. Prefix는 ASCII `[A-Za-z0-9._-]` 1..64자이며 active
owner 충돌은 새 suffix로 최대 8회 재시도한다. Fixed RID는 object role과 Store descriptor가 없는 manual
topology에서만 허용한다. Slot count, allocation group과 public allocation provider는 제공하지 않는다.

Framework가 모든 registration에서 만든 fully encoded MeshNode descriptor는 1 MiB 이하여야 한다.
Spot type과 object capability collection은 각각 최대 1024개다. Relocation adapter class와 opaque application
bytes는 peer descriptor에 게시하지 않는다. Runtime은 완성된 descriptor를 socket bind 전에 한 번에 검증한다.
Bound를 넘으면
startup을 실패시키며 collection을 truncate·split하거나 descriptor 일부를 게시하지 않는다.

## Exact public member `javap` inventory

아래 선언은 `javap`가 출력하는 binary signature 형식으로 이 category의 Java public type과 member를 고정한다.

```java
public final class systems.zlink.framework.configuration.ZLinkFlowOrigin extends java.lang.Enum<systems.zlink.framework.configuration.ZLinkFlowOrigin> {
  public static final systems.zlink.framework.configuration.ZLinkFlowOrigin INBOUND;
  public static final systems.zlink.framework.configuration.ZLinkFlowOrigin TIMER;
  public static final systems.zlink.framework.configuration.ZLinkFlowOrigin APPLICATION;
  public static final systems.zlink.framework.configuration.ZLinkFlowOrigin LIFECYCLE;
  public static systems.zlink.framework.configuration.ZLinkFlowOrigin[] values();
  public static systems.zlink.framework.configuration.ZLinkFlowOrigin valueOf(java.lang.String);
}
public interface systems.zlink.framework.spring.ZLinkFrameworkConfigurer {
  public abstract void configure(systems.zlink.framework.configuration.ZLinkFrameworkOptions);
}
public interface systems.zlink.framework.configuration.ZLinkNetworkOptions {
  public abstract java.lang.String bindHost();
  public abstract void setBindHost(java.lang.String);
  public abstract java.util.Optional<java.lang.String> advertiseHost();
  public abstract void setAdvertiseHost(java.lang.String);
}
public interface systems.zlink.framework.configuration.FanoutChannelBuilder {
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder enablePublisher(java.lang.String);
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder enablePublisher();
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder enablePublisher(int);
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder setBindHost(java.lang.String);
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder setAdvertiseHost(java.lang.String);
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder setRoutingId(systems.zlink.contracts.core.RoutingId);
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder setRoutingIdPrefix(java.lang.String);
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder enableSubscriber();
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder enableSubscriber(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkEndpointConnections subscriberConnections();
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder addHandlerGroup(java.lang.String);
  public abstract void addPublishHandler(java.lang.Class<?>, java.lang.Class<?>);
  public abstract void addPublishHandler(java.lang.Class<?>, java.lang.Class<?>, java.lang.String);
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder addPublishHandler(java.lang.Class<?>);
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder addPublishHandler(java.lang.Class<?>, java.lang.String);
}
public interface systems.zlink.framework.configuration.ZLinkCodecExtension {
  public abstract void register(systems.zlink.framework.configuration.ZLinkCodecRegistrar);
}
public interface systems.zlink.framework.configuration.ZLinkCodecRegistrar {
  public abstract void addSerializer(java.lang.String, systems.zlink.framework.ZLinkMessageSerializer);
  public abstract void addSerializer(java.lang.String, systems.zlink.framework.ZLinkMessageSerializer, java.util.function.Predicate<java.lang.Class<?>>);
  public abstract void addStreamCodec(java.lang.String, systems.zlink.framework.streams.ZLinkStreamCodec);
}
public interface systems.zlink.framework.configuration.ZLinkCodecRegistryBuilder {
  public abstract void use(systems.zlink.framework.configuration.ZLinkCodecExtension);
}
public interface systems.zlink.framework.configuration.ZLinkDiagnosticsOptions {
  public abstract systems.zlink.framework.configuration.ZLinkMessageFlowLogMode messageFlow();
  public abstract double sampleRate();
  public abstract boolean includeMessageSizes();
  public abstract boolean includeNativeDiagnostics();
  public abstract java.lang.String logFile();
  public abstract java.lang.String label();
  public abstract systems.zlink.framework.configuration.ZLinkMessageFlowLogMode effectiveMessageFlow();
}
public interface systems.zlink.framework.configuration.ZLinkDispatchOptions {
  public abstract systems.zlink.framework.configuration.ZLinkUnhandledDispatchOptions unhandled();
  public abstract systems.zlink.framework.configuration.ZLinkDiagnosticsOptions diagnostics();
  public abstract systems.zlink.framework.configuration.ZLinkDispatchOptions setMessageFlowObserver(java.lang.Class<? extends systems.zlink.framework.configuration.ZLinkMessageFlowObserver>);
  public abstract systems.zlink.framework.configuration.ZLinkDispatchOptions setMessageFlowObserver(systems.zlink.framework.configuration.ZLinkMessageFlowObserver);
  public abstract systems.zlink.framework.configuration.ZLinkDispatchOptions messageFlow(systems.zlink.framework.configuration.ZLinkMessageFlowLogMode);
  public abstract systems.zlink.framework.configuration.ZLinkDispatchOptions traceSampleRate(double);
  public abstract systems.zlink.framework.configuration.ZLinkDispatchOptions includeMessageSizes(boolean);
  public abstract systems.zlink.framework.configuration.ZLinkDispatchOptions traceLogFile(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkDispatchOptions traceLabel(java.lang.String);
}
public interface systems.zlink.framework.configuration.ZLinkEndpointConnections {
  public abstract void connect(java.lang.String);
  public abstract void disconnect(java.lang.String);
  public abstract java.util.List<java.lang.String> listConnections();
}
public interface systems.zlink.framework.configuration.ZLinkMeshNodeSocketConfig {
  public abstract long maxMessageSize();
  public abstract void setMaxMessageSize(long);
  public abstract int sendHighWaterMark();
  public abstract void setSendHighWaterMark(int);
  public abstract int receiveHighWaterMark();
  public abstract void setReceiveHighWaterMark(int);
  public abstract long mailboxMessageBudget();
  public abstract void setMailboxMessageBudget(long);
  public abstract long mailboxByteBudget();
  public abstract void setMailboxByteBudget(long);
  public abstract java.util.Optional<java.time.Duration> receiveTimeout();
  public abstract void setReceiveTimeout(java.time.Duration);
  public abstract java.util.Optional<java.time.Duration> sendTimeout();
  public abstract void setSendTimeout(java.time.Duration);
}
public final class systems.zlink.framework.configuration.ZLinkMeshPeerConnection extends java.lang.Record {
  public systems.zlink.framework.configuration.ZLinkMeshPeerConnection(java.lang.String, java.util.Optional<systems.zlink.contracts.core.RoutingId>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String endpoint();
  public java.util.Optional<systems.zlink.contracts.core.RoutingId> expectedRoutingId();
}
public interface systems.zlink.framework.configuration.ZLinkMeshPeerConnections {
  public abstract void connect(java.lang.String);
  public abstract void connect(systems.zlink.contracts.core.RoutingId, java.lang.String);
  public abstract void disconnect(java.lang.String);
  public abstract java.util.List<systems.zlink.framework.configuration.ZLinkMeshPeerConnection> listConnections();
}
public interface systems.zlink.framework.configuration.ZLinkMessageFlowControl {
  public abstract void setMessageFlowMode(systems.zlink.framework.configuration.ZLinkMessageFlowLogMode);
  public abstract systems.zlink.framework.configuration.ZLinkMessageFlowLogMode messageFlowMode();
}
public final class systems.zlink.framework.configuration.ZLinkMessageFlowEvent extends java.lang.Record {
  public systems.zlink.framework.configuration.ZLinkMessageFlowEvent(systems.zlink.framework.configuration.ZLinkMessageFlowOutcome, systems.zlink.framework.configuration.ZLinkDispatchErrorSurface, systems.zlink.framework.configuration.ZLinkDispatchMessageKind, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.Long, systems.zlink.framework.configuration.ZLinkDispatchErrorReason, systems.zlink.framework.configuration.ZLinkDispatchErrorAction, java.lang.String, java.lang.String, java.lang.String, systems.zlink.framework.configuration.ZLinkFlowOrigin);
  public systems.zlink.framework.configuration.ZLinkMessageFlowEvent(systems.zlink.framework.configuration.ZLinkMessageFlowOutcome, systems.zlink.framework.configuration.ZLinkDispatchErrorSurface, systems.zlink.framework.configuration.ZLinkDispatchMessageKind, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.Long, systems.zlink.framework.configuration.ZLinkDispatchErrorReason, systems.zlink.framework.configuration.ZLinkDispatchErrorAction, java.lang.String, java.lang.String);
  public systems.zlink.framework.configuration.ZLinkMessageFlowEvent(systems.zlink.framework.configuration.ZLinkMessageFlowOutcome, systems.zlink.framework.configuration.ZLinkDispatchErrorSurface, systems.zlink.framework.configuration.ZLinkDispatchMessageKind, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.Long);
  public systems.zlink.framework.configuration.ZLinkMessageFlowEvent withFlow(java.lang.String, systems.zlink.framework.configuration.ZLinkFlowOrigin);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.configuration.ZLinkMessageFlowOutcome outcome();
  public systems.zlink.framework.configuration.ZLinkDispatchErrorSurface surface();
  public systems.zlink.framework.configuration.ZLinkDispatchMessageKind messageKind();
  public java.lang.String packetName();
  public java.lang.String channelName();
  public java.lang.String topic();
  public java.lang.String correlationId();
  public java.lang.String sourceRid();
  public java.lang.String spotRid();
  public java.lang.String actorId();
  public java.lang.Long messageSize();
  public systems.zlink.framework.configuration.ZLinkDispatchErrorReason errorReason();
  public systems.zlink.framework.configuration.ZLinkDispatchErrorAction errorAction();
  public java.lang.String errorType();
  public java.lang.String errorMessage();
  public java.lang.String flowId();
  public systems.zlink.framework.configuration.ZLinkFlowOrigin flowOrigin();
}
public final class systems.zlink.framework.configuration.ZLinkMessageFlowLogMode extends java.lang.Enum<systems.zlink.framework.configuration.ZLinkMessageFlowLogMode> {
  public static final systems.zlink.framework.configuration.ZLinkMessageFlowLogMode OFF;
  public static final systems.zlink.framework.configuration.ZLinkMessageFlowLogMode ERRORS_ONLY;
  public static final systems.zlink.framework.configuration.ZLinkMessageFlowLogMode KEY_TRANSITIONS;
  public static final systems.zlink.framework.configuration.ZLinkMessageFlowLogMode VERBOSE;
  public static final systems.zlink.framework.configuration.ZLinkMessageFlowLogMode DIAGNOSTIC;
  public static systems.zlink.framework.configuration.ZLinkMessageFlowLogMode[] values();
  public static systems.zlink.framework.configuration.ZLinkMessageFlowLogMode valueOf(java.lang.String);
  public int value();
}
public interface systems.zlink.framework.configuration.ZLinkMessageFlowObserver {
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> onMessageFlow(systems.zlink.framework.configuration.ZLinkMessageFlowEvent);
}
public interface systems.zlink.framework.configuration.ZLinkMetadataPolicyBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkMetadataPolicyBuilder allowSessionToActor(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkMetadataPolicyBuilder allowActorToSession(java.lang.String);
}
public interface systems.zlink.framework.configuration.ZLinkSpotPublisherConfig {
  public abstract int sendHighWaterMark();
  public abstract void setSendHighWaterMark(int);
  public abstract java.util.Optional<java.time.Duration> sendTimeout();
  public abstract void setSendTimeout(java.time.Duration);
  public abstract java.util.Optional<java.time.Duration> linger();
  public abstract void setLinger(java.time.Duration);
}
public interface systems.zlink.framework.configuration.ZLinkStreamCompressionBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkStreamCompressionBuilder useDefault();
  public abstract systems.zlink.framework.configuration.ZLinkStreamCompressionBuilder useLz4();
  public abstract systems.zlink.framework.configuration.ZLinkStreamCompressionBuilder use(systems.zlink.framework.streams.ZLinkStreamCompressionCodec);
  public abstract systems.zlink.framework.configuration.ZLinkStreamCompressionBuilder disable();
}
public interface systems.zlink.framework.configuration.ZLinkStreamNodeBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkStreamNodeBuilder bind(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkStreamNodeBuilder setTlsServer(java.lang.String, java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkStreamNodeBuilder setTlsServer(java.lang.String, java.lang.String, boolean);
  public abstract systems.zlink.framework.configuration.ZLinkStreamNodeBuilder registerSession(java.lang.Class<? extends systems.zlink.framework.streams.ZLinkSession>);
  public abstract systems.zlink.framework.configuration.ZLinkStreamNodeBuilder enableActorDispatch();
  public abstract systems.zlink.framework.configuration.ZLinkStreamNodeBuilder addSessionPacketHandler(java.lang.Class<?>);
}
public final class systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction extends java.lang.Enum<systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction> {
  public static final systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction REPLY_ERROR;
  public static final systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction LOG_AND_DROP;
  public static final systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction DROP;
  public static final systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction THROW;
  public static systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction[] values();
  public static systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction valueOf(java.lang.String);
  public int value();
}
public interface systems.zlink.framework.configuration.ZLinkUnhandledDispatchOptions {
  public abstract void setRequest(systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction);
  public abstract void setSend(systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction);
  public abstract void setPublish(systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction);
  public abstract void setSendLogLevel(systems.zlink.framework.configuration.ZLinkLogLevel);
  public abstract void setPublishLogLevel(systems.zlink.framework.configuration.ZLinkLogLevel);
}
public interface systems.zlink.framework.configuration.ZLinkWorkerOptions {
  public abstract systems.zlink.framework.configuration.ZLinkWorkerOptions minThreads(int);
  public abstract systems.zlink.framework.configuration.ZLinkWorkerOptions maxThreads(int);
  public abstract systems.zlink.framework.configuration.ZLinkWorkerOptions idleTimeout(java.time.Duration);
  public abstract systems.zlink.framework.configuration.ZLinkWorkerOptions maxQueueLength(int);
}
```

## 나머지 구성 public member `javap` inventory

아래 선언은 위 inventory에 포함하지 않은 application-facing configuration type을 `javap`가 출력하는
binary signature 형식으로 고정한다.

```java
public final class systems.zlink.framework.configuration.ZLinkDispatchErrorAction extends java.lang.Enum<systems.zlink.framework.configuration.ZLinkDispatchErrorAction> {
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorAction REPLY_ERROR;
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorAction DROP;
  public static systems.zlink.framework.configuration.ZLinkDispatchErrorAction[] values();
  public static systems.zlink.framework.configuration.ZLinkDispatchErrorAction valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.configuration.ZLinkDispatchErrorReason extends java.lang.Enum<systems.zlink.framework.configuration.ZLinkDispatchErrorReason> {
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorReason HANDLER_MISSING;
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorReason PAYLOAD_DECODE_FAILED;
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorReason HANDLER_EXCEPTION;
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorReason INVALID_FRAME;
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorReason REPLY_PATH_MISSING;
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorReason UNEXPECTED_REPLY;
  public static systems.zlink.framework.configuration.ZLinkDispatchErrorReason[] values();
  public static systems.zlink.framework.configuration.ZLinkDispatchErrorReason valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.configuration.ZLinkDispatchErrorSurface extends java.lang.Enum<systems.zlink.framework.configuration.ZLinkDispatchErrorSurface> {
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorSurface CHANNEL;
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorSurface ROUTE_MESH_CHANNEL;
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorSurface SPOT_ROUTE;
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorSurface SPOT_SUBSCRIPTION;
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorSurface SPOT_ACTOR;
  public static final systems.zlink.framework.configuration.ZLinkDispatchErrorSurface STREAM_SESSION;
  public static systems.zlink.framework.configuration.ZLinkDispatchErrorSurface[] values();
  public static systems.zlink.framework.configuration.ZLinkDispatchErrorSurface valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.configuration.ZLinkDispatchFailure extends java.lang.Record {
  public systems.zlink.framework.configuration.ZLinkDispatchFailure(systems.zlink.framework.configuration.ZLinkDispatchErrorSurface, systems.zlink.framework.configuration.ZLinkDispatchMessageKind, systems.zlink.framework.configuration.ZLinkDispatchErrorReason, systems.zlink.framework.configuration.ZLinkDispatchErrorAction, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String, java.lang.String);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.configuration.ZLinkDispatchErrorSurface surface();
  public systems.zlink.framework.configuration.ZLinkDispatchMessageKind messageKind();
  public systems.zlink.framework.configuration.ZLinkDispatchErrorReason reason();
  public systems.zlink.framework.configuration.ZLinkDispatchErrorAction action();
  public java.lang.String packetName();
  public java.lang.String channelName();
  public java.lang.String topic();
  public java.lang.String spotRid();
  public java.lang.String actorId();
  public java.lang.String sourceRid();
  public java.lang.String correlationId();
  public java.lang.String errorType();
  public java.lang.String errorMessage();
}
public final class systems.zlink.framework.configuration.ZLinkDispatchMessageKind extends java.lang.Enum<systems.zlink.framework.configuration.ZLinkDispatchMessageKind> {
  public static final systems.zlink.framework.configuration.ZLinkDispatchMessageKind REQUEST;
  public static final systems.zlink.framework.configuration.ZLinkDispatchMessageKind SEND;
  public static final systems.zlink.framework.configuration.ZLinkDispatchMessageKind PUBLISH;
  public static final systems.zlink.framework.configuration.ZLinkDispatchMessageKind RESPONSE;
  public static final systems.zlink.framework.configuration.ZLinkDispatchMessageKind ERROR;
  public static final systems.zlink.framework.configuration.ZLinkDispatchMessageKind ACTOR_REQUEST;
  public static final systems.zlink.framework.configuration.ZLinkDispatchMessageKind ACTOR_SEND;
  public static systems.zlink.framework.configuration.ZLinkDispatchMessageKind[] values();
  public static systems.zlink.framework.configuration.ZLinkDispatchMessageKind valueOf(java.lang.String);
  public int value();
}
public interface systems.zlink.framework.configuration.ZLinkFrameworkOptions {
  public abstract java.time.Duration defaultRequestTimeout();
  public abstract void setDefaultRequestTimeout(java.time.Duration);
  public abstract systems.zlink.framework.configuration.ZLinkCodecRegistryBuilder codecs();
  public abstract void addHandlersFromPackageOf(java.lang.Class<?>);
  public abstract systems.zlink.framework.configuration.ZLinkMetadataPolicyBuilder configureMetadata();
  public abstract void addRelocationStore(systems.zlink.framework.locations.ZLinkRelocationStore);
  public abstract void setApplicationVersion(long);
  public abstract void setMaintenanceWave(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder addRouteMesh(java.lang.String);
  public abstract systems.zlink.framework.configuration.ClientServerChannelBuilder addClientServerChannel(java.lang.String);
  public abstract systems.zlink.framework.configuration.FanoutChannelBuilder addFanoutChannel(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkStreamNodeBuilder addStreamNode(java.lang.String);
  public abstract void addLocationStore(systems.zlink.framework.locations.ZLinkLocationStore);
  public abstract systems.zlink.framework.locations.ZLinkLocationOptions configureLocations();
  public abstract systems.zlink.framework.configuration.ZLinkNetworkOptions configureNetwork();
  public abstract void useFilter(java.lang.Class<? extends systems.zlink.framework.ZLinkHandlerFilter>);
  public abstract systems.zlink.framework.configuration.ZLinkDispatchOptions configureDispatch();
  public abstract systems.zlink.framework.configuration.ZLinkStreamCompressionBuilder configureStreamCompression();
  public abstract systems.zlink.framework.configuration.ZLinkWorkerOptions configureWorkers();
  public abstract void useVirtualThreadHandlers();
  public abstract void useHandlerExecutor(java.util.concurrent.Executor);
}
public final class systems.zlink.framework.configuration.ZLinkLogLevel extends java.lang.Enum<systems.zlink.framework.configuration.ZLinkLogLevel> {
  public static final systems.zlink.framework.configuration.ZLinkLogLevel TRACE;
  public static final systems.zlink.framework.configuration.ZLinkLogLevel DEBUG;
  public static final systems.zlink.framework.configuration.ZLinkLogLevel INFO;
  public static final systems.zlink.framework.configuration.ZLinkLogLevel WARN;
  public static final systems.zlink.framework.configuration.ZLinkLogLevel ERROR;
  public static systems.zlink.framework.configuration.ZLinkLogLevel[] values();
  public static systems.zlink.framework.configuration.ZLinkLogLevel valueOf(java.lang.String);
  public int value();
}
public interface systems.zlink.framework.configuration.ZLinkMeshNodeBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkMeshChannelBuilder channel(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder listen(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder listen();
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder listen(int);
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder setBindHost(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder setAdvertiseHost(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder setRoutingId(systems.zlink.contracts.core.RoutingId);
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder setRoutingIdPrefix(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder setPlacementWeight(int);
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder setObjectCapacity(int, int);
  public abstract systems.zlink.framework.configuration.ZLinkMeshObjectRoleBuilder objects();
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeSocketConfig configureRouterSocket();
  public abstract systems.zlink.framework.configuration.ZLinkSpotPublisherConfig configureSpotPublisher();
  public abstract systems.zlink.framework.configuration.ZLinkMeshPeerConnections peerConnections();
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder setDefaultRequestTimeout(java.time.Duration);
  public abstract <THandler extends systems.zlink.framework.channels.ZLinkRouteSendHandler<TMessage>, TMessage> systems.zlink.framework.configuration.ZLinkMeshNodeBuilder addRouteSendHandler(java.lang.Class<THandler>, java.lang.Class<TMessage>);
  public abstract <THandler extends systems.zlink.framework.channels.ZLinkRouteRequestHandler<TRequest, TReply>, TRequest, TReply> systems.zlink.framework.configuration.ZLinkMeshNodeBuilder addRouteRequestHandler(java.lang.Class<THandler>, java.lang.Class<TRequest>, java.lang.Class<TReply>);
}
public interface systems.zlink.framework.configuration.ZLinkMeshObjectRoleBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkMeshObjectClientBuilder client();
  public abstract systems.zlink.framework.configuration.ZLinkMeshObjectServerBuilder server();
}
public interface systems.zlink.framework.configuration.ZLinkMeshObjectClientBuilder {
}
public interface systems.zlink.framework.configuration.ZLinkMeshObjectServerBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkMeshObjectServerBuilder addEntrySpot(java.lang.Class<? extends systems.zlink.framework.spots.ZLinkEntrySpot<?>>);
  public abstract <TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>> systems.zlink.framework.configuration.ZLinkMeshObjectServerBuilder addSpotFactory(java.lang.String, java.lang.Class<TSpot>, systems.zlink.framework.configuration.ZLinkObjectPlacementOptions, systems.zlink.framework.actors.ZLinkRelocationPolicy<TSpot>);
  public abstract <TSpot extends systems.zlink.framework.spots.ZLinkInstanceSpot> systems.zlink.framework.configuration.ZLinkMeshObjectServerBuilder addInstanceSpotFactory(java.lang.String, java.lang.Class<TSpot>, systems.zlink.framework.configuration.ZLinkObjectPlacementOptions, systems.zlink.framework.actors.ZLinkRelocationPolicy<TSpot>);
  public abstract <TActor extends systems.zlink.framework.actors.ZLinkActor> systems.zlink.framework.configuration.ZLinkMeshObjectServerBuilder addActorFactory(java.lang.String, java.lang.Class<TActor>, java.lang.Class<? extends systems.zlink.framework.actors.ZLinkActorFactory>, systems.zlink.framework.configuration.ZLinkObjectPlacementOptions, systems.zlink.framework.actors.ZLinkRelocationPolicy<TActor>);
}
public final class systems.zlink.framework.configuration.ZLinkObjectPlacementOptions extends java.lang.Record {
  public systems.zlink.framework.configuration.ZLinkObjectPlacementOptions(java.util.Set<java.lang.String>, java.lang.Integer, java.lang.Integer);
  public java.util.Set<java.lang.String> placementProfiles();
  public java.lang.Integer maxActiveObjects();
  public java.lang.Integer maxPendingActivations();
}
public final class systems.zlink.framework.configuration.ZLinkMessageFlowOutcome extends java.lang.Enum<systems.zlink.framework.configuration.ZLinkMessageFlowOutcome> {
  public static final systems.zlink.framework.configuration.ZLinkMessageFlowOutcome RECEIVED;
  public static final systems.zlink.framework.configuration.ZLinkMessageFlowOutcome DISPATCHED;
  public static final systems.zlink.framework.configuration.ZLinkMessageFlowOutcome REPLIED;
  public static final systems.zlink.framework.configuration.ZLinkMessageFlowOutcome DROPPED;
  public static final systems.zlink.framework.configuration.ZLinkMessageFlowOutcome SENT;
  public static final systems.zlink.framework.configuration.ZLinkMessageFlowOutcome REPLY_RECEIVED;
  public static final systems.zlink.framework.configuration.ZLinkMessageFlowOutcome ERROR;
  public static systems.zlink.framework.configuration.ZLinkMessageFlowOutcome[] values();
  public static systems.zlink.framework.configuration.ZLinkMessageFlowOutcome valueOf(java.lang.String);
  public int value();
}
public interface systems.zlink.framework.spring.EnableZLinkFramework extends java.lang.annotation.Annotation {
}
public interface systems.zlink.framework.spring.ZLinkMetricsCustomizer {
  public abstract void customize(io.micrometer.core.instrument.MeterRegistry);
}
```

## Spring bean 계약

Spring starter는 `ZLinkFrameworkRuntime`, `ZLinkRouteMeshRuntime`, `ZLinkClientServerRuntime`과
`ZLinkFanoutRuntime`을 singleton bean으로 제공한다. 세 topology bean은 runtime의 대응 accessor가 반환한
객체를 그대로 등록하므로 새 adapter나 별도 runtime을 만들지 않는다. Public client와 나머지 runtime service
bean도 같은 `ZLinkFrameworkRuntime`이 소유한 객체를 사용한다.

Bean을 생성하는 동안 service socket, discovery loop와 application worker를 시작하지 않는다.
`SmartLifecycle.start()`가 같은 runtime의 start를 한 번 호출한다. Application에 보장하는 계약은 public bean의
type, singleton 수명과 reference identity다. Auto-configuration class, bean factory method, lifecycle adapter와
그 constructor는 implementation detail이며 application public signature가 아니다.

Contract test는 네 runtime bean을 각각 두 번 resolve해 singleton인지 확인한다. 이어서 세 topology bean을
`runtime.routeMeshRuntime()`, `runtime.clientServerRuntime()`과 `runtime.fanoutRuntime()`의 반환값과
`assertSame`으로 비교한다. Lifecycle 회귀 test는 bean 생성 전후에 service socket이 시작되지 않고, start와
shutdown이 같은 `ZLinkFrameworkRuntime`에 한 번씩 전달되는지 확인한다.
