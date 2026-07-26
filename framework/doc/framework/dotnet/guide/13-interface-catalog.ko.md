<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: 운영 — 메트릭 · drain · readiness](12-operations.ko.md) | [다음: ZLink를 어디에 쓰나](14-grpc-alternative.ko.md)
<!-- framework-adapter-nav:end -->

# 13. 인터페이스 카탈로그 — 모든 계약을 코드로

> 이 챕터는 framework adapter가 노출하는 **모든 public 계약 인터페이스**를 한곳에
> 모아, 각 인터페이스가 실제로 어떻게 쓰이는지 코드와 함께 보여 준다. 개념·사용
> 흐름은 앞 챕터(05~12)가, 언어 중립 정식 정의는
> [spec/handler-interfaces](../../common/spec/server/languages/dotnet/02-handler-interfaces.ko.md)가 다룬다. 이 문서는
> 그 둘을 잇는 **10.0.0 목표 계약 예문 색인**이다.

## 0. 이 카탈로그를 읽는 법

이 문서의 모든 코드 조각은 `.NET` exact interface가 정의한 10.0.0 목표 계약을 사용한다.
현재 source·package와 목표 계약의 차이는
[구현 차이](../../common/spec/90-implementation-gap.ko.md)가 소유한다. 각 절 끝의 `검증` 줄은 구현을
목표 계약에 맞출 때 함께 갱신하고 통과시켜야 할 계약 테스트를 가리킨다.

- **완전성 기준.** 10.0.0 구현이 끝나면
  `ContractSurfaceCoverage.Every_public_contract_interface_has_a_scenario_example`이
  `Zlink.Framework.Contracts.*`의 모든 public interface가 적어도 하나의
  `[ContractExample(typeof(...))]` 시나리오에 등장하는지 단언해야 한다. 인터페이스가 바뀌면
  exact interface, 계약 테스트와 이 문서를 함께 갱신한다.
- **예문의 도메인.** 계약 테스트는 게임 서버 도메인(`room`/`player`/`authenticate`,
  `RoomEvent`, `JoinRoom`/`JoinedRoom`)으로 통일돼 있다. 등록(`AddZLinkFramework`)
  코드 흐름은 앞 챕터의 실사용 예와 같고, 여기서는 **인터페이스 표면(메서드
  시그니처·fluent chain)** 에 집중한다.
- **표기.** 서버 framework 타입은 전부 `ZLink`(대문자 L)다. 표 안에서 제네릭
  인자는 `IZLinkSpotPacketHandler<TSpot, TMessage>`처럼 풀어 적되, 본문 코드는
  실제 구현 형태를 보인다.
- **비동기 이름 규칙.** 공통 의미는
  [비동기 실행과 coroutine 정책](../../common/spec/04-async-execution-policy.ko.md)을
  따른다. `.NET`에서는 `Task`, `ValueTask`, `Task<T>`, `ValueTask<T>`를 반환하는
  공개 호출 종결자가 `Async(...)`로 끝난다. 실행 worker에 작업을 넣는
  `IZLinkWorkerCall<TResult>.Submit(...)`은 이 규칙의 적용 대상이 아니다.

각 절 끝의 `검증` 줄은 해당 표면을 고정해야 할 계약 테스트 메서드를 가리킨다.

## 1. Channel messaging — request · send · pub/sub

> 사용법은 [05-channel-messaging](05-channel-messaging.ko.md). 검증 클래스는
> `ChannelContracts`, `HandlerContracts`, `CodecContracts`.

### 1.1 outbound client와 종결 호출

```csharp
// IZLinkRouteClient를 주입받아 process-local 송신 경로의 ChannelName으로 send/request 한다.
await client
    .SendToChannel("api", new AuthenticateRequest("player-1")) // IZLinkSendCall
    .Async(ct);

var reply = await client
    .RequestToChannel("api", new AuthenticateRequest("player-1")) // IZLinkRequestCall
    .Timeout(TimeSpan.FromSeconds(3))     // request만 가진 종결자 — reply 대기 상한이다.
    .Async<AuthenticateReply>();          // reply 타입은 종결자에서 지정한다.
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkRouteClient` | `SendToChannel(channelName, msg)` / `RequestToChannel(channelName, req)`의 select-one 표면. ChannelName으로 process-local RouteMesh 또는 ClientServer 송신 경로를 고르고 준비 상태인 서버 구성원 하나를 선택한다 |
| `IZLinkSendCall` | send 종결자 — `Async(ct)`가 먼저 즉시 admission을 시도하고, 용량이 없으면 MeshNode send timeout까지 기다린다. 원격 handler 완료는 기다리지 않는다 |
| `IZLinkRequestCall` | request 종결자. 선택 `Timeout(...)` 후 `Async<TReply>(ct)`. reply 타입은 종결자에서 지정 |

검증: `ChannelContracts`의 ChannelName send/request 계약.

### 1.2 route client — MeshName 안의 target node 호출

```csharp
var target = RoutingId.From("play-node-1");   // MeshName 안에서 직접 지정할 최종 대상 RID다.

await client
    .SendToNode("play", target, new RoomEvent("opened")) // 인자 = (MeshName, target RID, payload)
    .Async(ct);

var room = await client
    .RequestToNode("play", target, new AllocateRoom("alice")) // IZLinkRequestCall
    .Timeout(TimeSpan.FromSeconds(2))
    .Async<RoomAllocated>();
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkRouteClient` | MeshName과 target RID로 보내거나, 논리적 `SpotHandle` 대상으로 보내는 client. 노드는 `SendToNode(meshName, nodeRid, …)`/`RequestToNode(meshName, nodeRid, …)`, spot은 `SendToSpot(handle, …)`/`RequestToSpot(handle, …)`을 쓴다(§3.2). |
| `IZLinkSendCall` | route send 종결자(`Async`) |
| `IZLinkRequestCall` | route request 종결자(`Timeout` · `Async<TReply>`) |
| `IZLinkRouteSendHandler<TMessage>` | route mesh channel의 단방향 수신 handler. `HandleAsync(msg, ZLinkRouteMessageContext, ct)` |
| `IZLinkRouteRequestHandler<TRequest, TReply>` | route mesh channel의 요청 수신 handler. `HandleAsync(req, ZLinkRouteMessageContext, ct) → ValueTask<TReply>` |

