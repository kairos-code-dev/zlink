<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: 운영 — 메트릭 · drain · readiness](12-operations.ko.md) | [다음: ZLink를 어디에 쓰나](14-grpc-alternative.ko.md)
<!-- framework-adapter-nav:end -->

# 13. 인터페이스 카탈로그 — 모든 계약을 코드로

> 이 챕터는 framework adapter가 노출하는 **모든 public 계약 인터페이스**를 한곳에
> 모아, 각 인터페이스가 실제로 어떻게 쓰이는지 코드와 함께 보여 준다. 개념·사용
> 흐름은 앞 챕터(05~12)가, 언어 중립 정식 정의는
> [spec/handler-interfaces](../../spec/server/languages/dotnet/02-handler-interfaces.ko.md)가 다룬다. 이 문서는
> 그 둘을 잇는 **실행 검증된 예문 색인**이다.

## 0. 이 카탈로그를 읽는 법

이 문서의 모든 코드 조각은 추측이 아니라
`tests/Zlink.Framework.ContractTests`의 **컴파일·실행되는 계약 테스트**에서
가져온 것이다. 각 절은 그 테스트 클래스/메서드 하나에 대응한다.

- **완전성 보장.** `ContractSurfaceCoverage.Every_public_contract_interface_has_a_scenario_example`
  테스트가 `Zlink.Framework.Contracts.*`의 모든 public interface가 적어도 하나의
  `[ContractExample(typeof(...))]` 시나리오에 등장하는지 단언한다. 즉 이 카탈로그가
  비면 빌드가 깨진다. 인터페이스가 새로 생기면 테스트와 이 문서가 함께 따라온다.
- **예문의 도메인.** 계약 테스트는 게임 서버 도메인(`room`/`player`/`authenticate`,
  `RoomEvent`, `JoinRoom`/`JoinedRoom`)으로 통일돼 있다. 등록(`AddZLinkFramework`)
  코드 흐름은 앞 챕터의 실사용 예와 같고, 여기서는 **인터페이스 표면(메서드
  시그니처·fluent chain)** 에 집중한다.
- **표기.** 서버 framework 타입은 전부 `ZLink`(대문자 L)다. 표 안에서 제네릭
  인자는 `IZLinkSpotPacketHandler<TSpot, TMessage>`처럼 풀어 적되, 본문 코드는
  실제 구현 형태를 보인다.
- **비동기 이름 규칙.** 공통 의미는
  [비동기 실행과 coroutine 정책](../../spec/04-async-execution-policy.ko.md)을
  따른다. `.NET`에서는 `Task`, `ValueTask`, `Task<T>`, `ValueTask<T>`를 반환하는
  공개 호출 종결자가 `Async(...)`로 끝난다. awaitable을 반환하지 않는
  callback request 표면만 `Submit(callback)` 이름을 유지한다.

각 절 끝의 `검증` 줄은 해당 표면을 단언하는 계약 테스트 메서드를 가리킨다.

## 1. Channel messaging — request · send · pub/sub

> 사용법은 [05-channel-messaging](05-channel-messaging.ko.md). 검증 클래스는
> `ChannelContracts`, `HandlerContracts`, `CodecContracts`.

### 1.1 outbound client와 종결 호출

```csharp
// IZLinkChannelClient를 주입받아 channel 이름으로 send/request 한다.
client
    .SendToChannel("api", new AuthenticateRequest("player-1"))   // IZLinkSendCall
    .Submit();

var reply = await client
    .RequestToChannel("api", new AuthenticateRequest("player-1")) // IZLinkRequestCall
    .Timeout(TimeSpan.FromSeconds(3))     // request만 가진 종결자 — reply 대기 상한(send 엔 없다)
    .Async<AuthenticateReply>();          // reply 타입은 호출이 아니라 종결자에서 지정
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkChannelClient` | `SendToChannel(channelName, msg)` / `RequestToChannel(channelName, req)`의 기본 표면. client-server channel 호출의 뿌리 |
| `IZLinkSendCall` | send 종결자 — `Submit(ct)` 하나. 응답을 기다리지 않으므로 `Timeout` 없음 |
| `IZLinkRequestCall` | request 종결자. 선택 `Timeout(...)` 후 `Async<TReply>(ct)`. reply 타입은 종결자에서 지정 |

검증: `ChannelContracts.Channel_client_sends_and_requests_by_channel_name`.

### 1.2 route client — router channel 너머 target node 호출

```csharp
var target = RoutingId.From("play-node-1");   // router channel 너머 최종 대상 노드(§1.1의 channel-name 호출과 다른 점)

client
    .SendToNode("play-router", target, new RoomEvent("opened")) // 인자 = (router channel, target node, payload)
    .Submit();

var room = await client
    .RequestToNode("play-router", target, new AllocateRoom("alice")) // IZLinkRequestCall
    .Timeout(TimeSpan.FromSeconds(2))
    .Async<RoomAllocated>();
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkRouteClient` | router channel 이름과 target node로 보내거나, 논리적 `SpotHandle` 대상으로 보내는 client. 노드는 `SendToNode(channel, nodeRid, …)`/`RequestToNode(channel, nodeRid, …)`, spot은 `SendToSpot(handle, …)`/`RequestToSpot(handle, …)`을 쓴다(§3.2). |
| `IZLinkSendCall` | route send 종결자(`Submit(ct)`) |
| `IZLinkRequestCall` | route request 종결자(`Timeout` · `Async<TReply>`) |
| `IZLinkRouteSendHandler<TMessage>` | route mesh channel의 단방향 수신 handler. `HandleAsync(msg, ZLinkRouteSendContext, ct)` |
| `IZLinkRouteRequestHandler<TRequest, TReply>` | route mesh channel의 요청 수신 handler. `HandleAsync(req, ZLinkRouteRequestContext, ct) → ValueTask<TReply>` |

