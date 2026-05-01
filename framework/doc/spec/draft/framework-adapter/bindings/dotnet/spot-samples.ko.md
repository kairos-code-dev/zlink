[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md)

# Draft -- ZLink Framework .NET SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`에서 `SPOT`을 실제 코드 흐름으로 한 번에 보기
> 위한 샘플 문서다.

## 1. 이 문서의 목적

`SPOT`은 room, stage, zone 같은 논리 인스턴스 모델과 targeted packet, subscribe,
timer가 함께 나오기 때문에 설명만 보면 감이 잘 안 올 수 있다.
이 문서는 아래를 한 번에 모아 보여 준다.

1. `SpotNode` 등록
2. spot 객체 생성
3. packet handler 등록
4. subscribe handler 등록
5. timer 등록
6. channel send / request
7. topic publish

현재 구현 범위를 기준으로 이 문서는 두 층으로 나눠 읽어야 한다.

- 3.1과 3.1.4, 그리고 이 절의 `IZLinkSpot` / `IZLinkSpotClient` 예시는 현재
  framework core public surface에 맞춘 샘플이다.
- 3.2.1의 actor/session membership 예시는 stage wrapper 같은 상위 확장 아이디어
  메모다. 현재 framework core public surface가 아니다.
- `targetRid + spotRid` direct routed 호출 예시도 같은 이유로 현재 core 계약으로
  읽지 않는다.

## 2. 인터페이스 초안

먼저 이 문서에서 전제로 두는 인터페이스 초안을 따로 본다.
아래는 샘플 구현이 기대하는 최소 표면이다.

> **주의**: 아래 정의는 [handler-interfaces.ko.md](./handler-interfaces.ko.md)의
> 해당 섹션과 동일하다. 인터페이스가 변경되면 두 문서를 함께 갱신해야 한다.
> 최신 계약은 항상 `handler-interfaces.ko.md`를 기준으로 한다.

```csharp
public interface IZLinkSpot
{
    void Configure()
    {
    }

    ValueTask OnInitializeAsync(
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnClosingAsync(
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkSpotContext
{
    RoutingId SpotRid { get; }
    RoutingId NodeRid { get; }
    string SpotName { get; }

    void AddPacket<THandler>()
        where THandler : class
    {
    }

    void AddSubscribe<THandler>(
        string topic)
        where THandler : class
    {
    }

    void AddActorJoin<THandler, TActor, TRequest, TReply>()
        where THandler : class
        where TActor : IZLinkActor
    {
    }

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message)
    {
        return default!;
    }

    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message)
    {
        return default!;
    }

    IZLinkRequestCall RequestChannel<TRequest>(
        string channelName,
        TRequest request)
    {
        return default!;
    }

    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        CancellationToken cancellationToken)
        where THandler : class
    {
        return ValueTask.FromResult<IZLinkTimer>(default!);
    }
}

public interface IZLinkSpotPacketHandler<TSpot, in TMessage>
    where TSpot : IZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRequestHandler<TSpot, in TRequest, TReply>
    where TSpot : IZLinkSpot
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotSubscriptionHandler<TSpot, in TEvent>
    where TSpot : IZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TEvent message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotTimerHandler<TSpot>
    where TSpot : IZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        CancellationToken cancellationToken);
}

// Low-level Zlink binding basis
public sealed class Timer : IDisposable, IAsyncDisposable
{
    public static Timer FromSpot(Spot spot);

    public void Start(ulong intervalNs, ulong repeatCount);

    public void Stop();

    public ulong Recv(int flags = 0);

    public void OnFire(Action<Timer, ulong> handler);
}

public readonly record struct ZLinkSpotCreateResult(
    RoutingId SpotRid,
    string SpotName,
    bool Created);

public readonly record struct ZLinkSpotInfo(
    RoutingId SpotRid,
    string SpotName);

// IZLinkSpotManager is defined in handler-interfaces.ko.md section 6.3.

public interface IZLinkClient
{
    IZLinkSendCall Send<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall Request<TMessage>(
        string channelName,
        TMessage request);
}

public interface IZLinkSpotClient
{
    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall RequestChannel<TMessage>(
        string channelName,
        TMessage request);

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message);
}

public interface IZLinkSpotPublisherClient
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}

public enum ZLinkDispatchMode
{
    Compiled = 1,
    Dynamic = 2
}

public interface IZLinkDispatchOptions
{
    ZLinkDispatchMode SpotDispatchMode { get; set; }
    ZLinkDispatchMode StreamDispatchMode { get; set; }
}
```

## 3. 샘플 코드

### 3.1 등록 코드

```csharp
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using Stage.Protocol; // protoc generated

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();

    options.AddChannel("play", channel =>
    {
        channel.EnableServer(server =>
        {
            server.Bind("tcp://0.0.0.0:7101");
        });
    });

    options.UseSpotDiscovery("game.stage", registry =>
    {
        registry.Add("tcp://registry1:5551");
    });

    options.AddSpotNode("stage-node", spot =>
    {
        spot.Bind("tcp://0.0.0.0:9000");

        spot.EnableRouter(router =>
        {
            router.ConfigureSocket(socket =>
            {
                socket.MaxMessageSize = 1024 * 1024;
                socket.SendHighWaterMark = 10_000;
                socket.ReceiveHighWaterMark = 10_000;
                socket.SendTimeout = TimeSpan.FromMilliseconds(200);
                socket.ReceiveTimeout = TimeSpan.FromMilliseconds(200);
                socket.Immediate = true;
            });

            router.ConfigureRouting(options =>
            {
                options.Mandatory = true;
                options.Handover = true;
            });
        });

        spot.EnablePubSub(pubsub =>
        {
            pubsub.ConfigurePublisherOptions(options =>
            {
                options.SendHighWaterMark = 50_000;
                options.SendTimeout = TimeSpan.FromMilliseconds(100);
                options.NoDrop = true;
            });

            pubsub.ConfigureSubscriberOptions(options =>
            {
                options.ReceiveHighWaterMark = 50_000;
                options.ReceiveTimeout = TimeSpan.FromMilliseconds(50);
                options.Linger = TimeSpan.Zero;
            });
        });

        spot.AttachChannelClient("orders", client =>
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
                routing.ProbeRouter = true;
            });
        });

        spot.AttachSpotPublisherClient("game.stage", publisher =>
        {
            publisher.ConfigureSocket(socket =>
            {
                socket.SendHighWaterMark = 20_000;
                socket.SendTimeout = TimeSpan.FromMilliseconds(100);
                socket.Immediate = true;
            });
        });

        spot.AddSpotFactory<SampleSpot>("sample");
    });
});

var app = builder.Build();
app.Run();
```

여기서 capability와 attach 함수는 서로 다른 역할을 가진다.

- `AttachChannelClient("orders")`
  - stage spot이 `orders` channel로 send/request 할 outbound client를 붙인다.
- `EnableRouter()`
  - 같은 SPOT channel의 다른 `SpotNode`와 routed packet을 주고받는 local router를
    켠다.
- `EnablePubSub()`
  - local spot 문맥에서 `IZLinkSpotClient.Publish(...)`를 쓸 수 있게 한다.
