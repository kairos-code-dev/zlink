[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [Monitoring](./aspnet-core-monitoring.ko.md) | [Registry](./aspnet-core-registry.ko.md)

# Draft -- ZLink Framework .NET Interface Catalog

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`에서 `ZLink Framework`가 노출할 인터페이스와
> attribute를 한 곳에 모은 기준 문서다.

## 1. 목적

이 문서는 `.NET` `ZLink Framework`의 **모든 공용 인터페이스와 attribute 정의**를
한 곳에 모은다. 다른 문서에서 인터페이스를 참조할 때는 이 문서를 기준으로 한다.

사용 예시와 프로그래밍 모델 설명은 이 문서에 넣지 않는다.
사용법은 아래 문서를 참고한다.

- 서버 간 messaging 프로그래밍 모델 →
  [aspnet-core-channel-messaging.ko.md](./aspnet-core-channel-messaging.ko.md)
- 서버 간 messaging 샘플 →
  [channel-messaging-samples.ko.md](./channel-messaging-samples.ko.md)
- SPOT 통합 →
  [aspnet-core-spot.ko.md](./aspnet-core-spot.ko.md)
- SPOT 샘플 →
  [spot-samples.ko.md](./spot-samples.ko.md)
- STREAM 통합 →
  [aspnet-core-stream.ko.md](./aspnet-core-stream.ko.md)
- STREAM 샘플 →
  [stream-samples.ko.md](./stream-samples.ko.md)
- Registry 통합 →
  [aspnet-core-registry.ko.md](./aspnet-core-registry.ko.md)

## 2. 인터페이스 전체 목록

| 분류 | 인터페이스 | 역할 | section |
|------|-----------|------|---------|
| context | `IZLinkHandlerContext` | 모든 handler context의 공통 기반 | 3.1 |
| context | `IZLinkSpotSelf` | spot 자신의 identity 조회 | 3.2 |
| context | `IZLinkSpotContext` | SPOT handler context 기반. packet/subscribe/timer 등록과 channel 호출 표면 | 3.2 / 4.3.1 |
| context | `IZLinkSpotActorMembership` | Spot 안에서 actor를 attach/detach할 때 framework가 주입하는 별도 표면 | 4.4.1 |
| handler | `IZLinkRequestHandler<TRequest, TResponse>` | request-response handler | 4.1 |
| handler | `IZLinkSendHandler<TMessage>` | one-way send handler | 4.2 |
| handler | `IZLinkRouteSendHandler<TMessage>` | routed channel one-way send handler | 4.2.1 |
| handler | `IZLinkRouteRequestHandler<TRequest, TReply>` | routed channel request-response handler | 4.2.1 |
| handler | `IZLinkPublishHandler<TMessage>` | pub/sub publish handler | 4.3 |
| handler | `IZLinkSpotPacketHandler<TSpot, TMessage>` | SPOT one-way packet handler | 4.3.1 |
| handler | `IZLinkSpotRequestHandler<TSpot, TRequest, TReply>` | SPOT request-response handler | 4.3.1 |
| handler | `IZLinkSpotSubscriptionHandler<TSpot, TEvent>` | SPOT subscription handler | 4.3.1 |
| handler | `IZLinkSpotTimerHandler<TSpot>` | SPOT lifecycle timer handler | 4.3.1 |
| handler | `IZLinkSession` | stream session lifecycle + header/body callback | 4.4 |
| context | `IZLinkSessionContext` | stream session의 send, channel request, actor dispatch 표면 | 4.4 |
| context | `IZLinkSessionIdentityContext` | stream session identity 조회 | 4.4 |
| context | `IZLinkSessionChannelClient` | session 안에서 channel request/send 호출 | 4.4 |
| context | `IZLinkSessionClientStream` | session에서 client stream으로 send/reply | 4.4 |
| context | `IZLinkSessionActorDispatchContext` | session에서 actor 생성과 dispatch 수행 | 4.4 |
| context | `IZLinkSessionLifecycle` | session close 제어 | 4.4 |
| context | `IZLinkSessionActorAttachmentContext` | actor와 session stream 연결/해제 | 4.4 |
| value | `IZLinkActorRef` | session이 actor dispatch target으로 들고 있는 handle | 4.4.1 |
| handler | `IZLinkActor` | actor runtime 안에서 생성되는 application actor | 4.4.1 |
| context | `IZLinkActorContext` | actor packet 등록, spot join, channel/client stream 호출 | 4.4.1 |
| handler | `IZLinkActorSendHandler<TMessage>` | session actor dispatch one-way handler | 4.4.2 |
| handler | `IZLinkActorRequestHandler<TRequest, TReply>` | session actor dispatch request-response handler | 4.4.2 |
| value | `ZLinkMessageMetadata` | actor/session proxy call에 전달되는 application/codec metadata snapshot | 4.4.2 |
| policy | `IZLinkMessageMetadataPolicy` | application metadata forwarding 허용 여부 | 4.4.2 |
| factory | `IZLinkActorFactory` | actor type별 actor 생성 | 4.4.1 |
| handler | `IZLinkSpotActorJoinHandler<TSpot, TActor, TRequest, TReply>` | spot에 actor가 join할 때 호출되는 handler | 4.4.1 |
| client | `IZLinkActorClient` | actor id 기반 actor runtime 호출 | 5.5 |
| client | `IZLinkRouteClient` | routed channel direct target send/request (runtime/internal 표면) | 5.5.1 |
| client | `IZLinkSessionProxy` | actor id 기반 actor -> client session 호출 | 5.6 |
| resolver | `IZLinkActorPlayRouteResolver` | actor id에서 play/runtime route 조회 | 5.7 |
| resolver | `IZLinkActorSessionRouteResolver` | actor id에서 현재 session route 조회 | 5.7 |
| writer | `IZLinkActorSessionLocationWriter` | session binding/unbind를 application route store에 반영 | 5.7 |
| handler | `IZLinkRuntimeEventHandler<TEvent>` | runtime monitoring event handler | 10.3 |
| lifecycle | `IZLinkSpot` | spot lifecycle registration base | 4.3.1 |
| stream | `IZLinkStream` | stream I/O와 peer 식별 | 4.4 |
| value | `ZLinkStreamSessionError` | stream session error category enum | 4.4 |
| value | `ZLinkStreamError` | stream error detail + errno helper | 4.4 |
| value | `ZLinkDispatchMode` | dispatch activation/performance mode enum | 4.4.3 |
| value | `ZLinkSocketEventKind`, `ZLinkSocketEvent` | socket runtime event | 10.3 |
| value | `ZLinkRegistryEventKind`, `ZLinkRegistryEvent` | registry runtime event | 10.3 |
| value | `ZLinkSpotEventKind`, `ZLinkSpotEvent` | spot runtime event | 10.3 |
| options | `IZLinkMonitoringOptions` | runtime monitoring source 등록 옵션 | 10.3 |
| options | `IZLinkDispatchOptions` | dispatch mode configuration | 4.4.3 |
| options | `IZLinkCodecRegistryBuilder` | codec registry builder | 6.1 |
| serializer | `IZLinkMessageSerializer` | `Message` payload 직렬화/역직렬화 | 4.5 |
| client | `IZLinkClientServerClient` | client-server channel outbound client base | 5.1 |
| client | `IZLinkClient` | 서버 간 outbound client | 5.1 |
| client | `IZLinkSpotClient` | SPOT outbound client | 5.2 |
| client | `IZLinkSpotMeshPublisherClient` | spot mesh publisher client base | 5.3 |
| client | `IZLinkSpotPublisherClient` | spot channel publish client | 5.3 |
| client | `IZLinkFanoutPublisher` | fanout publisher base | 5.4 |
| client | `IZLinkEventPublisher` | pub/sub event publisher | 5.4 |
| builder | `IZLinkFrameworkOptions` | framework 등록 루트 builder | 6.1 |
| builder | `IZLinkChannelBuilder` | legacy/general channel 등록 builder | 6.1 |
| builder | `IZLinkClientServerChannelBuilder` | client-server channel 등록 builder | 6.1 |
| builder | `IZLinkFanoutChannelBuilder` | fanout (pub/sub) channel 등록 builder | 6.1 |
| builder | `IZLinkDealerMeshChannelBuilder` | dealer mesh channel 등록 builder | 6.1 |
| builder | `IZLinkRouteMeshChannelBuilder` | route mesh channel 등록 builder | 6.1 |
| builder | `IChannelServerCapabilityBuilder` | channel server capability builder | 6.1 |
| builder | `IChannelClientCapabilityBuilder` | channel client capability builder | 6.1 |
| builder | `IDealerMeshChannelClientCapabilityBuilder` | dealer mesh client capability builder | 6.1 |
| builder | `IChannelPublisherCapabilityBuilder` | channel publisher capability builder | 6.1 |
| builder | `IChannelSubscriberCapabilityBuilder` | channel subscriber capability builder | 6.1 |
| builder | `IZLinkStreamNodeBuilder` | STREAM node 등록 builder | 6.1 |
| builder | `IZLinkSpotNodeBuilder` | SPOT node 등록 builder | 6.3 |
| builder | `IZLinkSpotMeshBuilder` | SPOT mesh 등록 builder | 6.3 |
| builder | `IZLinkSpotMeshNodeBuilder` | SPOT mesh node 등록 builder | 6.3 |
| management | `IZLinkChannelConnectionManager` | channel capability별 수동 연결 제어 | 6.2 |
| management | `IZLinkSpotManager` | spot 인스턴스 생성/삭제 | 6.3 |
| management | `IZLinkSpotConnectionManager` | spot capability별 수동 연결 제어 | 6.4 |
| timer | `IZLinkTimer` | timer handle | 7 |
| filter | `IZLinkHandlerFilter` | handler 전후 공통 처리 | 8 |
| filter | `ZLinkHandlerInvocation` | filter pipeline 호출 context | 8 |
| filter | `ZLinkHandlerDelegate` | filter pipeline next delegate | 8 |
| registry | `IZLinkRegistryQuery` | in-process Registry 조회 | 10.1 |
| registry | `IZLinkRegistryQueryClient` | 원격 Registry 조회 | 10.2 |

## 3. Context 인터페이스

### 3.1 공통 context

모든 handler context가 공유하는 최소 집합이다.
실제 구현에서는 transport별 부가 정보가 파생 context에 추가된다.

```csharp
public interface IZLinkHandlerContext
{
    string? ChannelName { get; }
    string? PacketName { get; }
    string? ContentType { get; }
    string? CorrelationId { get; }
    DateTimeOffset? Deadline { get; }
    CancellationToken ConnectionAborted { get; }
}
```

`IServiceProvider Services`는 현재 단계에서 framework 내부 전용(internal)이며
public 표면으로 노출하지 않는다. handler 안에서 서비스가 필요하면 service locator로
context에서 꺼내 쓰지 않고, handler class의 생성자 주입(constructor injection)으로 받는다.

### 3.2 파생 context

| context 타입 | 사용처 | 추가 정보 |
|-------------|--------|----------|
| `ZLinkRequestContext` | request-response handler | caller metadata, timeout |
| `ZLinkSendContext` | one-way send handler | caller metadata |
| `ZLinkPublishContext` | publish handler | topic, source |
| `ZLinkRouteSendContext` | routed channel send handler | source routing id, router channel id, metadata |
| `ZLinkRouteRequestContext` | routed channel request handler | source routing id, router channel id, metadata, deadline |
| `ZLinkSpotRequestContext` | SPOT request handler | self spot info, source rid, source spot rid |
| `ZLinkSpotSubscriptionContext` | SPOT subscription handler | self spot info, topic, source rid, dispatch metadata |

파생 context의 상세 필드는 구현 전에 더 좁혀야 한다.
현재 초안에서는 이름과 역할만 고정한다.

`SPOT` 객체 안에서는 외부 lookup과 별개로, 현재 spot 자신에 대한 identity 조회도
가능해야 한다. 이 초안에서는 별도 `Self` wrapper를 두지 않고
SPOT 생성자에서 받는 `IZLinkSpotContext`에 `SpotRid`, `NodeRid`,
`SpotName`을 직접 둔다. handler 호출에 붙는 `ZLinkRequestContext`,
`ZLinkSendContext`, `ZLinkPublishContext`와 SPOT 객체가 들고 있는
`IZLinkSpotContext`는 목적이 다르다.

## 4. Handler 인터페이스

### 4.1 request-response handler

요청 하나에 응답 하나가 돌아오는 handler다.

```csharp
public interface IZLinkRequestHandler<in TRequest, TResponse>
{
    ValueTask<TResponse> HandleAsync(
        TRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken);
}
```

- `TRequest`는 이미 decode된 body다.
- `TResponse`도 framework가 encode할 typed 결과다.
- raw multipart header는 인자로 주지 않는다.
- 이 인터페이스를 구현한 class는 `ZLinkHandlerScanner`가 attribute 없이도
  자동으로 endpoint로 등록한다. attribute(`[ZLinkRequest]`)가 붙은 메서드와
  인터페이스 구현 두 방식 모두 지원된다.

### 4.2 send handler

응답이 없는 one-way 전송을 처리하는 handler다.

```csharp
public interface IZLinkSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkSendContext context,
        CancellationToken cancellationToken);
}
```

이 인터페이스를 구현하면 `ZLinkHandlerScanner`가 attribute 없이도 endpoint로
자동 등록한다. attribute(`[ZLinkSend]`) 기반과 interface 기반은 둘 다 지원된다.

### 4.2.1 routed channel handler

routed channel(`AddRouteChannel(...)`, `AddRouteMeshChannel(...)`)이 받는 메시지를
처리하는 handler다. 일반 channel handler와 달리 source `RoutingId`까지 context로
같이 노출한다.

```csharp
public interface IZLinkRouteSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        ZLinkRouteSendContext context,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkRouteRequestHandler<in TRequest, TReply>
{
    ValueTask<TReply> HandleAsync(
        ZLinkRouteRequestContext context,
        TRequest request,
        CancellationToken cancellationToken);
}

