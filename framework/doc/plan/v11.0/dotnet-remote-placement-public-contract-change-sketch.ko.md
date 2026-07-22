# .NET remote placement public contract 변경 초안

> **문서 상태:** [Actor·Spot remote placement와 node identity 변경 제안](actor-spot-remote-placement-and-node-identity-change-proposal.ko.md)을
> .NET public interface에 대입한 변경 초안이다. 현재 공개 계약이나 구현 기준이 아니다. `TO-BE`의 type과
> member 이름은 M5 이후 공통 spec과 다섯 언어 exact interface review에서 변경할 수 있다.

이 문서의 독자는 RouteMesh 11.0 contract amendment를 작성할 Framework 설계자와 .NET runtime 담당자다.
이 문서는 “현재 .NET interface에서 무엇을 유지하고 무엇을 바꿔야 application이 node RID를 알지 않고
Actor·Spot을 생성하고 호출할 수 있는가?”에 답한다.

`AS-IS`는 현재 [.NET exact interface](../../framework/spec/server/languages/dotnet/interfaces/README.ko.md)의
관련 선언을 대조하기 위한 요약이다. `TO-BE`는 공개 의미와 C# 표현을 검토하기 위한 설계 후보다. 다른
언어는 이 C# signature를 번역하지 않는다. 먼저 Framework 공통 spec에서 공개 의미를 확정한 뒤 C++, .NET,
Java, Kotlin과 Node.js가 같은 기능 수준의 exact interface를 각각 정의한다.

## 1. 변경 원칙

1. ActorId와 User Spot RID는 같은 Location Store namespace의 모든 MeshNode에서 전역으로 유일하며 public
   identity에 MeshName과 owner node RID를 포함하지 않는다.
2. Actor remote create의 필수 입력은 기존처럼 logical actor ID와 stable type이다. Spot create의 필수 입력은
   Spot kind와 operation에 따라 logical Spot ID다. MeshName과 creation payload는 fluent call에서 선택한다.
3. Caller는 target node, endpoint, owner token, generation과 retry policy를 전달하지 않는다.
4. Framework는 Location Store의 `Missing → Creating` authority CAS를 factory 실행 전에 수행한다.
5. Create와 lookup 결과는 exact incarnation과 조회 시점의 MeshName·NodeRid를 담은 immutable snapshot을 반환한다.
   이 위치 정보는 messaging target이 아니며 transfer 뒤 stale할 수 있다.
6. Message API는 ActorId 또는 SpotRid만 받고 Framework가 MeshName, current ObjectGeneration과 owner route를 조회한다.
7. Destroy와 close는 ID만 받지 않고 exact ObjectGeneration을 가진 reference를 사용한다.
8. MeshNode는 object role을 None·Client·Server 가운데 하나로 startup 전에 고정한다.
9. Client role은 remote create·lookup·message만 제공하고 local factory와 placement capability를 게시하지 않는다.
10. Server role은 client capability를 포함하고 등록한 Actor·Spot type의 placement target이 된다.
11. Placement requirement는 type registration에 연결하고 일반 create call의 필수 argument로 노출하지 않는다.
12. MeshNode RID는 기본적으로 diagnostic prefix와 random suffix로 생성한다.
13. Slot number와 deterministic RID naming은 application public contract에서 제거한다.

## 2. Actor identity와 messaging

### 2.1 AS-IS

현재 `ActorRef`는 owner node RID를 공개하고, message call은 MeshName을 별도 argument로 받는다.

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
```

이 shape에서는 Actor가 transfer된 뒤 `ActorRef.NodeRid`가 현재 owner인지 application이 판단할 수 없다.
MeshName도 `ActorRef`와 call에 나뉘어 있어 서로 다른 mesh를 잘못 조합할 수 있다.

### 2.2 TO-BE 후보

ActorId는 같은 Location Store namespace의 모든 MeshNode에서 전역으로 유일한 messaging address다. `ActorRef`는
create 결과나 lifecycle snapshot에서 exact incarnation을 나타내지만 message target으로 사용하지 않는다. Current
MeshName, owner route와 generation resolve는 Framework location cache와 authority가 처리한다.

```csharp
public readonly record struct ActorRef(
    string ActorId,
    ulong Generation,
    string MeshName,
    RoutingId NodeRid);

public interface IZLinkActorClient
{
    IZLinkActorSendCall SendToActor<TMessage>(
        string actorId,
        TMessage message);
    IZLinkActorRequestCall RequestToActor<TRequest>(
        string actorId,
        TRequest request);
}
```

Create, lookup과 messaging은 같은 global `actorId`를 사용한다. MeshName은 create에서 initial placement domain을
선택하는 attribute이며 identity key가 아니다. 서로 다른 Mesh에 같은 ActorId를 동시에 만들 수 없다. `ActorRef`는
Ready object의 exact ObjectGeneration과 조회 시점의 MeshName·NodeRid를 보존한다. NodeRid는 Mesh 안에서만
유일하므로 위치 snapshot에는 두 값을 함께 포함한다. Transfer 뒤 ActorId와 Generation은 유지되고 위치 field가
바뀐 새 `ActorRef`를 resolve한다. Destroy 뒤 같은 ID로 다시 만든 Actor는 새 generation을 가진다.

Send·request terminal method는 `actorId`로 current route와 ObjectGeneration을 한 번 resolve한 뒤 wire frame에
고정한다. Resolve와 target admission 사이에 destroy·recreate가 발생하면 old generation frame을 새 Actor에
적용하지 않고 stale로 끝낸다. Framework가 같은 application operation을 새 generation으로 다시 resolve해
재제출하지 않는다.

TO-BE의 `ActorRef`는 runtime resource를 소유하지 않는 immutable value object이며 JSON과 durable state에서 직접
serialize할 수 있다. 따라서 같은 field를 복제하는 `ActorRefSnapshot`을 제공하지 않는다. Framework runtime은
global `ActorId`별 current MeshName, owner route와 ObjectGeneration을 별도 cache에 보관한다. `ActorRef`는 이 cache나
Location Store의 Ready authority에서 만든 immutable location snapshot이며 transport state나 dispose lifecycle을
소유하지 않는다. 오래된 `ActorRef.NodeRid`는 transfer 뒤 current location이라고 보장하지 않으므로 messaging과
일반 messaging target으로 사용하지 않는다. Session binding은 exact incarnation을 빠르게 연결하기 위해
ActorRef의 MeshName·NodeRid를 initial route hint로 사용할 수 있다.

### 2.3 Membership context의 node RID 제거

현재 Actor context는 Entry Spot 이동에서 target node RID를 받고 Spot context는 local node RID를 공개한다.

```csharp
public interface IZLinkActorContext
{
    string MeshName { get; }
    RoutingId? SpotRid { get; }
    IZLinkActorJoinSpotCall JoinSpot(
        RoutingId spotRid,
        ZLinkMessage request);
    IZLinkActorJoinEntrySpotCall JoinEntrySpot(
        RoutingId spotNodeRid,
        ZLinkMessage request);
}

