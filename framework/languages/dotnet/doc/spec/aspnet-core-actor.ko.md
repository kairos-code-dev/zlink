<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework ASP.NET Core STREAM Integration](./aspnet-core-stream.ko.md) | [다음: ZLink Framework .NET Session Actor Dispatch](./session-actor-dispatch.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[.NET 묶음](../README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [Registry](./aspnet-core-registry.ko.md)

# ZLink Framework ASP.NET Core Actor

## 1. 목적

이 절은 framework 가 actor 개념을 어떤 방향으로 노출하려고 하는지, 그 목표와
범위를 한 자리에 정리한다.

이 문서의 목표는 zlink core 가 정의한 **actor**[^actor] 개념을 `ASP.NET Core`
애플리케이션 위에서 `.NET` 다운 모양으로 노출하는 것이다.

zlink core 모델에서 actor 는 다음 성질을 가진다. 더 자세한 정의는 zlink core 의
[SPOT Actor Guide](../../../../../doc/guide/07-4-actor.md) 에서 다룬다.

- **`SpotNode`에 소속된다.** actor는 생성 직후 그 node의
  `Entry Spot`[^entryspot]에 위치한다.
- 선택적으로 **STREAM session에 binding**[^session-bind]할 수 있다. 한 session
  에는 여러 actor가 bind될 수 있지만, 하나의 actor는 한 번에 최대 한 session
  에만 bind된다. session binding은 client relay 경로일 뿐이고, actor가 실제로
  어디에 위치하는지를 결정하지는 않는다.
- 선택적으로 **user Spot에 join**[^user-spot-join]할 수 있다. 이 경우 Entry
  Spot에서 user Spot으로 이동한다. user Spot join은 STREAM session binding
  없이도 수행할 수 있다.
- user Spot에서 다시 Entry Spot으로 돌아오려면 **leave**, actor 자체를 완전히
  제거하려면 **destroy**를 수행한다. destroy는 actor가 Entry Spot에 있을 때만
  허용된다.

framework 는 위 모델 위에 `.NET` 다운 모양을 한 겹 더 얹어서 사용자에게
노출한다. 그 한 겹은 두 종류의 요소로 이루어진다.

- lifecycle 요소 -- constructor injection, DI
  scope[^di-scope]
- fluent 호출 표면 -- `IZLinkActorContext`, `IZLinkBoundSession`

책임은 두 갈래로 나눠서 본다.

- **framework가 자동으로 관리하는 영역**: Entry Spot 자체의 생성과 소멸, user
  Spot에서 Entry Spot으로 돌아오는 leave, actor destroy가 여기에 해당한다.
  즉 application 코드는 `zlink_spot_node_entry_spot()` /
  `zlink_spot_node_actor_leave_spot()` / `zlink_spot_node_actor_destroy()`
  같은 raw API[^raw-api]를 직접 호출하지 않는다.
- **application이 구현하는 Entry Spot 로직**: actor가 Entry Spot에 머무는 동안
  받는 packet의 handler는 application이 정한다. 이 handler는 actor 클래스에
  붙이는 것이 아니라 Entry Spot 전용 registry에 등록한다. 예를 들어 인증
  결과를 보고 target user Spot을 골라 `JoinSpot(...)`을 호출하는 entry 단계
  로직이 여기에 들어간다. Entry Spot의 `on_join` / `on_leave` lifecycle
  callback handler 역시 일반 user Spot과 별도로 등록할 수 있어야 한다.

호출하는 쪽에서는 actor 가 어느 노드의 어느 spot 에 있는지 알 필요가 없다.
**`actorId`** 하나만 들고 부르면 된다. 실제 라우팅은 framework 가
application 이 등록한 resolver[^resolver] 에 위임한다.

이 문서가 다루는 범위는 다음과 같다.

- actor lifecycle (Entry Spot 머무름 → session bind → user Spot join → 명시적 disconnect notification)
- handler 모델 (typed actor handler / 일반 actor handler)
- Entry Spot에서의 application 로직 (인증, target Spot 선택)
- STREAM session binding
- user Spot join (framework가 노출하는 표면)
- session actor dispatch[^session-actor-dispatch] (gateway[^gateway]) 패턴
- outbound `IZLinkBoundSession` 표면
- 등록 API 정리

다음 항목은 이 문서가 다루지 않으므로 별도 문서에서 본다.

- channel messaging[^channel-messaging] 자체는
  [aspnet-core-channel-messaging.ko.md](./aspnet-core-channel-messaging.ko.md)
  에서 다룬다.
- SPOT 자체는 [aspnet-core-spot.ko.md](./aspnet-core-spot.ko.md)에서 다룬다.
- STREAM session 자체는 [aspnet-core-stream.ko.md](./aspnet-core-stream.ko.md)
  에서 다룬다.
- C API의 raw actor 표면(`zlink_spot_node_actor_recv_part`,
  `zlink_spot_node_actor_send_bound_session_msg` 등)은 core 가이드에서 다룬다.

## 2. Actor 개념

이 절은 framework 의 actor 가 core 의 actor 모델을 어떻게 그대로 가져오는지,
그리고 그것이 일반 handler 클래스와 어떻게 다른지를 비교해서 정리한다.

framework 의 **actor** 는 zlink core 가 정의한 actor 모델을 그대로 따른다. 즉
ID 로 식별되는 stateful object 이며, SpotNode 에 소속되고, 선택적으로 session
에 bind 할 수 있다. 그 위에 framework 가 application 코드에서 쓰기 편한
`.NET` 표면을 한 겹 더 얹는다.

일반 handler 클래스와 비교해 보면 actor 의 특징은 다음과 같다.

| 일반 handler | actor |
| --- | --- |
| 기본적으로 stateless | **stateful** -- 객체 자체가 상태를 보관한다 |
| 메시지마다 새 scope에서 resolve | 같은 actor id로 들어오는 메시지는 항상 **같은 인스턴스**가 받는다 |
| packet 등록은 채널 매핑이 담당 | actor packet 등록은 Entry Spot 또는 user Spot registry가 담당한다 |
| identity 없음 | `ActorId`가 1급 identity (core의 `zlink_actor_ref_t`에 대응) |
| 메시지 한 건짜리 lifecycle | `Configure` → 여러 메시지 처리. disconnect notification 은 Spot actor handler 로 처리 |

### 두 가지 직교 축

actor 의 상태는 서로 독립된 두 축으로 본다. 즉 위치 축과 binding 축은 서로
영향을 주지 않는다.

| 축 | 값 |
| --- | --- |
| **위치** (어느 Spot에 있는가) | Entry Spot (생성 직후의 default) ↔ user Spot (join 이후) |
| **binding** (어느 client에 묶였는가) | unbound ↔ bound to STREAM session |

application 입장에서 자주 마주치는 조합은 두 가지다.

- **standalone actor** -- session bind 없이, 다른 노드에서 routed[^routed]
  호출만 받는 형태다. Entry Spot 에 그대로 머문다. 단순 background worker 나
  scheduler 같은 패턴이 여기에 해당한다.
- **session-bound actor** -- 인증된 client stream 에 묶여 있는 형태다. 보통
  user Spot 에 join 해서 game room 이나 stage 같은 도메인 객체로 동작한다.
  대부분의 `ASP.NET Core` gateway 시나리오에서 기본형으로 쓰는 모양이다.

actor 인스턴스는 session bind 과정에서 임의로 만들어지지 않는다. application 이
actor 가 필요하다고 판단한 시점에 `IZLinkActorManager` 를 호출하고, framework 는
등록된 **factory**[^factory] 를 통해 actor 를 만든다.

## 3. Actor 라이프사이클

이 절은 actor 가 어떤 인터페이스를 구현하고, 어떻게 만들어지며, 어떤 단계로
이어지는지를 차례대로 정리한다.

### 3.1 `IZLinkActor`

모든 actor 클래스가 구현해야 하는 기본 인터페이스다.

```csharp
public interface IZLinkActor
{
    string ActorId { get; }

    IZLinkActorContext Context { get; }

    void Configure()
    {
    }

}
```

각 멤버의 의미는 다음과 같다.

- **`ActorId`** -- actor 를 식별하는 고유 ID 다. 보통 인증 단계에서 결정되어,
  factory 의 생성자 인자로 전달된다.
- **`Context`** -- framework 가 actor 생성 시점에 factory 로 넘기는 값이다.
  actor 가 메시지 dispatch 나 outbound 호출을 하려면 이 context 를 거친다.
  application 코드는 factory 에서 받은 context 를 actor 생성자에 넘기고,
  actor 는 get-only property 로 노출한다.
- **`Configure()`** -- actor 생성 뒤 한 번 호출되는 초기화 지점이다.
  actor packet handler 는 여기서 등록하지 않는다. Entry Spot 또는 user Spot 의
  `Configure()` 에서 `AddActorPacket(...)` 으로 등록한다.

actor 자체에는 disconnect callback 을 두지 않는다. actor 는 Entry Spot 또는
user Spot 문맥 안에서 동작하므로, session 끊김을 actor 에 알려야 하는 경우에도
application 이 session callback 에서 대상 actor 를 고른 뒤
`IZLinkSessionActor.NotifyDisconnectedAsync(...)` 를 호출한다.
framework 는 그 actor 의 현재 Spot 실행 문맥에서 별도 actor disconnected handler 를
호출하며, actor 를 room 에서 자동으로 leave 시키지 않는다.

### 3.2 `IZLinkActorFactory`

application 은 `IZLinkActorManager` 로 actor 생성을 명시한다. `CreateAsync(...)`
는 이미 같은 actor id가 있으면 `ActorAlreadyExists` 를 던진다. 같은 actor id를
다른 actor type으로 다시 사용하면 `ActorTypeMismatch` 를 던진다.
`GetOrCreateAsync(...)` 는 같은 actor type의 기존 actor 를 재사용하고, 없으면
factory 로 새 actor 를 만든다.

```csharp
public interface IZLinkActorManager
{
    ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActor?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActor> GetOrCreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);
}
```

actor 인스턴스를 만들어 내는 application 객체다. 사용 흐름은 두 단계다. 먼저
DI 에 등록한다. 그 다음
`options.AddActorFactory<...>(actorType)` 로 framework 에 매핑한다.

```csharp
public interface IZLinkActorFactory
{
    ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default);
}
```

`actorType` 은 사용자가 직접 정의하는 짧은 문자열 키다 (예: `"player"`). 한
앱에서 여러 종류의 actor 를 등록할 수 있고, 종류마다 별도 factory 를 둔다.
session actor dispatch 처럼 actor 종류를 메시지로 받아 처리하는 경우, framework
는 이 actorType 키로 어떤 factory 를 부를지 결정한다.

```csharp
public sealed class PlayerActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(
            new PlayerActor(actorId, context));
    }
}
```

등록 코드는 다음과 같다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddActorFactory<PlayerActorFactory>("player");
});
```

### 3.3 라이프사이클 단계

이 소절은 core actor 가 어떤 상태들을 거쳐 가는지, 그리고 그 전이를 framework
가 어느 시점에 손대는지 정리한다.

core actor 의 상태 전이는 다음과 같이 이어진다.

```text
None
  +--(factory.CreateAsync)-> Created (Entry Spot, unbound)
        +--(bind session)-> Entry Spot + bound to session
        |     +--(JoinSpot)-> user Spot + bound to session
        |           +--(leave: framework)-> Entry Spot + bound
        +--(disconnect / unbind: framework)-> destroy -> None
