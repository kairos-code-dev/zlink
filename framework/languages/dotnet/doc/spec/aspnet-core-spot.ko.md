<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework ASP.NET Core Channel Messaging](./aspnet-core-channel-messaging.ko.md) | [다음: Stage Wrapper On SPOT](./stage-wrapper-on-spot.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[.NET 묶음](../README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [SPOT 샘플](../guide/samples/spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [STREAM](./aspnet-core-stream.ko.md)

# ZLink Framework ASP.NET Core SPOT Integration

## 1. 목표

이 절은 `ZLink Framework` 가 `SPOT` 을 `ASP.NET Core` 안에서 어떻게 다루려고
하는지, 그 방향을 한 문장으로 정리한다.

`SPOT` 은 zlink 쪽에서 이미 독립된 개념과 runtime 을 갖는다. 즉
`ZLink Framework` 가 이 개념을 새로 만들거나 없애려는 것이 아니다. 대신
`ASP.NET Core` 사용자가 익숙한 모양으로 다룰 수 있도록 감싸는 것이 목적이다.

이 문서가 다루는 축은 다음과 같다.

- `SpotNode`[^spotnode] lifecycle 관리
- `Spot` publish[^publish-subscribe]/subscribe facade[^facade] 주입
- Entry Spot[^entry-spot] application registry 등록
- actor[^actor] packet handler[^handler]와 join/leave lifecycle handler 등록
- room, stage, zone 같은 논리 인스턴스 모델 설명
- 현재 channel publish/subscribe
- attach[^attach]된 다른 channel client를 통한 send/request
- `Discovery`[^discovery] 기반 peer 구성
- background subscriber handler

## 2. 기반이 되는 .NET binding

이 절은 framework 가 새로 만든 것이 아니라 기존 binding 위에 얹는 작업임을
밝히는 자리다.

현재 하부 토대는 다음 binding 표면이다.

- `Discovery`
- `SpotNode`
- `Spot`
- `Spot` publish / subscribe
- channel client attach 기반 channel send / request

즉 이 문서의 핵심은 `SPOT` 기능 자체를 새로 만드는 일이 아니다. 이미 존재하는
binding 기능을 `ASP.NET Core` 안에 자연스럽게 녹여 넣는 방법을 정리하는 것이
목적이다.

등록 코드부터 handler, channel send / request, topic publish 까지 한 흐름으로
보는 샘플은 [spot-samples.ko.md](../guide/samples/spot-samples.ko.md) 에 모아 두었다. 또한
`playhouse` 의 `Stage` 같은 상위 모델을 `SPOT` 위에 다시 감쌀 때 필요한 추가
조건은 [stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md) 에서 다룬다.

## 3. SPOT을 무엇으로 보는가

이 절은 `SPOT` 을 어떤 개념으로 읽어야 하는지부터 짚는다. 그 다음에 같은
관점에서 `Spot`, `SpotNode`, channel 의 관계를 한 줄씩 정리한다.

현재 스펙에서 `SPOT` 은 단순한 pub / sub helper 가 아니다. 오히려 **주소 가능한
논리 인스턴스**[^addressable-instance] 로 이해하는 편이 더 정확하다. 대표적인 예는
다음과 같다.

- 게임 room
- playhouse stage
- 채팅 room
- MMORPG zone
- 필요하다면 Redis pub/sub 같은 fan-out[^fan-out] 주제 공간

즉 `SPOT` 은 "토픽 시스템" 이 아니라 먼저 "논리 대상 인스턴스" 로 설명되어야
한다. publish / subscribe 는 그 안에서 함께 사용할 수 있는 한 가지 활용 방식일
뿐이다.

이 관점에서 각 요소 사이의 관계를 더 정확히 정리하면 다음과 같다.

- `Spot`은 특정 service에 종속되지 않는다.
- `Spot`은 `SpotNode`에 종속된다.
- `SpotNode`는 channel 이름을 직접 소유하지 않는다.
- `AddSpotMesh(channelName, mesh => mesh.UseDiscovery(...))` 등록이 active
  channel view[^channel-view]를 공급한다.
- 같은 `SpotNode`에는 active SPOT channel view를 하나만 둔다.
- `SpotNode.router`와 pub/sub mesh[^mesh]는 같은 channel에 속한 다른
  `SpotNode`와만 연결된다.
- 다른 channel 호출은 `SpotNode.router`가 아니라 attach된 channel client 경로로
  처리한다.
- 따라서 `spotRid`는 service에서 부여되는 값이 아니라, `SpotNode`가 spot
  인스턴스를 생성할 때 발급하는 식별자다.

이 관점에서 특히 중요한 점은 다음과 같다.

- 현재 SPOT channel 안에서는 topic publish/subscribe를 사용한다.
- 다른 channel 호출은 attach된 channel client를 통해 보낸다.
- `SpotNode.router`는 peer topology[^topology]와 내부 routed[^routed] delivery를
  위해 남겨 두되, framework core의 public high-level API에서는
  `targetRid + spotRid`를 직접 받는 direct routed 호출 표면을 두지 않는다.
- spot name 또는 spot rid를 다른 노드의 user Spot 위치로 변환해야 하면
  `IZLinkSpotRouteResolver`[^route-resolver]를 쓴다. resolver 구현체만
  `RoutingId`[^routing-id]를 알고, application handler는 spot name/id만 기준으로
  호출한다.
- 외부 `PUB -> Spot` 입력은 generic pub/sub attach가 아니라 별도의
  ingress[^ingress] 표면으로 분리한다.

여기서 경계를 분명히 짚어 두면 다음과 같다.

- `SPOT` 이 제공하는 것은 주소 가능한 논리 인스턴스와 그 인스턴스에 대한
  메시징, publish / subscribe, timer, lifecycle 까지다.
- 반면 room broadcast 정책과 도메인별 권한 모델은 여전히 응용 계층의 책임으로
  남는다.
- 현재 draft 구현에서는 actor join, actor factory[^factory] 등록, 그리고
  stream callback 에서 `IZLinkSessionContext` 로 actor packet / disconnect 를
  같은 `SPOT` 실행 문맥에 올리는 브리지[^bridge] 까지를 framework core 범위에
  포함한다.

## 4. ASP.NET Core 등록 모델 초안

이 절은 실제 `AddZLinkFramework(...)` 등록이 어떤 모양인지부터 한 덩어리로
보여 준 다음, 같은 코드를 한 줄씩 풀어서 설명한다.

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
            node.AddEntrySpot<StageEntrySpot>();
            node.AddSpotFactory<StageSpot>("stage");
        });
    });

    options.UseRegistrySpotRoutes("game");
});
```

이 등록 코드의 의미는 다음과 같다.

- 논리 `SpotNode` 이름은 `stage-node`
- 그에 대응하는 backing `SpotNode` 생성
- `AddSpotMesh("game.stage", mesh => mesh.UseDiscovery(...))`가 active channel
  view 공급
- 같은 channel에 속한 다른 `SpotNode`와만 mesh 구성
- local routed router capability[^capability] 활성화
- local SPOT pub/sub capability 활성화
- 다른 channel 호출용 client attach
- 필요하다면 외부 노드용 spot publish client attach
- 자동 Entry Spot에 붙일 application registry 등록
- spot name/id 기반 호출 또는 actor `JoinSpot(...)` 경로에서 사용할 Registry 기반
  spot route resolver 등록
- host shutdown 시 lifecycle 정리

`AddSpotMesh(...)` 는 같은 channel 에 속하는 여러 `SpotNode` 를 하나의 묶음으로
등록한다. 그 묶음 안에서 각 항목이 맡는 역할은 다음과 같다.

- mesh 안에서는 `mesh.AddNode(name, configure)` 로 노드를 추가한다.
- mesh 단위의 discovery 설정은 `mesh.UseDiscovery(...)` 가 담당한다.

즉 같은 채널을 가리키는 `SpotNode` 묶음을 한 mesh 에 모아 두는 모양이다. 덕분에
한 앱 안에서 서로 다른 channel mesh 를 따로 등록할 수도 있고, 한 mesh 안에 같은
channel 을 공유하는 여러 노드를 함께 둘 수도 있다.

mesh 로 묶지 않고 discovery 없이 단일 노드만 띄우는 경우라면
`options.AddSpotNode(...)` 표면을 직접 쓸 수 있다. 다만 이 standalone 등록은
mesh discovery, `EnableRouter`, channel attach 같은 mesh 기능과 함께 사용할 수
없다는 점에 주의한다. 노드가 mesh 기능을 쓰면서 standalone 등록을 시도하면,
`RegistrationValidator` 가 시작 시점에 `AddSpotMesh` 등록을 강제하는 오류로
막아 준다.

이 등록 함수들은 각각 다음과 같이 역할이 나뉜다.

- `EnableRouter()`
  - local `SpotNode.router` 경로를 켠다. 같은 channel에 속한 다른 `SpotNode`와
    routed packet을 주고받는 축이다.
- `EnablePubSub()`
  - 현재 SPOT channel 안의 publish/subscribe 축을 켠다. local spot 안에서
    `IZLinkSpotClient.Publish(...)`를 사용하려면 이 capability가 필요하다.
- `AttachClientServerChannelClient("orders")`
  - `orders` channel로 outbound[^outbound] send/request를 보낼
    `DEALER(client)`[^dealer-router] 경로를 붙인다.
- `AttachSpotMeshPublisherClient("game.stage")`
  - local spot 인스턴스를 갖지 않는 외부 노드가 `game.stage` SPOT channel로
    publish할 수 있도록 별도의 publisher client를 붙인다.
- `AddEntrySpot<StageEntrySpot>()`
  - 이 노드의 자동 Entry Spot에 붙일 application registry를 등록한다.
  - Entry Spot 자체의 native 생성과 소멸은 framework가 관리한다.
  - 등록하지 않으면 빈 Entry Spot registry가 사용된다. 이 경우 actor가 Entry Spot에
    머무는 동안 처리할 application actor packet handler와 lifecycle handler가
    없다는 뜻이다.
- `AddSpotFactory<StageSpot>("stage")`
  - 이 노드가 생성하고 소유할 `StageSpot` factory를 `stage` 이름으로 등록한다.
  - 같은 `SpotNode`에 여러 spot factory를 둘 수 있는 경우, 생성 시점에 이 이름을
    기준으로 어떤 factory를 쓸지 선택한다.
  - 이미 등록된 이름을 다시 사용하면 조용히 덮어쓰지 않고 예외를 던진다.

즉 `SpotNode` 는 더 이상 여러 service surface 를 동시에 소유하는 hub 처럼
설명되지 않는다. 현재 방향에서 그 역할 분담은 다음과 같다.

- `AddSpotMesh(channelName, mesh => mesh.UseDiscovery(...))` 등록이 노드의
  channel 정체성을 닫는다.
- 다른 channel 호출은 별도로 attach 된 client 경로를 통해 푼다.

이 모델에서 중요한 점은 다음과 같다.

- mesh 묶음 없이 standalone `SpotNode` 생성 호출만으로는 channel 범위가 닫히지
  않는다.
- `AddSpotMesh("game.stage", mesh => { ... })`가 이 노드의 mesh 범위를 정한다.
- 같은 `SpotNode`에 active SPOT channel view는 하나만 둔다.
- `EnableRouter()`와 `EnablePubSub()`는 별개의 capability다.
- 다른 channel에 대한 send/request는 attach된 client가 담당한다.
- 외부 노드에서 SPOT channel로 publish하려면 별도의 spot publisher client를 쓴다.
- 따라서 SPOT 등록 시점에도 channel client attach와 spot publisher client attach를
  서로 다른 함수로 드러내는 편이 자연스럽다.

### 4.1 Entry Spot과 actor handler 등록

이 소절은 Entry Spot 에서 어떤 handler 를 어디에 등록하는지, 그리고 그 등록을
application 이 직접 손대지 않는 raw 표면과 어떻게 구분하는지 정리한다.

Entry Spot 은 actor 가 생성된 직후 처음 머무르는 기본 실행 문맥이다. 따라서
application 은 raw Entry Spot handle 을 직접 만들거나 보관하지 않는다. 대신
`AddEntrySpot<TEntrySpot>()` 로 Entry Spot 에서 실행할 actor packet handler 와
join / leave lifecycle handler registry 를 등록한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddSpotMesh("game.stage", mesh =>
    {
        mesh.AddNode("stage-node", node =>
        {
            node.Bind("tcp://0.0.0.0:9000");

            node.AddEntrySpot<StageEntrySpot>();
            node.AddSpotFactory<StageSpot>("stage");
        });
    });
});
```

