<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework for .NET — 개요와 시작](./01-overview.ko.md) | [다음: 핵심 개념 — .NET 표면 멘탈 모델](./03-concepts.ko.md)
<!-- framework-adapter-nav:end -->

# Getting Started

> 정식 등록 시그니처와 옵션 전체는 [spec/aspnet-core-channel-messaging](../spec/aspnet-core-channel-messaging.ko.md)와
> [spec/handler-interfaces](../spec/handler-interfaces.ko.md)가 소유한다. 이
> 문서는 "처음 한 번 띄워 보는 것"만 다룬다.

## 1. 사전 조건

- 런타임: `.NET 8`(`net8.0`) 이상, 주 개발 기준 `.NET 10`.
- 언어: `C# 12` 이상.
- 패키지: `Systems.Zlink.Framework` (역순 도메인 규칙). 자세한 버전·CI 플랫폼
  기준은 [.NET 문서 진입점](../README.ko.md) §1.1을 참고한다.

## 2. 최소 예제 — 서버 간 요청/응답

두 개의 `ASP.NET Core` 앱(`price-server`, `caller`)으로 가정한다.

### 2.1 메시지 타입 (공유)

```csharp
public sealed record PriceReq(string Symbol);
public sealed record PriceRes(string Symbol, decimal Price);
```

### 2.2 서버: handler + channel 등록

```csharp
[ZLinkRequest]
public sealed class GetPriceHandler : IZLinkRequestHandler<PriceReq, PriceRes>
{
    public ValueTask<PriceRes> HandleAsync(
        PriceReq request, ZLinkRequestContext context, CancellationToken ct)
        => ValueTask.FromResult(new PriceRes(request.Symbol, 42m));
}

// Program.cs
builder.Services.AddZLinkFramework(options =>
{
    options.AddClientServerChannel("price", channel => channel.EnableServer());
});
```

`[ZLinkRequest]` handler는 attribute scan으로 발견되지만, **어느 channel로
노출할지는** `EnableServer(...)`를 선언한 channel 등록이 소유한다. handler
attribute는 channel 이름을 인자로 받지 않는다.

### 2.3 호출자: outbound client

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddClientServerChannel("price", channel => channel.EnableClient());
});

public sealed class Caller(IZLinkClient client)
{
    public async Task<decimal> AskAsync(CancellationToken ct)
    {
        var res = await client
            .Request("price", new PriceReq("AAPL"))
            .SubmitAsync<PriceRes>(ct);
        return res.Price;
    }
}
```

`EnableClient()`만 선언한 앱은 inbound handler 없이 outbound 전용으로 동작한다.

## 3. 실행과 확인

1. 두 앱을 띄운다. `price` channel의 위치는 channel별 `Discovery`가 해결하므로
   호출자는 서버 주소를 직접 지정하지 않는다(수동 연결은 [03-concepts](./03-concepts.ko.md) §5).
2. `Caller.AskAsync(...)`가 `42`를 돌려주면 inbound handler dispatch까지 정상이다.
3. 동작이 안 보이면 runtime 이벤트로 진단한다 →
   [spec/aspnet-core-monitoring](../spec/aspnet-core-monitoring.ko.md).

## 4. 다음 단계

| 하고 싶은 것 | 가는 곳 |
|--------------|---------|
| 표면 개념을 정리 | [03-concepts](./03-concepts.ko.md) |
| 기능별 난이도·선택 | [04-feature-map](./04-feature-map.ko.md) |
| 등록·handler 전체 계약 | [spec/aspnet-core-channel-messaging](../spec/aspnet-core-channel-messaging.ko.md) |
| 실행 가능한 전체 예제 | [guide/samples](./samples/channel-messaging-samples.ko.md) |