```

framework가 처리하는 시퀀스는 다음과 같다.

```mermaid
sequenceDiagram
    autonumber
    participant App as Application
    participant FW as Framework Runtime
    participant Fact as IZLinkActorFactory
    participant Act as IZLinkActor
    participant Loc as Location Writer
    participant Spot as User Spot (선택)

    Note over FW: 1. 생성
    App->>FW: IZLinkActorManager.GetOrCreateAsync(actorId, "player")
    FW->>Fact: CreateAsync(actorId, context)
    Fact-->>FW: actor instance
    FW->>Act: Context property 검증
    FW->>Act: Configure()
    Note over FW: handler는 Entry/User Spot registry가 등록

    Note over FW: 2. session bind (선택)
    FW->>FW: STREAM session에 actor bind
    FW->>Loc: BindSessionAsync(actorId, sessionRid, token)
    Note over FW: bind는 session relay만 연결

    Note over FW: 3. user Spot join (선택, bind와 독립)
    Act->>FW: Context.JoinSpot(spotRid, request).SubmitAsync<TReply>()
    FW->>Spot: actor join 요청
    Spot-->>FW: accept + reply
    FW-->>Act: reply 반환

    Note over FW,Act: 이후 메시지 들어오면 dispatch
    FW->>Act: HandleAsync(actor, message, ct)
    Act-->>FW: result

    Note over FW: 4. session cleanup
    FW->>Loc: UnbindSessionAsync(actorId, token) (bound인 경우)
    Note over FW: actor 위치와 membership은 유지
