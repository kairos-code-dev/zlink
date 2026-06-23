<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: 기능 맵](10-feature-map.ko.md) | [다음: ZLink 을 어디에 쓰나](12-grpc-alternative.ko.md)
<!-- framework-adapter-nav:end -->

# 11. 인터페이스 카탈로그 — 모든 계약을 코드로

> 이 챕터는 framework adapter 가 노출하는 **모든 public 계약 인터페이스**를 한곳에
> 모아, 각 인터페이스가 실제로 어떻게 쓰이는지 코드와 함께 보여 준다. 개념·사용
> 흐름은 앞 챕터(04~09)가, 언어 중립 정식 정의는
> [spec/handler-interfaces](../spec/handler-interfaces.ko.md)가 다룬다. 이 문서는
> 그 둘을 잇는 **실행 검증된 예문 색인**이다.

## 0. 이 카탈로그를 읽는 법

이 문서의 모든 코드 조각은 추측이 아니라
`tests/Zlink.Framework.ContractTests` 의 **컴파일·실행되는 계약 테스트**에서
가져온 것이다. 각 절은 그 테스트 클래스/메서드 하나에 대응한다.

- **완전성 보장.** `ContractSurfaceCoverage.Every_public_contract_interface_has_a_scenario_example`
  테스트가 `Zlink.Framework.Contracts.*` 의 모든 public interface 가 적어도 하나의
  `[ContractExample(typeof(...))]` 시나리오에 등장하는지 단언한다. 즉 이 카탈로그가
  비면 빌드가 깨진다. 인터페이스가 새로 생기면 테스트와 이 문서가 함께 따라온다.
- **예문의 도메인.** 계약 테스트는 게임 서버 도메인(`room`/`player`/`authenticate`,
  `RoomEvent`, `JoinRoom`/`JoinedRoom`)으로 통일돼 있다. 등록(`AddZLinkFramework`)
  코드 흐름은 앞 챕터의 실사용 예와 같고, 여기서는 **인터페이스 표면(메서드
  시그니처·fluent chain)** 에 집중한다.
- **표기.** 서버 framework 타입은 전부 `ZLink`(대문자 L)다. 표 안에서 제네릭
  인자는 `IZLinkSpotPacketHandler<TSpot, TMessage>` 처럼 풀어 적되, 본문 코드는
  실제 구현 형태를 보인다.
- **비동기 이름 규칙.** 공통 의미는
  [비동기 실행과 coroutine 정책](../../common/spec/async-execution-policy.ko.md)을
  따른다. `.NET`에서는 `Task`, `ValueTask`, `Task<T>`, `ValueTask<T>`를 반환하는
  공개 호출 종결자가 `Async(...)`로 끝난다. awaitable을 반환하지 않는
  callback request 표면만 `Submit(callback)` 이름을 유지한다.

각 절 끝의 `검증` 줄은 해당 표면을 단언하는 계약 테스트 메서드를 가리킨다.

## 1. Channel messaging — request · send · pub/sub

> 사용법은 [04-channel-messaging](04-channel-messaging.ko.md). 검증 클래스는
> `ChannelContracts`, `HandlerContracts`, `CodecContracts`.

### 1.1 outbound client 와 종결 호출

```csharp
// IZLinkChannelClient 를 주입받아 channel 이름으로 send/request 한다.
await client
    .SendToChannel("api", new AuthenticateRequest("player-1"))   // IZLinkSendCall
    .PacketName("authenticate")
    .Async();

var reply = await client
    .RequestToChannel("api", new AuthenticateRequest("player-1")) // IZLinkRequestCall
    .PacketName("authenticate")
    .Timeout(TimeSpan.FromSeconds(3))
    .Async<AuthenticateReply>();
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkChannelClient` | `SendToChannel(channelName, msg)` / `RequestToChannel(channelName, req)` 의 기본 표면. client-server channel 호출의 뿌리 |
| `IZLinkSendCall` | send 종결자. `PacketName(...)` 후 `Async(ct)`. 응답을 기다리지 않으므로 `Timeout` 없음 |
| `IZLinkRequestCall` | request 종결자. `PacketName(...)` · `Timeout(...)` 후 `Async<TReply>(ct)`. reply 타입은 종결자에서 지정 |

검증: `ChannelContracts.Channel_client_sends_and_requests_by_channel_name`.

### 1.2 route client — router channel 너머 target node 호출

```csharp
var target = RoutingId.From("play-node-1");

await client
    .Send("play-router", target, new RoomEvent("opened")) // IZLinkSendCall
    .PacketName("room.event")
    .Async();

var room = await client
    .Request("play-router", target, new AllocateRoom("alice")) // IZLinkRequestCall
    .PacketName("room.allocate")
    .Timeout(TimeSpan.FromSeconds(2))
    .Async<RoomAllocated>();
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkRouteClient` | router channel 이름 + target `RoutingId` 로 특정 노드를 직접 지정하는 client. `Send(...)` / `Request(...)` |
| `IZLinkSendCall` | route send 종결자(`PacketName` → `Async(ct)`) |
| `IZLinkRequestCall` | route request 종결자(`PacketName` · `Timeout` → `Async<TReply>`) |
| `IZLinkRouteSendHandler<TMessage>` | route mesh channel 의 단방향 수신 handler. `HandleAsync(msg, ZLinkRouteSendContext, ct)` |
| `IZLinkRouteRequestHandler<TRequest, TReply>` | route mesh channel 의 요청 수신 handler. `HandleAsync(req, ZLinkRouteRequestContext, ct) → ValueTask<TReply>` |

