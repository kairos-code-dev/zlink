[Python 가이드](./index.md) · [다음: 메시징 →](./02-messaging.md)

# 시작하기

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

## 소유권 규칙

| 상황 | 규칙 |
|------|------|
| `submit()` 성공 | 전달된 bytes는 내부적으로 복사되므로 원본 재사용 가능 |
| `recv_into()` 성공 | `with received:` 블록으로 수명 관리. `close()` 시 파트 해제 |
| 요청 회신 파트 | `submit_async()` 완료 후 각 파트를 `part.close()` 필요 |

---

## 비동기 지원

Python 바인딩은 `asyncio`와 함께 사용할 수 있습니다. 블로킹 작업은
`asyncio.to_thread()`로 오프로드합니다.

```python
import asyncio, zlink

async def main():
    with zlink.create_context() as ctx:
        with zlink.create_dealer_socket(ctx) as dealer:
            dealer.connect("tcp://127.0.0.1:5561")
            reply = await dealer.request().message(b"ping").timeout(2.0).submit_async()
            try:
                print([p.to_bytes() for p in reply])
            finally:
                for p in reply: p.close()

asyncio.run(main())
```
