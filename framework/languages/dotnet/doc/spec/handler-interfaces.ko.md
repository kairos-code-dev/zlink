<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: 기능 맵 — 무엇을, 얼마나 쉽게, 언제](../guide/10-feature-map.ko.md) | [다음: ZLink Framework ASP.NET Core Channel Messaging](./aspnet-core-channel-messaging.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[.NET 묶음](../README.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [channel 샘플](../guide/samples/channel-messaging-samples.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [SPOT 샘플](../guide/samples/spot-samples.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [STREAM 샘플](../guide/samples/stream-samples.ko.md) | [Monitoring](./aspnet-core-monitoring.ko.md) | [Registry](./aspnet-core-registry.ko.md)

# ZLink Framework .NET Interface Catalog

## 1. 목적

이 문서는 한 가지 역할만 맡는다. `.NET` `ZLink Framework` 가 노출하는 **모든
공용 interface 와 attribute 정의** 를 한곳에 모아 두는 것이다. 즉 카탈로그
역할이다.

다른 문서에서 interface 를 참조할 때는 항상 이 문서를 기준으로 삼는다.

사용 예시나 프로그래밍 모델 설명은 여기 넣지 않는다. 실제 사용법은 아래
문서들을 참고한다.

- 서버 간 messaging 프로그래밍 모델 →
  [aspnet-core-channel-messaging.ko.md](./aspnet-core-channel-messaging.ko.md)
- 서버 간 messaging 샘플 →
  [channel-messaging-samples.ko.md](../guide/samples/channel-messaging-samples.ko.md)
- SPOT 통합 →
  [aspnet-core-spot.ko.md](./aspnet-core-spot.ko.md)
- SPOT 샘플 →
  [spot-samples.ko.md](../guide/samples/spot-samples.ko.md)
- STREAM 통합 →
  [aspnet-core-stream.ko.md](./aspnet-core-stream.ko.md)
- STREAM 샘플 →
  [stream-samples.ko.md](../guide/samples/stream-samples.ko.md)
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
| handler | `IZLinkSession` | stream session lifecycle + session packet callback | 4.4 |
| context | `IZLinkSessionContext` | stream session의 send/reply, actor dispatch 표면 | 4.4 |
| context | `IZLinkSessionIdentityContext` | stream session identity 조회 | 4.4 |
| context | `IZLinkSessionClientStream` | session에서 client stream으로 send/reply | 4.4 |
| context | `IZLinkSessionActorDispatchContext` | session에서 actor handle binding과 dispatch 수행 | 4.4 |
| context | `IZLinkSessionLifecycle` | session close 제어 | 4.4 |
| context | `IZLinkSessionActorAttachmentContext` | actor와 session stream 연결/해제 | 4.4 |
| value | `IZLinkActorRef` | session이 actor dispatch target으로 들고 있는 handle | 4.4.1 |
| handler | `IZLinkActor` | actor runtime 안에서 생성되는 application actor | 4.4.1 |
| manager | `IZLinkActorManager` | actor id와 actor type으로 actor 생성, 조회, 재사용 | 4.4.1 |
| context | `IZLinkActorContext` | actor 상태 조회와 spot join 호출 | 4.4.1 |
| handler | `IZLinkActorSendHandler<TMessage>` / `IZLinkActorRequestHandler<TRequest, TReply>` | actor 안에서 처리하는 session relay message handler | 4.4.1 |
| handler | `IZLinkActorPacketHandler<TActor, TMessage>` / `IZLinkActorRequestHandler<TActor, TRequest, TReply>` | actor instance 를 직접 받는 actor message handler | 4.4.1 |
| context | `ZLinkActorSendContext` / `ZLinkActorRequestContext` | actor handler 실행 context. bound session 과 metadata 를 제공한다 | 4.4.1 |
| handler | `IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>` / `IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>` | Entry Spot actor message handler | 4.4.2 |
| handler | `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>` / `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` | user Spot actor message handler | 4.4.2 |
| value | `ZLinkMessageMetadata` | actor/bound session call에 전달되는 application/codec metadata snapshot | 4.4.2 |
| policy | `IZLinkMessageMetadataPolicy` | application metadata forwarding 허용 여부 | 4.4.2 |
| factory | `IZLinkActorFactory` | actor type별 actor 생성 | 4.4.1 |
| handler | `IZLinkSpotActorJoinHandler<TSpot, TActor, TRequest, TReply>` | spot에 actor가 join할 때 호출되는 handler | 4.4.1 |
| internal | route transport helper | routed channel direct target send/request (backend/internal 표면) | 5.5.1 |
| client | `IZLinkBoundSession` | 현재 actor -> 현재 client session 호출 | 5.6 |
| resolver | `IZLinkSpotRemoteAddressResolver` | spot name/id에서 user Spot route 조회 | 5.7 |
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
| client | `IZLinkClientServerClient` | client-server request/send 와 같은 좁은 method set | 5.1 |
| client | `IZLinkClient` | 일반 channel request/send outbound client(client-server, dealer mesh) | 5.1 |
| client | `IZLinkSpotClient` | SPOT outbound client | 5.2 |
| client | `IZLinkRoutedSpotClient` | current Spot 없이 명시한 egress channel 로 target Spot 호출 | 5.2.1 |
| client | `IZLinkSpotMeshPublisherClient` | spot mesh publisher client base | 5.3 |
| client | `IZLinkSpotPublisherClient` | spot channel publish client | 5.3 |
| client | `IZLinkFanoutPublisher` | fanout publisher base | 5.4 |
| client | `IZLinkEventPublisher` | pub/sub event publisher | 5.4 |
| builder | `IZLinkFrameworkOptions` | framework 등록 루트 builder | 6.1 |
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

모든 handler context 가 공유하는 최소 집합이다. 즉 어떤 종류의 handler 든
이만큼은 항상 받는다.

실제 구현에서는 transport[^transport] 별 부가 정보가 따로 있다. 그 부가
정보는 이 공통 context 를 파생한 별도 context 에 덧붙는 형태로 노출된다.

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

`IServiceProvider Services` 는 현재 단계에서 framework 내부 전용(internal)
이다. 즉 public 표면으로는 노출하지 않는다.

handler 안에서 서비스가 필요하면 context 에서 service locator 방식으로 꺼내
쓰지 않는다. 대신 handler class 의 생성자 주입(constructor injection)으로
받는다.

### 3.2 파생 context

handler 종류마다 받아야 하는 부가 정보가 다르다. 그 차이를 공통 context 에
다 우겨 넣지 않고, 종류별로 별도 context 타입을 두어 노출한다.

아래 표는 어떤 handler 가 어떤 context 타입을 받는지, 그 context 가 공통
필드 외에 어떤 정보를 더 들고 있는지 정리한 것이다.

| context 타입 | 사용처 | 추가 정보 |
|-------------|--------|----------|
| `ZLinkRequestContext` | request-response handler | caller metadata, timeout |
| `ZLinkSendContext` | one-way send handler | caller metadata |
| `ZLinkPublishContext` | publish handler | topic, source |
| `ZLinkRouteSendContext` | routed channel send handler | source routing id, router channel id, metadata |
| `ZLinkRouteRequestContext` | routed channel request handler | source routing id, router channel id, metadata, deadline |
| `ZLinkSpotRequestContext` | SPOT request handler | self spot info, source rid, source spot rid |
| `ZLinkSpotSubscriptionContext` | SPOT subscription handler | self spot info, topic, source rid, dispatch metadata |

파생 context 의 상세 필드는 구현에 들어가기 전에 더 좁혀야 한다. 현재 스펙
단계에서는 이름과 역할 정도만 고정해 둔다.

`SPOT` 객체 안에서는 외부 lookup 과 별개로, 현재 spot 자신의 identity 도
조회할 수 있어야 한다. 이 문서에서는 별도의 `Self` wrapper 를 두지 않는다.
대신 SPOT 생성자에서 받는 `IZLinkSpotContext` 에 `SpotRid`, `NodeRid`,
`SpotName` 을 직접 노출한다.

여기서 두 가지 context 의 역할이 다르다는 점에 유의한다. handler 호출마다
따라붙는 `ZLinkRequestContext`, `ZLinkSendContext`, `ZLinkPublishContext` 는
"이번 호출 한 건"에 대한 정보다. 반면 SPOT 객체가 들고 있는
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

routed channel(`AddRouteMeshChannel(...)`) 이 수신하는
메시지를 처리하는 handler 다.

일반 channel handler 와 한 가지 차이가 있다. source `RoutingId` 를 포함한
라우팅 정보까지 context 로 함께 노출한다는 점이다.

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

routed channel handler 등록은 transport builder 가 책임진다. 구체적으로는
`AddSendHandler<THandler>()`, `AddRequestHandler<THandler>()` 처럼 handler 타입만
지정하는 메서드나, message/reply 타입을 함께 지정하는 명시적 overload 를 통해
이루어진다.
자세한 내용은 §6.1 의 `IZLinkRouteChannelBuilder` 를 참고한다.

### 4.3 publish handler

pub/sub 로 publish 된 메시지를 처리하는 handler 다.

이름 규칙에는 의도가 있다. producer 쪽 동사
(`IZLinkEventPublisher.Publish(...)`) 에 맞추어, `Request` / `Send` /
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

### 4.3.1 SPOT lifecycle handler

이 절은 SPOT 객체의 lifecycle 표면과, 그 SPOT 안에서 동작하는 handler
종류들을 정의한다.

현재 framework 초안의 방향은 다음과 같다. `SpotNode.CreateSpot()` 로 만든
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

    ValueTask OnCreateAsync(
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
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

public interface IZLinkActorHandlerRegistry
{
    void AddHandler<THandler>()
        where THandler : class;

    void AddHandler<THandler>(string packetName)
        where THandler : class;

    void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorPacket<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor;

    void AddPostActorJoined<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorLeft<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;
}

public readonly record struct RoutingId(string Value)
{
    public override string ToString() => Value;
}

public interface IZLinkSpotContext : IZLinkActorHandlerRegistry
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

    void AddActorJoin<THandler>()
        where THandler : class;

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
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default)
        where THandler : class;
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

public interface IZLinkEntrySpotContext : IZLinkActorHandlerRegistry
{
    RoutingId NodeRid { get; }
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
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotPostActorJoinedHandler<TSpot, TActor>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorLifecycleContext info,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorLeftHandler<TSpot, TActor>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorLifecycleContext info,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, in TMessage>
    where TEntrySpot : class, IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TEntrySpot entrySpot,
        TActor actor,
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
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotPostActorJoinedHandler<TEntrySpot, TActor>
    where TEntrySpot : class, IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorLifecycleContext info,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorLeftHandler<TEntrySpot, TActor>
    where TEntrySpot : class, IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorLifecycleContext info,
        CancellationToken cancellationToken);
}

public enum ZLinkSpotActorLifecycleKind
{
    Joined = 1,
    Left = 2
}

public sealed record ZLinkSpotActorLifecycleContext(
    ZLinkSpotActorLifecycleKind Kind,
    string ActorId,
    RoutingId? PreviousNodeRid,
    RoutingId? PreviousSpotRid,
    RoutingId? CurrentNodeRid,
    RoutingId? CurrentSpotRid,
    string? PreviousSpotName,
    string? CurrentSpotName,
    bool PreviousIsEntrySpot,
    bool CurrentIsEntrySpot,
    ulong CommitEpoch);
```

`OnCreateAsync(...)` 는 생성 요청이 넘긴 multipart payload를 spot 상태로
해석하는 단계다. framework가 새 spot 인스턴스를 만든 경우에만 호출된다.
`OnInitializeAsync(...)` 는 payload와 무관한 lifecycle 준비 단계다. timer 등록처럼
생성 메시지와 직접 관련 없는 준비 작업은 이 callback에서 수행한다.

새 spot을 만드는 경우 호출 순서는 `Configure()`, descriptor binding,
`OnCreateAsync(createParts, ...)`, `OnInitializeAsync(...)` 순서다.
`GetOrCreateAsync(...)`가 이미 ready 상태인 spot을 반환하는 경우에는 새
`OnCreateAsync(...)`나 `OnInitializeAsync(...)`를 호출하지 않는다.
`CreateAsync(spotName)`처럼 create payload가 없는 편의 overload도 빈 multipart
payload로 `OnCreateAsync(...)`를 한 번 호출한다.

`Configure()` 는 호출 시점이 정해져 있다. SPOT 이 생성된 직후, descriptor
를 바인딩하기 전 시점에 단 한 번 호출된다.

다음 호출들은 이 `Configure()` 단계 안에서만 허용된다.

- `Context.AddPacket(...)`
- `Context.AddHandler(...)`
- `Context.AddActorPacket(...)`
- `Context.AddPostActorJoined(...)`
- `Context.AddActorLeft(...)`
- `Context.AddSubscribe(...)`
- `Context.AddActorJoin(...)`

초기화가 끝난 뒤에 handler 를 추가하면 어떻게 될까. native subscription 과
dispatch table 의 의미가 어긋나게 된다. 그래서 framework 는 이 경우 예외를
던진다.

`AddHandler<THandler>(...)` 는 `THandler` 가 구현한 actor handler interface 를 보고
actor 타입, send/request/lifecycle 종류, packet 이름 기본값을 추론한다.
handler 가 여러 actor handler interface 를 구현해서 모호하면 명시적인
`AddActorPacket<THandler, TActor>(...)`, `AddPostActorJoined<THandler, TActor>()`,
`AddActorLeft<THandler, TActor>()` 를 사용한다.

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
`IZLinkEntrySpotContext.AddHandler<THandler>()` 를 호출한다.

Entry Spot handler 는 Entry Spot 인스턴스, actor, payload 를 함께 받는다.
그래야 handler 가 Entry Spot 이 가진 입장 처리 상태나 helper 메서드를 직접
사용할 수 있고, user Spot handler 와 같은 호출 모양을 유지할 수 있다.

```csharp
public sealed class PlayerEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddHandler<AuthenticateHandler>();
        Context.AddHandler<JoinMatchHandler>();
        Context.AddHandler<PlayerEntryJoinedHandler>();
        Context.AddHandler<PlayerEntryLeftHandler>();
    }
}
```

Entry Spot 의 actor packet handler 는 아래 두 interface 중 하나를 구현한다.

```csharp
public interface IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, in TMessage>
    where TEntrySpot : class, IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TEntrySpot entrySpot,
        TActor actor,
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
        TRequest request,
        CancellationToken cancellationToken);
}
```

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
`IZLinkSpotContext.AddHandler<THandler>()` 를 호출한다. actor 타입을 호출 쪽에서
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
        Context.AddHandler<PlaceMarkHandler>();
        Context.AddHandler<PlayerMatchJoinedHandler>();
        Context.AddHandler<PlayerMatchLeftHandler>();
    }
}
```

