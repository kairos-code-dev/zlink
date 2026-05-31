[English](./02-messaging.md) | [한국어](./02-messaging.ko.md)

[← 시작하기](./01-getting-started.ko.md) · [.NET 가이드](./index.ko.md) · [다음: 서비스 →](./03-services.ko.md)

# 메시징 — 소켓 패턴별 사용법

8종 소켓 모두 `ctx.Create...Socket()`로 만들고 `Bind`/`Connect`로 연결합니다.
메시지 소켓(PAIR·DEALER)은 송신 빌더 `Send()...Submit()`과 `Recv(received)`를,
ROUTER·STREAM은 라우팅이 포함된 송신/수신을, PUB/SUB은 `Publish()`/`Subscribe()`를
사용합니다. 패턴별 의미는 [소켓 패턴](../../03-0-socket-patterns.ko.md)을 참고하세요.

> **연결 대기**: 아래 예제들은 첫 메시지 전에 연결이 맺어졌는지
> `MonitorOpen(...)` + `monitor.Recv()`로 기다립니다. 자세한 내용은
> [운영 — 모니터링](./04-operations.ko.md#모니터링-monitor) 절을 참고하세요.

---

## PAIR

1:1 배타적 연결. 가장 단순한 패턴입니다.

```csharp
// 서버
using var ctx = Zlink.CreateContext();
using var server = ctx.CreatePairSocket();
using var mon = server.MonitorOpen(SocketEvent.ConnectionReady);
server.Bind("tcp://127.0.0.1:5555");
mon.Recv();   // 연결될 때까지 대기

using var received = Received.Create();
server.Recv(received);
Console.WriteLine(received.FirstPart().GetString());   // PING

using var reply = Message.From("ACK");
server.Send().Message(reply).Submit();
```

```csharp
// 클라이언트
using var ctx = Zlink.CreateContext();
using var client = ctx.CreatePairSocket();
using var mon = client.MonitorOpen(SocketEvent.ConnectionReady);
client.Connect("tcp://127.0.0.1:5555");
mon.Recv();

using var ping = Message.From("PING");
client.Send().Message(ping).Submit();

using var received = Received.Create();
client.Recv(received);
Console.WriteLine(received.FirstPart().GetString());   // ACK
```

---

## DEALER / ROUTER

확장 가능한 비동기 요청/응답입니다. `Dealer`는 `Request()`로 요청을 보내고 응답을
비동기로 기다리며, `Router`는 수신한 요청에 응답합니다.

```csharp
// 클라이언트 (Dealer)
using var dealer = ctx.CreateDealerSocket();
dealer.SetRoutingId(RoutingId.From("order-client"));
dealer.Connect("tcp://127.0.0.1:5555");

using var request = Message.From("ping");
IReadOnlyList<Message> reply = await dealer.Request()
    .Message(request)
    .Timeout(TimeSpan.FromSeconds(2))
    .SubmitAsync();

using Message payload = reply[0];
Console.WriteLine(payload.GetString());          // pong
for (int i = 1; i < reply.Count; i++)            // 응답 파트는 호출자 소유
    reply[i].Dispose();
```

```csharp
// 서버 (Router)
using var router = ctx.CreateRouterSocket();
router.Bind("tcp://127.0.0.1:5555");

using var received = Received.Create();
router.Recv(received);

string body = received.FirstPart().GetString();

// 수신 봉투로 바로 응답 (요청 경로/시퀀스를 자동 사용)
using var pong = Message.From("pong");
received.Reply().Message(pong).Submit();
```

> `Router`는 명시적으로도 응답할 수 있습니다:
> `router.Reply(received.RoutingId!.Value, received.RequestSeq ?? 0UL).Message(pong).Submit();`
> 평범한 라우팅 송신은 `received.Send()...` 또는 `router.Send(routingId)...`.

---

## PUB / SUB

토픽 기반 발행/구독입니다. `Sub`은 구독한 토픽만 받습니다.

```csharp
// 발행자
using var publisher = ctx.CreatePubSocket();
using var pmon = publisher.MonitorOpen(SocketEvent.ConnectionReady);
publisher.Bind("tcp://127.0.0.1:5556");

// 구독자
using var subscriber = ctx.CreateSubSocket();
using var smon = subscriber.MonitorOpen(SocketEvent.ConnectionReady);
subscriber.Connect("tcp://127.0.0.1:5556");
pmon.Recv(); smon.Recv();

subscriber.SetSubscription("prices");           // 토픽 구독 (여러 개 가능)

using (var msg = Message.From("101.25"))
    publisher.Publish("prices").Message(msg).Submit();

using var topicMsg = new TopicMessage();
if (subscriber.Subscribe(topicMsg))              // 매칭 메시지 수신
{
    Console.WriteLine($"{topicMsg.Topic}: {topicMsg.SinglePartOrThrow().GetString()}");
    // prices: 101.25
}
```

`UnsetSubscription("prices")`로 구독을 해제합니다.

> PUB/SUB은 slow-joiner 특성이 있습니다 — 구독이 발행자에 전파되기 전에 발행된
> 메시지는 버려집니다. 위 예제의 `Subscribe(topicMsg)`는 메시지가 올 때까지
> **블로킹**하므로, 운영 코드에서는 `RecvFlags.DontWait`로 폴링하거나 발행을
> 반복하세요. 개념은 [PUB/SUB](../../03-2-pubsub.ko.md) 참고.

---

## XPUB / XSUB

`XPub`은 `Pub`과 같지만 **구독자의 (un)subscribe 이벤트**를 직접 받을 수 있어,
구독 시점에 환영 메시지를 보내거나 구독 수를 추적하는 데 씁니다.

```csharp
using var xpub = ctx.CreateXPubSocket();
xpub.Bind("tcp://127.0.0.1:5557");

using var ev = new SubscriptionEvent();
if (xpub.ReceiveSubscriptionEvent(ev, RecvFlags.DontWait))
{
    Console.WriteLine($"{(ev.Subscribed ? "구독" : "해제")}: {ev.Topic} (peer={ev.RoutingId})");
}
```

`XSub`은 구독을 메시지로 전달하는 구독자 측입니다(프록시 구성에 사용).

---

## STREAM

원시 TCP 피어와 통신합니다(비-zlink 클라이언트 포함). 수신 시 피어의 라우팅 ID가
함께 옵니다.

```csharp
using var stream = ctx.CreateStreamSocket();
stream.Options.Linger = TimeSpan.Zero;
using var mon = stream.MonitorOpen(SocketEvent.Accepted);
stream.Bind("tcp://127.0.0.1:5558");
mon.Recv();   // 클라이언트 accept 대기

using var received = Received.Create();
stream.Recv(received);
RoutingId peer = received.RoutingId!.Value;
string    data = received.FirstPart().GetString();

using var reply = Message.From("ack");
received.Send().Message(reply).Submit();         // 같은 피어에게 응답
```

### 수신 방식 — direct recv vs 패킷 콜백 (택일)

STREAM은 프레이밍(패킷 경계)을 응용이 정의합니다. .NET 바인딩이 노출하는 수신
방식은 두 가지이고 **서로 배타적**입니다(콜백을 켜면 direct recv 불가). 개념과 C
레벨의 추가 모드는 코어 [STREAM §콜백](../../03-5-stream.ko.md)이 소유합니다.

| 방식 | 호출 | 언제 |
|------|------|------|
| direct recv | `stream.Recv(received)` (폴러와 함께) | 직접 루프/폴러에서 당겨 처리. 프레이밍을 응용이 파싱 |
| 패킷 콜백 | `stream.OnPacket(handler)` | 고정 프레이밍(2B 헤더 길이 + 4B 바디 길이, big-endian)을 따르는 패킷 — 조각 누적을 라이브러리가 대신 |

**패킷 콜백** — 프레임 단위로 받습니다. 핸들러가 header·body를 **소유하므로 각각
정확히 한 번 dispose** 해야 합니다.

```csharp
StreamPacketHandler handler = (routingId, header, body) =>
{
    using (header)
    using (body)
        Console.WriteLine(body.GetString());
};
stream.OnPacket(handler);
```

> 콜백은 백그라운드 디스패치 스레드에서 돕니다. 콜백 안에서 소켓을 닫지 마세요.
> 콜백은 하나만 등록됩니다. 외부 클라이언트를 Actor로 잇는 STREAM 게이트웨이는
> [서비스 — Actor](./03-services.ko.md#actor-액터)를 참고하세요.

---

## 프록시 (Proxy)

두 소켓 사이를 중계합니다(예: ROUTER 프론트엔드 ↔ DEALER 백엔드). 자세한 패턴은
[프록시](../../03-6-proxy.ko.md)를 참고하세요.

```csharp
using var frontend = ctx.CreateRouterSocket();
using var backend  = ctx.CreateDealerSocket();
frontend.Bind("tcp://127.0.0.1:5559");
backend.Bind("tcp://127.0.0.1:5560");

Zlink.Proxy(frontend, backend);                  // 블로킹 (보통 전용 스레드에서)
// 제어 가능한 형태: Zlink.ProxySteerable(frontend, backend, capture, control);
```

---

다음: [서비스 — Registry · Discovery · SpotNode·Spot · Actor →](./03-services.ko.md)
