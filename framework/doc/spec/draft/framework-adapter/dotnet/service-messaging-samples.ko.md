[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework .NET Service Messaging Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` service messaging 초안을 실제 코드 흐름으로
> 한 번에 보기 위한 샘플 문서다.
> 현재 범위는 `ROUTER <-> ROUTER` 기반 서버간 `request/send`와 일반 `PUB/SUB`
> 까지만 다룬다. `SPOT`은 여기 넣지 않는다.

## 1. 이 문서의 목적

앞선 문서들은 설명 단위로 나뉘어 있어서, 실제 사용 코드를 한 번에 보기 어렵다.
이 문서는 아래 순서로 샘플을 모아서 보여 준다.

1. 서비스 등록
2. 공용 outbound client 인터페이스
3. ZLink request/send handler
4. 기존 HTTP handler에서의 사용
5. event subscribe와 publish

피드백은 이 문서의 코드 흐름을 기준으로 받는 것을 목표로 한다.

## 2. 한 번에 보는 전체 예시

아래 코드는 하나의 `ASP.NET Core` 애플리케이션 안에서

- `api` 서버군에 속한 앱이 ZLink handler를 받고
- 필요하면 다른 내부 service로 outbound 요청을 보내고
- 기존 HTTP endpoint 안에서도 같은 `IZLinkClient`를 쓰고
- event도 publish/subscribe 하는

모양을 한 번에 모아 둔 예시다.

```csharp
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.ServiceId = "api";
    options.NodeName = "api-1";
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
        registry.Add("tcp://registry2:5551");
    });
});

builder.Services.AddZLinkHandlersFromAssemblyContaining<Program>();

var app = builder.Build();

app.MapPost("/profiles/get", async (
    GetProfileHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    var reply = await client.RequestAsync<GetProfileReply>(
        "api.profile",
        new GetProfileRequest { AccountId = request.AccountId },
        cancellationToken);

    return Results.Ok(reply);
});

app.MapPost("/profiles/refresh-cache", async (
    RefreshProfileCacheHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    await client.SendAsync(
        "api.profile",
        new RefreshProfileCacheCommand { AccountId = request.AccountId },
        cancellationToken);

    return Results.Accepted();
});

app.Run();

public sealed class UserHandlers
{
    private readonly IZLinkClient _client;
    private readonly IZLinkEventPublisher _publisher;

    public UserHandlers(
        IZLinkClient client,
        IZLinkEventPublisher publisher)
    {
        _client = client;
        _publisher = publisher;
    }

    [ZLinkRequest("user.get")]
    public async ValueTask<GetUserReply> GetUserAsync(
        GetUserRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var account = await _client.RequestAsync<GetAccountReply>(
            "api.account",
            new GetAccountRequest { AccountId = request.AccountId },
            cancellationToken);

        return new GetUserReply
        {
            AccountId = request.AccountId,
            Nickname = account.Nickname
        };
    }

    [ZLinkSend("user.refresh-cache")]
    public async ValueTask RefreshCacheAsync(
        RefreshUserCacheCommand command,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        await _publisher.PublishAsync(
            "api",
            "user.cache-refreshed",
            new UserCacheRefreshedEvent
            {
                AccountId = command.AccountId
            },
            cancellationToken);
    }
}

public sealed class ItemHandlers
{
    [ZLinkRequest("item.get")]
    public ValueTask<GetItemReply> GetItemAsync(
        GetItemRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new GetItemReply());
    }
}

