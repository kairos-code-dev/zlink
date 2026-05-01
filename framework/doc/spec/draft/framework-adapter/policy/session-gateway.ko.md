[스펙 목차](../../README.ko.md)

[초안 묶음](./README.ko.md) | [개요](./overview.ko.md) | [상호작용 모델](./interaction-model.ko.md) | [메시지 모델](./message-model.ko.md) | [channel topology](./channel-topology.ko.md) | [framework API](./framework-api.ko.md)

# Draft -- Session Gateway And Actor Relay (Superseded)

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니다.
> 이 문서의 public API 표면은
> [session-gateway-usability.ko.md](./session-gateway-usability.ko.md)의
> session actor dispatch 모델로 대체되었다.
> 이 문서는 이전 설계 배경과 문제 맥락을 확인할 때만 사용한다.
> 새 구현과 sample은 이 문서의 `EnableSessionGateway(...)`,
> `AddSessionProxyHandler(...)`, `BindActorAsync(...)`, `OpenActorRelay(...)`,
> `IZLinkSessionGateway.SendToActor(...)` 표면을 기준으로 삼지 않는다.

## 0. 현재 기준

현재 구현 기준은 [Session Actor Dispatch 사용성 초안](./session-gateway-usability.ko.md)이다.
새 기준에서는 session에서 actor로 가는 방향을 actor create/dispatch helper로 표현하고,
actor에서 client session으로 가는 방향만 `SessionProxy`로 표현한다. 이 문서의
`SessionGateway` 이름과 direct target API는 제거 대상이다.

## 1. 목적

대규모 실시간 서비스에서는 client 연결을 가진 서버와 실제 gameplay나 domain
상태를 가진 서버를 분리하는 경우가 많다. 이 구조에서는 session server가
client stream을 소유하고, play server는 actor와 room, spot 같은 실행 상태를
소유한다.

이 문서는 아래 구조를 framework가 어떻게 도울지 정의한다.

```text
+----------+     +----------------+     +-------------+
| Client   | --> | Session Server | --> | Play Server |
+----------+     +----------------+     +-------------+
                         ^                     |
                         |                     v
                         +---------------------+
```

이 구조에서 client는 session server에만 연결한다. session server는 인증 후
`actorId`를 현재 client stream에 묶고, client packet을 현재 actor가 있는 play
server로 relay한다. play server가 client에게 메시지를 보낼 때도 `actorId`를
기준으로 session server의 gateway를 호출한다.

## 2. 핵심 전제

이 초안에서는 `playerId`와 `actorId`를 같은 값으로 본다. 따라서 gateway와 relay
표면에서 client를 식별하는 안정적인 키는 `sessionId`가 아니라 `actorId`다.

`sessionId`는 물리 연결을 나타내는 내부 값이다. client가 끊겼다가 다른 session
server로 다시 연결되면 `sessionId`는 바뀔 수 있다. 반면 `actorId`는 같은 사용자의
논리 actor를 나타내므로 session server가 바뀌어도 유지된다.

정리하면 아래와 같다.

| 값 | 의미 | 외부 라우팅 키로 사용하는가 |
|----|------|-----------------------------|
| `actorId` | player이자 actor의 논리 ID | 예 |
| `sessionId` | 한 session server 안의 물리 연결 ID | 아니오 |
| `targetNodeRid` | 실제 packet을 보낼 zlink node의 `RoutingId` | 예, transport target |

`actorId`는 domain key이고, `targetNodeRid`는 transport target이다. framework는
둘을 섞지 않아야 한다.

## 3. 책임 경계

framework가 제공할 책임은 작게 유지한다.

- session server에서 `actorId -> 현재 client stream` binding을 관리한다.
- session server가 특정 play node로 client packet을 relay할 수 있게 한다.
- play server가 특정 session node로 client-facing packet을 보낼 수 있게 한다.
- request/reply는 메시지 이름이 아니라 request sequence를 기준으로 이어 준다.

아래 책임은 application이 구현한다.

