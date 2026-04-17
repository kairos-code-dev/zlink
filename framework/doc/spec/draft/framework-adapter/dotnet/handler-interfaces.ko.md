[스펙 목차](../../../README.ko.md)

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
  [aspnet-core-service-messaging.ko.md](./aspnet-core-service-messaging.ko.md)
- 서버 간 messaging 샘플 →
  [service-messaging-samples.ko.md](./service-messaging-samples.ko.md)
- SPOT 통합 →
  [aspnet-core-spot.ko.md](./aspnet-core-spot.ko.md)
- SPOT 샘플 →
  [spot-samples.ko.md](./spot-samples.ko.md)
- Registry 통합 →
  [aspnet-core-registry.ko.md](./aspnet-core-registry.ko.md)

## 2. 인터페이스 전체 목록

| 분류 | 인터페이스 | 역할 | section |
|------|-----------|------|---------|
| context | `IZLinkHandlerContext` | 모든 handler context의 공통 기반 | 3.1 |
| handler | `IZLinkRequestHandler<TRequest, TResponse>` | request-response handler | 4.1 |
| handler | `IZLinkSendHandler<TMessage>` | one-way send handler | 4.2 |
| handler | `IZLinkEventHandler<TEvent>` | pub/sub event handler | 4.3 |
| handler | `IZLinkStreamPacketHandler` | stream packet handler (raw) | 4.4 |
| handler | `IZLinkStreamPacketHandler<TPacket>` | stream packet handler (typed) | 4.4 |
| handler | `IZLinkStreamSessionHandler` | stream session handler | 4.5 |
| client | `IZLinkClient` | 서버 간 outbound client | 5.1 |
| client | `IZLinkSpotClient` | SPOT outbound client | 5.2 |
| client | `IZLinkEventPublisher` | pub/sub event publisher | 5.3 |
| management | `IZLinkSpotManager` | spot 인스턴스 생성/삭제 | 6 |
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
    string ServiceName { get; }
    string Pattern { get; }
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
| `ZLinkStreamContext` | stream handler | peer/session 식별, connection 수명 |

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

### 4.4 stream packet handler

stream packet을 처리하는 handler다. raw 버전과 typed 버전 두 가지가 있다.

```csharp
public interface IZLinkStreamPacketHandler
{
    ValueTask HandleAsync(
        ZLinkStreamPacket packet,
        ZLinkStreamContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkStreamPacketHandler<in TPacket>
{
    ValueTask HandleAsync(
        TPacket packet,
        ZLinkStreamContext context,
        CancellationToken cancellationToken);
}
```

### 4.5 stream session handler

connection 수준에서 stream을 처리하는 handler다.
packet handler와 session handler 중 어느 쪽을 기본으로 둘지는 미정이다.

```csharp
public interface IZLinkStreamSessionHandler
{
    Task OnConnectedAsync(
        ZLinkStreamSession session,
        CancellationToken cancellationToken);
}
```

## 5. Client 인터페이스

### 5.1 IZLinkClient

서버 간 outbound 호출을 위한 공용 client다.
DI로 주입되며, ZLink handler와 기존 ASP.NET Core HTTP handler 양쪽에서
동일하게 사용할 수 있다.

호출 방식은 세 가지 축이 있다.

- `serviceName` 기준 호출 -- Discovery가 대상을 선택한다
- `RoutingId targetRid` 기준 직접 호출 -- 특정 peer를 지정한다
- `targetRid + spotRid` 기준 호출 -- 특정 spot 인스턴스를 지정한다

```csharp
public interface IZLinkClient
{
    // --- serviceName 기준 ---
    ValueTask SendAsync<TMessage>(
        string serviceName,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestAsync<TReply>(
        string serviceName,
        IZLinkRequest<TReply> request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestAsync<TReply>(
        string serviceName,
        IZLinkRequest<TReply> request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    // --- targetRid 직접 지정 ---
    ValueTask SendToAsync<TMessage>(
        RoutingId targetRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        IZLinkRequest<TReply> request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        IZLinkRequest<TReply> request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    // --- targetRid + spotRid ---
    ValueTask SendToAsync<TMessage>(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        IZLinkRequest<TReply> request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        IZLinkRequest<TReply> request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    // --- timer ---
    ValueTask<IZLinkTimer> ScheduleOnceAsync(
        TimeSpan dueTime,
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkTimer> SchedulePeriodicAsync(
        TimeSpan period,
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken = default);
}
```

