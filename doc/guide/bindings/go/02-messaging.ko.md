[← 시작하기](./01-getting-started.ko.md) · [Go 가이드](./index.ko.md) · [다음: 서비스 →](./03-services.ko.md)

# 메시징

소켓 패턴별 Go API 사용법을 설명합니다. 각 패턴의 **개념**(언제 쓰는지, 내부 동작)은
코어 가이드를 참고하고, 여기서는 **Go에서 어떻게 코딩하는지**에 집중합니다.

---

## PAIR

1:1 배타적 연결. 라우팅 없음. ([코어 참고](../../03-1-pair.ko.md))

```go
ctx, _ := zlink.NewContext()
defer ctx.Close()

server, _ := ctx.PairSocket()
defer server.Close()
client, _ := ctx.PairSocket()
defer client.Close()

server.Bind("tcp://127.0.0.1:5560")
client.Connect("tcp://127.0.0.1:5560")

// 전송
msg, _ := zlink.NewMessageFrom([]byte("hello"))
client.Send().Message(msg).Submit(nil)

// 수신
var received zlink.Received
server.Recv(&received, zlink.RecvFlagsNone)
defer received.Close()

part, _ := received.SinglePartOrError()
fmt.Println(string(part.Data()))
```

---

## DEALER / ROUTER

비동기 요청/응답. DEALER가 클라이언트, ROUTER가 서버.
([코어 DEALER](../../03-3-dealer.ko.md) / [코어 ROUTER](../../03-4-router.ko.md))

### 단순 송수신

```go
router, _ := ctx.RouterSocket()
defer router.Close()
dealer, _ := ctx.DealerSocket()
defer dealer.Close()

// 라우팅 ID 설정 (선택, 설정 안 하면 임의 할당)
rid := zlink.NewRoutingIDFromString("client-01")
dealer.SetRoutingID(rid)

router.Bind("tcp://127.0.0.1:5561")
dealer.Connect("tcp://127.0.0.1:5561")

// 클라이언트: 요청 전송
req, _ := zlink.NewMessageFrom([]byte("get-price"))
dealer.Send().Message(req).Submit(nil)

// 서버: 수신 후 회신
var request zlink.Received
router.Recv(&request, zlink.RecvFlagsNone)
defer request.Close()

reply, _ := zlink.NewMessageFrom([]byte("101.25"))
request.Send().Message(reply).Submit(nil) // Received에서 직접 회신

// 클라이언트: 응답 수신
var response zlink.Received
dealer.Recv(&response, zlink.RecvFlagsNone)
defer response.Close()

part, _ := response.SinglePartOrError()
fmt.Println(string(part.Data())) // 101.25
```

### 비동기 요청 (SubmitAsync)

응답을 채널로 받습니다.

```go
completions, err := dealer.Request().
    Message(msg).
    Timeout(2 * time.Second).
    SubmitAsync(nil) // nil = 취소 채널 없음
if err != nil { ... }

completion := <-completions
if completion.Err != nil { ... }

// 회신 파트 사용 후 반드시 닫기
parts := completion.Parts
defer func() {
    for _, p := range parts { p.Close() }
}()
fmt.Println(string(parts[0].Data()))
```

서버 쪽은 `request.RequestSeq()`로 시퀀스를 가져와 ROUTER에서 회신합니다:

```go
var request zlink.Received
router.Recv(&request, zlink.RecvFlagsNone)
defer request.Close()

if request.HasRequestSeq() {
    seq := request.RequestSeq()
    reply, _ := zlink.NewMessageFrom([]byte("ok"))
    router.Reply(request.RoutingID(), seq).Message(reply).Submit(nil)
}
```

### 멀티파트 전송

한 번의 Submit으로 여러 프레임을 보냅니다.

```go
header, _ := zlink.NewMessageFrom([]byte("cmd:buy"))
body, _ := zlink.NewMessageFrom([]byte(`{"qty":10}`))
dealer.Send().Message(header).Message(body).Submit(nil)
```

수신 쪽에서는 `received.Parts()`로 모든 프레임에 접근합니다.

---

## PUB / SUB

토픽 기반 팬아웃. ([코어 참고](../../03-2-pubsub.ko.md))