user Spot 의 actor packet handler 는 아래 두 interface 중 하나를 구현한다.

```csharp
public interface IZLinkSpotActorSendHandler<TSpot, TActor, in TMessage>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
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
        TRequest request,
        CancellationToken cancellationToken);
}
```

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

#### 4.3.1.3 actor join/leave lifecycle handler 등록

actor 가 Entry Spot 또는 user Spot 에 들어오거나 빠져나간 직후의 후속
처리는 별도 handler 로 등록한다. 사용하는 메서드는
`AddPostActorJoined<THandler, TActor>()` 와 `AddActorLeft<THandler, TActor>()`
다.

이 문서는 `OnJoinActor` 나 `OnLeaveActor` 같은 Spot method override 를
공개 계약으로 두지 않는다. 즉 lifecycle 처리 역시 packet handler 와 같은
방식을 따른다. registry 에 명시적으로 등록한다.

Entry Spot lifecycle handler 는 아래 interface 를 구현한다.

```csharp
public interface IZLinkSpotPostActorJoinedHandler<TEntrySpot, TActor>
    where TEntrySpot : class, IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorLifecycleContext info,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorLeftHandler<TEntrySpot, TActor>
    where TEntrySpot : class, IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorLifecycleContext info,
        CancellationToken cancellationToken);
}
```

user Spot lifecycle handler 는 아래 interface 를 구현한다.

```csharp
public interface IZLinkSpotPostActorJoinedHandler<TSpot, TActor>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorLifecycleContext info,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorLeftHandler<TSpot, TActor>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorLifecycleContext info,
        CancellationToken cancellationToken);
}
```

lifecycle 도 attribute 방식으로 선언할 수 있다.

```csharp
public sealed class PlayerMatchJoinedHandler
{
    [ZLinkSpotPostActorJoined]
    public ValueTask HandleAsync(
        MatchSpot spot,
        PlayerActor actor,
        ZLinkSpotActorLifecycleContext info,
        CancellationToken cancellationToken)
    {
        // ...
    }
}
```

`AddPostActorJoined(...)` 와 `AddActorLeft(...)` 로 등록한 handler 는 호출
시점이 정해져 있다. join/leave commit 이 끝난 뒤, 동일한 실행 문맥에서
호출된다.

이 callback 의 역할도 한정된다. admission 을 결정하는 hook 이 아니다.
commit 이후에 application 상태를 갱신하거나 알림을 발송하는, 후속 처리
단계다.

중복 등록 여부는 같은 registry 단위로 검사한다. Entry Spot registry 와 각
user Spot registry 는 서로 별개의 namespace 로 본다.

같은 registry 안에서 동일한 `actor type + packet kind + packet name` 조합이
둘 이상 등록되면, framework 는 이를 startup validation 오류로 처리한다.
`AddPostActorJoined(...)` 와 `AddActorLeft(...)` 도 같은 규칙을 따른다. 즉 같은
registry 안에서 동일 actor 타입에 대해 하나씩만 허용한다.

`IZLinkSpotContext` 가 노출하는 호출 표면들은 다음 역할을 한다.

- `Context.Publish(topic, ...)` 는 편의 함수다. 현재 SPOT 이 속한 active
  SPOT channel 에 publish 하기 위한 것이다.
- `Context.SendChannel(...)` 과 `Context.RequestChannel(...)` 은 현재 SPOT
  의 실행 문맥에서 channel client 를 호출한다.

`OnClosingAsync(...)` 의 호출 시점은 한정된다.
`IZLinkSpotManager.RemoveAsync(...)` 로 SPOT 을 정상 제거할 때, 실행 문맥
안에서 호출된다.

이 콜백은 destructor 가 아니라는 점에 주의한다. 즉 host shutdown 이나
process 종료 시에 반드시 호출되는 것이 아니다.

이 문서가 전제하는 low-level `.NET` 바인딩 표면도 함께 문서에 고정해 둘
필요가 있다. 현재 `bindings/dotnet/src/Zlink` 기준의 실제 public surface
는 다음과 같다.

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
timer handler 를 호출한다. Entry Spot timer 는 Entry Spot 전체 queue 에 묶지
않고 별도 task 흐름에서 호출한다.

`RequestChannelAsync(...)` 의 completion 은 **항상 같은 spot execution
context 안에서** 실행된다. 즉 임의의 thread 에서 promise 를 직접 완료하지
않는다.

이 보장 덕분에 continuation 도 별도의 `SynchronizationContext` 설정 없이,
spot state 와 동일한 실행 규칙을 따르게 된다.

framework 의 `Context.AddTimer<THandler>(...)` 는 low-level native timer 를
직접 노출하는 표면이 아니다.

현재 잡혀 있는 방향은 다음과 같다. framework runtime 이 policy-aware managed
timer 를 만든다. user Spot timer 는 그 tick 을 **같은 spot execution context**
안으로 enqueue 해서 `IZLinkSpotTimerHandler<TSpot>.HandleAsync(...)` 를 호출한다.
Entry Spot timer 는 전역 Entry Spot queue 에 enqueue 하지 않는다.

`IZLinkTimer.CancelAsync()` 는 이 managed timer loop 를 중단하고 정리하는
고수준 handle 로 이해하면 된다.

#### 4.3.2 SPOT 실행 문맥 정책

이 절의 핵심은 한 가지다. **내부 구현 방식**이 아니라, **사용자에게 보이는
실행 계약**이다.

framework 초안은 `Spot` 을 단순한 recv helper 로 보지 않는다. 같은 user
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
  `Context.AddPacket<THandler>(...)`,
  `Context.AddSubscribe<THandler>(...)`,
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

내부 구현 측면에서는 mailbox + single consumer 모델이 유력 후보다. 다만
이는 구현 메모 내지 internals 성격의 설명으로 남긴다.

binding spec 의 공개 표면에서는 두 가지만 드러내는 편이 적절하다. handler
등록 모델과, session/actor 조합 모델이다.

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
        Message payload,
        SendFlags flags = SendFlags.None);

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
    IZLinkSessionContext Context { get; }

    ValueTask OnConnectedAsync(CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(CancellationToken cancellationToken);

    ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken);

    ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionIdentityContext
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }
}

public interface IZLinkSessionClientStream
{
    IZLinkSessionSendCall Send<TMessage>(TMessage message);

    IZLinkSessionReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkSessionActorDispatchContext
{
    IReadOnlyCollection<IZLinkActorRef> BoundActors { get; }

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

    bool TryGetBoundActor(
        string actorId,
        out IZLinkActorRef actor);

    ValueTask RelayToActorAsync(
        IZLinkActorRef actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default);
}

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
}

public interface IZLinkSessionContext :
    IZLinkSessionIdentityContext,
    IZLinkSessionClientStream,
    IZLinkSessionActorDispatchContext,
    IZLinkSessionLifecycle;

public interface IZLinkSessionSendCall
{
    IZLinkSessionSendCall Metadata(string key, string value);

    IZLinkSessionSendCall PacketName(string messageName);

    IZLinkSessionSendCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall Metadata(string key, string value);

    IZLinkSessionReplyCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}

