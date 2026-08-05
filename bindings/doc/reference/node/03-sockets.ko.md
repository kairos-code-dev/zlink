한국어 | [English](03-sockets.en.md)

[레퍼런스 목차](README.ko.md)

# 03. Sockets

이 category는 `Socket`/`ConnectableSocket`(공유 기반 interface),
`CommonSocketOptions`와 타입별 확장, 8개 구체 socket interface, 공유
상수를 다룬다. 모든 socket의 `send`/`publish`/`request`/`reply`는
Messaging category에 문서화된 operation-builder family를 반환한다 — 이
category는 각 builder가 어디서 시작하고 각 socket type이 고유하게 무엇을
더하는지만 다룬다. socket은 `Context`의 메서드가 아니라 최상위
`createXxxSocket(ctx)` 함수(Core category)로 생성된다. 정확한 signature는
[`contracts/sockets/`](../../../../bindings/node/src/zlink/contracts/sockets/)가
소유한다.

---

## `Socket` / `ConnectableSocket` 공유 기반

모든 socket type이 확장하는 기반 interface: binding, TLS, monitoring,
disposal, (그리고 `StreamSocket`을 제외한 모든 socket type의) outbound
connection.

```ts
socket.bind('tcp://*:5555');
socket.setTlsServer(certPath, keyPath, true);
const monitor = socket.monitorOpen(SOCKET_MONITOR_EVENT_ALL);
socket.close();
```

**Options.** `Socket`: `bind(endpoint: string)`, `unbind(endpoint:
string)`, `close()`, `monitorOpen(events?: number, handler?:
SocketMonitorHandler)`(지금까지 다룬 다른 모든 언어와 달리, **handler를
open 시점에 이 메서드의 두 번째 인자로 직접 등록할 수 있다**, 항상 이후에
별도의 `onEvent` 스타일 호출이 필요한 게 아니라), `setTlsServer(cert,
key, requireClientCert?)`, `setTlsClient(ca, hostname, trustSystem?)`.
`ConnectableSocket extends Socket`은 `connect(endpoint: string)`,
`disconnect(endpoint: string)`, `disconnectRid(routingId: RoutingId)`를
더한다. 아래 모든 구체 socket type은 **`StreamSocket`을 제외하고**
`ConnectableSocket`을 확장한다 — `StreamSocket`은 `Socket`을 직접
확장하고 자신의 `disconnectRid`를 독립적으로 선언한다(아래 참고).
`BaseSocket` union 타입(`PairSocket | PubSocket | SubSocket |
DealerSocket | RouterSocket | XPubSocket | XSubSocket | StreamSocket`)은
`proxy`/`proxySteerable`(Core category)처럼 임의 구체 socket type을
받는 API를 위해 export된다.

**Completion result.** `monitorOpen`을 제외한 모든 member는 반환값 없이
동기다. `monitorOpen`은 `MonitorSocket`(Eventing category)을 동기로
반환한다 — caller가 소유하며 반드시 close해야 한다.

**선택 기준.** `bind`/`connect` 전에 각각 `setTlsServer`/`setTlsClient`를
호출한다. `recv` 스타일 drain을 위해 monitor를 따로 유지할 필요가 없을
땐 handler를 `monitorOpen(events, handler)`에 직접 넘긴다.

---

## `CommonSocketOptions`와 타입별 확장

`socket.options`로 도달하는, 모든 socket type이 공유하는 typed option
facade. getter/setter 메서드 쌍이 아니라 일반 property 접근으로
get/set하는 mutable property의 순수 interface다.

```ts
socket.options.sendHwm = 100_000n;
socket.options.linger = 1000;
socket.options.submitRetryMode = SubmitRetryMode.LocalFailure;
```