Entry Spot 클래스는 `IZLinkEntrySpot` 을 구현한다. `Configure()` 안에서
Entry 단계의 handler 를 등록한다. Entry Spot 과 user Spot 은 등록할 수 있는
기능 표면이 같다. 차이는 실행 정책이다. user Spot 은 같은 spot 으로 들어온
callback 을 하나의 실행 줄에서 직렬로 처리하고, Entry Spot 은 공용 입구이므로
actor packet 이나 일반 packet 을 Entry Spot 전체 실행 줄에 묶지 않는다.

```csharp
public sealed class StageEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddPacket<StageAdmissionHandler>();
        Context.AddSubscribe<StageAdmissionEventHandler>("stage.admission");
        Context.AddActorPacket<AuthenticateStageActorHandler, StageActor>();
        Context.AddActorPacket<JoinStageHandler, StageActor>();
        Context.AddActorJoined<StageEntryJoinedHandler, StageActor>();
        Context.AddActorLeft<StageEntryLeftHandler, StageActor>();
    }
}
```

user Spot 클래스도 같은 방식이다. `IZLinkSpot` 을 구현하고, user Spot 단계의
actor handler 를 등록한다. room, stage, zone 상태를 다루는 packet 은 Entry Spot
이 아니라 이쪽 registry 에 둔다.

Entry Spot 과 user Spot 모두 context 는 생성자에서 주입받아 `Context` property
로 그대로 노출한다. framework 는 생성된 spot 이 주입된 context 를 노출하지
않으면 activation 을 실패시킨다.

```csharp
public sealed class StageSpot(IZLinkSpotContext context) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddActorPacket<MoveOnStageHandler, StageActor>();
        Context.AddActorPacket<ReportStageStateHandler, StageActor>();
        Context.AddActorJoined<StageJoinedHandler, StageActor>();
        Context.AddActorLeft<StageLeftHandler, StageActor>();
    }
}
```

Entry Spot registry 와 user Spot registry 는 서로 다른 namespace 다. 따라서
같은 actor 타입과 packet 이름이라도, Entry 단계와 user Spot 단계에서 서로 다른
handler 로 매핑할 수 있다.

반대로 같은 registry 안에서 같은 `actor type + packet kind + packet name`
조합을 둘 이상 등록하면 startup validation 오류가 된다. `AddActorJoined(...)`
와 `AddActorLeft(...)` 역시 같은 registry 안에서 같은 actor 타입에 대해
하나씩만 허용한다.

join / leave lifecycle 은 `OnJoinActor` 나 `OnLeaveActor` 같은 Spot 메서드
override 로 정의하지 않는다. Entry Spot 과 user Spot 모두
`AddActorJoined(...)` / `AddActorLeft(...)` 에 해당하는 registry 등록으로 후속
처리를 붙인다. 이 callback 은 join / leave commit 이 끝난 뒤 같은 실행 문맥에서
호출된다. 그래서 admission[^admission] 을 결정하는 hook 이 아니라는 점에
주의한다.