```

core 모델에서 비롯된 핵심 제약은 다음과 같다.

- **user Spot join 은 bound session 을 요구하지 않는다.** actor 의 위치 이동과
  STREAM session binding 은 서로 독립된 상태 전이로 본다.
- **destroy 는 actor 가 Entry Spot 에 있을 때만 가능하다.** session disconnect 는
  destroy 나 user Spot leave 를 자동으로 만들지 않는다.
- **discovery[^discovery] actor remote address publish 는 user Spot join 성공 뒤에
  갱신된다.** actor 를 생성하기만 해서는 active route 가 공개되지 않는다.
  session bind / unbind 도 active route 를 새로 만들거나 지우지 않는다.

### 3.4 Entry Spot에서의 application 로직

이 소절은 Entry Spot 에서 어떤 로직이 흔히 실행되는지, 그리고 그 로직의 packet
실행 순서가 user Spot 과 어떻게 다른지를 정리한다.

actor 가 Entry Spot 에 머무는 동안 받는 packet 은 Entry Spot 전용 handler
registry 가 처리한다. framework 는 Entry Spot 자체의 생성과 소멸은 자동으로
관리한다. 다만 Entry Spot 의 message handler 와 `on_join` / `on_leave`
lifecycle callback handler 는 application 이 따로 등록할 수 있어야 한다.

Entry 단계와 user Spot 단계는 같은 actor 객체를 보더라도 의미가 다르다. 그래서
동일한 handler 묶음으로 합치지 않는다.

이 단계에서 자주 구현하는 로직은 다음과 같다.

- **인증 / 권한 확인** -- 인증 packet이 도착하면 actor가 검증한 뒤 결과를
  reply하거나, 실패한 경우 fail 응답을 보내고 disconnect한다.
- **target Spot 선택** -- 클라이언트의 요청 packet에서 어느 game room이나
  stage로 들어갈지 결정한 뒤 해당 user Spot 의 `RoutingId`를 얻고
  `Context.JoinSpot(spotRid, request).SubmitAsync<TReply>(...)`을 호출한다.
  `gameId`, `matchId`, `roomId` 같은 domain 값은 application 이 먼저
  `RoutingId`로 변환하거나 registry 에서 조회한다.
- **session 초기 상태 설정** -- session metadata, profile lookup 같은 초기
  작업이 여기 들어간다.

Entry Spot 의 actor packet 은 Entry Spot 전체 실행 줄에 세우지 않는다. 이렇게
둔 이유는 다음과 같다. Entry Spot 은 모든 actor 가 처음 거치는 공용 입구다.
여기서 actor packet 을 전역으로 직렬화하면, 서로 관계없는 actor 까지 같이
기다리게 된다. 따라서 Entry Spot actor packet 은 대상 actor 의
mailbox[^mailbox] 로 들어간다. 같은 actor 의 packet 끼리만 순서가 보장된다.

반면 Entry Spot 자체의 registry 나 lifecycle 상태를 다루는 작업은 Entry Spot
실행 문맥에서 직렬화해도 된다. 예를 들어 `OnInitializeAsync(...)`,
`OnClosingAsync(...)`, actor joined / left lifecycle callback 같은 작업이
여기에 해당한다.

즉 Entry Spot 은 lifecycle 을 보호하고, actor packet 의 처리 순서는 actor
mailbox 가 보호하는 구도다.

```csharp
public sealed class PlayerActor(
    string actorId,
    IZLinkActorContext context,
    IAuthService auth) : IZLinkActor
{
    public string ActorId { get; } = actorId;
    public IZLinkActorContext Context { get; } = context;
}

public sealed class PlayerEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddHandler<AuthenticateRequestHandler>();
        Context.AddHandler<JoinMatchRequestHandler>();
        Context.AddHandler<PlayerEntryJoinedHandler>();
        Context.AddHandler<PlayerEntryLeftHandler>();
    }
}

public sealed class MatchSpot(IZLinkSpotContext context) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddHandler<PlaceMarkRequestHandler>();
        Context.AddHandler<PlayerMatchJoinedHandler>();
        Context.AddHandler<PlayerMatchLeftHandler>();
    }
}
```

즉 Entry Spot 전용 handler 등록 표면을 별도로 둔다. actor 객체는 상태를
보관하는 자리고, message 와 lifecycle callback 은 현재 actor 가 어느 실행
문맥에 있는지에 따라 Entry Spot registry 또는 user Spot registry 가 처리한다.

이 표면의 목적은 application handler 가 `Context.IsJoined` 같은 상태값으로
entry / user 단계를 직접 분기하지 않도록 막는 것이다. `RoutingId` 같은
transport 위치값도 handler 표면에 노출하지 않는다.

## 4. Handler 모델

이 절은 actor 의 packet handler 를 어디에 어떻게 등록하는지, 그리고 Entry Spot
과 user Spot 의 등록 표면을 어떻게 나누는지 정리한다.

actor 가 처리할 packet handler 는 **현재 실행 문맥의 registry 에 등록한다.**
즉 Entry Spot 은 Entry Spot 전용 registry 를 갖고, user Spot 은 각 Spot
타입마다 별도의 registry 를 갖는다.

일반 channel handler 처럼 attribute scan[^attribute-scan] 과 그룹 매핑으로
노출하는 모델이 아니다. 대신 각 Spot 의 `Configure()` 안에서
`Context.AddHandler<THandler>()` 로 등록한다. 이 메서드는 handler 가 구현한
actor handler interface 에서 actor 타입과 packet/lifecycle 종류를 추론한다.
handler 가 여러 actor handler interface 를 구현해서 모호한 경우에는
`Context.AddActorPacket<THandler, TActor>()` 같은 명시 등록 메서드를 사용한다.

```csharp
public sealed class PlayerActor(
    string actorId,
    IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;
}
```

이 모델의 의도는 다음과 같다.

- actor packet 묶음은 현재 실행 문맥인 Entry Spot 또는 user Spot 이 정한다.
- actor type 과 실행 문맥이 달라지면 packet 매핑도 다르게 가져갈 수 있다.
- 같은 handler 클래스를 여러 actor type 이 공유해도 된다.

### 4.1 Entry Spot actor handler

Entry Spot 에 있는 actor message 는 Entry Spot registry 에 등록한 handler 가
처리한다. 이 handler 는 Entry Spot 인스턴스, actor 인스턴스, dispatch context,
payload 를 함께 받는다. Entry Spot 에도 입장 처리 상태나 helper 메서드가 있을
수 있으므로 handler 에서 현재 Entry Spot 인스턴스에 접근할 수 있어야 한다.
`ZLinkSpotActorSendContext` / `ZLinkSpotActorRequestContext` 는 session 에서
넘어온 packet 이름과 전달 허용된 metadata 를 제공한다. 현재 actor 에 묶인
client 로 push 해야 하면 handler 가 받은 actor 의 `Context.BoundSession` 을
사용한다. request context 의 `Reply` 옵션은 handler 반환값으로 만들어지는
response frame 에 metadata 나 compression 을 적용할 때 사용한다.

```csharp
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
```

샘플은 다음과 같다.

```csharp
internal sealed class JoinMatchHandler(GameNotificationPublisher notifications)
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
        // request.MatchId는 application domain id다.
        // application registry가 user Spot RoutingId로 변환하거나 조회한다.
        var matchSpotRid = RoutingId.From(request.MatchId);
        var result = await actor.Context
            .JoinSpot(matchSpotRid, request)
            .Timeout(TimeSpan.FromSeconds(2))
            .SubmitAsync<JoinMatchSpotResult>(cancellationToken)
            .ConfigureAwait(false);

        await notifications.PublishAsync(result.Reply.Events, cancellationToken);

        if (result.ResultCode != 0)
        {
            return new JoinMatchRes(result.Reply.MatchId, result.Actor.ActorId, ...);
        }

        return new JoinMatchRes(result.Reply.MatchId, result.Actor.ActorId, ...);
    }
}
```

### 4.2 user Spot actor handler

user Spot 에 join 된 actor 의 message 는 해당 Spot 타입의 registry 에 등록한
handler 가 처리한다. 이 handler 는 spot 객체, actor 객체, dispatch context 를
함께 받는다. 즉 room 이나 stage 의 상태는 spot 에서 읽고, player 나 entity 의
상태는 actor 에서 읽는 구도다.

```csharp
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
```

Entry Spot 과 user Spot 어느 쪽이든 `AddHandler(...)` 로 lifecycle callback
handler 를 등록할 수 있다. actor 타입을 호출 쪽에서 명시해야 하면
`AddPostActorJoined(...)` / `AddActorLeft(...)` 를 사용한다. 이
callback 은 join / leave 가 commit 된 직후 같은 실행 문맥에서 호출된다.

### 4.3 등록 순서

handler 클래스는 Entry Spot 이나 user Spot 의 `Configure()` 에서 타입으로
등록한다. framework 는 handler 실행 시 DI 에 등록된 의존성을 사용해 handler
인스턴스를 만든다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddHandlersFromAssemblyOf<JoinMatchHandler>();
});
```

