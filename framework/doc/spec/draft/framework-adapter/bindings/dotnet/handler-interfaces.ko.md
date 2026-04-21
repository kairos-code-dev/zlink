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
| handler | `IZLinkRequestHandler<TRequest, TResponse>` | request-response handler | 4.1 |
| handler | `IZLinkSendHandler<TMessage>` | one-way send handler | 4.2 |
| handler | `IZLinkEventHandler<TEvent>` | pub/sub event handler | 4.3 |
| handler | `IZLinkPacketStreamSession` | packet stream session lifecycle + packet callback | 4.4 |
| handler | `IZLinkRawStreamSession` | raw stream session lifecycle + raw callback | 4.4 |
| handler | `IZLinkRuntimeEventHandler<TEvent>` | runtime monitoring event handler | 10.3 |
| stream | `IZLinkStream` | stream I/O와 peer 식별 | 4.4 |
| value | `ZLinkStreamSessionError` | stream session error category enum | 4.4 |
| value | `ZLinkStreamError` | stream error detail + errno helper | 4.4 |
| value | `ZLinkSocketEventKind`, `ZLinkSocketEvent` | socket runtime event | 10.3 |
| value | `ZLinkDiscoveryEventKind`, `ZLinkDiscoveryEvent` | discovery runtime event | 10.3 |
| value | `ZLinkRegistryEventKind`, `ZLinkRegistryEvent` | registry runtime event | 10.3 |
| value | `ZLinkSpotEventKind`, `ZLinkSpotEvent` | spot runtime event | 10.3 |
| options | `IZLinkMonitoringOptions` | runtime monitoring source 등록 옵션 | 10.3 |
| serializer | `IZLinkMessageSerializer` | `Message` payload 직렬화/역직렬화 | 4.5 |
| client | `IZLinkClient` | 서버 간 outbound client | 5.1 |
| client | `IZLinkSpotClient` | SPOT outbound client | 5.2 |
| client | `IZLinkSpotPublisherClient` | spot channel publish client | 5.3 |
| client | `IZLinkEventPublisher` | pub/sub event publisher | 5.4 |
| management | `IZLinkChannelConnectionManager` | channel capability별 수동 연결 제어 | 6.1 |
| management | `IZLinkSpotManager` | spot 인스턴스 생성/삭제 | 6.2 |
| management | `IZLinkSpotConnectionManager` | spot capability별 수동 연결 제어 | 6.3 |
| timer | `IZLinkTimer` | timer handle | 7 |
| filter | `IZLinkHandlerFilter` | handler 전후 공통 처리 | 8 |
| marker | `IZLinkRequest<TReply>` | request 타입 marker | 9 |
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
    IServiceProvider Services { get; }
    CancellationToken ConnectionAborted { get; }
}
```

### 3.2 파생 context

| context 타입 | 사용처 | 추가 정보 |
|-------------|--------|----------|
| `ZLinkRequestContext` | request-response handler | caller metadata, timeout |
| `ZLinkSendContext` | one-way send handler | caller metadata |
| `ZLinkEventContext` | event handler | topic, source |
| `ZLinkSpotRequestContext` | SPOT request handler | self spot info, source rid, source spot rid |
| `ZLinkSpotSubscriptionContext` | SPOT subscription handler | self spot info, topic, source rid, dispatch metadata |
파생 context의 상세 필드는 구현 전에 더 좁혀야 한다.
현재 초안에서는 이름과 역할만 고정한다.

`SPOT` 쪽에서는 외부 lookup과 별개로, 현재 spot 자신에 대한 identity 조회도
가능해야 한다. 최소 초안은 아래 정도가 자연스럽다.

```csharp
public interface IZLinkSpotSelf
{
    RoutingId SpotRid { get; }
    RoutingId NodeRid { get; }
}
```

예를 들어 `ZLinkSpotRequestContext`와 `ZLinkSpotSubscriptionContext`는 아래처럼
현재 spot 자신을 읽을 수 있는 표면을 가질 수 있다.

```csharp
public interface IZLinkSpotContext : IZLinkHandlerContext
{
    IZLinkSpotSelf Self { get; }
}
```

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

### 4.3 event handler

pub/sub 이벤트를 처리하는 handler다.

```csharp
public interface IZLinkEventHandler<in TEvent>
{
    ValueTask HandleAsync(
        TEvent message,
        ZLinkEventContext context,
        CancellationToken cancellationToken);
}
```

topic/pattern이 중요한 경우 아래 후보도 검토 중이다.

```csharp
public interface IZLinkTopicHandler<in TEvent>
{
    ValueTask HandleAsync(
        TEvent message,
        ZLinkTopicContext context,
        CancellationToken cancellationToken);
}
```

`IZLinkEventHandler`와 `IZLinkTopicHandler` 중 최종 이름은 미정이다.

### 4.4 stream session

stream은 packet path와 raw path를 나눌 수 있지만, 둘 다 session lifecycle 위에서
설명하는 방향을 기본으로 본다. 즉 `STREAM` application 표면은 별도
`ZLinkStreamContext`보다 `IZLinkStream` 객체를 중심으로 본다.

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

    bool Write(
        Message header,
        Message body,
        SendFlags flags = SendFlags.None);
}

public enum ZLinkStreamSessionError
{
    Internal = 0,
    TransportError,
    HandshakeFailed
}

public readonly record struct ZLinkStreamError(
    ZLinkStreamSessionError Error,
    int InternalErrno)
{
    public ErrorCode GetErrorCode()
        => ZlinkException.MapErrorCode(InternalErrno);

    public string GetErrorMessage()
        => Zlink.Strerror(InternalErrno);
}

public interface IZLinkPacketStreamSession
{
    ValueTask OnConnectedAsync(
        IZLinkStream stream,
        CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(
        IZLinkStream stream,
        CancellationToken cancellationToken);

    ValueTask OnErrorAsync(
        IZLinkStream stream,
        ZLinkStreamError error,
        CancellationToken cancellationToken);

    ValueTask OnPacketAsync(
        IZLinkStream stream,
        Message header,
        Message body,
        CancellationToken cancellationToken);
}

public interface IZLinkRawStreamSession
{
    ValueTask OnConnectedAsync(
        IZLinkStream stream,
        CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(
        IZLinkStream stream,
        CancellationToken cancellationToken);

    ValueTask OnErrorAsync(
        IZLinkStream stream,
        ZLinkStreamError error,
        CancellationToken cancellationToken);

    ValueTask OnRawAsync(
        IZLinkStream stream,
        Message payload,
        CancellationToken cancellationToken);
}
```

