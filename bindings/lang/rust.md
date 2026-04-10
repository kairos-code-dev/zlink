# Rust Request-Reply Interface

> **공통 정책**: [`bindings/README.md`](../README.md) 의 Request-Reply Policy 섹션

---

## 언어별 차이점

- async 표면은 `async fn` → `Result<Received, ZlinkError>`. `.await` 로 대기.
- Rust 는 overloading 이 없으므로 callback 버전은 `*_callback` 접미사로 분리.
- callback 은 `impl FnOnce(Result<Received, ZlinkError>) + Send + 'static`.
- `timeout = Duration::ZERO` 이면 socket default timeout.
- `try_reply` 는 `SendResult` 를 반환한다 (`Result` 가 아님).
- snake_case: `request`, `try_request`, `request_callback`, `try_request_callback`,
  `reply`, `try_reply`, `on_request`.
- callback → async 변환: callback 에서 waker notify.

---

## Interface

```rust
impl RouterSocket {
    // async
    async fn request(&self, routing_id: &RoutingId, msg: Message,
        timeout: Duration) -> Result<Received, ZlinkError>;
    // callback
    fn request_callback(&self, routing_id: &RoutingId, msg: Message,
        callback: impl FnOnce(Result<Received, ZlinkError>) + Send + 'static,
        timeout: Duration);

    async fn try_request(&self, routing_id: &RoutingId, msg: Message,
        timeout: Duration) -> Result<Received, ZlinkError>;
    fn try_request_callback(&self, routing_id: &RoutingId, msg: Message,
        callback: impl FnOnce(Result<Received, ZlinkError>) + Send + 'static,
        timeout: Duration);

    fn reply(&self, routing_id: &RoutingId, request_seq: u64,
             msg: Message) -> Result<(), ZlinkError>;
    fn try_reply(&self, routing_id: &RoutingId, request_seq: u64,
                 msg: Message) -> SendResult;
}

impl DealerSocket {
    async fn request(&self, msg: Message, timeout: Duration)
        -> Result<Received, ZlinkError>;
    fn request_callback(&self, msg: Message,
        callback: impl FnOnce(Result<Received, ZlinkError>) + Send + 'static,
        timeout: Duration);

    async fn try_request(&self, msg: Message, timeout: Duration)
        -> Result<Received, ZlinkError>;
    fn try_request_callback(&self, msg: Message,
        callback: impl FnOnce(Result<Received, ZlinkError>) + Send + 'static,
        timeout: Duration);
}
```

---

## 사용 예

```rust
let reply = router.request(&routing_id, msg, Duration::from_secs(5)).await?;
let reply2 = router.request(&routing_id, msg, Duration::ZERO).await?;  // socket default

router.on_request(|routing_id, request_seq, parts| {
    router.reply(routing_id, request_seq, Message::from(b"ok")).unwrap();
});

// callback
router.request_callback(&routing_id, msg, |result| {
    match result {
        Ok(received) => println!("reply: {:?}", received),
        Err(e) => println!("error: {:?}", e),
    }
}, Duration::from_secs(5));
```

---

## SPOT Messaging Interface

SPOT pub/sub, routed direct messaging, event dispatcher 의
언어별 인터페이스는 이 섹션에서 정의한다.
공통 정책은 [`README.md`](../README.md) 의 SPOT Messaging Policy 와
SPOT Event Dispatcher Policy 를 참조한다.

### Pub/Sub

```rust
impl Spot {
    fn publish(&self, topic_id: &str, msg: Message) -> Result<(), ZlinkError>;
    fn try_publish(&self, topic_id: &str, msg: Message) -> SendResult;
    fn set_subscription(&self, filter: &str) -> Result<(), ZlinkError>;
    fn unset_subscription(&self, filter: &str) -> Result<(), ZlinkError>;
    fn on_subscribe(&self, handler: impl Fn(&RoutingId, &str, Received)
        + Send + 'static);
}
```

### Routed Direct Messaging

```rust
impl Spot {
    fn send_to_spot(&self, dest_node_rid: &RoutingId, dest_spot_rid: &RoutingId,
                 msg: Message) -> Result<(), ZlinkError>;
    fn send_to_router(&self, peer_rid: &RoutingId,
                   msg: Message) -> Result<(), ZlinkError>;

    // Spot 수신 handler — ordinary + request-reply 통합
    fn on_message(&self, handler: impl Fn(&RoutingId, &RoutingId, u64, Received)
        + Send + 'static);
}

impl RouterSocket {
    // Router 의 SPOT 수신 handler — spot -> router 메시지 수신
    fn on_spot_message(&self, handler: impl Fn(&RoutingId, &RoutingId, u64, Received)
        + Send + 'static);
}
```

### SPOT Request-Reply

```rust
impl Spot {
    // spot -> spot
    async fn request_to_spot(&self, dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId, msg: Message, timeout: Duration)
        -> Result<Received, ZlinkError>;
    fn reply_to_spot(&self, dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId, request_seq: u64,
        msg: Message) -> Result<(), ZlinkError>;

    // spot -> router
    async fn request_to_router(&self, peer_rid: &RoutingId, msg: Message,
        timeout: Duration) -> Result<Received, ZlinkError>;
    fn reply_to_router(&self, peer_rid: &RoutingId, request_seq: u64,
        msg: Message) -> Result<(), ZlinkError>;
}

impl RouterSocket {
    // router -> spot
    async fn request_to_spot(&self, dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId, msg: Message, timeout: Duration)
        -> Result<Received, ZlinkError>;
    fn reply_to_spot(&self, dest_node_rid: &RoutingId,
        dest_spot_rid: &RoutingId, request_seq: u64,
        msg: Message) -> Result<(), ZlinkError>;
}
```

### Event Monitor

```rust
impl ServiceMonitor {
    fn new(node: &SpotNode, options: ServiceMonitorOptions) -> Self;
    fn on_event(&self, handler: impl Fn(&ServiceEvent) + Send + 'static);
}
```

### 사용 예

```rust
// pub/sub
spot.publish("market.price", msg)?;
spot.set_subscription("market.*")?;
spot.on_subscribe(|src, topic, received| {
    // 같은 I/O thread context �� lock 불필요
});

// routed direct
spot.send_to_spot(&node_rid, &spot_rid, Message::from(b"hello"))?;

// event dispatcher — 모든 callback 이 같은 thread context
spot.on_message(|src_rid, spot_rid, request_seq, received| {
    if request_seq != 0 {
        spot.reply_to_spot(src_rid, spot_rid, request_seq,
                           Message::from(b"ok")).unwrap();
    }
});

// timer — 같은 context 에서 실행
let timers = Timers::new();
timers.add(Duration::from_secs(1), |timer_id| {
    spot.publish("heartbeat", Message::from(b"ping")).unwrap();
});

// monitor
let monitor = ServiceMonitor::new(&node, Default::default());
monitor.on_event(|e| { /* peer/subject events */ });
```