검증: `ChannelContracts.Route_client_addresses_a_target_node_through_a_router_channel`.

### 1.3 fanout publisher

```csharp
await publisher
    .Publish("events", new RoomEvent("opened")) // ChannelName과 typed event만 넘기며 packet name은 framework가 결정한다.
    .Async(ct);
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkFanoutClient` | classic fanout publish 표면. `Publish(channelName, typedEvent)`로 transport topic을 노출하지 않는다 |
| `IZLinkFanoutPublishCall` | classic fanout 전용 publish 종결자 — `Async(ct)`가 local publisher admission을 기다리고 결과를 반환한다 |

검증 대상: `ChannelContracts`의 ChannelName + typed event classic fanout 계약.

### 1.4 handler와 filter

```csharp
public sealed class AuthenticateRequestHandler
    : IZLinkRequestHandler<Authenticate, Authenticated>
{
    public ValueTask<Authenticated> HandleAsync(
        Authenticate request, IZLinkMessageContext context, CancellationToken ct)
        => ValueTask.FromResult(new Authenticated(request.PlayerId));   // 결과는 반환값으로 돌려준다(별도 reply call 아님)
}

public sealed class AuditingFilter : IZLinkHandlerFilter
{
    public ValueTask InvokeAsync(
        ZLinkHandlerInvocation invocation, ZLinkHandlerFilterNext next, CancellationToken ct)
        => next();   // 호출하지 않으면 이 ChannelName send/request의 handler가 실행되지 않는다.
}
```

`OnDispatchAsync(...)`의 `payload`는 framework `ZLinkMessage` 다. session은 이를
decode 하거나 `IZLinkSessionActor.RelayAsync(...)`에 그대로 넘긴다. raw transport write를
직접 호출하는 public 표면은 없다.

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkRequestHandler<TRequest, TReply>` | 요청-응답 handler. 결과를 반환값으로 돌려준다 |
| `IZLinkSendHandler<TMessage>` | 단방향 send handler. `ValueTask` 반환 |
| `IZLinkFanoutHandler<TEvent>` | classic fanout typed event 수신 handler |
| `IZLinkHandlerFilter` | ChannelName request/send 처리(logging/auth/metrics). 인자 없는 `next()`로 다음 filter와 handler를 호출한다 |
| `IZLinkHandlerContext` | 모든 handler context의 공통 베이스. packet name, content type, metadata와 cancellation을 제공하고 ChannelName·MeshName은 전용 context가 제공한다 |

검증: `HandlerContracts.Channel_handlers_and_filters_keep_user_code_behind_typed_contracts`.

### 1.5 codec 등록

```csharp
codecs.Use(ZLinkMessagePackCodec.Default);     // extension codec 등록(extension codec 등록)
codecs.Use(ZLinkProtobufCodec.Default);
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkCodecRegistryBuilder` | framework가 제공하는 codec extension 등록(`Use`)을 담당한다. `options.Codecs`로 접근 |
| `IZLinkMessageCodec` | content type과 message type을 기준으로 업무 객체와 `ZLinkMessage`를 변환하는 codec 계약 |

검증: 목표 계약 구현에서는 codec registry와 `IZLinkMessageCodec` 경계를 다루는 계약 테스트를 함께 고정한다.

### 1.6 웹 백엔드 — gRPC 대체 시나리오

게임 도메인뿐 아니라 일반 웹/마이크로서비스 백엔드에서도 같은 channel messaging
표면이 **gRPC의 대체재**가 된다. 서비스마다 host:port를 알리거나 앞단에 gateway/
로드밸런서를 두지 않고, 논리 `channel name` + location store 자동 연결로 서비스 간
호출을 묶는다.

```csharp
// gRPC unary RPC -> request/response
var placed = await orders
    .RequestToChannel("orders", new PlaceOrder("order-1042", "acct-77", 18742))
    .Timeout(TimeSpan.FromSeconds(2))
    .Async<OrderPlaced>();

// gRPC unary returning Empty -> one-way send (응답 안 기다림)
await orders
    .SendToChannel("inventory", new ReserveStock("order-1042", "sku-9", 3))
    .Async(ct);

// gRPC server-streaming / 상태 피드 -> pub/sub fan-out
await events
    .Publish("order.events", new OrderStatusChanged("order-1042", "Placed"))
    .Async(ct);
```

| gRPC 패턴 | ZLink 대체 | 표면 / 챕터 |
|-----------|------------|-------------|
| Unary RPC | request/response | `IZLinkRouteClient.RequestToChannel(...).Async<TReply>` ([4](05-channel-messaging.ko.md)) |
| Unary `Empty` / one-way send | one-way send | `IZLinkRouteClient.SendToChannel(...).Async` ([4](05-channel-messaging.ko.md)) |
| Server streaming / 이벤트 피드 | pub/sub fan-out | `IZLinkFanoutClient.Publish` + `IZLinkFanoutHandler<T>` ([4](05-channel-messaging.ko.md)) |
| Client/Bidi streaming | STREAM session | `IZLinkSession`/`IZLinkSessionContext` ([9](09-stream.ko.md), §5) |
| Interceptor | handler filter | `IZLinkHandlerFilter` ([4](05-channel-messaging.ko.md) §5, §1.4) |
| Deadline/timeout | request timeout | `IZLinkRequestCall.Timeout(...)` (§1.1) |
| Metadata/trailer | metadata 정책 | `ConfigureMetadata` + `IZLinkMetadataPolicyBuilder` (§2.1, §5.2) |

> proto 컴파일·HTTP/2 전용 인프라·`.proto` IDL 없이, DTO(record)와 typed handler
> 만으로 같은 4가지 호출 형태를 얻는다. gRPC↔ZLink 사용 흐름 비교는
> [05-channel-messaging](05-channel-messaging.ko.md) §0이 더 자세히 다룬다.

검증: `ChannelContracts.Channel_messaging_replaces_grpc_unary_command_and_streaming_for_web_services`.

## 2. Configuration — options · builder · 역할 · 연결

> 사용법은 [02-getting-started](02-getting-started.ko.md),
> [05-channel-messaging](05-channel-messaging.ko.md) §3·§6,
> [06-spot](06-spot.ko.md) §2. 검증 클래스는 `BuilderContracts`,
> `ConnectionAndConfigContracts`.

### 2.1 최상위 옵션 표면

`AddZLinkFramework(options => ...)`의 `options`가 구현하는 표면이다.

```csharp
options.AddHandlersFromAssemblyOf<Program>();
options.ConfigureMetadata()
    .AllowSessionToActor("trace-id")                // session에서 Actor로 전달할 metadata key를 허용한다.
    .AllowActorToSession("trace-id");               // Actor에서 session으로 전달할 metadata key를 허용한다.