검증: `ChannelContracts.Route_client_addresses_a_target_node_through_a_router_channel`.

### 1.3 fanout publisher

```csharp
await publisher
    .Publish("events", "room.opened", new RoomEvent("opened")) // IZLinkPublishCall
    .PacketName("room.event")
    .Async();
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkFanoutClient` | pub/sub publish 표면. `Publish(channelName, topic, message)` (인자 3개) |
| `IZLinkPublishCall` | publish 종결자(`PacketName` → `Async(ct)`) |

검증: `ChannelContracts.Fanout_client_publishes_events_to_a_topic`.

### 1.4 handler 와 filter

```csharp
public sealed class AuthenticateRequestHandler
    : IZLinkRequestHandler<Authenticate, Authenticated>
{
    public ValueTask<Authenticated> HandleAsync(
        Authenticate request, ZLinkRequestContext context, CancellationToken ct)
        => ValueTask.FromResult(new Authenticated(request.PlayerId));
}

public sealed class AuditingFilter : IZLinkHandlerFilter
{
    public ValueTask<object?> InvokeAsync(
        ZLinkHandlerInvocation invocation, ZLinkHandlerDelegate next, CancellationToken ct)
        => next(ct);   // 호출하지 않으면 handler 미실행
}
```

`OnDispatchAsync(...)` 의 `payload` 는 framework `ZLinkMessage` 다. session 은 이를
decode 하거나 `IZLinkSessionActor.RelayAsync(...)` 에 그대로 넘긴다. raw `IZLinkStream.Write(...)` 에
application 이 직접 만든 `Message` 를 넘길 때만 caller 가 그 `Message` 수명을
계속 책임진다.

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkRequestHandler<TRequest, TReply>` | 요청-응답 handler. 결과를 반환값으로 돌려준다 |
| `IZLinkSendHandler<TMessage>` | 단방향 send handler. `ValueTask` 반환 |
| `IZLinkPublishHandler<TEvent>` | publish 수신(구독자) handler |
| `IZLinkHandlerFilter` | request/send/publish 공통 처리(logging/auth/metrics). `next(ct)` 로 다음 단계 호출 |
| `IZLinkHandlerContext` | 모든 handler context 의 공통 베이스(channel/packet/content-type/cancellation) |

검증: `HandlerContracts.Channel_handlers_and_filters_keep_user_code_behind_typed_contracts`.

### 1.5 codec 등록

```csharp
codecs.AddJson();
codecs.Use(ZLinkMessagePackCodec.Default);
codecs.Use(ZLinkProtobufCodec.Default);
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkCodecRegistryBuilder` | JSON 기본 codec 활성화(`AddJson`), framework codec extension 등록(`Use`), custom serializer 등록(`AddSerializer`)을 담당한다. `options.Codecs` 로 접근 |
| `IZLinkCodecExtension` | codec 묶음을 registry 에 붙이는 확장점. `Register(IZLinkCodecRegistryBuilder)` 로 자기 codec 을 등록한다(예: protobuf·messagepack codec) |
| `IZLinkMessageSerializer` | 업무 객체 ↔ `Message`(byte payload) 변환을 맡는 custom serializer 계약(예: Avro·Thrift). `AddSerializer` 로 등록 |

검증: `CodecContracts.Codec_registry_builder_registers_extensions_and_serializers`,
`CodecContracts.Message_serializer_converts_business_objects_to_and_from_messages`.

### 1.6 웹 백엔드 — gRPC 대체 시나리오

게임 도메인뿐 아니라 일반 웹/마이크로서비스 백엔드에서도 같은 channel messaging
표면이 **gRPC 의 대체재**가 된다. 서비스마다 host:port 를 알리거나 앞단에 gateway/
로드밸런서를 두지 않고, 논리 `channel name` + discovery 로 서비스 간 호출을 묶는다.

```csharp
// gRPC unary RPC -> request/response
var placed = await orders
    .RequestToChannel("orders", new PlaceOrder("order-1042", "acct-77", 18742))
    .PacketName("orders.place")
    .Timeout(TimeSpan.FromSeconds(2))
    .Async<OrderPlaced>();

// gRPC unary returning Empty -> one-way send (응답 안 기다림)
await orders
    .SendToChannel("inventory", new ReserveStock("order-1042", "sku-9", 3))
    .PacketName("inventory.reserve")
    .Async();

// gRPC server-streaming / 상태 피드 -> pub/sub fan-out
await events
    .Publish("order.events", "order.status", new OrderStatusChanged("order-1042", "Placed"))
    .PacketName("order.status-changed")
    .Async();
