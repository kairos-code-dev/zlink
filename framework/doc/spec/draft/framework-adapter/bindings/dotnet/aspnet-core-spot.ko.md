[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [STREAM](./aspnet-core-stream.ko.md)

# Draft -- ZLink Framework ASP.NET Core SPOT Integration

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `ASP.NET Core`에서 `SPOT`을 framework lifecycle 안에
> 넣는 방향을 정리한다.

## 1. 목표

`SPOT`은 zlink 쪽에서 이미 독립된 개념과 runtime을 갖는다.
`ZLink Framework`의 목적은 그 개념을 없애는 것이 아니라, `ASP.NET Core`에서
아래처럼 자연스럽게 다루게 만드는 것이다.

- `SpotNode` lifecycle 관리
- `Spot` publish/subscribe facade 주입
- room, stage, zone 같은 논리 인스턴스 모델 설명
- current channel publish/subscribe
- attach된 다른 channel client를 통한 send/request
- discovery 기반 peer 구성
- background subscriber handler

## 2. 기반이 되는 .NET binding

현재 하부 토대는 아래 binding 표면이다.

- `Discovery`
- `SpotNode`
- `Spot`
- `Spot` publish/subscribe
- channel client attach 기반 channel send/request

즉 이 문서의 핵심은 `SPOT` 기능을 새로 만드는 일이 아니라,
기존 binding 기능을 `ASP.NET Core` 안에 녹이는 방법이다.

등록부터 handler, channel send/request, topic publish까지 이어서 보는 샘플은
[spot-samples.ko.md](./spot-samples.ko.md)를 참고한다.
`playhouse`의 `Stage` 같은 상위 모델을 `SPOT` 위에 다시 감쌀 때 필요한 추가 조건은
[stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md)를 참고한다.

## 3. SPOT을 무엇으로 보는가

현재 초안에서 `SPOT`은 단순 pub/sub helper가 아니다.
오히려 아래처럼 **주소 가능한 논리 인스턴스**로 보는 편이 맞다.

- 게임 room
- playhouse stage
- 채팅 room
- MMORPG zone
- 필요하면 Redis pub/sub 같은 fan-out 주제 공간

즉 `SPOT`은 "토픽 시스템"보다 먼저 "논리 대상 인스턴스"로 설명되어야 한다.
publish/subscribe는 그 안에서 함께 쓰일 수 있는 한 가지 사용 방식이다.

여기서 관계를 더 정확히 잡으면 아래와 같다.

- `Spot`은 특정 service에 속하지 않는다.
- `Spot`은 `SpotNode`에 종속된다.
- `SpotNode`가 channel 이름을 직접 소유하지 않는다.
- `AddSpotMesh(channelName, mesh => mesh.UseDiscovery(...))` 등록이 active channel view를 소유한다.
- 같은 `SpotNode`에는 active SPOT channel view를 하나만 둔다.
- `SpotNode.router`와 pub/sub mesh는 같은 channel의 다른 `SpotNode`와만 연결된다.
- 다른 channel 호출은 `SpotNode.router`가 아니라 attach된 channel client 경로로 푼다.
- 따라서 `spotRid`는 service에서 생기는 값이 아니라, `SpotNode`가 개별 spot
  인스턴스를 만들 때 생기는 식별자다.

이 관점에서 중요한 점은 아래와 같다.

- 현재 SPOT channel 안에서는 topic publish/subscribe를 쓴다.
- 다른 channel 호출은 attach된 channel client를 통해 보낸다.
- `SpotNode.router`는 peer topology와 내부 routed delivery를 위해 남기되, 현재
  framework core의 public high-level API에서는 `targetRid + spotRid`를 직접
  받는 direct routed 호출 표면을 두지 않는다.
- 외부 `PUB -> Spot` 입력은 generic pub/sub attach가 아니라 별도 ingress 표면으로
  분리한다.

여기서 경계를 분명히 하면, `SPOT`이 제공하는 것은 주소 가능한 논리 인스턴스와
그 인스턴스에 대한 메시징, publish/subscribe, timer, lifecycle까지다.
반면 room broadcast 정책과 도메인별 권한 모델은 여전히 응용 계층 책임으로 둔다.
현재 draft 구현에서는 actor join, actor factory 등록, stream callback에서
`IZLinkSessionContext`으로 actor packet/disconnect를 같은 `SPOT` 실행 문맥에 올리는
브리지는 framework core 범위에 포함한다.

## 4. ASP.NET Core 등록 모델 초안

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddSpotMesh("game.stage", mesh =>
    {
        mesh.UseDiscovery(discovery =>
        {
            discovery.Add("tcp://registry1:5551");
        });

        mesh.AddNode("stage-node", node =>
        {
            node.Bind("tcp://0.0.0.0:9000");
            node.EnableRouter();
            node.EnablePubSub();
            node.AttachClientServerChannelClient("orders");
            node.AttachSpotMeshPublisherClient("game.stage");
            node.AddSpotFactory<StageSpot>("stage");
        });
    });
});
```

이 등록은 아래를 의미한다.

- 논리 `SpotNode` 이름 = `stage-node`
- backing `SpotNode` 생성
- `AddSpotMesh("game.stage", mesh => mesh.UseDiscovery(...))`가 active channel view를 공급
- 같은 channel의 다른 `SpotNode`와만 mesh 구성
- local routed router capability 활성화
- local SPOT pub/sub capability 활성화
- 다른 channel 호출용 client attach
- 필요하면 외부 노드용 spot publish client attach
- host shutdown 시 lifecycle 정리

`AddSpotMesh(...)`는 같은 channel에 속하는 여러 `SpotNode`를 한 묶음으로 등록한다.
mesh 안에서는 `mesh.AddNode(name, configure)`로 노드를 추가하고, mesh 단위 discovery
설정은 `mesh.UseDiscovery(...)`가 정한다. 같은 채널을 가리키는 `SpotNode` 묶음을
한 mesh에 모아 두기 때문에, 한 앱에서 서로 다른 channel mesh를 따로 등록할 수도 있고
한 mesh 안에서 같은 channel을 공유하는 여러 node를 같이 둘 수도 있다.

mesh로 묶지 않고 discovery 없이 단일 노드만 띄우는 경우에는 `options.AddSpotNode(...)`
표면을 직접 쓸 수 있다. 다만 이 standalone 등록은 mesh discovery, `EnableRouter`,
channel attach 같은 mesh 기능과 함께 쓸 수 없다. RegistrationValidator는 노드가
mesh 기능을 켜는데 standalone 등록을 쓰면 시작 시점에 `AddSpotMesh` 등록을 강제하는
오류로 막는다.

여기서 registration 함수들은 역할이 다르다.

- `EnableRouter()`
  - local `SpotNode.router` 경로를 켠다. 같은 channel의 다른 `SpotNode`와 routed
    packet을 주고받는 축이다.
- `EnablePubSub()`
  - 현재 SPOT channel 안의 publish/subscribe 축을 켠다. local spot 안에서
    `IZLinkSpotClient.Publish(...)`를 쓸 수 있으려면 이 capability가 필요하다.
- `AttachClientServerChannelClient("orders")`
  - `orders` channel로 outbound send/request를 보낼 `DEALER(client)` 경로를 붙인다.
  - 기존 `AttachChannelClient("orders")`는 standalone `SpotNode` 빌더에서 같은
    경로를 가리키는 legacy alias로 남아 있다. mesh 노드 빌더에는 `AttachClientServerChannelClient`만
    있다.
- `AttachSpotMeshPublisherClient("game.stage")`
  - local spot 인스턴스가 없는 외부 노드가 `game.stage` SPOT channel로 publish할
    별도 publisher client를 붙인다.
  - 기존 `AttachSpotPublisherClient("game.stage")`는 standalone `SpotNode` 빌더에서
    같은 경로를 가리키는 legacy alias다. mesh 노드 빌더에는
    `AttachSpotMeshPublisherClient`만 있다.
- `AddSpotFactory<StageSpot>("stage")`
  - 이 node가 생성하고 소유할 `StageSpot` factory를 `stage` 이름으로 등록한다.
  - 같은 `SpotNode`에 여러 spot factory를 둘 수 있다면, 생성 시에는 이 이름으로
    어떤 factory를 쓸지 고른다.
  - 이미 등록된 이름을 다시 쓰면 조용히 덮어쓰지 않고 예외를 던진다.

즉 `SpotNode`는 더 이상 여러 service surface를 동시에 소유하는 hub처럼 설명하지
않는다. 현재 방향에서는 `AddSpotMesh(channelName, mesh => mesh.UseDiscovery(...))`
등록이 node의 channel 정체성을 닫고, 다른 channel 호출은 별도 attach된 client
경로로 푼다.

여기서 중요한 점은 아래와 같다.

- mesh 묶음 없이 standalone `SpotNode` 생성 호출만으로는 channel 범위가 닫히지 않는다.
- `AddSpotMesh("game.stage", mesh => { ... })`가 이 node의 mesh 범위를 정한다.
- 같은 `SpotNode`에 active SPOT channel view는 하나만 둔다.
- `EnableRouter()`와 `EnablePubSub()`는 별도 capability다.
- 다른 channel에 대한 send/request는 attach된 client가 맡는다.
- 외부 노드에서 SPOT channel로 publish하려면 별도 spot publisher client를 쓴다.
- 따라서 SPOT 등록 시점에도 channel client attach와 spot publisher client
  attach를 별도 함수로 드러내는 편이 맞다.

### 4.1 capability별 수동 연결

SPOT도 일반 channel과 마찬가지로 수동 연결은 capability별로 나눠서 다뤄야 한다.
`router`, channel client, `pub/sub`, spot publish client는 모두 각 capability가
사용할 endpoint 집합을 따로 관리한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddSpotMesh("game.stage", mesh =>
    {
        mesh.UseDiscovery(discovery =>
        {
            discovery.Add("tcp://registry1:5551");
        });

        mesh.AddNode("stage-node", node =>
        {
            node.Bind("tcp://0.0.0.0:9000");

            node.EnableRouter(router =>
            {
                router.UseManualConnections(peers =>
                {
                    peers.Connect("tcp://10.0.0.10:9000");
                });
            });

            node.EnablePubSub(pubsub =>
            {
                pubsub.UseManualConnections(peers =>
                {
                    // Remote SpotNode mesh PUB endpoint.
                    // The local mesh SUB side connects to this address.
                    peers.Connect("tcp://10.0.0.20:9100");
                });
            });

            node.AttachClientServerChannelClient("orders", client =>
            {
                client.UseManualConnections(peers =>
                {
                    // Remote orders channel server endpoint
                    peers.Connect("tcp://10.0.0.30:9200");
                });
            });

            node.AttachSpotMeshPublisherClient("game.stage", publisher =>
            {
                publisher.UseManualConnections(peers =>
                {
                    // Remote game.stage SPOT publish endpoint
                    peers.Connect("tcp://10.0.0.40:9300");
                });
            });

            node.AddSpotFactory<StageSpot>("stage");
        });
    });
});
```