여기서 `Write(...)`는 remote application 처리 완료를 기다리는 비동기 RPC가
아니다. 현재 `.NET zlink` binding이 가진 sync send/write 표면에 맞춰, 현재
session에 raw payload 또는 framed packet을 submit하는 동작으로 본다.

`OnErrorAsync(...)`는 application handler 내부 예외를 받는 callback이 아니다.
이 초안에서는 `SocketMonitor`에서 관찰 가능한 session-correlatable transport 오류만
`ZLinkStreamError`로 다시 올리는 용도로 제한한다.

여기서 `ZLinkStreamSessionError`는 framework가 먼저 보여 주는 오류 분류 enum이다.
이 분류만으로 부족할 때는 `InternalErrno`를 보고 기존 `.NET zlink` 오류 체계를
다시 사용할 수 있어야 한다. 그래서 `ZLinkStreamError`는 아래 편의
함수를 같이 가진다.

- `GetErrorCode()`
  - `InternalErrno`를 `ErrorCode` enum으로 다시 매핑한다.
- `GetErrorMessage()`
  - `InternalErrno`를 사람이 읽을 수 있는 문자열로 바꾼다.

즉 현재 방향은 아래처럼 정리된다.

- packet session
  - `OnPacketAsync(...)`로 framed `header/body`를 받는다.
- raw session
  - `OnRawAsync(...)`로 raw payload chunk를 받는다.
- 공통 lifecycle
  - `OnConnectedAsync(...)`
  - `OnDisconnectedAsync(...)`
  - `OnErrorAsync(...)`

이 초안에서도 recv loop를 application 표면으로 직접 노출하지 않는다.
즉 사용자가 `Recv(...)`로 직접 drain loop를 돌리는 모델보다, framework가 dispatch를
맡고 application은 packet session 또는 raw session을 구현하는 모델을 기본으로 본다.

또한 stream 핫패스에서는 `Message.ToArray()` 같은 추가 복사를 기본 사용법으로 두면
안 된다. `Message.AsReadOnlySpan()` 같은 현재 표면이나, 그 위에 얹는
protobuf/json decode helper가 가능한 한 추가 메모리 할당 없이 동작하도록
설계하는 쪽을 기본 원칙으로 본다.

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

    IZLinkSendCall WithDontWait();

    bool Exec();
}

