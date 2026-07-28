<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Channel Messaging](05-channel-messaging.ko.md) | [다음: Actor & Spot 호스팅](07-actor-spot.ko.md)
<!-- framework-adapter-nav:end -->

# 6. Spot

> 정확한 .NET 시그니처는 [Spot 공개 인터페이스](../../common/spec/server/languages/dotnet/interfaces/05-spots.ko.md)가 정의한다.
> Actor와 Spot membership은 [Actor & Spot 호스팅](07-actor-spot.ko.md)에서 설명한다.

Spot은 room, stage, zone처럼 문자열 ID로 찾는 실행 단위다. `SpotId`는 Location Store 전체에서
유일하며, 대소문자를 구분한다. Application은 Spot이 존재하는 `NodeRid`를 선택하거나 보관하지 않는다.
Framework가 현재 위치와 generation을 조회한다.

## 1. 세 가지 Spot

| 종류 | 만드는 방법 | 주요 용도 |
|---|---|---|
| User Spot | `IZLinkSpotManager`로 생성 | room, stage, zone |
| Instance Spot | 첫 send/request가 필요할 때 생성 | matchmaking worker처럼 요청 기반 실행 단위 |
| Entry Spot | Object Server가 시작할 때 Framework가 생성 | Actor 생성 직후의 기본 실행 위치 |

User Spot과 Instance Spot은 역할이 다르다. User Spot은 caller가 ID를 지정하거나 Framework가 새 ID를
발급한다. Instance Spot은 별도 create API를 사용하지 않는다. 첫 메시지에 instance type을 지정하면
Framework가 기존 instance를 선택하거나 필요한 위치에 생성한 뒤 같은 메시지를 처리한다.

## 2. Object Server 등록

Spot을 실행할 MeshNode는 Object Server role과 factory를 등록한다. 고정 `NodeRid`를 사용해 배치 대상을
선택하지 않는다. 같은 stable type을 등록한 `Serving` node가 배치 후보가 된다.

```csharp
var mesh = options.AddRouteMesh("play")
    .Listen("tcp://0.0.0.0:9001")
    .SetRoutingIdPrefix("play");

mesh.Objects().Server()
    .AddEntrySpot<PlayEntrySpot>() // Actor가 처음 배치될 Entry Spot을 등록한다.
    .AddSpotFactory<GameRoom>(
        "game-room",
        new ZLinkUserSpotFactoryOptions
        {
            ExecutionMode = ZLinkUserSpotExecutionMode.SpotWide
        },
        ZLinkRelocationPolicy<GameRoom>.Disabled)
    .AddInstanceSpotFactory<Matchmaker>(
        "matchmaker",
        null,
        ZLinkRelocationPolicy<Matchmaker>.Recreate);
```

User Spot 실행 모드는 다음과 같이 선택한다.

| 모드 | 상태와 실행 순서 |
|---|---|
| `SpotWide` | Spot과 소속 Actor가 하나의 직렬 실행 단위를 이룬다. relocation 때 함께 이동한다. |
| `PerActor` | Actor마다 직렬 실행 순서를 유지한다. Spot 자체는 stateless shell로 사용한다. |

`PerActor` Spot의 공유 상태와 Spot-level schedule은 Redis나 database 같은 외부 저장소에 둔다.
Factory relocation policy는 `Recreate`만 사용할 수 있다.

## 3. User Spot 만들기

새 ID가 필요하면 `Create`를 사용한다. Framework가 SpotId를 발급하고 eligible node를 선택한다.

```csharp
ZLinkSpotCreateResult created = await spots
    .Create("game-room") // stable type으로 factory와 배치 후보를 선택한다.
    .InMesh("play")
    .Request(new CreateGame("ranked")) // OnCreateAsync에 전달할 생성 요청이다.
    .Timeout(TimeSpan.FromSeconds(10))
    .Async(cancellationToken);

string spotId = created.Spot.SpotId; // 이후 메시징에는 전역 SpotId만 사용한다.
```

Caller가 정한 ID를 사용하려면 `GetOrCreate`를 사용한다. 동시에 여러 caller가 같은 ID를 요청해도
Framework는 하나의 생성 시도만 실행한다.

```csharp
ZLinkSpotCreateResult result = await spots
    .GetOrCreate("lobby-eu-1", "lobby")
    .InMesh("play")
    .Request(new CreateLobby("eu"))
    .Async(cancellationToken);

if (result.State == ZLinkSpotCreateState.Rejected)
{
    // 생성 callback이 거절했으므로 Ready Spot은 만들어지지 않았다.
    throw new InvalidOperationException("Lobby creation was rejected.");
}
```

`SpotRef`는 조회 시점의 exact incarnation이다. 일반 메시징에는 사용하지 않는다. 같은 incarnation을
닫을 때만 사용한다.

```csharp
SpotRef? current = await spots.FindAsync("lobby-eu-1", cancellationToken);
if (current is { } exact)
{
    await spots.CloseAsync(exact, cancellationToken); // 다른 generation을 실수로 닫지 않는다.
}
```

## 4. Spot 작성