public interface IZLinkSpotCommonContext
{
    string MeshName { get; }
    RoutingId SpotRid { get; }
    RoutingId NodeRid { get; }
}
```

TO-BE에서는 User Spot membership target도 global `SpotRid`만 사용한다. Target Spot이 다른 node에 있으면 `JoinSpot`이
필요한 Actor relocation을 내부에서 수행한다. Entry Spot 복귀는 Framework가 relocation target과 그 node의 Entry
Spot을 함께 선택하므로 `JoinEntrySpot` argument로 node RID를 받지 않는다. Join request는 선택 항목이므로 `JoinSpot`과
`JoinEntrySpot`의 필수 argument에서 제거하고 join call의 `Request(...)` option으로 제공한다.

```csharp
public interface IZLinkActorContext
{
    ActorRef Ref { get; }
    SpotRef? CurrentSpot { get; }
    IZLinkBoundSession BoundSession { get; }
    IZLinkActorJoinSpotCall JoinSpot(
        RoutingId spotRid);
    IZLinkActorJoinEntrySpotCall JoinEntrySpot();
}

public interface IZLinkActorJoinSpotCall : IZLinkActorJoinCall
{
    IZLinkActorJoinSpotCall Request(ZLinkMessage request);
    IZLinkActorJoinSpotCall Request<TRequest>(TRequest request);
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);
}

public interface IZLinkActorJoinEntrySpotCall : IZLinkActorJoinCall
{
    IZLinkActorJoinEntrySpotCall Request(ZLinkMessage request);
    IZLinkActorJoinEntrySpotCall Request<TRequest>(TRequest request);
    IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout);
}

public interface IZLinkSpotCommonContext
{
    SpotRef Ref { get; }
    IZLinkSpotOutbound Outbound { get; }
}
```

Request를 생략하면 target Spot에는 empty join request를 전달한다. Option 이름은 기존 call builder의
`Timeout(...)`, `Metadata(...)`와 같은 규칙을 따라 `WithRequest(...)`가 아니라 `Request(...)`를 사용한다.

Target Spot과 Actor owner가 같으면 Framework는 membership만 변경한다. 서로 다르면 `JoinSpot`이 Actor relocation을
자동으로 포함한다. Application은 별도 actor transfer 함수를 호출하거나 target node를 전달하지 않는다. Framework는
Actor factory에 등록된 `Disabled`·`Recreate`·`Snapshot` policy를 적용해 state capture와 restore를 수행하고,
Actor owner와 current Spot을 같은 authority commit에서 변경한다. 따라서 join은 다음 경계를 지켜야 한다.

- Target Spot의 join proposal이 거부되면 Actor owner, state와 current membership을 바꾸지 않는다.
- Transfer policy가 `Disabled`이면 cross-node join을 capture 전에 거부한다. Same-node join은 허용한다.
- `Recreate`는 해당 Actor type이 state 복구 없이 다시 만들어져도 된다고 등록한 경우에만 사용한다.
- `Snapshot`은 등록된 typed adapter를 Framework가 호출하며 join call에서 adapter나 state를 받지 않는다.
- Commit 전 timeout·callback failure·CAS conflict는 source Actor를 유지한다.
- Commit 뒤에는 같은 `ActorRef.Generation`을 유지하고 새 NodeRid의 snapshot을 발급하며 restore와 joined callback을 완료한다.
- Request payload는 target Spot의 join 판단에만 사용하고 transfer state나 retry payload로 재사용하지 않는다.

이렇게 하면 `JoinSpot`은 “membership을 바꾸고 필요한 relocation까지 완료한다”는 하나의 깊은 operation이 된다.
Monitoring과 infrastructure handler가 local node identity를 관측해야 하면 application Spot·Actor context가 아니라
runtime snapshot 또는 exact-node context를 사용한다.

## 3. Actor create와 lookup

### 3.1 AS-IS

현재 manager signature에는 target node와 MeshName이 없다. Manager는 현재 Framework runtime에 연결되어 있고,
`actorId`는 node 위치가 아닌 logical Actor ID다. 다만 현재 구현은 이 요청을 local actor creation으로 처리한다.

```csharp
public interface IZLinkActorManager
{
    ValueTask<ActorRef> CreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);
    ValueTask<ActorRef> CreateAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default);
    ValueTask<ActorRef> CreateAsync<TRequest>(
        string actorId,
        string actorType,
        TRequest createRequest,
        CancellationToken cancellationToken = default);

    ValueTask<ActorRef?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default);

    ValueTask<ActorRef> GetOrCreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);
    ValueTask<ActorRef> GetOrCreateAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default);
    ValueTask<ActorRef> GetOrCreateAsync<TRequest>(
        string actorId,
        string actorType,
        TRequest createRequest,
        CancellationToken cancellationToken = default);
}
```

### 3.2 TO-BE 후보

필수 argument인 `actorId`와 `actorType`의 의미는 유지한다. MeshName과 creation payload는 선택 항목이므로 overload를
늘리지 않고 fluent create call에서 지정한다. Manager는 선택된 Mesh에서 actor type을 제공하는 eligible Server
node를 고르고 authority CAS 뒤 해당 node의 factory를 실행한다. Caller는 target node를 지정하지 않는다.

```csharp
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
    ValueTask<ActorRef> Async(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorGetOrCreateCall
{
    IZLinkActorGetOrCreateCall InMesh(string meshName);
    IZLinkActorGetOrCreateCall Request(ZLinkMessage request);
    IZLinkActorGetOrCreateCall Request<TRequest>(TRequest request);
    IZLinkActorGetOrCreateCall Timeout(TimeSpan timeout);
    ValueTask<ActorRef> Async(
        CancellationToken cancellationToken = default);
}

```

기본 호출은 기존 필수 정보만 표현한다. Mesh가 하나이면 다음 호출만으로 생성 위치를 Framework가 할당한다.

```csharp
ActorRef actor = await actors
    .Create("player-42", "player") // Actor ID와 type만 application이 결정한다.
    .Async(cancellationToken);
```

여러 Mesh를 사용하는 host이거나 creation payload가 필요할 때만 선택 항목을 추가한다.

```csharp
ActorRef actor = await actors
    .GetOrCreate("player-42", "player")
    .InMesh("game") // 여러 Mesh 가운데 logical routing domain을 선택하며 target node는 선택하지 않는다.
    .Request(new CreatePlayer("neo")) // CAS winner의 factory에만 전달한다.
    .Async(cancellationToken);
```

Mesh 선택 규칙은 다음 순서로 고정한다.

1. `InMesh(...)`가 있으면 해당 Mesh를 사용한다.
2. 생략했고 object Client 또는 Server role의 Mesh가 하나이면 그 Mesh를 사용한다.
3. 생략했고 후보 Mesh가 없으면 object client가 구성되지 않았다는 오류를 반환한다.
4. 생략했고 후보 Mesh가 둘 이상이면 임의로 선택하지 않고 `MeshSelectionRequired` 오류를 반환한다.

`InMesh(...)`는 Missing object를 처음 배치할 domain만 선택한다. Global ActorId가 이미 존재하면
`GetOrCreate(...)`는 current MeshName과 관계없이 existing Actor를 반환하며 이를 지정한 Mesh로 이동시키지 않는다.
Exclusive `Create(...)`는 existing 오류로 끝난다. Lookup과 messaging은 MeshName을 받지 않는다.

이 규칙은 fluent call을 만드는 시점이 아니라 `Async`에서 현재 완료된 startup configuration을 기준으로
검증한다. `Request(...)`를 생략하면 empty creation request와 같은 의미다. 같은 call에서 `InMesh(...)`나
`Request(...)`를 두 번 지정하면 마지막 값을 조용히 사용하는 대신 configuration 오류를 반환한다.
`Timeout(...)`을 생략하면 Framework의 default request timeout을 create authority 조회, placement,
factory와 Ready barrier 전체 deadline에 사용한다. Timeout이 끝나도 이미 commit된 authority를 caller
cancellation만으로 rollback하지 않고 global ID로 terminal state를 reconcile한다.

`CreateAsync(actorId, actorType, ...)` overload를 그대로 두고 optional overload만 fluent call로 추가하는 대안도
비교했다. 이 방식은 같은 operation에 두 호출 형태가 생기고 sample과 언어별 binding이 어느 표면을 표준으로
사용할지 다시 판단해야 한다. 따라서 TO-BE 후보는 create와 get-or-create를 fluent call 하나로 통합한다.

`CreateAsync`와 `GetOrCreateAsync`의 현재 이름은 fluent call의 `Create`와 `GetOrCreate`로 바뀌지만, 필수 입력과
최종 `ActorRef` 결과는 유지한다. 아래 표의 operation 이름은 각각 fluent call의 `Async` 결과를 뜻한다.

| Operation | Missing에서 CAS winner | 같은 type의 Ready | 같은 type의 Creating | 다른 type |
|---|---|---|---|---|
| `Create` | 새 `ActorRef` | `ActorAlreadyExists` 오류 | Current creation의 terminal 결과를 확인한 뒤 `ActorAlreadyExists` 또는 같은 failure | `ActorTypeMismatch` 오류 |
| `GetOrCreate` | 새 `ActorRef` | 기존 `ActorRef` | Current creation completion에 합류한 뒤 같은 `ActorRef`로 수렴 | `ActorTypeMismatch` 오류 |

`GetOrCreate`가 creation payload의 적용 여부를 별도 result state로 반환하는 대안도 비교했다. 그러나 기존
interface는 Actor 확보를 한 operation으로 감추고 있으며, caller가 생성 경쟁의 승패를 처리하게 만들면
distributed creation 절차가 public contract로 누출된다. 따라서 반환 type도 기존 `ActorRef`를 유지한다.

Creation payload는 CAS winner의 factory와 initial Entry Spot lifecycle에만 전달한다. CAS loser가 existing
object를 반환할 때 payload를 handler message로 바꾸거나 새 owner에게 제출하지 않는다.

`DestroyAsync(actorRef)`는 `ActorId + Generation`을 exact incarnation으로 고정한다. `false`는 해당
incarnation이 이미 없는 경우만 나타내고, 같은 ActorId의 다른 generation이 있거나 transfer seal이
진행 중인 경우는 typed framework error로 구분한다. Destroy call이 current owner에 admission된 뒤
Framework가 같은 operation을 다른 generation으로 hidden retry하지 않는다.

현재 `IZLinkActorDirectory.FindAsync(meshName, actorId)`는 global ActorId 계약과 중복된다. TO-BE에서는
`IZLinkActorManager.FindAsync(actorId)`로 통합하고 `IZLinkActorDirectory`를 제거한다. 기존
`IZLinkActorSpotHandleResolver` 기능을 유지해야 하면 handle을 반환하지 않고 global ActorId로 current
`SpotRef?`를 조회하는 Manager query로 옮긴다. Exact member 이름은 contract amendment에서 고정한다.

### 3.3 Session binding by exact ActorRef

현재 session binding은 local Actor instance 또는 `ActorRef`를 받는다. Local instance overload는 remote placement와
맞지 않지만 create·lookup이 반환한 ActorRef route는 immediate bind의 Store read를 줄이는 initial route로 사용할
수 있다.

```csharp
public interface IZLinkSessionActors
{
    ValueTask<IZLinkSessionActor> BindAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);
    ValueTask<IZLinkSessionActor> BindAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);
    ValueTask<IZLinkSessionActor> BindOrGetAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);
}
```

Session binding은 장기간 유지되는 관계이므로 messaging과 달리 exact Actor incarnation을 입력으로 받는다. Caller는
create 또는 lookup이 반환한 `ActorRef`를 전달한다. Framework는 ActorRef의 MeshName·NodeRid로 bind control request를
먼저 전송하고 `ActorId + Generation`을 exact binding identity로 사용한다. Bind source가 Location Store에서 current
route를 선조회하지 않는다.

```csharp
public interface IZLinkSessionActors
{
    IReadOnlyCollection<IZLinkSessionActor> Bound { get; }

