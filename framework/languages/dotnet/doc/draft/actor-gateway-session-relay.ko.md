<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Draft -- ZLink Framework .NET Channel Handler Exposure And SPOT Route Transport](./channel-handler-exposure-and-spot-route-transport.ko.md)
<!-- framework-adapter-nav:end -->

[.NET 묶음](../README.ko.md) | [Actor](../spec/aspnet-core-actor.ko.md) | [Session Actor Dispatch](../spec/session-actor-dispatch.ko.md) | [SPOT](../spec/aspnet-core-spot.ko.md) | [SpotNode](../spec/spot-node.ko.md) | [인터페이스](../spec/handler-interfaces.ko.md) | [Regression Matrix](../internals/regression-test-matrix.ko.md)

# Draft -- ZLink Framework .NET ActorGateway Session Relay

> 이 문서는 공통 초안
> [`doc/spec/draft/actor-gateway-session-relay.ko.md`](../../../../../doc/spec/draft/actor-gateway-session-relay.ko.md)의
> `.NET` framework projection 이다. 공통 C API, binding 공통 계약, core 문서가 단일 기준이며,
> 이 문서는 framework 적용 상태와 회귀 기준만 정리한다.

## 1. 반영 상태

현재 framework 는 session actor relay 를 application route mesh channel 로 보내지 않는다.
STREAM session 이 사용할 local SpotNode 를 `AttachActorGateway(...)` 로 지정하고,
`BindActorHandleAsync(...)` 는 local actor handle 또는 framework 가 발급한 remote actor
locator 를 core ActorGateway 경로에 bind 한다.

```csharp
options.AddSpotMesh("game.session", mesh =>
{
    mesh.AddNode("session-node", node =>
    {
        node.Bind(sessionSpotEndpoint);
        node.EnableRouter(router =>
        {
            router.Bind(sessionRouterEndpoint);
            router.ConfigureRouting(routing => routing.RoutingId = sessionNodeRid);
        });
    });
});

options.AddStreamNode("client-stream", stream =>
{
    stream.AttachActorGateway("session-node");
    stream.Bind(streamEndpoint);
    stream.RegisterSession<GameSession>();
});
```

session handler 는 route mesh channel 이름이나 router socket 을 알 필요가 없다. actor 가
다른 process 의 ActorGateway 에 있으면 Play 서버가 `ZLinkActorRemoteAddress` 를 응답에 싣고,
Session 서버는 그 locator 로 bind 한다.

```csharp
var actor = await Context.BindActorHandleAsync(
    actorId,
    actorType,
    remoteAddress,
    cancellationToken);

await Context.RelayToActorAsync(
    actor,
    header,
    payload,
    cancellationToken);
```

## 2. Public Surface

### 2.1 session side

`IZLinkSessionActorDispatchContext` 는 local bind 와 remote bind 를 모두 제공한다. remote bind 의
`ZLinkActorRemoteAddress` 는 사용자가 임의로 만드는 route mesh 주소가 아니라 actor host
runtime 이 `IZLinkActorManager.GetRemoteAddressAsync(...)` 로 발급한 ActorGateway locator 다.

```csharp
public interface IZLinkSessionActorDispatchContext
{
    ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        ZLinkActorRemoteAddress remoteAddress,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActorRef> BindActorHandleAsync(
        IZLinkActorRef actor,
        CancellationToken cancellationToken = default);

    ValueTask RelayToActorAsync(
        IZLinkActorRef actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default);
}
```

`IZLinkActorRef` 는 actor identity handle 이며 local/remote 상태를 함께 드러낸다.
`IsRemote` 와 `RemoteAddress` 는 session rebind 와 sample contract 전달에 쓰는 locator 정보다.
일반 dispatch 는 `RelayToActorAsync(...)` 에 맡기며 caller 가 application route mesh 로 직접
분기하지 않는다.

### 2.2 actor side

Actor 에서 현재 bound client session 으로 보내는 표면은 `BoundSession` 이다.

```csharp
public interface IZLinkActorContext
{
    IZLinkBoundSession BoundSession { get; }
}

public interface IZLinkBoundSession
{
    IZLinkBoundSessionSendCall Send<TMessage>(TMessage message);

    ValueTask DisconnectAsync(
        CancellationToken cancellationToken = default);
}
```

`BoundSession` 은 server-to-client request API 를 제공하지 않는다. client request 에 대한 응답은
actor request handler 의 반환값과 원래 request correlation 으로 처리한다.

## 3. Runtime Mapping