**Options.** `linger`(`number`, ms), `sendHwm`/`recvHwm`(`bigint`,
accounted-byte HWM — `0n`은 무제한), `sendTimeout`/`recvTimeout`/
`connectTimeout`(`number`, ms), `immediate`(`boolean`),
`ridDuplicatePolicy`(`RidDuplicatePolicyValue`),
`ipv6`/`tcpNoDelay`(`boolean`), `tcpKeepalive`(`number`, -1/0/1),
`maxMsgSize`(`bigint`, `-1n`은 제한 없음), `lastEndpoint`(`string`, 읽기
전용), `backlog`(`number`), `reconnectInterval`/
`reconnectIntervalMax`(`number`, ms), `submitRetryMode`(**순수 `number`로
타입 지정, `SubmitRetryModeValue`가 아님** — `ridDuplicatePolicy`의 typed
property와 비교하면 비일관적), `submitRetryTimeout`(`number`, ms),
`submitRetryAttempts`(`number`). 타입별 확장: `DealerSocketOptions`는
`probe`(`boolean`), `requestTimeout`(`number`, ms),
`peerWeight`(`number`, 0-100)를 더한다. `RouterSocketOptions`는
`mandatory`/`handover`/`probe`(`boolean`), `connectRoutingId`
(`RoutingId | null`, 읽기 전용)에 더해 `setConnectRoutingId
(routingId)`, `requestTimeout`/`peerWeight`(Dealer와 같은 형태)를
더한다. `StreamSocketOptions`는 `notify`(`boolean`)를 더한다.
`PubSocketOptions`는 `verbose`/`verboser`/`noDrop`/`manual`/
`manualLastValue`(`boolean`), `topicsCount`(`number`, 읽기 전용),
`welcomeMessage()`(`Message` 반환, caller가 소유)/
`setWelcomeMessage(message: MessageLike)`,
`approveSubscribe(routingId)`/`rejectSubscribe(routingId)`(`manual`
필요)를 더한다. `SubSocketOptions`는 `topicsCount`(읽기 전용)만 더한다.

**Completion result.** 모든 property 읽기/쓰기는 동기다.

**선택 기준.** 기본값이 배포 환경에 맞지 않을 때 socket이 메시지 교환을
시작하기 전에 `sendHwm`/`recvHwm`, `linger`를 설정한다. wildcard 주소로
bind한 후 `lastEndpoint`를 읽는다.

---

## `PairSocket`

라우팅이 없는 배타적 1:1 peering socket. `DealerSocket`이 확장하는
기반 형태 역할을 한다(아래 참고) — Pair와 Dealer에 별도 공유
`IMessageSocket` 스타일 기반을 주는 dotnet/java/cpp와 달리, 여기선
`DealerSocket extends PairSocket`이 직접 확장한다.

```ts
const pair = createPairSocket(ctx);
pair.send().message(Message.from('ping')).submit();
const received = new Received();
if (pair.recv(received)) { /* ... */ }
```

**Options.** `options`(`CommonSocketOptions`), `send()`(공유
`SendOperation` builder 시작), `recv(result: Received, flags?:
RecvFlags)`, `setSendReadyHandler(handler: () => void)`.

**Completion result.** `recv`는 `boolean`을 반환한다 —
`RecvFlags.DontWait`가 설정되고 메시지가 없을 때만 `false`다.

**선택 기준.** 배타적 point-to-point 링크엔 PAIR를 쓴다 — peer 라우팅이
없고 load-balance하지 않는다.

---

## `DealerSocket`

연결된 peer 전체에 send를 load-balance하고 routed request를 낼 수
있다. `PairSocket`을 직접 확장한다(별도 공유 message-socket interface가
아니라).

```ts
const dealer = createDealerSocket(ctx);
dealer.setRoutingId(RoutingId.from('worker-3'));
const reply = await dealer.request().message(Message.from('payload')).submit();
```

**Options.** `PairSocket`의 모든 것에 더해: `options`
(`DealerSocketOptions`로 override됨), `setRoutingId(routingId)`/
`getRoutingId()`, `request()`(공유 `RequestOperation` builder 시작 —
target 인자 없음, DEALER는 API 레벨 peer routing id가 없기 때문).

**Completion result.** (상속된) `recv`는 `PairSocket`과 같은 `boolean`
관례를 따른다.

**선택 기준.** peer가 첫 메시지부터 이를 관찰하도록 connect 전에
`setRoutingId`를 설정한다. DEALER는 임의 token에 reply할 protocol
envelope helper가 없다 — 대신 수신된 request context
(`Received.reply()`)나 명시적 ROUTER reply 표면에서 답한다.

---

## `RouterSocket`

