[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Python Request-Reply Interface

> **공통 정책**: [`bindings/README.md`](../README.md) 의 Request-Reply Policy 섹션

---

## 언어별 차이점

- async 표면은 `async def` coroutine. `await` 로 대기.
- Python 은 overloading 이 없으므로 callback 버전은 `*_with_callback` 접미사로 분리.
- callback 은 `Callable[[ZlinkError | None, Received | None], None]`.
  `err` 가 `None` 이면 성공.
- `timeout = None` 이면 socket default timeout. 단위는 seconds (float, Python 관례).
- snake_case: `request`, `try_request`, `reply`, `try_reply`,
  `request_with_callback`, `try_request_with_callback`, `on_request`.
- callback → async 변환: callback 에서 `asyncio.Future.set_result()`.

---

## Interface

```python
class RouterSocket:
    # coroutine
    async def request(self, routing_id: RoutingId, message: Message,
                      *, timeout: float | None = None) -> Received: ...
    # callback
    def request_with_callback(
        self, routing_id: RoutingId, message: Message,
        callback: Callable[[ZlinkError | None, Received | None], None],
        *, timeout: float | None = None) -> None: ...

    async def try_request(self, routing_id: RoutingId, message: Message,
                          *, timeout: float | None = None) -> Received: ...
    def try_request_with_callback(
        self, routing_id: RoutingId, message: Message,
        callback: Callable[[ZlinkError | None, Received | None], None],
        *, timeout: float | None = None) -> None: ...

    def reply(self, routing_id: RoutingId, request_seq: int,
              message: Message) -> None: ...
    def try_reply(self, routing_id: RoutingId, request_seq: int,
                  message: Message) -> SendResult: ...

class DealerSocket:
    async def request(self, message: Message,
                      *, timeout: float | None = None) -> Received: ...
    def request_with_callback(
        self, message: Message,
        callback: Callable[[ZlinkError | None, Received | None], None],
        *, timeout: float | None = None) -> None: ...

    async def try_request(self, message: Message,
                          *, timeout: float | None = None) -> Received: ...
    def try_request_with_callback(
        self, message: Message,
        callback: Callable[[ZlinkError | None, Received | None], None],
        *, timeout: float | None = None) -> None: ...
```

---

## 사용 예

```python
reply = await router.request(routing_id, msg, timeout=5.0)
reply2 = await router.request(routing_id, msg)  # socket default

def handle_request(routing_id, request_seq, parts):
    router.reply(routing_id, request_seq, b"ok")

router.on_request(handle_request)

# callback
def on_done(err, received):
    if err:
        print(f"error: {err}")
    else:
        print(f"reply: {received}")

router.request_with_callback(routing_id, msg, on_done, timeout=5.0)
```

---

## SPOT Messaging Interface

SPOT pub/sub, routed direct messaging, event dispatcher 의
언어별 인터페이스는 이 섹션에서 정의한다.
공통 정책은 [`README.md`](../README.md) 의 SPOT Messaging Policy 와
SPOT Event Dispatcher Policy 를 참조한다.

### Pub/Sub

```python
class Spot:
    def publish(self, topic_id: str, message: Message) -> None: ...
    def try_publish(self, topic_id: str, message: Message) -> SendResult: ...
    def set_subscription(self, filter: str) -> None: ...
    def unset_subscription(self, filter: str) -> None: ...
    def on_subscribe(self, handler: Callable[[RoutingId, str, Received], None]
                     ) -> None: ...
```

### Routed Direct Messaging

```python
class Spot:
    def send_to_spot(self, dest_node_rid: RoutingId, dest_spot_rid: RoutingId,
                  message: Message) -> None: ...
    def send_to_router(self, peer_rid: RoutingId, message: Message) -> None: ...

    # Spot 수신 handler — ordinary + request-reply 통합
    def on_message(self, handler: Callable[
        [RoutingId, RoutingId, int, Received], None]) -> None: ...

class RouterSocket:
    # Router 의 SPOT 수신 handler — spot -> router 메시지 수신
    def on_spot_message(self, handler: Callable[
        [RoutingId, RoutingId, int, Received], None]) -> None: ...
```

### SPOT Request-Reply

```python
class Spot:
    # spot -> spot
    async def request_to_spot(self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, message: Message,
        *, timeout: float | None = None) -> Received: ...
    def reply_to_spot(self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, request_seq: int,
        message: Message) -> None: ...

    # spot -> router
    async def request_to_router(self, peer_rid: RoutingId, message: Message,
        *, timeout: float | None = None) -> Received: ...
    def reply_to_router(self, peer_rid: RoutingId, request_seq: int,
        message: Message) -> None: ...

class RouterSocket:
    # router -> spot
    async def request_to_spot(self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, message: Message,
        *, timeout: float | None = None) -> Received: ...
    def reply_to_spot(self, dest_node_rid: RoutingId,
        dest_spot_rid: RoutingId, request_seq: int,
        message: Message) -> None: ...
```

### Event Monitor

```python
class ServiceMonitor:
    def __init__(self, node: SpotNode,
                 options: ServiceMonitorOptions | None = None) -> None: ...
    def on_event(self, handler: Callable[[ServiceEvent], None]) -> None: ...
```

### 사용 예

```python
# setup
spot.set_subscription("market.*")
timer = SpotTimer(spot)
timer.start(interval_ns=1_000_000_000, repeat_count=0)  # 1초, 무한 반복

# dispatch event handler — 단일 context, 동기화 불필요
def on_dispatch(s, event):
    if event == SpotDispatchEvent.SUBSCRIBE_READABLE:
        sub = s.subscribe_recv()
        # pub/sub 메시지 처리
    elif event == SpotDispatchEvent.ROUTED_READABLE:
        msg = s.spot_recv()
        if msg.request_seq != 0:
            s.reply_to_spot(msg.source_rid, msg.spot_rid,
                            msg.request_seq, b"ok")
    elif event == SpotDispatchEvent.TIMER_READABLE:
        fire_count = timer.recv()
        s.publish("heartbeat", b"ping")

spot.on_dispatch_event(on_dispatch)

# send
spot.publish("market.price", msg)
spot.send_to_spot(node_rid, spot_rid, b"hello")

# monitor
monitor = ServiceMonitor(node)
monitor.on_event(lambda e: ...)  # peer/subject events
```
