# .NET Spot 공개 인터페이스

[.NET exact interface 목차](README.ko.md)

## 1. Spot

Spot은 MeshNode가 소유하는 logical mailbox다. `SpotHandle`은 location snapshot과 generation을 보존하지만
application에 target node RID를 노출하지 않는다.

```csharp
public enum ZLinkSpotKind
{
    Invalid = 0,
    Entry = 1,
    User = 2,
    Instance = 3
}

public sealed record InstanceSpotAddress(
    string MeshName,
    string InstanceSpotType,
    RoutingId SpotRid);

public sealed record ZLinkInstanceSpotFactoryOptions
{
    public int MaxActiveInstances { get; init; } = 4096;
    public TimeSpan ActivationTimeout { get; init; } = TimeSpan.FromSeconds(3);
}

public abstract class SpotHandle
{
    public abstract string MeshName { get; }
    public abstract RoutingId SpotRid { get; }
}

public interface IZLinkSpotHandleResolver
{
    ValueTask<SpotHandle?> ResolveSpotHandleAsync(
        string meshName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorSpotHandleResolver
{
    ValueTask<SpotHandle?> ResolveActorSpotHandleAsync(
        string meshName,
        string actorId,
        CancellationToken cancellationToken = default);
}

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
    IZLinkSendCall SendToSpot<TMessage>(SpotHandle target, TMessage message);
    IZLinkRequestCall RequestToSpot<TRequest>(SpotHandle target, TRequest request);
    IZLinkSendCall SendToSpot<TMessage>(InstanceSpotAddress target, TMessage message);
    IZLinkRequestCall RequestToSpot<TRequest>(InstanceSpotAddress target, TRequest request);
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

public interface IZLinkEntrySpotOptions
{
    RoutingId RoutingId { get; set; }
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

두 resolver는 대상 MeshName을 명시적으로 받는다. Spot RID와 Actor identity는 서로 다른 MeshName에서
같을 수 있으므로 등록된 여러 mesh를 순서대로 탐색하거나 첫 번째 결과를 선택하지 않는다. 유효한
location row가 없으면 `null`을 반환하며, 성공하면 입력한 MeshName이 `SpotHandle.MeshName`에 유지된다.
owner route와 generation 갱신 규칙은
[Spot 주소 메시징 §2~3](../../../24-spot-address-messaging.ko.md#2-spothandle과-instancespotaddress)을 따른다.

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
    IZLinkSendCall SendToSpot<TMessage>(SpotHandle target, TMessage message);
    IZLinkRequestCall RequestToSpot<TRequest>(SpotHandle target, TRequest request);
    IZLinkSendCall SendToSpot<TMessage>(InstanceSpotAddress target, TMessage message);
    IZLinkRequestCall RequestToSpot<TRequest>(InstanceSpotAddress target, TRequest request);
}

public readonly record struct ZLinkSpotInfo(RoutingId SpotRid);

public enum ZLinkSpotCreateState
{
    Existing = 0,
    Created = 1,
    Rejected = 2
}

public readonly record struct ZLinkSpotCreateResult(
    RoutingId SpotRid,
    ZLinkSpotCreateState State,
    ZLinkMessage? Reply);

public interface IZLinkSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        string meshName,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        string meshName,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot, TRequest>(
        string meshName,
        TRequest request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        // Typed request는 ZLinkMessage overload로 연결한다.
        return CreateAsync<TSpot>(meshName, ZLinkMessage.From(request), cancellationToken);
    }

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        string meshName,
        RoutingId spotRid,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot, TRequest>(
        string meshName,
        RoutingId spotRid,
        TRequest request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        // Typed request는 ZLinkMessage overload로 연결한다.
        return GetOrCreateAsync<TSpot>(
            meshName,
            spotRid,
            ZLinkMessage.From(request),
            cancellationToken);
    }

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        string meshName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotInfo?> FindAsync(
        string meshName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        string meshName,
        CancellationToken cancellationToken = default);

    ValueTask<bool> CloseAsync(
        string meshName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotPublisherClient
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}
```

