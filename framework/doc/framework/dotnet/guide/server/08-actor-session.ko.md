<!-- framework-adapter-nav:start -->
[문서 목록](../../../../README.ko.md) | [이전: Actor & Spot 호스팅](07-actor-spot.ko.md) | [다음: STREAM](09-stream.ko.md)
<!-- framework-adapter-nav:end -->

# 8. Session과 Actor binding

> 정확한 .NET 시그니처는 [STREAM session 공개 인터페이스](../../../common/spec/server/languages/dotnet/interfaces/07-stream-session.ko.md)와
> [bound session 공개 인터페이스](../../../common/spec/server/languages/dotnet/interfaces/07-bound-stream-session.ko.md)가 정의한다.

Session binding은 client STREAM session과 exact Actor incarnation을 연결한다. Binding 뒤 session은
client packet을 Actor로 relay할 수 있고, Actor는 같은 session으로 push할 수 있다.

Binding은 Actor의 Spot membership과 독립이다. Actor가 다른 Spot이나 node로 relocation되어도
`ActorId`와 `ObjectGeneration`은 유지되며 Framework가 binding route를 갱신한다.

## 1. 인증 뒤 Actor bind

Session handler에서 Actor를 생성하거나 조회한 다음 `ActorRef`를 bind한다. Local Actor instance나
target `NodeRid`를 직접 전달하지 않는다.

```csharp
public sealed class AuthenticateHandler(IZLinkActorManager actors)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, Authenticate>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Authenticate request,
        CancellationToken cancellationToken)
    {
        ActorRef actor = (await actors
            .GetOrCreate(request.PlayerId, "player")
            .Request(new CreatePlayer(request.DisplayName))
            .Async(cancellationToken)) switch
        {
            ZLinkActorCreateResult.Existing value => value.Actor,
            ZLinkActorCreateResult.Created value => value.Actor,
            _ => throw new InvalidOperationException("Player creation was rejected.")
        };

        await context.Actors.BindOrGetAsync(
            actor,
            cancellationToken); // 같은 exact incarnation이 이미 bind됐으면 기존 route를 반환한다.

        await context.Client
            .Reply(new Authenticated(actor.ActorId))
            .Async(cancellationToken); // 현재 request의 one-shot reply를 제출한다.
    }
}
```

`BindAsync`는 중복 bind를 오류로 처리한다. 인증 재전송처럼 이미 bind됐을 수 있는 흐름은
`BindOrGetAsync`를 사용한다.

## 2. Session packet을 Actor로 relay

Session의 `Configure()`에서 인증 같은 session 전용 handler를 등록한다. 처리하지 않은 packet은
bound Actor로 넘긴다.

```csharp
public sealed class PlaySession(IZLinkSessionContext context) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers
            .AddHandler<AuthenticateHandler>(); // Actor binding 전에 처리할 packet을 등록한다.
    }

    public async ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        if (await Context.Handlers.TryHandleAsync(
                dispatch,
                payload,
                cancellationToken))
        {
            return;
        }

        IZLinkSessionActor actor = Context.Actors.Bound.Count == 1
            ? Context.Actors.Bound.Single()
            : throw new InvalidOperationException(
                "Exactly one actor must be bound before relay.");

        await actor.RelayAsync(
            payload,
            cancellationToken); // decode하지 않고 Framework-owned payload를 Actor handler로 넘긴다.
    }

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        => ValueTask.CompletedTask;

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
        => ValueTask.CompletedTask;

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        => ValueTask.CompletedTask;
}
```

한 session에 여러 Actor를 bind할 수 있다. 이 경우 application protocol이 선택한 ActorId를
`Context.Actors.Find(actorId)`에 전달한다. Framework는 임의의 Actor를 선택하지 않는다.

## 3. Disconnect 통지

Physical STREAM disconnect는 Framework가 current binding 전체에 자동으로 통지한다. 연결이 유지된
상태에서 logical disconnect를 알릴 때만 명시적으로 호출한다.

```csharp
IZLinkSessionActor? actor = Context.Actors.Find(playerId);
if (actor is not null)
{
    await actor.NotifyDisconnectedAsync(cancellationToken);
    // Actor가 속한 Spot의 OnDisconnectActorAsync callback 완료까지 기다린다.
}
```

Disconnect는 Actor를 삭제하거나 Entry Spot으로 이동시키지 않는다. 재접속한 session은 같은
ActorRef를 다시 조회해 bind할 수 있다.

## 4. Actor에서 client로 push

Actor handler는 `Context.BoundSession`으로 현재 bound client에 메시지를 보낸다.

```csharp
public sealed class StateChangedHandler
    : IZLinkSpotActorSendHandler<GameRoom, PlayerActor, StateChanged>
{
    public async ValueTask HandleAsync(
        GameRoom spot,
        PlayerActor actor,
        IZLinkMessageContext messageContext,
        StateChanged message,
        CancellationToken cancellationToken)
    {
        await actor.Context.BoundSession
            .Send(new GameStateNotify(message.State))
            .Metadata("revision", message.Revision.ToString())
            .Async(cancellationToken); // 현재 bound session의 local admission까지 기다린다.
    }
}
```

Bound session은 push와 disconnect만 제공한다. Client request에 대한 Actor reply는 request handler의
반환값으로 처리한다.

## 5. 오류를 처리하는 기준

| 상황 | 결과 |
|---|---|
| Actor가 없거나 Ready가 아님 | bind가 typed framework error로 끝난다. |
| `ObjectGeneration`이 다름 | stale ActorRef를 다른 incarnation에 bind하지 않는다. |
| Actor relocation seal 진행 중 | `ActorMoving`으로 끝나며 hidden retry하지 않는다. |
| Binding 뒤 Actor relocation | Framework가 route를 갱신하며 session을 다시 bind하지 않는다. |
| Session disconnect | Actor와 Spot membership은 유지한다. |

`ActorRef.MeshName`과 `NodeRid`는 최초 control route의 snapshot이다. Application은 stale route를
새로 조합하지 말고 `IZLinkActorManager.FindAsync`로 current ref를 다시 조회한다.

## 6. 관련 문서

- 이 챕터 계약의 실행 검증 예문: [13-interface-catalog](13-interface-catalog.ko.md) §5 — 검증 클래스 `StreamContracts`
- STREAM node와 session lifecycle: [STREAM](09-stream.ko.md)
- Actor 생성과 Spot join: [Actor와 Spot](07-actor-spot.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../README.ko.md) | [이전: Actor와 Spot](07-actor-spot.ko.md) | [다음: STREAM](09-stream.ko.md)
<!-- framework-adapter-nav:bottom:end -->