`AddHandlersFromAssemblyOf` 는 attribute scan 과 함께 typed actor handler
후보까지 한꺼번에 모은다. 다만 실제 actor 매핑은 Entry Spot 이나 user Spot 의
`Configure()` 에서 일어난다는 점은 그대로 유지된다.

## 5. Actor context

이 절은 actor 안에서 어떤 표면을 통해 spot join 과 현재 상태 조회를 하는지,
그리고 그 표면이 가진 멤버들이 무엇을 의미하는지 정리한다.

actor 가 다른 user Spot 으로 이동하려면 framework 가 attach 한
`IZLinkActorContext` 를 거쳐야 한다. channel outbound 는 actor context 의
기능이 아니다. Entry Spot 또는 user Spot 안에서 channel 로 메시지를 보내려면
해당 spot 의 `Context.Outbound.SendToChannel(...)` / `Context.Outbound.RequestToChannel(...)` 을
사용한다.

```csharp
public interface IZLinkActorContext
{
    RoutingId? SpotRid { get; }
    bool IsJoined { get; }

    IZLinkBoundSession BoundSession { get; }

    IZLinkSpot GetSpot();
    TSpot GetSpot<TSpot>()
        where TSpot : IZLinkSpot;

    IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        RoutingId spotRid,
        TRequest request);

    IZLinkActorJoinEntrySpotCall JoinEntrySpot(
        RoutingId spotNodeRid);
}
```

각 표면의 의미는 다음과 같다.

| 표면 | 의미 |
| --- | --- |
| `SpotRid` / `IsJoined` | user Spot에 join한 경우 그 spot의 domain 이름, routing id, join 상태. Entry Spot에 있을 때는 `IsJoined`가 false이고 `SpotRid`는 없다 |
| `BoundSession` | actor 에 bind 된 STREAM session 으로 push 하거나 disconnect |
| `GetSpot()` / `GetSpot<TSpot>()` | 자기가 join한 user Spot 객체에 접근 |
| `JoinSpot(spotRid, request).SubmitAsync<TReply>(...)` | user Spot에 join 요청 (Entry → user Spot 또는 user Spot → user Spot 이동). STREAM session binding을 전제로 하지 않는다. `spotRid`은 user Spot routing id(`RoutingId`) |
| `JoinEntrySpot(spotNodeRid).SubmitAsync(...)` | target SpotNode 의 Entry Spot 으로 이동. message payload와 join reply payload는 없다 |

actor request 에 대한 reply 는 actor context 의 별도 `Reply(...)` 호출이 아니라
request handler 의 반환값으로 처리한다. actor, Entry Spot actor, user Spot actor
request handler 는 모두 `TReply` 를 반환하고, framework 가 원래 request 의
sequence 로 response 를 작성한다. request packet 은 send handler 로 fallback
dispatch 되지 않는다.

## 6. Actor Route Resolution

session 이 actor 로 packet 을 relay 할 때는 `IZLinkSession.OnDispatchAsync(...)`
에서 actor handle 을 만들거나 찾은 뒤 `IZLinkSessionActor.RelayAsync(...)` 를 호출한다.
application 이 actor runtime 을 직접 호출하는 별도 public client 는 두지 않는다.
이때 session callback 으로 받은 payload 는 framework runtime 이 callback 동안
빌려준 값이다. session 은 이를 직접 해제하거나 `Move()` 로 소비하지 않고,
`IZLinkSessionActor.RelayAsync(...)` 에 그대로 넘긴다. remote ActorGateway 로 보내기 위해
필요한 내부 frame 은 framework 가 별도로 만든다.

remote actor 위치 해석은 public resolver 가 아니라 core ActorGateway 경로가 맡는다.
session 은 local actor 를 actor id/type 으로 bind 하거나, Play 서버가 join 결과에서 받은
`ActorRef` 로 remote actor handle 을 bind 한다. 이 구조에서는 session packet 마다
application 저장소를 조회하지 않으며, application route mesh channel 을 직접 고르지도 않는다.

## 7. SPOT에 actor 붙이기

이 절은 SPOT 안의 객체로 actor 를 쓰는 패턴을 정리한다. 즉 어떤 spot 에
누가 들어올 수 있는지, 그리고 들어온 actor 가 request / send 를 어떻게
처리하는지를 차례로 본다.

SPOT 안의 객체로 actor 를 쓰고 싶을 때 적용하는 패턴이다. room 의 player,
stage 의 character, zone 의 entity 같은 경우가 여기에 해당한다.

### 7.1 spot 안에서 actor join handler 등록

SPOT spec ([aspnet-core-spot.ko.md](./aspnet-core-spot.ko.md)) 의
`IZLinkSpotContext` 에는 `AddActorJoin<...>()` 표면이 있다. 이 표면을 써서
다음 두 가지를 매핑한다.

- spot 에 합류 요청이 들어오면 어느 handler 를 부를지
- 합류에 성공하면 어떤 actor type 을 생성할지