- `AttachSpotPublisherClient("game.stage")`
  - local spot 인스턴스가 없는 외부 노드가 `game.stage` SPOT channel로 publish할
    별도 publisher client를 붙인다.
- `ConfigureSocket(...)`, `ConfigureRouting(...)`,
  `ConfigurePublisherOptions(...)`, `ConfigureSubscriberOptions(...)`
  - 실제 `.NET` 바인딩의 `CommonSocketOptions`, routed peer 옵션,
    outbound peer 옵션, `SpotNodePublisherOptions`,
    `SpotNodeSubscriberOptions`와 같은 typed facade를 capability별로 등록한다.
  - 호출 단위 `WithTimeout(...)`과 달리 runtime 기본 동작을 정하는 설정이다.
- `AddSpotFactory<SampleSpot>("sample")`
  - 이 node가 생성하고 소유할 `SampleSpot` factory를 `sample` 이름으로 등록한다.
  - 같은 `SpotNode`에는 서로 다른 이름으로 여러 spot factory를 둘 수 있고,
    생성 시에는 이 이름으로 어떤 타입을 만들지 고른다.
  - 이미 등록된 이름을 다시 쓰면 조용히 덮어쓰지 않고 예외를 던진다.

### 3.1.1 capability별 수동 연결

SPOT도 일반 channel과 마찬가지로 수동 연결은 capability별로 다뤄야 한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.AddProtobuf();

    options.UseSpotDiscovery("game.stage", registry =>
    {
        registry.Add("tcp://registry1:5551");
    });

    options.AddSpotNode("stage-node", spot =>
    {
        spot.Bind("tcp://0.0.0.0:9000");

        spot.EnableRouter(router =>
        {
            router.UseManualConnections(peers =>
            {
                // Remote SpotNode router endpoint
                peers.Connect("tcp://10.0.0.10:9000");
            });
        });

        spot.EnablePubSub(pubsub =>
        {
            pubsub.UseManualConnections(peers =>
            {
                // Remote SpotNode mesh PUB endpoint.
                // The local mesh SUB side connects to this address.
                peers.Connect("tcp://10.0.0.20:9100");
            });
        });

        spot.AttachChannelClient("orders", client =>
        {
            client.UseManualConnections(peers =>
            {
                // Remote orders channel server endpoint
                peers.Connect("tcp://10.0.0.30:9200");
            });
        });

        spot.AttachSpotPublisherClient("game.stage", publisher =>
        {
            publisher.UseManualConnections(peers =>
            {
                // Remote game.stage SPOT publish endpoint
                peers.Connect("tcp://10.0.0.40:9300");
            });
        });

        spot.AddSpotFactory<SampleSpot>("sample");
    });
});
```

여기서 중요한 규칙은 아래와 같다.

- `router`, `pub/sub`, attach된 channel client, attach된 spot publisher client는
  서로 다른 연결 집합이다.
- 같은 capability에서는 `Discovery`와 `Manual`을 섞지 않는다.
- 같은 `SpotNode`에서 `spotName`은 비어 있으면 안 된다.
- 이미 등록된 `spotName`을 다시 등록하면 기존 값을 덮어쓰지 않고 예외를 던진다.
- `router` manual 연결도 endpoint 집합만 등록한다. 이 초안에서는 `Connect(...)`
  호출 시 remote router id를 별도 파라미터로 받지 않는다.
- channel client manual 연결은 endpoint 집합만 등록한다. 하부 `DEALER`가 이미
  connect된 peer 집합을 대상으로 요청을 보내므로 remote `RoutingId`를 별도
  파라미터로 받지 않는다.
- `pub/sub`는 local publish 소켓과 local subscribe 소켓을 함께 쓰는 capability지만,
  manual 연결에서 등록하는 주소는 "다른 `SpotNode`의 mesh publish bind 주소"다.
- 즉 `EnablePubSub(...).UseManualConnections(...).Connect(endpoint)`는 local
  `SUB/XSUB` 쪽이 remote `PUB/XPUB` 주소로 붙는다는 뜻이다.
- `pub/sub`와 spot publisher client는 endpoint 집합만 등록한다. 다만 둘은 같은
  endpoint 집합이 아니다. 전자는 peer `SpotNode` mesh 주소이고, 후자는 외부
  publish ingress 주소다.

#### 소켓 옵션 설정 샘플

`SPOT`에서는 옵션 소유자를 두 단계로 나눠서 보는 편이 읽기 쉽다.

- `SpotNode` pub/sub 기본값
  - 실제 low-level 바인딩의 `SpotNode.PublisherOptions`,
    `SpotNode.SubscriberOptions`에 대응한다.
- capability별 socket 기본값
  - `router`, attach된 channel client, attach된 spot publisher client가 각각
    들고 있는 `CommonSocketOptions` 계열 기본값

아래 코드는 아직 확정 계약이 아니라, `.NET` 표면에서 이런 식으로 보이는 편이
자연스럽다는 방향 예시다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.AddProtobuf();

    options.UseSpotDiscovery("game.stage", registry =>
    {
        registry.Add("tcp://registry1:5551");
    });

    options.AddSpotNode("stage-node", spot =>
    {
        spot.Bind("tcp://0.0.0.0:9000");

        spot.EnableRouter(router =>
        {
            router.ConfigureSocket(socket =>
            {
                socket.MaxMessageSize = 1024 * 1024;
                socket.SendHighWaterMark = 10_000;
                socket.ReceiveHighWaterMark = 10_000;
                socket.SendTimeout = TimeSpan.FromMilliseconds(200);
                socket.ReceiveTimeout = TimeSpan.FromMilliseconds(200);
                socket.Immediate = true;
            });

            router.ConfigureRouting(options =>
            {
                options.Mandatory = true;
                options.Handover = true;
            });
        });

        spot.EnablePubSub(pubsub =>
        {
            pubsub.ConfigurePublisherOptions(options =>
            {
                options.SendHighWaterMark = 50_000;
                options.SendTimeout = TimeSpan.FromMilliseconds(100);
                options.NoDrop = true;
            });

            pubsub.ConfigureSubscriberOptions(options =>
            {
                options.ReceiveHighWaterMark = 50_000;
                options.ReceiveTimeout = TimeSpan.FromMilliseconds(50);
                options.Linger = TimeSpan.Zero;
            });
        });

        spot.AttachChannelClient("orders", client =>
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
                routing.ProbeRouter = true;
            });
        });

        spot.AttachSpotPublisherClient("game.stage", publisher =>
        {
            publisher.ConfigureSocket(socket =>
            {
                socket.SendHighWaterMark = 20_000;
                socket.SendTimeout = TimeSpan.FromMilliseconds(100);
                socket.Immediate = true;
            });
        });

        spot.AddSpotFactory<SampleSpot>("sample");
    });
});
```

이 예시에서 의도하는 구분은 아래와 같다.

- `pubsub.ConfigurePublisherOptions(...)`, `pubsub.ConfigureSubscriberOptions(...)`
  는 실제 `.NET` 바인딩의 `SpotNode.PublisherOptions`,
  `SpotNode.SubscriberOptions`처럼 `SPOT` mesh 자체가 소유한 옵션 facade를
  capability 표면으로 끌어온다.