여기서 중요한 규칙은 아래와 같다.

- 수동 연결은 `SpotNode` 전체가 아니라 capability별로 관리한다.
- 같은 capability에서는 `Discovery`와 `Manual`을 섞지 않는다.
- 같은 `SpotNode`에서 `spotName`은 비어 있으면 안 된다.
- 이미 등록된 `spotName`을 다시 등록하면 기존 값을 덮어쓰지 않고 예외를 던진다.
- `router` manual 연결도 endpoint 집합만 등록한다. 이 초안에서는 `Connect(...)`
  호출 시 remote router id를 별도 파라미터로 받지 않는다.
- channel client manual 연결은 endpoint 집합만 등록한다. 하부 `DEALER`가 이미
  connect된 peer 집합을 대상으로 요청을 보내므로 remote `RoutingId`를 별도
  파라미터로 받지 않는다.
- `pub/sub` manual 연결에서 등록하는 주소는 다른 `SpotNode`의 mesh publish bind
  주소다. local `SUB/XSUB` 쪽이 그 주소로 붙는다.
- `pub/sub`, spot publisher client manual 연결은 endpoint 집합만 등록한다.
  다만 전자는 peer `SpotNode` mesh 주소이고, 후자는 외부 publish ingress 주소다.

#### capability별 소켓 옵션

