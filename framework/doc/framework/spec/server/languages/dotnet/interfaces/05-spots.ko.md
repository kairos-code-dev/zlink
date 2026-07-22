# .NET Spot 공개 인터페이스

[.NET exact interface 목차](README.ko.md)

## 1. Spot

SpotRid는 Location Store transaction domain 전체에서 유일한 logical ID다. 일반 message는 SpotRid만 받고
current authority를 resolve한다. `SpotRef`는 exact incarnation을 닫을 때 사용하는 immutable location
snapshot이며 runtime resource나 local Spot instance를 소유하지 않는다.

```csharp
public enum ZLinkSpotKind
{
    Invalid = 0,
    Entry = 1,
    User = 2,
    Instance = 3
}

public enum ZLinkCreatableSpotKind
{
    User = 1,
    Instance = 2
}

public readonly record struct SpotRef(
    RoutingId SpotRid,
    ulong ObjectGeneration,
    string MeshName,
    RoutingId NodeRid);

public interface IZLinkSpot
{
    IZLinkSpotContext Context { get; }
    void Configure()
    {
    }

    ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        // 기본 lifecycle은 생성 요청을 수락한다.
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkInstanceSpot
{
    IZLinkInstanceSpotContext Context { get; }
    void Configure()
    {
    }

    ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public readonly record struct ZLinkSpotCreateResponse(
    bool Accepted,
    ZLinkMessage? Reply)
{
    public static ZLinkSpotCreateResponse Accept(ZLinkMessage? reply = null);
    public static ZLinkSpotCreateResponse Accept<TReply>(TReply reply);
    public static ZLinkSpotCreateResponse Reject(ZLinkMessage? reply = null);
    public static ZLinkSpotCreateResponse Reject<TReply>(TReply reply);
}

public interface IZLinkSpotHandlerRegistry : IZLinkActorHandlerRegistry
{
    void AddPacket<THandler>() where THandler : class;
    void AddSubscribe<THandler>(string channelName, string topic) where THandler : class;
}

public interface IZLinkInstanceSpotHandlerRegistry
{
    void AddPacket<THandler>() where THandler : class;
}

public interface IZLinkSpotOutbound
{
    IZLinkSendCall SendToSpot<TMessage>(RoutingId spotRid, TMessage message);
    IZLinkRequestCall RequestToSpot<TRequest>(RoutingId spotRid, TRequest request);
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
    IZLinkSpotHandlerRegistry Handlers { get; }

    ValueTask LeaveActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask<bool> CloseAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkInstanceSpotContext : IZLinkSpotCommonContext
{
    IZLinkInstanceSpotHandlerRegistry Handlers { get; }

    ValueTask<bool> CloseAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkEntrySpot
{
    IZLinkEntrySpotContext Context { get; }
    void Configure()
    {
    }

    ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkSpotActorLifecycle<TActor>
    where TActor : IZLinkActor
{
    ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken);

    ValueTask OnJoinedActorAsync(
        TActor actor,
        CancellationToken cancellationToken);

    ValueTask OnLeaveActorAsync(
        TActor actor,
        CancellationToken cancellationToken);

    ValueTask OnDisconnectActorAsync(
        TActor actor,
        CancellationToken cancellationToken)
    {
        // 연결 단절 처리가 필요하지 않은 Spot은 이 callback을 구현하지 않아도 된다.
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkSpot<TActor> : IZLinkSpot, IZLinkSpotActorLifecycle<TActor>
    where TActor : IZLinkActor;

public interface IZLinkEntrySpot<TActor> : IZLinkEntrySpot, IZLinkSpotActorLifecycle<TActor>
    where TActor : IZLinkActor
{
    ValueTask OnCreateActorAsync(
        TActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        // 별도 초기화가 필요하지 않은 Actor는 기본 구현을 사용한다.
        return ValueTask.CompletedTask;
    }
}

public readonly record struct ZLinkSpotActorJoinResult(
    bool Accepted,
    ZLinkMessage? Reply)
{
    public static ZLinkSpotActorJoinResult Accept(ZLinkMessage? reply = null);
    public static ZLinkSpotActorJoinResult Accept<TReply>(TReply reply);
    public static ZLinkSpotActorJoinResult Reject(ZLinkMessage? reply = null);
    public static ZLinkSpotActorJoinResult Reject<TReply>(TReply reply);
}

public interface IZLinkEntrySpotContext : IZLinkSpotCommonContext
{
    IZLinkSpotHandlerRegistry Handlers { get; }

    ValueTask DestroyActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);
}

public sealed class ZLinkSpotActorReplyOptions
{
    public ZLinkSpotActorReplyOptions();
    public ZLinkSpotActorReplyOptions Metadata(string key, string value);
    public ZLinkSpotActorReplyOptions Compress(bool enabled = true);
}

public sealed class ZLinkSpotActorSendContext : IZLinkHandlerContext
{
    public string? ChannelName { get; }
    public string PacketName { get; }
    public string? ContentType { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public CancellationToken ConnectionAborted { get; }
}

public sealed class ZLinkSpotActorRequestContext : IZLinkHandlerContext
{
    public string? ChannelName { get; }
    public string PacketName { get; }
    public string? ContentType { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public CancellationToken ConnectionAborted { get; }
    public ZLinkSpotActorReplyOptions Reply { get; }
}

public interface IZLinkSpotActorSendHandler<TSpot, TActor, in TMessage>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorSendContext context,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorRequestHandler<TSpot, TActor, in TRequest, TReply>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, in TMessage>
    where TEntrySpot : class, IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorSendContext context,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, in TRequest, TReply>
    where TEntrySpot : class, IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request,
        CancellationToken cancellationToken);
}
```

