한국어 | [English](03-sockets.en.md)

[레퍼런스 목차](README.ko.md)

# 03. Sockets

이 category는 `Socket`(모든 socket type이 확장하는 공유 기반),
`CommonSocketOptions`와 타입별 서브클래스, 8개 구체 socket interface, handler
functional interface를 다룬다. 모든 socket의
`send`/`publish`/`request`/`reply`는 Messaging category에 문서화된
operation-builder family를 반환한다 — 이 category는 각 builder가 어디서
시작하고 각 socket type이 고유하게 무엇을 더하는지만 다룬다. dotnet과 달리
**공유 `IConnectableSocket` 계층이 없다** — `bind`/`connect`/`unbind`/
`disconnect`/`disconnectRid`는 하나의 공유 connectable-socket tier에서
상속되는 게 아니라 각 구체 socket interface에 독립적으로 재선언된다. 정확한
signature는
[`contracts/sockets/`](../../../../bindings/java/src/main/java/systems/zlink/contracts/sockets/)가
소유한다.

---

## `Socket` 공유 기반

모든 socket type이 확장하는 기반 interface: option, monitoring, TLS, disposal.

```java
socket.setTlsServer(certPem, keyPem, true);
try (SocketMonitor monitor = socket.monitorOpen(MonitorEventType.CONNECTED)) { /* ... */ }
socket.close();
```

**Options.** `options()`(`CommonSocketOptions` 반환, 아래), `monitorOpen()`/
`monitorOpen(MonitorEventType... events)`, `setTlsServer(String certPem,
String keyPem, boolean requireClientCert)`, `setTlsClient(String caCertPem,
String hostname, boolean trustSystem)`, `setSendReadyHandler
(SendReadyHandler)`, `close()`. `Socket` 자체는 `bind`/`connect`를 선언하지
않는다 — 아래 각 구체 socket interface가 자신만의 lifecycle 메서드를
재선언한다.

**Completion result.** `options()`/`monitorOpen()`을 제외한 모든 member는
반환값 없이 동기다. `Socket extends AutoCloseable`이다.

**선택 기준.** `bind`/`connect` 전에 각각 `setTlsServer`/`setTlsClient`를
호출한다.

---

## `CommonSocketOptions`와 타입별 option facade

`socket.options()`로 도달하는, 모든 socket type이 공유하는 typed option
facade. **소스의 `CommonSocketOptions`에 선언된 많은 메서드가 `public`
수식어가 없어 application 코드에서 도달할 수 없다** — 이 항목은 public 표면만
나열한다.

```java
socket.options().sendHwm(100_000L);
socket.options().linger(Duration.ofSeconds(1));
socket.options().submitRetryMode(SubmitRetryMode.LOCAL_FAILURE);
```

**Options — public.** `linger()`/`linger(Duration)`, `sendHwm()`/
`sendHwm(long)`와 `recvHwm()`/`recvHwm(long)`(unsigned 64-bit accounted-byte
HWM; `Long.MAX_VALUE`를 넘으면 `Long.toUnsignedString(long)` 사용),
`sendBuffer()`/`sendBuffer(int)`, `recvBuffer()`/`recvBuffer(int)`,
`sendTimeout()`/`sendTimeout(Duration)`, `recvTimeout()`/
`recvTimeout(Duration)`, `immediate()`/`immediate(boolean)`,
`ridDuplicatePolicy()`/`ridDuplicatePolicy(RidDuplicatePolicy)`,
`connectTimeout()`/`connectTimeout(Duration)`, `ipv6()`/`ipv6(boolean)`,
`tcpNoDelay()`/`tcpNoDelay(boolean)`, `tcpKeepalive()`/`tcpKeepalive(int)`,
`maxMessageSize()`/`maxMessageSize(long)`, `backlog()`/`backlog(int)`,
`reconnectInterval()`/`reconnectInterval(Duration)`,
`reconnectIntervalMax()`/`reconnectIntervalMax(Duration)`,
`submitRetryMode()`/`submitRetryMode(SubmitRetryMode)`,
`submitRetryTimeout()`/`submitRetryTimeout(Duration)`,
`submitRetryAttempts()`/`submitRetryAttempts(int)`, `lastEndpoint()`(읽기
전용).