```

session 구현체는 framework 가 생성자에 넘긴 `IZLinkSessionContext` 를
`Context` property 로 그대로 노출해야 한다. 이 규칙은 문서 가이드가 아니라
runtime 이 검증하는 계약이다.

`CloseAsync(...)` 는 현재 session 의 stream peer 연결을 서버 쪽에서 끊는
동작이다.

호출 시점은 다음과 같은 상황이다. 인증 실패, protocol 위반, idle timeout
처럼 더 이상 packet 을 받을 이유가 없는 경우다.

연결을 끊은 뒤의 session binding 정리는 framework 가 담당한다. 정리 기준은
`sessionId + bindingToken` 이다. 이 binding 상태는 public resolver 계약이
아니라, framework/core runtime 의 내부 상태다.

`Write(...)` 는 framework Header 기반 packet session 에서 stream 으로 packet 을
보내는 low-level submit 이다. 일반 application 코드는 가능한 한 `Context.Send(...)`,
`Context.Reply(...)`, `IZLinkBoundSession` 같은 framework helper 를 사용한다.

session handler 에서 다른 channel 로 send/request 를 보내야 한다면
`IZLinkSessionContext` 가 아니라 DI 로 주입받은 `IZLinkClient` 를 사용한다.
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

즉 현재 방향은 다음과 같이 정리할 수 있다.

- header session
  - `OnDispatchAsync(...)`로 framework가 decode 한 `ZlinkStreamHeader`와
    `Message` payload를 받는다.
  - callback 안에서 payload 를 바로 읽거나 `RelayToActorAsync(...)` 로 넘길
    수 있다. framework runtime 이 수신 payload 를 해제하므로 session handler 는
    `Dispose()` 나 `Move()` 를 기본 사용법으로 쓰지 않는다. callback 뒤에도
    보관할 때만 `Copy()` 또는 `Move()` 를 사용한다.
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

즉 binding/framework 가 노출해야 할 표면은 다음과 같다. "`session` 을 어느
`Spot` 에 join 시키는가"라는 상위 조합 표면이다. `Spot` 자체가 session
타입을 직접 소유하는 고정 모델이 아니다.

이 문서는 recv loop 를 application 표면으로 직접 내보내지 않는다. 즉 두
가지 모델 중 두 번째를 기본으로 삼는다.

- 첫 번째: 사용자가 `Recv(...)` 로 drain loop 를 돌리는 모델.
- 두 번째 (기본): framework 가 dispatch 를 담당하고, application 은 Header
  기반 packet session 을 구현하는 모델.

또한 stream 핫패스에서는 메모리 할당을 최소화하는 것이 원칙이다. 즉
`Message.ToArray()` 같은 불필요한 추가 복사를 기본 사용법으로 두지 않는다.

`Message.AsReadOnlySpan()` 같은 기존 표면이나 그 위에 얹는 protobuf/json
decode helper 가, 가능한 한 추가 메모리 할당 없이 동작하도록 설계하는 것을
기본 원칙으로 둔다.

#### 4.4.1 actor/session 상위 모델 메모

actor join, actor factory, stream-attached actor 모델은 현재 draft
`Zlink.Framework` 의 구현 범위에 포함된다.

이 절은 다음 공개 계약들을 기준으로 설명한다.

- `IZLinkActor`
- `IZLinkActorContext.JoinSpot(...)`
- `IZLinkEntrySpotContext.AddHandler<THandler>()`
- `IZLinkEntrySpotContext.AddActorPacket<THandler, TActor>()`
- `IZLinkEntrySpotContext.AddPostActorJoined<THandler, TActor>()`
- `IZLinkEntrySpotContext.AddActorLeft<THandler, TActor>()`
- `IZLinkSpotContext.AddHandler<THandler>()`
- `IZLinkSpotContext.AddActorPacket<THandler, TActor>()`
- `IZLinkSpotContext.AddActorJoin<THandler, TActor, TRequest, TReply>()`
- `IZLinkSpotContext.AddActorJoin<THandler>()`
- stream session 의 actor dispatch 표면인 `IZLinkSessionContext`
- actor stream 연결/해제를 담당하는 `IZLinkSessionActorAttachmentContext`

`stage-wrapper-on-spot.ko.md` 는 이 계약 위에서 room/stage wrapper 를
어떻게 구성하는지 보여 주는 상위 모델 문서다. 함께 읽으면 도움이 된다.

##### zlink native Actor API 위임

zlink 라이브러리가 native Actor API 를 제공한다. framework 는 actor
lifecycle 관리를 이 API 로 위임한다.

- `SpotNode.CreateActor(string actorId)` — actor node에서 application actor에 대응하는 native actor를 생성한다.
- `SpotNode.EntrySpot()` → `Spot` — actor join 요청을 받는 입장 spot을 얻는다.
- `Spot.RecvActorJoin(RecvFlags)` → `ActorJoinRequest?` — join 요청을 수신한다.
- `Spot.ReplyActorJoin(request, accepted, message)` — join 수락/거부 결과를 응답한다.
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
2. 이 `TargetActor` 를 spot actor membership 에서 조회한다.
3. 조회에 성공하면 actor join handler 를 호출한다.
4. `TargetActor` 를 찾지 못하면 join 요청을 거부한다.

framework 는 native `ActorRef` 를 public surface 에 그대로 노출하지
않는다.

`AddHandler(...)`, `AddPostActorJoined(...)`, `AddActorLeft(...)` 로 등록한
lifecycle handler 에는
`ZLinkSpotActorLifecycleContext` 가 전달된다. 이 값은 다음 정보를 담는다.

- join/leave 종류
- actor id
- 이동 전/후 node rid
- 이동 전/후 spot rid
- 가능하면 commit epoch 까지

commit epoch 의 값은 출처에 따라 다르다.

- native callback 을 통해 온 값이면, commit epoch 를 그대로 전달한다.
- framework membership 변경만으로 만들어 낸 알림이면, epoch 는 `0` 으로
  둔다.

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
  과 user Spot 에 각각 별도로 등록할 수 있어야 한다. `AddActorJoin(...)`
  은 join admission 요청 처리이고, `AddPostActorJoined(...)` /
  `AddActorLeft(...)` 는 commit 이후의 lifecycle callback 이라는 점에
  주의한다.
- `JoinSpot(...)` 이나 actor join handler 가 actor 의 현재 `Spot` 을
  바꾸는 경우가 있다. 이때 framework 는 actor session state 갱신과 이후
  dispatch 선택이 서로 경합하지 않도록 보장해야 한다. join 이후에 도착한
  packet 은 새 `Spot` 의 실행 문맥으로 흘러 들어가야 한다.
- 이 계약은 두 가지 표면을 분리하는 효과를 가진다. actor 가 사용하는
  stream I/O 표면과, `Spot` 상태 변경 표면이다. session 은 packet ingress
  역할만 담당한다. join 된 actor 의 game/domain 처리는 `Spot` 의 실행
  문맥에서 직렬화된다.

actor 실행 객체와 session dispatch handle 은 분리해서 다룬다. session 은
`IZLinkActorRef` 를 저장하고 dispatch 에 사용한다.

`IZLinkActor` 는 actor node 에서 생성되는 application 객체다. framework
가 `Context` 를 설정한 다음, `Configure()` 를 한 번 호출한다. 그 이후의
callback signature 는 context 인자를 매번 다시 받지 않는다.

actor handler 계약은 actor 런타임이 소유하므로 `Zlink.Framework.Contracts.Actors`
namespace 에 둔다. session 은 `IZLinkSessionActorDispatchContext` 로 packet 을
actor 에 전달할 뿐이고, handler 선택과 실행은 actor runtime 이 처리한다.

```csharp
public interface IZLinkActorRef
{
    string ActorId { get; }
    string ActorType { get; }

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

    ValueTask OnDisconnectedAsync(CancellationToken cancellationToken);
}

public interface IZLinkActorContext
{
    string ActorId { get; }
    string? SessionId { get; }
    string? SpotName { get; }
    bool IsJoined { get; }

    IZLinkSpot GetSpot();

    TSpot GetSpot<TSpot>()
        where TSpot : class;

    IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        string spotName,
        TRequest request);

    IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        RoutingId spotRid,
        TRequest request);
}

public interface IZLinkActorJoinSpotCall
{
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);

    ValueTask<TReply> SubmitAsync<TReply>(
        CancellationToken cancellationToken = default);
}

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