검증: `ChannelContracts.Route_client_addresses_a_target_node_through_a_router_channel`.

### 1.3 fanout publisher

```csharp
publisher
    .Publish("events", "room.opened", new RoomEvent("opened")) // 인자 = (channel, topic, message). topic이 fan-out 라우팅 키.
    .Submit();
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkFanoutClient` | pub/sub publish 표면. `Publish(channelName, topic, message)` (인자 3개) |
| `IZLinkPublishCall` | publish 종결자 — `Submit(ct)` 하나 |

검증: `ChannelContracts.Fanout_client_publishes_events_to_a_topic`.

### 1.4 handler와 filter

```csharp
public sealed class AuthenticateRequestHandler
    : IZLinkRequestHandler<Authenticate, Authenticated>
{
    public ValueTask<Authenticated> HandleAsync(
        Authenticate request, ZLinkRequestContext context, CancellationToken ct)
        => ValueTask.FromResult(new Authenticated(request.PlayerId));   // 결과는 반환값으로 돌려준다(out 파라미터·context.Reply 아님)
}

public sealed class AuditingFilter : IZLinkHandlerFilter
{
    public ValueTask<object?> InvokeAsync(
        ZLinkHandlerInvocation invocation, ZLinkHandlerDelegate next, CancellationToken ct)
        => next(ct);   // 호출하지 않으면 handler 미실행
}
```

`OnDispatchAsync(...)`의 `payload`는 framework `ZLinkMessage` 다. session은 이를
decode 하거나 `IZLinkSessionActor.RelayAsync(...)`에 그대로 넘긴다. raw `IZLinkStream.Write(...)`에
application이 직접 만든 `Message`를 넘길 때만 caller가 그 `Message` 수명을
계속 책임진다.

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkRequestHandler<TRequest, TReply>` | 요청-응답 handler. 결과를 반환값으로 돌려준다 |
| `IZLinkSendHandler<TMessage>` | 단방향 send handler. `ValueTask` 반환 |
| `IZLinkPublishHandler<TEvent>` | publish 수신(구독자) handler |
| `IZLinkHandlerFilter` | request/send/publish 공통 처리(logging/auth/metrics). `next(ct)`로 다음 단계 호출 |
| `IZLinkHandlerContext` | 모든 handler context의 공통 베이스(channel/packet/content-type/cancellation) |

검증: `HandlerContracts.Channel_handlers_and_filters_keep_user_code_behind_typed_contracts`.

### 1.5 codec 등록

```csharp
codecs.Use(ZLinkMessagePackCodec.Default);     // extension codec 등록(extension codec 등록)
codecs.Use(ZLinkProtobufCodec.Default);
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkCodecRegistryBuilder` | framework codec extension 등록(`Use`)을 담당한다. `options.Codecs`로 접근 |
| `IZLinkCodecExtension` | codec 묶음을 붙이는 확장점. `Register(IZLinkCodecRegistrar)`로 자기 codec을 등록한다(예: protobuf·messagepack codec) |
| `IZLinkCodecRegistrar` | extension이 codec/serializer를 실제로 등록하는 표면(content type 별 custom serializer 포함) |
| `IZLinkMessageSerializer` | 업무 객체 ↔ `Message`(byte payload) 변환을 맡는 custom serializer 계약(예: Avro·Thrift). codec extension으로 등록 |

검증: `CodecContracts.Codec_registry_builder_registers_extensions_and_serializers`,
`CodecContracts.Message_serializer_converts_business_objects_to_and_from_messages`.

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
orders
    .SendToChannel("inventory", new ReserveStock("order-1042", "sku-9", 3))
    .Submit();

// gRPC server-streaming / 상태 피드 -> pub/sub fan-out
events
    .Publish("order.events", "order.status", new OrderStatusChanged("order-1042", "Placed"))
    .Submit();
```

| gRPC 패턴 | ZLink 대체 | 표면 / 챕터 |
|-----------|------------|-------------|
| Unary RPC | request/response | `IZLinkChannelClient.RequestToChannel(...).Async<TReply>` ([4](05-channel-messaging.ko.md)) |
| Unary `Empty` / fire-and-forget | one-way send | `IZLinkChannelClient.SendToChannel(...).Submit` ([4](05-channel-messaging.ko.md)) |
| Server streaming / 이벤트 피드 | pub/sub fan-out | `IZLinkFanoutClient.Publish` + `IZLinkPublishHandler<T>` ([4](05-channel-messaging.ko.md)) |
| Client/Bidi streaming | STREAM session | `IZLinkSession`/`IZLinkSessionContext` ([9](09-stream.ko.md), §5) |
| Interceptor | handler filter | `IZLinkHandlerFilter` ([4](05-channel-messaging.ko.md) §5, §1.4) |
| Deadline/timeout | request timeout | `IZLinkRequestCall.Timeout(...)` (§1.1) |
| Metadata/trailer | metadata 정책 | `ConfigureMetadata` + `IZLinkMessageMetadataPolicy` (§2.1, §5.2) |

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
options.ConfigureMetadata().AddForwardedMetadataKey("trace-id");  // 이 key만 다운스트림으로 전달 허용(허용 목록)
var spot = options.AddRouteMesh("play-spots");
spot.AddActorFactory<PlayerActorFactory>("player");
spot.AddActorTransferAdapter<PlayerActor, PlayerActorTransferAdapter>("player"); // remote 이동 state가 있을 때만 등록
options.AddLocationStore(new ZLinkRedisLocationStore(r => r
    .SetConnectionString("redis:6379").SetKeyPrefix("app")));      // 자동 연결·위치 조회의 저장소(§8)
