<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: 기능 맵 — 무엇을, 얼마나 쉽게, 언제](../../../../dotnet/guide/04-feature-map.ko.md) | [다음: ZLink Framework ASP.NET Core Channel Messaging](01-system-structure.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[.NET 묶음](../../../../dotnet/README.ko.md) | [channel](01-system-structure.ko.md) | [SPOT](01-system-structure.ko.md) | [STREAM](01-system-structure.ko.md) | [Monitoring](01-system-structure.ko.md) | [Location](01-system-structure.ko.md)

# ZLink Framework .NET Interface Catalog

## 1. 목적

이 문서는 한 가지 역할만 맡는다. `.NET` `ZLink Framework` 가 노출하는 **모든
공용 interface 와 attribute 정의** 를 한곳에 모아 두는 것이다. 즉 카탈로그
역할이다.

다른 문서에서 interface 를 참조할 때는 항상 이 문서를 기준으로 삼는다.

Spot Actor Join / Transfer 관련 interface도 이 문서에 기록된 정식 계약이며,
그 동작 의미는 [공통 스펙](../../23-spot-actor.ko.md)을 따른다. 구현이나 contract test가
이 시그니처와 다르면 계약 불일치로 처리한다.

사용 예시나 프로그래밍 모델 설명은 여기 넣지 않는다. 실제 사용법은 아래
문서들을 참고한다.

- 서버 간 messaging 프로그래밍 모델 →
  [system-structure.ko.md](01-system-structure.ko.md)
- 서버 간 messaging 샘플 →
  [.NET channel 가이드](../../../../dotnet/guide/05-channel-messaging.ko.md)
- SPOT 통합 →
  [system-structure.ko.md](01-system-structure.ko.md)
- SPOT 샘플 →
  [.NET SPOT 가이드](../../../../dotnet/guide/06-spot.ko.md)
- STREAM 통합 →
  [system-structure.ko.md](01-system-structure.ko.md)
- STREAM 샘플 →
  [.NET STREAM 가이드](../../../../dotnet/guide/09-stream.ko.md)
- Location 통합 →
  [system-structure.ko.md](01-system-structure.ko.md)

## 2. 인터페이스 전체 목록

| 분류 | 인터페이스 | 역할 | section |
|------|-----------|------|---------|
| context | `IZLinkHandlerContext` | 모든 handler context의 공통 기반 | 3.1 |
| context | `IZLinkSpotContext` | SPOT handler context 기반. spot identity(`SpotRid`/`NodeRid`), packet/subscribe/timer 등록과 channel 호출 표면 | 3.2 / 4.3.1 |
| handler | `IZLinkRequestHandler<TRequest, TResponse>` | request-response handler | 4.1 |
| handler | `IZLinkSendHandler<TMessage>` | one-way send handler | 4.2 |
| handler | `IZLinkRouteSendHandler<TMessage>` | routed channel one-way send handler | 4.2.1 |
| handler | `IZLinkRouteRequestHandler<TRequest, TReply>` | routed channel request-response handler | 4.2.1 |
| handler | `IZLinkPublishHandler<TMessage>` | pub/sub publish handler | 4.3 |
| handler | `IZLinkSpotPacketHandler<TSpot, TMessage>` | SPOT one-way packet handler | 4.3.1 |
| handler | `IZLinkSpotRequestHandler<TSpot, TRequest, TReply>` | SPOT request-response handler | 4.3.1 |
| handler | `IZLinkSpotSubscriptionHandler<TSpot, TEvent>` | SPOT subscription handler | 4.3.1 |
| handler | `IZLinkSpotTimerHandler<TSpot>` | SPOT lifecycle timer handler | 4.3.1 |
| handler | `IZLinkSession` | stream session lifecycle + session packet callback | 4.4 |
| context | `IZLinkSessionContext` | stream session identity, client 응답, actor binding, close 제어 | 4.4 |
| context | `IZLinkSessionClient` | session에서 client stream으로 send/reply | 4.4 |
| context | `IZLinkSessionActors` | session에서 actor handle bind와 lookup 수행 | 4.4 |
| value | `IZLinkSessionActor` | session이 actor dispatch target으로 보유한 handle | 4.4.1 |
| handler | `IZLinkActor` | actor runtime 안에서 생성되는 application actor | 4.4.1 |
| manager | `IZLinkActorManager` | actor id와 actor type으로 actor 생성, 조회, 재사용 | 4.4.1 |
| context | `IZLinkActorContext` | actor 상태 조회와 spot join 호출 | 4.4.1 |
| context | `ZLinkSpotActorSendContext` / `ZLinkSpotActorRequestContext` | Spot actor handler 실행 context. packet metadata 와 request reply 옵션을 제공한다 | 4.4.2 |
| handler | `IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>` / `IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>` | Entry Spot actor message handler | 4.4.2 |
| handler | `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>` / `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` | user Spot actor message handler | 4.4.2 |
| value | `ZLinkMessageMetadata` | actor/bound session call에 전달되는 metadata snapshot | 4.4.2 |
| policy | `IZLinkMessageMetadataPolicy` | metadata forwarding 허용 여부 | 4.4.2 |
| factory | `IZLinkActorFactory` | actor type별 actor 생성 | 4.4.1 |
| lifecycle | `IZLinkSpot<TActor>.OnActorJoinAsync(...)` | user Spot에 actor가 join할 때 호출되는 admission callback. actor instance가 아니라 actor id를 받는다 | 4.4.1 |
| lifecycle | `IZLinkActorTransferAdapter<TActor>` | remote actor transfer에서 actor state를 선택적으로 `ZLinkMessage`로 전달하고 target actor를 materialize하는 actor type별 adapter | 4.4.1 |
| client | `IZLinkBoundSession` | 현재 actor -> 현재 client session 호출 | 5.6 |
| resolver | `IZLinkSpotHandleResolver` | spot rid에서 메시징 handle 조회 | 5.7 |
| handler | `IZLinkRuntimeEventHandler<TEvent>` | runtime monitoring event handler | 10.3 |
| lifecycle | `IZLinkSpot` | spot lifecycle registration base | 4.3.1 |
| stream | `IZLinkStream` | stream I/O와 peer 식별 | 4.4 |
| value | `ZLinkStreamSessionError` | stream session error category enum | 4.4 |
| value | `ZLinkStreamError` | stream error detail + errno helper | 4.4 |
| value | `ZLinkSocketEventKind`, `ZLinkSocketEvent` | socket runtime event | 10.3 |
| value | `ZLinkSpotEvent` sealed records | spot runtime event | 10.3 |
| options | `IZLinkMonitoringOptions` | runtime monitoring source 등록 옵션 | 10.3 |
| options | `IZLinkDispatchOptions` | unhandled/diagnostics configuration | 4.4.3 |
| options | `IZLinkCodecRegistryBuilder` | codec registry builder | 6.1 |
| serializer | `IZLinkMessageSerializer` | `Message` payload 직렬화/역직렬화 | 4.5 |
| client | `IZLinkChannelClient` | 일반 client-server channel request/send outbound client | 5.1 |
| client | `IZLinkSpotOutbound` | SPOT outbound client | 5.2 |
| client | `IZLinkRouteClient` | route mesh channel 로 target node 호출 | 5.2.1 |
| client | `IZLinkSpotPublisherClient` | spot channel publish client | 5.3 |
| client | `IZLinkFanoutClient` | pub/sub fanout publish client | 5.4 |
| builder | `IZLinkFrameworkOptions` | framework 등록 루트 builder | 6.1 |
| builder | `IZLinkClientServerChannelBuilder` | client-server channel 등록 builder | 6.1 |
| builder | `IZLinkFanoutChannelBuilder` | fanout (pub/sub) channel 등록 builder | 6.1 |
| builder | `IZLinkRouteMeshChannelBuilder` | route mesh channel 등록 builder | 6.1 |
| builder | `IZLinkStreamNodeBuilder` | STREAM node 등록 builder | 6.1 |
| builder | `IZLinkSpotNodeBuilder` | SPOT node 등록 builder | 6.3 |
| runtime | `IZLinkEndpointConnections` | capability별 manual 연결 제어 표면 | 6.2 |
| management | `IZLinkSpotManager` | spot 인스턴스 생성/종료 | 6.3 |
| timer | `IZLinkTimer` | timer handle | 7 |
| filter | `IZLinkHandlerFilter` | handler 전후 공통 처리 | 8 |
| filter | `ZLinkHandlerInvocation` | filter pipeline 호출 context | 8 |
| filter | `ZLinkHandlerDelegate` | filter pipeline next delegate | 8 |

## 3. Context 인터페이스

### 3.1 공통 context

모든 handler context 가 공유하는 최소 집합이다. 즉 어떤 종류의 handler 든
이만큼은 항상 받는다.

실제 구현에서는 transport[^transport] 별 부가 정보가 따로 있다. 그 부가
정보는 이 공통 context를 파생한 별도 context에 포함하는 형태로 노출된다.

```csharp
public interface IZLinkHandlerContext
{
    string? ChannelName { get; }
    string? PacketName { get; }
    string? ContentType { get; }
    CancellationToken ConnectionAborted { get; }
}
```

`IServiceProvider` 는 handler context 에 넣지 않는다. handler 안에서 서비스가
필요하면 context 에서 service locator 방식으로 꺼내 쓰지 않고, handler class 의
생성자 주입(constructor injection)으로 받는다.

### 3.2 파생 context

handler 종류마다 받아야 하는 부가 정보가 다르다. 그 차이를 공통 context 에
다 우겨 넣지 않고, 종류별로 별도 context 타입을 두어 노출한다.

아래 표는 어떤 handler 가 어떤 context 타입을 받는지, 그 context 가 공통
필드 외에 어떤 정보를 더 포함하는지 정리한 것이다.

| context 타입 | 사용처 | 추가 정보 |
|-------------|--------|----------|
| `ZLinkRequestContext` | request-response handler | 공통 context 필드만 사용한다 |
| `ZLinkSendContext` | one-way send handler | 공통 context 필드만 사용한다 |
| `ZLinkPublishContext` | publish handler | topic, source |
| `ZLinkRouteSendContext` | routed channel send handler | source routing id, router channel id |
| `ZLinkRouteRequestContext` | routed channel request handler | source routing id, router channel id |
| `ZLinkSpotActorRequestContext` | SPOT / Entry Spot actor request handler | `Metadata`, `Reply`(`ZLinkSpotActorReplyOptions`) |
| `ZLinkSpotActorSendContext` | SPOT / Entry Spot actor send handler | `Metadata` |

일반 SPOT packet/request/subscription/timer handler 는 별도 per-call context 타입을
받지 않는다. handler 는 `(TSpot spot, 메시지, CancellationToken)` 형태로 spot 인스턴스와
메시지를 직접 받고, spot identity 는 아래의 `IZLinkSpotContext` 로 조회한다. per-call
context 타입은 actor packet handler(위 두 타입)와 channel handler 계열에만 적용된다.

`SPOT` 객체 안에서는 외부 lookup 과 별개로, 현재 spot 자신의 identity 도
조회할 수 있어야 한다. 이 문서에서는 별도의 `Self` wrapper 를 두지 않는다.
대신 SPOT 생성자에서 받는 `IZLinkSpotContext` 에 `SpotRid`, `NodeRid` 를 직접
노출한다.

여기서 두 가지 context 의 역할이 다르다는 점에 유의한다. handler 호출마다
호출과 함께 전달되는 `ZLinkRequestContext`, `ZLinkSendContext`, `ZLinkPublishContext` 는
"이번 호출 한 건"에 대한 정보다. 반면 SPOT 객체가 보유한
`IZLinkSpotContext` 는 "이 spot 인스턴스 전체"에 대한 정보다.

## 4. Handler 인터페이스

이 절은 실제로 메시지를 받아 처리하는 handler interface 들을 모은다.
request / send / publish / SPOT / session / actor 처럼 흐름이 다른 handler
들이 각각 어떤 모양인지 정의한다.

### 4.1 request-response handler

요청 하나에 응답 하나가 대응하는 handler 다. 즉 호출자가 답을 받기 위해
기다리는 형태다.

```csharp
public interface IZLinkRequestHandler<in TRequest, TResponse>
{
    ValueTask<TResponse> HandleAsync(
        TRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken);
}
```

- `TRequest` 는 이미 decode 된 payload 다.
- `TResponse` 도 framework 가 encode 할 typed 결과다.
- raw multipart header 는 인자로 넘기지 않는다.
- 이 interface 를 구현한 class 는 `ZLinkHandlerScanner` 가 attribute 없이도
  자동으로 endpoint 로 등록한다. 즉 attribute(`[ZLinkRequest]`) 가 붙은
  메서드와, interface 구현 방식 두 가지 모두 지원된다.

### 4.2 send handler

응답을 돌려주지 않는 one-way 전송을 처리하는 handler 다. 즉 호출자는 답을
기다리지 않고, 메시지를 한 번 보내는 것으로 끝난다.

```csharp
public interface IZLinkSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkSendContext context,
        CancellationToken cancellationToken);
}
```

이 interface 를 구현하기만 하면 `ZLinkHandlerScanner` 가 attribute 없이도
endpoint 로 자동 등록해 준다. 즉 attribute(`[ZLinkSend]`) 기반 등록과
interface 기반 등록을 모두 지원한다.

### 4.2.1 routed channel handler

routed channel(`AddRouteMeshChannel`) 이 수신하는
메시지를 처리하는 handler 다.

일반 channel handler 와 한 가지 차이가 있다. source `RoutingId` 를 포함한
라우팅 정보까지 context 로 함께 노출한다는 점이다.

```csharp
public interface IZLinkRouteSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkRouteSendContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkRouteRequestHandler<in TRequest, TReply>
{
    ValueTask<TReply> HandleAsync(
        TRequest request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken);
}

public sealed class ZLinkRouteSendContext : ZLinkHandlerContext
{
    public string RouterChannelId { get; }
    public RoutingId SourceNodeRid { get; }
}

public sealed class ZLinkRouteRequestContext : ZLinkHandlerContext
{
    public string RouterChannelId { get; }
    public RoutingId SourceNodeRid { get; }
}
```

routed channel handler 등록은 transport builder 가 책임진다. 구체적으로는
`AddSendHandler<THandler>()`, `AddRequestHandler<THandler>()` 처럼 handler 타입만
지정하는 메서드나, message/reply 타입을 함께 지정하는 명시적 overload 를 통해
이루어진다.
자세한 내용은 §6.1 의 `IZLinkRouteMeshChannelBuilder` 를 참고한다.

### 4.3 publish handler

pub/sub 로 publish 된 메시지를 처리하는 handler 다.

이름 규칙에는 의도가 있다. producer 쪽 동사
(`IZLinkFanoutClient.Publish(...)`) 에 맞추어, `Request` / `Send` /
`Publish` 세 가지 표면이 같은 패턴으로 읽히도록 정렬한 것이다.

payload 자체는 굳이 이 패턴에 맞출 필요가 없다. `*Event` 처럼 의미가
드러나는 이름을 그대로 사용해도 된다.

```csharp
public interface IZLinkPublishHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkPublishContext context,
        CancellationToken cancellationToken);
}
```

topic 이나 pattern 정보가 필요해도 별도의 `Topic` handler 이름을 늘리지
않는다. 대신 `ZLinkPublishContext` 안에서 읽어 가는 방식을 따른다.

이 interface 를 구현하기만 하면 `ZLinkHandlerScanner` 가 attribute 없이도
endpoint 로 자동 등록해 준다. 즉 attribute(`[ZLinkPublish]`) 기반과
interface 기반을 모두 지원한다.

### 4.3.1 SPOT lifecycle callback 과 handler

이 절은 SPOT 객체의 lifecycle 표면과, 그 SPOT 안에서 동작하는 handler
종류들을 정의한다.

framework 계약은 다음과 같다. `SpotNode.CreateSpot()`로 만든
low-level `Spot` 위에, application 친화적인 lifecycle 을 한 겹 얹는다.

SPOT 객체는 Actor 와 같은 원칙을 따른다. 즉 callback 표면과 실행 context
표면을 분리해 둔다. 샘플과 wrapper 문서가 공통으로 사용하는 최소 표면은
다음과 같다.

```csharp
public interface IZLinkSpot
{
    IZLinkSpotContext Context { get; }

    void Configure()
    {
    }

    ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    ValueTask OnInitializeAsync(
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnClosingAsync(
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkSpotActorLifecycle<TActor>
    where TActor : IZLinkActor
{
    ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken);

    ValueTask OnJoinedActorAsync(
        TActor actor,
        CancellationToken cancellationToken);

    ValueTask OnLeaveActorAsync(
        TActor actor,
        CancellationToken cancellationToken);

    ValueTask OnDisconnectActorAsync(
        TActor actor,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkSpot<TActor> : IZLinkSpot, IZLinkSpotActorLifecycle<TActor>
    where TActor : IZLinkActor;

public interface IZLinkActorTransferAdapter<TActor>
    where TActor : IZLinkActor
{
    ValueTask<ZLinkMessage> TransferOutAsync(
        TActor actor,
        CancellationToken cancellationToken);

    ValueTask<TActor> TransferInAsync(
        string actorId,
        IZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken);
}

public interface IZLinkActorHandlerRegistry
{
    void AddHandler<THandler>()
        where THandler : class;

    void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;

}

public interface IZLinkSpotHandlerRegistry : IZLinkActorHandlerRegistry
{
    void AddPacket<THandler>()
        where THandler : class;

    void AddSubscribe<THandler>(
        string topic)
        where THandler : class;
}

public interface IZLinkSpotOutbound
{
    IZLinkSendCall SendToSpot<TMessage>(
        SpotHandle target,
        TMessage message);

    IZLinkRequestCall RequestToSpot<TRequest>(
        SpotHandle target,
        TRequest request);

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message);

    IZLinkSendCall SendToChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall RequestToChannel<TRequest>(
        string channelName,
        TRequest request);
}

public interface IZLinkSpotCommonContext
{
    RoutingId SpotRid { get; }
    RoutingId NodeRid { get; }

    IZLinkSpotHandlerRegistry Handlers { get; }
    IZLinkSpotOutbound Outbound { get; }

    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default)
        where THandler : class;

    IZLinkWorkerCall<TResult> RunWorker<TResult>(
        Func<CancellationToken, TResult> work);
}

public interface IZLinkSpotContext : IZLinkSpotCommonContext
{
    ValueTask LeaveActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask<bool> CloseAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkEntrySpot
{
    IZLinkEntrySpotContext Context { get; }

    void Configure()
    {
    }

    ValueTask OnInitializeAsync(
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnClosingAsync(
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkEntrySpot<TActor> : IZLinkEntrySpot,
    IZLinkSpotActorLifecycle<TActor>
    where TActor : IZLinkActor
{
    ValueTask OnCreateActorAsync(
        TActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkEntrySpotContext : IZLinkSpotCommonContext
{
    ValueTask DestroyActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);
}

// Context property는 안내용 convention 이 아니라 공개 계약이다. framework 는
// 객체를 생성할 때 현재 인스턴스에 맞는 context 를 생성자 인자로 넘기며,
// 생성된 객체가 그 context 를 그대로 노출하지 않으면 activation 을 실패시킨다.
// application 코드는 생성자에서 받은 값을 get-only property 에 보관한다.
public interface IZLinkSpotPacketHandler<TSpot, in TMessage>
    where TSpot : class
{
    ValueTask HandleAsync(
        TSpot spot,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRequestHandler<TSpot, in TRequest, TReply>
    where TSpot : class
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotSubscriptionHandler<TSpot, in TEvent>
    where TSpot : class
{
    ValueTask HandleAsync(
        TSpot spot,
        TEvent message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotTimerHandler<TSpot>
    where TSpot : class
{
    ValueTask HandleAsync(
        TSpot spot,
        ZLinkTimerTick tick,
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

public readonly record struct ZLinkSpotActorJoinResult(
    bool Accepted,
    ZLinkMessage? Reply)
{
    public static ZLinkSpotActorJoinResult Accept(ZLinkMessage? reply = null);
    public static ZLinkSpotActorJoinResult Reject(ZLinkMessage? reply = null);
}
```

`DestroyActorAsync`: Entry Spot에 있는 actor의 native actor ref, framework registry,
bound session mapping을 정리한다. user Spot에 있는 actor는 먼저 leave를 완료해야 하며,
이 정리 과정에서는 actor lifecycle callback을 호출하지 않는다.

session 종료가 곧 actor leave 나 actor destroy 를 뜻하지 않는다. session 종료 사실을
actor에 전달해야 하면 application이 대상 actor를 선택해 별도의 disconnect 알림을 보낸다.

`IZLinkActorTransferAdapter<TActor>` 등록은 remote transfer에서 직접 옮길 actor state가 있음을 나타낸다.
등록이 없으면 framework는 빈 `ZLinkMessage`를 보내고, target에서 기존 actor factory 또는 public actor
생성 경로로 `TActor` instance를 만든다.
custom adapter의 target method는 actor를 만들 때 사용할 `IZLinkActorContext`를 받는다. `.NET` actor는
factory가 받은 context를 그대로 노출해야 하므로, transfer adapter가 새 actor instance를 반환하는
경우에도 framework가 넘긴 context를 actor 생성자에 전달해야 한다.

actor join admission callback 의 request는 `ZLinkMessage`다. application은
필요한 DTO를 `Decode<T>()`로 얻는다. reply는 DTO 또는
`ZLinkMessage`로 `ZLinkSpotActorJoinResult`에 담아 반환한다. runtime은 등록된 codec
registry로 request와 reply를 encode/decode한다.

`Accepted` 가 `true` 이면 framework 는 actor join 을 commit 하고, commit 이 끝난 뒤
`OnJoinedActorAsync(...)` 를 호출한다. `Accepted` 가 `false` 이면 framework 는
join 을 거부하고 actor 위치를 바꾸지 않으며 `OnJoinedActorAsync(...)` 를 호출하지
않는다.

`OnCreateAsync(...)` 는 생성 요청이 넘긴 단일 `ZLinkMessage`를 spot 상태로
해석하고 생성 허용 여부를 돌려주는 단계다. framework가 새 spot 인스턴스를 만든
경우에만 호출된다.
`OnInitializeAsync(...)` 는 payload와 무관한 lifecycle 준비 단계다. timer 등록처럼
생성 메시지와 직접 관련 없는 준비 작업은 이 callback에서 수행한다.

새 spot을 만드는 경우 호출 순서는 `Configure()`, descriptor binding,
`OnCreateAsync(request, ...)`, `OnInitializeAsync(...)` 순서다. `OnCreateAsync(...)`가
reject를 반환하면 `OnInitializeAsync(...)`는 호출하지 않고 spot을 등록하지 않는다.
`GetOrCreateAsync(...)`가 이미 ready 상태인 spot을 반환하는 경우에는 새
`OnCreateAsync(...)`나 `OnInitializeAsync(...)`를 호출하지 않는다.
`CreateAsync<TSpot>()`처럼 create payload가 없는 편의 overload도 빈 `ZLinkMessage`로
`OnCreateAsync(...)`를 한 번 호출한다.

`Configure()` 는 호출 시점이 정해져 있다. SPOT 이 생성된 직후, descriptor
를 바인딩하기 전 시점에 단 한 번 호출된다.

다음 호출들은 이 `Configure()` 단계 안에서만 허용된다.

- `Context.Handlers.AddPacket(...)`
- `Context.Handlers.AddHandler(...)`
- `Context.Handlers.AddActorPacket(...)`
- `Context.Handlers.AddSubscribe(...)`

초기화가 끝난 뒤에 handler 를 추가하면 어떻게 될까. native subscription 과
dispatch table 의 의미가 어긋나게 된다. 그래서 framework 는 이 경우 예외를
던진다.

`AddHandler<THandler>(...)` 는 `THandler` 가 구현한 actor handler interface 를 보고
actor 타입, send/request/lifecycle 종류, packet 이름 기본값을 추론한다.
handler 가 여러 actor handler interface 를 구현해서 모호하면 명시적인
`AddActorPacket<THandler, TActor>(...)` 를 사용한다.

`AddActorPacket<THandler, TActor>(...)` 는 actor 타입을 호출 쪽에서 명시하고,
`THandler` 가 구현한 handler interface 를 보고 send 와 request 를 구분한다.

각 spot 종류별로 handler 를 선언하는 방법은 두 가지다.

- interface 방식: handler 클래스가 아래 interface 중 하나를 구현한다. 컴파일
  타임에 method 시그니처를 강하게 확인할 수 있다.
- attribute 방식: handler 클래스의 public instance method 에
  `[ZLinkSpotActorRequest]` 같은 attribute 를 붙인다. 한 클래스에 여러 역할을
  모을 수 있지만, method 시그니처 검증은 startup validation 단계에서 수행된다.

interface 방식에서 spot 종류별로 구현해야 하는 interface 는 다음과 같다.

- user Spot 의 경우: `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>`
  또는 `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` 중
  하나를 구현해야 한다.
- Entry Spot 의 경우: `IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>`
  또는 `IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>` 중
  하나를 구현해야 한다.

하나의 handler 타입이 두 개 이상의 actor packet interface 를 구현하거나, 두 개
이상의 actor handler attribute method 를 선언하면 의도가 모호하다. framework 는
이를 startup validation 단계에서 오류로 처리한다.

#### 4.3.1.1 Entry Spot actor handler 등록

Entry Spot 은 기본 실행 문맥이다. 즉 session 에서 막 생성된 actor 가 가장
먼저 도착하는 자리다.

이 단계에서 처리할 actor message 는 `IZLinkEntrySpot.Configure()` 안에서
등록한다. 구체적으로는
`IZLinkEntrySpotContext.Handlers.AddHandler<THandler>()` 를 호출한다.

Entry Spot handler 는 Entry Spot 인스턴스, actor, payload 를 함께 받는다.
그래야 handler 가 Entry Spot 이 가진 입장 처리 상태나 helper 메서드를 직접
사용할 수 있고, user Spot handler 와 같은 호출 모양을 유지할 수 있다.

```csharp
public sealed class PlayerEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<AuthenticateHandler>();
        Context.Handlers.AddHandler<JoinMatchHandler>();
        Context.Handlers.AddHandler<PlayerEntryJoinedHandler>();
        Context.Handlers.AddHandler<PlayerEntryLeftHandler>();
    }
}
```

Entry Spot의 actor packet handler는 위에서 고정한
`IZLinkEntrySpotActorSendHandler` 또는 `IZLinkEntrySpotActorRequestHandler`를 구현한다.
actor 연결 해제 callback은 `IZLinkSpotActorLifecycle<TActor>`의
`OnDisconnectActorAsync(...)`를 구현한다.

같은 의미를 attribute 방식으로 쓰면 다음과 같다.

```csharp
public sealed class JoinMatchHandler
{
    [ZLinkSpotActorRequest]
    public ValueTask<JoinMatchRes> HandleAsync(
        PlayerEntrySpot entrySpot,
        PlayerActor actor,
        JoinMatchReq request,
        CancellationToken cancellationToken)
    {
        // ...
    }
}
```

Entry Spot 에서 `AddHandler(...)` 로 등록한 actor packet handler 는 처리 범위가
한정된다. 해당 actor 가 user Spot 에 join 하기 전에 도착한 message 만
처리한다.

user Spot 에 join 된 이후의 actor message 는 뒤에서 설명할 user Spot
registry 가 담당한다.

#### 4.3.1.2 user Spot actor handler 등록

user Spot 에 join 된 actor 의 message 는 `IZLinkSpot.Configure()` 안에서
등록한다. 구체적으로는
`IZLinkSpotContext.Handlers.AddHandler<THandler>()` 를 호출한다. actor 타입을 호출 쪽에서
명시해야 하거나 handler 가 여러 actor handler interface 를 구현하면
`AddActorPacket<THandler, TActor>()` 같은 명시적 등록 메서드를 사용한다.

user Spot handler 는 두 가지 객체를 함께 받는다. spot 객체와 actor 객체다.
역할 분담은 다음과 같다.

- room, game, stage 같은 실행 문맥 상태는 spot 쪽에서 읽는다.
- player 상태는 actor 쪽에서 읽는다.

```csharp
public sealed class MatchSpot(IZLinkSpotContext context) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<PlaceMarkHandler>();
        Context.Handlers.AddHandler<PlayerMatchJoinedHandler>();
        Context.Handlers.AddHandler<PlayerMatchLeftHandler>();
    }
}
```

user Spot의 actor packet handler는 4.3.1에서 고정한
`IZLinkSpotActorSendHandler` 또는 `IZLinkSpotActorRequestHandler`를 구현한다.

attribute 방식은 같은 parameter 순서를 사용한다.

```csharp
public sealed class PlaceMarkHandler
{
    [ZLinkSpotActorRequest]
    public ValueTask<PlaceMarkRes> HandleAsync(
        MatchSpot spot,
        PlayerActor actor,
        PlaceMarkReq request,
        CancellationToken cancellationToken)
    {
        // ...
    }
}
```

#### 4.3.1.3 actor join/leave lifecycle callback

actor 가 Entry Spot 또는 user Spot 에 들어오거나 빠져나간 직후의 후속
처리는 Spot 멤버 callback 으로 선언한다. `OnJoinedActorAsync(...)` 와
`OnLeaveActorAsync(...)` 는 기본 no-op 구현을 공개 계약으로 제공하지 않는다.
필요한 Spot 은 actor 타입을 구체화한 public instance method 를 직접 선언한다.
framework 는 Spot descriptor 를 바인딩할 때 이 method 를 찾아 callback 으로 사용한다.
기존 `return ValueTask.CompletedTask;` 형태의 기본 구현 API는 삭제 대상이다.

user Spot 에 actor 가 들어올 수 있는지를 판단하는 admission 처리는
`OnActorJoinAsync(...)` 로 선언한다. 이름을 분리해서 join 요청 처리와 join commit
이후 callback 을 코드에서 구분한다.

Entry Spot lifecycle callback 은 아래 method 를 Entry Spot class 에 선언한다.

```csharp
public ValueTask OnJoinedActorAsync(
    PlayerActor actor,
    CancellationToken cancellationToken);

public ValueTask OnLeaveActorAsync(
    PlayerActor actor,
    CancellationToken cancellationToken);
```

user Spot lifecycle callback 도 같은 method 이름을 사용한다.

```csharp
public ValueTask OnJoinedActorAsync(
    PlayerActor actor,
    CancellationToken cancellationToken);

public ValueTask OnLeaveActorAsync(
    PlayerActor actor,
    CancellationToken cancellationToken);
```

actor disconnected notification 은 별도 handler 를 등록하지 않는다. Spot 이
`IZLinkSpot<TActor>` 또는 `IZLinkEntrySpot<TActor>` 를 구현하고 필요한 경우
아래 callback 을 override 한다.

```csharp
public ValueTask OnDisconnectActorAsync(
    PlayerActor actor,
    CancellationToken cancellationToken);
```

Entry Spot 도 같은 callback 이름을 사용한다.

```csharp
public ValueTask OnDisconnectActorAsync(
    PlayerActor actor,
    CancellationToken cancellationToken);
```

generic Spot은 4.3.1의 `IZLinkSpotActorLifecycle<TActor>`를 상속한다.
`OnDisconnectActorAsync(...)`의 기본 구현은 아무 작업도 하지 않는다.

actor disconnected callback 은 join/leave lifecycle 과 별개다. session 이
끊겼다는 사실을 actor 에 알려야 할 때 application 이
session actor handle 의 `NotifyDisconnectedAsync(CancellationToken)` 을 호출하면 그 handle 이 가리키는 actor 에 대해 실행된다(대상 actor 를 인자로 받지 않는다). 이
callback 은 actor membership 을 바꾸지 않는다.

`OnJoinedActorAsync(...)` 와 `OnLeaveActorAsync(...)` 는 호출 시점이
정해져 있다. join/leave commit 이 끝난 뒤, 동일한 실행 문맥에서 호출된다.

이 callback 의 역할도 한정된다. admission 을 결정하는 hook 이 아니다.
commit 이후에 application 상태를 갱신하거나 알림을 발송하는, 후속 처리
단계다.

중복 등록 여부는 같은 registry 단위로 검사한다. Entry Spot registry 와 각
user Spot registry 는 서로 별개의 namespace 로 본다.

같은 registry 안에서 동일한 `actor type + packet kind + packet name` 조합이
둘 이상 등록되면, framework 는 이를 startup validation 오류로 처리한다.
Spot lifecycle callback 도 같은 규칙을 따른다. 즉 같은 Spot 안에서 동일 actor
타입에 대해 하나씩만 허용한다.

`IZLinkSpotContext` 가 노출하는 호출 표면들은 다음 역할을 한다.

- `Context.Outbound.SendToSpot(...)` 과 `Context.Outbound.RequestToSpot(...)` 은 현재 SPOT 의
  실행 문맥에서 다른 SPOT 으로 routed send/request 를 보낸다. target은
  `IZLinkSpotHandleResolver`가 만든 `SpotHandle`로 지정하며 실제 target node와 route
  channel은 handle 내부 상태다.
- `Context.Outbound.Publish(topic, ...)` 는 편의 함수다. 현재 SPOT 이 속한 active
  SPOT channel 에 publish 하기 위한 것이다.
- `Context.Outbound.SendToChannel(...)` 과 `Context.Outbound.RequestToChannel(...)` 은 현재 SPOT
  의 실행 문맥에서 channel client 를 호출한다.
- `Context.CloseAsync(...)` 는 현재 user Spot 을 정상 종료하도록 요청한다. 이미 join된
  actor 가 남아 있으면 `false` 를 반환하고 종료하지 않는다. packet handler 나 timer
  handler 안에서 호출한 경우에는 현재 callback 이 끝난 뒤 `OnClosingAsync(...)` 와
  native SPOT facade 정리가 이어진다.

`OnClosingAsync(...)` 의 호출 시점은 한정된다.
`IZLinkSpotManager.CloseAsync(...)` 또는 `IZLinkSpotContext.CloseAsync(...)` 로 SPOT 을
정상 종료할 때, 실행 문맥 안에서 호출된다.

이 콜백은 destructor 가 아니라는 점에 주의한다. 즉 host shutdown 이나
process 종료 시에 반드시 호출되는 것이 아니다.

다음 low-level `.NET` 바인딩 표면은 framework 구현이 사용하는 현재 공개 API다.
framework public contract 자체는 이 타입들을 application에 노출하지 않는다.

```csharp
public sealed class SpotNode : IDisposable, IAsyncDisposable
{
    public SpotNodePublisherOptions PublisherOptions { get; }
    public SpotNodeSubscriberOptions SubscriberOptions { get; }

    public Spot CreateSpot();
}

public sealed class Spot : IDisposable, IAsyncDisposable
{
    public void SetSubscription(string topicOrPattern);

    public TopicMessage? Subscribe(RecvFlags flags = RecvFlags.None);

    public SubscriptionEvent ReceiveSubscriptionEvent(
        RecvFlags flags = RecvFlags.None);

    public Received RecvRoute(RecvFlags flags = RecvFlags.None);

    // channel 이름 기반 호출 (framework route bridge 경유)
    public void SendToChannel(string channelName, ReadOnlyMemory<byte> payload);

    public Task<Received> RequestToChannelAsync(
        string channelName,
        ReadOnlyMemory<byte> payload,
        TimeSpan timeout = default,
        CancellationToken cancellationToken = default);

    // dispatch_info 기반 통합 dispatch callback
    public void OnDispatchEvent(Action<Spot, SpotDispatchInfo> handler);

    public void OnRouteReceive(Action<Received> handler);

    // CHANNEL_REPLY_READABLE dispatch 시 사용
    public void DrainChannelReplyFrom(object dealerSubject);
}

public sealed class Timer : IZlinkTimer
{
    public static Timer FromSpot(Spot spot);

    public void Start(TimeSpan interval, ulong repeatCount);

    public void Stop();

    public ulong? Recv(RecvFlags flags = RecvFlags.None);

    public void OnFire(Action<IZlinkTimer, ulong> handler);

    public void Close();
}
```

`SpotDispatchInfo` 는 managed 타입이다. core `zlink_spot_dispatch_info_t`
를 감싸며, `Event`, `SubjectKind`, `Subject` 를 노출한다.

```csharp
public readonly struct SpotDispatchInfo
{
    public SpotDispatchEvent Event { get; }
    public SpotDispatchSubjectKind SubjectKind { get; }
    public object? Subject { get; }  // dealer handle 등 backend subject
}

public enum SpotDispatchEvent
{
    SubscribeReadable    = 1,
    RouteReadable       = 2,
    TimerReadable        = 3,
    ChannelReplyReadable = 4,
    ActorReadable        = 5,
    ActorJoinReadable    = 6,
}

public enum SpotDispatchSubjectKind
{
    Spot          = 1,
    Timer         = 2,
    ChannelDealer = 3,
    Actor         = 4,
}
```

framework 의 dispatch loop 는 event 종류와 `Subject` 를 한꺼번에 처리한다.
예시는 다음과 같다.

```csharp
spot.OnDispatchEvent((s, info) =>
{
    switch (info.Event)
    {
        case SpotDispatchEvent.SubscribeReadable:
            /* s.Subscribe() 로 drain */
            break;
        case SpotDispatchEvent.RouteReadable:
            /* s.RecvRoute() 로 drain */
            break;
        case SpotDispatchEvent.ChannelReplyReadable:
            /* info.Subject 가 dealer handle */
            s.DrainChannelReplyFrom(info.Subject!);
            break;
    }
});
```

framework timer 는 이 dispatch enum 에 직접 묶이지 않는다. 동작 흐름은
다음과 같다. runtime 이 생성한 managed `.NET` timer 가 tick 을 만들고,
user Spot timer 는 그 tick 을 같은 spot execution context 안으로 enqueue 해서
timer handler 를 호출한다. Entry Spot timer 는 route packet, subscription,
worker completion과 같은 Entry Spot 직렬 실행 줄에 enqueue해서 호출한다.

`RequestToChannelAsync(...)` 의 completion 은 **항상 같은 spot execution
context 안에서** 실행된다. 즉 임의의 thread 에서 promise 를 직접 완료하지
않는다.

이 보장 덕분에 continuation 도 별도의 `SynchronizationContext` 설정 없이,
spot state 와 동일한 실행 규칙을 따르게 된다.

framework 의 `Context.AddTimer<THandler>(...)` 는 low-level native timer 를
직접 노출하는 표면이 아니다.

현재 잡혀 있는 방향은 다음과 같다. framework runtime 이 policy-aware managed
timer 를 만든다. user Spot timer 는 그 tick 을 **같은 spot execution context**
안으로 enqueue 해서 `IZLinkSpotTimerHandler<TSpot>.HandleAsync(...)` 를 호출한다.
Entry Spot actor packet 은 대상 actor mailbox 에서 처리하므로 timer 실행 줄 설명에
포함하지 않는다. Entry Spot timer 정합성은 actor packet dispatch 계약과 분리해서 다룬다.

`IZLinkTimer.CancelAsync()` 는 이 managed timer loop 를 중단하고 정리하는
고수준 handle 로 이해하면 된다.

#### 4.3.2 SPOT 실행 문맥 정책

이 절의 핵심은 한 가지다. **내부 구현 방식**이 아니라, **사용자에게 보이는
실행 계약**이다.

framework는 `Spot`을 단순한 recv helper로 보지 않는다. 같은 user
Spot 에 속한 handler 와, join 을 마친 actor 가 **하나의 spot execution
context** 에서 처리되는 표면으로 다룬다.

session 에서 actor 로 relay 되는 packet 의 처리 순서는 다음과 같다.

1. 먼저 대상 actor 의 순서 규칙을 통과한다.
2. 이후 actor 가 Entry Spot 에 있다면, Entry Spot registry handler 를
   actor 별 순서로 실행한다.
3. actor 가 user Spot 에 있다면, user Spot 실행 queue 로 넘겨 Spot 상태를
   보호한다.

사용자 관점에서의 공개 계약은 다음과 같다.

- 사용자는 `Recv(...)` 나 `Drain(...)` loop 를 직접 작성하지 않는다.
- 사용자는 고수준 표면만 사용한다. 예를 들어
  `Context.Handlers.AddPacket<THandler>(...)`,
  `Context.Handlers.AddSubscribe<THandler>(...)`,
  `Context.AddTimer<THandler>(...)`, stream attach 같은 것들이다.
- 같은 user Spot 에 속한 handler, timer handler, channel reply continuation
  은 framework 가 정의한 동일한 실행 문맥 규칙을 따른다.
- 이 계약이 유지되는 한, 사용자는 `SampleSpot.ActorCount` 같은 spot state
  를 handler 내부에서 자유롭게 다룰 수 있다.

따라서 사용자에게 노출되어야 하는 항목은 다음 정도다.

- handler 등록
- timer 등록
- stream attach
- spot state 접근 규칙

반면 아래는 모두 framework 내부 구현 영역으로 둔다.

- mailbox 사용 여부
- queue 개수
- single consumer task 운용 방식
- low-level callback을 internal work item으로 변환하는 방법

문서가 전달해야 할 핵심은 한 문장으로 정리된다. "framework 가 같은 `Spot`
상태를 동일한 실행 규칙으로 처리해 준다"는 점이다.

이는 사용자가 mailbox runtime 을 직접 소유하거나 관리한다는 의미가 아니다.

공개 계약은 내부 queue 구조를 고정하지 않고 handler 등록 모델과
session/actor 조합 모델만 노출한다.

### 4.4 stream session

이 절은 stream session 의 lifecycle 와, 그 위에서 packet 이 어떻게 흘러
들어오고 나가는지를 정의한다.

stream 은 framework Header 기반 packet path 를 session lifecycle 위에서
설명하는 방향을 기본으로 잡는다. 즉 `STREAM` application 표면은 별도의
`ZLinkStreamContext` 가 아니라, `IZLinkStream` 객체를 중심으로 보는 시각을
택한다.

```csharp
public interface IZLinkStream
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }

    bool Write(
        ZLinkMessage payload,
        SendFlags flags = SendFlags.None);

    ValueTask CloseAsync();
}

public enum ZLinkStreamSessionError
{
    Internal = 0,
    TransportError,
    // OnErrorAsync로 전달되지 않는다. handshake 실패는 runtime monitoring에만 남긴다.
    HandshakeFailed
}

public readonly record struct ZLinkStreamError(
    ZLinkStreamSessionError Error,
    ZLinkStreamDiagnostic? Diagnostic);

public readonly record struct ZLinkStreamDiagnostic(
    int NativeCode,
    string? Message);

public interface IZLinkSession
{
    IZLinkSessionContext Context { get; }

    void Configure()
    {
    }

    ValueTask OnConnectedAsync(CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(CancellationToken cancellationToken);

    ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken);

    ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionClient
{
    IZLinkSessionSendCall Send<TMessage>(TMessage message);

    IZLinkSessionReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkSessionActors
{
    IReadOnlyCollection<IZLinkSessionActor> Bound { get; }

    ValueTask<IZLinkSessionActor> BindAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkSessionActor> BindAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkSessionActor> BindOrGetAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);

    IZLinkSessionActor? Find(string actorId);
}

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

public interface IZLinkSessionContext
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }

    IZLinkSessionClient Client { get; }

    IZLinkSessionActors Actors { get; }

    IZLinkSessionHandlerRegistry Handlers { get; }

    ValueTask CloseAsync();
}

public interface IZLinkSessionSendCall
{
    IZLinkSessionSendCall Metadata(string key, string value);

    IZLinkSessionSendCall Compress();

    void Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall Metadata(string key, string value);

    IZLinkSessionReplyCall Compress();

    void Submit(CancellationToken cancellationToken = default);
}

```

session 구현체는 framework 가 생성자에 넘긴 `IZLinkSessionContext` 를
`Context` property 로 그대로 노출해야 한다. 이 규칙은 문서 가이드가 아니라
runtime 이 검증하는 계약이다.

`CloseAsync()` 는 현재 session 의 stream peer 연결을 서버 쪽에서 끊는
동작이다.

호출 시점은 다음과 같은 상황이다. 인증 실패, protocol 위반, idle timeout
처럼 더 이상 packet 을 받을 이유가 없는 경우다.

연결을 끊은 뒤의 session binding 정리는 framework 가 담당한다. 정리 기준은
`sessionId + bindingToken` 이다. 이 binding 상태는 public resolver 계약이
아니라, framework/core runtime 의 내부 상태다.

stream send/reply call은 `ZLinkMessage`를 stream 으로 보내는 submit 이다. 일반
application 코드는 가능한 한 `Context.Reply(...)`, `IZLinkBoundSession` 같은 framework
helper 를 사용한다.

session handler 에서 다른 channel 로 send/request 를 보내야 한다면
`IZLinkSessionContext` 가 아니라 DI 로 주입받은 `IZLinkChannelClient` 를 사용한다.
channel 호출은 현재 stream peer 로 나가지 않고, channel 이름에 맞는 framework
client socket 으로 나가기 때문이다.

backpressure 는 framework 내부에서 처리한다. actor 가 stream 을 직접 다루는
패턴이 아니라면, 현재 actor 의 client 는 `IZLinkBoundSession`, actor id 를
지정하는 service 는 `IZLinkBoundSession` 를 경유한다(actor-model §10 참고).

`OnErrorAsync(...)` 는 application handler 내부에서 발생한 예외를 받는
callback 이 아니다.

이 문서에서는 그 용도를 좁혀 둔다. `SocketMonitor` 로 관찰 가능한, session
과 연결 짓기 좋은 transport 오류만 `ZLinkStreamError` 로 다시 올려 보내는
용도로 제한한다.

session callback 실행 계약은 다음과 같이 고정한다.

- `IZLinkSession` 의 callback 은 native/socket callback 안에서 직접 호출
  하지 않는다.
- framework 가 session callback 을 managed task 로 넘긴 뒤, application
  callback 을 호출한다. 이 규칙의 의도는 명확하다. transport callback 이
  application 처리 시간, 예외, 재진입에 직접 묶이지 않도록 하기 위해서다.
- 같은 session 안에서는 `OnConnectedAsync(...)`, `OnDispatchAsync(...)`,
  `OnErrorAsync(...)`, `OnDisconnectedAsync(...)` 가 서로 병렬로 실행되지
  않는다. framework 는 같은 session 의 callback 순서를 보존한다. 즉 이전
  callback 이 끝난 뒤에야 다음 callback 을 호출한다.
- 같은 session 의 stream frame 도착 순서는 stream socket 이 보존한다.
  framework 는 이 도착 순서를 session 별 내부 실행 queue 로 이어받아,
  callback 순서로 변환해 준다. 이 queue 는 framework 내부 구현이다.
  application 이 별도로 session mailbox 를 만들거나 관리할 일은 없다.
- 서로 다른 session 의 callback 은 상호 독립이다. 즉 같은 session 안에서의
  직렬성만 보장한다. stream node 전체에 대한 전역 단일 실행 순서는
  보장하지 않는다.

여기서 `ZLinkStreamSessionError` 는 framework 가 1차로 노출하는 오류 분류
enum 이다.

이 분류만으로 부족한 경우에는 `Diagnostic` 안의 native detail 까지 함께
확인할 수 있어야 한다.

다만 `Diagnostic` 값은 항상 채워지는 것은 아니다. framework 가 늘 보장하는
필수 계약이 아니라, 현재 backend 가 제공할 수 있을 때에 한해 채워지는
optional detail 로 본다.

현재 방향은 다음과 같이 정리할 수 있다.

- header session
  - `OnDispatchAsync(...)`로 `ZLinkSessionDispatchContext`와 `ZLinkMessage` payload를
    받는다.
  - callback 안에서 payload 를 바로 읽거나 `IZLinkSessionActor.RelayAsync(...)` 로 넘길
    수 있다.
  - application 이 직접 만든 `Message` 를 raw `IZLinkStream.Write(...)` 에
    넘길 때는 framework 가 caller payload 를 소비하지 않는다. 호출자가 그
    `Message` 의 수명을 계속 책임진다.
  - stream에 응답을 보내거나 actor로 넘기는 동작은 `Context`를 통해 수행한다.
- 공통 lifecycle
  - `OnConnectedAsync(...)`
  - `OnDisconnectedAsync(...)`
  - `OnErrorAsync(...)`

이 interface 자체가 "`Spot` 이 session 타입을 정적으로 등록한다"는 의미는
아니다.

게임 room 같은 상위 모델에서는 더 자연스러운 구조가 따로 있다. session 이
먼저 독립적인 transport 객체로 만들어진다. 인증과 입장 절차가 끝난 뒤에야,
특정 `Spot` 또는 actor 에 귀속되는 구조다.

binding/framework 가 노출해야 할 표면은 다음과 같다. "`session` 을 어느
`Spot` 에 join 시키는가"라는 상위 조합 표면이다. `Spot` 자체가 session
타입을 직접 소유하는 고정 모델이 아니다.

이 문서는 recv loop 를 application 표면으로 직접 내보내지 않는다. 즉 두
가지 모델 중 두 번째를 기본으로 삼는다.

- 첫 번째: 사용자가 `Recv(...)` 로 drain loop 를 돌리는 모델.
- 두 번째 (기본): framework 가 dispatch 를 담당하고, application 은 Header
  기반 packet session 을 구현하는 모델.

또한 stream 핫패스에서는 메모리 할당을 최소화하는 것이 원칙이다. 즉
`Message.ToArray()` 같은 불필요한 추가 복사를 기본 사용법으로 두지 않는다.

session handler 는 raw `Message`나 codec별 protobuf/json decode helper를 직접
다루지 않는다. framework 는 `ZLinkMessage`를 전달하고, handler 는
`ZLinkMessage.Decode<T>()` 로 업무 DTO를 얻는다. codec 선택은 startup/options 에
등록된 codec registry 가 맡는다.

기존 방식 중 유지되는 것은 `options.Codecs.Use(...)` 같은 codec extension 등록이다.
custom codec을 추가하는 위치와 의미는 변경하지 않는다. 제거되는 기존 방식은 session
handler가 binding `Message`, `JsonMessageExtensions`, protobuf/json helper를 직접
호출해 payload를 만들거나 해석하는 코드다. 변경 후 handler는 DTO 또는 `ZLinkMessage`만
다룬다.

#### 4.4.1 actor/session 상위 모델 메모

actor join, actor factory, stream-attached actor 모델은 현재 draft
`Zlink.Framework` 의 구현 범위에 포함된다.

이 절은 다음 공개 계약들을 기준으로 설명한다.

- `IZLinkActor`
- `IZLinkActorContext.JoinSpot(...)`
- `IZLinkEntrySpotContext.Handlers.AddHandler<THandler>()`
- `IZLinkEntrySpotContext.Handlers.AddActorPacket<THandler, TActor>()`
- `IZLinkSpotContext.Handlers.AddHandler<THandler>()`
- `IZLinkSpotContext.Handlers.AddActorPacket<THandler, TActor>()`
- `IZLinkSpot<TActor>.OnActorJoinAsync(...)`
- `IZLinkSpot<TActor>.OnJoinedActorAsync(...)`
- `IZLinkSpot<TActor>.OnLeaveActorAsync(...)`
- `IZLinkEntrySpot<TActor>.OnJoinedActorAsync(...)`
- `IZLinkEntrySpot<TActor>.OnLeaveActorAsync(...)`
- stream session 의 actor dispatch 표면인 `IZLinkSessionContext`

`25-stage-wrapper-on-spot.ko.md` 는 이 계약 위에서 room/stage wrapper 를
어떻게 구성하는지 보여 주는 상위 모델 문서다. 함께 읽으면 도움이 된다.

##### zlink native Actor API 위임

zlink 라이브러리가 native Actor API 를 제공한다. framework 는 actor
lifecycle 관리를 이 API 로 위임한다.

- `SpotNode.CreateActor(string actorId)` — actor node에서 application actor에 대응하는 native actor를 생성한다.
- `SpotNode.EntrySpot()` → `Spot` — actor join 요청을 받는 입장 spot을 얻는다.
- `Spot.RecvActorJoin(RecvFlags)` → `ActorJoinRequest?` — join 요청을 수신한다.
- `Spot.ReplyActorJoin(request, joinResultCode, message)` — join handler 의 application
  결과 코드를 응답한다. `0` 은 join 허용이고, 0 이 아닌 값은 application 이 정의한
  거부 코드다.
- `Spot.OnActorLifecycle(onJoin, onLeave)` — actor가 해당 spot에 들어오거나 나간
  commit 이후 callback을 등록한다. `Entry Spot`과 일반 spot 모두 같은 API를 쓴다.
- `Actor.Join(spot, request, timeout, ct)` — actor가 특정 spot에 join을 요청한다.
- `Actor.Leave(spot, timeout)` — actor가 spot에서 나간다.
- `Actor.RecvPart(flags)` — STREAM 메시지 part를 수신한다.

framework 의 `SpotActivation` 은 두 가지 이벤트를 수신한다.
`SpotDispatchEvent.ActorJoinReadable` 과
`SpotDispatchEvent.ActorReadable` 이다. 각각 join drain 과 STREAM dispatch
를 처리한다.

직렬화 규칙은 spot 종류에 따라 다르다.

- user Spot 의 경우: 두 경로 모두 spot serial executor 안에서 직렬화된다.
- Entry Spot 의 actor readable 경로: Entry Spot 전체의 직렬 실행 줄이
  아니라, 대상 actor 의 mailbox 로 넘겨야 한다.

`OnDispatchEvent` 핸들러는 spot 초기화 시 항상 등록한다. 이유는 다음과
같다. packet/join handler 가 없는 spot 이라도, 런타임에 actor 가 join 될
때 `ActorReadable` 이벤트를 받아야 하기 때문이다.

`ActorJoinReadable` 처리 흐름은 다음과 같다.

1. framework 는 join 요청에서 `TargetActor` 를 꺼낸다. 이는 해당 spot 에
   이미 등록되어 있는 로컬 actor 다.
2. 이 `TargetActor` 를 framework actor registry 에서 조회한다.
3. 조회에 성공하면 target user Spot 의 `OnActorJoinAsync(...)` 를 호출한다.
4. `TargetActor` 를 찾지 못하면 join 요청을 거부한다.

framework 는 native `ActorRef` 를 public API 표면에 그대로 노출하지
않는다.

join admission callback에는 아직 actor instance가 없으므로 actor id, request와
cancellation token을 전달한다. commit 이후 joined/leave/disconnect callback에는 actor
instance와 cancellation token을 전달하며 actor id는 instance에서 읽는다. 현재 callback이
실행되는 Spot은 현재 Spot instance의 `Context`에서 읽는다. framework는 이동 전/후 Spot
rid, commit epoch, native flag 같은 내부 commit metadata를 public handler contract로
노출하지 않는다.

lifecycle callback 의 실행 문맥은 spot 종류에 따라 다르다.

- user Spot lifecycle callback 은 해당 user Spot 의 실행 queue 에서
  실행된다. 그래서 spot 상태를 읽고 쓰는 코드가, 일반 user Spot packet
  handler 와 동일한 직렬화 규칙을 따른다.
- Entry Spot lifecycle callback 은 Entry Spot lifecycle 실행 문맥에서
  실행된다. 다만 Entry Spot 의 actor packet handler 실행 순서는 이
  lifecycle 실행 문맥이 아니라, 대상 actor 의 mailbox 가 보장한다.

actor packet 실행 계약은 다음과 같이 둔다.

- actor packet 은 actor interface callback 으로 흘러 들어가지 않는다.
- actor 가 아직 user Spot 에 join 하기 전이라면, Entry Spot registry 에
  등록된 actor message handler 를 선택한다. 단 실행은 대상 actor mailbox
  를 통해 순서대로 처리한다.
- actor 가 user Spot 에 join 한 뒤에는, 해당 `Spot` registry 에 등록된
  actor message handler 를 반드시 해당 `Spot` 의 실행 문맥에서 호출한다.
  actor 가 room 또는 stage 상태를 읽고 쓸 수 있는 만큼, join 이후의
  dispatch 가 stream session callback 문맥에서 직접 실행되어서는 안 된다.
- Entry Spot registry 와 user Spot registry 는 서로 별개다. 같은 actor
  type 과 packet type 이라 해도, Entry 단계와 user Spot 단계에서 서로
  다른 message handler 를 각각 등록할 수 있어야 한다.
- `on_join` / `on_leave` commit 이후 callback 도 마찬가지다. Entry Spot
  과 user Spot 에 각각 별도로 선언할 수 있어야 한다. `OnActorJoinAsync(...)`
  는 join admission 요청 처리이고, `OnJoinedActorAsync(...)` /
  `OnLeaveActorAsync(...)` 는 commit 이후의 lifecycle callback 이라는 점에
  주의한다. framework public interface는 이 lifecycle callback에 기본 no-op 구현을
  제공하지 않는다.
- `JoinSpot(...)` 이 actor 의 현재 `Spot` 을 바꾸는 경우가 있다. 이때
  framework 는 actor session state 갱신과 이후 dispatch 선택이 서로
  경합하지 않도록 보장해야 한다. `OnActorJoinAsync(...)` 는 admission 만
  처리하며, join 이후에 도착한 packet 은 commit 된 새 `Spot` 의 실행
  문맥으로 흘러 들어가야 한다.
- 이 계약은 두 가지 표면을 분리하는 효과를 가진다. actor 가 사용하는
  stream I/O 표면과, `Spot` 상태 변경 표면이다. session 은 packet ingress
  역할만 담당한다. join 된 actor 의 game/domain 처리는 `Spot` 의 실행
  문맥에서 직렬화된다.

actor 실행 객체와 session dispatch handle 은 분리해서 다룬다. session 은
`IZLinkSessionActor` 를 저장하고 dispatch 에 사용한다.

`IZLinkActor` 는 actor node 에서 생성되는 application 객체다. framework
가 `Context` 를 설정한 다음, `Configure()` 를 한 번 호출한다. 그 이후의
callback signature 는 context 인자를 매번 다시 받지 않는다.

actor handler 계약은 actor 런타임이 소유하므로 `Zlink.Framework.Contracts.Actors`
namespace 에 둔다. session 은 `IZLinkSessionActors` 로 actor handle 을
bind 하거나 찾고, packet 전달은 `IZLinkSessionActor.RelayAsync(...)` 로 요청한다.
handler 선택과 실행은 actor runtime 이 처리한다.

```csharp
public interface IZLinkSessionActor
{
    string ActorId => Ref.ActorId;
    ActorRef Ref { get; }

    ValueTask RelayAsync(
        ZLinkMessage payload,
        CancellationToken cancellationToken = default);

    ValueTask NotifyDisconnectedAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActor
{
    string ActorId { get; }

    IZLinkActorContext Context { get; }

    void Configure()
    {
    }
}

public interface IZLinkActorContext
{
    RoutingId? SpotRid { get; }

    IZLinkBoundSession BoundSession { get; }

    IZLinkActorJoinSpotCall JoinSpot(
        RoutingId spotRid,
        ZLinkMessage request);

    IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        RoutingId spotRid,
        TRequest request);

    IZLinkActorJoinEntrySpotCall JoinEntrySpot(
        RoutingId spotNodeRid,
        ZLinkMessage request);

    IZLinkActorJoinEntrySpotCall JoinEntrySpot<TRequest>(
        RoutingId spotNodeRid,
        TRequest request);
}

public interface IZLinkActorJoinCall
{
    ValueTask<ZLinkActorJoinResult> Async(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkActorJoinResult<TReply>> Async<TReply>(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorJoinSpotCall : IZLinkActorJoinCall
{
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);
}

public interface IZLinkActorJoinEntrySpotCall : IZLinkActorJoinCall
{
    IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout);
}

public abstract record ZLinkActorJoinResult
{
    private protected ZLinkActorJoinResult() { }

    public sealed record Accepted(ActorRef Actor, ZLinkMessage Reply)
        : ZLinkActorJoinResult;

    public sealed record Rejected(ZLinkMessage Reply)
        : ZLinkActorJoinResult;
}

public abstract record ZLinkActorJoinResult<TReply>
{
    private protected ZLinkActorJoinResult() { }

    public sealed record Accepted(ActorRef Actor, TReply Reply)
        : ZLinkActorJoinResult<TReply>;

    public sealed record Rejected(TReply Reply)
        : ZLinkActorJoinResult<TReply>;
}
```

actor join call object는 `Async(...)` 하나로 완료를 기다린다. framework가 원래
실행 문맥의 직렬 상태와 join 진행에 필요한 독립 실행을 함께 관리하므로 호출자가
turn 반납 방식을 선택하지 않는다.
`SpotRid`가 `null`인지 여부가 join 상태의 단일 기준이다. join 결과도 승인과 거절을
서로 다른 record로 표현하므로 모순된 `Accepted`/`Actor` 조합을 만들 수 없다.

```csharp
public interface IZLinkActorFactory
{
    ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default);
}

// actor 구현체는 factory 가 받은 context 를 생성자에 넘겨 보관하고,
// Context property 로 그대로 노출해야 한다. framework 는 bind 시점에
// 같은 context 인스턴스인지 검증한다.
//
// actor packet 은 actor 자체에 handler 를 등록하지 않는다. Entry Spot 또는
// user Spot 의 Configure() 단계에서 AddActorPacket(...) 으로 등록한 handler 가
// 처리한다.

public sealed class ZLinkSpotActorSendContext : ZLinkHandlerContext
{
    public ZLinkMessageMetadata Metadata { get; }
}

public sealed class ZLinkSpotActorRequestContext : ZLinkHandlerContext
{
    public ZLinkMessageMetadata Metadata { get; }
    public ZLinkSpotActorReplyOptions Reply { get; }
}

public sealed class ZLinkSpotActorReplyOptions
{
    public ZLinkSpotActorReplyOptions Metadata(string key, string value);
    public ZLinkSpotActorReplyOptions Compress(bool enabled = true);
}

// Spot outbound의 전체 선언은 4.3.1의 IZLinkSpotOutbound 계약을 사용한다.

// Spot context의 전체 선언은 4.3.1의 IZLinkSpotCommonContext,
// IZLinkSpotContext, IZLinkEntrySpotContext 계약을 사용한다.
```

actor join admission 은 user Spot 멤버 method 로 선언한다. framework 는
`IZLinkSpot<TActor>` 의 actor 타입을 내부 routing 계약으로 사용하지만 admission callback 에는
actor id만 넘긴다. request 와 reply 는 codec-neutral `ZLinkMessage` 로 고정한다.

```csharp
public sealed class MatchSpot(IZLinkSpotContext context) : IZLinkSpot<PlayerActor>
{
    public IZLinkSpotContext Context { get; } = context;

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var decoded = request.Decode<JoinMatchReq>();
        return ValueTask.FromResult(
            ZLinkSpotActorJoinResult.Accept(new JoinMatchSpotResult(decoded.MatchId)));
    }
}
```

`OnActorJoinAsync(...)` 가 `Accepted=true` 를 반환하면 framework 는 join commit 을
수행한다. `Accepted=false` 를 반환하면 join 을 거절하고 post-joined callback 을 호출하지
않는다. application callback 은 별도의 `JoinActorAsync(...)` 를 호출하지 않는다.
`leaveActor(...)` 는 현재 user Spot 에서 actor 를 Entry Spot 으로 되돌리는
편의 API 다. 이 호출이 성공하면 source Spot 의 `ActorLeft` 와 Entry Spot 의
post-joined lifecycle callback 이 호출된다. 실패하면 actor 위치와 framework state 는
기존 상태를 유지하고 lifecycle callback 은 호출되지 않는다.

actor context 는 현재 client session 의 `SessionId` 만 조회값으로 노출한다.

session rid 와 binding token 은 actor 가 client 로 push 할 때 사용하는
runtime 내부 metadata 다. 그래서 actor context 에는 드러내지 않는다.

이유는 단순하다. application actor 코드가 session 위치값을 직접 보관하면,
재접속 시 stale 상태로 빠지기 쉽기 때문이다. framework runtime 은 필요한
session route 를 actor state 안에서 관리하고, actor code 에는 `BoundSession`
만 노출한다.

framework runtime 은 actor context 를 먼저 주입한 뒤 actor 객체를 만든다.
actor packet handler 등록은 actor 객체 쪽이 아니라, Entry Spot 또는 user
Spot 의 `Configure()` 단계에서 이루어진다.

이렇게 두면 한 가지 이점이 있다. Entry 단계와 user Spot 단계를 하나의
actor class 안에서 상태 분기로 섞을 필요가 없다.

outbound 는 actor context 의 기능이 아니다. Entry Spot 또는 user Spot 안에서
다른 Spot 이나 channel 로 메시지를 보내야 하면 handler 가 받은
`entrySpot.Context` 또는 `spot.Context` 의 `SendToSpot(...)`,
`RequestToSpot(...)`, `SendToChannel(...)`, `RequestToChannel(...)` 을 사용한다.
client stream 으로 push 해야 하면 Spot actor handler 가 받은 actor 의
`Context.BoundSession` 을 사용한다. actor handler 는 `ZLinkSpotActorSendContext`
또는 `ZLinkSpotActorRequestContext` 로 stream packet metadata 를 받는다.

Spot actor request handler 의 응답 body 는 handler 반환값으로 정한다. 응답
stream header 에 metadata 를 추가하거나 payload 압축을 켜야 하면
`ZLinkSpotActorRequestContext.Reply` 를 사용한다. 이 표면은 응답 전송 자체를
수행하지 않고, handler 반환값을 framework 가 response frame 으로 만들 때 사용할
옵션만 기록한다.

generic actor context는 Spot instance getter를 제공하지 않는다. Spot 상태가 필요한
코드는 Spot handler가 받은 `spot` 인자를 사용한다.

actor 위치 변경은 join commit 이 성공한 뒤에만 반영한다. `JoinSpot(...)` 성공은
target user Spot 으로 이동하고, `JoinEntrySpot(..., request)` 또는 `leaveActor(...)`
성공은 Entry Spot 으로 이동한다. 실패한 join/leave 는 source Spot membership 을
그대로 둔다.

`JoinSpot(spotRid, request)` 는 user Spot routing id(`RoutingId`) 와
DTO 또는 `ZLinkMessage` request 를 받는다.
`gameId`, `matchId`, `roomId` 같은 도메인 키를 그대로 넘기지 않는다. application
registry 가 먼저 domain key 를 user Spot `RoutingId` 로 변환하거나 조회한다.

`JoinEntrySpot(spotNodeRid, request)` 는 target SpotNode routing id(`RoutingId`) 와
DTO 또는 `ZLinkMessage` request 를 받는다. Entry Spot 은 SpotNode마다 하나뿐이므로
Entry Spot rid 를 별도로 넘기지 않는다. 보낼 payload가 없어도 `ZLinkMessage.Empty` 또는
빈 DTO를 명시해서 넘긴다.

`BoundSession.Send(...)` 는 현재 actor 에 연결되어 있는 stream client 로 packet 을
보낸다. request 에 대한 응답은 actor context 에서 직접 쓰지 않고,
request handler 의 반환값으로 보낸다.

Entry Spot actor, user Spot actor request 는 같은 방식으로 처리한다.
`IZLinkEntrySpotActorRequestHandler<..., TReply>`,
`IZLinkSpotActorRequestHandler<..., TReply>` 가 반환한 값이 reply 가 되고,
framework 가 원래 request 의 sequence 정보를 사용해 response 를 작성한다.
따라서 request packet 은 send handler 로 fallback dispatch 되지 않는다.

context 가 `IZLinkStream` 객체를 직접 노출하지는 않는다. stream 이 연결되지
않은 actor 에서 `BoundSession.Send(...)` 를 호출하면, 명확한 실패로 처리된다.

actor 또는 `Spot` callback 안에서 task 기반 request 를 `await` 할 때
주의할 점이 있다. thread 를 점유하지는 않지만, 현재 callback task 는 응답
이나 timeout 이 발생하기 전까지 종료되지 않는다.

따라서 같은 `Spot` 의 다음 작업은 그 뒤에야 실행된다. 명시적 timeout 을
지정하지 않으면, framework 의 기본 timeout 이 적용된다.

#### 4.4.2 session actor dispatch handler

session actor dispatch 는 actor 객체의 callback 을 직접 호출하지 않는다.
대신 현재 actor 위치에 맞는 registry 에 등록된 typed handler 를 호출한다.
이 registry 는 Entry Spot 또는 user Spot 의 registry 다.

handler 는 저수준 정보를 직접 보지 않는다. 즉 raw routed envelope, stream
sequence, session rid 같은 것들은 노출되지 않는다.

각 단계의 handler 가 구현해야 할 interface 는 다음과 같다.

- Entry Spot handler: `IZLinkEntrySpotActorSendHandler<...>` 또는
  `IZLinkEntrySpotActorRequestHandler<...>` 중 하나를 구현한다.
- user Spot handler: `IZLinkSpotActorSendHandler<...>` 또는
  `IZLinkSpotActorRequestHandler<...>` 중 하나를 구현한다.

실행 순서는 현재 actor 위치에 따라 결정된다.

- actor 가 Entry Spot 에 있으면: session 에서 넘어온 packet 은 actor 별
  mailbox 에서 순서대로 처리된다.
- actor 가 user Spot 에 있으면: user Spot 의 실행 queue 에서 처리된다.
  덕분에 동일 Spot 상태를 함께 보호하게 된다.

공통 metadata 타입은 모든 호출 경로에서 같은 snapshot 규칙을 따른다. 즉
actor dispatch, bound session, channel 호출 모두 동일하다.

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

#### 4.4.3 dispatch policy

handler lookup과 invocation 최적화는 framework가 소유한다. 등록 시 descriptor와
delegate를 준비하고 packet hot path에서 reflection과 per-packet service lookup을
사용하지 않는다. application이 `Compiled` 또는 `Dynamic` 같은 구현 전략을 고르는
public option은 제공하지 않는다.

```csharp
public interface IZLinkDispatchOptions
{
    IZLinkUnhandledDispatchOptions Unhandled { get; }
    IZLinkDiagnosticsOptions Diagnostics { get; }
    IZLinkDispatchOptions SetMessageFlowObserver<TObserver>()
        where TObserver : class, IZLinkMessageFlowObserver;
    IZLinkDispatchOptions SetMessageFlowObserver(
        IZLinkMessageFlowObserver observer);
    IZLinkDispatchOptions MessageFlow(ZLinkMessageFlowLogMode mode);
    IZLinkDispatchOptions TraceSampleRate(double rate);
    IZLinkDispatchOptions IncludeMessageSizes(bool include);
    IZLinkDispatchOptions TraceLogFile(string path);
    IZLinkDispatchOptions TraceLabel(string label);
}
```

### 4.5 message serializer

`playhouse/extensions` 에서 보이듯, serializer 계층은 transport interface
와 분리한다.

`STREAM` handler 는 framework `ZLinkMessage` 를 받는다. protobuf/json 같은 객체
변환은 등록된 serializer 또는 extension provider 가 담당한다.

```csharp
public interface IZLinkMessageSerializer
{
    ZLinkEncodedPayload Serialize(object value, Type type);
    object? Deserialize(ZLinkEncodedPayload payload, Type type);
}
```

실사용 표면은 binding core 의 `Message` 자체에 직접 얹지 않는다. application 은
`ZLinkMessage.Decode<T>()` 또는 typed handler를 기본으로 쓴다. 예시는 다음과 같다.

```csharp
ChatRequest request = payload.Decode<ChatRequest>();
```

이 문서에서는 다음 규칙을 기본으로 둔다.

- `T` 가 generated protobuf 타입이면서 `IMessage<T>` 계열이면, protobuf
  로 해석한다.
- 그 밖의 일반 class 는 json 으로 해석한다.

두 가지 구조 중 후자를 택한다.

- 첫 번째: transport 가 serializer 를 직접 내장하는 구조.
- 두 번째 (기본): framework `ZLinkMessage`가 codec registry를 통해 type 기준 decode를 수행하는 구조.

두 번째 구조가 `playhouse/extensions` 와도 더 가깝고, 문서도 더 단순하게
읽힌다. codec extension package 는 registry에 serializer를 등록하고, handler는
binding `Message` bytes나 codec helper를 직접 다루지 않는다.

## 5. Client 인터페이스

이 절은 서버에서 다른 서버로 메시지를 보내는 client interface 들을 정의한다.

### 5.1 IZLinkChannelClient

서버 간 outbound 호출을 위한 공용 client 다. DI 로 주입되며, ZLink handler
와 기존 ASP.NET Core HTTP handler 양쪽에서 동일하게 사용할 수 있다.

호출 방식은 한 가지 축을 기본으로 둔다.

- `channelName` 기준 호출 — location store 자동 연결 또는 수동 endpoint 설정이 대상을 선택한다.

다시 말해, 일반 channel messaging 에서는 특정 `ROUTER(server)` 를 `rid` 로
직접 지정해 호출하지 않는다. `rid` 를 넣는 routed 호출은 SPOT spot-to-spot
경로에만 남겨 둔다.

packet key는 message type의 `ZLinkPacket` metadata 또는 `Type.Name`으로 registration 시
한 번 확정한다. call site와 handler builder는 이름을 다시 받지 않는다. request timeout과
metadata처럼 호출별로 달라지는 값만 call builder가 소유한다.

```csharp
public interface IZLinkSendCall
{
    void Submit(
        CancellationToken cancellationToken = default);
}

public interface IZLinkRequestCall
{
    IZLinkRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(
        CancellationToken cancellationToken = default);
}

public interface IZLinkChannelClient
{
    IZLinkSendCall SendToChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall RequestToChannel<TMessage>(
        string channelName,
        TMessage request);
}

```

`IZLinkChannelClient` 는 client-server channel request/send outbound 표면이다.
호출자는 `channelName`만 넘기고, runtime 은 등록된 channel bundle 을 보고 local
client DEALER socket 을 선택한다.

runtime 의 채널 구성 방식은 다음과 같다.

- 등록된 `channelName` 마다 별도의 outbound channel 을 생성한다.
- 각 channel 은 역할 마다 다시 별도의 outbound runtime 을 가진다.
- 특히 수동 연결은 `channel` 전체가 아니라 `channel + capability` 단위로
  관리한다.

예를 들어 다음처럼 구분해야 한다.

- `profile.client` 수동 연결
- `profile.subscriber` 수동 연결

이유는 명확하다. `profile` channel 하나만으로는 "request client 연결인지,
subscriber 연결인지" 를 식별할 수 없다. 그래서 framework 역시 역할
별 runtime 을 따로 관리하게 된다.

packet key는 registration 시 message type descriptor에서 한 번 확정한다. attribute가
있으면 그 이름을 사용하고, 없으면 nominal type 이름을 사용한다. typed call마다
`PacketName(...)`으로 같은 결정을 다시 지정하지 않는다.

timeout 은 request 와 send 간에 다르게 다룬다.

- `RequestToChannel(...)` 는 reply 를 기다리므로 `Timeout(...)` 을 둘 수 있다.
- `SendToChannel(...)` 는 응답을 기다리지 않으므로 timeout 설정을 두지 않는다.
- `Publish(...)` 도 같은 이유로 timeout 설정을 두지 않는다.
- `SendToChannel(...).Submit(...)` 는 입력 검증과 bounded local queue 수락까지만
  동기로 수행한다. queue 수락 뒤 transport와 handler 완료를 기다리지 않는다.
- `Publish(...).Submit(...)` 도 동일한 의미다. subscriber 의 handler
  완료나 subscriber 수신을 기다리지 않는다. local publish transport 에
  submit 되는 시점까지만 대기한다.
- one-way local queue가 수락할 수 없으면 즉시 예외를 던진다. 호출자 thread에서
  socket readiness나 `SendTimeout`까지 기다리지 않는다.
- `RequestToChannel(...).Async<TReply>(...)` 도 마찬가지다. request packet 을
  내보내는 단계에서는, `SendToChannel(...).Submit(...)` 와 동일한 nonblocking
  submit 경로를 사용한다.
- `RequestToChannel(...).Timeout(...)` 은 reply 대기 시간만을 결정한다.
- 별도의 public no-wait 옵션은 제공하지 않는다. queue 수락 뒤 temporary
  backpressure는 framework 내부 ready notification으로 처리한다.

send 호출자는 local queue 수락 이후 transport 완료를 기다리지 않는다. backpressure가
걸려도 framework는 caller thread나 thread pool worker를 점유하지 않고 socket ready
callback이나 poller wakeup에서 pending submit을 이어서 진행한다.

고성능 구현을 위한 기본 계약은 다음과 같다.

- 즉시 전송 또는 local queue 수락 fast path에서는 완료 객체를 만들지 않는다.
- pending send queue는 무한 queue가 아니다. channel/socket의 high water mark,
  `SendTimeout`, cancellation, runtime stop 중 어떤 조건으로든 반드시 빠져나올 수
  있어야 한다.
- socket ready callback 은 pending item 을 하나만 처리하고 끝내지 않는다.
  정해진 batch budget 범위 안에서 queue 를 drain 한다. 이는 ready event
  폭주와 context switch 를 줄이기 위함이다.
- pending request 등록은 request packet submit 이전에 완료되어야 한다.
  submit 실패, timeout, cancellation, runtime stop 중 어떤 상황에서든
  해당 pending request 는 즉시 제거한다.
- request reply timeout 은 submit 이 완료된 시점부터 카운트한다. submit
  단계에서의 지연은 `SendTimeout` 이 담당한다.
- payload encoding 과 native `Message` 의 소유권은 한 곳에서 정리한다.
  정리 시점은 submit 이 완료되거나 실패하는 시점이다. retry 도중 같은
  frame 을 중복 전송하거나 중복 dispose 해서는 안 된다.
- stream connector public options 에는 `SendTimeout` 을 두지 않는다.
  connector send 는 응답이 없는 submit 이다. connector 의 request reply
  대기에는 `RequestTimeout` 을 사용하고, `WaitFor(...)` 의 push 대기에는
  `WaitTimeout` 을 사용한다.

따라서 public 호출 감각은 다음과 같이 잡힌다.

```csharp
var reply = await client
    .RequestToChannel("profile", new GetProfileRequest { AccountId = accountId })
    .Timeout(TimeSpan.FromMilliseconds(200))
    .Async<GetProfileReply>(cancellationToken);

client
    .SendToChannel("profile", new RefreshProfileCacheCommand { AccountId = accountId })
    .Submit(cancellationToken);
```

### 5.2 IZLinkSpotOutbound

현재 spot runtime 안에서의 outbound 호출을 담당하는 client 다.
`IZLinkChannelClient` 와는 독립된 interface 이며, 하부에서 서로 다른 C API 를
감싼다.

현재 다루는 축은 세 가지다.

- 현재 SPOT channel 안에서의 publish/subscribe
- channel egress bridge를 거치는 다른 channel 의 send/request
- spot rid 기반의 routed spot send/request

Spot 대상 호출은 `IZLinkSpotHandleResolver`가 반환한 `SpotHandle`을 사용한다. handle이
내부 주소 snapshot과 location event 갱신을 소유하고 route transport가 실제 전송을 담당한다.
request는 handler가 실행되지 않았음이 확정된 target-not-found에서만 주소를 한 번 갱신해
한 번 다시 전송한다. one-way send는 최신 snapshot으로 한 번만 제출한다.

application 이 `targetRid + spotRid` 를 직접 넘기는 일은 없다.

전체 member 시그니처는 4.3.1의 `IZLinkSpotOutbound` 선언으로 고정한다.

`IZLinkSpotContext` 와 `IZLinkEntrySpotContext` 는 같은 outbound 표면을
직접 노출한다. 따라서 SPOT lifecycle callback 이나 handler 안에서는 별도
client 를 주입하지 않고 `Context.Outbound.SendToSpot(...)`, `Context.Outbound.RequestToSpot(...)`,
`Context.Outbound.SendToChannel(...)`, `Context.Outbound.RequestToChannel(...)`, `Context.Outbound.Publish(...)`
를 사용할 수 있다.

`IZLinkChannelClient` 와 비교했을 때의 차이는 다음과 같다.

- `Publish(topic, ...)` 가 포함된다. SPOT 쪽은 현재 channel 안에서 topic
  publish 를 함께 사용하는 경우가 많기 때문에, 같은 interface 에 둔다.
- `SendToSpot(...)` / `RequestToSpot(...)` 은 spot handle resolver를 사용한다.
- `SendToChannel(...)` / `RequestToChannel(...)` 은 channel egress bridge를
  통해 해소한다.
- 따라서 local `SpotNode` 나 local spot runtime 이 없는 앱이라면, 기본
  outbound 표면은 `IZLinkChannelClient` 다. 그런 앱에서 외부 SPOT channel
  publish 만 필요한 경우에는, `IZLinkSpotPublisherClient` 를 별도로
  사용한다.
- channel send/request 는 일반 `IZLinkChannelClient` 와 동일한 builder 감각을
  따른다.
- timer 는 별도 callback scheduler 로 두지 않는다. 대신 spot lifecycle
  안에서 `Context.AddTimer<THandler>(name, period, ...)` 로 등록하는 한
  가지 모델로 정의한다. 구현에서는 framework
  runtime 이 만든 managed `.NET` timer 를 동일한 spot execution context
  로 매핑하는 방향이 적절하다.

framework 계약에서 말하는 "spot 용 함수"와 "channelName으로 호출하는
함수" 는 서로 별개의 경로다. 두 경로는 다음과 같이 갈라진다.

- channel 이름 기준 호출은 channel egress bridge를 사용한다.
- spot rid 기반 호출은 `IZLinkSpotHandleResolver`가 만든 handle을 framework 내부
  transport가 사용한다.

`targetRid + spotRid` 를 직접 넘기는 raw route 함수는 application public
표면에 두지 않는다.

`IZLinkChannelClient` 와 `IZLinkSpotOutbound` 는 상하 관계가 아니다. 두 interface
는 서로 다른 하부 C API 를 감싸며, 각자 독립적인 구현을 가진다.

#### 5.2.1 route client 와 Spot route 경계

`IZLinkRouteClient` 는 route mesh channel 로 target node 에 send/request 할 때
사용한다. 반환 타입은 channel client 와 같은 `IZLinkSendCall`,
`IZLinkRequestCall` 이다. route 전용 call interface 는 public API 표면에 두지
않는다.

```csharp
public interface IZLinkRouteClient
{
    IZLinkSendCall SendToNode<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message);

    IZLinkRequestCall RequestToNode<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request);

    IZLinkSendCall SendToSpot<TMessage>(
        SpotHandle target,
        TMessage message);

    IZLinkRequestCall RequestToSpot<TRequest>(
        SpotHandle target,
        TRequest request);
}
```

Spot 으로 가는 routed transport 는 application 이 직접 egress client 를 고르는
public 표면으로 노출하지 않는다. current Spot callback 안에서는
`spot.Context.Outbound.SendToSpot(...)` 또는
`spot.Context.Outbound.RequestToSpot(...)` 을 사용한다. current Spot 이 없는
session, HTTP handler, background service 에서는 actor 생성 또는 entry spot
join 같은 도메인 흐름으로 `ActorRef` 를 얻은 뒤 session actor handle 로 bind 한다.

### 5.3 IZLinkSpotPublisherClient

Spot publish 는 사용 상황이 다르다.

- `spot.Context.Outbound.Publish(...)` 는 이미 실행 중인 local spot 문맥에서
  현재 SPOT channel 로 publish 할 때 사용한다.
- `IZLinkSpotPublisherClient` 는 local spot 인스턴스가 없는 외부 노드가,
  특정 spot channel 로 publish 할 때 사용하는 별도의 client 다.

```csharp
public interface IZLinkSpotPublisherClient
{
    IZLinkPublishCall PublishSpot<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}
```

여기서 `channelName` 은 target SPOT channel 의 이름이다. 즉 이 interface
는 다음 상황을 위한 것이다. `game.stage`, `game.chat` 처럼 여러 SPOT
channel 이 존재할 때, 외부 노드가 어느 channel mesh 로 publish 할지
선택하는 용도다.

두 publish 표면의 차이는 다음과 같이 정리할 수 있다.

- `spot.Context.Outbound.Publish(...)`
  - local spot 문맥 안에서 현재 SPOT channel 로 publish 한다.
- `IZLinkSpotPublisherClient.PublishSpot(...)`
  - local spot 인스턴스 없이, 외부 노드에서 target SPOT channel 로
    publish 한다.

### 5.4 IZLinkFanoutClient

일반 `PUB/SUB` event 를 publish 하기 위한 interface 다.

SPOT publish 와는 별개의 경로다. 즉 channel messaging 쪽에서 사용한다.

```csharp
public interface IZLinkPublishCall
{
    void Submit(
        CancellationToken cancellationToken = default);
}

public interface IZLinkFanoutClient
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}

```

`IZLinkFanoutClient` 는 일반 fanout channel 에 publish 하는 public DI 표면이다.
별도 event publisher 별칭은 두지 않는다.

여기서 두 문자열의 역할은 각각 다음과 같다.

- `channelName`
  - 어느 논리 channel 의 `PUB/SUB` mesh 에 publish 할지를 지정한다.
- `topic`
  - 해당 channel 내부에서 어떤 subscriber 집합이 이벤트를 수신할지를
    지정한다.

따라서 `Publish("profile", "profile.cache-refreshed", evt)` 호출의 의미는
다음과 같다. `profile` channel 안의 `profile.cache-refreshed` topic 으로
fan-out 한다는 뜻이다.

일반 `PUB/SUB` publish 도 `SendToChannel(...)` 와 마찬가지로 timeout 을 두지
않으며 typed packet 이름 override도 제공하지 않는다. `Submit(...)`은 입력 검증과
bounded local queue 수락을 동기로 수행한 뒤 반환한다. cancellation은 queue 수락 전
호출 취소에만 적용된다. 입력이 잘못됐거나 route가 준비되지 않았거나 queue가 수락할
수 없으면 즉시 예외를 던진다. queue 수락 뒤의 transport 실패는 monitoring/error
observer로 전달하며 이미 반환한 호출에 예외를 되돌리지 않는다.

publish 도 send 와 동일한 성능 규칙을 따른다. 즉 다음과 같이 동작한다.

- subscriber 마다 별도 task 를 생성하지 않는다.
- subscriber 수만큼 payload 를 재직렬화하지도 않는다.
- 가능한 경우 topic frame 과 payload frame 을 한 번만 만든다.
- local queue가 수락할 수 없으면 동기 예외로 처리하고 호출자 thread에서 transport
  readiness를 기다리지 않는다.

### 5.5 Actor Route Resolver

session 에서 actor 로 packet 을 relay 할 때는 `IZLinkSessionContext` 의
`IZLinkSessionActor.RelayAsync(...)` 를 사용한다. actor runtime 을 직접 호출하는 별도
public client 는 두지 않는다.

remote actor 위치는 session 이 직접 계산하지 않는다. session 은 actor id/type 으로
local actor handle 을 만들거나, actor 생성 또는 join 결과의 `ActorRef` 로
remote actor handle 을 만들고, core SessionRelay 가 그 actor ref 를 기준으로 relay 한다.

### 5.6 IZLinkBoundSession

actor handler 가 현재 client session 으로 push 를 보낼 때 사용하는 client 다.
client 에게 새 request 를 보내는 API 는 제공하지 않는다. client request 에 대한
응답은 actor request handler 의 반환값으로 처리한다.

`IZLinkBoundSession` 자체는 연결된 client stream 을 향한 proxy 이므로
`Zlink.Framework.Contracts.Streams` 에 둔다. Spot actor handler 는 actor 의
`Context.BoundSession` 으로 이 proxy 에 접근하고, stream packet metadata 는
`ZLinkSpotActorSendContext` / `ZLinkSpotActorRequestContext` 로 받는다.

**Spot actor handler 인터페이스와 `ZLinkSpotActorSendContext`·`ZLinkSpotActorRequestContext`는
`Zlink.Framework.Contracts.Spots`에 둔다.**

application handler 는 actor id 만 넘긴다. 다음 metadata 들은
framework/core 의 actor-session binding 안에만 머문다.

- session server `RoutingId`
- stream `SessionId`
- binding token

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
    IZLinkBoundSessionSendCall Metadata(
        string key,
        string value);

    void Submit(CancellationToken cancellationToken = default);
}
```

`Submit(...)` 은 bound session send의 one-way terminator다. 호출자는 client push의
송신 수락 완료를 기다리지 않는다. Entry Spot actor handler에서도 같은 규칙을 사용한다.

### 5.7 actor/spot ref resolver와 actor-session binding

public resolver 는 두 축으로 제한한다. actor 와 spot 이다.

- actor resolver: actor id 로부터 actor runtime route 를 조회한다.
- spot resolver: spot rid 로부터 user Spot route 를 조회한다.

`IZLinkBoundSession` 는 actor context 가 현재 actor id 를 알고 있을 때만
제공한다. 사용자는 actor id 를 다시 넘기지 않고 현재 actor 에 묶인 client
session 으로 보낸다. 다른 actor 의 client session 으로 보내야 하면 먼저 해당
actor 에게 메시지를 보내고, 그 actor 의 handler 가 자기 `BoundSession` 로 push 한다.
`DisconnectAsync(...)` 도 현재 actor 의 binding 상태를 사용한다. actor 가
client 연결을 끊기로 결정한 경우 이 메서드를 호출하며, session callback 으로
`OnDisconnectedAsync(...)` 를 다시 올리지 않는다.

분산 배포 환경에서도 session binding 은 actor runtime state 에 저장한다. 별도 public
session 위치 resolver 나 session 위치 저장소는 제공하지 않는다.

```csharp
namespace Zlink.Framework.Contracts.Locations;