- `router.ConfigureSocket(...)`, `client.ConfigureSocket(...)` 같은 표면은 실제
  `CommonSocketOptions`와 같은 공통 socket 기본 동작을 정한다.
- `router.ConfigureRouting(...)`, `client.ConfigureRouting(...)`은 실제
  routed peer 옵션과 outbound peer 옵션에 대응하는 capability 전용 facade다.
- `RequestChannel(...).WithTimeout(...)` 같은 호출 단위 옵션은 특정 호출 하나에만
  적용되고, 위 설정은 runtime 기본값이다.

이렇게 두면 framework 사용자는 low-level `spot_node_option`이나 `setsockopt`
이름을 직접 앞면에서 보지 않아도 되고, 어떤 옵션이 node 전체에 적용되는지,
어떤 옵션이 개별 capability에만 적용되는지도 바로 구분할 수 있다.

### 3.1.2 spot 생성과 조회

spot 인스턴스를 만들 때는 factory 타입이 아니라 등록 이름을 넘긴다. 이렇게 해야
같은 `SpotNode`에 여러 spot factory가 있을 때도 어떤 타입을 만들지 명확해진다.

```csharp
app.MapPost("/stage/create", async (
    IZLinkSpotManager spotManager,
    CancellationToken cancellationToken) =>
{
    var created = await spotManager.CreateAsync("stage", cancellationToken);
    var spotInfo = await spotManager.GetAsync(created.SpotRid, cancellationToken);

    return Results.Ok(new
    {
        created.SpotRid,
        created.SpotName,
        created.Created,
        LookupName = spotInfo?.SpotName
    });
});
```

여기서 `SpotName`은 생성 결과와 조회 표면 둘 다에서 다시 보인다. 즉 운영 코드가
`spotRid`만 들고 있어도, 이 인스턴스가 어떤 등록 이름으로 만들어졌는지 다시
확인할 수 있다.

### 3.1.3 outbound-only SPOT-aware 앱

local `SpotNode`와 local spot runtime을 열지 않고, 다른 channel이나 다른 spot으로만
outbound 호출하는 앱이 있을 수 있다. 이런 경우의 기본 outbound 표면은
`IZLinkClient`다. local `SpotNode`가 없으면 attach된 channel client 경로도 없으므로
`IZLinkSpotClient.RequestChannel(...)` 같은 표면을 바로 쓰는 모델로 설명하면 안 된다.

```csharp
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
    });
});

var app = builder.Build();

app.MapPost("/stage/query", async (
    GetStageStateHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    var reply = await client
        .Request(
            "orders",
            new SampleGetStateRequest { SpotRid = request.StageRid })
        .Async<SampleGetStateReply>(cancellationToken);

    return Results.Ok(reply);
});

app.Run();
```

현재 framework core public surface는 RID direct routed 호출을 열지 않는다.
그래서 이 샘플도 `channelName` 기반 request/send와 `IZLinkSpotPublisherClient`
기반 publish까지만 현재 계약으로 본다.

send/publish builder는 기본 async submit이다. temporary backpressure는
별도 public no-wait 옵션으로 호출자에게 맡기지 않고, framework 내부의
nonblocking send, pending queue, ready notification으로 처리한다. send 대기 한계는
channel 또는 socket의 `SendTimeout` 옵션을 따른다.

### 3.1.4 외부 노드에서 SPOT channel publish

local spot 인스턴스가 없는 외부 노드가 특정 SPOT channel로 publish해야 할 수도
있다. 이런 경우에는 `IZLinkSpotPublisherClient`를 따로 주입받아 쓴다.
여기서 별도 `SpotDiscovery` 객체를 두는 것으로 설명하면 오해가 생길 수 있다.
이 샘플에서 필요한 것은 일반 `Discovery` 등록이고, 실제 SPOT channel 선택은
`AttachSpotPublisherClient("game.stage")`가 맡는다.

```csharp
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.AddProtobuf();

    options.UseDiscovery(registry =>
    {
        registry.Add("tcp://registry1:5551");
    });

    options.AddSpotNode("publisher-node", spot =>
    {
        spot.AttachSpotPublisherClient("game.stage");
    });
});

var app = builder.Build();

app.MapPost("/stage/publish", async (
    PublishStageStateHttpRequest request,
    IZLinkSpotPublisherClient spotPublisherClient,
    CancellationToken cancellationToken) =>
{
    await spotPublisherClient
        .Publish(
            "game.stage",
            "sample.state.updated",
            new SampleStateUpdatedEvent
            {
                SpotRid = request.StageRid,
                ActorCount = request.UserCount,
                ConnectedSessionCount = request.UserCount
            })
        .Async(cancellationToken);

    return Results.Accepted();
});

app.Run();
```

### 3.2 spot 객체

```csharp
public sealed class SampleSpot(IZLinkSpotContext context) : IZLinkSpot
{
    private IZLinkTimer? _sessionTimeoutSweep;

    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddPacket<SampleGetStateHandler>();
        Context.AddPacket<SampleReportStateHandler>();
        Context.AddActorJoin<SampleJoinRoomHandler, SampleActor, SampleJoinRoomRequest, SampleJoinRoomReply>();

        Context.AddSubscribe<SampleStateUpdatedHandler>(
            "sample.state.updated");
    }

    public int ActorCount { get; private set; } = 10;

    public int ConnectedSessionCount { get; private set; } = 8;

    public async ValueTask OnInitializeAsync(
        CancellationToken cancellationToken)
    {
        _sessionTimeoutSweep = await Context.AddTimer<SampleSessionTimeoutSweepHandler>(
            "session-timeout-sweep",
            TimeSpan.FromSeconds(1),
            cancellationToken);
    }

    public void ApplyReportedState(SampleReportStateCommand command)
    {
        ActorCount = command.ActorCount;
        ConnectedSessionCount = command.ConnectedSessionCount;
    }
}
```

### 3.2.1 room이 먼저 있고, session이 인증한 뒤 actor가 room에 들어가는 상세 샘플

> 이 절은 현재 draft framework core가 포함하는 actor/session membership 표면을
> stage wrapper 관점에서 길게 풀어 쓴 상세 샘플이다. room 정책과 actor factory
> 선택 규칙은 응용 계층 예시지만, `IZLinkActor`, `IZLinkActorContext.JoinSpot(...)`,
> `IZLinkSessionContext` 자체는 현행 public surface다.

게임 서버에서는 outgame, 로비, 매치메이킹을 보통 웹 서버 쪽에서 처리하고,
room 서버는 이미 만들어진 room에 플레이어를 들이는 역할만 맡는 경우가 많다.
이 절의 샘플도 그 흐름을 그대로 따른다.

이 샘플이 가정하는 순서는 아래와 같다.

