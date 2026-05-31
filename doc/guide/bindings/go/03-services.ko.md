[← 메시징](./02-messaging.ko.md) · [Go 가이드](./index.ko.md) · [다음: 운영 →](./04-operations.ko.md)

# 서비스

Registry, Discovery, SpotNode/Spot, Actor의 Go API 사용법을 설명합니다.

---

## Registry

서비스 토폴로지를 유지하는 레지스트리 서버입니다.
([코어 참고](../../07-4-registry.ko.md))

```go
ctx, _ := zlink.NewContext()
defer ctx.Close()

registry, err := ctx.Registry()
if err != nil { ... }
defer registry.Close()

// PUB(상태 브로드캐스트)와 ROUTER(쿼리) 두 엔드포인트에 바인드
registry.Bind("tcp://127.0.0.1:7400", "tcp://127.0.0.1:7401")
```

레지스트리 상태 조회:

```go
entries, _ := registry.Topology(nil) // nil = 필터 없음
for _, e := range entries {
    fmt.Printf("role=%v channel=%s state=%v\n",
        e.ServiceRole, e.ChannelName, e.State)
}
```

---

## Discovery

레지스트리에서 피어를 배우고 자동으로 연결하는 서비스 발견 레이어입니다.
([코어 참고](../../07-1-discovery.ko.md))

```go
// 채널 이름과 자동 연결 방식을 지정
discovery, err := ctx.Discovery(zlink.AutoConnectFanout, "prices")
if err != nil { ... }
defer discovery.Close()

// 레지스트리에 연결
discovery.ConnectRegistry("tcp://127.0.0.1:7401")

// 소켓에 어태치 — 이후 해당 채널의 피어에 자동 연결
pub, _ := ctx.PubSocket()
pub.AttachDiscovery(discovery)
pub.Bind("tcp://127.0.0.1:5600")
```

자동 연결 방식:

| 상수 | 설명 |
|------|------|
| `AutoConnectFanout` | PUB→SUB 팬아웃 |
| `AutoConnectRouteMesh` | ROUTER 풀 메시 |
| `AutoConnectClientServer` | 클라이언트가 서버에 연결 |
| `AutoConnectDealerMesh` | DEALER 풀 메시 |
| `AutoConnectSpotMesh` | SpotNode 메시 |

---

## SpotNode / Spot

메시 노드(SpotNode)와 그 위에서 동작하는 메시징 엔드포인트(Spot)입니다.
([코어 참고](../../07-3-spot.ko.md))

### SpotNode 설정

```go
node, err := ctx.SpotNode()
if err != nil { ... }
defer node.Close()

// 노드 라우팅 ID (다른 노드가 이 노드를 식별하는 값)
node.SetRoutingID(zlink.NewRoutingIDFromString("node-1"))

// PUB/SUB 메시지를 위한 엔드포인트 바인드
node.SetPubBind("tcp://127.0.0.1:5700")

// 다른 노드에 연결
node.ConnectPeer("tcp://10.0.0.2:5700")
```

### Spot 생성과 발행/구독

```go
spot, err := node.Spot()
if err != nil { ... }
defer spot.Close()

spot.SetRoutingID(zlink.NewRoutingIDFromString("spot-pub"))
spot.SetSubscription("market:BTC")   // 구독 토픽 등록

// 피어가 연결될 때까지 대기 (실제 코드에서는 적절한 방식으로 대기)
// ...

// 발행
msg, _ := zlink.NewMessageFrom([]byte("67000.00"))
spot.Publish("market:BTC").Message(msg).Submit(nil)

// 구독 수신
var topic zlink.TopicMessage
spot.Subscribe(&topic, zlink.RecvFlagsNone)
defer topic.Close()

part, _ := topic.SinglePartOrError()
fmt.Printf("%s: %s\n", topic.Topic(), part.Data())
```

### Spot 요청/응답

```go
// 채널을 통한 요청
completion, err := spot.RequestToChannel("orders").
    Message(orderMsg).
    Timeout(5 * time.Second).
    SubmitAsync(nil)
if err != nil { ... }

c := <-completion
defer func() {
    for _, p := range c.Parts { p.Close() }
}()
```

---

## Actor

상태를 가진 엔티티(플레이어, 세션, 에이전트)입니다.
Spot 위에서 생성되고 Spot 간에 이동할 수 있습니다.
([코어 참고](../../07-4-actor.ko.md))

### 액터 생성

```go
node, _ := ctx.SpotNode()
defer node.Close()

actor, err := node.Actor("player-42")
if err != nil { ... }
// 액터는 노드가 닫히면 함께 소멸됩니다

ref := actor.Ref() // 다른 노드에서 이 액터를 참조할 때 사용
```

### 스팟 조인

```go
spot, _ := node.Spot()
defer spot.Close()

joinDone := make(chan error, 1)
_, err := actor.Join(spot).
    Message(joinPayload).
    Flags(zlink.SendFlagsDontWait).
    Timeout(5 * time.Second).
    Submit(nil, func(result zlink.ActorJoinResult, parts []*zlink.Message) {
        defer func() {
            for _, p := range parts { p.Close() }
        }()
        if result.Result == zlink.RequestOK {
            joinDone <- nil
        } else {
            joinDone <- fmt.Errorf("join failed: %v", result.Result)
        }
    })
if err != nil { ... }

// 스팟 쪽에서 조인 요청 수락
go func() {
    var req zlink.ActorJoinRequest
    spot.RecvActorJoin(&req, zlink.RecvFlagsNone)
    defer req.Close()
    replyMsg, _ := zlink.NewMessageFrom([]byte("welcome"))
    spot.ReplyActorJoin(&req, 0).Message(replyMsg).Submit(nil)
}()

if err := <-joinDone; err != nil { ... }
```

### 액터 메시지 수신

```go
received, err := actor.Recv(zlink.RecvFlagsDontWait)
if err != nil { ... }
if received != nil {
    defer received.Close()
    fmt.Printf("from actor: %s\n", received.Message.Data())
}
```

### 스팟 떠나기

```go
leaveCh, err := actor.Leave(spot).Timeout(3 * time.Second).SubmitAsync(nil)
if err != nil { ... }
leave := <-leaveCh
if leave.Err != nil { ... }
zlink.MultipartClose(leave.Parts)
```
