[← 운영](./04-operations.md) · [Python 가이드](./index.md)

# 레퍼런스

---

## 에러 처리

```python
try:
    socket.send().message(b"data").submit()
except zlink.SubmitError as e:
    if e.result == zlink.SubmitResult.BACKPRESSURED:
        pass  # 재시도
    else:
        raise
```

예외 타입: `SubmitError`, `RequestError`, `RecvError`,
`BindError`, `ConnectError`, `ConfigError`, `CloseError`, `HandlerError`.
모두 `ZlinkError`를 상속합니다.

---

## 코덱

```bash
pip install zlink-codec-json
pip install zlink-codec-messagepack
pip install zlink-codec-protobuf
```

---

## C API ↔ Python 대응표

| C API | Python API |
|-------|-----------|
| `zlink_ctx_new()` | `zlink.create_context()` |
| `zlink_ctx_term()` | `ctx.close()` |
| `zlink_socket(ctx, type)` | `zlink.create_pair_socket(ctx)` 등 |
| `zlink_bind(s, ep)` | `socket.bind(ep)` |
| `zlink_connect(s, ep)` | `socket.connect(ep)` |
| `zlink_send_part(...)` | `socket.send().message(b).submit()` |
| `zlink_recv(...)` | `socket.recv_into(received)` |
| `zlink_msg_data(msg)` | `part.to_bytes()` |
| `zlink_routing_id_t` | `zlink.RoutingId(b"id")` |
| `zlink_spot_node_new(ctx)` | `zlink.create_spot_node(ctx)` |
| `zlink_registry_new(ctx)` | `zlink.create_registry(ctx)` |
| `zlink_discovery_new(ctx,...)` | `zlink.create_discovery(ctx, type, ch)` |

---

## API 레퍼런스 생성 (Sphinx)

```bash
cd bindings/python
pip install -e ".[docs]"
sphinx-build -b html docs docs/_build
```

---

## 샘플 목록

`bindings/python/samples/` 디렉터리의 검증된 샘플입니다.

| 파일 | 설명 |
|------|------|
| `pair_recv_sample.py` | PAIR 송수신 |
| `dealer_router_recv_sample.py` | DEALER/ROUTER 송수신 |
| `request_reply_async_sample.py` | asyncio 비동기 요청/응답 |
| `pubsub_recv_sample.py` | XPUB/SUB 발행·구독 |
| `stream_recv_sample.py` | STREAM 원시 TCP |
| `stream_packet_callback_sample.py` | STREAM 패킷 콜백 |
| `monitor_recv_sample.py` | 모니터 이벤트 수신 |
| `discovery_registry_sample.py` | Registry + Discovery |
| `registry_query_sample.py` | Registry 토폴로지 쿼리 |
| `spot_recv_sample.py` | SpotNode/Spot PUB/SUB |
| `spot_request_async_sample.py` | SpotNode 비동기 요청 |
| `actor_single_player_queue_sample.py` | 액터 조인·이동·메시지 큐 |
| `actor_room_server_sample.py` | 방 서버 패턴 |
| `actor_gateway_relay_sample.py` | 게이트웨이 릴레이 |

```bash
cd bindings/python/samples
python pair_recv_sample.py
python run_samples.py   # 전체 실행
```