var spot = options.AddRouteMesh("play-spots");
spot.Objects().Server().AddActorFactory<PlayerActor, PlayerActorFactory>(
    "player",
    placement: null,
    ZLinkRelocationPolicy<PlayerActor>
        .Snapshot<PlayerActorRelocationAdapter>()); // factory policy에 opaque-state adapter를 연결한다.
options.AddLocationStore(new ZLinkRedisLocationStore(r =>
    .SetConnectionString("redis:6379").SetKeyPrefix("app")));      // 자동 연결·위치 조회의 저장소(§8)
options.AddRelocationStore(new ZLinkRedisRelocationStore(r =>
    .SetConnectionString("redis:6379").SetKeyPrefix("app-relocation"))); // immutable relocation payload 저장소
options.UseFilter<AuditingFilter>();
options.ConfigureDispatch()
    .MessageFlow(ZLinkMessageFlowMode.ErrorsOnly);                // 오류 흐름만 구조화해 기록
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkFrameworkOptions` | framework 최상위 등록 표면. channel/spot/stream node 등록, codec, handler scan, location store, filter와 dispatch 설정을 소유 |
| `IZLinkMeshNodeBuilder` | MeshNode의 `Listen`·`Channel(name).Client()/Server()` 역할과 Object Server 진입점을 소유한다. Actor·Spot relocation adapter는 각 factory의 policy에 연결한다 |
| `IZLinkMetadataPolicyBuilder` | session→Actor와 Actor→session 방향별 metadata key 허용 목록(`AllowSessionToActor`/`AllowActorToSession`) |

검증: `BuilderContracts.Framework_options_register_the_top_level_runtime_surface`.

### 2.2 MeshNode·channel 역할·fanout builder

```csharp
{
    var mesh = options.AddRouteMesh("game")
        .Listen(5000)                              // listen port와 bind host는 서로 분리해 설정한다.
        .SetBindHost("127.0.0.1")
        .SetRoutingId(RoutingId.From("api-a"));
    mesh.PeerConnections.Connect(
        "tcp://127.0.0.1:5001");                  // 다른 MeshNode를 수동 peer로 지정한다.
    mesh.Channel("api")
        .Server()
        .AddRequestHandler<ApiRequestHandler, ApiRequest, ApiReply>();
    mesh.AddRouteRequestHandler<RouteRequestHandler, ApiRequest, ApiReply>();
}