### 4.2 SPOT 실행 queue와 actor mailbox

이 소절은 "같은 user Spot 안의 callback 은 왜 한 줄로 실행되는가" 와
"Entry Spot 의 actor packet 은 왜 actor 단위로 갈라지는가" 두 질문을 묶어서
정리한다.

user Spot 은 room, game, stage 같은 하나의 상태 객체로 본다. 따라서 user Spot
안에서 실행되는 callback 은 같은 Spot 실행 queue[^execution-queue] 에서
순서대로 처리한다. 여기에 포함되는 것은 다음과 같다.

- Spot packet, Spot request
- subscription, timer
- actor join
- user Spot 에 머무는 actor 에게 전달되는 packet

이 규칙 덕분에, 같은 user Spot 안의 `actor A` 와 `actor B` 가 모두 같은 게임판
상태를 바꾸더라도 두 handler 가 동시에 실행되지 않는다. 즉 application 은 user
Spot 인스턴스의 상태를 일일이 별도 lock 으로 보호하지 않아도 된다.

Entry Spot 은 사정이 조금 다르다. Entry Spot 은 특정 room 상태를 소유하는 곳이
아니라, 모든 actor 가 처음 거쳐 가는 공용 입구이기 때문이다. 그래서 Entry Spot
actor packet 은 Entry Spot 전체 queue 가 아니라, 대상 actor 의
mailbox[^mailbox] 로 들어간다. 같은 actor 의 packet 은 순서대로 실행되지만,
서로 다른 actor 의 packet 은 굳이 기다릴 필요 없이 병렬로 진행된다.

정리하면 다음과 같다.

| 대상 | 실행 줄 |
| --- | --- |
| Entry Spot actor packet | actor별 mailbox |
| Entry Spot initialize / closing / lifecycle callback | Entry Spot 실행 문맥 |
| user Spot actor packet | user Spot 실행 queue |
| user Spot packet / timer / subscription | user Spot 실행 queue |

Entry Spot actor handler 는 actor 와 payload 를 받는다. user Spot actor
handler 는 spot, actor, payload 를 함께 받는다. 두 표면을 따로 둔 이유는 간단
하다. Entry Spot 에는 아직 user Spot 객체가 없고, user Spot 에서는 spot 상태와
actor 상태를 함께 다뤄야 하기 때문이다.

자세한 시그니처는
[handler-interfaces.ko.md](./handler-interfaces.ko.md) 의 SPOT lifecycle
handler 섹션을 기준으로 본다.

### 4.2 capability별 수동 연결

이 소절은 discovery 를 쓰지 않고 endpoint 를 직접 지정해 연결할 때, 그 설정을
어디에 어떻게 둬야 하는지를 정리한다.

SPOT 역시 일반 channel 과 마찬가지로 수동 연결은 capability 단위로 나눠서
다뤄야 한다. `router`, channel client, `pub/sub`, spot publish client 는 각자
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

            node.AddEntrySpot<StageEntrySpot>();
            node.AddSpotFactory<StageSpot>("stage");
        });
    });
});
```

여기서 따라야 할 규칙은 다음과 같다.

- 수동 연결은 `SpotNode` 전체가 아니라 capability별로 관리한다.
- 같은 capability 안에서는 `Discovery`와 `Manual`을 섞지 않는다.
- 같은 `SpotNode`에서 `spotName`은 비어 있으면 안 된다.
- 이미 등록된 `spotName`을 다시 등록하면 기존 값을 덮어쓰지 않고 예외를 던진다.
- `router` manual 연결도 endpoint 집합만 등록한다. 이 문서에서는 `Connect(...)`
  호출 시 remote router id를 별도 파라미터로 받지 않는다.
- channel client manual 연결도 endpoint 집합만 등록한다. 하부 `DEALER`가 이미
  connect된 peer 집합을 대상으로 요청을 보내기 때문에, remote `RoutingId`를 별도
  파라미터로 받지 않는다.
- `pub/sub` manual 연결에서 등록하는 주소는 다른 `SpotNode`의 mesh publish bind
  주소다. local `SUB/XSUB`[^sub-xsub] 쪽이 그 주소로 붙는다.

### 4.3 Spot route resolver

이 소절은 application 코드가 `RoutingId` 를 직접 다루지 않고도 다른 노드의
user Spot 으로 호출을 보낼 수 있도록, framework 가 어떤 인터페이스를 두고 그
구현을 어떻게 위임받는지 정리한다.

`IZLinkSpotRouteResolver` 는 spot name 또는 spot rid 를, 현재 user Spot 이
위치한 노드와 spot rid 로 변환한다. framework 는 그 resolver 가 registry, Redis,
memory cache 중 무엇을 쓰는지 알지 못한다. handler 와 actor 코드 역시
`RoutingId` 를 직접 들고 다니지 않는다.

```csharp
namespace Zlink.Framework.Contracts.Spots;

public interface IZLinkSpotRouteResolver
{
    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        string spotName,
        CancellationToken cancellationToken);

    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkSpotRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    RoutingId SpotRid);
