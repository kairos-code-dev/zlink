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

## 2. 인터페이스 초안

먼저 이 문서에서 전제로 두는 인터페이스 초안을 따로 본다.
아래는 샘플 구현이 기대하는 최소 표면이다.

```csharp
public abstract class ZLinkSpot
{
    protected ZLinkSpot(RoutingId spotRid, RoutingId nodeRid)
    {
        SpotRid = spotRid;
        NodeRid = nodeRid;
    }

    public RoutingId SpotRid { get; }
    public RoutingId NodeRid { get; }

    protected void AddPacket<THandler>(IServiceProvider services)
        where THandler : class
    {
    }

    protected void AddSubscribe<THandler>(
        string topic,
        IServiceProvider services)
        where THandler : class
    {
    }

    protected ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        IServiceProvider services,
        CancellationToken cancellationToken)
        where THandler : class
    {
        return ValueTask.FromResult<IZLinkTimer>(default!);
    }

    public virtual ValueTask OnInitializeAsync(
        IServiceProvider services,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkSpotPacketHandler<TSpot, in TMessage>
    where TSpot : ZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRequestHandler<TSpot, in TRequest, TReply>
    where TSpot : ZLinkSpot
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotSubscriptionHandler<TSpot, in TEvent>
    where TSpot : ZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TEvent message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotTimerHandler<TSpot>
    where TSpot : ZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkSpotCreateResult(
    RoutingId SpotRid,
    string SpotName,
    bool Created);

public readonly record struct ZLinkSpotInfo(
    RoutingId SpotRid,
    string SpotName);

public interface IZLinkSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        CancellationToken cancellationToken = default);

    ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}

public interface IZLinkClient
{
    IZLinkSendCall Send<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall<TReply> Request<TReply>(
        string channelName,
        IZLinkRequest<TReply> request);

    IZLinkSendCall SendTo<TMessage>(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message);

    IZLinkRequestCall<TReply> RequestTo<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        IZLinkRequest<TReply> request);
}

public interface IZLinkSpotClient
{
    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall<TReply> RequestChannel<TReply>(
        string channelName,
        IZLinkRequest<TReply> request);

    IZLinkSendCall SendTo<TMessage>(
        RoutingId targetRid,
        RoutingId spotRid,
        TMessage message);

    IZLinkRequestCall<TReply> RequestTo<TReply>(
        RoutingId targetRid,
        RoutingId spotRid,
        IZLinkRequest<TReply> request);

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
    options.Codecs.AddProtobuf();

    options.AddChannel("play", channel =>
    {
        channel.EnableServer();
    });

    options.UseSpotDiscovery("game.stage", registry =>
    {
        registry.Add("tcp://registry1:5551");
    });

    options.AddSpotNode("stage-node", spot =>
    {
        spot.Bind("tcp://0.0.0.0:9000");
        spot.EnableRouter();
        spot.EnablePubSub();
        spot.AttachChannelClient("orders");
        spot.AttachSpotPublisherClient("game.stage");
        spot.AddSpotFactory<StageSpot>("stage");
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
- `AddSpotFactory<StageSpot>("stage")`
  - 이 node가 생성하고 소유할 `StageSpot` factory를 `stage` 이름으로 등록한다.
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
                peers.Connect(
                    RoutingId.Parse("01-00-00-00-00-00-00-10"),
                    "tcp://10.0.0.10:9000");
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

        spot.AddSpotFactory<StageSpot>("stage");
    });
});
```

여기서 중요한 규칙은 아래와 같다.

- `router`, `pub/sub`, attach된 channel client, attach된 spot publisher client는
  서로 다른 연결 집합이다.
- 같은 capability에서는 `Discovery`와 `Manual`을 섞지 않는다.
- 같은 `SpotNode`에서 `spotName`은 비어 있으면 안 된다.
- 이미 등록된 `spotName`을 다시 등록하면 기존 값을 덮어쓰지 않고 예외를 던진다.
- `router` manual 연결은 `RoutingId + endpoint`를 함께 등록한다.
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

- `SpotNode` 자체 기본값
  - topic routed data plane 전체에 공통으로 적용되는 기본값