- `actorId -> play node RoutingId` 위치 저장
- `actorId -> session node RoutingId` 위치 저장
- actor 이동 정책
- room, spot, match lifecycle 정책
- actor instance 생성, 복구, snapshot, migration

이 경계를 지키는 이유는 session gateway 기능이 게임 서버 전용 registry나 migration
정책으로 굳어지는 것을 막기 위해서다. framework는 안정적인 전달 경로만 제공하고,
어느 서버가 어떤 actor를 소유하는지는 application이 결정한다.

## 4. Routed Channel

현재 channel client가 `DEALER` 기반 client만 제공하면 특정 play server를 지정해서
보내는 relay를 표현하기 어렵다. `DEALER` client는 channel 이름 기준으로 연결된
peer 집합에 요청을 보내는 모델이고, `targetNodeRid`를 public API에서 지정하는
모델이 아니다.

따라서 session gateway 초안을 구현하려면 기존 `AddChannel`과 별개로 router 간
직접 메시징을 위한 routed channel이 필요하다. routed channel은 handler group을
등록하는 개념이 아니라, 같은 router 연결망을 구분하는 ID다.

```text
routerChannelId -> router mesh id
targetNodeRid   -> destination node in that mesh
packetName     -> handler selection key in the message envelope
```

`routerChannelId`는 같은 router 연결망에 참여하는 node들을 구분한다.
`targetNodeRid`는 그 연결망 안의 실제 목적지다. `packetName`은 도착한 node에서
어떤 handler를 호출할지 고르는 값이며, routed channel 설정에 미리 나열하지 않는다.

설정 표면은 기존 `AddChannel`과 분리한다.

```csharp
public interface IZLinkFrameworkOptions
{
    void AddRoutedChannel(
        string routerChannelId,
        Action<IZLinkRoutedChannelBuilder> configure);
}

options.AddRoutedChannel("backend", routed =>
{
    routed.Bind("tcp://0.0.0.0:7000");

    routed.ConfigureRouting(routing =>
    {
        routing.RoutingId = RoutingId.FromString("session-1");
    });

    routed.UseManualConnections(peers =>
    {
        peers.Connect("tcp://10.0.0.20:7000");
    });
});
```

이 API에서 `"backend"`는 service-style handler 이름이 아니다. 같은 `"backend"`에
묶인 router들이 직접 routing할 수 있는 연결망 ID다.

builder의 최소 표면은 아래와 같다.

```csharp
public interface IZLinkRoutedChannelBuilder
{
    void Bind(string endpoint);

    void ConfigureRouting(
        Action<IRoutedPeerOptions> configure);

    void UseManualConnections(
        Action<IRoutedChannelConnections> configure);

    void AddSendHandler<THandler, TMessage>(
        string? packetName = null)
        where THandler : class, IZLinkRoutedSendHandler<TMessage>;

    void AddRequestHandler<THandler, TRequest, TReply>(
        string? packetName = null)
        where THandler : class, IZLinkRoutedRequestHandler<TRequest, TReply>;

    void EnableSessionGateway();

    void AddSessionProxyHandler<THandler>()
        where THandler : class, IZLinkSessionProxyHandler;
}

public interface IRoutedChannelConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}
```

`AddSendHandler(...)`와 `AddRequestHandler(...)`는 일반 `AddChannel` handler registry와
다른 routed handler registry에 등록한다. `packetName`을 넘기지 않으면 handler의
message 타입 metadata에서 기본 이름을 얻는다.

```csharp
public interface IZLinkRoutedClient
{
    IZLinkRoutedSendCall SendTo<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message);

    IZLinkRoutedRequestCall RequestTo<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request);
}
```

`IZLinkRoutedSendCall`과 `IZLinkRoutedRequestCall`은 기존 channel call builder와 같은
모양을 따른다. 차이는 call을 만들 때 이미 `routerChannelId`와 `targetNodeRid`가
정해져 있다는 점뿐이다.