```

resolver 입력은 spot key 하나로 제한한다. 즉 packet 이름, metadata, request
payload 는 resolver 에 넘기지 않는다. 그런 값이 필요한 경우라면 application 의
placement 코드가 먼저 spot name 또는 spot rid 를 결정해 두어야 한다.
- `pub/sub`과 spot publisher client의 manual 연결은 endpoint 집합만 등록한다.
  다만 전자는 peer `SpotNode`의 mesh 주소이고, 후자는 외부 publish ingress 주소다.

#### capability별 소켓 옵션

소켓 옵션은 호출 단위 builder 옵션과 섞지 않는다. 대신 등록 시점의 runtime
기본값으로 정의한다.

- `router.ConfigureSocket(...)`
  - 실제 `.NET` 바인딩의 `CommonSocketOptions`와 같은 공통 socket 기본값을 정한다.
- `router.ConfigureRouting(...)`
  - routed peer 연결에만 적용되는 전용 옵션을 정한다.
- `pubsub.ConfigurePublisherOptions(...)`
  - 실제 `SpotNode.PublisherOptions`에 들어가는 mesh publish 기본값을 정한다.
- `pubsub.ConfigureSubscriberOptions(...)`
  - 실제 `SpotNode.SubscriberOptions`에 들어가는 mesh subscribe 기본값을 정한다.
- `client.ConfigureSocket(...)`, `client.ConfigureRouting(...)`
  - attach된 channel client의 공통 socket 설정과 routed outbound 설정을 나눠
    구성한다.
- `publisher.ConfigureSocket(...)`
  - attach된 spot publisher client의 publish ingress 기본값을 정한다.

예시를 풀어 보면 다음처럼 읽힌다.

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
                    routing.RequireKnownPeer = true;
                    routing.AllowPeerHandover = true;
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
                    routing.ProbeRouterOnConnect = true;
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

이때 timeout 은 socket option 이 아니다. 실제 low-level 바인딩의
`Spot.RequestChannelAsync(..., TimeSpan timeout, ...)` 처럼 호출 단위 인자로
들어가는 값이다. 즉 `RequestChannel(...).Timeout(...)` 같은 framework builder
옵션은 특정 요청 하나에만 적용된다. 위 등록 설정은 그와 별개로 runtime
기본값으로 유지된다.

### 4.4 spot 실행 문맥과 timer

이 소절의 핵심은 "timer 를 어디서 만들고 어느 문맥에서 실행하는가" 를 분명히
적어 두는 데 있다.

현재 core spec 기준으로 이미 다음과 같은 점이 정해져 있다.

- 같은 user `Spot`의 dispatch callback delivery는 직렬화된다.
- Entry Spot 은 user Spot 과 같은 handler/callback 등록 표면을 갖지만,
  Entry Spot 전체 실행 줄로 packet callback 을 직렬화하지 않는다. 여러 actor 와
  입장 요청이 공유하는 입구이기 때문에 서로 관계없는 요청을 한 줄로 세우지 않는다.
- subscribe, routed, **channel reply** completion은 모두 같은 spot execution
  context 안에서 처리된다.
- timer는 native timer를 직접 노출하지 않고, framework runtime이 만든 managed
  `.NET` timer를 사용한다.
- managed timer tick 은 user Spot 에서는 routed, subscribe, channel reply와
  동일한 직렬 실행 경로로 들어온다.
- Entry Spot timer callback 은 Entry Spot 전체 직렬 실행 줄에 묶지 않는다.
  Entry Spot 은 여러 actor 가 공유하는 입구이므로 timer 하나가 관계없는 Entry
  Spot callback 을 전역으로 막으면 안 된다. 단일 timer instance 안에서는 이전
  callback 이 끝나기 전에 다음 callback 을 겹쳐 실행하지 않는다.

여기서 핵심은 channel reply completion 과 timer callback 이 모두 같은 spot
실행 계약 안에 포함된다는 점이다.

- `Spot.RequestChannelAsync(...)` 호출이 반환하는 `Task`는 임의의 thread가
  아니라 **spot execution context 안에서** complete된다.
- request completion callback이 같은 spot executor에서 실행되므로,
  continuation도 spot state에 별도 lock 없이 접근할 수 있다.
- binding이 attached dealer마다 별도 progress pump를 돌리지 않아도 된다.
  `Spot` progress loop 하나로 channel reply completion까지 처리된다.
- actor가 `Spot`에 join된 뒤에는 `IZLinkSpotContext.AddActorPacket(...)`으로
  등록한 actor packet handler 역시 같은 spot execution context에서 실행된다.
  stream session은 packet ingress를 맡고, actor가 room 또는 stage 상태를 다루는
  코드는 `Spot` 실행 문맥으로 들어간다.
- actor join으로 현재 `Spot`이 바뀌는 경우, join이 완료된 뒤 들어오는 actor
  dispatch는 새 `Spot` 실행 문맥에서 실행되어야 한다. framework는 actor session
  state 갱신과 packet dispatch 선택 사이의 경합을 막는다.

dispatch event 종류와 drain 대상은 아래처럼 정리된다.

| dispatch event | `SpotDispatchSubjectKind` | drain 방법 |
|---------------|--------------------------|------------|
| `SubscribeReadable` | `Spot` | `Subscribe()` |
| `RouteReadable` | `Spot` | `RecvRoute()` |
| `ChannelReplyReadable` | `ChannelDealer` | `DrainChannelReplyFrom(subject)` |

timer 는 이 low-level dispatch table 에 직접 기대지 않는다. 대신 framework
runtime 이 만든 managed `.NET` timer tick 을 user Spot 문맥에서는 같은 spot
queue 로 enqueue 해서 처리한다. Entry Spot timer 는 같은 등록 표면을 쓰지만
Entry Spot 전체 queue 로 enqueue 하지 않는다.

즉 framework 문서에서 "같은 spot 문맥" 이라고 설명하는 부분은 새 semantics 를
정의하는 작업이 아니다. 기존 core 계약과 framework 가 소유한 timer dispatch 를
`.NET` 사용자 눈높이로 풀어 적는 일에 더 가깝다. channel reply 역시 이제 그
"같은 spot 문맥" 안에 포함된다.

### 4.5 Spot 생성과 lifecycle 초안

이 소절은 `Spot` 인스턴스를 누가 만들고 누가 소유하는지를 정리한다. 그리고
그에 맞춰 manager 표면을 어떤 모양으로 두는 것이 자연스러운지 본다.

현재 방향에서는 handler 클래스가 spot 을 만들지 않는다. `Spot` 인스턴스는
`SpotNode` 가 생성하고 소유한다. handler 는 이미 존재하는 spot 으로 들어오는
request, publish, subscribe 를 처리할 뿐이다.

이 기준에서 manager 는 `channelName` 이 아니라 현재 앱의 `SpotNode` 를
대상으로 동작하는 편이 더 자연스럽다.

`IZLinkSpotManager` 의 전체 정의는
[handler-interfaces.ko.md](./handler-interfaces.ko.md) 의 section 6.3 을
기준으로 본다. 이 문서에서는 그 인터페이스를 어떻게 읽고 어떤 상황에 쓰는지만
다룬다.

이 표면은 다음 상황을 함께 설명한다.

- `spotName`으로 factory를 고르고 runtime이 새 `spotRid`를 발급하는 생성
- 생성 요청이 넘긴 multipart payload를 `OnCreateAsync(...)`로 전달하는 경우
- 이미 존재하는 `spotRid`라면 그대로 얻어 오는 `get-or-create` 성격의 동작

여기서 중요한 점은 반환값이 장기적으로 들고 다닐 spot instance handle 이
아니라는 사실이다. 생성 결과는 `spotRid`, `spotName`, `Created` 정도면
충분하다. 이후 메시징은 현재 channel publish 또는 attach 된 channel client 를
통한 send / request 로 푸는 쪽이, 지금의 topology 초안과 더 잘 맞는다.

생성 요청의 payload는 multipart로 받을 수 있어야 한다. framework는 caller가
넘긴 part 경계를 보존해서 `IZLinkSpot.OnCreateAsync(createParts, ...)`에 한 번
전달한다. 이 payload는 방 설정, seed, 접근 정책처럼 spot이 처음 만들어질 때만
해석해야 하는 값에 사용한다.
`CreateAsync(spotName)`처럼 payload가 없는 편의 overload는 빈 multipart payload를
넘긴 것과 같다. 새 spot이 만들어지면 `OnCreateAsync(...)`는 빈 list를 받아 한 번
실행된다.

생성 요청에는 어떤 spot factory를 사용할지도 함께 들어가야 한다. framework
표면에서는 이 값을 `spotName`으로 표현한다. `spotName`은 등록된 factory key이므로
remote framework node가 같은 이름으로 등록된 factory를 선택할 수 있다. 반대로
CLR class name이나 assembly-qualified type name은 wire 계약에 싣지 않는다. 그런
구현 타입명은 배포 형태와 언어에 묶이므로 공개 요청 계약으로 쓰기 어렵다.

명시적 `spotRid`가 필요한 경우 public surface는
`CreateAsync(spotName, spotRid, ...)`가 아니라
`GetOrCreateAsync(spotName, spotRid, createParts, ...)`로 표현한다. 이미 같은
`spotRid`의 framework spot이 ready 상태면 `Created = false`를 반환하고, 새
요청의 `createParts`는 `OnCreateAsync(...)`로 전달하지 않는다. initializing
상태면 첫 생성 요청의 `OnCreateAsync(...)` 완료를 기다린다. 다만 기존 entry의
`spotName`이 요청의 `spotName`과 다르면 같은 logical spot을 다른 framework type으로
해석하려는 시도이므로 `SpotTypeMismatch`로 실패해야 한다.

remote framework node에 생성 요청을 relay하는 경우도 같은 구조를 유지한다.
metadata에는 `spotName`과 선택적인 `spotRid`를 넣고, metadata 뒤의 message part들을
create payload로 보낸다. 이렇게 해야 payload codec을 열기 전에도 수신 node가
factory를 결정할 수 있다.

여러 factory 를 같은 `SpotNode` 에 등록할 수 있다면, 운영 코드에서
`spotRid -> spotName` 매핑을 다시 볼 수 있어야 한다. 그래서 `GetAsync(...)` 와
`ListAsync(...)` 를 함께 둔다. 즉 어떤 `spotRid` 가 어떤 이름으로 생성됐는지
바깥에서 다시 확인할 수 있다.

등록 단계에서 이름 충돌은 조용히 덮어쓰지 않는다.
`AddSpotFactory<TSpot>(spotName)` 호출이 이미 등록된 이름을 다시 받으면
startup 시점에 예외를 던진다. 설정 실수를 바로 드러내는 쪽을 기본 규칙으로
본다.

따라서 사용자는 생성 직후 식별자만 얻고:

```csharp
var stage = await spotManager.CreateAsync("stage", cancellationToken);

