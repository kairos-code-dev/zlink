[English](./05-reference.md) | [한국어](./05-reference.ko.md)

[← 운영](./04-operations.ko.md) · [.NET 가이드](./index.ko.md)

# 레퍼런스 — 에러 · 코덱 · C API 대응표 · API 레퍼런스 · 샘플

찾아보기용 문서입니다. 에러 처리·직렬화 같은 .NET 고유 표면과, 다른 언어·C에서
넘어올 때 쓰는 대응표, 그리고 생성형 전체 레퍼런스로 가는 링크를 모았습니다.

---

## 에러 처리

하드 실패는 작업별 타입 예외로 표면화됩니다. 모두 `ZlinkException`을 상속하며
`Code`(정수 코드)와 작업별 `Result`(열거형)를 노출합니다.

```csharp
try
{
    socket.Bind("tcp://127.0.0.1:5555");
}
catch (ZlinkBindException ex) when (ex.Result == ZlinkBindException.ErrorCode.AddrInUse)
{
    Console.Error.WriteLine("포트가 이미 사용 중입니다.");
}
catch (ZlinkException ex)
{
    Console.Error.WriteLine($"zlink 오류 {ex.Code}: {ex.Message}");
    throw;
}
```

| 예외 | 발생 작업 |
|---|---|
| `ZlinkSubmitException` | 송신/발행 (`Submit`) |
| `ZlinkRequestException` | 요청/응답 (`Request`) — `TimedOut` 등 |
| `ZlinkRecvException` | 수신 (`Recv`) |
| `ZlinkBindException` / `ZlinkConnectException` | 바인드/연결 |
| `ZlinkConfigException` | 옵션/설정 |
| `ZlinkCloseException` / `ZlinkHandlerException` | 종료/콜백 |

**데이터 없음·일시적 백프레셔는 예외가 아닙니다.** 논블로킹 수신은 `Recv(...)`가
`false`를, 논블로킹 송신은 `Submit()`이 `false`를 반환하는 것으로 구분하세요:

```csharp
if (!socket.Recv(received, RecvFlags.DontWait)) { /* 데이터 없음 */ }
if (!socket.Send().Message(m).Flags(SendFlags.DontWait).Submit()) { /* 백프레셔 */ }
```

---

## 직렬화 (코덱)

객체를 메시지로 인코딩하려면 선택형 코덱 패키지를 추가합니다. JSON, MessagePack,
Protobuf용이 별도로 제공됩니다.

```bash
dotnet add package Systems.Zlink.Codecs.Json         # 또는 .MessagePack / .Protobuf
```

자세한 내용: [바인딩 코덱 정책](../../../spec/bindings/dotnet/codec.md).

---

## C API ↔ .NET 대응표

C 코어(`zlink.h`)에서 넘어오거나 다른 언어 바인딩과 비교할 때 쓰는 압축 매핑입니다.
.NET은 raw 함수 대신 객체와 플루언트 빌더로 감싸므로 1:1은 아니지만, 개념 단위로는
대응합니다. 전체 C 함수 목록은 [코어 C API 가이드](../../02-core-api.ko.md)를
참고하세요.

