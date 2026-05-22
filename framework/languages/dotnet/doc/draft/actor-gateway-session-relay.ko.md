<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Draft -- ZLink Framework .NET Channel Handler Exposure And SPOT Route Transport](./channel-handler-exposure-and-spot-route-transport.ko.md)
<!-- framework-adapter-nav:end -->

[.NET 묶음](../README.ko.md) | [Actor](../spec/aspnet-core-actor.ko.md) | [Session Actor Dispatch](../spec/session-actor-dispatch.ko.md) | [SPOT](../spec/aspnet-core-spot.ko.md) | [SpotNode](../spec/spot-node.ko.md) | [인터페이스](../spec/handler-interfaces.ko.md) | [Regression Matrix](../internals/regression-test-matrix.ko.md)

# Draft -- ZLink Framework .NET ActorGateway Session Relay

> 이 문서는 **구현 전 초안**이다.
> 아직 공개 계약[^public-contract]이 아니며, remote actor 와 session relay 를
> `RouteMeshChannel` 직접 설정이 아니라 SpotNode 기반 `ActorGateway` 모델로 정리하기
> 위한 설계안이다.

## 1. 목적

현재 Bingo 와 TicTacToe session gateway 샘플은 remote actor dispatch 를 위해 session
서버에도 route mesh channel 을 등록한다.

```csharp
options.AddRouteMeshChannel("bingo.gateway", routed =>
{
    routed.Bind(session.RouterEndpoint);
    routed.ConfigureRouting(routing => routing.RoutingId = session.RoutingId);
});
```

이 설정은 동작에는 필요하다. session 이 remote actor 로 packet 을 relay 하고, remote
actor 가 `SessionProxy` 로 client stream 에 push 하려면 session 서버도 routed transport 에
참여해야 하기 때문이다.

하지만 사용자 관점에서는 개념이 어색하다.

- 사용자는 stream session 과 actor 를 쓰고 있는데, 왜 일반 route mesh channel 을 직접
  만들어야 하는지 알기 어렵다.
- `RouteMeshChannel` 이 application routed messaging 용도인지, actor/session 내부
  transport 인지 구분하기 어렵다.
- SpotNode 는 이미 router 를 갖고 있는데, actor/session transport 를 별도 route mesh 로
  또 구성해야 하는지 혼란스럽다.
- session 이 actor remote address 의 router channel id 를 들고 직접 relay 하는 구조는
  session 이 transport 세부를 아는 것처럼 보인다.

이 초안의 목표는 다음과 같다.

1. session actor bind 와 relay 는 반드시 SpotNode 기반 actor runtime 위에서 동작하도록
   정한다.
2. remote actor 로 가는 메시지는 session 이 route mesh channel 을 직접 고르는 대신,
   local SpotNode 안의 framework owned `ActorGateway` 를 경유하도록 한다.
3. actor 가 다른 SpotNode 의 EntrySpot 또는 user Spot 으로 이동해도 session 은 같은 actor
   handle 로 계속 relay 할 수 있어야 한다.
4. `SessionProxy` 역방향 메시지도 같은 gateway 모델로 정리한다.
5. 일반 `AddRouteMeshChannel(...)` 은 application routed channel messaging 용도로 남기고,
   actor/session 내부 transport 와 혼합하지 않는다.

## 2. 용어

### 2.1 ActorGateway

`ActorGateway` 는 SpotNode 안에 framework 가 소유하는 actor relay endpoint 다.
사용자가 직접 생성하거나 handler group 으로 노출하는 actor 가 아니다.

역할은 다음과 같다.

- session 에 붙은 actor handle 의 현재 위치를 해석한다.
- session 에서 actor 로 들어오는 packet 을 local actor 또는 remote SpotNode 로 전달한다.
- actor 가 EntrySpot 또는 user Spot 으로 join 한 뒤 actor 의 current location 을 갱신한다.
- remote actor 가 `SessionProxy` 로 client 에 push 할 때 session binding owner 로 전달한다.
- actor/session relay 에 필요한 내부 packet kind 와 routing metadata 를 숨긴다.

이름은 `ProxyActor` 대신 `ActorGateway` 를 사용한다. `ProxyActor` 는 user actor 처럼
보일 수 있고, "진짜 actor 의 proxy" 만 의미하는 것으로 좁게 읽힐 수 있다. 이 설계에서
gateway 는 actor 생성, actor 위치 변경, session binding, remote relay 를 함께 다루는
framework transport boundary 이므로 `ActorGateway` 가 더 맞다.

### 2.2 local ActorGateway

session 이 실행되는 framework runtime 안의 SpotNode 에 붙은 gateway 다.
session 은 actor relay 를 할 때 항상 local ActorGateway 로 먼저 보낸다.

session 이 remote actor 의 router channel, target node rid, spot rid 를 직접 선택하지 않는다.
local ActorGateway 가 현재 actor location 을 보고 다음 hop 을 결정한다.

### 2.3 current actor location

actor 의 현재 실행 위치다.

초기에는 session host 의 local ActorGateway 가 소유할 수 있다. actor 가 Play 서버의
EntrySpot 으로 join 하면 current actor location 은 해당 SpotNode 의 actor runtime 으로
바뀐다. actor 가 다시 다른 Spot 으로 이동하면 location 도 다시 바뀐다.

location 은 framework 내부 상태다. session application code 는 location 을 직접 들고 있지
않고 `IZLinkActorRef` 같은 logical handle 만 유지한다.

### 2.4 actor bind

session 이 actor 와 연결되는 작업이다. bind 뒤 session 은 client packet 을 actor 로 relay 할
수 있고, actor 는 `SessionProxy` 로 client stream 에 push 할 수 있다.

이 초안에서는 actor bind 를 하려면 해당 runtime 에 SpotNode 와 ActorGateway 가 있어야 한다.
SpotNode 없이 session 만 있는 host 에서는 actor bind 를 허용하지 않는다.

### 2.5 route mesh transport