await spotClient
    .Publish(
        "stage.state.updated",
        new StageStateUpdatedEvent
        {
            StageRid = stage.SpotRid.ToString()
        })
    .Submit(cancellationToken);

var spotInfo = await spotManager.GetAsync(stage.SpotRid, cancellationToken);
```

처럼 사용하면 된다. 생성된 `Spot` 인스턴스를 응용이 직접 오래 관리하는 모델은
현재 방향에서는 다루지 않는다.

초기 payload 를 함께 넘기는 create 표면은 framework 기본 계약에 포함한다.
다만 core C API는 payload를 해석하지 않는다. core는 logical spot 확보의 원자성만
보장하고, payload 전달과 typed 초기화는 framework lifecycle의 책임이다.
factory resolve, activation, `OnCreateAsync(...)`, `OnInitializeAsync(...)` 실패는
`SpotCreateFailed` 계열로 분류한다.

## 5. SPOT outbound 모델 초안

이 절은 SPOT 쪽 outbound 호출이 어떤 축으로 갈라지는지, 그리고 그 축마다 어느
표면을 쓰는지를 정리한다.

현재 방향에서는 다음 세 종류를 구분하는 편이 더 자연스럽다.

- 현재 SPOT channel 안의 topic publish
- attach 된 다른 channel client 를 통한 channel send / request
- spot name / id 기반 routed spot send / request

각 표면이 맡는 역할은 다음과 같다.

- `SendChannel(...)` / `RequestChannel(...)` 는 attach 된 channel client 를
  사용한다.
- `SendSpot(...)` / `RequestSpot(...)` 는 spot route resolver 가 찾은 target
  route 를 이용한다.
- `targetRid + spotRid` 를 직접 받는 raw 호출은 하부 바인딩에 남아 있더라도,
  application guide 의 기본 API 로는 문서화하지 않는다.

`IZLinkSpotClient` 인터페이스의 전체 정의는
[handler-interfaces.ko.md](./handler-interfaces.ko.md) 의 section 5.2 를
참고한다. 현재 방향에서는 `SendSpot(...)`, `RequestSpot(...)`,
`SendChannel(...)`, `RequestChannel(...)`, `Publish(...)` 를 함께 제공한다.
timer 는 `IZLinkSpotContext.AddTimer<THandler>(...)` 처럼 spot lifecycle
registration 표면으로 두는 쪽이 더 자연스럽다.

현재 `.NET` framework 표면은 channel 이름 기준 호출과 spot key 기반 호출을
구분한다. `targetRid + spotRid` 를 직접 받는 raw route 함수가 하부 바인딩에 있어도,
framework application 문서에서는 backend / internal transport helper 로만
다룬다. 일반 application 은 `IZLinkSpotRouteResolver` 가 숨긴 위치값을 직접
보지 않는다.

예를 들면 다음과 같이 사용할 수 있다.

```csharp
await client
    .SendChannel(
        "orders",
        new RoomNoticeMessage())
    .Submit(cancellationToken);

var reply = await client
    .RequestChannel(
        "orders",
        new GetStageStateRequest())
    .Timeout(TimeSpan.FromMilliseconds(200))
    .SubmitAsync<GetStageStateReply>(cancellationToken);

await client
    .SendSpot(
        "stage-17",
        new StageNoticeMessage())
    .Submit(cancellationToken);
```

`Stage wrapper` 같은 상위 모델을 생각하면 timer 도 함께 필요하다. 다만 현재
초안은 이를 `IZLinkSpotClient` 의 callback scheduler 로 두지 않는다. 대신
`IZLinkSpotContext.AddTimer<THandler>(...)` 로 등록하는 lifecycle timer 한 가지
모델로 정리한다. 그래야 stage state 를 별도 lock 없이 다루는 상위 모델을
설명하기 쉬워진다.

다만 이 관계를 `IZLinkClient` 위에 `IZLinkSpotClient` 를 얹는 형태로 설명하면
안 된다. 두 인터페이스는 하부에서 서로 다른 C API 를 감싸기 때문이다. 현재
방향에서는 책임을 다음과 같이 나눈다.

- `IZLinkClient` 는 일반 channel messaging 을 맡는다.
- `IZLinkSpotClient` 는 current SPOT channel publish, 다른 channel
  send / request, spot-routed send / request 를 맡는다.

`IZLinkSpot` 기반 클래스의 `protected Publish(topic, message)` 편의 메서드는
`IZLinkSpotClient.Publish(...)` 를 내부적으로 위임한다. 즉 spot 코드에서 직접
`Publish(...)` 를 호출하는 것과, `IZLinkSpotClient` 를 constructor
injection[^constructor-injection] 해서 호출하는 것은 같은 경로를 사용한다.
다만 `IZLinkSpot` 외부에서 현재 SPOT channel 로 publish 하는 경우에는
`IZLinkSpotClient` 를 명시적으로 주입받아 쓰는 쪽이 의도를 더 분명하게
드러낸다.

## 6. publish 모델 초안

이 절은 SPOT 쪽 publish 모델을 두 갈래로 나누어 정리한다. 먼저 local spot
안에서의 topic publish 를 보고, 그 다음에 local spot 이 없는 외부 노드에서의
SPOT channel publish 를 본다.

### 6.1 topic publish

`IZLinkSpotClient` 는 spot-to-spot routed call 과 publish 를 함께 가질 수 있다
([handler-interfaces.ko.md](./handler-interfaces.ko.md) section 5.2 참고).
이렇게 둔 이유는 `SPOT` 쪽에서 두 기능을 함께 쓰는 경우가 많기 때문이다.

여기서 `topic` 과 `spotRid` 는 역할이 서로 다르다.

- `spotRid`: 특정 room/stage/zone 인스턴스를 가리키는 논리 주소
- `topic`: 여러 subscriber가 함께 듣는 fan-out 주제 이름

현재 topology 초안에서는 framework 기본 표면을 `targetRid + spotRid` direct
호출 중심으로 설명하지 않는다. 대신 high-level framework 문서는 다음 세 축을
먼저 보여 준다.

- 같은 channel 안의 publish / subscribe
- attach 된 다른 channel client 를 통한 send / request
- spot name / id 기반 routed send / request

이때 channel send / request, spot send / request, topic publish 는 일반
channel messaging[^channel-messaging] 과 비슷한 builder 감각으로 읽힌다.

```csharp
var reply = await spotClient
    .RequestChannel(
        "orders",
        new GetStageStateRequest())
    .Timeout(TimeSpan.FromMilliseconds(200))
    .SubmitAsync<GetStageStateReply>(cancellationToken);

await spotClient
    .SendSpot(
        "stage-17",
        new StageNoticeMessage())
    .Submit(cancellationToken);

await spotClient
    .Publish(
        "stage.state.updated",
        new StageStateUpdatedEvent())
    .Submit(cancellationToken);