`InstanceSpotAddress.MeshName`과 `InstanceSpotType`은 비어 있을 수 없고 UTF-8로 각각 255 byte 이하여야 한다.
`SpotRid`는 빈 RID일 수 없다. Equality와 hash는 세 값을 모두 사용하지만 location key의 유일성은
`(MeshName, SpotRid)`가 소유한다. 같은 key에 다른 Instance type이나 User Spot을 함께 둘 수 없다.

`IZLinkInstanceSpot`은 `IZLinkSpot`을 상속하지 않는 actor-free lifecycle interface다. Direct packet과 timer
handler만 등록할 수 있고 Actor handler나 Logical Multicast subscription을 등록하면 Framework가
`Ready` commit 전에 activation을 거부한다. Lifecycle은 scope와 Spot을 만든 뒤 Configure,
`OnInitializeAsync`, Location `Ready` commit 순서로 실행한다. `OnCreateAsync` 또는 empty
`ZLinkMessage`를 사용하지 않으며 첫 업무 message는 barrier 뒤 direct handler에 전달한다.

Store-backed dynamic User Spot도 authority 내부 `Creating` row를 `NewObject` CAS로 만든 뒤 factory,
`Configure`, `OnInitializeAsync`를 완료하고 `Ready` CAS를 수행한다. Resolve와 remote messaging은 `Ready`만
사용한다. 실패하면 exact owner fence로 delete하고 결과를 read해 reconcile한다. Delete가 확인될 때까지 같은
typed failure를 반환하고 hidden retry는 0이며, `Missing`이 확인된 뒤 다음 caller만 새 create를 시작한다. 이
barrier를 제어하는 public API는 없다.

User Spot의 `CloseAsync`는 active Actor membership이 있으면 `false`를 반환한다. Spot state, admission과
authority는 바꾸지 않고 `OnClosingAsync`를 호출하거나 Actor를 자동 leave·destroy하지 않는다. Caller는
Actor를 명시적으로 leave 또는 destroy한 뒤 다시 close한다. Manager에서 Spot이 missing인 경우도 `false`이므로
caller는 사전 read 없이 두 경우를 구분하지 않는다. Host Shutdown·Retire는 Actor barrier를 끝낸 뒤 Spot
cleanup을 수행한다.

Instance address overload가 반환한 `IZLinkSendCall`은 cache 상태와 관계없이 `SubmitAsync()`로만 제출한다.
Source는 location resolve·eligible target 선택과 `ColdActivating` CAS claim을 outbound보다 먼저 같은 send
deadline 안에서 완료한다. Target은 source가 확정한 token과 generation을 다시 검증하고 factory activation과
`Ready` CAS만 수행하며 target-side claim을 시작하지 않는다. `SubmitAsync()`는 source local outbound
admission까지 기다리지만 target factory 실행, activation queue 수락, `Ready`와 원격 handler 완료는 기다리지
않는다. Request는
`IZLinkRequestCall`의 timeout과 비동기 실행 계약을 사용한다. Caller는 target
node, owner token, generation, `createIfMissing` 또는 retry option을 전달하지 않는다. `IZLinkSpotManager`의
`CreateAsync<TSpot>`와 `GetOrCreateAsync<TSpot>`는 `IZLinkSpot` User Spot만 받고 `meshName`이 선택한 local
MeshNode에서만 생성한다. `IZLinkInstanceSpot`을 받는 별도 create overload는 없다. `FindAsync`는 local
existing-only 조회이고 missing Spot을 만들지 않는다. Remote 위치 조회와 messaging handle 획득은 별도
resolver가 소유한다.

Framework public contract에는 Instance activation driver, native token, owner admission handle과 placement
SPI를 제공하지 않는다. Runtime은 bindings의 public raw socket API와 Framework가 소유한 location authority를
조합해 cold activation을 내부에서 수행한다. `InstanceSpotAddress` overload만 missing Instance의 cold
activation을 허용한다. `SpotHandle`과 `IZLinkSpotManager`는 remote activation을 시작하지 않는다.

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