public sealed class ZLinkActorSendContext : ZLinkHandlerContext
{
    public string ActorId { get; }
    public string RouterChannelId { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public IZLinkBoundSession BoundSession { get; }
}

public sealed class ZLinkActorRequestContext : ZLinkHandlerContext
{
    public string ActorId { get; }
    public string RouterChannelId { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public IZLinkBoundSession BoundSession { get; }
    public new DateTimeOffset? Deadline { get; }
}

// Spot에 actor를 join할 때 호출되는 handler. spot 등록의 AddActorJoin<...>() 표면이
// 이 generic 인자를 받고, framework는 join 시점에 target spot, joining actor,
// request payload를 함께 넘긴다. spot 실행 문맥 안에서 호출된다.
public interface IZLinkSpotActorJoinHandler<TSpot, TActor, in TRequest, TReply>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}

// IZLinkSpotContext 자체에는 actor attach/detach 실행 표면을 두지 않는다.
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

actor join handler 는 attribute 방식도 지원한다. 이 경우 `Configure()` 에서는
`Context.AddActorJoin<THandler>()` 를 호출하고, framework 는 method parameter와
반환 타입에서 spot, actor, request, reply 타입을 추론한다.

```csharp
public sealed class JoinMatchSpotHandler
{
    [ZLinkSpotActorJoin]
    public ValueTask<JoinMatchSpotResult> HandleAsync(
        MatchSpot spot,
        PlayerActor actor,
        JoinMatchReq request,
        CancellationToken cancellationToken)
    {
        // ...
    }
}
```

`IZLinkSpotContext` (§4.3.1) 와 `IZLinkSpotActorMembership` 은 서로 분리된
표면이다. 다만 같은 spot 실행 문맥에서 함께 사용된다. 각각의 역할은 다음과
같다.

- `IZLinkSpotContext` 는 packet/subscribe/timer 등록과 channel 호출을
  담당한다.
- `IZLinkSpotActorMembership` 은 Spot 안에서 actor 를 attach/detach 할 때
  framework 가 주입해 주는 표면이다.

actor join handler 코드가 actor membership 을 직접 다뤄야 하는 경우에만,
두 번째 표면을 함께 주입받는다.

actor context 는 현재 client session 의 `SessionId` 만 조회값으로 노출한다.

session rid 와 binding token 은 actor 가 client 로 push 할 때 사용하는
runtime 내부 metadata 다. 그래서 actor context 에는 드러내지 않는다.

이유는 단순하다. application actor 코드가 session 위치값을 직접 들고 있으면,
재접속 시 stale 상태로 빠지기 쉽기 때문이다. framework runtime 은 필요한
session route 를 actor state 안에서 관리하고, actor code 에는 `BoundSession`
만 노출한다.

framework runtime 은 actor context 를 먼저 주입한 뒤 actor 객체를 만든다.
actor packet handler 등록은 actor 객체 쪽이 아니라, Entry Spot 또는 user
Spot 의 `Configure()` 단계에서 이루어진다.

이렇게 두면 한 가지 이점이 있다. Entry 단계와 user Spot 단계를 하나의
actor class 안에서 상태 분기로 섞을 필요가 없다.

channel outbound 는 actor context 의 기능이 아니다. Entry Spot 또는 user Spot
안에서 channel 로 메시지를 보내야 하면 handler 가 받은 `entrySpot.Context` 또는
`spot.Context` 의 `SendChannel(...)` / `RequestChannel(...)` 을 사용한다.
client stream 으로 push 해야 하면 `ZLinkActorSendContext.BoundSession`,
`ZLinkActorRequestContext.BoundSession`, 또는 `IZLinkBoundSession` 를
사용한다.

`GetSpot(...)` 은 actor 가 `Spot` 에 join 한 뒤에만 유효하다. join 전
호출은 명확한 실패로 처리된다.

actor membership 변경은 actor callback 에서 처리하지 않는다. 대신
`IZLinkSpotActorMembership.JoinActorAsync(...)` 와 `LeaveActorAsync(...)`
에서 처리한다 (§4.4.1 참고).

`JoinSpot(spotRid, request)` 는 user Spot routing id
(`string`) 을 받는다. `gameId`, `matchId`, `roomId` 같은 도메인 키를 그대로
사용할 수 있다.

`spotName -> RoutingId` 변환은 framework 내부의 spot remote address resolver 가
담당한다. actor handler 표면에는 `RoutingId` 를 노출하지 않는다.

`Send(...)` 는 현재 actor 에 연결되어 있는 stream client 로 packet 을
보낸다. request 에 대한 응답은 actor context 에서 직접 쓰지 않고,
request handler 의 반환값으로 보낸다.

actor, Entry Spot actor, user Spot actor request 는 모두 같은 방식으로
처리한다. `IZLinkActorRequestHandler<..., TReply>`,
`IZLinkEntrySpotActorRequestHandler<..., TReply>`,
`IZLinkSpotActorRequestHandler<..., TReply>` 가 반환한 값이 reply 가 되고,
framework 가 원래 request 의 sequence 정보를 사용해 response 를 작성한다.
따라서 request packet 은 send handler 로 fallback dispatch 되지 않는다.

context 가 `IZLinkStream` 객체를 직접 노출하지는 않는다. stream 이 연결되지
않은 actor 에서 `Send(...)` 를 호출하면, 명확한 실패로 처리된다.

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

`SPOT` 과 actor packet 처리에는 두 가지 모드를 모두 두는 편이 자연스럽다.
편의 모드와 고성능 모드다.

이유는 응용마다 우선순위가 다르기 때문이다.

- 어떤 응용은 constructor injection 과 동적 resolve 의 편의가 더 중요하다.
- 또 다른 응용은 packet hot path 에서 reflection 이나 per-packet resolve
  를 절대로 허용해서는 안 된다.

이 문서에서 잡는 최소 표면은 다음과 같다.

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

각 모드의 의미는 다음과 같이 읽는다.

- `Compiled`
  - reflection 사용은 registration 또는 runtime warm-up 단계까지만
    허용한다.
  - packet hot path 에서는 미리 만들어 둔 자원만 사용한다. 즉 cached
    delegate, prebuilt dispatch table, 미리 선택해 둔 factory 만 사용한다.
  - per-packet `IServiceProvider` resolve 나 `MethodInfo.Invoke(...)` 같은
    경로는 피한다.
- `Dynamic`
  - 유연한 등록과 늦은 바인딩을 우선한다.
  - 성능이 덜 중요한 관리용 handler 나, 초기 실험 단계에서 허용해 둘
    만하다.

framework 가 두 모드를 모두 제공할 수는 있다. 다만 기본 성능 원칙은 명확
하다. "`Compiled` 모드에서는 hot path 에 reflection 이 남아 있어서는 안
된다" 쪽으로 읽는 것이 맞다.

### 4.5 message serializer

`playhouse/extensions` 에서 보이듯, serializer 계층은 transport interface
와 분리해 두는 쪽이 자연스럽다.

즉 `STREAM` handler 는 `Message` 를 받기만 한다. protobuf/json 같은 객체
변환은 별도의 serializer 또는 extension helper 가 담당한다.

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

실사용 표면은 binding core 의 `Message` 자체에 직접 얹지 않는다. 대신
별도의 codec extension/helper 계층으로 얹는 방식을 기본으로 본다. 예시는
다음과 같다.

```csharp
public static class MessageExtensions
{
    public static T Parse<T>(this Message message);
}
```

이 문서에서는 다음 규칙을 기본으로 둔다.

- `T` 가 generated protobuf 타입이면서 `IMessage<T>` 계열이면, protobuf
  로 해석한다.
- 그 밖의 일반 class 는 json 으로 해석한다.

즉 두 가지 구조 중 후자를 택한다.

- 첫 번째: transport 가 serializer 를 직접 내장하는 구조.
- 두 번째 (기본): `Message` 위에 type 기준의 parse helper 를 얹는 구조.

두 번째 구조가 `playhouse/extensions` 와도 더 가깝고, 문서도 더 단순하게
읽힌다. 이때 `Parse<T>()` 는 binding core 의 필수 인스턴스 메서드가
아니다. framework 또는 codec extension package 가 제공하는 public helper
로 보는 편이 맞다.

## 5. Client 인터페이스

이 절은 서버에서 다른 서버로 메시지를 보내는 client interface 들을 정의한다.

### 5.1 IZLinkClient

서버 간 outbound 호출을 위한 공용 client 다. DI 로 주입되며, ZLink handler
와 기존 ASP.NET Core HTTP handler 양쪽에서 동일하게 사용할 수 있다.

호출 방식은 한 가지 축을 기본으로 둔다.

- `channelName` 기준 호출 — Discovery 가 대상을 선택한다.

다시 말해, 일반 channel messaging 에서는 특정 `ROUTER(server)` 를 `rid` 로
직접 지정해 호출하지 않는다. `rid` 를 넣는 routed 호출은 SPOT spot-to-spot
경로에만 남겨 둔다.

packet key 는 매번 별도의 문자열로 받지 않는 것이 기본이다. 기본적으로는
payload 타입의 `Type.Name` 을 사용해 해석하는 쪽을 기준으로 본다. 예컨대
`GetProfileRequest` 는 기본적으로 `GetProfileRequest` packet 으로 매핑된다.

이 기본 규칙만으로 충분하지 않은 경우를 위해, public 표면은 builder
패턴을 따른다. 즉 `Request(...)` 와 `Send(...)` 가 builder 를 돌려준다.
`PacketName`, `Timeout` 같은 변형은 builder 에 체이닝으로 이어 붙인다.

이렇게 두는 이유는 단순하다. `packetName` 과 `timeout` 조합마다 overload
를 계속 늘릴 필요가 없기 때문이다.

```csharp
public interface IZLinkSendCall
{
    IZLinkSendCall PacketName(string packetName);

    ValueTask Submit(
        CancellationToken cancellationToken = default);
}

public interface IZLinkRequestCall
{
    IZLinkRequestCall PacketName(string packetName);

    IZLinkRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> SubmitAsync<TReply>(
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

`IZLinkClient` 는 일반 channel request/send outbound 표면이다. 호출자는
`channelName`만 넘기고, runtime 은 등록된 channel bundle 을 보고 client-server
channel 이면 local client DEALER socket 을, dealer mesh channel 이면 mesh DEALER
socket 을 선택한다.

`IZLinkClientServerClient` 는 같은 method set 을 가진 좁은 base interface 로
남긴다. 특정 코드가 client-server channel 만 다룬다는 의도를 드러내고 싶을 때
주입받을 수 있다. 다만 일반 channel-to-channel 예제와 새 샘플 코드는
client-server 와 dealer mesh 를 같은 호출 표면으로 설명하기 위해 `IZLinkClient`를
기본으로 사용한다.

runtime 의 채널 구성 방식은 다음과 같다.

- 등록된 `channelName` 마다 별도의 outbound channel 을 생성한다.
- 각 channel 은 capability 마다 다시 별도의 outbound runtime 을 가진다.
- 특히 수동 연결은 `channel` 전체가 아니라 `channel + capability` 단위로
  관리한다.

예를 들어 다음처럼 구분해야 한다.

- `profile.client` 수동 연결
- `profile.subscriber` 수동 연결

이유는 명확하다. `profile` channel 하나만으로는 "request client 연결인지,
subscriber 연결인지" 를 식별할 수 없다. 그래서 framework 역시 capability
별 runtime 을 따로 관리하게 된다.

packet key 해석 규칙은 다음 순서를 기본으로 본다.

1. builder 에서 `PacketName(...)` 이 지정되어 있으면, 그 값을 사용한다.
2. 지정되어 있지 않으면, payload 타입에 선언된 packet metadata 를 본다.
3. 그것도 없으면, `Type.Name` 을 packet key 로 사용한다.

즉 단순한 경우라면 타입 이름만으로도 충분하다. 모호하거나 충돌이 발생하는
경우에만, 명시적인 `PacketName` 을 지정하도록 유도한다.

timeout 은 request 와 send 간에 다르게 다룬다.

- `Request(...)` 는 reply 를 기다리므로 `Timeout(...)` 을 둘 수 있다.
- `Send(...)` 는 응답을 기다리지 않으므로 timeout 설정을 두지 않는다.
- `Publish(...)` 도 같은 이유로 timeout 설정을 두지 않는다.
- `Send(...).Submit(...)` 는 handler 완료를 기다리는 호출이 아니다.
  framework 가 메시지를 transport 에 위임할 수 있을 때까지 기다리는,
  비동기 submit 이다.
- `Publish(...).Submit(...)` 도 동일한 의미다. subscriber 의 handler
  완료나 subscriber 수신을 기다리지 않는다. local publish transport 에
  submit 되는 시점까지만 대기한다.
- send backpressure 의 대기 한계는 builder 가 아니라, channel 또는
  socket 의 `SendTimeout` 옵션을 따른다.
- framework channel/socket option 의 `SendTimeout` 기본값은
  `TimeSpan.FromMilliseconds(200)` 이다. async submit runtime 은 core
  socket 의 기본값을 사용하지 않는다. 대신 framework 가 socket/channel
  option 에 설정한 resolved `SendTimeout` 값을 읽는다. 사용자가
  `SendTimeout = null` 로 명시한 경우에 한해, core `-1` 과 같은 무한 대기
  로 본다.
- `Request(...).SubmitAsync<TReply>(...)` 도 마찬가지다. request packet 을
  내보내는 단계에서는, `Send(...).Submit(...)` 와 동일한 nonblocking
  submit 경로를 사용한다.
- `Request(...).Timeout(...)` 은 reply 대기 시간만을 결정한다.
- 이 문서는 별도의 public no-wait 옵션을 제공하지 않는다. temporary
  backpressure 는 public `false` 반환값이 아니라, framework 내부의 queue
  와 ready notification 으로 처리한다.

호출자가 `await` 하면, 호출 흐름은 submit 완료 시점까지 멈춘다. 다만 구현
은 thread 를 점유해서는 안 된다.

즉 backpressure 가 걸려 있는 동안에는 현재 thread 나 thread pool worker
를 잡지 않는다. socket ready callback 이나 poller wakeup 이 도달하면,
pending submit 을 이어서 진행해야 한다.

고성능 구현을 위한 기본 계약은 다음과 같다.

- 즉시 전송이 가능한 fast path에서는 completed `ValueTask`를 반환하며 heap
  allocation을 발생시키지 않는다.
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
  대기에는 `RequestTimeout` 만 사용한다.

따라서 public 호출 감각은 다음과 같이 잡힌다.

```csharp
var reply = await client
    .Request("profile", new GetProfileRequest { AccountId = accountId })
    .Timeout(TimeSpan.FromMilliseconds(200))
    .SubmitAsync<GetProfileReply>(cancellationToken);

await client
    .Send("profile", new RefreshProfileCacheCommand { AccountId = accountId })
    .PacketName("profile.refresh-cache")
    .Submit(cancellationToken);
```

### 5.2 IZLinkSpotClient

현재 spot runtime 안에서의 outbound 호출을 담당하는 client 다.
`IZLinkClient` 와는 독립된 interface 이며, 하부에서 서로 다른 C API 를
감싼다.

현재 다루는 축은 세 가지다.

- 현재 SPOT channel 안에서의 publish/subscribe
- attach 된 channel client 를 거치는 다른 channel 의 send/request
- spot name/id 기반의 routed spot send/request

spot name/id 기반 호출의 흐름은 다음과 같다. `IZLinkSpotRemoteAddressResolver`
가 target node 와 spot rid 를 조회한다. 그다음 framework 내부의 route
transport 가 실제 전송을 담당한다.

application 이 `targetRid + spotRid` 를 직접 넘기는 일은 없다.

```csharp
public interface IZLinkSpotClient
{
    IZLinkSendCall SendSpot<TMessage>(
        string spotName,
        TMessage message);

