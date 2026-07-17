# .NET handler, context와 messaging client 공개 계약

[.NET 계약 목차](README.ko.md) · [RouteMesh·MeshNode](05-route-mesh.ko.md) ·
[공통 상호작용 모델](../../../02-interaction-model.ko.md)

## 1. 범위

이 문서는 ZLink Framework 10.0.0의 Node direct, ChannelName, Spot, Actor와 STREAM session handler,
context와 client의 정확한 C# 시그니처를 고정한다. RouteMesh builder, ChannelName membership, manual peer와
runtime option은 [05 RouteMesh·MeshNode](05-route-mesh.ko.md)가 소유하며 이 문서에서 재선언하지 않는다.

## 2. 공통 metadata와 call

Handler metadata는 변경할 수 없는 snapshot이다.

```csharp
public sealed class ZLinkMessage
{
    public static ZLinkMessage Empty { get; }
    public string? ContentType { get; }
    public bool IsEmpty { get; }
    public static ZLinkMessage From<T>(T value);
    public T Decode<T>();
}

public sealed class ZLinkMessageMetadata
{
    public static ZLinkMessageMetadata Empty { get; }
    public IReadOnlyDictionary<string, string> Values { get; }
    public string? Find(string key);
}

public interface IZLinkSendCall : IZLinkMetadataCall<IZLinkSendCall>
{
    ZLinkSubmitResult TrySubmit();
    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkRequestCall : IZLinkMetadataCall<IZLinkRequestCall>
{
    IZLinkRequestCall Timeout(TimeSpan timeout);
    ValueTask<TReply> Async<TReply>(
        CancellationToken cancellationToken = default);
    ValueTask<TReply> Yield<TReply>(
        CancellationToken cancellationToken = default);
}

public interface IZLinkPublishCall : IZLinkMetadataCall<IZLinkPublishCall>
{
    ZLinkPublishResult TrySubmit();
    ValueTask<ZLinkPublishResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public enum ZLinkSubmitStatus
{
    Submitted = 0,
    Backpressured = 1,
    TimedOut = 2,
    TargetNotFound = 3,
    RouteNotConnected = 4,
    Shutdown = 5
}

public readonly record struct ZLinkSubmitResult(ZLinkSubmitStatus Status);

public readonly record struct ZLinkLogicalMulticastDetail(
    ulong SnapshotRemoteNodeCount,
    ulong AdmittedRemoteNodeCount,
    ulong DroppedRemoteNodeCount,
    ulong SnapshotLocalSpotCount,
    ulong AdmittedLocalSpotCount,
    ulong DroppedLocalSpotCount);

public readonly record struct ZLinkPublishResult(
    ZLinkSubmitStatus Status,
    ZLinkLogicalMulticastDetail Detail);

public interface IZLinkWorkerCall<TResult>
{
    IZLinkWorkerCall<TResult> Timeout(TimeSpan timeout);
    void Submit(CancellationToken cancellationToken = default);
    ValueTask<TResult> Async(CancellationToken cancellationToken = default);
    ValueTask<TResult> Yield(CancellationToken cancellationToken = default);
}

public interface IZLinkWorkerOptions
{
    int MinThreads { get; set; }
    int MaxThreads { get; set; }
    TimeSpan IdleTimeout { get; set; }
    int MaxQueueLength { get; set; }
}
```