소켓 옵션은 호출 단위 builder 옵션과 섞지 않고, 등록 시점의 runtime 기본값으로
정의하는 편이 맞다.

- `router.ConfigureSocket(...)`
  - 실제 `.NET` 바인딩의 `CommonSocketOptions`와 같은 공통 socket 기본값을 정한다.
- `router.ConfigureRouting(...)`
  - routed peer 연결에만 적용되는 전용 옵션을 정한다.
- `pubsub.ConfigurePublisherOptions(...)`
  - 실제 `SpotNode.PublisherOptions`에 있는 mesh publish 기본값을 정한다.
- `pubsub.ConfigureSubscriberOptions(...)`
  - 실제 `SpotNode.SubscriberOptions`에 있는 mesh subscribe 기본값을 정한다.
- `client.ConfigureSocket(...)`, `client.ConfigureRouting(...)`
  - attach된 channel client의 공통 socket 설정과 routed outbound 설정을 나눠 둔다.
- `publisher.ConfigureSocket(...)`
  - attach된 spot publisher client의 publish ingress 기본값을 정한다.

예시는 아래처럼 읽는 편이 자연스럽다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.DefaultTimeout = TimeSpan.FromSeconds(1);

    options.AddSpotMesh("game.stage", mesh =>
    {
        mesh.UseDiscovery(discovery =>
        {
            discovery.Add("tcp://registry1:5551");
        });

        mesh.AddNode("stage-node", node =>
        {
            node.Bind("tcp://0.0.0.0:9000");

            node.EnableRouter(router =>
            {
                router.ConfigureSocket(socket =>
                {
                    socket.MaxMessageSize = 1024 * 1024;
                    socket.SendTimeout = TimeSpan.FromMilliseconds(200);
                    socket.ReceiveTimeout = TimeSpan.FromMilliseconds(200);
                    socket.SendHighWaterMark = 10_000;
                    socket.ReceiveHighWaterMark = 10_000;
                    socket.Immediate = true;
                });

                router.ConfigureRouting(routing =>
                {
                    routing.Mandatory = true;
                    routing.Handover = true;
                });
            });

            node.EnablePubSub(pubsub =>
            {
                pubsub.ConfigurePublisherOptions(pubOpt =>
                {
                    pubOpt.SendHighWaterMark = 50_000;
                    pubOpt.SendTimeout = TimeSpan.FromMilliseconds(100);
                    pubOpt.NoDrop = true;
                });

                pubsub.ConfigureSubscriberOptions(subOpt =>
                {
                    subOpt.ReceiveHighWaterMark = 50_000;
                    subOpt.ReceiveTimeout = TimeSpan.FromMilliseconds(50);
                    subOpt.Linger = TimeSpan.Zero;
                });
            });

            node.AttachClientServerChannelClient("orders", client =>
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

            node.AttachSpotMeshPublisherClient("game.stage", publisher =>
            {
                publisher.ConfigureSocket(socket =>
                {
                    socket.SendHighWaterMark = 20_000;
                    socket.SendTimeout = TimeSpan.FromMilliseconds(100);
                    socket.Immediate = true;
                });
            });

            node.AddSpotFactory<StageSpot>("stage");
        });
    });
});
```

이때 timeout은 socket option이 아니라 실제 low-level 바인딩의
`Spot.RequestChannelAsync(..., TimeSpan timeout, ...)`처럼 호출 단위 인자로
들어간다. 즉 `RequestChannel(...).WithTimeout(...)` 같은 framework builder 옵션은
특정 요청 하나에만 적용되고, 위 등록 설정은 runtime 기본값으로 유지된다.

### 4.2 spot 실행 문맥과 timer

이 부분은 "timer를 어디서 만들고 어느 문맥에서 실행하느냐"를 분명히 적어 두는
것이 핵심이다.

현재 core spec 기준으로 아래는 이미 정해져 있다.

- 같은 `spot`의 dispatch callback delivery는 직렬화된다.
- subscribe, routed, **channel reply** completion은 같은 spot execution context 안에서
  처리된다.
- timer는 native timer를 직접 노출하지 않고, framework runtime이 만든 managed
  `.NET` timer를 사용한다.
- managed timer tick도 routed, subscribe, channel reply와 같은 직렬 실행 경로로
  들어온다.

channel reply completion 과 timer callback 이 같은 spot 실행 계약 안에 포함된다는
점이 핵심이다.

- `Spot.RequestChannelAsync(...)` 호출이 반환하는 `Task` 는 arbitrary thread 가
  아니라 **spot execution context 안에서** complete 된다.
- request completion callback 이 같은 spot executor 에서 실행되므로, continuation
  도 spot state 에 대해 별도 lock 없이 접근할 수 있다.
- binding 이 attached dealer 별 별도 progress pump 를 돌리지 않아도 된다.
  `Spot` progress loop 하나로 channel reply completion 까지 처리된다.
- actor가 `Spot`에 join된 뒤에는 `IZLinkActorContext.AddPacket(...)`으로 등록한
  actor packet handler도 같은 spot execution context에서 실행된다. stream session은
  packet ingress를 맡고, actor가 room 또는 stage 상태를 다루는 코드는 `Spot` 실행
  문맥으로 들어간다.
- actor join으로 현재 `Spot`이 바뀌는 경우, join 완료 뒤 들어오는 actor dispatch는
  새 `Spot` 실행 문맥에서 실행되어야 한다. framework는 actor session state 갱신과
  packet dispatch 선택 사이의 경합을 막아야 한다.

dispatch event 종류와 drain 대상은 아래처럼 정리된다.

| dispatch event | `SpotDispatchSubjectKind` | drain 방법 |
|---------------|--------------------------|------------|
| `SubscribeReadable` | `Spot` | `Subscribe()` |
| `RoutedReadable` | `Spot` | `RecvRouted()` |
| `ChannelReplyReadable` | `ChannelDealer` | `DrainChannelReplyFrom(subject)` |

timer는 이 low-level dispatch table에 직접 기대지 않고, framework runtime이 만든
managed `.NET` timer tick을 같은 spot 문맥으로 enqueue해서 처리한다.

즉 framework 문서에서 "같은 spot 문맥"이라고 설명하는 부분은 새 semantics를
정하는 일이 아니라, 기존 core 계약과 framework-owned timer dispatch를 `.NET`
사용자 눈높이로 풀어 적는 일에
가깝다. channel reply 도 이제 그 "같은 spot 문맥" 안에 포함된다.

### 4.3 Spot 생성과 lifecycle 초안

현재 방향에서는 handler class가 spot을 만드는 것이 아니다.
`Spot` 인스턴스는 `SpotNode`가 생성하고 소유한다. handler는 이미 존재하는 spot으로
들어오는 request, publish, subscribe를 처리할 뿐이다.

이 기준에서 manager는 `channelName`이 아니라 현재 앱의 `SpotNode`를 대상으로
동작하는 편이 더 자연스럽다.

`IZLinkSpotManager` 전체 정의는
[handler-interfaces.ko.md](./handler-interfaces.ko.md)의 section 6.3을 기준으로 본다.
이 문서에서는 그 인터페이스를 어떻게 읽고 어떤 상황에 쓰는지만 설명한다.

이 표면은 아래 상황을 함께 설명한다.

- `spotName`으로 factory를 고르고 runtime이 새 `spotRid`를 발급하는 생성
- `spotName`으로 factory를 고른 뒤 호출자가 특정 `spotRid`를 함께 지정해서
  생성하는 경우
- 이미 존재하는 `spotRid`면 그대로 얻는 `get-or-create` 성격의 동작

중요한 점은 반환값이 장기적으로 들고 다닐 spot instance handle이 아니라는 점이다.
생성 결과는 `spotRid`, `spotName`, `Created` 정도면 충분하다. 이후 메시징은
현재 channel publish 또는 attach된 channel client를 통한 send/request로 푸는
쪽이 현재 topology 초안과 더 잘 맞는다.

여러 factory를 같은 `SpotNode`에 등록할 수 있다면, 운영 코드에서
`spotRid -> spotName` 매핑을 다시 볼 수 있어야 한다. 그래서 `GetAsync(...)`와
`ListAsync(...)`를 함께 두고, 어떤 `spotRid`가 어떤 이름으로 생성됐는지
바깥에서 다시 확인할 수 있게 한다.

등록 단계에서 이름 충돌은 조용히 덮어쓰지 않는다. `AddSpotFactory<TSpot>(spotName)`
호출이 이미 등록된 이름을 다시 받으면 startup 시점에 예외를 던져 설정 실수를 바로
드러내는 쪽을 기본 규칙으로 본다.

즉 사용자는 생성 직후 식별자만 얻고:

```csharp
var stage = await spotManager.CreateAsync("stage", cancellationToken);