```csharp
public interface IZLinkRoutedSendCall
{
    IZLinkRoutedSendCall WithPacketName(string packetName);

    ValueTask Async(
        CancellationToken cancellationToken = default);
}

public interface IZLinkRoutedRequestCall
{
    IZLinkRoutedRequestCall WithPacketName(string packetName);

    IZLinkRoutedRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(
        CancellationToken cancellationToken = default);
}
```

`SendTo(...).Async(...)`는 peer handler 완료를 기다리는 호출이 아니다. framework가
메시지를 transport에 맡길 수 있을 때까지 기다리는 비동기 submit이다. 구현은
blocking send를 task로 감싸면 안 된다. 먼저 nonblocking send를 시도하고,
temporary backpressure가 발생하면 channel의 pending send queue에 넣은 뒤
socket ready callback 또는 poller wakeup에서 다시 전송해야 한다. 그래서 호출
thread는 backpressure 동안 막히지 않는다.

send backpressure 대기 한계는 call builder가 아니라 channel 또는 socket의
`SendTimeout` 옵션을 따른다. `SendTo(...).Async(...)`의
`CancellationToken`은 호출자가 이 submit 대기를 취소할 때만 사용한다. 이 초안은
별도 public no-wait 옵션을 제공하지 않는다. temporary backpressure는 public API의
분기값이 아니라 framework 내부 queue와 ready notification으로 처리한다.
framework routed channel의 기본 `SendTimeout`은 `TimeSpan.FromMilliseconds(200)`으로
둔다. async submit runtime은 core socket 기본값을 직접 사용하지 않고, routed
channel option에 설정된 resolved `SendTimeout` 값을 읽어 pending deadline으로
사용한다. 사용자가 `SendTimeout = null`을 명시한 경우에만 core `-1`과 같은 무한
대기로 본다.

`RequestTo(...).Async<TReply>(...)`도 request packet을 보내는 단계에서는 같은
nonblocking submit 경로를 사용한다. `WithTimeout(...)`은 reply 대기 시간만
정한다. request packet을 transport에 맡기는 동안 발생하는 backpressure는
`SendTimeout` 정책이 처리하고, send 실패나 취소가 발생하면 pending request를
제거한 뒤 호출을 실패시켜야 한다. reply timeout은 request packet submit이
끝난 뒤부터 계산하는 편을 기본으로 본다.

호출자가 `await SendTo(...).Async(...)` 또는
`await RequestTo(...).Async<TReply>(...)`를 사용하면 application 흐름은 submit
완료나 reply 도착까지 기다린다. 그러나 backpressure 동안 thread를 점유하면 안
된다. 구현은 pending item을 등록하고 즉시 제어를 반환한 뒤, socket ready callback
또는 poller wakeup에서 이어서 처리해야 한다.

router-to-router 경로는 서버 간 통신에 쓰이므로 고성능 구현 조건을 별도로 둔다.

- immediate send가 성공하면 completed `ValueTask`를 사용한다.
- pending routed send queue는 bounded여야 한다. 한계는 channel/socket high water
  mark, `SendTimeout`, cancellation, runtime stop으로 제어한다.
- ready callback은 pending queue를 batch로 drain한다. batch budget은 다른 socket과
  timer가 굶지 않을 정도로 제한한다.
- request는 sequence를 먼저 할당하고 pending request table에 등록한 뒤 전송한다.
  send 실패, send timeout, cancellation이 발생하면 pending request table에서 같은
  sequence를 제거한다.
- 같은 request frame은 retry 과정에서 한 번만 실제 전송되어야 한다.
- serialization 결과와 native message buffer는 retry 동안 재사용할 수 있게
  소유권을 명확히 두고, 완료나 실패 시 한 번만 해제한다.

사용자는 같은 routed channel 안에서 target node를 명시해서 메시지를 보낸다.

```csharp
await routedClient
    .SendTo("backend", targetNodeRid, new NodePingMsg("session-1"))
    .Async(cancellationToken);
```

이 routed client는 내부적으로 routed socket 기능을 사용해야 한다. 구현체는
`targetNodeRid`를 알고 있을 때 `SendTo`와 `RequestTo`를 제공하고, target 없는
`Send`/`Request`와 의미를 섞지 않는다.

