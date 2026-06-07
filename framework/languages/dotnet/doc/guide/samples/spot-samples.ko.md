<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../doc/README.ko.md) | [이전: ZLink Framework .NET Channel Messaging Samples](./channel-messaging-samples.ko.md) | [다음: ZLink Framework .NET STREAM Samples](./stream-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../../doc/spec/draft/README.ko.md)

[.NET 묶음](../../README.ko.md) | [인터페이스](../../spec/handler-interfaces.ko.md) | [SPOT](../../spec/aspnet-core-spot.ko.md) | [Stage wrapper](../../spec/stage-wrapper-on-spot.ko.md) | [channel](../../spec/aspnet-core-channel-messaging.ko.md)

# ZLink Framework .NET SPOT Samples

> 이 문서는 실행 가능한 sample 중심 문서다. room, stage, zone 같은 도메인에 ZLink 를
> 넣을지 판단하는 설명은 [12-grpc-alternative](../12-grpc-alternative.ko.md)와 각
> 케이스 스터디가 맡고, 이 문서는 SPOT 등록과 호출 코드를 따라 하는 데 집중한다.

## 1. 이 문서의 목적

`SPOT` 은 다음 두 갈래가 한꺼번에 얽힌 모델이다.

- room, stage, zone 같은 논리 인스턴스 모델
- targeted packet, subscribe, timer 같은 메시지 표면

그래서 글로만 설명을 읽으면 그림이 잘 잡히지 않는다. 이 문서는 다음 요소들을
한 자리에 모아 코드로 보여 준다.

1. `SpotNode`[^spotnode] 등록
2. spot 객체 생성
3. packet handler[^handler] 등록
4. subscribe handler 등록
5. timer 등록
6. channel[^channel] send / request
7. topic publish

다만 현재 구현 범위를 기준으로 보면, 이 문서는 두 층으로 나눠서 읽어야 한다.

- 3.1 과 3.1.4, 그리고 이 절에 등장하는 `IZLinkSpot` / `IZLinkSpotOutbound`
  예시는 현재 framework core 의 public surface 에 맞춘 샘플이다.
- 3.2.1 의 actor[^actor] / session[^session] membership 예시는 stage wrapper
  같은 상위 확장 아이디어를 메모해 둔 부분이다. 아직 framework core 의 public
  surface 는 아니다.
- `targetRid + spotRid` 형태의 direct routed[^direct-routed] 호출은 framework
  public surface 에 포함하지 않는다. spot rid 기반 호출은 모두
  `IZLinkSpotOutbound` 가 resolver[^resolver] 를 거쳐 처리한다.

## 2. 인터페이스 초안

이 절은 샘플이 전제로 삼는 인터페이스 초안을 한 자리에 모아 둔다. 아래는
샘플 구현이 의존하는 최소한의 표면 정의다.

> **주의**: 아래 정의는 [handler-interfaces.ko.md](../../spec/handler-interfaces.ko.md)의
> 해당 섹션과 동일하다. 인터페이스가 바뀌면 두 문서를 함께 갱신해야 하며,
> 최신 계약은 언제나 `handler-interfaces.ko.md`를 기준으로 본다.

```csharp
public interface IZLinkSpot
{
    IZLinkSpotContext Context { get; }

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

public readonly record struct RoutingId(string Value)
{
    public override string ToString() => Value;
}

public interface IZLinkSpotOutbound
{
    IZLinkSendCall SendToSpot<TMessage>(
        RoutingId spotRid,
        TMessage message)
    {
        return default!;
    }

    IZLinkRequestCall RequestToSpot<TRequest>(
        RoutingId spotRid,
        TRequest request)
    {
        return default!;
    }

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message)
    {
        return default!;
    }

    IZLinkSendCall SendToChannel<TMessage>(
        string channelName,
        TMessage message)
    {
        return default!;
    }

    IZLinkRequestCall RequestToChannel<TRequest>(
        string channelName,
        TRequest request)
    {
        return default!;
    }
}

public interface IZLinkSpotContext : IZLinkSpotOutbound
{
    RoutingId SpotRid { get; }
    RoutingId NodeRid { get; }

    // IZLinkSpot.Context 는 framework 가 생성자에 넘긴 context 를
    // 그대로 노출하는 공개 계약이다.

    void AddPacket<THandler>()
        where THandler : class
    {
    }

    void AddSubscribe<THandler>(
        string topic)
        where THandler : class
    {
    }

    void AddHandler<THandler>()
        where THandler : class
    {
    }

    void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor
    {
    }

    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default)
        where THandler : class
    {
        return ValueTask.FromResult<IZLinkTimer>(default!);
    }
}

public interface IZLinkSpotPacketHandler<TSpot, in TMessage>
    where TSpot : class
{
    ValueTask HandleAsync(
        TSpot spot,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRequestHandler<TSpot, in TRequest, TReply>
    where TSpot : class
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotSubscriptionHandler<TSpot, in TEvent>
    where TSpot : class
{
    ValueTask HandleAsync(
        TSpot spot,
        TEvent message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotTimerHandler<TSpot>
    where TSpot : class
{
    ValueTask HandleAsync(
        TSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken);
}

// Low-level Zlink binding basis
public sealed class Timer : IZlinkTimer
{
    public static Timer FromSpot(Spot spot);

    public void Start(TimeSpan interval, ulong repeatCount);

    public void Stop();

    public ulong? Recv(RecvFlags flags = RecvFlags.None);

    public void OnFire(Action<IZlinkTimer, ulong> handler);

    public void Close();
}

public readonly record struct ZLinkSpotCreateResult(
    RoutingId SpotRid,
    ZLinkSpotCreateState State,
    Message? Reply);

public readonly record struct ZLinkSpotInfo(
    RoutingId SpotRid);

// IZLinkSpotManager is defined in handler-interfaces.ko.md section 6.3.

public interface IZLinkChannelClient
{
    IZLinkSendCall Send<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall Request<TMessage>(
        string channelName,
        TMessage request);
}

public interface IZLinkSpotOutbound
{
    IZLinkSendCall SendToSpot<TMessage>(
        RoutingId spotRid,
        TMessage message);

    IZLinkRequestCall RequestToSpot<TMessage>(
        RoutingId spotRid,
        TMessage request);

    IZLinkSendCall SendToChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall RequestToChannel<TMessage>(
        string channelName,
        TMessage request);

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message);
}

public interface IZLinkSpotPublisherClient
{
    IZLinkPublishCall PublishSpot<TEvent>(
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

이 절부터 실제 등록 코드와 spot 객체, handler, session 흐름 샘플을 차례로
보여 준다.

### 3.1 등록 코드

이 등록 코드는 `SpotNode` 와 그 안의 capability(`router`, `pub/sub`, attach 된
channel client, attach 된 spot publisher client)를 한 번에 묶어 둔 모양이다.

```csharp
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using Stage.Protocol; // protoc generated

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();

    options.AddClientServerChannel("play", channel =>
    {
        channel.EnableServer(server =>
        {
            server.Bind("tcp://0.0.0.0:7101");
        });
    });

    options.AddSpotMesh("game.stage", mesh =>
    {
        mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://registry1:5551"));

        mesh.AddNode("stage-node", node =>
        {

            node.EnableRouter(router =>
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

                router.ConfigureRouting(routing =>
                {
                    routing.RequireKnownPeer = true;
                    routing.AllowPeerHandover = true;
                });
            });

            node.AcceptSpotRoutesFromChannel("play");

            node.EnablePubSub(pubsub =>
            {
                pubsub.BindPubSub("tcp://0.0.0.0:9000");
                pubsub.ConfigurePublisher(pubOpt =>
                {
                    pubOpt.SendHighWaterMark = 50_000;
                    pubOpt.SendTimeout = TimeSpan.FromMilliseconds(100);
                    pubOpt.NoDrop = true;
                });

                pubsub.ConfigureSubscriber(subOpt =>
                {
                    subOpt.ReceiveHighWaterMark = 50_000;
                    subOpt.ReceiveTimeout = TimeSpan.FromMilliseconds(50);
                    subOpt.Linger = TimeSpan.Zero;
                });
            });

            node.AttachChannelClient("orders", client =>
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

            node.AttachSpotPublisherClient("game.stage", publisher =>
            {
                publisher.ConfigureSocket(socket =>
                {
                    socket.SendHighWaterMark = 20_000;
                    socket.SendTimeout = TimeSpan.FromMilliseconds(100);
                    socket.Immediate = true;
                });
            });

            node.AddSpotFactory<SampleSpot>();
        });
    });
});

