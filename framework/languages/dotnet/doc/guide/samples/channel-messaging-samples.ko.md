<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../doc/README.ko.md) | [이전: ZLink Framework ASP.NET Core Registry Integration](../../spec/aspnet-core-registry.ko.md) | [다음: ZLink Framework .NET SPOT Samples](./spot-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../../doc/spec/draft/README.ko.md)

[.NET 묶음](../../README.ko.md) | [인터페이스](../../spec/handler-interfaces.ko.md) | [channel](../../spec/aspnet-core-channel-messaging.ko.md) | [SPOT](../../spec/aspnet-core-spot.ko.md) | [STREAM](../../spec/aspnet-core-stream.ko.md)

# ZLink Framework .NET Channel Messaging Samples

## 1. 이 문서의 목적

이 절에서는 이 샘플 문서를 어떤 흐름으로 읽으면 되는지 짧게 안내한다.

앞선 문서들은 설명 단위로 잘게 나뉘어 있다. 그래서 실제 사용 코드를 한 번에 보기는
어렵다. 이 문서는 그 흐름을 한 자리에 모아 둔다. 순서는 다음과 같다.

1. channel[^channel] 등록
2. 공용 outbound client[^outbound] 인터페이스
3. ZLink request / send handler[^handler]
4. 기존 HTTP handler 에서의 사용
5. event subscribe 와 publish[^pubsub]

피드백은 이 문서의 코드 흐름을 기준으로 받는 것을 목표로 한다.

## 2. channel 등록 샘플부터 보면

이 절에서는 channel 등록의 두 가지 방식, 그리고 한 앱 안에서 이 둘을 어떻게 골라 쓰는지를
샘플로 정리한다.

framework 는 channel 마다 역할을 선언하게 되어 있다. request client capability[^capability]
에 대해서는 두 가지 방식을 모두 지원한다.

- `Discovery`[^discovery] 를 이용한 자동 연결
- endpoint 집합만 등록하는 수동 연결

다만 한 가지 제약이 있다. 같은 channel 의 request client capability 안에서는, 자동
연결과 수동 연결 중 하나만 골라야 한다.