await spotClient
    .Publish(
        "stage.state.updated",
        new StageStateUpdatedEvent
        {
            StageRid = stage.Context.SpotRid.ToString()
        })
    .Async(cancellationToken);

var spotInfo = await spotManager.GetAsync(stage.SpotRid, cancellationToken);
```

처럼 사용하면 된다. 생성된 `Spot` 인스턴스를 응용이 직접 오래 관리하는 모델은
현재 방향으로 보지 않는다.

초기 metadata를 같이 넘기는 create 표면은 `Stage wrapper` 같은 상위 계층에서는
유용할 수 있다. 다만 현재 하부 C API 공개 계약에서 바로 읽히는 내용은 아니므로,
framework 기본 계약처럼 적기보다 wrapper 확장 후보로 따로 다루는 편이 맞다.

## 5. SPOT outbound 모델 초안

현재 방향에서는 아래 두 종류를 구분하는 편이 더 자연스럽다.

- 현재 SPOT channel 안의 topic publish
- attach된 다른 channel client를 통한 channel send/request

`SendChannel(...)` / `RequestChannel(...)`는 attach된 channel client를 사용한다.
`SpotNode.router`는 peer topology와 내부 transport를 위해 남지만, 현재 framework
public surface는 `SpotId` 기반 routed spot send/request를 구현 계약으로 제공하지
않는다. `targetRid + spotRid`를 직접 받는 raw 호출은 하부 바인딩에 남아 있어도,
application guide의 기본 API로 문서화하지 않는다.

`IZLinkSpotClient` 인터페이스 전체 정의는
[handler-interfaces.ko.md](./handler-interfaces.ko.md)의 section 5.2를
참고한다. 현재 방향에서는 `SendChannel(...)`,
`RequestChannel(...)`, `Publish(...)`를 함께 제공하고, timer는
`IZLinkSpotContext.AddTimer<THandler>(...)`처럼 spot lifecycle registration 표면으로
두는 쪽이 더 자연스럽다.

현재 `.NET` 바인딩의 raw `Spot` 표면도 이 구분을 그대로 가진다.

- channel 이름 기준 호출:
  `Spot.SendChannel(...)`, `Spot.RequestChannel(...)`
- SPOT routed 호출:
  `Spot.SendToRouter(...)`, `Spot.RequestToRouterAsync(...)`,
  `Spot.ReplyToSpot(...)`

즉 "spot용 routed 함수"와 "channelName으로 호출하는 함수"가 둘 다 있고, 둘은
서로 다른 경로를 뜻한다.

예를 들면 아래처럼 쓸 수 있다.

```csharp
await client
    .SendChannel(
        "orders",
        new RoomNoticeMessage())
    .Async(cancellationToken);

