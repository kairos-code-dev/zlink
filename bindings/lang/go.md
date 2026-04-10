# Go Request-Reply Interface

> **공통 정책**: [`bindings/README.md`](../README.md) 의 Request-Reply Policy 섹션

---

## 언어별 차이점

- blocking 표면은 `context.Context` 로 timeout/cancellation 처리.
  caller goroutine 이 결과를 기다리지만 OS thread blocking 이 아니다.
- callback 표면은 `*Async` 접미사. `timeout = 0` 이면 socket default.
- callback 은 `func(Received, error)`. `error` 가 `nil` 이면 성공.
- PascalCase: `Request`, `TryRequest`, `RequestAsync`, `TryRequestAsync`,
  `Reply`, `TryReply`, `OnRequest`.
- callback → async 변환: callback 에서 channel send.

---

## Interface

```go
// context — caller goroutine 대기
func (s *RouterSocket) Request(ctx context.Context,
    routingId RoutingId, msg Message) (Received, error)
// callback
func (s *RouterSocket) RequestAsync(routingId RoutingId, msg Message,
    callback func(Received, error), timeout time.Duration)

func (s *RouterSocket) TryRequest(ctx context.Context,
    routingId RoutingId, msg Message) (Received, error)
func (s *RouterSocket) TryRequestAsync(routingId RoutingId, msg Message,
    callback func(Received, error), timeout time.Duration)

func (s *RouterSocket) Reply(routingId RoutingId,
    requestSeq uint64, msg Message) error
func (s *RouterSocket) TryReply(routingId RoutingId,
    requestSeq uint64, msg Message) (SendResult, error)

func (s *DealerSocket) Request(ctx context.Context,
    msg Message) (Received, error)
func (s *DealerSocket) RequestAsync(msg Message,
    callback func(Received, error), timeout time.Duration)

func (s *DealerSocket) TryRequest(ctx context.Context,
    msg Message) (Received, error)
func (s *DealerSocket) TryRequestAsync(msg Message,
    callback func(Received, error), timeout time.Duration)
```

---

## 사용 예

```go
ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
defer cancel()
reply, err := router.Request(ctx, routingId, msg)

router.OnRequest(func(routingId RoutingId, requestSeq uint64, parts []Message) {
    router.Reply(routingId, requestSeq, NewMessage([]byte("ok")))
})

// callback
router.RequestAsync(routingId, msg, func(received Received, err error) {
    if err != nil {
        log.Println("error:", err)
    } else {
        log.Println("reply:", received)
    }
}, 5*time.Second)
```

---

## SPOT Messaging Interface

SPOT pub/sub, routed direct messaging, event dispatcher 의
언어별 인터페이스는 이 섹션에서 정의한다.
공통 정책은 [`README.md`](../README.md) 의 SPOT Messaging Policy 와
SPOT Event Dispatcher Policy 를 참조한다.

### Pub/Sub

```go
func (s *Spot) Publish(topicId string, msg Message) error
func (s *Spot) TryPublish(topicId string, msg Message) (SendResult, error)
func (s *Spot) SetSubscription(filter string) error
func (s *Spot) UnsetSubscription(filter string) error
func (s *Spot) OnSubscribe(handler func(sourceRid RoutingId,
    topic string, received Received))
```

### Routed Direct Messaging

```go
func (s *Spot) SendToSpot(destNodeRid, destSpotRid RoutingId, msg Message) error
func (s *Spot) SendToRouter(peerRid RoutingId, msg Message) error

// Spot 수신 handler — ordinary + request-reply 통합
func (s *Spot) OnMessage(handler func(sourceRid, spotRid RoutingId,
    requestSeq uint64, received Received))

// Router 의 SPOT 수신 handler — spot -> router 메시지 수신
func (s *RouterSocket) OnSpotMessage(handler func(sourceNodeRid, sourceSpotRid RoutingId,
    requestSeq uint64, received Received))
```

### SPOT Request-Reply

```go
// spot -> spot
func (s *Spot) RequestToSpot(ctx context.Context, destNodeRid, destSpotRid RoutingId,
    msg Message) (Received, error)
func (s *Spot) ReplyToSpot(destNodeRid, destSpotRid RoutingId,
    requestSeq uint64, msg Message) error

// spot -> router
func (s *Spot) RequestToRouter(ctx context.Context, peerRid RoutingId,
    msg Message) (Received, error)
func (s *Spot) ReplyToRouter(peerRid RoutingId, requestSeq uint64,
    msg Message) error

// router -> spot
func (s *RouterSocket) RequestToSpot(ctx context.Context,
    destNodeRid, destSpotRid RoutingId, msg Message) (Received, error)
func (s *RouterSocket) ReplyToSpot(destNodeRid, destSpotRid RoutingId,
    requestSeq uint64, msg Message) error
```

### Event Monitor

```go
func NewServiceMonitor(node *SpotNode, options ServiceMonitorOptions) *ServiceMonitor
func (m *ServiceMonitor) OnEvent(handler func(event ServiceEvent))
```

### 사용 예

```go
// pub/sub
spot.Publish("market.price", msg)
spot.SetSubscription("market.*")
spot.OnSubscribe(func(src RoutingId, topic string, received Received) {
    // 같은 I/O thread context — lock 불필요
})

// routed direct
spot.SendToSpot(nodeRid, spotRid, NewMessage([]byte("hello")))

// event dispatcher — 모든 callback 이 같은 thread context
spot.OnMessage(func(srcRid, spotRid RoutingId, requestSeq uint64, received Received) {
    if requestSeq != 0 {
        spot.ReplyToSpot(srcRid, spotRid, requestSeq, NewMessage([]byte("ok")))
    }
})

// timer — 같은 context 에서 실행
timers := NewTimers()
timers.Add(1*time.Second, func(timerID int) {
    spot.Publish("heartbeat", NewMessage([]byte("ping")))
})

// monitor
monitor := NewServiceMonitor(node, ServiceMonitorOptions{})
monitor.OnEvent(func(e ServiceEvent) { /* peer/subject events */ })
```