**Options — 선언은 됐지만 package-private(도달 불가):** `affinity`, `rate`,
`recoveryInterval`, `handshakeInterval`, `routeValueMaxSize`, `tos`,
`multicastHops`, `multicastMaxTpdu`, `bindToDevice`, `tcpKeepaliveCount`,
`tcpKeepaliveIdle`, `tcpKeepaliveInterval`, `tcpMaxRt`, `conflate`,
`blocky`, `invertMatching`, `fd`, `events`, `socketType`, `zmpMetadata`.

**Options — 타입별 서브클래스.** `DealerSocketOptions`: `probe()`/
`probe(boolean)`(둘 다 public), `requestTimeout(Duration)`(**set-only,
getter 없음**), `peerWeight(int)`(**set-only, getter 없음** — 둘 다 있는
dotnet의 `PeerWeight`와 다름). `RouterSocketOptions`: `mandatory()`/
`mandatory(boolean)`, `handover()`/`handover(boolean)`
(`ridDuplicatePolicy`의 shorthand), `probe()`/`probe(boolean)`,
`connectRoutingId()`(`Optional<RoutingId>`, 읽기 전용)/
`setConnectRoutingId(RoutingId)`, `requestTimeout()`/
`requestTimeout(Duration)`(Dealer와 달리 양방향 모두), `peerWeight()`/
`peerWeight(int)`(양방향 모두). `StreamSocketOptions`: `notifyEnabled()`/
`notify(boolean)`(getter/setter 이름이 비대칭 — `notify()`/`notify(...)`가
아님). `PubSocketOptions`: `verbose()`/`verbose(boolean)`, `verboser()`/
`verboser(boolean)`, `noDrop()`/`noDrop(boolean)`, `manual()`/
`manual(boolean)`(**getter가 native option이 아니라 client-side 캐시
필드를 읽는다** — 아래 참고), `manualLastValue()`/
`manualLastValue(boolean)`, `approveSubscribe(RoutingId)`/
`rejectSubscribe(RoutingId)`, `welcomeMessage()`/`welcomeMessage(Message)`,
`topicsCount()`(읽기 전용). `SubSocketOptions`: `topicsCount()`만.

**Completion result.** 모든 property get/set은 동기다.
`PubSocketOptions.manual()`의 getter는 native socket option을 다시 읽는
게 아니라 이 facade instance에서 마지막으로 `manual(boolean)`에 넘긴 값을
반환한다 — 다른 경로로 option이 바뀐 적이 있다면 이 getter는 그걸 반영하지
않는다.

**선택 기준.** 기본값이 배포 환경에 맞지 않을 때 socket이 메시지 교환을
시작하기 전에 `sendHwm`/`recvHwm`, `linger`를 설정한다. package-private
option은 application 코드에서 쓸 수 없는 것으로 취급한다 — 이 레퍼런스의
범위 밖인 스펙 차원의 질문이지 우회할 대상이 아니다.

---

## `PairSocket`

라우팅이 없는 배타적 1:1 peering socket.

```java
try (PairSocket pair = context.createPairSocket()) {
    pair.send().message(Message.from("ping")).submit();
    Received received = new Received();
    if (pair.recv(received, RecvFlags.NONE)) { /* ... */ }
}
```

**Options.** `bind(String)`, `connect(String)`, `unbind(String)`,
`disconnect(String)`, `disconnectRid(RoutingId)`, `send()`(공유
`SendOperation` builder 시작), `recv(Received result, RecvFlags flags)`.