public abstract class SpotHandle
{
    internal SpotHandle() { }
    public abstract RoutingId SpotRid { get; }
}
```

`SpotHandle`의 public constructor는 없다. resolver가 만들며 owner node, generation,
channel 선택과 location event subscription은 내부 상태로 유지한다.

actor-session route 는 public contract 가 아니다. session bind 시 framework runtime 이
현재 actor state 에 session rid 와 binding token 을 저장하고, `IZLinkBoundSession`
가 그 내부 상태를 사용한다.

resolver 의 입력에는 다음 값들을 넘기지 않는다. metadata, packet name,
raw message, decoded payload 다.

이런 값들이 필요해 보이면 어떻게 해야 할까. caller 의 domain placement
코드에서 actor id 나 spot key 를 먼저 결정해야 한다.

resolver 의 책임은 좁다. 위치 저장소 접근만 담당한다. 작은 dispatcher 역할
까지 떠안아서는 안 된다.

`내부 actor session state` 는 resolver 가 아니다. session
bind/unbind lifecycle 과 `BoundSession` 조회를 하나의 저장소 계약으로 묶는
역할이다. 이렇게 묶어 두는 의도는 stale binding 처리를 한곳에 가두기
위해서다.

같은 actor id 가 새 stream session 으로 다시 bind 되는 경우의 동작은
다음과 같다.

- framework 는 새 논리 actor 를 만들지 않는다.
- 기존 actor runtime state 가 남아 있으면, 해당 actor 인스턴스를 그대로
  재사용한다. session binding token 만 새 값으로 교체한다.
- actor factory 가 호출되어 새 actor 를 만들어야 하는 경우에도, factory
  가 반환한 `ActorId` 는 요청한 actor id 와 정확히 일치해야 한다.

일치하지 않으면 어떻게 될까. logical actor handle 과 session binding 이 서로 다른
actor id 를 가리키게 된다. 이 경우 configuration 오류로 실패한다.

## 6. 등록과 관리 인터페이스

이 절은 framework 가 부팅 시 받는 등록 표면과, runtime 에서 channel/spot
연결을 관리하는 표면을 정의한다.

### 6.1 framework 등록 루트

이 카탈로그에서는 `AddZLinkFramework(...)` 의 builder 표면까지 함께
고정한다. 이렇게 두는 이유는, 샘플 문서에 등장하는 표면들의 소유자를
분명히 하기 위해서다. 해당 표면들은 `AddClientServerChannel`,
`UseFilter(...)` 다.

SPOT mesh channel 과 node 집합은 `AddSpotMesh` 안에서 함께 등록한다.
이렇게 하면 channel view 의 소유자가 하나로 고정되어 node 등록 순서나
분리 호출 여부가 의미에 영향을 주지 않는다.
peer 획득은 이 등록이 직접 소유하지 않는다. location store 자동 연결 또는
manual endpoint 설정이 peer 집합을 공급한다.

channel 자동 연결의 등록 위치는 다음과 같이 정해 둔다.

- location store 는 역할 별 builder 아래에 중복으로 두지 않는다.
  framework 등록 루트에 `AddLocationStore(...)` 로 한 번만 둔다.
- 이 등록의 의미는 다음과 같다. framework 안의 자동 연결 역할들이 공유하는
  peer/spot/actor/route 위치 row 저장소를 가리킨다.
- 반대로 manual 연결은 역할 별 runtime 설정에 해당한다. 그래서 역할을 켜는
  builder 메서드의 endpoint 인자로 둔다.

```csharp
public interface IZLinkMetadataPolicyBuilder
{
    void AddForwardedMetadataKey(string key);
}