    IZLinkSendCall SendSpot<TMessage>(
        RoutingId spotRid,
        TMessage message);

    IZLinkRequestCall RequestSpot<TMessage>(
        string spotName,
        TMessage request);

    IZLinkRequestCall RequestSpot<TMessage>(
        RoutingId spotRid,
        TMessage request);

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

`IZLinkClient` 와 비교했을 때의 차이는 다음과 같다.

- `Publish(topic, ...)` 가 포함된다. SPOT 쪽은 현재 channel 안에서 topic
  publish 를 함께 사용하는 경우가 많기 때문에, 같은 interface 에 둔다.
- `SendSpot(...)` / `RequestSpot(...)` 은 spot remote address resolver 를 사용한다.
- `SendChannel(...)` / `RequestChannel(...)` 은 attach 된 channel client
  를 통해 해소한다.
- 따라서 local `SpotNode` 나 local spot runtime 이 없는 앱이라면, 기본
  outbound 표면은 `IZLinkClient` 다. 그런 앱에서 외부 SPOT channel
  publish 만 필요한 경우에는, `IZLinkSpotPublisherClient` 를 별도로
  사용한다.
- channel send/request 는 일반 `IZLinkClient` 와 동일한 builder 감각을
  따르는 편이 자연스럽다.
- timer 는 별도 callback scheduler 로 두지 않는다. 대신 spot lifecycle
  안에서 `Context.AddTimer<THandler>(name, period, ...)` 로 등록하는 한
  가지 모델로 설명하는 편이 더 자연스럽다. 구현 측면에서는, framework
  runtime 이 만든 managed `.NET` timer 를 동일한 spot execution context
  로 매핑하는 방향이 적절하다.

framework 초안에서 말하는 "spot 용 함수" 와 "channelName 으로 호출하는
함수" 는 서로 별개의 경로다. 두 경로는 다음과 같이 갈라진다.

- channel 이름 기준 호출은 attach 된 channel client 를 사용한다.
- spot name/id 기반 호출은 `IZLinkSpotRemoteAddressResolver` 가 해소한 위치값을,
  framework 내부 transport 가 사용한다.

`targetRid + spotRid` 를 직접 넘기는 raw route 함수는 application public
표면에 두지 않는다.

`IZLinkClient` 와 `IZLinkSpotClient` 는 상하 관계가 아니다. 두 interface
는 서로 다른 하부 C API 를 감싸며, 각자 독립적인 구현을 가진다.

#### 5.2.1 IZLinkRoutedSpotClient

`IZLinkRoutedSpotClient` 는 active Spot callback 밖에서 target Spot 으로
send/request 할 때 사용한다. 이 interface 는 spot name 으로 transport 를
찾지 않는다. 호출자는 먼저 `ViaEgressChannel(...)`로 source process 가 이미
보유한 local egress channel 을 고른다. target 은 `RoutingId` 로만 받는다.
string 이름이 필요하면 호출자가 `RoutingId.Of(...)` 같은 변환을 먼저 수행한다.

```csharp
public interface IZLinkRoutedSpotClient
{
    IZLinkRoutedSpotChannelClient ViaEgressChannel(
        string localEgressChannelName);
}

public interface IZLinkRoutedSpotChannelClient
{
    IZLinkSendCall SendSpot<TMessage>(
        RoutingId spotRid,
        TMessage message);

    IZLinkRequestCall RequestSpot<TRequest>(
        RoutingId spotRid,
        TRequest request);
}
```

`localEgressChannelName`은 target Spot 의 channel 이름이 아니다. 이 값은
source process 안에 등록된 client-server client channel 또는 route mesh channel
이름이다. 해당 channel 은 builder 에서
`EnableSpotRouteEgress(targetSpotNodeChannelName)`으로 target SpotNode 가
`AcceptSpotRoutesFromChannel(...)`로 연 ingress channel 이름을 저장해야 한다.

이 분리를 두는 이유는 target Spot rid 만으로는 어떤 connection 을 타야 하는지
항상 알 수 없기 때문이다. 같은 process 가 client-server egress 와 route mesh
egress 를 둘 다 갖고 있을 수 있으므로, caller 가 사용할 local egress channel 을
명시해야 한다.

### 5.3 IZLinkSpotPublisherClient

`IZLinkSpotClient.Publish(...)` 와 `IZLinkSpotPublisherClient` 는 사용
상황이 다르다.

- `IZLinkSpotClient.Publish(...)` 는 이미 실행 중인 local spot 문맥에서
  현재 SPOT channel 로 publish 할 때 사용한다.
- `IZLinkSpotPublisherClient` 는 local spot 인스턴스가 없는 외부 노드가,
  특정 spot channel 로 publish 할 때 사용하는 별도의 client 다.

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

`IZLinkSpotPublisherClient` 는 `IZLinkSpotMeshPublisherClient` 를 그대로
상속한다. 단일 SPOT publisher 와 mesh publisher 양쪽이, 동일한 publish
호출 표면을 공유한다.

여기서 `channelName` 은 target SPOT channel 의 이름이다. 즉 이 interface
는 다음 상황을 위한 것이다. `game.stage`, `game.chat` 처럼 여러 SPOT
channel 이 존재할 때, 외부 노드가 어느 channel mesh 로 publish 할지
선택하는 용도다.

`IZLinkSpotClient.Publish(...)` 와의 차이는 다음과 같이 정리할 수 있다.

- `IZLinkSpotClient.Publish(...)`
  - local spot 문맥 안에서 현재 SPOT channel 로 publish 한다.
- `IZLinkSpotPublisherClient.Publish(...)`
  - local spot 인스턴스 없이, 외부 노드에서 target SPOT channel 로
    publish 한다.

### 5.4 IZLinkEventPublisher

일반 `PUB/SUB` event 를 publish 하기 위한 interface 다.

SPOT publish 와는 별개의 경로다. 즉 channel messaging 쪽에서 사용한다.

```csharp
public interface IZLinkPublishCall
{
    IZLinkPublishCall PacketName(string packetName);

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

`IZLinkEventPublisher` 는 `IZLinkFanoutPublisher` 를 그대로 상속한다. 즉
두 가지 모두 주입 표면으로 쓸 수 있다.

- capability 별 신규 명명을 따르는 `IZLinkFanoutPublisher`
- 기존 별칭인 `IZLinkEventPublisher`

여기서 두 문자열의 역할은 각각 다음과 같다.

- `channelName`
  - 어느 논리 channel 의 `PUB/SUB` mesh 에 publish 할지를 지정한다.
- `topic`
  - 해당 channel 내부에서 어떤 subscriber 집합이 이벤트를 수신할지를
    지정한다.

따라서 `Publish("profile", "profile.cache-refreshed", evt)` 호출의 의미는
다음과 같다. `profile` channel 안의 `profile.cache-refreshed` topic 으로
fan-out 한다는 뜻이다.

일반 `PUB/SUB` publish 도 `Send(...)` 와 마찬가지로 timeout 을 두지
않는다. 다만 필요할 때 packet 이름 override 정도는 지정할 수 있다.

여기서 `Async(...)` 의 의미에 주의한다. remote peer 의 처리 완료를
기다린다는 뜻이 아니다. framework local runtime 이 send/publish 를 받아 줄
수 있을 때까지 기다리는, 비동기 submit 을 의미한다.

실패 처리는 두 갈래로 나뉜다.

- temporary backpressure: framework 내부 queue 와 ready notification 으로
  처리한다.
- 그 외 submit 실패 (예: route-not-ready): 예외로 처리한다.

publish 도 send 와 동일한 성능 규칙을 따른다. 즉 다음과 같이 동작한다.

- subscriber 마다 별도 task 를 생성하지 않는다.
- subscriber 수만큼 payload 를 재직렬화하지도 않는다.
- 가능한 경우 topic frame 과 payload frame 을 한 번만 만든다. 그리고
  하부 publish socket 의 submit 경로가 backpressure 를 처리하게 한다.
- `NoDrop` 같은 publish socket 정책이 켜져 있다면, drop 대신
  `SendTimeout` 까지 backpressure 를 기다린다. timeout 이 만료되면 예외로
  실패한다.

### 5.5 Actor Route Resolver

session 에서 actor 로 packet 을 relay 할 때는 `IZLinkSessionContext` 의
`RelayToActorAsync(...)` 를 사용한다. actor runtime 을 직접 호출하는 별도
public client 는 두지 않는다.

remote actor 위치는 session 이 직접 계산하지 않는다. session 은 actor id/type 으로
local actor handle 을 만들거나, actor host runtime 이 발급한 `ZLinkActorRemoteAddress` 로
remote actor handle 을 만들고, core ActorGateway 가 그 actor ref 를 기준으로 relay 한다.

### 5.5.1 route transport helper

route transport helper 는 application 의 public surface 가 아니다.
internal transport helper 다.

사용처는 다음과 같다. routed channel (`AddRouteMeshChannel(...)`) 을 통해 특정 노드의 `RoutingId` 로 direct
send/request 를 보내야 하는 framework backend, 또는 별도의 adapter
package 가 사용한다.

일반 application 코드는 `RoutingId` 를 직접 넘기지 않는다. 대신 actor id
또는 spot key 기반 client 를 사용한다.

이 helper 의 내부 wire 형식은 공통 message model 의 multipart
`header + payload` 계약을 따른다.

typed `message` 나 `request` 인자를 받더라도, runtime 은 route header 와
payload 를 하나의 `Message` 로 합쳐서 직렬화하지 않는다. 대신 part 를 분리해
둔다.

- framework header 는 첫 번째 part 에 둔다.
- codec 이 생성한 payload bytes 는 별도의 part 에 둔다.
- actor dispatch 나 bound session 처럼 내부 metadata 가 추가로 필요한
  경로에서는, payload 앞에 metadata part 를 더 붙일 수 있다.

```csharp
internal interface IZLinkRouteTransport
{
    IZLinkRouteSendCall SendToAsync<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message);

    IZLinkRouteRequestCall RequestToAsync<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request);
}

internal interface IZLinkRouteSendCall
{
    IZLinkRouteSendCall PacketName(string packetName);
    IZLinkRouteSendCall Metadata(string key, string value);
    ValueTask Submit(CancellationToken cancellationToken = default);
}

internal interface IZLinkRouteRequestCall
{
    IZLinkRouteRequestCall PacketName(string packetName);
    IZLinkRouteRequestCall Metadata(string key, string value);
    IZLinkRouteRequestCall Timeout(TimeSpan timeout);
    ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default);
}
```

기본 application 표면에서는 session relay, actor context, spot client 같은
도메인 표면을 권장한다. actor runtime 으로 직접 보내는 범용 public client 는
두지 않는다.

direct target helper 는 transport 위치값을 이미 알고 있는 framework 내부
경로에 한해서만 둔다.

### 5.6 IZLinkBoundSession

actor handler 가 현재 client session 으로 push 를 보낼 때 사용하는 client 다.
client 에게 새 request 를 보내는 API 는 제공하지 않는다. client request 에 대한
응답은 actor request handler 의 반환값으로 처리한다.

`IZLinkBoundSession` 자체는 연결된 client stream 을 향한 proxy 이므로
`Zlink.Framework.Contracts.Streams` 에 둔다. 다만 이 proxy 를 받는
`ZLinkActorSendContext` / `ZLinkActorRequestContext` 와 actor handler
인터페이스는 actor runtime 이 선택하고 실행하므로
`Zlink.Framework.Contracts.Actors` 에 둔다.

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
    IZLinkBoundSessionSendCall PacketName(string packetName);

    IZLinkBoundSessionSendCall Metadata(
        string key,
        string value);

    ValueTask Submit(CancellationToken cancellationToken = default);
}
```