**Completion result.** `recv`는 `boolean`을 반환한다 — `RecvFlags.DONT_WAIT`가
설정되고 메시지가 없을 때만 `false`다.

**선택 기준.** 배타적 point-to-point 링크엔 PAIR를 쓴다 — peer 라우팅이
없고 load-balance하지 않는다.

---

## `DealerSocket`

연결된 peer 전체에 send를 load-balance하고 routed request를 낼 수 있다.

```java
try (DealerSocket dealer = context.createDealerSocket()) {
    dealer.setRoutingId(RoutingId.from("worker-3"));
    List<Message> reply = dealer.request().message(Message.from("payload")).await();
}
```

**Options.** `bind`/`connect`/`unbind`/`disconnect`/`disconnectRid`
(`PairSocket`과 같은 형태), `setRoutingId(RoutingId)`/`getRoutingId()`,
`send()`, `recv(Received, RecvFlags)`, `request()`(공유 `RequestOperation`
builder 시작 — target 인자 없음, DEALER는 API 레벨 peer routing id가 없기
때문), `options()`(`DealerSocketOptions`를 반환하도록 override됨).

**Completion result.** `recv`는 `PairSocket`과 같은 `boolean` 관례를
따른다.

**선택 기준.** peer가 첫 메시지부터 이를 관찰하도록 connect 전에
`setRoutingId`를 설정한다. DEALER는 임의 token에 reply할 protocol envelope
helper가 없다 — 대신 수신된 request context(`Received.reply()`)나 명시적
ROUTER reply 표면에서 답한다.

---

## `RouterSocket`

routing id로 지정된 peer에게 메시지를 보내고, 특정 peer의 request에 reply할
수 있다.

```java
try (RouterSocket router = context.createRouterSocket()) {
    router.send(peerRid).message(Message.from("hello")).submit();
    router.setCompletionControlHandler((rid, parts) -> { /* ... */ });
}
```

**Options.** `bind`/`connect`/`unbind`/`disconnect`/`disconnectRid`,
`setRoutingId(RoutingId)`/`getRoutingId()`, `send(RoutingId)`,
`recv(Received, RecvFlags)`, `request(RoutingId)`(Messaging category의
`RequestOperation`, 특정 peer로 향함), `reply(RoutingId, long
requestSequence)`(Messaging category의 `ReplyOperation`),
`trySendCompletionControl(RoutingId peerRid, List<Message> parts)`
(`parts`를 소비하지 않음), `setCompletionControlHandler
(CompletionControlHandler)`(콜백이 `parts`의 모든 메시지를 소유하며 한 번
close해야 함), `options()`(`RouterSocketOptions` 반환).

**Completion result.** `trySendCompletionControl`은 `boolean`을 반환한다 —
completion connection이 back-pressure일 때만 `false`다. `recv`는 위
`boolean` 관례를 따른다.

**선택 기준.** DEALER가 특정 peer를 지정할 수 없는 ROUTER 주도·ROUTER
응답 request/reply엔 `request(peerRid)`/`reply(rid, requestSequence)`를
쓴다. application-level receive와 독립적인 opaque bounded control
record엔 `trySendCompletionControl`/`setCompletionControlHandler`를 쓴다.

---

## `PubSocket` / `XPubSocket`

PUB는 매칭되는 구독자가 없으면 버리는 topic-filtered 메시지를 publish하고,
XPUB는 추가로 구독자의 subscribe/unsubscribe event를 노출한다.

```java
try (PubSocket pub = context.createPubSocket()) {
    pub.publish("prices").message(Message.from(tick)).submit();
}

try (XPubSocket xpub = context.createXPubSocket()) {
    SubscriptionEvent evt = new SubscriptionEvent();
    if (xpub.receiveSubscriptionEvent(evt, RecvFlags.NONE)) { /* ... */ }
}
```