```csharp
public sealed class TicTacToeGameSpot(IZLinkSpotContext context) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;

    // packet/subscribe/timer/actor-join 등록은 Configure()에서 한다.
    public void Configure()
    {
        Context.AddActorJoin<TicTacToeGameJoinHandler>();
        // ...
    }

    // 비동기 초기화가 필요하면 OnInitializeAsync를 쓴다.
    // context는 생성자 주입 또는 framework가 Configure() 단계에서 attach한 값을 쓴다.
    public ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        => ValueTask.CompletedTask;
}
```

join handler 는 별도의 핸들러 인터페이스를 구현하거나
`[ZLinkSpotActorJoin]` attribute 가 붙은 public method 로 선언한다. attribute
방식은 등록 코드를 짧게 만들 수 있지만, method 시그니처 검증은 startup
validation 단계에서 이루어진다. 자세한 시그니처는
[handler-interfaces.ko.md](./handler-interfaces.ko.md) §5.7 에서 다룬다.

### 7.2 actor가 spot에 합류하기

다른 곳에 사는 actor (예: session-attached actor) 가 어떤 spot 에 합류하려면
자기 context 의 `JoinSpot(spotRid, request)` 를 호출한다. 여기서 `spotRid`
은 user Spot routing id(`RoutingId`) 이다. domain id 에서 `RoutingId` 로의
변환이나 조회는 application registry 가 처리한다.

```csharp
var matchSpotRid = RoutingId.From(matchId);
var result = await actor.Context
    .JoinSpot(matchSpotRid, new JoinMatchReq(...))
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync<JoinMatchSpotResult>(cancellationToken);
```

이 호출은 spot 쪽 join handler 의 결과를 `Reply` 로 돌려주고, application join 결정은
`ResultCode` 로 표현한다. `ResultCode == 0` 은 join 허용, 0 이 아닌 값은 room full,
match closed 같은 application 정의 거절 코드다. transport, timeout, protocol failure 는
결과값이 아니라 예외로 처리한다. 성공 시 actor 쪽 상태가 다음과 같이 갱신된다.

- `Context.IsJoined` 가 `true` 가 된다.
- `Context.SpotRid` 가 채워진다.

이후부터 spot 은 actor 객체에 직접 접근할 수 있다 (spot handler 에서 `actor`
인자로 받게 된다).

### 7.3 client stream push

session-bound actor 에서 client stream 으로 push 해야 하는 경우에는
`IZLinkBoundSession` 를 사용한다. actor request 에 대한 응답은 별도 push 로
쓰지 않고 request handler 반환값으로 보낸다.

## 8. STREAM session에 actor 붙이기

이 절은 STREAM session 위에서 actor 를 어떻게 만들고 attach 하는지, 그리고
그 흐름이 client 연결과 어떻게 함께 움직이는지를 정리한다.

session-attached actor 는 client stream 연결과 함께 사용할 수 있지만,
session 종료가 곧 actor leave 나 actor destroy 를 뜻하지 않는다. client 가
인증을 마치고 session 이 열리면 그 session 안에서 actor handle 을 bind 할 수
있다. session 이 닫혔을 때 어떤 actor 에게 disconnect 를 알릴지는
application 이 결정한다.

이 패턴은 보통 **gateway / playhouse[^playhouse]** 같은 서버에서 사용한다.
즉 client 는 stream 으로 들어오고, server 는 그 client 를 actor 로 다룬다.
모든 routing 을 actor id 기준으로 통일하는 모양이다.

### 8.1 session-actor binding 표면

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

`IZLinkSessionActor` 는 session 에 bind 된 actor handle 이며, handle 자체가
stream packet relay 와 disconnect notification 을 수행한다.

```csharp
public interface IZLinkSessionActor
{
    string ActorId => Ref.ActorId;
    ActorRef Ref { get; }

    ValueTask RelayAsync(...);

    ValueTask NotifyDisconnectedAsync(...);
}
```

- `BindAsync(actor, ...)` -- local `IZLinkActor` instance 를 session 에 bind 한다. actor 를 새로 만들지 않고, runtime 에 이미 생성된 actor instance 여야 한다.
- `BindAsync(actorRef, ...)` -- `JoinSpot(...)` /
  `JoinEntrySpot(...)` 결과가 돌려준 최종 ActorRef 로 session binding 을 만든다.
- `Bound` -- 현재 session 에 bind 된 actor handle snapshot 이다.
- `actor.NotifyDisconnectedAsync(...)` -- session application 이 선택한
  actor 하나에 disconnect notification 을 전달한다. 이 호출은 actor membership
  을 변경하지 않는다.
- `Find(actorId)` -- 현재 session 에 이미 bind 된 actor
  handle 을 actor id 로 찾는다. 한 session 이 여러 actor 를 bind 할 수 있으므로
  framework 의 session binding 을 조회하고, application 이 actor handle 목록을
  따로 복제하지 않게 한다.
- `actor.RelayAsync(...)` -- 들어온 packet 을 actor 에게 dispatch 한다.
  보통 framework 가 자동으로 처리한다.
- session disconnect 는 actor 에게 자동 전파되지 않는다. 알림이 필요하면 session
  code 가 대상 actor 를 고른 뒤 `actor.NotifyDisconnectedAsync(...)` 를 호출한다.

session callback 에서 unbound standalone actor 를 만드는 표면은 두지 않는다.
standalone actor 가 필요하다면 actor node 측에서 별도의 등록 표면을 쓴다 (예:
actor factory 와 actor node 가 직접 호출하는 create helper). 정책 기준은
[공통 actor 모델](../../../../doc/spec/actor-model.ko.md) §4 lifecycle 표를
참고한다.

### 8.2 session 안에서의 흐름

```mermaid
sequenceDiagram
    autonumber
    participant C as Client
    participant S as Session
    participant G as ActorGateway
    participant Act as IZLinkActor

    C->>S: STREAM connect + authenticate
    S->>S: 인증 (AuthenticateReq → actorId)
    S->>Act: BindAsync(actor)
    Note over Act: bind는 actor instance를 새로 만들지 않음
    S->>G: Bind sessionRid to logical actor

    Note over C,Act: 이후 client packet
    C->>S: PlaceMarkReq
    S->>G: actor.RelayAsync(...)
    G->>Act: Dispatch by current actor route
    Act->>Act: handler 실행

    Note over C,Act: 연결 종료
    C-->>S: 끊김
    S->>G: Conditional unbind by session token
    opt application decides to notify this actor
        S->>G: actor.NotifyDisconnectedAsync()
        G->>Act: Spot actor disconnected handler
    end
```

## 9. Session actor dispatch (gateway 패턴)

이 절은 이 문서에서 다루는 가장 큰 use case 를 정리한다. 서버 역할을 두 종류로
나누고, 각 역할이 어떤 ActorGateway binding 위에서 동작하는지 본다.

서버를 여러 대 두는 구성을 가정해 보자. 이 구성에서 **Session 서버** 는
client 연결만 받는다. 실제 gameplay 로직은 **Play 서버** 의 actor 가 처리한다.