### 5.7 actor/spot remote address resolver와 actor-session binding

public resolver 는 두 축으로 제한한다. actor 와 spot 이다.

- actor resolver: actor id 로부터 actor runtime route 를 조회한다.
- spot resolver: spot name 이나 spot rid 로부터 user Spot route 를 조회한다.

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
namespace Zlink.Framework.Contracts.Spots;

public interface IZLinkSpotRemoteAddressResolver
{
    ValueTask<ZLinkSpotRemoteAddress> ResolveSpotRemoteAddressAsync(
        string spotName,
        CancellationToken cancellationToken);

    ValueTask<ZLinkSpotRemoteAddress> ResolveSpotRemoteAddressAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken);
}

public enum ZLinkSpotKind
{
    Invalid = 0,
    Entry = 1,
    User = 2,
}

public readonly record struct ZLinkSpotRemoteAddress(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    RoutingId SpotRid,
    ZLinkSpotKind SpotKind);
```

`RouterChannelId`는 실제 router-capable channel 이름이다. 이 값이 가리키는 channel은
`AddClientServerChannel(...)`의 server `ROUTER`이거나 `AddRouteMeshChannel(...)`의
route mesh `ROUTER`여야 한다. target `SpotNode`는 같은 이름을
`AcceptSpotRoutesFromChannel(...)`로 수락해야 하며, resolver는 연결을 만들지 않는다.

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
분명히 하기 위해서다. 해당 표면들은 `AddClientServerChannel(...)`,
`AddFanoutChannel(...)`, `AddSpotMesh(...)`, `UseDiscovery(...)`,
`UseFilter(...)` 다.

SPOT discovery 와 node 집합은 `AddSpotMesh(...)` 안에서 함께 등록한다.
이렇게 하면 channel view 의 소유자가 하나로 고정되어 node 등록 순서나
분리 호출 여부가 의미에 영향을 주지 않는다.

channel discovery 의 등록 위치는 다음과 같이 정해 둔다.

- channel discovery 는 capability 별 builder 아래에 중복으로 두지 않는다.
  framework 등록 루트에 한 번만 둔다.
- 이 discovery registration 의 의미는 다음과 같다. framework 안의
  discovery 기반 channel capability 들이 공유하는, registry endpoint 집합
  을 가리킨다.
- 반대로 manual 연결은 capability 별 runtime 설정에 해당한다. 그래서 각
  capability builder 아래에 둔다.

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
        Action<IZLinkRoutePolicyOptions> configure);
}

public interface IChannelClientCapabilityBuilder
{
    void ConfigureSocket(
        Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(
        Action<IZLinkOutboundRoutePolicyOptions> configure);

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

    void ConfigureRouting(Action<IZLinkRoutePolicyOptions> configure);

    void UseManualConnections(Action<IRouteChannelConnections> configure);

    void AddHandlerGroup(string groupName);

    void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>;

    void AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>;

    void AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;

    void EnableSpotRouteEgress(string targetSpotNodeChannelName);
}

public interface IZLinkStreamNodeBuilder
{
    void Bind(string endpoint);

    void RegisterSession<TSession>()
        where TSession : class, IZLinkSession;
}

public interface IZLinkSessionPacketHandler<TSessionContext>
{
    string PacketName { get; }

    ValueTask HandleAsync(
        TSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionPacketDispatcher<TSessionContext>
{
    ValueTask<bool> TryHandleAsync(
        TSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default);
}

session packet dispatcher 는 등록된 packet handler 호출만 담당한다. 미등록 packet 을
actor 로 relay 할지, 무시할지, 오류로 처리할지는 session 구현체의 정책이다.
handler 로 전달되는 payload 는 `OnDispatchAsync(...)` 의 payload 와 같은 borrowed
lifetime 이므로 handler 는 직접 해제하거나 `Move()` 로 소비하지 않는다.

public interface IZLinkClientServerChannelBuilder
{
    void EnableServer(
        Action<IChannelServerCapabilityBuilder>? configure = null);

    void EnableClient(
        Action<IChannelClientCapabilityBuilder>? configure = null);

    void AddHandlerGroup(string groupName);

    void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;

    void AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;

    void AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;

    void EnableSpotRouteEgress(string targetSpotNodeChannelName);
}

public interface IZLinkFanoutChannelBuilder
{
    void EnablePublisher(
        Action<IChannelPublisherCapabilityBuilder>? configure = null);

    void EnableSubscriber(
        Action<IChannelSubscriberCapabilityBuilder>? configure = null);

    void AddHandlerGroup(string groupName);

    void AddPublishHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkPublishHandler<TMessage>;

    void AddPublishHandler<THandler>(string? packetName = null)
        where THandler : class;
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

    void ConfigureRouting(Action<IZLinkRoutePolicyOptions> configure);

    void UseManualConnections(Action<IRouteChannelConnections> configure);
}

public interface IZLinkFrameworkOptions
{
    TimeSpan DefaultTimeout { get; set; }

    IZLinkCodecRegistryBuilder Codecs { get; }

    void ConfigureMetadata(Action<IZLinkMetadataPolicyBuilder> configure);

    void AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory;

    void AddSpotRemoteAddressResolver<TResolver>()
        where TResolver : class, IZLinkSpotRemoteAddressResolver;

}
```

`DefaultTimeout`의 기본값은 30초다.

각 함수의 의미는 다음과 같다.

- `DefaultTimeout`
  - request 호출이 별도 timeout을 지정하지 않았을 때 쓰는 framework 기본값이다.
- `Codecs`
  - protobuf/json/messagepack 같은 codec provider를 framework registry에 등록하는
    진입점이다.
- `ConfigureMetadata(...)`
  - session actor dispatch와 bound session 경로에서 전달할 application metadata key를
    등록한다.
- `AddActorFactory(...)`
  - actor type 문자열에 대응하는 actor factory를 등록한다.
- `AddSpotRemoteAddressResolver(...)`
  - `IZLinkSpotClient`나 `JoinSpot(spotRid, ...)`이 spot rid로 user
    Spot route를 찾을 때 사용할 resolver를 등록한다.
- actor-session binding
  - 별도 public registration 함수로 등록하지 않는다. stream session이 actor handle을
    만들거나 actor에 attach되면 framework/core가 binding 상태를 갱신하고,
    `IZLinkBoundSession`는 그 상태를 사용해 client stream으로 보낸다.
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
- `AddRouteMeshChannel(...)`
  - route mesh 채널을 등록한다. bind endpoint, socket option, routing option,
    manual connection을 한 builder 안에서 함께 설정한다.
- `UseDiscovery(...)`
  - 일반 channel capability들이 공유할 registry endpoint 집합을 등록한다.
  - `client.UseDiscovery(...)`처럼 capability 아래에 다시 두지 않는다.
- `UseFilter<TFilter>()`
  - handler filter 타입을 framework pipeline에 등록한다.
- `AddSpotMesh(...)`
  - 여러 `SpotNode`가 같은 SPOT mesh discovery view를 공유하도록 묶어 등록한다.
    mesh builder는 자체 `UseDiscovery(...)`와 `AddNode(spotNodeName, ...)`를
    노출한다.
    mesh node builder는 `EnableRouter`, `EnablePubSub`,
    `AttachClientServerChannelClient`, `AttachSpotMeshPublisherClient`,
    `AddSpotFactory<TSpot>(...)`, `AddEntrySpot<TEntrySpot>()`를 노출한다.
    ActorGateway 는 별도 node builder 를 갖지 않고, stream 이 router capability
    를 켠 SpotNode 를 `AttachActorGateway(...)` 로 참조한다.
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

중요한 규칙은 다음과 같다.

- 수동 연결은 `channel` 전체가 아니라 `channel + capability` 단위다.
- manual `Connect(...)` 는 startup 과 런타임 제어 모두 endpoint 만 인자로
  받는다.
- 같은 capability 안에서 `Discovery` 와 manual 연결을 섞지 않는다.
- `client` 와 `subscriber` 는 서로 다른 연결 집합으로 본다.
- publisher 는 outbound fan-out submit capability 로 간주한다. 이 초안
  에서는 publisher 에 대해 별도의 manual peer 관리 표면을 두지 않는다.

### 6.2 channel 연결 관리

위 규칙에 따라 manual capability 를 런타임에서 제어하려면, 별도의 관리
표면이 필요하다.

startup builder 에서 사용하는 `UseManualConnections(...)` 는 단순 등록
용도이므로, 동기 `Connect(...)` 를 유지한다.