options.UseFilter<AuditingFilter>();
options.ConfigureDispatch()
    .MessageFlow(ZLinkMessageFlowLogMode.ErrorsOnly);             // 오류 흐름만 구조화해 기록
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkFrameworkOptions` | framework 최상위 등록 표면. channel/spot/stream node 등록, codec, handler scan, location store, filter와 dispatch 설정을 소유 |
| `IZLinkMeshNodeBuilder` | MeshNode의 `Listen`·`ChannelName` membership, Entry Spot, Spot factory, actor factory와 actor transfer adapter 등록을 소유 |
| `IZLinkMetadataPolicyBuilder` | 어플리케이션 metadata 전달 정책(`AddForwardedMetadataKey(key)`) |

검증: `BuilderContracts.Framework_options_register_the_top_level_runtime_surface`.

### 2.2 channel builder와 역할

```csharp
{
    var channel = options.AddClientServerChannel("api")
        .EnableServer("tcp://127.0.0.1:5000")     // 같은 endpoint 라도 server/client 역할은 별개로 활성화한다
        .EnableClient("tcp://127.0.0.1:5000");
    channel.AddRequestHandler<ApiRequestHandler, ApiRequest, ApiReply>();
}

{
    var channel = options.AddFanoutChannel("events")
        .EnablePublisher("tcp://127.0.0.1:5100")
        .EnableSubscriber("tcp://127.0.0.1:5100");
}
{
    var channel = options.AddRouteMeshChannel("play-router")
        .EnableServer("tcp://127.0.0.1:5300")
        .EnableClient("tcp://127.0.0.1:5301");
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkClientServerChannelBuilder` | client-server channel(`EnableServer`/`EnableClient`, request/send handler, `SetRoutingId`) |
| `IZLinkFanoutChannelBuilder` | fanout channel(`EnablePublisher`/`EnableSubscriber`, publish handler) |
| `IZLinkRouteMeshChannelBuilder` | route mesh channel(`EnableServer`/`EnableClient`, route handler, `SetRoutingId`) |
| `IZLinkClientServerChannelOptions` / `IZLinkRouteMeshChannelOptions` | 각 channel의 build-time 옵션 묶음(socket/routing 설정 접근) |
| `IZLinkRouteMeshRuntimeOptions` | **런타임** mesh 옵션(DI singleton). `MeshNode(mesh).MaxMessageSize`와 `Channel(mesh, channel).Weight`만 serving 중 변경 가능([12-operations §5](12-operations.ko.md)) |

검증: `BuilderContracts.Channel_builders_expose_only_the_handlers_and_capabilities_valid_for_that_channel`.

### 2.3 spot node · stream node · spot mesh builder

```csharp
{
    var stream = options.AddStreamNode("gateway");
    stream.Bind("tcp://127.0.0.1:5400");
    stream.RegisterSession<GatewaySession>();

}

{
    var mesh = options.AddRouteMesh("play-mesh");

    mesh.Listen("tcp://127.0.0.1:5501");                     // MeshNode의 유일한 router 소켓 bind
    mesh.SetRoutingId(RoutingId.From("spot-router"));        // 이 노드의 routing id

    mesh.ChannelName("play-mesh").SetWeight(100);            // logical membership + select-one 가중치
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
| `IZLinkStreamNodeBuilder` | stream node(`Bind`, `SetTlsServer`, `RegisterSession<TSession>`, `EnableActorDispatch(mesh)`) — 스트림 압축은 프레임워크 레벨 `options.ConfigureStreamCompression()` |
| `IZLinkStreamCompressionBuilder` | stream payload 압축 설정(`UseDefault()` 등) |
| `IZLinkMeshNodeBuilder` | MeshNode 등록 표면(`Listen`·`ChannelName`·routing id·`PeerConnections`·entry/spot/actor factory·drain 정책) |
| `IZLinkMeshChannelBuilder` | logical membership 표면(`SetWeight`, membership 스코프 send/request handler) |
| `IZLinkMeshPeerConnections` | 수동 peering 집합(`Connect(endpoint)`·`Connect(rid, endpoint)`·`Disconnect`·`ListConnections`) |

검증: `BuilderContracts.Mesh_and_stream_builders_declare_node_local_roles_and_channel_memberships`.

### 2.4 연결 집합 — role 별 endpoint 소유

수동 연결은 역할(role) 단위로 별개 집합을 소유한다. 같은 endpoint 목록이라도
client·subscriber·spot router가 서로 다른 연결 집합이다.

```csharp
// 각 호출은 role(client / subscriber / mesh peer …)별 독립 연결 집합을 소유한다 — endpoint가 겹쳐도 공유하지 않는다.
channel.EnableClient("tcp://127.0.0.1:5001");
subscriber.EnableSubscriber("tcp://127.0.0.1:5002");
mesh.PeerConnections.Connect("tcp://127.0.0.1:5003");
```

검증: `BuilderContracts.Channel_builders_expose_only_the_handlers_and_capabilities_valid_for_that_channel`,
`BuilderContracts.Mesh_and_stream_builders_declare_node_local_roles_and_channel_memberships`.

### 2.5 socket · routing · spot · dispatch 설정

```csharp
// config 객체는 직접 만들지 않는다 — builder의 Configure*() 접근자가 돌려준다.
var routed = options.AddRouteMeshChannel("game.route");
IZLinkSocketConfig socket = routed.ConfigureServerSocket();
socket.TcpNoDelay = true;
socket.MaxMessageSize = 1024;
IZLinkRouteConfig route = routed.ConfigureServerRouting();
route.RequireKnownPeer = true;                       // 미지의 peer 거부(inbound 보안 계약)
IZLinkOutboundRouteConfig outbound = routed.ConfigureClientRouting();
outbound.ProbeRouterOnConnect = true;

var node = options.AddRouteMesh("game.stage");
IZLinkMeshNodeSocketConfig router = node.ConfigureRouterSocket();  // HWM·timeout은 startup-only
router.ReceiveHighWaterMark = 64;
IZLinkSpotPublisherConfig publisher = node.ConfigureSpotPublisher();
publisher.SendHighWaterMark = 32;
node.ConfigureEntrySpot().RoutingId = RoutingId.From("entry");

var dispatch = options.ConfigureDispatch();
dispatch.Unhandled.Request = ZLinkUnhandledDispatchAction.ReplyError; // 처리할 handler가 없으면 오류 응답
dispatch.MessageFlow(ZLinkMessageFlowLogMode.ErrorsOnly);             // 오류 흐름만 기록
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSocketConfig` | socket 옵션(HWM, 버퍼, timeout, `TcpNoDelay`, `Immediate`, `IPv6`, `MaxMessageSize` 등) |
| `IZLinkRouteConfig` | inbound router routing 옵션(`RequireKnownPeer`, `AllowPeerHandover`, `EnablePeerProbe`, `ConnectRoutingId`) |
| `IZLinkOutboundRouteConfig` | client 측 routing 옵션(`ProbeRouterOnConnect`) |
| `IZLinkSpotPublisherConfig` | spot publisher 옵션(`SendHighWaterMark`, `SendTimeout`, `Linger`) |
| `IZLinkSpotSubscriberConfig` | spot subscriber 옵션(`ReceiveHighWaterMark`, `ReceiveTimeout`, `Linger`) |
| `IZLinkEntrySpotOptions` | Entry Spot의 routing id 지정 |
| `IZLinkDispatchOptions` | 처리할 handler가 없을 때의 정책과 message-flow 진단 설정 |
| `IZLinkUnhandledDispatchOptions` | 미등록 handler 처리 정책(`Request`/`Send`/`Publish`, log level) |
| `IZLinkDiagnosticsOptions` | message flow 진단 설정(`MessageFlow`, `SampleRate`, message size/native 진단 포함 여부) |
| `IZLinkMessageFlowObserver` | 메시지 생애주기 이벤트 수신(`OnMessageFlowAsync`) — `ConfigureDispatch().SetMessageFlowObserver<T>()`로 등록([11-monitoring §5](11-monitoring.ko.md)) |
| `IZLinkMessageFlowControl` | 실행 중 flow 로그 모드 조회/변경(`MessageFlowMode`, `SetMessageFlowMode`) — 운영 중 토글용 |
| `IZLinkWorkerOptions` | worker pool 설정(`MinThreads`, `MaxThreads`, `IdleTimeout`, `MaxQueueLength`) |
| `IZLinkWorkerCall<TResult>` | spot context의 `RunWorker(...)` 결과 종결자(`Timeout`, `Async`) |

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
        Context.Handlers.AddSubscribe<RoomEventHandler>("room.events");
        Context.Handlers.AddActorPacket<PlayerActorPacketHandler, PlayerActor>();
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
        if (await spots.ResolveSpotHandleAsync(RoutingId.From("room-2"), ct) is { } room2)
        {
            Context.Outbound.SendToSpot(room2, new RoomEvent("opened")).Submit(ct);                    // send: reply 없음
            await Context.Outbound.RequestToSpot(room2, new JoinRoom("room-2")).Async<JoinedRoom>(ct); // request: reply 대기
        }

        Context.Outbound.Publish("room.events", new RoomEvent("opened")).Submit(ct);
        Context.Outbound.SendToChannel("api", new RoomEvent("opened")).Submit(ct);
        await Context.Outbound.RequestToChannel("api", new JoinRoom("room-1")).Async<JoinedRoom>(ct);
    }
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpot` | user spot 인스턴스. `Context` + lifecycle(`Configure`/`OnCreateAsync`/`OnInitializeAsync`/`OnClosingAsync`) |
| `IZLinkSpot<TActor>` | actor를 받는 user spot. `OnActorJoinAsync`/`OnJoinedActorAsync`/`OnLeaveActorAsync`/`OnDisconnectActorAsync` lifecycle을 추가한다 |
| `IZLinkEntrySpot` | Entry Spot 인스턴스. `IZLinkEntrySpotContext` + lifecycle |
| `IZLinkEntrySpot<TActor>` | actor를 받는 Entry Spot. `OnCreateActorAsync`/`OnActorJoinAsync`/`OnJoinedActorAsync`/`OnLeaveActorAsync`/`OnDisconnectActorAsync` lifecycle을 추가한다 |
| `IZLinkSpotContext` | user spot context. handler registry + outbound + `SpotRid`/`NodeRid` + `LeaveActorAsync` + `CloseAsync` + `AddTimer` + `RunWorker` |
| `IZLinkEntrySpotContext` | Entry Spot context. handler registry + outbound + `SpotRid`/`NodeRid` + `DestroyActorAsync` + `AddTimer` + `RunWorker` |
| `IZLinkActorHandlerRegistry` | actor handler 등록(`AddHandler`, `AddActorPacket`) |
| `IZLinkSpotHandlerRegistry` | `IZLinkActorHandlerRegistry` + spot packet/subscribe(`AddPacket`, `AddSubscribe`) |
| `IZLinkSpotOutbound` | spot 안 outbound. `SendToSpot(SpotHandle, msg)`/`RequestToSpot(SpotHandle, req)`(§3.2) + `Publish(topic, msg)`/`SendToChannel`/`RequestToChannel` |
| `IZLinkTimer` | 등록된 timer 핸들. `CancelAsync()` / `DisposeAsync()` (§8도 참조) |

검증: `SpotContracts.Spot_context_registers_handlers_timers_actor_lifecycle_and_outbound_messages`.

### 3.2 spot outbound — local · routed · publisher

```csharp
// ① SpotHandle를 한 번 resolve 해서 보관 — 내부 주소 갱신은 framework가 담당
SpotHandle room1 = await spots.ResolveSpotHandleAsync(RoutingId.From("room-1"))
                         ?? throw new InvalidOperationException("room-1이 아직 없다");

// ② current Spot callback 안에서 — 보관한 SpotHandle로 send/request (IZLinkSpotOutbound)
spot.Context.Outbound.SendToSpot(room1, new RoomEvent("opened")).Submit();
var reply = await spot.Context.Outbound
    .RequestToSpot(room1, new JoinRoom("room-1"))
    .Async<JoinedRoom>();

// ③ spot 밖(일반 코드)에서 — route mesh channel 이름 + SpotHandle (IZLinkRouteClient)
await routes.RequestToSpot(room1, new JoinRoom("room-1")).Async<JoinedRoom>();

// local spot 없는 프로세스에서 publish (topic은 SpotHandle가 필요 없다)
publisher.PublishSpot("play-events", "room.events", new RoomEvent("opened")).Submit(); // IZLinkSpotPublisherClient
```

framework는 위치 event와 주기적 조회로 `SpotHandle`의 내부 주소를 갱신한다. request 도중
주소가 무효화되면 안전한 경우에 한해 한 번 갱신하고 재전송한다. send는 중복 전달 가능성 때문에
자동 재전송하지 않으며, local 제출 실패는 monitoring 오류 경로에서 관측한다
([공통 스펙: spot 주소 메시징](../../spec/server/24-spot-address-messaging.ko.md)).

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpotManager` | spot 인스턴스 생성/조회/종료(`CreateAsync`, `GetOrCreateAsync`, `FindAsync`, `ListAsync`, `CloseAsync`) |
| `IZLinkSpotOutbound` | current Spot callback 안에서의 outbound(`SendToSpot(SpotHandle, …)`/`RequestToSpot(SpotHandle, …)`/`SendToChannel`/`RequestToChannel`/`Publish`) |
| `IZLinkSpotPublisherClient` | local spot 없는 프로세스의 spot channel publish(`PublishSpot(channelName, topic, msg)`) |
| `IZLinkSpotHandleResolver` | spot rid → `SpotHandle` 조회(`ResolveSpotHandleAsync`) — location store를 읽는 기본 메시징 조회 |
| `IZLinkActorSpotHandleResolver` | actor id → actor가 있는 spot의 `SpotHandle` 조회(`ResolveActorSpotHandleAsync`) |

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
| `IZLinkSpot<TActor>.OnActorJoinAsync(...)` | user spot join admission callback. actor instance나 route metadata 없이 actor id와 `ZLinkMessage`를 받으며 codec decode는 framework registry를 사용한다 |
| `IZLinkEntrySpot<TActor>.OnActorJoinAsync(...)` | Entry Spot join 요청 callback. user Spot에서 Entry Spot으로 돌아오는 명시적 join을 accept/reject한다 |
| `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>` | user spot actor 단방향 handler. context 뒤에 payload |
| `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` | user spot actor 요청 handler. context 뒤에 request |
| `IZLinkSpot<TActor>.OnJoinedActorAsync(...)` | user spot actor join commit 이후 lifecycle |
| `IZLinkSpot<TActor>.OnLeaveActorAsync(...)` | user spot actor leave lifecycle |
| `IZLinkSpot<TActor>.OnDisconnectActorAsync` | user spot actor disconnect notification. membership은 변경하지 않음 |
| `IZLinkEntrySpot<TActor>.OnCreateActorAsync(...)` | Entry Spot actor create lifecycle |
| `IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>` | Entry Spot actor 단방향 handler. context 뒤에 payload |
| `IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>` | Entry Spot actor 요청 handler. context 뒤에 request |
| `IZLinkEntrySpot<TActor>.OnJoinedActorAsync(...)` | Entry Spot actor join lifecycle |
| `IZLinkEntrySpot<TActor>.OnLeaveActorAsync(...)` | Entry Spot actor leave lifecycle |
| `IZLinkEntrySpot<TActor>.OnDisconnectActorAsync` | Entry Spot actor disconnect notification. membership은 변경하지 않음 |

검증: `SpotContracts.Spot_handlers_receive_the_spot_instance_and_actor_when_the_contract_requires_it`.

## 4. Actor — actor · context · factory · handler

> 사용법은 [07-actor-spot](07-actor-spot.ko.md). 검증 클래스는 `ActorContracts`.

### 4.1 actor와 lifecycle

```csharp
ActorRef actor = await manager.GetOrCreateAsync("player-1", "player"); // IZLinkActorManager
// Spot 밖 public handler는 ActorRef를 보관하거나 session bind에 넘긴다.
// room join 같은 admission은 Entry Spot actor request handler에서 처리한다.
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkActor` | ID로 식별되는 상태 보유 actor. `ActorId`, `Context` |
| `IZLinkActorContext` | actor의 상태와 동작 표면. `SpotRid?`, `BoundSession`, `JoinSpot`, `JoinEntrySpot`을 제공한다. |
| `IZLinkActorJoinSpotCall` | `JoinSpot(...)` 종결자(`Timeout` → `Async`). 결과는 승인 또는 거절 variant와 reply `ZLinkMessage`를 제공한다. |
| `IZLinkActorJoinEntrySpotCall` | `JoinEntrySpot(..., request)` 종결자(`Timeout` → `Async`). 결과는 승인 또는 거절 variant와 reply `ZLinkMessage`를 제공한다. |
| `IZLinkActorFactory` | `actorType` 별 actor 생성(`CreateAsync(actorId, context, ct)`) |
| `IZLinkActorTransferAdapter<TActor>` | remote transfer에서 선택적으로 actor state를 `ZLinkMessage`로 만들고 target actor를 materialize한다. 미등록이면 기본 빈 state transfer를 사용한다 |
| `IZLinkActorManager` | actor ref 생성/조회(`CreateAsync`, `FindAsync`, `GetOrCreateAsync`) |
| `IZLinkActorDirectory` | 배치 지정 확보 표면 — `FindAsync(actorId)`, `EnsureAsync(actorId, createRequest, ZLinkActorPlacement)`. placement로 선호 node·route mesh를 지정한다 |

`TransferInAsync(actorId, context, state, ct)`의 `state`는 source node의
`TransferOutAsync(actor, ct)`가 반환한 `ZLinkMessage`다. framework가 content type과 payload를 target으로
전달하며, join admission의 request와는 별도 message다. 전체 사용 흐름과 예제는
[07-actor-spot §3](07-actor-spot.ko.md#transferinasync의-state는-어디서-오는가)을 참고한다.

검증: `ActorContracts.Actor_context_creates_actors_and_joins_a_spot_by_routing_id`.

### 4.2 Spot actor handler

```csharp
// Spot actor handler: 현재 Spot, actor, context, payload를 함께 받는다.
public sealed class RoomRequestHandler
    : IZLinkSpotActorRequestHandler<RoomSpot, PlayerActor, PlayerRequest, PlayerReply>
{
    public ValueTask<PlayerReply> HandleAsync(
        RoomSpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        PlayerRequest request,
        CancellationToken ct)
    {
        context.Reply.Metadata("trace-id", "reply-trace").Compress();   // 응답 frame 옵션은 반환값과 별개로 context.Reply로 설정
        return ValueTask.FromResult(new PlayerReply($"room:{actor.ActorId}:{request.Value}")); // 응답 body는 반환값
    }
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>` | user Spot actor 단방향 handler. Spot, actor, context, payload 순서 |
| `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` | user Spot actor 요청 handler. Spot, actor, context, payload 순서 |
| `IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>` | Entry Spot actor 단방향 handler. Entry Spot, actor, context, payload 순서 |
| `IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>` | Entry Spot actor 요청 handler. Entry Spot, actor, context, payload 순서 |
| `ZLinkSpotActorReplyOptions` | Spot actor request 응답 frame 옵션. `context.Reply.Metadata(...).Compress()` |

검증: `ActorContracts.Actor_handlers_receive_the_actor_instance`,
`SpotContracts.Spot_actor_handlers_receive_context_before_payload`.

## 5. STREAM session — session · context · push · bound session

> 사용법은 [09-stream](09-stream.ko.md), [08-actor-session](08-actor-session.ko.md) §3.
> 검증 클래스는 `StreamContracts`.

### 5.1 session과 session context

```csharp
public sealed class ClientHeaderSession(IZLinkSessionContext context) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    // payload는 framework ZLinkMessage 다. decode 하거나 framework API에 그대로 넘긴다.
    public async ValueTask OnDispatchAsync(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload, CancellationToken ct)
    {
        ActorRef actor = await actors.GetOrCreateAsync("player-1", "player", ct);
        var actorRef = await context.Actors.BindAsync(actor, ct); // session↔actor binding 성립(재접속 이전성의 근거)
        if (context.Actors.Find(actorRef.ActorId) is { } boundActor)
        {
            await boundActor.RelayAsync(payload, ct);   // framework ZLinkMessage를 그대로 넘긴다
        }

        context.Client.Send(new PlayerJoined("player-1"))            // IZLinkSessionSendCall: 단방향 push.Metadata("trace-id", "abc").Compress().Submit();
        context.Client.Reply(new AuthenticateReply("player-1")) // IZLinkSessionReplyCall
            .Metadata("trace-id", "abc").Compress().Submit();
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
| `IZLinkSessionActors` | actor binding/lookup(`Bound`, `BindAsync`, `BindAsync(ActorRef, ...)`, `BindOrGetAsync(ActorRef, ...)`, `Find`) |
| `IZLinkSessionActor` | session-bound actor handle. `RelayAsync`, `NotifyDisconnectedAsync`로 대상 actor에게 명시 동작을 보냄 |
| `IZLinkSessionHandlerRegistry` | session `Configure()`에서 handler class를 등록하고, dispatch 때 등록된 handler만 호출 |
| `IZLinkSessionPacketHandler<TSessionContext, TMessage>` | session이 직접 처리할 typed packet handler |
| `IZLinkSessionSendCall` | session push 종결자(`Metadata`/`Compress` → `Submit()`) |
| `IZLinkSessionReplyCall` | session reply 종결자(`Metadata`/`Compress` → `Submit()`) |
| `IZLinkSessionActor` | session relay 용 actor handle(`ActorId`, `Ref`, `RelayAsync`, `NotifyDisconnectedAsync`) |
| `IZLinkStream` | `Send(ZLinkMessage)` / `Reply(ZLinkMessage)` / `CloseAsync`. 보통은 send/reply call에서 metadata와 compression을 지정한다 |

검증: `StreamContracts.Session_context_collects_identity_stream_and_actor_operations`.

### 5.2 bound session — actor가 자기 client로 push

```csharp
// bound session은 단방향 push만 한다(request 표면 없음 → reply를 받지 않는다).
boundSession.Send(new PlayerJoined("player-1"))          // IZLinkBoundSessionSendCall: 단방향 push.Metadata("trace-id", "abc").Submit();
await boundSession.DisconnectAsync();

// 전달 가능한 metadata key 정책 — 허용된 key만 다운스트림으로 전파한다.
IZLinkMessageMetadataPolicy policy = /* ... */;
policy.CanForward("trace-id");   // true / false

ZLinkMessageMetadata metadata = /* ... */;
var traceId = metadata.Find("trace-id");        // 없으면 null
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkBoundSession` | actor에 묶인 client로의 단방향 push. `Send<TMessage>(message)` + `DisconnectAsync`. (요청 표면 없음 — push는 단방향) |
| `IZLinkBoundSessionSendCall` | bound session push 종결자(`Metadata` → `Submit(ct)`) |
| `IZLinkMessageMetadataPolicy` | metadata key 전달 허용 여부(`CanForward`) |

검증: `StreamContracts.Bound_session_sends_to_the_bound_session_without_exposing_stream_transport`.

## 6. Monitoring — source 등록과 runtime event handler

> 사용법은 [11-monitoring](11-monitoring.ko.md). 검증 클래스는 `EventingContracts`.

```csharp
options.AddSocketEvents("router", ZLinkSocketEventKind.Connected); // 인자 = (source 이름, 받을 event kind)
options.AddSpotEvents("spot-node", TimeSpan.FromSeconds(1));
options.AddLocationRuntimeEvents("location-runtime", TimeSpan.FromSeconds(1)); // store 등록 배포

public sealed class SocketEventHandler : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken ct)
        => ValueTask.CompletedTask;
}