그러면서 client 입장에서는 stream 을 하나만 유지한다. Play 서버가 client 에게
message 를 보낼 때도, 그 stream 을 그대로 타고 push 되어야 한다.

이 구조의 핵심 표면은 다음과 같다.

- **actor handle** -- Session 서버가 actor id/type 으로 만드는 handle 이다.
  local actor 는 process 안의 native actor ref 로 bind 하고, remote actor 는 actor 생성 또는
  join 결과의 `ActorRef` 로 ActorGateway remote actor ref 를 얻어 bind 한다.
- **STREAM ActorGateway attach** -- Session 서버의 STREAM node 가 어느 SpotNode 를
  session owner gateway 로 사용할지 지정하는 등록이다. 이 등록이 있어야 session 에서
  actor 로 가는 relay 와 actor 에서 bound session 으로 돌아오는 push 가 같은 gateway
  상태를 사용한다.
- **`IZLinkSpotRemoteAddressResolver`** -- "spot rid → user Spot routing id" 를
  푼다. actor 가 `JoinSpot(spotRid, ...)` 로 node 경계를 넘을 수 있다면 이
  resolver 를 등록한다.
- **`IZLinkBoundSession`** -- Play 서버 actor 가 자기 client 에게 push 를 보낼
  때 쓰는 표면이다. 현재 actor id 는 framework 가 알고 있으므로 호출자가 다시
  넘기지 않는다.

### 9.1 전체 흐름

```mermaid
sequenceDiagram
    autonumber
    participant C as Client
    participant S as Session Server
    participant P as Play Server (Actor)
    participant G as ActorGateway

    C->>S: STREAM 연결 + 인증
    S->>G: Bind sessionRid + token to logical actor

    C->>S: PlaceMarkReq
    S->>G: Relay to logical actor
    G->>P: Dispatch to current actor owner
    P->>P: actor handler 실행
    P->>P: 결과 → notification 필요
    P->>G: BoundSession.Send(...)
    G->>S: Relay to bound session owner
    S->>C: STREAM push (TurnChangedNotify)

    Note over C,P: 재접속 시
    C->>S: 재인증 (다른 Session 서버일 수 있음)
    S->>G: Replace binding by session token
    Note over P,G: Play actor는 새 binding으로 push
```

핵심은 다음과 같다. Play 서버의 actor 는 stream 을 직접 들고 있지 않다. 대신
actor handler 는 **`IZLinkBoundSession`** 에 "현재 actor 의 client 로 message
를 보내라" 고만 부탁한다. 다른 actor 의 client 로 보내야 하는 service 는 먼저
그 actor 에 메시지를 보내고, 대상 actor handler 가 자기 `IZLinkBoundSession` 를 사용한다.

Spot actor handler 인터페이스와 `ZLinkSpotActorSendContext` /
`ZLinkSpotActorRequestContext` 는 `Zlink.Framework.Contracts.Spots` 에 둔다.
actor instance handler 는 actor 인스턴스와 payload 만 받는 낮은 수준의 fallback
표면이며 stream metadata 를 노출하지 않는다. `IZLinkBoundSession` 는 client
stream 연결을 향한 proxy 이므로 `Zlink.Framework.Contracts.Streams` 에 남긴다.

이 부탁을 받은 framework 는 다음 순서로 일을 처리한다.

1. actor handler 의 현재 actor id/type 으로 bound session binding 을 찾는다.
2. core ActorGateway actor-to-session API 로 payload 를 내려보낸다.
3. bound session owner 가 local 이면 해당 STREAM session 으로 바로 보내고, remote 이면
   owner gateway 로 내부 relay 를 보낸다.

ActorGateway 내부 relay packet 은 application `AddRouteMeshChannel(...)` handler group 으로
노출되지 않는다. application route mesh channel 은 일반 routed messaging 용도로 남고,
session actor relay 의 public 설정 조건이 아니다.

client 와 Session 서버 사이의 STREAM packet 은 그대로 단일 stream packet
frame 으로 처리한다. 따라서 actor dispatch payload 를 내부 DTO[^dto] 의 `byte[]`
필드에 담아 다시 JSON envelope[^json-envelope] 로 감싸 보내는 방식은 이
초안의 목표가 아니다.

### 9.2 `IZLinkBoundSession`

```csharp
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Streams;

public interface IZLinkBoundSession
{
    IZLinkBoundSessionSendCall Send<TMessage>(TMessage message);
    ValueTask DisconnectAsync(CancellationToken cancellationToken = default);
}
```

`IZLinkBoundSession` 은 server-to-client request API 를 제공하지 않는다.
client request 에 대한 응답은 actor request handler 의 반환값으로 처리한다.

actor handler 에서 받아 쓰는 모습은 다음과 같다.

```csharp
public sealed class JoinMatchHandler
    : IZLinkSpotActorRequestHandler<GameSpot, PlayerActor, JoinMatchReq, JoinMatchRes>
{
    public async ValueTask<JoinMatchRes> HandleAsync(
        GameSpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        JoinMatchReq request,
        CancellationToken cancellationToken)
    {
        await actor.Context.BoundSession
            .Send(new OpponentJoinedNotify(...))
            .Submit(cancellationToken);

        context.Reply
            .Metadata("trace-id", "reply-trace")
            .Compress();

        return new JoinMatchRes(...);
    }
}
```

`context.Reply` 는 응답을 직접 보내지 않는다. actor request handler 의 응답은
항상 반환값으로 정하고, `Reply` 는 그 응답 frame 의 metadata/compression 옵션만
기록한다.

같은 user Spot 안에서 다른 actor 의 client session 으로 push 가 필요하면 대상 actor 로
메시지를 보내고, 대상 actor handler 가 actor context 의 `IZLinkBoundSession` 를 사용한다.
actor message handler 는 transport raw header 나 session rid 를 직접 받지 않는다.

actor 가 client 연결을 끊어야 한다고 판단한 경우에는
`IZLinkBoundSession.DisconnectAsync(...)` 를 호출한다. 이 동작은
application 이 시작한 close 이므로 session 의 `OnDisconnectedAsync(...)` 를
다시 호출하지 않는다.

### 9.3 라우팅 상태

session relay 는 application route mesh resolver 를 사용하지 않는다. session 이 actor id/type 으로
local actor handle 을 만들거나 Play 서버가 돌려준 `ActorRef` 로 remote actor
handle 을 만들면, core ActorGateway 가 해당 actor ref 를 기준으로 relay 한다.

actor-session binding 은 public route resolver 결과가 아니다. 이전 stream
  의 뒤늦은 close 가 새 binding 을 지우지 못하도록, 내부에서 binding token
  으로 조건부 갱신을 수행한다.

### 9.4 등록 패턴