## 5. Inbound Dispatch

routed channel은 handler group을 미리 나열하지 않지만, inbound packet을 처리할
dispatch 규칙은 필요하다. 구현은 routed channel마다 하나의 local router socket을
열고, 받은 envelope의 `packetName`과 `kind`로 handler를 찾는다.

여기서 handler group을 미리 나열하지 않는다는 말은 `AddChannel("Api", ...)`처럼
channel 이름 아래에 처리 대상을 묶지 않는다는 뜻이다. routed channel은
`routerChannelId`로 연결망만 정하고, inbound handler는 메시지별로 따로 등록한다.

handler key는 아래 세 값을 사용한다.

| key | 의미 |
|-----|------|
| `routerChannelId` | routed channel ID |
| `kind` | send 또는 request |
| `packetName` | envelope의 packet name |

즉 `packetName`만 전역으로 유일할 필요는 없다. 같은 packet name이라도
`routerChannelId`나 `kind`가 다르면 별도 handler가 될 수 있다. 반대로 같은
`routerChannelId + kind + packetName`에 handler가 둘 이상 등록되면 startup에서
실패해야 한다. body type은 handler registry에 저장된 decode 대상이지, incoming
packet을 찾는 key가 아니다.

generic routed handler는 기존 channel handler shape를 재사용한다. context만 routed
전용 context를 사용한다.

```csharp
public interface IZLinkRoutedSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkRoutedSendContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkRoutedRequestHandler<in TRequest, TReply>
{
    ValueTask<TReply> HandleAsync(
        TRequest request,
        ZLinkRoutedRequestContext context,
        CancellationToken cancellationToken);
}

public sealed class ZLinkRoutedSendContext : IZLinkHandlerContext
{
    public string RouterChannelId { get; }
    public RoutingId SourceNodeRid { get; }
}

public sealed class ZLinkRoutedRequestContext : IZLinkHandlerContext
{
    public string RouterChannelId { get; }
    public RoutingId SourceNodeRid { get; }
    public DateTimeOffset? Deadline { get; }
}
```

구현은 일반 channel attribute scanner와 분리해서 routed handler registry를 만든다.
그래야 같은 `packetName`을 일반 channel과 routed channel에서 각각 사용할 수 있다.

session gateway와 actor relay는 이 generic routed handler 위에 framework가 올리는
특수 handler다. 사용자가 직접 handler key를 맞추지 않아도 되도록, session gateway
기능을 켜면 framework가 필요한 internal handler를 routed channel에 등록한다.

framework가 예약하는 internal packet name은 아래 두 개다.

| packet name | 방향 | 처리 주체 |
|--------------|------|-----------|
| `ZLink.ActorRelay` | session server -> play server | `AddSessionProxyHandler<THandler>()` |
| `ZLink.SessionGateway` | play server -> session server | `EnableSessionGateway()` |

```csharp
options.AddRoutedChannel("backend", routed =>
{
    routed.Bind("tcp://0.0.0.0:7000");
    routed.EnableSessionGateway();
    routed.AddSessionProxyHandler<PlaySessionProxyHandler>();
});
```

`EnableSessionGateway()`는 play server가 보낸 client-facing message를 session
server가 받을 수 있게 한다. `AddSessionProxyHandler<THandler>()`는 session server가
보낸 actor relay message를 play server가 받을 수 있게 한다. 둘은 같은 routed
channel에서 동시에 켤 수 있지만, 필요한 역할만 켜도 된다.

## 6. Session Server 표면

session server는 client stream을 소유한다. 인증이 끝나면 session은 `actorId`를
현재 stream에 묶는다.

```csharp
public interface IZLinkSessionContext
{
    string SessionId { get; }

    ValueTask BindActorAsync(
        string actorId,
        CancellationToken cancellationToken = default);

    ValueTask UnbindActorAsync(
        CancellationToken cancellationToken = default);

    IZLinkActorRelay OpenActorRelay(
        string routerChannelId,
        RoutingId targetPlayNodeRid,
        string actorId);

    IZLinkSessionRequestCall Request<TRequest>(
        TRequest request);
}
```

