<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework ASP.NET Core Actor](aspnet-core-actor.ko.md) | [다음: ZLink Stream Connector For .NET](streaming-client.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [Actor](./aspnet-core-actor.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [policy/Session Actor Dispatch 사용성](../../policy/session-gateway-usability.ko.md)

# Draft -- ZLink Framework .NET Session Actor Dispatch

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` `ZLink Framework`에서 session actor dispatch
> 표면을 어떤 시그니처와 등록 코드로 노출할지 정리한 문서다.
>
> cross-binding 정책 부분 (의미·계약·실패 의미·테스트 항목·POSD 결론, error kind
> 매트릭스, 회귀 테스트 목록)은
> [policy/session-gateway-usability.ko.md](../../policy/session-gateway-usability.ko.md)
> 를 본다. 이 문서는 그 정책을 `.NET` 표면으로만 내려 옮긴다.

## 1. 목적

`.NET` 사용자가 session 서버와 play 서버를 분리하는 구조를 구체적인 시그니처와
DI 등록 코드로 보고 싶을 때 참고하는 문서다. cross-binding 의미는
[policy/session-gateway-usability.ko.md](../../policy/session-gateway-usability.ko.md)
에 있다.

## 2. 핵심 표면 요약

session actor dispatch가 `.NET`에서 노출하는 핵심 표면은 아래 네 가지다.

| 축 | `.NET` 표면 |
|----|-------------|
| session → actor dispatch | `IZLinkSessionContext.CreateActorAsync(...)`, `CreateActorHandleAsync(...)`, `DispatchToActorAsync(...)` |
| actor handler | `IZLinkEntrySpotActorSendHandler<TActor, TMessage>`, `IZLinkEntrySpotActorRequestHandler<TActor, TRequest, TReply>`, `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>`, `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` |
| actor → client push | `IZLinkSessionProxy.Send(actorId, msg).Submit(...)` / `IZLinkSessionProxy.Request(actorId, req).Submit<TReply>(...)` |
| route 해석 | `IZLinkActorPlayRouteResolver`; actor → client push는 framework/core actor-session binding을 사용 |

전체 인터페이스 정의는 [handler-interfaces.ko.md](./handler-interfaces.ko.md)
§4.4, §5.5, §5.6, §5.7에 모여 있다. 본 문서는 사용 모양과 등록 코드 예시를 모은다.

## 3. Actor handler 표면

### 3.1 typed handler 시그니처

```csharp
public interface IZLinkEntrySpotActorRequestHandler<TActor, in TRequest, TReply>
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorRequestHandler<TSpot, TActor, in TRequest, TReply>
    where TSpot : IZLinkSpot
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorSendHandler<TActor, in TMessage>
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TActor actor,
        TMessage message,
        CancellationToken cancellationToken);
}
```

handler는 transport raw header를 직접 받지 않는다. Session route, stream sequence,
binding token은 framework runtime metadata로 남긴다.

typed actor context는 source session의 `RoutingId`를 노출하지 않는다. handler가
즉시 client에게 push를 보내야 할 때도 `context.SessionProxy.Send(context.ActorId, message)`
처럼 resolver 기반 표면을 사용해야 한다.

### 3.2 metadata snapshot

```csharp
public sealed class ZLinkMessageMetadata
{
    public static ZLinkMessageMetadata Empty { get; }

    public IReadOnlyDictionary<string, string> Application { get; }
    public IReadOnlyDictionary<string, string> Codec { get; }

    public bool TryGetApplicationValue(
        string key,
        out string? value);

    public bool TryGetCodecValue(
        string key,
        out string? value);
}

public interface IZLinkMessageMetadataPolicy
{
    bool CanForwardApplicationKey(string key);
}
```

기본 `IZLinkMessageMetadataPolicy`는 application metadata를 전달하지 않는다. trace id 같은 값을 actor handler까지 전달하려면 framework 등록 단계에서 명시한다.

```csharp
options.ConfigureMetadata(metadata =>
{
    metadata.ForwardApplicationKey("trace-id");
    metadata.ForwardApplicationKey("tenant-id");
});
```

## 4. SessionProxy 호출 표면

```csharp
public interface IZLinkSessionProxy
{
    IZLinkSessionProxySendCall Send<TMessage>(
        string actorId,
        TMessage message);