var reply = await client
    .RequestChannel(
        "orders",
        new GetStageStateRequest())
    .WithTimeout(TimeSpan.FromMilliseconds(200))
    .Async<GetStageStateReply>(cancellationToken);
```

`Stage wrapper` 같은 상위 모델을 생각하면 timer도 같이 필요하다.
다만 현재 초안은 이것을 `IZLinkSpotClient`의 callback scheduler로 두기보다,
`IZLinkSpotContext.AddTimer<THandler>(...)`로 등록하는 lifecycle timer 한 가지 모델로
정리하는 쪽이 더 자연스럽다. 그래야 stage state를 별도 lock 없이 다루는 상위
모델을 설명하기 쉽다.

하지만 이것을 `IZLinkClient` 위에 `IZLinkSpotClient`를 얹는 관계로 설명하면 안
된다. 두 인터페이스는 하부에서 서로 다른 C API를 감싼다. 현재 방향에서는
`IZLinkClient`가 일반 channel messaging을 맡고, `IZLinkSpotClient`는 current
SPOT channel publish와 다른 channel send/request를 맡는 식으로 책임을 나누는
편이 더 자연스럽다.

`IZLinkSpot` 기반 클래스의 `protected Publish(topic, message)` 편의 메서드는
`IZLinkSpotClient.Publish(...)`를 내부적으로 위임한다.
즉 spot 코드에서 `Publish(...)`를 직접 호출하는 것과, `IZLinkSpotClient`를
constructor injection해서 호출하는 것은 같은 경로를 사용한다.
`IZLinkSpot` 외부에서 현재 SPOT channel로 publish할 때는 `IZLinkSpotClient`를
명시적으로 주입받아 쓰는 편이 의도를 더 명확히 드러낸다.

## 6. publish 모델 초안

### 6.1 topic publish

`IZLinkSpotClient`는 spot-to-spot routed call과 publish를 함께 가질 수 있다
([handler-interfaces.ko.md](./handler-interfaces.ko.md) section 5.2 참고).
이는 `SPOT` 쪽이 두 기능을 함께 쓰는 경우가 많기 때문이다.

여기서 `topic`과 `spotRid`는 역할이 다르다.

- `spotRid`: 특정 room/stage/zone 인스턴스를 가리키는 논리 주소
- `topic`: 여러 subscriber가 함께 듣는 fan-out 주제 이름

현재 topology 초안에서는 framework 기본 표면을 `targetRid + spotRid` routed
호출 중심으로 설명하지 않는다. high-level framework 문서는 아래 두 축을 먼저
보여 주는 편이 더 자연스럽다.

- 같은 channel 안의 publish/subscribe
- attach된 다른 channel client를 통한 send/request

이때 channel send/request와 topic publish는 일반 channel messaging과 비슷한
builder 감각으로 읽히는 편이 자연스럽다.

```csharp
var reply = await spotClient
    .RequestChannel(
        "orders",
        new GetStageStateRequest())
    .WithTimeout(TimeSpan.FromMilliseconds(200))
    .Async<GetStageStateReply>(cancellationToken);