```

| gRPC 패턴 | ZLink 대체 | 표면 / 챕터 |
|-----------|------------|-------------|
| Unary RPC | request/response | `IZLinkChannelClient.RequestToChannel(...).Async<TReply>` ([4](04-channel-messaging.ko.md)) |
| Unary `Empty` / fire-and-forget | one-way send | `IZLinkChannelClient.SendToChannel(...).Async` ([4](04-channel-messaging.ko.md)) |
| Server streaming / 이벤트 피드 | pub/sub fan-out | `IZLinkFanoutClient.Publish` + `IZLinkPublishHandler<T>` ([4](04-channel-messaging.ko.md)) |
| Client/Bidi streaming | STREAM session | `IZLinkSession`/`IZLinkSessionContext` ([7](07-stream.ko.md), §5) |
| Service discovery(DNS/xDS) | Registry + Discovery | `UseDiscovery` + `IZLinkRegistryQuery` ([8](08-registry.ko.md), §6) |
| Interceptor | handler filter | `IZLinkHandlerFilter` ([4](04-channel-messaging.ko.md) §5, §1.4) |
| Deadline/timeout | request timeout | `IZLinkRequestCall.Timeout(...)` (§1.1) |
| Metadata/trailer | metadata 정책 | `ConfigureMetadata` + `IZLinkMessageMetadataPolicy` (§2.1, §5.2) |

> proto 컴파일·HTTP/2 전용 인프라·`.proto` IDL 없이, DTO(record)와 typed handler
> 만으로 같은 4가지 호출 형태를 얻는다. gRPC↔ZLink 사용 흐름 비교는
> [04-channel-messaging](04-channel-messaging.ko.md) §0 이 더 자세히 다룬다.

검증: `ChannelContracts.Channel_messaging_replaces_grpc_unary_command_and_streaming_for_web_services`.

## 2. Configuration — options · builder · 역할 · 연결

> 사용법은 [02-getting-started](02-getting-started.ko.md),
> [04-channel-messaging](04-channel-messaging.ko.md) §3·§6,
> [05-spot](05-spot.ko.md) §2. 검증 클래스는 `BuilderContracts`,
> `ConnectionAndConfigContracts`.

### 2.1 최상위 옵션 표면

`AddZLinkFramework(options => ...)` 의 `options` 가 구현하는 표면이다.

```csharp
options.Codecs.AddJson();
options.AddHandlersFromAssemblyOf<Program>();
options.ConfigureMetadata().AddForwardedMetadataKey("trace-id");
options.AddActorFactory<PlayerActorFactory>("player");
options.AddSpotRemoteAddressResolver<MySpotResolver>();
options.UseRegistrySpotRemoteAddresses("game").RouterChannelId = "play-router";
options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:6000");
options.AddSpotMesh("play-spots").UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:6001");
options.UseFilter<AuditingFilter>();
options.ConfigureDispatch().SpotDispatchMode = ZLinkDispatchMode.Compiled;
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkFrameworkOptions` | framework 최상위 등록 표면. channel/spot/stream node 등록, codec, handler scan, discovery, filter, dispatch, actor factory 를 모두 소유 |
| `IZLinkDiscoveryBuilder` | `UseDiscovery(...)` 안에서 discovery endpoint 를 `AddRegistryEndpoint(endpoint)` 로 추가한다 |
| `IZLinkMetadataPolicyBuilder` | 응용 metadata 전달 정책(`AddForwardedMetadataKey(key)`) |
| `IZLinkRegistrySpotRemoteAddressesOptions` | Registry 기반 spot 주소 해석 옵션(`RouterChannelId`) |

검증: `BuilderContracts.Framework_options_register_the_top_level_runtime_surface`.

### 2.2 channel builder 와 역할

```csharp
{
    var channel = options.AddClientServerChannel("api")
        .EnableServer("tcp://127.0.0.1:5000")
        .EnableClient("tcp://127.0.0.1:5000");
    channel.AddRequestHandler<ApiRequestHandler, ApiRequest, ApiReply>();
    channel.EnableSpotRouteEgress("play-spots");
}