public interface IZLinkRequestCall<TReply>
{
    IZLinkRequestCall<TReply> WithPacketName(string packetName);

    IZLinkRequestCall<TReply> WithTimeout(TimeSpan timeout);

    ValueTask<TReply> ExecAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkClient
{
    IZLinkSendCall Send<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall<TReply> Request<TReply>(
        string channelName,
        IZLinkRequest<TReply> request);

    IZLinkSendCall SendTo<TMessage>(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message);

    IZLinkRequestCall<TReply> RequestTo<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        IZLinkRequest<TReply> request);
}
```

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

timeout과 non-blocking 구분은 request/send에서 다르게 다룬다.

- `Request(...)`는 reply를 기다리므로 `WithTimeout(...)`을 둘 수 있다.
- `Send(...)`는 응답을 기다리지 않으므로 timeout 설정을 두지 않는다.
- `Publish(...)`도 응답을 기다리지 않으므로 timeout 설정을 두지 않는다.
- `Send(...)` / `SendTo(...)`는 기본적으로 blocking submit이다.
- 필요하면 send/publish builder에서 `WithDontWait()`를 붙여 temporary backpressure
  상황에서 즉시 `false`를 돌려받을 수 있다.
- `SendTo(...)` / `RequestTo(...)`는 caller가 `targetRid`와 `spotRid`를 이미 알고
  있을 때 direct spot routed 호출로 쓴다.

즉 public 호출 감각은 아래처럼 보는 편이 맞다.

```csharp
var reply = await client
    .Request("profile", new GetProfileRequest { AccountId = accountId })
    .WithTimeout(TimeSpan.FromMilliseconds(200))
    .ExecAsync(cancellationToken);

client
    .Send("profile", new RefreshProfileCacheCommand { AccountId = accountId })
    .WithPacketName("profile.refresh-cache")
    .Exec();

bool submitted = client
    .Send("profile", new RefreshProfileCacheCommand { AccountId = accountId })
    .WithDontWait()
    .Exec();

client
    .SendTo(
        targetRid: RoutingId.Parse("01-00-00-00-00-00-00-10"),
        spotRid: RoutingId.Parse("01-00-00-00-00-00-10-01"),
        message: new ReportStageStateCommand { UserCount = 12 })
    .Exec();

GetStageStateReply directReply = await client
    .RequestTo<GetStageStateReply>(
        targetRid: RoutingId.Parse("01-00-00-00-00-00-00-10"),
        spotRid: RoutingId.Parse("01-00-00-00-00-00-10-01"),
        request: new GetStageStateRequest { StageRid = "01-00-00-00-00-00-10-01" })
    .WithTimeout(TimeSpan.FromMilliseconds(200))
    .ExecAsync(cancellationToken);
```

수동 연결 capability를 런타임에서 제어하려면 아래와 같은 별도 management
표면이 필요하다.

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

public interface IZLinkChannelConnectionManager
{
    IChannelClientConnections GetClient(string channelName);

    IChannelSubscriberConnections GetSubscriber(string channelName);
}
```

이 인터페이스는 아무 channel에나 항상 열리는 것이 아니라, 해당 capability가
manual 모드일 때만 유효한 표면으로 보는 편이 맞다. discovery 모드인 capability는
peer 집합을 discovery가 소유하므로 수동 `Connect`, `Disconnect`를 허용하지
않는다.

### 5.2 IZLinkSpotClient

SPOT outbound 호출을 위한 client다.
`IZLinkClient`와 독립된 인터페이스이며, 하부에서 서로 다른 C API를 감싼다.
최신 SPOT topology 초안에서는 high-level public surface에서 `targetRid +
spotRid` routed 호출을 기본으로 두지 않는다. 현재 방향은 아래 세 축이다.

- 현재 SPOT channel 안의 publish/subscribe
- attach된 channel client를 통한 다른 channel send/request
- 필요할 때만 쓰는 spot-to-spot routed 호출

```csharp
public interface IZLinkSpotClient
{
    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall<TReply> RequestChannel<TReply>(
        string channelName,
        IZLinkRequest<TReply> request);

    IZLinkSendCall SendTo<TMessage>(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message);

    IZLinkRequestCall<TReply> RequestTo<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        IZLinkRequest<TReply> request);

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message);
}
```

`IZLinkClient`와의 차이점은 아래와 같다.

- `Publish(topic, ...)`가 있다. SPOT 쪽은 현재 channel 안의 topic publish를
  함께 쓰는 경우가 많으므로 한 인터페이스에 같이 둔다.