### 2.1 자동 연결 샘플

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();
    options.AddClientServerChannel("api", channel =>
    {
        channel.EnableServer(server =>
        {
            server.Bind("tcp://0.0.0.0:7101");
        });
        channel.MapHandlerGroup("api");
    });

    options.AddClientServerChannel("profile", channel =>
    {
        channel.EnableClient();
    });

    options.AddClientServerChannel("account", channel =>
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

이 경우 runtime 의 동작은 다음과 같다.

- channel 별로 선언한 capability 를 만든다.
- client capability 를 둔 channel 에 대해서는, `Discovery` channel view 를 붙잡아
  provider 집합을 관리한다.

local handler 를 등록하지 않은 상태라면, 이 단계에서는 outbound `DEALER(client)`
runtime 만 생긴다. 이 outbound `DEALER(client)` 는, framework 입장에서 주로 reply
수신 경로로 본다.

### 2.2 수동 연결 샘플

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();
    options.AddClientServerChannel("api", channel =>
    {
        channel.EnableServer(server =>
        {
            server.Bind("tcp://0.0.0.0:7101");
        });
        channel.MapHandlerGroup("api");
    });

    options.AddClientServerChannel("profile", channel =>
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

이 경우 framework 는 `Discovery` 를 강제하지 않는다. 호출자가, 어떤 channel 의 client
capability 에 어떤 peer 를 붙일지 직접 정한다. channel 은 그 목록만 가지고 연결을
관리한다.

여기서 짚어 둘 점이 하나 있다. 이 설정은 `profile` channel 전체가 아니라,
`profile.client` 연결 집합에만 적용된다.

### 2.3 앱 전체에서는 channel별로 나눠 쓸 수 있다

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddClientServerChannel("api", channel =>
    {
        channel.EnableServer(server =>
        {
            server.Bind("tcp://0.0.0.0:7101");
        });
        channel.MapHandlerGroup("api");
    });

    options.AddClientServerChannel("profile", channel =>
    {
        channel.EnableClient();
    });

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
    });

    options.AddClientServerChannel("account", channel =>
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

이 문서에서 channel client 수동 연결은, remote `RoutingId`[^rid] 를 받지 않는다. 이유는
다음과 같다. 하부 모델이 이미 connect 된 `DEALER` 를 attach 하는 방식이라, framework
표면도 endpoint 집합만 다루는 편이 자연스럽기 때문이다.

이 예시의 구성은 다음과 같다.

- `profile` channel 은 `Discovery` 기반 자동 연결로 둔다.
- `account` channel 은 수동 연결로 둔다.

여기서 핵심은 한 가지다. 같은 outbound channel 안에 두 방식을 함께 넣는 것은 허용하지
않는다.

이유는 zlink core 의 동작 때문이다. `Discovery` 가 붙은 `DEALER` 는, 수동 `connect` 를
다시 받지 않는다. 그래서 framework 도 같은 channel runtime 안에서 두 방식을 함께 섞지
않는다.

### 2.3.1 런타임 수동 연결 제어 샘플

startup 등록만으로는 부족한 경우가 있다. 이 절에서는 런타임에서 연결을 동적으로
바꿔야 하는 예시를 다룬다.

이를 위해 manual capability 는, 런타임의 `Connect` / `Disconnect` 도 함께 지원해야 한다.

```csharp
public sealed class WarmupService : BackgroundService
{
    private readonly IZLinkChannelConnectionManager _connections;

    public WarmupService(IZLinkChannelConnectionManager connections)
    {
        _connections = connections;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        IZLinkEndpointConnections profileConnections = await _connections
            .GetClientServerClientAsync("profile", stoppingToken);

        await profileConnections.ConnectAsync(
            "tcp://10.0.10.17:7101",
            stoppingToken);
    }
}
```

이 샘플도 `profile` channel 전체가 아니라, `profile.client` 연결 집합을 제어하는
예시로 읽어야 한다.

subscriber capability 를 수동으로 운영한다면 어떻게 되는가. 그쪽은 그쪽대로, 별도
manager 를 통해 제어해야 한다.

### 2.3.2 소켓 옵션 설정 샘플

소켓 옵션도 결국, capability 가 소유한 runtime 의 기본값으로 보는 편이 자연스럽다.
즉 두 가지를 구분해서 설명해야 한다.

- 요청 하나마다 주는 `Timeout(...)` 같은 호출 단위 옵션
- channel 등록 시점에 넣는 socket 기본 옵션

아래 코드는 아직 확정된 계약은 아니다. `.NET` 표면이 이런 모양으로 보이는 편이 읽기
쉽다는, 방향 예시 정도로 본다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();

    options.AddClientServerChannel("api", channel =>
    {
        channel.EnableServer(server =>
        {
            server.ConfigureSocket(socket =>
            {
                socket.SendHighWaterMark = 20_000;
                socket.ReceiveHighWaterMark = 20_000;
                socket.SendTimeout = TimeSpan.FromMilliseconds(200);
                socket.ReceiveTimeout = TimeSpan.FromMilliseconds(200);
                socket.Immediate = true;
            });

            server.ConfigureRouting(routing =>
            {
                routing.RequireKnownPeer = true;
                routing.AllowPeerHandover = true;
            });
        });
    });

    options.AddFanoutChannel("api.events", channel =>
    {
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

    options.AddClientServerChannel("profile", channel =>
    {
        channel.EnableClient(client =>
        {
            client.ConfigureSocket(socket =>
            {
                socket.ConnectTimeout = TimeSpan.FromSeconds(3);
                socket.HandshakeInterval = TimeSpan.FromSeconds(3);
                socket.SendHighWaterMark = 5_000;
                socket.ReceiveHighWaterMark = 5_000;
                socket.Immediate = true;
            });

            client.ConfigureRouting(routing =>
            {
                routing.ProbeRouterOnConnect = true;
            });
        });
    });

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
    });
});
```

이 예시에서 의도하는 구분은 다음과 같다.

- `server.ConfigureSocket(...)` 과 `client.ConfigureSocket(...)` 은, capability 가
  들고 있는 socket 기본 동작을 정한다.
- `server.ConfigureRouting(...)` 과 `client.ConfigureRouting(...)` 은, capability 별로
  routed 연결 정책을 따로 둔다는 뜻이다. public 설정 이름은 `RequireKnownPeer`,
  `AllowPeerHandover`, `ProbeRouterOnConnect` 처럼 framework 의미가 드러나는 이름을
  쓴다. 하부 backend option 이름은 노출하지 않는다.
- `client.Request(...).Timeout(...)` 은 특정 호출 하나에만 적용되는 값이다. 실제
  low-level 바인딩에서도, `DealerSocket.RequestAsync(..., TimeSpan timeout, ...)` 처럼
  호출 인자로 전달된다. 반면 위 `ConfigureSocket(...)` 설정은, capability 전체의
  기본값이다.

이렇게 둬야 두 가지 이점이 생긴다.

- framework 사용자가, low-level `setsockopt` 이름을 직접 외울 필요가 없다.
- 어떤 옵션이 어느 runtime 에 적용되는지를, `channel + capability` 기준으로 바로 읽을
  수 있다.

### 2.4 outbound-only client 앱도 가능해야 한다

local handler 를 전혀 붙이지 않고, 내부 서비스 호출만 하는 앱도 가능해야 한다. 이런
앱의 동작은 다음과 같다.

- 등록한 remote channel 마다, 그 channel 용 `DEALER(client)` 만 만든다.
- local `ROUTER(server)` 는 열지 않는다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();
    options.AddClientServerChannel("profile", channel =>
    {
        channel.EnableClient();
    });

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
    });
});
```