1. 웹 서버가 room 서버에 room 생성 요청을 미리 보낸다.
2. 웹 서버가 client에게 room 서버 주소와 `roomId`를 내려 준다.
3. client가 room 서버에 연결한다.
4. `SampleSession`이 `SampleAuthenticateRequest`를 받고 API 서버에 토큰 검증을 요청한다.
5. 검증이 끝나면 `actorType + accountId`로 기존 `SampleActor`를 찾거나 새로 만든다.
6. actor와 stream을 먼저 연결한다.
7. client가 `SampleJoinRoomRequest`를 보내면 `SampleSession`이
   `IZLinkSpotClient.JoinActorAsync(...)`로 target room에 join 요청을 넣는다.
8. join이 끝난 뒤의 heartbeat, gameplay packet, disconnect, timer는 같은
   `SampleSpot` 실행 문맥에서 처리된다고 읽는다.

이 절에서 경계는 아래처럼 본다.

- framework draft 계약
  - `IZLinkSpot`
  - `IZLinkSession`
  - `IZLinkSessionContext`
  - `IZLinkActor`
  - `IZLinkActorContext`
  - `IZLinkSpotActorJoinHandler<...>`
- sample 전용 타입
  - `SampleSpot`
  - `SampleSession`
  - `SampleActor`
  - `ISampleRoomDirectory`
  - `ISampleAuthVerifier`
  - `ISampleActorFactoryRegistry`

핵심은 `Spot`이 session을 직접 소유하지 않고 actor만 다룬다는 점이다.
session handler는 인증과 재연결을 맡는 ingress adapter이고, 실제 live 연결은
`IZLinkSessionContext`가 관리한다. actor는 계정 단위의 논리 객체다. 그래서 stream이 끊겨도
actor는 room에 남아 있을 수 있고, 새 stream이 같은
`accountId`로 다시 들어오면 같은 actor에 다시 연결할 수 있다.
그리고 이 모델이 public 계약으로 보이려면 actor join은 `IZLinkSpot` override가
아니라 `IZLinkSessionContext.AttachActorAsync(...)`, `IZLinkActorContext.JoinSpot(...)`,
`IZLinkSpotActorJoinHandler<TSpot, TActor, TRequest, TReply>` 조합으로 보여야 한다.
join 승인과 membership 기록은 반드시 target `Spot` 실행 문맥에서 함께 처리되어야
하기 때문이다.
actor 타입은 handler의 generic 인자로 명시한다. 이렇게 하면 handler 내부에서
`IZLinkActor`를 다시 캐스팅하지 않아도 되고, 잘못된 actor 타입을 등록하거나
전달한 경우 runtime이 명확한 오류로 막을 수 있다.

샘플을 읽기 전에 먼저 봐야 하는 framework draft 계약은 actor contract 하나다.

```csharp
public interface IZLinkActor
{
    string ActorId { get; }

    IZLinkActorContext Context { get; set; }

    void Configure()
    {
    }

    ValueTask OnDisconnectedAsync(CancellationToken cancellationToken);
}
```

이 인터페이스를 room 모델에 대입하면 의미는 아래처럼 읽는다.

- `IZLinkActor`
  - `accountId` 같은 stable identity를 가진 논리 객체다.
- `IZLinkActor.Context`
  - 현재 stream session과 room join 상태를 제공한다. reconnect가 일어나면 context가
    가리키는 stream session이 바뀔 수 있다.
- `IZLinkActor.Configure()`
  - context가 주입된 뒤 한 번 호출된다. actor가 받을 packet handler는 이 안에서
    `Context.AddPacket<THandler>()`로 등록한다.
- `IZLinkActorContext.GetSpot<TSpot>()`
  - 현재 들어가 있는 room instance를 가져온다. room에 아직 안 들어갔으면 실패한다.
- stream attach/disconnect와 room join/leave는 다른 수명이다.
- framework는 stale disconnect를 내부에서 걸러낸 뒤에만 `OnDisconnectedAsync(...)`를 호출한다.

bootstrap은 아래처럼 읽는다. room은 외부에서 이미 만들어진다고 가정하므로,
stream node는 client 접속만 받고 spot node는 room 인스턴스를 제공한다.

```csharp
builder.Services.AddScoped<ISampleRoomDirectory, SampleRoomDirectory>();
builder.Services.AddScoped<ISampleAuthVerifier, SampleAuthVerifier>();
builder.Services.AddScoped<ISampleActorFactoryRegistry, SampleActorFactoryRegistry>();

builder.Services.AddZLinkFramework(options =>
{
    options.AddActorFactory<SampleWarriorActorFactory>("warrior");
    options.AddActorFactory<SampleTraderActorFactory>("trader");

    options.ConfigureDispatch(dispatch =>
    {
        dispatch.SpotDispatchMode = ZLinkDispatchMode.Compiled;
        dispatch.StreamDispatchMode = ZLinkDispatchMode.Compiled;
    });

    options.Codecs.AddProtobuf();

    options.AddStreamNode("client.stream", stream =>
    {
        stream.Bind("tcp://0.0.0.0:9100");
        stream.AddHeaderSession<SampleSession>();
    });

    options.UseSpotDiscovery("game.room", registry =>
    {
        registry.Add("tcp://registry1:5551");
    });

    options.AddSpotNode("room.node", spot =>
    {
        spot.AddSpotFactory<SampleSpot>("sample-room");
    });
});
```

실제 샘플 코드는 아래처럼 읽는다. `SampleSpot`은 actor table만 소유하고,
`SampleSession`은 인증과 actor 재연결, 그리고 actor로 넘길
`header/body` 정규화와 room join 요청을 맡는다.
여기서 `IZLinkSessionContext`는 stream session이 actor stream 연결, stale disconnect 필터,
actor packet dispatch를 framework runtime으로 넘길 때 쓰는 session 전용 context다.