    ValueTask<IZLinkSessionActor> BindAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkSessionActor> BindOrGetAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);

    IZLinkSessionActor? Find(string actorId);
}
```

Target node에 exact Actor가 있으면 current authority, AuthorityOwnerGeneration과 owner lease를 검증한 뒤 binding을
commit한다. Actor가 없고 active forwarding entry가 있으면 original bind control request와 reply route를 mapping
target으로 relay한다. User Spot aggregate transfer도 member Actor의 bind request를 같은 forwarding window에서 relay한다.

Mapping이 없거나 만료됐으면 `ActorLocationStale`, 같은 ID의 current generation이 다르면
`ActorGenerationStale`, transfer pre-commit seal 중이면 `ActorMoving`으로 끝낸다. Framework는 Location Store를 읽어
같은 bind operation을 다른 route로 hidden retry하지 않는다. Bind가 commit된 뒤 Actor가 transfer되면 Framework가
binding route와 owner fence를 갱신하며 session application은 다시 bind하지 않는다.

`BindOrGetAsync(actorRef)`의 `Get`은 Actor create나 directory get을 뜻하지 않는다. 같은 session에 해당
`ActorId + Generation`의 current binding이 있으면 그 `IZLinkSessionActor`를 반환하고, 없으면 `BindAsync(actorRef)`와
같은 bind를 시도한다. 같은 ActorId라도 generation이 다르면 기존 binding을 새 incarnation으로 간주하지 않는다.
이름은 정식 interface review에서 `GetOrBindAsync(actorRef)`로 바꿀지도 함께 검토한다.

`IZLinkSessionActor.Ref`는 current binding route에서 만든 location snapshot을 반환한다. Transfer 전 snapshot을 bind에
전달해도 active forwarding mapping이 current owner까지 relay할 수 있다. Local `IZLinkActor` overload와 string
actorId overload는 제거 후보다.

Global ActorId가 MeshName을 resolve하므로 STREAM Actor dispatch도 한 MeshName을 caller가 고정할 이유가 없다.
`EnableActorDispatch(string meshName)`은 다음처럼 MeshName 없는 capability enablement로 바꾸는 후보가 된다.

```csharp
public interface IZLinkStreamNodeBuilder
{
    IZLinkStreamNodeBuilder EnableActorDispatch();
}
```

## 4. Spot identity, create와 close

### 4.1 AS-IS

현재 `SpotHandle`은 owner node RID를 노출하지 않지만 create 결과는 Spot RID만 반환한다. Manager의 create와
close는 local MeshNode를 대상으로 하며 `CloseAsync`는 generation 없는 `(MeshName, SpotRid)`를 받는다.

```csharp
public abstract class SpotHandle
{
    public abstract string MeshName { get; }
    public abstract RoutingId SpotRid { get; }
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

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        string meshName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotInfo?> FindAsync(
        string meshName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<bool> CloseAsync(
        string meshName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}
```

### 4.2 TO-BE 후보

User Spot RID는 같은 Location Store namespace의 모든 MeshNode에서 전역으로 유일하다. Create의 MeshName과 creation
payload는 Actor와 같은 fluent 선택 항목으로 제공하지만 MeshName은 identity에 포함하지 않는다. Messaging과 lookup은
Spot RID만 사용한다. Create 결과의 `SpotRef`는 owner route와 local object를 노출하지 않는 exact incarnation
snapshot이다. Close는 stale caller가 같은 RID의 새 incarnation을 닫지 못하도록 exact generation의 reference를
받는다.

```csharp
public readonly record struct SpotRef(
    RoutingId SpotRid,
    ulong Generation,
    string MeshName,
    RoutingId NodeRid);

public readonly record struct ZLinkSpotCreateResult(
    SpotRef Spot,
    ZLinkSpotCreateState State,
    ZLinkMessage? Reply);

public interface IZLinkSpotClient
{
    IZLinkSendCall SendToSpot<TMessage>(
        RoutingId spotRid,
        TMessage message);
    IZLinkRequestCall RequestToSpot<TRequest>(
        RoutingId spotRid,
        TRequest request);
}

public interface IZLinkSpotManager
{
    IZLinkSpotCreateCall Create(
        string spotType);

    IZLinkSpotGetOrCreateCall GetOrCreate(
        RoutingId spotRid,
        string spotType);

    IZLinkInstanceSpotGetOrCreateCall GetOrCreateInstance(
        RoutingId spotRid,
        string instanceSpotType);

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
    IZLinkSpotCreateCall Timeout(TimeSpan timeout);
    ValueTask<ZLinkSpotCreateResult> Async(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotGetOrCreateCall
{
    IZLinkSpotGetOrCreateCall InMesh(string meshName);
    IZLinkSpotGetOrCreateCall Request(ZLinkMessage request);
    IZLinkSpotGetOrCreateCall Request<TRequest>(TRequest request);
    IZLinkSpotGetOrCreateCall Timeout(TimeSpan timeout);
    ValueTask<ZLinkSpotCreateResult> Async(
        CancellationToken cancellationToken = default);
}

public interface IZLinkInstanceSpotGetOrCreateCall
{
    IZLinkInstanceSpotGetOrCreateCall InMesh(string meshName);
    IZLinkInstanceSpotGetOrCreateCall Timeout(TimeSpan timeout);
    ValueTask<SpotRef> Async(
        CancellationToken cancellationToken = default);
}
```

Mesh가 하나이고 payload가 없으면 `spots.Create("room").Async(...)`만 호출한다. 여러 Mesh 가운데 하나를
선택하거나 payload가 필요하면 `InMesh(...)`와 `Request(...)`를 `Async(...)` 앞에 연결한다. Actor와
같은 Mesh 선택 규칙을 적용하며, MeshName을 생략했다고 target node가 local로 고정되지 않는다.

`Create(spotType)`은 Framework가 logical Spot RID와 target node를 각각 선택한다. Spot RID는 node RID와 다른
namespace와 수명을 가지며 random node identity에서 파생하지 않는다. 생성된 RID는 MeshName과 관계없이 Location
Store namespace 전체에서 유일해야 한다. `GetOrCreate(spotRid, spotType)`의 CAS loser는 current creation에 합류하거나
existing reference를 반환하며 별도 factory를 실행하지 않는다.

Client-only node는 target implementation type이나 factory를 등록하지 않으므로 public create call이 `TSpot`을
remote type identity로 사용하지 않는다. `spotType`은 Server의 `AddSpotFactory<TSpot>(spotType, ...)`가
게시한 stable type을 선택하며 implementation class는 target node 내부에 남는다.

Instance Spot도 최초 Missing RID에서는 `GetOrCreateInstance(spotRid, instanceSpotType)`로 type과 initial Mesh를
고정한다. 이 operation이 Ready를 반환한 뒤는 `IZLinkSpotClient` 및 `IZLinkSpotOutbound`가 SpotRid만
받는다. Owner loss 뒤 reactivation은 authority에 저장된 type과 Mesh를 사용한다. TO-BE에서
`InstanceSpotAddress`, `SpotHandle`, `IZLinkSpotHandleResolver`와 address·handle client overload를 제거한다.

`SpotRef`는 local Spot instance나 scope를 보유하지 않고 Ready authority를 조회한 시점의 MeshName·NodeRid를 담는
value object다. Application은 session, API response와 durable state에서 exact incarnation과 location snapshot으로
전달할 수 있다. Messaging terminal method는 `SpotRid`별 current MeshName, owner route와 ObjectGeneration을
resolve하고 frame admission에 generation을 고정한다. Transfer 뒤에는 SpotRid와 Generation을 유지하고 위치 field가
바뀐 새 `SpotRef`를 조회한다. Server 내부에서 local object lifetime을 관리하는 handle이 필요하면 public snapshot과
구분된 internal type이 소유한다.

기존 `ListAsync(meshName)`은 global namespace의 unbounded application query로 확장하지 않는다. Manager에서
제거하고, 운영상 목록이 필요하면 Location runtime query가 kind·type·MeshName filter와 page token을
받아 `SpotRef` projection을 반환한다. `ZLinkSpotInfo`도 `SpotRef`와 중복되므로 제거한다.

## 5. MeshNode object role과 factory registration

### 5.1 AS-IS

Factory는 MeshNode builder에 직접 등록한다. Factory가 없으면 결과적으로 local Actor·Spot을 만들 수 없지만,
object client와 server intent를 startup configuration에서 명시적으로 구분하지 않는다. User Spot factory는
.NET class만 등록하고 Instance Spot만 stable string type을 명시한다.

```csharp
public interface IZLinkMeshNodeBuilder
{
    IZLinkMeshNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot;
    IZLinkMeshNodeBuilder AddInstanceSpotFactory<TSpot>(
        string instanceSpotType,
        ZLinkInstanceSpotFactoryOptions? options = null)
        where TSpot : class, IZLinkInstanceSpot;
}
```

### 5.2 TO-BE 후보

MeshNode는 object role을 한 번 선택한다. `Client()`는 manager, directory와 message client만 활성화하고 Entry
Spot이나 factory를 만들지 않는다. `Server()`는 client capability를 포함하고 factory·capacity·stable type
capability를 descriptor에 게시한다.

```csharp
public interface IZLinkMeshNodeBuilder
{
    IZLinkMeshNodeBuilder SetWeight(int weight);
    IZLinkMeshObjectRoleBuilder Objects();
}

public interface IZLinkRouteMeshRuntimeOptions
{
    IZLinkMeshNodeRuntimeOptions Mesh(string meshName);
    IZLinkMeshChannelRuntimeOptions Channel(string channelName);
}

public interface IZLinkMeshNodeRuntimeOptions
{
    int Weight { get; set; }
}

public interface IZLinkMeshObjectRoleBuilder
{
    IZLinkMeshObjectClientBuilder Client();
    IZLinkMeshObjectServerBuilder Server();
}

public interface IZLinkMeshObjectClientBuilder
{
}

public interface IZLinkMeshObjectServerBuilder
{
    IZLinkMeshObjectServerBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : class, IZLinkEntrySpot;

    IZLinkMeshObjectServerBuilder AddSpotFactory<TSpot>(
        string spotType,
        ZLinkObjectPlacementOptions? placement,
        ZLinkTransferPolicy<TSpot> transfer)
        where TSpot : class, IZLinkSpot;

    IZLinkMeshObjectServerBuilder AddInstanceSpotFactory<TSpot>(
        string instanceSpotType,
        ZLinkInstanceSpotFactoryOptions? options,
        ZLinkObjectPlacementOptions? placement,
        ZLinkTransferPolicy<TSpot> transfer)
        where TSpot : class, IZLinkInstanceSpot;

    IZLinkMeshObjectServerBuilder AddActorFactory<TActor, TFactory>(
        string actorType,
        ZLinkObjectPlacementOptions? placement,
        ZLinkTransferPolicy<TActor> transfer)
        where TActor : class, IZLinkActor
        where TFactory : class, IZLinkActorFactory<TActor>;
}
```

Object role을 설정하지 않은 MeshNode는 object client service도 등록하지 않는다. `Objects().Client()`를 선택한
MeshNode는 remote create와 message를 시작할 수 있지만 descriptor에 Actor·Spot capability를 게시하지 않으므로
placement 후보에서 제외된다. `Objects().Server()`를 선택한 MeshNode는 등록한 type을 local에 생성할 수 있고
같은 client operation도 시작할 수 있다.

Object Client 또는 Server role을 선택한 host는 `IZLinkLocationStore`를 필수로 등록한다. Store가 없으면
startup validation이 실패한다. Object role이 없는 manual topology는 Store 없이 Node direct·Channel을
사용할 수 있지만, 같은 Manager 이름으로 local-only Actor·Spot 생성을 별도 제공하지 않는다.

Server가 client capability를 포함하므로 `Client()`와 `Server()`를 동시에 호출하지 않는다. Channel Server가
자기 ChannelName으로 outbound operation을 시작할 수 있는 현재 의미와 같은 방향이다. Role을 두 번 선택하거나
Client builder에서 factory를 등록하려는 구성은 startup 전 configuration error다.

`SetWeight(...)`와 runtime `Mesh(meshName).Weight`는 MeshNode ROUTER가 새 부하를 받을 비율을 같은
값으로 설정한다. Actor·Spot 생성과 transfer target 선택은 이 node-wide weight를 사용한다.
`Channel(channelName).Weight`는 해당 Channel의 select-one 비율이며 object placement weight로 해석하지
않는다. Node weight 0은 기존 object와 node direct를 종료하지 않고 새 placement·transfer target만
제외한다.

| 배포 예 | MeshNode object role | 결과 |
|---|---|---|
| Session과 gameplay를 한 process에서 함께 처리 | Server | Local hosting과 logical create·message를 모두 제공한다 |
| Session이 별도 Play process의 Actor·Spot만 사용 | Session은 Client, Play는 Server | Session descriptor는 object placement capability를 게시하지 않는다 |
| Object 기능을 사용하지 않는 gateway | 설정하지 않음 | Manager·directory·factory와 object capability를 만들지 않는다 |

Remote placement는 descriptor에서 User Spot type capability를 언어와 무관한 stable name으로 비교해야 한다.

Actor, User Spot과 Instance Spot factory는 모두 transfer policy를 명시한다. Adapter가 없는 구성을 지원하되 이를
암묵적인 누락으로 처리하지 않는다. `Disabled`는 normal create와 message를 허용하지만 active instance의 owner
relocation을 허용하지 않는다. `Recreate`는 adapter 없이 target factory를 다시 실행하고 `Snapshot`만 typed
adapter를 요구한다. Policy를 생략하는 overload를 제공할지는 migration 검토 대상이지만, 제공하더라도 의미는
문서화된 `Disabled` 외의 값으로 추론하지 않는다.

User Spot maintenance의 transfer 단위는 Spot 하나가 아니다. Framework는 User Spot과 seal 시점의 current member
Actor 전체를 internal aggregate로 만들고, 각 object에 등록된 policy를 preflight한다. 하나라도 `Disabled`이거나
target capability·adapter compatibility를 만족하지 않으면 capture 전에 전체 `Retire`를 차단한다.

모든 participant가 준비되면 Framework는 Spot owner, member Actor owner와 membership을 하나의 aggregate commit
barrier로 전환한다. Commit 전 실패는 source 전체를 유지하고, commit 뒤 failure는 일부 object를 source로
rollback하지 않고 같은 aggregate identity로 target 전체를 recovery한다. `SpotRef.Generation`과 각
`ActorRef.Generation`은 owner transfer에서 바뀌지 않지만 Ready resolve가 반환하는 NodeRid는 target으로 바뀐다.
이 절차를 조합하는 public transfer-group interface는 제공하지 않는다.

`ZLinkObjectPlacementOptions`는 이름만 제시한 설계 후보이며 아직 public member를 확정하지 않는다. Common create
path가 placement detail을 받지 않도록 type registration에 연결한다. Contract amendment는 최소한 다음 요구를
검토한다.

- Required capability 또는 placement profile
- Region·zone 같은 placement domain
- Affinity 또는 co-location group 사용 여부
- Type별 local active object와 pending activation 상한
- Placement requirement와 transfer target eligibility의 동일성
- Router의 신규 부하 배정에 사용하는 운영 weight를 Actor·Spot 생성과 transfer target 선택에도
  사용하는 규칙
- Node 전체와 type별 active·pending capacity를 weight보다 먼저 적용하는 후보 필터링 순서
- ID creation authority와 target node pending capacity를 함께 reservation하는 Location Store transaction

Type별 상한을 `ZLinkObjectPlacementOptions`에 두더라도 node 전체 Actor·User Spot·Instance Spot 상한을
설정하고 descriptor에 게시하는 Object Server capacity interface가 추가로 필요하다. Exact member는
active 상한과 pending reservation 상한을 별도로 받을지, 유한한 하나의 total 상한에서 Framework가
pending을 차감할지를 contract amendment에서 고정한다. Application create call에 capacity option을 넘기지
않는다.

문자열 label dictionary를 그대로 공개하면 caller와 deployment가 내부 selector 규칙을 공유하게 된다. 우선
공통 사용 사례를 충족하는 작은 typed option을 비교하고, 드문 배포 제약은 provider extension에 둘 수 있는지
검토한다.

Placement는 capacity를 먼저 적용한 뒤 남은 node를 같은 운영 weight 비율로 선택한다. Weight 0은
기존 object를 종료하지 않고 새 placement와 transfer target만 차단한다. `GetOrCreate`가 기존 object를
찾으면 capacity와 weight를 다시 판정하지 않는다. Capacity reservation 경쟁에서 패배한 신규 생성은
factory 실행 전에 다른 eligible node를 선택할 수 있고, 모든 후보의 capacity가 차있으면
terminal placement error를 반환한다.

별도 placement weight는 추가하지 않고 MeshNode ROUTER의 node-wide public weight를 공유한다. 여러
Channel weight를 평균하거나 가장 큰 값을 선택하는 암묵적 규칙은 사용하지 않는다.

## 6. Route cache와 stale-route forwarding

### 6.1 AS-IS

현재 .NET runtime에는 Actor transfer 뒤 source node가 bounded window 동안 stale Actor route를 target으로 전달하는
구현과 `ActorTransferForwardWindow` option이 있다. V11 target의 기본 forwarding window는 5초다. Location cache는
route snapshot과 deadline을 보관하도록 설계되어 있지만 public cache max-age와 User Spot forwarding 계약은 없다.

```csharp
public interface IZLinkFrameworkOptions
{
    TimeSpan ActorTransferForwardWindow { get; set; }
    ZLinkLocationOptions ConfigureLocations();
}
```

### 6.2 TO-BE 후보

Global ActorId와 SpotRid의 resolve 결과를 공통 location cache에 보관한다. Actor·User Spot·Instance Spot의
transfer가 같은 stale-route 문제를 가지므로 Actor 전용 forwarding option을 location 공통 option으로 옮긴다.

```csharp
public sealed class ZLinkLocationOptions
{
    public TimeSpan RouteCacheMaxAge { get; set; } = TimeSpan.FromSeconds(15);
    public TimeSpan TransferForwardingWindow { get; set; } = TimeSpan.FromSeconds(30);
}
```

`RouteCacheMaxAge`는 positive Ready route를 authoritative refresh 없이 사용할 수 있는 최대 기간이다. Cache entry는
logical key, ObjectGeneration, AuthorityOwnerGeneration, StoreVersion, owner lease, owner node lifecycle과 current
connection을 함께 보존한다. Location watch, higher StoreVersion, lease expiry와 explicit stale result는 15초를
기다리지 않고 entry를 invalidate한다.

`TransferForwardingWindow`는 commit 뒤 source node가 이전 Actor·Spot route로 받은 message를 current owner로
전달하는 기간이다. 기본값 후보는 30초다. 양수이면 `RouteCacheMaxAge`보다 커야 하며, 15초와 30초 조합은 cache가
transfer 직전에 채워진 경우에도 queue·network delay를 위한 여유를 남긴다. Exact validation에는 최대 relay delay와
scheduling margin도 포함해야 한다. `TransferForwardingWindow == TimeSpan.Zero`는 forwarding을 끄며 이 경우 stale
cache로 보낸 operation은 명시적인 stale result로 끝난다.

Message를 받은 node에 exact Actor·Spot instance가 없고 같은 ObjectGeneration과 source owner fence의 active
forwarding entry가 있으면 entry target으로 그대로 relay한다. Relay node는 handler를 실행하지 않으며 Location
Store에서 current authority를 다시 조회하지 않는다. A에서 B로, 다시 B에서 C로 transfer됐다면 active mapping에
따라 `A → B → C` relay가 가능하다. 각 mapping은 committed transfer에서만 만들고 target
AuthorityOwnerGeneration이 source보다 높아야 한다. Mapping이 없거나 만료됐거나 generation이 다르면 stale
result로 끝낸다. Original operation identity, payload와 reply route를 모든 relay가 그대로 보존한다.

User Spot aggregate transfer는 Spot과 모든 member Actor forwarding entry를 aggregate commit과 함께 설치한다.
Forwarding entry와 relay queue는 active object capacity와 transfer inventory로 bound하며 window가 끝나면 제거한다.
Negative lookup은 positive route와 같은 15초 동안 보관하지 않는다.

별도 cache option을 노출하지 않고 forwarding window에서 절반 값으로 derive하는 대안은 invalid 조합을 없앤다.
반면 Store 부하와 route churn을 독립적으로 조정할 수 없다. 정식 contract amendment에서는 두 값을 public option으로
둘지, 30초/15초의 하나의 location profile로 고정할지 결정한다.

## 7. Routing ID configuration

### 7.1 AS-IS

현재 MeshNode와 fanout publisher는 fixed RID 또는 slot 기반 RID allocation을 선택한다.

```csharp
public interface IZLinkMeshNodeBuilder
{
    IZLinkMeshNodeBuilder SetRoutingId(RoutingId routingId);
    IZLinkMeshNodeBuilder UseAllocatedRoutingId(int slotCount);
    IZLinkMeshNodeBuilder UseAllocatedRoutingId(
        int slotCount,
        string routingIdPrefix);
    IZLinkMeshNodeBuilder SetRoutingIdAllocationGroup(string groupName);
}

public interface IZLinkFanoutChannelBuilder
{
    IZLinkFanoutChannelBuilder SetRoutingId(RoutingId publisherRoutingId);
    IZLinkFanoutChannelBuilder UseAllocatedRoutingId(int slotCount);
    IZLinkFanoutChannelBuilder UseAllocatedRoutingId(
        int slotCount,
        string routingIdPrefix);
    IZLinkFanoutChannelBuilder SetRoutingIdAllocationGroup(string groupName);
}
```

Slot allocation은 `IZLinkRoutingIdSlotAllocationStore`, `IZLinkAllocatedRoutingIdProvider`와 slot·group result type을
public provider contract로 노출한다.

### 7.2 TO-BE 후보

Automatic discovery를 사용하는 topology는 random RID를 기본으로 만들고 caller가 diagnostic prefix만 설정한다.

```csharp
public interface IZLinkMeshNodeBuilder
{
    IZLinkMeshNodeBuilder SetRoutingIdPrefix(string prefix);
}

public interface IZLinkFanoutChannelBuilder
{
    IZLinkFanoutChannelBuilder SetRoutingIdPrefix(string prefix);
}
```

`AddRouteMesh(meshName)`과 automatic fanout publisher는 prefix를 생략해도 Framework 기본 prefix와 random suffix로
RID를 만든다. Slot count, allocation group과 numeric slot은 public configuration과 readiness result에서 제거한다.
따라서 다음 public type도 제거 후보가 된다.

- `IZLinkRoutingIdSlotAllocationStore`
- `IZLinkAllocatedRoutingIdProvider`
- `ZLinkRoutingIdSlot*` request·result·snapshot
- `ZLinkAllocatedRoutingId`와 `ZLinkFanoutAllocatedRoutingId`

Fixed `SetRoutingId(...)`는 다음 두 대안을 비교한다.

| 대안 | 장점 | 제약 |
|---|---|---|
| Automatic·manual 모두 제거 | 모든 production node가 opaque lifecycle identity를 사용한다 | Expected RID가 필요한 manual peer와 일부 isolated test 구성을 바꿔야 한다 |
| Manual topology에서만 유지 | Store 없는 closed topology가 exact peer identity를 검증할 수 있다 | Fixed RID가 application naming rule로 다시 사용되지 않도록 mode validation이 필요하다 |

우선 방향은 manual topology에서만 유지하는 것이다. Location Store descriptor를 게시하거나 automatic discovery를
사용하는 MeshNode에서 `SetRoutingId(...)`를 호출하면 startup configuration error로 거부한다. Test는 가능하면
runtime snapshot에서 생성된 RID를 읽고 특정 문자열을 가정하지 않는다.

Random RID uniqueness는 slot allocation과 다른 provider capability다. CSPRNG 충돌 가능성을 충분히 낮추더라도
descriptor publish는 `(MeshName, RID)`의 active owner conflict를 원자적으로 검출해야 한다. Exact store interface를
새로 추가할지 host owner lease와 descriptor create CAS에 포함할지는 Location provider contract review에서
결정한다.

## 8. Location Store provider surface

현재 `IZLinkAuthorityStore.CompareExchangeAuthorityAsync(...)`는 authority key 하나만 CAS한다. 따라서
global ID의 `Missing → Creating` 전이와 target node의 pending capacity 차감을 함께 보장할 수 없다.
Descriptor의 `Available`을 먼저 읽고 authority를 나중에 CAS하면 동시 생성이 같은 node capacity를
초과할 수 있다. ID authority와 node capacity를 별도 operation으로 갱신한 뒤 compensation으로
맞추는 방식도 crash 사이의 누수를 제거하지 못한다.

따라서 `IZLinkLocationStore`는 object 종류의 lifecycle을 해석하지 않으면서 다음 세 전이를 원자적으로
제공하는 generic placement reservation capability를 함께 제공해야 한다.

```csharp
public interface IZLinkPlacementReservationStore
{
    ValueTask<ZLinkPlacementReserveResult> ReservePlacementAsync(
        ZLinkPlacementReserveRequest request,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkPlacementCommitResult> CommitPlacementAsync(
        ZLinkPlacementReservation reservation,
        ZLinkAuthorityMutation readyMutation,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkPlacementAbortResult> AbortPlacementAsync(
        ZLinkPlacementReservation reservation,
        CancellationToken cancellationToken = default);
}
```

`ReservePlacementAsync(...)`는 object authority expectation, target `ZLinkMeshNodeDescriptorKey`, object kind·stable type과
Creating mutation을 받아 ID authority 생성과 pending capacity 차감을 함께 적용한다.
`CommitPlacementAsync(...)`는 같은 reservation fence에서 Creating을 Ready로 바꾸고 pending을 active로
전환한다. `AbortPlacementAsync(...)`는 exact reservation이 소유한 Creating authority와 pending capacity를
함께 정리한다. Request·result·reservation의 exact field와 exhaustion result는 protocol amendment에서
고정한다. Provider에 `CreateActorRowAsync` 또는 `CreateSpotRowAsync`처럼 Framework object별 method를
추가하지 않는다.

Global identity를 적용하면 `ZLinkActorLocationKey`는 ActorId만, `ZLinkSpotLocationKey`는 SpotRid만
소유한다. MeshName은 location value의 current placement attribute로 남고 authority key에 포함하지
않는다. Location event, `ZLinkLocationKey`, scan prefix와 Redis key schema도 같은 global key로 변경한다.

`ZLinkMeshNodeDescriptor`는 node-wide Router weight와 node 전체 active·pending capacity를 게시한다.
`ZLinkPlacementObjectKind`는 Actor·UserSpot·InstanceSpot을 모두 표현하고 `ZLinkObjectCapability`가
kind·stable type·transfer policy·readable state contract·type별 capacity를 함께 소유한다. 현재의 별도
`SpotTypes` set은 제거한다.

Provider contract은 이 capability와 함께 random RID active-owner conflict와 durable creation intent의
size·retention을 검토한다.

### 8.1 함께 변경할 public surface

Manager·Client signature만 변경하면 local·Mesh-scoped 의미가 context, resolver, provider와 운영 API에
남는다. Contract amendment는 다음 disposition을 같은 change set으로 처리한다.

| 현재 surface | TO-BE disposition |
|---|---|
| `IZLinkActorDirectory` | `IZLinkActorManager.FindAsync(actorId)`로 통합하고 제거한다 |
| `SpotHandle`, `IZLinkSpotHandleResolver` | `SpotRef`와 `IZLinkSpotManager.FindAsync(spotRid)`로 대체하고 제거한다 |
| `IZLinkActorSpotHandleResolver` | Global ActorId로 current `SpotRef?`를 조회하는 Manager query로 대체한다 |
| `IZLinkSpotOutbound` | User·Instance Spot 모두 `SendToSpot(spotRid, ...)`·`RequestToSpot(spotRid, ...)`를 사용한다 |
| `InstanceSpotAddress` | Initial create intent를 Manager로 옮기고 message overload와 type을 제거한다 |
| `ZLinkSpotInfo`, `IZLinkSpotManager.ListAsync(meshName)` | `SpotRef`와 paged Location operational query로 대체한다 |
| `IZLinkEntrySpotOptions`, `ConfigureEntrySpot()`, `SetEntrySpotRoutingId(...)` | Object Server의 `AddEntrySpot(...)`에 통합하고 Entry identity는 Framework가 발급한다 |
| `IZLinkFrameworkOptions.ActorTransferForwardWindow` | Actor·Spot 공통 `ZLinkLocationOptions.TransferForwardingWindow`로 옮긴다 |
| `ZLinkActorLocationKey`, `ZLinkSpotLocationKey` | MeshName을 key에서 제거하고 global ID만 사용한다 |
| `ZLinkMeshNodeDescriptor` | Node-wide Router weight, object role·capability와 active·pending capacity를 추가한다 |
| `ZLinkPlacementObjectKind`, `ZLinkObjectCapability` | User Spot을 포함하고 세 object kind의 transfer·capacity 표현을 통합한다 |
| `IZLinkLocationRuntimeQuery` | Global Actor·Spot paged query와 node·type별 placement capacity projection을 추가한다 |
| `ZLinkFrameworkErrorKind` | Mesh 선택, placement capacity, generation stale, moving, transfer disabled와 Store failure를 구분한다 |
| `ZLinkMeshNodeSnapshot`·runtime event | Node weight, active·pending·max capacity, reservation failure와 placement 결과를 추가한다 |
| DI registration·startup validation | Object Client·Server role이 있으면 Location Store를 필수로 하고 role에 따라 Manager·Client·factory를 노출한다 |

`IZLinkSpotOutbound`의 TO-BE 형태는 외부 client와 같은 logical target 계약을 사용한다.

```csharp
public interface IZLinkSpotOutbound
{
    IZLinkSendCall SendToSpot<TMessage>(
        RoutingId spotRid,
        TMessage message);
    IZLinkRequestCall RequestToSpot<TRequest>(
        RoutingId spotRid,
        TRequest request);
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
```

`IZLinkTransferStateAdapter`, `ZLinkTransferPolicy`, Actor·Spot handler, factory callback과 `IZLinkBoundSession`은
target 선택 위치를 caller에게 노출하지 않으므로 기본 형태를 유지한다. Local Actor instance를
받는 Spot lifecycle callback은 aggregate target에 materialize된 instance에서만 실행하므로 remote address로
바꾸지 않는다.

현재 .NET exact interface의 `IZLinkActorManager`는 MeshName argument를 받지만 source interface는 이미 global
ActorId와 `FindAsync(actorId)`를 사용한다. Contract amendment inventory는 exact spec을 AS-IS 기준으로
삼되, 이 baseline drift를 별도 retain·replace disposition으로 기록하고 누락된 current source member를 조용히
정식 계약으로 취급하지 않는다.

## 9. AS-IS / TO-BE 요약

| 범위 | AS-IS | TO-BE 후보 |
|---|---|---|
| Actor identity | `NodeRid + ActorId + Generation`, MeshName 별도 | Global `ActorId`, snapshot은 `ActorId + Generation + MeshName + NodeRid` |
| Actor serialization | 같은 field의 `ActorRefSnapshot`을 별도로 사용 | Immutable `ActorRef`를 직접 사용하고 snapshot type 제거 |
| Actor message | `meshName`과 `ActorRef`를 함께 전달 | Global `actorId`만 전달하고 Framework가 generation·route resolve |
| Actor membership | Spot RID·Entry Spot node RID·request를 필수 전달 | Global `spotRid`만 필수로 받고 request는 fluent option, cross-node relocation은 Framework가 수행 |
| Spot server context | MeshName, Spot RID와 Node RID를 공개 | `SpotRef`만 공개하고 node identity는 monitoring으로 분리 |
| Actor create | Async overload에 필수·선택 입력을 함께 전달하고 local에서 실행 | 필수 입력은 유지하고 MeshName·payload는 fluent call에서 선택, Framework가 remote placement 수행 |
| Actor create result | `ActorRef` | 유지. Actor 확보 결과만 반환 |
| Actor lookup | `actorId` | Global `actorId`만 사용하고 Framework가 MeshName·route resolve |
| Actor destroy | Entry Spot의 local Actor instance 기반 | Exact `ActorRef`를 Manager에 전달하여 stale incarnation 종료를 차단 |
| Session Actor bind | Local `IZLinkActor` 또는 route-bearing `ActorRef` | Ref 위치로 먼저 전송하고 ID·generation을 고정하며 stale route는 transfer mapping으로 relay |
| Spot create | MeshName을 필수로 받는 local generic create, result는 Spot RID | Stable `spotType`을 받고 MeshName·payload는 fluent 선택 항목, remote result는 exact `SpotRef` |
| Spot message | Local handle 또는 address 중심 | Global `spotRid`만 전달하고 Framework가 generation·route resolve |
| Instance Spot | `InstanceSpotAddress` 첫 message가 cold activation | Manager가 type·initial Mesh creation intent를 확정한 뒤 모든 message는 SpotRid만 사용 |
| Spot location snapshot | Spot RID와 local handle 중심 | `SpotRid + Generation + MeshName + NodeRid` immutable snapshot |
| Spot close | `(meshName, spotRid)` | ObjectGeneration을 보존한 `SpotRef` |
| User Spot type | .NET implementation type 중심 | Descriptor에 게시할 stable `spotType` 등록 |
| User Spot maintenance | Spot과 member Actor를 개별 처리하거나 blocker로 남김 | Spot과 current member Actor를 하나의 transfer aggregate로 commit |
| Transfer policy | Object 종류마다 등록 표면과 적용 범위가 다름 | Actor·User Spot·Instance Spot이 `Disabled`·`Recreate`·`Snapshot` 체계를 공유 |
| Route cache | Public max-age 없음 | Positive logical route의 기본 max-age 15초 후보와 event 기반 조기 invalidation |
| Stale-route forwarding | Actor 전용 option과 구현, V11 target 기본 5초 | Actor·Spot 공통 location option, instance가 없고 active mapping이 있으면 target으로 relay |
| MeshNode object role | Factory 등록 여부로 hosting capability를 간접 결정 | None·Client·Server를 명시하고 Server가 client capability를 포함 |
| Placement | Application handler가 node를 간접 선택 | Capacity를 먼저 적용하고 MeshNode Router weight로 target 선택 |
| Object capacity | Instance Spot type별 local 상한 중심 | Node 전체·type별 active·pending capacity와 Store reservation |
| Automatic RID | Slot count·prefix·group으로 deterministic allocation | Diagnostic prefix와 random suffix |
| Fixed RID | Automatic·manual topology에서 설정 가능 | Manual topology 전용 유지 후보 |
| Node replacement | 기존 RID를 유지할 public operation 없음 | 교체 node에 새 RID를 발급하고 application은 이전 RID의 유지에 의존하지 않음 |
| Store | Mesh-scoped key와 single-key authority CAS | Global object key, generic placement reservation과 random RID collision 검출 capability |

## 10. Spec 적용 전 결정 항목

1. Global `ActorId`와 `ActorRef`의 validation·JSON field contract
2. Global `SpotRid`와 `SpotRef`의 validation·JSON field contract
3. Mesh 후보가 없거나 여러 개인 경우의 exact error type
4. Fluent call의 duplicate option과 재실행 계약
5. ActorRef bind의 stale generation·moving Actor exact result
6. `EnableActorDispatch()`의 no-Mesh configuration과 migration
7. Join proposal 승인, state capture와 authority commit의 정확한 순서
8. Join relocation과 maintenance가 공유할 Actor transfer policy와 failure result
9. Exclusive create와 get-or-create의 existing·type mismatch 오류 계약
10. Creating CAS loser가 기다리는 deadline과 terminal result
11. Creation payload durability와 factory retry-safe contract
12. `SpotRef.Generation`을 직접 공개할지 opaque field로 표현할지
13. Stable User Spot type 이름과 cross-language mapping
14. Placement option의 최소 typed surface
15. Aggregate commit record, participant bound와 Store capability의 exact interface
16. `RouteCacheMaxAge`를 public option으로 둘지 forwarding window에서 derive할지
17. Forwarding mapping fence, relay queue bound와 Spot forwarding protocol
18. Transfer policy 생략 overload의 제거 여부와 migration
19. Fixed RID manual-only mode의 범위
20. Slot allocation public surface의 제거와 migration 영향
21. Random RID active-owner conflict에 필요한 Store capability
22. Node-wide Router weight builder·runtime option·descriptor의 exact member
23. Node 전체·type별 active·pending capacity option과 exhaustion result
24. Generic placement reservation request·fence·commit·abort provider interface
25. Instance Spot create intent·reactivation과 `InstanceSpotAddress` 제거 migration
26. `IZLinkActorDirectory`, Spot resolver·list·Entry Spot option의 remove·replace disposition
27. Actor destroy·Spot close의 exact-generation result와 error kind
28. Object role의 Location Store 필수 startup validation과 no-Store migration
29. Exact interface와 current .NET source의 Actor Manager baseline drift 처리

## 11. M5 이후 적용 절차

1. 이 문서의 `TO-BE`를 C# signature 정본으로 사용하지 않고 공통 semantic decision으로 분해한다.
2. Framework 공통 spec에서 identity, create, placement, failure와 maintenance 의미를 먼저 확정한다.
3. Public contract trace에서 현재 member의 retain·replace·remove·add disposition을 기록한다.
4. .NET exact interface에서 승인한 C# signature를 다시 작성한다.
5. C++, Java, Kotlin과 Node.js exact interface가 같은 기능 수준과 closed result를 각 언어 관례로 표현한다.
6. Protocol schema와 fixture가 logical identity, create authority와 placement reservation을 고정한다.
7. M6A·M6B·M6C가 같은 contract snapshot을 구현한다.
8. M7 E2E와 sample은 target node RID를 조립하지 않고 ChannelName·global ActorId·global SpotRid 경로를 검증한다.

다른 언어 구현이나 기존 .NET source는 새 public contract의 근거가 아니다. 정식 spec과 다섯 언어 exact
interface가 승인된 뒤에만 이 초안의 `TO-BE` code block을 구현 입력으로 대체한다.