`BindActorAsync`는 현재 session server 안에서 `actorId -> stream` binding을
만든다. 같은 `actorId`가 새 연결로 다시 인증되면 구현체는 이전 binding을 교체할 수
있어야 한다. 이 교체 정책은 한 session server 안의 동작이며, 여러 session server
사이의 전역 위치 관리는 application의 책임이다.

`OpenActorRelay`는 현재 session에서 받은 client packet을 특정 play node로 보내기
위한 relay 객체를 만든다. `routerChannelId`는 어떤 router 연결망을 사용할지
정하고, `targetPlayNodeRid`는 그 연결망 안의 목적지를 정한다. 이 API는
`actorId`만으로 target을 찾지 않는다. application은 먼저
`actorId -> targetPlayNodeRid`를 조회한 뒤 이 함수를 호출한다.

`Request<TRequest>(...)`는 session server가 현재 client stream으로 request를 보낼
때 사용한다. session gateway 내부 구현도 이 표면을 사용하고, application도 같은
방식으로 client에게 request를 보낼 수 있다.

```csharp
public interface IZLinkActorRelay
{
    IZLinkActorRelaySendCall Send<TMessage>(TMessage message);

    IZLinkActorRelayRequestCall Request<TRequest>(TRequest request);

    ValueTask DispatchAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorRelaySendCall
{
    IZLinkActorRelaySendCall WithPacketName(string packetName);

    ValueTask Async(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorRelayRequestCall
{
    IZLinkActorRelayRequestCall WithPacketName(string packetName);

    IZLinkActorRelayRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<Message> Async(
        CancellationToken cancellationToken = default);
}
```

`DispatchAsync`는 stream header의 request 정보를 보존해야 한다. client packet이
request이면 play server로 routed request를 보내고, reply가 오면 원래 client
request sequence로 session이 reply한다. client packet이 단순 message이면 routed
send로 전달한다.

`IZLinkActorRelaySendCall.WithPacketName(...)`과
`IZLinkActorRelayRequestCall.WithPacketName(...)`은 play server handler가 볼
inner packet name을 바꾼다. routed channel의 outer packet name은 항상
`ZLink.ActorRelay`로 고정된다.

## 7. Actor Relay Envelope

session server가 play server로 보내는 actor relay는 framework internal envelope를
사용한다. body는 별도 `Message` part로 유지하고, envelope에는 routing과 reply에
필요한 최소 metadata만 넣는다.

```csharp
public readonly record struct ZLinkActorRelayEnvelope(
    string ActorId,
    ZlinkStreamHeader StreamHeader,
    bool ExpectsReply);
```

wire 구성은 구현체가 정하지만, 의미는 아래와 같이 고정한다.

| field | 의미 |
|-------|------|
| `ActorId` | relay 대상 actor이자 player ID |
| `StreamHeader` | client가 보낸 원본 stream header |
| `ExpectsReply` | 원본 packet이 request인지 여부 |
| `Body` | 원본 client body `Message` |

`StreamHeader`에는 원본 request sequence가 들어 있어야 한다. play server는 이 값을
직접 해석하지 않아도 되지만, session server가 client에게 reply할 때 같은 sequence를
사용할 수 있어야 한다.

## 8. Play Server 표면

play server는 session server에서 relay된 packet을 받는다. relay packet의 domain
key는 `actorId`다.

```csharp
public readonly record struct ZLinkActorRelayMessage(
    ZLinkActorRelayEnvelope Envelope,
    Message Body);
```

play server handler는 `Envelope.ActorId`로 application actor registry를 조회하거나
actor를 생성한다. framework가 actor 위치 registry를 대신 들지는 않는다. `actorId`는
message와 context에 중복해서 넣지 않고 envelope에서만 읽는다.

