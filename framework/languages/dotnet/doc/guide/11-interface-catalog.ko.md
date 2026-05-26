<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: 기능 맵](./10-feature-map.ko.md) | [다음: ZLink 을 어디에 쓰나](./12-grpc-alternative.ko.md)
<!-- framework-adapter-nav:end -->

# 인터페이스 카탈로그 — 모든 계약을 코드로

> 이 챕터는 framework adapter 가 노출하는 **모든 public 계약 인터페이스**를 한곳에
> 모아, 각 인터페이스가 실제로 어떻게 쓰이는지 코드와 함께 보여 준다. 개념·사용
> 흐름은 앞 챕터(04~09)가, 언어 중립 정식 정의는
> [spec/handler-interfaces](../spec/handler-interfaces.ko.md)가 소유한다. 이 문서는
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

각 절 끝의 `검증` 줄은 해당 표면을 단언하는 계약 테스트 메서드를 가리킨다.

## 1. Channel messaging — request · send · pub/sub

> 사용법은 [04-channel-messaging](./04-channel-messaging.ko.md). 검증 클래스는
> `ChannelContracts`, `HandlerContracts`, `CodecContracts`.

### 1.1 outbound client 와 종결 호출

```csharp
// IZLinkClient 를 주입받아 channel 이름으로 send/request 한다.
await client
    .Send("api", new AuthenticateRequest("player-1"))   // IZLinkSendCall
    .PacketName("authenticate")
    .Submit();

var reply = await client
    .Request("api", new AuthenticateRequest("player-1")) // IZLinkRequestCall
    .PacketName("authenticate")
    .Timeout(TimeSpan.FromSeconds(3))
    .SubmitAsync<AuthenticateReply>();
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkClientServerClient` | `Send(channelName, msg)` / `Request(channelName, req)` 의 기본 표면. client-server channel 호출의 뿌리 |
| `IZLinkClient` | DI 로 주입받는 일반 outbound client. `IZLinkClientServerClient` 를 상속해 추가 표면 없이 그대로 노출 |
| `IZLinkSendCall` | send 종결자. `PacketName(...)` 후 `Submit(ct)`. 응답을 기다리지 않으므로 `Timeout` 없음 |
| `IZLinkRequestCall` | request 종결자. `PacketName(...)` · `Timeout(...)` 후 `SubmitAsync<TReply>(ct)`. reply 타입은 종결자에서 지정 |

검증: `ChannelContracts.Client_server_client_sends_and_requests_by_channel_name`.

### 1.2 route client — router channel 너머 target node 호출

```csharp
var target = RoutingId.Of("play-node-1");

await client
    .SendTo("play-router", target, new RoomEvent("opened")) // IZLinkRouteSendCall
    .PacketName("room.event")
    .Submit();

var room = await client
    .RequestTo("play-router", target, new AllocateRoom("alice")) // IZLinkRouteRequestCall
    .PacketName("room.allocate")
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync<RoomAllocated>();
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkRouteClient` | router channel 이름 + target `RoutingId` 로 특정 노드를 직접 지정하는 client. `SendTo(...)` / `RequestTo(...)` |
| `IZLinkRouteSendCall` | route send 종결자(`PacketName` → `Submit`) |
| `IZLinkRouteRequestCall` | route request 종결자(`PacketName` · `Timeout` → `SubmitAsync<TReply>`) |
| `IZLinkRouteSendHandler<TMessage>` | route mesh channel 의 단방향 수신 handler. `HandleAsync(msg, ZLinkRouteSendContext, ct)` |
| `IZLinkRouteRequestHandler<TRequest, TReply>` | route mesh channel 의 요청 수신 handler. `HandleAsync(req, ZLinkRouteRequestContext, ct) → ValueTask<TReply>` |

검증: `ChannelContracts.Route_client_addresses_a_target_node_through_a_router_channel`.

### 1.3 fanout publisher

```csharp
await publisher
    .Publish("events", "room.opened", new RoomEvent("opened")) // IZLinkPublishCall
    .PacketName("room.event")
    .Submit();
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkFanoutPublisher` | pub/sub publish 표면. `Publish(channelName, topic, message)` (인자 3개) |
| `IZLinkEventPublisher` | `IZLinkFanoutPublisher` 의 호환 별칭. 새 코드는 capability 이름과 맞는 `IZLinkFanoutPublisher` 사용 |
| `IZLinkPublishCall` | publish 종결자(`PacketName` → `Submit`) |

검증: `ChannelContracts.Fanout_publisher_publishes_events_to_a_topic`.

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