- 다른 channel send/request는 attach된 channel client를 통해 푼다.
- `SendTo(...)` / `RequestTo(...)`는 spot-to-spot routed 호출에만 쓴다.
  일반 channel `ROUTER(server)`를 `rid`로 직접 지정하는 용도는 아니다.
- 같은 direct routed 호출 표면을 `IZLinkClient`에도 둘 수 있다. 차이는
  `IZLinkSpotClient`가 local spot 문맥 안에서 이어지는 호출이라는 점이다.
- channel send/request는 일반 `IZLinkClient`와 같은 builder 감각을 따르는 편이
  자연스럽다.
- timer는 callback scheduler로 따로 두지 않고, spot lifecycle 안에서
  `AddTimer<THandler>(name, period, ...)`로 등록하는 한 가지 모델로 설명하는 편이
  더 자연스럽다. 이 부분은 하부 C API 계약(`zlink_spot_timer_new`)을 따른다.

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
public interface IZLinkSpotPublisherClient
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}
```

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

    IZLinkPublishCall WithDontWait();

    bool Exec();
}

public interface IZLinkEventPublisher
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}
```

여기서 두 문자열의 역할은 다르다.

- `channelName`
  - 어느 논리 channel의 `PUB/SUB` mesh에 publish할지 정한다.
- `topic`
  - 그 channel 안에서 어떤 subscriber 집합이 이벤트를 받을지 정한다.

즉 `Publish("profile", "profile.cache-refreshed", evt)`는 `profile` channel 안의
`profile.cache-refreshed` topic으로 fan-out 한다는 뜻이다.

일반 `PUB/SUB` publish도 `Send(...)`와 비슷하게 timeout은 두지 않는다. 대신
필요하면 packet 이름 override와 `WithDontWait()`를 둘 수 있다.

여기서 `Exec()`은 remote peer 처리 완료를 기다리는 뜻이 아니다. framework local
runtime에 send/publish를 맡기는 종결 동작으로 본다. 기본 blocking submit에서는
성공 시 `true`를 돌려주고, `WithDontWait()`를 쓴 경우 temporary backpressure면
`false`를 돌려준다. route-not-ready 같은 다른 submit 실패는 예외로 본다.

## 6. 관리 인터페이스

### 6.1 channel 연결 관리

위 `IZLinkChannelConnectionManager`는 `channel + capability` 단위 manual 연결을
제어하는 별도 관리 표면이다. 현재 초안에서는 아래 규칙을 기본으로 본다.

- `client`와 `subscriber`는 별도 연결 집합이다.
- 같은 capability는 `Discovery` 또는 `Manual` 중 하나만 쓴다.
- manual capability는 startup 등록과 런타임 `Connect` / `Disconnect`를 둘 다
  지원한다.
- `SPOT`은 channel capability 연결 모델과 섞지 않고 별도 topology로 다룬다.

### 6.2 Spot 관리 인터페이스

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

public interface IChannelClientConnections
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

public interface ISpotRouterCapabilityBuilder
{
    void UseManualConnections(
        Action<ISpotRouterConnections> configure);
}

public interface ISpotPubSubCapabilityBuilder
{
    void UseManualConnections(
        Action<ISpotPublisherConnections> configure);
}

public interface ISpotPublisherClientCapabilityBuilder
{
    void UseManualConnections(
        Action<ISpotPublisherConnections> configure);
}

public interface ISpotChannelClientCapabilityBuilder
{
    void UseManualConnections(
        Action<IChannelClientConnections> configure);
}

public interface IZLinkDiscoveryBuilder
{
    void Add(string endpoint);
}

public interface IZLinkSpotNodeBuilder
{
    void Bind(string endpoint);

    void EnableRouter(
        Action<ISpotRouterCapabilityBuilder>? configure = null);

    void EnablePubSub(
        Action<ISpotPubSubCapabilityBuilder>? configure = null);

    void AttachChannelClient(
        string channelName,
        Action<ISpotChannelClientCapabilityBuilder>? configure = null);

    void AttachSpotPublisherClient(
        string channelName,
        Action<ISpotPublisherClientCapabilityBuilder>? configure = null);

    void AddSpotFactory<TSpot>(string spotName)
        where TSpot : ZLinkSpot;
}

public interface IZLinkFrameworkOptions
{
    void UseSpotDiscovery(
        string channelName,
        Action<IZLinkDiscoveryBuilder> configure);