반면 host 가 올라온 뒤 실제 runtime 상태를 변경하는 관리 표면은 다르다.
lazy startup 과 I/O 경계를 가리지 않기 위해 비동기로 설계한다.

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
    ValueTask<IZLinkEndpointConnections> GetClientServerClientAsync(
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetFanoutSubscriberAsync(
        string channelName,
        CancellationToken cancellationToken = default);
}
```

연결 관리 표면 역시 capability 이름을 그대로 드러낸다.
`GetClientAsync(...)` 처럼 여러 capability 를 동시에 암시할 수 있는
일반화된 이름은, public draft 계약에 두지 않는다.

이 interface 는 모든 channel 에 항상 열려 있는 것이 아니다. 해당
capability 가 manual 모드일 때에 한해 유효한 표면으로 본다.

discovery 모드인 capability 는 peer 집합의 소유권이 discovery 에 있다.
그래서 수동 `Connect` / `Disconnect` 를 허용하지 않는다.

### 6.3 Spot 관리와 등록 인터페이스

`IZLinkSpotManager` 는 `SpotNode` 안에서 spot 인스턴스를 생성하고 삭제하는
데 사용하는 interface 다.

역할 분담은 다음과 같다.

- spot 을 만드는 주체는 handler 가 아니라 manager 다.
- handler 는 들어오는 메시지를 처리하는 역할만 맡는다.

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
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        string spotName,
        RoutingId spotRid,
        IReadOnlyList<Message> createParts,
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

`CreateAsync` 와 `GetOrCreateAsync` 는 각각 다음 상황에 대응한다.

- `spotRid`만 받는 생성
  - 등록된 이름으로 factory를 선택하고, runtime이 새 `spotRid`를 발급한다.
- `spotName + createParts`
  - runtime이 새 `spotRid`를 발급하고, multipart create payload를
    `IZLinkSpot.OnCreateAsync(...)`에 전달한다.
- `spotName + RoutingId spotRid + createParts`
  - 등록된 이름으로 factory를 선택하되, 호출자가 특정 logical spot rid를 직접
    지정한다. 이미 같은 `spotRid`가 있으면 기존 spot을 반환하고 새
    `createParts`는 전달하지 않는다.

반환값은 세 가지를 묶어서 돌려준다. `spotRid`, `spotRid`, 그리고 새로
생성된 인스턴스인지 여부다.

장기적으로 들고 다닐 instance handle 이 아니라, 생성 결과만 돌려주는
형태라는 점에 주의한다.

`OnCreateAsync(...)` 는 spot 생성 요청의 multipart payload를 받는 lifecycle
callback이다. framework는 caller가 넘긴 part 수와 순서를 유지해서 전달해야 하고,
여러 part를 하나의 serialized envelope로 합치면 안 된다. `CreateAsync(spotName)`
처럼 create payload가 없는 overload는 빈 list를 넘긴 것과 같다. `createParts`
인자 자체는 `null`이면 안 된다. 같은 `spotRid`에 대해 동시에
`GetOrCreateAsync(...)`가 들어오면 처음 생성에 사용된 payload만
`OnCreateAsync(...)`로 전달된다. 나중 caller의 payload는 재전달하지 않는다.

`spotRid`은 생성 요청의 framework type discriminator다. framework는 이 이름으로
등록된 factory를 선택한다. CLR type name이나 assembly-qualified type name은 요청
계약에 포함하지 않는다. 같은 `spotRid`에 대해 기존 entry의 `spotRid`과 다른
`spotRid`으로 `GetOrCreateAsync(...)`를 호출하면 `SpotTypeMismatch`로 실패해야
한다.

remote 생성 요청도 같은 구조를 따른다. framework metadata에는 `spotRid`과
선택적인 `spotRid`가 들어가고, metadata 뒤의 message part들이 create payload가
된다. `spotRid`는 `GetOrCreateAsync(...)`처럼 명시적 logical spot을 확보하는
요청에서는 required이고, 수신 node가 새 id를 발급하는 create 요청에서는 optional이다.
`spotRid`은 payload 안에 넣지 않는다. payload codec을 해석하기 전에도 factory를
선택할 수 있어야 하기 때문이다.

`GetAsync(...)` 와 `ListAsync(...)` 는 조회 표면이다. runtime 이 보유한
`spotRid -> spotName` 매핑을, 운영 코드에서 다시 들여다볼 수 있게 해 준다.

이 조회가 필요한 이유는 다음과 같다. 같은 `SpotNode` 에 여러 spot factory
를 등록할 수 있다. 그래서 어떤 `spotRid` 가 어떤 이름으로 생성되었는지를,
외부에서 확인할 수 있어야 하기 때문이다.

현재 SPOT topology 초안에서는 high-level public surface 에
`spotRid -> targetRid` 주소를 직접 노출하지 않는다.

주소 해석은 `IZLinkSpotRemoteAddressResolver` 가 담당한다. framework 의 기본
SPOT 표면은 다음 순서로 설명한다. spot name/id, channel publish, channel
send/request 다.

`SPOT` registration 자체는 별도의 builder 로 설명하는 편이 자연스럽다.
현재 스펙에서 잡는 최소 표면은 다음과 같다.

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

public interface IZLinkRoutePolicyOptions
{
    bool RequireKnownPeer { get; set; }

    bool AllowPeerHandover { get; set; }

    bool EnablePeerProbe { get; set; }
}

public interface IZLinkOutboundRoutePolicyOptions
{
    bool ProbeRouterOnConnect { get; set; }
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
    void Bind(string endpoint);

    void ConfigureSocket(
        Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(
        Action<IZLinkRoutePolicyOptions> configure);

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
        Action<IZLinkOutboundRoutePolicyOptions> configure);

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

    void AttachClientServerChannelClient(
        string channelName,
        Action<ISpotChannelClientCapabilityBuilder>? configure = null);

    void AttachSpotMeshPublisherClient(
        string channelName,
        Action<ISpotPublisherClientCapabilityBuilder>? configure = null);

    void ConfigureEntrySpot(
        Action<IZLinkEntrySpotOptions> configure);

    void AddSpotFactory<TSpot>(string spotName)
        where TSpot : class;

    void AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot;
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
        where TSpot : class;

    void ConfigureEntrySpot(
        Action<IZLinkEntrySpotOptions> configure);

    void AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot;
}

public interface IZLinkEntrySpotOptions
{
    RoutingId RoutingId { get; set; }
}
```

각 함수의 의미는 아래와 같다.

- `EnableRouter(...)`
  - spot-to-spot routed packet을 처리할 local router capability를 켠다.
- `EnablePubSub(...)`
  - 현재 SPOT channel 안의 publish/subscribe capability를 켠다.
- `AttachClientServerChannelClient(...)`
  - 다른 channel로 send/request 할 outbound `DEALER(client)` 경로를 붙인다.
- `AttachSpotMeshPublisherClient(...)`
  - local spot 인스턴스가 없는 외부 노드가 특정 SPOT channel로 publish할
    outbound publisher client를 붙인다.
- `AddSpotFactory<TSpot>(spotName)`
  - 이 node가 생성하고 소유할 spot factory를 이름과 함께 등록한다.
  - 같은 `SpotNode` 안에서는 `spotRid`이 비어 있으면 안 된다.
  - 이미 등록된 `spotRid`을 다시 등록하면 기존 값을 덮어쓰지 않고 예외를 던진다.
  - `CreateAsync(spotName, ...)`와 `GetOrCreateAsync(spotName, ...)`는 이 이름과
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

여기서 수동 연결은 channel 쪽과 마찬가지로 capability 단위로 다뤄야 한다.

예를 들어 `router`, channel client, publish 쪽은 모두 각 capability 가
사용할 endpoint 집합을 따로 관리한다.

이 문서에서는 manual `Connect(...)` 시점에 remote router id 를 별도
파라미터로 받지 않는다. 따라서 `UseManualConnections(...)` 도 한군데에
모아 두지 않고, capability builder 별로 분리해 두는 편이 자연스럽다.

소켓 옵션 역시 같은 방식으로 소유자를 나눠서 설명한다.

- `ConfigureSocket(...)`
  - 실제 `.NET` 바인딩의 `CommonSocketOptions`와 같은 공통 socket facade를
    capability 아래에 노출하는 모델이다.
- `ConfigurePublisherOptions(...)`, `ConfigureSubscriberOptions(...)`
  - 실제 `SpotNode.PublisherOptions`, `SpotNode.SubscriberOptions`와 같은
    `SPOT` pub/sub 전용 facade를 framework 등록 쪽으로 끌어올린다.
- `ConfigureRouting(...)`
  - capability가 routed peer와 맺는 연결 규칙을 따로 설정한다.
  - framework public 표면에서는 remote `RoutingId`를 설정하지 않는다. discovery 기반
    경로는 resolver와 discovery registry가 위치값을 소유하고, manual 연결은 endpoint
    집합만 소유한다.
- `Timeout(...)`
  - request 한 번에만 적용되는 호출 단위 옵션이다.
  - 실제 바인딩에서도 `DealerSocket.RequestAsync(..., TimeSpan timeout, ...)`,
    `RouterSocket.RequestAsync(..., TimeSpan timeout, ...)`,
    `Spot.RequestChannelAsync(..., TimeSpan timeout, ...)`처럼 호출 인자로 받는다.
  - 위 등록 설정과 달리 capability runtime 기본값을 바꾸지 않는다.

`AddSpotMesh(channelName, ...)` 는 SPOT channel 이름과 node 묶음을 함께
소유한다. 그래서 `AddNode(...)` 안에서 같은 channel 이름을 다시 받는
함수는 두지 않는다.

ActorGateway 도 같은 원칙을 따른다. 별도 `AddActorGatewayNode(...)` 표면을 두지
않고, `AddNode(...)` 로 등록한 SpotNode 에 `EnableRouter(...)` 와 router
`Bind(...)` 를 설정한 뒤 stream 이 `AttachActorGateway(spotNodeName)` 으로
그 local ingress node 를 참조한다.

정리하면, `SPOT` 등록 시점에도 다음 축들을 함께 드러내는 편이 맞다.

- local routed router capability 활성화
- local SPOT pub/sub capability 활성화
- 외부 channel 호출용 client attach
- 외부 SPOT publish client attach

### 6.4 Spot 연결 관리

SPOT 역시 수동 연결을 사용할 때는, capability 별 런타임 제어 표면이
필요하다.

```csharp
public interface IZLinkSpotConnectionManager
{
    ValueTask<IZLinkEndpointConnections> GetRouterAsync(
        string spotNodeName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetPubSubAsync(
        string spotNodeName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetClientServerChannelClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetSpotMeshPublisherClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default);
}
```

Spot 연결 관리 표면 역시 capability 이름을 그대로 드러낸다. 같은
capability 를 가리키는 일반화된 이름 alias 는, public draft 계약에 두지
않는다.

이 관리 interface 또한 모든 node 에 항상 열려 있는 것이 아니다. 해당
capability 가 manual 모드일 때에 한해 유효한 표면으로 본다.

## 7. Timer 인터페이스

이 절은 spot lifecycle 에서 사용하는 timer 의 공개 표면을 정의한다.

현재 스펙에서는 spot lifecycle 안에 등록한 `Context.AddTimer<THandler>(...)`
가 반환하는 timer handle 이다.

```csharp
public interface IZLinkTimer : IAsyncDisposable
{
    bool IsDisposed { get; }

    ValueTask CancelAsync(
        CancellationToken cancellationToken = default);
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
경로 (`ExecuteSerializedAsync(...)` 같은 것) 로 들어가고, Entry Spot timer 는
Entry Spot 전체 직렬 실행 줄에 묶이지 않는다. 그곳에서
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
- Entry Spot timer callback 도 Entry Spot 전체 실행 줄에 묶지 않는다. 다만
  같은 timer instance 안에서는 이전 callback 이 끝나기 전에 다음 callback 을
  겹쳐 실행하지 않는다.

## 8. Handler Filter

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
    .Request("profile", new GetProfileRequest { AccountId = accountId })
    .SubmitAsync<GetProfileReply>(cancellationToken);
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

## 10. Registry 조회 인터페이스

Registry 조회 인터페이스는 infrastructure 성격이므로 상세 정의는
[aspnet-core-registry.ko.md](./aspnet-core-registry.ko.md)의 section 7에 있다.
여기서는 역할만 요약한다.

### 10.1 IZLinkRegistryQuery

같은 프로세스 안의 embedded Registry 를 조회하는 interface 다.

등록 시점과 제공 내용은 다음과 같다.

- `AddZLinkRegistry(...)` 시점에 자동으로 DI 에 등록된다.
- status, service summary, topology, member peers 를 제공한다.

조회 API 가 비동기인 이유는 두 가지다. registry 가 아직 시작되지 않은
상태일 수 있고, snapshot 수집도 host lifecycle 과 맞물려 있기 때문이다.

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

`MemberPeersAsync(...)` 는 `channelName` 하나만 인자로 받는다.

이전에 있던 `(ZLinkServiceType serviceType, string serviceName, ...)`
형태는 더 이상 사용하지 않는다. service type/name 구분 대신, channel 이름
자체가 member peer 집합의 단위가 된다.

### 10.2 IZLinkRegistryQueryClient

다른 프로세스의 Registry 를 원격 조회하는 interface 다.

등록은 `AddZLinkRegistryQueryClient(...)` 로 별도로 한다. topology
snapshot 만 제공한다.

원격 요청이라는 특성상, 이 interface 역시 비동기로 설계한다.

### 10.3 runtime monitoring

runtime monitoring 은 운영 표면이다. socket 하부 monitor 와, registry/spot
의 snapshot diff 를 함께 감싸는 역할이다.

공용 handler shape 는 다음과 같이 두는 편이 자연스럽다.

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

이유는 단순하다. enum 만으로는 source name, routing id, endpoint, snapshot
같은 운영 정보를 한꺼번에 전달하기 어렵기 때문이다.

native monitor enum 과 raw status 값에 대해서도 비슷한 원칙을 적용한다.
즉 framework 가 항상 보장하는 필수 계약이 아니라, backend 가 제공할 수
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
    SubjectsChanged,
    TimerHandlerFailed,
    TimerStoppedAfterUnhandledException
}

public readonly record struct ZLinkSpotTimerDiagnostic(
    RoutingId SpotRid,
    string SpotName,
    bool IsEntrySpot,
    string TimerName,
    string HandlerType,
    ulong DeliveryIndex,
    ulong ScheduledIndex,
    string ExceptionType,
    string ExceptionMessage);

public readonly record struct ZLinkSpotEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkSpotEventKind Event,
    ZLinkSpotNodeStatus? Status,
    IReadOnlyList<ZLinkSpotNodePeerEntry>? Peers,
    IReadOnlyList<ZLinkSpotNodeSubjectEntry>? Subjects,
    ZLinkSpotTimerDiagnostic? TimerDiagnostic = null)
    : IZLinkRuntimeEvent;
```