물리 transport 는 여전히 ROUTER mesh 일 수 있다.
하지만 ActorGateway 가 쓰는 transport 는 public `RouteMeshChannel` 과 같은 application
channel 로 보이면 안 된다.

정리하면 다음과 같다.

| 구분 | 목적 | public route handler | 사용자 client |
|------|------|----------------------|---------------|
| application route mesh channel | 사용자가 직접 보내는 routed request/send | 가능 | `IZLinkRouteClient` |
| ActorGateway transport | actor/session 내부 relay | 불가 | 노출하지 않음 |
| SpotNode router | Spot ingress 와 actor runtime gateway | 불가 또는 별도 system packet 만 허용 | 직접 노출하지 않음 |

## 3. 현재 구조의 문제

### 3.1 session host 가 route mesh 를 직접 설정한다

현재 session host 는 actor relay 를 위해 route mesh channel 을 등록해야 한다.
이 설정은 동작상 필요하지만, 사용자 의도와 맞지 않는다.

사용자는 다음 기능을 쓰려는 것이다.

```csharp
var actor = await context.Stream.BindActorHandleAsync(...);
await context.Stream.RelayToActorAsync(actor, header, payload, cancellationToken);
```

그런데 실제로는 `AddRouteMeshChannel(...)` 이 없으면 relay 가 실패한다. 이 의존성은
actor/session 표면이 아니라 transport 표면으로 드러난다.

### 3.2 session 이 actor route 세부를 안다

현재 remote actor bind 는 `ZLinkActorRemoteAddress` 를 session 쪽으로 가져온다.
session 은 actor id 와 함께 router channel id, target node rid, actor generation 을 가진
actor ref 를 들고 relay 한다.

이 방식은 hot path 에 resolver 를 쓰지 않는다는 장점은 있다. 하지만 session 이 route
transport 를 아는 모양이 되며, actor 가 SpotNode 사이를 이동하는 책임도 session binding
쪽으로 번진다.

### 3.3 SpotNode router 와 route mesh channel 의 관계가 모호하다

SpotNode 는 이미 router 를 가질 수 있다.

```csharp
spot.EnableRouter();
```

하지만 session host 에서는 별도 route mesh channel 도 등록한다.

```csharp
options.AddRouteMeshChannel("bingo.gateway", routed => { ... });
```

사용자에게는 둘 다 "router" 로 보인다. 어느 router 가 actor relay 에 쓰이는지, 어느 router 가
Spot ingress 에 쓰이는지 설명이 길어진다. 특히 routed Spot egress 와 actor/session relay 가
같은 이름의 channel 을 공유하면 개념이 더 섞인다.

### 3.4 일반 route mesh 와 framework internal route 가 섞인다

`AddRouteMeshChannel(...)` 은 application routed messaging 의 public channel 이다.
handler group 을 붙이고 `IZLinkRouteClient` 로 send/request 할 수 있는 표면이다.

actor/session relay 는 framework internal packet 이다. 이 둘을 같은 registration bucket 에
넣으면 다음 문제가 생긴다.

- handler 등록 가능 여부가 헷갈린다.
- monitoring source 이름이 application channel 과 system transport 를 함께 담는다.
- 보안 정책을 나누기 어렵다.
- sample 설정이 feature 사용 의도보다 transport 세부를 먼저 보여 준다.

## 4. 설계 결정

### 4.1 ActorGateway 를 SpotNode 기능으로 둔다

ActorGateway 는 SpotNode 에 붙는다. session actor bind 를 쓰려면 framework runtime 안에
ActorGateway 를 가진 SpotNode 가 하나 이상 있어야 한다.

초안 API 는 다음 형태를 후보로 둔다.

```csharp
options.AddSpotMesh("bingo.actors", spotMesh =>
{
    spotMesh.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
    spotMesh.AddNode("session-node", node =>
    {
        node.Bind(session.SpotEndpoint);
        node.EnableActorGateway(gateway =>
        {
            gateway.RoutingId = session.RoutingId;
        });
    });
});
```

또는 stream node 와 가까운 형태로 별도 helper 를 둘 수 있다.

```csharp
options.AddStreamNode("client.stream", stream =>
{
    stream.Bind(session.StreamEndpoint);
    stream.AddHeaderSession<BingoSession>();
    stream.UseActorGateway("session-node");
});
```

첫 구현에서는 중복 API 를 피하고 SpotNode 쪽 capability 로 시작하는 편이 낫다. actor
gateway 는 actor runtime 과 SpotNode router 에 강하게 묶이기 때문이다.

### 4.2 session actor bind 는 ActorGateway 없이는 실패한다

session 에서 actor bind 를 호출하려면 local ActorGateway 가 필요하다.

```csharp
var actor = await context.Stream.BindActorHandleAsync(
    actorId,
    SampleNames.PlayerActorType,
    cancellationToken);
```

gateway 가 없으면 framework 는 명확한 설정 오류를 내야 한다.

후보 오류:

- startup validation: stream node 가 actor session relay 를 쓰도록 등록되었는데
  ActorGateway 가 없음
- bind 시점 오류: `BindActorHandleAsync(...)` 호출 시 local ActorGateway 가 없음

사용자 경험은 startup validation 이 더 좋다. 하지만 모든 session 이 actor bind 를 쓰는지
정적으로 알기 어렵다. 따라서 첫 단계에서는 bind 시점 `ZLinkConfigurationException` 으로
시작하고, stream node 에 `RequireActorGateway()` 같은 opt-in 을 추가하면 startup validation 으로
강화할 수 있다.

### 4.3 session 은 actor remote address 를 직접 들지 않는다

목표 모델에서 session 은 `ZLinkActorRemoteAddress` 를 public attach 입력으로 받지 않는다.
session 은 logical actor handle 을 bind 한다.

```csharp
ValueTask<IZLinkActorRef> BindActorHandleAsync(
    string actorId,
    string actorType,
    CancellationToken cancellationToken = default);
```

