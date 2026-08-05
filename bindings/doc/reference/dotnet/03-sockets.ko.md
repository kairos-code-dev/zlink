한국어 | [English](03-sockets.en.md)

[레퍼런스 목차](README.ko.md)

# 03. Sockets

이 category는 (Core category의 `IContext` factory로 생성되는) 8개 socket-type
interface, 이들의 공유 lifecycle/option 기반, 그리고 타입별 typed option을 다룬다.
모든 socket의 `Send`/`Publish`/`Request`/`Reply`는 Messaging category에 문서화된
operation-builder family를 반환한다 — 이 category는 각 builder가 어디서 시작하고 각
socket type이 고유하게 무엇을 더하는지만 다룬다. 정확한 signature는
[`Contracts/Sockets/`](../../../../bindings/dotnet/src/Zlink/Contracts/Sockets/)가
소유한다.

---

## `ISocket` / `IConnectableSocket` 공유 lifecycle

모든 socket type이 구현하는 기반 contract다 — binding, TLS, monitoring, disposal, 그리고
(기본 marker를 제외한 모든 socket type의) outbound connection.

```csharp
socket.Bind("tcp://*:5555");
socket.SetTlsServer(certPath, keyPath, requireClientCert: true);
using IZlinkSocket monitor = (IZlinkSocket)socket.MonitorOpen(SocketEvent.All);
socket.Close();
```

**Options.** `ISocket`: `Options`(`CommonSocketOptions`, 아래), `Bind(string address)`,
`Unbind(string address)`, `MonitorOpen(SocketEvent events = SocketEvent.All)`,
`SetTlsServer(certPath, keyPath, requireClientCert = false)`(binding 전에 적용),
`SetTlsClient(caCertPath, hostname, trustSystem = false)`(connecting 전에 적용),
`Close()`. `IConnectableSocket`(기본 `IZlinkSocket` marker를 제외한 모든 socket type)은
`Connect(string address)`, `Disconnect(string address)`,
`DisconnectRid(RoutingId peerRid)`를 더한다.

**Completion result.** `MonitorOpen`을 제외한 모든 member는 반환값 없이 동기다.
`MonitorOpen`은 `ISocketMonitor`(Eventing category)를 동기로 반환한다 — caller가
소유하며 반드시 dispose해야 한다. `Close()`는 native socket을 즉시 닫는다 —
`Dispose()`와 달리 `IDisposable` semantic을 기다리지 않는다. `ISocket`/`IZlinkSocket`
자체가 `IDisposable`/`IAsyncDisposable`이다.

**선택 기준.** `Bind`/`Connect` 전에 각각 `SetTlsServer`/`SetTlsClient`를 호출한다 —
이미 bind·connect된 후에 적용하면 기존 상태엔 영향이 없다. socket 자체엔 `using`을
선호하고, native socket이 일반 disposal이 아니라 즉시 해제돼야 할 때만 `Close()`를
쓴다.

---

## `CommonSocketOptions`

`socket.Options`로 도달하는, 모든 socket type이 공유하는 typed option facade.

```csharp
socket.Options.SendHighWaterMark = 100_000;
socket.Options.Linger = TimeSpan.FromSeconds(1);
socket.Options.SubmitRetryMode = SubmitRetryMode.LocalFailure;
```

**Options.** `MaxMessageSize`(`long`, -1 = 제한 없음), `SendHighWaterMark`/
`ReceiveHighWaterMark`(`ulong` accounted-byte 제한, 0 = 제한 없음 — Core category의
byte-HWM 참고), `SendBufferSize`/`ReceiveBufferSize`(`int`, -1 = OS 기본값), `Linger`
(`TimeSpan?`, null = 무기한 대기), `ReconnectInterval`/`ReconnectIntervalMax`
(`TimeSpan?`, null이면 비활성화/무제한), `Backlog`(`int`), `ReceiveTimeout`/
`SendTimeout`/`ConnectTimeout`/`HandshakeInterval`(`TimeSpan?`, null = 무기한 block /
OS·native 기본값), `TcpKeepAlive`(`int`, -1/0/1), `IPv6`/`TcpNoDelay`/`Immediate`
(`bool`), `SubmitRetryMode`(`SubmitRetryMode`), `SubmitRetryTimeoutMilliseconds`/
`SubmitRetryAttempts`(`int`), `RoutingIdDuplicatePolicy`(`RidDuplicatePolicy`),
`LastEndpoint`(`string`, 읽기 전용 — 실제로 resolve된 bind 주소).

