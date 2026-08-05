[한국어](03-sockets.ko.md) | English

[Reference index](README.en.md)

# 03. Sockets

This category covers `Socket` (the shared base every socket type extends), `CommonSocketOptions`
and its per-type subclasses, the eight concrete socket interfaces, and the handler functional
interfaces. Every socket's `send`/`publish`/`request`/`reply` returns the operation-builder family
documented in the Messaging category — this category only covers where each builder starts and
what each socket type uniquely adds. Unlike dotnet, **there is no shared `IConnectableSocket`
layer** — `bind`/`connect`/`unbind`/`disconnect`/`disconnectRid` are redeclared independently on
each concrete socket interface rather than inherited from one shared connectable-socket tier. The
exact signatures are owned by
[`contracts/sockets/`](../../../../bindings/java/src/main/java/systems/zlink/contracts/sockets/).

---

## `Socket` shared base

The base interface every socket type extends: options, monitoring, TLS, disposal.

```java
socket.setTlsServer(certPem, keyPem, true);
try (SocketMonitor monitor = socket.monitorOpen(MonitorEventType.CONNECTED)) { /* ... */ }
socket.close();
```

**Options.** `options()` (returns `CommonSocketOptions`, below), `monitorOpen()`/
`monitorOpen(MonitorEventType... events)`, `setTlsServer(String certPem, String keyPem, boolean
requireClientCert)`, `setTlsClient(String caCertPem, String hostname, boolean trustSystem)`,
`setSendReadyHandler(SendReadyHandler)`, `close()`. Note `Socket` itself declares no `bind`/
`connect` — each concrete socket interface below redeclares its own lifecycle methods.

**Completion result.** All members are synchronous with no return value except `options()`/
`monitorOpen()`. `Socket extends AutoCloseable`.

**When to use.** Call `setTlsServer`/`setTlsClient` before `bind`/`connect` respectively.

---

## `CommonSocketOptions` and per-type option facades

The typed options facade shared by every socket type, reached via `socket.options()`. **Many
methods declared on `CommonSocketOptions` in source have no `public` modifier and are not
reachable from application code** — this entry lists only the public surface.

```java
socket.options().sendHwm(100_000L);
socket.options().linger(Duration.ofSeconds(1));
socket.options().submitRetryMode(SubmitRetryMode.LOCAL_FAILURE);
```

**Options — public.** `linger()`/`linger(Duration)`, `sendHwm()`/`sendHwm(long)` and
`recvHwm()`/`recvHwm(long)` (unsigned 64-bit accounted-byte HWM; use `Long.toUnsignedString(long)`
above `Long.MAX_VALUE`), `sendBuffer()`/`sendBuffer(int)`, `recvBuffer()`/`recvBuffer(int)`,
`sendTimeout()`/`sendTimeout(Duration)`, `recvTimeout()`/`recvTimeout(Duration)`,
`immediate()`/`immediate(boolean)`, `ridDuplicatePolicy()`/`ridDuplicatePolicy(RidDuplicatePolicy)`,
`connectTimeout()`/`connectTimeout(Duration)`, `ipv6()`/`ipv6(boolean)`,
`tcpNoDelay()`/`tcpNoDelay(boolean)`, `tcpKeepalive()`/`tcpKeepalive(int)`,
`maxMessageSize()`/`maxMessageSize(long)`, `backlog()`/`backlog(int)`,
`reconnectInterval()`/`reconnectInterval(Duration)`,
`reconnectIntervalMax()`/`reconnectIntervalMax(Duration)`,
`submitRetryMode()`/`submitRetryMode(SubmitRetryMode)`,
`submitRetryTimeout()`/`submitRetryTimeout(Duration)`,
`submitRetryAttempts()`/`submitRetryAttempts(int)`, `lastEndpoint()` (read-only).

**Options — declared but package-private (not reachable):** `affinity`, `rate`,
`recoveryInterval`, `handshakeInterval`, `routeValueMaxSize`, `tos`, `multicastHops`,
`multicastMaxTpdu`, `bindToDevice`, `tcpKeepaliveCount`, `tcpKeepaliveIdle`, `tcpKeepaliveInterval`,
`tcpMaxRt`, `conflate`, `blocky`, `invertMatching`, `fd`, `events`, `socketType`, `zmpMetadata`.