| 영역 | C API (`zlink_*`) | .NET |
|------|-------------------|------|
| 컨텍스트 | `zlink_ctx_new` / `zlink_ctx_term` | `Zlink.CreateContext()` / `IContext.Dispose()` |
| 컨텍스트 옵션 | `zlink_ctx_set` / `zlink_ctx_get` | `IContext.Options` (`IoThreads`, `MaxSockets`, …) |
| 소켓 생성 | `zlink_socket(ctx, TYPE)` | `ctx.Create<Type>Socket()` (`CreatePairSocket()` 등) |
| 바인드 / 연결 | `zlink_bind` / `zlink_connect` | `socket.Bind(...)` / `socket.Connect(...)` |
| 연결 해제 | `zlink_disconnect` / `zlink_disconnect_rid` | `socket.Disconnect(string)` / `socket.DisconnectRid(RoutingId)` |
| 소켓 옵션 | `zlink_set_option` / `zlink_get_option` | 소켓별 강타입 속성(`socket.Options`) |
| routing id | `zlink_set_routing_id` / `zlink_get_routing_id` | `socket.SetRoutingId(RoutingId)` / `RoutingId` |
| 메시지 생성 | `zlink_msg_init` / `_init_size` / `_init_data` | `new Message(size)` / `Message.From(...)` |
| 메시지 접근 | `zlink_msg_data` / `zlink_msg_size` | `Message.AsReadOnlySpan()` / `Message.Size` |
| 메시지 해제 | `zlink_msg_close` / `zlink_multipart_close` | `Message.Dispose()` / `Zlink.MultipartClose(parts)` |
| 송신 | `zlink_send_part` (+`_rid`) | `socket.Send().Message(...).Submit()` |
| 수신 | `zlink_recv_part` | `socket.Recv(Received)` |
| 요청 / 응답 | `zlink_dealer_request_part` / `zlink_router_reply_part` | `dealer.Request()....SubmitAsync()` / `router.Reply(...)` |
| 구독 | `zlink_spot_subscribe_part` / `zlink_sub_*` | `socket.SetSubscription(...)` / `socket.Subscribe(TopicMessage)` |
| 모니터 | `zlink_socket_monitor_open` / `_recv` | `socket.MonitorOpen(...)` / `monitor.Recv()` |
| 폴러 / 타이머 | `zlink_poller_*` / `zlink_timer_*` | `Zlink.CreatePoller()` / `Zlink.CreateTimer()` |
| discovery | `zlink_discovery_new` / `_connect_registry` | `ctx.CreateDiscovery(...)` / `IDiscovery.ConnectRegistry(...)` |
| spot node | `zlink_spot_node_new` / `_set_router_bind` | `ctx.CreateSpotNode()` / `node.SetRouterBind(...)` |
| spot | `zlink_spot_new` / `zlink_spot_publish_part` | `node.CreateSpot()` / `spot.Publish(topic)...` |
| actor | `zlink_spot_node_actor_new` / `_actor_join_spot` | `node.CreateActor(id)` / `actor.Join(spot)...` |
| registry | `zlink_registry_new` / `_bind` | `ctx.CreateRegistry()` / `IRegistry.Bind(...)` |
| 프록시 | `zlink_proxy` / `zlink_proxy_steerable` | `Zlink.Proxy(...)` / `Zlink.ProxySteerable(...)` |

> **이름 규칙**: C의 `snake_case`는 .NET에서 `PascalCase`가 됩니다. C의 `*_part`
> 계열(멀티파트 substrate)은 .NET에서 플루언트 빌더의 `.Message(...)` 누적으로
> 표현됩니다 — public 모양은 언어 관례를 따르되 의미 계약은 동일합니다.

---

## API 레퍼런스

전체 API(모든 타입·멤버·XML 주석)는 docfx로 생성합니다. `bindings/dotnet/`에서:

```bash
docfx docfx.json     # → _site/index.html
```

멤버의 권위 있는 전체 목록은 이 레퍼런스이며, 이 가이드는 사용 흐름 중심입니다.

---

## 샘플 프로젝트

`bindings/dotnet/samples/`에 기능별 실행 가능한 예제가 있습니다.

| 샘플 | 다루는 기능 |
|---|---|
| `PairRecv` | PAIR 송수신 |
| `DealerRouterRecv` | DEALER/ROUTER 라우팅 |
| `RequestReplyAsync` | 비동기 요청/응답 |
| `PubSubRecv` | PUB/SUB 토픽 |
| `MonitorRecv` | 소켓 모니터 |
| `StreamRecv`, `StreamPacketCallback` | STREAM + 패킷 콜백 |
| `DiscoveryRegistry`, `RegistryQuery` | 레지스트리/디스커버리 |
| `SpotRecv`, `SpotRequestAsync` | SpotNode/Spot |
| `ActorRoomServer`, `ActorSinglePlayerQueue`, `ActorGatewayRelay` | 액터 |

실행: `./samples/run_samples.sh` (또는 `run_samples.ps1`).

---

## 다음 단계 / 더 보기

- [소켓 패턴](../../03-0-socket-patterns.ko.md) · [트랜스포트](../../04-transports.ko.md) ·
  [서비스](../../07-0-services.ko.md)
- [메시지 API](../../09-message-api.ko.md) · [소켓 옵션](../../12-socket-options.ko.md) ·
  [성능](../../10-performance.ko.md)
