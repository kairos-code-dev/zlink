[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [STREAM](./aspnet-core-stream.ko.md)

# Draft -- ZLink Framework .NET Channel Messaging Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` channel messaging 초안을 실제 코드 흐름으로
> 한 번에 보기 위한 샘플 문서다.
> 현재 범위는 `DEALER(client) -> ROUTER(server)` 기반 channel `request/send`와
> 일반 `PUB/SUB`까지만 다룬다. `SPOT`은 여기 넣지 않는다.

## 1. 이 문서의 목적

앞선 문서들은 설명 단위로 나뉘어 있어서, 실제 사용 코드를 한 번에 보기 어렵다.
이 문서는 아래 순서로 샘플을 모아서 보여 준다.

1. channel 등록
2. 공용 outbound client 인터페이스
3. ZLink request/send handler
4. 기존 HTTP handler에서의 사용
5. event subscribe와 publish

피드백은 이 문서의 코드 흐름을 기준으로 받는 것을 목표로 한다.

## 2. channel 등록 샘플부터 보면

framework는 channel마다 역할을 선언하고, request client capability에 대해서는
두 연결 방식을 모두 지원한다. 다만 같은 channel의 request client capability는
자동 연결과 수동 연결 중 하나만 선택해야 한다.

- `Discovery`를 이용한 자동 연결
- endpoint와 `RoutingId`를 직접 넣는 수동 연결

### 2.1 자동 연결 샘플

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();
    options.AddChannel("api", channel =>
    {
        channel.EnableServer();
    });

    options.AddChannel("profile", channel =>
    {
        channel.EnableClient();
    });

    options.AddChannel("account", channel =>
    {
        channel.EnableClient();
    });

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
        registry.Add("tcp://registry2:5551");
    });
});
```

이 경우 runtime은 channel마다 선언한 capability를 만들고, client capability를
둔 channel은 `Discovery` channel view를 붙잡아 provider 집합을 관리한다.
local handler를 등록하지 않으면, 이 단계에서는 outbound `DEALER(client)` runtime만
생긴다. 이 outbound `DEALER(client)`는 framework 관점에서 주로 reply 수신
경로로 본다.

### 2.2 수동 연결 샘플

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();
    options.AddChannel("api", channel =>
    {
        channel.EnableServer();
    });

    options.AddChannel("profile", channel =>
    {
        channel.EnableClient(client =>
        {
            client.UseManualConnections(peers =>
            {
                peers.Connect("tcp://10.0.10.15:7101");

                peers.Connect("tcp://10.0.10.16:7101");
            });
        });
    });
});
```

이 경우 framework가 `Discovery`를 강제하지 않는다. 호출자는 어떤 channel의
client capability에 어떤 peer를 붙일지 직접 정하고, channel은 그 목록만 기준으로
연결을 관리한다. 중요한 점은 이 설정이 `profile` channel 전체가 아니라
`profile.client` 연결 집합에만 적용된다는 점이다.

### 2.3 앱 전체에서는 채널별로 나눠 쓸 수 있다

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddChannel("api", channel =>
    {
        channel.EnableServer();
    });

    options.AddChannel("profile", channel =>
    {
        channel.EnableClient();
    });

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
    });

    options.AddChannel("account", channel =>
    {
        channel.EnableClient(client =>
        {
            client.UseManualConnections(peers =>
            {
                peers.Connect("tcp://10.0.20.15:7101");
            });
        });
    });
});
```

이 draft에서 channel client manual 연결은 remote `RoutingId`를 받지 않는다.
하부 모델이 이미 connect된 `DEALER`를 attach하는 방식이기 때문에, framework
표면도 endpoint 집합만 다루는 편이 맞다.

이 예시는 `profile` channel은 `Discovery` 기반 자동 연결로, `account`
channel은 수동 연결로 나눠 둔 경우다.

중요한 점은 같은 outbound channel에 두 방식을 같이 넣는 것은 허용하지 않는다는
점이다. zlink core에서 `Discovery`가 붙은 `DEALER`는 수동 `connect`를 다시
받지 않으므로, framework도 같은 channel runtime에서 두 방식을 함께 섞지 않는다.

### 2.3.1 런타임 수동 연결 제어 샘플

startup 등록만으로 부족한 경우를 위해, manual capability는 런타임
`Connect` / `Disconnect`도 지원해야 한다.

```csharp
public sealed class WarmupService : BackgroundService
{
    private readonly IZLinkChannelConnectionManager _connections;

    public WarmupService(IZLinkChannelConnectionManager connections)
    {
        _connections = connections;
    }