- capability별 socket 기본값
  - `router`, `pub/sub`, attach된 channel client, attach된 spot publisher client가
    각각 들고 있는 socket 기본값

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

        spot.ConfigureNode(node =>
        {
            node.TopicSendHighWaterMark = 20_000;
            node.TopicReceiveHighWaterMark = 20_000;
            node.RoutedSendHighWaterMark = 10_000;
            node.RoutedReceiveHighWaterMark = 10_000;
        });

        spot.EnableRouter(router =>
        {
            router.ConfigureSocket(socket =>
            {
                socket.SendHighWaterMark = 10_000;
                socket.ReceiveHighWaterMark = 10_000;
                socket.SendTimeout = TimeSpan.FromMilliseconds(200);
                socket.ReceiveTimeout = TimeSpan.FromMilliseconds(200);
            });

            router.ConfigureRouter(options =>
            {
                options.RequestTimeout = TimeSpan.FromMilliseconds(700);
                options.Mandatory = true;
            });
        });

        spot.EnablePubSub(pubsub =>
        {
            pubsub.ConfigurePublisherSocket(socket =>
            {
                socket.SendHighWaterMark = 50_000;
                socket.TcpNoDelay = true;
            });

            pubsub.ConfigureSubscriberSocket(socket =>
            {
                socket.ReceiveHighWaterMark = 50_000;
                socket.ReceiveTimeout = TimeSpan.FromMilliseconds(50);
            });
        });

        spot.AttachChannelClient("orders", client =>
        {
            client.ConfigureSocket(socket =>
            {
                socket.ConnectTimeout = TimeSpan.FromSeconds(3);
                socket.ReconnectInterval = TimeSpan.FromMilliseconds(200);
                socket.ReconnectIntervalMax = TimeSpan.FromSeconds(5);
            });

            client.ConfigureDealer(dealer =>
            {
                dealer.RequestTimeout = TimeSpan.FromMilliseconds(500);
            });
        });

        spot.AttachSpotPublisherClient("game.stage", publisher =>
        {
            publisher.ConfigureSocket(socket =>
            {
                socket.SendHighWaterMark = 20_000;
                socket.SendTimeout = TimeSpan.FromMilliseconds(100);
            });
        });

        spot.AddSpotFactory<StageSpot>("stage");
    });
});
```

이 예시에서 의도하는 구분은 아래와 같다.

- `spot.ConfigureNode(...)`는 `SpotNode` data plane 전체 기본값을 정한다.
- `router.ConfigureSocket(...)`, `pubsub.ConfigurePublisherSocket(...)` 같은 표면은
  각 capability가 소유한 socket 기본 동작을 정한다.
- `client.ConfigureDealer(...)`처럼 socket type 전용 옵션은 capability 전용
  builder에 따로 두는 편이 읽기 쉽다.
- `RequestTo(...).WithTimeout(...)` 같은 호출 단위 옵션은 특정 호출 하나에만
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

### 3.1.3 outbound-only spot client 앱

`SPOT`도 local `SpotNode`를 열지 않고, 다른 channel이나 다른 spot으로만 outbound
호출하는 앱이 있을 수 있다. 이런 경우에는 `IZLinkSpotClient`만 주입받아 쓰고,
local `SpotNode`와 local spot runtime은 만들지 않는다.

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
    IZLinkSpotClient spotClient,
    CancellationToken cancellationToken) =>
{
    var reply = await spotClient
        .RequestChannel(
            "orders",
            new GetStageStateRequest { StageRid = request.StageRid })
        .ExecAsync(cancellationToken);

    return Results.Ok(reply);
});

app.Run();
```

RID를 이미 알고 있는 경우에는 outbound client가 direct routed 호출을 바로 쓸 수도
있다.

```csharp
app.MapPost("/stage/direct-query", async (
    DirectStageRouteHttpRequest request,
    IZLinkClient client,
    CancellationToken cancellationToken) =>
{
    var reply = await client
        .RequestTo<GetStageStateReply>(
        targetRid: RoutingId.Parse(request.TargetNodeRid),
        spotRid: RoutingId.Parse(request.TargetSpotRid),
        request: new GetStageStateRequest
        {
            StageRid = request.TargetSpotRid
        })
        .WithTimeout(TimeSpan.FromMilliseconds(200))
        .ExecAsync(cancellationToken);

    client
        .SendTo(
            targetRid: RoutingId.Parse(request.TargetNodeRid),
            spotRid: RoutingId.Parse(request.TargetSpotRid),
            message: new ReportStageStateCommand
            {
                UserCount = request.UserCount
            })
        .Exec();

    return Results.Ok(reply);
});
```