`TrySubmit()`은 기다리지 않고 admission 결과를 반환한다. `SubmitAsync()`는 MeshNode send timeout까지
admission을 기다리며 원격 handler 완료를 기다리지 않는다. `IZLinkMetadataCall<TSelf>`의 정확한
시그니처와 1024-byte 상한은
[05 RouteMesh·MeshNode §6](05-route-mesh.ko.md#6-메시징-metadata)이 소유한다. 같은 key를 여러 번 설정하면
마지막 값이 전송된다. Reply는 request metadata를 자동 복사하지 않는다.

Worker call의 `Submit`, `Async`와 `Yield`는
[비동기 실행 정책 §1.2](../../../04-async-execution-policy.ko.md#12-worker-offload)의 완료 의미를 따른다.
Worker option은 host가 시작되기 전에만 설정할 수 있다.

Assembly scan에서 사용하는 최소 attribute 표면은 다음과 같다.

```csharp
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true)]
public sealed class ZLinkHandlerGroupAttribute(string groupName) : Attribute
{
    public string GroupName { get; } = groupName;
}

[AttributeUsage(AttributeTargets.Method)]
public sealed class ZLinkRequestAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Method)]
public sealed class ZLinkSendAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Method)]
public sealed class ZLinkPublishAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
public sealed class ZLinkPacketAttribute(string packetName) : Attribute
{
    public string PacketName { get; } = packetName;
}
```

Root에 등록한 assembly에서 method attribute를 찾으며, `ZLinkHandlerGroupAttribute`는 해당 handler가
참여하는 handler group을 지정한다. Method의 `PacketName`을 생략하면 message type의
`ZLinkPacketAttribute`를 확인하고, 그것도 없으면 type 이름을 사용한다. Packet name은 등록할 때 한 번
결정되며 codec 선택으로 바뀌지 않는다.

## 3. Node direct와 ChannelName

Node direct와 ChannelName은 서로 다른 handler family를 사용한다. Node direct context는 source RID를,
ChannelName context는 logical membership을 제공한다.

```csharp
public interface IZLinkHandlerContext
{
    string MeshName { get; }
    string? ChannelName { get; }
    string PacketName { get; }
    string? ContentType { get; }
    ZLinkMessageMetadata Metadata { get; }
    CancellationToken ConnectionAborted { get; }
}

public sealed class ZLinkSendContext : IZLinkHandlerContext
{
    public string MeshName { get; }
    public string ChannelName { get; }
    public string PacketName { get; }
    public string? ContentType { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public CancellationToken ConnectionAborted { get; }
}

public sealed class ZLinkRequestContext : IZLinkHandlerContext
{
    public string MeshName { get; }
    public string ChannelName { get; }
    public string PacketName { get; }
    public string? ContentType { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public CancellationToken ConnectionAborted { get; }
}

public sealed class ZLinkRouteSendContext : IZLinkHandlerContext
{
    public string MeshName { get; }
    public string? ChannelName { get; }
    public string PacketName { get; }
    public string? ContentType { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public CancellationToken ConnectionAborted { get; }
    public RoutingId SourceNodeRid { get; }
}

public sealed class ZLinkRouteRequestContext : IZLinkHandlerContext
{
    public string MeshName { get; }
    public string? ChannelName { get; }
    public string PacketName { get; }
    public string? ContentType { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public CancellationToken ConnectionAborted { get; }
    public RoutingId SourceNodeRid { get; }
}

public interface IZLinkSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkSendContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkRequestHandler<in TRequest, TReply>
{
    ValueTask<TReply> HandleAsync(
        TRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkRouteSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkRouteSendContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkRouteRequestHandler<in TRequest, TReply>
{
    ValueTask<TReply> HandleAsync(
        TRequest request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken);
}
```

`IZLinkSendHandler`와 `IZLinkRequestHandler`는 `ChannelName(...)` builder에 등록한다.
`IZLinkRouteSendHandler`와 `IZLinkRouteRequestHandler`는 MeshNode builder에 등록한다. 같은 packet name을
두 family에 등록할 수 있으며 각 family 안의 중복 key는 startup 오류다.

Global DI client는 MeshName을 명시한다. Channel operation은 MeshName과 ChannelName을 함께 받아 서로
다른 physical mesh에서 같은 ChannelName을 사용할 수 있게 한다.

```csharp
public interface IZLinkRouteClient
{
    IZLinkSendCall SendToNode<TMessage>(
        string meshName,
        RoutingId targetNodeRid,
        TMessage message);

    IZLinkRequestCall RequestToNode<TRequest>(
        string meshName,
        RoutingId targetNodeRid,
        TRequest request);

    IZLinkSendCall SendToChannel<TMessage>(
        string meshName,
        string channelName,
        TMessage message);

    IZLinkRequestCall RequestToChannel<TRequest>(
        string meshName,
        string channelName,
        TRequest request);
}
```

Node direct는 target RID 하나로 submit한다. ChannelName operation은 ready positive-weight member 하나를
round-robin으로 선택하고 같은 operation에서 submit한다. Client는 선택된 RID를 반환하지 않는다.

Classic fanout handler는 독립 fanout channel에서 받은 typed event만 처리한다.

```csharp
public interface IZLinkFanoutHandler<in TEvent>
{
    ValueTask HandleAsync(
        TEvent message,
        CancellationToken cancellationToken);
}
```

## 4. Spot

Spot은 MeshNode가 소유하는 logical mailbox다. `SpotHandle`은 location snapshot과 generation을 보존하지만
application에 target node RID를 노출하지 않는다.

```csharp
public enum ZLinkSpotKind
{
    Invalid = 0,
    Entry = 1,
    User = 2
}

public abstract class SpotHandle
{
    public abstract string MeshName { get; }
    public abstract RoutingId SpotRid { get; }
}

public interface IZLinkSpot
{
    IZLinkSpotContext Context { get; }
    void Configure();
    ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken);
    ValueTask OnInitializeAsync(CancellationToken cancellationToken);
    ValueTask OnClosingAsync(CancellationToken cancellationToken);
}

public readonly record struct ZLinkSpotCreateResponse(
    bool Accepted,
    ZLinkMessage? Reply)
{
    public static ZLinkSpotCreateResponse Accept(ZLinkMessage? reply = null);
    public static ZLinkSpotCreateResponse Reject(ZLinkMessage? reply = null);
}

public interface IZLinkSpotHandlerRegistry
{
    void AddPacket<THandler>() where THandler : class;
    void AddSubscribe<THandler>(string topic) where THandler : class;
}

public interface IZLinkSpotOutbound
{
    IZLinkSendCall SendToSpot<TMessage>(SpotHandle target, TMessage message);
    IZLinkRequestCall RequestToSpot<TRequest>(SpotHandle target, TRequest request);
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
    IZLinkSendCall SendToChannel<TMessage>(
        string channelName,
        TMessage message);
    IZLinkRequestCall RequestToChannel<TRequest>(
        string channelName,
        TRequest request);
}

public interface IZLinkSpotCommonContext
{
    string MeshName { get; }
    RoutingId SpotRid { get; }
    RoutingId NodeRid { get; }
    IZLinkSpotHandlerRegistry Handlers { get; }
    IZLinkSpotOutbound Outbound { get; }
    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default)
        where THandler : class;
    IZLinkWorkerCall<TResult> RunCpuWorker<TResult>(
        Func<CancellationToken, TResult> work);
    IZLinkWorkerCall<TResult> RunIoWorker<TResult>(
        Func<CancellationToken, ValueTask<TResult>> work);
}

public interface IZLinkSpotContext : IZLinkSpotCommonContext
{
    ValueTask<bool> CloseAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkEntrySpot
{
    IZLinkEntrySpotContext Context { get; }
    void Configure();
    ValueTask OnInitializeAsync(CancellationToken cancellationToken);
    ValueTask OnClosingAsync(CancellationToken cancellationToken);
}

public interface IZLinkSpotActorLifecycle
{
    ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        ZLinkActorJoinRequest actor,
        ZLinkMessage request,
        CancellationToken cancellationToken);
    ValueTask OnJoinedActorAsync(
        ZLinkActorMembership actor,
        CancellationToken cancellationToken);
    ValueTask OnLeaveActorAsync(
        ZLinkActorMembership actor,
        CancellationToken cancellationToken);
    ValueTask OnDisconnectActorAsync(
        ZLinkActorMembership actor,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkActorMembership(
    ActorRef Actor,
    string ActorType,
    ulong MembershipEpoch);

public readonly record struct ZLinkActorJoinRequest(
    ActorRef Actor,
    string ActorType,
    ulong ExpectedMembershipEpoch);

public interface IZLinkActorSpot
    : IZLinkSpot, IZLinkSpotActorLifecycle
{
}

public interface IZLinkActorEntrySpot
    : IZLinkEntrySpot, IZLinkSpotActorLifecycle
{
}

public readonly record struct ZLinkSpotActorJoinResult(
    bool Accepted,
    ZLinkMessage? Reply)
{
    public static ZLinkSpotActorJoinResult Accept(ZLinkMessage? reply = null);
    public static ZLinkSpotActorJoinResult Reject(ZLinkMessage? reply = null);
}

public interface IZLinkEntrySpotOptions
{
    RoutingId RoutingId { get; set; }
}

public interface IZLinkEntrySpotContext : IZLinkSpotCommonContext
{
    ValueTask DestroyActorAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);
}
```

Spot handler signatures는 다음과 같다.

```csharp
public interface IZLinkSpotPacketHandler<TSpot, in TMessage>
    where TSpot : class, IZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRequestHandler<TSpot, in TRequest, TReply>
    where TSpot : class, IZLinkSpot
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotSubscriptionHandler<TSpot, in TEvent>
    where TSpot : class, IZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TEvent message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotTimerHandler<TSpot>
    where TSpot : class, IZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken);
}

public interface IZLinkTimer : IAsyncDisposable
{
    bool IsDisposed { get; }
    ValueTask CancelAsync();
}

public sealed record ZLinkTimerOptions
{
    public ZLinkTimerOverrunPolicy OverrunPolicy { get; init; }
        = ZLinkTimerOverrunPolicy.SkipLateTicks;
    public int MaxCatchUpTicks { get; init; } = 1;
    public bool StopOnUnhandledException { get; init; }
}

public enum ZLinkTimerOverrunPolicy
{
    SkipLateTicks = 1,
    CatchUpBounded = 2,
    DelayNextTick = 3
}

public readonly record struct ZLinkTimerTick(
    string Name,
    ulong DeliveryIndex,
    ulong ScheduledIndex,
    TimeSpan Period,
    DateTimeOffset ScheduledAt,
    DateTimeOffset StartedAt,
    TimeSpan ScheduledElapsed,
    TimeSpan StartedElapsed,
    TimeSpan Delay,
    ulong SkippedTicks);
```

Spot 외부 client는 다음 시그니처를 사용한다.

```csharp
public interface IZLinkSpotClient
{
    IZLinkSendCall SendToSpot<TMessage>(SpotHandle target, TMessage message);
    IZLinkRequestCall RequestToSpot<TRequest>(SpotHandle target, TRequest request);
}

public readonly record struct ZLinkSpotInfo(RoutingId SpotRid);

public interface IZLinkSpotManager
{
    ValueTask<SpotHandle> CreateAsync(
        string meshName,
        string spotType,
        CancellationToken cancellationToken = default);
    ValueTask<SpotHandle> CreateAsync<TCreation>(
        string meshName,
        string spotType,
        TCreation creation,
        CancellationToken cancellationToken = default);
    ValueTask<SpotHandle> CreateAsync<TCreation>(
        string meshName,
        string spotType,
        RoutingId spotRid,
        TCreation creation,
        CancellationToken cancellationToken = default);
    ValueTask<SpotHandle> GetOrCreateAsync(
        string meshName,
        string spotType,
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
    ValueTask<SpotHandle> GetOrCreateAsync<TCreation>(
        string meshName,
        string spotType,
        RoutingId spotRid,
        TCreation creation,
        CancellationToken cancellationToken = default);
    ValueTask<SpotHandle> ResolveAsync(
        string meshName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
    ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        string meshName,
        CancellationToken cancellationToken = default);
    ValueTask DestroyAsync(
        SpotHandle spot,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotPublisherClient
{
    IZLinkPublishCall Publish<TEvent>(
        string meshName,
        string channelName,
        string topic,
        TEvent message);
}
```

`IZLinkSpotPublisherClient.Publish(...)`와 `IZLinkSpotOutbound.Publish(...)`는 Logical Multicast다. Publisher
설정의 `NoDrop` 기본값은 `true`이며 정확한 option은
[05 RouteMesh·MeshNode §5](05-route-mesh.ko.md#5-publisher와-runtime-option)가 소유한다. 같은 node의
일치하는 Spot queue는 immutable message storage를 공유한다.

## 5. Actor

Actor direct message는 MeshName context와 ActorRef의 owner route·generation을 사용한다. Actor handler는
Actor 자신의 registry에 등록하며 Actor payload를 Node 또는 Spot handler가 다시 분류하지 않는다.

```csharp
public readonly struct ActorRef
{
    public ActorRef(RoutingId nodeRid, string actorId, ulong generation);
    public RoutingId NodeRid { get; }
    public string ActorId { get; }
    public ulong Generation { get; }
}

public sealed record ActorRefSnapshot(
    RoutingId NodeRid,
    string ActorId,
    ulong Generation)
{
    public static ActorRefSnapshot From(ActorRef actorRef);
    public ActorRef ToActorRef();
}

public interface IZLinkActor
{
    string ActorId { get; }
    IZLinkActorContext Context { get; }
    void Configure();
}

public interface IZLinkActorContext
{
    string MeshName { get; }
    RoutingId? SpotRid { get; }
    IZLinkActorHandlerRegistry Handlers { get; }
    IZLinkBoundSession? BoundSession { get; }
    IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        RoutingId spotRid,
        TRequest request);
    IZLinkActorJoinEntrySpotCall JoinEntrySpot<TRequest>(
        RoutingId targetNodeRid,
        TRequest request);
    ValueTask LeaveSpotAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorHandlerRegistry
{
    void AddPacket<THandler>() where THandler : class;
}

public interface IZLinkActorFactory
{
    ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorTransferAdapter<TActor>
    where TActor : IZLinkActor
{
    ValueTask<ZLinkMessage> TransferOutAsync(
        TActor actor,
        CancellationToken cancellationToken);
    ValueTask<TActor> TransferInAsync(
        string actorId,
        IZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken);
}

public interface IZLinkActorClient
{
    IZLinkActorSendCall SendToActor<TMessage>(
        string meshName,
        ActorRef actor,
        TMessage message);
    IZLinkActorRequestCall RequestToActor<TRequest>(
        string meshName,
        ActorRef actor,
        TRequest request);
}

public interface IZLinkActorManager
{
    ValueTask<ActorRef> CreateAsync<TCreation>(
        string meshName,
        string actorType,
        string actorId,
        TCreation creation,
        CancellationToken cancellationToken = default);
    ValueTask<ActorRef> ResolveAsync(
        string meshName,
        string actorId,
        CancellationToken cancellationToken = default);
    ValueTask DestroyAsync(
        string meshName,
        ActorRef actor,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorSendCall : IZLinkMetadataCall<IZLinkActorSendCall>
{
    ZLinkSubmitResult TrySubmit();
    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorRequestCall : IZLinkMetadataCall<IZLinkActorRequestCall>
{
    IZLinkActorRequestCall Timeout(TimeSpan timeout);
    ValueTask<TReply> Async<TReply>(
        CancellationToken cancellationToken = default);
    ValueTask<TReply> Yield<TReply>(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorJoinCall
{
    ValueTask<ZLinkActorJoinResult> Async(
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorJoinResult<TReply>> Async<TReply>(
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorJoinResult> Yield(
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorJoinResult<TReply>> Yield<TReply>(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorJoinSpotCall : IZLinkActorJoinCall
{
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);
}

public interface IZLinkActorJoinEntrySpotCall : IZLinkActorJoinCall
{
    IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout);
}

public abstract record ZLinkActorJoinResult
{
    private protected ZLinkActorJoinResult() { }
    public sealed record Accepted(ActorRef Actor, ZLinkMessage Reply)
        : ZLinkActorJoinResult;
    public sealed record Rejected(ZLinkMessage Reply)
        : ZLinkActorJoinResult;
}

public abstract record ZLinkActorJoinResult<TReply>
{
    private protected ZLinkActorJoinResult() { }
    public sealed record Accepted(ActorRef Actor, TReply Reply)
        : ZLinkActorJoinResult<TReply>;
    public sealed record Rejected(TReply Reply)
        : ZLinkActorJoinResult<TReply>;
}
```

Actor packet handler는 Actor instance만 mutable owner로 받는다. Spot membership은 Actor context의 snapshot이며
Spot 소유 상태를 바꿀 때는 명시적인 Spot call을 사용한다.

```csharp
public sealed class ZLinkActorSendContext
{
    public ZLinkMessageMetadata Metadata { get; }
}

public sealed class ZLinkActorRequestContext
{
    public ZLinkMessageMetadata Metadata { get; }
}

public interface IZLinkActorSendHandler<TActor, in TMessage>
    where TActor : class, IZLinkActor
{
    ValueTask HandleAsync(
        TActor actor,
        ZLinkActorSendContext context,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkActorRequestHandler<TActor, in TRequest, TReply>
    where TActor : class, IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TActor actor,
        ZLinkActorRequestContext context,
        TRequest request,
        CancellationToken cancellationToken);
}
```

`SpotRid == null`은 Entry Spot 단계이고 값이 있으면 해당 user Spot에 참여한 상태다. 같은 상태를 나타내는
별도 boolean은 제공하지 않는다. `BoundSession`은 현재 binding이 없으면 `null`이다.

### 5.1 목표 계약 적용 추적

정식 계약은 위 시그니처다. Source와 package 적용이 남은 항목은 gap 문서가 추적하며 계약을 축소하지 않는다.

| gap | 적용 작업 |
|---|---|
| [90 §12.34](../../../90-implementation-gap.ko.md) | binding의 `ActorRef`에 계약 밖 `IsUnchecked` 공개 property가 남아 있다. |

## 6. Bound STREAM session

Actor에서 client로 보내는 표면은 one-way send와 disconnect만 제공한다.

```csharp
public interface IZLinkBoundSession
{
    IZLinkBoundSessionSendCall Send<TMessage>(TMessage message);
    ValueTask DisconnectAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkBoundSessionSendCall
    : IZLinkMetadataCall<IZLinkBoundSessionSendCall>
{
    ZLinkSubmitResult TrySubmit();
    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}
```

Client request에 대한 reply는 Actor request handler의 반환값으로 처리한다. Bound session은 client를 향한
새 request operation을 제공하지 않는다.

## 7. STREAM server session

STREAM session은 lifecycle과 typed packet handler를 소유한다. Transport callback은 application callback을
직접 실행하지 않는다.

```csharp
public interface IZLinkSession
{
    IZLinkSessionContext Context { get; }
    void Configure();
    ValueTask OnConnectedAsync(CancellationToken cancellationToken);
    ValueTask OnDisconnectedAsync(CancellationToken cancellationToken);
    ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken);
    ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionContext
{
    string SessionId { get; }
    RoutingId? RoutingId { get; }
    string? LocalAddress { get; }
    string? RemoteAddress { get; }
    IZLinkSessionClient Client { get; }
    IZLinkSessionActors Actors { get; }
    IZLinkSessionHandlerRegistry Handlers { get; }
    ValueTask CloseAsync();
}

public interface IZLinkSessionHandlerRegistry
{
    void AddHandler<THandler>() where THandler : class;
    void AddHandler<THandler>(string packetName) where THandler : class;
    ValueTask<bool> TryHandleAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionPacketHandler<in TSessionContext, TMessage>
    where TSessionContext : IZLinkSessionContext
{
    ValueTask HandleAsync(
        TSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionClient
{
    IZLinkSessionSendCall Send<TMessage>(TMessage message);
    IZLinkSessionReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkSessionSendCall
    : IZLinkMetadataCall<IZLinkSessionSendCall>
{
    IZLinkSessionSendCall Compress();
    ZLinkSubmitResult TrySubmit();
    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall Compress();
    ZLinkSubmitResult TrySubmit();
    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionActors
{
    IReadOnlyCollection<IZLinkSessionActor> Bound { get; }
    ValueTask<IZLinkSessionActor> BindAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);
    IZLinkSessionActor? Find(string actorId);
}

public interface IZLinkSessionActor
{
    string MeshName { get; }
    string ActorId { get; }
    ActorRef Ref { get; }
    ValueTask<ZLinkSubmitResult> RelayAsync<TMessage>(
        TMessage message,
        CancellationToken cancellationToken = default);
    ValueTask NotifyDisconnectedAsync(
        CancellationToken cancellationToken = default);
}

public enum ZLinkStreamSessionError
{
    Internal = 0,
    TransportError = 1
}

public readonly record struct ZLinkStreamDiagnostic(
    int NativeCode,
    string? Message);

public readonly record struct ZLinkStreamError(
    ZLinkStreamSessionError Error,
    ZLinkStreamDiagnostic? Diagnostic);

public sealed class ZLinkSessionDispatchContext
{
    public ZLinkSessionDispatchContext(
        string packetName,
        ZLinkMessageMetadata? metadata = null,
        bool canReply = false) { }
    public string PacketName { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public bool CanReply { get; }
}
```

같은 session의 packet과 lifecycle callback은 직렬로 실행한다. Handshake와 node 범위 오류는 runtime
monitoring으로 보고하며 `OnErrorAsync(...)`에 전달하지 않는다.

## 8. Monitoring과 Framework 오류

Monitoring source는 framework root와 같은 DI container에 등록한다. 공개 source 종류와 등록 표면은
다음과 같다. `ZLinkSocketEventKind`는 이 여섯 값으로 닫혀 있으며 native backend의 내부 event를 별도
public kind로 추가하지 않는다.

```csharp
public interface IZLinkMonitoringOptions
{
    void AddSocketEvents(
        string sourceName,
        params ZLinkSocketEventKind[] events);
    void AddSpotEvents(string sourceName, TimeSpan interval);
    void AddLocationRuntimeEvents(string sourceName, TimeSpan interval);
    void AddLocationPeerEvents(string sourceName);
    void AddLocationSpotEvents(string sourceName);
    void AddLocationActorEvents(string sourceName);
    void AddLocationRouteEvents(string sourceName);
}

public enum ZLinkSocketEventKind
{
    Connected = 0,
    ConnectionReady = 1,
    Disconnected = 2,
    HandshakeFailed = 3,
    PeerAdmissionChanged = 4,
    Closed = 5
}
```

Request와 lifecycle operation의 framework 실패는 다음 exception으로 전달한다. One-way submit은 §2의
`ZLinkSubmitResult`를 반환하며 잘못된 public 인자는 .NET 표준 `ArgumentException` 계열로 거부한다.

```csharp
public enum ZLinkErrorKind
{
    ActorRouteNotFound = 0,
    ActorCreateFailed = 1,
    ActorAlreadyExists = 2,
    ActorTypeMismatch = 3,
    SpotCreateFailed = 4,
    SpotRouteNotFound = 5,
    SpotTypeMismatch = 6,
    ActorSessionNotBound = 7,
    HandlerNotFound = 8,
    RouteHandlerNotFound = 9,
    ActorDispatchHandlerNotFound = 10,
    PayloadDecodeFailed = 11,
    RouteNotConnected = 12,
    RequestTargetNotFound = 13,
    RequestRejected = 14,
    RequestProtocolError = 15,
    RequestFailed = 16,
    WorkerQueueFull = 17,
    WorkerTimedOut = 18,
    WorkerFailed = 19,
    ActorLocationStale = 20,
    ActorCreateRejected = 21
}

public sealed class ZLinkException : Exception
{
    public ZLinkErrorKind Kind { get; }
    public bool IsRetriable { get; }
    public int? NativeCode { get; }
}

public enum ZLinkRequestFailureReason
{
    Timeout = 1,
    Cancelled = 2,
    Shutdown = 3
}

public sealed class ZLinkRequestFailureException : Exception
{
    public ZLinkRequestFailureException(
        ZLinkRequestFailureReason reason,
        string message,
        Exception? innerException = null);
    public ZLinkRequestFailureReason Reason { get; }
}

public sealed class ZLinkConfigurationException : Exception
{
}
```

`Kind`의 숫자 값과 기본 재시도 의미는 [공통 Framework API](../../../05-framework-api.ko.md#13-오류-kind)와
같다. `NativeCode`는 진단용이며 업무 분기 기준으로 사용하지 않는다.
Admission 뒤 request 대기가 reply 없이 끝나면 `ZLinkRequestFailureException.Reason`으로 timeout,
호출자 cancellation과 host shutdown을 구분한다. 세 원인을 `RequestFailed`로 합치지 않으며 remote
framework error는 계속 `ZLinkException`으로 전달한다.

## 9. Typed JSON 기본 경로

Handler와 client의 generic payload는 기본 JSON serializer로 encode/decode한다. JSON용 message-specific
codec registration 함수는 public contract에 없다. `ZLinkMessage`를 받는 overload는 lifecycle state나
명시적인 encoded payload가 필요한 경계에만 사용한다.

Packet name은 message type descriptor에서 결정한다. 선언적 packet metadata가 있으면 그 이름을 사용하고,
없으면 nominal type 이름을 사용한다. Codec, payload instance와 handler가 packet name 결정을 반복하지
않는다.

선택 codec과 handler filter의 exact extension 경계는 다음과 같다.

```csharp
public interface IZLinkMessageCodec
{
    string ContentType { get; }
    bool CanHandle(Type messageType);
    ZLinkMessage Encode(object value, Type messageType);
    object Decode(ZLinkMessage payload, Type messageType);
}

public sealed class ZLinkHandlerInvocation
{
    public string MeshName { get; }
    public string OwnerKind { get; }
    public string PacketName { get; }
    public ZLinkMessageMetadata Metadata { get; }
}

public delegate ValueTask ZLinkHandlerFilterNext();

public interface IZLinkHandlerFilter
{
    ValueTask InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerFilterNext next,
        CancellationToken cancellationToken);
}
```

## 10. Dispatch와 ownership

Core ready callback은 payload가 아니라 owner work 가능 상태를 알린다. Framework는 Node, Spot과 Actor
claim을 owner별로 drain하고 typed handler에 전달한다. Spot Logical Multicast는 수신 MeshNode가 local
subscription을 검사하고 일치하는 Spot queue에 immutable message reference를 넣는다.

Handler가 받은 metadata와 message context는 callback 동안 읽기 전용이다. Handler가 payload storage,
reply token, route envelope나 Core claim을 dispose하지 않는다. Framework가 callback completion과 함께
해당 resource의 lifecycle을 관리한다.