await publisher.PublishAsync(socketEvent, ct);   // IZLinkRuntimeEventPublisher
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkMonitoringOptions` | monitoring source 등록. socket(`AddSocketEvents`)·spot(`AddSpotEvents`)·location projection(`AddLocationRuntimeEvents`)·location row 이벤트(`AddLocationPeerEvents`/`AddLocationSpotEvents`/`AddLocationActorEvents`/`AddLocationRouteEvents`). `AddZLinkMonitoring(...)`의 표면 |
| `IZLinkRuntimeEvent` | 모든 runtime event payload의 마커. socket/spot/location event가 구현 |
| `IZLinkRuntimeEventHandler<TEvent>` | 특정 event 타입 수신 handler. DI로 생성·호출 |
| `IZLinkRuntimeEventPublisher` | runtime event 발행 표면(`PublishAsync<TEvent>` where `TEvent : IZLinkRuntimeEvent`) |

검증: `EventingContracts.Eventing_contracts_wire_event_sources_to_typed_runtime_handlers`.

### 6.1 운영 — 메트릭 · flow 상관관계 · drain

> 사용법은 [12-operations](12-operations.ko.md). 자세한 계약은
> [spec/aspnet-core-monitoring §10~§12](../../spec/server/languages/dotnet/01-system-structure.ko.md).

```csharp
builder.Services.AddOpenTelemetry().WithMetrics(m => m.AddMeter(ZLinkMeters.Framework));
builder.Services.AddHealthChecks().AddZLinkDrainHealthCheck();