public interface IZLinkStreamNodeBuilder
{
    IZLinkStreamNodeBuilder Bind(string endpoint);

    IZLinkStreamNodeBuilder SetTlsServer(
        string certPath,
        string keyPath,
        bool requireClientCert = false);

    IZLinkStreamNodeBuilder RegisterSession<TSession>()
        where TSession : class, IZLinkSession;
}

public interface IZLinkSessionHandlerRegistry
{
    void AddHandler<THandler>()
        where THandler : class;

    // packet name을 명시하는 override. 생략하면 payload 타입 이름을 사용한다.
    void AddHandler<THandler>(string packetName)
        where THandler : class;

    ValueTask<bool> TryHandleAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionPacketHandler<in TSessionContext, TMessage>
{
    ValueTask HandleAsync(
        TSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        TMessage message,
        CancellationToken cancellationToken);
}
```

session handler registry 는 session 의 `Configure()` 실행 중에만 handler class 등록을
허용한다. 미등록 packet 을 actor 로 relay 할지, 무시할지, 오류로 처리할지는 session 구현체의
정책이다. handler 로 전달되는 `message` 는 framework 가 packet payload 를 `TMessage` 로
decode 한 값이다.

```csharp
public interface IZLinkClientServerChannelBuilder : IZLinkClientServerChannelOptions
{
    IZLinkClientServerChannelBuilder EnableServer(string endpoint);

