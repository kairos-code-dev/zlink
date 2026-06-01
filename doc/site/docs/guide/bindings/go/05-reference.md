[← 운영](./04-operations.md) · [Go 가이드](./index.md)

# 레퍼런스

에러 처리, C API↔Go 대응표, API 레퍼런스 생성, 샘플 목록을 다룹니다.

---

## 에러 처리

Go 바인딩은 표준 `error` 인터페이스를 반환합니다. 결과 코드가 필요하면 타입
어서션으로 확인합니다.

```go
_, err := socket.Send().Message(msg).Submit(nil)
if err != nil {
    var submitErr *zlink.SubmitError
    if errors.As(err, &submitErr) {
        switch submitErr.Result {
        case zlink.SubmitBackpressured:
            // 백프레셔 — 잠시 후 재시도
        case zlink.SubmitNotConnected:
            // 연결된 피어 없음
        default:
            return err
        }
    }
    return err
}
```

에러 타입:

| 타입 | 설명 | Result 필드 |
|------|------|-------------|
| `*SubmitError` | 전송/발행 실패 | `SubmitResult` |
| `*RequestError` | 요청 실패 | `RequestResult` |
| `*RecvError` | 수신 실패 | `RecvResult` |
| `*BindError` | 바인드 실패 | `BindResult` |
| `*ConnectError` | 연결 실패 | `ConnectResult` |
| `*ConfigError` | 옵션 설정 실패 | `ConfigResult` |
| `*CloseError` | 닫기 실패 | `CloseResult` |
| `*HandlerError` | 핸들러 등록 실패 | `HandlerResult` |

논블로킹 수신에서 메시지가 없는 경우는 에러가 아닙니다:

```go
ok, err := socket.Recv(&received, zlink.RecvFlagsDontWait)
if err != nil { /* 진짜 에러 */ }
if !ok { /* 메시지 없음 */ }
```

---

## C API ↔ Go 대응표

| C API | Go API |
|-------|--------|
| `zlink_ctx_new()` | `zlink.NewContext()` |
| `zlink_ctx_term()` | `ctx.Close()` |
| `zlink_socket(ctx, type)` | `ctx.PairSocket()` 등 |
| `zlink_close(socket)` | `socket.Close()` |
| `zlink_bind(socket, ep)` | `socket.Bind(ep)` |
| `zlink_connect(socket, ep)` | `socket.Connect(ep)` |
| `zlink_send_part(...)` | `socket.Send().Message(m).Submit(nil)` |
| `zlink_recv(...)` | `socket.Recv(&received, flags)` |
| `zlink_msg_data(msg)` | `msg.Data()` |
| `zlink_msg_size(msg)` | `msg.Size()` |
| `zlink_msg_close(msg)` | `msg.Close()` |
| `zlink_routing_id_t` | `zlink.RoutingID` |
| `zlink_socket_monitor_open(...)` | `zlink.OpenSocketMonitor(socket, ...)` |
| `zlink_poller_new()` | `zlink.NewPoller()` |
| `zlink_timer_new()` | `zlink.NewTimer()` |
| `zlink_spot_node_new(ctx)` | `ctx.SpotNode()` |
| `zlink_spot_node_get_or_create_spot(...)` | `node.Spot()` |
| `zlink_spot_node_actor_new(...)` | `node.Actor("id")` |
| `zlink_registry_new(ctx)` | `ctx.Registry()` |
| `zlink_discovery_new(ctx, ...)` | `ctx.Discovery(type, channel)` |

---

## API 레퍼런스 생성

패키지 문서를 로컬에서 확인합니다:

```bash
cd bindings/go

# 터미널에서 바로 확인
go doc ./...
go doc zlink.systems/zlink/contracts.Context

# 브라우저로 확인 (pkgsite)
go install golang.org/x/pkgsite/cmd/pkgsite@latest
pkgsite -http=:6060
# http://localhost:6060 에서 확인
```

---

## 샘플 목록

`bindings/go/samples/` 에 있는 검증된 샘플 코드입니다.

| 샘플 | 설명 |
|------|------|
| `pair_recv_sample` | PAIR 소켓 송수신 |
| `dealer_router_recv_sample` | DEALER/ROUTER 송수신 |
| `request_reply_async_sample` | 비동기 요청/응답 |
| `pubsub_recv_sample` | XPUB/SUB 발행·구독 |
| `stream_recv_sample` | STREAM 원시 TCP |
| `stream_packet_callback_sample` | STREAM 패킷 콜백 |
| `monitor_recv_sample` | 모니터 이벤트 수신 |
| `discovery_registry_sample` | Registry + Discovery 기본 |
| `registry_query_sample` | Registry 토폴로지 쿼리 |
| `spot_recv_sample` | SpotNode/Spot PUB/SUB |
| `spot_request_async_sample` | SpotNode 비동기 요청 |
| `actor_single_player_queue_sample` | 액터 조인/이동/메시지 큐 |
| `actor_room_server_sample` | 방 서버 액터 패턴 |
| `actor_gateway_relay_sample` | 게이트웨이 릴레이 |

샘플 실행:

```bash
cd bindings/go
go run ./samples/pair_recv_sample/...
# 또는 전체 실행
./samples/run_samples.sh
```