`OnDispatchAsync(...)` 의 `payload` 는 framework runtime 이 callback 동안 빌려준
값이다. session 은 `Dispose()` 나 `Move()` 를 기본 사용법으로 쓰지 않고,
`IZLinkSessionActor.RelayAsync(...)` 에도 그대로 넘긴다. raw `IZLinkStream.Write(...)` 에
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
codecs.AddMessagePack();
codecs.AddProtobuf();
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkCodecRegistryBuilder` | payload 직렬화 codec 활성화(`AddJson` / `AddMessagePack` / `AddProtobuf`). `options.Codecs` 로 접근 |

검증: `CodecContracts.Codec_registry_builder_declares_the_codecs_an_application_enables`.

### 1.6 웹 백엔드 — gRPC 대체 시나리오

게임 도메인뿐 아니라 일반 웹/마이크로서비스 백엔드에서도 같은 channel messaging
표면이 **gRPC 의 대체재**가 된다. 서비스마다 host:port 를 알리거나 앞단에 gateway/
로드밸런서를 두지 않고, 논리 `channel name` + discovery 로 서비스 간 호출을 묶는다.

```csharp
// gRPC unary RPC -> request/response
var placed = await orders
    .Request("orders", new PlaceOrder("order-1042", "acct-77", 18742))
    .PacketName("orders.place")
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync<OrderPlaced>();

// gRPC unary returning Empty -> one-way send (응답 안 기다림)
await orders
    .Send("inventory", new ReserveStock("order-1042", "sku-9", 3))
    .PacketName("inventory.reserve")
    .Submit();

// gRPC server-streaming / 상태 피드 -> pub/sub fan-out
await events
    .Publish("order.events", "order.status", new OrderStatusChanged("order-1042", "Placed"))
    .PacketName("order.status-changed")
    .Submit();