```csharp
public sealed class SampleSpot(IZLinkSpotContext context) : IZLinkSpot
{
    private readonly Dictionary<string, SampleActor> _actors =
        new(StringComparer.Ordinal);
    private IZLinkTimer? _sessionTimeoutSweep;

    public IZLinkSpotContext Context { get; } = context;

    public string RoomId => Context.SpotRid.ToString();

    public int ActorCount => _actors.Count;

    public int ConnectedSessionCount => _actors.Values.Count(x => x.Stream is not null);

    public void Configure()
    {
        Context.AddPacket<SampleGetStateHandler>();
        Context.AddPacket<SampleReportStateHandler>();
        Context.AddActorJoin<SampleJoinRoomHandler, SampleActor, SampleJoinRoomRequest, SampleJoinRoomReply>();

        Context.AddSubscribe<SampleStateUpdatedHandler>(
            "sample.state.updated");
    }

    public async ValueTask OnInitializeAsync(
        CancellationToken cancellationToken)
    {
        _sessionTimeoutSweep = await Context.AddTimer<SampleSessionTimeoutSweepHandler>(
            "session-timeout-sweep",
            TimeSpan.FromSeconds(1),
            cancellationToken);
    }

    internal async ValueTask<SampleJoinRoomReply> JoinActorAsync(
        SampleActor actor,
        SampleJoinRoomRequest request,
        CancellationToken cancellationToken = default)
    {
        if (actor.Spot is SampleSpot current && !ReferenceEquals(current, this))
        {
            await current.Context.LeaveActorAsync(actor, cancellationToken);
        }

        _actors[actor.ActorId] = actor;
        await Context.JoinActorAsync(actor, cancellationToken);

        PublishSampleState();

        return new SampleJoinRoomReply
        {
            RoomId = RoomId,
            ActorId = actor.ActorId,
            ConnectedSessionCount = ConnectedSessionCount
        };
    }

    internal async ValueTask LeaveActorAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        if (!_actors.Remove(actorId, out SampleActor? actor))
        {
            return;
        }

        await Context.LeaveActorAsync(actor, cancellationToken);
        PublishSampleState();
    }

    public async ValueTask HandleDisconnectedActorSessionAsync(
        SampleActor actor,
        CancellationToken cancellationToken)
    {
        if (!_actors.ContainsKey(actor.ActorId))
        {
            await actor.OnDisconnectedAsync(cancellationToken);
            return;
        }

        await actor.OnDisconnectedAsync(cancellationToken);
        PublishSampleState();
    }

    public ValueTask BroadcastChatAsync(
        SampleActor sender,
        SampleSendRoomChatCommand command,
        CancellationToken cancellationToken)
    {
        foreach (SampleActor actor in _actors.Values)
        {
            if (actor.Stream is null)
            {
                continue;
            }

            using Message body = new SampleRoomChatPushed
            {
                FromActorId = sender.ActorId,
                Text = command.Text
            }.ToProtoMessage();

            using Message header = new RouteHeader
            {
                MsgId = "SampleRoomChatPushed",
                StageId = RoomId,
                AccountId = actor.ActorId
            }.ToProtoMessage();

            actor.Stream.Write(
                header,
                body);
        }

        return ValueTask.CompletedTask;
    }

    public ValueTask PublishSampleStateAsync(
        CancellationToken cancellationToken = default)
    {
        return Publish(
            "sample.state.updated",
            new SampleStateUpdatedEvent
            {
                SpotRid = Context.SpotRid.ToString(),
                ActorCount = ActorCount,
                ConnectedSessionCount = ConnectedSessionCount
            })
        .Async(cancellationToken);
    }

    public async ValueTask SweepInactiveActorsAsync(
        TimeSpan idleTimeout,
        TimeSpan reconnectGrace,
        CancellationToken cancellationToken)
    {
        DateTimeOffset now = DateTimeOffset.UtcNow;
        List<string> expiredActors = new();

        foreach (SampleActor actor in _actors.Values)
        {
            if (actor.Stream is not null &&
                actor.IsIdleTimedOut(now, idleTimeout))
            {
                await actor.OnDisconnectedAsync(
                    cancellationToken);
                continue;
            }

            if (actor.Stream is null &&
                actor.IsReconnectExpired(now, reconnectGrace))
            {
                expiredActors.Add(actor.ActorId);
            }
        }

        foreach (string actorId in expiredActors)
        {
            await LeaveActorAsync(actorId, cancellationToken);
        }
    }
}

public sealed class SampleActor : IZLinkActor
{
    private IZLinkStream? _stream;

    public SampleActor(
        string actorId,
        string displayName)
    {
        ActorId = actorId;
        DisplayName = displayName;
    }

    public string ActorId { get; }

    public string DisplayName { get; }

    public IZLinkStream? Stream => _stream;

    public int X { get; private set; }

    public int Y { get; private set; }

    public DateTimeOffset LastSeenAt { get; private set; } = DateTimeOffset.UtcNow;

    public DateTimeOffset? DisconnectedAt { get; private set; }

    public void Configure()
    {
        Context.AddPacket<SampleMoveActorHandler>();
    }

    public async ValueTask SendSnapshotAsync(
        CancellationToken cancellationToken)
    {
        LastSeenAt = DateTimeOffset.UtcNow;

        await Context
            .Send(new SampleActorSnapshot
            {
                ActorId = ActorId,
                DisplayName = DisplayName,
                X = X,
                Y = Y
            })
            .WithPacketName("SampleActorSnapshot")
            .Async(cancellationToken);
    }

    public ValueTask OnDisconnectedAsync(
        CancellationToken cancellationToken)
    {
        _stream = null;
        DisconnectedAt = DateTimeOffset.UtcNow;

        return ValueTask.CompletedTask;
    }

    public void MarkSeen(
        DateTimeOffset now)
    {
        LastSeenAt = now;
    }

    public bool IsIdleTimedOut(
        DateTimeOffset now,
        TimeSpan idleTimeout)
    {
        return _stream is not null && now - LastSeenAt >= idleTimeout;
    }

    public bool IsReconnectExpired(
        DateTimeOffset now,
        TimeSpan reconnectGrace)
    {
        return _stream is null &&
            DisconnectedAt is not null &&
            now - DisconnectedAt.Value >= reconnectGrace;
    }

    public SampleSpot GetRoom()
    {
        return Context.GetSpot<SampleSpot>();
    }

}

public sealed class SampleMoveActorHandler
    : IZLinkActorPacketHandler<SampleActor, SampleMoveActorCommand>
{
    public ValueTask HandleAsync(
        SampleActor actor,
        SampleMoveActorCommand message,
        CancellationToken cancellationToken)
    {
        SampleSpot room = actor.Context.GetSpot<SampleSpot>();
        actor.MarkSeen(DateTimeOffset.UtcNow);
        room.PublishSampleState();
        return actor.Context.Reply(new SampleMoveActorReply
        {
            X = message.X,
            Y = message.Y
        }).Async(cancellationToken);
    }
}

public sealed class SampleSession
    : IZLinkSession
{
    private readonly ISampleRoomDirectory _rooms;
    private readonly ISampleAuthVerifier _authVerifier;
    private readonly ISampleActorFactoryRegistry _actors;

    public SampleSession(
        ISampleRoomDirectory rooms,
        ISampleAuthVerifier authVerifier,
        ISampleActorFactoryRegistry actors)
    {
        _rooms = rooms;
        _authVerifier = authVerifier;
        _actors = actors;
    }

    public IZLinkSessionContext Context { get; set; } = default!;

    public SampleActor? Actor { get; private set; }

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        if (Actor is null)
        {
            return;
        }

        SampleActor actor = Actor;
        Actor = null;

        await Context.DisconnectActorAsync(cancellationToken);
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        int? errno = error.Diagnostic?.NativeCode;
        string? message = error.Diagnostic?.Message;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (Actor is null)
        {
            if (!string.Equals(
                header.Name,
                "SampleAuthenticateRequest",
                StringComparison.Ordinal))
            {
                throw new InvalidOperationException(
                    "The first client packet must be SampleAuthenticateRequest.");
            }

            SampleAuthenticateRequest authRequest =
                body.Parse<SampleAuthenticateRequest>();

            SampleAuthenticationResult auth =
                await _authVerifier.VerifyAsync(
                    authRequest.AccessToken,
                    cancellationToken);

            SampleActor actor =
                await _actors.GetOrCreateAsync(
                    auth.ActorType,
                    auth.AccountId,
                    auth.DisplayName,
                    cancellationToken);

            Actor = actor;
            await Context.AttachActorAsync(actor, cancellationToken);

            await Context
                .Reply(new SampleAuthenticateReply
                {
                    AccountId = auth.AccountId,
                    CurrentRoomId = (actor.Spot as SampleSpot)?.RoomId ?? string.Empty
                })
                .Async(cancellationToken);
            return;
        }

        if (string.Equals(
            header.Name,
            "SampleJoinRoomRequest",
            StringComparison.Ordinal))
        {
            SampleJoinRoomRequest join =
                body.Parse<SampleJoinRoomRequest>();

            SampleSpot room =
                await _rooms.GetRequiredAsync(
                    join.RoomId,
                    cancellationToken);

            SampleJoinRoomReply joinReply = await Actor.Context
                .JoinSpot(room.SpotRid, join)
                .Async<SampleJoinRoomReply>(cancellationToken);

            await Context
                .Reply(joinReply)
                .Async(cancellationToken);
            return;
        }

        if (Actor.Spot is null)
        {
            throw new InvalidOperationException(
                "JoinRoom is required before gameplay packets.");
        }

        await Context.DispatchToActorAsync(
            header,
            body,
            cancellationToken);
    }
}

public sealed class SampleJoinRoomHandler
    : IZLinkSpotActorJoinHandler<SampleSpot, SampleActor, SampleJoinRoomRequest, SampleJoinRoomReply>
{
    public ValueTask<SampleJoinRoomReply> HandleAsync(
        SampleSpot spot,
        SampleActor actor,
        SampleJoinRoomRequest request,
        CancellationToken cancellationToken)
    {
        return spot.JoinActorAsync(
            actor,
            request,
            cancellationToken);
    }
}

public interface ISampleRoomDirectory
{
    ValueTask<SampleSpot> GetRequiredAsync(
        string roomId,
        CancellationToken cancellationToken);
}

public interface ISampleAuthVerifier
{
    ValueTask<SampleAuthenticationResult> VerifyAsync(
        string accessToken,
        CancellationToken cancellationToken);
}

public interface ISampleActorFactory
{
    string ActorType { get; }

    ValueTask<SampleActor> CreateAsync(
        string accountId,
        string displayName,
        CancellationToken cancellationToken);
}

public interface ISampleActorFactoryRegistry
{
    ValueTask<SampleActor> GetOrCreateAsync(
        string actorType,
        string accountId,
        string displayName,
        CancellationToken cancellationToken);
}

public sealed record SampleAuthenticationResult(
    string AccountId,
    string DisplayName,
    string ActorType);
```