    IZLinkClientServerChannelBuilder EnableClient();

    IZLinkClientServerChannelBuilder EnableClient(string endpoint);

    IZLinkEndpointConnections ClientConnections { get; }

    IZLinkClientServerChannelBuilder SetRoutingId(RoutingId routingId);

    IZLinkSocketConfig ConfigureServerSocket();

    IZLinkRouteConfig ConfigureServerRouting();

    IZLinkSocketConfig ConfigureClientSocket();

    IZLinkOutboundRouteConfig ConfigureClientRouting();

    IZLinkClientServerChannelBuilder SetDefaultRequestTimeout(TimeSpan timeout);

    IZLinkClientServerChannelBuilder AddHandlerGroup(string groupName);

    IZLinkClientServerChannelBuilder AddSendHandler<THandler, TMessage>(
        string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;

    IZLinkClientServerChannelBuilder AddSendHandler<THandler>(
        string? packetName = null)
        where THandler : class;

    IZLinkClientServerChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(
        string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;

    IZLinkClientServerChannelBuilder AddRequestHandler<THandler>(
        string? packetName = null)
        where THandler : class;

}

public interface IZLinkFanoutChannelBuilder
{
    IZLinkFanoutChannelBuilder EnablePublisher(string endpoint);

    IZLinkFanoutChannelBuilder EnableSubscriber();

    IZLinkFanoutChannelBuilder EnableSubscriber(string endpoint);

    IZLinkEndpointConnections SubscriberConnections { get; }

    IZLinkFanoutChannelBuilder SetRoutingId(RoutingId routingId);

    IZLinkFanoutChannelBuilder AddHandlerGroup(string groupName);

    IZLinkFanoutChannelBuilder AddPublishHandler<THandler, TMessage>(
        string? packetName = null)
        where THandler : class, IZLinkPublishHandler<TMessage>;

    IZLinkFanoutChannelBuilder AddPublishHandler<THandler>(
        string? packetName = null)
        where THandler : class;
}

public interface IZLinkRouteMeshChannelBuilder : IZLinkRouteMeshChannelOptions
{
    IZLinkRouteMeshChannelBuilder EnableServer(string endpoint);

    IZLinkRouteMeshChannelBuilder EnableClient();

    IZLinkRouteMeshChannelBuilder EnableClient(string endpoint);

    IZLinkEndpointConnections ClientConnections { get; }

    IZLinkSocketConfig ConfigureSocket();

    IZLinkRouteMeshChannelBuilder SetRoutingId(RoutingId routingId);

    IZLinkRouteMeshChannelBuilder SetDefaultRequestTimeout(TimeSpan timeout);

    IZLinkRouteMeshChannelBuilder AddHandlerGroup(string groupName);

    IZLinkRouteMeshChannelBuilder AddSendHandler<THandler, TMessage>(
        string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>;

    IZLinkRouteMeshChannelBuilder AddSendHandler<THandler>(
        string? packetName = null)
        where THandler : class;

    IZLinkRouteMeshChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(
        string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>;

    IZLinkRouteMeshChannelBuilder AddRequestHandler<THandler>(
        string? packetName = null)
        where THandler : class;


}

public interface IZLinkFrameworkOptions
{
    TimeSpan DefaultRequestTimeout { get; set; }
    TimeSpan ActorTransferForwardWindow { get; set; }

    TimeSpan? DefaultSocketSendTimeout { get; set; }

    IZLinkCodecRegistryBuilder Codecs { get; }

    IZLinkWorkerOptions Worker { get; }

    void AddHandlersFromAssemblyOf<TMarker>();

    void AddHandlersFromAssemblyOf(Type markerType);

    void AddHandlersFromAssembly(System.Reflection.Assembly assembly);

    void DisableImplicitHandlerAutoRegistration();

    IZLinkMetadataPolicyBuilder ConfigureMetadata();

    void AddLocationStore(IZLinkLocationStore store);

    void UseInMemoryLocationStores();

    ZLinkLocationOptions ConfigureLocations();

    IZLinkClientServerChannelBuilder AddClientServerChannel(string channelName);

    IZLinkFanoutChannelBuilder AddFanoutChannel(string channelName);

    IZLinkRouteMeshChannelBuilder AddRouteMeshChannel(string channelName);

    void UseFilter<TFilter>()
        where TFilter : class, IZLinkHandlerFilter;

    IZLinkDispatchOptions ConfigureDispatch();

    IZLinkStreamCompressionBuilder ConfigureStreamCompression();

    IZLinkStreamNodeBuilder AddStreamNode(string streamNodeName);

    IZLinkSpotMeshBuilder AddSpotMesh(string channelName);

}
```

`ConfigureLocations()`가 반환하는 옵션에는 Spot mesh와 route channel 이름이 다른 배포를
위한 명시적 매핑이 포함된다. 매핑이 없으면 두 이름이 같다고 해석한다.

```csharp
public sealed class ZLinkLocationOptions
{
    public void MapSpotMeshToRouteChannel(
        string spotMeshName,
        string routeChannelName);
}

options.ConfigureLocations().MapSpotMeshToRouteChannel(
    "game.stage",   // location store가 반환하는 Spot mesh 이름
    "game.route"); // 실제 전송에 사용할 route channel 이름
```

`DefaultRequestTimeout`의 기본값은 30초다. `ActorTransferForwardWindow`의 기본값은 5초이며,
remote actor 이동 뒤 old generation ref를 다음 hop으로 전달하는 기간을 정한다. 0으로 지정하면 commit
backlog만 넘기고 이후 straggler는 바로 stale 오류로 끝난다. 음수는 허용하지 않는다.
`DefaultSocketSendTimeout`의 기본값은
core socket 기본 send timeout과 같은 1000ms다. 채널별 기본 request timeout은
`SetDefaultRequestTimeout(...)`으로 지정할 수 있다.

각 함수의 의미는 다음과 같다.

- `DefaultRequestTimeout`
  - request 호출이 별도 timeout을 지정하지 않았을 때 쓰는 framework 기본값이다.
    호출별 `Timeout(...)`이 가장 먼저 적용되고, 그 다음 채널별
    `SetDefaultRequestTimeout(...)`, 마지막으로 이 전역 기본값이 적용된다.
- `DefaultSocketSendTimeout`
  - socket 이 바로 보낼 수 없을 때 async submit 이 기다릴 기본 backpressure
    한계다. request reply 대기 시간과는 다르다.
- `Codecs`
  - protobuf/json/messagepack 같은 codec provider를 framework registry에 등록하는
    진입점이다.
- `ConfigureMetadata(...)`
  - session actor dispatch와 bound session 경로에서 전달할 metadata key를 등록한다.
- `IZLinkSpotNodeBuilder.AddActorFactory(...)`
  - 해당 SpotNode에서 actor type 문자열에 대응하는 actor factory를 등록한다.
- `AddLocationStore(...)`
  - location store 인스턴스를 등록한다. framework는 이 store에서 Spot handle의
    내부 주소 snapshot, actor 위치, peer 위치와 route 위치를 조회한다.
- actor-session binding
  - 별도 public registration 함수로 등록하지 않는다. stream session이 actor handle을
    만들거나 actor에 attach되면 framework/core가 binding 상태를 갱신하고,
    `IZLinkBoundSession`는 그 상태를 사용해 client stream으로 보낸다.
- `AddClientServerChannel`
  - request/send 용 client-server 채널을 등록한다. builder는 `EnableServer(...)`와
    `EnableClient(...)`만 노출한다.
- `AddFanoutChannel`
  - pub/sub fanout 채널을 등록한다. builder는 `EnablePublisher(...)`와
    `EnableSubscriber(...)`만 노출한다.
- `AddRouteMeshChannel`
  - route mesh 채널을 등록한다. bind endpoint, socket option, routing option,
    manual connection을 한 builder 안에서 함께 설정한다.
  - route mesh 채널이 사용할 server/client 역할을 등록한다.
- `UseFilter<TFilter>()`
  - handler filter 타입을 framework pipeline에 등록한다.
- `AddSpotMesh`
  - SPOT mesh channel 과 이 프로세스의 단일 `SpotNode`를 함께 등록한다.
    `EnableRouter`, `EnablePubSub`, `AddSpotFactory<TSpot>(...)`,
    `AddEntrySpot<TEntrySpot>()`를 노출한다.
    SessionRelay 는 별도 node builder 를 갖지 않고, 같은 프로세스의 router 역할
    SpotNode 에 STREAM 의 session relay 입구가 자동으로 연결된다.
- `EnableServer(...)`
  - local request/send handler를 받을 `ROUTER(server)` 역할을 연다.
  - 이 역할은 local bind endpoint가 없으면 다른 프로세스에서 접근할 수
    없으므로, builder 안에서 `Bind(...)`를 같이 지정해야 한다.
- `EnableClient(...)`
  - request/send outbound 호출용 `DEALER(client)` 역할을 연다.
- `EnablePublisher(...)`
  - 일반 channel event publish 역할을 연다.
  - 이 역할도 remote subscriber가 연결될 local bind endpoint가 필요하므로
    builder 안에서 `Bind(...)`를 같이 지정해야 한다.
- `EnableSubscriber(...)`
  - 일반 channel event subscribe 역할을 연다.
- `AddStreamNode(...)`
  - framework Header 기반 packet session을 받을 STREAM node를 등록한다.
  - 한 node에는 stream session을 하나만 등록할 수 있다.
  - 같은 node에 session을 둘 이상 함께 등록하는 것은 허용하지 않는다.

중요한 규칙은 다음과 같다.

- 수동 연결은 `channel` 전체가 아니라 `channel + role` 단위다.
- startup 수동 연결은 역할을 켜는 메서드의 endpoint 인자로 지정한다.
- 같은 역할 안에서 location store 자동 연결과 manual 연결을 섞지 않는다.
- `client` 와 `subscriber` 는 서로 다른 연결 집합으로 본다.
- publisher는 outbound fan-out submit 역할로 간주한다. publisher의 manual peer도
  capability별 `IZLinkEndpointConnections`로 관리한다.

### 6.2 channel 수동 연결 설정

```csharp
public interface IZLinkEndpointConnections
{
    void Connect(string endpoint);
    void Disconnect(string endpoint);
    IReadOnlyList<string> ListConnections();
}
```

수동 연결은 startup builder에서 역할 단위로 등록할 수 있고, manual 모드 역할은 host
시작 뒤에도 capability별 `IZLinkEndpointConnections`로 endpoint를 변경할 수 있다.

client 역할은 `EnableClient(endpoint)`, subscriber 역할은
`EnableSubscriber(endpoint)` 처럼 역할을 켜는 메서드에서 endpoint 를 직접 받는다.
endpoint 인자는 startup 설정이며 connection property는 실행 중 manual 역할의 연결
집합을 바꾸는 runtime handle이다.

location store 자동 연결 모드인 역할은 peer 집합의 소유권이 location store 에 있다. 따라서
수동 연결이 필요하면 해당 역할을 manual 모드로 등록해야 한다.

### 6.3 Spot 관리와 등록 인터페이스

`IZLinkSpotManager` 는 `SpotNode` 안에서 spot 인스턴스를 생성하고 종료하는
데 사용하는 interface 다.

역할 분담은 다음과 같다.

- spot 을 만드는 주체는 handler 가 아니라 manager 다.
- handler 는 들어오는 메시지를 처리하는 역할만 맡는다.

```csharp
public readonly record struct ZLinkSpotCreateResponse(
    bool Accepted,
    ZLinkMessage? Reply)
{
    public static ZLinkSpotCreateResponse Accept(ZLinkMessage? reply = null);

    public static ZLinkSpotCreateResponse Reject(ZLinkMessage? reply = null);
}

public enum ZLinkSpotCreateState
{
    Existing,
    Created,
    Rejected
}

public readonly record struct ZLinkSpotCreateResult(
    RoutingId SpotRid,
    ZLinkSpotCreateState State,
    ZLinkMessage? Reply);

public readonly record struct ZLinkSpotInfo(
    RoutingId SpotRid);

public interface IZLinkSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot, TRequest>(
        TRequest request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot, TRequest>(
        RoutingId spotRid,
        TRequest request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotInfo?> FindAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        CancellationToken cancellationToken = default);

    ValueTask<bool> CloseAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}
```

`CreateAsync` 와 `GetOrCreateAsync` 는 각각 다음 상황에 대응한다.

- `CreateAsync<TSpot>()`
  - generic 타입으로 factory를 선택하고, runtime이 새 `spotRid`를 발급한다. create
    callback에는 빈 `ZLinkMessage`가 전달된다.
- `CreateAsync<TSpot>(request)`
  - runtime이 새 `spotRid`를 발급하고, create request DTO 또는 `ZLinkMessage`를
    `IZLinkSpot.OnCreateAsync(...)`에 전달한다.
- `GetOrCreateAsync<TSpot>(RoutingId spotRid, request)`
  - generic 타입으로 factory를 선택하되, 호출자가 특정 logical spot rid를 직접
    지정한다. 이미 같은 `spotRid`가 있으면 기존 spot을 반환하고 새
    `request`는 전달하지 않는다.

반환값은 세 가지를 묶어서 돌려준다. `spotRid`, 생성 상태, 그리고 create callback이
돌려준 선택적 reply `ZLinkMessage`다. `State`는 이미 있던 spot을 반환한 경우
`Existing`, 이번 요청으로 새 spot을 만든 경우 `Created`, create callback이 거부한
경우 `Rejected`다. `Existing`에서는 create callback을 호출하지 않으므로 새 reply는
없다.

반환값은 장기 보관하는 instance handle이 아니라 생성 결과다.

`OnCreateAsync(...)` 는 spot 생성 요청의 단일 `ZLinkMessage`를 받는 lifecycle
callback이다. framework는 등록된 codec registry를 사용해 JSON, Protobuf, MessagePack 또는
custom codec payload를 encode/decode한다. application은 DTO 또는 `ZLinkMessage`를 사용하고,
handler 안에서 raw `Message` codec helper를 직접 호출하지 않는다.
`CreateAsync<TSpot>()`처럼 create payload가 없는 overload는 빈 `ZLinkMessage`를
넘긴 것과 같다. 같은 `spotRid`에 대해 동시에 `GetOrCreateAsync(...)`가 들어오면
처음 생성에 사용된 request만 `OnCreateAsync(...)`로 전달된다. 나중 caller의
request는 재전달하지 않고, 생성이 성공하면 `Existing`, 거부되면 `Rejected` 결과를
받는다.

`TSpot`은 생성 요청의 framework type discriminator다. framework는 이 타입으로
등록된 factory를 선택한다. string 기반 spot rid은 public contract에 포함하지
않는다. 같은 `spotRid`에 대해 기존 entry의 Spot 타입과 다른 `TSpot`으로
`GetOrCreateAsync(...)`를 호출하면 `SpotTypeMismatch`로 실패해야 한다.

remote 생성 요청도 같은 구조를 따른다. public framework 표면은 string spot rid을
받지 않는다. remote 노드로 생성 요청을 전달해야 하는 구현은 framework 내부 metadata로
factory를 식별하고, metadata 뒤의 message part들을 create payload로 전달한다.
`spotRid`는 `GetOrCreateAsync(...)`처럼 명시적 logical spot을 확보하는 요청에서는
required이고, 수신 node가 새 id를 발급하는 create 요청에서는 optional이다.

`FindAsync(...)` 와 `ListAsync(...)` 는 조회 표면이다. 조회 결과는 public
식별자인 `SpotRid`만 돌려준다. Spot 타입이나 factory 선택 정보는 framework 내부
소유이며 application contract로 노출하지 않는다.

`CloseAsync(...)` 는 user Spot lifecycle 을 정상 종료한다. 대상 spot 이 없거나,
해당 spot 에 actor 가 남아 있으면 `false` 를 반환한다. `true` 를 반환한 경우 framework
는 `OnClosingAsync(...)` 를 spot 실행 문맥에서 호출하고, 이후 C API 의
`zlink_spot_destroy()` 로 이어지는 native SPOT facade 정리를 수행한다. Entry Spot 은
SpotNode lifecycle 이 소유하므로 이 API의 대상이 아니다.

SPOT topology 계약에서는 높은 수준의 public API 표면에
`spotRid -> targetRid` 주소를 직접 노출하지 않는다.

메시징 handle 조회는 `IZLinkSpotHandleResolver`가 담당한다. framework의 기본
SPOT 표면은 다음 순서로 설명한다. spot `RoutingId`, channel publish, channel
send/request 다.

`SPOT` 등록은 다음 builder 표면으로 고정한다. socket과 route 설정 타입은
§16의 `IZLinkSocketConfig`, `IZLinkRouteConfig`, `IZLinkSpotPublisherConfig`,
`IZLinkSpotSubscriberConfig` 정의를 사용한다.

```csharp
public interface IZLinkSpotNodeBuilder
{
    IZLinkSpotNodeBuilder EnableRouter(string endpoint);

    IZLinkSpotNodeBuilder ConnectRouter(string endpoint);

    IZLinkSpotNodeBuilder ConnectRouter(RoutingId peerRid, string endpoint);

    IZLinkSpotNodeBuilder SetRoutingId(RoutingId routingId);

    IZLinkSocketConfig ConfigureRouterSocket();

    IZLinkRouteConfig ConfigureRouterRouting();

    IZLinkSpotNodeBuilder EnablePubSub(string endpoint);

    IZLinkSpotNodeBuilder ConnectPeerPub(string endpoint);

    IZLinkEndpointConnections RouterConnections { get; }

    IZLinkEndpointConnections PubSubConnections { get; }

    IZLinkEndpointConnections ChannelClientConnections { get; }

    IZLinkEndpointConnections PublisherConnections { get; }


    IZLinkSpotPublisherConfig ConfigurePubSubPublisher();

    IZLinkSpotSubscriberConfig ConfigurePubSubSubscriber();






    IZLinkEntrySpotOptions ConfigureEntrySpot();

    IZLinkSpotNodeBuilder SetEntrySpotRoutingId(RoutingId routingId);

    IZLinkSpotNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot;

    IZLinkSpotNodeBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot;

    IZLinkSpotNodeBuilder AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory;

    IZLinkSpotNodeBuilder AddActorTransferAdapter<TActor, TAdapter>(string actorType)
        where TActor : IZLinkActor
        where TAdapter : class, IZLinkActorTransferAdapter<TActor>;
}


public interface IZLinkEntrySpotOptions
{
    RoutingId RoutingId { get; set; }
}
```

각 함수의 의미는 아래와 같다.

connection handle 네 이름이 네 개의 독립 socket을 뜻하지는 않는다.
`ChannelClientConnections`는 `RouterConnections`와 동일한 router capability 및 동일한
connection state를 나타내는 역할 중심 이름이다. `PublisherConnections`도
`PubSubConnections`와 동일한 peer publisher endpoint state를 나타낸다. 어느 한 이름으로
endpoint를 변경하면 짝이 되는 다른 이름에서도 같은 변경을 관찰한다. 이 별칭은 공통
framework builder의 역할별 사용성을 유지하기 위한 것이며, 별도 channel client나 publisher
socket을 추가하지 않는다.

- `EnableRouter(...)`
  - spot-to-spot routed packet을 처리할 local router 역할을 켠다.
- `EnablePubSub(...)`
  - 현재 SPOT channel 안의 publish/subscribe 역할을 켠다.
- 다른 channel로 send/request 하려면 해당 client/server channel에서
  `EnableClient(...)`를 설정한다. Spot node builder는 별도 channel client를
  부착하지 않는다.
- local spot 인스턴스가 없는 외부 노드가 특정 SPOT channel로 publish할 때는
  outbound publisher 역할을 설정한다.
- `AddSpotFactory<TSpot>()`
  - 이 node가 생성하고 소유할 spot factory를 타입 기준으로 등록한다.
  - 같은 `SpotNode` 안에서 같은 `TSpot`을 다시 등록하면 기존 값을 덮어쓰지 않고
    예외를 던진다.
  - `CreateAsync<TSpot>(...)`와 `GetOrCreateAsync<TSpot>(...)`는 이 타입과
    정확히 일치하는 factory를 고른다.
- `AddEntrySpot<TEntrySpot>()`
  - 이 node의 자동 Entry Spot에 붙일 application registry를 등록한다.
  - 등록하지 않으면 framework는 빈 Entry Spot registry를 사용한다. 이 경우 Entry Spot에
    actor packet이 도착했는데 매칭 handler가 없으면 명확한 dispatch error로 실패한다.
  - 같은 `SpotNode` 안에서 두 번 이상 등록하면 startup validation 오류다.
  - `TEntrySpot`은 node당 하나의 application object로 만든다. `Configure()`는 node가
    시작되기 전에 한 번 호출하고, `OnInitializeAsync(...)`는 native Entry Spot이 준비된 뒤
    Entry Spot 실행 문맥에서 호출한다. `OnClosingAsync(...)`는 node shutdown 시 같은
    실행 문맥에서 한 번 호출한다. Entry Spot actor packet은 이 실행 문맥에 전역으로
    세우지 않고 대상 actor의 mailbox로 보낸다.

여기서 수동 연결은 channel 쪽과 마찬가지로 역할 단위로 다뤄야 한다.

예를 들어 `router`, channel client, publish 쪽은 모두 각 역할이
사용할 endpoint 집합을 따로 관리한다.

endpoint 인자는 한군데에 모아 두지 않고 **역할별 메서드로 분리한다.** SPOT `router`의 수동
연결만 두 형태를 갖는다 — endpoint만 주는 형태와, **peer routing id를 이미 아는 경우** peer id와
endpoint를 함께 주는 형태다(아래 `ConnectRouter` 참조).

소켓 옵션은 `ConfigureRouterSocket()`, `ConfigureRouterRouting()`,
`ConfigurePubSubPublisher()`, `ConfigurePubSubSubscriber()`가 역할별 설정 객체를
반환한다. actor factory와 transfer adapter는 framework 등록 루트가 아니라
이 SpotNode builder가 소유한다.

- `ConnectRouter(RoutingId, endpoint)`
  - 수동 연결 대상의 routing id를 이미 아는 경우 peer id와 endpoint를 함께 등록한다.
- `Timeout(...)`
  - request 한 번에만 적용되는 호출 단위 옵션이다.
  - 실제 바인딩에서도 `DealerSocket.RequestAsync(..., TimeSpan timeout, ...)`,
    `RouterSocket.RequestAsync(..., TimeSpan timeout, ...)`,
    `Spot.RequestToChannelAsync(..., TimeSpan timeout, ...)`처럼 호출 인자로 받는다.
  - 위 등록 설정과 달리 역할 runtime 기본값을 바꾸지 않는다.

`AddSpotMesh(channelName)` 는 SPOT channel 이름과 이 프로세스의 단일 node 를
함께 소유한다. 그래서 같은 channel 안에 node 를 다시 추가하는 함수는 두지 않는다.

Session relay 도 같은 원칙을 따른다. 별도 relay node builder 를 두지 않고,
`AddSpotMesh(...)` 가 만든 router-capable SpotNode 를 framework 가 relay ingress 로
사용한다.

정리하면, `SPOT` 등록 시점에도 다음 축들을 함께 드러내는 편이 맞다.

- local routed router 역할 활성화
- local SPOT pub/sub 역할 활성화
- 외부 channel 호출용 egress bridge
- 외부 SPOT publish client attach

## 7. Timer 인터페이스

이 절은 spot lifecycle 에서 사용하는 timer 의 공개 표면을 정의한다.

공개 계약에서 spot lifecycle 안에 등록한 `Context.AddTimer<THandler>(...)`
가 반환하는 timer handle 이다.

```csharp
public interface IZLinkTimer : IAsyncDisposable
{
    bool IsDisposed { get; }

    ValueTask CancelAsync();
}

public sealed record ZLinkTimerOptions
{
    public ZLinkTimerOverrunPolicy OverrunPolicy { get; init; } =
        ZLinkTimerOverrunPolicy.SkipLateTicks;

    public int MaxCatchUpTicks { get; init; } = 1;

    public bool StopOnUnhandledException { get; init; }
}

public enum ZLinkTimerOverrunPolicy
{
    SkipLateTicks = 1,
    CatchUpBounded = 2,
    DelayNextTick = 3
}

public readonly record struct ZLinkTimerTick(
    string Name,
    ulong DeliveryIndex,
    ulong ScheduledIndex,
    TimeSpan Period,
    DateTimeOffset ScheduledAt,
    DateTimeOffset StartedAt,
    TimeSpan ScheduledElapsed,
    TimeSpan StartedElapsed,
    TimeSpan Delay,
    ulong SkippedTicks);
```

framework 의 timer abstraction 은 low-level `.NET` binding 의 native
timer 를 그대로 노출하지 않는다.

동작 흐름은 다음과 같다. framework runtime 이 managed timer 를 만든 뒤,
각 tick 을 handler 실행 문맥으로 넘긴다. user Spot timer 는 spot 직렬 실행
경로로 들어가고, Entry Spot timer 는 Entry Spot의 route packet, subscription,
worker completion과 같은 직렬 실행 줄로 들어간다. 그곳에서
`IZLinkSpotTimerHandler<TSpot>.HandleAsync(...)` 를 호출한다.

`ZLinkTimerTick` 은 timer 이름, handler 에 실제 전달된 callback 번호
(`DeliveryIndex`), fixed-rate 시간표의 tick 번호 (`ScheduledIndex`), 예정 시각,
시작 시각, 지연, 건너뛴 tick 수를 담는다. 지연과 skip 계산은 monotonic clock
기준으로 한다. `DateTimeOffset` 값은 로그와 운영 관찰을 위한 wall-clock 값이다.

`ZLinkTimerOverrunPolicy` 의 의미는 다음과 같다.

- `SkipLateTicks` 는 늦은 tick 을 합쳐서 건너뛰고 최신 예정 시각 기준으로 이어 간다.
- `CatchUpBounded` 는 밀린 tick 을 `MaxCatchUpTicks` 개 callback 까지만 연속 실행한다.
- `DelayNextTick` 은 fixed-rate 가 아니라 handler 완료 뒤 다시 period 를 기다리는
  fixed-delay 정책이다.

`MaxCatchUpTicks` 는 `CatchUpBounded` 에서만 의미가 있으며 `0`보다 커야 한다.
다른 정책에서는 framework 가 이 값을 scheduling 에 사용하지 않는다. 알 수 없는
`ZLinkTimerOverrunPolicy` 값은 설정 오류로 처리한다.

handler 예외는 기본적으로 runtime monitoring 의 `TimerHandlerFailed` event 로
기록하고 timer 는 계속 실행한다. `StopOnUnhandledException` 이 `true` 이면 첫 번째
처리되지 않은 예외 뒤 timer 를 중단하고 `TimerStoppedAfterUnhandledException` event
를 기록한다.

따라서 `IZLinkTimer.CancelAsync()` 는 native `Timer.Stop()` 을 감싸는
wrapper 가 아니다. framework 가 만든 managed timer loop 를 중단하는
표면으로 읽는 편이 맞다.

timer 가 어떤 실행 문맥에서 callback 을 호출하는지가 핵심이다.

- 현재 방향에서는 timer 를 별도의 client scheduler 로 두지 않는다.
- spot timer 는 framework 가 만든 managed timer 를 사용한다.
- user Spot timer handler 호출은 동일한 spot 실행 문맥 안에서 직렬화한다.
- packet, subscribe, channel reply callback, timer callback 은 모두 같은
  user Spot execution context 규칙을 따른다.
- user Spot 안의 actor packet 도 마찬가지다. 먼저 actor 별 mailbox 에서
  해당 actor 의 순서를 지킨다. 최종 handler 실행은 동일한 spot 실행 문맥
  에서 진행된다.
- 반면 Entry Spot actor packet 은 actor 별 mailbox 에서 처리한다. stream
  session 에서 actor 로 relay 되는 packet 은 두 단계로 처리된다. 먼저
  actor 별 순서를 보존한 뒤, 현재 actor 위치에 맞는 Entry Spot handler
  또는 user Spot 실행 queue 로 넘긴다.
- Entry Spot timer callback은 Entry Spot 전체 실행 줄에서 처리한다. 같은 timer
  instance 안에서는 이전 callback이 끝나기 전에 다음 callback을 겹쳐 실행하지
  않으며, route packet, subscription과 다른 Entry Spot callback과도 직렬 순서를 지킨다.

## 8. Handler Filter

### 8.1 서비스 레이어 AOP와의 경계

**AOP는 handler 메서드 자체가 아니라, handler가 주입받는 서비스 계층에서 동작한다.**

handler가 `IUserService`를 주입받고 그 서비스가 decorator·proxy·interceptor로 감싸져 있으면 그
AOP가 그대로 적용된다. **어떤 AOP 라이브러리를 쓸지는 그 라이브러리의 규칙을 따른다.**

**framework는 handler 메서드에 AOP를 걸지 않는다.** handler 자체에 걸어야 하는 관심사는
handler filter(§8)를 사용한다.


이 절은 ZLink handler 호출 전후의 공통 처리를 담당하는 filter 표면을
정의한다.

HTTP middleware 와는 별개의 메커니즘이다. ZLink handler 전후의 공통
처리를 담당한다.

```csharp
// filter pipeline의 next 단계를 나타내는 delegate.
// 호출하면 다음 filter 또는 실제 handler가 실행되고 결과가 반환된다.
public delegate ValueTask<object?> ZLinkHandlerDelegate(
    CancellationToken cancellationToken);

// filter에 전달되는 호출 context.
// 역직렬화된 message와 handler context를 함께 포함한다.
public sealed class ZLinkHandlerInvocation
{
    public object? Message { get; }
    public IZLinkHandlerContext Context { get; }
    public string? ChannelName { get; }
    public string? PacketName { get; }
}

public interface IZLinkHandlerFilter
{
    ValueTask<object?> InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerDelegate next,
        CancellationToken cancellationToken);
}
```

등록은 다음과 같이 한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.UseFilter<LoggingZLinkFilter>();
    options.UseFilter<ValidationZLinkFilter>();
});
```

filter 역시 framework 가 직접 `new` 로 만들지 않는다. `.NET DI` 에서
resolve 한다.

주된 용도는 다음과 같다.

- logging
- validation
- authorization
- metrics
- exception → framework 표준 오류 응답 매핑

기존 `ASP.NET Core` 의 HTTP middleware (`app.Use(...)`) 는 HTTP pipeline
전용이다. 즉 ZLink handler 에는 자동으로 적용되지 않는다. 공통 처리가
필요하다면 이 `IZLinkHandlerFilter` 를 사용한다.

## 9. Request reply 타입 지정

이 절은 request/reply 타입을 호출부에서 어떻게 지정하는지 정리한다.

request 메시지 타입에는 framework 전용 marker interface 를 붙이지 않는다.
규칙은 다음과 같다.

- 메시지는 codec 이 직렬화할 payload 계약만 표현해야 한다.
- reply 타입은 호출부에서 `Async<TReply>(...)` 로 명시한다.

```csharp
var reply = await client
    .RequestToChannel("profile", new GetProfileRequest { AccountId = accountId })
    .Async<GetProfileReply>(cancellationToken);
```

양쪽 진입점에서의 동작은 다음과 같다.

- handler 는 메서드 시그니처만으로 request/reply 타입을 결정한다.
- client 호출부는 request 메시지를 보낼 때 packet 이름과 payload 타입만
  넘기고, 기다릴 reply 타입은 `Async<TReply>(...)` 에서 지정한다.

기본 packet key 는 `Type.Name` 을 사용한다 (예: `GetProfileRequest`).

이 기본 이름이 적절하지 않은 경우에는, payload 타입에 명시적 metadata 를
부여할 수 있다.

```csharp
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct,
    AllowMultiple = false)]
public sealed class ZLinkPacketAttribute : Attribute
{
    public ZLinkPacketAttribute(string packetName);
    public string PacketName { get; }
}
```

이 metadata는 outbound 기본 해석과 inbound handler 기본 매핑 양쪽에서 공통으로
사용한다.

## 10. Location 조회 인터페이스

위치 조회 인터페이스는 infrastructure 성격이므로 상세 정의는
[system-structure.ko.md](01-system-structure.ko.md)와
[공통 location runtime 스펙](../../40-location-runtime.ko.md)에 있다.
여기서는 역할만 요약한다.

location store 를 등록한 배포에서 DI 로 노출되는 조회 표면이다. 캐시가 없고,
모든 조회는 store 에 도달하며 owner lease join 으로 유효성을 판정한다.

```csharp
public interface IZLinkLocationRuntimeQuery
{
    ValueTask<ZLinkLocationRuntimeStatus> GetStatusAsync(
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeerLocationsAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotLocationsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorLocationsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRouteLocationsAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkLocationTopologyEntry>> ListTopologyAsync(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkLocationServiceSummary>> ListServiceSummariesAsync(
        ZLinkLocationServiceSummaryFilter filter,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotHandleResolver
{
    ValueTask<SpotHandle?> ResolveSpotHandleAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorSpotHandleResolver
{
    ValueTask<SpotHandle?> ResolveActorSpotHandleAsync(
        string actorId,
        CancellationToken cancellationToken = default);
}
```

- 조회 API 가 비동기인 이유는 저장소가 프로세스 밖(store)에 있기 때문이다.
- 사용할 수 없는 서버의 row는 owner lease 만료 후 성공 결과에서 자동 제외된다.
- 메시징 resolver는 불투명한 `SpotHandle`을 반환한다. handle이 내부 주소 snapshot과
  안전한 1회 갱신을 소유한다([spot 주소 메시징](../../24-spot-address-messaging.ko.md)).

### 10.2.1 Redis location store

공식 Redis extension 은 `Zlink.Framework.Locations.Redis` 패키지의
`ZLinkRedisLocationStore` 다. 옵션은 빌더 형식이다:
`new ZLinkRedisLocationStore(redis => redis.SetConnectionString(...).SetKeyPrefix(...))`
(`SetConfiguration(ConfigurationOptions)` 으로 StackExchange.Redis 옵션 직접 전달 가능).

```csharp
public sealed class ZLinkRedisLocationOptions
{
    public string? ConnectionString { get; set; }
    public ConfigurationOptions? ConfigurationOptions { get; set; }
    public string KeyPrefix { get; set; } = string.Empty;

    public ZLinkRedisLocationOptions SetConnectionString(string connectionString);
    public ZLinkRedisLocationOptions SetConfiguration(ConfigurationOptions configuration);
    public ZLinkRedisLocationOptions SetKeyPrefix(string keyPrefix);
}

public sealed class ZLinkRedisLocationStore :
    IZLinkLocationStore,
    IZLinkLocationChangeStampStore,
    IAsyncDisposable
{
    public ZLinkRedisLocationStore(ZLinkRedisLocationOptions options);
    public ZLinkRedisLocationStore(Action<ZLinkRedisLocationOptions> configure);
}
```

두 constructor는 같은 설정 계약을 사용한다. `KeyPrefix`는 빈 문자열일 수 없고,
`ConnectionString`과 `ConfigurationOptions` 중 하나를 지정해야 한다.
`ConfigurationOptions`를 지정하면 `ConnectionString`보다 먼저 적용한다.

### 10.3 runtime monitoring

runtime monitoring 은 운영 표면이다. socket 하부 monitor 와 spot의 주기적 상태 비교 이벤트를 함께
감싸는 역할이다.

공용 handler 시그니처는 다음과 같다.

```csharp
public interface IZLinkMonitoringOptions
{
    void AddSocketEvents(
        string sourceName,
        params ZLinkSocketEventKind[] events);


    void AddSpotEvents(
        string sourceName,
        TimeSpan interval);

    void AddLocationRuntimeEvents(
        string sourceName,
        TimeSpan interval);

    void AddLocationPeerEvents(string sourceName);

    void AddLocationSpotEvents(string sourceName);

    void AddLocationActorEvents(string sourceName);

    void AddLocationRouteEvents(string sourceName);
}

public interface IZLinkRuntimeEvent
{
    string SourceName { get; }

    DateTimeOffset Timestamp { get; }
}

public interface IZLinkRuntimeEventHandler<in TEvent>
    where TEvent : IZLinkRuntimeEvent
{
    ValueTask HandleAsync(
        TEvent @event,
        CancellationToken cancellationToken);
}

public interface IZLinkRuntimeEventPublisher
{
    ValueTask PublishAsync<TEvent>(
        TEvent @event,
        CancellationToken cancellationToken)
        where TEvent : IZLinkRuntimeEvent;
}
```

event 표면은 두 단계로 나누어 둔다. event kind 는 enum 으로, 실제
callback payload 는 record struct 로 둔다.

이유는 단순하다. enum 만으로는 source name, routing id, endpoint, 한 시점에 읽은 상태 목록
같은 운영 정보를 한꺼번에 전달하기 어렵기 때문이다.

native monitor enum 과 raw status 값에 대해서도 비슷한 원칙을 적용한다.
framework 가 항상 보장하는 필수 계약이 아니라, backend 가 제공할 수
있을 때에만 채워지는 optional diagnostic detail 로 둔다. 이 방향이 backend
교체 정책과도 부합한다.

`AddSocketEvents(...)` 에서 event 목록을 비워 두면 어떻게 해석할까. 해당
source 가 발생시킬 수 있는 모든 logical event kind 를 구독한다는 의미로
해석한다.

application 이 직접 구현하는 쪽은 보통 `IZLinkRuntimeEventHandler<TEvent>` 이다.
`IZLinkRuntimeEventPublisher` 는 framework runtime 이 즉시 발생 event 를 monitoring
dispatcher 로 넘길 때 쓰는 public contract 다. `AddZLinkMonitoring(...)` 이 구성된
host 에서만 기본 publisher 가 등록된다.

```csharp
public enum ZLinkSocketEventKind
{
    Connected = 0,
    ConnectionReady,
    Disconnected,
    HandshakeFailed,
    PeerAdmissionChanged,
    Closed,
    Internal
}

public readonly record struct ZLinkSocketEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkSocketEventKind Event,
    RoutingId? RoutingId,
    string LocalAddr,
    string RemoteAddr,
    ZLinkSocketDiagnostic? Diagnostic) : IZLinkRuntimeEvent;

public readonly record struct ZLinkSocketDiagnostic(
    ZLinkSocketNativeEventType NativeEvent,
    uint NativeValue);

public abstract record ZLinkLocationRuntimeEvent(
    string SourceName,
    DateTimeOffset Timestamp) : IZLinkRuntimeEvent
{
    public sealed record StatusChanged(
        string SourceName, DateTimeOffset Timestamp, ZLinkLocationRuntimeStatus Status)
        : ZLinkLocationRuntimeEvent(SourceName, Timestamp);
    public sealed record TopologyChanged(
        string SourceName, DateTimeOffset Timestamp,
        IReadOnlyList<ZLinkLocationTopologyEntry> Topology)
        : ZLinkLocationRuntimeEvent(SourceName, Timestamp);
    public sealed record ServiceSummaryChanged(
        string SourceName, DateTimeOffset Timestamp,
        IReadOnlyList<ZLinkLocationServiceSummary> ServiceSummary)
        : ZLinkLocationRuntimeEvent(SourceName, Timestamp);
    public sealed record StoreFailure(string SourceName, DateTimeOffset Timestamp)
        : ZLinkLocationRuntimeEvent(SourceName, Timestamp);
    public sealed record StoreRecovered(string SourceName, DateTimeOffset Timestamp)
        : ZLinkLocationRuntimeEvent(SourceName, Timestamp);
}

public abstract record ZLinkLocationPeerEvent(
    string SourceName, DateTimeOffset Timestamp) : IZLinkRuntimeEvent
{
    public sealed record RowUpdated(string SourceName, DateTimeOffset Timestamp,
        ZLinkPeerLocationKey Key, ZLinkPeerLocation Peer)
        : ZLinkLocationPeerEvent(SourceName, Timestamp);
    public sealed record RowRemoved(string SourceName, DateTimeOffset Timestamp,
        ZLinkPeerLocationKey Key) : ZLinkLocationPeerEvent(SourceName, Timestamp);
    public sealed record DesiredSetChanged(string SourceName, DateTimeOffset Timestamp,
        ZLinkAutoConnectDesiredSetChange Change)
        : ZLinkLocationPeerEvent(SourceName, Timestamp);
}

public abstract record ZLinkLocationSpotEvent(
    string SourceName, DateTimeOffset Timestamp) : IZLinkRuntimeEvent
{
    public sealed record RowUpdated(string SourceName, DateTimeOffset Timestamp,
        ZLinkSpotLocationKey Key, ZLinkSpotLocation Spot)
        : ZLinkLocationSpotEvent(SourceName, Timestamp);
    public sealed record RowRemoved(string SourceName, DateTimeOffset Timestamp,
        ZLinkSpotLocationKey Key) : ZLinkLocationSpotEvent(SourceName, Timestamp);
    public sealed record ResolveMiss(string SourceName, DateTimeOffset Timestamp,
        ZLinkSpotLocationKey Key) : ZLinkLocationSpotEvent(SourceName, Timestamp);
}

public abstract record ZLinkLocationActorEvent(
    string SourceName, DateTimeOffset Timestamp) : IZLinkRuntimeEvent
{
    public sealed record RowUpdated(string SourceName, DateTimeOffset Timestamp,
        ZLinkActorLocationKey Key, ZLinkActorLocation Actor)
        : ZLinkLocationActorEvent(SourceName, Timestamp);
    public sealed record RowRemoved(string SourceName, DateTimeOffset Timestamp,
        ZLinkActorLocationKey Key) : ZLinkLocationActorEvent(SourceName, Timestamp);
    public sealed record ResolveMiss(string SourceName, DateTimeOffset Timestamp,
        ZLinkActorLocationKey Key) : ZLinkLocationActorEvent(SourceName, Timestamp);
}

public abstract record ZLinkLocationRouteEvent(
    string SourceName, DateTimeOffset Timestamp) : IZLinkRuntimeEvent
{
    public sealed record RowUpdated(string SourceName, DateTimeOffset Timestamp,
        ZLinkRouteLocationKey Key, ZLinkRouteLocation Route)
        : ZLinkLocationRouteEvent(SourceName, Timestamp);
    public sealed record RowRemoved(string SourceName, DateTimeOffset Timestamp,
        ZLinkRouteLocationKey Key) : ZLinkLocationRouteEvent(SourceName, Timestamp);
    public sealed record ResolveMiss(string SourceName, DateTimeOffset Timestamp,
        ZLinkRouteLocationKey Key) : ZLinkLocationRouteEvent(SourceName, Timestamp);
}

public readonly record struct ZLinkSpotTimerDiagnostic(
    RoutingId SpotRid,
    bool IsEntrySpot,
    string TimerName,
    string HandlerType,
    ulong DeliveryIndex,
    ulong ScheduledIndex,
    string ExceptionType,
    string ExceptionMessage);

public abstract record ZLinkSpotEvent(
    string SourceName, DateTimeOffset Timestamp) : IZLinkRuntimeEvent
{
    public sealed record StatusChanged(
        string SourceName, DateTimeOffset Timestamp, ZLinkSpotNodeStatus Status)
        : ZLinkSpotEvent(SourceName, Timestamp);
    public sealed record PeersChanged(
        string SourceName, DateTimeOffset Timestamp,
        IReadOnlyList<ZLinkSpotNodePeerEntry> Peers)
        : ZLinkSpotEvent(SourceName, Timestamp);
    public sealed record SubjectsChanged(
        string SourceName, DateTimeOffset Timestamp,
        IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects)
        : ZLinkSpotEvent(SourceName, Timestamp);
    public sealed record TimerHandlerFailed(
        string SourceName, DateTimeOffset Timestamp, ZLinkSpotTimerDiagnostic Diagnostic)
        : ZLinkSpotEvent(SourceName, Timestamp);
    public sealed record TimerStoppedAfterUnhandledException(
        string SourceName, DateTimeOffset Timestamp, ZLinkSpotTimerDiagnostic Diagnostic)
        : ZLinkSpotEvent(SourceName, Timestamp);
}
```

`TimerHandlerFailed` 와 `TimerStoppedAfterUnhandledException` 은 polling interval 을
기다리는 주기적 상태 비교 event 가 아니다. timer handler 에서 처리되지 않은 예외가
발생한 시점에 즉시 발행된다. exception 객체 자체는 public payload 에 넣지 않고,
`ZLinkSpotTimerDiagnostic` 에 직렬화 가능한 요약 정보를 담는다.

`ZLinkSpotNodeStatus` 와 `ZLinkSpotNodePeerEntry` 의 첫 번째 필드는
`ChannelName` 이다.

이 필드는 channel 단위로 통일되어 `ChannelName` 이다.

이 두 record 를 필드 단위로 풀어 쓰는 다른 문서들도, 이 이름을 기준으로
참고하면 된다.

이 문서에서 각 source 의 의미는 다음과 같이 정리한다.

- socket event
  - 하부 `SocketMonitor` 를 감싸는 표면이다.
  - source 이름은 `channel + capability` 또는 `spot node + capability`
    형태를 사용한다.
  - 예: `profile.server`, `profile.client`, `stage-node.router`
- location event
  - 하부 raw monitor 가 아니다. location runtime query 의 polling + diff 로
    합성한다(`GetStatusAsync()`, `ListTopologyAsync()`, `ListServiceSummariesAsync()`).
  - store 장애는 source 를 죽이지 않고 `StoreFailure` 이벤트로 강등된다.
- spot event
  - 하부 raw monitor 가 아니다. 다음 호출들의 polling + diff 로 합성한다.
    `Status()`, `Peers()`, `Subjects()`.

### 10.4 메시지 흐름 추적 (dispatch 관측)

monitoring 이 socket/location/spot **runtime 변화**를 다룬다면, 메시지 흐름 추적은 한 메시지의
생애주기(왔나/처리됐나/응답됐나/보냈나/응답받았나)를 dispatch 길목에서 관측한다. 공통 의미
(로그 모드·phase·event·observer·off 제로코스트 성능 계약·출력 라우팅·길목·스트림
correlation_id 와이어)는 [공통 스펙 — 메시지 흐름 추적](../../52-message-flow-tracing.ko.md)이
소유한다. 이 절은 그 의미의 `.NET` 표면만 적는다. dispatch **제어**가 아니라 **관측**이며,
observer 실패가 메시지 처리나 응답 전송을 깨지 않는다.

#### 표면

| 공통 개념 | `.NET` 타입 / 멤버 |
|-----------|---------------------|
| 로그 모드 | `ZLinkMessageFlowLogMode` { `Off`, `ErrorsOnly`(기본), `KeyTransitions`, `Verbose`, `Diagnostic` } |
| outcome | `ZLinkMessageFlowOutcome` { `Received`, `Dispatched`, `Replied`, `Dropped`, `Sent`, `ReplyReceived`, `Error` } |
| event | `ZLinkMessageFlowEvent`(record): `Outcome`, `Surface`, `MessageKind`, `PacketName`, `ChannelName`, `Topic`, `CorrelationId`, `SourceRid`, `LocalRid`, `PeerRid`, `SocketRole`, `SpotRid`, `ActorId`, `MessageSize`, `FlowId`, nullable `FlowOrigin`, 오류 필드 |
| observer | `IZLinkMessageFlowObserver.OnMessageFlowAsync(ZLinkMessageFlowEvent, CancellationToken)` |
| 진단 옵션(read-only) | `IZLinkDispatchOptions.Diagnostics` → `IZLinkDiagnosticsOptions` { `MessageFlow`, `EffectiveMessageFlow`, `SampleRate`, `IncludeMessageSizes`, `LogFile`, `Label` } |
| 런타임 토글 | `IZLinkMessageFlowControl.SetMessageFlowMode(...)` / `MessageFlowMode` (DI singleton) |

게이팅(공통 규칙): `Dropped`·에러는 `ErrorsOnly` 이상, 성공 전이(`Received`/`Dispatched`/
`Replied`/`Sent`/`ReplyReceived`)는 `KeyTransitions` 이상에서 발화한다. `SampleRate<1`은 성공
전이만 thinning하고 `Dropped`·에러는 항상 통과한다.

#### 10.4.1 설정 (builder 전용)

진단 필드는 read-only이며 `ConfigureDispatch()` fluent 체인으로만 설정한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.ConfigureDispatch()
        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
        .TraceLogFile("logs/flow-api.log")   // 지정=전용 파일(앱 로그와 분리)
        .TraceLabel("api")                  // 구조화 필드 label= 식별자
        .IncludeMessageSizes(true)           // Verbose에서 size= 출력
        .SetMessageFlowObserver<ApiFlowObserver>();   // 선택: 콜렉터/OTel 어댑터(앱 레이어)
});
```

- `TraceLogFile` 지정 시 트레이싱/에러는 전용 파일로만 가고 앱 `ILogger`와 섞이지 않는다.
  미지정 + 앱 로거 sink 있으면 통합, 둘 다 없으면 표준 에러스트림 폴백.
- 출력은 카테고리 `zlink.framework.dispatch` + 구조화 필드(phase/surface/kind/packet/channel/
  topic/corr/src/spot/actor/size/node)로 나가 콜렉터가 정규식 파싱 없이 ingest할 수 있다.
- `Off`이고 explicit observer도 없을 때는 log event를 생성하지 않는다(호출부 가드 + lazy).
  explicit observer를 등록한 경우에는 로그 모드와 별개로 observer에 전달할 event를 발행한다.

#### 10.4.2 런타임 토글

`IZLinkMessageFlowControl`을 DI에서 받아 재시작 없이 모드를 바꾼다. 공유 live cell을 모든
surface가 읽으므로 즉시 반영된다. `MessageFlow(...)`는 seed(기본값)다.

```csharp
var control = app.Services.GetRequiredService<IZLinkMessageFlowControl>();
control.SetMessageFlowMode(ZLinkMessageFlowLogMode.KeyTransitions);  // off→on, 즉시 반영
```

#### 10.4.3 관측 백엔드 경계

framework 가 제공하는 것은 `CorrelationId` + 구조화 필드 + observer 훅까지다. OpenTelemetry /
span / 외부 콜렉터(Loki/ELK 등) 어댑터는 앱이 `IZLinkMessageFlowObserver` 콜백에서 받아 끼운다.
framework 는 OTel에 의존하지 않는다(공통 스펙 §6 경계 원칙).

#### 10.4.4 샘플

Bingo 3노드(Api/Play/Session)는 각자 `MessageFlow(KeyTransitions)` +
`TraceLogFile(SampleFlowLog.Path(role))` + `TraceLabel(role)`로 분리 파일 로깅을 시연한다
(`BINGO_LOG_DIR`로 로그 디렉토리 override). 한 요청을 `corr=`로 grep하면 노드 간
`outcome=sent`→`outcome=received`→`outcome=replied`→`outcome=reply-received`가 시간순으로 이어진다.

### 10.5 런타임 메트릭 (runtime metrics)

공통 의미(계기 카탈로그·종류·라벨·성능 계약)는 [공통 스펙 — 런타임 메트릭](../../51-runtime-metrics.ko.md)이
소유한다. 이 절은 `.NET` 표면만 적는다.

> **설계 원칙(깊은 모듈): 공통 케이스는 무설정.** framework는 안정된 이름의 `Meter` 하나로 카탈로그
> 계기를 방출하고, 앱은 자기 OpenTelemetry 파이프라인에 그 meter만 포함한다. per-계기 API는
> 노출하지 않는다 — 계기 갱신 지점·라벨은 framework 내부에 있고, 앱이 배우는 것은 meter 이름
> 하나뿐이다.

#### 10.5.1 표면

| 공통 개념 | `.NET` |
|-----------|--------|
| meter 이름(상수) | `ZLinkMeters.Framework` = `"zlink.framework"` (meter/scope 이름은 언어 간 바이트 동일, 공통 §11) |
| 계기 방출 | `System.Diagnostics.Metrics.Meter("zlink.framework")` — `Counter`/`UpDownCounter`/`ObservableGauge`/`Histogram` |
| 앱 연결(공통 케이스) | OTel `MeterProviderBuilder.AddMeter(ZLinkMeters.Framework)` — 이게 전부다 |
| 비-OTel/테스트 수집 | .NET 표준 `MeterListener`가 `ZLinkMeters.Framework`를 직접 구독 — zlink 전용 listener interface 없음 |

```csharp
// 앱은 이 한 줄로 zlink 계기를 자기 OTel 파이프라인에 넣는다. 별도 zlink 설정 없음.
builder.Services.AddOpenTelemetry().WithMetrics(m => m
    .AddMeter(ZLinkMeters.Framework)
    .AddPrometheusExporter());
```

- 공통 §3의 `updown`=`UpDownCounter<long>`, `observable`=`ObservableGauge`(scrape 시 콜백),
  histogram=`Histogram<double>`(단위 `s`, 공통 §4.0).
- meter가 어떤 listener에도 연결되지 않으면 event별 allocation, lock, clock read와 sample 보관 없이
  최소 비용의 비활성 경로로 끝난다. 고정 instrument는 startup에 한 번 만들 수 있다(공통 §7.2).
- 대시보드·exporter는 앱 몫. framework는 내장 scrape 서버를 제공하지 않는다(공통 §6).

#### 10.5.2 왜 새 인터페이스가 없나

`.NET`의 `Meter`/`MeterListener`가 이미 벤더 중립 계기 파사드다. framework가 별도 `IZLinkMetrics*`
표면을 두면 그 위에 pass-through 층이 생겨 깊이가 없다(얕은 모듈). 그래서 공개 표면을 **안정된 meter
이름 하나**로 최소화한다. contract/E2E collector도 표준 `MeterListener`를 사용한다.

### 10.6 메시지 흐름 상관관계 (flow correlation)

공통 의미는 [공통 스펙 — 메시지 흐름 상관관계](../../53-flow-correlation.ko.md)가 소유한다. 이 절은
§9(메시지 흐름 추적)의 확장이며 **새 설정 표면을 만들지 않는다**. 기존 message-flow mode가 `Off`가
아니면 framework가 전역 유일 id를 자동 생성하고 기존 event에 필드를 더한다.

#### 10.6.1 표면

| 공통 개념 | `.NET` |
|-----------|--------|
| 생성 gate | 기존 `MessageFlow` mode가 `Off`가 아니면 create-if-absent 자동 생성 |
| event 필드(추가) | `string ZLinkMessageFlowEvent.FlowId`, `ZLinkFlowOrigin? ZLinkMessageFlowEvent.FlowOrigin` — dispatch 오류 이벤트에도 동일 |

```csharp
options.ConfigureDispatch()
    .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions); // flow id는 자동 생성·전파
```

- 생성은 트레이싱 모드 게이트, **전파(echo)는 무조건**이라 off 노드를 지나도 흐름이 끊기지 않는다
  (공통 §2.2).
- 두 필드는 하나의 optional pair다. `FlowId`가 빈 문자열이면 `FlowOrigin`은 `null`이고, `FlowId`가
  있으면 `FlowOrigin`도 반드시 있다. 로그와 observer event도 이 불변식을 유지한다.
- stream/actor gateway 로거는 부트스트랩에서 **자동 배선**된다(공통 §7). 명시 주입이 우선하되, 없을
  때 침묵 대신 기본 sink로 폴백 — "조용한 무로그"를 기본에서 제거한다. 게이팅은 불변(배선 ≠ 출력,
  `Off`면 완전 침묵).
- 로그 토큰 `flow=`는 언어 간 바이트 동일(공통 §8).

### 10.7 Graceful Drain & Handoff

공통 의미는 [공통 스펙 — Graceful Drain & Handoff](../../54-graceful-drain-handoff.ko.md)가 소유한다.
이 절은 **lifecycle 제어 표면**(관측이 아님)의 `.NET` 투영이다.

> **설계 원칙(복잡도 하향): 공통 케이스는 무설정.** framework가 host shutdown에 자동 참여해 drain하므로
> 앱은 아무 코드도 쓰지 않는다. draining 마커·owner lease 유지·`Takeover` 순서 같은 분산 정합은
> 공통 스펙 §3이 소유하며 앱 표면에 노출하지 않는다.

#### 10.7.1 표면

```csharp
public static class ZLinkMeters { public const string Framework = "zlink.framework"; }
public enum ZLinkFlowOrigin : byte
{
    Inbound = 1, Timer = 2, Application = 3, Lifecycle = 4
}
public enum ZLinkSpotDrainPolicy { DrainNatural = 0, ReleaseAndRecreate = 1 }
public enum ZLinkDrainForceReason
{
    DeadlineExceeded = 0, DrainingStatePublishFailed = 1,
    OwnerCleanupFailed = 2, TeardownFailed = 3
}
public enum ZLinkDrainState
{
    Serving = 0, Draining = 1, Drained = 2, ForceStopping = 3
}
public sealed record ZLinkDrainEvent(
    DateTimeOffset Timestamp,
    ZLinkDrainState State) : IZLinkRuntimeEvent
{
    public string SourceName => "drain";
}
public abstract record ZLinkDrainResult;
public sealed record Drained : ZLinkDrainResult;
public sealed record ForceStopped(ZLinkDrainForceReason Reason) : ZLinkDrainResult;
public interface IZLinkDrainControl
{
    bool IsReady { get; }
    ValueTask<ZLinkDrainResult> DrainAsync(CancellationToken cancellationToken = default);
    ValueTask<ZLinkDrainResult> DrainAsync(
        TimeSpan deadline, CancellationToken cancellationToken = default);
    ValueTask<ZLinkDrainResult> AwaitDrainedAsync(
        CancellationToken cancellationToken = default);
}
public static class ServiceCollectionExtensions
{
    public static IHealthChecksBuilder AddZLinkDrainHealthCheck(
        this IHealthChecksBuilder builder);
}
```

| 공통 개념 | `.NET` |
|-----------|--------|
| 자동 drain(기본) | framework hosted service가 `IHostApplicationLifetime` 종료에 참여, `StopAsync`에서 drain — 앱 코드 0 |
| SPOT drain 정책 | spot mesh 등록의 `UseDrainPolicy(ZLinkSpotDrainPolicy.{DrainNatural(기본)/ReleaseAndRecreate})` |
| terminal result | abstract `ZLinkDrainResult` + sealed `Drained`, `ForceStopped(ZLinkDrainForceReason Reason)`; reason은 `DeadlineExceeded`, `DrainingStatePublishFailed`, `OwnerCleanupFailed`, `TeardownFailed` |
| 명시 제어(선택) | `IZLinkDrainControl` { `ValueTask<ZLinkDrainResult> DrainAsync(TimeSpan deadline, CancellationToken)`, `DrainAsync(CancellationToken)`(30초), `AwaitDrainedAsync(CancellationToken)`, `bool IsReady { get; }` } (DI singleton) |
| readiness probe | `IZLinkDrainControl.IsReady` 또는 `IHealthChecksBuilder.AddZLinkDrainHealthCheck()` |
| 상태 관측 | 기존 `IZLinkRuntimeEventHandler<ZLinkDrainEvent>` 재사용. `ZLinkDrainEvent.State` { `Serving`/`Draining`/`Drained`/`ForceStopping` }, `SourceName` = 고정값 `"drain"` |

```csharp
// SPOT별 정책만 선언 — 나머지는 전부 기본 동작
options.AddSpotMesh("orders")
    .UseDrainPolicy(ZLinkSpotDrainPolicy.ReleaseAndRecreate);  // event-sourcing owner spot

// 배포 자동화가 세밀 제어를 원할 때만 (대개 불필요)
var drain = app.Services.GetRequiredService<IZLinkDrainControl>();
await drain.DrainAsync(TimeSpan.FromSeconds(25), ct);
await drain.AwaitDrainedAsync(ct);
```

- **왜 새 이벤트 구독을 안 만드나:** drain 상태 관측은 monitoring의 `IZLinkRuntimeEventHandler<T>`를
  그대로 쓴다(같은 개념 → 같은 메커니즘). 새 관측 표면을 만들지 않는다.
- **drain 이벤트는 source 등록이 필요 없다.** socket/spot source와 달리 drain은 노드 생애 수 회의
  저빈도 lifecycle 이벤트라 polling·filter 파라미터가 없다. `IZLinkRuntimeEventHandler<ZLinkDrainEvent>`
  핸들러가 DI에 있으면 monitoring 구성 유무와 무관하게 항상 수신한다 — flow correlation이 제거한
  "조용한 무관측" 함정을 여기서도 만들지 않는다(공통 §9).
- **왜 앱이 `Drain`을 안 불러도 되나:** host 종료 신호 처리를 framework가 흡수한다. `IZLinkDrainControl`
  은 배포 자동화가 세밀 제어할 때만 쓰는 탈출구다.
- `IsReady`는 술어 프로퍼티다(`Draining`이면 false). readiness probe가 이 값을 그대로 읽는다.

### 10.8 Session/actor dispatch 오류 표현 (`.NET` exception)

이 절은 framework 가 던지는 오류가 `.NET` 표면에서 어떤 모양으로 보이는지를
정리한다.

public `.NET` API 에서는 framework error 를 하나의 exception family 로
모은다.

```csharp
public sealed class ZLinkFrameworkException : Exception
{
    public ZLinkFrameworkException(
        ZLinkFrameworkErrorKind kind,
        string message,
        bool? isRetriable = null,
        Exception? innerException = null);

