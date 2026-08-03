[바인딩 가이드](../README.ko.md) · [코어 가이드](https://kairos-code-dev.github.io/zlink/guide/01-overview/)

# Python 바인딩 가이드 (`zlink`)

Python에서 zlink를 사용하는 방법을 실제 샘플 코드 중심으로 설명합니다.
메시징 개념은 [코어 가이드](https://kairos-code-dev.github.io/zlink/guide/01-overview/)를 참고하세요.

---

## 설치

```bash
pip install zlink
```

- **Python 3.9** 이상.
- 네이티브 코어가 플랫폼별 wheel에 번들됩니다.

```python
import zlink
```

---

## 5분 예제 — PING/ACK

모든 리소스는 `with` 문으로 관리합니다.

```python
import zlink

# 서버
with zlink.create_context() as ctx:
    with zlink.create_pair_socket(ctx) as server:
        server.bind("tcp://127.0.0.1:5555")
        received = zlink.create_received()
        server.recv_into(received)
        with received:
            payload = received.to_bytes_list()[0]
            print(payload)  # b"PING"
        server.send().message(b"ACK").submit()

# 클라이언트
with zlink.create_context() as ctx:
    with zlink.create_pair_socket(ctx) as client:
        client.connect("tcp://127.0.0.1:5555")
        client.send().message(b"PING").submit()

        received = zlink.create_received()
        client.recv_into(received)
        with received:
            print(received.to_bytes_list()[0])  # b"ACK"
```

---

## 핵심 타입

### 컨텍스트

```python
with zlink.create_context() as ctx:
    # ctx.close()는 with 블록 종료 시 자동 호출
    pass
```

### 메시지

Python 바인딩은 `bytes` 객체를 메시지로 직접 사용합니다. `send().message()` 호출 시
자동으로 복사본을 만듭니다.

```python
# 바이트 리터럴로 전송
socket.send().message(b"hello").submit()

# 문자열을 UTF-8로 인코딩
socket.send().message("hello".encode()).submit()

# 수신 후 페이로드 접근
received = zlink.create_received()
socket.recv_into(received)
with received:
    parts = received.to_bytes_list()     # [b"part1", b"part2", ...]
    text = parts[0].decode("utf-8")
```

### Received — 수신 봉투

```python
received = zlink.create_received()
ok = socket.recv_into(received)   # True = 수신 성공
with received:
    parts = received.to_bytes_list()
    rid = received.routing_id     # RoutingId 또는 None
    seq = received.request_seq    # int 또는 None (요청 수신 시)
```

### 라우팅 ID

```python
rid = zlink.RoutingId(b"server-01")
socket.set_routing_id(b"client-01")   # 바이트 직접 전달도 가능
```

---

## 소유권과 수명

| 상황 | 규칙 |
|------|------|
| `submit()` 성공 | 전달된 bytes는 내부적으로 복사되므로 원본 재사용 가능 |
| `recv_into()` 성공 | `with received:` 블록으로 수명 관리. `close()` 시 파트 해제 |
| 요청 회신 파트 | `submit(callback)` 콜백으로 받은 각 파트를 `part.close()` 필요 |

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

## C API 대응표

| C API | Python API |
|-------|-----------|
| `zlink_ctx_new()` | `zlink.create_context()` |
| `zlink_ctx_term()` | `ctx.close()` |
| `zlink_socket(ctx, type)` | `zlink.create_pair_socket(ctx)` 등 |
| `zlink_bind(s, ep)` | `socket.bind(ep)` |
| `zlink_connect(s, ep)` | `socket.connect(ep)` |
| `zlink_send_part(...)` | `socket.send().message(b).submit()` |
| `zlink_recv_part(...)` | `socket.recv_into(received)` |
| `zlink_msg_data(msg)` | `part.to_bytes()` |
| `zlink_routing_id_t` | `zlink.RoutingId(b"id")` |
| `zlink_spot_node_new(ctx, opts)` | `zlink.create_spot_node(ctx)` |

---

## 네이티브 라이브러리 / 배포

```python
major, minor, patch = zlink.version()   # (major, minor, patch) 튜플
print(f"zlink {major}.{minor}.{patch}")
```

네이티브 코어는 플랫폼별 wheel에 들어 있어 따로 설치하지 않아도 됩니다.

**스레딩:** `Context`는 스레드 간 공유 가능하지만 소켓은 **하나의 스레드에서만**
사용합니다. 블로킹 수신은 `asyncio.to_thread(socket.recv_into, received)`로
오프로드합니다.

---

## 샘플

`bindings/python/samples/` 디렉터리의 검증된 샘플입니다.

| 파일 | 설명 |
|------|------|
| `pair_recv_sample.py` | PAIR 송수신 |
| `dealer_router_recv_sample.py` | DEALER/ROUTER 송수신 |
| `request_reply_callback_sample.py` | 콜백 요청/응답 |
| `pubsub_recv_sample.py` | XPUB/SUB 발행·구독 |
| `stream_recv_sample.py` | STREAM 원시 TCP |
| `stream_packet_callback_sample.py` | STREAM 패킷 콜백 |
| `monitor_recv_sample.py` | 모니터 이벤트 수신 |
| `spot_recv_sample.py` | SpotNode/Spot PUB/SUB |
| `spot_request_callback_sample.py` | SpotNode 콜백 요청 |
| `actor_single_player_queue_sample.py` | 액터 조인·이동·메시지 큐 |
| `actor_room_server_sample.py` | 방 서버 패턴 |
| `actor_gateway_relay_sample.py` | 게이트웨이 릴레이 |

```bash
cd bindings/python/samples
python pair_recv_sample.py
python run_samples.py   # 전체 실행
```

---

## 더 보기

**소켓 패턴**
- [소켓 패턴 개요](https://kairos-code-dev.github.io/zlink/guide/03-0-socket-patterns/)
  — [PAIR](https://kairos-code-dev.github.io/zlink/guide/03-1-pair/) · [PUB/SUB](https://kairos-code-dev.github.io/zlink/guide/03-2-pubsub/) · [DEALER](https://kairos-code-dev.github.io/zlink/guide/03-3-dealer/) · [ROUTER](https://kairos-code-dev.github.io/zlink/guide/03-4-router/) · [STREAM](https://kairos-code-dev.github.io/zlink/guide/03-5-stream/) · [프록시](https://kairos-code-dev.github.io/zlink/guide/03-6-proxy/)

**서비스**
- [Framework 서비스 개요](../../../../framework/doc/framework/common/guide/server/03-concepts.ko.md)

**운영**
- [소켓 옵션](https://kairos-code-dev.github.io/zlink/guide/12-socket-options/)
- [TLS 보안](https://kairos-code-dev.github.io/zlink/guide/05-tls-security/)
- [모니터링](https://kairos-code-dev.github.io/zlink/guide/06-monitoring/)
- [스레드 안전성](https://kairos-code-dev.github.io/zlink/guide/11-thread-safety/)
- [메시지 API](https://kairos-code-dev.github.io/zlink/guide/09-message-api/)
- [라우팅 ID](https://kairos-code-dev.github.io/zlink/guide/08-routing-id/)