**Completion result.** 모든 property get/set은 동기다.

**선택 기준.** 기본값이 배포 환경에 맞지 않을 때 socket이 메시지 교환을 시작하기 전에
`SendHighWaterMark`/`ReceiveHighWaterMark`, `Linger`를 설정한다. wildcard 주소로
bind한 후 resolve된 포트를 알려면 `LastEndpoint`를 읽는다. local back-pressure에
걸린 submit이 caller에게 노출되지 않고 자동으로 재시도돼야 할 땐
`SubmitRetryMode.LocalFailure`를 쓴다.

---

## `IPairSocket`

PAIR socket — 배타적 1:1 peering이며 라우팅이 없고, 공유 `IMessageSocket` 표면 외에
필드나 옵션이 없다.

```csharp
using IPairSocket pair = context.CreatePairSocket();
pair.Send().Message(Message.From("ping")).Submit();
using Received received = Received.Create();
if (pair.Recv(received)) { /* ... */ }
```

**Options.** `IMessageSocket`(PAIR/DEALER 공유): `Send()`(공유 `SendOperation` builder
시작 — Messaging category), `Recv(Received result, RecvFlags flags = RecvFlags.None)`,
`OnSendReady(Action handler)`(back-pressure 해소 콜백, background dispatch 스레드에서
실행). `IPairSocket`은 `IMessageSocket` 외에 더하는 게 없다.

**Completion result.** `Recv`는 `bool`을 반환한다 — `RecvFlags.DontWait`가 설정되고
메시지가 없을 때만 `false`다.

**선택 기준.** 배타적 point-to-point 링크(예: 고정된 두 endpoint 사이의 제어
채널)엔 PAIR를 쓴다 — peer 라우팅이 없고 load-balance하지 않는다.

---

## `IDealerSocket`

DEALER socket — 연결된 peer 전체에 send를 load-balance하고 routed request를 낼 수
있다.

```csharp
using IDealerSocket dealer = context.CreateDealerSocket();
dealer.SetRoutingId(RoutingId.From("worker-3"));
IReadOnlyList<Message> reply = await dealer.Request()
    .Message(Message.From("payload"))
    .Async();
```

**Options.** `IMessageSocket`에 더하는 것: `Options`(`DealerSocketOptions`: `Probe`
set-only — connect 시 빈 probe 전송; `RequestTimeout` set-only `TimeSpan?`;
`PeerWeight` `int` load-balancing 가중치 0-100), `SetRoutingId(RoutingId)`/
`GetRoutingId()`, `Request()`(공유 `RequestOperation` builder 시작 — Messaging
category; target 인자가 없다 — DEALER는 API 레벨 peer routing id가 없기 때문이다).

**Completion result.** `Request()`의 builder는 Messaging category의
operation-builder 항목대로 resolve된다. `SetRoutingId`/`GetRoutingId`는 동기다.

**선택 기준.** peer가 첫 메시지부터 이를 관찰하도록 connect 전에 `SetRoutingId`를
설정한다. DEALER는 임의 token에 reply할 수 없다 — 그런 protocol envelope helper가
없다 — reply는 수신된 request context(`Received.Reply()`, Messaging category)에서
답하거나, target context가 요구할 때 명시적 ROUTER/service reply 표면에서 답한다.

---

## `IRouterSocket`

ROUTER socket — routing id로 지정된 peer에게 메시지를 보내고, 특정 peer의 request에
reply할 수 있다.

```csharp
using IRouterSocket router = context.CreateRouterSocket();
router.Send(peerRid).Message(Message.From("hello")).Submit();
router.OnCompletionControl((rid, parts) => { /* ... */ });
```

**Options.** `IRoutedMessageSocket`(`Send(RoutingId)`, `Recv(Received, RecvFlags)`,
`OnSendReady(Action)`)와 `IConnectableSocket`에 더하는 것: `Options`
(`RouterSocketOptions`: `Mandatory` `bool` — 알 수 없는 route로의 send를 조용히
버리는 대신 에러; `Handover` `bool` — `RoutingIdDuplicatePolicy`의 shorthand;
`Probe` `bool`; `ConnectRoutingId` `RoutingId?` 읽기 전용에 더해
`SetConnectRoutingId(RoutingId)`로 peer가 고르는 대신 다음 outbound connection의
id를 지정; `RequestTimeout` `TimeSpan?`; `PeerWeight` `int` 0-100),
`SetRoutingId(RoutingId)`/`GetRoutingId()`, `Request(RoutingId peerRid)`(Messaging
category의 `RequestOperation`), `Reply(RoutingId rid, ulong requestSeq)`(Messaging
category의 `ReplyOperation`), `TrySendCompletionControl(RoutingId peerRid,
IReadOnlyList<Message> parts)`, `OnCompletionControl(CompletionControlHandler
handler)`.