remote actor 위치는 ActorGateway 가 관리한다. actor ref 는 actor id, actor type, binding
token 같은 logical 값만 공개한다. route address 는 내부 state 로 내려간다.

기존 `ZLinkActorRemoteAddress` 는 다음 위치로 이동한다.

- public remote actor service call 이 정말 필요한 경우의 위치 descriptor
- registry resolver 내부 값
- ActorGateway 간 protocol metadata

session hot path 의 public 입력으로는 사용하지 않는다.

### 4.4 actor 생성과 EntrySpot join 은 gateway 가 수행한다

session 이 인증된 뒤 actor 를 만들고 Play 서버의 EntrySpot 으로 join 시키는 흐름은 다음처럼
보인다.

```csharp
var actor = await context.Stream.BindActorHandleAsync(
    authenticated.ActorId,
    SampleNames.PlayerActorType,
    cancellationToken);

await actor.JoinEntrySpotAsync(
    SampleNames.RoomEntrySpot,
    new JoinEntrySpotReq(...),
    cancellationToken);
```

이 코드는 초안용 예시다. 실제 API 이름은 actor context 와 spot join 표면을 함께 보고
정해야 한다.

중요한 점은 session 이 Play 서버의 route channel 을 몰라도 된다는 것이다.
ActorGateway 가 actor id 와 target EntrySpot 을 보고 다음 작업을 수행한다.

1. local actor slot 을 만들거나 기존 actor handle 을 찾는다.
2. target EntrySpot remote address 를 registry 또는 Spot discovery 로 찾는다.
3. ActorGateway transport 로 remote SpotNode 에 join request 를 보낸다.
4. remote SpotNode 가 actor 를 EntrySpot execution context 안으로 attach 한다.
5. actor current location 을 remote SpotNode 로 갱신한다.
6. session binding 은 같은 logical actor handle 을 유지한다.

### 4.5 relay 는 항상 gateway 로 들어간다

session relay 는 actor 가 local 인지 remote 인지 알지 않는다.

```csharp
await context.Stream.RelayToActorAsync(actor, header, payload, cancellationToken);
```

내부 흐름은 다음과 같다.

```text
+----------------+       +----------------+       +----------------+
| Session        |       | Local Gateway  |       | Target Gateway |
| Bind/Relay     | ----> | Resolve Actor  | ----> | Dispatch Actor |
+----------------+       +----------------+       +----------------+
```

local actor 라면 Local Gateway 가 바로 local actor mailbox 로 넣는다.
remote actor 라면 Local Gateway 가 current actor location 을 보고 Target Gateway 로 내부 packet 을
보낸다.

session 은 route mesh socket, target node rid, spot rid 를 직접 고르지 않는다.

### 4.6 SessionProxy 도 gateway 경유로 통일한다

remote actor 가 client 에게 push 할 때도 같은 gateway 모델을 쓴다.

```csharp
await Context.SessionProxy
    .Send(new BingoStateNotify(state))
    .PacketName(SampleNames.StatePacket)
    .Submit(cancellationToken);
```

내부 흐름은 다음과 같다.

```text
+----------------+       +----------------+       +----------------+
| Actor          |       | ActorGateway   |       | Session Host   |
| SessionProxy   | ----> | Resolve Bind   | ----> | Client Stream  |
+----------------+       +----------------+       +----------------+
```

ActorGateway 는 actor id 로 session binding owner 를 찾는다. binding owner 가 local 이면 바로
stream 에 쓴다. remote 이면 owner gateway 로 internal session proxy packet 을 보낸다.

이때 session 위치 조회를 public resolver 로 만들지 않는다. session binding 은 framework 내부
상태이며, actor-session bind/update/disconnect 흐름에서 유지한다.

## 5. 권장 흐름

### 5.1 인증과 actor bind

session handler 는 인증을 마친 뒤 actor 를 bind 한다.

```csharp
internal sealed class AuthenticateBingoSessionHandler(
    IZLinkClient channels)
    : IBingoSessionHandler
{
    public async ValueTask HandleAsync(
        BingoSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var authenticated = await channels
            .Request(SampleNames.ApiChannel, new AuthenticatePlayerReq(accessToken))
            .SubmitAsync<AuthenticatePlayerRes>(cancellationToken);

        var actor = await context.Stream.BindActorHandleAsync(
            authenticated.ActorId,
            SampleNames.PlayerActorType,
            cancellationToken);

        context.State.AttachAuthenticatedActor(
            authenticated.DisplayName,
            actor);
    }
}
```

여기에는 `EnsurePlayerActorReq` 로 remote address snapshot 을 받아오는 단계가 없다.
actor 생성과 위치 준비는 ActorGateway 의 책임이다.

### 5.2 EntrySpot join

인증 직후 actor 를 EntrySpot 으로 join 시켜도 되고, match 요청 시 join 시켜도 된다.

```csharp
var joined = await actor.JoinEntrySpotAsync(
    SampleNames.RoomEntrySpot,
    new JoinBingoEntryReq(authenticated.DisplayName),
    cancellationToken);
```

join 이 성공하면 actor current location 은 EntrySpot 을 소유한 remote SpotNode 쪽으로
바뀐다. 이후 session relay 는 별도 갱신 없이 remote actor 로 전달되어야 한다.

### 5.3 client packet relay

session packet handler 는 기존처럼 actor handle 로 relay 한다.

```csharp
var actor = context.State.RequireActor("matching bingo");
await context.Stream.RelayToActorAsync(actor, header, payload, cancellationToken);
```

actor 가 어느 SpotNode 에 있는지는 session handler 의 관심사가 아니다.

### 5.4 actor 에서 client 로 push

actor 또는 Spot handler 는 `SessionProxy` 를 사용한다.

```csharp
await actor.Context.SessionProxy
    .Send(new PlayerJoinedNotify(...))
    .PacketName(SampleNames.PlayerJoinedPacket)
    .Submit(cancellationToken);
```

ActorGateway 가 actor-session binding 을 보고 session owner 로 전달한다.

## 6. 설정 모델

