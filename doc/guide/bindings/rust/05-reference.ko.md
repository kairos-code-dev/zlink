[← 운영](./04-operations.ko.md) · [Rust 가이드](./index.ko.md)

# 레퍼런스

---

## 에러 처리

Rust 바인딩은 작업별 에러 타입을 `Result`로 반환합니다.

```rust
match socket.send().message(msg).submit() {
    Ok(()) => {}
    Err(e) => match e.result() {
        zlink::SubmitResult::Backpressured => { /* 재시도 */ }
        zlink::SubmitResult::NotConnected => { /* 연결 없음 */ }
        _ => return Err(e.into()),
    },
}
```

에러 타입: `SubmitError`, `RequestError`, `RecvError`, `BindError`,
`ConnectError`, `ConfigError`, `CloseError`, `HandlerError`.
각 타입은 `result()` 메서드로 결과 코드 enum을 노출합니다.

---

## 코덱

```toml
[dependencies]
zlink-codec-json = "6.0"
zlink-codec-messagepack = "6.0"
zlink-codec-protobuf = "6.0"
```

---

## C API ↔ Rust 대응표

| C API | Rust API |
|-------|----------|
| `zlink_ctx_new()` | `Context::new()` |
| `zlink_ctx_term()` | `drop(ctx)` |
| `zlink_socket(ctx, type)` | `ctx.pair_socket()` 등 |
| `zlink_bind(s, ep)` | `socket.bind(ep)` |
| `zlink_connect(s, ep)` | `socket.connect(ep)` |
| `zlink_send_part(...)` | `socket.send().message(m).submit()` |
| `zlink_recv(...)` | `socket.recv(&mut received, flags)` |
| `zlink_msg_data(msg)` | `part.as_bytes()` |
| `zlink_routing_id_t` | `RoutingId` |
| `zlink_socket_monitor_open(...)` | `SocketMonitor::open(&socket)` |
| `zlink_poller_new()` | `Poller::new()` |
| `zlink_timer_new()` | `Timer::new()` |
| `zlink_spot_node_new(ctx)` | `SpotNode::new(&ctx)` |
| `zlink_registry_new(ctx)` | `Registry::new(&ctx)` |
| `zlink_discovery_new(ctx,...)` | `Discovery::new(&ctx, type, ch)` |

---

## API 레퍼런스 생성 (rustdoc)

```bash
cd bindings/rust
cargo doc --no-deps --open
```

---

## 샘플 목록

`bindings/rust/samples/` 디렉터리의 검증된 샘플입니다.

| 파일 | 설명 |
|------|------|
| `pair_recv_sample.rs` | PAIR 송수신 |
| `dealer_router_recv_sample.rs` | DEALER/ROUTER 송수신 |
| `request_reply_async_sample.rs` | async 요청/응답 |
| `pubsub_recv_sample.rs` | PUB/SUB 발행·구독 |
| `stream_recv_sample.rs` | STREAM 원시 TCP |
| `stream_packet_callback_sample.rs` | STREAM 패킷 콜백 |
| `monitor_recv_sample.rs` | 모니터 이벤트 수신 |
| `discovery_registry_sample.rs` | Registry + Discovery |
| `registry_query_sample.rs` | Registry 토폴로지 쿼리 |
| `spot_recv_sample.rs` | SpotNode/Spot PUB/SUB |
| `spot_request_async_sample.rs` | SpotNode 비동기 요청 |
| `actor_single_player_queue_sample.rs` | 액터 조인·이동·메시지 큐 |
| `actor_room_server_sample.rs` | 방 서버 패턴 |
| `actor_gateway_relay_sample.rs` | 게이트웨이 릴레이 |

```bash
cd bindings/rust
cargo run --example pair_recv_sample
```