    protected override Task ExecuteAsync(CancellationToken stoppingToken)
    {
        var profileClient = _connections.GetClient("profile");

        profileClient.Connect(
            RoutingId.Parse("01HZZ..."),
            "tcp://10.0.10.17:7101");

        return Task.CompletedTask;
    }
}
```

이 샘플도 `profile` channel 전체가 아니라, `profile.client` 연결 집합을 제어하는
예시로 읽어야 한다. subscriber capability를 수동으로 운영한다면 그것도 별도
manager를 통해 제어해야 한다.

### 2.3.2 소켓 옵션 설정 샘플

소켓 옵션도 결국 capability가 소유한 runtime 기본값으로 보는 편이 자연스럽다.
즉 요청 하나마다 주는 `WithTimeout(...)`과 같은 호출 단위 옵션과, channel
등록 시점에 넣는 socket 기본 옵션은 구분해서 설명해야 한다.

아래 코드는 아직 확정 계약이 아니라, `.NET` 표면에서 이런 모양으로 보이는 편이
읽기 쉽다는 방향 예시다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();

    options.AddChannel("api", channel =>
    {
        channel.EnableServer(server =>
        {
            server.ConfigureSocket(socket =>
            {
                socket.SendHighWaterMark = 20_000;
                socket.ReceiveHighWaterMark = 20_000;
                socket.SendTimeout = TimeSpan.FromMilliseconds(200);
                socket.ReceiveTimeout = TimeSpan.FromMilliseconds(200);
            });

            server.ConfigureRouter(router =>
            {
                router.Mandatory = true;
                router.RequestTimeout = TimeSpan.FromSeconds(2);
            });
        });

        channel.EnableSubscriber(subscriber =>
        {
            subscriber.ConfigureSocket(socket =>
            {
                socket.ReceiveHighWaterMark = 50_000;
                socket.ReceiveTimeout = TimeSpan.FromMilliseconds(50);
                socket.TcpNoDelay = true;
            });
        });
    });

    options.AddChannel("profile", channel =>
    {
        channel.EnableClient(client =>
        {
            client.ConfigureSocket(socket =>
            {
                socket.ConnectTimeout = TimeSpan.FromSeconds(3);
                socket.ReconnectInterval = TimeSpan.FromMilliseconds(200);
                socket.ReconnectIntervalMax = TimeSpan.FromSeconds(5);
                socket.SendHighWaterMark = 5_000;
                socket.ReceiveHighWaterMark = 5_000;
            });

            client.ConfigureDealer(dealer =>
            {
                dealer.RequestTimeout = TimeSpan.FromMilliseconds(700);
            });
        });
    });

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
    });
});
```

이 예시에서 의도하는 구분은 아래와 같다.

- `server.ConfigureSocket(...)`, `client.ConfigureSocket(...)`는 capability가 들고
  있는 socket 기본 동작을 정한다.
- `server.ConfigureRouter(...)`, `client.ConfigureDealer(...)`는 socket type 전용
  옵션을 따로 둔다는 뜻이다.
- `client.Request(...).WithTimeout(...)`은 특정 호출 하나에만 적용되는 값이고,
  위 설정은 capability 전체의 기본값이다.

이렇게 두면 framework 사용자는 low-level `setsockopt` 이름을 직접 외우지 않아도
되고, 어떤 옵션이 어느 runtime에 적용되는지도 `channel + capability` 기준으로
바로 읽을 수 있다.

### 2.4 outbound-only client 앱도 가능해야 한다

아래처럼 local handler를 전혀 붙이지 않고, 내부 서비스 호출만 하는 앱도 가능해야
한다. 이런 앱은 등록한 remote channel마다 그 channel용 `DEALER(client)`만 만들고,
local `ROUTER(server)`는 열지 않는다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();
    options.AddChannel("profile", channel =>
    {
        channel.EnableClient();
    });

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
    });
});
```

조금 더 완결된 샘플로 쓰면 아래처럼 볼 수 있다.

```csharp
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();
    options.AddChannel("profile", channel =>
    {
        channel.EnableClient();
    });

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
    });
});

var app = builder.Build();

app.MapPost("/profiles/get", async (
    GetProfileHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    var reply = await client
        .Request(
            "profile",
            new GetProfileRequest { AccountId = request.AccountId })
        .ExecAsync(cancellationToken);

    return Results.Ok(reply);
});