```

`Stage wrapper` 같은 상위 계층이 별도의 directory 나 lookup 을 얹는 것은
가능하다. 다만 그것을 framework 의 기본 표면으로 고정하는 모델은 현재
방향에서 채택하지 않는다.

### 6.2 외부 노드에서의 SPOT channel publish

local spot 인스턴스를 가지지 않는 외부 노드가 특정 SPOT channel 로 publish
해야 하는 경우도 있다. 이때는 `IZLinkSpotClient.Publish(...)` 가 아니라
`IZLinkSpotPublisherClient.Publish(channelName, topic, ...)` 를 사용한다
([handler-interfaces.ko.md](./handler-interfaces.ko.md) section 5.3 참고).

```csharp
await spotPublisherClient
    .Publish(
        "game.stage",
        "stage.state.updated",
        new StageStateUpdatedEvent())
    .Submit(cancellationToken);
```

이 인터페이스는 local spot 문맥이 없는 외부 노드에서도 target SPOT channel
이름을 명시해 publish 할 수 있게 해 준다. 따라서 두 경우를 분리해 설명한다.
하나는 local spot 안에서 현재 channel 로 publish 하는 경우이고, 다른 하나는
외부 노드에서 특정 SPOT channel 로 publish 하는 경우다.

또한 subscribe handler 는 router request handler 와 같은 종류의 매핑으로 보면
안 된다. 두 경우 모두 문자열을 키로 쓰지만, dispatch 의미는 서로 다르기
때문이다.

- packet 은 header 의 `msgId`[^msg-id] 를 기준으로 targeted dispatch 된다.
- subscribe 는 `"stage.state.updated"` 같은 topic subscription 으로 consumer
  등록된다.

## 7. subscribe 모델 초안

이 절은 `SPOT` 안에서 packet handler, subscribe handler, timer 가 어떤
모양으로 등록되는지를 정리한다. 핵심은 attribute 기반이 아니라 `Configure()`
안에서 직접 등록하는 점이다.

실제 handler 인터페이스 초안은
[handler-interfaces.ko.md](./handler-interfaces.ko.md) 를 기준으로 본다.

현재 `SPOT` 샘플은 attribute[^attribute] 기반이 아니다. 대신 spot 객체가
`Configure()` 단계에서 직접 handler 를 등록하는 쪽을 기본으로 본다.

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
            new ZLinkTimerOptions
            {
                OverrunPolicy = ZLinkTimerOverrunPolicy.DelayNextTick
            },
            cancellationToken);
    }
}
```

여기서 기대하는 동작은 다음과 같다.

- `Context.AddPacket<THandler>(...)`는 request와 send packet을 함께 등록한다.
- packet dispatch key는 packet 타입의 header `msgId`다.
- `protobuf`[^protobuf]를 쓰면 `msgId`는 protobuf message 이름이 된다.
- `json`을 쓰면 `msgId`는 CLR class 이름이 된다.
- `Context.AddSubscribe<THandler>(...)`는 topic consumer 등록이다.
- `Context.AddTimer<THandler>(...)`는 현재 spot lifecycle 안에 timer를 등록한다.
  세 번째 인자인 `ZLinkTimerOptions` 로 overrun 정책과 handler 예외 정책을 정한다.
- handler는 별도의 class로 두고, `StageSpot` 안에는 코어 로직만 남길 수 있다.
- handler가 다른 서버나 다른 spot으로 outbound 호출을 해야 한다면 `IZLinkClient`
  또는 `IZLinkSpotClient`를 constructor injection으로 받는 쪽이 더 자연스럽다.
- framework는 per-spot scope[^per-spot-scope]를 만들고, 등록된 handler 타입을 그
  scope에서 자동으로 resolve하는 방식을 기본으로 본다.

timer handler 는 아래처럼 tick metadata 를 받는다.

```csharp
public sealed class StageHeartbeatHandler
    : IZLinkSpotTimerHandler<StageSpot>
{
    public ValueTask HandleAsync(
        StageSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}
```

`ZLinkTimerTick` 은 callback 번호, fixed-rate 시간표의 tick 번호, 예정 시각,
시작 시각, 지연, 건너뛴 tick 수를 포함한다. `SkipLateTicks`와 `CatchUpBounded`는
fixed-rate 기준 시각을 유지하고, `DelayNextTick`은 handler 완료 뒤 period 를 다시
기다리는 fixed-delay 정책이다. timer handler 예외는 runtime monitoring 에
`TimerHandlerFailed` event 로 기록된다. `StopOnUnhandledException` 이 켜져 있으면
timer 를 중단하고 `TimerStoppedAfterUnhandledException` event 를 기록한다.

### 7.1 room 계열 사용과 핫패스 원칙

이 소절은 `SPOT` 을 FPS 같은 게임의 room 으로 쓸 때 어떤 성능 기준을 들어야
하는지, 그리고 실제 성능을 좌우하는 항목이 무엇인지 정리한다.

`SPOT` 이 FPS 같은 게임의 room 으로 쓰이더라도, 이 모델 자체가 곧바로 과한
오버헤드를 만든다고 보지는 않는다. 다만 `SPOT` 쪽 메시지 handler 호출은 room
의 핫패스[^hot-path] 가 될 수 있다. 따라서 일반 channel messaging 보다 더 강한
성능 기준을 적용한다.

- reflection[^reflection] 은 registration 단계까지만 허용한다.
- per-packet allocation, 과도한 DI 재구성, 불필요한 boxing[^boxing] 은 피해야
  한다.

즉 `Context.AddPacket<THandler>(...)` 같은 등록 표면은 startup, spot
`Configure()`, actor `Configure()` 단계에서만 비용이 들도록 둔다. 실제 packet
hot path 에서는 반복적인 reflection 이나 과도한 객체 생성이 남지 않게 해야
한다.

실제 room 성능에 더 큰 영향을 주는 것은 보통 registration 문법보다 다음
항목들이다.

- protobuf encode/decode 비용
- 같은 spot 안의 queue 적체
- broadcast fan-out
- allocator pressure[^allocator-pressure]
- lock contention[^lock-contention]

따라서 framework 문서는 "class 기반 handler 라서 느리다" 가 아니라 "핫패스
구현을 어떻게 캐시하고 어떻게 줄일 것인가" 를 더 중요한 원칙으로 본다.

여기서 말하는 강한 최적화 기준은 `SPOT` packet 처리 쪽에 우선 적용된다. 반대로
일반 socket / service 메시지 handler 의 성능을 포기해도 된다는 뜻은 아니다.
차이는 "성능을 포기해도 된다" 가 아니다. 일반 channel messaging 쪽은 `SPOT`
room 의 핫패스에 비해 편의 기능을 조금 더 허용할 여지가 있다는 정도로 본다.

## 8. SPOT과 direct call의 관계

이 절은 framework 안에서 일반 channel messaging 과 `SPOT` 두 축을 어떻게
구분해 설명할지를 정리한다.

`ZLink Framework` 는 direct channel call 만 제공하는 계층처럼 비춰져서는 안
된다. `SPOT` 역시 framework 안에서 동등한 축으로 다뤄야 한다.

즉 다음 두 축이 함께 존재해야 한다.

- `channelName` 기반 일반 channel messaging
- `SPOT` 기반 current channel publish / subscribe 와 channel send / request

또한 현재 하부 topology 는 `SpotNode.router` peer 경로와 attach 된 channel
client 경로를 함께 가진다. framework 문서에서는 다음 두 종류를 구분해서 설명
한다.

- 같은 channel 안의 topic publish / subscribe
- attach 된 다른 channel client 를 통한 send / request

이 점은 `playhouse` 시나리오에서 특히 중요하다.

- play -> api 는 direct call
- stage/state sync 는 `SPOT`

또한 `rid`[^rid] 를 직접 넣는 routed 호출은 SPOT spot-to-spot 경로에만 남는다.
특정 channel 의 `ROUTER(server)`[^dealer-router] 를 `rid` 로 직접 지정해서
호출하는 모델은 현재 방향에서 채택하지 않는다.

즉 `SPOT` 은 pub / sub 만으로 설명하면 부족하다. 다음 세 가지를 함께 설명해야
한다.