{
    var channel = options.AddFanoutChannel("events")
        .EnablePublisher(5100)
        .SetBindHost("127.0.0.1")
        .SetRoutingId(RoutingId.From("events-a"))
        .EnableSubscriber();                       // location store에서 같은 ChannelName publisher를 자동 발견한다.
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkFanoutChannelBuilder` | fanout channel 역할. `EnablePublisher(port)`와 endpoint 없는 `EnableSubscriber()`는 자동 발견을 사용하고, `ConnectSubscriber(endpoint)`는 수동 연결을 사용한다 |
| `IZLinkMeshNodeBuilder` | MeshName 하나의 endpoint·routing id·manual peer·node direct handler와 ChannelName client/server 역할을 소유 |
| `IZLinkMeshChannelRoleBuilder` | ChannelName에서 `Client()` 또는 `Server()` 역할을 고르는 표면 |
| `IZLinkMeshChannelServerBuilder` | server membership의 weight와 send/request handler namespace를 소유 |
| `IZLinkMeshPeerConnections` | manual peer intent의 연결·해제·목록 조회를 제공 |
| `IZLinkRouteMeshRuntimeOptions` | **런타임** mesh 옵션(DI singleton). `MeshNode(mesh).MaxMessageSize`와 `Channel(channel).Weight`만 serving 중 변경 가능([12-operations §5](12-operations.ko.md)) |

검증: `BuilderContracts.Mesh_and_stream_builders_declare_node_local_roles_and_channel_memberships`.

### 2.3 spot node · stream node · spot mesh builder

```csharp
{
    var stream = options.AddStreamNode("gateway");
    stream.Bind(5400)
        .SetBindHost("127.0.0.1")
        .AddSession<GatewaySession>();

}

{
    var mesh = options.AddRouteMesh("play-mesh");

    mesh.Listen(5501)                                        // MeshNode의 유일한 router 소켓 bind
        .SetBindHost("127.0.0.1");
    mesh.SetRoutingId(RoutingId.From("spot-router"));        // 이 노드의 routing id

    mesh.Channel("play-mesh").Server().SetWeight(100);       // server membership + select-one 가중치
    mesh.ConfigureSpotPublisher().SendHighWaterMark = 1024;  // publish 측 전송 옵션
    mesh.ConfigureRouterSocket().SendHighWaterMark = 512;    // HWM·timeout — startup-only

    mesh.SetEntrySpotRoutingId(RoutingId.From("entry"));
    mesh.AddSpotFactory<RoomSpot>();   // user Spot: 요청마다 동적 생성
    mesh.AddEntrySpot<EntrySpot>();    // Entry Spot: MeshNode 당 단일 진입점

    mesh.PeerConnections.Connect(     // store 없는 수동 peering (store가 있으면 자동)
        RoutingId.From("play-b"), "tcp://127.0.0.1:5601");
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkStreamNodeBuilder` | stream node(`Bind(port)`, `SetBindHost`, `SetAdvertiseHost`, `SetTlsServer`, `AddSession<TSession>`, `EnableActorDispatch()`) |
| `IZLinkMeshNodeBuilder` | MeshNode 등록 표면(`Listen(port)`·`SetBindHost`·`SetAdvertiseHost`·`Channel`·routing id·`PeerConnections`·entry/spot/actor factory) |
| `IZLinkMeshChannelRoleBuilder` | `Client()`와 `Server()` 중 ChannelName 역할 하나를 선택하는 표면 |
| `IZLinkMeshChannelServerBuilder` | server membership의 `SetWeight`와 send/request handler 등록 표면 |
| `IZLinkMeshPeerConnections` | 수동 peering 집합(`Connect(endpoint)`·`Connect(rid, endpoint)`·`Disconnect`·`ListConnections`) |

검증: `BuilderContracts.Mesh_and_stream_builders_declare_node_local_roles_and_channel_memberships`.

### 2.4 연결 집합 — 기능별 endpoint 소유

수동 연결은 MeshNode peer와 fanout subscriber가 각각 소유한다. 두 기능에 같은 endpoint가
필요하더라도 한쪽의 연결 설정을 다른 쪽이 공유하지 않는다.

```csharp
// 각 호출은 fanout subscriber와 MeshNode peer의 독립 연결 의도를 기록한다.
subscriber.ConnectSubscriber("tcp://127.0.0.1:5002");
mesh.PeerConnections.Connect("tcp://127.0.0.1:5003");
```

검증: `BuilderContracts.Mesh_and_stream_builders_declare_node_local_roles_and_channel_memberships`.

### 2.5 socket · routing · spot · dispatch 설정

```csharp
// config 객체는 직접 만들지 않는다. MeshNode builder의 접근자가 돌려준다.
var node = options.AddRouteMesh("game.stage");
IZLinkMeshNodeSocketConfig router = node.ConfigureRouterSocket();  // HWM·timeout은 startup-only
router.ReceiveHighWaterMark = 64;
IZLinkSpotPublisherConfig publisher = node.ConfigureSpotPublisher();
publisher.SendHighWaterMark = 32;
node.ConfigureEntrySpot().RoutingId = RoutingId.From("entry");

var dispatch = options.ConfigureDispatch();
dispatch.MessageFlow(ZLinkMessageFlowMode.ErrorsOnly)                 // 오류 흐름만 기록
    .SetMessageFlowObserver<FlowObserver>()                           // immutable flow event 관측
    .SetRuntimeErrorSink<RuntimeErrorSink>();                         // observer 실패 등 runtime 오류 관측
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkMeshNodeSocketConfig` | MeshNode의 시작 전 socket 옵션(`MaxMessageSize`, HWM, receive/send timeout) |
| `IZLinkSpotPublisherConfig` | spot publisher 옵션(`SendHighWaterMark`, `SendTimeout`, `Linger`) |
| `IZLinkEntrySpotOptions` | Entry Spot의 routing id 지정 |
| `IZLinkDispatchOptions` | message-flow observer, runtime error sink와 flow 기록 mode 설정 |
| `IZLinkMessageFlowObserver` | 메시지 생애주기 이벤트 수신(`OnMessageFlowAsync`) — `ConfigureDispatch().SetMessageFlowObserver<T>()`로 등록([11-monitoring §5](11-monitoring.ko.md)) |
| `IZLinkRuntimeErrorSink` | observer 실패처럼 업무 handler 밖에서 발생한 runtime 오류 수신 |
| `IZLinkMessageFlowRuntime` | 실행 중 flow mode 조회·변경과 bounded flow event stream 관측 |
| `IZLinkWorkerOptions` | worker pool 설정(`MinThreads`, `MaxThreads`, `IdleTimeout`, `MaxQueueLength`) |
| `IZLinkWorkerCall<TResult>` | spot context의 `RunWorker(...)` 결과 종결자(`Timeout`, `Submit`, `Async`, `Yield`) |

검증: `ConnectionAndConfigContracts.Configuration_contracts_keep_socket_routing_spot_and_dispatch_options_typed`.

## 3. SPOT — spot · context · client · handler

> 사용법은 [06-spot](06-spot.ko.md). 검증 클래스는 `SpotContracts`.

### 3.1 spot 인스턴스와 context

```csharp
public sealed class RoomSpot(IZLinkSpotContext context) : IZLinkSpot<PlayerActor>
{
    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddPacket<RoomPacketHandler>();
        Context.Handlers.AddSubscribe<RoomEventHandler>(
            "room.events",                       // event를 받을 ChannelName이다.
            "room.updated");                    // exact topic을 함께 등록한다.
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken ct)
    {
        var join = request.Decode<JoinRoom>();
        // join admission 결정 지점 — Accept/Reject가 여기서 정해진다(handler 등록이 아니다).
        return ValueTask.FromResult(
            ZLinkSpotActorJoinResult.Accept(new JoinedRoom(join.RoomId)));
    }

    public ValueTask OnJoinedActorAsync(
        PlayerActor actor,
        CancellationToken ct)
        => ValueTask.CompletedTask;

    public ValueTask OnLeaveActorAsync(
        PlayerActor actor,
        CancellationToken ct)
        => ValueTask.CompletedTask;

    public ValueTask OnDisconnectActorAsync(
        PlayerActor actor,
        CancellationToken ct)
        => ValueTask.CompletedTask;

    // timer 등록·outbound 호출은 lifecycle(OnInitializeAsync)에 진입한 뒤에야 안전하다.
    public async ValueTask OnInitializeAsync(CancellationToken ct)
    {
        await Context.AddTimer<RoomTimerHandler>("heartbeat", TimeSpan.FromSeconds(1));

        // 다른 spot으로 보내려면 SpotHandle가 필요하다 — resolve 한 번으로 얻어 보관해 두고 쓴다(§3.2).
        if (await spots.ResolveSpotHandleAsync(
                Context.MeshName,
                RoutingId.From("room-2"),
                ct) is { } room2)
        {
            await Context.Outbound
                .SendToSpot(room2, new RoomEvent("opened"))
                .Async(ct);                                                                     // send: reply 없음
            await Context.Outbound.RequestToSpot(room2, new JoinRoom("room-2")).Async<JoinedRoom>(ct); // request: reply 대기
        }

        await Context.Outbound
            .Publish("play-events", "room.events", new RoomEvent("opened"))
            .Async(ct);
        await Context.Outbound
            .SendToChannel("api", new RoomEvent("opened"))
            .Async(ct);
        await Context.Outbound.RequestToChannel("api", new JoinRoom("room-1")).Async<JoinedRoom>(ct);
    }
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpot` | user spot 인스턴스. `Context` + lifecycle(`Configure`/`OnCreateAsync`/`OnInitializeAsync`/`OnClosingAsync`) |
| `IZLinkSpot<TActor>` | 지정한 Actor type을 받는 user spot. `OnActorJoinAsync`/`OnJoinedActorAsync`/`OnLeaveActorAsync`/`OnDisconnectActorAsync` lifecycle을 추가한다 |
| `IZLinkEntrySpot` | Entry Spot 인스턴스. `IZLinkEntrySpotContext` + lifecycle |
| `IZLinkEntrySpot<TActor>` | Actor를 받는 Entry Spot. `OnCreateActorAsync`/`OnJoinedActorAsync`/`OnLeaveActorAsync`/`OnDisconnectActorAsync` lifecycle을 추가한다 |
| `IZLinkSpotContext` | User Spot context. handler registry + outbound + `MeshName`/`SpotId`/`NodeRid` + `LeaveActorAsync` + `CloseAsync` + `AddTimer` + `RunWorker` |
| `IZLinkEntrySpotContext` | Entry Spot context. handler registry + outbound + `MeshName`/`SpotId`/`NodeRid` + `DestroyActorAsync` + `AddTimer` + `RunWorker` |
| `IZLinkSpotHandlerRegistry` | spot packet과 subscription handler를 등록한다(`AddPacket`, `AddSubscribe`) |
| `IZLinkSpotOutbound` | spot 안 outbound. `SendToSpot(SpotHandle, msg)`/`RequestToSpot(SpotHandle, req)`(§3.2) + `Publish(channelName, topic, msg)`/`SendToChannel`/`RequestToChannel` |
| `IZLinkTimer` | 등록된 timer 핸들. `CancelAsync()` / `DisposeAsync()` (§8도 참조) |

검증: `SpotContracts.Spot_context_registers_handlers_timers_actor_lifecycle_and_outbound_messages`.

### 3.2 spot outbound — local · routed · publisher

```csharp
// ① SpotHandle를 한 번 resolve 해서 보관 — 내부 주소 갱신은 framework가 담당
SpotHandle room1 = await spots.ResolveSpotHandleAsync(
                           "game.room",
                           RoutingId.From("room-1"))
                         ?? throw new InvalidOperationException("room-1이 아직 없다");

// ② current Spot callback 안에서 — 보관한 SpotHandle로 send/request (IZLinkSpotOutbound)
await spot.Context.Outbound.SendToSpot(room1, new RoomEvent("opened")).Async(ct);
var reply = await spot.Context.Outbound
    .RequestToSpot(room1, new JoinRoom("room-1"))
    .Async<JoinedRoom>();

// ③ spot 밖(일반 코드)에서 — route mesh channel 이름 + SpotHandle (IZLinkRouteClient)
await routes.RequestToSpot(room1, new JoinRoom("room-1")).Async<JoinedRoom>();

// local spot 없는 프로세스에서 publish (topic은 SpotHandle가 필요 없다)
await publisher.Publish("play-events", "room.events", new RoomEvent("opened")).Async(ct); // IZLinkSpotPublisherClient
```

framework는 위치 event와 주기적 조회로 `SpotHandle`의 내부 주소를 갱신한다. request 도중
주소가 무효화되면 안전한 경우에 한해 한 번 갱신하고 재전송한다. send는 중복 전달 가능성 때문에
자동 재전송하지 않으며, local 제출 실패는 monitoring 오류 경로에서 관측한다
([공통 스펙: spot 주소 메시징](../../common/spec/24-spot-address-messaging.ko.md)).

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpotManager` | spot 인스턴스 생성/조회/종료(`CreateAsync`, `GetOrCreateAsync`, `ResolveAsync`, `ListAsync`, `DestroyAsync`) |
| `IZLinkSpotOutbound` | current Spot callback 안에서의 outbound(`SendToSpot(SpotHandle, …)`/`RequestToSpot(SpotHandle, …)`/`SendToChannel`/`RequestToChannel`/`Publish`) |
| `IZLinkSpotPublisherClient` | local spot 없는 프로세스의 Logical Multicast publish(`Publish(channelName, topic, msg)`) |
| `IZLinkSpotHandleResolver` | MeshName + spot rid → `SpotHandle` 조회(`ResolveSpotHandleAsync`) — location store를 읽는 기본 메시징 조회 |
| `IZLinkActorSpotHandleResolver` | MeshName + actor id → actor가 있는 spot의 `SpotHandle` 조회(`ResolveActorSpotHandleAsync`) |

검증: `SpotContracts.Spot_clients_separate_local_spot_api_routed_egress_and_publisher_channels`.

### 3.3 spot handler — spot/actor 인스턴스를 첫 인자로 받는다

```csharp
public sealed class RoomRequestHandler
    : IZLinkSpotRequestHandler<RoomSpot, JoinRoom, JoinedRoom>
{
    public ValueTask<JoinedRoom> HandleAsync(RoomSpot spot, JoinRoom request, CancellationToken ct)  // 첫 인자 = spot 인스턴스(이 절의 핵심)
        => ValueTask.FromResult(new JoinedRoom(request.RoomId));
}

// actor join admission은 handler 등록이 아니라 RoomSpot.OnActorJoinAsync(...)에 둔다.
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpotPacketHandler<TSpot, TMessage>` | spot 단방향 packet handler. 첫 인자 = spot |
| `IZLinkSpotRequestHandler<TSpot, TRequest, TReply>` | spot 요청 handler. 반환값이 응답 |
| `IZLinkSpotSubscriptionHandler<TSpot, TMessage>` | spot 구독 topic 수신 handler |
| `IZLinkSpotTimerHandler<TSpot>` | spot timer tick handler(`ZLinkTimerTick`) |
| `IZLinkSpotActorLifecycle<TActor>.OnActorJoinAsync(...)` | user Spot과 Entry Spot이 함께 사용하는 join admission callback. Actor ID와 요청 payload를 받아 승인 여부와 선택 reply를 반환한다 |
| `IZLinkSpotActorLifecycle<TActor>.OnJoinedActorAsync(...)` | join commit 이후 생성된 `TActor` 인스턴스를 받는 lifecycle callback |
| `IZLinkSpotActorLifecycle<TActor>.OnLeaveActorAsync(...)` | membership을 제거할 때 `TActor` 인스턴스를 받는 lifecycle callback |
| `IZLinkSpotActorLifecycle<TActor>.OnDisconnectActorAsync(...)` | 연결 단절 시 `TActor` 인스턴스를 받는 선택 callback이며 기본 구현은 아무 작업도 하지 않는다 |
| `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>` | User Spot에 속한 Actor 단방향 handler. spot, actor, message context, payload 순서로 받는다 |
| `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` | User Spot에 속한 Actor 요청 handler. spot, actor, message context, request 순서로 받고 reply를 반환한다 |

검증: `SpotContracts.Spot_handlers_receive_the_spot_instance_and_actor_when_the_contract_requires_it`.

## 4. Actor — actor · context · factory · handler

> 사용법은 [07-actor-spot](07-actor-spot.ko.md). 검증 클래스는 `ActorContracts`.

### 4.1 actor와 lifecycle

```csharp
ActorRef actor = await manager.CreateAsync(
    "game.room", "player", "player-1", new CreatePlayer("player-1"), ct); // 새 Actor를 명시적으로 생성한다.
ActorRef resolved = await manager.ResolveAsync(
    "game.room", "player-1", ct);                                         // 기존 Actor의 현재 ref를 조회한다.
// Spot 밖 public handler는 ActorRef를 보관하거나 session bind에 넘긴다.
// room join 같은 admission은 Entry Spot actor request handler에서 처리한다.
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkActor` | ID로 식별되는 상태 보유 actor. `ActorId`, `Context` |
| `IZLinkActorContext` | Actor의 상태와 동작 표면. `SpotId?`, `BoundSession`, `JoinSpot`, `JoinEntrySpot`을 제공한다. |
| `IZLinkActorJoinSpotCall` | User Spot join intent를 현재 handler에 등록하는 종결자(`Timeout` → `Defer`). |
| `IZLinkActorJoinEntrySpotCall` | Entry Spot join intent를 현재 handler에 등록하는 종결자(`Timeout` → `Defer`). |
| `IZLinkActorFactory` | `actorType` 별 actor 생성(`CreateAsync(actorId, context, ct)`) |
| `IZLinkActorRelocationAdapter<TActor>` | `Snapshot` relocation에서 actor state를 opaque bytes로 capture하고 factory가 만든 target actor에 restore한다 |
| `IZLinkActorManager` | MeshName을 명시하는 actor 생성·조회·삭제(`CreateAsync`, `ResolveAsync`, `DestroyAsync`) |

`RestoreAsync(actor, payload, ct)`의 `payload`는 source node의
`CaptureAsync(actor, ct)`가 반환한 byte 배열이다. Framework는 payload를 opaque하게 복사·저장하며 state
contract ID, state type이나 relocation codec을 제공하지 않는다. Target factory가 만든 actor instance에
restore한 뒤 owner·membership을 commit한다. 전체 흐름과 예제는
[07-actor-spot §3](07-actor-spot.ko.md#restoreasync의-payload는-어디서-오는가)을 참고한다.

검증: `ActorContracts.Actor_context_creates_actors_and_joins_a_spot_by_routing_id`.

### 4.2 Actor handler

```csharp
// Actor handler는 현재 Actor와 전용 context, payload를 받는다.
// Spot membership lifecycle은 IZLinkSpotActorLifecycle<TActor>가 별도로 소유한다.
public sealed class RoomRequestHandler
    : IZLinkSpotActorRequestHandler<RoomSpot, PlayerActor, PlayerRequest, PlayerReply>
{
    public ValueTask<PlayerReply> HandleAsync(
        RoomSpot spot,
        PlayerActor actor,
        IZLinkMessageContext context,
        PlayerRequest request,
        CancellationToken ct)
    {
        return ValueTask.FromResult(
            new PlayerReply($"actor:{actor.ActorId}:{request.Value}")); // 반환값이 request reply다.
    }
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>` | User Spot에 속한 Actor 단방향 handler. spot, actor, `IZLinkMessageContext`, payload 순서 |
| `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` | User Spot에 속한 Actor 요청 handler. spot, actor, `IZLinkMessageContext`, request 순서이며 반환값이 reply다 |

검증: 목표 계약 구현에서는 Actor handler가 Actor instance와 전용 context를 받는 시그니처를 고정한다.

## 5. STREAM session — session · context · push · bound session

> 사용법은 [09-stream](09-stream.ko.md), [08-actor-session](08-actor-session.ko.md) §3.
> 검증 클래스는 `StreamContracts`.

### 5.1 session과 session context

```csharp
public sealed class ClientHeaderSession(
    IZLinkSessionContext context,
    IZLinkActorManager actors) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    // payload는 framework ZLinkMessage 다. decode 하거나 framework API에 그대로 넘긴다.
    public async ValueTask OnDispatchAsync(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload, CancellationToken ct)
    {
        ActorRef actor = await actors.ResolveAsync("game.room", "player-1", ct);
        var actorRef = await context.Actors.BindAsync(actor, ct); // session↔actor binding 성립(재접속 이전성의 근거)
        if (context.Actors.Find(actorRef.ActorId) is { } boundActor)
        {
            await boundActor.RelayAsync(payload, ct);   // framework ZLinkMessage를 그대로 넘긴다
        }

        await context.Client
            .Send(new PlayerJoined("player-1"))       // send call만 metadata를 추가할 수 있다.
            .Metadata("trace-id", "abc")
            .Compress()
            .Async(ct);
        await context.Client
            .Reply(new AuthenticateReply("player-1")) // reply call은 metadata 없이 compression만 설정한다.
            .Compress()
            .Async(ct);
        await context.CloseAsync();                             // Lifecycle
    }

    public ValueTask OnConnectedAsync(CancellationToken ct) => ValueTask.CompletedTask;
    public ValueTask OnDisconnectedAsync(CancellationToken ct) => ValueTask.CompletedTask;
    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken ct) => ValueTask.CompletedTask;
}
```

`OnDispatchAsync(...)`로 받은 `payload`는 framework `ZLinkMessage` 다.
session은 이 값을 decode 하거나 `IZLinkSessionActor.RelayAsync(...)`에 그대로 넘긴다.

session 전용 packet handler를 나누고 싶을 때는
session의 `Configure()`에서 `Context.Handlers.AddHandler<THandler>()`로 handler class를
등록한다. handler는 `IZLinkSessionPacketHandler<TSessionContext, TMessage>`를 구현하고,
framework는 `TMessage`를 decode 해서 넘긴다. 미등록 packet을 actor로 relay 할지,
거절할지, 로그만 남길지는 application session이 결정한다.

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSession` | stream session. `Context` + `OnConnectedAsync`/`OnDisconnectedAsync`/`OnErrorAsync` + 기본 구현 `OnDispatchAsync` |
| `IZLinkSessionContext` | session 식별값과 `Client`, `Actors`, `Handlers`, `CloseAsync`를 가진 root context |
| `IZLinkSessionClient` | client로의 push(`Send(msg)`) / 요청 응답(`Reply(msg)`) |
| `IZLinkSessionActors` | actor binding/lookup(`Bound`, `BindAsync(ActorRef, ...)`, `Find`) |
| `IZLinkSessionActor` | session-bound actor handle. `RelayAsync`, `NotifyDisconnectedAsync`로 대상 actor에게 명시 동작을 보냄 |
| `IZLinkSessionHandlerRegistry` | session `Configure()`에서 handler class를 등록하고, dispatch 때 등록된 handler만 호출 |
| `IZLinkSessionPacketHandler<TSessionContext, TMessage>` | session이 직접 처리할 typed packet handler |
| `IZLinkSessionSendCall` | session push 종결자. 선택적으로 `Metadata`와 `Compress`를 적용한 뒤 `Async(ct)`로 끝낸다 |
| `IZLinkSessionReplyCall` | session reply 종결자. 선택적으로 `Compress`를 적용한 뒤 `Async(ct)`로 끝내며 metadata를 받지 않는다 |

검증: `StreamContracts.Session_context_collects_identity_stream_and_actor_operations`.

### 5.2 bound session — actor가 자기 client로 push

```csharp
// bound session은 단방향 push만 한다(request 표면 없음 → reply를 받지 않는다).
await boundSession
    .Send(new PlayerJoined("player-1"))        // 허용 목록에 있는 metadata를 client push에 포함한다.
    .Metadata("trace-id", "abc")
    .Async(ct);
await boundSession.DisconnectAsync();

ZLinkMessageMetadata metadata = /* ... */;
var traceId = metadata.Find("trace-id");        // 없으면 null
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkBoundSession` | actor에 묶인 client로의 단방향 push. `Send<TMessage>(message)` + `DisconnectAsync`. (요청 표면 없음 — push는 단방향) |
| `IZLinkBoundSessionSendCall` | bound session push 종결자. 선택적으로 `Metadata`를 적용한 뒤 `Async(ct)`로 끝낸다 |

검증: `StreamContracts.Bound_session_sends_to_the_bound_session_without_exposing_stream_transport`.

## 6. Monitoring — source 등록과 runtime 관측

> 사용법은 [11-monitoring](11-monitoring.ko.md). 검증 클래스는 `EventingContracts`.

```csharp
options.AddSocketEvents("router", ZLinkSocketEventKind.Connected); // 인자 = (source 이름, 받을 event kind)
options.AddSpotEvents("spot-node", TimeSpan.FromSeconds(1));
options.AddLocationRuntimeEvents("location-runtime", TimeSpan.FromSeconds(1)); // store 등록 배포

var flow = app.Services.GetRequiredService<IZLinkMessageFlowRuntime>();
await foreach (ZLinkMessageFlowEvent @event in flow.ObserveAsync(cancellationToken: ct))
{
    // bounded stream에서 immutable message-flow event를 관측한다.
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkMonitoringOptions` | monitoring source 등록. socket(`AddSocketEvents`)·spot(`AddSpotEvents`)·location projection(`AddLocationRuntimeEvents`)·location row 이벤트(`AddLocationPeerEvents`/`AddLocationSpotEvents`/`AddLocationActorEvents`/`AddLocationRouteEvents`). `AddZLinkMonitoring(...)`의 표면 |
| `IZLinkMessageFlowRuntime` | 현재 flow mode를 바꾸고 bounded `ZLinkMessageFlowEvent` stream을 관측하는 singleton |
| `IZLinkMessageFlowObserver` | dispatch 시점의 immutable flow event를 받는 observer |
| `IZLinkRuntimeErrorSink` | observer 실패를 포함한 framework runtime 오류를 받는 sink |

검증: 목표 계약 구현에서는 monitoring source 등록과 bounded runtime 관측을 각각 계약 테스트로 고정한다.

### 6.1 운영 — 메트릭 · flow 상관관계 · drain

> 사용법은 [12-operations](12-operations.ko.md). 자세한 계약은
> [spec/aspnet-core-monitoring §10~§12](../../common/spec/server/languages/dotnet/01-system-structure.ko.md).

```csharp
builder.Services.AddOpenTelemetry().WithMetrics(m =>
    m.AddMeter("zlink.framework")); // Framework가 계기를 방출하는 정식 meter 이름이다.

var runtime = app.Services.GetRequiredService<IZLinkRouteMeshRuntime>();

await foreach (ZLinkMeshRuntimeEvent meshEvent in
    runtime.ObserveAsync("game.room", cancellationToken: ct))
{
    // 같은 MeshNode의 component 상태 전이를 Sequence 순서로 관측한다.
}

var host = app.Services.GetRequiredService<IZLinkFrameworkRuntime>();
ZLinkFrameworkTerminationResult termination =
    await host.RetireAsync(TimeSpan.FromSeconds(30), ct); // host 전체 continuity 이전을 시작한다.
```

Host `Retire`는 별도 MeshNode 정책을 받지 않는다. Current turn을 끝낸 ready unit만 permit을 얻어 seal하며,
미실행 queue·journal·timer를 target에 복원한다. User Spot과 member Actor는 하나의 aggregate로 relocation하고
Entry member Actor maintenance는 target `OnActorRelocatedAsync`를 사용한다. `Shutdown`은 새 relocation 없이
local resource를 bounded cleanup한다.

| 계약 | 역할 |
|------|------|
| `System.Diagnostics.Metrics` | meter 이름 `"zlink.framework"`를 구독한다. 별도 zlink 메트릭 인터페이스는 없다 |
| `IZLinkRouteMeshRuntime` | MeshName별 component snapshot·readiness·event를 관측하는 DI singleton |
| `IZLinkFrameworkRuntime` | host state·snapshot·event와 `RetireAsync`·`ShutdownAsync`를 제공하는 DI singleton |
| `ZLinkFrameworkTerminationResult` | effective intent, `Stopped`·`Blocked`·`ForceStopped` outcome과 닫힌 reason을 제공하는 host terminal result |
| `ZLinkMeshRuntimeEvent` | MeshName과 sequence를 포함하는 MeshNode 상태 전이 event. `ObserveAsync(meshName, ...)`의 bounded stream으로 관측한다 |
| `ZLinkMessageFlowEvent` | `FlowOrigin`은 `inbound`/`timer`/`application`/`lifecycle` 중 하나이거나 값이 없는 문자열 필드다 |

## 7. Timer — 등록된 timer 핸들

> 사용법은 [06-spot](06-spot.ko.md) §3. 검증 클래스는 `TimerContracts`.

```csharp
// 인자 = (timer 이름, 주기). 반환 핸들을 await using/CancelAsync로 정리해야 timer 누수가 없다.
await using var timer = await Context.AddTimer<RoomTimerHandler>("heartbeat", TimeSpan.FromSeconds(1));
await timer.CancelAsync();   // IZLinkTimer
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkTimer` | `AddTimer<THandler>(...)`가 돌려주는 timer 핸들. `IsDisposed`, `CancelAsync()`, `IAsyncDisposable`로 정리 |

검증: `TimerContracts.Timer_contract_allows_spot_code_to_cancel_a_registered_timer`.

## 8. Location — store 등록 · 위치 조회 · 운영 조회

> 사용법은 [10-location](10-location.ko.md), 계약은
> [공통 스펙](../../common/spec/40-location-runtime.ko.md). 검증 클래스는 `LocationContractTests`.

```csharp
// Authority와 immutable relocation payload는 별도 public capability로 등록한다.
options.AddLocationStore(new ZLinkRedisLocationStore(r =>
    .SetConnectionString("redis:6379").SetKeyPrefix("app-location")));
options.AddRelocationStore(new ZLinkRedisRelocationStore(r =>
    .SetConnectionString("redis:6379").SetKeyPrefix("app-relocation")));

var loc = options.ConfigureLocations();
loc.OwnerLeaseTtl = TimeSpan.FromSeconds(15);

ActorRef? actor = await actorManager.FindAsync("player-1", ct); // current Ready Actor ref만 조회한다.
SpotRef? spot = await spotManager.FindAsync(spotId, ct);        // current Ready User Spot ref만 조회한다.
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkLocationStore` | descriptor, owner lease, Actor·Spot authority·membership, relocation phase와 reference를 expected-version CAS로 관리한다 |
| `IZLinkRelocationStore` | application bytes, 실행하지 않은 queue·journal·timer와 replay payload를 immutable root로 저장한다 |
| `ZLinkRedisLocationStore` | 공식 Redis Location Store 구현. Relocation Store와 별도 class·key prefix를 사용한다 |
| `ZLinkRedisRelocationStore` | 공식 Redis Relocation Store 구현. 같은 Redis deployment 또는 별도 Redis를 사용할 수 있다 |
| `IZLinkActorManager` / `IZLinkSpotManager` | global ID로 current Ready ref를 조회한다. Manager create는 Actor와 User Spot만 대상으로 한다 |

두 Store 사이의 distributed transaction은 요구하지 않는다. Relocation root를 먼저 저장하고 Location Store의
CAS가 reference와 checksum을 publish해야 target이 payload를 복원 근거로 사용한다. Public manager 조회는
유효한 owner lease와 `Ready` authority만 반환한다.

검증: `LocationContractTests` (store 계약 — in-memory와 Redis가 같은 시나리오를 통과).

## 9. 완전성 — 카탈로그가 곧 계약 표면

| 영역(namespace) | 검증 클래스 | 시나리오 수 |
|-----------------|-------------|:-----------:|
| `Channels` | `ChannelContracts` | 4 |
| `Handlers` | `HandlerContracts` | 2 |
| `Codecs` | `CodecContracts` | 2 |
| `Configuration` | `BuilderContracts` · `ConnectionAndConfigContracts` | 4 |
| `Spots` | `SpotContracts` | 6 |
| `Workers` | `SpotContracts`(`IZLinkWorkerCall<>`·`IZLinkWorkerOptions`) | (위 Spots에 포함) |
| `Actors` | `ActorContracts` | 1 |
| `Streams` | `StreamContracts` | 3 |
| `Eventing` | `EventingContracts` | 1 |
| `Timers` | `TimerContracts` | 1 |
| `Dispatch` | `ConnectionAndConfigContracts`(`IZLinkDispatchOptions`) | (위 4에 포함) |

10.0.0 구현 완료 gate에서는 위 시나리오의 `[ContractExample(...)]` 합집합이
`Zlink.Framework.Contracts.*`의 public interface 집합과 같은지 검사한다. 현재 source/package가
아직 목표 계약에 도달하지 않은 차이는 [구현 차이](../../common/spec/90-implementation-gap.ko.md)가 소유한다.
Exact interface를 바꾸면 목표 계약 테스트와 이 카탈로그를 함께 갱신한다.

> client 측 Stream Connector(`Systems.Zlink.Stream.Connector`)의 `Zlink*` 타입은
> 별도 client 라이브러리라 이 카탈로그(서버 framework 계약)에 포함되지 않는다.
> connector 표면은 [09-stream](09-stream.ko.md) §2와
> [Stream Connector 공개 계약](../../common/spec/stream-connector/languages/dotnet/03-stream-connector.ko.md)이 다룬다.

## 10. 더 보기

- 언어 중립 정식 정의: [spec/handler-interfaces](../../common/spec/server/languages/dotnet/02-handler-interfaces.ko.md)
- 기능 선택 지도: [04-feature-map](04-feature-map.ko.md)
- 계약 테스트 소스: `framework/languages/dotnet/tests/Zlink.Framework.ContractTests`

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: 운영 — 메트릭 · drain · readiness](12-operations.ko.md) | [다음: ZLink를 어디에 쓰나](14-grpc-alternative.ko.md)
<!-- framework-adapter-nav:bottom:end -->