routing id로 지정된 peer에게 메시지를 보내고, 특정 peer의 request에
reply할 수 있다. `PairSocket`이 아니라 `ConnectableSocket`을 직접
확장한다.

```ts
const router = createRouterSocket(ctx);
router.send(peerRid).message(Message.from('hello')).submit();
router.setCompletionControlHandler((rid, parts) => { /* ... */ });
```

**Options.** `options`(`RouterSocketOptions`), `send(routingId)`,
`recv(result: Received, flags?: RecvFlags)`,
`setSendReadyHandler(handler)`, `setRoutingId(routingId)`/
`getRoutingId()`, `request(peerRid)`(Messaging category의
`RequestOperation`, 특정 peer로 향함), `reply(peerRid, requestSeq:
bigint)`(Messaging category의 `ReplyOperation`),
`trySendCompletionControl(peerRid, parts: readonly MessageLike[])`
(`parts`를 소비하지 않음), `setCompletionControlHandler(handler:
(sourceRoutingId, parts: Message[]) => void)`(handler가 수신 메시지를
소유).

**Completion result.** `trySendCompletionControl`은 `boolean`을
반환한다 — completion connection이 back-pressure일 때만 `false`다.
`recv`는 위 `boolean` 관례를 따른다.

**선택 기준.** DEALER가 특정 peer를 지정할 수 없는 ROUTER 주도·ROUTER
응답 request/reply엔 `request(peerRid)`/`reply(peerRid, requestSeq)`를
쓴다. application-level receive와 독립적인 opaque bounded control
record엔 `trySendCompletionControl`/`setCompletionControlHandler`를
쓴다.

---

## `PubSocket` / `XPubSocket`

PUB는 매칭되는 구독자가 없으면 버리는 topic-filtered 메시지를
publish하고, XPUB는 추가로 구독자의 subscribe/unsubscribe event를
노출한다. `XPubSocket extends PubSocket`.

```ts
const pub = createPubSocket(ctx);
pub.publish('prices').message(Message.from(tick)).submit();

const xpub = createXPubSocket(ctx);
const evt = new SubscriptionEvent();
if (xpub.receiveSubscriptionEvent(evt)) { /* ... */ }
```

**Options.** `PubSocket extends ConnectableSocket`: `options`
(`PubSocketOptions`), `publish(topic: string)`(공유 `SendOperation`
builder 시작), `setSendReadyHandler(handler)`. **`PubSocket`엔
`setRoutingId`/`getRoutingId`가 없다**(둘 다 있는 dotnet의
`IPubSocket`과 다름). `XPubSocket extends PubSocket`은
`receiveSubscriptionEvent(result: SubscriptionEvent, flags?:
RecvFlags)`만 더한다.

**Completion result.** `receiveSubscriptionEvent`는 위와 같은 관례로
`boolean`을 반환한다.

**선택 기준.** `receiveSubscriptionEvent`로 구독자 변동을 관찰하거나
`PubSocketOptions.manual`/`approveSubscribe`/`rejectSubscribe`로 수동
admission을 하려면 특별히 `XPubSocket`을 쓴다. 그 외엔 publish 자체는
둘이 같게 동작한다.

---

## `SubSocket` / `XSubSocket`

SUB는 구독을 socket option으로 설정하는 방식으로 topic을 구독하고,
XSUB는 대신 구독을 메시지로 실어 나른다. `XSubSocket extends SubSocket
{}` — 완전히 빈 interface 본문으로, 지금까지 다룬 모든 wrapper binding
중 가장 순수한 delta-only 선언이다.

```ts
const sub = createSubSocket(ctx);
sub.setSubscription('prices.');
const msg = new TopicMessage();
if (sub.subscribe(msg)) { /* ... */ }
```

**Options.** `SubSocket extends ConnectableSocket`: `options`
(`SubSocketOptions`), `setSubscription(filter: string)`/
`unsetSubscription(filter: string)`(구독은 누적된다),
`subscriptionAt(index: number)`(`SubscriptionEntry | null`),
`subscribe(result: TopicMessage, flags?: RecvFlags)`. `XSubSocket`은
아무것도 더하지 않는다 — 모든 member가 상속된 `SubSocket` 표면 그대로다.