- room / stage / zone 같은 논리 인스턴스 모델
- channel publish / send / request
- `SpotNode` 가 spot 인스턴스를 생성하고 소유하는 lifecycle

## 9. discovery와 service name

이 절은 `SpotNode` 가 어떻게 channel 정체성을 닫는지, 그리고 그 결정이
discovery 와 어떻게 묶이는지를 짧게 정리한다.

최신 topology 초안에서는 `SpotNode` 가 channel 이름을 직접 소유하지 않는다.
대신 `AddSpotMesh(channelName, mesh => mesh.UseDiscovery(...))` 등록이 active
channel view 를 공급한다. 그 view 가 같은 channel 에 속한 peer mesh 의 범위를
닫는다.

예를 들어 `AddSpotMesh("game.stage", mesh => mesh.UseDiscovery(...))` 로
등록했다고 하자. 이 경우 그 mesh 에 포함된 `SpotNode` 는 `game.stage` channel
mesh 안에서 동작한다고 이해하면 된다.

mesh 등록을 쓰지 않고 옛 방식으로 `UseSpotDiscovery(channelName, ...)` 와
`AddSpotNode(...)` 를 따로 호출하는 패턴이 코드에 남아 있을 수 있다. 다만
sample 코드는 mesh 묶음 형태를 권장한다.

## 10. 결정된 기준

- attach된 channel client와 spot publisher client 설정은 capability별 builder 하나로
  묶는다. socket option과 manual connection처럼 runtime이 소유하는 설정만
  노출하고, 그보다 더 세밀한 하위 builder 트리는 기본 표면으로 확장하지 않는다.
- spot rid 는 별도 wrapper 없이 `RoutingId` 로 노출한다. framework 문서에서는
  node rid 와 spot rid 를 이름으로 구분한다.
- Entry Spot application registry는 `SpotNode` 등록 안에서
  `AddEntrySpot<TEntrySpot>()`로 붙인다. Entry Spot 자체의 native lifecycle은
  framework가 관리한다.
- Entry Spot과 user Spot의 actor packet handler, actor joined handler, actor
  left handler는 각 context의 registry에 등록한다. join/leave lifecycle을 Spot
  메서드 override만으로 설명하지 않는다.
- Entry Spot 과 user Spot 은 packet, subscription, timer, channel outbound,
  actor handler 등록 표면을 맞춘다. 실행 직렬화 정책만 서로 다르다.
- `IZLinkSpotManager`는 생성과 조회를 함께 가진다. `GetAsync(...)`,
  `ListAsync(...)`는 별도 query 서비스로 분리하지 않고 manager에 남긴다.
- subscriber concurrency와 backpressure[^backpressure]는 per-handler나 per-topic
  API가 아니라, subscriber capability option에서 노드 단위로 설정한다.

`Stage wrapper` 에서 필요한 metadata 전달, membership, 실행 문맥 규칙은
framework 의 기본 계약이 아니다. 이 항목들은
[stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md) 에서 다루는 상위
wrapper 축으로 본다.

## 11. 회귀 테스트

이 절은 SPOT 문서가 다룬 항목들을 어떤 테스트로 검증하는지 한꺼번에 본다.