var app = builder.Build();
app.Run();
```

이 코드에서 capability[^capability] 와 attach 함수가 각각 다른 역할을 맡는다.
역할을 항목별로 짚어 보면 다음과 같다.

- `AttachChannelClient("orders")`
  - stage spot이 `orders` channel로 send / request를 보낼 때 사용할 outbound
    client를 붙인다.
- `EnableRouter(router => router.BindRouter(endpoint))`
  - 같은 SPOT channel에 속한 다른 `SpotNode`와 routed packet을 주고받기 위한
    local router를 켜고 routed ingress endpoint를 명시한다.
- `AcceptSpotRoutesFromChannel("play")`
  - `play` client/server channel의 server `ROUTER`에서 이 node의 user Spot으로
    routed send/request를 보낼 수 있게 한다.
  - 같은 표면은 `AddRouteMeshChannel(...)`의 route mesh `ROUTER`에도 사용할 수 있다.
  - 이 설정은 target SpotNode 쪽 ingress 연결이다. application public surface 에
    별도 routed Spot egress client 를 노출하지 않는다.
- `EnablePubSub()`
  - local spot 문맥에서 `spot.Context.Outbound.Publish(...)`를 호출할 수 있게 한다.
- `AttachSpotPublisherClient("game.stage")`
  - local spot 인스턴스가 없는 외부 노드가 `game.stage` SPOT channel로 publish할
    수 있도록 별도의 publisher client를 붙인다.
- `ConfigureSocket(...)`, `ConfigureRouting(...)`,
  `ConfigurePublisher(...)`, `ConfigureSubscriber(...)`
  - 실제 `.NET` 바인딩의 `CommonSocketOptions`, route policy 옵션, outbound
    route policy 옵션, `SpotNodePublisherOptions`, `SpotNodeSubscriberOptions`
    같은 typed facade[^typed-facade]를 capability별로 등록한다.
  - 호출 단위로 적용되는 `Timeout(...)`과 달리, 이쪽은 runtime의 기본 동작을
    정해 두는 설정이다.
- `AddSpotFactory<SampleSpot>()`
  - 이 node가 생성하고 소유할 `SampleSpot` factory[^factory]를 `sample`이라는
    이름으로 등록한다.
  - 하나의 `SpotNode` 안에 서로 다른 이름으로 여러 spot factory를 둘 수 있고,
    실제 생성 시점에는 이 이름을 키로 어떤 타입을 만들지 결정한다.
  - 이미 등록된 이름을 다시 사용하면 조용히 덮어쓰지 않고 예외를 던진다.

### 3.1.1 capability별 수동 연결

이 절은 discovery 대신 endpoint 를 직접 지정해 capability 를 잇는 모양을
보여 준다.

SPOT 역시 일반 channel 과 마찬가지로 수동 연결은 capability 단위로 다뤄야
한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.AddProtobuf();

    options.AddSpotMesh("game.stage", mesh =>
    {
        mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://registry1:5551"));

        mesh.AddNode("stage-node", node =>
        {

            node.EnableRouter(router =>
            {
                router.UseManualConnections(peers =>
                {
                    // Remote SpotNode router endpoint
                    peers.Connect("tcp://10.0.0.10:9000");
                });
            });

            node.EnablePubSub(pubsub =>
            {
                pubsub.BindPubSub("tcp://0.0.0.0:9000");
                pubsub.UseManualConnections(peers =>
                {
                    // Remote SpotNode mesh PUB endpoint.
                    // The local mesh SUB side connects to this address.
                    peers.Connect("tcp://10.0.0.20:9100");
                });
            });

            node.AttachChannelClient("orders", client =>
            {
                client.UseManualConnections(peers =>
                {
                    // Remote orders channel server endpoint
                    peers.Connect("tcp://10.0.0.30:9200");
                });
            });

            node.AttachSpotPublisherClient("game.stage", publisher =>
            {
                publisher.UseManualConnections(peers =>
                {
                    // Remote game.stage SPOT publish endpoint
                    peers.Connect("tcp://10.0.0.40:9300");
                });
            });

            node.AddSpotFactory<SampleSpot>();
        });
    });
});
```

이 코드에서 꼭 짚어야 할 규칙은 다음과 같다.

- `router`, `pub/sub`, attach 된 channel client, attach 된 spot publisher
  client 는 서로 별개의 연결 집합을 다룬다.
- 같은 capability 안에서는 `Discovery`[^discovery] 와 `Manual` 방식을 섞지
  않는다.
- 같은 `SpotNode` 안에서 `spotRid` 은 비어 있으면 안 된다.
- 이미 등록된 `spotRid` 을 다시 등록하면, 기존 값을 덮어쓰지 않고 예외가
  발생한다.
- `router` 의 수동 연결도 endpoint 집합만 등록한다. 이 문서에서는
  `Connect(...)` 호출 시 remote router id 를 별도 파라미터로 받지 않는다.
- channel client 의 수동 연결도 endpoint 집합만 등록한다. 하부 `DEALER` 가
  이미 connect 된 peer 집합을 대상으로 요청을 보내므로, remote `RoutingId` 를
  따로 넘기지 않아도 된다.
- `pub/sub` 는 local publish 소켓과 local subscribe 소켓을 함께 쓰는
  capability 다. 다만 수동 연결에서 등록하는 주소는 "다른 `SpotNode` 의 mesh
  publish bind 주소"라는 점에 유의한다.
- 즉 `EnablePubSub(...).UseManualConnections(...).Connect(endpoint)` 는 local
  `SUB/XSUB` 쪽이 remote `PUB/XPUB` 주소에 붙는다는 의미다.
- `pub/sub` 와 spot publisher client 는 둘 다 endpoint 집합만 등록한다. 다만
  그 endpoint 집합 자체는 서로 다르다.
  - `pub/sub` 의 endpoint 는 peer `SpotNode` 의 mesh 주소다.
  - spot publisher client 의 endpoint 는 외부 publish ingress[^ingress] 주소다.

#### 소켓 옵션 설정 샘플

이 절은 socket 옵션과 pub/sub 옵션을 어떤 단위로 보면 좋은지 정리한다.

`SPOT` 에서는 옵션을 누가 소유하는지를 두 단계로 나눠서 보는 편이 읽기 쉽다.

- `SpotNode` 의 pub/sub 기본값
  - 실제 low-level 바인딩의 `SpotNode.PublisherOptions`,
    `SpotNode.SubscriberOptions` 에 대응한다.
- capability 별 socket 기본값
  - `router`, attach 된 channel client, attach 된 spot publisher client 가 각각
    가지고 있는 `CommonSocketOptions` 계열 기본값이다.

