# .NET Actor 공개 인터페이스

[.NET exact interface 목차](README.ko.md)

Session에 bind된 Actor의 physical disconnect는 Framework가 automatic all-settled로 통지한다. Actor
disconnect callback은 destroy·leave·membership 변경이 아니다. Actor relocation은 같은 ObjectGeneration에
대해 owner·membership commit, 필요한 lifecycle callback과 accepted journal replay·logical timer 복원, durable source cleanup,
`Completed` CAS를 차례로 끝낸 뒤 command 44·45로 해당 binding route만 바꾼다. Relocation 자체는
disconnect callback을 실행하지 않는다. 같은 Session의 다른 Actor route와 physical STREAM connection은
유지하며 routed ACK와 steady normalization 전에는 target session packet·push admission을 열지 않는다.

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
    IZLinkActorContext Context { get; }
    void Configure() { }
    ValueTask OnJoinCompletedAsync(
        ZLinkActorJoinCompletion completion,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkActorContext
{
    string ActorId { get; }
    ulong ObjectGeneration { get; }
    string MeshName { get; }
    string? SpotId { get; }
    IZLinkBoundSession BoundSession { get; }
    IZLinkActorJoinSpotCall JoinSpot(string spotId);
    IZLinkActorJoinSpotCall JoinSpot(
        string spotId,
        ZLinkMessage request);
    IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        string spotId,
        TRequest request)
    {
        return JoinSpot(spotId, ZLinkMessage.From(request));
    }
    IZLinkActorJoinEntrySpotCall JoinEntrySpot();
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
        IZLinkActorContext context,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorFactory<TActor>
    : IZLinkActorFactory
    where TActor : class, IZLinkActor
{
    new ValueTask<TActor> CreateAsync(
        IZLinkActorContext context,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorRelocationAdapter<TActor>
    where TActor : class, IZLinkActor
{
    ValueTask<byte[]> CaptureAsync(
        TActor actor,
        CancellationToken cancellationToken);
    ValueTask RestoreAsync(
        TActor actor,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken);
}

public abstract class ZLinkRelocationPolicy<TInstance>
    where TInstance : class
{
    private protected ZLinkRelocationPolicy();

    public static ZLinkRelocationPolicy<TInstance> Disabled { get; }
    public static ZLinkRelocationPolicy<TInstance> Recreate { get; }
    public static ZLinkRelocationPolicy<TInstance> Snapshot<TAdapter>()
        where TAdapter : class;
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
    IZLinkActorCreateCall Timeout(TimeSpan timeout);
    ValueTask<ZLinkActorCreateResult> Async(CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorCreateResult> Yield(CancellationToken cancellationToken = default);
}

public interface IZLinkActorGetOrCreateCall
{
    IZLinkActorGetOrCreateCall InMesh(string meshName);
    IZLinkActorGetOrCreateCall Request(ZLinkMessage request);
    IZLinkActorGetOrCreateCall Request<TRequest>(TRequest request);
    IZLinkActorGetOrCreateCall Timeout(TimeSpan timeout);
    ValueTask<ZLinkActorCreateResult> Async(CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorCreateResult> Yield(CancellationToken cancellationToken = default);
}

public abstract record ZLinkActorCreateResult
{
    private protected ZLinkActorCreateResult() { }

    public sealed record Existing(ActorRef Actor)
        : ZLinkActorCreateResult;

    public sealed record Created(
        ActorRef Actor,
        ZLinkMessage? Reply)
        : ZLinkActorCreateResult;

    public sealed record Rejected(
        ZLinkMessage? Reply)
        : ZLinkActorCreateResult;
}

public interface IZLinkActorSendCall : IZLinkMetadataCall<IZLinkActorSendCall>
{
    ValueTask Async(
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

public interface IZLinkActorDeferredJoinCall
{
    void Defer();
}

public interface IZLinkActorJoinSpotCall : IZLinkActorDeferredJoinCall
{
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);
}

public interface IZLinkActorJoinEntrySpotCall : IZLinkActorDeferredJoinCall
{
    IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout);
}

public readonly record struct ZLinkActorJoinOperationId(ulong High, ulong Low);

public abstract record ZLinkActorJoinCompletion
{
    private protected ZLinkActorJoinCompletion() { }
    public sealed record Accepted(
        ZLinkActorJoinOperationId OperationId,
        ActorRef Actor,
        ZLinkMessage? Reply) : ZLinkActorJoinCompletion;
    public sealed record Rejected(
        ZLinkActorJoinOperationId OperationId,
        ZLinkMessage? Reply) : ZLinkActorJoinCompletion;
    public sealed record Failed(
        ZLinkActorJoinOperationId OperationId,
        ZLinkFrameworkErrorKind Kind,
        bool IsRetriable) : ZLinkActorJoinCompletion;
}
```

Actor packet handler는 Spot이 소유한 registry에 등록한다. Handler의 정확한 context와 generic parameter는
[Spot interface](05-spots.ko.md)가 정의한다. `SpotId == null`은 Entry Spot 단계이고 값이 있으면 해당 user
Spot에 참여한 상태다. 같은 상태를 나타내는 별도 boolean은 제공하지 않는다.

Actor join call은 결과 없는 동기 `Defer()`만 제공하고 `Async(...)`·`Yield(...)`를 제공하지 않는다. `SpotWide` User Spot의 member
Actor가 Actor·Spot·Channel request 또는 worker call을 `Yield(...)`하면 Actor queue claim은 유지하고 User
Spot gate만 반환한다. 같은 Actor의 다음 job은 terminal continuation이 gate를 다시 얻어 현재 job을 완료할
때까지 시작하지 않는다. Entry Spot과 `PerActor` User Spot Actor에서는 request·worker operation submit 전에
`InvalidConfiguration`으로 완료한다.

`Defer()`는 현재 handler에 immutable Join intent를 등록하며 handler 정상 terminal 뒤 실행한다. Target
admission·relocation 결과는 같은 128-bit operation ID의 `OnJoinCompletedAsync(...)` callback으로 전달한다.
Handler가 `Yield(...)`를 사용해도 barrier는 최종 continuation terminal 전에는 활성화하지 않는다.
Operation ID는 completion idempotency ID이며 RelocationId나 reservation ID가 아니다. Same-node outcome과
Rejected·commit 전 Failed completion retry는 current process lifetime으로 제한한다. Cross-node commit 뒤
Accepted만 Relocation manifest에 operation ID, optional reply와 cursor를 보존해 durable at-least-once로 전달한다.
Request 없는 overload는 empty `ZLinkMessage`를 고정한다. Timeout 기본값은 5초이고 명시 값은
millisecond 올림 기준 finite `1..int.MaxValue` ms다. `Defer()`에서 monotonic absolute deadline을 고정한다.

Relocation policy는 Actor factory registration이 소유한다. `Disabled`는 cross-node materialization이 필요한
이동을 capture 전에 거부한다. `Recreate`는 target factory로 같은 logical identity를 다시 만들고 application
state를 복구하지 않는다. `Snapshot<TAdapter>()`은 `IZLinkActorRelocationAdapter<TActor>`가 반환한 byte 배열을
opaque application payload로 저장하고 target Actor instance에 복원한다. 별도 application state generic과 stable
state contract ID를 받지 않으며 Framework message wrapper를 payload로 사용하지 않는다. Adapter는 relocation
reference, accepted journal, relocation phase, source·target owner와 Store CAS version을 받지 않는다.

다른 node에서 Actor instance를 materialize하는 maintenance, cross-node User Spot·Entry Spot join과 whole User
Spot relocation의 모든 Actor participant는 같은 Actor factory policy를 사용한다. `Snapshot`일 때만 Actor adapter의
`CaptureAsync(...)`와 `RestoreAsync(...)`를 호출한다. Same-node join은 adapter를 호출하지 않으며 `Disabled`로
거부하지도 않는다. `Disabled` policy의 cross-node 이동은 adapter 없이 capture 전에 거부한다.

Target은 owner commit 전에 restore와 accepted journal validation·staging만 완료하며 application handler를
실행하지 않는다. Standalone Actor는 owner commit 뒤 lifecycle callback과 old Entry membership의 durable
callback을 완료한 다음 journal replay를 실행하고 old Entry membership을 포함한 source resource를 durable하게
cleanup한다. `Activated`에 도달해도
application과 session ingress는 sealed 상태를 유지하고 bound-session route는 staged 상태로만 준비한다. Source
cleanup이 terminal 상태에 도달하고 authority의 `Completed` CAS가 성공한 뒤에만 target을 `Ready`로 열고 relocation
fence를 해제한다. `Completed`
뒤의 target failure는 ordinary owner loss로 처리하며 이전 relocation을 transparent replay하지 않는다. 이
barrier를 조작하는 public phase API는 제공하지 않는다.

Target replacement가 발생하면 stable relocation 안의 각 attempt가 factory와 `RestoreAsync(...)`를 at-least-once
호출할 수 있고 중단된 stale attempt callback이 successor와 겹칠 수 있다. `CaptureAsync(...)`도 immutable
relocation root가 authority에 연결되기 전까지 반복될 수 있다. 두 callback은 같은 logical relocation에 대해 같은
결과를 내도록 retry-safe해야 하며 외부 side effect의 exactly-once 실행에 의존하면 안 된다. Capture exception은
durable abort와 source normalization 뒤 admission을 복원한다. `CaptureAsync(...)` 결과는 최대 64 MiB이며 빈
배열은 유효하고 null은 contract 위반이다. Framework는 완료된 배열을 즉시 복사한다. `RestoreAsync(...)`의
`ReadOnlyMemory<byte>`는 callback 완료까지만 유효하다. Restore exception이 발생한 instance는 폐기하고 새
attempt의 factory가 만든 instance에 같은 immutable payload를 적용한다. Framework가 operation deadline 때문에
callback을 취소하면 `DeadlineExceeded`로 분류한다. Current exact owner와 attempt fence만 completion을 commit하고
admission을 열 수 있으며 callback에는 relocation ID를 제공하지 않는다.

Relocated terminal reply accounting은 internal command ID 46 `replyRelayAck`를 사용한다. 이 command는 stable
relocation ID, operation ID, exact request-source fence(owner ID, lease generation, node RID, node generation)와
status만 가지며 payload와 metadata를 싣지 않는다. Physical connection close는 terminal 증거가 아니다. ACK 또는
accepted record에 저장한 exact request-source lease expiry만 terminal accounting을 완료하며 public ACK API는 없다.

Source는 connection-bound one-way를 포함해 admission한 모든 connection-bound work가 terminal accounting에
도달한 뒤에만 `Captured`를 commit한다. Durable accepted journal은 exact owner lease가 있는 source에서만
사용한다. Pre-`Captured` drain이 deadline 안에 끝나지 않으면 relocation을 abort하고 host Retire를
`Blocked/DeadlineExceeded`로 끝낸다. Connection-bound one-way를 미완료 상태로 capture하는 예외는 없다.

Entry Spot maintenance와 일반 join에서 실행하는 lifecycle callback의 순서, callback 실패 뒤 sealed retry와 whole
User Spot aggregate move의 callback 생략은 [Spot interface](05-spots.ko.md)가 정한다. Actor relocation adapter는 이
lifecycle callback을 대신하지 않는다. 이 순서를 제어하는 public phase API는 없다.

새 distributed Actor는 generic placement reservation이 authority의 `Creating` row와 target Actor reserved capacity를
함께 확보한 뒤 factory, initial Entry membership과 initialize를 수행한다. 성공하면 같은 reservation을
`Ready`와 active capacity로 commit하고, 실패하면 abort한다. Resolve와 remote messaging은 `Ready`만 사용한다.
CAS loser는 별도 factory를 시작하지 않으며 exact reservation 결과를 read해 reconcile한다. Entry Spot
initialization도 Host `Serving` publication보다 먼저 완료한다. 이 barrier를 위한 application API는 없다.

Actor factory와 relocation policy는
[Topology configuration](03-configuration-topology.ko.md)의 `AddActorFactory<TActor,TFactory>(...)` 한 호출에서
등록한다. Policy를 생략하는 overload는 제공하지 않는다.

Create와 GetOrCreate call은 single-use다. 같은 option을 두 번 설정하면 `InvalidConfiguration`, terminal
`Async(...)` 또는 `Yield(...)`를 두 번 호출하면 `AlreadySubmitted`다. 두 terminal은 같은
`ZLinkActorCreateResult`를 반환한다. `Yield(...)`는 `SpotWide` User Spot 또는 Instance Spot application
callback에서만 현재 Spot gate를 반납하며, 다른 문맥에서는 reservation과 factory 실행 전에
`InvalidConfiguration`으로 완료한다. Terminal 호출 시 resolve, reservation, factory와 Ready
barrier 전체에 적용할 deadline 하나를 확정한다. `InMesh(...)`를 생략했을 때 object-role Mesh가 하나이면
그 Mesh를 사용하고, 0개이면 `ObjectClientNotConfigured`, 둘 이상이면 `MeshSelectionRequired`다. 명시한 Mesh가
없으면 `MeshNotFound`다. Target RID, predicate와
callback을 지정하지 않는다.

`Create`는 같은 ActorId의 Ready incarnation이 있으면 `ActorAlreadyExists`, stable type이 다르면
`ActorTypeMismatch`다. 새 attempt는 `Created` 또는 `Rejected`를 반환한다. `GetOrCreate`는 같은 type의 Ready
Actor를 찾으면 factory와 `OnCreateActorAsync(...)`를 호출하지 않고 `Existing`을 반환하며, Creating
attempt가 있으면 authority 변경을 기다린다. Ready면 `Existing`, rejection cleanup이면 새 reservation으로
자신의 request를 실행한다. 서로 다른 operation은 앞선 `Rejected` reply를 공유하지 않고 동일한
`OperationId`의 retry만 terminal result를 재사용한다. 현재 callback과 동시에 별도 factory를 실행하지 않는다.
Creating 대기가 deadline 안에 끝나지 않으면 `DeadlineExceeded`다. Creation
request는 최대 1 MiB이고 reservation 전에 immutable reference와 hash를 기록한다. Factory는 같은 ID,
ObjectGeneration과 creation attempt에 대해 retry-safe해야 한다.

`FindAsync(actorId)`는 current Ready `ActorRef`만 반환한다. `FindSpotAsync(actorId)`는 current User Spot
membership의 `SpotRef`만 반환한다. 별도 Actor directory와 public handle·resolver는 제공하지 않는다.
`DestroyAsync(actorRef)`는 exact incarnation만 종료한다. 해당 incarnation이 없으면 `false`, generation이
다르면 `ActorGenerationStale`, pre-commit seal 중이면 `ActorMoving`이며 current ref를 찾아 hidden retry하지
않는다.

`ActorRef.ObjectGeneration`은 1..`long.MaxValue`다. `MeshName`과 `NodeRid`는 조회 시점의 route snapshot이며
logical identity에는 포함되지 않는다. Relocation 뒤에도 ActorId와 ObjectGeneration은 유지되고 새 location을
가진 ref가 발급된다. 일반 messaging은 ref route를 고정하지 않는다.
