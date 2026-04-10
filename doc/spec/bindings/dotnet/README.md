[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# .NET Request-Reply Interface

> **공통 정책**: [`bindings/README.md`](../README.md) 의 Request-Reply Policy 섹션

---

## 언어별 차이점

- async 표면은 `Task<Received>`. `await` 로 대기.
- `CancellationToken` 으로 취소 지원. timeout 과 cancellation 모두 적용되며 먼저 발생한 쪽이 우선.
- callback 은 `Action<ZlinkException, Received>`. 성공 시 `(null, received)`.
- `TimeSpan.Zero` (또는 `default`) 이면 socket default timeout.
- PascalCase: `RequestAsync`, `TryRequestAsync`, `Reply`, `TryReply`, `OnRequest`.
- async 메서드는 `*Async` 접미사, callback 메서드는 접미사 없음.

---

## Interface

```csharp
public class RouterSocket {
    Task<Received> RequestAsync(RoutingId routingId, Message message,
                                TimeSpan timeout = default,
                                CancellationToken ct = default);
    void Request(RoutingId routingId, Message message,
                 Action<ZlinkException, Received> callback,
                 TimeSpan timeout = default);

    Task<Received> TryRequestAsync(RoutingId routingId, Message message,
                                   TimeSpan timeout = default,
                                   CancellationToken ct = default);
    void TryRequest(RoutingId routingId, Message message,
                    Action<ZlinkException, Received> callback,
                    TimeSpan timeout = default);

    void Reply(RoutingId routingId, ulong requestSeq, Message message);
    SendResult TryReply(RoutingId routingId, ulong requestSeq, Message message);
}

public class DealerSocket {
    Task<Received> RequestAsync(Message message, TimeSpan timeout = default,
                                CancellationToken ct = default);
    void Request(Message message,
                 Action<ZlinkException, Received> callback,
                 TimeSpan timeout = default);

    Task<Received> TryRequestAsync(Message message, TimeSpan timeout = default,
                                   CancellationToken ct = default);
    void TryRequest(Message message,
                    Action<ZlinkException, Received> callback,
                    TimeSpan timeout = default);
}
```

---

## 사용 예

```csharp
var reply = await router.RequestAsync(routingId, msg, TimeSpan.FromSeconds(5));
var reply2 = await router.RequestAsync(routingId, msg);  // socket default

router.OnRequest((routingId, requestSeq, parts) => {
    router.Reply(routingId, requestSeq, Message.From("ok"));
});

// cancellation
var cts = new CancellationTokenSource(TimeSpan.FromSeconds(3));
var reply3 = await router.RequestAsync(routingId, msg, ct: cts.Token);
```

---

## SPOT Messaging Interface

SPOT pub/sub, routed direct messaging, event dispatcher 의
언어별 인터페이스는 이 섹션에서 정의한다.
공통 정책은 [`README.md`](../README.md) 의 SPOT Messaging Policy 와
SPOT Event Dispatcher Policy 를 참조한다.

### Pub/Sub

```csharp
public class Spot {
    void Publish(string topicId, Message message);
    SendResult TryPublish(string topicId, Message message);
    void SetSubscription(string filter);
    void UnsetSubscription(string filter);
    void OnSubscribe(Action<RoutingId, string, Received> handler);
}
```

### Routed Direct Messaging

```csharp
public class Spot {
    void SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid, Message message);
    void SendToRouter(RoutingId peerRid, Message message);

    // Spot 수신 handler — ordinary + request-reply 통합
    void OnMessage(Action<RoutingId, RoutingId, ulong, Received> handler);
}

public class RouterSocket {
    // Router 의 SPOT 수신 handler — spot -> router 메시지 수신
    void OnSpotMessage(Action<RoutingId, RoutingId, ulong, Received> handler);
}
```

### SPOT Request-Reply

```csharp
public class Spot {
    // spot -> spot
    Task<Received> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, TimeSpan timeout = default, CancellationToken ct = default);
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq, Message message);

    // spot -> router
    Task<Received> RequestToRouterAsync(RoutingId peerRid, Message message,
        TimeSpan timeout = default, CancellationToken ct = default);
    void ReplyToRouter(RoutingId peerRid, ulong requestSeq, Message message);
}

public class RouterSocket {
    // router -> spot
    Task<Received> RequestToSpotAsync(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, TimeSpan timeout = default, CancellationToken ct = default);
    void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq, Message message);
}
```

### Event Monitor

```csharp
public class ServiceMonitor {
    ServiceMonitor(SpotNode node, ServiceMonitorOptions options = default);
    void OnEvent(Action<ServiceEvent> handler);
}
```

### 사용 예

```csharp
// setup
spot.SetSubscription("market.*");
var timer = new SpotTimer(spot);
timer.Start(TimeSpan.FromSeconds(1), 0);

// dispatch event handler — 단일 context, 동기화 불필요
spot.OnDispatchEvent((s, evt) => {
    switch (evt) {
        case SpotDispatchEvent.SubscribeReadable:
            var sub = s.SubscribeRecv();
            break;
        case SpotDispatchEvent.RoutedReadable:
            var msg = s.SpotRecv();
            if (msg.RequestSeq != 0)
                s.ReplyToSpot(msg.SourceRid, msg.SpotRid,
                              msg.RequestSeq, Message.From("ok"));
            break;
        case SpotDispatchEvent.TimerReadable:
            var fireCount = timer.Recv();
            s.Publish("heartbeat", Message.From("ping"));
            break;
    }
});

// send
spot.Publish("market.price", Message.From(data));
spot.SendToSpot(nodeRid, spotRid, Message.From("hello"));

// monitor
var monitor = new ServiceMonitor(node);
monitor.OnEvent(e => { /* peer/subject events */ });
```