public sealed class ZLinkRouteSendContext : ZLinkHandlerContext
{
    public string RouterChannelId { get; }
    public RoutingId SourceNodeRid { get; }
    public ZLinkMessageMetadata Metadata { get; }
}

public sealed class ZLinkRouteRequestContext : ZLinkHandlerContext
{
    public string RouterChannelId { get; }
    public RoutingId SourceNodeRid { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public DateTimeOffset? Deadline { get; }
}
```

routed channel handler 등록은 transport builder의
`AddSendHandler<THandler, TMessage>()` /
`AddRequestHandler<THandler, TRequest, TReply>()`로 한다 (§6.1
`IZLinkRouteChannelBuilder` 참고).

### 4.3 publish handler

pub/sub로 publish된 메시지를 처리하는 handler다. 이름은 producer 쪽 동사
(`IZLinkEventPublisher.Publish(...)`)와 일치시켜 `Request` / `Send` / `Publish`
세 표면을 같은 패턴으로 읽도록 한다. payload 자체는 `*Event` 같은 의미적 이름을
그대로 써도 된다.

```csharp
public interface IZLinkPublishHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkPublishContext context,
        CancellationToken cancellationToken);
}
```

topic이나 pattern 정보가 필요해도 별도 `Topic` handler 이름을 늘리지 않고,
`ZLinkPublishContext` 안에서 읽는다.

이 인터페이스를 구현하면 `ZLinkHandlerScanner`가 attribute 없이도 endpoint로
자동 등록한다. attribute(`[ZLinkPublish]`) 기반과 interface 기반은 둘 다 지원된다.

### 4.3.1 SPOT lifecycle handler

현재 framework 초안은 `SpotNode.CreateSpot()`로 만든 low-level `Spot` 위에
application-friendly lifecycle를 얹는 방향이다. SPOT 객체는 Actor와 같은 방식으로
callback 표면과 실행 context 표면을 분리한다. 샘플과 wrapper 문서에서 공통으로
쓰는 최소 표면은 아래 정도다.

```csharp
public interface IZLinkSpot
{
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

public interface IZLinkSpotContext
{
    RoutingId SpotRid { get; }
    RoutingId NodeRid { get; }
    string SpotName { get; }

    void AddPacket<THandler>()
        where THandler : class;

    void AddSubscribe<THandler>(
        string topic)
        where THandler : class;

    void AddActorJoin<THandler, TActor, TRequest, TReply>()
        where THandler : class
        where TActor : IZLinkActor;

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message);

    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall RequestChannel<TRequest>(
        string channelName,
        TRequest request);

    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        CancellationToken cancellationToken = default)
        where THandler : class;
}

public interface IZLinkSpotPacketHandler<TSpot, in TMessage>
    where TSpot : IZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRequestHandler<TSpot, in TRequest, TReply>
    where TSpot : IZLinkSpot
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotSubscriptionHandler<TSpot, in TEvent>
    where TSpot : IZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TEvent message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotTimerHandler<TSpot>
    where TSpot : IZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        CancellationToken cancellationToken);
}
```

`Configure()`는 SPOT이 생성된 뒤 descriptor를 바인딩하기 전에 한 번 호출된다.
`Context.AddPacket(...)`, `Context.AddSubscribe(...)`, `Context.AddActorJoin(...)`은
이 단계에서만 허용한다. 초기화 뒤에 handler를 추가하면 native subscription과
dispatch table의 의미가 흔들리기 때문에 framework는 예외를 반환한다.

`Context.Publish(topic, ...)`는 현재 SPOT이 속한 active SPOT channel에 publish하는
편의 함수다. `Context.SendChannel(...)`과 `Context.RequestChannel(...)`은 현재
SPOT 실행 문맥에서 channel client를 호출한다. `OnClosingAsync(...)`는
`IZLinkSpotManager.RemoveAsync(...)`로 SPOT을 정상 제거할 때 실행 문맥 안에서
호출된다. host shutdown이나 process 종료에서 반드시 호출되는 destructor 의미는
아니다.

이 초안이 기대하는 low-level `.NET` 바인딩 기반 표면도 문서 안에 같이 고정해 둘
필요가 있다. 현재 `bindings/dotnet/src/Zlink` 기준 실제 public surface는 아래와
같다.

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

    // channel 이름 기반 호출 (attach된 dealer 경유)
    public void SendChannel(string channelName, ReadOnlyMemory<byte> payload);

    public Task<Received> RequestChannelAsync(
        string channelName,
        ReadOnlyMemory<byte> payload,
        TimeSpan timeout = default,
        CancellationToken cancellationToken = default);

    // SPOT routed 호출
    public void SendToRouter(RoutingId targetRid, RoutingId spotRid, ReadOnlyMemory<byte> payload);

    public Task<Received> RequestToRouterAsync(
        RoutingId targetRid,
        RoutingId spotRid,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken = default);

    public void ReplyToSpot(RoutingId targetRid, ReadOnlyMemory<byte> payload);

    // dispatch_info 기반 통합 dispatch callback
    public void OnDispatchEvent(Action<Spot, SpotDispatchInfo> handler);

    public void OnRouteReceive(Action<Received> handler);

    // CHANNEL_REPLY_READABLE dispatch 시 사용
    public void DrainChannelReplyFrom(object dealerSubject);
}

public sealed class Timer : IDisposable, IAsyncDisposable
{
    public static Timer FromSpot(Spot spot);

    public void Start(ulong intervalNs, ulong repeatCount);

    public void Stop();

    public ulong Recv(int flags = 0);

    public void OnFire(Action<Timer, ulong> handler);
}
```

`SpotDispatchInfo`는 core `zlink_spot_dispatch_info_t`를 감싸는 managed 타입으로,
`Event`, `SubjectKind`, `Subject`를 노출한다.

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

framework의 dispatch loop 는 아래처럼 event 종류와 `Subject`를 함께 처리한다.

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

framework timer는 이 dispatch enum에 직접 매달리지 않는다. runtime이 만든 managed
`.NET` timer가 tick을 만들고, 그 tick을 같은 spot execution context 안으로 enqueue해
timer handler를 호출한다.

`RequestChannelAsync(...)` completion 은 **같은 spot execution context 안에서**
실행된다. arbitrary thread 에서 promise 를 직접 완료하지 않는다. 이 덕분에
continuation 도 별도 SynchronizationContext 설정 없이 spot state 와 같은 실행
규칙을 따른다.

framework의 `Context.AddTimer<THandler>(...)`는 low-level native timer를 직접 노출하는
표면이 아니다. 현재 방향에서는 framework runtime이 `.NET`에서 제공하는
`PeriodicTimer` 같은 managed timer를 만들고, 그 tick을 **같은 spot execution
context** 안으로 enqueue해서 `IZLinkSpotTimerHandler<TSpot>.HandleAsync(...)`를
호출한다. `IZLinkTimer.CancelAsync()`는 이 managed timer loop를 중단하고 정리하는
고수준 handle로 보는 편이 자연스럽다.

#### 4.3.2 SPOT 실행 문맥 정책

이 절에서 중요한 것은 **내부 구현 방식**이 아니라 **사용자에게 보이는 실행 계약**이다.

framework 초안은 `Spot`을 단순 recv helper가 아니라, 같은 `Spot`에 귀속된 handler와
join이 끝난 session/actor가 **같은 spot execution context**에서 처리되는 표면으로
본다.

사용자 기준 공개 계약은 아래와 같다.

- 사용자는 `Recv(...)`나 `Drain(...)` loop를 직접 작성하지 않는다.
- 사용자는 `Context.AddPacket<THandler>(...)`, `Context.AddSubscribe<THandler>(...)`,
  `Context.AddTimer<THandler>(...)`, stream attach 같은 고수준 표면만 사용한다.
- 같은 `Spot`에 귀속된 handler, timer handler, channel reply continuation,
  stream session callback은 framework가 정한 같은 실행 문맥 규칙을 따른다.
- 이 계약이 유지되는 한, 사용자는 `SampleSpot.ActorCount` 같은 spot state를
  handler 안에서 직접 다룰 수 있다.

즉 사용자에게 보여야 하는 것은 아래뿐이다.

- handler 등록
- timer 등록
- stream attach
- spot state 접근 규칙

반대로 아래 내용은 framework 내부 구현이다.

- mailbox를 쓰는지
- queue를 몇 개 두는지
- single consumer task를 어떻게 돌리는지
- low-level callback을 어떤 internal work item으로 바꾸는지

문서가 전달해야 하는 핵심은 "framework가 같은 `Spot` 상태를 같은 실행 규칙으로
처리해 준다"는 점이지, 사용자가 mailbox runtime을 직접 소유하거나 관리한다는 뜻이
아니다.

내부 구현 예로 mailbox + single consumer 모델이 유력할 수는 있다.
하지만 그것은 구현 메모 또는 internals 성격의 설명으로 남기고, binding spec의
공개 표면에서는 handler 등록과 session/actor 조합 모델만 드러내는 편이 맞다.

### 4.4 stream session

stream은 framework Header 기반 packet path를 session lifecycle 위에서 설명하는 방향을
기본으로 본다. 즉 `STREAM` application 표면은 별도
`ZLinkStreamContext`보다 `IZLinkStream` 객체를 중심으로 본다.

```csharp
public interface IZLinkStream
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }

    ValueTask WriteAsync(
        Message payload,
        CancellationToken cancellationToken = default);

    ValueTask WriteAsync(
        Message header,
        Message body,
        CancellationToken cancellationToken = default);

    ValueTask CloseAsync(
        CancellationToken cancellationToken = default);
}

public enum ZLinkStreamSessionError
{
    Internal = 0,
    TransportError,
    // OnErrorAsync로 전달되지 않는다. handshake 실패는 runtime monitoring에만 남긴다.
    // stream-open-items.ko.md section 4.2 참고.
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
    IZLinkSessionContext Context { get; set; }

    ValueTask OnConnectedAsync(CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(CancellationToken cancellationToken);

    ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken);

    ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionIdentityContext
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }
}

public interface IZLinkSessionChannelClient
{
    IZLinkRequestCall RequestChannel<TRequest>(
        string channelName,
        TRequest request);

    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);
}

public interface IZLinkSessionClientStream
{
    IZLinkSessionSendCall Send<TMessage>(TMessage message);

    IZLinkSessionReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkSessionActorDispatchContext
{
    ValueTask<IZLinkActorRef> CreateActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActorRef> CreateRemoteActorAsync(
        RoutingId actorNodeId,
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    IZLinkSessionRequestCall Request<TRequest>(TRequest request);

    ValueTask DispatchToActorAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default);

    ValueTask DispatchToActorAsync(
        IZLinkActorRef actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionLifecycle
{
    ValueTask CloseAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionActorAttachmentContext
{
    ValueTask AttachActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask DisconnectActorAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionContext :
    IZLinkSessionIdentityContext,
    IZLinkSessionChannelClient,
    IZLinkSessionClientStream,
    IZLinkSessionActorDispatchContext,
    IZLinkSessionLifecycle;

public interface IZLinkSessionSendCall
{
    IZLinkSessionSendCall WithMetadata(string key, string value);

    IZLinkSessionSendCall WithPacketName(string messageName);

    IZLinkSessionSendCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall WithMetadata(string key, string value);

    IZLinkSessionReplyCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionRequestCall
{
    IZLinkSessionRequestCall WithPacketName(string packetName);

    IZLinkSessionRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Submit<TReply>(CancellationToken cancellationToken = default);
}
```

