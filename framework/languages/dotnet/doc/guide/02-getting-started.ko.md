<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework for .NET — 개요](./01-overview.ko.md) | [다음: 핵심 개념 — .NET 표면 멘탈 모델](./03-concepts.ko.md)
<!-- framework-adapter-nav:end -->

# Getting Started — 처음 한 번 띄워 보기

> 이 문서는 "두 개의 `ASP.NET Core` 앱을 띄워 서로 호출되는 것까지" 가는 가장
> 짧은 경로를 다룬다. 등록 시그니처와 옵션 전체는
> [04-channel-messaging](./04-channel-messaging.ko.md)과
> [spec/aspnet-core-channel-messaging](../spec/aspnet-core-channel-messaging.ko.md)이
> 소유한다.
>
> 🔰 **용어가 낯설면** [03-concepts §0 "용어 빠르게 잡기"](./03-concepts.ko.md)를
> 먼저 펼쳐 두고 읽으면 channel·handler·client·SPOT 같은 단어가 바로 잡힌다.

## 1. 사전 조건

| 항목 | 기준 |
|------|------|
| 최소 런타임 | `.NET 8` (`net8.0`) |
| 주 개발 기준 | `.NET 10` (`net10.0`) |
| 최소 언어 버전 | `C# 12` |
| 패키지 | `Systems.Zlink.Framework` |

지원 RID(`win-x64`, `linux-x64`, `osx-arm64` 등 6종)와 CI 기준은
[.NET 문서 진입점](../README.ko.md) §1.1 을 참고한다.

```bash
dotnet add package Systems.Zlink.Framework
```

## 2. 토폴로지

이 예제는 두 개의 앱으로 구성한다.

```mermaid
flowchart LR
  Caller["caller 앱<br/>(EnableClient)"] -- "Request(\"price\", ...)" --> PriceServer["price-server 앱<br/>(EnableServer + handler)"]
```

- `price-server` : `price` channel 에 server capability 를 열고 handler 를 둔다.
- `caller` : `price` channel 에 client capability 만 열고 호출한다.

두 앱은 서로의 주소를 직접 모른다. 위치는 `Discovery`(또는 수동 연결)가 해결한다.

## 3. 공유 메시지 타입

두 앱이 공유하는 DTO 다. 단순 POCO/record 면 충분하다.

```csharp
public sealed record PriceRequest(string Symbol);
public sealed record PriceReply(string Symbol, decimal Price);
```

## 4. price-server: handler + channel 등록

```csharp
// Program.cs (price-server)
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.AddClientServerChannel("price", channel =>
    {
        channel.EnableServer(server => server.Bind("tcp://0.0.0.0:7301"));
        channel.AddRequestHandler<GetPriceHandler>();
    });

    // 위치 해결: 같은 Registry 를 가리키게 한다.
    options.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://127.0.0.1:5551"));
});

var app = builder.Build();
app.Run();

public sealed class GetPriceHandler
    : IZLinkRequestHandler<PriceRequest, PriceReply>
{
    public ValueTask<PriceReply> HandleAsync(
        PriceRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        // 실제로는 시세 캐시/DB 조회. 여기서는 고정값.
        return ValueTask.FromResult(new PriceReply(request.Symbol, 187.42m));
    }
}
```

핵심 두 가지:

- **server capability 는 `Bind(...)` 가 필수**다. 다른 프로세스가 접근할 local
  endpoint 가 있어야 한다.
- handler 를 channel 에 노출하는 방법은 두 가지다. 위 예제처럼
  `AddRequestHandler<THandler>()`로 **직접 등록**하거나, handler class 에 group 을
  붙이고 `AddHandlerGroup(...)`으로 노출한다. 자세한 차이는
  [04-channel-messaging](./04-channel-messaging.ko.md) §3 에서 다룬다.
- handler class 자체는 interface 기반과 attribute 기반 중 하나로 작성할 수 있다.
  interface 기반은 컴파일 타임 타입 체크가 강하고, attribute 기반은 한 class 에 여러
  handler 메서드를 묶기 쉽다.

## 5. caller: outbound client

```csharp
// Program.cs (caller)
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.AddClientServerChannel("price", channel => channel.EnableClient());
    options.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://127.0.0.1:5551"));
});

var app = builder.Build();

app.MapGet("/price/{symbol}", async (
    string symbol,
    IZLinkChannelClient client,
    CancellationToken cancellationToken) =>
{
    var reply = await client
        .RequestToChannel("price", new PriceRequest(symbol))
        .SubmitAsync<PriceReply>(cancellationToken);

    return Results.Ok(reply);
});

app.Run();
```

`EnableClient()`만 선언한 앱은 inbound handler 없이 **outbound 전용**으로
동작한다. `Bind(...)`는 필요 없다.

## 6. Registry 띄우기

`Discovery`가 위치를 해결하려면 Registry 서버가 하나 떠 있어야 한다. 가장 간단한
방법은 별도 Registry 앱이다.

```csharp
// Program.cs (registry)
var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkRegistry(registry =>
{
    registry.PubEndpoint = "tcp://0.0.0.0:5550";
    registry.RouterEndpoint = "tcp://0.0.0.0:5551";
});

builder.Build().Run();
```

운영 배포 모델(embedded/standalone)과 topology 조회는
[08-registry](./08-registry.ko.md)에서 자세히 다룬다. 수동 연결만으로 Registry
없이 붙이는 방법은 [03-concepts](./03-concepts.ko.md) §5 에 있다.

## 7. 실행과 확인

1. `registry` → `price-server` → `caller` 순으로 띄운다.
2. `caller` 에 `GET /price/AAPL` 을 호출한다.
3. `{ "symbol": "AAPL", "price": 187.42 }` 가 돌아오면 Discovery 연결과 inbound
   handler dispatch 까지 정상이다.

## 8. 잘 안 될 때

| 증상 | 점검 |
|------|------|
| 호출이 timeout 으로 떨어진다 | `price-server` 가 떴는지, Registry endpoint(`5551`)를 양쪽이 같이 가리키는지 |
| `ZLinkConfigurationException` | client capability 에 `UseDiscovery`도 수동 연결도 없는 경우. 둘 중 하나는 있어야 한다 |
| 시작 시 예외 | channel 이름 중복, 같은 channel 안 `kind + packet name` 중복 → 시작 단계에서 막힌다([03-concepts](./03-concepts.ko.md) §4) |
| 그래도 안 보이면 | runtime 이벤트로 진단 → [09-monitoring](./09-monitoring.ko.md) |

## 9. 다음 단계

| 하고 싶은 것 | 가는 곳 |
|--------------|---------|
| 표면 개념 정리(channel, capability, DI) | [03-concepts](./03-concepts.ko.md) |
| request/send/pub-sub 전체 사용법 | [04-channel-messaging](./04-channel-messaging.ko.md) |
| room/stage 같은 동적 노드 | [05-spot](./05-spot.ko.md) |
| 외부 game/mobile client 받기 | [07-stream](./07-stream.ko.md) |
| 실행 가능한 전체 예제 | [guide/samples](./samples/channel-messaging-samples.ko.md) |
