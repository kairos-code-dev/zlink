# .NET RouteMesh·MeshNode 공개 인터페이스

[언어별 인터페이스 목차](README.ko.md) · [공통 topology](../../10-channel-topology.ko.md) ·
[MeshNode](../../21-mesh-node.ko.md) · [메시지 모델](../../../03-message-model.ko.md)

## 1. 범위

이 문서는 ZLink Framework 10.0.0의 .NET RouteMesh·MeshNode 공개 인터페이스를 고정한다. 대상 독자는
.NET framework와 bindings 구현자다. 물리 mesh 등록, 논리 channel membership, manual peer, handler,
Spot·Actor 등록과 실행 중 weight 변경의 정확한 C# signature를 이 문서가 소유한다.

## 2. 등록 인터페이스

```csharp
public interface IZLinkFrameworkOptions
{
    TimeSpan DefaultRequestTimeout { get; set; }
    TimeSpan? ActorTransferTimeout { get; set; }
    TimeSpan? ActorTransferForwardWindow { get; set; }
    TimeSpan DefaultSocketSendTimeout { get; set; }
    IZLinkCodecRegistryBuilder Codecs { get; }
    IZLinkWorkerOptions Worker { get; }

    void AddHandlersFromAssemblyOf<TMarker>();
    void AddHandlersFromAssemblyOf(Type markerType);
    void AddHandlersFromAssembly(System.Reflection.Assembly assembly);
    void DisableImplicitHandlerAutoRegistration();
    IZLinkMetadataPolicyBuilder ConfigureMetadata();
    void AddLocationStore(IZLinkLocationStore store);
    ZLinkLocationOptions ConfigureLocations();
    IZLinkDispatchOptions ConfigureDispatch();
    void UseFilter<TFilter>() where TFilter : class, IZLinkHandlerFilter;

    IZLinkMeshNodeBuilder AddRouteMesh(string meshName);
    IZLinkFanoutChannelBuilder AddFanoutChannel(string channelName);
    IZLinkStreamNodeBuilder AddStreamNode(string streamNodeName);
}

public interface IZLinkMeshNodeBuilder
{
    IZLinkMeshChannelBuilder ChannelName(string channelName);
    IZLinkMeshNodeBuilder Listen(string endpoint);
    IZLinkMeshNodeBuilder SetRoutingId(RoutingId routingId);
    IZLinkMeshNodeBuilder UseAllocatedRoutingId(int slotCount);
    IZLinkMeshNodeBuilder UseAllocatedRoutingId(
        int slotCount,
        string routingIdPrefix);
    IZLinkMeshNodeBuilder SetRoutingIdAllocationGroup(string groupName);
    IZLinkMeshNodeSocketConfig ConfigureRouterSocket();
    IZLinkSpotPublisherConfig ConfigureSpotPublisher();
    IZLinkMeshNodeBuilder UseDrainPolicy(ZLinkMeshNodeDrainPolicy policy);
    IZLinkMeshPeerConnections PeerConnections { get; }

    IZLinkMeshNodeBuilder SetDefaultRequestTimeout(TimeSpan timeout);
    IZLinkMeshNodeBuilder AddRouteSendHandler<THandler, TMessage>(
        string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>;
    IZLinkMeshNodeBuilder AddRouteSendHandler<THandler>(string? packetName = null)
        where THandler : class;
    IZLinkMeshNodeBuilder AddRouteRequestHandler<THandler, TRequest, TReply>(
        string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>;
    IZLinkMeshNodeBuilder AddRouteRequestHandler<THandler>(string? packetName = null)
        where THandler : class;

    IZLinkEntrySpotOptions ConfigureEntrySpot();
    IZLinkMeshNodeBuilder SetEntrySpotRoutingId(RoutingId routingId);
    IZLinkMeshNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot;
    IZLinkMeshNodeBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot;
    IZLinkMeshNodeBuilder AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory;
    IZLinkMeshNodeBuilder AddActorTransferAdapter<TActor, TAdapter>(string actorType)
        where TActor : IZLinkActor
        where TAdapter : class, IZLinkActorTransferAdapter<TActor>;
}

public enum ZLinkMeshNodeDrainPolicy
{
    DrainNatural = 0,
    ReleaseAndRecreate = 1
}

public interface IZLinkFanoutChannelBuilder
{
    IZLinkFanoutChannelBuilder EnablePublisher(string endpoint);
    IZLinkFanoutChannelBuilder ConnectSubscriber(string endpoint);
    IZLinkFanoutChannelBuilder AddHandler<THandler, TEvent>(
        string? packetName = null)
        where THandler : class, IZLinkFanoutHandler<TEvent>;
}

public interface IZLinkStreamNodeBuilder
{
    IZLinkStreamNodeBuilder Bind(string endpoint);
    IZLinkStreamNodeBuilder EnableActorDispatch(string meshName);
    IZLinkStreamNodeBuilder SetTlsServer(
        string certificatePath,
        string keyPath,
        bool requireClientCertificate = false);
    IZLinkStreamNodeBuilder AddSession<TSession>()
        where TSession : class, IZLinkSession;
}

public interface IZLinkCodecRegistryBuilder
{
    void Add<TCodec>() where TCodec : class, IZLinkMessageCodec;
}

public interface IZLinkMetadataPolicyBuilder
{
    IZLinkMetadataPolicyBuilder AllowSessionToActor(string key);
    IZLinkMetadataPolicyBuilder AllowActorToSession(string key);
}

```