var drain = app.Services.GetRequiredService<IZLinkDrainControl>();
ZLinkDrainResult result = await drain.DrainAsync(TimeSpan.FromSeconds(25), ct);
```

| 계약 | 역할 |
|------|------|
| `ZLinkMeters` | meter 이름 상수(`Framework` = `"zlink.framework"`). 별도 메트릭 인터페이스는 없다 — `.NET` 표준 `Meter`/`MeterListener`가 표면 |
| `IZLinkDrainControl` | drain 명시 제어(DI singleton) — `IsReady`, `DrainAsync(ct)`(기본 30초), `DrainAsync(deadline, ct)`, `AwaitDrainedAsync(ct)` |
| `ZLinkDrainResult` | drain terminal result — `Drained` 또는 `ForceStopped(ZLinkDrainForceReason)` |
| `ZLinkDrainForceReason` | 강제 종료 사유(`DeadlineExceeded`/`DrainingStatePublishFailed`/`OwnerCleanupFailed`/`TeardownFailed`) |
| `ZLinkMeshNodeDrainPolicy` | MeshNode의 drain 정책(`DrainNatural`/`ReleaseAndRecreate`) — `IZLinkMeshNodeBuilder.UseDrainPolicy(...)`에 넘긴다 |
| `ZLinkDrainEvent` / `ZLinkDrainState` | drain 상태 전이 이벤트(`Serving`/`Draining`/`Drained`/`ForceStopping`, `SourceName`=`"drain"`) — `IZLinkRuntimeEventHandler<ZLinkDrainEvent>`로 구독 |
| `ZLinkFlowOrigin` | 메시지 흐름의 시작 지점(`Inbound`/`Timer`/`Application`/`Lifecycle`) — `ZLinkMessageFlowEvent.FlowOrigin` 필드의 값 |

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
> [공통 스펙](../../spec/server/40-location-runtime.ko.md). 검증 클래스는 `LocationContractTests`.

```csharp
// 등록 — 통합 store 인스턴스 하나 (Redis extension 또는 사용자 구현)
options.AddLocationStore(new ZLinkRedisLocationStore(r => r
    .SetConnectionString("redis:6379").SetKeyPrefix("app")));