Spot과 Actor의 current location 조회는 manager가 global ID로 수행한다. Public resolver와 runtime handle은
제공하지 않는다. owner route와 generation 갱신 규칙은
[Spot 주소 메시징](../../../24-spot-address-messaging.ko.md)을 따른다.

Spot handler signatures는 다음과 같다.

```csharp
public interface IZLinkSpotPacketHandler<TSpot, in TMessage>
    where TSpot : class
{
    ValueTask HandleAsync(
        TSpot spot,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRequestHandler<TSpot, in TRequest, TReply>
    where TSpot : class
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotSubscriptionHandler<TSpot, in TEvent>
    where TSpot : class
{
    ValueTask HandleAsync(
        TSpot spot,
        TEvent message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotTimerHandler<TSpot>
    where TSpot : class
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
    IZLinkSendCall SendToSpot<TMessage>(RoutingId spotRid, TMessage message);
    IZLinkRequestCall RequestToSpot<TRequest>(RoutingId spotRid, TRequest request);
}

public enum ZLinkSpotCreateState
{
    Existing = 0,
    Created = 1,
    Rejected = 2
}

public readonly record struct ZLinkSpotCreateResult(
    SpotRef Spot,
    ZLinkSpotCreateState State,
    ZLinkMessage? Reply);

public interface IZLinkSpotManager
{
    IZLinkSpotCreateCall Create(
        ZLinkCreatableSpotKind kind,
        string spotType);
    IZLinkSpotGetOrCreateCall GetOrCreate(
        RoutingId spotRid,
        ZLinkCreatableSpotKind kind,
        string spotType);
    ValueTask<SpotRef?> FindAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
    ValueTask<bool> CloseAsync(
        SpotRef spot,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotCreateCall
{
    IZLinkSpotCreateCall InMesh(string meshName);
    IZLinkSpotCreateCall Request(ZLinkMessage request);
    IZLinkSpotCreateCall Request<TRequest>(TRequest request);
    IZLinkSpotCreateCall PlacementProfile(string placementProfile);
    IZLinkSpotCreateCall AffinityKey(string affinityKey);
    IZLinkSpotCreateCall Timeout(TimeSpan timeout);
    ValueTask<ZLinkSpotCreateResult> Async(CancellationToken cancellationToken = default);
}

public interface IZLinkSpotGetOrCreateCall
{
    IZLinkSpotGetOrCreateCall InMesh(string meshName);
    IZLinkSpotGetOrCreateCall Request(ZLinkMessage request);
    IZLinkSpotGetOrCreateCall Request<TRequest>(TRequest request);
    IZLinkSpotGetOrCreateCall PlacementProfile(string placementProfile);
    IZLinkSpotGetOrCreateCall AffinityKey(string affinityKey);
    IZLinkSpotGetOrCreateCall Timeout(TimeSpan timeout);
    ValueTask<ZLinkSpotCreateResult> Async(CancellationToken cancellationToken = default);
}

public interface IZLinkSpotPublisherClient
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}
```