`AddRouteMesh(meshName)`은 process-local MeshNode 하나를 등록한다. 같은 process에서 같은 `meshName`을
두 번 등록하면 host startup이 `ZLinkConfigurationException`으로 실패한다. `ChannelName(...)`은 별도
socket을 만들지 않고 immutable membership과 handler namespace를 추가한다. 하나 이상의 channel을
등록해야 한다.

`AddHandlersFromAssemblyOf(...)`와 `AddHandlersFromAssembly(...)`는 명시한 assembly만 handler scan 범위로
추가한다. Scan에 사용하는 method, group과 packet attribute의 정확한 선언은
[02 handler와 client §2](02-handler-interfaces.ko.md#2-공통-metadata와-call)가 소유한다.

`EnableActorDispatch(meshName)`은 STREAM node가 session Actor dispatch에 사용할 MeshName 하나를 고정한다.
같은 builder에서 두 번 호출하거나 등록되지 않은 MeshName을 지정하면 startup이 실패한다. Actor dispatch를
사용하지 않는 STREAM node는 이 메서드를 호출하지 않아도 된다. Session의 `BindAsync(ActorRef)`는 이
MeshName context를 사용하므로 호출마다 mesh 이름을 받지 않는다.

`DefaultRequestTimeout`의 기본값은 30초, `DefaultSocketSendTimeout`의 기본값은 1초다.
`ActorTransferTimeout`과 `ActorTransferForwardWindow`는 기본값이 `null`이며 transfer adapter를 하나라도
등록하면 startup 전에 둘 다 양수로 설정해야 한다. `Worker`는 bounded worker scheduler의 최소·최대 thread
수, idle timeout과 queue 상한을 host startup 전에 설정한다. Core claim batch 크기는 public 설정으로
노출하지 않는다.

## 3. Manual peer

```csharp
public readonly record struct ZLinkMeshPeerConnection(
    string Endpoint,
    RoutingId? ExpectedRoutingId);

public interface IZLinkMeshPeerConnections
{
    void Connect(string endpoint);
    void Connect(RoutingId expectedRoutingId, string endpoint);
    void Disconnect(string endpoint);
    IReadOnlyList<ZLinkMeshPeerConnection> ListConnections();
}
```

expected RID를 생략하면 admission handshake가 remote identity를 결정한다. expected RID를 지정한 경우
handshake identity가 다르면 연결을 admission하지 않는다. Manual 연결도 자동 discovery 연결과 같은
MeshName·RID·ChannelName·security 검증을 사용한다.

## 4. Channel handler scope

```csharp
public interface IZLinkMeshChannelBuilder
{
    IZLinkMeshChannelBuilder SetWeight(int weight);
    IZLinkMeshChannelBuilder AddSendHandler<THandler, TMessage>(
        string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;
    IZLinkMeshChannelBuilder AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;
    IZLinkMeshChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(
        string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;
    IZLinkMeshChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;
}
```

channel handler는 `(MeshName, ChannelName, message kind, packet name)`으로 구분한다. RID direct route
handler는 MeshNode builder에 등록하며 source RID를 제공하는 route handler context를 사용한다. 같은 key의
중복 등록은 startup 오류이고, 서로 다른 channel이나 route family에 같은 packet name을 등록할 수 있다.

weight는 0부터 100까지다. 0은 해당 channel의 새 select-one과 Logical Multicast remote target에서만
제외한다. RID direct route, 다른 membership과 이미 제출한 operation에는 영향을 주지 않는다.

## 5. Publisher와 runtime option

```csharp
public interface IZLinkSpotPublisherConfig
{
    bool NoDrop { get; set; }
}

public interface IZLinkRouteMeshRuntimeOptions
{
    IZLinkMeshNodeRuntimeOptions MeshNode(string meshName);
    IZLinkMeshChannelRuntimeOptions Channel(
        string meshName,
        string channelName);
}

public interface IZLinkMeshNodeRuntimeOptions
{
    long MaxMessageSize { get; set; }
}

public interface IZLinkMeshChannelRuntimeOptions
{
    int Weight { get; set; }
}

public interface IZLinkMeshNodeSocketConfig
{
    long MaxMessageSize { get; set; }
    int SendHighWaterMark { get; set; }
    int ReceiveHighWaterMark { get; set; }
    TimeSpan? ReceiveTimeout { get; set; }
    TimeSpan? SendTimeout { get; set; }
}
```

`ConfigureSpotPublisher().NoDrop`의 기본값은 `true`다. `true`이면 local queue와 모든 remote pipe에 대한
원자적 admission이 성공해야 publish한다. blocking 호출은 MeshNode send timeout까지 기다리고,
non-blocking 호출은 즉시 backpressure 결과를 반환한다. `false`이면 막힌 대상만 drop할 수 있다.

`IZLinkRouteMeshRuntimeOptions`는 public DI singleton이다. 등록되지 않은 mesh 또는 membership을 조회하면
`ZLinkConfigurationException`이다. 실행 중에는 `MeshNode(meshName).MaxMessageSize`와
`Channel(meshName, channelName).Weight`만 변경할 수 있다. HWM과 timeout은
`ConfigureRouterSocket()`에서 startup 전에 설정한다. MeshNode가 지원하지 않는 raw ROUTER option을 이
interface에 노출하지 않는다.

`MaxMessageSize = 0`은 Framework가 별도 상한을 적용하지 않는 값이다. .NET adapter는 이를 Core의
`ZLINK_OPT_MAXMSGSIZE = -1`로 변환한다. 양수는 같은 byte 상한으로 전달하며 음수는
`ZLinkConfigurationException`으로 거부한다.

## 6. 메시징 metadata

Node direct, ChannelName, Spot direct, Actor send/request와 Logical Multicast call builder는 다음 overload를 공통으로 가진다.
handler context는 변경할 수 없는 `ZLinkMessageMetadata` snapshot을 제공한다.

```csharp
public interface IZLinkMetadataCall<TSelf>
{
    TSelf Metadata(string key, string value);
    TSelf Metadata(ZLinkMessageMetadata metadata);
}
```

같은 key를 여러 번 설정하면 마지막 값이 전송된다. metadata 전체의 UTF-8 encoded 크기는 1024 bytes를
넘을 수 없다. reply는 request metadata를 자동 복사하지 않으며 일반 reply에는 metadata setter를 두지
않는다. STREAM session과 Actor relay에 적용할 allowlist는 root `ConfigureMetadata()`가 소유한다.

## 7. Location store와 startup

자동 discovery, 분산 Spot·Actor 주소 또는 Actor transfer를 사용하는 host는 location store를 명시적으로
등록해야 한다. 공식 Redis location store package가 production 기본 구현이다. 등록이 없으면 host startup이
실패한다. process-local in-memory 구현은 단일 process contract test에서만 등록할 수 있다.
정확한 store capability와 Redis 생성자·option은
[.NET Location Store·Redis](06-location-store.ko.md)가 소유한다.

## 8. Runtime snapshot, event와 drain

```csharp
public enum ZLinkMeshNodeState
{
    Starting = 0,
    Serving = 1,
    Draining = 2,
    Drained = 3,
    ForceStopping = 4,
    Stopped = 5,
    Faulted = 6
}

public sealed record ZLinkMeshPeerSnapshot(
    RoutingId Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    string AdmissionState,
    bool Ready,
    string DrainState,
    IReadOnlyList<string> ChannelNames,
    string? LastFailure);

public sealed record ZLinkMeshChannelSnapshot(
    string ChannelName,
    int LocalWeight,
    int ReadyMemberCount,
    bool Selectable);

public sealed record ZLinkLogicalMulticastSnapshot(
    bool NoDrop,
    ulong Submitted,
    ulong Backpressured,
    ulong Dropped,
    ulong RemoteSnapshotCount,
    ulong RemoteAdmittedCount,
    ulong RemoteDroppedCount,
    ulong LocalSnapshotCount,
    ulong LocalAdmittedCount,
    ulong LocalDroppedCount,
    ulong PendingAdmissionCount);

public sealed record ZLinkMeshClaimSnapshot(
    bool ApplicationActive,
    ulong PendingApplicationWork,
    bool InfrastructureActive,
    ulong PendingInfrastructureWork);

public sealed record ZLinkMeshDrainSnapshot(
    ZLinkMeshNodeState State,
    DateTimeOffset? Deadline,
    bool WorkSealed,
    ulong PendingRequestCount,
    ulong PendingTransferCount,
    ulong PendingStreamBarrierCount);

public sealed record ZLinkLocationRuntimeSnapshot(
    string State,
    DateTimeOffset? LastSuccessAt,
    DateTimeOffset? LastFailureAt);

public sealed record ZLinkMeshNodeSnapshot(
    string MeshName,
    RoutingId Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    ZLinkMeshNodeState State,
    ulong Sequence,
    DateTimeOffset ObservedAt,
    IReadOnlyList<string> DescriptorSources,
    IReadOnlyList<ZLinkMeshPeerSnapshot> Peers,
    IReadOnlyList<ZLinkMeshChannelSnapshot> Channels,
    ZLinkLogicalMulticastSnapshot Multicast,
    ZLinkMeshClaimSnapshot Claims,
    ZLinkLocationRuntimeSnapshot Location,
    ZLinkMeshDrainSnapshot Drain);

public sealed record ZLinkMeshRuntimeEvent(
    string Identifier,
    ulong Sequence,
    DateTimeOffset Timestamp,
    string MeshName,
    RoutingId SourceRid,
    RoutingId? PeerRid,
    ulong? LifecycleGeneration,
    ulong? DescriptorRevision,
    string? ChannelName,
    string? ClaimDomain,
    string? MessageKind,
    ulong? RemoteSnapshotCount,
    ulong? RemoteAdmittedCount,
    ulong? RemoteDroppedCount,
    ulong? LocalSnapshotCount,
    ulong? LocalAdmittedCount,
    ulong? LocalDroppedCount,
    string? Reason,
    ZLinkMeshNodeState? State);

public abstract record ZLinkMeshDrainResult
{
    private protected ZLinkMeshDrainResult() { }
    public sealed record Drained : ZLinkMeshDrainResult;
    public sealed record ForceStopped(string Reason) : ZLinkMeshDrainResult;
}

public interface IZLinkRouteMeshRuntime
{
    ZLinkMeshNodeSnapshot Snapshot(string meshName);
    IAsyncEnumerable<ZLinkMeshRuntimeEvent> ObserveAsync(
        string meshName,
        int capacity = 1024,
        CancellationToken cancellationToken = default);
    bool IsReady(string meshName);
    ValueTask<ZLinkMeshDrainResult> DrainAsync(
        string meshName,
        TimeSpan? deadline = null,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkMeshDrainResult> AwaitDrainedAsync(
        string meshName,
        CancellationToken cancellationToken = default);
}
```

`DrainAsync`의 `deadline == null`은 30초다. 첫 호출이 shared drain deadline을 고정하며 후속 호출과
`AwaitDrainedAsync`는 같은 terminal result를 기다린다. Cancellation은 해당 waiter만 끝내고 shared drain을
취소하지 않는다. Event identifier와 조건부 field의 닫힌 값은
[Runtime monitoring](../../50-runtime-monitoring.ko.md), terminal result의 reason은
[Graceful drain](../../54-graceful-drain-handoff.ko.md)이 소유한다.

## 9. Message flow와 metric

```csharp
public enum ZLinkMessageFlowMode
{
    Off = 0,
    ErrorsOnly = 1,
    KeyTransitions = 2,
    Verbose = 3
}

public sealed record ZLinkMessageFlowEvent(
    string EventId,
    DateTimeOffset Timestamp,
    string? Phase,
    string Surface,
    string MessageKind,
    string Outcome,
    string? Reason,
    string? Action,
    string? MeshName,
    string? ChannelName,
    RoutingId? SourceRid,
    RoutingId? TargetRid,
    string? PacketName,
    string? Topic,
    RoutingId? SpotRid,
    string? ActorId,
    string? CorrelationId,
    string? FlowId,
    string? FlowOrigin,
    ulong? RemoteSnapshotCount,
    ulong? RemoteAdmittedCount,
    ulong? RemoteDroppedCount,
    ulong? LocalSnapshotCount,
    ulong? LocalAdmittedCount,
    ulong? LocalDroppedCount,
    ulong? TargetCount,
    ulong? DropCount,
    long? MessageSizeBytes,
    double? DurationSeconds);

public sealed record ZLinkRuntimeErrorEvent(
    string EventId,
    DateTimeOffset Timestamp,
    string Kind,
    string Source,
    string Reason);

public interface IZLinkMessageFlowObserver
{
    ValueTask OnMessageFlowAsync(
        ZLinkMessageFlowEvent flow,
        CancellationToken cancellationToken);
}

public interface IZLinkRuntimeErrorSink
{
    ValueTask OnRuntimeErrorAsync(
        ZLinkRuntimeErrorEvent error,
        CancellationToken cancellationToken);
}

public interface IZLinkDispatchOptions
{
    IZLinkDispatchOptions SetMessageFlowObserver<TObserver>()
        where TObserver : class, IZLinkMessageFlowObserver;
    IZLinkDispatchOptions SetMessageFlowObserver(IZLinkMessageFlowObserver observer);
    IZLinkDispatchOptions SetRuntimeErrorSink<TSink>()
        where TSink : class, IZLinkRuntimeErrorSink;
    IZLinkDispatchOptions SetRuntimeErrorSink(IZLinkRuntimeErrorSink sink);
    IZLinkDispatchOptions MessageFlow(ZLinkMessageFlowMode mode);
}

public interface IZLinkMessageFlowRuntime
{
    ZLinkMessageFlowMode Mode { get; set; }
    IAsyncEnumerable<ZLinkMessageFlowEvent> ObserveAsync(
        int capacity = 1024,
        CancellationToken cancellationToken = default);
}
```

Message flow의 기본 mode는 `ErrorsOnly`이며 실행 중 변경할 수 있다. Observer는 immutable event를 받고
dispatch 결정에 참여하지 않는다. `EventId == "zlink.dispatch_error"`일 때 `Outcome`은
`"failed"`이고 `Reason`과 `Action`이 모두 존재한다. runtime error sink는 observer 실패를
`EventId == "zlink.runtime_error"`, `Kind == "observer_failed"`, `Source == "message_flow_observer"`로
받는다. 두 event에 exception object를 포함하지 않으며 sink 실패는 다시 sink를 호출하지
않는다. 닫힌 문자열 값과 조건부 field 규칙은 [Message flow](../../52-message-flow-tracing.ko.md)이
소유한다. Metric은 `System.Diagnostics.Metrics`의 `Meter` 이름
`zlink.framework`로 제공하고, 계기 이름·단위·label은
[Runtime metrics](../../51-runtime-metrics.ko.md)의 공통 계약을 그대로 사용한다.

## 10. 예제

```csharp
services.AddZLinkFramework(options =>
{
    var mesh = options.AddRouteMesh("world")
        .Listen("tcp://0.0.0.0:7300") // MeshNode의 유일한 ROUTER endpoint를 설정한다.
        .SetRoutingId(gameNodeRid)
        .AddSpotFactory<StageSpot>(); // Spot과 Actor 등록도 같은 MeshNode가 소유한다.

    mesh.ChannelName("game")
        .AddSendHandler<GameCommandHandler, GameCommand>(); // logical channel handler를 등록한다.
    mesh.ChannelName("actors"); // 같은 MeshNode의 두 번째 immutable membership이다.

    mesh.ConfigureSpotPublisher().NoDrop = true; // 기본 publish admission 정책이다.

    options.AddFanoutChannel("events")
        .EnablePublisher("tcp://0.0.0.0:7400"); // classic PUB/SUB는 독립 fanout 기능이다.
});
```