{
    var channel = options.AddFanoutChannel("events")
        .EnablePublisher("tcp://127.0.0.1:5100")
        .EnableSubscriber("tcp://127.0.0.1:5100");
}
{
    var channel = options.AddDealerMeshChannel("mesh")
        .EnableServer("tcp://127.0.0.1:5200")
        .EnableClient("tcp://127.0.0.1:5201");
}
{
    var channel = options.AddRouteMeshChannel("play-router")
        .EnableServer("tcp://127.0.0.1:5300")
        .EnableClient("tcp://127.0.0.1:5301");
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkClientServerChannelBuilder` | client-server channel(`EnableServer`/`EnableClient`, request/send handler, `EnableSpotRouteEgress`) |
| `IZLinkFanoutChannelBuilder` | fanout channel(`EnablePublisher`/`EnableSubscriber`, publish handler) |
| `IZLinkDealerMeshChannelBuilder` | dealer mesh channel(`EnableServer`/`EnableClient`, send/request handler) |
| `IZLinkRouteMeshChannelBuilder` | route mesh channel(`EnableServer`/`EnableClient`, routing/socket, route send/request handler, `EnableSpotRouteEgress`) |

검증: `BuilderContracts.Channel_builders_expose_only_the_handlers_and_capabilities_valid_for_that_channel`.

### 2.3 spot node · stream node · spot mesh builder

```csharp
{
    var stream = options.AddStreamNode("gateway");
    stream.Bind("tcp://127.0.0.1:5400");
    stream.RegisterSession<GatewaySession>();

}

{
    var mesh = options.AddSpotMesh("play-mesh");
    mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:6003");

    {
        var spot = mesh.AddNode("play-spots");

        spot.EnableRouter("tcp://127.0.0.1:5501");
        spot.SetRouterRoutingId(RoutingId.From("spot-router"));

        spot.EnablePubSub("tcp://0.0.0.0:9000");
        spot.ConnectPeerPub("tcp://127.0.0.1:5500");
        spot.ConfigurePubSubPublisher().NoDrop = true;

        var api = options.AddClientServerChannel("api");
        api.EnableClient("tcp://127.0.0.1:5300");
        api.ConfigureClientSocket().Immediate = true;
        spot.AttachSpotPublisherClient("events");
        spot.AttachSpotPublisherClient("mesh-events");
        spot.AcceptSpotRoutesFromChannel("play-router", "tcp://127.0.0.1:5300");
        spot.ConfigureEntrySpot().RoutingId = RoutingId.From("entry");
        spot.AddSpotFactory<RoomSpot>();
        spot.AddEntrySpot<EntrySpot>();
    }

    {
        var spot = mesh.AddNode("session-node");
        spot.EnableRouter("tcp://127.0.0.1:5601");
        spot.ConfigureRouterRouting().RoutingId = RoutingId.From("session-gateway");
    }
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkStreamNodeBuilder` | stream node(`Bind`, `AttachActorGateway`, `RegisterSession<TSession>`) |
| `IZLinkSpotNodeBuilder` | spot node 등록 표면(`EnableRouter`, `EnablePubSub`, channel route bridge, route 수용, entry/spot factory) |
| `IZLinkSpotMeshNodeBuilder` | spot mesh 안의 노드 빌더(`IZLinkSpotNodeBuilder` 와 같은 표면, mesh 컨텍스트) |
| `IZLinkSpotMeshBuilder` | discovery 기반 spot mesh(`UseDiscovery`, `AddNode`) |

검증: `BuilderContracts.Spot_and_stream_builders_declare_node_local_roles_and_channel_attachments`.

### 2.4 연결 집합 — role 별 endpoint 소유

수동 연결은 역할(role) 단위로 별개 집합을 소유한다. 같은 endpoint 목록이라도
client·subscriber·spot router 가 서로 다른 연결 집합이다.

```csharp
channel.EnableClient("tcp://127.0.0.1:5001");
subscriber.EnableSubscriber("tcp://127.0.0.1:5002");
spot.ConnectRouter("tcp://127.0.0.1:5003");
spot.ConnectPeerPub("tcp://127.0.0.1:5004");
spot.AttachSpotPublisherClient("events", "tcp://127.0.0.1:5005");
spot.AcceptSpotRoutesFromChannel("play-router", "tcp://127.0.0.1:5006");
```

검증: `BuilderContracts.Channel_builders_expose_only_the_handlers_and_capabilities_valid_for_that_channel`,
`BuilderContracts.Spot_and_stream_builders_declare_node_local_roles_and_channel_attachments`.

### 2.5 socket · routing · spot · dispatch 설정

```csharp
var socket = new SocketConfig { TcpNoDelay = true, Immediate = true, MaxMessageSize = 1024, /* ... */ };
var route = new RouteConfig { RoutingId = RoutingId.From("router"), RequireKnownPeer = true };
var outbound = new OutboundRouteConfig { RoutingId = RoutingId.From("client"), ProbeRouterOnConnect = true };
var publisher = new SpotPublisherConfig { SendHighWaterMark = 32, NoDrop = true };
var subscriber = new SpotSubscriberConfig { ReceiveHighWaterMark = 64 };
var entry = new EntrySpotOptions { RoutingId = RoutingId.From("entry") };
var dispatch = new DispatchOptions { SpotDispatchMode = ZLinkDispatchMode.Compiled, StreamDispatchMode = ZLinkDispatchMode.Dynamic };
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSocketConfig` | socket 옵션(HWM, 버퍼, timeout, `TcpNoDelay`, `Immediate`, `IPv6`, `MaxMessageSize` 등) |
| `IZLinkRouteConfig` | inbound router routing 옵션(`RoutingId`, `RequireKnownPeer`, `AllowPeerHandover`, `EnablePeerProbe`, `ConnectRoutingId`) |
| `IZLinkOutboundRouteConfig` | client 측 routing 옵션(`RoutingId`, `ProbeRouterOnConnect`) |
| `IZLinkSpotPublisherConfig` | spot publisher 옵션(`SendHighWaterMark`, `SendTimeout`, `Linger`, `NoDrop`) |
| `IZLinkSpotSubscriberConfig` | spot subscriber 옵션(`ReceiveHighWaterMark`, `ReceiveTimeout`, `Linger`) |
| `IZLinkEntrySpotOptions` | Entry Spot 의 routing id 지정 |
| `IZLinkDispatchOptions` | spot/stream dispatch mode(`Compiled`/`Dynamic`) |
| `IZLinkUnhandledDispatchOptions` | 미등록 handler 처리 정책(`Request`/`Send`/`Publish`, log level) |
| `IZLinkDiagnosticsOptions` | message flow 진단 설정(`MessageFlow`, `SampleRate`, message size/native 진단 포함 여부) |
| `IZLinkWorkerOptions` | worker pool 설정(`MinThreads`, `MaxThreads`, `IdleTimeout`, `MaxQueueLength`) |
| `IZLinkWorkerCall<TResult>` | spot context 의 `RunWorker(...)` 결과 종결자(`Timeout`, `Async`, `Submit`) |

검증: `ConnectionAndConfigContracts.Configuration_contracts_keep_socket_routing_spot_and_dispatch_options_typed`.

## 3. SPOT — spot · context · client · handler

> 사용법은 [05-spot](05-spot.ko.md). 검증 클래스는 `SpotContracts`.

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
        PlayerActor actor,
        ZLinkMessage request,
        CancellationToken ct)
    {
        var join = request.Decode<JoinRoom>();
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

    public async ValueTask OnInitializeAsync(CancellationToken ct)
    {
        await Context.AddTimer<RoomTimerHandler>("heartbeat", TimeSpan.FromSeconds(1));
        await Context.Outbound.SendToSpot(RoutingId.From("room-2"), new RoomEvent("opened")).Async(ct);
        await Context.Outbound.RequestToSpot(RoutingId.From("room-2"), new JoinRoom("room-2")).Async<JoinedRoom>(ct);
        await Context.Outbound.Publish("room.events", new RoomEvent("opened")).Async(ct);
        await Context.Outbound.SendToChannel("api", new RoomEvent("opened")).Async(ct);
        await Context.Outbound.RequestToChannel("api", new JoinRoom("room-1")).Async<JoinedRoom>(ct);
    }
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpot` | user spot 인스턴스. `Context` + lifecycle(`Configure`/`OnCreateAsync`/`OnInitializeAsync`/`OnClosingAsync`) |
| `IZLinkSpot<TActor>` | actor 를 받는 user spot. `OnActorJoinAsync`/`OnJoinedActorAsync`/`OnLeaveActorAsync`/`OnDisconnectActorAsync` lifecycle 을 추가한다 |
| `IZLinkEntrySpot` | Entry Spot 인스턴스. `IZLinkEntrySpotContext` + lifecycle |
| `IZLinkEntrySpot<TActor>` | actor 를 받는 Entry Spot. `OnCreateActorAsync`/`OnActorJoinAsync`/`OnJoinedActorAsync`/`OnLeaveActorAsync`/`OnDisconnectActorAsync` lifecycle 을 추가한다 |
| `IZLinkSpotContext` | user spot context. handler registry + outbound + `SpotRid`/`NodeRid` + `leaveActor` + `CloseAsync` + `AddTimer` + `RunWorker` |
| `IZLinkEntrySpotContext` | Entry Spot context. handler registry + outbound + `SpotRid`/`NodeRid` + `DestroyActorAsync` + `AddTimer` + `RunWorker` |
| `IZLinkActorHandlerRegistry` | actor handler 등록(`AddHandler`, `AddActorPacket`) |
| `IZLinkSpotHandlerRegistry` | `IZLinkActorHandlerRegistry` + spot packet/subscribe(`AddPacket`, `AddSubscribe`) |
| `IZLinkSpotOutbound` | spot 안 outbound(`SendToSpot`, `RequestToSpot`, `Publish(topic, msg)`, `SendToChannel`, `RequestToChannel`) |
| `IZLinkTimer` | 등록된 timer 핸들. `CancelAsync()` / `DisposeAsync()` (§8 도 참조) |

검증: `SpotContracts.Spot_context_registers_handlers_timers_actor_lifecycle_and_outbound_messages`.

### 3.2 spot outbound — local · routed · publisher

```csharp
// 현재 노드의 spot API
await localClient.SendToSpot(spotRid, new RoomEvent("opened")).Async();            // IZLinkSpotOutbound
var reply = await localClient.RequestToSpot(spotRid, new JoinRoom("room-1")).Async<JoinedRoom>();

// current Spot callback 안에서 outbound 호출
await spot.Context.Outbound
    .RequestToSpot(remoteAddress.SpotRid, new JoinRoom("room-1"))
    .Async<JoinedRoom>();

// local spot 없는 노드에서 publish
await publisher.PublishSpot("play-events", "room.events", new RoomEvent("opened")).Async(); // IZLinkSpotPublisherClient
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpotManager` | spot 인스턴스 생성/조회/종료(`CreateAsync`, `GetOrCreateAsync`, `FindAsync`, `ListAsync`, `CloseAsync`) |
| `IZLinkSpotOutbound` | current Spot callback 안에서의 outbound(`SendToSpot`/`RequestToSpot`/`SendToChannel`/`RequestToChannel`/`Publish`) |
| `IZLinkSpotPublisherClient` | local spot 없는 노드의 spot channel publish(`PublishSpot(channelName, topic, msg)`) |
| `IZLinkSpotRemoteAddressResolver` | spot `RoutingId` → `ZLinkSpotRemoteAddress` 해석(`ResolveSpotRemoteAddressAsync`) |

검증: `SpotContracts.Spot_clients_separate_local_spot_api_routed_egress_and_publisher_channels`.

### 3.3 spot handler — spot/actor 인스턴스를 첫 인자로 받는다

```csharp
public sealed class RoomRequestHandler
    : IZLinkSpotRequestHandler<RoomSpot, JoinRoom, JoinedRoom>
{
    public ValueTask<JoinedRoom> HandleAsync(RoomSpot spot, JoinRoom request, CancellationToken ct)
        => ValueTask.FromResult(new JoinedRoom(request.RoomId));
}

// actor join admission 은 handler 등록이 아니라 RoomSpot.OnActorJoinAsync(...) 에 둔다.
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpotPacketHandler<TSpot, TMessage>` | spot 단방향 packet handler. 첫 인자 = spot |
| `IZLinkSpotRequestHandler<TSpot, TRequest, TReply>` | spot 요청 handler. 반환값이 응답 |
| `IZLinkSpotSubscriptionHandler<TSpot, TMessage>` | spot 구독 topic 수신 handler |
| `IZLinkSpotTimerHandler<TSpot>` | spot timer tick handler(`ZLinkTimerTick`) |
| `IZLinkSpot<TActor>.OnActorJoinAsync(...)` | user spot join 요청 callback. 기본 계약은 `(TActor, Message)` |
| `IZLinkEntrySpot<TActor>.OnActorJoinAsync(...)` | Entry Spot join 요청 callback. user Spot에서 Entry Spot으로 돌아오는 명시적 join을 accept/reject한다 |
| `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>` | user spot actor 단방향 handler. context 뒤에 payload |
| `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` | user spot actor 요청 handler. context 뒤에 request |
| `IZLinkSpot<TActor>.OnJoinedActorAsync(...)` | user spot actor join commit 이후 lifecycle |
| `IZLinkSpot<TActor>.OnLeaveActorAsync(...)` | user spot actor leave lifecycle |
| `IZLinkSpot<TActor>.OnDisconnectActorAsync` | user spot actor disconnect notification. membership 은 변경하지 않음 |
| `IZLinkEntrySpot<TActor>.OnCreateActorAsync(...)` | Entry Spot actor create lifecycle |
| `IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>` | Entry Spot actor 단방향 handler. context 뒤에 payload |
| `IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>` | Entry Spot actor 요청 handler. context 뒤에 request |
| `IZLinkEntrySpot<TActor>.OnJoinedActorAsync(...)` | Entry Spot actor join lifecycle |
| `IZLinkEntrySpot<TActor>.OnLeaveActorAsync(...)` | Entry Spot actor leave lifecycle |
| `IZLinkEntrySpot<TActor>.OnDisconnectActorAsync` | Entry Spot actor disconnect notification. membership 은 변경하지 않음 |

검증: `SpotContracts.Spot_handlers_receive_the_spot_instance_and_actor_when_the_contract_requires_it`.

## 4. Actor — actor · context · factory · handler

> 사용법은 [06-actor-session](06-actor-session.ko.md). 검증 클래스는 `ActorContracts`.

### 4.1 actor 와 lifecycle

```csharp
var actor = await manager.GetOrCreateAsync("player-1", "player"); // IZLinkActorManager
var joinReply = await actor.Context
    .JoinSpot(RoutingId.From("room-1"), new JoinRoom("room-1")) // IZLinkActorContext
    .Async();
if (!joinReply.Accepted)
{
    return;
}
var joined = joinReply.Reply.Decode<JoinedRoom>();
actor.Configure();
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkActor` | ID 로 식별되는 상태 보유 actor. `ActorId`, `Context` |
| `IZLinkActorContext` | actor 의 상태/동작 표면. `SpotRid?`/`IsJoined`, `BoundSession`, `JoinSpot`/`JoinEntrySpot`, `GetSpot<T>` |
| `IZLinkActorJoinSpotCall` | `JoinSpot(...)` 종결자(`Timeout` → `Async`). 결과는 `Accepted`, `ActorRef`, reply `ZLinkMessage` |
| `IZLinkActorJoinEntrySpotCall` | `JoinEntrySpot(..., request)` 종결자(`Timeout` → `Async`). 결과는 `Accepted`, `ActorRef`, reply `ZLinkMessage` |
| `IZLinkActorFactory` | `actorType` 별 actor 생성(`CreateAsync(actorId, context, ct)`) |
| `IZLinkActorManager` | actor 생성/조회(`CreateAsync`, `FindAsync`, `GetOrCreateAsync`) |

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
        context.Reply.Metadata("trace-id", "reply-trace").Compress();
        return ValueTask.FromResult(new PlayerReply($"room:{actor.ActorId}:{request.Value}"));
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

> 사용법은 [07-stream](07-stream.ko.md), [06-actor-session](06-actor-session.ko.md) §4.
> 검증 클래스는 `StreamContracts`.

### 5.1 session 과 session context

```csharp
public sealed class ClientHeaderSession(IZLinkSessionContext context) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public async ValueTask OnDispatchAsync(ZlinkStreamHeader header, ZLinkMessage payload, CancellationToken ct)
    {
        IZLinkActor actor = await actors.GetOrCreateAsync("player-1", "player", ct);
        var actorRef = await context.Actors.BindAsync(actor, ct); // ActorDispatch
        if (context.Actors.Find(actorRef.ActorId) is { } boundActor)
        {
            await boundActor.RelayAsync(header, payload, ct);
        }

        await context.Client.Send(new PlayerJoined("player-1"))      // IZLinkSessionSendCall
            .PacketName("player.joined").Metadata("trace-id", "abc").Compress().Async();
        await context.Client.Reply(new AuthenticateReply("player-1")) // IZLinkSessionReplyCall
            .Metadata("trace-id", "abc").Compress().Async();
        await context.CloseAsync();                             // Lifecycle
    }

    public ValueTask OnConnectedAsync(CancellationToken ct) => ValueTask.CompletedTask;
    public ValueTask OnDisconnectedAsync(CancellationToken ct) => ValueTask.CompletedTask;
    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken ct) => ValueTask.CompletedTask;
}
```

`OnDispatchAsync(...)` 로 받은 `payload` 는 framework `ZLinkMessage` 다.
session 은 이 값을 decode 하거나 `IZLinkSessionActor.RelayAsync(...)` 에 그대로 넘긴다.

session 전용 packet handler 를 나누고 싶을 때는
`IZLinkSessionPacketDispatcher<TSessionContext>` 를 session 생성자에 주입한다.
dispatcher 는 `IZLinkSessionPacketHandler<TSessionContext>` 구현 중 packet name 이
맞는 handler 만 호출하고, 미등록 packet 은 `false` 로 돌려준다. 미등록 packet 을
actor 로 relay 할지, 거절할지, 로그만 남길지는 application session 이 결정한다.

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSession` | stream session. `Context` + `OnConnectedAsync`/`OnDisconnectedAsync`/`OnErrorAsync` + 기본 구현 `OnDispatchAsync` |
| `IZLinkSessionContext` | session 식별값과 `Client`, `Actors`, `CloseAsync` 를 가진 root context |
| `IZLinkSessionClient` | client 로의 push(`Send(msg)`) / 요청 응답(`Reply(msg)`) |
| `IZLinkSessionActors` | actor binding/lookup(`Bound`, `BindAsync`, `BindAsync(ActorRef, ...)`, `Find`) |
| `IZLinkSessionActor` | session-bound actor handle. `RelayAsync`, `NotifyDisconnectedAsync` 로 대상 actor 에게 명시 동작을 보냄 |
| `IZLinkSessionPacketHandler<TSessionContext>` | session 이 직접 처리할 packet handler. payload 는 session callback 과 같은 framework `ZLinkMessage` |
| `IZLinkSessionPacketDispatcher<TSessionContext>` | 등록된 session packet handler 만 호출하고 미등록 packet 은 `false` 반환 |
| `IZLinkSessionSendCall` | session push 종결자(`Metadata`/`PacketName`/`Compress` → `Async()`) |
| `IZLinkSessionReplyCall` | session reply 종결자(`Metadata`/`Compress` → `Async()`) |
| `IZLinkSessionActor` | session relay 용 actor handle(`ActorId`, `Ref`, `RelayAsync`, `NotifyDisconnectedAsync`) |
| `IZLinkStream` | raw stream write(`Write(Message, SendFlags)`) / `CloseAsync`. 보통은 `Send`/`Reply` 를 쓴다 |

검증: `StreamContracts.Session_context_collects_identity_stream_and_actor_operations`.

### 5.2 bound session — actor 가 자기 client 로 push

```csharp
await boundSession.Send(new PlayerJoined("player-1"))    // IZLinkBoundSessionSendCall
    .PacketName("player.joined").Metadata("trace-id", "abc").Async();
await boundSession.DisconnectAsync();

// 전달 가능한 metadata key 정책
IZLinkMessageMetadataPolicy policy = /* ... */;
policy.CanForward("trace-id");   // true / false

ZLinkMessageMetadata metadata = /* ... */;
var traceId = metadata.Find("trace-id");        // 없으면 null
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkBoundSession` | actor 에 묶인 client 로의 단방향 push. `Send<TMessage>(message)` + `DisconnectAsync`. (요청 표면 없음 — push 는 단방향) |
| `IZLinkBoundSessionSendCall` | bound session push 종결자(`PacketName`/`Metadata` → `Async(ct)`) |
| `IZLinkMessageMetadataPolicy` | metadata key 전달 허용 여부(`CanForward`) |

검증: `StreamContracts.Bound_session_sends_to_the_bound_session_without_exposing_stream_transport`.

## 6. Registry — status · topology · client options

> 사용법은 [08-registry](08-registry.ko.md). 검증 클래스는 `RegistryContracts`.

```csharp
options.PubEndpoint = "tcp://127.0.0.1:6001";   // IZLinkRegistryOptions
options.RouterEndpoint = "tcp://127.0.0.1:6002";
options.AddPeer("tcp://127.0.0.1:7001");

var clientOptions = new ExampleRegistryQueryClientOptions { Endpoint = options.RouterEndpoint };

var status = await query.StatusAsync();                 // IZLinkRegistryQuery
var topology = await query.TopologyAsync(new ZLinkRegistryTopologyFilter(ChannelName: "play"));
var snapshot = await client.TopologyAsync();                    // IZLinkRegistryQueryClient
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkRegistryOptions` | Registry 서버 설정(`PubEndpoint`, `RouterEndpoint`, `RegistryId`, heartbeat/broadcast interval, `AddPeer`) |
| `IZLinkRegistryQuery` | in-process Registry 조회(`StatusAsync`, `ServiceSummaryAsync`, `TopologyAsync`, `MemberPeersAsync`) |
| `IZLinkRegistryQueryClient` | 원격 Registry 조회. topology `TopologyAsync` 만 제공(C API 제약) |
| `IZLinkRegistryQueryClientOptions` | 원격 query client 의 `Endpoint`(ROUTER) |

검증: `RegistryContracts.Registry_contracts_describe_status_topology_and_client_options`.

## 7. Monitoring — source 등록과 runtime event handler

> 사용법은 [09-monitoring](09-monitoring.ko.md). 검증 클래스는 `EventingContracts`.

```csharp
options.AddSocketEvents("router", ZLinkSocketEventKind.Connected); // IZLinkMonitoringOptions
options.AddRegistryEvents("registry", TimeSpan.FromSeconds(1));
options.AddSpotEvents("spot-node", TimeSpan.FromSeconds(1));

public sealed class SocketEventHandler : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken ct)
        => ValueTask.CompletedTask;
}