`CloseAsync(...)`는 현재 session의 stream peer 연결을 서버 쪽에서 끊는다.
인증 실패, protocol 위반, idle timeout처럼 더 이상 packet을 받을 필요가 없는
상황에서 사용한다. 연결 종료 뒤의 session binding 정리는 framework가 현재
`sessionId + bindingToken` 기준으로 처리하고, 전역 session route는 session location
writer의 조건부 unbind 규칙을 따른다.

`WriteAsync(...)`는 framework Header 기반 packet session에서 stream으로 packet을
쓰는 async submit이다. backpressure는 framework 내부 nonblocking write와 ready
notification으로 처리한다. 동기 `bool Write(...)`는 더 이상 public 표면이 아니며,
framework 내부 fast path에서만 쓰는 deprecated surface로 본다. application 코드는
`WriteAsync(...)` 둘 중 하나(또는 actor가 stream을 직접 쓰는 패턴이 아니라면
`IZLinkSessionProxy` 경유)를 쓴다 (actor-model §10 참고).

`OnErrorAsync(...)`는 application handler 내부 예외를 받는 callback이 아니다.
이 초안에서는 `SocketMonitor`에서 관찰 가능한 session-correlatable transport 오류만
`ZLinkStreamError`로 다시 올리는 용도로 제한한다.

session callback 실행 계약은 아래와 같이 고정한다.

- `IZLinkSession`의 callback은 native/socket callback 안에서 직접 호출하지
  않는다.
- framework는 session callback을 managed task로 넘긴 뒤 application callback을
  호출한다. 이 규칙은 transport callback이 application 처리 시간, 예외, 재진입에
  직접 묶이지 않게 하기 위한 계약이다.
- 같은 session 안에서는 `OnConnectedAsync(...)`, `OnDispatchAsync(...)`,
  `OnErrorAsync(...)`, `OnDisconnectedAsync(...)`가 서로 병렬로 실행되지 않는다.
  framework는 같은 session의 callback 순서를 보존하고, 이전 callback이 끝난 뒤
  다음 callback을 호출한다.
- 서로 다른 session의 callback은 서로 독립적이다. 같은 session 직렬성만 보장하며,
  전체 stream node에 대한 전역 단일 실행 순서는 보장하지 않는다.

여기서 `ZLinkStreamSessionError`는 framework가 먼저 보여 주는 오류 분류 enum이다.
이 분류만으로 부족할 때는 `Diagnostic` 안의 native detail을 같이 볼 수 있어야
한다. 다만 이 값은 framework가 항상 보장하는 필수 계약이 아니라, 현재 backend가
제공할 수 있을 때만 채워지는 optional detail로 본다.

즉 현재 방향은 아래처럼 정리된다.

- header session
  - `OnDispatchAsync(...)`로 decoded `ZlinkStreamHeader`와 `body`를 받는다.
  - stream에 응답을 보내거나 actor로 넘기는 동작은 `Context`를 통해 수행한다.
- 공통 lifecycle
  - `OnConnectedAsync(...)`
  - `OnDisconnectedAsync(...)`
  - `OnErrorAsync(...)`

이 인터페이스가 곧바로 "`Spot`이 session 타입을 정적으로 등록한다"는 뜻은 아니다.
게임 room 같은 상위 모델에서는 session이 먼저 독립 transport 객체로 만들어지고,
인증과 입장 절차가 끝난 뒤 특정 `Spot` 또는 actor에 귀속되는 구조가 더 자연스럽다.
즉 binding/framework가 보여 줘야 하는 것은 "`session`을 어느 `Spot`에 join시키는가"
라는 상위 조합 표면이지, `Spot` 자체가 session 타입을 직접 소유한다는 고정 모델이
아니다.

이 초안에서도 recv loop를 application 표면으로 직접 노출하지 않는다.
즉 사용자가 `Recv(...)`로 직접 drain loop를 돌리는 모델보다, framework가 dispatch를
맡고 application은 Header 기반 packet session을 구현하는 모델을 기본으로 본다.

또한 stream 핫패스에서는 `Message.ToArray()` 같은 추가 복사를 기본 사용법으로 두면
안 된다. `Message.AsReadOnlySpan()` 같은 현재 표면이나, 그 위에 얹는
protobuf/json decode helper가 가능한 한 추가 메모리 할당 없이 동작하도록
설계하는 쪽을 기본 원칙으로 본다.

#### 4.4.1 actor/session 상위 모델 메모

actor join, actor factory, stream-attached actor 모델은 현재 draft `Zlink.Framework`
구현 범위에 포함한다. 공개 계약은 `IZLinkActor`, `IZLinkActorContext.JoinSpot(...)`,
`IZLinkSpotContext.AddActorJoin<THandler, TActor, TRequest, TReply>()`, stream session의
actor dispatch 표면인 `IZLinkSessionContext`, 그리고 actor stream 연결/해제를 맡는
`IZLinkSessionActorAttachmentContext`를 기준으로 설명한다. `stage-wrapper-on-spot.ko.md`는
이 계약 위에서 room/stage wrapper를 어떻게 조직하는지 보여 주는 상위 모델 문서로
읽는다.

##### zlink native Actor API 위임

zlink 라이브러리가 native Actor API를 제공함에 따라 framework는 actor lifecycle
관리를 해당 API로 위임한다.

- `SpotNode.CreateActor(string actorId)` — actor node에서 application actor에 대응하는 native actor를 생성한다.
- `SpotNode.EntrySpot()` → `Spot` — actor join 요청을 받는 입장 spot을 얻는다.
- `Spot.RecvActorJoin(RecvFlags)` → `ActorJoinRequest?` — join 요청을 수신한다.
- `Spot.ReplyActorJoin(request, accepted, message)` — join 수락/거부 결과를 응답한다.
- `Actor.Join(spot, request, timeout, ct)` — actor가 특정 spot에 join을 요청한다.
- `Actor.Leave(spot, timeout)` — actor가 spot에서 나간다.
- `Actor.RecvPart(flags)` — STREAM 메시지 part를 수신한다.

framework의 `SpotActivation`은 `SpotDispatchEvent.ActorJoinReadable`와
`SpotDispatchEvent.ActorReadable` 이벤트를 수신해 각각 join drain과 STREAM
dispatch를 처리한다. 두 경로 모두 spot serial executor 안에서 직렬화된다.
`OnDispatchEvent` 핸들러는 spot 초기화 시 항상 등록하며, 이는 패킷/join handler가
없는 spot도 런타임에 actor가 join될 때 `ActorReadable` 이벤트를 수신하기 위함이다.

`ActorJoinReadable` 처리 시 framework는 join 요청의 `TargetActor`(해당 spot에 이미
등록된 로컬 actor)를 spot actor membership에서 조회하고, actor join handler를 호출한다.
`TargetActor`를 찾지 못하면 join 요청을 거부한다.

actor packet 실행 계약은 아래와 같이 둔다.

- actor packet은 actor interface callback으로 들어가지 않는다.
  actor는 `IZLinkActorContext.AddPacket<THandler>()`로 packet handler를 등록한다.
- actor가 아직 어떤 `Spot`에도 join되지 않은 상태라면 actor packet handler는 actor
  session runtime의 일반 dispatch 경로에서 호출된다.
- actor가 `Spot`에 join된 뒤에는 같은 actor의 packet handler를 반드시 해당 `Spot`
  실행 문맥에서 호출한다. actor가 room 또는 stage 상태를 읽고 쓸 수 있기 때문에,
  join 이후 dispatch가 stream session callback 문맥에서 직접 실행되면 안 된다.
- `JoinSpot(...)` 또는 actor join handler가 actor의 현재 `Spot`을 바꾸는 경우,
  framework는 actor session state 갱신과 이후 dispatch 선택이 서로 경합하지 않게
  해야 한다. join 이후 도착한 packet은 새 `Spot` 실행 문맥으로 들어가야 한다.
- 이 계약은 actor가 사용하는 stream I/O 표면과 `Spot` 상태 변경 표면을 분리한다.
  session은 packet ingress 역할을 하고, join된 actor의 game/domain 처리는
  `Spot` 실행 문맥에서 직렬화된다.

actor 실행 객체와 session dispatch handle은 분리한다. session은 `IZLinkActorRef`를
저장하고 dispatch에 사용한다. `IZLinkActor`는 actor node에서 생성되는 application
객체이며, framework가 `Context`를 설정한 뒤 `Configure()`를 한 번 호출한다.
callback signature는 context 인자를 반복해서 받지 않는다.

```csharp
public interface IZLinkActorRef
{
    string ActorId { get; }
    string ActorType { get; }
}

public interface IZLinkActor
{
    string ActorId { get; }

    IZLinkActorContext Context { get; set; }

    void Configure()
    {
    }

    ValueTask OnDisconnectedAsync(CancellationToken cancellationToken);
}

public interface IZLinkActorContext
{
    string ActorId { get; }
    string? SessionId { get; }
    string? SpotName { get; }
    bool IsJoined { get; }

    void AddPacket<THandler>()
        where THandler : class;

    void AddPacket<THandler>(string packetName)
        where THandler : class;

    IZLinkSpot GetSpot();

    TSpot GetSpot<TSpot>()
        where TSpot : IZLinkSpot;

    // spot은 application domain spot 이름(string)으로 식별한다. RoutingId 변환은
    // framework 내부 spot route resolver가 푼다.
    IZLinkActorJoinSpotCall<TReply> JoinSpot<TReply, TRequest>(
        string spotName,
        TRequest request);

    IZLinkRequestCall RequestChannel<TRequest>(
        string channelName,
        TRequest request);

    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkActorSendCall Send<TMessage>(TMessage message);

    IZLinkActorReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkActorJoinSpotCall<TReply>
{
    IZLinkActorJoinSpotCall<TReply> WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkActorStreamClient
{
    IZLinkActorSendCall Send<TMessage>(TMessage message);

    IZLinkActorReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkActorSendCall
{
    IZLinkActorSendCall WithMetadata(string key, string value);

    IZLinkActorSendCall WithPacketName(string messageName);

    IZLinkActorSendCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkActorReplyCall
{
    IZLinkActorReplyCall WithMetadata(string key, string value);

    IZLinkActorReplyCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkActorFactory
{
    ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorPacketHandler<in TActor, in TMessage>
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TActor actor,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkActorRequestHandler<in TActor, in TRequest, TReply>
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}

// Spot에 actor를 join할 때 호출되는 handler. spot 등록의 AddActorJoin<...>() 표면이
// 이 generic 인자를 받고, framework는 join 시점에 target spot, joining actor,
// request body를 함께 넘긴다. spot 실행 문맥 안에서 호출된다.
public interface IZLinkSpotActorJoinHandler<TSpot, TActor, in TRequest, TReply>
    where TSpot : IZLinkSpot
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}

// IZLinkSpotContext 자체에는 actor join/leave 표면을 두지 않는다.
// (§4.3.1의 단일 정의가 정답이다.) actor를 Spot에 attach/detach할 때 framework가
// 주입하는 별도 표면은 아래 IZLinkSpotActorMembership으로 분리한다.
public interface IZLinkSpotActorMembership
{
    ValueTask JoinActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask LeaveActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);
}
```

`IZLinkSpotContext` (§4.3.1)와 `IZLinkSpotActorMembership`은 같은 spot 실행 문맥에서
함께 쓰는 별도 표면이다. `IZLinkSpotContext`는 packet/subscribe/timer 등록과 channel
호출을 다루고, `IZLinkSpotActorMembership`은 Spot 안에서 actor를 attach/detach할 때
framework가 주입하는 표면이다. actor join handler 코드가 actor membership을 직접
관리해야 할 때만 이 두 번째 표면을 함께 받는다.

actor context는 현재 client session의 `SessionId`만 조회값으로 노출한다. session router
id와 binding token은 actor -> client send/request를 위한 runtime 내부 metadata와 session
route resolver/writer의 책임이며 actor context에 노출하지 않는다. actor가 session 위치를
직접 저장하면 재접속 시 stale 상태가 되기 쉽기 때문이다.

framework runtime은 actor context를 먼저 주입한 뒤 `Configure()`를 한 번 호출한다.
actor packet handler 등록은 이 단계에서 `Context.AddPacket<THandler>(...)`로 한다.
이렇게 하면 session handler가 actor 내부 packet 목록을 알 필요가 없다.

`RequestChannel(...)`과 `SendChannel(...)`은 actor의 join 상태에 따라 내부 경로를
선택한다. join 전에는 framework의 일반 channel client 경로를 사용하고, join 후에는
현재 actor가 join된 `Spot`의 channel client 경로를 사용한다. 사용자는 actor
코드에서 `IZLinkClient`와 `IZLinkSpotClient`를 직접 구분하지 않는다.

