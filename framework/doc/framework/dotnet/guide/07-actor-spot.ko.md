<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Spot](06-spot.ko.md) | [다음: Session Actor Dispatch](08-actor-session.ko.md)
<!-- framework-adapter-nav:end -->

# 7. Actor와 Spot

> 정확한 .NET 시그니처는 [Actor 공개 인터페이스](../../common/spec/server/languages/dotnet/interfaces/06-actors.ko.md)와
> [Spot 공개 인터페이스](../../common/spec/server/languages/dotnet/interfaces/05-spots.ko.md)가 정의한다.

Actor는 전역 문자열 `ActorId`로 찾는 상태 객체다. 생성 직후에는 Object Server의 Entry Spot에
존재한다. Application handler가 join을 예약하면 User Spot으로 이동한다.

Actor 위치와 client session binding은 서로 다른 상태다. Client가 연결되지 않아도 Actor와 Spot
membership은 유지된다. Session binding은 [다음 문서](08-actor-session.ko.md)에서 설명한다.

## 1. 등록

Object Server에 Entry Spot과 Actor factory를 함께 등록한다. `actorType`을 등록한 `Serving` node가
생성 후보가 된다.

아래는 [Bingo 샘플](../../common/sample/bingo/README.ko.md)의 Play 서버 등록이다.

```csharp
mesh.Objects().Server()
    .AddEntrySpot<BingoEntrySpot>()
    .AddActorFactory<PlayerActor, PlayerActorFactory>(
        SampleNames.PlayerActorType,
        factory => factory
            .PreserveStateWith<PlayerActorRelocationAdapter>());
```

Relocation policy는 factory 등록에서 정확히 하나를 고정하며 실행 중에는 바꾸지 않는다.
Actor가 다른 node의 Spot으로 join할 때와 host `Relocate`로 이전할 때 모두 이 policy를
따른다.

| policy | 다른 node에서 만드는 방법 |
| --- | --- |
| `DisableRelocation()` | Cross-node 이동을 시작하기 전에 거절한다. 이 대상이 남아 있으면 host relocation을 완료할 수 없다. |
| `RecreateOnRelocation()` | 같은 logical identity로 새 instance를 만든다. 대기 중이던 message와 timer는 유지하지만 application state는 복원하지 않는다. |
| `PreserveStateWith<TAdapter>()` | adapter가 저장한 `byte[]`를 새 instance에 복원한다. Framework queue와 timer도 함께 유지한다. |

## 2. Actor 만들기

`Create`는 같은 ActorId가 이미 있으면 실패한다. `GetOrCreate`는 같은 type의 Ready Actor가 있으면
`Existing`을 반환한다. Caller는 target node를 지정하지 않는다.

```csharp
ZLinkActorCreateResult result = await actors
    .GetOrCreate(playerId, "player")
    .InMesh("play")
    .Request(new CreatePlayer(displayName))
    .Timeout(TimeSpan.FromSeconds(10))
    .Async(cancellationToken);

ActorRef actor = result switch
{
    ZLinkActorCreateResult.Existing value => value.Actor,
    ZLinkActorCreateResult.Created value => value.Actor,
    ZLinkActorCreateResult.Rejected =>
        throw new InvalidOperationException("Player creation was rejected.")
};
```

`ActorRef`는 exact incarnation과 조회 당시 owner route를 담는다. Session binding이나 exact destroy에
사용한다. 일반 Actor 메시징은 ActorId만 사용한다.

```csharp
ActorRef? current = await actors.FindAsync(playerId, cancellationToken);
SpotRef? currentSpot = await actors.FindSpotAsync(playerId, cancellationToken);

if (current is { } exact)
{
    await actors.DestroyAsync(exact, cancellationToken); // generation이 다른 Actor는 종료하지 않는다.
}
```

Actor는 Entry Spot에서만 종료할 수 있다. User Spot에 있으면 먼저 Entry Spot join을 완료한다.

## 3. Entry Spot

Entry Spot은 Actor 생성 요청을 승인하거나 거절하고, Actor가 들어오고 나가는 lifecycle을 처리한다.
Relocation 때문에 Actor가 다른 node의 Entry Spot에 복원되는 경우에는 application membership callback을
호출하지 않는다.

```csharp
public sealed class PlayEntrySpot(IZLinkEntrySpotContext context)
    : IZLinkEntrySpot<PlayerActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers
            .AddActorPacket<JoinGameHandler, PlayerActor>(); // Entry Spot의 Actor handler를 등록한다.
    }

    public ValueTask<ZLinkActorCreateResponse> OnCreateActorAsync(
        PlayerActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        var request = createRequest.Decode<CreatePlayer>();
        actor.SetDisplayName(request.DisplayName);
        return ValueTask.FromResult(ZLinkActorCreateResponse.Accept());
    }

    public ValueTask OnJoinedActorAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
        => ValueTask.CompletedTask;

    public ValueTask OnLeaveActorAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
        => ValueTask.CompletedTask;
}
```

Entry Spot은 Actor별 application state를 따로 보관하지 않는 편이 안전하다. Actor state는 Actor가
소유하고, Entry Spot은 handler와 membership lifecycle만 제공한다.

Actor를 종료하려면 먼저 Entry Spot으로 돌아온 뒤 현재 Actor instance를
`IZLinkEntrySpotContext.DestroyActorAsync(actor)`에 넘긴다.

```csharp
await Context.DestroyActorAsync(
    actor,
    cancellationToken); // Entry Spot이 현재 Actor의 종료를 요청한다.
```