await spotClient
    .Publish(
        "stage.state.updated",
        new StageStateUpdatedEvent())
    .Async(cancellationToken);
```

`Stage wrapper` 같은 상위 계층이 별도 directory나 lookup을 얹는 것은 가능하지만,
그것을 framework 기본 표면으로 고정하는 것은 현재 방향으로 보지 않는다.

### 6.2 외부 노드에서의 SPOT channel publish

local spot 인스턴스가 없는 외부 노드가 특정 SPOT channel로 publish해야 할 수도
있다. 이런 경우에는 `IZLinkSpotClient.Publish(...)`가 아니라
`IZLinkSpotPublisherClient.Publish(channelName, topic, ...)`를 쓴다
([handler-interfaces.ko.md](./handler-interfaces.ko.md) section 5.3 참고).

```csharp
await spotPublisherClient
    .Publish(
        "game.stage",
        "stage.state.updated",
        new StageStateUpdatedEvent())
    .Async(cancellationToken);
```

이 인터페이스는 local spot 문맥이 없는 외부 노드에서도 target SPOT channel 이름을
명시해서 publish할 수 있게 해 준다. 따라서 local spot 안에서 현재 channel로
publish하는 경우와, 외부 노드에서 특정 SPOT channel로 publish하는 경우를 분리해
설명하는 편이 맞다.

또한 subscribe handler는 router request handler와 같은 종류의 매핑으로 보면 안
된다.

- packet은 header의 `msgId`를 기준으로 targeted dispatch 된다.
- subscribe는 `"stage.state.updated"` 같은 topic subscription으로 consumer 등록된다.
- 즉 둘 다 문자열을 쓰더라도 dispatch 의미가 다르다.

## 7. subscribe 모델 초안

실제 handler 인터페이스 초안은 [handler-interfaces.ko.md](./handler-interfaces.ko.md)를
기준으로 본다.

현재 `SPOT` 샘플은 attribute 기반보다, spot 객체가 `Configure()` 단계에서 직접
handler를 등록하는 쪽을 기본으로 본다.

```csharp
public sealed class StageSpot(IZLinkSpotContext context) : IZLinkSpot
{
    private IZLinkTimer? _heartbeat;

    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddPacket<GetStageStateHandler>();
        Context.AddPacket<ReportStageStateHandler>();

