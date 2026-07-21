# .NET Actor 공개 인터페이스

[.NET exact interface 목차](README.ko.md)

## 1. Actor

Actor direct message는 MeshName context와 ActorRef의 owner route·generation을 사용한다. Actor handler는
Spot이 소유한 registry에 등록한다.

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
    void Configure() { }
}

public interface IZLinkActorContext
{
    string MeshName { get; }
    RoutingId? SpotRid { get; }
    IZLinkBoundSession BoundSession { get; }
    IZLinkActorJoinSpotCall JoinSpot(
        RoutingId spotRid,
        ZLinkMessage request);
    IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        RoutingId spotRid,
        TRequest request)
    {
        return JoinSpot(spotRid, ZLinkMessage.From(request));
    }
    IZLinkActorJoinEntrySpotCall JoinEntrySpot(
        RoutingId spotNodeRid,
        ZLinkMessage request);
    IZLinkActorJoinEntrySpotCall JoinEntrySpot<TRequest>(
        RoutingId spotNodeRid,
        TRequest request)
    {
        return JoinEntrySpot(spotNodeRid, ZLinkMessage.From(request));
    }
}

public interface IZLinkActorHandlerRegistry
{
    void AddHandler<THandler>()
        where THandler : class;
    void AddHandler<THandler>(string packetName)
        where THandler : class;
    void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;
    void AddActorPacket<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor;
}

public interface IZLinkActorFactory
{
    ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorFactory<TActor>
    : IZLinkActorFactory
    where TActor : class, IZLinkActor
{
    new ValueTask<TActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default);
}

public interface IZLinkTransferStateAdapter<TInstance, TState>
    where TInstance : class
{
    ValueTask<TState> CaptureAsync(
        TInstance instance,
        CancellationToken cancellationToken);
    ValueTask RestoreAsync(
        TInstance instance,
        TState state,
        CancellationToken cancellationToken);
}

public abstract class ZLinkTransferPolicy<TInstance>
    where TInstance : class
{
    private protected ZLinkTransferPolicy();

    public static ZLinkTransferPolicy<TInstance> Disabled { get; }
    public static ZLinkTransferPolicy<TInstance> Recreate { get; }
    public static ZLinkTransferPolicy<TInstance> Snapshot<TState, TAdapter>(
        string stateContractId)
        where TAdapter : class, IZLinkTransferStateAdapter<TInstance, TState>;
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
    ValueTask<ActorRef> CreateAsync(
        string meshName,
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);
    ValueTask<ActorRef> CreateAsync(
        string meshName,
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default);
    ValueTask<ActorRef> CreateAsync<TRequest>(
        string meshName,
        string actorId,
        string actorType,
        TRequest createRequest,
        CancellationToken cancellationToken = default)
    {
        return CreateAsync(
            meshName,
            actorId,
            actorType,
            ZLinkMessage.From(createRequest),
            cancellationToken);
    }
    ValueTask<ActorRef> GetOrCreateAsync(
        string meshName,
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);
    ValueTask<ActorRef> GetOrCreateAsync(
        string meshName,
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default);
    ValueTask<ActorRef> GetOrCreateAsync<TRequest>(
        string meshName,
        string actorId,
        string actorType,
        TRequest createRequest,
        CancellationToken cancellationToken = default)
    {
        return GetOrCreateAsync(
            meshName,
            actorId,
            actorType,
            ZLinkMessage.From(createRequest),
            cancellationToken);
    }
}