`TimerHandlerFailed` 와 `TimerStoppedAfterUnhandledException` 은 polling interval 을
기다리는 snapshot diff event 가 아니다. timer handler 에서 처리되지 않은 예외가
발생한 시점에 즉시 발행된다. exception 객체 자체는 public payload 에 넣지 않고,
`ZLinkSpotTimerDiagnostic` 에 직렬화 가능한 요약 정보를 담는다.

`ZLinkSpotNodeStatus` 와 `ZLinkSpotNodePeerEntry` 의 첫 번째 필드는
`ChannelName` 이다.

예전에는 `ServiceName` 이라는 이름을 썼다. 다만 channel 단위로 통일하면서
`ChannelName` 으로 rename 되었다.

이 두 record 를 필드 단위로 풀어 쓰는 다른 문서들도, 이 이름을 기준으로
참고하면 된다.

이 문서에서 각 source 의 의미는 다음과 같이 정리한다.

- socket event
  - 하부 `SocketMonitor` 를 감싸는 표면이다.
  - source 이름은 `channel + capability` 또는 `spot node + capability`
    형태가 자연스럽다.
  - 예: `profile.server`, `profile.client`, `stage-node.router`
- discovery state
  - runtime event 로 올리지 않는다.
  - 현재 provider 상태는 registry topology/service/member snapshot 으로
    조회한다.
- registry event
  - 하부 raw monitor 가 아니다. 다음 호출들의 polling + diff 로 합성한다.
    `StatusSnapshotAsync()`, `TopologySnapshotAsync()`,
    `ServiceSummarySnapshotAsync()`.
- spot event
  - 하부 raw monitor 가 아니다. 다음 호출들의 polling + diff 로 합성한다.
    `StatusSnapshot()`, `PeersSnapshot()`, `SubjectsSnapshot()`.

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

options.AddClientServerChannel("api", channel =>
{
    channel.EnableServer(server => server.Bind("tcp://0.0.0.0:7101"));
    channel.AddHandlerGroup("api");
});
```

동일한 handler 그룹을 여러 channel 에 매핑하는 것은 허용한다.

다만 같은 channel 안에서 동일 `kind + packet name` 이 둘 이상으로 해석되는
상황은 허용하지 않는다. 같은 그룹 안에서의 충돌이든, 서로 다른 그룹의
충돌이 한 channel 에 함께 묶이는 경우든, 모두 startup validation 오류로
처리한다.

attribute scan 용 보조 표면도 둘 수 있다. 예를 들어
`MapHandlersFromAssemblyContaining<TMarker>()` 나
`MapHandlersFromAssembly(...)` 같은 것들이다. 빠른 prototype 단계이거나,
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

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotActorJoinAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotPostActorJoinedAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotActorLeftAttribute : Attribute
{
}
```

method 시그니처는 아래 순서를 따른다.

- send: `(spotOrEntrySpot, actor, message, CancellationToken)` 반환값 없음
- request: `(spotOrEntrySpot, actor, request, CancellationToken)` reply 반환
- actor join: `(spot, actor, request, CancellationToken)` reply 반환
- joined/left: `(spotOrEntrySpot, actor, ZLinkSpotActorLifecycleContext, CancellationToken)` 반환값 없음

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
(`IZLinkEventPublisher.Publish(...)`) 와 맞추기 위해서다. 그래야
`[ZLinkRequest]` / `[ZLinkSend]` / `[ZLinkPublish]` 세 표면이 동일한
패턴으로 읽힌다.

publish handler 도 모든 subscriber channel 에 전역으로 자동 노출되지
않는다. subscriber capability 를 가진 channel 이라 해도, 노출할 그룹은
`AddHandlerGroup(...)` 으로 명시해야 한다.

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
    channel.AddHandlerGroup("api.events");
});
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

### 11.5 stream

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

stream 은 framework Header 기반의 packet session 을 하나의 축으로 본다.
recv 방식은 현재 스펙 범위에서 제외한다.

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

- 일반 channel messaging 의 실행 문맥은 inbound channel capability 다.
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
  scope 에서 resolve 한다. registration 함수는 handler 타입만 받는 편이
  자연스럽다.
- 즉 `Context.AddPacket<THandler>()`, `Context.AddTimer<THandler>(...)`
  같은 표면은 service locator 가 아니다. "이 타입을 spot scope 에서 사용해
  달라" 는 등록 선언으로 읽는 편이 맞다.
- `OnCreateAsync(...)` 와 `OnInitializeAsync(...)` 도 마찬가지다.
  `IServiceProvider` 를 직접 받지 않고, spot 자체의 constructor injection 과
  cached dependency 를 사용하는 편이 좋다. 그래야 hot path 와의 경계가 더
  분명해진다.

### 13.1 public service DI 등록 조건

모든 public service interface 를 항상 DI 에 등록하지는 않는다. 생성자 주입은
그 기능을 사용할 수 있다는 신호가 되므로, capability 가 없는 service 는 등록하지
않는다. 자세한 결정 배경은
[di-capability-exposure-policy.ko.md](../internals/di-capability-exposure-policy.ko.md) 에서
다룬다.

| Interface | DI 등록 조건 |
|-----------|--------------|
| `IZLinkClient`, `IZLinkClientServerClient` | 항상 등록한다. channel 누락은 호출 시 `ZLinkConfigurationException` 으로 처리한다 |
| `IZLinkRouteClient` | 항상 등록한다. route channel 누락은 호출 시 `ZLinkConfigurationException` 으로 처리한다 |
| `IZLinkEventPublisher`, `IZLinkFanoutPublisher` | 항상 등록한다. publisher capability 누락은 호출 시 `ZLinkConfigurationException` 으로 처리한다 |
| `IZLinkChannelConnectionManager` | 항상 등록한다. 대상 channel capability 누락은 호출 시 `ZLinkConfigurationException` 으로 처리한다 |
| `IZLinkSpotManager`, `IZLinkSpotClient`, `IZLinkSpotConnectionManager` | `SpotNode` 가 하나 이상 있을 때 등록한다 |
| `IZLinkRoutedSpotClient` | client-server channel 또는 route mesh channel 중 `EnableSpotRouteEgress(...)`가 켜진 local egress channel 이 하나 이상 있을 때 등록한다 |
| `IZLinkSpotPublisherClient`, `IZLinkSpotMeshPublisherClient` | Spot publisher client capability 가 하나 이상 있을 때 등록한다 |
| `IZLinkActorManager` | `SpotNode` 와 actor factory 가 모두 있을 때 등록한다 |
| `IZLinkBoundSessionFactory`, `IZLinkBoundSession` | actor bound session runtime 등록한다 |
| `IZLinkSpotRemoteAddressResolver` | 해당 resolver registration 이 있을 때 등록한다 |

local handler 가 붙는 channel 의 의미는 다음과 같다. route prefix 가
아니라, 애플리케이션이 해당 channel 에서 server 역할을 수행한다는 의미다.

channel 이름의 위치도 정해 둔다. handler class 나 method attribute 가
아니라, channel registration 에 둔다.

예: `options.AddClientServerChannel("api", channel => channel.EnableServer(...))`

다만 outbound-only 앱이라면, server capability 를 가진 channel 이 아예
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
- `Zlink.Framework` runtime 은 `IZLinkClient` 위에 channel 별 typed
  wrapper 를 공식 기본 표면으로 제공하지 않는다. typed wrapper 가 필요하
  다면, 응용 측이나 별도 확장 패키지가 `IZLinkClient` 위에 얹는 방식을
  기본으로 본다.
- `spotRid` 타입은 `RoutingId` 를 사용한다. transport `RoutingId` 와
  logical spot rid 를 같은 타입으로 노출하지 않는다.
- `IZLinkRegistryQuery` 와 `IZLinkRegistryQueryClient` 는 묶지 않는다.
  in-process 조회와 원격 조회는 lifecycle, 실패 모델, 제공 범위가 다르기
  때문이다. 그래서 별도의 interface 로 유지한다.

## 15. 회귀 테스트

이 절은 이 문서의 interface 정의를 보호하는 회귀 테스트들을 가리킨다.

이 문서의 interface 항목들은 두 가지를 확인해야 한다.

- public surface 가 backend 구현 세부사항을 새어 내지 않는지.
- 등록 · handler · client 표면이 런타임 테스트와 같은 이름을 유지하는지.

interface 설명을 변경하면, 아래 테스트도 함께 조정한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ScaffoldSmokeTests.PublicSurface_DoesNotExpose_BackendConcreteTypes` | framework public API가 허용된 값 타입 외의 backend concrete type을 직접 노출하지 않는다. |
| `ScaffoldSmokeTests.PublicSurface_Removes_DirectRouteContracts_And_Exposes_ActorContracts` | direct route 계약은 빠지고 actor/session 계약은 public surface에 남아 있다. |
| `RegistryAndMonitoringTests.AddZLinkFramework_RegistersValidatedConfigurationAndFilterTypes` | options, codec, filter, channel, stream, spot 등록 표면이 DI 등록 결과에 반영된다. |
| `FiltersAndHttpTests.Filters_Run_In_Registration_Order_Around_Handler_Dispatch` | handler filter 인터페이스가 등록 순서대로 dispatch 앞뒤를 감싼다. |
| `HandlerResultAwaiterTests.AwaitAsync_Returns_ValueTaskOfT_Result` | `ValueTask<T>` handler 결과를 값 타입 boxing 여부와 무관하게 기다리고 실제 reply 값을 반환한다. |
| `ProtocolTests.ActorPacketRegistry_DoesNot_Resolve_Request_To_Send_Handler` | actor request packet 이 send handler 로 fallback dispatch 되지 않고, send/request 밖 stream kind 도 actor packet 으로 처리되지 않는다. |
| `ProtocolTests.SpotActorRegistry_DoesNot_Resolve_Request_To_Send_Handler` | Entry Spot/user Spot actor request packet 이 send handler 로 fallback dispatch 되지 않고, send/request 밖 stream kind 도 actor packet 으로 처리되지 않는다. |
| `LocalSessionRelayTests.LocalSessionActorDispatch_Relays_Stream_Request_And_Replies_From_Request_Handler` | local actor relay 도 request handler 반환값으로 stream response 를 작성한다. |
| `ScaffoldSmokeTests.PublicSurface_Removes_ActorReply_And_StreamClientContracts` | actor context Reply 와 actor stream client 계약이 public surface 에 다시 노출되지 않는다. |

[^public-contract]: 라이브러리가 외부에 약속한 공식 API. 한 번 공개되면 호환성을 깨지 않고는 변경하기 어렵다.
[^transport]: 메시지가 실제로 네트워크나 IPC 위에서 오가는 하부 계층. ZLink에서는 socket, stream, route 등이 이에 해당한다.