이 경로는 channel 이름으로 라우팅하지 않고, caller가 `targetRid`와 `spotRid`를
직접 알고 있을 때만 쓰는 direct spot routed 호출이다.

send/publish builder는 기본 blocking submit이고, 필요하면 `WithDontWait()`를
붙여 temporary backpressure 시 `false`를 받을 수 있다.

```csharp
bool submitted = client
    .SendTo(
        targetRid: RoutingId.Parse(request.TargetNodeRid),
        spotRid: RoutingId.Parse(request.TargetSpotRid),
        message: new ReportStageStateCommand
        {
            UserCount = request.UserCount
        })
    .WithDontWait()
    .Exec();
```

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

app.MapPost("/stage/publish", (
    PublishStageStateHttpRequest request,
    IZLinkSpotPublisherClient spotPublisherClient) =>
{
    spotPublisherClient
        .Publish(
            "game.stage",
            "stage.state.updated",
            new StageStateUpdatedEvent
            {
                StageRid = request.StageRid,
                UserCount = request.UserCount
            })
        .Exec();

    return Results.Accepted();
});

app.Run();
```

### 3.2 spot 객체

```csharp
public sealed class StageSpot : ZLinkSpot
{
    private IZLinkTimer? _heartbeat;

    public StageSpot(
        RoutingId spotRid,
        RoutingId nodeRid)
        : base(spotRid, nodeRid)
    {
    }

    public int UserCount { get; private set; } = 10;

    public override async ValueTask OnInitializeAsync(
        IServiceProvider services,
        CancellationToken cancellationToken)
    {
        AddPacket<GetStageStateHandler>(services);
        AddPacket<ReportStageStateHandler>(services);

        AddSubscribe<StageStateUpdatedHandler>(
            "stage.state.updated",
            services);

        _heartbeat = await AddTimer<StageHeartbeatHandler>(
            "heartbeat",
            TimeSpan.FromSeconds(1),
            services,
            cancellationToken);
    }

    public void ApplyReportedState(ReportStageStateCommand command)
    {
        UserCount = command.UserCount;
    }
}
```

### 3.3 handler 클래스

```csharp
public sealed class GetStageStateHandler
    : IZLinkSpotRequestHandler<StageSpot, GetStageStateRequest, GetStageStateReply>
{
    private readonly IZLinkSpotClient _spotClient;

    public GetStageStateHandler(IZLinkSpotClient spotClient)
    {
        _spotClient = spotClient;
    }

    public async ValueTask<GetStageStateReply> HandleAsync(
        StageSpot spot,
        GetStageStateRequest request,
        CancellationToken cancellationToken)
    {
        _spotClient
            .SendChannel(
                "orders",
                new ReportStageQueryCommand
                {
                    StageRid = spot.SpotRid.ToString()
                })
            .Exec();

        return new GetStageStateReply
        {
            SpotRid = spot.SpotRid,
            UserCount = spot.UserCount
        };
    }
}

public sealed class ReportStageStateHandler
    : IZLinkSpotPacketHandler<StageSpot, ReportStageStateCommand>
{
    private readonly IZLinkSpotClient _spotClient;

    public ReportStageStateHandler(IZLinkSpotClient spotClient)
    {
        _spotClient = spotClient;
    }

    public async ValueTask HandleAsync(
        StageSpot spot,
        ReportStageStateCommand message,
        CancellationToken cancellationToken)
    {
        spot.ApplyReportedState(message);

        _spotClient
            .SendChannel(
                "orders",
                new ReportStageQueryCommand
                {
                    StageRid = spot.SpotRid.ToString()
                })
            .Exec();
    }
}

public sealed class StageStateUpdatedHandler
    : IZLinkSpotSubscriptionHandler<StageSpot, StageStateUpdatedEvent>
{
    private readonly IZLinkSpotClient _spotClient;

    public StageStateUpdatedHandler(IZLinkSpotClient spotClient)
    {
        _spotClient = spotClient;
    }

    public async ValueTask HandleAsync(
        StageSpot spot,
        StageStateUpdatedEvent message,
        CancellationToken cancellationToken)
    {
        await _spotClient
            .RequestChannel(
                "orders",
                new SyncStageStateRequest
                {
                    StageRid = spot.SpotRid.ToString(),
                    UserCount = message.UserCount
                })
            .WithTimeout(TimeSpan.FromMilliseconds(200))
            .ExecAsync(cancellationToken);
    }
}