SPOT 문서의 항목은 factory 등록, mesh / discovery 구성, lifecycle, publish,
actor join 문맥이 함께 검증되어야 한다. 또한 spot 이름과 id 를 다루는 public
표면은, 호출자가 transport 위치를 알지 못해도 동작해야 한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RegistrationValidationTests.AddZLinkFramework_Throws_WhenSpotFactoryNameIsDuplicatedAcrossNodes` | 같은 `spotName` factory를 중복 등록하면 startup validation 예외가 난다. |
| `RegistrationValidationTests.AddZLinkFramework_Throws_WhenSpotMeshHasNoUseDiscovery` | Discovery 없는 mesh 구성은 시작 전에 실패한다. |
| `SpotIntegrationTests.SpotManager_Create_List_Remove_And_Publish_Work_Through_FrameworkRuntime` | `CreateAsync`, `GetAsync`, `ListAsync`, `RemoveAsync`와 scope 정리가 일관된다. |
| `SpotIntegrationTests.Spot_Publish_Timer_And_Remove_Stop_Callbacks_Work` | timer와 publish callback이 spot lifecycle 안에서 돌고, 제거 뒤에는 멈춘다. |
| `SpotIntegrationTests.SpotTimer_Provides_Tick_Metadata` | timer handler가 callback 번호, 예정/시작 시각, 지연, skip metadata를 받는다. |
| `SpotIntegrationTests.SpotTimer_Skips_Late_Ticks_When_Configured` | `SkipLateTicks` 정책은 늦은 tick을 무제한 전달하지 않고 `SkippedTicks`로 드러낸다. |
| `SpotIntegrationTests.SpotTimer_Catches_Up_Within_Configured_Limit` | `CatchUpBounded` 정책은 `MaxCatchUpTicks` 상한 안에서만 연속 실행한다. |
| `SpotIntegrationTests.SpotTimer_DelayNextTick_Waits_After_Handler_Completion` | `DelayNextTick` 정책은 handler 완료 뒤 period를 다시 기다린다. |
| `SpotIntegrationTests.SpotTimer_NonCatchUpPolicy_Ignores_MaxCatchUpTicks` | `CatchUpBounded`가 아닌 정책에서는 `MaxCatchUpTicks`가 scheduling 의미를 바꾸지 않는다. |
| `SpotIntegrationTests.SpotTimer_CatchUpPolicy_Rejects_Invalid_MaxCatchUpTicks` | `CatchUpBounded` 정책에서 `MaxCatchUpTicks <= 0`은 설정 오류다. |
| `SpotIntegrationTests.SpotTimer_Rejects_Unknown_OverrunPolicy` | 알 수 없는 overrun 정책 값은 설정 오류다. |
| `SpotIntegrationTests.SpotTimer_Reports_Handler_Exception_To_Monitoring` | handler 예외가 runtime monitoring의 timer failure event로 기록된다. |
| `SpotIntegrationTests.SpotTimer_StopOnUnhandledException_Stops_Timer` | `StopOnUnhandledException`이 켜진 timer는 첫 handler 예외 뒤 중단된다. |
| `SpotIntegrationTests.SpotTimer_CancelAsync_Stops_Managed_Timer_Loop` | `CancelAsync()` 뒤 managed timer loop가 추가 callback을 실행하지 않는다. |
| `SpotIntegrationTests.OutboundOnly_SpotPublisherClient_Publishes_To_TargetChannel` | 외부 publisher client가 target SPOT channel로 publish한다. |
| `SpotIntegrationTests.SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext` | actor join, 이동, packet dispatch가 현재 spot 실행 문맥에서 실행된다. |
| `SpotIntegrationTests.EntrySpot_ActorPackets_Are_Serialized_Per_Actor_And_Parallel_Across_Actors` | Entry Spot actor packet이 Entry Spot 전체 실행 줄에 막히지 않고 actor별 순서를 지킨다. |
| `SpotIntegrationTests.EntrySpot_PacketHandlers_Are_Dispatched_Without_EntrySpot_Serialization` | Entry Spot 일반 packet handler가 user Spot과 같은 방식으로 등록되며 Entry Spot 전체 직렬 실행 줄에 묶이지 않는다. |
| `SpotIntegrationTests.EntrySpotTimer_Does_Not_Block_EntrySpot_Callbacks_Globally` | 긴 Entry Spot timer callback이 다른 Entry Spot callback을 전역으로 막지 않는다. |
| `SpotIntegrationTests.EntrySpotTimer_Does_Not_Reenter_Same_Timer` | Entry Spot timer는 전역 queue에 묶이지 않아도 같은 timer callback을 겹쳐 실행하지 않는다. |
| `SpotIntegrationTests.EntrySpot_NativeActorReadableBatch_Dispatches_Actors_In_Parallel` | native `ActorReadable` batch 안에서도 서로 다른 Entry Spot actor packet이 병렬로 진행된다. |

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^spot]: `SPOT` 은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 추상이다. `SpotNode` 는 하나 이상의 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^lifecycle]: lifecycle 은 객체나 컴포넌트가 만들어져서 초기화·동작·정리 단계를 거쳐 사라지기까지의 흐름을 가리킨다.
[^spotnode]: `SpotNode` 는 같은 channel 안의 spot 인스턴스들을 호스팅하고 router/pub-sub 같은 capability 를 묶어 관리하는 컨테이너 노드다.
[^publish-subscribe]: publish/subscribe 는 한쪽이 topic 으로 메시지를 내보내면 그 topic 을 구독한 여러 수신자가 함께 메시지를 받는 fan-out 방식의 메시징 패턴이다.
[^facade]: facade 는 복잡한 하부 기능을 단순한 표면 하나로 묶어 제공하는 디자인 패턴을 가리킨다.
[^entry-spot]: Entry Spot 은 actor 가 생성된 직후 처음 위치하는 공용 입구 역할의 Spot 이다. 인증, 초기 상태 설정, target Spot 선택 같은 단계가 여기서 이뤄진다.
[^actor]: actor 는 자기 상태와 mailbox 를 갖고 메시지 단위로 동작하는 단위 객체로, 같은 actor 의 메시지는 순서대로 직렬 처리된다.
[^handler]: handler 는 특정 메시지(packet, request, event 등)가 도착했을 때 실행되는 콜백 함수 또는 메서드를 뜻한다.
[^attach]: attach 는 한 node 가 다른 channel 의 client 경로를 자기 안에 끌어다 붙여, 그 channel 로 send/request 를 보낼 수 있게 하는 동작이다.
[^discovery]: `Discovery` 는 Registry 같은 외부 디렉토리를 통해 같은 channel 에 속한 peer 의 endpoint 를 자동으로 찾아 연결을 구성하는 메커니즘이다.
[^addressable-instance]: 주소 가능한 논리 인스턴스(addressable instance) 는 식별자로 직접 지목해 메시지를 보낼 수 있는 단위 객체(room, stage 등)를 가리킨다.
[^fan-out]: fan-out 은 한 발신자가 보낸 메시지를 여러 수신자에게 동시에 전달하는 구조를 뜻한다.
[^channel-view]: active channel view 는 한 `SpotNode` 가 현재 어떤 channel 의 일원으로 동작하고 있는지를 나타내는 활성 channel 시야를 가리킨다.
[^mesh]: mesh 는 같은 channel 에 속한 여러 `SpotNode` 가 서로 peer 로 연결돼 routed/pub-sub 트래픽을 주고받는 망 구조를 가리킨다.
[^topology]: topology 는 어떤 노드(channel, spot, registry 등)가 어디에 있는지, 그리고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^routed]: routed 호출은 router 가 목적지 식별자를 기준으로 패킷을 특정 peer 로 전달하는 방식의 송수신을 뜻한다.
[^route-resolver]: route resolver 는 spot name/id 같은 논리 키를 실제 transport 위치(node rid 등)로 변환해 주는 컴포넌트다.
[^routing-id]: `RoutingId` 는 transport 계층에서 peer 를 식별하는 라우팅 키다. application 코드는 보통 이 값을 직접 다루지 않고 resolver 가 숨긴다.
[^ingress]: ingress 는 외부에서 시스템 안으로 들어오는 트래픽의 진입 지점을 가리킨다.
[^factory]: factory 는 특정 종류의 객체(여기서는 spot 인스턴스)를 만들어 내는 생성기 컴포넌트를 뜻한다.
[^bridge]: 여기서 bridge 는 외부 stream session 으로 들어온 packet/disconnect 를 framework 내부의 actor 메시지로 이어 같은 실행 문맥에 올려 주는 연결 지점을 가리킨다.
[^capability]: capability 는 어떤 노드(channel, spot 등)가 외부에 노출하는 역할이나 기능 단위(예: server, subscriber, publisher)를 가리킨다.
[^outbound]: outbound 는 현재 노드에서 외부로 나가는 방향의 호출(send, request 등)을 뜻한다.
[^dealer-router]: `DEALER`/`ROUTER` 는 ZeroMQ 의 비대칭 소켓 쌍으로, `DEALER` 는 client 측, `ROUTER` 는 server 측에서 routing id 기반 송수신을 담당한다.
[^admission]: admission 은 어떤 actor/요청을 받아들일지 거절할지 결정하는 입장 통제 단계를 뜻한다.
[^execution-queue]: execution queue 는 같은 실행 문맥에서 순서대로 콜백을 처리하기 위한 직렬 작업 큐를 가리킨다.
[^mailbox]: mailbox 는 actor 가 받은 메시지를 도착 순서대로 보관하고 하나씩 꺼내 처리하기 위한 actor 전용 큐다.
[^sub-xsub]: `SUB`/`XSUB` 는 ZeroMQ 의 구독 측 소켓 종류로, `SUB` 는 기본 구독자, `XSUB` 는 구독 메시지를 그대로 노출해 중간 프록시 등에 쓸 수 있는 변형이다.
[^constructor-injection]: constructor injection 은 의존성을 클래스 생성자 인자로 주입받는 DI 방식이다. 객체 생성 시점에 필요한 의존성이 모두 채워지는 것이 특징이다.
[^channel-messaging]: channel messaging 은 채널 이름을 키로 삼아 메시지를 주고받는 방식이다. request / send 는 요청-응답과 단방향 전달, event messaging 은 publish / subscribe 형태의 이벤트 전달을 가리킨다.
[^msg-id]: `msgId` 는 packet header 에 실리는 메시지 종류 식별자로, dispatcher 가 어느 handler 로 보낼지 결정할 때 키로 사용한다.
[^attribute]: attribute 는 `.NET` 에서 타입이나 메서드에 메타데이터를 붙이는 선언적 마커로, framework 가 reflection 으로 읽어 자동 등록 등에 사용한다.
[^protobuf]: `protobuf` 는 Google Protocol Buffers 의 약칭으로, 스키마 기반의 이진 직렬화 포맷이다.
[^per-spot-scope]: per-spot scope 는 spot 인스턴스마다 별도로 만들어지는 DI 컨테이너 scope 로, 그 spot 의 lifecycle 동안 등록된 서비스들이 함께 살아 있다.
[^hot-path]: hot path 는 호출이 매우 잦아 성능에 직접 영향을 주는 핵심 실행 경로를 뜻한다.
[^reflection]: reflection 은 런타임에 타입과 멤버 정보를 동적으로 조사·호출하는 기능이다. 강력하지만 hot path 에서는 비용이 크므로 보통 등록 단계까지만 제한한다.
[^boxing]: boxing 은 값 타입(value type)을 참조 타입(object)으로 감싸 힙에 올리는 변환으로, 핫패스에서 누적되면 GC 압력을 키운다.
[^allocator-pressure]: allocator pressure 는 짧은 시간에 잦은 할당이 일어나 GC 가 자주 동작하면서 latency 와 throughput 을 함께 떨어뜨리는 상태를 가리킨다.
[^lock-contention]: lock contention 은 여러 thread 가 같은 lock 을 동시에 잡으려고 다투면서 대기가 누적되어 처리량이 떨어지는 상태를 가리킨다.
[^rid]: `rid` 는 routing id 의 약칭으로, transport 계층에서 특정 peer 를 가리키는 식별자다.
[^backpressure]: backpressure 는 송신 측이 수신 측의 처리 속도를 넘어 메시지를 밀어 넣지 못하도록 흐름을 조절하는 메커니즘이다.