아래 코드는 확정된 계약은 아니다. 다만 `.NET` 표면에서 이런 식으로 보이는
편이 자연스럽다는 방향을 보여 주는 예시다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.AddProtobuf();

    options.AddSpotMesh("game.stage", mesh =>
    {
        mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://registry1:5551"));

        mesh.AddNode("stage-node", node =>
        {

            node.EnableRouter(router =>
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

                router.ConfigureRouting(routing =>
                {
                    routing.RequireKnownPeer = true;
                    routing.AllowPeerHandover = true;
                });
            });

            node.EnablePubSub(pubsub =>
            {
                pubsub.BindPubSub("tcp://0.0.0.0:9000");
                pubsub.ConfigurePublisher(pubOpt =>
                {
                    pubOpt.SendHighWaterMark = 50_000;
                    pubOpt.SendTimeout = TimeSpan.FromMilliseconds(100);
                    pubOpt.NoDrop = true;
                });

                pubsub.ConfigureSubscriber(subOpt =>
                {
                    subOpt.ReceiveHighWaterMark = 50_000;
                    subOpt.ReceiveTimeout = TimeSpan.FromMilliseconds(50);
                    subOpt.Linger = TimeSpan.Zero;
                });
            });

            node.AttachChannelClient("orders", client =>
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

            node.AttachSpotPublisherClient("game.stage", publisher =>
            {
                publisher.ConfigureSocket(socket =>
                {
                    socket.SendHighWaterMark = 20_000;
                    socket.SendTimeout = TimeSpan.FromMilliseconds(100);
                    socket.Immediate = true;
                });
            });

            node.AddSpotFactory<SampleSpot>();
        });
    });
});
```

이 예시가 의도하는 구분은 다음과 같다.

- `pubsub.ConfigurePublisher(...)`,
  `pubsub.ConfigureSubscriber(...)` 는 실제 `.NET` 바인딩의
  `SpotNode.PublisherOptions`, `SpotNode.SubscriberOptions` 처럼, `SPOT` mesh
  자체가 소유하는 옵션 facade 를 capability 표면으로 끌어올린 것이다.
- `router.ConfigureSocket(...)`, `client.ConfigureSocket(...)` 같은 표면은
  `CommonSocketOptions` 같은 공통 socket 기본 동작을 정한다.
- `router.ConfigureRouting(...)`, `client.ConfigureRouting(...)` 은 route
  policy 와 outbound route policy 에 대응하는 capability 전용 facade 다.
- `RequestToChannel(...).Timeout(...)` 같은 호출 단위 옵션은 그 호출 하나에만
  적용된다. 반면 위에 정리한 설정은 runtime 의 기본값을 잡는 용도다.

이렇게 두면 두 가지 이점이 생긴다.

- framework 사용자는 low-level `spot_node_option` 이나 `setsockopt` 이름을
  표면에서 직접 마주칠 일이 없다.
- 어떤 옵션이 node 전체에 적용되고, 어떤 옵션이 개별 capability 에만
  적용되는지 바로 구분할 수 있다.

### 3.1.2 spot 생성과 조회

이 절은 spot 인스턴스를 만들고 그 인스턴스를 다시 조회하는 표면을 정리한다.

spot 인스턴스를 만들 때는 factory 타입이 아니라 등록할 때 사용한 이름을
넘긴다. 이렇게 해야 같은 `SpotNode` 에 여러 spot factory 가 등록되어 있을 때
어떤 타입을 만들지 명확해진다.

```csharp
app.MapPost("/stage/create", async (
    IZLinkSpotManager spotManager,
    CancellationToken cancellationToken) =>
{
    var created = await spotManager.CreateAsync<StageSpot>(cancellationToken);
    var spotInfo = await spotManager.GetAsync(created.SpotRid, cancellationToken);

    return Results.Ok(new
    {
        created.SpotRid,
        created.State,
        LookupRid = spotInfo?.SpotRid
    });
});
```

조회 표면은 public 식별자인 `SpotRid`만 돌려준다. 어떤 factory 타입으로 생성됐는지는
framework 내부 소유 정보이며 application contract로 다시 노출하지 않는다.

### 3.1.3 outbound-only SPOT-aware 앱

이 절은 local `SpotNode` 를 띄우지 않고 다른 노드로 호출만 보내는 앱의
표면을 다룬다.

local `SpotNode` 나 local spot runtime 을 띄우지 않고, 다른 channel 이나 다른
spot 으로 outbound 호출만 하는 앱도 있을 수 있다. 이런 경우 기본 outbound
표면은 `IZLinkChannelClient` 다.

local `SpotNode` 가 없으면 attach 된 channel client 경로도 존재할 수 없다.
따라서 `spot.Context.Outbound.RequestToChannel(...)` 같은 표면을 바로 사용하는
모델로 설명하면 안 된다.

current Spot callback 안에서는 `spot.Context.Outbound.SendToSpot(...)` /
`RequestToSpot(...)` 을 사용한다. 일반 HTTP handler나 channel handler처럼 current Spot 이
없는 코드에는 target Spot 으로 직접 send/request 하는 별도 public client 를 두지 않는다.

```csharp
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);
    options.Codecs.AddProtobuf();

    options.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://registry1:5551"));

    options.AddClientServerChannel("gateway.client", channel =>
    {
        channel.EnableClient(client =>
        {
            client.UseManualConnections(peers =>
            {
                peers.Connect("tcp://play-node-1:7201");
            });
        });
        channel.EnableSpotRouteEgress("play");
    });
});

var app = builder.Build();

app.MapPost("/stage/query", async (
    GetStageStateHttpRequest request,
    CancellationToken cancellationToken) =>
{
    // current Spot 밖에서는 actor 생성 또는 Entry Spot join 으로 ActorRef 를 얻은 뒤
    // session actor handle 로 bind 하는 흐름을 사용한다.
    var reply = new SampleGetStateReply(request.StageRid.ToString());

    return Results.Ok(reply);
});

app.Run();
```

현재 framework core 의 public surface 에는 RID[^rid] 기반의 direct routed
호출이 열려 있지 않다. 그래서 이 샘플도 현행 계약을 다음 두 가지만으로
한정해 본다.

- `channelName` 기준의 request / send
- `IZLinkSpotPublisherClient` 기반 publish

send / publish builder 는 기본적으로 async submit 이다. 일시적인
backpressure[^backpressure] 는 별도의 public no-wait 옵션을 호출자에게
노출하지 않는다. 그 대신 framework 내부에서 nonblocking send, pending queue,
ready notification 을 조합해 처리한다. send 대기 한계는 channel 또는 socket
의 `SendTimeout` 옵션을 따른다.

### 3.1.4 외부 노드에서 SPOT channel publish

이 절은 local spot 인스턴스 없이 publish 만 해야 하는 외부 노드의 표면을
다룬다.

local spot 인스턴스가 없는 외부 노드가 특정 SPOT channel 로 publish 해야 할
때가 있다. 이런 경우에는 `IZLinkSpotPublisherClient` 를 별도로 주입받아
사용한다.

이 샘플에서 publisher 노드는 spot mesh 등록에 attach 만 걸어 두면 충분하다.
실제로 어느 SPOT channel 에 publish 할지는, mesh discovery scope 와
`AttachSpotPublisherClient("game.stage")` 가 함께 결정한다.

```csharp
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.AddProtobuf();

    options.AddSpotMesh("game.stage", mesh =>
    {
        mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://registry1:5551"));

        mesh.AddNode("publisher-node", node =>
        {
            node.AttachSpotPublisherClient("game.stage");
        });
    });
});

var app = builder.Build();

app.MapPost("/stage/publish", async (
    PublishStageStateHttpRequest request,
    IZLinkSpotPublisherClient spotPublisherClient,
    CancellationToken cancellationToken) =>
{
    await spotPublisherClient
        .PublishSpot(
            "game.stage",
            "sample.state.updated",
            new SampleStateUpdatedEvent
            {
                SpotRid = request.StageRid,
                ActorCount = request.UserCount,
                ConnectedSessionCount = request.UserCount
            })
        .Submit(cancellationToken);

    return Results.Accepted();
});

app.Run();
```

### 3.2 spot 객체

이 절은 spot 객체가 어떤 모양으로 등록되고, 어떤 상태와 handler 를 들고
있는지를 코드로 보여 준다.

```csharp
public sealed class SampleSpot(IZLinkSpotContext context) : IZLinkSpot<SampleActor>
{
    private IZLinkTimer? _sessionTimeoutSweep;

    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddPacket<SampleGetStateHandler>();
        Context.AddPacket<SampleReportStateHandler>();
        Context.AddHandler<SampleMoveActorHandler>();