`GetSpot(...)`은 actor가 `Spot`에 join된 뒤에만 유효하다. join 전 호출은 명확한
실패로 처리한다. actor membership 변경은 actor callback이 아니라
`IZLinkSpotActorMembership.JoinActorAsync(...)`와 `LeaveActorAsync(...)`에서
처리한다 (§4.4.1 참고).

`JoinSpot(spotName, request)`는 application domain spot 이름(`string`)을 받는다.
`gameId`, `matchId`, `roomId` 같은 domain key를 그대로 사용할 수 있고,
`spotName -> RoutingId` 변환은 framework 내부 spot route resolver가 푼다. actor
handler 표면에는 `RoutingId`를 노출하지 않는다.

`Send(...)`와 `Reply(...)`는 현재 actor에 연결된 stream client로 packet을 쓴다.
context는 `IZLinkStream`이나 `IZLinkActorStreamClient` 객체를 직접 노출하지 않는다.
stream이 없는 actor가 이 API를 호출하면 명확한 실패로 처리한다.

task 기반 request를 actor 또는 `Spot` callback 안에서 `await`하면 thread를
점유하지는 않지만, 현재 callback task는 응답 또는 timeout 전까지 끝나지 않는다.
따라서 같은 `Spot`의 다음 작업은 그 뒤에 실행된다. 명시 timeout이 없으면 framework
default timeout을 사용한다.

#### 4.4.2 session actor dispatch handler

session actor dispatch는 actor 객체 callback을 직접 호출하지 않고, actor 실행 문맥에
등록된 typed handler를 호출한다. handler는 raw routed envelope, stream sequence,
session router id를 직접 보지 않는다.
actor의 `JoinSpot(...)`이나 `GetSpot(...)`처럼 actor 실행 문맥 자체가 필요한 request는
actor-specific request handler를 사용한다.

```csharp
public interface IZLinkActorSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkActorSendContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkActorRequestHandler<in TRequest, TReply>
{
    ValueTask<TReply> HandleAsync(
        TRequest request,
        ZLinkActorRequestContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkActorRequestHandler<in TActor, in TRequest, TReply>
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}

public sealed class ZLinkActorSendContext : ZLinkHandlerContext
{
    public string ActorId { get; }
    public string RouterChannelId { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public IZLinkSessionProxy SessionProxy { get; }
}

public sealed class ZLinkActorRequestContext : ZLinkHandlerContext
{
    public string ActorId { get; }
    public string RouterChannelId { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public IZLinkSessionProxy SessionProxy { get; }
    public DateTimeOffset? Deadline { get; }
}

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

#### 4.4.3 dispatch mode

`SPOT`과 actor packet 처리에는 편의 모드와 고성능 모드를 함께 둘 필요가 있다.
어떤 응용은 constructor injection과 동적 resolve 편의가 더 중요하지만, 어떤 응용은
packet hot path에서 reflection과 per-packet resolve를 절대 허용하면 안 되기
때문이다.

이 초안의 최소 표면은 아래 정도가 자연스럽다.

```csharp
public enum ZLinkDispatchMode
{
    Compiled = 1,
    Dynamic = 2
}

public interface IZLinkDispatchOptions
{
    ZLinkDispatchMode SpotDispatchMode { get; set; }
    ZLinkDispatchMode StreamDispatchMode { get; set; }
}
```

의미는 아래처럼 읽는다.

- `Compiled`
  - registration 또는 runtime warm-up 단계까지만 reflection을 허용한다.
  - packet hot path는 cached delegate, prebuilt dispatch table, 미리 고른 factory만
    사용한다.
  - per-packet `IServiceProvider` resolve, `MethodInfo.Invoke(...)` 같은 경로는
    피한다.
- `Dynamic`
  - 유연한 등록과 늦은 바인딩을 더 우선한다.
  - 성능이 덜 중요한 관리용 handler나 초기 실험 단계에서는 허용할 수 있다.

즉 framework는 두 모드를 다 제공할 수 있지만, 기본 성능 원칙은 "`Compiled` 모드에서
hot path에 reflection이 남아 있으면 안 된다"는 쪽으로 읽는 편이 맞다.

### 4.5 message serializer

`playhouse/extensions`처럼 serializer 계층은 transport 인터페이스와 분리해서 두는
쪽이 자연스럽다. 즉 `STREAM` handler는 `Message`를 받고, protobuf/json 같은
객체 변환은 별도 serializer 또는 extension helper가 맡는다.

```csharp
public interface IZLinkMessageSerializer
{
    string Name { get; }

    bool CanSerialize(Type type);

    bool CanDeserialize(Type type);

    Message Serialize<T>(T value);

    T Deserialize<T>(Message message);

    bool TryDeserialize<T>(Message message, out T? value);
}
```

실사용 표면은 아래처럼 binding core `Message` 자체가 아니라, 별도 codec
extension/helper 계층으로 얹는 쪽을 기본으로 본다.

```csharp
public static class MessageExtensions
{
    public static T Parse<T>(this Message message);
}
```

이 초안에서는 아래 규칙을 기본으로 본다.

- `T`가 generated protobuf 타입이고 `IMessage<T>` 계열이면 protobuf로 해석한다.
- 그 밖의 일반 class면 json으로 해석한다.

즉 transport가 serializer를 직접 내장하는 구조보다, `Message` 위에 type 기준
parse helper를 얹는 구조가 `playhouse/extensions`와 더 가깝고 문서도 단순하게
읽힌다. 이때 `Parse<T>()`는 binding core의 필수 인스턴스 메서드가 아니라,
framework 또는 codec extension package가 제공하는 public helper로 보는 편이 맞다.

## 5. Client 인터페이스

### 5.1 IZLinkClient

서버 간 outbound 호출을 위한 공용 client다.
DI로 주입되며, ZLink handler와 기존 ASP.NET Core HTTP handler 양쪽에서
동일하게 사용할 수 있다.

호출 방식은 한 가지 축을 기본으로 둔다.

- `channelName` 기준 호출 -- Discovery가 대상을 선택한다

즉 일반 channel messaging에서는 특정 `ROUTER(server)`를 `rid`로 직접 지정해
호출하지 않는다. `rid`를 넣는 routed 호출은 SPOT spot-to-spot 경로에만 남긴다.

packet key는 매번 별도 문자열을 받기보다, 기본적으로 payload 타입의
`Type.Name`으로 해석하는 쪽을 기준으로 본다. 예를 들면
`GetProfileRequest`는 기본적으로 `GetProfileRequest` packet으로 매핑된다.

이 기본 규칙만으로 충분하지 않은 경우를 위해, public 표면은 `Request(...)`,
`Send(...)`가 builder를 돌려주고, `PacketName`, `Timeout` 같은 변형은 builder에
이어 붙이는 형태를 기준으로 본다. 이렇게 하면 `packetName`, `timeout` 조합마다
overload를 계속 늘리지 않아도 된다.

```csharp
public interface IZLinkSendCall
{
    IZLinkSendCall WithPacketName(string packetName);

    ValueTask Submit(
        CancellationToken cancellationToken = default);
}

public interface IZLinkRequestCall
{
    IZLinkRequestCall WithPacketName(string packetName);

    IZLinkRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Submit<TReply>(
        CancellationToken cancellationToken = default);
}

public interface IZLinkClientServerClient
{
    IZLinkSendCall Send<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall Request<TMessage>(
        string channelName,
        TMessage request);
}

public interface IZLinkClient : IZLinkClientServerClient
{
}
```

`IZLinkClient`는 `IZLinkClientServerClient`를 그대로 상속한다. legacy `IZLinkClient`를
주입받는 코드는 그대로 동작하고, 새로 작성하는 client-server outbound 코드는
`IZLinkClientServerClient`를 직접 받아도 된다.

runtime은 등록한 `channelName`마다 별도 outbound channel을 만든다.
각 channel은 capability마다 별도 outbound runtime을 가진다. 특히 수동 연결은
`channel` 전체가 아니라 `channel + capability` 기준으로 관리한다.

예를 들면 아래처럼 구분해야 한다.

- `profile.client` 수동 연결
- `profile.subscriber` 수동 연결

즉 `profile` channel 하나만으로는 "request client 연결인지, subscriber
연결인지"를 알 수 없고, framework도 capability별 runtime을 따로 관리해야 한다.

packet key 해석 규칙은 아래 순서를 기본으로 본다.

1. builder에서 `WithPacketName(...)`이 지정되면 그것을 사용한다.
2. 없으면 payload 타입에 선언된 packet metadata를 본다.
3. 그것도 없으면 `Type.Name`을 packet key로 사용한다.

즉 simple case에서는 타입 이름만으로 충분하고, 모호하거나 충돌하는 경우에만
explicit `PacketName`을 쓰게 한다.

timeout은 request/send에서 다르게 다룬다.

- `Request(...)`는 reply를 기다리므로 `WithTimeout(...)`을 둘 수 있다.
- `Send(...)`는 응답을 기다리지 않으므로 timeout 설정을 두지 않는다.
- `Publish(...)`도 응답을 기다리지 않으므로 timeout 설정을 두지 않는다.
- `Send(...).Submit(...)`는 handler 완료를 기다리는 호출이 아니다. framework가
  메시지를 transport에 맡길 수 있을 때까지 기다리는 비동기 submit이다.
- `Publish(...).Submit(...)`도 같은 의미다. subscriber의 handler 완료나 subscriber
  수신을 기다리지 않고, local publish transport에 submit될 때까지 기다린다.
- send backpressure 대기 한계는 builder가 아니라 channel 또는 socket의
  `SendTimeout` 옵션을 따른다.
- framework channel/socket option의 기본 `SendTimeout`은
  `TimeSpan.FromMilliseconds(200)`으로 둔다. async submit runtime은 core socket
  기본값을 직접 사용하지 않고, framework가 socket/channel option에 설정한
  resolved `SendTimeout` 값을 읽는다. 사용자가 `SendTimeout = null`을 명시한
  경우에만 core `-1`과 같은 무한 대기로 본다.
- `Request(...).Submit<TReply>(...)`도 request packet을 보내는 단계에서는
  `Send(...).Submit(...)`와 같은 nonblocking submit 경로를 사용한다.
- `Request(...).WithTimeout(...)`은 reply 대기 시간만 정한다.
- 이 초안은 별도 public no-wait 옵션을 제공하지 않는다. temporary backpressure는
  public `false` 반환값이 아니라 framework 내부 queue와 ready notification으로
  처리한다.

호출자가 `await`하면 호출 흐름은 submit 완료까지 멈춘다. 다만 구현은 thread를
막으면 안 된다. 즉 backpressure가 있는 동안에는 현재 thread나 thread pool worker를
점유하지 않고, socket ready callback 또는 poller wakeup이 오면 pending submit을
다시 진행해야 한다.

고성능 구현을 위해 아래 조건을 기본 계약으로 둔다.

- 즉시 전송 가능한 fast path는 completed `ValueTask`를 돌려주고 heap allocation을
  만들지 않는다.
- pending send queue는 무한 queue가 아니다. channel/socket의 high water mark와
  `SendTimeout`, cancellation, runtime stop으로 반드시 빠져나갈 수 있어야 한다.
- socket ready callback은 pending item을 하나만 처리하고 끝내지 않고, 정해진 batch
  budget 안에서 queue를 drain한다. 그래야 ready event 폭주와 context switch를 줄일
  수 있다.
- pending request 등록은 request packet submit 전에 끝나야 한다. submit 실패,
  timeout, cancellation, runtime stop이 발생하면 pending request를 즉시 제거한다.
- request reply timeout은 submit 완료 뒤부터 계산한다. submit 단계 지연은
  `SendTimeout`이 담당한다.
- payload encoding과 native `Message` 소유권은 submit 완료 또는 실패 시점에 한
  곳에서 정리한다. retry 중 같은 frame을 중복 전송하거나 중복 dispose하면 안 된다.
- stream connector public options에는 `SendTimeout`을 두지 않는다. connector
  send는 응답 없는 submit이고, connector request reply 대기에는 `RequestTimeout`만
  사용한다.

즉 public 호출 감각은 아래처럼 보는 편이 맞다.

```csharp
var reply = await client
    .Request("profile", new GetProfileRequest { AccountId = accountId })
    .WithTimeout(TimeSpan.FromMilliseconds(200))
    .Submit<GetProfileReply>(cancellationToken);

await client
    .Send("profile", new RefreshProfileCacheCommand { AccountId = accountId })
    .WithPacketName("profile.refresh-cache")
    .Submit(cancellationToken);