### 6.1 현재 Bingo 설정

현재 session host 는 route mesh channel 을 직접 등록한다.

```csharp
options.AddRouteMeshChannel(SampleNames.RouterChannel, routed =>
{
    routed.Bind(session.RouterEndpoint);
    routed.ConfigureRouting(routing => routing.RoutingId = session.RoutingId);
});
```

목표는 이 설정을 session sample 에서 제거하는 것이다.

### 6.2 목표 설정

session host 는 stream node 와 ActorGateway 를 가진 SpotNode 를 등록한다.

```csharp
options.AddSpotMesh(SampleNames.ActorMesh, mesh =>
{
    mesh.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
    mesh.AddNode(session.NodeName, node =>
    {
        node.Bind(session.SpotEndpoint);
        node.EnableActorGateway(gateway =>
        {
            gateway.RoutingId = session.RoutingId;
        });
    });
});

options.AddStreamNode(SampleNames.StreamNode, stream =>
{
    stream.Bind(session.StreamEndpoint);
    stream.AddHeaderSession<BingoSession>();
});
```

Play host 도 ActorGateway 를 가진 SpotNode 를 등록한다.

```csharp
options.AddSpotMesh(SampleNames.ActorMesh, mesh =>
{
    mesh.UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint));
    mesh.AddNode(SampleNames.PlayNode, node =>
    {
        node.Bind(topology.PlaySpotEndpoint);
        node.EnableRouter();
        node.EnableActorGateway(gateway =>
        {
            gateway.RoutingId = topology.PlayRid;
        });
        node.AddEntrySpot<BingoEntrySpot>();
        node.AddSpotFactory<BingoRoomSpot>(SampleNames.RoomSpotType);
    });
});
```

실제 API 는 `EnableRouter()` 와 `EnableActorGateway(...)`의 관계를 정리한 뒤 확정한다.
ActorGateway 가 SpotNode router 를 사용한다면 `EnableActorGateway(...)`가 필요한 router
capability 를 내부에서 켤 수 있어야 한다. 사용자가 같은 의미의 router 설정을 두 번 쓰게 하면
안 된다.

### 6.3 route mesh channel 의 남은 역할

`AddRouteMeshChannel(...)` 은 제거하지 않는다. 다만 역할을 application routed messaging 으로
제한한다.

```csharp
options.AddRouteMeshChannel("admin.routes", route =>
{
    route.Bind("tcp://0.0.0.0:7901");
    route.AddHandlerGroup("admin");
});
```

actor/session relay 는 이 public route channel 을 쓰지 않는다.

## 7. 내부 transport 선택

### 7.1 SpotNode router 를 재사용할 수 있는가

가능하다. 다만 그대로 섞으면 안 된다.

SpotNode router 를 재사용하려면 frame kind 를 분리해야 한다.

- Spot packet
- Spot request
- ActorGateway relay
- ActorGateway session proxy
- ActorGateway location update

Spot application handler dispatch 는 framework internal packet 을 받으면 안 된다.
반대로 ActorGateway 는 user spot packet 을 처리하면 안 된다.

### 7.2 별도 internal route socket 을 둘 수 있는가

가능하다. 이 경우 SpotNode 와 ActorGateway 가 같은 lifecycle 안에 있어도 socket 은 분리된다.

장점:

- protocol 분리가 쉽다.
- monitoring 과 보안 정책을 분리하기 좋다.
- 기존 SpotNode router 동작을 덜 건드린다.

단점:

- endpoint 와 discovery metadata 가 늘어난다.
- "SpotNode 가 있는데 actor gateway socket 도 있다"는 내부 구조가 생긴다.

### 7.3 초안의 선택

초안은 **ActorGateway 를 SpotNode capability 로 두되, public route mesh channel 과는 registry 를
분리한다**는 결정을 먼저 내린다.

구현에서 SpotNode router socket 을 재사용할지, internal route socket 을 둘지는 2차 설계에서
정한다. 사용자 계약은 두 구현 선택에 영향받지 않아야 한다.

즉 public 계약은 다음을 보장한다.

- session actor bind 는 local ActorGateway 가 필요하다.
- session relay 는 ActorGateway 를 경유한다.
- actor current location 은 ActorGateway 가 관리한다.
- application `AddRouteMeshChannel(...)`은 actor/session internal transport 가 아니다.

## 8. 상태 모델

### 8.1 actor identity

actor identity 는 다음 값으로 본다.

- actor id
- actor type

actor id 는 session 과 application 이 아는 logical key 다.
actor type 은 factory 와 handler registry 를 고르는 값이다.

### 8.2 actor instance generation

actor 가 생성될 때 concrete generation 을 가진다.
destroy 후 같은 actor id 로 다시 생성되면 새 generation 을 가져야 한다.

generation 은 stale packet 과 stale location update 를 막기 위해 필요하다.

### 8.3 current actor location

current actor location 은 내부적으로 다음 값을 가질 수 있다.

- gateway namespace 또는 mesh id
- target SpotNode rid
- actor id
- actor generation
- current spot id 또는 entry spot id
- location epoch

이 값은 public session handler 에 노출하지 않는다.

### 8.4 session binding

session binding 은 actor id 와 client stream session 의 연결이다.
framework 내부 값은 다음을 포함할 수 있다.

- actor id
- actor generation
- session id
- session owner gateway rid
- binding token
- disconnect state

session binding 은 actor location 과 다르다.
actor 는 Play SpotNode 에 있을 수 있고, session binding owner 는 Session SpotNode 에 있을 수
있다.

## 9. 메시지 흐름

### 9.1 actor bind

```text
+----------+    +---------------+    +-------------+
| Session  | -> | ActorGateway  | -> | Actor Store |
+----------+    +---------------+    +-------------+
```

1. session 이 `BindActorHandleAsync(actorId, actorType)` 를 호출한다.
2. runtime 이 local ActorGateway 를 찾는다.
3. ActorGateway 가 actor slot 을 만들거나 기존 slot 을 찾는다.
4. ActorGateway 가 session binding 을 기록한다.
5. session 은 logical actor ref 를 받는다.