    void AddSpotNode(
        string spotNodeName,
        Action<IZLinkSpotNodeBuilder> configure);
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

또한 `UseSpotDiscovery(...)`에서 이미 SPOT channel 이름을 등록하므로,
`AddSpotNode(...)` 안에서 같은 channel 이름을 다시 받는 함수는 두지 않는다.

즉 `SPOT` 등록 시점에도

- local routed router capability 활성화
- local SPOT pub/sub capability 활성화
- 외부 channel 호출용 client attach
- 외부 SPOT publish client attach

두 축을 같이 드러내는 편이 맞다.

### 6.3 Spot 연결 관리

SPOT도 수동 연결을 쓸 때는 capability별 런타임 제어 표면이 필요하다.

```csharp
public interface IZLinkSpotConnectionManager
{
    ISpotRouterConnections GetRouter(string spotNodeName);

    ISpotPublisherConnections GetPubSub(string spotNodeName);

    IChannelClientConnections GetChannelClient(
        string spotNodeName,
        string channelName);

    ISpotPublisherConnections GetSpotPublisherClient(
        string spotNodeName,
        string channelName);
}
```

이 관리 인터페이스도 아무 node에나 항상 열리는 것이 아니라, 해당 capability가
manual 모드일 때만 유효한 표면으로 보는 편이 맞다.

## 7. Timer 인터페이스

현재 초안에서는 spot lifecycle 안에 등록한 `AddTimer<THandler>(...)`가 반환하는
timer handle이다.

```csharp
public interface IZLinkTimer : IAsyncDisposable
{
    bool IsDisposed { get; }

    ValueTask CancelAsync(
        CancellationToken cancellationToken = default);
}
```

timer가 어떤 실행 문맥에서 callback을 호출하는지가 중요하다.

- 현재 방향에서는 timer를 별도 client scheduler로 두지 않는다.
- spot timer는 같은 spot 실행 문맥 안에 등록되고, 해당 문맥에서 처리되는 편이
  Stage wrapper에 더 자연스럽다.

이 구분은 framework가 새 의미를 발명하기보다, 하부 C API 계약
(`zlink_spot_timer_new`)을 `.NET` 표면으로 옮기는 성격이다.

## 8. Handler Filter

HTTP middleware와 별도로, ZLink handler 전후 공통 처리를 위한 filter다.

```csharp
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

## 9. Marker 인터페이스

request 타입이 어떤 reply 타입과 쌍을 이루는지 컴파일 타임에 연결하는
marker다.

```csharp
public interface IZLinkRequest<TReply>
{
}
```

handler는 메서드 시그니처만으로 request/reply 타입을 읽을 수 있으므로 marker가
필수는 아니다. 반면 client 호출부에서는 `Request(...)`가 어떤 reply 타입을
돌려줘야 하는지 알기 위해 이 marker를 쓰는 편이 맞다.

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
status, service summary, topology, member peers를 제공한다.

### 10.2 IZLinkRegistryQueryClient

다른 프로세스의 Registry를 원격 조회한다.
`AddZLinkRegistryQueryClient(...)` 로 별도 등록한다.
topology snapshot만 제공한다.

### 10.3 runtime monitoring

runtime monitoring은 socket/discovery의 하부 monitor와, registry/spot의
snapshot diff를 함께 감싸는 운영 표면이다. 공용 handler shape는 아래처럼 두는
편이 자연스럽다.

```csharp
public interface IZLinkMonitoringOptions
{
    void AddSocketEvents(
        string sourceName,
        SocketEvent events = SocketEvent.All);

    void AddDiscoveryEvents(
        string sourceName,
        params ServiceMonitorEventMask[] events);

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
같이 전달하기 어렵기 때문이다.

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
    MonitorEventType NativeEvent,
    uint Value,
    RoutingId? RoutingId,
    string LocalAddr,
    string RemoteAddr) : IZLinkRuntimeEvent;

public enum ZLinkDiscoveryEventKind
{
    ServiceUp = 0,
    ServiceDown,
    ProvidersChanged,
    PeerAdmissionChanged,
    Error,
    Closed,
    Internal
}

public readonly record struct ZLinkDiscoveryEvent(
    string SourceName,
    DateTimeOffset Timestamp,
    ZLinkDiscoveryEventKind Event,
    ServiceEventType NativeEventType,
    uint Status,
    uint ErrorCode,
    ulong Value,
    uint DetailFlags,
    string ServiceName,
    string Endpoint,
    RoutingId? RoutingId,
    string Subject,
    SubjectKind SubjectKind) : IZLinkRuntimeEvent;

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
    RegistryStatus? Status,
    IReadOnlyList<RegistryTopologyEntry>? Topology,
    IReadOnlyList<RegistryServiceSummaryEntry>? ServiceSummary)
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
    SpotNodeStatus? Status,
    IReadOnlyList<SpotNodePeerEntry>? Peers,
    IReadOnlyList<SpotNodeSubjectEntry>? Subjects)
    : IZLinkRuntimeEvent;