```

### 5.2 IZLinkSpotClient

현재 spot runtime 안의 outbound 호출을 다루는 client다. `IZLinkClient`와 독립된
인터페이스이며, 하부에서 서로 다른 C API를 감싼다. 현재 구현 범위는 아래 두 축이다.

- 현재 SPOT channel 안의 publish/subscribe
- attach된 channel client를 통한 다른 channel send/request

`SpotId`만 받아 다른 spot으로 routed send/request를 보내는 public 표면은 아직
현재 구현 계약에 포함하지 않는다. target node와 spot id를 함께 알아야 하는 raw
호출은 하부 바인딩에는 남아 있지만, framework application 표면의 기본 사용법으로
문서화하지 않는다.

```csharp
public interface IZLinkSpotClient
{
    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall RequestChannel<TMessage>(
        string channelName,
        TMessage request);

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message);
}
```

`IZLinkClient`와의 차이점은 아래와 같다.

- `Publish(topic, ...)`가 있다. SPOT 쪽은 현재 channel 안의 topic publish를
  함께 쓰는 경우가 많으므로 한 인터페이스에 같이 둔다.
- 다른 channel send/request는 attach된 channel client를 통해 푼다.
- 따라서 local `SpotNode`나 local spot runtime이 없는 앱의 기본 outbound 표면은
  `IZLinkClient`다. 그런 앱에서 외부 SPOT channel publish만 필요하면
  `IZLinkSpotPublisherClient`를 따로 쓴다.
- channel send/request는 일반 `IZLinkClient`와 같은 builder 감각을 따르는 편이
  자연스럽다.
- timer는 callback scheduler로 따로 두지 않고, spot lifecycle 안에서
  `Context.AddTimer<THandler>(name, period, ...)`로 등록하는 한 가지 모델로 설명하는 편이
  더 자연스럽다. 구현은 framework runtime이 만든 managed `.NET` timer를 같은
  spot execution context로 매핑하는 방향이 자연스럽다.

현재 `.NET` 바인딩의 raw `Spot` 표면은 이 두 종류를 더 직접적으로 나눈다.

- channel 이름 기준 호출:
  `Spot.SendChannel(...)`, `Spot.RequestChannel(...)`
- SPOT routed 호출:
  `Spot.SendToRouter(...)`, `Spot.RequestToRouterAsync(...)`,
  `Spot.ReplyToSpot(...)`

즉 framework 초안에서 말하는 "spot용 함수"와 "channelName으로 호출하는 함수"는
실제 바인딩에서도 분리되어 있다.

`IZLinkClient`와 `IZLinkSpotClient`는 상하 관계가 아니다. 두 인터페이스는 서로
다른 하부 C API를 감싸며, 각자 독립 구현을 가진다.

### 5.3 IZLinkSpotPublisherClient

`IZLinkSpotClient.Publish(...)`는 이미 실행 중인 local spot 문맥에서 현재 SPOT
channel로 publish할 때 쓴다. 반면 `IZLinkSpotPublisherClient`는 local spot
인스턴스가 없는 외부 노드가 특정 spot channel로 publish할 때 쓰는 별도 client다.

```csharp
public interface IZLinkSpotMeshPublisherClient
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}

public interface IZLinkSpotPublisherClient : IZLinkSpotMeshPublisherClient
{
}
```

`IZLinkSpotPublisherClient`는 `IZLinkSpotMeshPublisherClient`를 그대로 상속한다.
단일 SPOT publisher와 mesh publisher 양쪽이 같은 publish 호출 표면을 공유한다.

여기서 `channelName`은 target SPOT channel 이름이다. 즉 이 인터페이스는
`game.stage`, `game.chat`처럼 여러 SPOT channel이 있을 때 외부 노드가 어느
channel mesh로 publish할지 고르는 용도로 쓴다.

`IZLinkSpotClient.Publish(...)`와의 차이는 아래처럼 정리하면 된다.

- `IZLinkSpotClient.Publish(...)`
  - local spot 문맥 안에서 현재 SPOT channel로 publish
- `IZLinkSpotPublisherClient.Publish(...)`
  - local spot 인스턴스 없이 외부 노드에서 target SPOT channel로 publish

### 5.4 IZLinkEventPublisher

일반 `PUB/SUB` event를 publish하는 인터페이스다.
SPOT publish와 별도로, channel messaging 쪽에서 쓴다.

```csharp
public interface IZLinkPublishCall
{
    IZLinkPublishCall WithPacketName(string packetName);

    ValueTask Submit(
        CancellationToken cancellationToken = default);
}

public interface IZLinkFanoutPublisher
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}

public interface IZLinkEventPublisher : IZLinkFanoutPublisher
{
}
```

`IZLinkEventPublisher`는 `IZLinkFanoutPublisher`를 그대로 상속한다. capability별
새 명명을 따르는 `IZLinkFanoutPublisher`와 기존 별칭 `IZLinkEventPublisher` 모두
주입 표면으로 쓸 수 있다.

여기서 두 문자열의 역할은 다르다.

- `channelName`
  - 어느 논리 channel의 `PUB/SUB` mesh에 publish할지 정한다.
- `topic`
  - 그 channel 안에서 어떤 subscriber 집합이 이벤트를 받을지 정한다.

즉 `Publish("profile", "profile.cache-refreshed", evt)`는 `profile` channel 안의
`profile.cache-refreshed` topic으로 fan-out 한다는 뜻이다.

일반 `PUB/SUB` publish도 `Send(...)`와 비슷하게 timeout은 두지 않는다. 대신
필요하면 packet 이름 override만 둘 수 있다.

여기서 `Async(...)`는 remote peer 처리 완료를 기다리는 뜻이 아니다. framework
local runtime에 send/publish를 맡길 수 있을 때까지 기다리는 비동기 submit이다.
temporary backpressure는 framework 내부 queue와 ready notification으로 처리하고,
route-not-ready 같은 다른 submit 실패는 예외로 본다.

publish도 send와 같은 성능 규칙을 따른다. subscriber마다 별도 task를 만들거나
subscriber 수만큼 payload를 다시 직렬화하지 않는다. 가능한 경우 topic frame과
payload frame을 한 번 만들고, 하부 publish socket submit 경로가 backpressure를
처리하게 한다. `NoDrop` 같은 publish socket 정책이 켜져 있으면 drop 대신
`SendTimeout`까지 backpressure를 기다리고, timeout이 지나면 예외로 실패한다.

### 5.5 IZLinkActorClient

actor id만 알고 actor runtime으로 packet을 보내는 client다. route 결정은
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

    IZLinkActorClientSendCall WithMetadata(
        string key,
        string value);

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkActorClientRequestCall
{
    IZLinkActorClientRequestCall WithPacketName(string packetName);

    IZLinkActorClientRequestCall WithMetadata(
        string key,
        string value);

    IZLinkActorClientRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Submit<TReply>(CancellationToken cancellationToken = default);
}
```

### 5.5.1 IZLinkRouteClient

routed channel (`AddRouteChannel(...)`, `AddRouteMeshChannel(...)`)을 통해 특정 노드
`RoutingId`로 direct send/request를 보내는 client다. `IZLinkActorClient`와 달리
caller가 target node `RoutingId`를 직접 넘긴다. application public 표면에서는 보통
direct target send/request를 권장하지 않지만 (resolver 기반 표면을 권장), routed
channel 자체의 transport contract를 그대로 노출하는 client는 framework runtime이
internal/runtime service로 노출할 수 있다.

```csharp
public interface IZLinkRouteClient
{
    IZLinkRouteSendCall SendTo<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message);

    IZLinkRouteRequestCall RequestTo<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request);
}

public interface IZLinkRouteSendCall
{
    IZLinkRouteSendCall WithPacketName(string packetName);
    IZLinkRouteSendCall WithMetadata(string key, string value);
    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkRouteRequestCall
{
    IZLinkRouteRequestCall WithPacketName(string packetName);
    IZLinkRouteRequestCall WithMetadata(string key, string value);
    IZLinkRouteRequestCall WithTimeout(TimeSpan timeout);
    ValueTask<TReply> Submit<TReply>(CancellationToken cancellationToken = default);
}
```

기본 application 표면에서는 actor id 또는 spot id 기반 호출(`IZLinkActorClient`,
`IZLinkSpotClient`)을 권장한다. `IZLinkRouteClient`는 transport 위치값을 이미 알고
있는 framework internal helper나 별도 adapter 패키지에서 사용한다.

### 5.6 IZLinkSessionProxy