**Completion result.** `TrySendCompletionControl`은 동기로 `bool`을 반환한다 —
`parts`를 소비하지 않는다(Core는 payload에 명령 의미를 부여하지 않는다). `false`는
completion-lane back-pressure를 뜻하고, 그 외 실패는 `ZlinkSubmitException`을
던진다. `CompletionControlHandler`는 background dispatch 스레드에서 실행되며
`parts`의 모든 메시지를 소유한다 — 각각 정확히 한 번 dispose해야 한다.

**선택 기준.** DEALER가 특정 peer를 지정할 수 없는 ROUTER 주도·ROUTER 응답
request/reply엔 `Request(peerRid)`/`Reply(rid, requestSeq)`를 쓴다.
application-level receive와 독립적인, peer의 기존 completion connection 위 opaque
bounded control record엔 `TrySendCompletionControl`/`OnCompletionControl`을 쓴다.

---

## `IPubSocket` / `IXPubSocket`

PUB는 매칭되는 구독자가 없으면 버리는 topic-filtered 메시지를 publish하고, XPUB는
추가로 구독자의 subscribe/unsubscribe event를 노출한다.

```csharp
using IPubSocket pub = context.CreatePubSocket();
pub.Publish("prices").Message(Message.From(tick)).Submit();

using IXPubSocket xpub = context.CreateXPubSocket();
using SubscriptionEvent evt = new SubscriptionEvent();
if (xpub.ReceiveSubscriptionEvent(evt)) { /* ... */ }
```

**Options.** `IPublisherSocket`(PUB/XPUB 공유): `Publish(string topic)`(공유
`SendOperation` builder 시작), `OnSendReady(Action handler)`. `IPubSocket`은
`Options`(`PubSocketOptions`, 아래), `SetRoutingId(RoutingId)`/`GetRoutingId()`를
더한다. `IXPubSocket`은 `Options`(역시 `PubSocketOptions` — 같은 facade 타입)와
`ReceiveSubscriptionEvent(SubscriptionEvent result, RecvFlags flags =
RecvFlags.None)`를 더한다. `IPubSocket`과 달리 **`IXPubSocket`은 자신만의
`SetRoutingId`/`GetRoutingId`가 없다** — `IPublisherSocket`만 상속한다.
`PubSocketOptions`: `Verbose`/`Verboser` `bool`(중복 포함 모든 (un)subscribe
메시지 전달), `Manual` `bool`(구독이 auto-accept 대신 `ApproveSubscribe`/
`RejectSubscribe`를 요구), `ManualLastValue` `bool`(새로 승인된 구독자에게 topic별
마지막 캐시 메시지도 재생하는 manual 모드), `NoDrop` `bool`(back-pressure에서 조용히
버리는 대신 에러), `WelcomeMessage`(`Message`, 새로 연결된 구독자마다 자동 전송 —
getter는 caller 소유 복사본을 반환), `TopicsCount`(`int`, 읽기 전용),
`ApproveSubscribe(RoutingId)`/`RejectSubscribe(RoutingId)`(`Manual` 필요).

**Completion result.** `ReceiveSubscriptionEvent`는 `bool`을 반환한다 —
`RecvFlags.DontWait`에서 아무것도 없을 때만 `false`다. `ApproveSubscribe`/
`RejectSubscribe`는 반환값 없이 동기다.

**선택 기준.** publish 자체는 둘이 같게 동작하지만, `ReceiveSubscriptionEvent`로
구독자 변동을 관찰하려면(또는 `Manual`/`ApproveSubscribe`/`RejectSubscribe`로 수동
admission을 하려면) `IPubSocket` 대신 `IXPubSocket`을 쓴다.

---

## `ISubSocket` / `IXSubSocket`

SUB는 구독을 socket option으로 설정하는 방식으로 topic을 구독하고, XSUB는 대신
구독을 메시지로 실어 나른다.

```csharp
using ISubSocket sub = context.CreateSubSocket();
sub.SetSubscription("prices.");
using TopicMessage msg = new TopicMessage();
if (sub.Subscribe(msg)) { /* ... */ }
```