app.Run();
```

이 outbound-only 예시에서도 일반 handler dispatch는 없다. `IZLinkClient`가
사용하는 outbound `DEALER(client)`는 request를 보내고 reply를 받는 경로로
동작한다.

## 3. 한 번에 보는 전체 예시

아래 코드는 하나의 `ASP.NET Core` 애플리케이션 안에서

- `api` 서버군에 속한 앱이 ZLink handler를 받고
- 필요하면 다른 내부 channel로 outbound 요청을 보내고
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
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();
    options.AddChannel("api", channel =>
    {
        channel.EnableServer();
        channel.EnableSubscriber();
    });

    options.AddChannel("profile", channel =>
    {
        channel.EnableClient();
    });

    options.AddChannel("account", channel =>
    {
        channel.EnableClient();
    });

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
    var reply = await client
        .Request(
            "profile",
            new GetProfileRequest { AccountId = request.AccountId })
        .ExecAsync(cancellationToken);

    return Results.Ok(reply);
});

app.MapPost("/profiles/refresh-cache", async (
    RefreshProfileCacheHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    client
        .Send(
            "profile",
            new RefreshProfileCacheCommand { AccountId = request.AccountId })
        .Exec();

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

    [ZLinkRequest]
    public async ValueTask<GetUserReply> GetUserAsync(
        GetUserRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var account = await _client
            .Request(
                "account",
                new GetAccountRequest { AccountId = request.AccountId })
            .ExecAsync(cancellationToken);

        return new GetUserReply
        {
            AccountId = request.AccountId,
            Nickname = account.Nickname
        };
    }

    [ZLinkSend]
    public async ValueTask RefreshCacheAsync(
        RefreshUserCacheCommand command,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        _publisher
            .Publish(
                "api",
                "user.cache-refreshed",
                new UserCacheRefreshedEvent
                {
                    AccountId = command.AccountId
                })
            .Exec();
    }
}

public sealed class ItemHandlers
{
    [ZLinkRequest]
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
    [ZLinkEvent(PacketName = "user.cache-refreshed")]
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

public sealed class GetUserRequest : IZLinkRequest<GetUserReply>
{
    public long AccountId { get; set; }
}

public sealed class GetUserReply
{
    public long AccountId { get; set; }
    public string Nickname { get; set; } = "";
}

public sealed class GetAccountRequest : IZLinkRequest<GetAccountReply>
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

이 전체 예시에서도 실제 request/send handler dispatch는 local `ROUTER(server)`가
받은 메시지에 대해서만 일어난다. 반대로 outbound `DEALER(client)`가 받는
메시지는 먼저 보낸 request의 reply를 완료하는 경로로 본다. 현재 초안은
`ROUTER -> DEALER` 임의 push를 channel messaging 공용 API에 넣지 않는다.

## 4. 이 샘플을 어떻게 읽으면 되는가

이 샘플에서 중요한 부분은 아래 여섯 가지다.

- `IZLinkClient`는 하나만 주입받는다.
- 요청 대상은 endpoint가 아니라 `channel name`이다.
- local handler를 등록한 경우에만 이 앱은 `api` channel에서 server 역할을 한다.
- runtime은 channel마다 선언한 capability에 맞는 runtime만 만든다.
- `account`, `profile`처럼 client capability를 둔 channel은 그 channel 전용
  `Discovery`와 outbound `DEALER(client)` socket을 가진다.
- 기본 packet key는 payload 타입 이름이고, timeout/packet override는 builder에
  이어 붙인다.
- 같은 `IZLinkClient`를 ZLink handler와 HTTP handler가 함께 쓴다.
- handler class는 `UserHandlers`, `ItemHandlers`처럼 주제별로 묶어도 된다.

즉 응용 코드 입장에서는 공용 client 하나만 보이지만, framework 내부에서는
channel별 outbound 경로가 분리되어 관리된다.

그리고 handler class는 dispatch key가 아니라 **코드 조직 단위**다. 실제 dispatch는
기본적으로 `GetUserRequest`, `GetItemRequest` 같은 payload 타입 이름으로 이뤄진다.

## 5. client와 publisher 인터페이스

위 예시가 전제하는 `IZLinkClient`, `IZLinkEventPublisher`, `IZLinkRequest<TReply>`
전체 정의는 [handler-interfaces.ko.md](./handler-interfaces.ko.md)의 section 5와
section 9를 참고한다.

이 샘플은 기본 packet key를 payload 타입 이름으로 해석하는 규칙을 전제로 한다.
즉 `GetUserRequest`는 기본적으로 `GetUserRequest`, `RefreshUserCacheCommand`는
기본적으로 `RefreshUserCacheCommand` packet으로 매핑된다. 기본 이름이 맞지 않는
경우에만 `PacketName` override를 쓴다.

## 6. 함수 호출 예시

위 인터페이스는 실제 코드에서 아래처럼 호출된다.

```csharp
client
    .Send(
        "profile",
        new RefreshProfileCacheCommand { AccountId = accountId })
    .Exec();