```

| gRPC 패턴 | ZLink 대체 | 표면 / 챕터 |
|-----------|------------|-------------|
| Unary RPC | request/response | `IZLinkClient.Request(...).SubmitAsync<TReply>` ([04](./04-channel-messaging.ko.md)) |
| Unary `Empty` / fire-and-forget | one-way send | `IZLinkClient.Send(...).Submit` ([04](./04-channel-messaging.ko.md)) |
| Server streaming / 이벤트 피드 | pub/sub fan-out | `IZLinkFanoutPublisher.Publish` + `IZLinkPublishHandler<T>` ([04](./04-channel-messaging.ko.md)) |
| Client/Bidi streaming | STREAM session | `IZLinkSession`/`IZLinkSessionContext` ([07](./07-stream.ko.md), §5) |
| Service discovery(DNS/xDS) | Registry + Discovery | `UseDiscovery` + `IZLinkRegistryQuery` ([08](./08-registry.ko.md), §6) |
| Interceptor | handler filter | `IZLinkHandlerFilter` ([04](./04-channel-messaging.ko.md) §5, §1.4) |
| Deadline/timeout | request timeout | `IZLinkRequestCall.Timeout(...)` (§1.1) |
| Metadata/trailer | metadata 정책 | `ConfigureMetadata` + `IZLinkMessageMetadataPolicy` (§2.1, §5.2) |

> proto 컴파일·HTTP/2 전용 인프라·`.proto` IDL 없이, DTO(record)와 typed handler
> 만으로 같은 4가지 호출 형태를 얻는다. gRPC↔ZLink 사용 흐름 비교는
> [04-channel-messaging](./04-channel-messaging.ko.md) §0 이 더 자세히 다룬다.

검증: `ChannelContracts.Channel_messaging_replaces_grpc_unary_command_and_streaming_for_web_services`.

## 2. Configuration — options · builder · capability · 연결

> 사용법은 [02-getting-started](./02-getting-started.ko.md),
> [04-channel-messaging](./04-channel-messaging.ko.md) §3·§6,
> [05-spot](./05-spot.ko.md) §2. 검증 클래스는 `BuilderContracts`,
> `ConnectionAndConfigContracts`.

### 2.1 최상위 옵션 표면

`AddZLinkFramework(options => ...)` 의 `options` 가 구현하는 표면이다.

```csharp
options.DefaultTimeout = TimeSpan.FromSeconds(5);
options.Codecs.AddJson();
options.AddHandlersFromAssemblyOf<Program>();
options.ConfigureMetadata(metadata => metadata.ForwardApplicationKey("trace-id"));
options.AddActorFactory<PlayerActorFactory>("player");
options.AddSpotRemoteAddressResolver<MySpotResolver>();
options.UseRegistrySpotRemoteAddresses("game", registry => registry.RouterChannelId = "play-router");
options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:6000"));
options.AddSpotMesh("play-spots", mesh => mesh.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:6001")));
options.UseFilter<AuditingFilter>();
options.ConfigureDispatch(dispatch => dispatch.SpotDispatchMode = ZLinkDispatchMode.Compiled);
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkFrameworkOptions` | framework 최상위 등록 표면. channel/spot/stream node 등록, codec, handler scan, discovery, filter, dispatch, actor factory 를 모두 소유 |
| `IZLinkDiscoveryBuilder` | discovery endpoint 추가(`Add(endpoint)`). 전역 `UseDiscovery` 또는 mesh `UseDiscovery` 에서 쓰인다 |
| `IZLinkMetadataPolicyBuilder` | 응용 metadata 전달 정책(`ForwardApplicationKey(key)`) |
| `IZLinkRegistrySpotRemoteAddressesOptions` | Registry 기반 spot 주소 해석 옵션(`RouterChannelId`) |

검증: `BuilderContracts.Framework_options_register_the_top_level_runtime_surface`.

### 2.2 channel builder 와 capability

```csharp
options.AddClientServerChannel("api", channel =>
{
    channel.EnableServer(server =>          // IChannelServerCapabilityBuilder
    {
        server.Bind("tcp://127.0.0.1:5000");
        server.ConfigureSocket(socket => socket.TcpNoDelay = true);
        server.ConfigureRouting(route => route.RoutingId = RoutingId.Of("api-server"));
    });
    channel.EnableClient(client =>          // IChannelClientCapabilityBuilder
        client.UseManualConnections(c => c.Connect("tcp://127.0.0.1:5000")));
    channel.AddRequestHandler<ApiRequestHandler, ApiRequest, ApiReply>();
    channel.EnableSpotRouteEgress("play-spots");
});

options.AddFanoutChannel("events", channel => { channel.EnablePublisher(); channel.EnableSubscriber(); });
options.AddDealerMeshChannel("mesh", channel => channel.EnableClient());
options.AddRouteMeshChannel("play-router", channel => channel.Bind("tcp://127.0.0.1:5300"));
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkClientServerChannelBuilder` | client-server channel(`EnableServer`/`EnableClient`, request/send handler, `EnableSpotRouteEgress`) |
| `IZLinkFanoutChannelBuilder` | fanout channel(`EnablePublisher`/`EnableSubscriber`, publish handler) |
| `IZLinkDealerMeshChannelBuilder` | dealer mesh channel(`EnableClient`) |
| `IZLinkRouteMeshChannelBuilder` | route mesh channel(`Bind`, routing/socket, route send/request handler, `EnableSpotRouteEgress`) |
| `IZLinkRouteChannelBuilder` | route channel 공통 빌더 표면 |
| `IChannelServerCapabilityBuilder` | server capability(`Bind`, `ConfigureSocket`, `ConfigureRouting`) |
| `IChannelClientCapabilityBuilder` | client capability(`ConfigureSocket`, `ConfigureRouting`, `UseManualConnections`) |
| `IDealerMeshChannelClientCapabilityBuilder` | dealer mesh client capability(`Bind` 포함) |
| `IChannelPublisherCapabilityBuilder` | publisher capability(`Bind`, `ConfigureSocket`) |
| `IChannelSubscriberCapabilityBuilder` | subscriber capability(`ConfigureSocket`, `UseManualConnections`) |
| `IRouteChannelConnections` | route mesh channel 의 수동 연결 집합(`Connect`/`Disconnect`/`ListConnections`) |

검증: `BuilderContracts.Channel_builders_expose_only_the_handlers_and_capabilities_valid_for_that_channel`.

### 2.3 spot node · stream node · spot mesh builder

```csharp
options.AddStreamNode("gateway", stream =>
{
    stream.Bind("tcp://127.0.0.1:5400");
    stream.RegisterSession<GatewaySession>();
});

options.AddSpotMesh("play-mesh", mesh =>
{
    mesh.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:6003"));
    mesh.AddNode("play-spots", spot =>
    {
        spot.EnableRouter(router =>
        {
            router.SetRouterBind("tcp://127.0.0.1:5501");
            router.SetRoutingId(RoutingId.Of("spot-router"));
        });
        spot.EnablePubSub(pubSub =>
        {
            pubSub.SetPubBind("tcp://127.0.0.1:5500");
            pubSub.ConfigurePublisherConfig(p => p.NoDrop = true);
        });
        spot.AttachChannelClient("api", c => c.ConfigureSocket(s => s.Immediate = true));
        spot.AttachClientServerChannelClient("api");
        spot.AttachSpotPublisherClient("events");
        spot.AttachSpotPublisherClient("mesh-events");
        spot.AcceptSpotRoutesFromChannel("play-router", a =>
            a.UseManualConnections(c => c.Connect("tcp://127.0.0.1:5300")));
        spot.ConfigureEntrySpot(entry => entry.RoutingId = RoutingId.Of("entry"));
        spot.AddSpotFactory<RoomSpot>();
        spot.AddEntrySpot<EntrySpot>();
    });
    mesh.AddNode("session-node", spot =>
    {
        spot.EnableRouter(router =>
        {
            router.SetRouterBind("tcp://127.0.0.1:5601");
            router.ConfigureRouting(r => r.RoutingId = RoutingId.Of("session-gateway"));
        });
    });
});
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkStreamNodeBuilder` | stream node(`Bind`, `AttachActorGateway`, `RegisterSession<TSession>`) |
| `IZLinkSpotNodeBuilder` | spot node 등록 표면(`Bind`, router/pubsub, channel attach, route 수용, entry/spot factory) |
| `IZLinkSpotMeshNodeBuilder` | spot mesh 안의 노드 빌더(`IZLinkSpotNodeBuilder` 와 같은 표면, mesh 컨텍스트) |
| `IZLinkSpotMeshBuilder` | discovery 기반 spot mesh(`UseDiscovery`, `AddNode`) |
| `IZLinkSpotRouteChannelAcceptanceBuilder` | `AcceptSpotRoutesFromChannel` 의 ingress 연결 설정(`UseManualConnections`) |
| `ISpotRouterCapabilityBuilder` | spot router capability(`Bind`/`ConfigureSocket`/`ConfigureRouting`/`UseManualConnections`) |
| `ISpotPubSubCapabilityBuilder` | spot pub/sub capability(`ConfigurePublisherConfig`/`ConfigureSubscriberConfig`/`UseManualConnections`) |
| `ISpotPublisherClientCapabilityBuilder` | spot publisher client attach 설정 |
| `ISpotChannelClientCapabilityBuilder` | spot 의 일반 channel client attach 설정 |
| `ISpotRouterChannelConnections` | spot route ingress 수동 연결 집합 |

검증: `BuilderContracts.Spot_and_stream_builders_declare_node_local_roles_and_channel_attachments`.

### 2.4 연결 집합 — role 별 endpoint 소유

수동 연결은 capability(role) 단위로 별개 집합을 소유한다. 같은 endpoint 목록이라도
client·subscriber·spot router 가 서로 다른 연결 집합이다.

```csharp
channelClient.Connect("tcp://127.0.0.1:5001");   // IChannelClientConnections
subscriber.Connect("tcp://127.0.0.1:5002");       // IChannelSubscriberConnections
spotRouter.Connect("tcp://127.0.0.1:5003");       // ISpotRouterConnections
spotPubSub.Connect("tcp://127.0.0.1:5004");        // ISpotPubSubConnections
spotPublisher.Connect("tcp://127.0.0.1:5005");     // ISpotPublisherConnections
spotRouteIngress.Connect("tcp://127.0.0.1:5006");  // ISpotRouterChannelConnections
```

| 인터페이스 | 역할 |
|------------|------|
| `IChannelClientConnections` | client capability 수동 연결(`Connect`/`Disconnect`/`ListConnections`) |
| `IChannelSubscriberConnections` | subscriber capability 수동 연결 |
| `ISpotRouterConnections` | spot router 수동 연결 |
| `ISpotPubSubConnections` | spot pub/sub 수동 연결 |
| `ISpotPublisherConnections` | spot publisher client 수동 연결 |
| `ISpotRouterChannelConnections` | spot route ingress 수동 연결 |

검증: `ConnectionAndConfigContracts.Connection_contracts_record_the_startup_endpoints_owned_by_each_runtime_role`.

### 2.5 socket · routing · spot · dispatch 설정

```csharp
var socket = new SocketConfig { TcpNoDelay = true, Immediate = true, MaxMessageSize = 1024, /* ... */ };
var route = new RouteConfig { RoutingId = RoutingId.Of("router"), RequireKnownPeer = true };
var outbound = new OutboundRouteConfig { RoutingId = RoutingId.Of("client"), ProbeRouterOnConnect = true };
var publisher = new SpotPublisherConfig { SendHighWaterMark = 32, NoDrop = true };
var subscriber = new SpotSubscriberConfig { ReceiveHighWaterMark = 64 };
var entry = new EntrySpotOptions { RoutingId = RoutingId.Of("entry") };
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

검증: `ConnectionAndConfigContracts.Configuration_contracts_keep_socket_routing_spot_and_dispatch_options_typed`.

## 3. SPOT — spot · context · client · handler

> 사용법은 [05-spot](./05-spot.ko.md). 검증 클래스는 `SpotContracts`.

### 3.1 spot 인스턴스와 context

```csharp
public sealed class RoomSpot(IZLinkSpotContext context) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddPacket<RoomPacketHandler>();
        Context.AddSubscribe<RoomEventHandler>("room.events");
        Context.AddActorPacket<PlayerActorPacketHandler, PlayerActor>();
        Context.AddActorJoin<PlayerJoinHandler, PlayerActor, JoinRoom, JoinedRoom>();
        Context.AddPostActorJoined<PlayerJoinedHandler, PlayerActor>();
        Context.AddActorLeft<PlayerLeftHandler, PlayerActor>();
        Context.AddActorDisconnected<PlayerDisconnectedHandler, PlayerActor>();
    }

    public async ValueTask OnInitializeAsync(CancellationToken ct)
    {
        await Context.AddTimer<RoomTimerHandler>("heartbeat", TimeSpan.FromSeconds(1));
        await Context.SendSpot(RoutingId.Of("room-2"), new RoomEvent("opened")).Submit(ct);
        await Context.RequestSpot(RoutingId.Of("room-2"), new JoinRoom("room-2")).SubmitAsync<JoinedRoom>(ct);
        await Context.Publish("room.events", new RoomEvent("opened")).Submit(ct);
        await Context.SendChannel("api", new RoomEvent("opened")).Submit(ct);
        await Context.RequestChannel("api", new JoinRoom("room-1")).SubmitAsync<JoinedRoom>(ct);
    }
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpot` | user spot 인스턴스. `Context` + lifecycle(`Configure`/`OnCreateAsync`/`OnInitializeAsync`/`PostActorJoined handler`/`ActorLeft handler`/`OnClosingAsync`) |
| `IZLinkEntrySpot` | Entry Spot 인스턴스. `IZLinkEntrySpotContext` + lifecycle |
| `IZLinkSpotContext` | user spot context. handler registry + outbound + `SpotRid`/`NodeRid` + `LeaveActorAsync` + `AddTimer` |
| `IZLinkEntrySpotContext` | Entry Spot context. handler registry + outbound + `SpotRid`/`NodeRid` + `AddTimer` |
| `IZLinkActorHandlerRegistry` | actor handler 등록(`AddHandler`, `AddActorPacket`, `AddPostActorJoined`, `AddActorLeft`, `AddActorDisconnected`) |
| `IZLinkSpotHandlerRegistry` | `IZLinkActorHandlerRegistry` + spot packet/subscribe/actor-join(`AddPacket`, `AddSubscribe`, `AddActorJoin`) |
| `IZLinkSpotOutboundContext` | spot 안 outbound(`SendSpot`, `RequestSpot`, `Publish(topic, msg)`, `SendChannel`, `RequestChannel`) |
| `IZLinkTimer` | 등록된 timer 핸들. `CancelAsync()` / `DisposeAsync()` (§8 도 참조) |

검증: `SpotContracts.Spot_context_registers_handlers_timers_actor_lifecycle_and_outbound_messages`.

### 3.2 spot client — local · routed · publisher

```csharp
// 현재 노드의 spot API
await localClient.SendSpot(spotRid, new RoomEvent("opened")).Submit();        // IZLinkSpotClient
var reply = await localClient.RequestSpot(spotRid, new JoinRoom("room-1")).SubmitAsync<JoinedRoom>();

// current Spot 밖에서 routed 호출
await routedClient
    .ViaEgressChannel("play-router")                                          // IZLinkRoutedSpotEgressClient
    .RequestSpot(remoteAddress.SpotRid, new JoinRoom("room-1"))
    .SubmitAsync<JoinedRoom>();

// local spot 없는 노드에서 publish
await publisher.Publish("play-events", "room.events", new RoomEvent("opened")).Submit(); // IZLinkSpotPublisherClient
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpotManager` | spot 인스턴스 생성/조회(`CreateAsync`, `GetOrCreateAsync`, `GetAsync`, `ListAsync`, `RemoveAsync`) |
| `IZLinkSpotClient` | current Spot callback 안에서의 outbound(`SendSpot`/`RequestSpot`/`SendChannel`/`RequestChannel`/`Publish`) |
| `IZLinkRoutedSpotClient` | current Spot 없는 코드의 spot 호출. `ViaEgressChannel` 로 local egress 선택 |
| `IZLinkRoutedSpotEgressClient` | egress 선택 후의 routed 호출 표면(`SendSpot`/`RequestSpot`, `RoutingId`) |
| `IZLinkSpotPublisherClient` | local spot 없는 노드의 spot channel publish(`Publish(channelName, topic, msg)`) |
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

public sealed class PlayerJoinHandler
    : IZLinkSpotActorJoinHandler<RoomSpot, PlayerActor, JoinRoom, JoinedRoom>
{
    public ValueTask<JoinedRoom> HandleAsync(
        RoomSpot spot, PlayerActor actor, JoinRoom request, CancellationToken ct)
        => ValueTask.FromResult(new JoinedRoom(request.RoomId));
}
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSpotPacketHandler<TSpot, TMessage>` | spot 단방향 packet handler. 첫 인자 = spot |
| `IZLinkSpotRequestHandler<TSpot, TRequest, TReply>` | spot 요청 handler. 반환값이 응답 |
| `IZLinkSpotSubscriptionHandler<TSpot, TMessage>` | spot 구독 topic 수신 handler |
| `IZLinkSpotTimerHandler<TSpot>` | spot timer tick handler(`ZLinkTimerTick`) |
| `IZLinkSpotActorJoinHandler<TSpot, TActor, TRequest, TReply>` | user spot join 요청 handler. (spot, actor, request) |
| `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>` | user spot actor 단방향 handler. context 뒤에 payload |
| `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` | user spot actor 요청 handler. context 뒤에 request |
| `IZLinkSpotPostActorJoinedHandler<TSpot, TActor>` | user spot actor join 완료 lifecycle(`ZLinkSpotActorChangeResult`) |
| `IZLinkSpotActorLeftHandler<TSpot, TActor>` | user spot actor leave lifecycle |
| `IZLinkSpotActorDisconnectedHandler<TSpot, TActor>` | user spot actor disconnect notification. membership 은 변경하지 않음 |
| `IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>` | Entry Spot actor 단방향 handler. context 뒤에 payload |
| `IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>` | Entry Spot actor 요청 handler. context 뒤에 request |
| `IZLinkSpotPostActorJoinedHandler<TEntrySpot, TActor>` | Entry Spot actor join lifecycle |
| `IZLinkSpotActorLeftHandler<TEntrySpot, TActor>` | Entry Spot actor leave lifecycle |
| `IZLinkEntrySpotActorDisconnectedHandler<TEntrySpot, TActor>` | Entry Spot actor disconnect notification. membership 은 변경하지 않음 |

검증: `SpotContracts.Spot_handlers_receive_the_spot_instance_and_actor_when_the_contract_requires_it`.

## 4. Actor — actor · context · factory · handler

> 사용법은 [06-actor-session](./06-actor-session.ko.md). 검증 클래스는 `ActorContracts`.

### 4.1 actor 와 lifecycle

```csharp
var actor = await manager.GetOrCreateAsync("player-1", "player"); // IZLinkActorManager
var joinReply = await actor.Context
    .JoinSpot(RoutingId.Of("room-1"), new JoinRoom("room-1")) // IZLinkActorContext
    .SubmitAsync<JoinedRoom>();
if (joinReply.ResultCode != 0)
{
    return;
}
actor.Configure();
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkActor` | ID 로 식별되는 상태 보유 actor. `ActorId`, `Context` |
| `IZLinkActorContext` | actor 의 상태/동작 표면. `ActorId`/`SessionId?`/`SpotRid?`/`IsJoined`, `BoundSession`, `JoinSpot`/`JoinEntrySpot`, `GetSpot<T>` |
| `IZLinkActorJoinSpotCall` | `JoinSpot(...)` 종결자(`Timeout` → `SubmitAsync<TReply>`) |
| `IZLinkActorFactory` | `actorType` 별 actor 생성(`CreateAsync(actorId, context, ct)`) |
| `IZLinkActorManager` | actor 생성/조회(`CreateAsync`, `FindAsync`, `GetOrCreateAsync`) |

검증: `ActorContracts.Actor_context_creates_actors_and_joins_a_spot_by_routing_id`.

### 4.2 actor handler 와 Spot actor handler

```csharp
// actor fallback handler: actor 인스턴스를 첫 인자로 받는다.
public sealed class ActorRequestHandler
    : IZLinkActorRequestHandler<PlayerActor, PlayerRequest, PlayerReply>
{
    public ValueTask<PlayerReply> HandleAsync(
        PlayerActor actor, PlayerRequest request, CancellationToken ct)
        => ValueTask.FromResult(new PlayerReply($"actor:{actor.ActorId}:{request.Value}"));
}

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
| `IZLinkActorPacketHandler<TActor, TMessage>` | actor fallback 단방향 handler. 첫 인자 = actor |
| `IZLinkActorRequestHandler<TActor, TRequest, TReply>` | actor fallback 요청 handler. 첫 인자 = actor |
| `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>` | user Spot actor 단방향 handler. Spot, actor, context, payload 순서 |
| `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` | user Spot actor 요청 handler. Spot, actor, context, payload 순서 |
| `IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>` | Entry Spot actor 단방향 handler. Entry Spot, actor, context, payload 순서 |
| `IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>` | Entry Spot actor 요청 handler. Entry Spot, actor, context, payload 순서 |
| `ZLinkSpotActorReplyOptions` | Spot actor request 응답 frame 옵션. `context.Reply.Metadata(...).Compress()` |

검증: `ActorContracts.Actor_handlers_receive_the_actor_instance`,
`SpotContracts.Spot_actor_handlers_receive_context_before_payload`.

## 5. STREAM session — session · context · push · bound session

> 사용법은 [07-stream](./07-stream.ko.md), [06-actor-session](./06-actor-session.ko.md) §4.
> 검증 클래스는 `StreamContracts`.

### 5.1 session 과 session context

```csharp
public sealed class ClientHeaderSession(IZLinkSessionContext context) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public async ValueTask OnDispatchAsync(ZlinkStreamHeader header, Message payload, CancellationToken ct)
    {
        var actorRef = await context.BindActorAsync("player-1", ct); // ActorDispatch
        if (context.FindActor(actorRef.ActorId) is { } boundActor)
        {
            await boundActor.RelayAsync(header, payload, ct);
        }

        await context.Send(new PlayerJoined("player-1"))      // ClientStream → IZLinkSessionSendCall
            .PacketName("player.joined").Metadata("trace-id", "abc").Compress().Submit(ct);
        await context.Reply(new AuthenticateReply("player-1")) // IZLinkSessionReplyCall
            .Metadata("trace-id", "abc").Compress().Submit(ct);
        await context.CloseAsync(ct);                          // Lifecycle
    }

    public ValueTask OnConnectedAsync(CancellationToken ct) => ValueTask.CompletedTask;
    public ValueTask OnDisconnectedAsync(CancellationToken ct) => ValueTask.CompletedTask;
    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken ct) => ValueTask.CompletedTask;
}
```

`OnDispatchAsync(...)` 로 받은 `payload` 는 callback 동안만 빌린 값이다.
session 은 이 값을 직접 해제하거나 `Move()` 로 소비하지 않는다.
`IZLinkSessionActor.RelayAsync(...)` 는 caller payload 를 소비하지 않으므로 받은 값을 그대로
넘기면 된다. callback 뒤에도 보관해야 할 때만 별도 `Copy()` 또는 명시적인
소유권 이전을 선택한다.

session 전용 packet handler 를 나누고 싶을 때는
`IZLinkSessionPacketDispatcher<TSessionContext>` 를 session 생성자에 주입한다.
dispatcher 는 `IZLinkSessionPacketHandler<TSessionContext>` 구현 중 packet name 이
맞는 handler 만 호출하고, 미등록 packet 은 `false` 로 돌려준다. 미등록 packet 을
actor 로 relay 할지, 거절할지, 로그만 남길지는 application session 이 결정한다.

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkSession` | stream session. `Context` + `OnConnectedAsync`/`OnDisconnectedAsync`/`OnErrorAsync` + 기본 구현 `OnDispatchAsync` |
| `IZLinkSessionContext` | 아래 4개 sub-context 의 합성 |
| `IZLinkSessionIdentityContext` | session 식별(`SessionId`, `RoutingId?`, `LocalAddr?`, `RemoteAddr?`) |
| `IZLinkSessionClientStream` | client 로의 push(`Send(msg)`) / 요청 응답(`Reply(msg)`) |
| `IZLinkSessionActorBindingContext` | actor binding/lookup(`BoundActors`, `BindActorAsync`, `BindActorAsync(ActorRef, ...)`, `FindActor`) |
| `IZLinkSessionActor` | session-bound actor handle. `RelayAsync`, `NotifyDisconnectedAsync` 로 대상 actor 에게 명시 동작을 보냄 |
| `IZLinkSessionLifecycle` | 서버 측 연결 종료(`CloseAsync`) |
| `IZLinkSessionPacketHandler<TSessionContext>` | session 이 직접 처리할 packet handler. payload 는 session callback 과 같은 borrowed lifetime |
| `IZLinkSessionPacketDispatcher<TSessionContext>` | 등록된 session packet handler 만 호출하고 미등록 packet 은 `false` 반환 |
| `IZLinkSessionSendCall` | session push 종결자(`Metadata`/`PacketName`/`Compress` → `Submit`) |
| `IZLinkSessionReplyCall` | session reply 종결자(`Metadata`/`Compress` → `Submit`) |
| `IZLinkSessionActor` | session relay 용 actor handle(`ActorId`, `Ref`, `RelayAsync`, `NotifyDisconnectedAsync`) |
| `IZLinkStream` | raw stream write(`Write(Message, SendFlags)`) / `CloseAsync`. 보통은 `Send`/`Reply` 를 쓴다 |

검증: `StreamContracts.Session_context_collects_identity_stream_and_actor_operations`.

### 5.2 bound session — actor 가 자기 client 로 push

```csharp
await boundSession.Send(new PlayerJoined("player-1"))    // IZLinkBoundSessionSendCall
    .PacketName("player.joined").Metadata("trace-id", "abc").Submit();
await boundSession.DisconnectAsync();

// 전달 가능한 metadata key 정책
IZLinkMessageMetadataPolicy policy = /* ... */;
policy.CanForwardApplicationKey("trace-id");   // true / false
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkBoundSession` | actor 에 묶인 client 로의 단방향 push. `Send<TMessage>(message)` + `DisconnectAsync`. (요청 표면 없음 — push 는 단방향) |
| `IZLinkBoundSessionSendCall` | bound session push 종결자(`PacketName`/`Metadata` → `Submit`) |
| `IZLinkMessageMetadataPolicy` | 응용 metadata key 전달 허용 여부(`CanForwardApplicationKey`) |

검증: `StreamContracts.Bound_session_sends_to_the_bound_session_without_exposing_stream_transport`.

## 6. Registry — status · topology · client options

> 사용법은 [08-registry](./08-registry.ko.md). 검증 클래스는 `RegistryContracts`.

```csharp
options.PubEndpoint = "tcp://127.0.0.1:6001";   // IZLinkRegistryOptions
options.RouterEndpoint = "tcp://127.0.0.1:6002";
options.AddPeer("tcp://127.0.0.1:7001");

var clientOptions = new ExampleRegistryQueryClientOptions { Endpoint = options.RouterEndpoint };

var status = await query.StatusSnapshotAsync();                 // IZLinkRegistryQuery
var topology = await query.TopologyQueryAsync(new ZLinkRegistryTopologyFilter(ChannelName: "play"));
var snapshot = await client.SnapshotAsync();                    // IZLinkRegistryQueryClient
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkRegistryOptions` | Registry 서버 설정(`PubEndpoint`, `RouterEndpoint`, `RegistryId`, heartbeat/broadcast interval, `AddPeer`) |
| `IZLinkRegistryQuery` | in-process Registry 조회(`StatusSnapshotAsync`, `ServiceSummarySnapshotAsync`, `TopologySnapshotAsync`, `TopologyQueryAsync`, `MemberPeersAsync`) |
| `IZLinkRegistryQueryClient` | 원격 Registry 조회. topology `SnapshotAsync` 만 제공(C API 제약) |
| `IZLinkRegistryQueryClientOptions` | 원격 query client 의 `Endpoint`(ROUTER) |

검증: `RegistryContracts.Registry_contracts_describe_status_topology_and_client_options`.

## 7. Monitoring — source 등록과 runtime event handler

> 사용법은 [09-monitoring](./09-monitoring.ko.md). 검증 클래스는 `MonitoringContracts`.

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

검증: `MonitoringContracts.Monitoring_contracts_wire_event_sources_to_typed_runtime_handlers`.

## 8. Timer — 등록된 timer 핸들

> 사용법은 [05-spot](./05-spot.ko.md) §3. 검증 클래스는 `TimerContracts`.

```csharp
await using var timer = await Context.AddTimer<RoomTimerHandler>("heartbeat", TimeSpan.FromSeconds(1));
await timer.CancelAsync();   // IZLinkTimer
```

| 인터페이스 | 역할 |
|------------|------|
| `IZLinkTimer` | `AddTimer<THandler>(...)` 가 돌려주는 timer 핸들. `CancelAsync()` 로 중지, `IAsyncDisposable` 로 정리 |

검증: `TimerContracts.Timer_contract_allows_spot_code_to_cancel_a_registered_timer`.

## 9. 완전성 — 카탈로그가 곧 계약 표면

| 영역(namespace) | 검증 클래스 | 시나리오 수 |
|-----------------|-------------|:-----------:|
| `Channels` | `ChannelContracts` | 3 |
| `Handlers` | `HandlerContracts` | 1 |
| `Codecs` | `CodecContracts` | 1 |
| `Configuration` | `BuilderContracts` · `ConnectionAndConfigContracts` | 5 |
| `Spots` | `SpotContracts` | 3 |
| `Actors` | `ActorContracts` | 2 |
| `Streams` | `StreamContracts` | 2 |
| `Registry` | `RegistryContracts` | 1 |
| `Monitoring` | `MonitoringContracts` | 1 |
| `Timers` | `TimerContracts` | 1 |
| `Dispatch` | `ConnectionAndConfigContracts`(`IZLinkDispatchOptions`) | (위 5 에 포함) |

`ContractSurfaceCoverage` 가 위 시나리오의 `[ContractExample(...)]` 합집합 ==
`Zlink.Framework.Contracts.*` 의 public interface 집합임을 단언한다. 따라서 이
카탈로그에 빠진 인터페이스가 있으면 빌드가 실패하고, 새 인터페이스를 추가하면
계약 테스트와 이 문서가 함께 갱신돼야 한다.

> client 측 Stream Connector(`Systems.Zlink.Stream.Connector`)의 `Zlink*` 타입은
> 별도 client 라이브러리라 이 카탈로그(서버 framework 계약)에 포함되지 않는다.
> connector 표면은 [07-stream](./07-stream.ko.md) §2 와
> [samples/streaming-client](./samples/streaming-client.ko.md)가 다룬다.

## 10. 더 보기

- 언어 중립 정식 정의: [spec/handler-interfaces](../spec/handler-interfaces.ko.md)
- 기능 선택 지도: [10-feature-map](./10-feature-map.ko.md)
- 계약 테스트 소스: `framework/languages/dotnet/tests/Zlink.Framework.ContractTests`