public sealed class UserCacheEventHandlers
{
    [ZLinkEvent("user.cache-refreshed")]
    public ValueTask HandleAsync(
        UserCacheRefreshedEvent message,
        ZLinkEventContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public sealed class GetUserHttpRequest
{
    public long AccountId { get; set; }
}

public sealed class RefreshUserCacheHttpRequest
{
    public long AccountId { get; set; }
}

public sealed class GetUserRequest
{
    public long AccountId { get; set; }
}

public sealed class GetUserReply
{
    public long AccountId { get; set; }
    public string Nickname { get; set; } = "";
}

public sealed class GetAccountRequest
{
    public long AccountId { get; set; }
}

public sealed class GetAccountReply
{
    public string Nickname { get; set; } = "";
}

public sealed class GetItemRequest
{
    public long ItemId { get; set; }
}

public sealed class GetItemReply
{
    public long ItemId { get; set; }
}

public sealed class RefreshUserCacheCommand
{
    public long AccountId { get; set; }
}

public sealed class UserCacheRefreshedEvent
{
    public long AccountId { get; set; }
}
```

## 3. 이 샘플을 어떻게 읽으면 되는가

이 샘플에서 중요한 부분은 아래 여섯 가지다.

- `IZLinkClient`는 하나만 주입받는다.
- 요청 대상은 endpoint가 아니라 `service_name`이다.
- 이 앱은 `serviceId = "api"` 서버군에 속한다.
- runtime은 `api`, `api.account`처럼 접근한 service마다 별도 channel을 만든다.
- 각 channel은 그 service 전용 `Discovery`와 outbound socket을 가진다.
- 같은 `IZLinkClient`를 ZLink handler와 HTTP handler가 함께 쓴다.
- handler class는 `UserHandlers`, `ItemHandlers`처럼 주제별로 묶어도 된다.

즉 응용 코드 입장에서는 공용 client 하나만 보이지만, framework 내부에서는
service별 outbound channel이 분리되어 관리된다.

그리고 handler class는 dispatch key가 아니라 **코드 조직 단위**다. 실제 dispatch는
`user.get`, `item.get` 같은 메시지 이름으로 이뤄진다.

## 4. client 함수 시그니처를 따로 보면

위 예시가 전제로 하는 client 함수 시그니처는 아래와 같다.

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

public interface IZLinkEventPublisher
{
    ValueTask PublishAsync<TEvent>(
        string serviceName,
        string topic,
        TEvent message,
        CancellationToken cancellationToken = default);
}
```

함수별로 다시 풀어 쓰면 아래와 같다.

```csharp
ValueTask SendAsync<TMessage>(
    string serviceName,
    TMessage message,
    CancellationToken cancellationToken = default);
```

```csharp
ValueTask<TReply> RequestAsync<TReply>(
    string serviceName,
    object request,
    CancellationToken cancellationToken = default);
```

```csharp
ValueTask<TReply> RequestAsync<TReply>(
    string serviceName,
    object request,
    TimeSpan timeout,
    CancellationToken cancellationToken = default);
```

```csharp
ValueTask PublishAsync<TEvent>(
    string serviceName,
    string topic,
    TEvent message,
    CancellationToken cancellationToken = default);
```

```csharp
ValueTask SendToAsync<TMessage>(
    RoutingId targetRid,
    TMessage message,
    CancellationToken cancellationToken = default);
```

```csharp
ValueTask<TReply> RequestToAsync<TReply>(
    RoutingId targetRid,
    object request,
    CancellationToken cancellationToken = default);
```

```csharp
ValueTask<TReply> RequestToAsync<TReply>(
    RoutingId targetRid,
    object request,
    TimeSpan timeout,
    CancellationToken cancellationToken = default);
```

```csharp
ValueTask SendToSpotAsync<TMessage>(
    RoutingId spotRid,
    TMessage message,
    CancellationToken cancellationToken = default);
```

```csharp
ValueTask<TReply> RequestToSpotAsync<TReply>(
    RoutingId spotRid,
    object request,
    CancellationToken cancellationToken = default);
```

```csharp
ValueTask<TReply> RequestToSpotAsync<TReply>(
    RoutingId spotRid,
    object request,
    TimeSpan timeout,
    CancellationToken cancellationToken = default);
```

```csharp
ValueTask SendToSpotAsync<TMessage>(
    RoutingId targetRid,
    RoutingId spotRid,
    TMessage message,
    CancellationToken cancellationToken = default);
```

```csharp
ValueTask<TReply> RequestToSpotAsync<TReply>(
    RoutingId targetRid,
    RoutingId spotRid,
    object request,
    CancellationToken cancellationToken = default);
```

```csharp
ValueTask<TReply> RequestToSpotAsync<TReply>(
    RoutingId targetRid,
    RoutingId spotRid,
    object request,
    TimeSpan timeout,
    CancellationToken cancellationToken = default);
```

현재 샘플 기준으로 일반 client 쪽 public 함수는 이 열세 개를 먼저 본다.

## 5. 함수 호출 예시만 따로 보면

위 시그니처는 실제 코드에서 아래처럼 호출된다.

```csharp
await client.SendAsync(
    "api.profile",
    new RefreshProfileCacheCommand { AccountId = accountId },
    cancellationToken);
```

```csharp
var reply = await client.RequestAsync<GetProfileReply>(
    "api.profile",
    new GetProfileRequest { AccountId = accountId },
    cancellationToken);
```

```csharp
var fastReply = await client.RequestAsync<GetProfileReply>(
    "api.profile",
    new GetProfileRequest { AccountId = accountId },
    TimeSpan.FromMilliseconds(200),
    cancellationToken);
```

```csharp
await client.SendToSpotAsync(
    stageRid,
    new StageNoticeMessage { Text = "start" },
    cancellationToken);
```

```csharp
var stageReply = await client.RequestToSpotAsync<GetStageStateReply>(
    targetRid,
    stageRid,
    new GetStageStateRequest(),
    TimeSpan.FromMilliseconds(200),
    cancellationToken);
```

```csharp
await publisher.PublishAsync(
    "api.profile",
    "profile.cache-refreshed",
    new ProfileCacheRefreshedEvent { AccountId = accountId },
    cancellationToken);
```

`rid`를 이미 알고 있으면 직접 타겟팅 호출도 가능해야 한다.

```csharp
await client.SendToAsync(
    targetRid,
    new RefreshUserCacheCommand { AccountId = accountId },
    cancellationToken);
```

```csharp
var directReply = await client.RequestToAsync<GetUserReply>(
    targetRid,
    new GetUserRequest { AccountId = accountId },
    cancellationToken);
```

```csharp
var timedDirectReply = await client.RequestToAsync<GetUserReply>(
    targetRid,
    new GetUserRequest { AccountId = accountId },
    TimeSpan.FromMilliseconds(200),
    cancellationToken);
```

## 6. handler 시그니처만 따로 보면

request와 send handler는 아래 감각을 기준으로 본다.

```csharp
public sealed class UserHandlers
{
    [ZLinkRequest("user.get")]
    public ValueTask<GetUserReply> GetUserAsync(
        GetUserRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new GetUserReply());
    }

    [ZLinkSend("user.refresh-cache")]
    public ValueTask RefreshCacheAsync(
        RefreshUserCacheCommand command,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}
```

핵심은 raw header를 method 인자로 직접 받지 않는다는 점이다. body는 typed
object로 받고, metadata는 context에서 읽는다.

여기서 중요한 점은 class 이름이 dispatch key가 아니라는 점이다.

- `UserHandlers` 아래에 `user.get`, `user.set`, `user.refresh-cache`를 같이 둘 수 있다.
- `ItemHandlers` 아래에 `item.get`, `item.list`를 같이 둘 수 있다.
- 반대로 패킷 하나당 class 하나로 쪼개고 싶으면 그렇게 해도 된다.

예를 들면 아래처럼도 가능하다.

```csharp
public sealed class UserGetHandler
{
    [ZLinkRequest("user.get")]
    public ValueTask<GetUserReply> HandleAsync(
        GetUserRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new GetUserReply());
    }
}

public sealed class ItemGetHandler
{
    [ZLinkRequest("item.get")]
    public ValueTask<GetItemReply> HandleAsync(
        GetItemRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new GetItemReply());
    }
}
```

즉 framework가 강제하는 것은 class 구조가 아니라 "메시지 이름 하나는 하나의
handler에만 매핑된다"는 규칙이다.

## 7. HTTP handler에서 outbound만 따로 보면

기존 HTTP endpoint에서도 같은 client를 그대로 써야 한다.

```csharp
app.MapPost("/profiles/get", async (
    GetUserHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    var reply = await client.RequestAsync<GetUserReply>(
        "api",
        new GetUserRequest { AccountId = request.AccountId },
        cancellationToken);

    return Results.Ok(reply);
});
```

이 부분이 있어야 기존 웹 요청 처리와 ZLink 서버간 요청 처리가 같은 outbound
표면으로 묶인다.

## 8. `rid` 직접 타겟팅이 필요한 경우

기본 경로는 `serviceName` 기준 호출이다. 이 경우 framework가 service channel 안의
`rid` 집합을 보고 대상을 고른다.

하지만 아래 경우에는 `rid`를 직접 지정하는 오버로드도 필요하다.

- 이미 특정 peer의 `rid`를 알고 있을 때
- sticky session처럼 같은 peer로 다시 보내고 싶을 때
- 분산 정책을 framework가 아니라 응용이 직접 정하고 싶을 때

즉 client 표면은 아래 두 축을 모두 가져야 한다.

- `serviceName` 기준 호출
- `rid` 기준 직접 호출

## 9. 피드백 포인트

이 문서로 피드백을 받을 때는 아래를 보면 된다.

- `IZLinkClient` 시그니처가 충분히 단순한가
- `service_name` 기준 client 표면이 자연스러운가
- `rid` 직접 타겟팅 오버로드가 같이 있어야 하는가
- `serviceId`를 앱 등록 레벨 개념으로 두는 것이 맞는가
- request/send handler 시그니처가 HTTP handler와 비슷하게 느껴지는가
- 주제별 handler 묶음과 패킷별 단일 class를 둘 다 허용하는 것이 자연스러운가
- event publish/subscribe를 같은 응용 안에서 같이 쓰는 흐름이 괜찮은가
- service별 channel 구조가 코드 관점에서도 이해되기 쉬운가