        Context.AddSubscribe<SampleStateUpdatedHandler>(
            "sample.state.updated");
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        SampleActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        var join = request.Decode<SampleJoinRoomRequest>();
        var reply = new SampleJoinRoomReply(join.RoomId).Encode();
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(reply));
    }

    public int ActorCount { get; private set; } = 10;

    public int ConnectedSessionCount { get; private set; } = 8;

    public async ValueTask OnInitializeAsync(
        CancellationToken cancellationToken)
    {
        _sessionTimeoutSweep = await Context.AddTimer<SampleSessionTimeoutSweepHandler>(
            "session-timeout-sweep",
            TimeSpan.FromSeconds(1),
            cancellationToken: cancellationToken);
    }

    public void ApplyReportedState(SampleReportStateCommand command)
    {
        ActorCount = command.ActorCount;
        ConnectedSessionCount = command.ConnectedSessionCount;
    }
}
```

### 3.2.1 room이 먼저 있고, session이 인증한 뒤 actor가 room에 들어가는 상세 샘플

이 절은 인증, actor 매칭, room join, 재연결까지 한 흐름으로 풀어 쓰는 가장
긴 샘플이다.

> 이 절은 현재 draft framework core 가 포함하는 actor / session membership
> 표면을 stage wrapper 관점에서 길게 풀어 쓴 상세 샘플이다. room 정책과 actor
> factory 선택 규칙 자체는 응용 계층의 예시다. 다만 `IZLinkActor`,
> `IZLinkActorContext.JoinSpot(...)`, `IZLinkSessionContext` 는 현행 public
> surface 다.

게임 서버에서는 보통 다음과 같은 분담이 흔하다.

- 웹 서버 쪽: outgame, 로비, 매치메이킹을 처리한다.
- room 서버 쪽: 이미 만들어진 room 에 플레이어를 들이는 역할만 맡는다.

이 절의 샘플도 같은 흐름을 그대로 따른다.

샘플이 가정하는 순서는 다음과 같다.

1. 웹 서버가 room 서버에 room 생성 요청을 미리 보낸다.
2. 웹 서버가 client에게 room 서버 주소와 `roomId`를 내려 준다.
3. client가 room 서버에 연결한다.
4. `SampleSession`이 `SampleAuthenticateRequest`를 받고, API 서버에 토큰 검증을
   요청한다.
5. 검증이 끝나면 `actorType + accountId`로 기존 `SampleActor`를 찾거나 새로 만든다.
6. actor와 stream[^stream]을 먼저 연결한다.
7. client가 `SampleJoinRoomRequest`를 보내면 actor 쪽 handler가
   `IZLinkActorContext.JoinSpot(...)`을 통해 target room에 join 요청을 넣는다.
8. join이 끝난 뒤의 heartbeat, gameplay packet, disconnect, timer는 같은
   `SampleSpot` 실행 문맥에서 처리한다고 본다.

이 절에서 경계는 다음과 같이 잡는다.

- framework draft 계약
  - `IZLinkSpot`
  - `IZLinkSession`
  - `IZLinkSessionContext`
  - `IZLinkActor`
  - `IZLinkActorContext`
  - `IZLinkSpot<TActor>.OnActorJoinAsync(...)`
- 샘플 전용 타입
  - `SampleSpot`
  - `SampleSession`
  - `SampleActor`
  - `ISampleRoomDirectory`
  - `ISampleAuthVerifier`
  - `ISampleActorFactoryRegistry`

핵심은 `Spot` 이 session 을 직접 소유하지 않고 actor 만 다룬다는 점이다.
역할을 항목별로 나눠 보면 다음과 같다.

- session handler 는 인증과 재연결을 담당하는 ingress adapter 다.
- 실제 live 연결은 `IZLinkSessionContext` 가 관리한다.
- actor 는 계정 단위의 논리 객체다. 따라서 stream 이 끊겨도 actor 자체는
  room 에 남아 있을 수 있다.
- 새 stream 이 같은 `accountId` 로 다시 들어오면 같은 actor 에 다시 연결할
  수 있다.

이 모델을 public 계약으로 노출하려면, actor join 은 다음 표면들의 조합으로
나타나야 한다.

- session 의 `BindAsync(...)`
- `IZLinkActorContext.JoinSpot(...)`
- target user Spot 의 `OnActorJoinAsync(...)`

join 승인과 membership 기록은 반드시 target `Spot` 의 실행 문맥에서 처리되어야
하기 때문이다.

actor 타입은 handler 의 generic 인자로 명시한다. 이렇게 두면 두 가지 이점이
있다.

- handler 내부에서 `IZLinkActor` 를 다시 캐스팅할 필요가 없다.
- 잘못된 actor 타입이 등록되거나 전달되면 runtime 이 명확한 오류로 막을 수
  있다.

샘플 코드를 읽기 전에 먼저 살펴봐야 하는 framework draft 계약은 actor 계약
하나다.

```csharp
public interface IZLinkActor
{
    string ActorId { get; }

    IZLinkActorContext Context { get; }

    void Configure()
    {
    }
}
```

이 인터페이스를 room 모델에 대입해 보면 의미는 다음과 같다.

- `IZLinkActor`
  - `accountId` 처럼 stable identity[^stable-identity] 를 가진 논리 객체다.
- `IZLinkActorContext`
  - actor 생성 시 constructor 를 통해 주입되는 framework context 다. actor 는
    이 context 를 외부에 직접 공개하지 않는다. 필요한 동작을 자기 method 안에
    감싸 둔다.
- `IZLinkActor.Configure()`
  - actor 가 생성된 직후 한 번 호출된다. actor 자체의 초기화에만 사용한다.
    actor packet handler 등록은
    `IZLinkSpotContext.AddHandler<THandler>()` 로 분리한다.
- `IZLinkActorContext.GetSpot<TSpot>()`
  - 현재 들어가 있는 room 인스턴스를 가져온다. 아직 room 에 join 하지 않았다면
    실패한다.
- stream attach / disconnect 와 room join / leave 는 서로 다른 수명을 가진다.
- framework 는 stale[^stale] disconnect 를 내부에서 걸러내고 session binding 만
  정리한다. actor 에게 disconnect 를 알려야 하면 session application 이 대상
  actor 를 골라 `NotifyDisconnectedAsync(...)` 를 호출한다.

bootstrap 은 다음과 같이 읽는다. room 은 외부에서 이미 만들어져 있다고
가정하므로, 두 노드는 각자 역할만 맡는다.

- stream node: client 접속을 받는다.
- spot node: room 인스턴스를 제공한다.

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
        stream.RegisterSession<SampleSession>();
    });

    options.AddSpotMesh("game.room", mesh =>
    {
        mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://registry1:5551"));

        mesh.AddNode("room.node", node =>
        {
            node.AddSpotFactory<SampleSpot>();
        });
    });
});
```

실제 샘플 코드는 다음과 같다. 각 타입의 역할은 다음 두 가지로 정리된다.

- `SampleSpot` 은 actor table 만 소유한다.
- `SampleSession` 은 인증, actor 재연결, actor 로 넘길 session packet 정규화,
  room join 요청을 맡는다.

여기서 `IZLinkSessionContext` 는 stream session 이 다음 작업을 framework
runtime 에 넘길 때 사용하는 session 전용 context 다.

- channel request
- stream reply
- actor create
- actor packet dispatch

