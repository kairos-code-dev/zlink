[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# C++ Request-Reply Interface

> **공통 정책**: [`bindings/README.md`](../README.md) 의 Request-Reply Policy 섹션

---

## 언어별 차이점

- async 표면은 `async_result_t<T>` 이다. C++20 coroutine 에서 `co_await` 가능.
- 일반 thread context 에서는 `.get()` / `.wait()` 를 caller 가 선택할 수 있다.
- callback 은 `std::function<void(ZlinkError, Received)>` 하나로 합친다.
  `err.code() == 0` 이면 성공.
- timeout 기본값은 `0ms`. `0ms` 이면 socket default timeout.
- `try_request` / `try_reply` 는 snake_case (C++ 관례).
- callback → async 변환: callback 에서 `async_result_t` 를 complete 한다.

---

## Interface

```cpp
class RouterSocket {
    void set_default_request_timeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds get_default_request_timeout() const;

    // request — writable 대기 후 전송 + reply 대기
    async_result_t<Received> request(const RoutingId& routing_id, Message msg,
                                     std::chrono::milliseconds timeout = 0ms);
    // request callback
    void request(const RoutingId& routing_id, Message msg,
                 std::function<void(ZlinkError, Received)> callback,
                 std::chrono::milliseconds timeout = 0ms);

    // tryRequest — non-blocking send + reply 대기
    async_result_t<Received> try_request(const RoutingId& routing_id, Message msg,
                                         std::chrono::milliseconds timeout = 0ms);
    void try_request(const RoutingId& routing_id, Message msg,
                     std::function<void(ZlinkError, Received)> callback,
                     std::chrono::milliseconds timeout = 0ms);

    // reply — writable 대기
    void reply(const RoutingId& routing_id, uint64_t request_seq, Message msg);
    // tryReply — non-blocking send
    SendResult try_reply(const RoutingId& routing_id, uint64_t request_seq,
                         Message msg);
};

class DealerSocket {
    void set_default_request_timeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds get_default_request_timeout() const;

    async_result_t<Received> request(Message msg,
                                     std::chrono::milliseconds timeout = 0ms);
    void request(Message msg,
                 std::function<void(ZlinkError, Received)> callback,
                 std::chrono::milliseconds timeout = 0ms);

    async_result_t<Received> try_request(Message msg,
                                         std::chrono::milliseconds timeout = 0ms);
    void try_request(Message msg,
                     std::function<void(ZlinkError, Received)> callback,
                     std::chrono::milliseconds timeout = 0ms);
};
```

---

## 사용 예

```cpp
// C++20 coroutine
auto reply = co_await router.request(routing_id, msg, 5000ms);
auto reply2 = co_await router.request(routing_id, msg);  // socket default

// request handler
router.on_request([&](const RoutingId& peer_rid, uint64_t req_seq,
                       Received req) {
    router.reply(peer_rid, req_seq, Message::from("ok"));
});

// tryRequest — backpressure 시 즉시 실패
auto reply3 = co_await router.try_request(routing_id, msg, 3000ms);

// tryReply — non-blocking
SendResult result = router.try_reply(peer_rid, req_seq, Message::from("ok"));
```

---

## SPOT Messaging Interface

SPOT pub/sub, routed direct messaging, event dispatcher 의
언어별 인터페이스는 이 섹션에서 정의한다.
공통 정책은 [`README.md`](../README.md) 의 SPOT Messaging Policy 와
SPOT Event Dispatcher Policy 를 참조한다.

### Pub/Sub

```cpp
class Spot {
    void publish(const std::string& topic_id, Message msg);
    SendResult try_publish(const std::string& topic_id, Message msg);

    void set_subscription(const std::string& filter);
    void unset_subscription(const std::string& filter);

    void on_subscribe(std::function<void(const RoutingId&, std::string_view topic,
                                         Received)> handler);
};
```

### Routed Direct Messaging

```cpp
class Spot {
    void send_to_spot(const RoutingId& dest_node_rid, const RoutingId& dest_spot_rid,
                   Message msg);
    void send_to_router(const RoutingId& peer_rid, Message msg);

    // Spot 수신 handler — ordinary + request-reply 통합
    // request_seq = 0 이면 ordinary, != 0 이면 request
    void on_message(std::function<void(const RoutingId& source_rid,
                                       const RoutingId& spot_rid,
                                       uint64_t request_seq, Received)> handler);
};

class RouterSocket {
    // Router 의 SPOT 수신 handler — spot -> router 메시지 수신
    // request_seq = 0 이면 ordinary, != 0 이면 request
    void on_spot_message(std::function<void(const RoutingId& source_node_rid,
                                            const RoutingId& source_spot_rid,
                                            uint64_t request_seq, Received)> handler);
};
```

### SPOT Request-Reply

```cpp
class Spot {
    // spot -> spot request/reply
    async_result_t<Received> request_to_spot(const RoutingId& dest_node_rid,
        const RoutingId& dest_spot_rid, Message msg,
        std::chrono::milliseconds timeout = 0ms);
    void request_to_spot(const RoutingId& dest_node_rid,
        const RoutingId& dest_spot_rid, Message msg,
        std::function<void(ZlinkError, Received)> callback,
        std::chrono::milliseconds timeout = 0ms);
    void reply_to_spot(const RoutingId& dest_node_rid,
        const RoutingId& dest_spot_rid, uint64_t request_seq, Message msg);

    // spot -> router request
    async_result_t<Received> request_to_router(const RoutingId& peer_rid, Message msg,
        std::chrono::milliseconds timeout = 0ms);
    void request_to_router(const RoutingId& peer_rid, Message msg,
        std::function<void(ZlinkError, Received)> callback,
        std::chrono::milliseconds timeout = 0ms);
    void reply_to_router(const RoutingId& peer_rid, uint64_t request_seq, Message msg);
};

class RouterSocket {
    // router -> spot request/reply
    async_result_t<Received> request_to_spot(const RoutingId& dest_node_rid,
        const RoutingId& dest_spot_rid, Message msg,
        std::chrono::milliseconds timeout = 0ms);
    void reply_to_spot(const RoutingId& dest_node_rid,
        const RoutingId& dest_spot_rid, uint64_t request_seq, Message msg);
};
```

### Event Monitor

```cpp
class ServiceMonitor {
    explicit ServiceMonitor(SpotNode& node, ServiceMonitorOptions options = {});
    void on_event(std::function<void(const ServiceEvent&)> handler);
};
```

### 사용 예

```cpp
// setup
spot.set_subscription("market.*");
auto timer = SpotTimer(spot);
timer.start(1s, 0);  // 1초 간격, 무한 반복

// dispatch event handler — 단일 context, 동기화 불필요
spot.on_dispatch_event([&](Spot& s, SpotDispatchEvent event) {
    switch (event) {
    case SpotDispatchEvent::SubscribeReadable: {
        auto [src, topic, msg] = s.subscribe_recv();
        // pub/sub 메시지 처리
        break;
    }
    case SpotDispatchEvent::RoutedReadable: {
        auto [src, spot_rid, req_seq, msg] = s.spot_recv();
        if (req_seq != 0) {
            s.reply_to_spot(src, spot_rid, req_seq, Message::from("ok"));
        }
        break;
    }
    case SpotDispatchEvent::TimerReadable: {
        auto fire_count = timer.recv();
        s.publish("heartbeat", Message::from("ping"));
        break;
    }
    }
});

// send
spot.publish("market.price", Message::from(data));
spot.send_to_spot(node_rid, spot_rid, Message::from("hello"));

// monitor
ServiceMonitor monitor(node);
monitor.on_event([](const ServiceEvent& e) { /* peer/subject events */ });
```