### 9.2 remote EntrySpot join

```text
+---------------+    +----------------+    +-------------+
| Local Gateway | -> | Remote Gateway  | -> | Entry Spot  |
+---------------+    +----------------+    +-------------+
```

1. actor ref 또는 actor context 가 EntrySpot join 을 요청한다.
2. local ActorGateway 가 target EntrySpot remote address 를 찾는다.
3. remote ActorGateway 로 join command 를 보낸다.
4. remote ActorGateway 가 EntrySpot execution context 에 actor 를 attach 한다.
5. remote ActorGateway 가 actor current location 을 publish 또는 reply 한다.
6. local ActorGateway 가 actor location 을 갱신한다.

### 9.3 session packet relay

```text
+----------+    +---------------+    +----------------+    +-------+
| Session  | -> | Local Gateway | -> | Remote Gateway | -> | Actor |
+----------+    +---------------+    +----------------+    +-------+
```

1. session 이 `RelayToActorAsync(actorRef, header, payload)` 를 호출한다.
2. local ActorGateway 가 actorRef 의 actor id 와 generation 을 확인한다.
3. location 이 local 이면 local mailbox 로 dispatch 한다.
4. location 이 remote 이면 remote ActorGateway 로 internal actor packet 을 보낸다.
5. remote ActorGateway 가 actor mailbox 또는 current Spot execution context 로 dispatch 한다.

### 9.4 SessionProxy push

```text
+-------+    +----------------+    +---------------+    +--------+
| Actor | -> | Remote Gateway | -> | Owner Gateway | -> | Stream |
+-------+    +----------------+    +---------------+    +--------+
```

1. actor 가 `SessionProxy.Send(...)` 를 호출한다.
2. remote ActorGateway 가 actor id 로 session binding owner 를 찾는다.
3. owner gateway 가 local 이면 stream 에 쓴다.
4. owner gateway 가 remote 이면 internal session proxy packet 을 보낸다.
5. session owner runtime 이 현재 stream binding token 을 확인하고 client 로 보낸다.

## 10. 공개 API 후보

### 10.1 SpotNode builder

```csharp
public interface IZLinkSpotNodeBuilder
{
    void EnableActorGateway(
        Action<IZLinkActorGatewayBuilder>? configure = null);
}

public interface IZLinkActorGatewayBuilder
{
    void ConfigureRouting(Action<IZLinkRoutingBuilder> configure);
}
```

`EnableActorGateway(...)`는 SpotNode 가 actor relay endpoint 로 동작하도록 켠다.
이 호출은 application handler 를 노출하지 않는다.

### 10.2 session context

```csharp
public interface IZLinkSessionActorDispatchContext
{
    ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask RelayToActorAsync(
        IZLinkActorRef actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default);
}
```

remote address 를 직접 받는 overload 는 제거 후보가 된다. 다만 migration 단계에서는
obsolete 로 남길 수 있다.

### 10.3 actor ref

```csharp
public interface IZLinkActorRef
{
    string ActorId { get; }

    string ActorType { get; }

    bool IsRemote { get; }
}
```

`IsRemote` 는 현재 actor location 이 local gateway 밖인지 알려 주는 값으로 남길 수 있다.
하지만 route address 를 public 으로 노출하지 않는 것이 원칙이다.

### 10.4 actor join

actor join 표면은 별도 결정이 필요하다.

후보 1: actor ref 에 join 기능을 둔다.

```csharp
ValueTask<TReply> JoinEntrySpotAsync<TRequest, TReply>(
    string entrySpotName,
    TRequest request,
    CancellationToken cancellationToken = default);
```

후보 2: actor context 에서만 join 할 수 있게 한다.

```csharp
ValueTask<TReply> JoinEntrySpotAsync<TRequest, TReply>(
    string entrySpotName,
    TRequest request,
    CancellationToken cancellationToken = default);
```

session 에서 actor 생성 직후 remote EntrySpot 으로 join 시키려면 후보 1 이 편하다.
하지만 actor ref 가 너무 많은 동작을 갖게 될 수 있다. 첫 설계에서는 `IZLinkActorManager`
또는 `IZLinkActorGatewayClient` 같은 service 표면도 함께 검토해야 한다.

## 11. 검증 규칙

### 11.1 startup / runtime validation

다음 조건은 오류여야 한다.

- session actor bind 를 호출했는데 local ActorGateway 가 없다.
- ActorGateway 를 켰는데 SpotNode routing id 가 없다.
- ActorGateway discovery 가 필요한데 discovery endpoint 가 없다.
- actor location update 의 generation 이 현재 generation 과 맞지 않는다.
- SessionProxy packet 의 binding token 이 현재 binding 과 맞지 않는다.

### 11.2 regression test

필수 테스트는 다음과 같다.

| 테스트 | 검증 내용 |
|--------|-----------|
| session bind without gateway | ActorGateway 없이 `BindActorHandleAsync(...)` 호출 시 설정 오류 |
| session bind creates local actor | session bind 가 local ActorGateway 에 actor slot 을 만든다 |
| entry spot join moves actor | actor 가 remote EntrySpot 에 join 하면 current location 이 remote 로 바뀐다 |
| relay after join reaches remote actor | session relay 가 추가 작업 없이 remote actor handler 에 도착한다 |
| session proxy after join reaches client | remote actor 의 `SessionProxy.Send(...)`가 original session stream 으로 도착한다 |
| stale generation relay rejected | 오래된 actor generation 으로 들어온 relay 가 새 actor 로 전달되지 않는다 |
| stale binding proxy rejected | 닫힌 session binding token 으로 들어온 proxy packet 이 client 로 전달되지 않는다 |
| public route channel isolation | application `AddRouteMeshChannel(...)` handler 와 ActorGateway internal packet 이 섞이지 않는다 |
| spot ingress isolation | Spot packet 과 ActorGateway packet 이 서로 다른 dispatch 경로를 탄다 |