```csharp
public interface IZLinkSessionProxyHandler
{
    ValueTask<Message?> HandleAsync(
        IZLinkSessionProxyContext context,
        ZLinkActorRelayMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionProxyContext
{
    RoutingId SourceSessionNodeRid { get; }

    IZLinkSessionGateway SessionGateway { get; }
}
```

`SourceSessionNodeRid`는 이 relay를 보낸 session node의 `RoutingId`다. play server가
즉시 client에게 응답성 push를 보내야 할 때는 이 값을 사용할 수 있다. 다만 장기적인
client 위치는 application의 `actorId -> session node RoutingId` 저장소를 기준으로
다시 조회하는 편이 안전하다. client가 다른 session server로 다시 연결되었을 수
있기 때문이다.

`HandleAsync(...)`의 반환값은 원본 client packet이 request일 때만 사용한다.
`message.Envelope.ExpectsReply`가 `true`이면 handler는 client에 돌려줄 body를
`Message`로 반환해야 한다. 이때 framework는 반환된 `Message`를 client reply로
전송한 뒤 dispose한다. `ExpectsReply`가 `true`인데 handler가 `null`을 반환하면
framework는 `ActorRelayFailed` error reply를 보낸다. `ExpectsReply`가 `false`이면
반환값은 무시한다.

## 9. Play To Client Gateway

play server가 client에게 메시지를 보낼 때도 공개 키는 `actorId`다. 다만 실제
transport target으로는 현재 actor가 연결된 session node의 `RoutingId`가 필요하다.
이 값은 application이 관리하는 위치 저장소에서 얻는다.

```csharp
public interface IZLinkSessionGateway
{
    IZLinkSessionGatewaySendCall SendToActor<TMessage>(
        string routerChannelId,
        RoutingId targetSessionNodeRid,
        string actorId,
        TMessage message);

    IZLinkSessionGatewayRequestCall RequestActor<TRequest>(
        string routerChannelId,
        RoutingId targetSessionNodeRid,
        string actorId,
        TRequest request);
}

public interface IZLinkSessionGatewaySendCall
{
    IZLinkSessionGatewaySendCall WithPacketName(string packetName);

    ValueTask Async(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionGatewayRequestCall
{
    IZLinkSessionGatewayRequestCall WithPacketName(string packetName);

    IZLinkSessionGatewayRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(
        CancellationToken cancellationToken = default);
}
```

이 API에서 `actorId`는 domain key이고, `routerChannelId`는 router 연결망 ID이며,
`targetSessionNodeRid`는 전달 대상이다. `sessionId`는 session gateway API에 나오지
않는다.

session server는 gateway message를 받으면 `actorId -> stream` binding을 조회해서
현재 client stream으로 보낸다. binding이 없으면 application이 처리할 수 있는
명확한 오류를 돌려준다. 예를 들어 client가 이미 끊겼거나 다른 session node로
이동한 경우다.

session gateway도 framework internal envelope를 사용한다.

```csharp
public readonly record struct ZLinkSessionGatewayEnvelope(
    string ActorId,
    string PacketName,
    bool ExpectsReply);
```

wire 구성은 구현체가 정하지만, 의미는 아래와 같이 고정한다.

| field | 의미 |
|-------|------|
| `ActorId` | client stream을 찾기 위한 actor이자 player ID |
| `PacketName` | client stream handler가 볼 inner packet name |
| `ExpectsReply` | play server가 client reply를 기다리는지 여부 |
| `Body` | client stream으로 보낼 body `Message` |

`SendToActor(...)`는 session server에 one-way routed send를 보낸다. 이 경우 session
server에 binding이 없으면 framework는 runtime event와 log를 남기고 packet을
버린다. one-way send는 원격 NACK을 호출자에게 돌려주지 않는다.

`RequestActor(...)`는 session server에 routed request를 보낸다. session server에
binding이 없으면 `ActorSessionNotBound` 오류 reply를 돌려준다. binding이 있으면
session server는 client stream으로 request를 보내고, client reply를 받은 뒤 play
server의 request에 reply한다.

