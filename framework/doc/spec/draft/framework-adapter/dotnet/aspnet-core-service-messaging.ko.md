[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework ASP.NET Core Service Messaging

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `ASP.NET Core`에서 direct service call과 event
> messaging을 어떤 API로 드러낼지 방향을 정리한다.

## 1. 목표

`ASP.NET Core` 애플리케이션에서 아래 경험을 제공하는 것이 목표다.

- `service_name` 기준 direct call
- DI로 주입되는 공용 outbound client
- event publish
- service별 discovery channel 기반 요청
- handler 등록과 DI 통합

여기서 outbound client는 ZLink 메시지 handler 안에서도, 기존 ASP.NET Core HTTP
handler/controller 안에서도 똑같이 쓸 수 있어야 한다.

즉 사용자는 `DealerSocket`, `RouterSocket`, `Discovery`를 직접 조합하기보다,
`AddZLinkFramework(...)`, `IZLinkClient`, handler registration 같은 표면으로
작업하게 한다.

등록부터 handler, HTTP endpoint, outbound 호출까지 이어서 보는 샘플은
[service-messaging-samples.ko.md](./service-messaging-samples.ko.md)를
참고한다.

## 2. 기반이 되는 .NET binding

현재 초안은 아래 `.NET` binding 기능을 하부 토대로 본다.

- `Discovery`
- `DealerSocket`
- `RouterSocket`
- request-reply helper
- `PubSocket` / `SubSocket`

`ZLink Framework`는 이 표면을 감추기보다, 그 위에 service별 channel을 관리하는
더 높은 수준의 프레임워크 통합 API를 얹는 방향으로 본다.

## 3. ASP.NET Core에서 기대하는 등록 방식

### 3.1 서비스 등록

현재 초안은 아래 같은 등록 모양을 우선 가정한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.ServiceId = "api";
    options.NodeName = "api-1";
    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
        registry.Add("tcp://registry2:5551");
    });
});
```

이 등록은 framework 전역 runtime, service channel factory, codec registry의 기본
구성을 맡는다.

### 3.2 outbound client 등록

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();
});
```

핵심은 아래 네 점이다.

- `IZLinkClient`는 DI로 주입된다.
- 호출 대상은 gateway 주소가 아니라 `service_name`
- runtime은 접근한 `service_name`마다 별도 outbound channel을 만든다.
- 각 channel은 그 service 전용 `Discovery`와 outbound socket을 가진다.

### 3.3 handler 등록

```csharp
builder.Services.AddZLinkHandlersFromAssemblyContaining<Program>();
```

또는 endpoint-style registration도 가능하지만, 1차 초안은 DI와 attribute 기반을
우선 검토한다.

현재 1차 attribute 후보는 아래처럼 둔다.

```csharp
[ZLinkRequest("profile.get")]
[ZLinkSend("profile.refresh-cache")]
```

여기서 `serviceId`는 route prefix가 아니라 앱이 속한 서버군 식별자이므로,
handler class attribute보다 등록 옵션에 두는 편을 현재 방향으로 본다.

## 4. 서버 쪽 프로그래밍 모델 초안

실제 handler 인터페이스 초안은 [handler-interfaces.ko.md](./handler-interfaces.ko.md)를
기준으로 본다. 이 문서는 그중 `ASP.NET Core` 매핑 경험을 설명하는 쪽에 집중한다.

### 4.1 request handler

```csharp
public sealed class UserHandlers
{
    private readonly IZLinkClient _client;

    public UserHandlers(IZLinkClient client)
    {
        _client = client;
    }

    [ZLinkRequest("user.get")]
    public async ValueTask<UserReply> GetUserAsync(
        UserRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var account = await _client.RequestAsync<GetAccountReply>(
            "api.account",
            new GetAccountRequest { AccountId = request.AccountId },
            cancellationToken);

        return new UserReply
        {
            AccountId = request.AccountId,
            Nickname = account.Nickname
        };
    }
}
```

이 모델에서 기대하는 점은 아래와 같다.

- body는 typed object로 역직렬화된다.
- `ZLinkContext`에서 header, correlation, deadline, caller metadata를 읽는다.
- `CancellationToken`으로 timeout/cancel을 연결한다.
- handler class는 `UserHandlers`, `ItemHandlers`처럼 주제별로 묶어도 된다.
- 패킷 하나당 class 하나로 쪼개도 된다.
- 실제 dispatch key는 class 이름이 아니라 `user.get` 같은 메시지 이름이다.

### 4.2 event handler

```csharp
public sealed class CacheEventHandlers
{
    [ZLinkEvent("cache.invalidate")]
    public ValueTask HandleAsync(
        CacheInvalidateEvent message,
        ZLinkEventContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}
```

request-response와 event는 분리된 표면으로 보이는 편이 좋다.

현재 기준으로는 아래 세 인터페이스를 먼저 고정하는 편이 낫다.

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