여기서 `ISampleActorFactoryRegistry`는 `actorType -> factory` 매핑을 잡고,
인증 결과에 따라 `SampleWarriorActor`, `SampleTraderActor` 같은 서로 다른 actor
타입을 고를 수 있게 하는 샘플용 registry로 읽는다. 이 선택은 인증이나 재연결
경계에서 한 번만 일어나고, gameplay packet hot path에는 다시 들어오지 않는 쪽이
맞다.

이 샘플에서 관계를 짧게 정리하면 아래와 같다.

- `SampleSession`
  - stream에서 packet을 받고, 인증을 하고, actor factory를 고르고, room join 전후를 이어 준다.
- `SampleActor`
  - `accountId` 기준으로 살아 있는 논리 객체다.
  - 현재 `Stream`과 현재 `Spot`을 직접 가진다.
- `SampleSpot`
  - room이다.
  - actor table만 소유하고 stream 인증 로직은 모른다.

패킷 흐름은 아래처럼 읽는다.

1. 첫 패킷은 `SampleAuthenticateRequest`다.
2. `SampleSession`이 API 서버에 토큰 검증을 요청한다.
3. 인증 결과의 `actorType`과 `accountId`로 `SampleActor`를 찾거나 새로 만든다.
4. 이미 다른 stream이 붙어 있었다면 framework가 stale 여부를 확인한 뒤 현재 stream을 actor에 다시 attach한다.
5. client가 `SampleJoinRoomRequest`를 보낸다.
6. `SampleSession`은 room 정보를 찾은 뒤 `IZLinkSpotClient.JoinActorAsync(...)`로
   target room의 join callback을 호출한다.
7. 등록된 `SampleJoinRoomHandler`가 같은 `SampleSpot` 실행 문맥에서 승인,
   `Context.JoinActorAsync(actor)` 호출, 기존 room에서의 이탈, 결과 생성까지
   마무리한다.
8. 그 뒤의 `SampleHeartbeatCommand`, `SampleMoveActorCommand`,
   `SampleSendRoomChatCommand` 같은 패킷은 framework 내부 `SubmitAsync(...)`를 통해
   actor가 붙은 `Spot` 문맥으로 제출된다.
9. 같은 `Spot` 실행 문맥 안에서 실제 처리는
   `IZLinkActorContext.AddPacket(...)`으로 등록한 actor packet handler가 맡는다.
10. room 전체 로직이 필요하면 handler가 `actor.Context.GetSpot<SampleSpot>()`으로
    room instance를 가져와 이어서 처리한다.

즉 room은 actor만 붙잡고, session handler는 room 밖에서 인증과 재연결을 처리한다.
이 구조로 보면 "같은 account의 actor는 유지하고 stream만 교체"하는 reconnect 정책이
설명하기 쉽다.
이 샘플 코드는 packet session 기준이지만, raw session도 같은 모델로 읽는다.
즉 raw session은 자기 protocol 규칙으로 chunk를 재조립한 뒤 `header/body`를 만들고,
framework 내부 dispatch 경로로 같은 `Spot` 문맥에 올린 뒤, 최종 actor 로직은
등록된 actor packet handler가 처리한다.
또한 room timer는 room에 join된 actor만 본다. 즉 인증은 끝났지만 아직
`SampleJoinRoomRequest`를 보내지 않은 연결은 `SampleSpot` 바깥의 stream 수명으로
읽고, room 안의 heartbeat timeout과는 구분한다.

### 3.3 handler 클래스