var loc = options.ConfigureLocations();
loc.OwnerLeaseTtl = TimeSpan.FromSeconds(15);

// 메시징 조회 — SpotHandle를 한 번 받아 보관한다 (DI 주입)
SpotHandle? spot = await spots.ResolveSpotHandleAsync(spotRid, ct);          // IZLinkSpotHandleResolver
SpotHandle? actorSpot = await actors.ResolveActorSpotHandleAsync("p-1", ct); // IZLinkActorSpotHandleResolver

// 운영 조회 — 활성 raw row와 상태 (DI 주입)
var status = await query.GetStatusAsync(ct);                                        // IZLinkLocationRuntimeQuery
var nodes = await query.ListMeshNodeDescriptorsAsync("game.room", ct);
var topology = await query.ListTopologyAsync(new ZLinkLocationTopologyFilter(), page, ct);
var summaries = await query.ListServiceSummariesAsync(new ZLinkLocationServiceSummaryFilter(), ct);
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkLocationStore` | store 5-role(MeshNode descriptor/spot/actor/owner lease/actor transfer)의 통합 계약 — 한 물리 backend가 전 역할을 제공한다. `AddLocationStore(instance)`로 등록 |
| `IZLinkMeshNodeLocationStore` / `IZLinkSpotLocationStore` / `IZLinkActorLocationStore` | 역할별 store 계약(update/remove/list·resolve). 통합 계약이 이들을 상속한다 |
| `IZLinkOwnerLeaseStore` | runtime instance 별 owner lease(생존 신고) 저장 계약 |
| `IZLinkActorTransferStore` | Actor transfer authority(participant CAS·transfer token·recovery lease) — location row와 같은 물리 store를 공유해 한 시계로 fencing 한다 |
| `IZLinkLocationChangeStampStore` | (선택) scope 별 change stamp — 변경 번호만 읽어 변경 유무를 싸게 감지하는 최적화 |
| `IZLinkSpotHandleResolver` | spot rid → `SpotHandle` 메시징 조회 |
| `IZLinkActorSpotHandleResolver` | actor id → 그 actor가 있는 spot의 `SpotHandle` |
| `IZLinkLocationRuntimeQuery` | 운영/E2E 조회 4종 — `GetStatusAsync`, `ListMeshNodeDescriptorsAsync(mesh)`, `ListTopologyAsync`(합성 topology 보기), `ListServiceSummariesAsync`(channel 단위 요약) |

모든 조회는 store에 도달한다(캐시 없음). 비활성 서버의 row는 owner lease 만료 후
성공 결과에서 자동 제외된다.

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

`ContractSurfaceCoverage`가 위 시나리오의 `[ContractExample(...)]` 합집합 ==
`Zlink.Framework.Contracts.*`의 public interface 집합임을 단언한다. 따라서 이
카탈로그에 빠진 인터페이스가 있으면 빌드가 실패하고, 새 인터페이스를 추가하면
계약 테스트와 이 문서가 함께 갱신돼야 한다.

> client 측 Stream Connector(`Systems.Zlink.Stream.Connector`)의 `Zlink*` 타입은
> 별도 client 라이브러리라 이 카탈로그(서버 framework 계약)에 포함되지 않는다.
> connector 표면은 [09-stream](09-stream.ko.md) §2와
> [Stream Connector 공개 계약](../../spec/stream-connector/languages/dotnet/03-stream-connector.ko.md)이 다룬다.

## 10. 더 보기

- 언어 중립 정식 정의: [spec/handler-interfaces](../../spec/server/languages/dotnet/02-handler-interfaces.ko.md)
- 기능 선택 지도: [04-feature-map](04-feature-map.ko.md)
- 계약 테스트 소스: `framework/languages/dotnet/tests/Zlink.Framework.ContractTests`

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: 운영 — 메트릭 · drain · readiness](12-operations.ko.md) | [다음: ZLink를 어디에 쓰나](14-grpc-alternative.ko.md)
<!-- framework-adapter-nav:bottom:end -->