### 11.3 sample validation

Bingo 와 TicTacToe session gateway 샘플은 다음 상태가 되어야 한다.

- Session server 에서 `AddRouteMeshChannel(...)` 제거
- Session server 에 ActorGateway 를 가진 SpotNode 추가
- Play server 의 SpotNode 에 ActorGateway 추가
- session handler 에서 remote address snapshot 을 직접 받는 코드 제거
- actor 생성, EntrySpot join, relay, SessionProxy push 가 동작

## 12. 문서 반영 계획

구현 전에는 이 문서만 유지한다. 구현이 끝난 뒤 다음 문서에 나누어 반영한다.

| 문서 | 반영 내용 |
|------|-----------|
| `doc/spec/aspnet-core-stream.ko.md` | session actor bind 가 local ActorGateway 를 요구한다는 계약 |
| `doc/spec/aspnet-core-actor.ko.md` | actor location, gateway relay, SessionProxy 의미 |
| `doc/spec/aspnet-core-spot.ko.md` | SpotNode 의 ActorGateway capability |
| `doc/spec/spot-node.ko.md` | SpotNode router 와 ActorGateway dispatch 분리 |
| `doc/spec/handler-interfaces.ko.md` | `IZLinkActorRef`, session actor dispatch, gateway builder public surface |
| `doc/guide/06-actor-session.ko.md` | session 에서 actor bind 후 remote EntrySpot join 하는 사용 흐름 |
| `doc/guide/07-stream.ko.md` | stream session 이 route mesh 를 직접 설정하지 않는다는 설명 |
| `doc/guide/samples/bingo-game-sample.ko.md` | Bingo 샘플 설정과 인증/매칭 흐름 갱신 |
| `doc/guide/samples/tictactoe-game-sample.ko.md` | TicTacToe session gateway 구조 갱신 |
| `doc/internals/behavior-matrix.ko.md` | ActorGateway 상태 전이와 failure behavior |
| `doc/internals/regression-test-matrix.ko.md` | 위 regression test 항목 추가 |
| `doc/internals/di-capability-exposure-policy.ko.md` | ActorGateway 관련 service 노출 정책 |

공통 문서에는 다음을 반영한다.

| 문서 | 반영 내용 |
|------|-----------|
| `doc/spec/` 공통 actor/spot 관련 문서 | actor 가 SpotNode gateway 를 통해 이동하고 relay 된다는 공통 의미 |
| `doc/guide/` 공통 guide | route mesh channel 과 actor/session gateway transport 의 구분 |

## 13. C API 레벨 검토

이 개념은 `.NET` framework 안에서만 닫을 수 없다. core C API 는 이미 actor 와 STREAM
session binding 을 다룬다. 따라서 ActorGateway 모델은 최소한 C API 의 의미와 core spec 에
반영되어야 한다.

현재 core 표면에는 다음 함수가 있다.

```c
zlink_config_result_t zlink_spot_node_actor_new(
  void *node,
  const char *actor_id,
  zlink_actor_ref_t *actor_out);

zlink_submit_result_t zlink_spot_node_actor_join_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_actor_join_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_stream_bind_actor(
  void *stream,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  zlink_reply_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_stream_send_bound_actor_part(
  void *stream,
  const zlink_routing_id_t *session_rid,
  const char *actor_id,
  zlink_msg_t *part,
  zlink_send_flags_t flags,
  zlink_part_flag_t part_flag);

zlink_submit_result_t zlink_spot_node_actor_send_bound_session_msg(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_msg_t *message,
  zlink_send_flags_t flags);
```

이 함수들은 이미 ActorGateway 에 가까운 역할을 갖고 있다.

- `zlink_stream_bind_actor(...)` 는 stream session 과 actor 를 연결한다.
- `zlink_stream_send_bound_actor_part(...)` 는 session 에서 bound actor 로 relay 한다.
- `zlink_spot_node_actor_send_bound_session_msg(...)` 는 actor 에서 bound session 으로 되돌려 보낸다.
- `zlink_spot_node_actor_join_spot(...)` 는 actor 의 current Spot 위치를 바꾼다.

따라서 C API 에 완전히 새 "gateway socket" 함수군을 바로 추가하는 것보다, 먼저 기존
actor/session C API 의 의미를 ActorGateway 모델로 재정의하는 편이 맞다.

### 13.1 현재 C API 계약과 충돌하는 지점

현재 core spec 은 remote join 뒤 session mapping 이 자동으로 target actor 로 redirect 되지
않는다고 설명한다. application 이 join completion 에서 받은 final actor ref 로 다시 attach 해야
한다고 되어 있다.

ActorGateway 모델에서는 이 동작이 맞지 않는다. session 은 logical actor handle 을 들고 있고,
actor 가 다른 SpotNode 로 이동한 뒤에도 추가 작업 없이 같은 handle 로 relay 해야 한다.

따라서 C API 계약은 다음처럼 바뀌어야 한다.

1. `zlink_stream_bind_actor(...)` 는 actor id 에 대한 logical session binding 을 만든다.
2. binding 내부에는 actor generation 과 current actor owner 를 저장하지만, relay caller 가
   그 route 를 직접 관리하지 않는다.
3. `zlink_spot_node_actor_join_spot(...)` 이 성공하면, 해당 actor 에 bound session 이 있으면
   ActorGateway state 도 final actor ref 로 갱신한다.
4. `zlink_stream_send_bound_actor_part(...)` 는 bind 시점의 오래된 actor ref 로 고정하지 않고,
   gateway 가 가진 current actor location 으로 전달한다.
5. stale generation 으로 들어온 relay 는 새 actor 로 전달하지 않는다.

즉 바뀌는 핵심은 "session mapping 은 actor ref snapshot 을 저장한다"에서
"session mapping 은 actor id/generation 과 gateway current location 을 따라간다"로
옮기는 것이다.

### 13.2 C API 에서 필요한 capability 표현