**Options — per-type subclasses.** `DealerSocketOptions`: `probe()`/`probe(boolean)` (both
public), `requestTimeout(Duration)` (**set-only, no getter**), `peerWeight(int)` (**set-only, no
getter** — unlike dotnet, whose `PeerWeight` has both). `RouterSocketOptions`: `mandatory()`/
`mandatory(boolean)`, `handover()`/`handover(boolean)` (shorthand over `ridDuplicatePolicy`),
`probe()`/`probe(boolean)`, `connectRoutingId()` (`Optional<RoutingId>`, read-only)/
`setConnectRoutingId(RoutingId)`, `requestTimeout()`/`requestTimeout(Duration)` (both directions,
unlike Dealer's), `peerWeight()`/`peerWeight(int)` (both directions). `StreamSocketOptions`:
`notifyEnabled()`/`notify(boolean)` (asymmetric getter/setter names — not `notify()`/`notify(...)`).
`PubSocketOptions`: `verbose()`/`verbose(boolean)`, `verboser()`/`verboser(boolean)`,
`noDrop()`/`noDrop(boolean)`, `manual()`/`manual(boolean)` (**the getter reads a client-side cached
field, not the native option** — see below), `manualLastValue()`/`manualLastValue(boolean)`,
`approveSubscribe(RoutingId)`/`rejectSubscribe(RoutingId)`, `welcomeMessage()`/
`welcomeMessage(Message)`, `topicsCount()` (read-only). `SubSocketOptions`: `topicsCount()` only.

**Completion result.** Every property get/set is synchronous. `PubSocketOptions.manual()`'s getter
returns the last value passed to `manual(boolean)` on this facade instance rather than reading the
native socket option back — if the option were ever changed through another path, this getter would
not reflect it.

**When to use.** Set `sendHwm`/`recvHwm` and `linger` before the socket starts exchanging messages
when the defaults don't fit the deployment. Treat the package-private options as unavailable to
application code — a spec-level question outside this reference's scope, not something to route
around.

---

## `PairSocket`

An exclusive one-to-one peering socket with no routing.

```java
try (PairSocket pair = context.createPairSocket()) {
    pair.send().message(Message.from("ping")).submit();
    Received received = new Received();
    if (pair.recv(received, RecvFlags.NONE)) { /* ... */ }
}
```

**Options.** `bind(String)`, `connect(String)`, `unbind(String)`, `disconnect(String)`,
`disconnectRid(RoutingId)`, `send()` (starts the shared `SendOperation` builder), `recv(Received
result, RecvFlags flags)`.

**Completion result.** `recv` returns `boolean` — `false` only when `RecvFlags.DONT_WAIT` is set
and no message is available.

**When to use.** Use PAIR for an exclusive point-to-point link — it has no peer routing and does
not load-balance.

---

## `DealerSocket`

Load-balances sends across its connected peers and can issue routed requests.

```java
try (DealerSocket dealer = context.createDealerSocket()) {
    dealer.setRoutingId(RoutingId.from("worker-3"));
    List<Message> reply = dealer.request().message(Message.from("payload")).await();
}
```

**Options.** `bind`/`connect`/`unbind`/`disconnect`/`disconnectRid` (same shape as `PairSocket`),
`setRoutingId(RoutingId)`/`getRoutingId()`, `send()`, `recv(Received, RecvFlags)`, `request()`
(starts the shared `RequestOperation` builder — no target parameter, since DEALER has no
API-level peer routing id), `options()` (overridden to return `DealerSocketOptions`).

**Completion result.** `recv` follows the same `boolean` convention as `PairSocket`.

**When to use.** Set `setRoutingId` before connecting so peers observe it from the first message.
DEALER has no protocol envelope helper to reply to an arbitrary token — reply from a received
request context (`Received.reply()`) or an explicit ROUTER reply surface instead.

---

## `RouterSocket`

Routes messages to peers addressed by routing id, and can reply to a specific peer's request.

```java
try (RouterSocket router = context.createRouterSocket()) {
    router.send(peerRid).message(Message.from("hello")).submit();
    router.setCompletionControlHandler((rid, parts) -> { /* ... */ });
}
```

**Options.** `bind`/`connect`/`unbind`/`disconnect`/`disconnectRid`, `setRoutingId(RoutingId)`/
`getRoutingId()`, `send(RoutingId)`, `recv(Received, RecvFlags)`, `request(RoutingId)` (the
Messaging category's `RequestOperation`, addressed to a specific peer), `reply(RoutingId, long
requestSequence)` (the Messaging category's `ReplyOperation`), `trySendCompletionControl(RoutingId
peerRid, List<Message> parts)` (does not consume `parts`), `setCompletionControlHandler
(CompletionControlHandler)` (the callback owns every message in `parts` and must close it once),
`options()` (returns `RouterSocketOptions`).

**Completion result.** `trySendCompletionControl` returns `boolean` — `false` only when the
completion connection is back-pressured. `recv` follows the `boolean` convention above.

**When to use.** Use `request(peerRid)`/`reply(rid, requestSequence)` for ROUTER-initiated or
ROUTER-answered request/reply, where DEALER cannot address a specific peer. Use
`trySendCompletionControl`/`setCompletionControlHandler` for an opaque bounded control record
independent from application-level receive.

---

## `PubSocket` / `XPubSocket`

PUB publishes topic-filtered messages, dropping ones with no matching subscriber; XPUB additionally
surfaces subscriber subscription/unsubscription events.

```java
try (PubSocket pub = context.createPubSocket()) {
    pub.publish("prices").message(Message.from(tick)).submit();
}

try (XPubSocket xpub = context.createXPubSocket()) {
    SubscriptionEvent evt = new SubscriptionEvent();
    if (xpub.receiveSubscriptionEvent(evt, RecvFlags.NONE)) { /* ... */ }
}
```

**Options.** `PubSocket`: `bind`/`connect`/`unbind`/`disconnect`/`disconnectRid`,
`setRoutingId(RoutingId)` (**no `getRoutingId()`** — set-only, unlike dotnet's `IPubSocket` which
has both), `publish(String topicId)` (starts the shared `SendOperation` builder), `options()`
(returns `PubSocketOptions`). `XPubSocket`: the same `bind`/`connect`/`unbind`/`disconnect`/
`disconnectRid`/`setRoutingId`/`publish`/`options()` (also `PubSocketOptions` — the same facade
type) plus `receiveSubscriptionEvent(SubscriptionEvent result, RecvFlags flags)`.

**Completion result.** `receiveSubscriptionEvent` returns `boolean` (same convention as `recv`
above).

**When to use.** Use `XPubSocket` specifically to observe subscriber churn via
`receiveSubscriptionEvent`, or manual admission via `PubSocketOptions.manual()`/`approveSubscribe`/
`rejectSubscribe`; otherwise the two behave the same for publishing.

---

## `SubSocket` / `XSubSocket`

SUB subscribes to topics with subscriptions set as socket options; XSUB carries its subscriptions
as messages instead.

```java
try (SubSocket sub = context.createSubSocket()) {
    sub.setSubscription("prices.");
    TopicMessage msg = new TopicMessage();
    if (sub.subscribe(msg, RecvFlags.NONE)) { /* ... */ }
}
```

**Options.** `SubSocket`: `bind`/`connect`/`unbind`/`disconnect`/`disconnectRid`,
`setSubscription(String filter)`/`unsetSubscription(String filter)` (subscriptions accumulate),
`subscriptionAt(int index)` (`Optional<SubscriptionEntry>`), `subscribe(TopicMessage result,
RecvFlags flags)`, `options()` (returns `SubSocketOptions`). **`XSubSocket` has the identical member
set** — every method independently redeclared with the same signatures; the only difference between
the two types is what SUB/XSUB themselves mean at the wire level, not anything visible in this
contract.

**Completion result.** `subscribe` returns `boolean` (same convention as `recv` above).

**When to use.** Use `SubSocket` for the common case; use `XSubSocket` specifically when
subscriptions must be carried as ordinary messages instead.

---

## `StreamSocket`

Exchanges framed packets directly with raw TCP peers, outside the zlink wire protocol used by every
other socket type.

```java
try (StreamSocket stream = context.createStreamSocket()) {
    stream.onPacket((routingId, header, body) -> { /* owns header/body */ });
}
```

**Options.** `bind(String)`, `unbind(String)` (no `connect`/`disconnect`/`disconnectRid` on this
interface, unlike every other socket type here), `setRoutingId(RoutingId)`/`getRoutingId()`,
`send(RoutingId)`, `recv(Received result, RecvFlags flags)`, `onPacket(StreamPacketHandler
handler)` (the handler receives `(RoutingId routingId, Message header, Message body)`), `options()`
(returns `StreamSocketOptions`).

**Completion result.** `recv` follows the `boolean` convention above.

**When to use.** Use `onPacket` for a callback-driven packet loop.

---

## Handler functional interfaces

Every callback registration point in this category takes a `@FunctionalInterface`.

| Interface | Registered by | Signature |
|---|---|---|
| `SendReadyHandler` | `Socket.setSendReadyHandler(...)` | `void onReady()` |
| `StreamPacketHandler` | `StreamSocket.onPacket(...)` | `void onPacket(RoutingId routingId, Message header, Message body)` |
| `CompletionControlHandler` | `RouterSocket.setCompletionControlHandler(...)` | `void onControl(RoutingId sourceRoutingId, List<Message> parts)` — owns every message in `parts` |
| `RequestCallback` | `RequestSubmitOperation.submit(callback)`/`RequestCallbackSubmitOperation.submit(callback)` (Messaging category) | `void onComplete(RequestResult result, List<Message> parts)` — owns `parts` only when `result == RequestResult.OK` |

---

## Socket enums

Shared enums referenced across every entry above.

| Enum | Used by | Values |
|---|---|---|
| `SocketType` | Internal socket-kind identification | `ANY`, `PAIR`, `PUB`, `SUB`, `DEALER`, `ROUTER`, `XPUB`, `XSUB`, `STREAM` |
| `AutoHwmProfile` | `ContextOptions.autoHwmProfile` (Core category) | `COMPACT`, `LOW_LATENCY`, `BALANCED`, `THROUGHPUT` |
| `AutoHwmRecalcReason` | Monitor status (Eventing category) — its `value()`/`fromValue()` helpers are package-private | `NONE`, `INITIAL`, `ROLE_CHANGE`, `POLICY_TOGGLE`, `REFRESH`, `DEFERRED_SHRINK` |
| `RidDuplicatePolicy` | `CommonSocketOptions.ridDuplicatePolicy`, `RouterSocketOptions.handover` | `REJECT`, `HANDOVER` |
| `SubmitRetryMode` | `CommonSocketOptions.submitRetryMode` | `OFF`, `LOCAL_FAILURE` |
| `SendFlags` | Every send/request/reply builder's `.flags(...)` stage (Messaging category) | `NONE`, `DONT_WAIT` |
| `RecvFlags` | Every `recv`/`subscribe`/`receiveSubscriptionEvent` | `NONE`, `DONT_WAIT` |
| `SendResult` | The outcome of a non-blocking send attempt | `SENT`, `BACKPRESSURED`, `NOT_READY` |
| `SubmitResult` | Mirrored by `ZlinkSubmitException` (Errors category) | `OK`, `BACKPRESSURED`, `NOT_CONNECTED`, `NOT_FOUND`, `TERMINATED`, `INVALID_HANDLE`, `INVALID_ARGUMENT`, `NOT_SUPPORTED`, `INVALID_STATE`, `THREAD_VIOLATION`, `OUT_OF_MEMORY`, `SEQ_EXHAUSTED`, `INTERNAL_ERROR`, `NOT_ADMITTED` |
| `RecvResult` | Mirrored by `ZlinkRecvException` (Errors category) | `OK`, `NO_DATA`(201), `BUSY`(202), `TERMINATED`(203), `INVALID_HANDLE`(204), `NOT_SUPPORTED`(205), `INTERNAL_ERROR`(206) |
| `RequestResult` | Mirrored by `ZlinkRequestException` (Errors category), delivered by `RequestCallback` | `OK`, `TIMED_OUT`(101), `NOT_FOUND`(102), `TERMINATED`(103), `PROTOCOL_ERROR`(104), `INTERNAL_ERROR`(105), `REJECTED`(106), `CONFLICT`(107), `BUSY`(108), `NOT_CONNECTED`(109), `INVALID_ARGUMENT`(110), `INVALID_STATE`(111), `NOT_SUPPORTED`(112), `BACKPRESSURED`(113) |

**When to use.** `DONT_WAIT` on either flags enum turns a blocking call into a non-blocking one
that reports `false`/back-pressure instead of blocking.

---

See [`contracts/sockets/`](../../../../bindings/java/src/main/java/systems/zlink/contracts/sockets/)
and the [Java binding spec](../../spec/java/README.en.md) for the full rationale.
