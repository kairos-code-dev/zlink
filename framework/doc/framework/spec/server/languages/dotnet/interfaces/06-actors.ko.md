# .NET Actor 공개 인터페이스

[.NET exact interface 목차](README.ko.md)

## 1. Actor

ActorId는 Location Store transaction domain 전체에서 유일한 logical ID다. UTF-8 encoded 크기는
1..255 bytes이고 case-sensitive exact value로 비교하며 normalization하지 않는다. 일반 Actor message는
ActorId만 받고 current authority를 resolve한다. `ActorRef`는 exact incarnation을 변경하거나 session에
bind할 때 사용하는 immutable location snapshot이다.

```csharp
public readonly record struct ActorRef(
    string ActorId,
    ulong ObjectGeneration,
    string MeshName,
    RoutingId NodeRid);

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
        ZLinkMessage request);
    IZLinkActorJoinEntrySpotCall JoinEntrySpot<TRequest>(
        TRequest request)
    {
        return JoinEntrySpot(ZLinkMessage.From(request));
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
        string actorId,
        TMessage message);
    IZLinkActorRequestCall RequestToActor<TRequest>(
        string actorId,
        TRequest request);
}

public interface IZLinkActorManager
{
    IZLinkActorCreateCall Create(
        string actorId,
        string actorType);
    IZLinkActorGetOrCreateCall GetOrCreate(
        string actorId,
        string actorType);
    ValueTask<ActorRef?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default);
    ValueTask<SpotRef?> FindSpotAsync(
        string actorId,
        CancellationToken cancellationToken = default);
    ValueTask<bool> DestroyAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorCreateCall
{
    IZLinkActorCreateCall InMesh(string meshName);
    IZLinkActorCreateCall Request(ZLinkMessage request);
    IZLinkActorCreateCall Request<TRequest>(TRequest request);
    IZLinkActorCreateCall PlacementProfile(string placementProfile);
    IZLinkActorCreateCall AffinityKey(string affinityKey);
    IZLinkActorCreateCall Timeout(TimeSpan timeout);
    ValueTask<ActorRef> Async(CancellationToken cancellationToken = default);
}

public interface IZLinkActorGetOrCreateCall
{
    IZLinkActorGetOrCreateCall InMesh(string meshName);
    IZLinkActorGetOrCreateCall Request(ZLinkMessage request);
    IZLinkActorGetOrCreateCall Request<TRequest>(TRequest request);
    IZLinkActorGetOrCreateCall PlacementProfile(string placementProfile);
    IZLinkActorGetOrCreateCall AffinityKey(string affinityKey);
    IZLinkActorGetOrCreateCall Timeout(TimeSpan timeout);
    ValueTask<ActorRef> Async(CancellationToken cancellationToken = default);
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
transfer reference, accepted journal, transfer phase, source·target owner와 Store CAS version을 받지 않는다.

같은 `stateContractId`를 사용하는 source와 target adapter는 `frameworkJsonV1` semantic profile로 호환되어야
한다. 이 profile은 enum을 string, 64-bit integer를 decimal string, binary를 padded base64로 표현하고 unknown
field는 무시한다. Duplicate field와 required field 누락은 거부한다. Application state의 JSON byte 배열 자체는
canonical하지 않으며 Transfer Store에는 opaque bytes로 보관한다. Canonical byte identity는 Framework 내부
root manifest, chunk와 envelope에만 적용한다. Message별 codec 등록이나 transfer 전용 codec API는 제공하지
않는다.

Target이 `Activated`에 도달해도 application과 session ingress는 sealed 상태를 유지하고 restore, accepted
journal replay와 bound-session route는 staged 상태로만 준비한다. Source cleanup이 terminal 상태에 도달하고
authority의 `Completed` CAS가 성공한 뒤에만 target을 `Ready`로 열고 transfer fence를 해제한다. `Completed`
뒤의 target failure는 ordinary owner loss로 처리하며 이전 transfer를 transparent replay하지 않는다. 이
barrier를 조작하는 public phase API는 제공하지 않는다.

Target replacement가 발생하면 stable transfer 안의 각 attempt가 factory와 `RestoreAsync(...)`를 at-least-once
호출할 수 있고 중단된 stale attempt callback이 successor와 겹칠 수 있다. `CaptureAsync(...)`도 immutable
transfer root가 authority에 연결되기 전까지 반복될 수 있다. Current exact owner와 attempt fence만 completion을
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

새 distributed Actor는 generic placement reservation이 authority의 `Creating` row와 target pending capacity를
함께 확보한 뒤 factory, initial Entry membership과 initialize를 수행한다. 성공하면 같은 reservation을
`Ready`와 active capacity로 commit하고, 실패하면 abort한다. Resolve와 remote messaging은 `Ready`만 사용한다.
CAS loser는 별도 factory를 시작하지 않으며 exact reservation 결과를 read해 reconcile한다. Entry Spot
initialization도 Host `Serving` publication보다 먼저 완료한다. 이 barrier를 위한 application API는 없다.

Actor factory와 transfer policy는
[Topology configuration](03-configuration-topology.ko.md)의 `AddActorFactory<TActor,TFactory>(...)` 한 호출에서
등록한다. Policy를 생략하는 overload는 제공하지 않는다.

Create와 GetOrCreate call은 single-use다. 같은 option을 두 번 설정하면 `InvalidConfiguration`, terminal
`Async(...)`를 두 번 호출하면 `AlreadySubmitted`다. Terminal 호출 시 resolve, reservation, factory와 Ready
barrier 전체에 적용할 deadline 하나를 확정한다. `InMesh(...)`를 생략했을 때 object-role Mesh가 하나이면
그 Mesh를 사용하고, 0개이면 `ObjectClientNotConfigured`, 둘 이상이면 `MeshSelectionRequired`다. 명시한 Mesh가
없으면 `MeshNotFound`다. `PlacementProfile`과 `AffinityKey`는 UTF-8 1..255 bytes이며 target RID, predicate와
callback을 지정하지 않는다.

`Create`는 같은 ActorId의 Ready incarnation이 있으면 `ActorAlreadyExists`, stable type이 다르면
`ActorTypeMismatch`다. `GetOrCreate`는 같은 type의 Ready 또는 Creating attempt에 합류하며 CAS loser는 별도
factory를 실행하지 않는다. Creating attempt가 deadline 안에 끝나지 않으면 `DeadlineExceeded`다. Creation
request는 최대 1 MiB이고 reservation 전에 immutable reference와 hash를 기록한다. Factory는 같은 ID,
ObjectGeneration과 creation attempt에 대해 retry-safe해야 한다.

`FindAsync(actorId)`는 current Ready `ActorRef`만 반환한다. `FindSpotAsync(actorId)`는 current User Spot
membership의 `SpotRef`만 반환한다. 별도 Actor directory와 public handle·resolver는 제공하지 않는다.
`DestroyAsync(actorRef)`는 exact incarnation만 종료한다. 해당 incarnation이 없으면 `false`, generation이
다르면 `ActorGenerationStale`, pre-commit seal 중이면 `ActorMoving`이며 current ref를 찾아 hidden retry하지
않는다.

`ActorRef.ObjectGeneration`은 1..`long.MaxValue`다. `MeshName`과 `NodeRid`는 조회 시점의 route snapshot이며
logical identity에는 포함되지 않는다. Transfer 뒤에도 ActorId와 ObjectGeneration은 유지되고 새 location을
가진 ref가 발급된다. 일반 messaging은 ref route를 고정하지 않는다.