await publisher.PublishAsync(socketEvent, ct);   // IZLinkRuntimeEventPublisher
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkMonitoringOptions` | monitoring source 등록(`AddSocketEvents`, `AddRegistryEvents`, `AddSpotEvents`). `AddZLinkMonitoring(...)` 의 표면 |
| `IZLinkRuntimeEvent` | 모든 runtime event payload 의 마커. socket/registry/spot event 가 구현 |
| `IZLinkRuntimeEventHandler<TEvent>` | 특정 event 타입 수신 handler. DI 로 생성·호출 |
| `IZLinkRuntimeEventPublisher` | runtime event 발행 표면(`PublishAsync<TEvent>` where `TEvent : IZLinkRuntimeEvent`) |

검증: `EventingContracts.Eventing_contracts_wire_event_sources_to_typed_runtime_handlers`.

## 8. Timer — 등록된 timer 핸들

> 사용법은 [05-spot](05-spot.ko.md) §3. 검증 클래스는 `TimerContracts`.

```csharp
await using var timer = await Context.AddTimer<RoomTimerHandler>("heartbeat", TimeSpan.FromSeconds(1));
await timer.CancelAsync();   // IZLinkTimer
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkTimer` | `AddTimer<THandler>(...)` 가 돌려주는 timer 핸들. `IsDisposed`, `CancelAsync()`, `IAsyncDisposable` 로 정리 |

검증: `TimerContracts.Timer_contract_allows_spot_code_to_cancel_a_registered_timer`.

## 9. 완전성 — 카탈로그가 곧 계약 표면

| 영역(namespace) | 검증 클래스 | 시나리오 수 |
|-----------------|-------------|:-----------:|
| `Channels` | `ChannelContracts` | 4 |
| `Handlers` | `HandlerContracts` | 2 |
| `Codecs` | `CodecContracts` | 2 |
| `Configuration` | `BuilderContracts` · `ConnectionAndConfigContracts` | 4 |
| `Spots` | `SpotContracts` | 6 |
| `Workers` | `SpotContracts`(`IZLinkWorkerCall<>`·`IZLinkWorkerOptions`) | (위 Spots 에 포함) |
| `Actors` | `ActorContracts` | 1 |
| `Streams` | `StreamContracts` | 3 |
| `Registry` | `RegistryContracts` | 1 |
| `Eventing` | `EventingContracts` | 1 |
| `Timers` | `TimerContracts` | 1 |
| `Dispatch` | `ConnectionAndConfigContracts`(`IZLinkDispatchOptions`) | (위 4 에 포함) |

`ContractSurfaceCoverage` 가 위 시나리오의 `[ContractExample(...)]` 합집합 ==
`Zlink.Framework.Contracts.*` 의 public interface 집합임을 단언한다. 따라서 이
카탈로그에 빠진 인터페이스가 있으면 빌드가 실패하고, 새 인터페이스를 추가하면
계약 테스트와 이 문서가 함께 갱신돼야 한다.

> client 측 Stream Connector(`Systems.Zlink.Stream.Connector`)의 `Zlink*` 타입은
> 별도 client 라이브러리라 이 카탈로그(서버 framework 계약)에 포함되지 않는다.
> connector 표면은 [07-stream](07-stream.ko.md) §2 와
> [samples/streaming-client](samples/streaming-client.ko.md)가 다룬다.

## 10. 더 보기

- 언어 중립 정식 정의: [spec/handler-interfaces](../spec/handler-interfaces.ko.md)
- 기능 선택 지도: [10-feature-map](10-feature-map.ko.md)
- 계약 테스트 소스: `framework/languages/dotnet/tests/Zlink.Framework.ContractTests`

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: 기능 맵](10-feature-map.ko.md) | [다음: ZLink 을 어디에 쓰나](12-grpc-alternative.ko.md)
<!-- framework-adapter-nav:bottom:end -->