`IZLinkSessionGatewaySendCall.WithPacketName(...)`과
`IZLinkSessionGatewayRequestCall.WithPacketName(...)`은 client stream으로 보낼
inner packet name을 바꾼다. routed channel의 outer packet name은 항상
`ZLink.SessionGateway`로 고정된다.

session context의 `Request<TRequest>(...)`는 아래 call builder를 반환한다.

```csharp
public interface IZLinkSessionRequestCall
{
    IZLinkSessionRequestCall WithPacketName(string packetName);

    IZLinkSessionRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(
        CancellationToken cancellationToken = default);
}
```

`Request<TRequest>(...).Async<TReply>(...)`는 현재 client stream에 새 request
sequence를 할당한다. reply는 packet name이 아니라 그 sequence로만 맞춘다.

## 10. Request/Reply 규칙

relay 경로에서 request/reply를 메시지 이름으로 맞추면 안 된다. 같은 메시지 이름의
request가 동시에 여러 개 있을 수 있기 때문이다.

규칙은 아래와 같다.

- client가 보낸 request의 sequence는 session server가 보존한다.
- session server가 play server로 보낸 routed request는 별도 transport request일 수
  있지만, 완료되면 원래 client request sequence로 reply한다.
- play server가 session gateway로 request를 보낼 때도 gateway transport의 request
  sequence를 사용한다.
- session server가 session gateway request를 client stream request로 바꿀 때는
  client stream용 새 sequence를 할당하고, client reply를 받은 뒤 원래 gateway
  request에 reply한다.
- `WithPacketName` 같은 이름 override는 decode/handler 선택을 위한 값이지,
  request/reply 상관관계를 나타내는 값이 아니다.

구현 기준은 아래와 같다.

1. client request를 받은 session runtime은 원본 `ZlinkStreamHeader`와 body
   `Message`를 보존한다.
2. `OpenActorRelay(...).DispatchAsync(...)`는 원본 packet이 request이면 routed
   request를 만들고, 원본 packet이 message이면 routed send를 만든다.
3. routed request가 성공하면 session runtime은 원본 stream request sequence로
   client에게 reply한다.
4. routed request가 timeout되거나 handler 오류를 받으면 session runtime은 같은
   원본 stream request sequence로 error reply를 보낸다.
5. client 연결이 이미 끊긴 경우 session runtime은 reply를 보내지 않고, pending
   request를 정리한 뒤 runtime event를 남긴다.

error reply payload는 아래 공통 구조를 사용한다.

```csharp
public readonly record struct ZLinkRelayError(
    string Code,
    string Message);
```

초기 오류 코드는 아래 세 가지를 최소로 둔다.

| code | 의미 |
|------|------|
| `ActorRelayTimedOut` | play server 응답 대기 시간이 초과됨 |
| `ActorRelayFailed` | play server handler 또는 routed transport 오류 |
| `ActorSessionNotBound` | session server에서 `actorId` binding을 찾지 못함 |

## 11. Session Binding Runtime

session server는 `BindActorAsync(...)`가 호출된 session만 client-facing packet을 받을
수 있게 한다. binding table은 session node runtime 안에 둔다.

```text
actorId -> active session runtime
```

동일한 session node 안에서 같은 `actorId`가 다시 bind되면 새 session이 이전
session을 대체한다. 구현체는 이전 session에 대해 더 이상 gateway message를 보내지
않아야 한다. 이전 session을 즉시 close할지, application에게 disconnect message를
올릴지는 stream session 정책에 따른다.

`UnbindActorAsync(...)`는 현재 session이 소유한 actor binding만 제거한다. 이미 새
session이 같은 `actorId`를 소유하고 있으면 오래된 session의 unbind 호출은 새 binding을
지우면 안 된다.

## 12. Target 없는 Actor Relay 금지

아래 형태의 API는 이 초안에서 제공하지 않는다.

```csharp
RelayToActor(string actorId, object message);
```