session disconnect 를 actor 에 알려야 하면 session code 가 대상 actor 를 고른 뒤
`NotifyDisconnectedAsync(...)` 를 호출한다.

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
        Context.AddHandler<SampleMoveActorHandler>();

        Context.AddSubscribe<SampleStateUpdatedHandler>(
            "sample.state.updated");
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        SampleActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        var join = request.Decode<SampleJoinRoomRequest>();
        var reply = new SampleJoinRoomReply(join.RoomId).Encode();
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(reply));
    }

    public async ValueTask OnInitializeAsync(
        CancellationToken cancellationToken)
    {
        _sessionTimeoutSweep = await Context.AddTimer<SampleSessionTimeoutSweepHandler>(
            "session-timeout-sweep",
            TimeSpan.FromSeconds(1),
            cancellationToken: cancellationToken);
    }

    internal async ValueTask<SampleJoinRoomReply> JoinActorAsync(
        SampleActor actor,
        SampleJoinRoomRequest request,
        CancellationToken cancellationToken = default)
    {
        _actors[actor.ActorId] = actor;

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
        _ = actor;
        _ = cancellationToken;
        PublishSampleState();
    }

    public async ValueTask BroadcastChatAsync(
        SampleActor sender,
        SampleSendRoomChatCommand command,
        CancellationToken cancellationToken)
    {
        // actor가 stream을 직접 들고 broadcast하지 않는다. 같은 room의 모든 actor는
        // 각자 자기 Context.BoundSession 으로 client 에 push 한다.
        SampleRoomChatPushed pushed = new()
        {
            FromActorId = sender.ActorId,
            Text = command.Text
        };

        foreach (SampleActor actor in _actors.Values)
        {
            await actor.PushChatAsync(pushed, cancellationToken);
        }
    }

    public ValueTask PublishSampleStateAsync(
        CancellationToken cancellationToken = default)
    {
        return Context.Outbound.Publish(
            "sample.state.updated",
            new SampleStateUpdatedEvent
            {
                SpotRid = Context.SpotRid.ToString(),
                ActorCount = ActorCount,
                ConnectedSessionCount = ConnectedSessionCount
            })
        .Submit(cancellationToken);
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
                actor.ClearStream();
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
    private readonly IZLinkActorContext _context;
    private IZLinkStream? _stream;

    public SampleActor(
        IZLinkActorContext context,
        string actorId,
        string displayName)
    {
        _context = context;
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

    public async ValueTask SendSnapshotAsync(
        CancellationToken cancellationToken)
    {
        LastSeenAt = DateTimeOffset.UtcNow;

        await _context.BoundSession
            .Send(new SampleActorSnapshot
            {
                ActorId = ActorId,
                DisplayName = DisplayName,
                X = X,
                Y = Y
            })
            .PacketName("SampleActorSnapshot")
            .Submit(cancellationToken);
    }

    public ValueTask PushChatAsync(
        SampleRoomChatPushed pushed,
        CancellationToken cancellationToken)
        => _context.BoundSession
            .Send(pushed)
            .PacketName("SampleRoomChatPushed")
            .Submit(cancellationToken);

    public void ClearStream()
    {
        _stream = null;
        DisconnectedAt = DateTimeOffset.UtcNow;
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
        return _context.GetSpot<SampleSpot>();
    }

    public async ValueTask<SampleJoinRoomReply> JoinRoomAsync(
        SampleSpot room,
        SampleJoinRoomRequest request,
        CancellationToken cancellationToken)
    {
        var result = await _context
            .JoinSpot(room.Context.SpotRid, request.Encode())
            .SubmitAsync(cancellationToken);
        return result.Reply.Decode<SampleJoinRoomReply>();
    }

}

public sealed class SampleMoveActorHandler
    : IZLinkSpotActorRequestHandler<SampleSpot, SampleActor, SampleMoveActorCommand, SampleMoveActorReply>
{
    public ValueTask<SampleMoveActorReply> HandleAsync(
        SampleSpot room,
        SampleActor actor,
        ZLinkSpotActorRequestContext context,
        SampleMoveActorCommand message,
        CancellationToken cancellationToken)
    {
        _ = context;
        actor.MarkSeen(DateTimeOffset.UtcNow);
        room.PublishSampleState();
        return ValueTask.FromResult(new SampleMoveActorReply
        {
            X = message.X,
            Y = message.Y
        });
    }
}

public sealed class SampleSession
    : IZLinkSession
{
    private readonly ISampleRoomDirectory _rooms;
    private readonly ISampleAuthVerifier _authVerifier;
    private readonly ISampleActorFactoryRegistry _actors;

    public SampleSession(
        IZLinkSessionContext context,
        ISampleRoomDirectory rooms,
        ISampleAuthVerifier authVerifier,
        ISampleActorFactoryRegistry actors)
    {
        _rooms = rooms;
        _authVerifier = authVerifier;
        _actors = actors;
    }

    public IZLinkSessionContext Context { get; } = context;

    public SampleActor? Actor { get; private set; }
    public string? ActorId { get; private set; }

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
        ActorId = null;

        // session binding cleanup is handled by the framework disconnect path.
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
        Message payload,
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
                payload.Decode<SampleAuthenticateRequest>();

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
            var actorRef = await Context.Actors.BindAsync(
                auth.AccountId,
                cancellationToken);
            ActorId = actorRef.ActorId;

            await Context
                .Reply(new SampleAuthenticateReply
                {
                    AccountId = auth.AccountId,
                    CurrentRoomId = (actor.Spot as SampleSpot)?.RoomId ?? string.Empty
                })
                .Submit();
            return;
        }

        if (string.Equals(
            header.Name,
            "SampleJoinRoomRequest",
            StringComparison.Ordinal))
        {
            SampleJoinRoomRequest join =
                payload.Decode<SampleJoinRoomRequest>();

            SampleSpot room =
                await _rooms.GetRequiredAsync(
                    join.RoomId,
                    cancellationToken);

            SampleJoinRoomReply joinReply = await Actor.JoinRoomAsync(
                room,
                join,
                cancellationToken);

            await Context
                .Reply(joinReply)
                .Submit();
            return;
        }

        if (Actor.Spot is null)
        {
            throw new InvalidOperationException(
                "JoinRoom is required before gameplay packets.");
        }

        var actorRef = ActorId is not null
            ? Context.Actors.Find(ActorId)
            : throw new InvalidOperationException("Actor is not bound.");
        actorRef ??= throw new InvalidOperationException("Actor is not bound.");
        await actorRef.RelayAsync(header, payload, cancellationToken);
    }
}