runtime은 접근한 `serviceName`마다 별도 outbound channel을 lazy하게 만든다.
각 channel은 그 service 전용 `Discovery`와 outbound socket을 가진다.

### 5.2 IZLinkSpotClient

SPOT outbound 호출을 위한 client다.
`IZLinkClient`와 독립된 인터페이스이며, 하부에서 서로 다른 C API를 감싼다.
다만 하부 기능이 겹치는 부분이 있으므로 호출 축 구조는 비슷하다.

```csharp
public interface IZLinkSpotClient
{
    // --- serviceName 기준 ---
    ValueTask SendAsync<TMessage>(
        string serviceName,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestAsync<TReply>(
        string serviceName,
        IZLinkRequest<TReply> request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestAsync<TReply>(
        string serviceName,
        IZLinkRequest<TReply> request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    // --- targetRid 직접 지정 ---
    ValueTask SendToAsync<TMessage>(
        RoutingId targetRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        IZLinkRequest<TReply> request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        IZLinkRequest<TReply> request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    // --- targetRid + spotRid ---
    ValueTask SendToAsync<TMessage>(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        IZLinkRequest<TReply> request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        IZLinkRequest<TReply> request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    // --- publish ---
    ValueTask PublishAsync<T>(
        string serviceName,
        string topic,
        T message,
        CancellationToken cancellationToken = default);

    // --- timer ---
    ValueTask<IZLinkTimer> ScheduleOnceAsync(
        TimeSpan dueTime,
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkTimer> SchedulePeriodicAsync(
        TimeSpan period,
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken = default);
}
```

`IZLinkClient`와의 차이점은 아래와 같다.

- `PublishAsync`가 있다. SPOT 쪽은 direct call과 publish를 함께 쓰는 경우가
  많으므로 한 인터페이스에 같이 둔다.
- timer callback은 가능하면 같은 spot 실행 문맥에서 실행되는 편이 더
  자연스럽다. 이 부분은 하부 C API 계약(`zlink_spot_timer_new`)을 따른다.

`IZLinkClient`와 `IZLinkSpotClient`는 상하 관계가 아니다. 두 인터페이스는 서로
다른 하부 C API를 감싸며, 각자 독립 구현을 가진다.

### 5.3 IZLinkEventPublisher

일반 `PUB/SUB` event를 publish하는 인터페이스다.
SPOT publish와 별도로, `ROUTER <-> ROUTER` 기반 서버간 messaging 쪽에서 쓴다.

```csharp
public interface IZLinkEventPublisher
{
    ValueTask PublishAsync<TEvent>(
        string serviceName,
        string topic,
        TEvent message,
        CancellationToken cancellationToken = default);
}
```

## 6. Spot 관리 인터페이스

`IZLinkSpotManager`는 `SpotNode` 안에서 spot 인스턴스를 생성하고 삭제하는
인터페이스다. handler가 spot을 만드는 것이 아니라, manager가 만들고 handler는
들어오는 메시지를 처리할 뿐이다.

```csharp
public readonly record struct ZLinkSpotCreateResult(
    RoutingId SpotRid,
    bool Created);

public interface IZLinkSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}
```

두 가지 `CreateAsync` 오버로드는 각각 아래 상황을 설명한다.

- 인자 없음: runtime이 새 `spotRid`를 발급
- `RoutingId spotRid`: 호출자가 특정 `spotRid`를 지정

반환값은 `spotRid`와 새로 만들었는지 여부다. 장기적으로 들고 다닐 instance
handle이 아니라, 생성 결과만 돌려준다.

추가로 외부에서 논리 키로 주소를 찾는 directory도 필요할 수 있다.
이건 `spot` 자신이 자기 정보를 읽는 기능과는 다른 축이다.

```csharp
public readonly record struct ZLinkSpotAddress(
    RoutingId TargetRid,
    RoutingId SpotRid);

public interface IZLinkSpotDirectory<TKey>
{
    ValueTask<ZLinkSpotAddress?> ResolveAsync(
        TKey key,
        CancellationToken cancellationToken = default);
}
```

즉 두 기능은 아래처럼 구분된다.

- `IZLinkSpotDirectory<TKey>`: 외부가 논리 키로 spot 주소를 찾는다.
- `ZLinkSpotContext.Self`: 현재 spot이 자기 `spotRid`, `nodeRid`를 읽는다.

## 7. Timer 인터페이스

`IZLinkClient.ScheduleOnceAsync`와 `IZLinkClient.SchedulePeriodicAsync`가
반환하는 timer handle이다.