```

```csharp
var reply = await client
    .Request(
        "profile",
        new GetProfileRequest { AccountId = accountId })
    .ExecAsync(cancellationToken);
```

```csharp
var fastReply = await client
    .Request(
        "profile",
        new GetProfileRequest { AccountId = accountId })
    .WithTimeout(TimeSpan.FromMilliseconds(200))
    .ExecAsync(cancellationToken);
```

```csharp
client
    .Send(
        "profile",
        new RefreshProfileCacheCommand { AccountId = accountId })
    .WithPacketName("profile.refresh-cache")
    .Exec();
```

### 6.1 framework client의 reply 처리 기준

이 문서에서 다루는 framework client는 reply를 raw `Message` part 목록으로 노출하지
않고, 요청한 typed reply로 바로 돌려주는 표면을 기준으로 본다.

즉 framework 사용자는 하부 `.NET` binding의 `DealerSocket.RequestAsync(...)`처럼
`IReadOnlyList<Message>`를 직접 받아서 parse하지 않는다. 그런 raw parse 예시는
binding 문서에서 다루고, framework 문서에서는 아래처럼 typed reply 표면만
설명한다.

```csharp
GetProfileReply reply = await client
    .Request(
        "profile",
        new GetProfileRequest { AccountId = accountId })
    .ExecAsync(cancellationToken);
```

```csharp
publisher
    .Publish(
        "profile",
        "profile.cache-refreshed",
        new ProfileCacheRefreshedEvent { AccountId = accountId })
    .Exec();
```

이 예시에서 첫 번째 문자열 `profile`은 publish 대상 `channelName`이고, 두 번째
문자열 `profile.cache-refreshed`는 그 channel 안의 `topic`이다. 즉 같은
`profile` channel 안에서도 여러 topic을 fan-out 할 수 있다.

## 7. handler 시그니처만 따로 보면

request와 send handler는 아래 감각을 기준으로 본다.

```csharp
public sealed class UserHandlers
{
    [ZLinkRequest]
    public ValueTask<GetUserReply> GetUserAsync(
        GetUserRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new GetUserReply());
    }

    [ZLinkSend]
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

여기서 중요한 점은 class 이름이 아니라 request/message payload 타입 이름이
기본 dispatch key라는 점이다.

- `UserHandlers` 아래에 여러 request/send handler를 같이 둘 수 있다.
- `ItemHandlers` 아래에 여러 request handler를 같이 둘 수 있다.
- 반대로 패킷 하나당 class 하나로 쪼개고 싶으면 그렇게 해도 된다.

예를 들면 아래처럼도 가능하다.

```csharp
public sealed class UserGetHandler
{
    [ZLinkRequest]
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
    [ZLinkRequest]
    public ValueTask<GetItemReply> HandleAsync(
        GetItemRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(new GetItemReply());
    }
}
```

즉 framework가 강제하는 것은 class 구조가 아니라 "resolved packet key 하나는
하나의 handler에만 매핑된다"는 규칙이다.

## 8. HTTP handler에서 outbound만 따로 보면

기존 HTTP endpoint에서도 같은 client를 그대로 써야 한다.

```csharp
app.MapPost("/profiles/get", async (
    GetUserHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    var reply = await client
        .Request(
            "api",
            new GetUserRequest { AccountId = request.AccountId })
        .ExecAsync(cancellationToken);

    return Results.Ok(reply);
});
```

이 부분이 있어야 기존 웹 요청 처리와 ZLink 서버간 요청 처리가 같은 outbound
표면으로 묶인다.

## 8. 피드백 포인트

이 문서로 피드백을 받을 때는 아래를 보면 된다.

- `IZLinkClient` 시그니처가 충분히 단순한가
- `channel name` 기준 client 표면이 자연스러운가
- `channelName`을 앱 등록 레벨 개념으로 두는 것이 맞는가
- `Request(...).WithTimeout(...).ExecAsync()` 같은 fluent 호출 구성이
  자연스러운가
- `Send(...).WithPacketName(...).Exec()`와 `Publish(...).Exec()`
  모양이 자연스러운가
- request/send handler 시그니처가 HTTP handler와 비슷하게 느껴지는가
- 주제별 handler 묶음과 패킷별 단일 class를 둘 다 허용하는 것이 자연스러운가
- event publish/subscribe를 같은 응용 안에서 같이 쓰는 흐름이 괜찮은가
- channel별 구조가 코드 관점에서도 이해되기 쉬운가