```go
pub, _ := ctx.PubSocket()
sub, _ := ctx.SubSocket()
defer pub.Close()
defer sub.Close()

pub.Bind("tcp://127.0.0.1:5562")
sub.Connect("tcp://127.0.0.1:5562")

// 구독 등록 (빈 문자열 = 전체 구독)
sub.SetSubscription("prices")

// 발행
msg, _ := zlink.NewMessageFrom([]byte("101.25"))
pub.Publish("prices").Message(msg).Submit(nil)

// 수신
var topic zlink.TopicMessage
sub.Subscribe(&topic, zlink.RecvFlagsNone)
defer topic.Close()

part, _ := topic.SinglePartOrError()
fmt.Printf("topic=%s payload=%s\n", topic.Topic(), part.Data())
```

> `SetSubscription`은 Connect/Bind 이후에 호출합니다. PUB는 구독자가 없으면 메시지를 버립니다.

---

## XPUB / XSUB

구독 이벤트를 직접 처리해야 할 때 사용합니다. XPUB은 구독/해지 이벤트를 수신하고,
XSUB은 구독을 메시지로 송신합니다. ([코어 참고](../../03-2-pubsub.ko.md))

```go
xpub, _ := ctx.XPubSocket()
sub, _ := ctx.SubSocket()

xpub.Bind("tcp://127.0.0.1:5563")
sub.Connect("tcp://127.0.0.1:5563")
sub.SetSubscription("events")

// 구독 이벤트 수신 (subscribe 메시지)
var event zlink.SubscriptionEvent
xpub.ReceiveSubscriptionEvent(&event, zlink.RecvFlagsNone)
fmt.Printf("subscribed=%v topic=%s\n", event.Subscribed(), event.Topic())
```

---

## STREAM

원시 TCP 피어와 바이트 스트림을 교환합니다. ([코어 참고](../../03-5-stream.ko.md))

```go
server, _ := ctx.StreamSocket()
defer server.Close()
server.Bind("tcp://127.0.0.1:5564")

// 일반 TCP 클라이언트 연결
conn, _ := net.Dial("tcp", "127.0.0.1:5564")
defer conn.Close()
conn.Write([]byte("hello"))

// STREAM 소켓 수신: 라우팅 ID = TCP 세션 ID
var received zlink.Received
server.Recv(&received, zlink.RecvFlagsNone)
defer received.Close()

part, _ := received.SinglePartOrError()
fmt.Println(string(part.Data())) // hello

// TCP 클라이언트로 회신
reply, _ := zlink.NewMessageFrom([]byte("world"))
received.Send().Message(reply).Submit(nil)
```

패킷 콜백 방식으로 처리할 수도 있습니다:

```go
server.SetRecvHandler(func(rid zlink.RoutingID, parts []*zlink.Message) {
    for _, p := range parts {
        fmt.Printf("from %s: %s\n", rid.String(), p.Data())
        p.Close()
    }
})
```

---

## 프록시 (Proxy)

FRONTEND → BACKEND 사이를 중계합니다. 호출하는 고루틴을 블록합니다.

```go
frontend, _ := ctx.XSubSocket()
backend, _ := ctx.XPubSocket()
frontend.Bind("tcp://127.0.0.1:5565")
backend.Bind("tcp://127.0.0.1:5566")

go zlink.Proxy(frontend, backend) // 컨텍스트 종료까지 블록
```

제어 가능한 프록시(런타임 제어):

```go
control, _ := ctx.PairSocket()
go zlink.ProxySteerable(frontend, backend, nil, control)
// control 소켓으로 "TERMINATE" 문자열을 보내면 중단
```

---

## 논블로킹 수신

`RecvFlagsDontWait`으로 블로킹 없이 시도합니다. 수신할 메시지가 없으면 `false, nil`을
반환합니다.

```go
var received zlink.Received
ok, err := socket.Recv(&received, zlink.RecvFlagsDontWait)
if err != nil { ... }   // 진짜 에러
if !ok { continue }     // 메시지 없음, 나중에 다시 시도
defer received.Close()
```

---

## 전송 엔드포인트

모든 소켓은 `tcp`, `ipc`, `inproc`, `ws`, `tls+tcp` 트랜스포트를 지원합니다.

```go
socket.Bind("tcp://0.0.0.0:5555")
socket.Bind("ipc:///tmp/my.sock")
socket.Bind("inproc://my-channel")
socket.Connect("tcp://10.0.0.1:5555")
socket.Disconnect("tcp://10.0.0.1:5555")
socket.Unbind("tcp://0.0.0.0:5555")
```