**Options.** `ISubscriberSocket`(SUB/XSUB 공유): `SetSubscription(string
topicOrPattern)`/`UnsetSubscription(string topicOrPattern)`(구독은 누적된다),
`SubscriptionAt(int index)`(`SubscriptionEntry?`, 범위를 벗어나면 null),
`Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None)`. `ISubSocket`은
`Options`(`SubSocketOptions`: `TopicsCount` `int`, 읽기 전용),
`SetRoutingId(RoutingId)`/`GetRoutingId()`를 더한다. **`IXSubSocket`은
`ISubscriberSocket` 외에 더하는 게 없다** — 자신의 `Options`(역시 `SubSocketOptions`
타입) 외엔 고유 member가 없고 모든 operation이 공유 `ISubscriberSocket` 표면이다.

**Completion result.** `Subscribe`는 `bool`을 반환한다 — `RecvFlags.DontWait`에서
아무것도 없을 때만 `false`다.

**선택 기준.** 일반적인 경우(구독을 socket option으로 설정)엔 `ISubSocket`을 쓴다.
구독을 일반 메시지로 실어 날라야 할 때(예: 메시지만 중계하는 device를 통해 구독
상태를 전달할 때)만 `IXSubSocket`을 쓴다.

---

## `IStreamSocket`

STREAM socket — 다른 모든 socket type이 쓰는 zlink wire protocol 밖에서, raw TCP
peer와 framed packet을 직접 주고받는다.

```csharp
using IStreamSocket stream = context.CreateStreamSocket();
stream.OnPacket((routingId, header, body) => { /* header/body 소유; 각각 한 번 dispose */ });
```

**Options.** `IRoutedMessageSocket`을 확장: `Options`(`StreamSocketOptions`: `Notify`
`bool` — peer connect/disconnect를 application message로 전달), `OnPacket
(StreamPacketHandler handler)`(background-dispatch-thread 콜백; handler가
`header`/`body`를 소유하며 정확히 한 번 dispose해야 함), `RecvPart(out RoutingId?
sourceRoutingId, out Message? part, out bool hasMore, RecvFlags flags =
RecvFlags.None)`(첫 receive 호출이 이 socket을 receive 모드로 고정한다 — `OnPacket`
등록과 함께 쓸 수 없다), `DisconnectRid(RoutingId peerRid)`.

**Completion result.** `RecvPart`는 동기로 `bool`을 반환한다 — 반환된 `part`는
caller 소유이며 반드시 dispose해야 한다. `StreamPacketHandler`는 메시지 소유권을
콜백으로 이전하며, 콜백은 `header`와 `body`를 정확히 한 번씩 dispose해야 한다.

**선택 기준.** callback 기반 packet loop엔 `OnPacket`을, pull 기반 loop엔
`RecvPart`를 선택한다 — 첫 receive 호출이 이뤄지면 둘은 상호 배타적이다.
application이 raw peer connect/disconnect를 직접 관찰해야 할 땐 `Notify`를 쓴다.

---

## Socket enum

위 모든 항목에서 참조하는 공유 enum.

| Enum | 사용처 | 값 |
|---|---|---|
| `SocketType` | 내부 socket 종류 식별 | `Any`, `Pair`, `Pub`, `Sub`, `Dealer`, `Router`, `XPub`, `XSub`, `Stream` |
| `AutoHwmProfile` | `IContextOptions.AutoHwmProfile`(Core category) | `Compact`, `LowLatency`, `Balanced`, `Throughput` |
| `RidDuplicatePolicy` | `CommonSocketOptions.RoutingIdDuplicatePolicy`, `RouterSocketOptions.Handover` | `Reject`, `Handover` |
| `SubmitRetryMode` | `CommonSocketOptions.SubmitRetryMode` | `Off`, `LocalFailure` |
| `SendFlags` | 모든 send/request/reply builder의 `.Flags(...)` 단계(Messaging category) | `None`, `DontWait` |
| `RecvFlags` | 모든 `Recv`/`Subscribe`/`ReceiveSubscriptionEvent`/`RecvPart` | `None`, `DontWait` |

**선택 기준.** 두 flags enum 어느 쪽이든 `DontWait`는 blocking 호출을
non-blocking으로 바꿔 block하는 대신 `false`/back-pressure를 보고한다 — 그 호출에서
`false`가 정확히 무엇을 뜻하는지는 위 각 항목을 참고한다.

---

[`Contracts/Sockets/`](../../../../bindings/dotnet/src/Zlink/Contracts/Sockets/)와
[.NET 바인딩 스펙](../../spec/dotnet/README.ko.md)에서 전체 근거를 확인한다.