```csharp
 public sealed class SampleGetStateHandler
    : IZLinkSpotRequestHandler<SampleSpot, SampleGetStateRequest, SampleGetStateReply>
{
    private readonly IZLinkSpotClient _spotClient;

    public SampleGetStateHandler(IZLinkSpotClient spotClient)
    {
        _spotClient = spotClient;
    }

    public async ValueTask<SampleGetStateReply> HandleAsync(
        SampleSpot spot,
        SampleGetStateRequest request,
        CancellationToken cancellationToken)
    {
        await _spotClient
            .SendChannel(
                "orders",
                new SampleReportStateQueryCommand
                {
                    SpotRid = spot.Context.SpotRid.ToString()
                })
            .Async(cancellationToken);

        return new SampleGetStateReply
        {
            SpotRid = spot.Context.SpotRid,
            ActorCount = spot.ActorCount,
            ConnectedSessionCount = spot.ConnectedSessionCount
        };
    }
}

public sealed class SampleReportStateHandler
    : IZLinkSpotPacketHandler<SampleSpot, SampleReportStateCommand>
{
    private readonly IZLinkSpotClient _spotClient;

    public SampleReportStateHandler(IZLinkSpotClient spotClient)
    {
        _spotClient = spotClient;
    }

    public async ValueTask HandleAsync(
        SampleSpot spot,
        SampleReportStateCommand message,
        CancellationToken cancellationToken)
    {
        spot.ApplyReportedState(message);

        await _spotClient
            .SendChannel(
                "orders",
                new SampleReportStateQueryCommand
                {
                    SpotRid = spot.Context.SpotRid.ToString()
                })
            .Async(cancellationToken);
    }
}

public sealed class SampleStateUpdatedHandler
    : IZLinkSpotSubscriptionHandler<SampleSpot, SampleStateUpdatedEvent>
{
    private readonly IZLinkSpotClient _spotClient;

    public SampleStateUpdatedHandler(IZLinkSpotClient spotClient)
    {
        _spotClient = spotClient;
    }

    public async ValueTask HandleAsync(
        SampleSpot spot,
        SampleStateUpdatedEvent message,
        CancellationToken cancellationToken)
    {
        await _spotClient
            .RequestChannel(
                "orders",
                new SampleSyncStateRequest
                {
                    SpotRid = spot.Context.SpotRid.ToString(),
                    ActorCount = message.ActorCount,
                    ConnectedSessionCount = message.ConnectedSessionCount
                })
            .WithTimeout(TimeSpan.FromMilliseconds(200))
            .Async<SampleSyncStateReply>(cancellationToken);
    }
}

public sealed class SampleSessionTimeoutSweepHandler
    : IZLinkSpotTimerHandler<SampleSpot>
{
    public ValueTask HandleAsync(
        SampleSpot spot,
        CancellationToken cancellationToken)
    {
        return spot.SweepInactiveActorsAsync(
            TimeSpan.FromSeconds(10),
            TimeSpan.FromMinutes(1),
            cancellationToken);
    }
}
```

이 high-level timer handler는 framework runtime이 만든 managed `.NET` timer 위에
얹는 wrapper로 읽어야 한다. runtime은 `PeriodicTimer` 같은 managed timer에서 tick을
받고, 그 tick을 같은 spot execution context 안으로 넘긴 뒤
`IZLinkSpotTimerHandler<TSpot>.HandleAsync(...)`를 호출하는 모델이 자연스럽다.
이 샘플에서는 timer가 상태 publish를 하는 것이 아니라,
`LastSeenAt`를 기준으로 오래 조용한 actor를 검사하는 sweep 역할을 맡는다.
현재 stream이 있는 actor는 idle timeout을 넘기면 stream bind를 풀고, 이미 stream이
끊긴 actor는 reconnect grace가 지나면 room에서 제거한다.
이때 framework의 `IZLinkTimer.CancelAsync()`는 managed timer loop 정리까지 함께
담은 고수준 handle에 가깝다.

### 3.4 protobuf packet 타입

```csharp
// SampleGetStateRequest, SampleGetStateReply, SampleReportStateCommand,
// SampleStateUpdatedEvent, SampleReportStateQueryCommand, SampleSyncStateRequest,
// SampleAuthenticateRequest, SampleAuthenticateReply,
// SampleJoinRoomRequest, SampleJoinRoomReply,
// SampleHeartbeatCommand, SampleHeartbeatReply,
// SampleMoveActorCommand, SampleMoveActorReply,
// SampleSendRoomChatCommand, SampleRoomChatPushed,
// SampleLeaveRoomCommand은
// .proto에서 생성된 protobuf message 타입이라고 가정한다.
// 실제 generated code는 샘플에서 생략한다.
```

## 4. 이 샘플을 어떻게 읽으면 되는가

이 샘플에서 중요한 부분은 아래다.

- `room.node`는 논리 `SpotNode` 이름이고, `UseSpotDiscovery("game.room", ...)`가
  `game.room` channel mesh 범위를 정한다.
- `SampleSpot`은 단순 handler class가 아니라 실제 spot 객체다.
- `SampleSpot`은 `IZLinkSpot`을 상속받고 자기 `Context.SpotRid`, `Context.NodeRid`를 상태로 가진다.
- `SampleSpot` 안에는 상태와 코어 로직만 두고, packet 처리는 별도 handler class로 뺄 수 있다.
- handler는 `IZLinkSpotClient`를 constructor injection으로 받을 수 있다.
- `SampleSpot`과 그 spot에 귀속된 handler, timer handler, join handler는 같은
  per-spot DI scope에서 resolve된다고 읽는 편이 맞다.
- 그래서 public registration 함수에 `IServiceProvider services`를 매번 넘기지 않고,
  `Context.AddPacket<THandler>()`처럼 타입만 등록하는 쪽이 더 자연스럽다.
- local spot 인스턴스가 없는 외부 노드는 `IZLinkSpotPublisherClient`를 따로
  주입받아 SPOT channel publish를 할 수 있다.
- `Context.AddPacket<THandler>(...)`는 request와 send packet을 함께 등록한다.
- `Context.AddSubscribe<THandler>(...)`는 topic subscription consumer를 등록한다.
- `Context.AddTimer<THandler>(...)`는 spot lifecycle 안에서 timer를 등록한다.
- 다른 channel 호출은 attach된 channel client를 통해 보낸다.
- **`RequestChannel(...).Async(...)`는 같은 spot execution context 안에서 완료된다.**
  arbitrary thread 에서 promise 를 직접 resolve 하지 않으므로, continuation 도
  spot state 에 대해 별도 lock 없이 접근할 수 있다.

즉 이 샘플의 핵심은 두 가지다.

1. `SampleSpot`이 이미 생성된 spot 객체라서, spot rid는 요청 scope에서 따로 꺼내는
   값이 아니라 `SampleSpot.Context.SpotRid` 속성으로 바로 읽는다.
2. subscribe handler, packet handler, timer handler, channel reply callback뿐 아니라
   room에 join된 actor의 client packet과 disconnect 처리도 같은 spot execution
   context 에서 직렬 실행된다. `SampleSpot` 의 mutable state (`ActorCount` 등)에
   별도 lock 이 필요 없는 이유다.

## 5. packet 등록과 subscribe 등록은 어떻게 다른가

이 초안에서는 `SampleSpot`이 자기 초기화 단계에서 직접 아래 세 종류를 등록한다.

- `Context.AddPacket<THandler>(...)`
- `Context.AddSubscribe<THandler>(topic)`
- `Context.AddTimer<THandler>(name, period, ...)`

반면 stream session은 `SampleSpot`이 자기 초기화 단계에서 정적으로 등록하지 않는다.
`SampleSession`은 `stream node`가 생성하고, 먼저 인증과 actor lookup을 마친 뒤
`SampleJoinRoomRequest`가 왔을 때 `IZLinkSpotClient.JoinActorAsync(...)`로
target `SampleSpot`의 join callback을 호출하는 쪽이 이 문서의 기본 모델이다.

packet은 request와 send를 함께 묶는 상위 개념이다.
즉 `SampleGetStateHandler`처럼 reply를 돌려주는 handler도 packet이고,
`SampleReportStateHandler`처럼 응답 없이 상태만 반영하는 handler도 packet이다.

반면 subscribe는 packet이 아니다.
subscribe는 topic fan-out consumer 등록이고, packet은 targeted delivery다.