        Context.AddSubscribe<StageStateUpdatedHandler>(
            "stage.state.updated");
    }

    public async ValueTask OnInitializeAsync(
        CancellationToken cancellationToken)
    {
        _heartbeat = await Context.AddTimer<StageHeartbeatHandler>(
            "heartbeat",
            TimeSpan.FromSeconds(1),
            cancellationToken);
    }
}
```

여기서 기대하는 점은 아래와 같다.

- `Context.AddPacket<THandler>(...)`는 request와 send packet을 함께 등록한다.
- packet dispatch key는 packet 타입의 header `msgId`다.
- `protobuf`를 쓰면 `msgId`는 protobuf message name이다.
- `json`을 쓰면 `msgId`는 CLR class name이다.
- `Context.AddSubscribe<THandler>(...)`는 topic consumer 등록이다.
- `Context.AddTimer<THandler>(...)`는 현재 spot lifecycle 안에 timer를 등록한다.
- handler는 별도 class로 두고, `StageSpot` 안에는 코어 로직만 남길 수 있다.
- handler가 다른 서버나 다른 spot으로 outbound 호출을 해야 하면, `IZLinkClient`
  또는 `IZLinkSpotClient`를 constructor injection으로 받는 쪽이 더 자연스럽다.
- framework는 per-spot scope를 만들고, 등록한 handler 타입을 그 scope에서 자동으로
  resolve하는 편을 기본으로 본다.

### 7.1 room 계열 사용과 핫패스 원칙

`SPOT`이 FPS 같은 게임의 room으로 쓰이더라도, 이 모델 자체가 곧바로 과한
오버헤드를 만든다고 보지는 않는다. 다만 `SPOT` 쪽 메시지 handler 호출은 room
핫패스로 쓰일 수 있으므로, 일반 channel messaging보다 더 강한 성능 기준을 둬야
한다.

- reflection은 registration 단계까지만 허용한다.
- per-packet allocation, 과도한 DI 재구성, 불필요한 boxing은 피해야 한다.

즉 `Context.AddPacket<THandler>(...)` 같은 등록 표면은 startup, spot `Configure()`,
actor `Configure()` 단계에서만 비용이 들고, 실제 packet hot path에서는 반복적인
reflection이나 과도한 객체 생성이 남지 않게 해야 한다.

실제 room 성능에 더 큰 영향을 주는 것은 보통 registration 문법보다 아래 항목이다.

- protobuf encode/decode 비용
- 같은 spot 안의 queue 적체
- broadcast fan-out
- allocator pressure
- lock contention

따라서 framework 문서에서는 "class 기반 handler라서 느리다"보다, "핫패스 구현을
어떻게 캐시하고 줄일 것인가"를 더 중요한 원칙으로 본다. 여기서 말하는 강한
최적화 기준은 `SPOT` packet 처리 쪽에 우선 적용된다. 반대로 일반 socket/service
메시지 handler가 성능이 낮아도 된다는 뜻은 아니다. 차이는 "성능을 포기해도 된다"가
아니라, 일반 channel messaging 쪽은 `SPOT` room 핫패스보다 편의 기능을 조금 더
허용할 여지가 있다는 점에 가깝다.

## 8. SPOT과 direct call의 관계

`ZLink Framework`는 direct channel call만 제공하는 계층처럼 보이면 안 된다.
`SPOT`도 framework 안에서 동등한 축으로 다뤄야 한다.

즉 아래 둘이 함께 있어야 한다.

- `channelName` 기반 일반 channel messaging
- `SPOT` 기반 current channel publish/subscribe와 channel send/request

또한 현재 하부 topology는 `SpotNode.router` peer 경로와 attach된 channel client
경로를 함께 가진다. framework 문서에서는 아래 두 종류를 구분해 설명하는 편이
더 맞다.

- 같은 channel 안의 topic publish/subscribe
- attach된 다른 channel client를 통한 send/request

이 점은 `playhouse` 시나리오에서 특히 중요하다.

- play -> api 는 direct call
- stage/state sync 는 `SPOT`

또한 `rid`를 직접 넣는 routed 호출은 SPOT spot-to-spot 경로에만 남는다.
특정 channel의 `ROUTER(server)`를 `rid`로 직접 지정해 호출하는 모델은 현재
방향으로 보지 않는다.

즉 `SPOT`은 pub/sub만으로 설명하면 부족하고, room/stage/zone 같은 논리 인스턴스
모델과 channel publish/send/request, 그리고 `SpotNode`가 spot 인스턴스를 생성하고
소유하는 lifecycle까지 함께 설명해야 한다.

## 9. discovery와 service name

최신 topology 초안에서는 `SpotNode`가 channel 이름을 직접 소유하지 않는다.
`AddSpotMesh(channelName, mesh => mesh.UseDiscovery(...))` 등록이 active channel
view를 공급하고, 그 view가 같은 channel의 peer mesh 범위를 닫는다.

예를 들어 `AddSpotMesh("game.stage", mesh => mesh.UseDiscovery(...))`로 등록했다면,
그 mesh에 속한 `SpotNode`는 `game.stage` channel mesh 안에서 동작한다고 이해하면
된다.

mesh 등록을 쓰지 않고 옛 방식으로 `UseSpotDiscovery(channelName, ...)`와
`AddSpotNode(...)`를 따로 호출하는 패턴도 코드에는 남아 있지만, sample 코드는
mesh 묶음 형태를 권장한다.

## 10. 결정된 기준

- attach된 channel client와 spot publisher client 설정은 capability별 builder 하나로
  묶는다.
  socket option과 manual connection 같은 runtime 소유 설정만 노출하고, 더 세밀한
  하위 builder 트리는 기본 표면으로 늘리지 않는다.
- `spotRid` 타입은 `RoutingId`를 그대로 사용한다.
- `IZLinkSpotManager`는 생성과 조회를 함께 가진다.
  `GetAsync(...)`, `ListAsync(...)`는 별도 query 서비스로 분리하지 않고 manager에
  남긴다.
- subscriber concurrency와 backpressure는 per-handler나 per-topic API가 아니라,
  subscriber capability option에서 node 단위로 설정한다.

`Stage wrapper`에서 필요한 metadata 전달, membership, 실행 문맥 규칙은 framework
기본 계약이 아니라 [stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md)에서
다루는 상위 wrapper 축으로 본다.