조금 더 완결된 샘플로 풀어 쓰면 아래와 같다.

```csharp
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();
    options.AddClientServerChannel("profile", channel =>
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
    GetUserHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    var reply = await client
        .Request(
            "profile",
            new GetUserRequest { AccountId = request.AccountId })
        .SubmitAsync<GetUserReply>(cancellationToken);

    return Results.Ok(reply);
});

app.Run();
```

이 outbound-only 예시에는 일반 handler dispatch[^dispatch] 가 없다. `IZLinkClient` 가
사용하는 outbound `DEALER(client)` 는, request 를 보내고 reply 를 받는 경로로만
동작한다.

## 3. 한 번에 보는 전체 예시

이 절에서는 지금까지 나눠 본 등록 / handler / outbound 호출을, 하나의 앱 안에서 함께
쓰는 흐름을 한 자리에 모아 본다.

아래 코드는 하나의 `ASP.NET Core` 애플리케이션 안에서 다음 흐름이 모두 일어나는 예시다.

- `api` 서버군에 속한 앱이, ZLink handler 를 받는다.
- 필요하면 다른 내부 channel 로, outbound 요청을 보낸다.
- 기존 HTTP endpoint 안에서도, 같은 `IZLinkClient` 를 쓴다.
- event 도 publish / subscribe 한다.

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
    options.AddClientServerChannel("api", channel =>
    {
        channel.EnableServer(server =>
        {
            server.Bind("tcp://0.0.0.0:7101");
        });
        channel.MapHandlerGroup("api");
    });

    options.AddFanoutChannel("api.events", channel =>
    {
        channel.EnablePublisher(publisher =>
        {
            publisher.Bind("tcp://0.0.0.0:7201");
        });
        channel.EnableSubscriber();
        channel.MapHandlerGroup("api.events");
    });

    options.AddClientServerChannel("profile", channel =>
    {
        channel.EnableClient();
    });

    options.AddClientServerChannel("account", channel =>
    {
        channel.EnableClient();
    });

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
        registry.Add("tcp://registry2:5551");
    });
    // Handler type을 DI에 등록하고 attribute scan 후보를 발견한다.
    // 실제 노출 channel은 위의 MapHandlerGroup(...) 호출이 정한다.
    options.AddHandlersFromAssemblyOf<Program>();
});