handler가 다른 서버나 다른 spot으로 outbound 호출을 해야 하면, `SampleSpot`에서
client를 꺼내 쓰기보다 handler가 `IZLinkSpotClient`를 직접 주입받는 쪽이 더
자연스럽다. 이러면 `SampleSpot`은 상태와 코어 로직에 집중하고, outbound 호출
책임은 handler가 맡을 수 있다.

이 샘플 기준으로 `SampleSpot` 안에 남는 것은 아래 정도다.

- `Context.SpotRid`, `Context.NodeRid` 같은 자기 identity
- `ActorCount`, `ConnectedSessionCount` 같은 현재 상태
- `SweepInactiveActorsAsync(...)` 같은 liveness 관리 메서드
- `ApplyReportedState(...)` 같은 순수 도메인 메서드
- 초기 registration을 위한 `Configure()`

## 6. packet 매핑은 무엇을 기준으로 하는가

이 초안에서는 `Context.AddPacket<THandler>(...)`가 문자열을 따로 받지 않는다.
packet 매핑 기준은 header의 `msgId`다.

이 문서의 전체 샘플 코드는 `options.Codecs.AddProtobuf()`를 쓰므로 기본 예시는
`protobuf` 기준이다. 즉 위 코드에서 packet 타입은 모두 `protoc`가 생성한
`IMessage<T>` 계열 타입이라고 보면 된다. 이 문서는 generated protobuf 타입에
framework용 marker interface를 직접 붙이는 방식을 전제로 하지 않는다.

즉 위 코드에서:

- `Context.AddPacket<SampleGetStateHandler>()`
  - `SampleGetStateRequest`의 protobuf `msgName`으로 등록
- `Context.AddPacket<SampleReportStateHandler>()`
  - `SampleReportStateCommand`의 protobuf `msgName`으로 등록

같은 형태로 읽으면 된다.

- `protobuf`를 쓰는 경우
  - `msgId = msgName`
- `json`을 쓰는 경우
  - `msgId = class name`

즉 framework는 `SampleGetStateHandler` 같은 handler 타입에서 packet 타입을 읽고,
그 packet 타입의 `msgId`를 dispatch key로 등록하는 방향을 기본으로 본다.

예를 들면 아래처럼 읽는다.

- `SampleGetStateRequest`의 `msgId`
  - `protobuf`면 protobuf packet name
  - `json`이면 `SampleGetStateRequest`
- `SampleReportStateCommand`의 `msgId`
  - `protobuf`면 protobuf packet name
  - `json`이면 `SampleReportStateCommand`

즉 `Context.AddPacket<SampleGetStateHandler>()`는 결국
`SampleGetStateRequest`의 `msgId`를 기준으로 등록되는 셈이다.

## 7. 어떤 상황에 어떤 API를 쓰는가

- 새 spot 인스턴스를 만들고 싶다
  - `IZLinkSpotManager.CreateAsync("stage", ...)`
- attach된 다른 channel에 send packet을 보내고 싶다
  - `SendChannel(...).Async(...)`
- attach된 다른 channel에 request packet을 보내고 싶다
  - `RequestChannel(...).WithTimeout(...).Async(...)`
- 다른 SPOT peer와 직접 RID routed 호출을 하고 싶다
  - 현재 framework core 기본 표면에는 없다. 필요하면 stage wrapper나 별도 확장
    패키지에서 다룬다.
- 현재 spot 자신의 rid를 알고 싶다
  - `SampleSpot.Context.SpotRid`
- 특정 `spotRid`가 어떤 이름으로 생성됐는지 다시 보고 싶다
  - `IZLinkSpotManager.GetAsync(spotRid)` 또는 `ListAsync()`
- stage 안에서 fan-out 하고 싶다
  - `Publish(topic, ...).Async(...)`
- local spot 인스턴스가 없는 외부 노드에서 특정 SPOT channel로 publish하고 싶다
  - `IZLinkSpotPublisherClient.Publish(channelName, topic, ...).Async(...)`
- stage 안에서 heartbeat timeout sweep 같은 주기 작업을 돌리고 싶다
  - `SampleSpot.OnInitializeAsync()`에서 `Context.AddTimer<THandler>(...)` 등록

## 8. room처럼 써도 되는가

`SPOT`이 room, stage, zone처럼 핫패스가 있는 용도로 쓰이더라도, 이 샘플 구조
자체가 바로 과한 오버헤드를 뜻하는 것은 아니다. 다만 `SPOT` packet handler 쪽은
room 핫패스로 쓰일 수 있으므로, 일반 channel messaging보다 더 강한 최적화 기준이
필요하다.

이 초안에서는 이 선택을 framework가 숨기지 않고, 사용자가 dispatch mode로 직접
고를 수 있어야 한다고 본다.

- `ZLinkDispatchMode.Compiled`
  - registration 또는 warm-up 단계까지만 reflection을 허용한다.
  - hot path는 cached delegate, prebuilt dispatch table, 미리 고른 actor factory만
    사용한다.
- `ZLinkDispatchMode.Dynamic`
  - 늦은 바인딩과 실험 편의를 더 우선한다.
  - 관리용 handler나 초기 prototype에는 허용할 수 있다.

- `AddPacket`, `AddSubscribe`, `AddTimer`는 registration 단계에서만 동작해야 한다.
- reflection은 registration 단계에서 끝나야 한다.
- 런타임 packet 처리 경로에서는 캐시된 dispatch table을 써야 한다.
- handler 호출은 캐시된 delegate 또는 그와 비슷한 저비용 경로여야 한다.
- per-packet allocation과 불필요한 DI 재구성은 줄여야 한다.

즉 샘플의 class 구조보다 더 중요한 것은 실제 실행 경로가 얼마나 짧은가다.
핫패스는 대략 아래 정도로 끝나야 한다.

1. `msgId` 읽기
2. dispatch table lookup
3. packet decode
4. handler resolve 또는 재사용
5. `HandleAsync(...)` 호출

이 문서의 샘플은 "등록 구조"를 보여 주는 예시이고, 실제 성능은 런타임이 reflection을
언제까지 허용하느냐보다, 핫패스에서 그 비용을 제거했느냐에 더 크게 좌우된다.
반대로 일반 socket/service handler가 성능이 낮아도 된다는 뜻은 아니다. 차이는
`SPOT` packet 처리 쪽은 room 핫패스이므로 더 강한 최적화를 먼저 요구하고, 일반
channel messaging 쪽은 편의 기능을 조금 더 허용할 여지가 있다는 점이다.

## 9. 정리

- `SPOT` 기본 모델은 `SampleSpot : IZLinkSpot` 같은 인터페이스 기반 lifecycle로 고정한다.
- current framework core registration 표면은 `AddPacket`, `AddSubscribe`,
  `AddTimer` 같은 명시 등록을 사용한다.
- actor join / actor factory / stream-to-actor bridge 예시는 3.2.1에서 현재 draft
  공용 계약이 room wrapper에 어떻게 매핑되는지 보여 주는 상세 샘플이다.
- packet dispatch 기준은 header `msgId`다.
- `IZLinkSpotManager`는 spot 생성과 조회를 함께 가진다.
- attach된 channel client와 SPOT publish 설정은 capability별 builder에서 노출한다.