session bind 가 SpotNode 없이 가능하면 안 된다. core spec 에 이미 `stream` 은 session owner
`SpotNode` 와 연결되어 있어야 한다는 의미가 있다. 이 조건을 더 명확히 해야 한다.

필요한 계약은 다음과 같다.

- STREAM session actor binding 을 쓰려면 stream 은 session owner SpotNode 를 가져야 한다.
- 그 SpotNode 는 actor gateway capability 를 가져야 한다.
- local actor 와 remote actor 모두 같은 gateway 경로를 탄다.
- application route mesh socket 은 이 gateway capability 와 같은 것이 아니다.

C API 이름은 반드시 `ActorGateway` 를 public type 으로 노출할 필요는 없다. core 레벨에서는
"SpotNode actor/session relay capability" 로 표현해도 된다. 다만 spec 에서는 이 capability 를
ActorGateway 라고 설명해도 무방하다.

후보 API 는 두 가지다.

후보 A: SpotNode options 에 capability flag 를 추가한다.

```c
typedef struct zlink_spot_node_options_t
{
    zlink_spot_node_mode_t mode;
    uint32_t actor_gateway_enabled;
} zlink_spot_node_options_t;
```

후보 B: 명시 함수로 켠다.

```c
zlink_config_result_t zlink_spot_node_enable_actor_gateway(
  void *node);
```

core 는 보통 node 생성 시 mode/options 로 기능을 정하는 쪽이 더 안정적이다. 다만 기존 ABI
호환을 고려하면 구조체 확장은 조심해야 한다. 호환성을 유지해야 하면 별도 enable 함수가 낫고,
호환성이 필요 없으면 options 확장이 더 단순하다.

### 13.3 stream 과 SpotNode 연결

현재 API 는 `zlink_stream_bind_actor(stream, session_rid, actor, ...)` 형태다. 인자에
SpotNode 가 없다. spec 은 stream 이 session owner SpotNode 와 연관되어 있어야 한다고 설명한다.

ActorGateway 모델에서는 이 연결이 더 중요해진다.

검토할 선택지는 다음과 같다.

1. 기존처럼 stream 내부에 owner SpotNode 를 보관한다.
2. bind/relay 함수에 owner SpotNode 를 명시 인자로 추가한다.
3. stream node 생성 또는 attach 시점에 owner SpotNode 를 명시적으로 연결한다.

2번은 API가 명확하지만 기존 stream relay 함수 시그니처를 크게 바꾼다. 1번과 3번은 기존
함수 형태를 유지하면서 "stream 은 gateway owner 를 가져야 한다"는 계약을 강화한다.

초안은 3번을 선호한다.

```c
zlink_config_result_t zlink_stream_attach_actor_gateway(
  void *stream,
  void *spot_node);
```

이 함수 이름은 예시다. 실제 C API 에서는 STREAM node 와 raw STREAM socket 의 구분을 먼저 보고
정해야 한다.

### 13.4 ActorGateway 상태 갱신

remote join 성공 시 core 는 다음 상태를 갱신해야 한다.

- actor active route
- actor current spot rid
- actor current node rid
- actor generation
- bound session mapping 의 current actor owner

현재 spec 은 active route update 와 session mapping update 를 분리한다. ActorGateway 모델에서는
분리는 유지하되, join success 가 gateway location 을 갱신해야 한다.

중요한 조건은 다음과 같다.

- join completion 의 final actor ref 가 authoritative 값이다.
- session binding 이 같은 actor id 와 expected generation 을 보고 있으면 final actor ref 로
  갱신한다.
- session binding 이 이미 다른 generation 을 보고 있으면 stale join completion 으로 보고 무시한다.
- actor id 가 같아도 generation 이 다르면 다른 actor slot 으로 취급한다.

### 13.5 relay 함수 의미 변경

`zlink_stream_send_bound_actor_part(...)` 는 ActorGateway 모델에서 다음 의미를 가져야 한다.

```text
stream + session_rid + actor_id
  -> session owner ActorGateway
  -> bound actor generation 확인
  -> current actor location 확인
  -> local actor mailbox 또는 remote ActorGateway 로 전달
```

즉 caller 는 actor location 을 모른다.

`zlink_spot_node_actor_send_bound_session_msg(...)` 는 다음 의미를 가져야 한다.

```text
request owner SpotNode
  -> actor owner/current ActorGateway
  -> session binding owner 확인
  -> stream owner gateway 로 전달
  -> bound client stream 으로 write
```

즉 actor 쪽도 session owner route 를 application 이 알 필요가 없다.

### 13.6 C API 에 추가하지 말아야 할 것

다음 API 는 피하는 것이 좋다.

```c
zlink_router_send_actor(...);
zlink_router_request_actor(...);
zlink_actor_gateway_send(...);
```

이런 함수는 application 이 actor transport 를 직접 선택하게 만들 가능성이 크다.
기존 route-resolution draft 에서 direct actor transport 를 피하고 Spot routed API 를
재사용하기로 했던 이유와 같은 문제다.

ActorGateway 는 public socket family 가 아니라 SpotNode actor/session capability 로 보는 것이
맞다.

### 13.7 core 문서와 바인딩 반영 범위

이 설계가 구현되면 다음 문서도 함께 바뀌어야 한다.

| 문서 | 변경 내용 |
|------|-----------|
| `doc/spec/core/service/spot.md` | STREAM session binding, actor join, actor-to-session send 의미를 ActorGateway 모델로 수정 |
| `doc/spec/core/errno-map.md` | ActorGateway 없음, stale generation, gateway route not found 오류 분류 추가 또는 기존 오류 재사용 명시 |
| `doc/spec/bindings/README.md` | binding 공통 계약에서 actor/session relay 가 SpotNode gateway capability 를 요구한다고 설명 |
| `doc/spec/bindings/c/README.md` | C binding public 함수와 ownership 규칙 갱신 |
| `doc/spec/bindings/dotnet/README.md` | `.NET` binding/framework 가 core ActorGateway 계약을 감싼다는 점 반영 |