```

이 초안에서 source별 의미는 아래처럼 정리한다.

- socket event
  - 하부 `SocketMonitor`를 감싼다.
  - source 이름은 `channel + capability` 또는 `spot node + capability`가 자연스럽다.
  - 예: `profile.server`, `profile.client`, `stage-node.router`
- discovery event
  - 하부 `ServiceMonitor`를 감싼다.
  - source 이름은 logical discovery registration 이름을 쓴다.
  - 예: `profile.client.discovery`, `game.stage.discovery`
- registry event
  - 하부 raw monitor가 아니라 `StatusSnapshot()`, `TopologySnapshot()`,
    `ServiceSummarySnapshot()`의 polling + diff로 만든다.
- spot event
  - 하부 raw monitor가 아니라 `StatusSnapshot()`, `PeersSnapshot()`,
    `SubjectsSnapshot()`의 polling + diff로 만든다.

## 11. Attribute 정의

### 11.1 서버 간 messaging

```csharp
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

### 11.2 event

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkEventAttribute : Attribute
{
    public ZLinkEventAttribute();
    public string? PacketName { get; init; }
}
```

`ZLinkTopicAttribute` 후보도 검토 중이나, 최종 이름은 미정이다.

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

stream은 packet session과 raw session 두 축으로 본다.
recv 방식은 현재 초안 범위에서 제외하고, session lifecycle은 `OnConnectedAsync`,
`OnDisconnectedAsync`, `OnErrorAsync`로 올린다.

## 12. 시그니처 규칙

attribute 기반 handler의 메서드 시그니처는 아래 규칙을 따른다.

- 첫 번째 인자: decoded body 타입
- 두 번째 인자: context 타입 (생략 가능)
- 마지막 인자: `CancellationToken` (생략 가능)
- request handler 반환: `ValueTask<T>` 또는 `Task<T>`
- send handler 반환: `ValueTask` (1차 권장)

framework가 강제하는 것은 class 구조가 아니라, resolved packet key 하나는
하나의 handler에만 매핑된다는 규칙이다. 주제별 handler 묶음(`UserHandlers`)과 패킷별
단일 class(`UserGetHandler`) 둘 다 허용한다.

## 13. DI 동작 기준

- handler class는 `.NET DI`에서 resolve한다.
- handler constructor injection이 동작해야 한다.
- outbound client도 같은 DI 컨테이너에서 주입된다.
- `IZLinkHandlerFilter` 구현체도 같은 DI 컨테이너에서 resolve한다.
- framework는 별도 객체 생성기를 두기보다, `ASP.NET Core`가 쓰는
  `IServiceProvider`를 기준으로 handler invocation을 구성한다.

local handler가 붙는 channel은 route prefix가 아니라 애플리케이션이 그 channel에서
server 역할을 한다는 뜻이다. handler class attribute보다 channel registration
(`options.AddChannel("api", channel => channel.EnableServer())`)에 두는 편을 현재
방향으로 본다. 다만 outbound-only 앱이라면 server capability가 있는 channel은
없을 수 있어야 한다.

## 14. 아직 확정하지 않는 것

- request/send를 인터페이스와 attribute 중 어느 쪽을 앞면으로 둘지
- `channelName`을 등록 옵션에서만 둘지, 별도 attribute도 허용할지
- pub/sub을 `IZLinkEventHandler<>`와 `IZLinkTopicHandler<>` 중 무엇으로 둘지
- `ZLinkRequestContext`와 `ZLinkSendContext`를 하나의 공통 context로 합칠지
- `OnErrorAsync(...)`로 올릴 monitor 이벤트 범위를 어디까지로 좁힐지
- pub/sub 최종 attribute 이름을 `ZLinkEvent`와 `ZLinkTopic` 중 무엇으로 고를지
- `IZLinkClient` 위에 channel별 typed wrapper를 공식 제공할지
- `spotRid` 타입을 `RoutingId` 그대로 쓸지, 별도 wrapper로 올릴지
- `IZLinkRegistryQuery`와 `IZLinkRegistryQueryClient`를 공용 인터페이스로 묶을지