public sealed class SampleSpot(IZLinkSpotContext context) : IZLinkSpot<SampleActor>
{
    public IZLinkSpotContext Context { get; } = context;

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        SampleActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        _ = actor;
        _ = cancellationToken;
        var join = request.Decode<SampleJoinRoomRequest>();
        return ValueTask.FromResult(
            ZLinkSpotActorJoinResult.Accept(new SampleJoinRoomReply(join.RoomId).Encode()));
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

`OnDispatchAsync(...)` 로 받은 `payload` 는 framework runtime 이 callback 동안
빌려준 값이다. session 은 payload 를 직접 해제하거나 `Move()` 로 소비하지 않고,
읽거나 `IZLinkSessionActor.RelayAsync(...)` 같은 framework API 에 그대로 넘긴다.

여기서 `ISampleActorFactoryRegistry` 는 `actorType -> factory` 매핑을 잡아
두는 샘플용 registry 로 보면 된다. 인증 결과에 따라 `SampleWarriorActor`,
`SampleTraderActor` 처럼 서로 다른 actor 타입을 고를 수 있게 해 준다.

이 타입 선택은 인증 또는 재연결 경계에서 한 번만 일어나야 한다. 즉 gameplay
packet 의 hot path[^hot-path] 까지 다시 끌고 들어오지 않는 편이 맞다.

이 샘플에서 각 객체의 관계를 짧게 정리하면 다음과 같다.

- `SampleSession`
  - stream에서 packet을 받아 인증을 수행하고, actor factory를 고른 뒤 room
    join 전후를 이어 준다.
- `SampleActor`
  - `accountId`를 키로 살아 있는 논리 객체다.
  - 현재 붙어 있는 `Stream`과 현재 속한 `Spot`을 직접 보유한다.
- `SampleSpot`
  - room이다.
  - actor table만 소유하며, stream 인증 로직은 알지 못한다.

패킷 흐름은 다음과 같이 읽는다.

1. 첫 패킷은 `SampleAuthenticateRequest`다.
2. `SampleSession`이 API 서버에 토큰 검증을 요청한다.
3. 인증 결과에 담긴 `actorType`과 `accountId`로 `SampleActor`를 찾거나 새로
   만든다.
4. 이미 다른 stream이 붙어 있던 경우, framework가 stale 여부를 확인한 뒤 현재
   stream을 actor에 다시 attach한다.
5. client가 `SampleJoinRoomRequest`를 보낸다.
6. `SampleSession`은 room 정보를 actor packet으로 넘기고, actor 쪽 handler는
   `IZLinkActorContext.JoinSpot(...)`으로 target room의 join callback을 호출한다.
7. 등록된 `SampleJoinRoomHandler`가 같은 `SampleSpot` 실행 문맥에서 승인 처리와
   결과 생성을 마치면, framework 가 join commit 을 수행해 actor 위치를 target
   room으로 갱신한다. 기존 room이 있으면 source room의 `ActorLeft` lifecycle 이
   호출된다.
8. 그 뒤에 들어오는 `SampleHeartbeatCommand`, `SampleMoveActorCommand`,
   `SampleSendRoomChatCommand` 같은 패킷은 framework 내부의 `SubmitAsync(...)`를
   거쳐 actor가 속한 `Spot` 문맥으로 제출된다.
9. 같은 `Spot` 실행 문맥 안에서 실제 처리는
   `IZLinkSpotContext.AddHandler(...)`로 등록한 actor packet handler가
   담당한다.
10. room 전체 로직이 필요한 경우, handler는 인자로 받은 `SampleSpot`과 actor의
    method를 함께 사용해 처리를 이어 간다.

요약하면 다음과 같다.

- room 은 actor 만 붙든다.
- session handler 는 room 바깥에서 인증과 재연결을 처리한다.

이 구조라면 "같은 account 의 actor 는 그대로 두고 stream 만 교체한다"는
reconnect 정책을 설명하기가 훨씬 쉬워진다.

이 샘플 코드는 framework session header와 payload를 기준으로 작성했다.
framework 는 decode 된 header와 payload를 내부 dispatch 경로로 같은 `Spot`
문맥에 올려 두고, 최종 actor 로직은 등록된 actor packet handler 가 처리한다.

또한 room timer 는 room 에 join 된 actor 만 본다. 인증은 끝났지만 아직
`SampleJoinRoomRequest` 를 보내지 않은 연결은 `SampleSpot` 바깥의 stream
수명으로 보고, room 안의 heartbeat timeout 과는 구분한다.

### 3.3 handler 클래스

이 절은 spot 객체와 분리되어 동작하는 packet / subscription / timer handler
클래스의 모양을 보여 준다.

```csharp
 public sealed class SampleGetStateHandler
    : IZLinkSpotRequestHandler<SampleSpot, SampleGetStateRequest, SampleGetStateReply>
{
    private readonly IZLinkSpotOutbound _spotClient;

    public SampleGetStateHandler(IZLinkSpotOutbound spotClient)
    {
        _spotClient = spotClient;
    }

    public async ValueTask<SampleGetStateReply> HandleAsync(
        SampleSpot spot,
        SampleGetStateRequest request,
        CancellationToken cancellationToken)
    {
        await _spotClient
            .SendToChannel(
                "orders",
                new SampleReportStateQueryCommand
                {
                    SpotRid = spot.Context.SpotRid.ToString()
                })
            .Submit(cancellationToken);

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
    private readonly IZLinkSpotOutbound _spotClient;

    public SampleReportStateHandler(IZLinkSpotOutbound spotClient)
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
            .SendToChannel(
                "orders",
                new SampleReportStateQueryCommand
                {
                    SpotRid = spot.Context.SpotRid.ToString()
                })
            .Submit(cancellationToken);
    }
}

public sealed class SampleStateUpdatedHandler
    : IZLinkSpotSubscriptionHandler<SampleSpot, SampleStateUpdatedEvent>
{
    private readonly IZLinkSpotOutbound _spotClient;

    public SampleStateUpdatedHandler(IZLinkSpotOutbound spotClient)
    {
        _spotClient = spotClient;
    }

    public async ValueTask HandleAsync(
        SampleSpot spot,
        SampleStateUpdatedEvent message,
        CancellationToken cancellationToken)
    {
        await _spotClient
            .RequestToChannel(
                "orders",
                new SampleSyncStateRequest
                {
                    SpotRid = spot.Context.SpotRid.ToString(),
                    ActorCount = message.ActorCount,
                    ConnectedSessionCount = message.ConnectedSessionCount
                })
            .Timeout(TimeSpan.FromMilliseconds(200))
            .SubmitAsync<SampleSyncStateReply>(cancellationToken);
    }
}

public sealed class SampleSessionTimeoutSweepHandler
    : IZLinkSpotTimerHandler<SampleSpot>
{
    public ValueTask HandleAsync(
        SampleSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        _ = tick;
        return spot.SweepInactiveActorsAsync(
            TimeSpan.FromSeconds(10),
            TimeSpan.FromMinutes(1),
            cancellationToken);
    }
}
```

이 high-level timer handler 는 framework runtime 이 만들어 둔 managed `.NET`
timer 위에 얹는 wrapper 로 읽어야 한다. 즉 다음 모델이 자연스럽다.

- runtime 은 policy-aware managed timer 로부터 tick 을 받는다.
- user Spot timer 는 그 tick 을 같은 spot execution context[^execution-context]
  안으로 넘긴다. Entry Spot timer 는 Entry Spot 전체 queue 에 묶지 않는다.
- 그 안에서 `IZLinkSpotTimerHandler<TSpot>.HandleAsync(...)` 를 호출한다.
- handler 는 `ZLinkTimerTick` 을 받아 callback 번호, 예정 시각, 실제 시작 시각,
  지연, 건너뛴 tick 수를 확인할 수 있다.

이 샘플에서 timer 가 하는 일은 상태 publish 가 아니라 sweep 이다. 즉
`LastSeenAt` 을 기준으로 오랫동안 조용했던 actor 를 찾아 정리하는 역할이다.

- 현재 stream 이 붙어 있는 actor 가 idle timeout 을 넘기면 stream bind 를 푼다.
- 이미 stream 이 끊긴 actor 는 reconnect grace 시간이 지나면 room 에서
  제거한다.

이때 framework 가 제공하는 `IZLinkTimer.CancelAsync()` 는 단순한 cancel 이
아니다. managed timer loop 정리까지 함께 담는 고수준 handle 에 가깝다.

room server tick 처럼 짧은 주기를 쓰는 경우에는 `ZLinkTimerOptions` 로 overrun
정책을 명시한다.

```csharp
_gameLoop = await Context.AddTimer<GameLoopTimerHandler>(
    "game-loop",
    TimeSpan.FromMilliseconds(1000.0 / 60.0),
    new ZLinkTimerOptions
    {
        OverrunPolicy = ZLinkTimerOverrunPolicy.SkipLateTicks
    },
    cancellationToken);
```

`SkipLateTicks` 는 늦은 tick 을 모두 callback 으로 밀어 넣지 않고 `SkippedTicks`
로 드러낸다. application 은 `SkippedTicks + 1` 을 보고 현재 callback 이 대표하는
server tick 수를 계산할 수 있다.

### 3.4 protobuf packet 타입

이 절은 샘플에 등장하는 packet 타입의 출처를 짚어 둔다.

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

이 절은 위 샘플 코드를 읽을 때 어디에 시선을 두면 좋을지를 정리한다.

이 샘플에서 중요한 포인트는 다음과 같다.

- `room.node` 는 논리적 `SpotNode` 이름이다. 그리고
  `AddSpotMesh("game.room", mesh => mesh.UseDiscovery(...AddRegistryEndpoint...))` 가 `game.room`
  channel mesh 의 범위를 정한다.
- `SampleSpot` 은 단순한 handler 클래스가 아니라 실제 spot 객체다.
- `SampleSpot` 은 `IZLinkSpot` 을 상속받는다. 그리고 자신의
  `Context.SpotRid`, `Context.NodeRid` 를 상태로 가진다.
- `SampleSpot` 안에는 상태와 코어 로직만 두고, packet 처리는 별도 handler
  클래스로 분리해도 된다.
- handler 는 `IZLinkSpotOutbound` 를 constructor injection 으로 주입받을 수
  있다.
- `SampleSpot` 과 그 spot 에 귀속된 handler, timer handler, join handler 는
  모두 같은 per-spot DI[^di] scope 에서 resolve 된다고 보는 편이 맞다.
- 그래서 public registration 함수에 매번 `IServiceProvider services` 를 넘기지
  않고, `Context.AddPacket<THandler>()` 처럼 타입만 등록하는 형태가 더
  자연스럽다.
- local spot 인스턴스가 없는 외부 노드는 `IZLinkSpotPublisherClient` 를 별도로
  주입받아 SPOT channel publish 를 할 수 있다.
- `Context.AddPacket<THandler>(...)` 는 request packet 과 send packet 을 함께
  등록한다.
- `Context.AddSubscribe<THandler>(...)` 는 topic subscription consumer 를
  등록한다.
- `Context.AddTimer<THandler>(...)` 는 spot lifecycle[^lifecycle] 안에서 timer
  를 등록한다.
- 다른 channel 로의 호출은 attach 된 channel client 를 통해 전송한다.
- **`RequestToChannel(...).Submit(...)` 는 같은 spot execution context 안에서
  완료된다.** 임의의 thread 에서 promise 를 직접 resolve 하지 않는다. 따라서
  continuation 도 spot state 에 별도의 lock 없이 접근할 수 있다.

이 샘플에서 핵심은 두 가지로 요약된다.

1. `SampleSpot` 은 이미 생성된 spot 객체다. 따라서 spot rid 를 요청 scope 에서
   따로 꺼내는 값으로 다루지 않고, `SampleSpot.Context.SpotRid` 속성에서 바로
   읽는다.
2. subscribe handler, packet handler, timer handler, channel reply callback 은
   물론 room 에 join 된 actor 의 client packet 과 disconnect 처리까지 같은
   spot execution context 에서 직렬로 실행된다. 그래서 `SampleSpot` 의
   mutable state(`ActorCount` 등)에 별도의 lock 이 필요하지 않다.

## 5. packet 등록과 subscribe 등록은 어떻게 다른가

이 절은 등록 시점에 어느 표면이 어떤 의도로 호출되는지 정리한다.

이 문서에서는 `SampleSpot` 이 자기 초기화 단계에서 다음 세 가지를 직접 등록한다.

- `Context.AddPacket<THandler>(...)`
- `Context.AddSubscribe<THandler>(topic)`
- `Context.AddTimer<THandler>(name, period, ...)`

반면 stream session 은 `SampleSpot` 초기화 단계에서 정적으로 등록하지 않는다.
session 의 흐름은 다음과 같다.

- `SampleSession` 은 `stream node` 가 생성한다.
- 인증과 actor lookup 을 먼저 끝낸 뒤, `SampleJoinRoomRequest` 가 들어오면
  actor packet 으로 join 의도를 전달한다.
- 실제 target `SampleSpot` 의 join callback 호출은 actor 쪽에서
  `IZLinkActorContext.JoinSpot(...)` 을 통해 수행한다. 이것이 이 문서의 기본
  모델이다.

packet 은 request 와 send 를 함께 묶는 상위 개념이다. 즉 다음 두 종류 모두
packet 으로 본다.

- `SampleGetStateHandler` 처럼 reply 를 돌려주는 handler
- `SampleReportStateHandler` 처럼 응답 없이 상태만 반영하는 handler

반면 subscribe 는 packet 이 아니다. 의미가 다르다.

- subscribe 는 topic fan-out consumer 등록이다.
- packet 은 targeted delivery 다.

handler 가 다른 서버나 다른 spot 으로 outbound 호출을 보내야 한다면,
`SampleSpot` 에서 client 를 꺼내 쓰기보다 handler 가 `IZLinkSpotOutbound` 를
직접 주입받는 편이 더 자연스럽다. 이렇게 두면 책임이 다음과 같이 나뉜다.

- `SampleSpot` 은 상태와 코어 로직에 집중한다.
- outbound 호출 책임은 handler 가 가져간다.

이 샘플 기준으로 `SampleSpot` 안에 남는 요소는 대략 다음 정도다.

- `Context.SpotRid`, `Context.NodeRid` 같은 자기 identity
- `ActorCount`, `ConnectedSessionCount` 같은 현재 상태
- `SweepInactiveActorsAsync(...)` 같은 liveness 관리 메서드
- `ApplyReportedState(...)` 같은 순수 도메인 메서드
- 초기 registration을 위한 `Configure()`

## 6. packet 매핑은 무엇을 기준으로 하는가

이 절은 `AddPacket(...)` 이 별도의 문자열 없이도 packet 타입을 어떻게
식별하는지를 설명한다.

이 문서에서는 `Context.AddPacket<THandler>(...)` 가 별도의 문자열을 받지
않는다. packet 매핑의 기준은 header 의 `msgId` 다.

이 문서의 전체 샘플 코드는 `options.Codecs.AddProtobuf()` 를 쓰고 있다.
그러므로 기본 예시는 `protobuf` 기준이다. 즉 위 코드에서 packet 타입은 모두
`protoc` 가 생성한 `IMessage<T>` 계열 타입이라고 보면 된다. 이 문서는 생성된
protobuf 타입에 framework 용 marker interface[^marker-interface] 를 직접
붙이는 방식을 전제로 하지 않는다.

위 코드의 등록은 다음과 같이 해석된다.

- `Context.AddPacket<SampleGetStateHandler>()`
  - `SampleGetStateRequest` 의 protobuf `msgName` 으로 등록된다.
- `Context.AddPacket<SampleReportStateHandler>()`
  - `SampleReportStateCommand` 의 protobuf `msgName` 으로 등록된다.

`msgId` 가 어디서 오는지는 codec 에 따라 달라진다.

- `protobuf` 를 사용하는 경우
  - `msgId = msgName`
- `json` 을 사용하는 경우
  - `msgId = class name`

정리하면, framework 는 `SampleGetStateHandler` 같은 handler 타입에서 packet
타입을 읽어 온다. 그 다음 그 packet 타입의 `msgId` 를 dispatch key 로
등록하는 방식을 기본으로 본다.

예를 들면 다음과 같이 매핑된다.

- `SampleGetStateRequest` 의 `msgId`
  - `protobuf` 면 protobuf packet name
  - `json` 이면 `SampleGetStateRequest`
- `SampleReportStateCommand` 의 `msgId`
  - `protobuf` 면 protobuf packet name
  - `json` 이면 `SampleReportStateCommand`

결국 `Context.AddPacket<SampleGetStateHandler>()` 는
`SampleGetStateRequest` 의 `msgId` 를 기준으로 등록되는 셈이다.

## 7. 어떤 상황에 어떤 API를 쓰는가

이 절은 상황별로 어느 표면을 골라야 하는지 간단히 정리해 둔다.

- 새 spot 인스턴스를 만들고 싶다
  - `IZLinkSpotManager.CreateAsync<StageSpot>(...)`
- 새 spot 인스턴스를 만들면서 초기 설정을 넘기고 싶다
  - `IZLinkSpotManager.CreateAsync<StageSpot>(request, ...)`를 사용하고,
    spot의 `OnCreateAsync(...)`에서 create request `Message`를 해석한다.
- 명시적 `spotRid`가 있고 없으면 만들고 있으면 가져오고 싶다
  - `IZLinkSpotManager.GetOrCreateAsync<StageSpot>(spotRid, request, ...)`
- attach된 다른 channel로 send packet을 보내고 싶다
  - `SendToChannel(...).Submit(...)`
- attach된 다른 channel로 request packet을 보내고 싶다
  - `RequestToChannel(...).Timeout(...).Submit(...)`
  - 다른 SPOT 인스턴스로 routed 호출을 보내고 싶다
  - SPOT callback 안에서는 `Context.Outbound.SendToSpot(...)` /
    `Context.Outbound.RequestToSpot(...)`처럼 `RoutingId`를 받는 표면을 사용한다.
  - SPOT callback 밖에서는 actor 생성 또는 Entry Spot join 으로 `ActorRef` 를 얻은 뒤
    session actor handle 로 bind 한다.
- 현재 spot 자신의 id를 알고 싶다
  - `SampleSpot.Context.SpotRid`
- 특정 `spotRid`가 어떤 이름으로 생성됐는지 다시 확인하고 싶다
  - `IZLinkSpotManager.GetAsync(spotRid)` 또는 `ListAsync()`
- stage 안에서 fan-out 하고 싶다
  - `Publish(topic, ...).Submit(...)`
- local spot 인스턴스가 없는 외부 노드에서 특정 SPOT channel로 publish하고 싶다
  - `IZLinkSpotPublisherClient.PublishSpot(channelName, topic, ...).Submit(...)`
- stage 안에서 heartbeat timeout sweep 같은 주기 작업을 돌리고 싶다
  - `SampleSpot.OnInitializeAsync()`에서 `Context.AddTimer<THandler>(...)`로 등록

## 8. room처럼 써도 되는가

이 절은 `SPOT` 을 hot path 가 있는 용도로 쓸 때 어떤 기준이 추가되는지를
다룬다.

`SPOT` 을 room, stage, zone 처럼 hot path 가 있는 용도로 쓰더라도, 이 샘플
구조 자체가 곧바로 과도한 오버헤드를 의미하지는 않는다. 다만 `SPOT` 의 packet
handler 경로는 room hot path 로 동작할 수 있다. 따라서 일반 channel messaging
보다 더 강한 최적화 기준이 필요하다.

이 문서는 이 선택을 framework 가 일방적으로 숨기지 않고, 사용자가 dispatch
mode[^dispatch-mode] 로 직접 고를 수 있어야 한다고 본다.

- `ZLinkDispatchMode.Compiled`
  - registration 또는 warm-up 단계까지만 reflection을 허용한다.
  - hot path에서는 cached delegate, prebuilt dispatch table, 미리 고른 actor
    factory만 사용한다.
- `ZLinkDispatchMode.Dynamic`
  - late binding[^late-binding]과 실험 편의성을 우선한다.
  - 관리용 handler나 초기 prototype에는 허용할 만한 모드다.

- `AddPacket`, `AddSubscribe`, `AddTimer`는 registration 단계에서만 동작해야
  한다.
- reflection은 registration 단계에서 끝나야 한다.
- 런타임 packet 처리 경로에서는 캐시된 dispatch table을 사용해야 한다.
- handler 호출은 캐시된 delegate 또는 그에 준하는 저비용 경로여야 한다.
- per-packet allocation과 불필요한 DI 재구성은 최소화해야 한다.

요약하면, 샘플의 class 구조보다 더 중요한 것은 실제 실행 경로가 얼마나
짧은지다. hot path 는 대략 다음 단계 정도로 끝나야 한다.

1. `msgId` 읽기
2. dispatch table lookup
3. packet decode
4. handler resolve 또는 재사용
5. `HandleAsync(...)` 호출

이 문서의 샘플은 어디까지나 "등록 구조"를 보여 주는 예시다. 실제 성능은
runtime 이 reflection 을 어디까지 허용하느냐보다, hot path 에서 그 비용을
얼마나 제거했느냐에 훨씬 크게 좌우된다.

물론 그렇다고 일반 socket / service handler 가 성능이 낮아도 된다는 뜻은
아니다. 다만 두 경로의 무게 중심이 다르다.

- `SPOT` packet 처리는 room hot path 다. 더 강한 최적화를 먼저 요구한다.
- 일반 channel messaging 쪽은 편의 기능을 조금 더 허용할 여지가 있다.

## 9. 정리

이 절은 이 문서가 다룬 결론을 다시 짧게 모아 둔다.

- `SPOT`의 기본 모델은 `SampleSpot : IZLinkSpot` 같은 인터페이스 기반
  lifecycle로 고정한다.
- 현재 framework core의 registration 표면은 `AddPacket`, `AddSubscribe`,
  `AddTimer` 같은 명시적 등록을 사용한다.
- actor join / actor factory / stream-to-actor bridge 예시는 3.2.1에서 현재
  draft 공용 계약이 room wrapper에 어떻게 매핑되는지를 보여 주는 상세 샘플이다.
- packet dispatch의 기준은 header의 `msgId`다.
- `IZLinkSpotManager`는 spot 생성과 조회 기능을 함께 제공한다.
- attach된 channel client와 SPOT publish 설정은 capability별 builder에서
  노출한다.

## 10. 회귀 테스트

이 절은 이 문서의 샘플 코드를 어느 통합 테스트와 함께 유지해야 하는지
정리한다.

SPOT 샘플은 room / stage / zone 같은 상위 모델이 framework public 표면만으로
구성되는지를 확인한다. 샘플 코드를 수정할 때는 다음 흐름을 아래 테스트와 함께
맞춰 둔다.

- spot 생성
- publish
- actor join
- channel request

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ManagerTests.SpotManager_Create_List_Close_And_Publish_Work_Through_FrameworkRuntime` | spot 생성과 조회, 종료, callback scope 정리가 동작한다. |
| `ManagerTests.SpotManager_CreateAsync_Passes_Empty_CreatePayload_To_OnCreate` | payload 없는 생성이 빈 `Message`로 `OnCreateAsync(...)`를 호출한다. |
| `ManagerTests.SpotManager_GetOrCreateAsync_Initializes_Once_With_First_CreatePayload` | 같은 `spotRid` 동시 확보에서 첫 create request `Message`만 `OnCreateAsync(...)`로 전달된다. |
| `ManagerTests.SpotManager_GetOrCreateAsync_Returns_Existing_Spot_For_Same_Type` | 같은 `spotRid`를 같은 Spot 타입으로 다시 확보하면 기존 spot을 반환한다. |
| `PublisherTests.OutboundOnly_SpotPublisherClient_Publishes_To_TargetChannel` | 외부 노드 publish 샘플이 target SPOT channel에 도달한다. |
| `ActorLifecycleTests.SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext` | actor가 room 역할의 spot에 join한 뒤, 해당 문맥에서 dispatch된다. |

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^spot]: `SPOT` 은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 추상이다.
[^spotnode]: `SpotNode` 는 하나 이상의 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^handler]: handler 는 특정 메시지(packet, subscribe event, timer tick 등)가 들어왔을 때 실제 로직을 실행하는 callback 클래스 또는 메서드를 뜻한다.
[^channel]: channel 은 이름을 키로 삼아 메시지를 주고받는 논리적 통신 경로다. request / send 양방향과 publish / subscribe 양방향이 모두 channel 단위로 묶인다.
[^actor]: actor 는 계정이나 식별자 단위로 살아 있는 논리 객체로, 상태와 행동을 함께 들고 있는 도메인 단위다.
[^session]: session 은 한 client 연결을 framework 안에서 다루기 위한 논리 단위이며, 인증과 packet dispatch의 첫 진입점이 된다.
[^direct-routed]: direct routed 호출은 routing id 와 spot rid 를 명시적으로 묶어 특정 인스턴스로 바로 보내는 방식이다.
[^resolver]: resolver 는 이름이나 id 같은 논리 식별자를 받아 실제 transport 위치(주소, routing id 등)로 변환해 주는 컴포넌트다.
[^capability]: capability 는 어떤 노드(channel, spot 등)가 외부에 노출하는 역할이나 기능 단위(예: server, subscriber, publisher)를 가리킨다.
[^typed-facade]: typed facade 는 native 옵션 키를 직접 노출하지 않고, 타입과 속성으로 감싸 IDE 자동완성과 컴파일 검증을 받게 하는 wrapper 인터페이스다.
[^factory]: factory 는 이름이나 타입 정보를 받아 새 인스턴스를 만들어 주는 등록형 생성기다. SPOT 의 경우 spot 인스턴스 생성을 담당한다.
[^discovery]: `Discovery` 는 Registry 같은 외부 서비스를 통해 peer 주소와 라우팅 정보를 자동으로 받아 오는 연결 방식이다.
[^ingress]: ingress 는 외부에서 들어오는 트래픽을 받는 진입 지점(주소나 endpoint)을 뜻한다.
[^rid]: RID(RoutingId) 는 라우팅 계층이 peer 노드를 식별하기 위해 사용하는 바이트 식별자다.
[^backpressure]: backpressure 는 송신 측이 수신 측의 처리 속도를 넘어 메시지를 밀어 넣지 못하도록 흐름을 조절하는 메커니즘이다.
[^stream]: stream 은 client 와 framework 사이를 이어 주는 양방향 연결이다. 인증, packet 송수신, disconnect 처리가 stream 위에서 일어난다.
[^stable-identity]: stable identity 는 연결이 끊기거나 다시 붙어도 변하지 않는 고정 식별자다. 보통 계정 id 처럼 시스템 외부에서 부여한다.
[^stale]: stale 은 더 이상 유효하지 않은 옛 상태를 가리킨다. stale disconnect 는 이미 새 연결로 교체된 옛 stream 의 disconnect 신호를 뜻한다.
[^hot-path]: hot path 는 실행 빈도가 매우 높아 성능 최적화의 우선순위가 높은 코드 경로를 가리킨다.
[^execution-context]: execution context 는 어떤 코드가 어떤 직렬화 보장 아래에서 실행되는지를 묶어 두는 실행 단위를 뜻한다. 같은 context 안에서는 lock 없이 상태에 접근할 수 있다.
[^di]: DI(Dependency Injection) 는 객체가 필요로 하는 의존을 컨테이너가 대신 주입해 주는 방식이다. `.NET` 에서는 `IServiceProvider` 가 표준 진입점이다.
[^lifecycle]: lifecycle 은 객체나 컴포넌트가 만들어져 동작하고 정리되는 단계별 흐름을 가리킨다(생성, 초기화, 실행, 종료 등).
[^marker-interface]: marker interface 는 멤버 없이 타입을 식별하는 용도로만 사용하는 인터페이스다. 코드 생성된 타입에 직접 붙이기 어려운 경우가 많다.
[^dispatch-mode]: dispatch mode 는 들어온 메시지를 handler 로 어떻게 연결할지를 정하는 전략 선택값이다. compiled 와 dynamic 처럼 reflection 허용 범위로 구분한다.
[^late-binding]: late binding 은 호출 대상이나 타입을 컴파일 시점이 아니라 실행 시점에 결정하는 방식이다. 유연하지만 hot path 비용이 높다.