var app = builder.Build();

app.MapPost("/profiles/get", async (
    GetUserHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    var reply = await client
        .Request(
            "profile",
            new GetUserRequest { AccountId = request.AccountId })
        .SubmitAsync<GetUserReply>(cancellationToken);

    return Results.Ok(reply);
});

app.MapPost("/profiles/refresh-cache", async (
    RefreshUserCacheHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    await client
        .Send(
            "profile",
            new RefreshUserCacheCommand { AccountId = request.AccountId })
        .Submit(cancellationToken);

    return Results.Accepted();
});

app.Run();

[ZLinkHandlerGroup("api")]
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
            .SubmitAsync<GetAccountReply>(cancellationToken);

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
        await _publisher
            .Publish(
                "api.events",
                "user.cache-refreshed",
                new UserCacheRefreshedEvent
                {
                    AccountId = command.AccountId
                })
            .Submit(cancellationToken);
    }
}

[ZLinkHandlerGroup("api")]
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

[ZLinkHandlerGroup("api.events")]
public sealed class UserCacheRefreshedEventHandler
    : IZLinkPublishHandler<UserCacheRefreshedEvent>
{
    [ZLinkPublish]
    public ValueTask HandleAsync(
        UserCacheRefreshedEvent message,
        ZLinkPublishContext context,
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

이 전체 예시에서도 dispatch 경계는 그대로다. 즉 다음과 같다.

- 실제 request / send handler dispatch 는, local `ROUTER(server)` 가 받은 메시지에
  한해서만 일어난다.
- 반대로 outbound `DEALER(client)` 가 받는 메시지는, 먼저 보낸 request 의 reply 를
  완료하는 경로로 본다.
- 현재 스펙은 `ROUTER -> DEALER` 임의 push 를, channel messaging 공용 API 에 넣지
  않는다.

## 4. 이 샘플을 어떻게 읽으면 되는가

이 절에서는 위 예시를 어떤 시선으로 읽으면 되는지, 그리고 handler class 가 dispatch
에서 어떤 위치를 차지하는지를 정리한다.

이 샘플에서 짚어 둘 부분은 다음과 같다.

- `IZLinkClient` 는 하나만 주입받는다.
- 요청 대상은 endpoint 가 아니라, `channel name` 이다.
- local handler 를 등록한 경우에만, 이 앱은 `api` channel 에서 server 역할을 한다.
- runtime 은 channel 마다 선언한 capability 에 맞는 runtime 만 만든다.
- `account`, `profile` 처럼 client capability 를 둔 channel 은, 그 channel 전용의
  `Discovery` 와 outbound `DEALER(client)` socket 을 가진다.
- 기본 packet key 는 payload 타입 이름이다. timeout 과 packet override 는 builder 에
  이어 붙인다.
- 같은 `IZLinkClient` 를, ZLink handler 와 HTTP handler 가 함께 쓴다.
- handler class 는 `UserHandlers`, `ItemHandlers` 처럼 주제별로 묶어 둬도 된다.

`AddHandlersFromAssemblyOf<Program>()` 는 두 가지 일을 한꺼번에 한다.

- handler 타입을 DI 에 올린다.
- `[ZLinkHandlerGroup(...)]` attribute scan[^attribute-scan] 후보를 발견한다.

다만 handler 가 실제로 노출되는 channel 은, channel 등록 쪽의
`channel.MapHandlerGroup("...")` 호출이 정한다. 따라서 자동 등록의 편의는 유지된다.
그렇다고 해서, 한 프로세스 안의 여러 channel 이 전역 handler registry 를 무조건
공유하는 것은 아니다.

즉 두 시선의 차이가 있다.

- 응용 코드 입장에서는, 공용 client 하나만 보인다.
- 그러나 framework 내부에서는, channel 별 outbound 경로가 따로 분리되어 관리된다.

그리고 handler class 는 dispatch key 가 아니라, **코드 조직 단위**다. 실제 dispatch
의 키는 다음 규칙을 따른다.

- 기본 dispatch 는 `GetUserRequest`, `GetItemRequest` 같은 payload 타입 이름을 기준으로
  이뤄진다.
- dispatch namespace 는, channel 별로 분리된다.
- 같은 channel 안에서는, 같은 `kind + packet key` 중복을 startup
  validation[^startupvalidation] 오류로 본다.
- 다만 다른 channel 에서는, 같은 packet key 를 다시 써도 된다.

## 5. client와 publisher 인터페이스

이 절에서는 위 예시 코드가 전제하는 client / publisher 인터페이스가 어디에 정의되어
있는지, 그리고 packet key 의 기본 해석 규칙은 무엇인지 정리한다.

위 예시가 전제하는 인터페이스는 다음 두 가지다.

- `IZLinkClient`
- `IZLinkEventPublisher`

전체 정의는 [handler-interfaces.ko.md](../../spec/handler-interfaces.ko.md) 의 section 5 를
참고한다. request 의 reply 타입은 메시지 타입에 붙이지 않는다. 대신 `Async<TReply>(...)`
쪽에서 명시한다.

이 샘플은 기본 packet key 를, payload 타입 이름으로 해석하는 규칙을 전제로 한다.

- `GetUserRequest` 는 기본적으로 `GetUserRequest` packet 으로 매핑된다.
- `RefreshUserCacheCommand` 는 기본적으로 `RefreshUserCacheCommand` packet 으로
  매핑된다.

기본 이름이 맞지 않는 경우에만, `PacketName`[^packetname] override 를 사용한다.

## 6. 함수 호출 예시

이 절에서는 위 인터페이스가 실제 코드에서 어떤 호출 chain 으로 사용되는지 짧게 모아
본다.

위 인터페이스는 실제 코드에서 다음과 같이 호출된다.

```csharp
await client
    .Send(
        "profile",
        new RefreshUserCacheCommand { AccountId = accountId })
    .Submit(cancellationToken);
```

```csharp
var reply = await client
    .Request(
        "profile",
        new GetUserRequest { AccountId = accountId })
    .SubmitAsync<GetUserReply>(cancellationToken);
```

```csharp
var fastReply = await client
    .Request(
        "profile",
        new GetUserRequest { AccountId = accountId })
    .Timeout(TimeSpan.FromMilliseconds(200))
    .SubmitAsync<GetUserReply>(cancellationToken);
```

```csharp
await client
    .Send(
        "profile",
        new RefreshUserCacheCommand { AccountId = accountId })
    .PacketName("profile.refresh-cache")
    .Submit(cancellationToken);
```

### 6.1 framework client의 reply 처리 기준

이 절에서는 framework client 가 reply 를 어떤 모양으로 사용자에게 돌려주는지를 짧게
정리한다. binding 의 raw 표면과 어떻게 다른지가 핵심이다.

이 문서에서 다루는 framework client 는, reply 를 raw `Message` part 목록으로 노출하지
않는다. 대신 요청한 typed reply 를, 곧장 돌려주는 표면을 기준으로 본다.

즉 framework 사용자는, 하부 `.NET` binding 의 `DealerSocket.RequestAsync(...)` 처럼
`IReadOnlyList<Message>` 를 직접 받아서 parse 하지 않는다. 그런 raw parse 예시는,
binding 문서에서 따로 다룬다. framework 문서에서는 아래처럼, typed reply 표면만
설명한다.

```csharp
GetUserReply reply = await client
    .Request(
        "profile",
        new GetUserRequest { AccountId = accountId })
    .SubmitAsync<GetUserReply>(cancellationToken);
```

```csharp
await publisher
    .Publish(
        "api.events",
        "user.cache-refreshed",
        new UserCacheRefreshedEvent { AccountId = accountId })
    .Submit(cancellationToken);
```

이 예시에서 두 문자열의 의미는 다음과 같다.

- 첫 번째 문자열 `api.events` 는, publish 대상 `channelName` 이다.
- 두 번째 문자열 `user.cache-refreshed` 는, 그 channel 안의 `topic` 이다.

즉 같은 `api.events` channel 안에서도, 여러 topic 으로 fan-out[^fanout] 할 수 있다.

## 7. handler 시그니처만 따로 보면

이 절에서는 위 예시에서 잠깐씩 등장한 handler 시그니처를, 따로 모아 본다. 그리고 class
구조가 dispatch 와 어떤 관계인지를 정리한다.

request 와 send handler 는, 다음 감각을 기준으로 본다.

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

핵심은 한 가지다. raw header 를 method 인자로 직접 받지 않는다. 대신 다음과 같이
받는다.

- payload 는 typed object 로 받는다.
- metadata 는 context 에서 읽는다.

여기서 또 한 가지 짚어 둘 점이 있다. 기본 dispatch key 는, class 이름이 아니라 request
나 message payload 타입 이름이다. 그래서 묶음 형태는 자유롭다.

- `UserHandlers` 아래에, 여러 request / send handler 를 함께 둘 수 있다.
- `ItemHandlers` 아래에, 여러 request handler 를 함께 둘 수 있다.
- 반대로 packet 하나당 class 하나로 쪼개고 싶다면, 그렇게 둬도 된다.

예를 들면 아래와 같이도 가능하다.

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

즉 framework 가 강제하는 것은, class 구조가 아니다. 다음 한 줄이다.

> "resolved packet key 하나는, 하나의 handler 에만 매핑된다."

## 8. HTTP handler에서 outbound만 따로 보면

이 절에서는 HTTP endpoint 안에서도, 같은 `IZLinkClient` 를 그대로 쓸 수 있다는 점을
한 번 더 짚어 둔다.

기존 HTTP endpoint 에서도, 같은 client 를 그대로 쓸 수 있어야 한다.

```csharp
app.MapPost("/profiles/get", async (
    GetUserHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    var reply = await client
        .Request(
            "profile",
            new GetUserRequest { AccountId = request.AccountId })
        .SubmitAsync<GetUserReply>(cancellationToken);

    return Results.Ok(reply);
});
```

이 부분이 있어야, 기존 웹 요청 처리와 ZLink 서버 간 요청 처리가 같은 outbound 표면으로
묶인다.

## 9. 정리

이 절에서는 이 샘플 문서가 일관되게 깔고 있는 규칙을, 한 번에 모아 본다.

- channel outbound 표면은, `IZLinkClient` 하나로 고정한다.
- 호출 대상은 endpoint 나 gateway 가 아니라, `channel name` 이다.
- capability 는 `EnableServer`, `EnableClient`, `EnablePublisher`, `EnableSubscriber`
  로 명시 등록한다.
- outbound-only 앱도, 같은 표면을 그대로 쓴다.
- request / send / event handler 는, HTTP handler 와 비슷한 DI[^di] 감각으로 읽히도록
  유지한다.
- event publish 는, publisher capability 가 열려 있는 channel 에서만 가능하다.

## 10. 회귀 테스트

이 절에서는 이 샘플 문서의 코드 흐름이 실제 framework 표면과 어긋나지 않도록, 어떤
테스트와 연결해 두는지를 정리한다.

샘플을 바꿀 때는, 다음 항목이 아래 테스트 범위 안에 그대로 남아 있어야 한다.

- 등록 코드
- handler
- outbound 호출

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ChannelMessagingIntegrationTests.ManualClient_Request_And_Send_Work_Across_Hosts` | 수동 연결 샘플의 request / send 흐름이 동작한다. |
| `ChannelMessagingIntegrationTests.DiscoveryClient_Request_And_Send_Work_Across_Hosts` | 자동 연결 샘플의 request / send 흐름이 동작한다. |
| `ChannelMessagingIntegrationTests.Publisher_And_Subscriber_Work_Across_Hosts` | publish / subscribe 샘플 흐름이 동작한다. |
| `ChannelMessagingIntegrationTests.HttpHandler_Uses_SameServiceProvider_ToResolve_IZLinkClient` | HTTP handler에서 outbound client를 사용하는 샘플 흐름이 동작한다. |

---

### 각주 모음

[^public-contract]: **public contract** 는 외부 사용자에게 공개되어, 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.

[^spot]: **SPOT** 은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 추상이다.

[^channel]: **channel** 은 zlink core 의 논리적 통신 경로 단위다. 같은 channel 이름을
    쓰는 노드끼리만 메시지를 주고받는다. 물리 endpoint(IP:port)와는 분리된 개념이다.

[^outbound]: **outbound** 는 "내가 보내는 쪽" 방향을 뜻한다. 반대 방향은 inbound
    (받는 쪽). client 는 outbound, server 는 inbound 역할을 맡는다.

[^handler]: **handler** 는 들어온 메시지를 처리하는 사용자 코드다. request handler 는
    응답을 돌려주고, send handler 는 단방향으로 받기만 하며, event handler 는 publish 된
    이벤트를 받는다.

[^pubsub]: **publish / subscribe** 는 1:N 이벤트 fan-out 패턴이다. publisher 가 토픽에
    이벤트를 보내면 그 토픽을 구독한 모든 subscriber 가 함께 받는다.

[^capability]: **capability** 는 한 channel 안에서 이 앱이 맡는 역할이다. server,
    client, publisher, subscriber 네 가지가 있다. 한 channel 이 둘 이상의 capability 를
    동시에 가질 수도 있다(channel 타입에 따라).

[^discovery]: **Discovery** 는 zlink core 의 자동 peer 발견 메커니즘이다. registry
    노드에 channel 의 provider 목록이 등록되어 있고, client 는 그 목록을 받아 자동으로
    연결한다. 수동 endpoint 관리가 필요 없다.

[^rid]: **RoutingId** (rid) 는 zlink core 가 각 peer 에게 부여하는 식별자다. channel
    안의 특정 노드를 가리킬 때 쓴다.

[^dispatch]: **dispatch** 는 들어온 메시지를 packet kind 와 packet name 같은 키로 보고,
    실행할 handler 메서드를 골라 호출하는 단계를 가리킨다.

[^attribute-scan]: **attribute scan** 은 어셈블리에 정의된 타입과 메서드를 훑어 보면서
    특정 attribute 가 붙은 항목을 찾아 등록하는 방식이다.

[^startupvalidation]: **startup validation** 은 앱이 뜨는 순간 설정을 검사해 오류가
    있으면 즉시 실패시키는 단계다. 런타임에서 늦게 드러나는 실패를 막는다.

[^packetname]: **packet name** 은 메시지 종류를 가리키는 문자열 키다. 기본값은 payload
    타입 이름이고, `[ZLinkRequest(PacketName = "...")]` 로 override 할 수 있다.

[^fanout]: **fan-out** 은 하나의 publish 가 여러 구독자에게 동시에 퍼져 나가는 흐름을
    가리킨다.

[^di]: **DI** = Dependency Injection. `ASP.NET Core` 가 기본으로 제공하는 의존성 주입
    컨테이너다. `builder.Services.Add...()` 로 등록하고 생성자 매개변수로 받아 쓴다.