    IZLinkSessionProxyRequestCall Request<TRequest>(
        string actorId,
        TRequest request);
}

public interface IZLinkSessionProxySendCall
{
    IZLinkSessionProxySendCall WithPacketName(string packetName);

    IZLinkSessionProxySendCall WithMetadata(
        string key,
        string value);

    ValueTask Submit(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionProxyRequestCall
{
    IZLinkSessionProxyRequestCall WithPacketName(string packetName);

    IZLinkSessionProxyRequestCall WithMetadata(
        string key,
        string value);

    IZLinkSessionProxyRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Submit<TReply>(
        CancellationToken cancellationToken = default);
}
```

호출 모양:

```csharp
await sessionProxy
    .Send(actorId, new GameStateChangedMsg(gameId, board))
    .Submit(cancellationToken);

GamePromptRep prompt = await sessionProxy
    .Request(actorId, new ChooseMoveReq(gameId, board))
    .Submit<GamePromptRep>(cancellationToken);
```

`SessionGateway`라는 기존 이름은 새 public API에서 제거한다. session → actor 방향은
`CreateActorAsync(...)`, `CreateActorHandleAsync(...)`, `DispatchToActorAsync(...)`,
actor → client 방향은 `IZLinkSessionProxy`다.

## 5. IZLinkActorClient (actor id 기반 호출)

`IZLinkActorClient`는 actor id 하나로 actor runtime에 호출을 보낸다. route 결정은
`IZLinkActorPlayRouteResolver`가 맡고, 호출자는 play node `RoutingId`를 넘기지 않는다.

```csharp
public interface IZLinkActorClient
{
    IZLinkActorClientSendCall Send<TMessage>(
        string actorId,
        TMessage message);

    IZLinkActorClientRequestCall Request<TRequest>(
        string actorId,
        TRequest request);
}

public interface IZLinkActorClientSendCall
{
    IZLinkActorClientSendCall WithPacketName(string packetName);
    IZLinkActorClientSendCall WithMetadata(string key, string value);
    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkActorClientRequestCall
{
    IZLinkActorClientRequestCall WithPacketName(string packetName);
    IZLinkActorClientRequestCall WithMetadata(string key, string value);
    IZLinkActorClientRequestCall WithTimeout(TimeSpan timeout);
    ValueTask<TReply> Submit<TReply>(CancellationToken cancellationToken = default);
}
```

## 6. Actor/Spot route resolver 등록

공개 resolver는 actor와 spot 축으로 제한한다. session actor dispatch에서 필요한
public resolver는 actor id에서 actor runtime route를 찾는 resolver 하나다. actor가
현재 연결된 client session으로 push/request를 보낼 때는 framework/core가 가진
actor-session binding 상태를 사용한다. actor가 `JoinSpot(spotName, ...)`로 user Spot에
들어가는 경로가 node 경계를 넘을 수 있으면 spot route resolver도 등록한다.

```csharp
public interface IZLinkActorPlayRouteResolver
{
    ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkActorRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid);

public interface IZLinkSpotRouteResolver
{
    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        string spotName,
        CancellationToken cancellationToken);

    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkSpotRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    RoutingId SpotRid);
```

DI 등록 (Session 서버):

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddActorPlayRouteResolver<RegistryPlayRouteStore>();
    options.AddSpotRouteResolver<RegistrySpotRouteStore>();
    // STREAM session 등록 + routed channel 등록 (별도 문서 참고)
});
```

DI 등록 (Play 서버):

```csharp
builder.Services.AddScoped<PlayerActorFactory>();
builder.Services.AddSingleton<RegistryPlayRouteStore>();
builder.Services.AddSingleton<RegistrySpotRouteStore>();
builder.Services.AddZLinkFramework(options =>
{
    options.AddActorFactory<PlayerActorFactory>("player");
    options.AddActorPlayRouteResolver<RegistryPlayRouteStore>();
    options.AddSpotRouteResolver<RegistrySpotRouteStore>();
    // routed channel 등록 + spot mesh 등록 (별도 문서 참고)
});
```

### 6.1 Actor-session binding 상태

framework는 registry 기반 session 위치 조회 API나 별도 기록 API를 public 기본
구현으로 제공하지 않는다. session binding은 actor handle 생성, stream attach, stream
disconnect 흐름에서 framework/core가 갱신하는 내부 상태다. sample이 registry나 Redis를
사용하더라도 그것은 framework public resolver가 아니라 application infrastructure의
구현 세부다.

`IRegistryDiscoveryMetadata` 같은 registry/discovery metadata adapter는 sample code에서
사용할 수 있지만 framework public contract가 아니다. session binding을 registry에
저장하는 샘플을 만들더라도 그 adapter는 framework 내부 binding state를 보조하는
infrastructure 코드일 뿐, 별도 public session 위치 조회 API를 도입하지 않는다.

`DeleteIfAsync(...)`는 저장된 `sessionId`와 `bindingToken`이 모두 맞을 때만 key를
삭제해야 한다. registry metadata API가 조건부 삭제나 compare-and-swap을 제공하지
않는다면 sample adapter는 read 후 delete로 흉내 내면 안 된다.

## 7. 등록 표면 (host 측)

```csharp
options.UseDiscovery(discovery => discovery.Add(registryEndpoint));

options.AddRoutedChannel("backend", routed =>
{
    routed.Bind(playEndpoint);
});

options.AddActorFactory<TicTacToeActorFactory>("player");

public sealed class TicTacToeActor(string actorId)
    : IZLinkActor
{
    public string ActorId { get; } = actorId;
    public IZLinkActorContext Context { get; set; } = default!;

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        => ValueTask.CompletedTask;
}
```

spot handler는 spot 객체 안에서 등록한다.

```csharp
options.AddSpotNode("play", spot =>
{
    spot.Bind(spotEndpoint);
    spot.AddEntrySpot<TicTacToeEntrySpot>();
    spot.AddSpotFactory<TicTacToeGame>("game");
});

public sealed class TicTacToeEntrySpot : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; }

    public void Configure()
    {
        Context.AddActorPacket<JoinMatchEntryHandler, TicTacToeActor>();
        Context.AddActorJoined<TicTacToeEntryJoinedHandler, TicTacToeActor>();
        Context.AddActorLeft<TicTacToeEntryLeftHandler, TicTacToeActor>();
    }
}

public sealed class TicTacToeGame : IZLinkSpot
{
    public IZLinkSpotContext Context { get; }

    public void Configure()
    {
        Context.AddActorJoin<JoinMatchHandler, TicTacToeActor, JoinMatchReq, JoinMatchRes>();
        Context.AddActorPacket<PlaceMarkHandler, TicTacToeActor>();
        Context.AddActorPacket<MoveHandler, TicTacToeActor>();
        Context.AddActorJoined<TicTacToeGameJoinedHandler, TicTacToeActor>();
        Context.AddActorLeft<TicTacToeGameLeftHandler, TicTacToeActor>();
    }
}
```

actor resolver는 transport builder가 아니라 framework service 설정에 등록한다.

```csharp
options.AddActorPlayRouteResolver<TicTacToePlayRouteResolver>();
```

## 8. 직접 dispatch 예시 (session callback)

session callback이 인증, actor type 선택, local actor handle 생성, dispatch 여부를
직접 고른다. framework helper는 actor-session binding과 transport 세부 작업만 숨긴다.

```csharp
public sealed class TicTacToeSession : IZLinkSession
{
    public IZLinkSessionContext Context { get; set; } = default!;

    public async ValueTask OnDispatchAsync(
        IZLinkSessionPacket packet,
        CancellationToken cancellationToken)
    {
        if (packet.PacketName == "auth")
        {
            AuthReq request = packet.Decode<AuthReq>();

            IZLinkActorRef actor = await Context.CreateActorHandleAsync(
                request.ActorId,
                request.ActorType,
                cancellationToken);

            authenticatedActors.Remember(request.ActorId, actor);

            await Context.Reply(new AuthRep(ok: true))
                .Submit(cancellationToken);
            return;
        }

        if (authenticatedActors.TryGet(packet, out IZLinkActorRef actor))
        {
            await Context.DispatchToActorAsync(
                actor,
                packet,
                cancellationToken);
            return;
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorNotAuthenticated,
            "Actor is not bound to this session.");
    }

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        => ValueTask.CompletedTask;

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        => ValueTask.CompletedTask;

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
        => ValueTask.CompletedTask;
}

public sealed class JoinMatchHandler
    : IZLinkEntrySpotActorRequestHandler<PlayerActor, JoinMatchReq, JoinMatchRes>
{
    public async ValueTask<JoinMatchRes> HandleAsync(
        PlayerActor actor,
        JoinMatchReq request,
        CancellationToken cancellationToken)
    {
        // request.MatchId는 application domain spot 이름이다.
        // RoutingId 변환은 framework 내부 spot route resolver가 푼다.
        var joined = await actor.Context
            .JoinSpot<JoinMatchSpotResult, JoinMatchReq>(request.MatchId, request)
            .Submit(cancellationToken);
        return joined.ToReply();
    }
}

public sealed class PlaceMarkHandler
    : IZLinkSpotActorRequestHandler<TicTacToeGameSpot, PlayerActor, PlaceMarkReq, PlaceMarkRes>
{
    public ValueTask<PlaceMarkRes> HandleAsync(
        TicTacToeGameSpot spot,
        PlayerActor actor,
        PlaceMarkReq request,
        CancellationToken cancellationToken)
    {
        var room = spot;
        return ValueTask.FromResult(room.PlaceMark(actor.ActorId, request.Cell));
    }
}
```

## 9. Error 표현 (`.NET` exception)

public `.NET` API에서는 framework error를 하나의 exception family로 모은다.

```csharp
public sealed class ZLinkFrameworkException : Exception
{
    public ZLinkFrameworkErrorKind Kind { get; }
    public bool IsRetriable { get; }
}

public enum ZLinkFrameworkErrorKind
{
    ActorNotAuthenticated,
    ActorRouteNotFound,
    ActorCreateFailed,
    ActorAlreadyExists,
    ActorSessionNotBound,
    SessionProxyTimeout,
    ActorDispatchTimeout,
    ActorDispatchHandlerFailed,
    CodecFailed,
}
```

각 kind의 발생 조건과 cross-binding 의미는
[policy/session-gateway-usability.ko.md](../../policy/session-gateway-usability.ko.md)
§17 error-kind 매트릭스를 본다.

`ActorCreateFailed`와 `ActorAlreadyExists`는 local `SpotNode` actor runtime에서 actor를
만들거나 handle을 준비할 때 사용한다. `CreateActorHandleAsync(...)`는 remote node를 직접
지정하지 않으며, 현재 actor에 bound session이 없어서 client push를 할 수 없으면
`ActorSessionNotBound`로 분류한다.

`IsRetriable`은 framework가 자동 retry한다는 뜻이 아니다. caller가 retry policy를
만들 때 참고할 수 있는 분류다. sample은 이 값을 사용해 retry loop를 만들지 않는다.

## 10. Diagnostic helper

`.NET` 사용자가 connection / topology 상태를 점검할 수 있는 diagnostic helper
초안이다. session actor dispatch의 필수 API는 아니고 운영 점검용이다.

```csharp
public interface IZLinkTopologyDiagnostics
{
    ValueTask<ZLinkRoutedChannelSnapshot> GetRoutedChannelAsync(
        string routerChannelId,
        CancellationToken cancellationToken = default);
}
```

retry helper와는 다르다. diagnostic helper는 registry view, discovery member,
local routed channel state를 보여 주는 역할만 한다.

## 11. 다른 문서와의 관계

- cross-binding 정책, 의미, 회귀 테스트, POSD 결론 →
  [policy/session-gateway-usability.ko.md](../../policy/session-gateway-usability.ko.md)
- 인터페이스 전체 정의 → [handler-interfaces.ko.md](./handler-interfaces.ko.md)
  §4.4 (session), §5.5 (`IZLinkActorClient`), §5.6 (`IZLinkSessionProxy`), §5.7
  (actor route resolver)
- actor 라이프사이클과 actor handler 모델 → [aspnet-core-actor.ko.md](./aspnet-core-actor.ko.md)
- TicTacToe sample contract → [tictactoe-game-sample.ko.md](./tictactoe-game-sample.ko.md)
- STREAM session 라이프사이클 → [aspnet-core-stream.ko.md](./aspnet-core-stream.ko.md)