    public ZLinkFrameworkErrorKind Kind { get; }
    public bool IsRetriable { get; }
}

public enum ZLinkFrameworkErrorKind
{
    ActorRouteNotFound = 0,
    ActorCreateFailed = 1,
    ActorAlreadyExists = 2,
    ActorTypeMismatch = 3,
    SpotCreateFailed = 4,
    SpotRouteNotFound = 5,
    SpotTypeMismatch = 6,
    ActorSessionNotBound = 7,
    HandlerNotFound = 8,
    RouteHandlerNotFound = 9,
    ActorDispatchHandlerNotFound = 10,
    PayloadDecodeFailed = 11,
    RouteNotConnected = 12,
    RequestTargetNotFound = 13,
    RequestRejected = 14,
    RequestProtocolError = 15,
    RequestFailed = 16,
    WorkerQueueFull = 17,
    WorkerTimedOut = 18,
    WorkerFailed = 19,
    ActorLocationStale = 20,
    ActorCreateRejected = 21,
}
```

각 kind 의 발생 조건과 cross-binding 의미는
[policy/session-gateway-usability.ko.md](../../31-session-actor-dispatch.ko.md)
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

## 11. Attribute 정의

### 11.1 서버 간 messaging

```csharp
// 클래스 attribute. handler 클래스가 어느 논리 그룹에 속하는지 표시한다.
// 이 그룹을 어느 채널에 노출할지는 channel builder의 AddHandlerGroup(...)이 정한다.
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true)]
public sealed class ZLinkHandlerGroupAttribute : Attribute
{
    public ZLinkHandlerGroupAttribute(string groupName);
    public string GroupName { get; }
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkRequestAttribute : Attribute
{
    public ZLinkRequestAttribute();
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSendAttribute : Attribute
{
    public ZLinkSendAttribute();
    public string? PacketName { get; init; }
}
```

`ZLinkHandlerGroupAttribute` 는 handler 클래스가 어떤 논리 그룹에 속하는
지를 표시한다.

그룹 이름은 사용자가 정하는 임의의 문자열이다. 실제 channel 이름과는
분리되어 있다. 그래서 매핑 관계가 자유롭다.

- 같은 그룹을 여러 channel 에 매핑할 수 있다.
- 같은 channel 에 여러 그룹을 매핑할 수도 있다.

`ZLinkRequestAttribute` 와 `ZLinkSendAttribute` 는 channel 이름을 받지
않는다.

이 attribute 가 표현하는 것은 두 가지뿐이다. handler method 가 어떤
packet kind 를 처리하는지, 그리고 packet name override 정도다.

실제로 handler 를 어떤 inbound channel 에 노출할지는 channel
registration 의 `AddHandlerGroup(...)` mapping 이 결정한다.

```csharp
[ZLinkHandlerGroup("api")]
public sealed class ProfileHandlers
{
    [ZLinkRequest]
    public ValueTask<GetProfileReply> GetAsync(
        GetProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        // ...
    }
}

{
    var channel = options.AddClientServerChannel("api");
        channel.EnableServer("tcp://0.0.0.0:7101");
    channel.AddHandlerGroup("api");

}
```

동일한 handler 그룹을 여러 channel 에 매핑하는 것은 허용한다.

다만 같은 channel 안에서 동일 `kind + packet name` 이 둘 이상으로 해석되는
상황은 허용하지 않는다. 같은 그룹 안에서의 충돌이든, 서로 다른 그룹의
충돌이 한 channel 에 함께 묶이는 경우든, 모두 startup validation 오류로
처리한다.

attribute scan 용 보조 표면도 둘 수 있다. 예를 들어
`AddHandlersFromAssemblyOf<TMarker>()` 나
`AddHandlersFromAssembly(...)` 같은 것들이다. 빠른 prototype 단계이거나,
group attribute 없이 일괄 매핑해야 하는 경우에 쓸 수 있다.

다만 이 보조 표면은 framework 가 제공할 수는 있어도 정식 경로는 아니다.
정식 sample, scope, regression matrix 는 group mapping 모델을 기본으로
간주한다.

### 11.2 Spot actor handler

Spot actor handler attribute 는 Entry Spot 과 user Spot 에서 같은 이름을 쓴다.
어느 registry 에 등록되는지는 `EntrySpot.Configure()` 또는 `Spot.Configure()` 의
등록 위치가 결정한다.

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotActorSendAttribute : Attribute
{
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotActorRequestAttribute : Attribute
{
    public string? PacketName { get; init; }
}

```

method 시그니처는 아래 순서를 따른다.

- send: `(spotOrEntrySpot, actor, ZLinkSpotActorSendContext context, message, CancellationToken)` 반환값 없음
- request: `(spotOrEntrySpot, actor, ZLinkSpotActorRequestContext context, request, CancellationToken)` reply 반환
- actor join admission: Spot instance member
  `(string actorId, ZLinkMessage request, CancellationToken)`가
  `ValueTask<ZLinkSpotActorJoinResult>`를 반환
- joined/left/disconnected: Spot class 의 public instance callback
  `(actor, CancellationToken)` 반환값 없음

`PacketName` 을 지정하지 않으면 message/request 타입의 packet 이름을 사용한다.
같은 handler 클래스에 여러 Spot actor handler attribute method 가 있으면
`AddHandler<THandler>()` 에서는 모호하므로 startup validation 오류가 된다.

### 11.3 publish

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkPublishAttribute : Attribute
{
    public ZLinkPublishAttribute();
    public string? PacketName { get; init; }
}
```

이 문서에서는 pub/sub attribute 이름을 `ZLinkPublishAttribute` 로 고정
한다.

이름을 `Event` 가 아니라 `Publish` 로 둔 까닭은 producer 쪽 동사
(`IZLinkFanoutClient.Publish(...)`) 와 맞추기 위해서다. 그래야
`[ZLinkRequest]` / `[ZLinkSend]` / `[ZLinkPublish]` 세 표면이 동일한
패턴으로 읽힌다.

publish handler 도 모든 subscriber channel 에 전역으로 자동 노출되지
않는다. subscriber 역할을 가진 channel 이라 해도, 노출할 그룹은
`AddHandlerGroup(...)` 으로 명시해야 한다.

```csharp
[ZLinkHandlerGroup("api.events")]
public sealed class CacheInvalidatedHandler
    : IZLinkPublishHandler<CacheInvalidatedEvent>
{
    // ...
}

{
    var channel = options.AddFanoutChannel("api.events");
    channel.EnableSubscriber();
    channel.AddHandlerGroup("api.events");

}
```

### 11.4 SPOT

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotRequestAttribute : Attribute
{
    public ZLinkSpotRequestAttribute();
    public string? PacketName { get; init; }
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotSubscriptionAttribute : Attribute
{
    public ZLinkSpotSubscriptionAttribute(
        string spotNodeName, string topic);
    public string SpotNodeName { get; }
    public string Topic { get; }
}
```

두 attribute는 별도 DI handler가 아니라 실제로 만들어진 concrete `IZLinkSpot`
instance의 public instance method에만 붙인다. request method는 decoded request를 첫 번째
인자로 받고, 선택적으로 마지막에 `CancellationToken`을 받으며 `Task<TReply>` 또는
`ValueTask<TReply>`를 반환한다. subscription method도 decoded event를 첫 번째 인자로 받고
선택적으로 마지막에 `CancellationToken`을 받지만 반환형은 `Task` 또는 `ValueTask`다.
subscription은 생성자에 적은 Spot node에서만 등록한다. packet 이름을 생략한 request와
subscription event의 packet 이름은 payload 타입에서 결정한다. static, open generic,
`ref`/`in`/`out` 인자, 같은 packet 또는 같은 topic과 packet 조합의 중복 등록은 시작 검증 오류다.

### 11.5 stream

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkStreamPacketAttribute : Attribute
{
    public ZLinkStreamPacketAttribute();
}

```

이 attribute는 실제로 만들어진 concrete `IZLinkSession` instance의 public instance
method에 붙인다. 첫 번째 인자는 decoded payload다. 그 뒤에는
`ZLinkSessionDispatchContext`, `CancellationToken`을 이 순서로 선택해서 받을 수 있다.
반환형은 `Task` 또는 `ValueTask`다. packet 이름은 payload 타입에서 결정한다. static,
open generic, `ref`/`in`/`out` 인자와 같은 packet의 중복 등록은 시작 검증 오류다.

stream 은 dispatch context 기반의 packet session 을 하나의 축으로 본다.
recv 방식은 이 공개 계약에 포함하지 않는다.

session lifecycle 은 세 가지 callback 으로 노출한다. `OnConnectedAsync`,
`OnDisconnectedAsync`, `OnErrorAsync` 다.

## 12. 시그니처 규칙

이 절은 attribute 기반 handler 의 메서드 시그니처 규칙을 정리한다.

attribute 기반 handler 의 메서드 시그니처는 다음 규칙을 따른다.

- 첫 번째 인자: decoded payload 타입
- 두 번째 인자: context 타입 (생략 가능)
- 마지막 인자: `CancellationToken` (생략 가능)
- request handler 반환: `ValueTask<T>` 또는 `Task<T>`
- send handler 반환: `ValueTask` 권장

handler scanner 와 runtime invoker 는 다음 네 가지를 명시적으로 구분해서
처리해야 한다. `Task`, `Task<T>`, `ValueTask`, `ValueTask<T>` 다.

`dynamic` 호출 결과에 의존해서 generic `ValueTask<T>` 를 기다리면 위험
하다. 값 타입이 boxing 된 뒤 `AsTask()` 를 찾지 못하는 오류가 뒤늦게 드러날
수 있기 때문이다.

따라서 framework 는 등록 단계에서 허용 반환형을 먼저 판정한다. 허용되지
않는 반환형은 startup validation 오류로 실패 처리한다.

node 경계를 넘는 payload 는 두 곳에서 나온다. request/reply payload, 그리고
session actor dispatch 다.

이 payload 는 codec 이 직렬화/역직렬화할 수 있는 DTO 여야 한다. root 타입
이나 컬렉션 원소 타입이 abstract class 혹은 interface 라면, 기본 codec
만으로는 구체 타입을 만들 수 없다.

이 경우 처리 원칙은 다음과 같다. 명시적인 codec/converter 계약이 없는 한,
등록 단계나 첫 submit 시점 이전에 명확한 configuration 오류로 실패시켜야
한다.

domain 내부의 이벤트 계층을 그대로 reply DTO 에 사용하지 않는다. 대신
wire 에 올릴 구체 record DTO 로 한 번 변환해서 사용한다.

framework 가 강제하는 것은 class 구조 자체가 아니다. 핵심 규칙은 단 하나다.

"resolved packet key 하나는 동일한 실행 문맥 안에서, 단 하나의 handler
에만 매핑된다"

여기서 실행 문맥의 구분은 다음과 같다.

- 일반 channel messaging 의 실행 문맥은 inbound channel 역할이다.
- actor 와 spot 은 각각 고유한 실행 문맥을 가진다.

class 구성 방식은 자유롭다. 주제별 handler 묶음 (`UserHandlers`) 도,
packet 별 단일 class (`UserGetHandler`) 도 모두 허용된다.

## 13. DI 동작 기준

이 절은 handler 와 client 가 `.NET DI` 위에서 어떻게 결합되는지를 정리
한다.

- handler class 는 `.NET DI` 에서 resolve 한다.
- handler 의 constructor injection 이 동작해야 한다.
- outbound client 도 동일한 DI 컨테이너에서 주입된다.
- `IZLinkHandlerFilter` 구현체 역시 같은 DI 컨테이너에서 resolve 한다.
- framework 는 별도의 객체 생성기를 두지 않는다. 대신 `ASP.NET Core` 가
  사용하는 `IServiceProvider` 를 기반으로 handler invocation 을 구성한다.
- 다만 public registration 함수에 `IServiceProvider services` 를 매번
  노출할 필요는 없다.
- `Spot`, packet handler, timer handler 는 framework 가 만든 per-spot
  scope 에서 resolve 한다. registration 함수는 handler 타입만 받는다.
- 즉 `Context.Handlers.AddPacket<THandler>()`, `Context.AddTimer<THandler>(...)`
  같은 표면은 service locator 가 아니다. "이 타입을 spot scope 에서 사용해
  달라" 는 등록 선언이다.
- `OnCreateAsync(...)` 와 `OnInitializeAsync(...)` 도 마찬가지다.
  `IServiceProvider` 를 직접 받지 않고, spot 자체의 constructor injection 과
  cached dependency 를 사용한다. 이렇게 해야 hot path 와의 경계가 분명하다.

### 13.1 public service DI 등록 조건

모든 public service interface 를 항상 DI 에 등록하지는 않는다. 생성자 주입은
그 기능을 사용할 수 있다는 신호가 되므로, 역할이 없는 service 는 등록하지
않는다. 이 절이 `.NET` public service의 DI 등록 계약을 소유한다.

| Interface | DI 등록 조건 |
|-----------|--------------|
| `IZLinkChannelClient` | 항상 등록한다. channel 누락은 호출 시 `ZLinkConfigurationException` 으로 처리한다 |
| `IZLinkRouteClient` | 항상 등록한다. route channel 누락은 호출 시 `ZLinkConfigurationException` 으로 처리한다 |
| `IZLinkFanoutClient` | 항상 등록한다. publisher 역할 누락은 호출 시 `ZLinkConfigurationException` 으로 처리한다 |
| `IZLinkSpotManager` | `SpotNode` 가 하나 이상 있을 때 등록한다 |
| `IZLinkSpotPublisherClient` | Spot publisher client 역할이 하나 이상 있을 때 등록한다 |
| `IZLinkActorManager` | `SpotNode` 와 actor factory 가 모두 있을 때 등록한다 |
| `IZLinkSpotHandleResolver`, `IZLinkActorSpotHandleResolver` | 해당 resolver registration 이 있을 때 등록한다 |

actor factory가 하나라도 있으면 같은 process에 `SpotNode`가 있어야 한다. 이 조건은
host 시작 전에 검증한다. `SpotNode` 없이 actor factory만 등록하면
`ZLinkConfigurationException`이 발생한다. `IZLinkBoundSessionFactory`는 framework
내부 runtime service이며 public DI 표면이 아니다. actor가 client session으로 보낼 때는
`IZLinkActorContext.BoundSession`을 사용하고, 현재 binding이 없으면
`ActorSessionNotBound`로 실패한다.

local handler가 등록되는 channel의 의미는 다음과 같다. route prefix가
아니라, 애플리케이션이 해당 channel 에서 server 역할을 수행한다는 의미다.

channel 이름의 위치도 정해 둔다. handler class 나 method attribute 가
아니라, channel registration 에 둔다.

예: `options.AddClientServerChannel("api").EnableServer(endpoint)`

다만 outbound-only 앱이라면, server 역할을 가진 channel 이 아예
없을 수도 있어야 한다.

## 14. 결정된 기준

이 절은 위에서 흩어져 다뤘던 결정들 중, 자주 참고할 만한 핵심을 묶어 둔다.

- `ZLinkRequestContext` 와 `ZLinkSendContext` 는 합치지 않는다.
  request-response 와 one-way send 는 timeout, reply, 호출 의미가 다르기
  때문이다. 그래서 별도의 context 를 유지한다.
- `OnErrorAsync(...)` 는 session 에 매핑할 수 있는 transport 오류만
  수신한다. 다음 종류의 오류들은 다른 경로에 둔다.
  - application handler 내부의 예외
  - bind/accept/close 같은 node 단위 오류
  - handshake 이전 단계의 monitor 이벤트
  - 이들은 runtime monitoring 표면에만 남긴다.
- `Zlink.Framework` runtime 은 `IZLinkChannelClient` 위에 channel 별 typed
  wrapper 를 공식 기본 표면으로 제공하지 않는다. typed wrapper 가 필요하
  다면, 응용 측이나 별도 확장 패키지가 `IZLinkChannelClient` 위에 얹는 방식을
  기본으로 본다.
- `spotRid`는 `.NET`의 `RoutingId` 값 타입을 사용하지만 의미는 logical Spot
  식별자다. owner node 같은 transport 대상은 이 값에서 꺼내지 않고 `SpotHandle`이
  내부에서 관리한다.

### 14.1 message dispatch error observer

미등록 메시지와 dispatch 실패 관측은 message flow observer 로 처리한다.
channel 별, spot 별 observer 등록은 이 버전의 공개 계약이 아니다. request 실패는 reply path 가 있으면
error reply 로 끝나고, one-way 실패는 drop 되지만 기본 로그, metric, observer event 를 남긴다.

```csharp
public interface IZLinkMessageFlowObserver
{
    ValueTask OnMessageFlowAsync(
        ZLinkMessageFlowEvent flow,
        CancellationToken cancellationToken);
}
```

dispatch 실패는 `ZLinkMessageFlowEvent` 의 `Outcome` 이 `Error` 인 event 로 전달된다.
event 는 어느 channel 과 socket role 에서 실패가 났는지, 어느 routing id 와 관련되었는지,
실패 이유와 runtime 이 선택한 처리 방식을 함께 담는다. native message 소유권이나 frame 참조는
포함하지 않는다.
`FlowId`와 nullable `FlowOrigin`은 하나의 optional pair다. `FlowId`가 빈 문자열인 event는
`FlowOrigin == null`이고, flow가 있는 event는 두 값을 함께 제공한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.ConfigureDispatch()
        .SetMessageFlowObserver<MyDispatchErrorObserver>();
});
```

## 15. 회귀 테스트

이 절은 이 문서의 interface 정의를 보호하는 회귀 테스트들을 가리킨다.

이 문서의 interface 항목들은 두 가지를 확인해야 한다.

- public API 표면이 backend 구현 세부사항을 새어 내지 않는지.
- 등록 · handler · client 표면이 런타임 테스트와 같은 이름을 유지하는지.

interface 설명을 변경하면, 아래 테스트도 함께 조정한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ScaffoldSmokeTests.PublicSurface_DoesNotExpose_BackendConcreteTypes` | framework public API가 허용된 값 타입 외의 backend concrete type을 직접 노출하지 않는다. |
| `ScaffoldSmokeTests.PublicSurface_Removes_DirectRouteContracts_And_Exposes_ActorContracts` | direct route 계약은 빠지고 actor/session 계약은 public API 표면에 남아 있다. |
| `ScaffoldSmokeTests.FrameworkRoot_IsDiscoverable_FromTestRuntime` | framework root type 이 test runtime 에서 발견되고 public 등록 표면이 조립된다. |
| `HandlerResultAwaiterTests.AwaitAsync_Returns_ValueTaskOfT_Result` | `ValueTask<T>` handler 결과를 값 타입 boxing 여부와 무관하게 기다리고 실제 reply 값을 반환한다. |
| `EntrySpotActorDispatchTests.EntrySpotActorDispatch_NoBindRequest_RepliesViaNoBind_AndDoesNotBindSession` | Entry Spot actor request는 request handler 결과로만 응답하고 send handler나 session bind 경로로 바뀌지 않는다. |
| `E2E:SM-D1` | local actor relay가 request handler의 reply를 stream session으로 전달한다. |
| `ScaffoldSmokeTests.PublicSurface_Removes_ActorReply_And_StreamClientContracts` | actor context Reply 와 actor stream client 계약이 public API 표면에 다시 노출되지 않는다. |

## 16. 전체 public interface inventory와 구현 상태

이 inventory는 server framework의 `Zlink.Framework.Contracts`와 client Stream Connector의
public contract를 범위로 한다. HTTP client는 별도 component이므로
`framework/doc/http-client/dotnet/` 스펙에서 고정한다.

이 절은 이 문서에서 빠진 interface가 없는지 확인하기 위한 정식 inventory다. 아래
이름과 각 본문의 시그니처가 `.NET` public contract다. 이름이 이 목록에 없으면 다른
언어에 같은 이름을 만들기 위한 근거로 사용하지 않는다.

| 기능 | public interface |
|------|------------------|
| handler와 context | `IZLinkHandlerContext`, `IZLinkRequestHandler<TRequest,TResponse>`, `IZLinkSendHandler<TMessage>`, `IZLinkPublishHandler<TMessage>`, `IZLinkRouteSendHandler<TMessage>`, `IZLinkRouteRequestHandler<TRequest,TReply>`, `IZLinkHandlerFilter` |
| channel call | `IZLinkChannelClient`, `IZLinkRouteClient`, `IZLinkFanoutClient`, `IZLinkSendCall`, `IZLinkRequestCall`, `IZLinkPublishCall` |
| actor | `IZLinkActor`, `IZLinkActorContext`, `IZLinkActorFactory`, `IZLinkActorManager`, `IZLinkActorDirectory`, `IZLinkActorClient`, `IZLinkActorSendCall`, `IZLinkActorRequestCall`, `IZLinkActorJoinCall`, `IZLinkActorJoinSpotCall`, `IZLinkActorJoinEntrySpotCall` |
| Spot | `IZLinkSpot`, `IZLinkSpot<TActor>`, `IZLinkEntrySpot`, `IZLinkEntrySpot<TActor>`, `IZLinkSpotActorLifecycle<TActor>`, `IZLinkActorTransferAdapter<TActor>`, `IZLinkActorHandlerRegistry`, `IZLinkSpotHandlerRegistry`, `IZLinkSpotOutbound`, `IZLinkSpotCommonContext`, `IZLinkSpotContext`, `IZLinkEntrySpotContext` |
| Spot handler와 관리 | `IZLinkSpotActorSendHandler`, `IZLinkSpotActorRequestHandler`, `IZLinkEntrySpotActorSendHandler`, `IZLinkEntrySpotActorRequestHandler`, `IZLinkSpotPacketHandler`, `IZLinkSpotRequestHandler`, `IZLinkSpotSubscriptionHandler`, `IZLinkSpotTimerHandler`, `IZLinkSpotManager`, `IZLinkSpotPublisherClient` |
| stream과 session | `IZLinkStream`, `IZLinkSession`, `IZLinkSessionContext`, `IZLinkSessionClient`, `IZLinkSessionActors`, `IZLinkSessionActor`, `IZLinkSessionHandlerRegistry`, `IZLinkSessionPacketHandler<TSessionContext,TMessage>`, `IZLinkSessionSendCall`, `IZLinkSessionReplyCall`, `IZLinkBoundSession`, `IZLinkBoundSessionSendCall`, `IZLinkMessageMetadataPolicy` |
| client Stream Connector | `IZlinkStreamConnector`, `IZlinkStreamLifecycleCall`, `IZlinkStreamSendCall`, `IZlinkStreamRequestCall`, `IZlinkStreamWaitCall`, `IZlinkStreamCompressionCodec`, `IZlinkStreamPayloadCodec`, `IZlinkStreamPacketNameResolver` |
| location | `IZLinkLocationStore`, `IZLinkPeerLocationStore`, `IZLinkSpotLocationStore`, `IZLinkActorLocationStore`, `IZLinkRouteLocationStore`, `IZLinkOwnerLeaseStore`, `IZLinkLocationWatchStore`, `IZLinkLocationChangeStampStore`, `IZLinkLocationReadiness`, `IZLinkLocationRuntimeQuery`, `IZLinkPeerLocationResolver`, `IZLinkSpotHandleResolver`, `IZLinkActorSpotHandleResolver` |
| codec | `IZLinkMessageSerializer`, `IZLinkCodecExtension`, `IZLinkCodecRegistryBuilder`, `IZLinkCodecRegistrar`, `IZLinkStreamCompressionBuilder` |
| configuration | `IZLinkFrameworkOptions`, `IZLinkClientServerChannelBuilder`, `IZLinkRouteMeshChannelBuilder`, `IZLinkFanoutChannelBuilder`, `IZLinkStreamNodeBuilder`, `IZLinkSpotNodeBuilder`, `IZLinkSpotMeshBuilder`, `IZLinkClientServerChannelOptions`, `IZLinkRouteMeshChannelOptions`, `IZLinkChannelRuntimeOptions`, `IZLinkEndpointConnections`, `IZLinkSocketConfig`, `IZLinkRouteConfig`, `IZLinkOutboundRouteConfig`, `IZLinkSpotPublisherConfig`, `IZLinkSpotSubscriberConfig`, `IZLinkEntrySpotOptions`, `IZLinkMetadataPolicyBuilder` |
| dispatch와 monitoring | `IZLinkDispatchOptions`, `IZLinkUnhandledDispatchOptions`, `IZLinkDiagnosticsOptions`, `IZLinkMessageFlowObserver`, `IZLinkMessageFlowControl`, `IZLinkMonitoringOptions`, `IZLinkRuntimeEvent`, `IZLinkRuntimeEventHandler<TEvent>`, `IZLinkRuntimeEventPublisher`, `IZLinkDrainControl` |
| timer와 worker | `IZLinkTimer`, `IZLinkWorkerCall<TResult>`, `IZLinkWorkerOptions` |

기존 본문에서 이름만 언급하고 전체 모양을 찾기 어려웠던 interface는 다음 시그니처를
추가 기준으로 삼는다.

```csharp
public interface IZLinkActorClient
{
    IZLinkActorSendCall SendToActor<TMessage>(ActorRef actor, TMessage message);
    IZLinkActorRequestCall RequestToActor<TRequest>(ActorRef actor, TRequest request);
}

public interface IZLinkActorSendCall
{
    void Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkActorRequestCall
{
    IZLinkActorRequestCall Timeout(TimeSpan timeout);
    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkActorDirectory
{
    ValueTask<ActorRef?> FindAsync(string actorId, CancellationToken cancellationToken = default);
    ValueTask<ActorRef> EnsureAsync(
        string actorId,
        ZLinkMessage createRequest,
        ZLinkActorPlacement placement = default,
        CancellationToken cancellationToken = default);
}

public interface IZLinkChannelRuntimeOptions
{
    IZLinkClientServerChannelOptions ClientServerChannel(string channelName);
    IZLinkRouteMeshChannelOptions RouteMeshChannel(string channelName);
}

public interface IZLinkCodecExtension
{
    void Register(IZLinkCodecRegistrar codecs);
}

public interface IZLinkCodecRegistrar
{
    void AddSerializer(string contentType, IZLinkMessageSerializer serializer);
    void AddSerializer(
        string contentType,
        IZLinkMessageSerializer serializer,
        Func<Type, bool> canSerialize);
    void AddStreamCodec(string contentType, ZlinkStreamCodec codec);
}
```

```csharp
public interface IZLinkWorkerCall<TResult>
{
    IZLinkWorkerCall<TResult> Timeout(TimeSpan timeout);
    ValueTask<TResult> Async(CancellationToken cancellationToken = default);
}
```

client Stream Connector는 server framework와 별도 package지만 공통 async와 stream
codec 정책의 public contract에 포함되므로 다음 interface도 이 catalog에서 고정한다.

```csharp
public interface IZlinkStreamConnector : IAsyncDisposable
{
    bool IsConnected { get; }
    ZlinkStreamConnectionState State { get; }
    ZlinkStreamConnectorOptions Options { get; }
    int PendingDispatchCount { get; }
    IZlinkStreamLifecycleCall Connect { get; }
    IZlinkStreamLifecycleCall Close { get; }
    IZlinkStreamLifecycleCall Dispatch { get; }
    event Func<ZlinkStreamError, CancellationToken, ValueTask>? ErrorReceived;
    event Func<ZlinkStreamDisconnected, CancellationToken, ValueTask>? Disconnected;
    event Func<ZlinkStreamConnectionStateChanged, CancellationToken, ValueTask>?
        ConnectionStateChanged;
    int ReceivedCount(string name);
    IZlinkStreamSendCall Send(ZlinkStreamEncodedPayload payload);
    IZlinkStreamRequestCall Request(ZlinkStreamEncodedPayload payload);
    IDisposable ObserveInbound(
        Func<ZlinkStreamInboundObservation, CancellationToken, ValueTask> observer);
    IDisposable On(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, CancellationToken, ValueTask>
            handler);
    IZlinkStreamWaitCall WaitFor(string name);
}

public enum ZlinkStreamCloseReason
{
    ClientClose,
    IdleTimeout,
    HeartbeatTimeout,
    ServerDrain,
    ProtocolError,
    TransportError,
}

public sealed record ZlinkStreamDisconnected(ZlinkStreamCloseReason CloseReason);

public interface IZlinkStreamLifecycleCall
{
    ValueTask Async(CancellationToken cancellationToken = default);
}

public interface IZlinkStreamSendCall
{
    IZlinkStreamSendCall PacketName(string name);
    IZlinkStreamSendCall Metadata(string key, string value);
    IZlinkStreamSendCall Metadata(ZlinkStreamMetadata metadata);
    IZlinkStreamSendCall Compress();
    void Submit(CancellationToken cancellationToken = default);
}

public interface IZlinkStreamRequestCall
{
    IZlinkStreamRequestCall PacketName(string name);
    IZlinkStreamRequestCall Metadata(string key, string value);
    IZlinkStreamRequestCall Metadata(ZlinkStreamMetadata metadata);
    IZlinkStreamRequestCall Timeout(TimeSpan timeout);
    IZlinkStreamRequestCall Compress();
    ValueTask<ZlinkStreamEncodedPayload> Async(CancellationToken cancellationToken = default);
    void Submit(Action<ZlinkStreamResult> callback);
    void Submit(Action<ZlinkStreamResult<ZlinkStreamEncodedPayload>> callback);
}

public interface IZlinkStreamWaitCall
{
    IZlinkStreamWaitCall Timeout(TimeSpan timeout);
    IZlinkStreamWaitCall Where(
        Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, bool> predicate);
    ValueTask<ZlinkStreamMessage<ZlinkStreamEncodedPayload>> Async(
        CancellationToken cancellationToken = default);
}

public interface IZlinkStreamCompressionCodec
{
    ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> payload);
    ReadOnlyMemory<byte> Decompress(
        ReadOnlyMemory<byte> payload,
        int maxDecompressedPayloadSize);
}

public interface IZlinkStreamPayloadCodec
{
    ZlinkStreamEncodedPayload Encode<TPayload>(TPayload payload);
    TPayload Decode<TPayload>(ZlinkStreamEncodedPayload payload);
}

public interface IZlinkStreamPacketNameResolver
{
    string Resolve(Type payloadType);
}
```

`IZlinkStreamConnector`는 connection state와 options를 노출하고 `Connect`, `Close`,
`Dispatch`, `Send`, `Request`, `WaitFor`, `On`, `ObserveInbound`, `ReceivedCount`를 소유한다.
callback과 event의 정확한 `CancellationToken` 시그니처도 `.NET` 언어별 계약에 포함한다.

다음 interface도 이름만 inventory에 두지 않고 member 전체를 정식 계약으로 고정한다.

```csharp
public interface IZLinkClientServerChannelOptions
{
    IZLinkSocketConfig ConfigureServerSocket();
    IZLinkRouteConfig ConfigureServerRouting();
    IZLinkSocketConfig ConfigureClientSocket();
    IZLinkOutboundRouteConfig ConfigureClientRouting();
}

public interface IZLinkRouteMeshChannelOptions
{
    IZLinkSocketConfig ConfigureSocket();
}

public interface IZLinkStreamCompressionBuilder
{
    IZLinkStreamCompressionBuilder UseDefault();
    IZLinkStreamCompressionBuilder UseLz4();
    IZLinkStreamCompressionBuilder Use(IZlinkStreamCompressionCodec codec);
    IZLinkStreamCompressionBuilder Disable();
}

public interface IZLinkCodecRegistryBuilder
{
    void Use(IZLinkCodecExtension extension);
}
```

```csharp
public interface IZLinkSocketConfig
{
    long MaxMessageSize { get; set; }
    int SendHighWaterMark { get; set; }
    int ReceiveHighWaterMark { get; set; }
    int SendBufferSize { get; set; }
    int ReceiveBufferSize { get; set; }
    TimeSpan? Linger { get; set; }
    TimeSpan? ReceiveTimeout { get; set; }
    TimeSpan? SendTimeout { get; set; }
    TimeSpan? ConnectTimeout { get; set; }
    TimeSpan? HandshakeInterval { get; set; }
    bool IPv6 { get; set; }
    bool TcpNoDelay { get; set; }
    bool Immediate { get; set; }
    int Weight { get; set; }
}

public interface IZLinkRouteConfig
{
    bool RequireKnownPeer { get; set; }
    bool AllowPeerHandover { get; set; }
    bool EnablePeerProbe { get; set; }
    RoutingId ConnectRoutingId { get; set; }
}

public interface IZLinkOutboundRouteConfig
{
    bool ProbeRouterOnConnect { get; set; }
}

public interface IZLinkSpotPublisherConfig
{
    int SendHighWaterMark { get; set; }
    TimeSpan? SendTimeout { get; set; }
    TimeSpan? Linger { get; set; }
    bool NoDrop { get; set; }
}

public interface IZLinkSpotSubscriberConfig
{
    int ReceiveHighWaterMark { get; set; }
    TimeSpan? ReceiveTimeout { get; set; }
    TimeSpan? Linger { get; set; }
}
```

```csharp
public interface IZLinkUnhandledDispatchOptions
{
    ZLinkUnhandledDispatchAction Request { get; set; }
    ZLinkUnhandledDispatchAction Send { get; set; }
    ZLinkUnhandledDispatchAction Publish { get; set; }
    LogLevel SendLogLevel { get; set; }
    LogLevel PublishLogLevel { get; set; }
}

public interface IZLinkDiagnosticsOptions
{
    ZLinkMessageFlowLogMode MessageFlow { get; }
    double SampleRate { get; }
    bool IncludeMessageSizes { get; }
    bool IncludeNativeDiagnostics { get; }
    string? LogFile { get; }
    string? Label { get; }
    ZLinkMessageFlowLogMode EffectiveMessageFlow { get; }
}

public interface IZLinkMessageFlowControl
{
    ZLinkMessageFlowLogMode MessageFlowMode { get; }
    void SetMessageFlowMode(ZLinkMessageFlowLogMode mode);
}
```

```csharp
public interface IZLinkPeerLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);
    ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationWriteResult> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);
}