이 호출은 membership lifecycle callback을 다시 호출하지 않고 native actor ref, Framework registry와
bound session mapping을 정리한다. User Spot에 있는 Actor는 직접 종료할 수 없다. 먼저 leave를 완료해
Entry Spot으로 돌아와야 한다.

## 4. User Spot membership

User Spot은 join 요청을 먼저 승인하거나 거절한다. 승인 뒤 membership이 commit되면
`OnJoinedActorAsync`가 호출된다.

```csharp
public sealed class GameRoom(IZLinkSpotContext context)
    : IZLinkSpot<PlayerActor>
{
    public IZLinkSpotContext Context { get; } = context;

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var join = request.Decode<JoinGame>();
        return ValueTask.FromResult(
            HasSeat(join.Seat)
                ? ZLinkSpotActorJoinResult.Accept(new Joined(join.Seat))
                : ZLinkSpotActorJoinResult.Reject(new RoomFull()));
    }

    public ValueTask OnJoinedActorAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
        => ValueTask.CompletedTask;

    public ValueTask OnLeaveActorAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
        => ValueTask.CompletedTask;
}
```

## 5. Join은 handler가 끝난 뒤 실행한다

Actor join call은 `Async`를 제공하지 않는다. 현재 handler에서 intent를 등록하고 `Defer()`를 호출한다.
Framework는 handler가 정상적으로 끝난 뒤에 위치 조회와 Store 작업을 시작한다. 따라서 현재 Actor job과
join 완료 callback의 순서가 섞이지 않는다.

```csharp
public ValueTask HandleAsync(
    PlayEntrySpot entrySpot,
    PlayerActor actor,
    IZLinkMessageContext messageContext,
    JoinGame command,
    CancellationToken cancellationToken)
{
    actor.Context
        .JoinSpot(command.SpotId, new JoinGameRequest(command.Seat))
        .Timeout(TimeSpan.FromSeconds(5))
        .Defer(); // 현재 handler가 성공한 뒤 join을 시작한다.

    return ValueTask.CompletedTask;
}
```

결과는 Actor callback으로 받는다.

```csharp
public ValueTask OnJoinCompletedAsync(
    ZLinkActorJoinCompletion completion,
    CancellationToken cancellationToken)
{
    switch (completion)
    {
        case ZLinkActorJoinCompletion.Accepted accepted:
            RememberCurrentLocation(accepted.Actor);
            break;
        case ZLinkActorJoinCompletion.Rejected:
            ClearPendingJoin();
            break;
        case ZLinkActorJoinCompletion.Failed failed when failed.IsRetriable:
            ScheduleApplicationRetry();
            break;
    }

    return ValueTask.CompletedTask;
}
```

User Spot에서 Entry Spot으로 돌아갈 때도 같은 방식이다.

```csharp
actor.Context
    .JoinEntrySpot(new LeaveGame(reason))
    .Timeout(TimeSpan.FromSeconds(5))
    .Defer();
```

`OperationId`는 completion 중복을 식별한다. Application은 같은 ID의 callback이 다시 실행되어도
안전하게 처리해야 한다.

## 6. Actor 메시징

Actor가 어느 Spot과 node에 있는지 몰라도 ActorId로 메시지를 보낼 수 있다.

```csharp
await actorClient
    .SendToActor(playerId, new AwardExperience(10))
    .Async(cancellationToken);

PlayerProfile profile = await actorClient
    .RequestToActor(playerId, new GetPlayerProfile())
    .Timeout(TimeSpan.FromSeconds(3))
    .Async<PlayerProfile>(cancellationToken);
```

Relocation 중에는 Framework가 current authority로 다시 전달하고, 이전 owner에 이미 도착한 메시지는
Message Follow 규칙에 따라 새 owner로 relay한다. Application은 `NodeRid`를 추적하지 않는다.

**이동 중에 보낸 request도 원래 caller에서 완료된다.** Target이 처리한 reply는 원래 caller로
correlate되고, timeout은 caller의 기존 경로를 그대로 따르며, 늦게 도착한 reply는 drop된다
([spot-actor 스펙 §10.5](../../common/spec/15-spot-actor.ko.md)). 이동 중 reply를 기다리는
request 수는 `zlink.mesh_node.requests.inflight`의 `surface=actor` 값으로 관측한다
([12-operations](12-operations.ko.md#1-런타임-메트릭)).

## 7. Relocation state adapter

Adapter는 Actor instance의 application state만 byte 배열로 저장하고 복원한다. Location authority,
queue, timer, accepted journal과 session route는 Framework가 처리한다.

```csharp
public sealed class PlayerActorRelocationAdapter
    : IZLinkActorRelocationAdapter<PlayerActor>
{
    public ValueTask<byte[]> CaptureAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
        => ValueTask.FromResult(actor.ExportState());

    public ValueTask RestoreAsync(
        PlayerActor actor,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken)
    {
        actor.ImportState(payload.Span);
        return ValueTask.CompletedTask;
    }
}
```

Capture와 restore는 같은 relocation에서 다시 호출될 수 있다. Adapter는 retry-safe해야 하며,
payload memory를 callback 밖에서 보관하려면 복사해야 한다.

## 8. 다음 문서

- Session과 Actor binding: [Session Actor Dispatch](08-actor-session.ko.md)
- STREAM server와 client: [STREAM](09-stream.ko.md)
- Actor·Spot 주소 해석 규칙: [Object routing](../../common/spec/18-object-routing.ko.md)