바인딩은 core C API 를 기준으로 다시 감싸야 한다. 특히 `.NET` framework 가 자체 route cache 로
ActorGateway 의미를 흉내 내면 안 된다. session relay 와 SessionProxy 의 최종 일관성은 core
ActorGateway 상태가 소유해야 한다.

## 14. 적용 계획

### 14.1 1단계: 문서와 테스트 골격

1. 이 draft 를 확정한다.
2. 현재 session-attached actor route 문서와 겹치는 결정을 표시한다.
3. contract test 에 ActorGateway public surface 후보를 추가한다.
4. E2E test 이름을 ActorGateway 모델 기준으로 먼저 추가하고 skip 없이 실패하게 만든다.

### 14.2 2단계: runtime 내부 모델 추가

1. SpotNode registration 에 ActorGateway capability 를 추가한다.
2. runtime state 에 public route channel 과 분리된 ActorGateway transport registry 를 둔다.
3. actor current location store 를 ActorGateway 아래로 이동한다.
4. session binding store 가 owner gateway rid 를 갖도록 정리한다.

### 14.3 3단계: session bind 변경

1. `BindActorHandleAsync(actorId, actorType)` 이 local ActorGateway 를 요구하게 한다.
2. remote address 를 받는 overload 를 제거하거나 obsolete 처리한다.
3. session relay 가 actor ref route snapshot 대신 gateway location lookup 을 사용하게 한다.
4. 기존 local-only actor bind 테스트를 gateway 모델로 고친다.

### 14.4 4단계: EntrySpot join 변경

1. actor 를 remote EntrySpot 으로 join 시키는 public 표면을 결정한다.
2. EntrySpot join 이 actor current location 을 remote SpotNode 로 갱신하게 한다.
3. join 후 session relay 가 remote actor 로 도착하는 E2E 를 통과시킨다.

### 14.5 5단계: SessionProxy 변경

1. `SessionProxy` packet 이 ActorGateway 를 통해 owner session gateway 로 전달되게 한다.
2. stale binding token guard 를 유지한다.
3. local actor, remote actor, moved actor 에서 client push 가 모두 동작하는지 검증한다.

### 14.6 6단계: sample 정리

1. Bingo session host 에서 `AddRouteMeshChannel(...)` 을 제거한다.
2. Bingo session/play host 에 ActorGateway SpotNode 설정을 추가한다.
3. TicTacToe session gateway 도 같은 구조로 맞춘다.
4. 샘플에서 remote address snapshot DTO 를 제거한다.
5. 샘플 문서에서 route mesh 설정 설명을 ActorGateway 설명으로 바꾼다.

### 14.7 7단계: 정식 문서 승격

1. draft 내용을 spec/guide/internals 로 나누어 반영한다.
2. 구현과 맞지 않는 이전 draft 내용을 폐기 또는 superseded 로 표시한다.
3. README 링크와 샘플 문서 링크를 갱신한다.
4. full solution build, unit, contract, E2E 를 통과시킨다.

## 15. 열린 결정 사항

### 15.1 ActorGateway transport socket

SpotNode router 를 재사용할지, 별도 internal route socket 을 둘지 결정해야 한다.
public API 는 이 선택을 숨겨야 한다.

### 15.2 ActorGateway routing id

SpotNode routing id 와 ActorGateway routing id 를 같은 값으로 볼지, gateway 전용 rid 를 둘지
정해야 한다.

같은 값을 쓰면 단순하다. 별도 값을 쓰면 monitoring 과 isolation 은 좋아지지만 설정이 늘어난다.

### 15.3 actor join public surface

session 이 actor 를 생성한 직후 EntrySpot 으로 join 시킬 수 있어야 한다.
이 기능을 actor ref 에 둘지, actor manager/gateway client service 에 둘지 정해야 한다.

### 15.4 registry 와 discovery 역할

ActorGateway 는 remote EntrySpot 과 actor location 을 찾아야 한다.
registry backed resolver 를 그대로 쓸지, ActorGateway 전용 discovery metadata 를 둘지 정해야 한다.

### 15.5 기존 `ZLinkActorRemoteAddress`

이 타입을 public 으로 유지할지, backend service 용으로만 축소할지 결정해야 한다.
session bind 에서는 제거하는 것이 이 초안의 방향이다.

### 15.6 기존 draft 와의 관계

`session-attached-actor-route.ko.md` 는 session 이 remote address snapshot 을 들고 hot path 에서
resolver 를 쓰지 않는 방향을 정리했다. ActorGateway 모델은 그보다 한 단계 더 나아가,
remote address snapshot 소유권을 session 에서 ActorGateway 로 옮긴다.

구현 시점에는 기존 draft 를 다음 중 하나로 처리해야 한다.

1. ActorGateway draft 가 supersede 한다고 표시한다.
2. remote address generation/stale update 부분만 ActorGateway internals 로 흡수한다.
3. public session attach overload 관련 내용은 폐기한다.

## 16. 결론

remote actor 와 session relay 는 일반 route mesh channel 을 application 이 직접 등록해서
쓰는 기능으로 두면 개념이 계속 흔들린다.

이 초안은 actor/session relay 를 SpotNode 의 framework owned `ActorGateway` 로 끌어내린다.
session 은 actor 를 bind 하고 packet 을 relay 할 뿐이며, actor 가 local 인지 remote 인지,
어느 route mesh socket 을 써야 하는지는 모른다.

물리적으로 ROUTER mesh transport 를 쓰더라도 public 모델은 다음처럼 유지되어야 한다.

- session 은 local ActorGateway 가 있어야 actor bind 를 할 수 있다.
- ActorGateway 는 actor 생성, EntrySpot join, current location, session binding 을 관리한다.
- route mesh channel 은 application routed messaging 용도다.
- SpotNode router 와 ActorGateway internal packet 은 dispatch 경계를 분명히 나눈다.

[^public-contract]: 공개 계약은 구현, 테스트, 정식 spec 문서가 함께 맞춰진 API 를 뜻한다.