public interface IZLinkRouteLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationWriteResult> RemoveRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
        ZLinkRouteLocationKey key,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);
}
```

```csharp
public interface IZLinkOwnerLeaseStore
{
    ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default);
    ValueTask<bool> RemoveOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkLocationStore :
    IZLinkPeerLocationStore,
    IZLinkSpotLocationStore,
    IZLinkActorLocationStore,
    IZLinkRouteLocationStore,
    IZLinkOwnerLeaseStore
{
    ValueTask<long> RemoveAllByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default);
}

public interface IZLinkLocationWatchStore
{
    IAsyncEnumerable<ZLinkLocationChanged> WatchAsync(
        ZLinkLocationWatchFilter filter,
        CancellationToken cancellationToken = default);
}

public interface IZLinkLocationChangeStampStore
{
    ValueTask<long> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope,
        CancellationToken cancellationToken = default);
}

public interface IZLinkLocationReadiness
{
    ValueTask<bool> IsPeerReadyAsync(
        string meshName,
        ZLinkLocationRole role,
        RoutingId? nodeRid = null,
        CancellationToken cancellationToken = default);
}

public interface IZLinkPeerLocationResolver
{
    ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListLivePeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default);
}
```

```csharp
public interface IZLinkWorkerOptions
{
    int MinThreads { get; set; }
    int MaxThreads { get; set; }
    TimeSpan IdleTimeout { get; set; }
    int MaxQueueLength { get; set; }
}
```

### 16.1 공통 스펙 기능 커버리지

공통 스펙의 모든 기능이 `.NET` interface에 나타나는지 다음 표로 확인한다. 공통
스펙이 내부 동작만 정의하는 경우에는 public interface를 새로 만들지 않고 기존
interface가 그 동작을 보장하도록 한다.

| 공통 기능 | `.NET` public contract | 문서 상태 | 현재 구현 |
|-----------|------------------------|-----------|-----------|
| typed object message와 기본 JSON codec | `IZLinkMessageSerializer`, `IZLinkCodecRegistryBuilder`, `IZLinkCodecExtension`, `IZLinkCodecRegistrar` | 명시됨 | 구현됨 |
| channel request/send/publish | `IZLinkChannelClient`, `IZLinkSendCall`, `IZLinkRequestCall`, `IZLinkPublishCall`, handler interface | 명시됨 | 구현됨 |
| route mesh request/send | `IZLinkRouteClient`, route handler, `IZLinkRouteMeshChannelBuilder` | 명시됨 | 구현됨 |
| channel topology와 수동 endpoint | channel builder의 `EnableClient(endpoint)`, `EnableServer(endpoint)`와 역할별 routing config | 명시됨 | 구현됨 |
| one-way 완료 객체 비노출 | send, publish, session send/reply의 `Submit(...) : void` | 명시됨 | 구현됨 |
| Spot 생성, 조회, 종료 | `IZLinkSpotManager`, `IZLinkSpot`, `IZLinkSpotContext` | 명시됨 | 구현됨 |
| SpotHandle 기반 메시징 | `IZLinkSpotHandleResolver`, `IZLinkActorSpotHandleResolver`, `IZLinkSpotOutbound`, `SpotHandle` | 명시됨 | 구현됨 |
| actor 생성과 위치 조회 | `IZLinkActorManager`, `IZLinkActorDirectory`, resolver와 location store | 이 절에서 inventory 보완 | 구현됨 |
| actor 직접 send/request | `IZLinkActorClient`, actor send/request call | 이 절에서 시그니처 보완 | 구현됨 |
| actor join | `IZLinkActorJoinCall`, join Spot/Entry Spot call | 이 절에서 시그니처 보완 | 구현됨 |
| Spot actor lifecycle | `IZLinkSpotActorLifecycle<TActor>` | 이 절에서 시그니처 보완 | 구현됨 |
| remote actor state transfer | `IZLinkActorTransferAdapter<TActor>` | 명시됨 | 구현됨 |
| 이동 중 packet handoff와 generation fencing | 기존 actor join/transfer interface의 관찰 가능한 동작 | 동작은 Spot Actor 공통 스펙 참조 | public helper 없이 runtime이 구현 |
| session lifecycle과 typed packet | `IZLinkSession`, `IZLinkSessionPacketHandler<TContext,TMessage>`, `IZLinkSessionHandlerRegistry` | 명시됨 | 구현됨 |
| client Stream Connector lifecycle와 wait/request | `IZlinkStreamConnector`와 call interface 4종 | 이 절에서 보완 | 구현됨 |
| client Stream Connector codec와 packet name | compression/payload codec와 packet-name resolver | 이 절에서 보완 | 구현됨 |
| session actor bind와 relay | `IZLinkSessionActors`, `IZLinkSessionActor`, `IZLinkBoundSession` | 명시됨 | 구현됨 |
| location store 역할과 owner lease | location store interface 8종 | inventory 보완 | 구현됨 |
| 공식 Redis location extension | `IZLinkLocationStore` 구현과 `IZLinkFrameworkOptions.AddLocationStore(...)` | core interface 명시됨 | extension package에서 구현 |
| location readiness와 운영 조회 | `IZLinkLocationReadiness`, `IZLinkLocationRuntimeQuery` | inventory 보완 | 구현됨 |
| dispatch와 unhandled 정책 | `IZLinkDispatchOptions`, `IZLinkUnhandledDispatchOptions`; 최적화 mode는 내부 정책 | inventory 보완 | 구현됨 |
| message-flow tracing과 runtime toggle | `IZLinkDiagnosticsOptions`, `IZLinkMessageFlowObserver`, `IZLinkMessageFlowControl` | 명시됨 | 구현됨 |
| runtime monitoring event | monitoring options, event, handler, publisher interface | 명시됨 | 구현됨 |
| runtime channel option | `IZLinkChannelRuntimeOptions`, client-server/route-mesh option interface | inventory 보완 | 구현됨 |
| timer | `IZLinkTimer`, Spot context의 `AddTimer<THandler>` | 명시됨 | 구현됨 |
| worker와 turn 복귀 | `IZLinkWorkerCall<TResult>`, `IZLinkWorkerOptions` | 이 절에서 시그니처 보완 | 구현됨 |
| stream compression | `IZLinkStreamCompressionBuilder` | inventory 보완 | 구현됨 |
| `.NET` cancellation | 취소 가능한 call과 callback의 `CancellationToken` | 명시됨 | 구현됨 |

이 표는 공통 기능과 `.NET` 공개 계약의 대응 관계를 검토하는 목록이다. 구현 상태는
아래 차이 표에서 별도로 판정하며, 이 목록 자체를 구현 완료의 근거로 사용하지 않는다.

### 16.2 현재 구현과의 차이

| 확인 항목 | 목표 계약 | 현재 구현 | 후속 작업 |
|-----------|-----------|-----------|-----------|
| 공개 interface inventory | 이 문서의 시그니처 | `Contracts/*`와 Stream Connector contracts에 선언됨 | 일치. exported type coverage와 실제 package consumer로 검증한다. |
| request/join/worker/HTTP client 완료 | terminator 세 축 — `submit`(one-way) / `Async(...)`(turn 유지) / `Yield(...)`(turn 반납) | `Async(...)` 하나뿐이고 그것이 **자동으로 turn을 반납**한다. `Yield` 표면이 없다 | **미충족** — [구현 차이 §12.21](../../90-implementation-gap.ko.md) |
| Spot 메시징 대상 | 불투명한 `SpotHandle`; 내부 주소 갱신 1회 | handle resolver와 handle 기반 outbound를 제공 | 일치. stale route에서 안전한 1회 refresh를 unit test로 검증한다. |
| custom Spot route resolver | 정식 location store와 handle resolver만 public | transport route lookup은 runtime 내부에만 존재 | 일치. package reflection에서 public type 부재를 검증한다. |
| dispatch 최적화 | registration 시 최적화하며 public mode 없음 | public mode type과 property 없음 | 일치. contract test로 검증한다. |
| typed packet name | registration descriptor가 한 번 결정 | typed framework call에 `PacketName(...)` 없음 | 일치. Stream Connector의 명시적 packet identity는 별도 계약으로 유지한다. |
| actor Spot 접근 | generic actor context에는 Spot instance getter 없음 | getter 없음 | 일치. Spot handler 인자를 사용한다. |
| actor membership | nullable `SpotRid`가 join 상태의 단일 기준 | `SpotRid`만 제공 | 일치. contract test로 검증한다. |
| actor join 결과 | 승인/거절 sealed record이며 승인 결과만 필수 actor ref를 가짐 | sealed `Accepted`/`Rejected` 결과 | 일치. contract test로 검증한다. |
| manual connection | capability별 `IZLinkEndpointConnections` runtime handle | builder capability별 runtime handle 제공 | 일치. 연결 추가/제거와 runtime 재시작 replay를 unit test로 검증한다. |
| monitoring event 상태 | kind별 sealed record | kind별 sealed event | 일치. contract/unit test로 검증한다. |
| channel, publish, session과 connector one-way call | `Submit(...)`이 `void` 반환 | 일치 | 내부 비동기 queue를 public completion으로 노출하지 않는다. |
| `IZLinkActorSendCall` | `Submit(CancellationToken): void` | 일치 | local queue 수락 뒤 오류는 runtime 관측 경로로 보낸다. |
| cancellation | 취소 가능한 `.NET` call과 callback에 `CancellationToken` 사용 | 일치 | 이 인자 모양은 다른 언어에 그대로 강제하지 않는다. |

관측·운영 public inventory에는 `ZLinkMeters`, `ZLinkFlowOrigin`, `ZLinkSpotDrainPolicy`,
`ZLinkDrainForceReason`, `ZLinkDrainResult`, `Drained`, `ForceStopped`, `IZLinkDrainControl`,
`ZlinkStreamCloseReason`, `ZlinkStreamDisconnected`도 포함한다. 앞의 connector 선언과
[ASP.NET Core Monitoring §10~12](01-system-structure.ko.md)의 전체 declaration이 정확한 member,
overload, default와 반환형을 고정하며 contract test는 이 타입들을 누락 없이 검사한다.

### 16.3 Exported 보조 타입의 정식 선언

interface가 반환하거나 인자로 받는 public enum, record, attribute, builder class도 호출자가 직접
컴파일하는 계약이다. 아래 선언은 앞 절의 interface와 같은 수준으로 고정한다. 코드에는 각 묶음의
핵심 제약만 주석으로 표시한다.

```csharp
public sealed record ActorRefSnapshot(
    RoutingId NodeRid,
    string ActorId,
    ulong Generation)
{
    public static ActorRefSnapshot From(ActorRef actorRef);
    public ActorRef ToActorRef();
}

public enum ZLinkAutoConnectType
{
    Invalid = 0,
    RouteMesh = 1,
    ClientServer = 2,
    Fanout = 4,   // location 전용 DealerMesh 값 3은 channel 표면에 노출하지 않는다.
    SpotMesh = 5
}

public interface IZLinkSpotMeshBuilder : IZLinkSpotNodeBuilder
{
    IZLinkSpotMeshBuilder UseDrainPolicy(ZLinkSpotDrainPolicy policy);
}
```

dispatch 오류는 유효한 surface, message kind, reason, action 조합으로 기록한다. enum 숫자는 event와
계기 연결에서 사용하는 고정 값이다.

```csharp
public enum ZLinkDispatchErrorSurface
{
    Channel = 0, RouteMeshChannel = 1, SpotRoute = 2,
    SpotSubscription = 3, SpotActor = 4, StreamSession = 5
}

public enum ZLinkDispatchMessageKind
{
    Request = 0, Send = 1, Publish = 2, Response = 3,
    Error = 4, ActorRequest = 5, ActorSend = 6
}

public enum ZLinkDispatchErrorReason
{
    HandlerMissing = 0, PayloadDecodeFailed = 1, HandlerException = 2,
    InvalidFrame = 3, ReplyPathMissing = 4, UnexpectedReply = 5
}

public enum ZLinkDispatchErrorAction { ReplyError = 0, Drop = 1, FailCaller = 2 }

public enum ZLinkDrainState
{
    Serving = 0, Draining = 1, Drained = 2, ForceStopping = 3
}
```

class 기반 Spot handler의 attribute는 constructor 인자를 필수 identity로 사용한다. method 기반
attribute의 선택적 packet name과 혼동하지 않는다.

```csharp
[AttributeUsage(AttributeTargets.Class)]
public sealed class ZLinkSpotPacketHandlerAttribute(string packetName) : Attribute
{
    public string PacketName { get; }
}

[AttributeUsage(AttributeTargets.Class)]
public sealed class ZLinkSpotRequestHandlerAttribute(string packetName) : Attribute
{
    public string PacketName { get; }
}

[AttributeUsage(AttributeTargets.Class)]
public sealed class ZLinkSpotSubscriptionHandlerAttribute(string topic) : Attribute
{
    public string Topic { get; }
}

[AttributeUsage(AttributeTargets.Class)]
public sealed class ZLinkSpotActorSendHandlerAttribute(string packetName) : Attribute
{
    public string PacketName { get; }
}

[AttributeUsage(AttributeTargets.Class)]
public sealed class ZLinkSpotActorRequestHandlerAttribute(string packetName) : Attribute
{
    public string PacketName { get; }
}

[AttributeUsage(AttributeTargets.Class)]
public sealed class ZLinkSpotTimerHandlerAttribute(
    string name,
    double periodMilliseconds) : Attribute
{
    public string Name { get; }
    public double PeriodMilliseconds { get; } // attribute 상수 제약 때문에 밀리초 숫자를 사용한다.
}
```

location row와 watch의 종류 값은 저장소 직렬화와 event에서 공유한다. `ZLinkLocationKey`는 인코딩된
문자열을 노출하지 않는 닫힌 typed key 집합이다.

```csharp
public enum ZLinkLocationAutoConnectType
{
    Invalid = 0, RouteMesh = 1, ClientServer = 2,
    DealerMesh = 3, Fanout = 4, SpotMesh = 5
}

public enum ZLinkRouteKind
{
    Invalid = 0, ActorSession = 1, SpotName = 2, FrameworkRoute = 3
}

public enum ZLinkLocationKind
{
    Invalid = 0, Peer = 1, Spot = 2, Actor = 3, Route = 4
}

public enum ZLinkLocationChangeType { Upserted = 1, Removed = 2, Expired = 3 }

public enum ZLinkLocationTopologyState
{
    Discovered = 1, Connecting = 2, Ready = 3,
    Lost = 4, Error = 5, Stopped = 6
}

public enum ZLinkLocationWriteStatus
{
    Stored = 1, IgnoredStale = 2, RejectedConflict = 3
}

public abstract record ZLinkLocationKey
{
    public sealed record Peer(ZLinkPeerLocationKey Key) : ZLinkLocationKey;
    public sealed record Spot(ZLinkSpotLocationKey Key) : ZLinkLocationKey;
    public sealed record Actor(ZLinkActorLocationKey Key) : ZLinkLocationKey;
    public sealed record Route(ZLinkRouteLocationKey Key) : ZLinkLocationKey;
}
```

Spot monitoring 값은 native 상태를 public event와 query 결과로 투영한다. 값과 underlying type을
변경하면 package consumer의 switch와 직렬화가 달라지므로 다음 선언을 유지한다.

```csharp
public enum ZLinkSpotKind { Invalid = 0, Entry = 1, User = 2 }
public enum ZLinkSpotNodeState
{
    Idle = 1, Connecting = 2, PartialReady = 3, Ready = 4, Error = 5
}
public enum ZLinkSpotPeerSource { Manual = 1, Discovery = 2, Mixed = 3 }
public enum ZLinkSpotPeerKind { SpotMesh = 1, RouterChannel = 2 }
public enum ZLinkSpotPeerState { Configured = 1, Connecting = 2, Connected = 3 }
public enum ZLinkSubjectKind : uint { None = 0, Topic = 1, Pattern = 2 }
public enum ZLinkSpotRole { Pub = 1, Sub = 2 }
```

Stream Connector의 transport, dispatch, packet, 오류 값과 생성 진입점은 별도 package의 public
계약이다. `Create(...)`는 구현 class가 아니라 interface를 반환한다.

```csharp
public enum ZlinkStreamTransport { Tcp, Tls, WebSocket, WebSocketSecure }
public enum ZlinkStreamDispatchMode { Manual, Immediate }
public enum ZlinkStreamMessageKind : byte
{
    Send = 1, Request = 2, Response = 3, Error = 4, Control = 5
}
public enum ZlinkStreamErrorCode
{
    Disconnected, ConfigurationError, ValidationFailed, RequestTimeout,
    ConnectTimeout, FrameDecodeFailed, FrameTooLarge, SendFailed,
    CompressionFailed, TlsValidationFailed, DecompressionFailed,
    UserCallbackFailed, ObserverFailed, ObserverDropped, RemoteError,
    ReceivedMessageDropped
}

public static class ZlinkStreamConnectorFactory
{
    public static IZlinkStreamConnector Create(ZlinkStreamConnectorOptions options);
}

public sealed class ZlinkStreamException : Exception
{
    public ZlinkStreamException(ZlinkStreamError error);
    public ZlinkStreamError Error { get; }
}

public sealed class ZlinkStreamConnectorOptions
{
    public required Uri Endpoint { get; init; }
    public ZlinkStreamTransport? Transport { get; init; }
    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(5);
    public TimeSpan RequestTimeout { get; init; } = TimeSpan.FromSeconds(30);
    public TimeSpan WaitTimeout { get; init; } = TimeSpan.FromSeconds(5);
    public ZlinkStreamHeartbeatOptions Heartbeat { get; init; } = new();
    public ZlinkStreamReconnectOptions Reconnect { get; init; } = new();
    public int MaxSendPayloadSize { get; init; } = 64 * 1024;
    public int MaxReceivePayloadSize { get; init; } = 64 * 1024;
    public int MaxReceivedMessages { get; init; } = 1024;
    public int MaxPendingDispatchCallbacks { get; init; } = 1024;
    public int MaxInboundObserverNotifications { get; init; } = 1024;
    public int MaxInboundObserverPayloadPreviewBytes { get; init; }
    public bool SkipServerCertificateValidation { get; init; }
    public ZlinkStreamDispatchMode DispatchMode { get; init; } = ZlinkStreamDispatchMode.Manual;
    public ZlinkStreamCompression Compression { get; init; } = ZlinkStreamCompression.Lz4;
    public IZlinkStreamCompressionCodec? CompressionCodec { get; init; }
    public IZlinkStreamPacketNameResolver NameResolver { get; init; }
        = new ZlinkStreamPacketNameResolver();
    public IZlinkStreamPayloadCodec? PayloadCodec { get; init; }
}

[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
public sealed class ZlinkStreamPacketNameAttribute(string name) : Attribute
{
    public string Name { get; }
}
```

heartbeat와 reconnect는 connector가 소유한다. 아래 기본값도 계약에 포함한다.

```csharp
public sealed class ZlinkStreamHeartbeatOptions
{
    public bool Enabled { get; init; } = true;
    public TimeSpan Interval { get; init; } = TimeSpan.FromSeconds(1);
    public TimeSpan Timeout { get; init; } = TimeSpan.FromSeconds(5);
}

public sealed class ZlinkStreamReconnectOptions
{
    public bool Enabled { get; init; } = true;
    public TimeSpan InitialDelay { get; init; } = TimeSpan.FromMilliseconds(250);
    public TimeSpan MaxDelay { get; init; } = TimeSpan.FromSeconds(5);
    public double BackoffFactor { get; init; } = 2.0;
    public int? MaxAttempts { get; init; } = 3;
}
```

위 heartbeat option은 connector가 서버 응답을 감시하는 설정이다. framework STREAM 서버도 공통
liveness 정책에 따라 1초마다 ping을 보내고 5초 동안 pong이 없으면 `HeartbeatTimeout`, application
message가 30초 동안 없으면 `IdleTimeout`으로 session을 종료한다. control packet은 application idle
시간을 갱신하지 않는다. 서버 값은 언어 간 같은 동작을 위한 내부 고정 정책이며
`IZLinkStreamNodeBuilder`에 별도 설정을 추가하지 않는다.

host가 framework runtime을 종료할 때는 session callback과 정상 정리를 전체 STREAM node 기준으로
최대 1초 기다린다. 이 상한을 넘으면 남은 session의 transport close를 시작하고 node 종료를 계속한다.
비협조 callback이 process 종료를 무한히 지연시키지 않게 하는 내부 고정 정책이며 public 설정은
추가하지 않는다. 정상 `CloseAsync()`는 이 강제 종료 경로와 달리 현재 peer의 transport close가
끝난 뒤 완료된다.

상한이 끝나면 실행 중 callback에 전달한 cancellation token을 취소한다. callback이 취소를 무시하면
framework는 해당 application callback을 더 기다리지 않으며, callback이 접근할 수 있는 session
context와 dependency injection scope도 동시에 dispose하지 않는다. 이 자원은 ForceStopping으로
종료되는 process와 함께 회수된다. framework transport close와 session table 제거는 callback과
분리해서 끝내므로, framework 소유 cleanup task가 application callback 뒤에 남아 process 종료 순서를
바꾸지 않는다.

typed helper는 별도 codec 등록 없이 JSON을 기본으로 사용한다. builder constructor는 public이 아니며
아래 extension에서만 얻는다.

```csharp
public static class ZlinkStreamJsonCodec
{
    public static JsonSerializerOptions SerializerOptions { get; private set; }
    public static void Configure(JsonSerializerOptions options);
}

public static class ZlinkStreamJsonExtensions
{
    public static T FromJson<T>(this ZlinkStreamEncodedPayload payload);
    public static ZlinkStreamEncodedPayload ToJson<T>(this T value);
}

public static class ZlinkStreamTypedConnectorExtensions
{
    public static IZlinkStreamConnector WithInboundObserver(
        this IZlinkStreamConnector connector,
        Func<ZlinkStreamInboundObservation, CancellationToken, ValueTask> observer);
    public static ZlinkStreamTypedSendBuilder Send<TPayload>(
        this IZlinkStreamConnector connector,
        TPayload payload);
    public static ZlinkStreamTypedRequestBuilder Request<TPayload>(
        this IZlinkStreamConnector connector,
        TPayload payload);
    public static IDisposable On<TPayload>(
        this IZlinkStreamConnector connector,
        Func<ZlinkStreamMessage<TPayload>, CancellationToken, ValueTask> handler);
    public static IDisposable On<TPayload>(
        this IZlinkStreamConnector connector,
        string name,
        Func<ZlinkStreamMessage<TPayload>, CancellationToken, ValueTask> handler);
    public static ZlinkStreamTypedWaitBuilder<TPayload> WaitFor<TPayload>(
        this IZlinkStreamConnector connector,
        string name);
    public static ZlinkStreamTypedWaitBuilder<TPayload> WaitFor<TPayload>(
        this IZlinkStreamConnector connector);
}

public sealed class ZlinkStreamTypedWaitBuilder<TPayload>
{
    public ZlinkStreamTypedWaitBuilder<TPayload> Timeout(TimeSpan timeout);
    public ZlinkStreamTypedWaitBuilder<TPayload> Where(
        Func<ZlinkStreamMessage<TPayload>, bool> predicate);
    public ValueTask<ZlinkStreamMessage<TPayload>> Async(
        CancellationToken cancellationToken = default);
}

public sealed class ZlinkStreamTypedSendBuilder
{
    public ZlinkStreamTypedSendBuilder PacketName(string name);
    public ZlinkStreamTypedSendBuilder Metadata(string key, string value);
    public ZlinkStreamTypedSendBuilder Metadata(ZlinkStreamMetadata metadata);
    public ZlinkStreamTypedSendBuilder Compress();
    public void Submit(CancellationToken cancellationToken = default);
}

public sealed class ZlinkStreamTypedRequestBuilder
{
    public ZlinkStreamTypedRequestBuilder PacketName(string name);
    public ZlinkStreamTypedRequestBuilder Metadata(string key, string value);
    public ZlinkStreamTypedRequestBuilder Metadata(ZlinkStreamMetadata metadata);
    public ZlinkStreamTypedRequestBuilder Timeout(TimeSpan timeout);
    public ZlinkStreamTypedRequestBuilder Compress();
    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
    public void Submit(Action<ZlinkStreamResult> callback);
    public void Submit<TReply>(Action<ZlinkStreamResult<TReply>> callback);
}
```

## 17. 공개 계약 산출물 검증

이 절은 **사람이 읽는 이 문서의 계약과 실제 배포 산출물을 정확히 비교하는 방법**을 정의한다.
공개 API의 의미와 사용 조건은 위 절들이 설명한다. 아래 text snapshot은 그 계약을 assembly와
NuGet package에서 **한 항목도 빠뜨리지 않고 검사하기 위한 기계 판독 가능한 부속 명세**다.

**snapshot은 문서 트리가 아니라 `.NET` 코드 옆(`framework/languages/dotnet/contract/`)에 둔다.**
사람이 읽는 문서가 아니라 검증기가 읽는 산출물이기 때문이다.

### 17.1 API snapshot

`framework/languages/dotnet/contract/api/`에는 정식 배포 대상 assembly 여섯 개의 모든 public type과 member를 기록한다.
각 파일은 다음 항목을 구분한다.

- assembly와 type 소유권
- base type과 구현 interface
- field, constructor, property, event와 method
- overload, parameter 이름, 기본값과 `ref`·`in`·`out`
- nullable read/write 상태
- generic variance와 runtime metadata에 남는 제약
- `required`, `init`, readonly struct, ref struct와 custom modifier

검증기는 정식 snapshot, source build assembly, 실제 NuGet package assembly를 세 방향으로 비교한다.
세 결과 중 하나라도 다르면 공개 계약 검증은 실패한다.

### 17.2 package snapshot

`framework/languages/dotnet/contract/packages/`에는 package마다 다음 정보를 기록한다.

- 전체 archive entry 경로
- package id, 작성자, 설명과 repository metadata
- target framework별 dependency id, version과 asset 제외 조건
- 배포 DLL과 XML 문서의 위치

매번 달라지는 contract 검증용 version, Git commit 값과 NuGet core-properties 파일 이름만 명시한
placeholder로 정규화한다. 그 밖의 예상하지 않은 파일이나 metadata 변화는 검증 실패다.

### 17.3 갱신 절차

일반 검증은 다음 명령을 사용한다.

```bash
cd framework/languages/dotnet
./scripts/verify_packaged_contract.sh
```

공개 계약 변경 후보를 검토할 때는 정식 snapshot 디렉토리 밖의 별도 경로에 snapshot 후보를 생성한다.

```bash
./scripts/verify_packaged_contract.sh \
  --generate-snapshot /tmp/zlink-dotnet-contract-review
```

생성 모드는 정식 snapshot 디렉토리를 직접 덮어쓰지 못한다. 후보와 현재 snapshot의 diff를 기능별 공개
계약 문서의 전·후 시그니처와 함께 리뷰한 뒤, 승인된 항목만 정식 snapshot에 반영한다. 구현이
달라졌다는 이유만으로 snapshot을 먼저 갱신하지 않는다. 검증기는 모든 project의 MSBuild
`IsPackable` 평가 결과, 정확한 6개 package 집합, package source mapping, 임시 package cache와
깨끗한 consumer 실행까지 함께 확인한다.

### 17.4 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` | source build assembly만 대상으로 정식 API snapshot과 모든 공개 서명이 일치하는지 확인한다. package 산출물은 이 테스트의 범위가 아니다. |
| `PublicContractSnapshotTests.Renderer_Preserves_CSharp_PublicContract_Distinctions` | snapshot renderer가 nullable, accessor, generic, by-ref와 type 제약을 서로 다른 계약으로 기록한다. |
| `SCRIPT:scripts/verify_packaged_contract.sh` | 정식 API snapshot, source build assembly, 실제 NuGet package assembly의 공개 서명을 세 방향으로 비교하고 package snapshot과 깨끗한 consumer 실행까지 검증한다. 실제 배포 package 계약의 최종 gate다. |

[^public-contract]: 라이브러리가 외부에 약속한 공식 API. 한 번 공개되면 호환성을 깨지 않고는 변경하기 어렵다.
[^transport]: 메시지가 실제로 네트워크나 IPC 위에서 오가는 하부 계층. ZLink에서는 socket, stream, route 등이 이에 해당한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md) | [이전: 기능 맵 — 무엇을, 얼마나 쉽게, 언제](../../../../dotnet/guide/04-feature-map.ko.md) | [다음: ZLink Framework ASP.NET Core Channel Messaging](01-system-structure.ko.md)
<!-- framework-adapter-nav:bottom:end -->
