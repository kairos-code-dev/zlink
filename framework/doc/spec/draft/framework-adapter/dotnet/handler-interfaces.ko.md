[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework .NET Handler Interfaces

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`에서 메시지 handler를 어떤 인터페이스와
> 시그니처로 고정할지 논의하기 위한 문서다.

## 1. 목적

이 문서는 `ASP.NET Core` 기준으로 `ZLink Framework`의 실제 handler 표면을 먼저
고정하기 위한 초안이다.

현재 우선 원칙은 아래와 같다.

- 서버 간 `request`와 `send`는 HTTP endpoint handler와 닮은 경험으로 보인다.
- application handler는 raw header를 직접 받지 않는다.
- header metadata, caller 정보, timeout 정보는 context에서 조회한다.
- 다른 서버로 보내는 outbound 호출은 DI로 주입된 공용 client를 통해 수행한다.
- 이 client는 ZLink handler 안에서도, 기존 ASP.NET Core HTTP handler 안에서도
  똑같이 사용할 수 있어야 한다.
- 현재 이 문서는 `ROUTER <-> ROUTER` 기반 서버간 request/send와 일반 `PUB/SUB`
  기준을 우선 다룬다.
- `SPOT`은 별도 문서에서 따로 다룬다.
- `STREAM`은 별도 handler 축으로 유지하되, 이 문서의 중심은 아니다.

## 2. 기본 context

모든 handler가 같은 context 타입을 쓰는 것은 아니다. 그래도 공통 기반은
비슷해야 한다.

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

이 인터페이스는 초안 기준 공통 최소 집합이다.
실제 구현에서는 routed 정보나 transport별 부가 정보가 파생 context에 추가된다.

## 3. 서버 간 request handler

가장 먼저 고정할 표면은 서버 간 request-response handler다.

```csharp
public interface IZLinkRequestHandler<in TRequest, TResponse>
{
    ValueTask<TResponse> HandleAsync(
        TRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken);
}
```

핵심은 아래와 같다.

- `TRequest`는 이미 decode된 body다.
- `TResponse`도 framework가 encode할 typed 결과다.
- raw multipart header는 인자로 주지 않는다.
- timeout, correlation, caller 정보는 `ZLinkRequestContext`에서 본다.

예시:

```csharp
public sealed class GetProfileHandler
    : IZLinkRequestHandler<ProfileRequest, ProfileReply>
{
    public ValueTask<ProfileReply> HandleAsync(
        ProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new ProfileReply());
    }
}
```

## 4. 서버 간 send handler

응답이 없는 one-way 전송은 별도 인터페이스로 두는 편이 명확하다.

```csharp
public interface IZLinkSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkSendContext context,
        CancellationToken cancellationToken);
}
```

이 인터페이스는 아래 상황에 맞는다.

- fire-and-forget command
- 후처리 트리거
- 응답 body가 필요 없는 내부 signal

예시:

```csharp
public sealed class WarmupCacheHandler
    : IZLinkSendHandler<WarmupCacheCommand>
{
    public ValueTask HandleAsync(
        WarmupCacheCommand message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}
```

## 5. attribute 매핑 표면

`ASP.NET Core`에서 최종 경험은 인터페이스 직접 구현보다 attribute 매핑이 더
자연스러울 수 있다. 그래도 내부 dispatcher가 결국 위 인터페이스로 정규화되는
구조가 이해하기 쉽다.

현재 초안은 request/send attribute 이름은 먼저 고정하되, `serviceId`는 route
prefix처럼 handler class에 두지 않는 편이 맞다고 본다.

### 5.1 1차 attribute 후보

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

이 두 개를 request/send 기본 축으로 본다.

- `ZLinkRequestAttribute`는 응답 있는 handler를 뜻한다.
- `ZLinkSendAttribute`는 응답 없는 one-way handler를 뜻한다.

`serviceId`는 handler class의 route prefix가 아니라, 애플리케이션이 속한 서버군
식별자에 가깝다. 그래서 현재 방향에서는 아래처럼 앱 등록에서 정하는 편이
자연스럽다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.ServiceId = "api";
});
```

### 5.2 request/send 시그니처 규칙

현재 초안은 method 시그니처를 아래 규칙으로 제한하는 편이 맞다.

- 첫 번째 인자는 decoded body 타입
- 두 번째 인자는 context 타입 또는 생략 가능
- 마지막 인자는 `CancellationToken` 또는 생략 가능
- `request`는 `ValueTask<T>` 또는 `Task<T>` 반환
- `send`는 `ValueTask`, `Task`, 또는 `void`까지는 열 수 있으나 1차는
  `ValueTask`를 권장

예시:

```csharp
public sealed class ProfileHandlers
{
    [ZLinkRequest("profile.get")]
    public ValueTask<ProfileReply> GetAsync(
        ProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new ProfileReply());
    }

    [ZLinkSend("profile.refresh-cache")]
    public ValueTask RefreshAsync(
        RefreshProfileCacheCommand command,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}
```

즉 사용자에게는 HTTP handler와 비슷한 메시지 매핑을 보여 주고, framework 내부는
이를 `IZLinkRequestHandler<,>` 또는 `IZLinkSendHandler<>`로 맞춘다.

여기서 class는 코드 조직 단위일 뿐이다. 예를 들면 아래 둘 다 허용할 수 있다.

- `UserHandlers` 아래에 `user.get`, `user.set`을 같이 둔다.
- 패킷 하나당 `UserGetHandler`, `UserSetHandler`처럼 class를 따로 둔다.

framework가 강제해야 하는 것은 class 구조가 아니라, 메시지 이름 중복 매핑 금지다.

### 5.3 인터페이스와 attribute의 관계

현재 1차 방향은 아래 둘을 모두 허용하는 것이다.

1. class가 `IZLinkRequestHandler<,>` 또는 `IZLinkSendHandler<>`를 직접 구현
2. 일반 class 메서드에 attribute를 붙여 등록

다만 framework 바깥에서 보이는 앞면은 attribute 매핑이 더 자연스럽다.
인터페이스는 내부 dispatcher 정규화와 명시적 고급 사용을 위한 표면으로 두는 편이
맞다.

## 6. outbound client 초안

현재 `.NET` 표면에서 더 중요한 것은 메시지마다 별도 client를 두는 것이 아니라,
DI로 주입되는 공용 outbound client 하나를 두는 일이다.

```csharp
public interface IZLinkClient
{
    ValueTask SendAsync<TMessage>(
        string serviceName,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestAsync<TReply>(
        string serviceName,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestAsync<TReply>(
        string serviceName,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToAsync<TMessage>(
        RoutingId targetRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToSpotAsync<TMessage>(
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId spotRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId spotRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToSpotAsync<TMessage>(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);
}
```

이 client는 아래 두 곳에서 모두 쓸 수 있어야 한다.

- ZLink 서버간 메시지 handler 내부
- 기존 ASP.NET Core HTTP controller 또는 minimal API handler 내부

즉 `.NET` 응용은 inbound transport가 HTTP인지 ZLink인지와 관계없이, outbound 서버간
호출은 같은 `IZLinkClient`로 보낼 수 있어야 한다.

현재 방향에서는 이 client가 내부적으로 service별 channel을 관리한다.

- 처음 `api.profile`에 요청하면 `api.profile` channel을 만든다.
- channel은 그 service 전용 `Discovery`와 outbound socket을 가진다.
- framework는 그 channel 안의 `rid` 집합과 연결 상태만 관리하면 된다.

동시에 특정 peer를 이미 알고 있을 때는 `rid` 직접 타겟팅도 가능해야 한다.
즉 outbound 표면은 아래 두 경로를 모두 가진다.

- `serviceName` 기준 일반 호출
- `RoutingId` 기준 직접 호출

그리고 request 계열은 per-call timeout 오버로드도 같이 두는 편이 맞다.

- 기본 timeout은 framework 전역 옵션을 따른다.
- 필요하면 특정 요청만 더 짧거나 길게 timeout을 덮어쓴다.

### 6.1 ZLink handler 안에서의 사용 예시

```csharp
public sealed class InventoryHandlers
{
    private readonly IZLinkClient _client;

    public InventoryHandlers(IZLinkClient client)
    {
        _client = client;
    }

    [ZLinkRequest("inventory.get")]
    public async ValueTask<GetInventoryReply> HandleAsync(
        GetInventoryRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var profile = await _client.RequestAsync<GetProfileReply>(
            "api.profile",
            new GetProfileRequest { AccountId = request.AccountId },
            cancellationToken);

        return new GetInventoryReply
        {
            AccountId = request.AccountId,
            Nickname = profile.Nickname
        };
    }
}
```

### 6.2 HTTP handler 안에서의 사용 예시

```csharp
app.MapPost("/inventory/get", async (
    GetInventoryHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    var reply = await client.RequestAsync<GetInventoryReply>(
        "api.inventory",
        new GetInventoryRequest { AccountId = request.AccountId },
        cancellationToken);

    return Results.Ok(reply);
});
```

즉 client는 "ZLink 전용 handler helper"가 아니라, 애플리케이션 전체에서 공용으로
쓰는 서버간 호출 도구가 되어야 한다.

`rid`를 알고 있을 때의 예시는 아래처럼 둘 수 있다.

```csharp
var reply = await client.RequestToAsync<GetInventoryReply>(
    targetRid,
    new GetInventoryRequest { AccountId = request.AccountId },
    cancellationToken);
```

timeout을 따로 주는 예시는 아래처럼 둘 수 있다.

```csharp
var fastReply = await client.RequestAsync<GetInventoryReply>(
    "api.inventory",
    new GetInventoryRequest { AccountId = request.AccountId },
    TimeSpan.FromMilliseconds(150),
    cancellationToken);
```

## 7. pub/sub handler 초안

pub/sub은 아직 설명 방식을 더 좁혀야 한다. 그래도 handler 모양은 아래 둘 중
하나로 수렴하는 편이 자연스럽다.

```csharp
public interface IZLinkEventHandler<in TEvent>
{
    ValueTask HandleAsync(
        TEvent message,
        ZLinkEventContext context,
        CancellationToken cancellationToken);
}
```

또는 topic/pattern이 중요한 경우:

```csharp
public interface IZLinkTopicHandler<in TEvent>
{
    ValueTask HandleAsync(
        TEvent message,
        ZLinkTopicContext context,
        CancellationToken cancellationToken);
}
```

현재는 어떤 이름을 최종 표면으로 고를지 미정이다.
다만 request/send와 마찬가지로 raw header를 직접 handler 인자로 넘기지는 않는다.

attribute 후보는 아래 둘 중 하나다.

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkEventAttribute : Attribute
{
    public ZLinkEventAttribute(string pattern);
    public string Pattern { get; }
    public string? ServiceName { get; init; }
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkTopicAttribute : Attribute
{
    public ZLinkTopicAttribute(string topic);
    public string Topic { get; }
    public string? ServiceName { get; init; }
}
```

## 8. STREAM handler 초안

`STREAM`은 request/send/pubsub와 다른 종류의 surface가 필요하다.
현재는 connection 중심과 packet 중심 두 방향이 있다.

### 8.1 packet handler

현재 binding에 더 가까운 방향은 packet handler다.

```csharp
public interface IZLinkStreamPacketHandler
{
    ValueTask HandleAsync(
        ZLinkStreamPacket packet,
        ZLinkStreamContext context,
        CancellationToken cancellationToken);
}
```

또는 typed payload를 원하면:

```csharp
public interface IZLinkStreamPacketHandler<in TPacket>
{
    ValueTask HandleAsync(
        TPacket packet,
        ZLinkStreamContext context,
        CancellationToken cancellationToken);
}
```

현재 binding과의 거리만 보면 attribute도 packet 중심으로 잡는 편이 낫다.

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkStreamPacketAttribute : Attribute
{
    public ZLinkStreamPacketAttribute(string? pattern = null);
    public string? Pattern { get; }
}
```

### 8.2 session handler

framework 친화성만 보면 session handler가 더 높을 수도 있다.

```csharp
public interface IZLinkStreamSessionHandler
{
    Task OnConnectedAsync(
        ZLinkStreamSession session,
        CancellationToken cancellationToken);
}
```

이 경우 packet read/write는 `ZLinkStreamSession`으로 감춘다.

현재 단계에서는 둘 중 무엇을 기본으로 둘지 아직 정하지 않는다.
다만 stream은 request handler에 억지로 끼워 넣지 않고 별도 축으로 둔다.

session 중심으로 가면 attribute는 아래처럼 별도 이름을 둘 수 있다.

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false)]
public sealed class ZLinkStreamSessionAttribute : Attribute
{
}
```

## 9. 현재 권장 결론

지금 바로 인터페이스를 먼저 고정한다면, 1차 후보는 아래가 가장 안정적이다.

```csharp
public interface IZLinkRequestHandler<in TRequest, TResponse>
{
    ValueTask<TResponse> HandleAsync(
        TRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkSendContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkStreamPacketHandler
{
    ValueTask HandleAsync(
        ZLinkStreamPacket packet,
        ZLinkStreamContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkClient
{
    ValueTask SendAsync<TMessage>(
        string serviceName,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestAsync<TReply>(
        string serviceName,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestAsync<TReply>(
        string serviceName,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToAsync<TMessage>(
        RoutingId targetRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToAsync<TReply>(
        RoutingId targetRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToSpotAsync<TMessage>(
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId spotRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId spotRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToSpotAsync<TMessage>(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> RequestToSpotAsync<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);
}
```

이 다섯 개를 기준선으로 두면 아래 요구를 동시에 만족시키기 쉽다.

- HTTP와 닮은 handler mapping
- header 비노출
- `ROUTER <-> ROUTER` request/send 분리
- `serviceName` 기준 호출과 `rid` 기준 직접 호출 공존
- 일반 client와 spot client가 서로 다른 C API 위에 있으면서도, 공통으로 필요한
  `serviceName`, `router rid`, `spot rid` 기반 호출을 함께 노출 가능
- request per-call timeout override
- 일반 `PUB/SUB` subscribe와 publish 분리
- `STREAM` 독립 축 유지
- HTTP handler와 ZLink handler가 같은 outbound client를 공유 가능

여기서 중요한 점은 `IZLinkClient`와 `IZLinkSpotClient`를 상하 관계로 설명하지
않는 일이다. 두 인터페이스는 서로 다른 하부 C API를 감싸며, 각자 독립 구현을
가진다. 다만 실제 사용 시 필요한 메시징 기능이 겹치기 때문에, 겹치는 함수군을
각 인터페이스가 각각 노출할 수 있다.

그리고 attribute 표면은 아래처럼 잡는 편이 가장 일관적이다.

```csharp
[ZLinkRequest("profile.get")]
[ZLinkSend("profile.refresh-cache")]
[ZLinkStreamPacket]
```

## 10. 아직 확정하지 않는 것

- request/send를 인터페이스와 attribute 중 어느 쪽을 앞면으로 둘지
- `serviceId`를 등록 옵션에서만 둘지, 별도 attribute도 허용할지
- pub/sub을 `IZLinkEventHandler<>`와 `IZLinkTopicHandler<>` 중 무엇으로 둘지
- `ZLinkRequestContext`와 `ZLinkSendContext`를 하나의 공통 context로 합칠지
- stream을 packet 중심으로 고정할지, session 중심으로 올릴지
- pub/sub 최종 attribute 이름을 `ZLinkEvent`와 `ZLinkTopic` 중 무엇으로 고를지
- `IZLinkClient` 위에 서비스별 typed wrapper를 공식 제공할지
