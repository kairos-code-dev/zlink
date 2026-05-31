[← 운영](./04-operations.md) · [Node.js 가이드](./index.md)

# 레퍼런스

---

## 에러 처리

Node 바인딩은 작업별 에러 클래스를 던집니다.

```javascript
try {
  socket.send().message(Buffer.from('data')).submit();
} catch (error) {
  if (error instanceof zlink.SubmitError) {
    if (error.result === zlink.SubmitResult.Backpressured) {
      // 재시도
    } else {
      throw error;
    }
  }
}
```

에러 클래스: `SubmitError`, `RequestError`, `RecvError`, `BindError`,
`ConnectError`, `ConfigError`, `CloseError`, `HandlerError`.
각각 `.result` 속성으로 결과 코드를 노출합니다.

---

## 코덱

```bash
npm install @zlink-systems/zlink-codec-json
npm install @zlink-systems/zlink-codec-messagepack
npm install @zlink-systems/zlink-codec-protobuf
```

---

## C API ↔ Node 대응표

| C API | Node API |
|-------|----------|
| `zlink_ctx_new()` | `zlink.createContext()` |
| `zlink_ctx_term()` | `ctx.close()` |
| `zlink_socket(ctx, type)` | `zlink.createPairSocket(ctx)` 등 |
| `zlink_bind(s, ep)` | `socket.bind(ep)` |
| `zlink_connect(s, ep)` | `socket.connect(ep)` |
| `zlink_send_part(...)` | `socket.send().message(buf).submit()` |
| `zlink_recv(...)` | `socket.recv(received)` |
| `zlink_msg_data(msg)` | `part.data()` (Buffer) |
| `zlink_routing_id_t` | `zlink.RoutingId` |
| `zlink_socket_monitor_open(...)` | `socket.monitorOpen([...])` |
| `zlink_poller_new()` | `zlink.createPoller()` |
| `zlink_timer_new()` | `zlink.createTimer()` |
| `zlink_spot_node_new(ctx)` | `zlink.createSpotNode(ctx)` |
| `zlink_registry_new(ctx)` | `zlink.createRegistry(ctx)` |
| `zlink_discovery_new(ctx,...)` | `zlink.createDiscovery(ctx, type, ch)` |

---

## API 레퍼런스 생성 (TypeDoc)

```bash
cd bindings/node
npm run docs
# docs/api/index.html 에서 확인
```

TypeScript 타입 정의는 패키지에 포함되어 있어 에디터 자동완성이 동작합니다.

---

## 샘플 목록

`bindings/node/samples/` 디렉터리의 검증된 샘플입니다.

| 파일 | 설명 |
|------|------|
| `pair_recv_sample.ts` | PAIR 송수신 |
| `dealer_router_recv_sample.ts` | DEALER/ROUTER 송수신 |
| `request_reply_async_sample.ts` | 비동기 요청/응답 |
| `pubsub_recv_sample.ts` | PUB/SUB 발행·구독 |
| `stream_recv_sample.ts` | STREAM 원시 TCP |
| `stream_packet_callback_sample.ts` | STREAM 패킷 콜백 |
| `monitor_recv_sample.ts` | 모니터 이벤트 수신 |
| `discovery_registry_sample.ts` | Registry + Discovery |
| `registry_query_sample.ts` | Registry 토폴로지 쿼리 |
| `spot_recv_sample.ts` | SpotNode/Spot PUB/SUB |
| `spot_request_async_sample.ts` | SpotNode 비동기 요청 |
| `actor_single_player_queue_sample.ts` | 액터 조인·이동·메시지 큐 |
| `actor_room_server_sample.ts` | 방 서버 패턴 |
| `actor_gateway_relay_sample.ts` | 게이트웨이 릴레이 |

```bash
cd bindings/node
npm run build
node dist-tools/samples/pair_recv_sample.js
```