**Options.** `PubSocket`: `bind`/`connect`/`unbind`/`disconnect`/
`disconnectRid`, `setRoutingId(RoutingId)`(**`getRoutingId()`가 없다** —
set-only, 둘 다 있는 dotnet의 `IPubSocket`과 다르다),
`publish(String topicId)`(공유 `SendOperation` builder 시작),
`options()`(`PubSocketOptions` 반환). `XPubSocket`: 같은 `bind`/`connect`/
`unbind`/`disconnect`/`disconnectRid`/`setRoutingId`/`publish`/
`options()`(역시 `PubSocketOptions` — 같은 facade 타입)에 더해
`receiveSubscriptionEvent(SubscriptionEvent result, RecvFlags flags)`.

**Completion result.** `receiveSubscriptionEvent`는 위와 같은 관례로
`boolean`을 반환한다.

**선택 기준.** `receiveSubscriptionEvent`로 구독자 변동을 관찰하거나
`PubSocketOptions.manual()`/`approveSubscribe`/`rejectSubscribe`로 수동
admission을 하려면 특별히 `XPubSocket`을 쓴다. 그 외엔 publish 자체는 둘이
같게 동작한다.

---

## `SubSocket` / `XSubSocket`

SUB는 구독을 socket option으로 설정하는 방식으로 topic을 구독하고, XSUB는
대신 구독을 메시지로 실어 나른다.

```java
try (SubSocket sub = context.createSubSocket()) {
    sub.setSubscription("prices.");
    TopicMessage msg = new TopicMessage();
    if (sub.subscribe(msg, RecvFlags.NONE)) { /* ... */ }
}
```

**Options.** `SubSocket`: `bind`/`connect`/`unbind`/`disconnect`/
`disconnectRid`, `setSubscription(String filter)`/
`unsetSubscription(String filter)`(구독은 누적된다), `subscriptionAt(int
index)`(`Optional<SubscriptionEntry>`), `subscribe(TopicMessage result,
RecvFlags flags)`, `options()`(`SubSocketOptions` 반환). **`XSubSocket`은
member 집합이 동일하다** — 모든 메서드가 같은 signature로 독립적으로
재선언돼 있으며, 둘의 유일한 차이는 이 contract에서 보이는 게 아니라
SUB/XSUB 자체가 wire 레벨에서 뜻하는 것뿐이다.

**Completion result.** `subscribe`는 위와 같은 관례로 `boolean`을
반환한다.

**선택 기준.** 일반적인 경우엔 `SubSocket`을 쓴다. 구독을 일반 메시지로
실어 날라야 할 때만 특별히 `XSubSocket`을 쓴다.

---

## `StreamSocket`

다른 모든 socket type이 쓰는 zlink wire protocol 밖에서, raw TCP peer와
framed packet을 직접 주고받는다.

```java
try (StreamSocket stream = context.createStreamSocket()) {
    stream.onPacket((routingId, header, body) -> { /* header/body 소유 */ });
}
```

**Options.** `bind(String)`, `unbind(String)`(여기 interface엔 다른 모든
socket type과 달리 `connect`/`disconnect`/`disconnectRid`가 없다),
`setRoutingId(RoutingId)`/`getRoutingId()`, `send(RoutingId)`,
`recv(Received result, RecvFlags flags)`, `onPacket(StreamPacketHandler
handler)`(handler는 `(RoutingId routingId, Message header, Message body)`를
받음), `options()`(`StreamSocketOptions` 반환).

**Completion result.** `recv`는 위 `boolean` 관례를 따른다.

**선택 기준.** callback 기반 packet loop엔 `onPacket`을 쓴다.

---

## Handler functional interface

이 category의 모든 콜백 등록 지점은 `@FunctionalInterface`를 받는다.