User·Instance SpotRid는 global key다. Stable type은 UTF-8 1..255 bytes이며 case-sensitive exact value로
비교하고 normalization하지 않는다. `SpotRef.ObjectGeneration`은 1..`long.MaxValue`다. MeshName과 NodeRid는
조회 시점의 route snapshot이며 identity key에 포함하지 않는다.

`IZLinkInstanceSpot`은 `IZLinkSpot`을 상속하지 않는 actor-free lifecycle interface다. Direct packet과 timer
handler만 등록할 수 있고 Actor handler나 Logical Multicast subscription을 등록하면 Framework가
`Ready` commit 전에 activation을 거부한다. Lifecycle은 scope와 Spot을 만든 뒤 Configure,
`OnInitializeAsync`, Location `Ready` commit 순서로 실행한다. `OnCreateAsync` 또는 empty
`ZLinkMessage`를 사용하지 않으며 첫 업무 message는 barrier 뒤 direct handler에 전달한다.

Store-backed User·Instance Spot도 generic placement reservation으로 `Creating` row와 target pending capacity를
함께 확보한 뒤 factory, `Configure`, `OnInitializeAsync`를 수행한다. 성공하면 같은 reservation을 `Ready`와
active capacity로 commit하고 실패하면 abort한다. Resolve와 remote messaging은 `Ready`만 사용한다. CAS loser는
별도 factory를 시작하지 않으며 exact reservation 결과를 read해 reconcile한다. 이 barrier를 제어하는
application API는 없다.

`CloseAsync(spotRef)`는 exact incarnation만 닫는다. 해당 incarnation이 없으면 `false`, generation이 다르면
`SpotGenerationStale`, pre-commit seal 중이면 `SpotMoving`이다. User Spot에 Actor membership이 남아 있으면
`false`이며 Actor를 자동 leave·destroy하지 않는다. Framework는 current ref를 다시 찾아 다른 incarnation을
닫지 않는다.

Create와 GetOrCreate call은 single-use다. 같은 option을 두 번 설정하면 `InvalidConfiguration`, terminal
`Async(...)`를 두 번 호출하면 `AlreadySubmitted`다. `InMesh(...)` 선택과 오류, placement option 및 전체
deadline 규칙은 Actor create와 같다. `Create`는 Framework가 새 global RID를 발급한다. `GetOrCreate`는 같은
kind·stable type의 Ready 또는 Creating attempt에 합류하고 CAS loser가 별도 factory를 실행하지 않는다. Kind나
type이 다르면 `SpotTypeMismatch`, deadline 안에 terminal state가 되지 않으면 `DeadlineExceeded`다. Creation
request는 최대 1 MiB이며 reservation 전에 immutable reference와 hash로 보관한다.

Instance Spot의 최초 create intent만 kind, stable type과 initial Mesh를 기록한다. Ready 이후의 message는
SpotRid만 사용한다. Owner loss 뒤 reactivation도 저장한 intent를 사용하며 message 호출이 새 intent를 만들지
않는다. Public activation driver, address, handle, resolver와 unbounded list는 제공하지 않는다. 운영 조회는
Location runtime의 page size 1..1000, encoded page 4 MiB 이하인 paged query가 소유한다.

Cold Instance factory·initialize가 실패하면 durable public `Failed` state를 게시하지 않는다. Runtime은 local
failed barrier를 유지하고 exact authority fence로 delete한 뒤 read해 reconcile한다. Delete 확인 전 같은 address
호출은 같은 typed failure를 반환하며 hidden retry는 0이다. `Missing` 확인 뒤 다음 caller만 새
`ColdActivating` claim을 시작한다. 이 recovery 상태를 조작하는 public API는 없다.

`IZLinkSpotPublisherClient.Publish(...)`와 `IZLinkSpotOutbound.Publish(...)`는 Logical Multicast다.
외부 publisher와 Spot callback의 outbound는 모두 ChannelName과 topic만 받는다. Process-local ChannelName
index가 owner MeshNode를 선택하며 caller는 MeshName을 추가로 넘기지 않는다.
각 remote target은 MeshNode ROUTER의 송신 규칙을 따르며, 같은 node의 일치하는 Spot queue는 immutable
message storage를 공유한다. 정확한 설정 표면은
[Topology configuration §5](03-configuration-topology.ko.md#5-publisher와-runtime-option)가 소유한다.