public interface IZLinkActorSendCall : IZLinkMetadataCall<IZLinkActorSendCall>
{
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
    ValueTask<ZLinkActorJoinResult> Yield(
        CancellationToken cancellationToken = default);
    async ValueTask<ZLinkActorJoinResult<TReply>> Async<TReply>(
        CancellationToken cancellationToken = default)
    {
        var result = await Async(cancellationToken).ConfigureAwait(false);
        return result switch
        {
            ZLinkActorJoinResult.Accepted accepted =>
                new ZLinkActorJoinResult<TReply>.Accepted(
                    accepted.Actor,
                    accepted.Reply.Decode<TReply>()),
            ZLinkActorJoinResult.Rejected rejected =>
                new ZLinkActorJoinResult<TReply>.Rejected(
                    rejected.Reply.Decode<TReply>()),
            _ => throw new InvalidOperationException("Unknown actor join result.")
        };
    }
    async ValueTask<ZLinkActorJoinResult<TReply>> Yield<TReply>(
        CancellationToken cancellationToken = default)
    {
        var result = await Yield(cancellationToken).ConfigureAwait(false);
        return result switch
        {
            ZLinkActorJoinResult.Accepted accepted =>
                new ZLinkActorJoinResult<TReply>.Accepted(
                    accepted.Actor,
                    accepted.Reply.Decode<TReply>()),
            ZLinkActorJoinResult.Rejected rejected =>
                new ZLinkActorJoinResult<TReply>.Rejected(
                    rejected.Reply.Decode<TReply>()),
            _ => throw new InvalidOperationException("Unknown actor join result.")
        };
    }
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

Actor packet handler는 Spot이 소유한 registry에 등록한다. Handler의 정확한 context와 generic parameter는
[Spot interface](05-spots.ko.md)가 정의한다. `SpotRid == null`은 Entry Spot 단계이고 값이 있으면 해당 user
Spot에 참여한 상태다. 같은 상태를 나타내는 별도 boolean은 제공하지 않는다.

Typed state policy는 Actor factory registration이 소유한다. `Disabled`는 해당 type instance가 남아 있으면
host `Retire`를 차단한다. `Recreate`는 target factory로 같은
logical identity를 다시 만들고 application state를 복구하지 않는다. `Snapshot`은 adapter가 typed state만
capture·restore하며 `stateContractId`는 비어 있을 수 없고 UTF-8 255 byte 이하여야 한다. Adapter는 raw bytes,
checkpoint reference, accepted journal, transfer phase, source·target owner와 Store CAS version을 받지 않는다.

같은 `stateContractId`를 사용하는 source와 target adapter는 `frameworkJsonV1` semantic profile로 호환되어야
한다. 이 profile은 enum을 string, 64-bit integer를 decimal string, binary를 padded base64로 표현하고 unknown
field는 무시한다. Duplicate field와 required field 누락은 거부한다. Application state의 JSON byte 배열 자체는
canonical하지 않으며 Checkpoint Store에는 opaque bytes로 보관한다. Canonical byte identity는 Framework 내부
root manifest, chunk와 envelope에만 적용한다. Message별 codec 등록이나 transfer 전용 codec API는 제공하지
않는다.

Target이 `Activated`에 도달해도 application과 session ingress는 sealed 상태를 유지하고 restore, accepted
journal replay와 bound-session route는 staged 상태로만 준비한다. Source cleanup이 terminal 상태에 도달하고
authority의 `Completed` CAS가 성공한 뒤에만 target을 `Ready`로 열고 checkpoint fence를 해제한다. `Completed`
뒤의 target failure는 ordinary owner loss로 처리하며 이전 checkpoint를 transparent replay하지 않는다. 이
barrier를 조작하는 public phase API는 제공하지 않는다.

Target replacement가 발생하면 stable transfer 안의 각 attempt가 factory와 `RestoreAsync(...)`를 at-least-once
호출할 수 있고 중단된 stale attempt callback이 successor와 겹칠 수 있다. `CaptureAsync(...)`도 immutable
checkpoint root가 authority에 연결되기 전까지 반복될 수 있다. Current exact owner와 attempt fence만 completion을
commit하고 admission을 열 수 있다. Callback에는 transfer ID를 추가하지 않으므로 application restore와 capture는
retry-safe해야 하며 exactly-once external side effect를 보장하지 않는다.

Transferred terminal reply accounting은 internal command ID 46 `replyRelayAck`를 사용한다. 이 command는 stable
transfer ID, operation ID, exact request-source fence(owner ID, lease generation, node RID, node generation)와
status만 가지며 payload와 metadata를 싣지 않는다. Physical connection close는 terminal 증거가 아니다. ACK 또는
accepted record에 저장한 exact request-source lease expiry만 terminal accounting을 완료하며 public ACK API는 없다.

Source는 connection-bound one-way를 포함해 admission한 모든 connection-bound work가 terminal accounting에
도달한 뒤에만 `Captured`를 commit한다. Durable accepted journal은 exact owner lease가 있는 source에서만
사용한다. Pre-`Captured` drain이 deadline 안에 끝나지 않으면 transfer를 abort하고 host Retire를
`Blocked/TransferDisabled`로 끝낸다. Connection-bound one-way를 미완료 상태로 capture하는 예외는 없다.

Transferable Actor는 source Entry Spot member여야 한다. User Spot member가 하나라도 남아 있으면 Retire
preflight는 `Blocked/TransferDisabled`이고 source authority와 admission을 바꾸지 않는다. `NewOwner` CAS는
owner, `AuthorityOwnerGeneration`과 current Spot을 target Entry identity로 원자적으로 바꾼다. Target factory와
restore, target `OnJoinedActorAsync`, journal replay 뒤에 source `OnLeaveActorAsync`와 old Entry membership
제거를 durable cleanup으로 수행한다. Lifecycle callback은 retry-safe해야 하며 at-least-once 호출될 수 있다.
이 순서를 제어하는 public phase API는 없다.

새 distributed Actor는 authority 내부 `Creating` row를 `NewObject` CAS로 만들고 최종 `ActorRef.Generation`,
factory 실행, initial Entry membership과 initialize를 완료한 뒤 `Ready` CAS를 수행한다. Resolver와 remote
messaging은 `Ready`만 사용한다. Factory나 initialize가 실패하면 exact owner fence로 delete하고 결과를 read해
reconcile한다. Delete가 확인될 때까지 같은 typed failure를 반환하고 hidden retry는 0이며, `Missing`이
확인된 뒤 다음 caller만 새 `Creating`을 시작한다. Entry Spot initialization도 Host `Serving` publication보다
먼저 완료한다. 이 barrier를 위한 public API는 없다.

Actor factory와 transfer policy는
[Topology configuration](03-configuration-topology.ko.md)의 `AddActorFactory<TActor,TFactory>(...)` 한 호출에서
등록한다. Policy를 받지 않는 builder overload는 `Disabled` registration으로 동작한다.

## 2. Actor directory

Canonical logical identity는 `(MeshName, ActorId)`다. Actor type은 create에서 factory를 선택한 뒤 authority
payload에 고정하는 immutable lifecycle attribute이며 `ActorRef`나 directory key에 반복하지 않는다. 같은
MeshName과 Actor ID에는 active type 하나만 존재한다. `GetOrCreateAsync`에 전달한 type이 existing authority의
type과 다르면 type conflict로 실패한다.

Directory는 MeshName과 Actor ID로 이미 존재하는 logical identity만 조회한다. Missing Actor를 만들지 않으며
target MeshNode를 선택하지 않는다. Local create와 get-or-create는 `IZLinkActorManager`가 actor type을
명시해서 수행한다. 이때 MeshName은 현재 host에 등록된 local MeshNode를 선택하며 remote creation이나 hidden
forwarding을 시작하지 않는다.

```csharp
public interface IZLinkActorDirectory
{
    ValueTask<ActorRef?> FindAsync(
        string meshName,
        string actorId,
        CancellationToken cancellationToken = default);
}
```