| Interface | 등록하는 곳 | Signature |
|---|---|---|
| `SendReadyHandler` | `Socket.setSendReadyHandler(...)` | `void onReady()` |
| `StreamPacketHandler` | `StreamSocket.onPacket(...)` | `void onPacket(RoutingId routingId, Message header, Message body)` |
| `CompletionControlHandler` | `RouterSocket.setCompletionControlHandler(...)` | `void onControl(RoutingId sourceRoutingId, List<Message> parts)` — `parts`의 모든 메시지를 소유 |
| `RequestCallback` | `RequestSubmitOperation.submit(callback)`/`RequestCallbackSubmitOperation.submit(callback)`(Messaging category) | `void onComplete(RequestResult result, List<Message> parts)` — `result == RequestResult.OK`일 때만 `parts` 소유 |

---

## Socket enum

위 모든 항목에서 참조하는 공유 enum.

| Enum | 사용처 | 값 |
|---|---|---|
| `SocketType` | 내부 socket 종류 식별 | `ANY`, `PAIR`, `PUB`, `SUB`, `DEALER`, `ROUTER`, `XPUB`, `XSUB`, `STREAM` |
| `AutoHwmProfile` | `ContextOptions.autoHwmProfile`(Core category) | `COMPACT`, `LOW_LATENCY`, `BALANCED`, `THROUGHPUT` |
| `AutoHwmRecalcReason` | Monitor status(Eventing category) — `value()`/`fromValue()` helper는 package-private | `NONE`, `INITIAL`, `ROLE_CHANGE`, `POLICY_TOGGLE`, `REFRESH`, `DEFERRED_SHRINK` |
| `RidDuplicatePolicy` | `CommonSocketOptions.ridDuplicatePolicy`, `RouterSocketOptions.handover` | `REJECT`, `HANDOVER` |
| `SubmitRetryMode` | `CommonSocketOptions.submitRetryMode` | `OFF`, `LOCAL_FAILURE` |
| `SendFlags` | 모든 send/request/reply builder의 `.flags(...)` 단계(Messaging category) | `NONE`, `DONT_WAIT` |
| `RecvFlags` | 모든 `recv`/`subscribe`/`receiveSubscriptionEvent` | `NONE`, `DONT_WAIT` |
| `SendResult` | non-blocking send 시도의 결과 | `SENT`, `BACKPRESSURED`, `NOT_READY` |
| `SubmitResult` | `ZlinkSubmitException`이 반영(Errors category) | `OK`, `BACKPRESSURED`, `NOT_CONNECTED`, `NOT_FOUND`, `TERMINATED`, `INVALID_HANDLE`, `INVALID_ARGUMENT`, `NOT_SUPPORTED`, `INVALID_STATE`, `THREAD_VIOLATION`, `OUT_OF_MEMORY`, `SEQ_EXHAUSTED`, `INTERNAL_ERROR`, `NOT_ADMITTED` |
| `RecvResult` | `ZlinkRecvException`이 반영(Errors category) | `OK`, `NO_DATA`(201), `BUSY`(202), `TERMINATED`(203), `INVALID_HANDLE`(204), `NOT_SUPPORTED`(205), `INTERNAL_ERROR`(206) |
| `RequestResult` | `ZlinkRequestException`이 반영(Errors category), `RequestCallback`이 전달 | `OK`, `TIMED_OUT`(101), `NOT_FOUND`(102), `TERMINATED`(103), `PROTOCOL_ERROR`(104), `INTERNAL_ERROR`(105), `REJECTED`(106), `CONFLICT`(107), `BUSY`(108), `NOT_CONNECTED`(109), `INVALID_ARGUMENT`(110), `INVALID_STATE`(111), `NOT_SUPPORTED`(112), `BACKPRESSURED`(113) |

**선택 기준.** 두 flags enum 어느 쪽이든 `DONT_WAIT`는 blocking 호출을
non-blocking으로 바꿔 block하는 대신 `false`/back-pressure를 보고한다.

---

[`contracts/sockets/`](../../../../bindings/java/src/main/java/systems/zlink/contracts/sockets/)와
[Java 바인딩 스펙](../../spec/java/README.ko.md)에서 전체 근거를 확인한다.