Session 서버는 다음과 같이 등록한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddSpotMesh("game.session", mesh =>
    {
        mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://registry1:5551"));
        mesh.AddNode("session-node", node =>
        {
            node.EnableRouter(router =>
            {
                router.BindRouter("tcp://0.0.0.0:7201");
            });
        });
    });

    options.AddStreamNode("client-stream", stream =>
    {
        stream.Bind("tcp://0.0.0.0:7101");
        stream.AttachActorGateway("session-node");
    });
});
```

Play 서버는 다음과 같이 등록한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddActorFactory<PlayerActorFactory>("player");
    options.UseRegistrySpotRemoteAddresses("game");

    options.AddSpotMesh("game.stage", mesh =>
    {
        mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://registry1:5551"));

        mesh.AddNode("play-node", node =>
        {
            node.EnableRouter(router =>
            {
                router.BindRouter("tcp://0.0.0.0:9000");
            });
            node.AddEntrySpot<PlayerEntrySpot>();
            node.AddSpotFactory<MatchSpot>();
        });
    });

    // routed channel 등록은 별도 문서 참고
});
```

actor remote address resolver 는 session relay 의 등록 요소가 아니다.
session 서버는 client packet 마다 application route lookup 을 수행하지 않고,
framework 가 만든 actor handle 과 ActorGateway 경로를 사용한다.

actor-session binding 은 framework / core runtime 내부에서 관리한다. 각 서버의
역할을 나누어 보면 다음과 같다.

- Session 서버는 인증 후 Play 서버의 ensure actor 응답이나 `JoinEntrySpot(...)` 결과에서
  ActorRef 를 받고, `BindAsync(actorRef, ...)` 로
  actor handle 과 session binding 을 얻는다.
- Play 서버는 `IZLinkActorManager.GetOrCreateAsync(...)` 로 actor 를 준비한 뒤
  필요한 Entry Spot/User Spot join 을 수행하고, join 결과의 ActorRef 를 응답에 싣는다.
- Play actor 는 `IZLinkBoundSession` 로 자기 client binding 을 사용한다. application
  service 가 다른 actor 의 client 로 보내야 하면 대상 actor 로 메시지를 보내서 처리한다.

이 동작을 위해 별도의 public session route API 나 기록 API 를 등록할 필요는
없다.

## 10. 등록 표면 종합

이 절은 앞서 본 표면들을 `IZLinkFrameworkOptions` 한 자리에 모아 다시 본다.

`IZLinkFrameworkOptions` 가 노출하는 actor 관련 등록 메서드는 다음과 같다.

```csharp
public interface IZLinkFrameworkOptions
{
    // ... (channel / spot / stream 등록 등 다른 메서드 생략)

    void AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory;

    void AddSpotRemoteAddressResolver<TResolver>()
        where TResolver : class, IZLinkSpotRemoteAddressResolver;

}
```

각 메서드의 용도를 정리하면 다음과 같다.

| 메서드 | 누가 필요한가 | 무엇을 하는가 |
| --- | --- | --- |
| `AddActorFactory<>(type)` | actor를 만들어 attach하는 서버 (Play 서버 / SPOT 호스트) | actorType 키로 factory를 매핑 |
| `AddSpotRemoteAddressResolver<>()` | actor가 spot rid로 user Spot에 join하거나 spot outbound를 쓰는 서버 | spot rid → spot routing |
| `AddSpotMesh(...).AddNode(...).AddEntrySpot<>()` | actor runtime을 가진 SPOT host | 자동 Entry Spot에 붙일 actor packet/lifecycle registry 등록 |
| `AddSpotMesh(...).AddNode(...).AddSpotFactory<>()` | user Spot을 만드는 SPOT host | Spot 타입 기준 factory 매핑 |

## 11. 다른 문서와의 관계

- 인터페이스 전체 정의:
  [handler-interfaces.ko.md](./handler-interfaces.ko.md) §5 (`IZLinkActor`,
  `IZLinkActorContext`, `IZLinkActorFactory`, actor handler 인터페이스,
  routing record 등)
- SPOT에 actor가 합류하는 표면(`AddActorJoin<...>`):
  [aspnet-core-spot.ko.md](./aspnet-core-spot.ko.md)
- STREAM session lifecycle과 `IZLinkSession` 표면:
  [aspnet-core-stream.ko.md](./aspnet-core-stream.ko.md)
- session actor dispatch 정책 문서 (구현 전 초안):
  [session-gateway-usability.ko.md](../../../../doc/spec/session-actor-dispatch.ko.md)
- TicTacToe sample에서 모든 표면이 함께 쓰이는 예시:
  [tictactoe-game-sample.ko.md](../guide/samples/tictactoe-game-sample.ko.md)

## 12. 결정된 기준

- actor 의 packet handler 와 lifecycle callback handler 는 Entry Spot 또는
  user Spot registry 에서 등록한다. attribute scan 과 그룹 매핑은 일반
  channel handler 전용이다.
- actor 의 위치는 application 의 resolver 가 결정한다. framework 는 그
  정보의 저장소를 소유하지 않는다.
- actor id 는 application identity[^identity] 다. 보통 인증 단계에서
  결정된다. framework 는 actor id 발급에 관여하지 않는다.
- Play 서버의 actor 가 자기 client 에 push 를 보낼 때는 **반드시 actor
  context 의 `IZLinkBoundSession`** 를 거친다. 특정 actor id 의 client 로
  보내야 하는 application service 는 먼저 해당 actor 에 메시지를 보내고,
  그 actor handler 가 자기 `IZLinkBoundSession` 을 사용한다. actor 가 stream
  socket 을 직접 들고 있는 구조가 아니다.

## 13. 회귀 테스트

이 절은 actor 문서가 다룬 항목들을 어떤 테스트로 검증하는지 한 자리에 모아
본다.

Actor 문서의 회귀 테스트 항목은 다음 흐름이 같은 public 표면 위에서 일관되게
이어지는지를 확인한다.

- actor factory
- Entry Spot
- user Spot join
- session bind
- session actor dispatch

이때 actor 가 어느 spot 에 붙어 있는지는 framework 가 관리한다. 사용자는 현재
context 만 다룬다는 원칙을 함께 검증한다.

