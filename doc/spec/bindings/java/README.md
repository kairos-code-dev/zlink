[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Java Request-Reply Interface

> **공통 정책**: [`bindings/README.md`](../README.md) 의 Request-Reply Policy 섹션

---

## 언어별 차이점

- async 표면은 `CompletableFuture<Received>`. Kotlin 에서 `.await()` 사용 가능.
- callback 은 `BiConsumer<Received, ZlinkException>`.
  성공 시 `(received, null)`, 실패 시 `(null, exception)`.
- `timeout = Duration.ZERO` 이면 socket default timeout.
- `requestSeq` 는 `long` (Java 에 unsigned 64 bit 없음).
- camelCase: `request`, `tryRequest`, `reply`, `tryReply`, `onRequest`.

---

## Interface

```java
public class RouterSocket {
    CompletableFuture<Received> request(RoutingId routingId, Message message,
                                        Duration timeout);
    void request(RoutingId routingId, Message message,
                 BiConsumer<Received, ZlinkException> callback, Duration timeout);

    CompletableFuture<Received> tryRequest(RoutingId routingId, Message message,
                                           Duration timeout);
    void tryRequest(RoutingId routingId, Message message,
                    BiConsumer<Received, ZlinkException> callback, Duration timeout);

    void reply(RoutingId routingId, long requestSeq, Message message);
    SendResult tryReply(RoutingId routingId, long requestSeq, Message message);
}

public class DealerSocket {
    CompletableFuture<Received> request(Message message, Duration timeout);
    void request(Message message, BiConsumer<Received, ZlinkException> callback,
                 Duration timeout);

    CompletableFuture<Received> tryRequest(Message message, Duration timeout);
    void tryRequest(Message message, BiConsumer<Received, ZlinkException> callback,
                    Duration timeout);
}
```

---

## 사용 예

```java
// async
var future = router.request(routingId, msg, Duration.ofSeconds(5));
var future2 = router.request(routingId, msg, Duration.ZERO);  // socket default

// request handler
router.onRequest((routingId, requestSeq, parts) -> {
    router.reply(routingId, requestSeq, Message.from("ok"));
});
```

```kotlin
// Kotlin coroutine
val reply = router.request(routingId, msg, 5.seconds.toJavaDuration()).await()
```

---

## SPOT Messaging Interface

SPOT pub/sub, routed direct messaging, event dispatcher 의
언어별 인터페이스는 이 섹션에서 정의한다.
공통 정책은 [`README.md`](../README.md) 의 SPOT Messaging Policy 와
SPOT Event Dispatcher Policy 를 참조한다.

### Pub/Sub

```java
public class Spot {
    void publish(String topicId, Message message);
    SendResult tryPublish(String topicId, Message message);
    void setSubscription(String filter);
    void unsetSubscription(String filter);
    void onSubscribe(TriConsumer<RoutingId, String, Received> handler);
}
```

### Routed Direct Messaging

```java
public class Spot {
    void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message message);
    void sendToRouter(RoutingId peerRid, Message message);

    // Spot 수신 handler — ordinary + request-reply 통합
    void onMessage(SpotMessageHandler handler);
}

public class RouterSocket {
    // Router 의 SPOT 수신 handler — spot -> router 메시지 수신
    void onSpotMessage(RouterSpotMessageHandler handler);
}
```

### SPOT Request-Reply

```java
public class Spot {
    // spot -> spot
    CompletableFuture<Received> requestToSpot(RoutingId destNodeRid,
        RoutingId destSpotRid, Message message, Duration timeout);
    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, BiConsumer<Received, ZlinkException> callback,
        Duration timeout);
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        long requestSeq, Message message);

    // spot -> router
    CompletableFuture<Received> requestToRouter(RoutingId peerRid,
        Message message, Duration timeout);
    void replyToRouter(RoutingId peerRid, long requestSeq, Message message);
}

public class RouterSocket {
    // router -> spot
    CompletableFuture<Received> requestToSpot(RoutingId destNodeRid,
        RoutingId destSpotRid, Message message, Duration timeout);
    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        long requestSeq, Message message);
}
```

### Event Monitor

```java
public class ServiceMonitor {
    ServiceMonitor(SpotNode node, ServiceMonitorOptions options);
    void onEvent(Consumer<ServiceEvent> handler);
}
```

### 사용 예

```java
// setup
spot.setSubscription("market.*");
var timer = new SpotTimer(spot);
timer.start(Duration.ofSeconds(1), 0);

// dispatch event handler — 단일 context, 동기화 불필요
spot.onDispatchEvent((s, event) -> {
    switch (event) {
        case SUBSCRIBE_READABLE -> {
            var sub = s.subscribeRecv();
        }
        case ROUTED_READABLE -> {
            var msg = s.spotRecv();
            if (msg.requestSeq() != 0) {
                s.replyToSpot(msg.sourceRid(), msg.spotRid(),
                              msg.requestSeq(), Message.from("ok"));
            }
        }
        case TIMER_READABLE -> {
            long fireCount = timer.recv();
            s.publish("heartbeat", Message.from("ping"));
        }
    }
});

// send
spot.publish("market.price", Message.from(data));
spot.sendToSpot(nodeRid, spotRid, Message.from("hello"));

// monitor
var monitor = new ServiceMonitor(node);
monitor.onEvent(event -> { /* peer/subject events */ });
```