**Completion result.** `subscribe`는 위와 같은 관례로 `boolean`을
반환한다.

**선택 기준.** 일반적인 경우엔 `SubSocket`을 쓴다. 구독을 일반 메시지로
실어 날라야 할 때만 특별히 `XSubSocket`을 쓴다 — interface 자체는 둘을
구분할 게 없으므로 선택은 전적으로 어떤 구체 타입을 생성하는지
(`createSubSocket` vs `createXSubSocket`)에 달려 있다.

---

## `StreamSocket`

다른 모든 socket type이 쓰는 zlink wire protocol 밖에서, raw TCP peer와
framed packet을 직접 주고받는다. (`ConnectableSocket`이 아니라)
`Socket`을 확장하고 자신의 `disconnectRid`를 독립적으로 선언한다.

```ts
const stream = createStreamSocket(ctx);
stream.setPacketHandler((sourceRid, header, body) => { /* header/body 소유 */ });
```

**Options.** `options`(`StreamSocketOptions`), `send(routingId)`,
`recv(result: Received, flags?: RecvFlags)`,
`setPacketHandler(handler: StreamPacketHandler)`,
`setSendReadyHandler(handler)`, `setRoutingId(routingId)`/
`getRoutingId()`, `disconnectRid(routingId)`(`StreamSocket`이
`ConnectableSocket`의 사본을 상속하지 않으므로 이 interface에 직접
선언됨).

**Completion result.** `recv`는 위 `boolean` 관례를 따른다. packet
handler는 수신하는 `header`와 `body` 메시지를 둘 다 소유한다.

**선택 기준.** callback 기반 packet loop엔 `setPacketHandler`를 쓴다.

---

## Socket 상수

위 모든 항목에서 참조하는 공유 상수 객체와 그 파생 타입.

| 상수 | 사용처 | 값 |
|---|---|---|
| `SocketType` | 내부 socket 종류 식별 | **이중 케이싱**: `ANY`/`PAIR`/`PUB`/`SUB`/`DEALER`/`ROUTER`/`XPUB`/`XSUB`/`STREAM`과 `Any`/`Pair`/`Pub`/`Sub`/`Dealer`/`Router`/`XPub`/`XSub`/`Stream` 둘 다 동일한 숫자값의 alias로 export됨 |
| `SOCKET_MONITOR_EVENT_ALL` | `Socket.monitorOpen(events)` | `0xFFFF` — "전체 구독" 편의 상수; 개별 lifecycle event flag(`Connected`, `Disconnected` 등)는 여기 `socket_constants.ts`가 아니라 Eventing category의 `contracts/eventing/monitor.ts`에 선언된 `MonitorEventType` 상수 객체다 |
| `RidDuplicatePolicy` | `CommonSocketOptions.ridDuplicatePolicy`, `RouterSocketOptions.handover` | `Reject`, `Handover` |
| `SubmitRetryMode` | `CommonSocketOptions.submitRetryMode`(property 자체는 순수 `number`로 타입 지정) | `Off`, `LocalFailure` |
| `SendFlags` | 모든 send/request/reply builder의 `.flags(...)` 단계(Messaging category) | `None`, `DontWait` |
| `RecvFlags` | 모든 `recv`/`subscribe`/`receiveSubscriptionEvent` | `None`, `DontWait` |
| `PollEventFlag` | Poller 등록/wait(Eventing category) | `PollIn`, `PollOut`, `PollErr`, `PollPri`, `PollCompletion` |

**선택 기준.** 두 flags 상수 어느 쪽이든 `DontWait`는 blocking 호출을
non-blocking으로 바꿔 block하는 대신 `false`/back-pressure를
보고한다. 모든 lifecycle event를 구독하려면 `monitorOpen(events)`에
`SOCKET_MONITOR_EVENT_ALL`을 넘기고, 구독을 필터링하려면 특정
`MonitorEventType` 값(Eventing category)을 OR해서 raw 숫자 mask로
넘긴다.

---

[`contracts/sockets/`](../../../../bindings/node/src/zlink/contracts/sockets/)와
[Node 바인딩 스펙](../../spec/node/README.ko.md)에서 전체 근거를 확인한다.