| 영역 | 현재 적용 기준 |
|------|----------------|
| stream initialization | `ZLinkStreamRuntimeManager` 가 stream bind 전에 configured SpotNode 에 `AttachActorGateway(...)` 를 호출한다 |
| session bind | `ZLinkSessionActorCoordinator` 가 local actor ref 또는 remote locator 에서 얻은 actor ref 를 backend stream `BindActorAsync(...)` 로 넘긴다 |
| session relay | `RelayToActorAsync(...)` 는 framework route mesh packet 을 만들지 않고 backend stream `SendBoundActor(...)` 를 사용한다 |
| actor push | `ZLinkBoundSessionService` 가 backend ActorGateway send wrapper 로 내려간다 |
| actor disconnect | `BoundSession.DisconnectAsync(...)` 는 backend ActorGateway close wrapper 로 내려간다 |
| route channel isolation | `ZLinkRouteChannelInitializer` 는 application route dispatcher 만 등록하고 session actor dispatch packet dispatcher 를 붙이지 않는다 |
| cleanup | stream close 는 session binding cleanup 만 수행하고 actor current Spot 을 바꾸지 않는다 |

framework 는 `bindings/dotnet` public API 만 호출한다. binding 에 없는 native 기능을
reflection 으로 우회하지 않는다.

## 4. Sample 상태

| 샘플 | 반영 기준 |
|------|-----------|
| Bingo session server | session relay 용 `AddRouteMeshChannel(...)` 을 제거하고 session SpotNode + `AttachActorGateway(...)` 를 사용한다 |
| Bingo play server | actor host SpotNode 를 routed-capable 로 구성하고 application Spot route egress 는 별도 route mesh channel 로 유지한다 |
| TicTacToe session gateway | Bingo 와 같은 session SpotNode + stream attach 구조를 사용한다 |
| sample contracts | ensure actor 응답은 actor id/type 과 ActorGateway remote address snapshot 을 담는다 |

Play 서버의 route mesh channel 은 routed Spot egress 를 위한 application channel 이다.
session actor relay 설정으로 해석하면 안 된다.

## 5. 문서 반영 상태

| 문서 | 반영 내용 |
|------|-----------|
| `../spec/aspnet-core-stream.ko.md` | session actor bind public signature 에 local/remote overload 를 반영 |
| `../spec/aspnet-core-actor.ko.md` | actor context 는 `BoundSession` 을 제공하고 ActorGateway remote locator flow 를 반영 |
| `../spec/session-actor-dispatch.ko.md` | session relay 와 actor-to-session push 를 ActorGateway/BoundSession 기준으로 설명 |
| `../spec/handler-interfaces.ko.md` | handler public surface 에 `BoundSession` 과 logical actor binding 을 반영 |
| `../guide/06-actor-session.ko.md` | actor/session 사용 흐름에서 route mesh relay 설정을 제거 |
| `../internals/behavior-matrix.ko.md` | stale bound session, gateway route, cleanup 의미를 반영 |
| `../internals/regression-test-matrix.ko.md` | bound session factory, gateway relay, sample regression 항목을 반영 |

## 6. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RemoteSessionRelayTests.SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence` | session 에서 remote Actor 로 보낸 request 가 ActorGateway 경로로 전달되고, 응답과 Actor 의 bound session push 가 client 로 돌아온다. |
| `RemoteProxyDisconnectTests.BoundSessionDisconnect_FromRemoteActor_Closes_Client_Without_Session_Disconnect_Callback` | remote Actor 가 `BoundSession.DisconnectAsync(...)` 를 호출하면 client stream 이 닫히고 session disconnect handler 로 우회하지 않는다. |
| `LocalSessionRelayTests.LocalSessionActorDispatch_Relays_Stream_Request_And_Replies_From_Request_Handler` | local session relay 도 application route mesh packet 없이 ActorGateway binding 으로 동작한다. |
| `ActorDisconnectNotifyTests.ClientClose_Cleans_Session_Without_Actor_Disconnect_Callback` | client stream close 는 session binding cleanup 만 수행하고 Actor disconnect callback 을 호출하지 않는다. |
| `NodesAndServicesTests.AddZLinkFramework_Registers_BoundSession_Factory` | framework DI 는 bound session factory 를 등록한다. |
| `RegressionTests.Bingo_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store` | Bingo sample 이 application route mesh resolver 없이 ActorGateway remote locator 로 bind 하고 session host 에 route mesh relay channel 을 두지 않는다. |
| `RegressionTests.TicTacToe_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store` | TicTacToe session gateway sample 도 같은 ActorGateway locator binding 규칙과 session ActorGateway attach 를 유지한다. |

## 7. 남은 검증 기준

이 projection 에서 남은 구현 항목은 공통 초안의 core/binding 전체 검증과 동일하다.
framework 단독으로 core ActorGateway 의미를 흉내 내는 코드를 추가해서는 안 된다.

[^public-contract]: 공개 계약은 구현, 테스트, 정식 spec 문서가 함께 맞춰진 API 를 뜻한다.
