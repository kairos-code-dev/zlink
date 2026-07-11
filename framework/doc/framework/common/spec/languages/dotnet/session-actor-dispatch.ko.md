<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework ASP.NET Core Actor](aspnet-core-actor.ko.md) | [다음: ZLink Framework ASP.NET Core Monitoring](aspnet-core-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[.NET 묶음](../../../../dotnet/README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [Actor](aspnet-core-actor.ko.md) | [STREAM](aspnet-core-stream.ko.md) | [policy/Session Actor Dispatch 사용성](../../session-actor-dispatch.ko.md)

# ZLink Framework .NET Session Actor Dispatch

## 1. 목적

이 문서가 다루는 범위는 다음과 같다.

- session 서버와 play 서버를 분리하는 구조를 `.NET` 사용자가 실제 시그니처와
  DI 등록 코드 모양으로 살펴 볼 수 있도록 정리한다.
- cross-binding 의미 자체는
  [policy/session-gateway-usability.ko.md](../../session-actor-dispatch.ko.md)
  에서 다룬다.
- 따라서 여기서는 `.NET` 표면만 다룬다.

## 2. 핵심 표면 요약

이 절은 session actor dispatch 가 `.NET` 에서 어떤 형태로 노출되는지를 한눈에
정리한다. 핵심 표면은 다음 네 축이다.

| 축 | `.NET` 표면 |
|----|-------------|
| session → actor relay | `IZLinkSessionContext.Actors.BindAsync(...)`, `IZLinkSessionActor.RelayAsync(...)` |
| spot actor handler | `IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>`, `IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>`, `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>`, `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` |
| actor → own client push | Spot actor handler 가 받은 actor 의 `Context.BoundSession.Send(msg).Submit(...)` |
| 다른 actor → client push | 먼저 대상 actor 에 메시지를 보내고, 대상 Spot actor handler 가 actor `Context.BoundSession` 으로 push |
| route 해석 | session relay 는 logical actor id/type handle 을 사용하고, core SessionRelay 가 현재 actor 위치를 해석한다. actor → client push 방향은 framework/core가 가진 actor-session binding[^actor-session-binding]을 사용한다 |

인터페이스 전체 정의는 [handler-interfaces.ko.md](handler-interfaces.ko.md)
§4.4, §5.5, §5.6 에 모여 있다. 이 문서에서는 사용 모양과 등록 코드
예시만 모아 둔다.

## 2.1 내부 routed wire 계약

이 절에서는 session 서버와 play 서버 사이의 wire 단계 규약을 정리한다.

session actor dispatch 의 public API 는 typed object 중심이다. 다만 서버
사이를 잇는 내부 route transport 단계에서는 공통
[message-model.ko.md](../../message-model.ko.md) 가 정한 multipart 계약을
그대로 따른다.

Session 서버에서 Play 서버 actor 로 보내는 actor dispatch request / send 는
아래와 같은 part 구성을 사용한다.

| part | 내용 |
|------|------|
| `parts[0]` | routed framework header. packet name은 internal actor dispatch packet 이름을 사용한다 |
| `parts[1]` | actor dispatch metadata. actor route와 함께, local actor를 새로 만들어야 하는 경우를 위한 `ActorId`, `ActorType`만 둔다 |
| `parts[2]` | encoded stream header bytes. stream packet kind, codec, request sequence, packet name, metadata snapshot은 모두 이 part에 둔다 |
| `parts[3]` | application payload bytes. framework codec이나 stream packet codec이 만든 payload를 그대로 둔다 |

반대 방향도 같은 원칙을 따른다. Play 서버 actor 에서 Session 서버의 client
stream 으로 보내는 bound session send / request 는 다음 part 구성을 사용한다.

| part | 내용 |
|------|------|
| `parts[0]` | routed framework header. packet name은 internal bound session packet 이름을 사용한다 |
| `parts[1]` | bound session metadata. `ActorId`, `BindingToken`, client packet name, reply 필요 여부, metadata snapshot을 함께 담는다 |
| `parts[2]` | application payload bytes |

reply 도 같은 원칙을 따른다. routed reply header 는 `parts[0]` 에 두고, reply
payload 는 별도 part 로 둔다. payload 가 없으면 빈 payload part 를 그대로 남긴다.

다음과 같은 형태는 이 문서의 내부 routed wire 계약이 아니다.

- `ZLinkActorDispatchPacket` 같은 단일 DTO 안에 `StreamHeader` 와 `byte[] Payload`
  를 같이 넣고, 그 DTO 전체를 다시 JSON 으로 직렬화하는 방식
- `ZLinkBoundSessionPacket` 같은 단일 DTO 안에 proxy metadata 와 payload bytes 를
  함께 묶는 방식
- `parts[0]` 한 part 만 보내고 그 안에서 header 와 payload 를 모두 decode 하는
  방식

이 제한은 단순히 성능 때문만이 아니다. 다음 두 가지 이유 때문이다.

- route 와 dispatch 는 header 와 metadata 만 읽고도 target 과 handler 를
  결정할 수 있어야 한다.
- application payload 는 handler 가 정해진 뒤에 등록된 codec 이 decode 해야 한다.

그래야 큰 payload, binary payload, 압축 payload, attachment 가 들어와도 framework 내부
route 경로가 payload 재인코딩 비용을 떠안지 않는다.

STREAM 자체는 예외다. client 와 Session 서버 사이의 STREAM transport 는
stream packet 하나 안에 stream header / payload frame 을 그대로 담는다. 위에서
말한 multipart 계약은 Session 서버와 Play 서버처럼 framework 서버끼리
주고받는 routed transport 구간에만 적용한다.

## 2.2 actor mailbox와 실행 순서

이 절은 actor 가 받은 packet 이 어디서 직렬화되는지, 그리고 왜 별도의 session
mailbox 를 두지 않는지 설명한다.

먼저 기본 전제는 이렇다.

- stream socket 은 같은 session 으로 들어온 frame 의 도착 순서를 보존한다.
- framework 는 같은 session 의 callback 을 직렬로 실행한다.
- 따라서 session actor dispatch 를 위해 별도의 session mailbox 를 application
  표면에 따로 두지 않는다.

Session 서버가 받은 stream packet 을 actor 로 relay 할 때, framework 는 현재
actor 위치에 맞는 실행 경계로 그 packet 을 넘긴다. actor 가 Entry Spot 에 있거나
아직 user Spot 에 들어가지 않은 상태라면 대상 actor 의 순서 규칙을 거친다.
actor 별 순서 규칙은 두 가지 역할을 한다.

- 같은 actor 로 들어온 packet 사이의 순서를 지킨다.
- 서로 다른 actor 의 packet 이 서로의 처리를 막지 않도록 보장한다.

이 규칙은 session relay 뿐 아니라 Entry Spot[^entry-spot] actor packet 에도
그대로 적용한다. Entry Spot 은 모든 actor 가 처음 거치는 공용 입구이기
때문이다. Entry Spot 전체에 실행 줄을 하나만 두고 actor packet 을 처리하면,
서로 관련 없는 actor 들이 한 줄로 묶여 막혀 버린다.

반면 user Spot[^user-spot] 안에서 처리되는 actor packet 은 user Spot 의 실행
queue 에서 handler 를 실행한다. user Spot handler 는 `spot` 인스턴스와 `actor`
인스턴스를 함께 받기 때문이다. 즉 room 이나 game 상태를 바꿀 수 있는 자리다.

같은 user Spot 안의 여러 actor 가 같은 상태를 만질 수 있으므로 Spot 단위로
순서를 지켜야 한다. managed runtime 경로에서는 actor 별 순서 규칙을 거친 뒤
user Spot queue 로 들어갈 수 있다. 반면 native bound actor 경로에서는 user
Spot queue 가 반드시 필요한 직렬화 경계다.

이를 정리하면 `.NET` runtime 의 실행 규칙은 아래와 같다.

| 입력 경로 | 실행 위치 |
| --- | --- |
| stream session → Entry/local actor | actor별로 순서를 보존한 뒤 현재 actor 위치로 dispatch |
| Entry Spot actor packet | actor별 mailbox |
| stream session → user Spot actor | user Spot 실행 queue |
| user Spot actor packet | user Spot 실행 queue |
| user Spot packet / timer / subscription | user Spot 실행 queue |
| Entry Spot initialize / closing / lifecycle callback | Entry Spot 실행 문맥 |


## 3. Actor handler 표면

이 절은 actor 쪽에서 application 이 구현하게 되는 handler 시그니처와, handler
가 받는 metadata 의 모양을 정리한다.

### 3.1 typed handler 시그니처

```csharp
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
```

handler 는 transport raw header 를 직접 받지 않는다. Session route 와 binding
token 같은 내부 값은 framework runtime 이 들고 있고 public handler 에 노출하지
않는다. stream packet 이름과 전달 허용된 application metadata 는
`ZLinkSpotActorSendContext` / `ZLinkSpotActorRequestContext` 로 전달된다.

typed actor context 는 source session 의 `RoutingId` 를 노출하지 않는다.
handler 가 즉시 자기 client 로 push 를 보내야 하는 경우에도 마찬가지다. 이때도
handler 가 받은 actor 의 `Context.BoundSession.Send(message)` 처럼 현재 actor 에
묶인 표면을 사용해야 한다.

request handler 의 응답 payload 는 handler 의 반환값이다. 다만 응답 stream
header 에 application metadata 를 추가하거나 payload compression 을 켜야 하는
경우에는 `ZLinkSpotActorRequestContext.Reply` 를 사용한다.

```csharp
public async ValueTask<JoinMatchRes> HandleAsync(
    GameSpot spot,
    PlayerActor actor,
    ZLinkSpotActorRequestContext context,
    JoinMatchReq request,
    CancellationToken cancellationToken)
{
    context.Reply
        .Metadata("trace-id", "reply-trace")
        .Compress();

    return new JoinMatchRes(request.MatchId);
}
```

`context.Reply` 는 응답을 직접 보내는 API 가 아니다. handler 가 반환한 값을
framework 가 response frame 으로 만들 때 사용할 header/body encoding 옵션만
기록한다.

### 3.2 metadata snapshot

이 절은 application metadata 를 어디까지 actor 쪽에 흘려 보낼지 정하는 표면을
다룬다.

```csharp
public sealed class ZLinkMessageMetadata
{
    public static ZLinkMessageMetadata Empty { get; }

    public IReadOnlyDictionary<string, string> Values { get; }

    public string? Find(string key);
}

public interface IZLinkMessageMetadataPolicy
{
    bool CanForward(string key);
}
```

기본 `IZLinkMessageMetadataPolicy` 는 client stream metadata 를 actor handler
쪽으로 전달하지 않는다. trace id 같은 값을 actor handler 까지 함께 흘려
보내려면, framework 등록 단계에서 명시적으로 허용해야 한다. 허용된 값은
`ZLinkMessageMetadata.Values` 에 저장되며, 사용자 코드는 `Find(key)` 가
`null` 을 반환하면 해당 key 가 없다고 보면 된다.

```csharp
{
    var metadata = options.ConfigureMetadata();
    metadata.AddForwardedMetadataKey("trace-id");
    metadata.AddForwardedMetadataKey("tenant-id");

}
```

## 4. BoundSession 호출 표면

이 절은 actor 가 client session 쪽으로 push 를 보내거나 client stream 을 닫을 때 사용하는
public 표면을 정리한다.

```csharp
public interface IZLinkBoundSession
{
    IZLinkBoundSessionSendCall Send<TMessage>(
        TMessage message);

    ValueTask DisconnectAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkBoundSessionSendCall
{
    IZLinkBoundSessionSendCall PacketName(string packetName);

    IZLinkBoundSessionSendCall Metadata(
        string key,
        string value);

    void Submit(
        CancellationToken cancellationToken = default);
}
```

`Submit(...)` 은 bound session send의 one-way terminator다. client push는 호출자가
송신 수락 완료를 기다리는 흐름으로 만들지 않는다. 송신 수락과 backpressure 처리는
framework 내부 책임이다.

호출 모양은 아래와 같다.

```csharp
await Context.BoundSession
    .Send(new GameStateChangedMsg(gameId, board))
    .Async(cancellationToken);
```

기존에 사용하던 `SessionGateway` 라는 이름은 새 public API 에서 제거한다.
이름이 두 갈래로 정리된다.

- session → actor 방향: `BindAsync(...)`, `IZLinkSessionActor.RelayAsync(...)` 를 사용한다.
  disconnect 알림과 binding cleanup 은 session lifecycle 경로에서 처리한다.
- actor → 자기 client 방향: `IZLinkBoundSession` 를 사용한다.
- 다른 actor 의 client session 에 보내야 하는 application service 는 먼저 대상
  actor 로 메시지를 보내고, 대상 actor handler 가 자기 `BoundSession` 을 사용한다.

`IZLinkBoundSession.Send(...).Submit(...)` 은 one-way push 다. 이 호출은
client application handler 가 메시지를 처리 완료했다는 ack 를 만들지 않는다.
송신 수락과 backpressure 처리는 framework 내부 책임으로 처리한다.

`IZLinkBoundSession.DisconnectAsync(...)` 는 actor 가 현재 actor id 에 묶인
client stream 을 끊어야 한다고 판단했을 때 호출한다. 이 close 는
application 이 의도한 동작이므로 session 의 `OnDisconnectedAsync(...)` callback
을 다시 올리지 않는다. framework 는 stream close 와 actor-session binding
정리만 수행한다.

stale binding token, 이미 닫힌 stream, 늦게 도착한 push 는 해당 push 하나만
실패해야 한다. 즉 route receive loop 나 host shutdown 자체를 실패시켜서는 안
된다.

client 처리 완료 ack 가 계약상 필요하면, actor message 나 session message 로
별도의 application-level reply 를 설계해야 한다. `BoundSession` 자체는 request / reply
표면을 제공하지 않는다.

재접속은 actor id 기준으로 idempotent 해야 한다. 같은 actor id 가 새 stream
session 에서 `BindAsync(...)` 로 다시 들어오면, framework 는 다음과
같이 동작한다.

- 기존 actor instance 와 spot membership 은 그대로 유지한다.
- session binding 만 새 stream 으로 옮긴다.

이 규칙이 있어야 client reconnect 가 "새 게임에 참여"가 아니라 "기존 actor 의
새 연결"로 동작한다.

## 5. Session에서 actor로 relay

session actor dispatch 에서 session 은 actor runtime 을 직접 호출하는 범용
public client 를 사용하지 않는다. client stream 에서 받은 packet 은
`IZLinkSession.OnDispatchAsync(...)` 로 올라오고, session 구현은 actor handle 을
만든 뒤 `IZLinkSessionActor.RelayAsync(...)` 로 전달한다.

```csharp
public interface IZLinkSessionActors
{
    IReadOnlyCollection<IZLinkSessionActor> Bound { get; }

    ValueTask<IZLinkSessionActor> BindAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkSessionActor> BindAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);

    IZLinkSessionActor? Find(string actorId);
}
```

이 표면만 남기면 session 코드의 의도가 분명해진다. session 은 "받은 client
packet 을 어떤 actor 에 relay 할지"만 결정한다. 같은 process 의 actor 는 `IZLinkActor`
인스턴스(예: `IZLinkActorManager.GetOrCreateAsync(...)` 결과)로 bind 하고, 다른 process 의
actor 는 `JoinSpot(...)` / `JoinEntrySpot(..., request)` 결과의 `ActorRef` 를 넘기는
overload 로 bind 한다.
한 session 이 여러 actor 를 bind 할 수 있으므로 이미 bind 한 actor handle 이 필요하면
`Bound` 로 현재 binding snapshot 을 보거나 `Find(actorId)` 로
actor id 기준 조회를 한다. session 은 actor handle 목록을 별도 application 상태로
복제하지 않는다.

packet relay 와 disconnect notification 은 bind 결과로 받은 `IZLinkSessionActor` 가
수행한다. 즉 session code 는 대상 handle 을 고른 뒤 `actor.RelayAsync(...)` 또는
`actor.NotifyDisconnectedAsync(...)` 를 호출한다.

session disconnect 는 bound actor 전체에 자동 전파되지 않는다. 연결이 끊겼을 때
어떤 actor 에게 알려야 하는지는 application 이 판단한다. 알림이 필요한 경우
`OnDisconnectedAsync(...)` 안에서 `Find(...)` 또는 `Bound` 로
대상을 고른 뒤 `actor.NotifyDisconnectedAsync(...)` 를 호출한다. 이 호출은
actor 의 현재 Spot 실행 문맥에서 disconnected handler 를 실행할 뿐이며, actor 를
room 에서 leave 시키지 않는다.

## 6. Spot ref resolver 등록

이 절은 actor 가 Spot 으로 join 하거나 Spot client 를 사용할 때 필요한 resolver 표면을 정리한다.

- session 에 이미 attach 된 actor 로 relay 할 때는 actor ref resolver 를
  사용하지 않는다. session 은 framework 가 만든 actor handle 을 저장한다.
- actor 가 현재 연결된 client session 으로 push 를 보낼 때는,
  framework / core 가 가진 actor-session binding 상태를 사용한다.
- actor 가 `JoinSpot(spotRid, ...)` 로 user Spot 에 들어가는 경로가 node
  경계를 넘을 수 있다면, spot ref resolver 도 함께 등록한다.

```csharp
namespace Zlink.Framework.Contracts.Spots;

public interface IZLinkSpotRefResolver
{
    ValueTask<SpotRef> ResolveSpotRefAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken);
}

public enum ZLinkSpotKind
{
    Invalid = 0,
    Entry = 1,
    User = 2,
}

public readonly record struct SpotRef(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    RoutingId SpotRid,
    ZLinkSpotKind SpotKind);
```

DI 등록 (Session 서버):

```csharp
builder.Services.AddZLinkFramework(options =>
{
    {
        var mesh =     options.AddSpotMesh("game.rooms");

    }
    // STREAM session 등록 + routed channel 등록 (별도 문서 참고)
});
```

DI 등록 (Play 서버):

```csharp
builder.Services.AddZLinkFramework(options =>
{
    spot.AddActorFactory<PlayerActorFactory>("player");
    // routed channel 등록 + spot mesh 등록 (별도 문서 참고)
});
```

### 6.1 Actor-session binding 상태

이 절은 actor 와 stream session 의 연결 정보를 누가 들고 있는지를 정리한다.

framework 는 session route resolver 를 public 기본 표면으로 제공하지 않는다.
session binding 은 다음 흐름에서 framework / core 가 갱신해 두는 내부
상태이기 때문이다.

- actor handle 생성
- stream attach
- stream disconnect

actor-session route 는 session bind 시 actor runtime state 에 저장된다. value 에는
session rid 와 binding token 이 들어간다. unbind 는 binding token 을 확인한 뒤
수행하므로 이전 stream 의 늦은 unbind 가 새 binding 을 지우지 못한다. 별도의 public
session route resolver 나 저장소 계약은 두지 않는다.

## 7. 등록 표면 (host 측)

이 절은 host 가 framework 를 띄울 때 작성하는 등록 코드 모양을 보여 준다.

```csharp

options.AddRouteMesh("backend")
    .EnableServer(playEndpoint);

spot.AddActorFactory<PlayActorFactory>("player");
spot.AddActorTransferAdapter<PlayActor, PlayActorTransferAdapter>("player");

public sealed class PlayActor(
    string actorId,
    IZLinkActorContext context)
    : IZLinkActor
{
    public string ActorId { get; } = actorId;
    public IZLinkActorContext Context { get; } = context;
}
```

spot handler는 spot 객체 안에서 등록한다.

```csharp
{
    var mesh = options.AddSpotMesh("game.rooms");
    {
        var spot = mesh;
        spot.EnableRouter(spotEndpoint);
        spot.AddEntrySpot<TicTacToeEntrySpot>();
        spot.AddSpotFactory<TicTacToeGame>();

    }

}

public sealed class TicTacToeEntrySpot : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; }

    public void Configure()
    {
        Context.Handlers.AddHandler<JoinMatchEntryHandler>();
        Context.Handlers.AddHandler<TicTacToeEntryJoinedHandler>();
        Context.Handlers.AddHandler<TicTacToeEntryLeftHandler>();
    }
}

public sealed class TicTacToeGame : IZLinkSpot<PlayActor>
{
    public IZLinkSpotContext Context { get; }

    public void Configure()
    {
        Context.Handlers.AddHandler<PlaceMarkHandler>();
        Context.Handlers.AddHandler<MoveHandler>();
        Context.Handlers.AddHandler<TicTacToeGameJoinedHandler>();
        Context.Handlers.AddHandler<TicTacToeGameLeftHandler>();
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var join = request.Decode<JoinMatchReq>();
        return ValueTask.FromResult(
            ZLinkSpotActorJoinResult.Accept(new JoinMatchRes(join.MatchId)));
    }

    public ValueTask OnJoinedActorAsync(PlayActor actor, CancellationToken cancellationToken)
        => ValueTask.CompletedTask;

    public ValueTask OnLeaveActorAsync(PlayActor actor, CancellationToken cancellationToken)
        => ValueTask.CompletedTask;
}
```

`OnActorJoinAsync(...)`는 actor id와 request만 받는 admission callback이다. remote transfer에서
`PlayActor`의 domain state를 옮겨야 하므로 이 구성은 transfer adapter를 등록한다. state를 옮길 필요가
없는 actor type은 adapter를 등록하지 않으며, framework가 빈 state와 actor factory를 사용한다.

## 8. 직접 dispatch 예시 (session callback)

이 절은 session callback 한곳에서 어떤 결정을 내리고, 어디서부터 framework
helper 를 부르는지 그림을 잡아 둔다.

session callback 이 직접 결정하는 것은 다음과 같다.

- 인증
- actor type 선택
- local actor handle 생성
- dispatch 여부

framework helper 는 actor-session binding 과 transport 세부 작업만 가려 준다.

```csharp
public sealed class TicTacToeSession(IZLinkSessionContext context, IZLinkActorManager actors) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public async ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        if (dispatch.PacketName == "auth")
        {
            AuthReq request = payload.Decode<AuthReq>();

            ActorRef authActor = await actors.GetOrCreateAsync(
                request.ActorId, "player", cancellationToken);
            IZLinkSessionActor actor = await context.Actors.BindAsync(
                authActor,
                cancellationToken);

            authenticatedActors.Remember(request.ActorId, actor);

            context.Client.Reply(new AuthRep(ok: true))
            .Submit();
            return;
        }

        if (authenticatedActors.TryGet(header, out IZLinkSessionActor actor))
        {
            await actor.RelayAsync(payload, cancellationToken);
            return;
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            "No actor route is bound to this session packet.");
    }

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        => ValueTask.CompletedTask;

    public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        authenticatedActors.Clear();
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
        => ValueTask.CompletedTask;
}

public sealed class JoinMatchHandler
{
    [ZLinkSpotActorRequest]
    public async ValueTask<JoinMatchRes> HandleAsync(
        PlayerEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        JoinMatchReq request,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        // request.MatchId는 application 도메인이 정한 match id다.
        // application registry가 user Spot RoutingId로 변환하거나 조회한다.
        var matchSpotRid = RoutingId.From(request.MatchId);
        var joined = await actor.Context
            .JoinSpot(matchSpotRid, request)
            .Async(cancellationToken);
        return joined.Reply.Decode<JoinMatchSpotResult>().ToReply();
    }
}

public sealed class PlaceMarkHandler
{
    [ZLinkSpotActorRequest]
    public ValueTask<PlaceMarkRes> HandleAsync(
        TicTacToeGameSpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        PlaceMarkReq request,
        CancellationToken cancellationToken)
    {
        var room = spot;
        return ValueTask.FromResult(room.PlaceMark(actor.ActorId, request.Cell));
    }
}
```

## 9. Error 표현 (`.NET` exception)

이 절은 framework 가 던지는 오류가 `.NET` 표면에서 어떤 모양으로 보이는지를
정리한다.

public `.NET` API 에서는 framework error 를 하나의 exception family 로
모은다.

```csharp
public sealed class ZLinkFrameworkException : Exception
{
    public ZLinkFrameworkErrorKind Kind { get; }
    public bool IsRetriable { get; }
}

public enum ZLinkFrameworkErrorKind
{
    ActorRouteNotFound,
    ActorCreateFailed,
    ActorAlreadyExists,
    ActorTypeMismatch,
    SpotCreateFailed,
    SpotRouteNotFound,
    SpotTypeMismatch,
    ActorSessionNotBound,
    HandlerNotFound,
    RouteHandlerNotFound,
    ActorDispatchHandlerNotFound,
    PayloadDecodeFailed,
    RouteNotConnected,
    RequestTargetNotFound,
    RequestRejected,
    RequestProtocolError,
    RequestFailed,
    WorkerQueueFull,
    WorkerTimedOut,
    WorkerFailed,
}
```

각 kind 의 발생 조건과 cross-binding 의미는
[policy/session-gateway-usability.ko.md](../../session-actor-dispatch.ko.md)
§17 error-kind 매트릭스에서 다룬다.

`ActorCreateFailed`, `ActorAlreadyExists`, `ActorTypeMismatch` 는
`IZLinkActorManager` 로 local actor 를 준비할 때 사용한다.
`SpotCreateFailed`, `SpotRouteNotFound`, `SpotTypeMismatch` 는 `IZLinkSpotManager` 와
registry 기반 spot route 조회에서 사용한다.
`BindAsync(...)` 와 routed actor dispatch 수신 경로는 actor 를
생성하지 않는다. bind 는 logical actor handle 을 core session relay binding 으로 넘기며,
actor ref resolver 를 fallback 으로 호출하지 않는다. actor 를 찾을 수 없거나
gateway 경로로 relay 할 수 없으면 `ActorRouteNotFound` 로 분류한다. 현재 actor 에
bound 된 session 이 없어서 client push 를 보낼 수 없으면 `ActorSessionNotBound` 로
분류한다.
handler 를 찾지 못했거나 payload decode 에 실패한 inbound request 는
`HandlerNotFound`, `RouteHandlerNotFound`, `ActorDispatchHandlerNotFound`,
`PayloadDecodeFailed` 로 분류한다. route/request 하부에서 반환되는 실패는
`RouteNotConnected`, `RequestTargetNotFound`, `RequestRejected`,
`RequestProtocolError`, `RequestFailed` 로 매핑한다.

`IsRetriable` 은 framework 가 자동으로 retry 해 준다는 의미가 아니다. caller
가 retry policy 를 만들 때 참고할 수 있는 분류일 뿐이다. sample 코드에서도 이
값을 이용해 retry loop 를 만들지 않는다.

## 10. Diagnostic helper

이 절은 `.NET` 사용자가 connection 이나 topology[^topology] 상태를 점검할 때
쓸 수 있는 경로를 정리한다. session actor dispatch 전용 diagnostics 인터페이스를
([handler-interfaces.ko.md](handler-interfaces.ko.md) §10) 표면으로 한다.
운영 점검용이며 session actor dispatch 의 필수 API 는 아니다.


- location runtime query
- location peer row

## 11. 다른 문서와의 관계

이 절은 같은 주제를 다른 각도에서 다루는 문서들을 한 자리에 모아 둔다.

- cross-binding 정책, 의미, 회귀 테스트, POSD 결론 →
  [policy/session-gateway-usability.ko.md](../../session-actor-dispatch.ko.md)
- 인터페이스 전체 정의 → [handler-interfaces.ko.md](handler-interfaces.ko.md)
  §4.4 (session), §5.5 (session relay), §5.6 (`IZLinkBoundSession`), §5.7
  (actor ref resolver)
- actor 라이프사이클과 actor handler 모델 →
  [aspnet-core-actor.ko.md](aspnet-core-actor.ko.md)
- TicTacToe sample contract →
  [tictactoe-game-sample.ko.md](../../../../dotnet/guide/samples/tictactoe-game-sample.ko.md)
- STREAM session 라이프사이클 →
  [aspnet-core-stream.ko.md](aspnet-core-stream.ko.md)

## 12. 회귀 테스트

이 절은 session actor dispatch 표면을 지키기 위해 어느 통합 테스트를 함께
유지해야 하는지를 정리한다.

session actor dispatch 항목은 다음 요소가 하나의 흐름으로 연동되는지를
확인한다.

- stream session
- actor factory
- logical actor binding
- actor-session binding

또한 이전 stream 에서 늦게 도착한 disconnect 가 현재 actor-session 연결을
끊지 않는지도 함께 확인한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RemoteSessionRelayTests.SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence` | session callback에서 actor request를 relay하고, request sequence를 통해 reply를 되돌린다. |
| `ActorDisconnectNotifyTests.ClientClose_Cleans_Session_Without_Actor_Disconnect_Callback` | client stream close 는 session binding cleanup 만 수행하고 Actor disconnect callback 을 호출하지 않는다. |
| `ActorBindingTests.BindActorAsync_DoesNot_Create_LocalActor` | logical actor binding 은 session attach 중 local actor 를 새로 만들지 않는다. |
| `ActorBindingTests.SessionActorBind_WithoutRoute_Is_LocalOnly` | route 없는 bind overload 는 local actor 에만 붙고 remote fallback 을 수행하지 않는다. |
| `RemoteProxyDisconnectTests.BoundSessionDisconnect_FromRemoteActor_Closes_Client_Without_Session_Disconnect_Callback` | remote actor 가 `BoundSession.DisconnectAsync(...)` 를 호출해도 session host 에서 같은 close 의미가 유지된다. |
| `EntrySpotActorDispatchTests.EntrySpotActorDispatch_ConcurrentActors_StartsOutsideEntrySpotSerialLine_AndKeepsSameActorOrdering` | Entry Spot actor packet은 같은 actor 순서를 보존하고, 서로 다른 actor handler 시작은 Entry Spot 직렬 실행 줄에 막히지 않는다. |
| `LocalActorMailboxExecutionTests.LocalActorPackets_Are_Serialized_On_The_EntrySpot_Line` | user Spot에 들어가지 않은 actor packet도 actor별 mailbox 순서를 따른다. |
| `ActorRegistryExecutionTests.ActorDispatch_Rechecks_CurrentLocation_After_Waiting_For_ActorMailbox` | 같은 actor의 앞 packet이 join을 마치고 나면, 대기 중이던 다음 packet이 새 user Spot 위치로 dispatch된다. |
| `ActorLifecycleTests.SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext` | actor join 이후의 dispatch가 현재 spot 실행 문맥에서 실행된다. |
| `ActorSessionStateTests.ActorSessionState_Filters_StaleDisconnect_And_Only_Disconnects_CurrentStream` | 이전 stream의 늦은 disconnect가 현재 actor-session 연결을 끊지 않는다. |
| `HeaderStreamSessionTests.HeaderStreamSession_Can_Close_Current_Client_Stream` | session context가 현재 client stream을 닫고 disconnect callback으로 자연스럽게 이어진다. |
| `SerialExecutorTests.StreamSessionSerialExecutor_Continues_After_Work_Exception` | session queue의 fire-and-forget work 예외가 error sink에 기록되고, 다음 work 실행을 막지 않는다. |
| `SerialExecutorTests.SpotSerialExecutor_Continues_After_Queued_Work_Exception` | Spot queue의 fire-and-forget work 예외가 error sink에 기록되고, 다음 work 실행을 막지 않는다. |
| `SerialExecutorTests.SpotSerialExecutor_ExecuteAsync_Propagates_Work_Exception` | Spot queue에서 완료를 기다리는 실행 경로는 handler 예외를 호출자에게 그대로 돌려준다. |
| `SerialExecutorTests.SerialExecutionQueue_RunAsync_Propagates_Work_Exception` | 공통 serial queue의 `RunAsync(...)`가 work 예외를 error sink에 기록하면서 호출자에게도 전파한다. |
| `SerialExecutorTests.SerialExecutionQueue_Wait_Cancellation_Does_Not_Remove_Queued_Work` | 공통 serial queue에서 completion wait가 취소되더라도 이미 queue에 들어간 work item은 제거되지 않는다. |
| `SerialExecutorTests.ActorDispatchCancellation_Does_Not_Stop_Current_Or_Later_Dispatch` | actor dispatch 대기를 취소해도 현재 실행 중인 dispatch나 이후 dispatch가 중단되지 않는다. |
| `SerialExecutorTests.ActorDispatchMailbox_Runs_Waiters_In_Fifo_Order` | actor mailbox가 등록 순서를 유지한다. |
| `RegressionTests.DotNetRegressionMatrix_Includes_ExecutionSerialization_Guards` | 중앙 regression matrix가 실행 직렬화 관련 회귀 항목을 유지한다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어, 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^session-actor-dispatch]: session actor dispatch는 클라이언트 세션으로 들어온 요청을 그 세션과 묶여 있는 actor로 자동 전달해 주는 패턴이다.
[^cross-binding]: cross-binding은 `.NET`, Java, C++ 등 서로 다른 언어 바인딩에 같은 의미가 동일하게 적용되어야 함을 가리키는 정책 축이다.
[^posd]: POSD(Point Of Significant Decision)는 의미 있는 설계 결정이 내려진 지점을 기록해 두는 표기 방식이다.
[^actor-session-binding]: actor-session binding은 특정 actor가 현재 어떤 client stream session에 연결되어 있는지를 framework/core가 보관하는 상태다.
[^entry-spot]: Entry Spot은 모든 actor가 처음 거치는 공용 입구 역할의 Spot이다. user Spot으로 옮겨 가기 전까지 actor가 머무는 위치다.
[^user-spot]: user Spot은 room이나 game처럼 application 도메인이 정의한 Spot으로, 같은 Spot 안의 actor들이 공유 상태를 두고 상호작용하는 곳이다.
[^topology]: topology는 어떤 노드(channel, spot, registry 등)가 어디에 있는지, 그리고 서로 어떻게 연결되어 있는지를 표현하는 구성 정보다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework ASP.NET Core Actor](aspnet-core-actor.ko.md) | [다음: ZLink Framework ASP.NET Core Monitoring](aspnet-core-monitoring.ko.md)
<!-- framework-adapter-nav:bottom:end -->