actor handler가 client session으로 push 또는 request를 보낼 때 쓰는 client다.
session server `RoutingId`, stream `SessionId`, binding token은 resolver와 runtime
metadata 안에 머물고 application handler는 actor id만 넘긴다.

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

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionProxyRequestCall
{
    IZLinkSessionProxyRequestCall WithPacketName(string packetName);

    IZLinkSessionProxyRequestCall WithMetadata(
        string key,
        string value);

    IZLinkSessionProxyRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Submit<TReply>(CancellationToken cancellationToken = default);
}
```

### 5.7 actor route resolver와 session location writer

resolver와 writer는 framework 등록 루트에 한 번씩 등록한다. `IZLinkActorClient`는
play route resolver를 사용하고, `IZLinkSessionProxy`는 session route resolver를 사용한다.
session에서 actor를 만들거나 remote actor 생성을 요청하면 session location writer가
현재 session binding을 application route store에 반영한다.

```csharp
public interface IZLinkActorPlayRouteResolver
{
    ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public interface IZLinkActorSessionRouteResolver
{
    ValueTask<ZLinkActorSessionRoute> ResolveSessionRouteAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public interface IZLinkActorSessionLocationWriter
{
    ValueTask BindSessionAsync(
        ZLinkActorSessionBinding binding,
        CancellationToken cancellationToken);

    ValueTask UnbindSessionAsync(
        ZLinkActorSessionUnbind binding,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkActorRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid);

public readonly record struct ZLinkActorSessionRoute(
    string RouterChannelId,
    RoutingId SessionRouterId,
    string SessionId,
    string BindingToken);

public readonly record struct ZLinkActorSessionBinding(
    string ActorId,
    string RouterChannelId,
    RoutingId SessionRouterId,
    string SessionId,
    string BindingToken);

public readonly record struct ZLinkActorSessionUnbind(
    string ActorId,
    string SessionId,
    string BindingToken);

```

## 6. 등록과 관리 인터페이스

### 6.1 framework 등록 루트

이 카탈로그에서는 `AddZLinkFramework(...)`의 builder 표면도 같이 고정한다.
그래야 샘플 문서에 나오는 `AddClientServerChannel(...)`, `AddFanoutChannel(...)`,
`AddSpotMesh(...)`, `UseDiscovery(...)`, `UseSpotDiscovery(...)`, `UseFilter(...)`의
소유자가 분명해진다.

channel discovery는 capability별 builder 아래에 다시 두지 않고,
framework 등록 루트에 한 번만 둔다. 이 discovery registration이 framework 안의
discovery 기반 channel capability들이 함께 쓰는 registry endpoint 집합을 뜻한다.
반대로 manual 연결은 capability별 runtime 설정이므로 각 capability builder 아래에
둔다.

```csharp
public interface IChannelClientConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface IChannelSubscriberConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface IZLinkDiscoveryBuilder
{
    void Add(string endpoint);
}

public interface IZLinkMetadataPolicyBuilder
{
    void ForwardApplicationKey(string key);
}

public interface IChannelServerCapabilityBuilder
{
    void Bind(string endpoint);

    void ConfigureSocket(
        Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(
        Action<IRoutePeerOptions> configure);
}

public interface IChannelClientCapabilityBuilder
{
    void ConfigureSocket(
        Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(
        Action<IOutboundPeerOptions> configure);

    void UseManualConnections(
        Action<IChannelClientConnections> configure);
}

public interface IChannelPublisherCapabilityBuilder
{
    void Bind(string endpoint);

    void ConfigureSocket(
        Action<IZLinkCommonSocketOptions> configure);
}

public interface IChannelSubscriberCapabilityBuilder
{
    void ConfigureSocket(
        Action<IZLinkCommonSocketOptions> configure);

    void UseManualConnections(
        Action<IChannelSubscriberConnections> configure);
}

public interface IRouteChannelConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface IZLinkRouteChannelBuilder
{
    void Bind(string endpoint);

    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(Action<IRoutePeerOptions> configure);

    void UseManualConnections(Action<IRouteChannelConnections> configure);

    void MapHandlerGroup(string groupName);

    void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>;

    void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>;
}

public interface IZLinkStreamNodeBuilder
{
    void Bind(string endpoint);

    void AddHeaderSession<TSession>()
        where TSession : class, IZLinkSession;
}

public interface IZLinkChannelBuilder
{
    void EnableServer(
        Action<IChannelServerCapabilityBuilder>? configure = null);

    void EnableClient(
        Action<IChannelClientCapabilityBuilder>? configure = null);

    void EnablePublisher(
        Action<IChannelPublisherCapabilityBuilder>? configure = null);

    void EnableSubscriber(
        Action<IChannelSubscriberCapabilityBuilder>? configure = null);

    // 이 채널에 노출할 handler group 이름을 매핑한다. 같은 채널에 여러 그룹을
    // 매핑할 수도 있고, 같은 그룹을 여러 채널에 매핑할 수도 있다.
    void MapHandlerGroup(string groupName);
}

public interface IZLinkClientServerChannelBuilder
{
    void EnableServer(
        Action<IChannelServerCapabilityBuilder>? configure = null);

    void EnableClient(
        Action<IChannelClientCapabilityBuilder>? configure = null);

    void MapHandlerGroup(string groupName);
}

public interface IZLinkFanoutChannelBuilder
{
    void EnablePublisher(
        Action<IChannelPublisherCapabilityBuilder>? configure = null);

    void EnableSubscriber(
        Action<IChannelSubscriberCapabilityBuilder>? configure = null);

    void MapHandlerGroup(string groupName);
}

public interface IZLinkDealerMeshChannelBuilder
{
    void EnableClient(
        Action<IDealerMeshChannelClientCapabilityBuilder>? configure = null);
}

public interface IDealerMeshChannelClientCapabilityBuilder
    : IChannelClientCapabilityBuilder
{
    void Bind(string endpoint);
}

public interface IZLinkRouteMeshChannelBuilder
{
    void Bind(string endpoint);

    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(Action<IRoutePeerOptions> configure);

    void UseManualConnections(Action<IRouteChannelConnections> configure);
}

public interface IZLinkFrameworkOptions
{
    TimeSpan DefaultTimeout { get; set; }

    IZLinkCodecRegistryBuilder Codecs { get; }

    void ConfigureMetadata(Action<IZLinkMetadataPolicyBuilder> configure);

    void AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory;

    void AddActorPlayRouteResolver<TResolver>()
        where TResolver : class, IZLinkActorPlayRouteResolver;

    void AddActorSessionRouteResolver<TResolver>()
        where TResolver : class, IZLinkActorSessionRouteResolver;

    void AddActorSessionLocationWriter<TWriter>()
        where TWriter : class, IZLinkActorSessionLocationWriter;

    // legacy/general 채널 등록. capability별 분기형(아래)이 권장된다.
    void AddChannel(
        string channelName,
        Action<IZLinkChannelBuilder> configure);

    void AddClientServerChannel(
        string channelName,
        Action<IZLinkClientServerChannelBuilder> configure);

    void AddFanoutChannel(
        string channelName,
        Action<IZLinkFanoutChannelBuilder> configure);

    void AddDealerMeshChannel(
        string channelName,
        Action<IZLinkDealerMeshChannelBuilder> configure);

    void AddRouteChannel(
        string routerChannelId,
        Action<IZLinkRouteChannelBuilder> configure);

    void AddRouteMeshChannel(
        string channelName,
        Action<IZLinkRouteMeshChannelBuilder> configure);

    void UseDiscovery(
        Action<IZLinkDiscoveryBuilder> configure);

    void UseSpotDiscovery(
        string channelName,
        Action<IZLinkDiscoveryBuilder> configure);

    void UseFilter<TFilter>()
        where TFilter : class, IZLinkHandlerFilter;

    void ConfigureDispatch(
        Action<IZLinkDispatchOptions> configure);

    void AddStreamNode(
        string streamNodeName,
        Action<IZLinkStreamNodeBuilder> configure);

    void AddSpotNode(
        string spotNodeName,
        Action<IZLinkSpotNodeBuilder> configure);

    void AddSpotMesh(
        string channelName,
        Action<IZLinkSpotMeshBuilder> configure);
}
```

`DefaultTimeout`의 기본값은 30초다.

각 함수의 의미는 아래와 같다.

- `DefaultTimeout`
  - request 호출이 별도 timeout을 지정하지 않았을 때 쓰는 framework 기본값이다.
- `Codecs`
  - protobuf/json/messagepack 같은 codec provider를 framework registry에 등록하는
    진입점이다.
- `ConfigureMetadata(...)`
  - session actor dispatch와 session proxy 경로에서 전달할 application metadata key를
    등록한다.
- `AddActorFactory(...)`
  - actor type 문자열에 대응하는 actor factory를 등록한다.
- `AddActorPlayRouteResolver(...)`
  - `IZLinkActorClient`가 actor id로 play/runtime route를 찾을 때 사용할 resolver를
    등록한다.
- `AddActorSessionRouteResolver(...)`
  - `IZLinkSessionProxy`가 actor id로 현재 client session route를 찾을 때 사용할 resolver를
    등록한다.
- `AddActorSessionLocationWriter(...)`
  - session actor create/bind와 disconnect/unbind를 application route store에 반영할
    writer를 등록한다.
- `AddChannel(...)`
  - legacy/general 진입점. logical channel 하나에 server/client/publisher/subscriber
    capability를 한 번에 enable할 수 있다. 새 코드는 아래 capability별 분기형을 권장한다.
- `AddClientServerChannel(...)`
  - request/send 용 client-server 채널을 등록한다. builder는 `EnableServer(...)`와
    `EnableClient(...)`만 노출한다.
- `AddFanoutChannel(...)`
  - pub/sub fanout 채널을 등록한다. builder는 `EnablePublisher(...)`와
    `EnableSubscriber(...)`만 노출한다.
- `AddDealerMeshChannel(...)`
  - DEALER mesh 채널을 등록한다. mesh client는 자신을 식별할 local bind endpoint를
    가져야 하므로 `IDealerMeshChannelClientCapabilityBuilder`에 `Bind(...)`가
    추가로 노출된다.
- `AddRouteChannel(...)`
  - session actor dispatch와 직접 routed handler가 사용할 routed channel mesh를 등록한다.
- `AddRouteMeshChannel(...)`
  - route mesh 채널을 등록한다. bind endpoint, socket option, routing option,
    manual connection을 한 builder 안에서 함께 설정한다.
- `UseDiscovery(...)`
  - 일반 channel capability들이 공유할 registry endpoint 집합을 등록한다.
  - `client.UseDiscovery(...)`처럼 capability 아래에 다시 두지 않는다.
- `UseSpotDiscovery(...)`
  - 앱 단위 active SPOT channel view와 registry endpoint 집합을 등록한다.
  - 같은 앱의 `SpotNode`들은 이 등록이 정한 channel view를 공유한다.
- `UseFilter<TFilter>()`
  - handler filter 타입을 framework pipeline에 등록한다.
- `AddSpotNode(...)`
  - 명명된 `SpotNode`를 등록한다. `EnableRouter()`, `EnablePubSub()`,
    `AttachChannelClient(...)`, `AttachClientServerChannelClient(...)`,
    `AttachSpotPublisherClient(...)`, `AttachSpotMeshPublisherClient(...)`,
    `AddSpotFactory<TSpot>(...)` 같은 capability 구성은 builder 람다 안에서 선언한다.
- `AddSpotMesh(...)`
  - 여러 `SpotNode`가 같은 SPOT mesh discovery view를 공유하도록 묶어 등록한다.
    mesh builder는 자체 `UseDiscovery(...)`와 `AddNode(spotNodeName, ...)`을 노출한다.
    mesh node builder는 `EnableRouter`, `EnablePubSub`,
    `AttachClientServerChannelClient`, `AttachSpotMeshPublisherClient`,
    `AddSpotFactory<TSpot>(...)`를 노출한다.
- `EnableServer(...)`
  - local request/send handler를 받을 `ROUTER(server)` capability를 연다.
  - 이 capability는 local bind endpoint가 없으면 다른 프로세스에서 접근할 수
    없으므로, builder 안에서 `Bind(...)`를 같이 지정해야 한다.
- `EnableClient(...)`
  - request/send outbound 호출용 `DEALER(client)` capability를 연다.
- `EnablePublisher(...)`
  - 일반 channel event publish capability를 연다.
  - 이 capability도 remote subscriber가 붙을 local bind endpoint가 필요하므로
    builder 안에서 `Bind(...)`를 같이 지정해야 한다.
- `EnableSubscriber(...)`
  - 일반 channel event subscribe capability를 연다.
- `AddStreamNode(...)`
  - framework Header 기반 packet session을 받을 STREAM node를 등록한다.
  - 한 node에는 stream session을 하나만 등록할 수 있다.
  - 같은 node에 session을 둘 이상 함께 등록하는 것은 허용하지 않는다.

중요한 규칙은 아래와 같다.

- 수동 연결은 `channel` 전체가 아니라 `channel + capability` 기준이다.
- manual `Connect(...)`는 startup과 런타임 제어 모두 endpoint만 받는다.
- 같은 capability 안에서는 `Discovery`와 manual 연결을 섞지 않는다.
- `client`와 `subscriber`는 서로 다른 연결 집합이다.
- publisher는 outbound fan-out submit capability로 보고, 이 초안에서는 별도
  manual peer 관리 표면을 두지 않는다.

### 6.2 channel 연결 관리

위 규칙에 따라 manual capability를 런타임에서 제어하려면 아래와 같은 별도
management 표면이 필요하다. startup builder에서 쓰는 `UseManualConnections(...)`
는 단순 등록이므로 동기 `Connect(...)`를 유지한다. 반대로 host가 올라간 뒤
실제 runtime 상태를 바꾸는 관리 표면은 lazy startup과 I/O 경계를 숨기지 않기
위해 비동기로 둔다.

```csharp
public interface IZLinkEndpointConnections
{
    ValueTask<bool> ConnectAsync(
        string endpoint,
        CancellationToken cancellationToken = default);

    ValueTask DisconnectAsync(
        string endpoint,
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<string>> ListConnectionsAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkChannelConnectionManager
{
    // capability별 분기형 (권장)
    ValueTask<IZLinkEndpointConnections> GetClientServerClientAsync(
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetFanoutSubscriberAsync(
        string channelName,
        CancellationToken cancellationToken = default);

    // legacy/general 이름. 위와 같은 capability를 가리킨다.
    ValueTask<IZLinkEndpointConnections> GetClientAsync(
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetSubscriberAsync(
        string channelName,
        CancellationToken cancellationToken = default);
}
```

`GetClientServerClientAsync(...)`와 `GetClientAsync(...)`는 같은 client-server client
capability를 가리키고, `GetFanoutSubscriberAsync(...)`와 `GetSubscriberAsync(...)`는
같은 fanout subscriber capability를 가리킨다. 새 코드는 capability를 분명히 드러내는
`GetClientServerClientAsync(...)` / `GetFanoutSubscriberAsync(...)` 쪽을 권장한다.

이 인터페이스는 아무 channel에나 항상 열리는 것이 아니라, 해당 capability가
manual 모드일 때만 유효한 표면으로 보는 편이 맞다. discovery 모드인 capability는
peer 집합을 discovery가 소유하므로 수동 `Connect`, `Disconnect`를 허용하지
않는다.

### 6.3 Spot 관리와 등록 인터페이스

`IZLinkSpotManager`는 `SpotNode` 안에서 spot 인스턴스를 생성하고 삭제하는
인터페이스다. handler가 spot을 만드는 것이 아니라, manager가 만들고 handler는
들어오는 메시지를 처리할 뿐이다.

```csharp
public readonly record struct ZLinkSpotCreateResult(
    RoutingId SpotRid,
    string SpotName,
    bool Created);

public readonly record struct ZLinkSpotInfo(
    RoutingId SpotRid,
    string SpotName);

public interface IZLinkSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        CancellationToken cancellationToken = default);

    ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}
```

두 가지 `CreateAsync` 오버로드는 각각 아래 상황을 설명한다.

- `spotName`만 받는 생성
  - 등록된 이름으로 factory를 고르고 runtime이 새 `spotRid`를 발급
- `spotName + RoutingId spotRid`
  - 등록된 이름으로 factory를 고르고 호출자가 특정 `spotRid`를 지정

반환값은 `spotRid`, `spotName`, 새로 만들었는지 여부다. 장기적으로 들고 다닐
instance handle이 아니라, 생성 결과만 돌려준다.

`GetAsync(...)`와 `ListAsync(...)`는 runtime이 들고 있는 `spotRid -> spotName`
매핑을 운영 코드에서 다시 볼 수 있게 하는 조회 표면이다. 여러 spot factory를 같은
`SpotNode`에 등록할 수 있다면, 어떤 `spotRid`가 어떤 이름으로 생성됐는지
바깥에서 확인할 수 있어야 하므로 이 조회가 같이 필요하다.

현재 SPOT topology 초안에서는 high-level public surface에서 `spotRid ->
targetRid` 주소 해석을 framework 기본 기능으로 두지 않는다. 직접 addressing이
필요하면 low-level socket API나 상위 wrapper가 맡고, framework의 기본 SPOT
표면은 channel publish와 channel send/request를 먼저 설명하는 편이 더 자연스럽다.

`SPOT` registration 자체는 별도 builder를 통해 설명하는 편이 맞다. 현재 초안의
최소 표면은 아래 정도가 자연스럽다.

```csharp
public interface ISpotRouterConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface ISpotPubSubConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface ISpotPublisherConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface IZLinkCommonSocketOptions
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
}

public interface IRoutePeerOptions
{
    RoutingId RoutingId { get; set; }

    bool Mandatory { get; set; }

    bool Handover { get; set; }

    bool Probe { get; set; }

    RoutingId ConnectRoutingId { get; set; }
}

public interface IOutboundPeerOptions
{
    RoutingId RoutingId { get; set; }

    bool ProbeRouter { get; set; }
}

public interface ISpotNodePublisherOptions
{
    int SendHighWaterMark { get; set; }

    TimeSpan? SendTimeout { get; set; }

    TimeSpan? Linger { get; set; }

    bool NoDrop { get; set; }
}

public interface ISpotNodeSubscriberOptions
{
    int ReceiveHighWaterMark { get; set; }

    TimeSpan? ReceiveTimeout { get; set; }

    TimeSpan? Linger { get; set; }
}

public interface ISpotRouterCapabilityBuilder
{
    void ConfigureSocket(
        Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(
        Action<IRoutePeerOptions> configure);

    void UseManualConnections(
        Action<ISpotRouterConnections> configure);
}

public interface ISpotPubSubCapabilityBuilder
{
    void ConfigurePublisherOptions(
        Action<ISpotNodePublisherOptions> configure);

    void ConfigureSubscriberOptions(
        Action<ISpotNodeSubscriberOptions> configure);

    void UseManualConnections(
        Action<ISpotPubSubConnections> configure);
}

public interface ISpotPublisherClientCapabilityBuilder
{
    void ConfigureSocket(
        Action<IZLinkCommonSocketOptions> configure);

    void UseManualConnections(
        Action<ISpotPublisherConnections> configure);
}

public interface ISpotChannelClientCapabilityBuilder
{
    void ConfigureSocket(
        Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(
        Action<IOutboundPeerOptions> configure);

    void UseManualConnections(
        Action<IChannelClientConnections> configure);
}

public interface IZLinkSpotNodeBuilder
{
    void Bind(string endpoint);

    void EnableRouter(
        Action<ISpotRouterCapabilityBuilder>? configure = null);

    void EnablePubSub(
        Action<ISpotPubSubCapabilityBuilder>? configure = null);

    // legacy/general channel attach
    void AttachChannelClient(
        string channelName,
        Action<ISpotChannelClientCapabilityBuilder>? configure = null);

    // 새 capability별 attach (권장)
    void AttachClientServerChannelClient(
        string channelName,
        Action<ISpotChannelClientCapabilityBuilder>? configure = null);

    // legacy/general spot publisher attach
    void AttachSpotPublisherClient(
        string channelName,
        Action<ISpotPublisherClientCapabilityBuilder>? configure = null);

    // 새 capability별 spot mesh publisher attach (권장)
    void AttachSpotMeshPublisherClient(
        string channelName,
        Action<ISpotPublisherClientCapabilityBuilder>? configure = null);

    void AddSpotFactory<TSpot>(string spotName)
        where TSpot : IZLinkSpot;
}

public interface IZLinkSpotMeshBuilder
{
    void UseDiscovery(Action<IZLinkDiscoveryBuilder> configure);

    void AddNode(
        string spotNodeName,
        Action<IZLinkSpotMeshNodeBuilder> configure);
}

public interface IZLinkSpotMeshNodeBuilder
{
    void Bind(string endpoint);

    void EnableRouter(
        Action<ISpotRouterCapabilityBuilder>? configure = null);

    void EnablePubSub(
        Action<ISpotPubSubCapabilityBuilder>? configure = null);

    void AttachClientServerChannelClient(
        string channelName,
        Action<ISpotChannelClientCapabilityBuilder>? configure = null);

    void AttachSpotMeshPublisherClient(
        string channelName,
        Action<ISpotPublisherClientCapabilityBuilder>? configure = null);

    void AddSpotFactory<TSpot>(string spotName)
        where TSpot : IZLinkSpot;
}
```

각 함수의 의미는 아래와 같다.

- `EnableRouter(...)`
  - spot-to-spot routed packet을 처리할 local router capability를 켠다.
- `EnablePubSub(...)`
  - 현재 SPOT channel 안의 publish/subscribe capability를 켠다.
- `AttachChannelClient(...)`
  - 다른 channel로 send/request 할 outbound `DEALER(client)` 경로를 붙인다.
- `AttachSpotPublisherClient(...)`
  - local spot 인스턴스가 없는 외부 노드가 특정 SPOT channel로 publish할
    outbound publisher client를 붙인다.
- `AddSpotFactory<TSpot>(spotName)`
  - 이 node가 생성하고 소유할 spot factory를 이름과 함께 등록한다.
  - 같은 `SpotNode` 안에서는 `spotName`이 비어 있으면 안 된다.
  - 이미 등록된 `spotName`을 다시 등록하면 기존 값을 덮어쓰지 않고 예외를 던진다.
  - `CreateAsync(spotName, ...)`는 이 이름과 정확히 일치하는 factory를 고른다.

여기서 수동 연결은 channel 쪽과 마찬가지로 capability별로 다뤄야 한다.
예를 들어 `router`, channel client, publish 쪽은 모두 각 capability가 쓸
endpoint 집합을 따로 관리하면 된다. 이 초안에서는 manual `Connect(...)`
시점에 remote router id를 별도 파라미터로 받지 않는다. 따라서
`UseManualConnections(...)`도 한 군데에 모아 두지 않고 capability builder별로
따로 두는 편이 맞다.

소켓 옵션도 같은 식으로 소유자를 나눠서 설명하는 편이 맞다.

- `ConfigureSocket(...)`
  - 실제 `.NET` 바인딩의 `CommonSocketOptions`와 같은 공통 socket facade를
    capability 아래에 노출하는 모델이다.
- `ConfigurePublisherOptions(...)`, `ConfigureSubscriberOptions(...)`
  - 실제 `SpotNode.PublisherOptions`, `SpotNode.SubscriberOptions`와 같은
    `SPOT` pub/sub 전용 facade를 framework 등록 쪽으로 끌어올린다.
- `ConfigureRouting(...)`
  - capability가 routed peer와 맺는 연결 규칙을 따로 설정한다.
  - 현재 backend가 `bindings/dotnet`일 때는 server/router 쪽과 outbound client 쪽이
    각자 다른 low-level option object에 대응한다.
- `WithTimeout(...)`
  - request 한 번에만 적용되는 호출 단위 옵션이다.
  - 실제 바인딩에서도 `DealerSocket.RequestAsync(..., TimeSpan timeout, ...)`,
    `RouterSocket.RequestAsync(..., TimeSpan timeout, ...)`,
    `Spot.RequestChannelAsync(..., TimeSpan timeout, ...)`처럼 호출 인자로 받는다.
  - 위 등록 설정과 달리 capability runtime 기본값을 바꾸지 않는다.

또한 `UseSpotDiscovery(...)`에서 앱 단위 SPOT channel 이름을 이미 등록하므로,
`AddSpotNode(...)` 안에서 같은 channel 이름을 다시 받는 함수는 두지 않는다.
현재 초안에서는 한 애플리케이션이 active SPOT channel view 하나를 공유하고,
여러 `SpotNode`가 필요하면 그 view 위에 node를 여러 개 올리는 모델을 기본으로 본다.

즉 `SPOT` 등록 시점에도

- local routed router capability 활성화
- local SPOT pub/sub capability 활성화
- 외부 channel 호출용 client attach
- 외부 SPOT publish client attach

두 축을 같이 드러내는 편이 맞다.

### 6.4 Spot 연결 관리

SPOT도 수동 연결을 쓸 때는 capability별 런타임 제어 표면이 필요하다.

```csharp
public interface IZLinkSpotConnectionManager
{
    ValueTask<IZLinkEndpointConnections> GetRouterAsync(
        string spotNodeName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetPubSubAsync(
        string spotNodeName,
        CancellationToken cancellationToken = default);

    // 새 capability별 분기형 (권장)
    ValueTask<IZLinkEndpointConnections> GetClientServerChannelClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetSpotMeshPublisherClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default);

    // legacy/general 이름. 위와 같은 capability를 가리킨다.
    ValueTask<IZLinkEndpointConnections> GetChannelClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetSpotPublisherClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default);
}
```

`GetClientServerChannelClientAsync(...)`와 `GetChannelClientAsync(...)`는 같은 channel
client capability를 가리키고, `GetSpotMeshPublisherClientAsync(...)`와
`GetSpotPublisherClientAsync(...)`는 같은 spot publisher capability를 가리킨다.
새 코드는 capability를 분명히 드러내는 mesh/client-server 형태를 권장한다.

이 관리 인터페이스도 아무 node에나 항상 열리는 것이 아니라, 해당 capability가
manual 모드일 때만 유효한 표면으로 보는 편이 맞다.

## 7. Timer 인터페이스

현재 초안에서는 spot lifecycle 안에 등록한 `Context.AddTimer<THandler>(...)`가 반환하는
timer handle이다.

```csharp
public interface IZLinkTimer : IAsyncDisposable
{
    bool IsDisposed { get; }

    ValueTask CancelAsync(
        CancellationToken cancellationToken = default);
}
```

framework timer abstraction은 low-level `.NET` binding의 native timer를 그대로
노출하지 않는다. framework runtime이 managed timer를 만들고, 각 tick을
`ExecuteSerializedAsync(...)` 같은 spot 직렬 실행 경로로 넘겨
`IZLinkSpotTimerHandler<TSpot>.HandleAsync(...)`를 호출한다. 따라서
`IZLinkTimer.CancelAsync()`는 native `Timer.Stop()` wrapper가 아니라, framework가
만든 managed timer loop를 중단하는 표면으로 읽는 편이 맞다.

timer가 어떤 실행 문맥에서 callback을 호출하는지가 중요하다.

- 현재 방향에서는 timer를 별도 client scheduler로 두지 않는다.
- spot timer는 framework가 만든 managed timer를 사용하되, 실제 handler 호출은
  같은 spot 실행 문맥 안에서 직렬화한다.
- packet, subscribe, channel reply callback, timer callback은 모두 같은 spot
  execution context 규칙을 따른다.

## 8. Handler Filter

HTTP middleware와 별도로, ZLink handler 전후 공통 처리를 위한 filter다.

```csharp
// filter pipeline의 next 단계를 나타내는 delegate.
// 호출하면 다음 filter 또는 실제 handler가 실행되고 결과가 반환된다.
public delegate ValueTask<object?> ZLinkHandlerDelegate(
    CancellationToken cancellationToken);

// filter에 전달되는 호출 context.
// 역직렬화된 message와 handler context를 함께 들고 다닌다.
public sealed class ZLinkHandlerInvocation
{
    public string? PacketName { get; init; }
    public object? Message { get; init; }
    public IZLinkHandlerContext Context { get; init; } = null!;
}

public interface IZLinkHandlerFilter
{
    ValueTask<object?> InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerDelegate next,
        CancellationToken cancellationToken);
}
```

등록은 아래처럼 한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.UseFilter<LoggingZLinkFilter>();
    options.UseFilter<ValidationZLinkFilter>();
});
```

filter도 framework가 직접 `new` 하지 않고, `.NET DI`에서 resolve한다.

겨냥하는 용도:

- logging
- validation
- authorization
- metrics
- exception → framework 표준 오류 응답 매핑

기존 `ASP.NET Core` HTTP middleware (`app.Use(...)`)는 HTTP pipeline 전용이므로
ZLink handler에 자동으로 적용되지 않는다. 공통 처리가 필요하면 이
`IZLinkHandlerFilter`를 쓴다.

## 9. Request reply 타입 지정

request 메시지 타입에는 framework 전용 marker interface를 붙이지 않는다.
메시지는 codec이 직렬화할 payload 계약만 표현해야 하며, reply 타입은 호출부에서
`Async<TReply>(...)`로 지정한다.

```csharp
var reply = await client
    .Request("profile", new GetProfileRequest { AccountId = accountId })
    .Submit<GetProfileReply>(cancellationToken);
```

handler는 메서드 시그니처만으로 request/reply 타입을 읽는다. client 호출부는
request 메시지를 보낼 때 packet 이름과 payload 타입만 제공하고, 기다릴 reply
타입은 `Async<TReply>(...)`에서 명시한다.

기본 packet key는 `Type.Name`을 쓴다. 예: `GetProfileRequest`.
이 기본 이름이 맞지 않을 때는 payload 타입에 explicit metadata를 둘 수 있다.

```csharp
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct,
    AllowMultiple = false)]
public sealed class ZLinkPacketAttribute : Attribute
{
    public ZLinkPacketAttribute(string packetName);
    public string PacketName { get; }
}
```

이 metadata는 outbound 기본 해석과 inbound handler 기본 매핑 양쪽에서 함께 쓴다.

## 10. Registry 조회 인터페이스

Registry 조회 인터페이스는 infrastructure 성격이므로 상세 정의는
[aspnet-core-registry.ko.md](./aspnet-core-registry.ko.md)의 section 7에 있다.
여기서는 역할만 요약한다.

### 10.1 IZLinkRegistryQuery

같은 프로세스의 embedded Registry를 조회한다.
`AddZLinkRegistry(...)` 시 자동으로 DI에 등록된다.
status, service summary, topology, member peers를 제공한다. registry가 아직
시작 전일 수 있고, snapshot 수집도 host lifecycle과 맞물리므로 조회 API는
비동기로 둔다.

```csharp
public interface IZLinkRegistryQuery
{
    ValueTask<ZLinkRegistryStatus> StatusSnapshotAsync(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRegistryServiceSummaryEntry[]> ServiceSummarySnapshotAsync(
        ZLinkRegistryServiceSummaryFilter? filter = null,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRegistryTopologyEntry[]> TopologySnapshotAsync(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRegistryTopologyEntry[]> TopologyQueryAsync(
        ZLinkRegistryTopologyFilter? filter = null,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkMemberPeerEntry[]> MemberPeersAsync(
        string channelName,
        CancellationToken cancellationToken = default);
}
```

`MemberPeersAsync(...)`는 `channelName` 하나만 받는다. 이전에 존재했던
`(ZLinkServiceType serviceType, string serviceName, ...)` 형태는 더 이상 없다.
service type/name 구분 대신, channel 이름 자체가 member peer 집합의 단위가 된다.

### 10.2 IZLinkRegistryQueryClient

다른 프로세스의 Registry를 원격 조회한다.
`AddZLinkRegistryQueryClient(...)` 로 별도 등록한다.
topology snapshot만 제공한다. 원격 요청이므로 이 인터페이스도 비동기로 둔다.

### 10.3 runtime monitoring

runtime monitoring은 socket 하부 monitor와, registry/spot의
snapshot diff를 함께 감싸는 운영 표면이다. 공용 handler shape는 아래처럼 두는
편이 자연스럽다.

```csharp
public interface IZLinkMonitoringOptions
{
    void AddSocketEvents(
        string sourceName,
        params ZLinkSocketEventKind[] events);

    void AddRegistryEvents(
        string sourceName,
        TimeSpan interval);

    void AddSpotEvents(
        string sourceName,
        TimeSpan interval);
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
```

event kind는 enum으로 두고, 실제 callback payload는 record struct로 두는 편이
맞다. enum만으로는 source name, routing id, endpoint, snapshot 같은 운영 정보를
같이 전달하기 어렵기 때문이다. 또한 native monitor enum과 raw status 값은
framework가 항상 보장하는 필수 계약이 아니라, backend가 제공할 수 있을 때만
채워지는 optional diagnostic detail로 두는 편이 backend 교체 정책과도 맞다.

`AddSocketEvents(...)`에서 event 목록을 비워 두면, 해당 source가 올릴 수 있는
모든 logical event kind를 구독하는 뜻으로 읽는다.

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

public enum ZLinkRegistryEventKind
{
    StatusChanged = 0,
    TopologyChanged,
    ServiceSummaryChanged
}

public readonly record struct ZLinkRegistryEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkRegistryEventKind Event,
    ZLinkRegistryStatus? Status,
    IReadOnlyList<ZLinkRegistryTopologyEntry>? Topology,
    IReadOnlyList<ZLinkRegistryServiceSummaryEntry>? ServiceSummary)
    : IZLinkRuntimeEvent;

public enum ZLinkSpotEventKind
{
    StatusChanged = 0,
    PeersChanged,
    SubjectsChanged
}

public readonly record struct ZLinkSpotEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkSpotEventKind Event,
    ZLinkSpotNodeStatus? Status,
    IReadOnlyList<ZLinkSpotNodePeerEntry>? Peers,
    IReadOnlyList<ZLinkSpotNodeSubjectEntry>? Subjects)
    : IZLinkRuntimeEvent;
```

`ZLinkSpotNodeStatus`와 `ZLinkSpotNodePeerEntry`의 첫 번째 필드는 `ChannelName`이다.
이전에는 `ServiceName`으로 불렸으나, 현재는 channel 단위로 통일하면서 `ChannelName`으로
rename되었다. 이 두 record를 필드 단위로 풀어 쓰는 다른 문서는 이 이름을 기준으로
참고한다.

이 초안에서 source별 의미는 아래처럼 정리한다.

- socket event
  - 하부 `SocketMonitor`를 감싼다.
  - source 이름은 `channel + capability` 또는 `spot node + capability`가 자연스럽다.
  - 예: `profile.server`, `profile.client`, `stage-node.router`
- discovery state
  - runtime event로 올리지 않는다.
  - 현재 provider 상태는 registry topology/service/member snapshot으로 조회한다.
- registry event
  - 하부 raw monitor가 아니라 `StatusSnapshotAsync()`, `TopologySnapshotAsync()`,
    `ServiceSummarySnapshotAsync()`의 polling + diff로 만든다.
- spot event
  - 하부 raw monitor가 아니라 `StatusSnapshot()`, `PeersSnapshot()`,
    `SubjectsSnapshot()`의 polling + diff로 만든다.

## 11. Attribute 정의

### 11.1 서버 간 messaging

```csharp
// 클래스 attribute. handler 클래스가 어느 논리 그룹에 속하는지 표시한다.
// 이 그룹을 어느 채널에 노출할지는 channel builder의 MapHandlerGroup(...)이 정한다.
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

`ZLinkHandlerGroupAttribute`는 handler 클래스가 어느 논리 그룹에 속하는지 표시한다.
그룹 이름은 사용자가 정하는 문자열이고, 실제 채널 이름과는 분리된다. 같은 그룹을
여러 채널에 매핑할 수 있고, 같은 채널에 여러 그룹을 매핑할 수 있다.

`ZLinkRequestAttribute`와 `ZLinkSendAttribute`는 channel 이름을 받지 않는다.
이 attribute는 handler method가 어떤 packet kind를 처리하는지와 packet name
override만 표현한다. handler를 어떤 inbound channel에 노출할지는 channel
registration의 `MapHandlerGroup(...)` mapping이 정한다.

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

options.AddClientServerChannel("api", channel =>
{
    channel.EnableServer(server => server.Bind("tcp://0.0.0.0:7101"));
    channel.MapHandlerGroup("api");
});
```

같은 handler 그룹을 여러 channel에 매핑하는 것은 허용한다. 하지만 같은 channel
안에서 같은 `kind + packet name`이 둘 이상으로 해석되면 (같은 그룹에 충돌이 있거나,
다른 그룹의 충돌이 한 채널에 같이 붙거나) startup validation 오류다.

attribute scan 보조 표면으로 `MapHandlersFromAssemblyContaining<TMarker>()` /
`MapHandlersFromAssembly(...)`를 둘 수도 있다 (예: 빠른 prototype, group attribute
없이 한 번에 매핑하는 경우). 이 보조 표면은 framework가 제공할 수 있지만, 정식
sample, scope, regression matrix는 group mapping 모델을 기본으로 본다.

### 11.2 publish

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkPublishAttribute : Attribute
{
    public ZLinkPublishAttribute();
    public string? PacketName { get; init; }
}
```

이 초안에서는 pub/sub attribute 이름을 `ZLinkPublishAttribute`로 고정한다.
이름을 `Event`가 아니라 `Publish`로 둔 이유는 producer 쪽 동사
(`IZLinkEventPublisher.Publish(...)`)와 일치시켜 `[ZLinkRequest]` / `[ZLinkSend]` /
`[ZLinkPublish]` 세 표면이 같은 패턴으로 읽히도록 하기 위함이다.

publish handler도 전역으로 모든 subscriber channel에 자동 노출하지 않는다.
subscriber capability를 가진 채널에서도 `MapHandlerGroup(...)`으로 노출할 그룹을
명시한다.

```csharp
[ZLinkHandlerGroup("api.events")]
public sealed class CacheInvalidatedHandler
    : IZLinkPublishHandler<CacheInvalidatedEvent>
{
    // ...
}

options.AddFanoutChannel("api.events", channel =>
{
    channel.EnableSubscriber();
    channel.MapHandlerGroup("api.events");
});
```

### 11.3 SPOT

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

### 11.4 stream

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkStreamPacketAttribute : Attribute
{
    public ZLinkStreamPacketAttribute();
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkStreamRawAttribute : Attribute
{
}
```

stream은 framework Header 기반 packet session 한 축으로 본다.
recv 방식은 현재 초안 범위에서 제외하고, session lifecycle은 `OnConnectedAsync`,
`OnDisconnectedAsync`, `OnErrorAsync`로 올린다.

## 12. 시그니처 규칙

attribute 기반 handler의 메서드 시그니처는 아래 규칙을 따른다.

- 첫 번째 인자: decoded body 타입
- 두 번째 인자: context 타입 (생략 가능)
- 마지막 인자: `CancellationToken` (생략 가능)
- request handler 반환: `ValueTask<T>` 또는 `Task<T>`
- send handler 반환: `ValueTask` 권장

framework가 강제하는 것은 class 구조가 아니라, resolved packet key 하나는
하나의 실행 문맥 안에서 하나의 handler에만 매핑된다는 규칙이다. 일반 channel
messaging의 실행 문맥은 inbound channel capability이고, actor와 spot은 각각
자기 실행 문맥을 가진다. 주제별 handler 묶음(`UserHandlers`)과 패킷별 단일
class(`UserGetHandler`) 둘 다 허용한다.

## 13. DI 동작 기준

- handler class는 `.NET DI`에서 resolve한다.
- handler constructor injection이 동작해야 한다.
- outbound client도 같은 DI 컨테이너에서 주입된다.
- `IZLinkHandlerFilter` 구현체도 같은 DI 컨테이너에서 resolve한다.
- framework는 별도 객체 생성기를 두기보다, `ASP.NET Core`가 쓰는
  `IServiceProvider`를 기준으로 handler invocation을 구성한다.
- 다만 public registration 함수에 `IServiceProvider services`를 매번 노출할 필요는
  없다.
- `Spot`, packet handler, timer handler는 framework가 만든 per-spot scope에서
  resolve하고, registration 함수는 handler 타입만 받는 편이 더 자연스럽다.
- 즉 `Context.AddPacket<THandler>()`, `Context.AddTimer<THandler>(...)` 같은 표면은 service
  locator가 아니라 "이 타입을 spot scope에서 써 달라"는 등록 의미로 읽는 쪽이
  맞다.
- `OnInitializeAsync(...)`도 `IServiceProvider`를 직접 받기보다, spot 자신의
  constructor injection과 cached dependency를 쓰는 편이 hot path와 경계를 더
  분명하게 만든다.

local handler가 붙는 channel은 route prefix가 아니라 애플리케이션이 그 channel에서
server 역할을 한다는 뜻이다. channel 이름은 handler class나 method attribute보다
channel registration
(`options.AddClientServerChannel("api", channel => channel.EnableServer(...))`)에
두는 편을 현재 방향으로 본다. 다만 outbound-only 앱이라면 server capability가
있는 channel은 없을 수 있어야 한다.

## 14. 결정된 기준

- `ZLinkRequestContext`와 `ZLinkSendContext`는 합치지 않는다.
  request-response와 one-way send는 timeout, reply, 호출 의미가 다르므로 별도
  context를 유지한다.
- `OnErrorAsync(...)`는 session으로 매핑 가능한 transport 오류만 받는다.
  application handler 내부 예외, bind/accept/close 같은 node 단위 오류, handshake
  이전 단계의 monitor 이벤트는 runtime monitoring 표면에만 남긴다.
- `Zlink.Framework` runtime은 `IZLinkClient` 위에 channel별 typed wrapper를 공식 기본 표면으로
  제공하지 않는다.
  typed wrapper가 필요하면 응용 또는 별도 확장 패키지가 `IZLinkClient` 위에 얹는
  편을 기본으로 본다.
- `spotRid` 타입은 `RoutingId`를 그대로 사용한다.
  현재 초안에서는 별도 wrapper value type을 올리지 않는다.
- `IZLinkRegistryQuery`와 `IZLinkRegistryQueryClient`는 묶지 않는다.
  in-process 조회와 원격 조회는 lifecycle, 실패 모델, 제공 범위가 다르므로 별도
  인터페이스를 유지한다.