이 API는 framework가 `actorId -> play node RoutingId` 위치 저장소를 소유한다는
뜻으로 읽힌다. 하지만 위치 저장과 migration 정책은 application 책임이다. 따라서
framework API는 항상 `targetNodeRid`를 받거나, application이 명시적으로 제공한
location resolver를 통해 target을 얻는 형태여야 한다.

초기 구현은 resolver 주입보다 명시적 target 인자를 우선한다.

```csharp
OpenActorRelay(string routerChannelId, RoutingId targetPlayNodeRid, string actorId);
SendToActor(string routerChannelId, RoutingId targetSessionNodeRid, string actorId, message);
```

## 13. 회귀 테스트 항목

구현이 끝나면 최소한 아래 동작은 자동 테스트로 고정해야 한다.

- 같은 `packetName`을 가진 routed request 여러 개가 동시에 진행되어도 reply가
  request sequence 기준으로 각각의 caller에 돌아간다.
- 서로 다른 `routerChannelId`에 같은 `packetName` handler를 등록할 수 있다.
- 같은 `routerChannelId + kind + packetName`에 handler를 두 번 등록하면 startup이
  실패한다.
- session server가 client request를 actor relay로 전달할 때 원본 client request
  sequence를 보존하고, play server reply를 같은 sequence로 client에게 돌려준다.
- actor relay timeout은 `ActorRelayTimedOut` error reply로 변환된다.
- play server가 session gateway request를 보낼 때 session server는 client stream에
  새 sequence를 할당하고, client reply를 원래 gateway request에 돌려준다.
- session gateway request에서 `actorId` binding이 없으면 `ActorSessionNotBound`
  error reply가 돌아간다.
- 같은 session node에서 같은 `actorId`를 새 session이 다시 bind하면 이전 session의
  unbind가 새 binding을 지우지 않는다.
- one-way `SendToActor(...)`에서 binding이 없으면 caller에게 성공 reply를 꾸며 내지
  않고 runtime event와 log만 남긴다.
- routed `SendTo(...).Async(...)`가 temporary backpressure를 만나도 caller thread를
  block하지 않고, socket ready 이후 pending send를 완료한다.
- routed `RequestTo(...).Async<TReply>(...)`는 request packet submit 단계에서
  `SendTo(...).Async(...)`와 같은 backpressure 경로를 사용하고, send 실패나 취소 시
  pending request를 제거한다.
- `RequestTo(...).WithTimeout(...)`은 reply 대기 시간만 제한한다. send 단계의
  backpressure timeout은 channel 또는 socket의 `SendTimeout` 옵션으로 검증한다.
- pending routed send queue가 high water mark에 도달하면 caller thread를 block하지
  않고 async 대기, send timeout, cancellation 중 하나로 완료된다.
- socket ready callback이 들어오면 pending routed send를 batch로 drain하고, 같은
  frame을 중복 전송하지 않는다.

## 14. 구현 순서

구현은 아래 순서로 진행한다.

1. framework backend에 router-to-router send/request wrapper를 추가한다.
2. `AddRoutedChannel(...)` builder와 routed channel runtime을 추가한다.
3. `IZLinkRoutedClient`와 routed call builder를 추가한다.
4. routed inbound dispatcher와 routed handler registry를 추가한다.
5. session binding runtime을 추가한다.
6. `OpenActorRelay(...)`와 actor relay envelope 처리를 추가한다.
7. `IZLinkSessionGateway`와 session gateway envelope 처리를 추가한다.
8. request/reply sequence 보존과 error reply regression test를 추가한다.

이 순서에서 1번부터 4번까지는 session gateway와 독립적인 routed channel 기능이다.
5번부터 8번까지는 session gateway와 actor relay 기능이다.

## 15. 비차단 결정

아래 항목은 구현 중 이름을 더 다듬을 수 있지만, 기능 분해를 막지는 않는다.

- `OpenActorRelay` 이름을 유지할지, `OpenSessionProxy` 같은 이름으로 바꿀지
- `IZLinkSessionGateway`를 DI service로만 제공할지, context에서도 가져오게 할지
- session gateway가 사용할 `routerChannelId` 기본값을 옵션으로 둘지
