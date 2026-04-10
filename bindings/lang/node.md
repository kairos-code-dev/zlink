# Node / TypeScript Request-Reply Interface

> **공통 정책**: [`bindings/README.md`](../README.md) 의 Request-Reply Policy 섹션

---

## 언어별 차이점

- async 표면은 `Promise<Received>`. `await` 로 대기.
- callback 은 Node 관례 `(err, reply)` 패턴. `err` 가 `null` 이면 성공.
- callback 유무로 Promise/callback 오버로드 구분.
- timeout 은 `options.timeout` (milliseconds, Node 관례). 생략 시 socket default.
- `requestSeq` 는 `bigint` (uint64).
- camelCase: `request`, `tryRequest`, `reply`, `tryReply`, `onRequest`.

---

## Interface

```typescript
class RouterSocket {
    // Promise
    request(routingId: RoutingId, message: Message,
            options?: { timeout?: number }): Promise<Received>;
    // callback
    request(routingId: RoutingId, message: Message,
            callback: (err: ZlinkError | null, reply?: Received) => void,
            options?: { timeout?: number }): void;

    tryRequest(routingId: RoutingId, message: Message,
               options?: { timeout?: number }): Promise<Received>;
    tryRequest(routingId: RoutingId, message: Message,
               callback: (err: ZlinkError | null, reply?: Received) => void,
               options?: { timeout?: number }): void;

    reply(routingId: RoutingId, requestSeq: bigint, message: Message): void;
    tryReply(routingId: RoutingId, requestSeq: bigint,
             message: Message): SendResult;
}

class DealerSocket {
    request(message: Message,
            options?: { timeout?: number }): Promise<Received>;
    request(message: Message,
            callback: (err: ZlinkError | null, reply?: Received) => void,
            options?: { timeout?: number }): void;

    tryRequest(message: Message,
               options?: { timeout?: number }): Promise<Received>;
    tryRequest(message: Message,
               callback: (err: ZlinkError | null, reply?: Received) => void,
               options?: { timeout?: number }): void;
}
```

---

## 사용 예

```typescript
const reply = await router.request(routingId, msg, { timeout: 5000 });
const reply2 = await router.request(routingId, msg);  // socket default

router.onRequest((routingId, requestSeq, parts) => {
    router.reply(routingId, requestSeq, Buffer.from("ok"));
});

// callback
router.request(routingId, msg, (err, reply) => {
    if (err) { console.error(err); }
    else { console.log(reply); }
}, { timeout: 5000 });
```

---

## SPOT Messaging Interface

SPOT pub/sub, routed direct messaging, event dispatcher 의
언어별 인터페이스는 이 섹션에서 정의한다.
공통 정책은 [`README.md`](../README.md) 의 SPOT Messaging Policy 와
SPOT Event Dispatcher Policy 를 참조한다.

### Pub/Sub

```typescript
class Spot {
    publish(topicId: string, message: Message): void;
    tryPublish(topicId: string, message: Message): SendResult;
    setSubscription(filter: string): void;
    unsetSubscription(filter: string): void;
    onSubscribe(handler: (sourceRid: RoutingId, topic: string,
                          received: Received) => void): void;
}
```

### Routed Direct Messaging

```typescript
class Spot {
    sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
             message: Message): void;
    sendToRouter(peerRid: RoutingId, message: Message): void;

    // Spot 수신 handler — ordinary + request-reply 통합
    onMessage(handler: (sourceRid: RoutingId, spotRid: RoutingId,
                        requestSeq: bigint, received: Received) => void): void;
}

class RouterSocket {
    // Router 의 SPOT 수신 handler — spot -> router 메시지 수신
    onSpotMessage(handler: (sourceNodeRid: RoutingId, sourceSpotRid: RoutingId,
                            requestSeq: bigint, received: Received) => void): void;
}
```

### SPOT Request-Reply

```typescript
class Spot {
    // spot -> spot
    requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
        message: Message, options?: { timeout?: number }): Promise<Received>;
    requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
        message: Message,
        callback: (err: ZlinkError | null, reply?: Received) => void,
        options?: { timeout?: number }): void;
    replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
        requestSeq: bigint, message: Message): void;

    // spot -> router
    requestToRouter(peerRid: RoutingId, message: Message,
        options?: { timeout?: number }): Promise<Received>;
    replyToRouter(peerRid: RoutingId, requestSeq: bigint, message: Message): void;
}

class RouterSocket {
    // router -> spot
    requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
        message: Message, options?: { timeout?: number }): Promise<Received>;
    replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
        requestSeq: bigint, message: Message): void;
}
```

### Event Monitor

```typescript
class ServiceMonitor {
    constructor(node: SpotNode, options?: ServiceMonitorOptions);
    onEvent(handler: (event: ServiceEvent) => void): void;
}
```

### 사용 예

```typescript
// pub/sub
spot.publish("market.price", msg);
spot.setSubscription("market.*");
spot.onSubscribe((sourceRid, topic, received) => {
    // 같은 I/O thread context — lock 불필요
});

// routed direct
spot.sendToSpot(nodeRid, spotRid, Buffer.from("hello"));

// event dispatcher — 모든 callback 이 같은 thread context
spot.onMessage((srcRid, spotRid, requestSeq, received) => {
    if (requestSeq !== 0n) {
        spot.replyToSpot(srcRid, spotRid, requestSeq, Buffer.from("ok"));
    }
});

// timer — 같은 context 에서 실행
const timers = new Timers();
timers.add(1000, (timerId) => {
    spot.publish("heartbeat", Buffer.from("ping"));
});

// monitor
const monitor = new ServiceMonitor(node);
monitor.onEvent((event) => { /* peer/subject events */ });
```