`Configure()`에서 handler를 등록하고 lifecycle callback에서 초기화와 정리를 수행한다.

```csharp
public sealed class GameRoom(IZLinkSpotContext context) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddPacket<ChatHandler>(); // Spot send handler를 등록한다.
        Context.Handlers.AddSubscribe<ScoreHandler>(
            "game-events",
            "score.changed"); // Logical Multicast 구독을 등록한다.
    }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var create = request.Decode<CreateGame>();
        return ValueTask.FromResult(
            create.Mode is "ranked" or "casual"
                ? ZLinkSpotCreateResponse.Accept(new GameCreated(create.Mode))
                : ZLinkSpotCreateResponse.Reject(new InvalidMode(create.Mode)));
    }

    public ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        // 생성 승인 뒤 메시지를 받기 전에 필요한 준비를 끝낸다.
        return ValueTask.CompletedTask;
    }

    public ValueTask OnClosingAsync(
        ZLinkSpotClosingContext closing,
        CancellationToken cleanupCancellationToken)
    {
        // Deadline까지 application resource를 정리한다.
        return ValueTask.CompletedTask;
    }
}
```

`OnClosingAsync`의 reason은 explicit close, host shutdown, relocation out을 구분한다.
Framework는 `Deadline`이 끝날 때 cleanup token을 취소한다.

## 5. Spot으로 메시지 보내기

일반 User Spot 메시징은 SpotId만 필요하다. 위치와 generation은 Framework가 현재 authority에서 찾는다.

```csharp
await spotClient
    .SendToSpot("room-42", new Chat("hello"))
    .Async(cancellationToken);

RoomState state = await spotClient
    .RequestToSpot("room-42", new GetRoomState())
    .Timeout(TimeSpan.FromSeconds(3))
    .Async<RoomState>(cancellationToken);
```

Instance Spot은 같은 호출 표면에 intent를 추가한다. type이 하나만 등록된 mesh에서는
`InstanceSpot()`을 사용할 수 있다. 둘 이상이면 type을 명시한다.

```csharp
MatchResult match = await spotClient
    .RequestToSpot("bronze", new FindMatch(playerId))
    .InstanceSpot("matchmaker") // 기존 instance가 없으면 Framework가 생성한다.
    .InMesh("matchmaking")
    .Async<MatchResult>(cancellationToken);
```

`SendToSpot`은 source-local admission까지 기다리는 one-way operation이다. Target handler 완료를
기다리지 않는다. `RequestToSpot`은 reply 또는 typed error까지 기다린다.

## 6. Timer와 worker

Timer는 Spot context에 등록한다. Framework가 relocation 때 등록 정보와 pending tick을 옮기므로
application adapter가 timer를 따로 저장하거나 target에서 다시 등록하지 않는다.

```csharp
await Context.AddTimer<HeartbeatHandler>(
    "heartbeat",
    TimeSpan.FromSeconds(1),
    new ZLinkTimerOptions
    {
        OverrunPolicy = ZLinkTimerOverrunPolicy.SkipLateTicks
    },
    cancellationToken);
```

긴 CPU 작업과 I/O는 Spot queue를 계속 점유하지 않도록 worker call로 실행할 수 있다. `Yield`는
`SpotWide` User Spot 안의 Actor가 자기 queue 순서를 유지하면서 Spot 전체 실행권만 잠시 반환해야 할 때
사용한다. Entry Spot과 `PerActor`에서는 허용되지 않는다.

## 7. Application이 relocation 경계를 고르는 경우

기본 `AnyTurnBoundary`는 Framework가 완료된 turn 사이에서 relocation을 시작한다. FPS round처럼
application만 안전한 경계를 아는 Spot은 `ApplicationSignaled`를 등록한다.

```csharp
new ZLinkUserSpotFactoryOptions
{
    ExecutionMode = ZLinkUserSpotExecutionMode.SpotWide,
    RelocationReadiness = ZLinkSpotRelocationReadinessMode.ApplicationSignaled
}
```

안전한 turn의 마지막 Framework operation으로 `Defer()`를 호출한다. 이동이 없거나 commit 전에
중단되면 source에서 `Continued`, 이동했으면 target에서 `Relocated` callback을 받는다.

```csharp
Context.RelocationReady().Defer(); // 이 handler가 끝난 뒤 다음 turn 전에 경계를 연다.

public ValueTask OnRelocationReadyCompletedAsync(
    ZLinkSpotRelocationReadyCompletion completion,
    CancellationToken cancellationToken)
{
    StartNextRound(completion.Outcome);
    return ValueTask.CompletedTask;
}
```

`Defer()` 뒤 같은 turn에서 다른 Framework operation을 시작하면 오류다. `PerActor`, Entry Spot,
Instance Spot과 기본 `AnyTurnBoundary`에서도 호출할 수 없다.

## 8. 다음 문서

- Actor 생성과 Spot 이동: [Actor & Spot 호스팅](07-actor-spot.ko.md)
- Session binding: [Session Actor Dispatch](08-actor-session.ko.md)
- Location Store 설정: [Location](10-location.ko.md)
