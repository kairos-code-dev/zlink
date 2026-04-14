[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework ASP.NET Core Service Messaging

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `ASP.NET Core`에서 direct service call과 event
> messaging을 어떤 API로 드러낼지 방향을 정리한다.

## 1. 목표

`ASP.NET Core` 애플리케이션에서 아래 경험을 제공하는 것이 목표다.

- `service_name` 기준 direct call
- typed request/response client
- event publish
- discovery 기반 provider selection
- handler 등록과 DI 통합

즉 사용자는 `DealerSocket`, `RouterSocket`, `Discovery`를 직접 조합하기보다,
`AddZLinkFramework(...)`, typed client, handler registration 같은 표면으로
작업하게 한다.

## 2. 기반이 되는 .NET binding

현재 초안은 아래 `.NET` binding 기능을 하부 토대로 본다.

- `Discovery`
- `DealerSocket`
- `RouterSocket`
- request-reply helper
- `PubSocket` / `SubSocket`

`ZLink Framework`는 이 표면을 감추기보다, 그 위에 더 높은 수준의 프레임워크
통합 API를 얹는 방향으로 본다.

## 3. ASP.NET Core에서 기대하는 등록 방식

### 3.1 서비스 등록

현재 초안은 아래 같은 등록 모양을 우선 가정한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.NodeName = "api-1";
    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
        registry.Add("tcp://registry2:5551");
    });
});
```

이 등록은 framework 전역 runtime, discovery link, codec registry, client factory의
기본 구성을 맡는다.

### 3.2 typed client 등록

```csharp
builder.Services.AddZLinkClient<IProfileClient>("api.profile", client =>
{
    client.DefaultTimeout = TimeSpan.FromSeconds(1);
    client.UseProtobuf();
});
```

핵심은 아래 두 점이다.

- 호출 대상은 gateway 주소가 아니라 `service_name`
- provider 선택은 내부 client-side policy

### 3.3 handler 등록

```csharp
builder.Services.AddZLinkHandlersFromAssemblyContaining<Program>();
```

또는 endpoint-style registration도 가능하지만, 1차 초안은 DI와 attribute 기반을
우선 검토한다.

## 4. 서버 쪽 프로그래밍 모델 초안

### 4.1 request handler

```csharp
public sealed class ProfileHandlers
{
    [ZLinkHandler("profile.get")]
    public async Task<ProfileReply> GetProfileAsync(
        ProfileRequest request,
        ZLinkContext context,
        CancellationToken cancellationToken)
    {
        return await Task.FromResult(new ProfileReply());
    }
}
```

이 모델에서 기대하는 점은 아래와 같다.

- body는 typed object로 역직렬화된다.
- `ZLinkContext`에서 header, correlation, deadline, caller metadata를 읽는다.
- `CancellationToken`으로 timeout/cancel을 연결한다.

### 4.2 event handler

```csharp
public sealed class CacheEventHandlers
{
    [ZLinkEvent("cache.invalidate")]
    public ValueTask HandleAsync(
        CacheInvalidateEvent message,
        ZLinkContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}
```

request-response와 event는 분리된 표면으로 보이는 편이 좋다.

## 5. 클라이언트 쪽 프로그래밍 모델 초안

### 5.1 low-ceremony typed client

```csharp
public interface IProfileClient
{
    Task<ProfileReply> GetAsync(ProfileRequest request,
        CancellationToken cancellationToken = default);
}
```

구현체는 `ZLink Framework`가 DI로 제공한다.

### 5.2 generic request client

typed interface 외에도 범용 client가 필요할 수 있다.

```csharp
public interface IZLinkRequestClient
{
    Task<TResponse> RequestAsync<TRequest, TResponse>(
        string serviceName,
        string method,
        TRequest request,
        CancellationToken cancellationToken = default);
}
```

이 표면은 아래 상황에 유용하다.

- 동적 method 이름
- 여러 service를 runtime에 고르는 경우
- framework 내부 공통 helper

## 6. Discovery와 provider selection

### 6.1 기본 방향

- 호출자는 `service_name`만 지정한다.
- Discovery가 현재 provider 목록을 유지한다.
- client factory 또는 request client가 provider를 선택한다.

### 6.2 왜 중요한가

이 모델의 핵심은 내부 서비스 호출에서 별도 gateway나 전용 load balancer를
강제하지 않는다는 점이다.

즉 아래 방향을 기본으로 본다.

- `IProfileClient`는 gateway 주소가 아니라 `api.profile`에 요청한다.
- `ZLink Framework`가 현재 provider 중 하나를 골라 직접 보낸다.

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
- typed client code generation을 할지
- request client와 event publisher를 얼마나 분리할지
- provider selection policy를 얼마나 공개 설정으로 열지
