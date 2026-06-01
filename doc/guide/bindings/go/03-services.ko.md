[← 메시징](./02-messaging.ko.md) · [Go 가이드](./index.ko.md) · [다음: 운영 →](./04-operations.ko.md)

# 서비스 레이어

raw 소켓은 "주소를 아는 두 지점"을 잇습니다. 서비스 레이어는 그 위에 **동적
토폴로지**(이름으로 발견, 런타임에 생겼다 사라지는 단위)를 얹습니다. 개념의 정식
정의와 층별 멘탈 모델은
[서비스 개요 §멘탈 모델](../../07-0-services.ko.md)이
소유하며, 이 문서는 **각 기능의 역할·언제** + **Go 사용 형태**를 다룹니다.

> 핵심: 서비스 레이어는 **상태 저장소가 아닙니다.** 룸·세션 데이터는 응용이
> 소유합니다. SPOT은 그 상태에 닿는 메시지를 **단일 실행 큐로 직렬 처리**(lock
> 불필요)하고, Actor는 세션이 어느 서버에 붙어 있든 **같은 엔티티로 이어 줍니다**.

## 언제 서비스 레이어가 필요한가

| 상황 | 권장 |
|------|------|
| 주소가 고정된 소수 노드 | raw 소켓([02 메시징](./02-messaging.ko.md))으로 충분 |
| 노드가 동적으로 늘고 줄어 이름으로 찾아야 함 | **Registry + Discovery** |
| 방·스테이지·존처럼 런타임에 생기는 라우팅 단위 | **SpotNode / Spot** |
| 세션/플레이어처럼 정체성을 갖는 엔티티 | **Actor** |

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

메시 노드(SpotNode)와 그 위의 메시징 엔드포인트(Spot)입니다. "방·스테이지·존"
같은 동적 단위가 전형적인 Spot입니다. 개념: [SPOT](../../07-3-spot.ko.md).

**왜 Spot인가 — 실행 직렬성.** 한 Spot으로 들어온 메시지는 **단일 실행 큐로 직렬
처리**됩니다. 룸 상태를 lock으로 보호할 필요 없이 동시성 문제가 사라집니다. 게임
룸·심볼 오더북·채팅방처럼 한 단위의 상태를 안전하게 갱신할 때 raw PUB/SUB 대신
Spot을 쓰는 이유입니다. 상태 데이터는 여전히 응용이 소유합니다.

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
if _, err := spot.Subscribe(&topic, zlink.RecvFlagsNone); err != nil { ... }
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

Spot에 합류(join)해 그 Spot으로 들어온 메시지를 받는 **상태 보유 엔티티**입니다
(플레이어·세션·작업 큐). 개념: [Actor](../../07-4-actor.ko.md).

**왜 Actor인가 — 재접속 이전성.** Actor는 actor id로 식별되며 세션 연결과 별개로
존재합니다. 클라이언트가 끊겼다 다른 연결 서버로 재접속해도 같은 Actor로 다시
묶입니다 — "어느 서버에 붙어 있었는지"를 외부 저장소로 관리하던 일을 라이브러리가
가져갑니다. Actor는 raw 소켓의 대안이 아니라 **Spot 위에 얹는 한 단계 더 높은
모델**이며, Actor 메시지도 결국 Spot routed 평면 위로 흐릅니다.

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

// 스팟 쪽에서 조인 요청 수락 — RecvActorJoin은 요청 객체를 반환합니다
go func() {
    req, err := spot.RecvActorJoin(zlink.RecvFlagsNone)
    if err != nil { return }
    defer req.Message.Close()
    replyMsg, _ := zlink.NewMessageFrom([]byte("welcome"))
    spot.ReplyActorJoin(req, 0).Message(replyMsg).Submit(nil)
}()

if err := <-joinDone; err != nil { ... }
```

### 액터 메시지 수신

`RecvPart`는 `*ActorPart`를 반환하며, 페이로드는 `.Message` 필드에 있습니다.

```go
part, err := actor.RecvPart(zlink.RecvFlagsDontWait)
if err != nil { ... }
if part != nil {
    defer part.Message.Close()
    fmt.Printf("from actor: %s\n", part.Message.Data())
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