public interface IZLinkClient
{
    ValueTask SendAsync<TMessage>(
        string serviceName,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestAsync<TResponse>(
        string serviceName,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestAsync<TResponse>(
        string serviceName,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToAsync<TMessage>(
        RoutingId targetRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestToAsync<TResponse>(
        RoutingId targetRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestToAsync<TResponse>(
        RoutingId targetRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToSpotAsync<TMessage>(
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestToSpotAsync<TResponse>(
        RoutingId spotRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestToSpotAsync<TResponse>(
        RoutingId spotRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToSpotAsync<TMessage>(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestToSpotAsync<TResponse>(
        RoutingId targetRid,
        RoutingId spotRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestToSpotAsync<TResponse>(
        RoutingId targetRid,
        RoutingId spotRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);
}
```

결론적으로 `.NET` 앞면은 "인터페이스 + attribute 둘 다 가능하지만, 일반 사용자는
attribute 매핑과 `IZLinkClient`를 함께 쓴다"를 기본으로 본다.

이 문서는 현재 `ROUTER <-> ROUTER` 기반 서버간 request/send와 일반 `PUB/SUB`
설계를 중심으로 다룬다. 다만 하부 C API가 허용하는 범위 때문에 `IZLinkClient`
시그니처에는 `spot rid` direct 호출 함수도 함께 포함한다. `SPOT` lifecycle과
handler 모델 자체는 별도 문서에서 따로 다룬다.

## 5. 클라이언트 쪽 프로그래밍 모델 초안

### 5.1 generic outbound client

```csharp
public interface IZLinkClient
{
    ValueTask SendAsync<TMessage>(
        string serviceName,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestAsync<TResponse>(
        string serviceName,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestAsync<TResponse>(
        string serviceName,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToAsync<TMessage>(
        RoutingId targetRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestToAsync<TResponse>(
        RoutingId targetRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestToAsync<TResponse>(
        RoutingId targetRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToSpotAsync<TMessage>(
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestToSpotAsync<TResponse>(
        RoutingId spotRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestToSpotAsync<TResponse>(
        RoutingId spotRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);

    ValueTask SendToSpotAsync<TMessage>(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestToSpotAsync<TResponse>(
        RoutingId targetRid,
        RoutingId spotRid,
        object request,
        CancellationToken cancellationToken = default);

    ValueTask<TResponse> RequestToSpotAsync<TResponse>(
        RoutingId targetRid,
        RoutingId spotRid,
        object request,
        TimeSpan timeout,
        CancellationToken cancellationToken = default);
}
```

구현체는 `ZLink Framework`가 DI로 제공한다.
이 표면은 `playhouse`의 sender/link 스타일을 서비스 이름 기반 호출에 맞게
다시 올린 형태로 볼 수 있다.
또한 일반 client도 하부 C API가 허용하는 범위에서는 `spot rid` 대상 send/request
함수를 함께 가진다. 다만 `IZLinkSpotClient`와 같은 인터페이스라는 뜻은 아니다.
두 인터페이스는 서로 다른 C API 위에서 각각 구현되며, 필요한 기능이 겹치는 부분만
공통으로 가진다.

### 5.2 HTTP handler에서의 사용

이 client는 ZLink handler 안에서만 쓰는 것이 아니다. 기존 ASP.NET Core HTTP
handler에서도 그대로 주입받아 쓸 수 있어야 한다.

```csharp
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
```

이 표면은 아래 상황에 유용하다.

- 기존 웹 요청 처리 중 다른 내부 서버 호출
- ZLink handler와 HTTP handler가 같은 outbound 호출 방식을 공유
- framework 내부 공통 helper
- 이미 알고 있는 `rid`로 직접 요청 또는 전송
- 특정 요청만 별도 timeout으로 보내기

## 6. Discovery와 service channel

### 6.1 기본 방향

- 호출자는 `service_name`만 지정한다.
- `IZLinkClient`는 접근한 `service_name`마다 별도 channel을 lazy하게 만든다.
- 각 channel은 그 service view에 묶인 `Discovery`와 outbound socket을 가진다.
- Discovery가 현재 service view의 provider 목록을 유지한다.
- framework는 그 service의 `rid` 집합과 연결 상태를 보고 요청을 보낸다.
- 필요하면 응용이 `RoutingId`를 직접 넣어 특정 peer를 바로 지정할 수도 있다.
- 필요하면 운영 점검용 별도 서비스가 `Registry` snapshot/query 결과를 읽어 현재
  topology를 노출할 수 있다.

### 6.2 왜 중요한가

이 모델의 핵심은 내부 서비스 호출에서 별도 gateway나 전용 load balancer를
강제하지 않으면서도, core의 fixed service view 철학을 그대로 따른다는 점이다.

즉 아래 방향을 기본으로 본다.

- `IZLinkClient`는 gateway 주소가 아니라 `service_name`으로 요청한다.
- `ZLink Framework`는 그 service 전용 channel로 직접 요청을 보낸다.
- 같은 service 안의 여러 provider는 그 channel 안에서만 관리한다.

## 7. codec과 message model

현재 초안은 아래 구성을 가정한다.

- message = `header + body`
- body codec = `protobuf` 또는 `json`

`.NET` 표면에서는 codec 등록과 serializer 선택을 아래처럼 보이게 할 수 있다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.AddProtobuf();
    options.Codecs.AddJson();
});
```

## 8. lifecycle 초안

`ASP.NET Core`에서는 아래 lifecycle이 중요하다.

- 앱 시작 시 runtime 부팅
- discovery 연결 수립
- handler dispatcher 시작
- 앱 종료 시 graceful shutdown

따라서 내부 구현은 `IHostedService` 또는 그와 비슷한 hosted lifecycle 모델과
잘 맞아야 한다.

## 9. 아직 확정하지 않는 것

- attribute model과 endpoint model 중 어느 쪽을 우선할지
- 서비스별 typed wrapper를 공식 제공할지
- 일반 서버간 쪽에서 `IZLinkClient`와 event publisher를 어떤 이름으로 확정할지
- service channel을 언제 만들고 언제 정리할지
- topology query surface를 운영 API로만 둘지, 일반 DI 서비스로도 열지