| 테스트 케이스 | 확인 기준 |
| --- | --- |
| `NodesAndServicesTests.AddZLinkFramework_Throws_WhenActorFactoryNameIsDuplicated` | actor factory 이름이 중복되면 startup validation에서 예외로 막는다. |
| `ActorRegistryExecutionTests.EntrySpot_And_UserSpot_ActorPacketRegistries_Dispatch_ActorPackets` | Entry Spot과 user Spot에 등록한 actor packet/lifecycle handler가 정상적으로 dispatch된다. |
| `EntryMailboxExecutionTests.EntrySpot_ActorPackets_Are_Serialized_Per_Actor_And_Parallel_Across_Actors` | Entry Spot에서 같은 actor의 packet은 순서대로 실행되고, 서로 다른 actor의 packet은 병렬로 진행된다. |
| `EntryMailboxExecutionTests.EntrySpot_NativeActorReadableBatch_Dispatches_Actors_In_Parallel` | native Entry Spot dispatch batch가 actor별 순서를 보존하면서도 다른 actor를 전역으로 막지 않는다. |
| `ActorLifecycleTests.SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext` | actor가 spot을 옮긴 뒤 stale spot 문맥으로 dispatch되지 않는다. |
| `RemoteSessionRelayTests.SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence` | stream session에서 bound actor로 request가 전달되고, sequence별 reply 순서가 맞는다. |
| `LocalSessionRelayTests.LocalSessionActorDispatch_Relays_Stream_Request_And_Replies_From_Request_Handler` | local actor relay 도 request handler 반환값으로 stream response 를 작성한다. |
| `ProtocolTests.SpotActorRegistry_DoesNot_Resolve_Request_To_Send_Handler` | Entry Spot/user Spot actor request packet 이 send handler 로 fallback dispatch 되지 않고, send/request 밖 stream kind 도 actor packet 으로 처리되지 않는다. |
| `ScaffoldSmokeTests.PublicSurface_Removes_ActorReply_And_StreamClientContracts` | actor context Reply 와 actor stream client 계약이 public surface 에 다시 노출되지 않는다. |

---

### 각주 모음

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.

[^actor]: **actor**는 ID로 식별되는 stateful application 객체다. 같은 actor id
    로 들어오는 메시지는 같은 인스턴스가 받는다. framework는 actor를
    라이프사이클 관리, 메시지 dispatch, 라우팅 식별의 단위로 본다.

[^entryspot]: **Entry Spot**은 SpotNode가 자동으로 갖는 입구 spot이다. actor는
    생성 직후 항상 Entry Spot에 위치하며, user Spot으로 옮기기 전까지의 인증
    이나 분기 같은 초기 로직을 여기서 처리한다.

[^session-bind]: **session bind**는 actor를 특정 STREAM session에 연결해서,
    그 session으로 들어오고 나가는 client relay 경로를 actor와 묶는 동작이다.
    actor의 물리적 위치를 결정하지는 않는다.

[^user-spot-join]: **user Spot join**은 actor를 Entry Spot에서 application
    정의 user Spot(room, stage 등)으로 이동시키는 동작이다. session binding과는
    독립적으로 일어난다.

[^di-scope]: DI scope는 `.NET` 의존성 주입 컨테이너에서 객체의 lifetime을
    묶는 단위(singleton, scoped, transient)를 가리킨다. handler 인스턴스 생성과
    소멸은 보통 이 scope를 기준으로 결정된다.

[^raw-api]: raw API는 framework 추상화를 거치지 않고 C 바인딩이 노출하는
    저수준 함수를 직접 부르는 호출을 뜻한다.

[^resolver]: resolver는 식별자(예: actor id, spot rid)를 받아 그 식별자가
    가리키는 실제 위치(routing id 등)를 돌려주는 application 컴포넌트다.
    framework는 위치 정보를 직접 소유하지 않고 resolver에게 위임한다.

[^session-actor-dispatch]: session actor dispatch는 클라이언트 session에서 들어
    온 요청을 그 session과 묶여 있는 actor 쪽으로 자동 전달하는 패턴이다.

[^gateway]: gateway는 외부 client 연결을 받아 인증한 뒤 내부 서비스로 라우팅
    하는 입구 서버를 가리킨다. 이 문서에서는 STREAM session을 받아 actor 호출
    로 변환하는 Session 서버 역할을 가리킨다.

[^channel-messaging]: channel messaging은 채널 이름을 키로 삼아 메시지를
    주고받는 방식이다. request / send는 요청-응답과 단방향 전달을, event
    messaging은 publish / subscribe 형태의 이벤트 전달을 가리킨다.

[^routed]: **routed channel**은 `AddRouteMeshChannel(...)`로 선언하는 양방향 채널이다. 일반 client-server
    채널과 달리 호출 시점에 목적지 노드의 `RoutingId`를 직접 지정한다. 자세한
    내용은
    [aspnet-core-channel-messaging.ko.md](./aspnet-core-channel-messaging.ko.md)
    §5.4 참고.

[^factory]: **factory**는 `IZLinkActorFactory`를 구현한 application 클래스다.
    actorType 키와 함께 등록해 두면, framework가 해당 type의 actor를 만들 때
    이 factory를 호출한다.

[^discovery]: discovery는 등록된 노드(channel, spot 등)의 위치 정보를 Registry
    같은 외부 저장소에서 자동으로 조회하고 갱신해 주는 메커니즘이다. application
    이 IP/port를 직접 박지 않아도 노드 위치를 찾을 수 있다.

[^mailbox]: mailbox는 같은 actor에게 들어오는 메시지를 한 줄로 세워 순차
    처리하는 큐다. actor 단위로 순서를 보장하면서도 서로 다른 actor 사이의
    병렬성은 유지한다.

[^attribute-scan]: attribute scan은 어셈블리 안의 타입과 메서드를 훑으면서
    특정 attribute가 붙은 항목을 찾아 자동으로 등록하는 방식이다.

[^playroute]: **play remote address resolver**는 actor id를 받아 그 actor가 지금 어느
    routed channel의 어느 노드에 사는지를 돌려주는 resolver다.
    session actor dispatch 의 relay hot path 에서는 사용하지 않고, session actor
    ref 가 없는 backend actor messaging 경로에서 사용한다.

[^startup-validation]: startup validation은 framework가 호스트 시작 시점에
    등록 상태를 점검해서, 누락된 의존성이나 잘못된 조합을 즉시 예외로 보고하는
    단계다. 잘못된 설정이 런타임으로 흘러가는 것을 막는다.

[^playhouse]: playhouse는 게임 서비스에서 STREAM client를 받아 actor / room /
    stage 위에서 처리하는 서버를 가리키는 사내 용어다. 여기서는 session 서버 +
    play 서버 조합의 한 형태로 본다.

[^dto]: DTO(Data Transfer Object)는 시스템 사이에서 데이터를 옮기기 위한
    데이터 전용 클래스/record를 가리킨다.

[^json-envelope]: JSON envelope는 실제 payload를 또 다른 JSON 객체 안의 필드로
    감싸 보내는 방식을 가리킨다. 보통 metadata를 함께 싣기 위해 쓰지만, 본문이
    이미 multipart로 나뉘는 경우에는 불필요한 wrapping이 된다.

[^actor-migration]: actor migration은 같은 actor를 다른 play 노드로 옮기는
    동작이다. 라우팅 정보가 갱신되어 외부 호출자가 새 위치로 forwarding된다.

[^identity]: identity는 객체가 누구인지를 식별하는 1급 값이다. actor의 경우
    `ActorId`가 identity로, application이 정한 도메인 단위를 그대로 따라간다.