```csharp
public interface IZLinkTimer : IAsyncDisposable
{
    bool IsDisposed { get; }

    ValueTask CancelAsync(
        CancellationToken cancellationToken = default);
}
```

timer가 어떤 실행 문맥에서 callback을 호출하는지가 중요하다.

- `IZLinkClient`의 timer: 응용 service 문맥에서 공용 작업을 예약하는 용도
- `IZLinkSpotClient`의 timer: 같은 spot 실행 문맥에서 callback이 실행되는 편이
  Stage wrapper에 더 자연스럽다

이 구분은 framework가 새 의미를 발명하기보다, 하부 C API 계약
(`zlink_timer_new`, `zlink_spot_timer_new`)을 `.NET` 표면으로 옮기는 성격이다.

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
필수는 아니다. 반면 client 호출부에서는 `RequestAsync`의 반환 타입 추론을 위해
이 marker를 쓰는 편이 맞다.

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

## 11. Attribute 정의

### 11.1 서버 간 messaging

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkRequestAttribute : Attribute
{
    public ZLinkRequestAttribute(string pattern);
    public string Pattern { get; }
    public string? ServiceName { get; init; }
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSendAttribute : Attribute
{
    public ZLinkSendAttribute(string pattern);
    public string Pattern { get; }
    public string? ServiceName { get; init; }
}
```

### 11.2 event

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkEventAttribute : Attribute
{
    public ZLinkEventAttribute(string pattern);
    public string Pattern { get; }
    public string? ServiceName { get; init; }
}
```

`ZLinkTopicAttribute` 후보도 검토 중이나, 최종 이름은 미정이다.

### 11.3 SPOT

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkSpotRequestAttribute : Attribute
{
    public ZLinkSpotRequestAttribute(string pattern);
    public string Pattern { get; }
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
    public ZLinkStreamPacketAttribute(string? pattern = null);
    public string? Pattern { get; }
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkStreamSessionAttribute : Attribute
{
}
```

stream을 packet 중심으로 고정할지, session 중심으로 올릴지는 미정이다.

## 12. 시그니처 규칙

attribute 기반 handler의 메서드 시그니처는 아래 규칙을 따른다.

- 첫 번째 인자: decoded body 타입
- 두 번째 인자: context 타입 (생략 가능)
- 마지막 인자: `CancellationToken` (생략 가능)
- request handler 반환: `ValueTask<T>` 또는 `Task<T>`
- send handler 반환: `ValueTask` (1차 권장)

framework가 강제하는 것은 class 구조가 아니라, 메시지 이름 하나는 하나의
handler에만 매핑된다는 규칙이다. 주제별 handler 묶음(`UserHandlers`)과 패킷별
단일 class(`UserGetHandler`) 둘 다 허용한다.

## 13. DI 동작 기준

- handler class는 `.NET DI`에서 resolve한다.
- handler constructor injection이 동작해야 한다.
- outbound client도 같은 DI 컨테이너에서 주입된다.
- `IZLinkHandlerFilter` 구현체도 같은 DI 컨테이너에서 resolve한다.
- framework는 별도 객체 생성기를 두기보다, `ASP.NET Core`가 쓰는
  `IServiceProvider`를 기준으로 handler invocation을 구성한다.

`serviceId`는 route prefix가 아니라 애플리케이션이 속한 서버군 식별자다.
handler class attribute보다 등록 옵션(`options.ServiceId = "api"`)에 두는 편을
현재 방향으로 본다.

## 14. 아직 확정하지 않는 것

- request/send를 인터페이스와 attribute 중 어느 쪽을 앞면으로 둘지
- `serviceId`를 등록 옵션에서만 둘지, 별도 attribute도 허용할지
- pub/sub을 `IZLinkEventHandler<>`와 `IZLinkTopicHandler<>` 중 무엇으로 둘지
- `ZLinkRequestContext`와 `ZLinkSendContext`를 하나의 공통 context로 합칠지
- stream을 packet 중심으로 고정할지, session 중심으로 올릴지
- pub/sub 최종 attribute 이름을 `ZLinkEvent`와 `ZLinkTopic` 중 무엇으로 고를지
- `IZLinkClient` 위에 서비스별 typed wrapper를 공식 제공할지
- `IZLinkSpotClient`에서 publish를 분리할지, 그대로 둘지
- `spotRid` 타입을 `RoutingId` 그대로 쓸지, 별도 wrapper로 올릴지
- `IZLinkRegistryQuery`와 `IZLinkRegistryQueryClient`를 공용 인터페이스로 묶을지