public sealed class StageHeartbeatHandler : IZLinkSpotTimerHandler<StageSpot>
{
    private readonly IZLinkSpotClient _spotClient;

    public StageHeartbeatHandler(IZLinkSpotClient spotClient)
    {
        _spotClient = spotClient;
    }

    public ValueTask HandleAsync(
        StageSpot spot,
        CancellationToken cancellationToken)
    {
        _spotClient
            .Publish(
                "stage.state.updated",
                new StageStateUpdatedEvent
                {
                    StageRid = spot.SpotRid.ToString(),
                    UserCount = spot.UserCount
                })
            .Exec();

        return ValueTask.CompletedTask;
    }
}
```

### 3.4 protobuf packet 타입

```csharp
// GetStageStateRequest, GetStageStateReply, ReportStageStateCommand,
// StageStateUpdatedEvent, ReportStageQueryCommand, SyncStageStateRequest는
// .proto에서 생성된 protobuf message 타입이라고 가정한다.
// 실제 generated code는 샘플에서 생략한다.
```

## 4. 이 샘플을 어떻게 읽으면 되는가

이 샘플에서 중요한 부분은 아래 아홉 가지다.

- `stage-node`는 논리 `SpotNode` 이름이고, `UseSpotDiscovery("game.stage", ...)`가
  `game.stage` channel mesh 범위를 정한다.
- `StageSpot`은 단순 handler class가 아니라 실제 spot 객체다.
- `StageSpot`은 `ZLinkSpot`을 상속받고 자기 `SpotRid`, `NodeRid`를 상태로 가진다.
- `StageSpot` 안에는 상태와 코어 로직만 두고, packet 처리는 별도 handler class로 뺄 수 있다.
- handler는 `IZLinkSpotClient`를 constructor injection으로 받을 수 있다.
- local spot 인스턴스가 없는 외부 노드는 `IZLinkSpotPublisherClient`를 따로
  주입받아 SPOT channel publish를 할 수 있다.
- `AddPacket<THandler>(...)`는 request와 send packet을 함께 등록한다.
- `AddSubscribe<THandler>(...)`는 topic subscription consumer를 등록한다.
- `AddTimer<THandler>(...)`는 spot lifecycle 안에서 timer를 등록한다.
- 다른 channel 호출은 attach된 channel client를 통해 보낸다.

즉 이 샘플의 핵심은 "`StageSpot`이 이미 생성된 spot 객체"라는 점이다.
그래서 현재 spot rid는 요청 scope에서 따로 꺼내는 값이 아니라, `StageSpot.SpotRid`
속성으로 바로 읽는다.

## 5. packet 등록과 subscribe 등록은 어떻게 다른가

이 초안에서는 `StageSpot`이 자기 초기화 단계에서 직접 아래 세 종류를 등록한다.

- `AddPacket<THandler>(...)`
- `AddSubscribe<THandler>(topic, ...)`
- `AddTimer<THandler>(name, period, ...)`

packet은 request와 send를 함께 묶는 상위 개념이다.
즉 `GetStageStateHandler`처럼 reply를 돌려주는 handler도 packet이고,
`ReportStageStateHandler`처럼 응답 없이 상태만 반영하는 handler도 packet이다.

반면 subscribe는 packet이 아니다.
subscribe는 topic fan-out consumer 등록이고, packet은 targeted delivery다.

handler가 다른 서버나 다른 spot으로 outbound 호출을 해야 하면, `StageSpot`에서
client를 꺼내 쓰기보다 handler가 `IZLinkSpotClient`를 직접 주입받는 쪽이 더
자연스럽다. 이러면 `StageSpot`은 상태와 코어 로직에 집중하고, outbound 호출
책임은 handler가 맡을 수 있다.

이 샘플 기준으로 `StageSpot` 안에 남는 것은 아래 정도다.

- `SpotRid`, `NodeRid` 같은 자기 identity
- `UserCount` 같은 현재 상태
- `ApplyReportedState(...)` 같은 순수 도메인 메서드
- 초기 registration을 위한 `OnInitializeAsync(...)`

## 6. packet 매핑은 무엇을 기준으로 하는가

이 초안에서는 `AddPacket<THandler>(...)`가 문자열을 따로 받지 않는다.
packet 매핑 기준은 header의 `msgId`다.

이 문서의 전체 샘플 코드는 `options.Codecs.AddProtobuf()`를 쓰므로 기본 예시는
`protobuf` 기준이다. 즉 위 코드에서 packet 타입은 모두 `protoc`가 생성한
`IMessage<T>` 계열 타입이라고 보면 된다. 이 문서는 generated protobuf 타입에
framework용 marker interface를 직접 붙이는 방식을 전제로 하지 않는다.

즉 위 코드에서:

- `AddPacket<GetStageStateHandler>(services)`
  - `GetStageStateRequest`의 protobuf `msgName`으로 등록
- `AddPacket<ReportStageStateHandler>(services)`
  - `ReportStageStateCommand`의 protobuf `msgName`으로 등록

같은 형태로 읽으면 된다.

- `protobuf`를 쓰는 경우
  - `msgId = msgName`
- `json`을 쓰는 경우
  - `msgId = class name`

즉 framework는 `GetStageStateHandler` 같은 handler 타입에서 packet 타입을 읽고,
그 packet 타입의 `msgId`를 dispatch key로 등록하는 방향을 기본으로 본다.

예를 들면 아래처럼 읽는다.

- `GetStageStateRequest`의 `msgId`
  - `protobuf`면 protobuf message name
  - `json`이면 `GetStageStateRequest`
- `ReportStageStateCommand`의 `msgId`
  - `protobuf`면 protobuf message name
  - `json`이면 `ReportStageStateCommand`

즉 `AddPacket<GetStageStateHandler>(...)`는 결국
`GetStageStateRequest`의 `msgId`를 기준으로 등록되는 셈이다.

## 7. 어떤 상황에 어떤 API를 쓰는가

- 새 spot 인스턴스를 만들고 싶다
  - `IZLinkSpotManager.CreateAsync("stage", ...)`
- attach된 다른 channel에 send packet을 보내고 싶다
  - `SendChannel(...).Exec()`
- attach된 다른 channel에 request packet을 보내고 싶다
  - `RequestChannel(...).WithTimeout(...).ExecAsync(...)`
- 다른 SPOT peer에 routed packet을 보내고 싶다
  - `IZLinkClient.SendTo(...).Exec()` 또는 `IZLinkSpotClient.SendTo(...).Exec()`
- 다른 SPOT peer에 routed request를 보내고 싶다
  - `IZLinkClient.RequestTo(...).WithTimeout(...).ExecAsync(...)` 또는
    `IZLinkSpotClient.RequestTo(...).WithTimeout(...).ExecAsync(...)`
- 현재 spot 자신의 rid를 알고 싶다
  - `StageSpot.SpotRid`
- 특정 `spotRid`가 어떤 이름으로 생성됐는지 다시 보고 싶다
  - `IZLinkSpotManager.GetAsync(spotRid)` 또는 `ListAsync()`
- stage 안에서 fan-out 하고 싶다
  - `Publish(topic, ...).Exec()`
- local spot 인스턴스가 없는 외부 노드에서 특정 SPOT channel로 publish하고 싶다
  - `IZLinkSpotPublisherClient.Publish(channelName, topic, ...).Exec()`
- stage 안에서 heartbeat 같은 주기 작업을 돌리고 싶다
  - `StageSpot.OnInitializeAsync()`에서 `AddTimer<THandler>(...)` 등록

## 8. room처럼 써도 되는가

`SPOT`이 room, stage, zone처럼 핫패스가 있는 용도로 쓰이더라도, 이 샘플 구조
자체가 바로 과한 오버헤드를 뜻하는 것은 아니다. 다만 `SPOT` packet handler 쪽은
room 핫패스로 쓰일 수 있으므로, 일반 channel messaging보다 더 강한 최적화 기준이
필요하다.

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

## 9. 피드백 포인트

- `StageSpot : ZLinkSpot` 같은 상속 모델이 자연스러운가
- `AddPacket / AddSubscribe / AddTimer` 같은 등록 표면이 자연스러운가
- packet 매핑을 header `msgId` 기준으로 보는 것이 자연스러운가
- `IZLinkSpotManager`가 spot 객체 factory와 어떻게 연결되는 것이 자연스러운가
- attach된 channel client 설정을 어떤 표면으로 노출하는 것이 자연스러운가
