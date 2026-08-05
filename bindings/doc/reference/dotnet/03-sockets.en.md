[한국어](03-sockets.ko.md) | English

[Reference index](README.en.md)

# 03. Sockets

This category covers the eight socket-type interfaces (created via the `IContext` factories in
the Core category), their shared lifecycle/option base, and their per-type typed options. Every
socket's `Send`/`Publish`/`Request`/`Reply` returns the operation-builder family documented in the
Messaging category — this category only covers where each builder starts and what each socket
type uniquely adds. The exact signatures are owned by
[`Contracts/Sockets/`](../../../../bindings/dotnet/src/Zlink/Contracts/Sockets/).

---

## `ISocket` / `IConnectableSocket` shared lifecycle

The base contract every socket type implements: binding, TLS, monitoring, disposal, and (for every
socket except the base marker) outbound connection.

```csharp
socket.Bind("tcp://*:5555");
socket.SetTlsServer(certPath, keyPath, requireClientCert: true);
using IZlinkSocket monitor = (IZlinkSocket)socket.MonitorOpen(SocketEvent.All);
socket.Close();
```

**Options.** `ISocket`: `Options` (`CommonSocketOptions`, below), `Bind(string address)`,
`Unbind(string address)`, `MonitorOpen(SocketEvent events = SocketEvent.All)`,
`SetTlsServer(certPath, keyPath, requireClientCert = false)` (apply before binding),
`SetTlsClient(caCertPath, hostname, trustSystem = false)` (apply before connecting), `Close()`.
`IConnectableSocket` (every socket type except the base `IZlinkSocket` marker) adds
`Connect(string address)`, `Disconnect(string address)`, `DisconnectRid(RoutingId peerRid)`.

**Completion result.** All members are synchronous with no return value except `MonitorOpen`,
which returns `ISocketMonitor` (Eventing category) synchronously — the caller owns and must
dispose it. `Close()` closes the native socket immediately; unlike `Dispose()`, it does not wait
on `IDisposable` semantics. `ISocket`/`IZlinkSocket` are themselves `IDisposable`/
`IAsyncDisposable`.

**When to use.** Call `SetTlsServer`/`SetTlsClient` before `Bind`/`Connect` respectively — applying
either after the socket is already bound or connected has no effect on existing state. Prefer
`using` for the socket itself; call `Close()` only when the native socket must release immediately
rather than through normal disposal.

---

## `CommonSocketOptions`

The typed options facade shared by every socket type, reached via `socket.Options`.

```csharp
socket.Options.SendHighWaterMark = 100_000;
socket.Options.Linger = TimeSpan.FromSeconds(1);
socket.Options.SubmitRetryMode = SubmitRetryMode.LocalFailure;
```

**Options.** `MaxMessageSize` (`long`, -1 = no limit), `SendHighWaterMark`/`ReceiveHighWaterMark`
(`ulong` accounted-byte limits, 0 = no limit — see the Core category's byte-HWM note),
`SendBufferSize`/`ReceiveBufferSize` (`int`, -1 = OS default), `Linger` (`TimeSpan?`, null = wait
indefinitely), `ReconnectInterval`/`ReconnectIntervalMax` (`TimeSpan?`, null disables/uncaps),
`Backlog` (`int`), `ReceiveTimeout`/`SendTimeout`/`ConnectTimeout`/`HandshakeInterval`
(`TimeSpan?`, null = block indefinitely / OS or native default), `TcpKeepAlive` (`int`, -1/0/1),
`IPv6`/`TcpNoDelay`/`Immediate` (`bool`), `SubmitRetryMode` (`SubmitRetryMode`),
`SubmitRetryTimeoutMilliseconds`/`SubmitRetryAttempts` (`int`), `RoutingIdDuplicatePolicy`
(`RidDuplicatePolicy`), `LastEndpoint` (`string`, read-only — the concrete resolved bind address).

**Completion result.** Every property get/set is synchronous.

**When to use.** Set `SendHighWaterMark`/`ReceiveHighWaterMark` and `Linger` before the socket
starts exchanging messages when the defaults don't fit the deployment. Read `LastEndpoint` after
binding to a wildcard address to learn the resolved port. Use `SubmitRetryMode.LocalFailure` when
a submit hitting local back-pressure should retry automatically rather than surface it to the
caller.

---

## `IPairSocket`

A PAIR socket: exclusive one-to-one peering with no routing, no fields or options beyond the
shared `IMessageSocket` surface.

```csharp
using IPairSocket pair = context.CreatePairSocket();
pair.Send().Message(Message.From("ping")).Submit();
using Received received = Received.Create();
if (pair.Recv(received)) { /* ... */ }
```

**Options.** `IMessageSocket` (shared by PAIR/DEALER): `Send()` (starts the shared `SendOperation`
builder — Messaging category), `Recv(Received result, RecvFlags flags = RecvFlags.None)`,
`OnSendReady(Action handler)` (back-pressure-cleared callback, runs on a background dispatch
thread). `IPairSocket` adds nothing beyond `IMessageSocket`.

**Completion result.** `Recv` returns `bool` — `false` only when `RecvFlags.DontWait` is set and
no message is available.

**When to use.** Use PAIR for an exclusive point-to-point link (for example, a control channel
between two fixed endpoints) — it has no peer routing and does not load-balance.

---

## `IDealerSocket`

A DEALER socket: load-balances sends across its connected peers and can issue routed requests.

```csharp
using IDealerSocket dealer = context.CreateDealerSocket();
dealer.SetRoutingId(RoutingId.From("worker-3"));
IReadOnlyList<Message> reply = await dealer.Request()
    .Message(Message.From("payload"))
    .Async();
```

**Options.** Adds to `IMessageSocket`: `Options` (`DealerSocketOptions`: `Probe` set-only —
send an empty probe on connect; `RequestTimeout` set-only `TimeSpan?`; `PeerWeight` `int`
load-balancing weight 0-100), `SetRoutingId(RoutingId)`/`GetRoutingId()`, `Request()` (starts the
shared `RequestOperation` builder — Messaging category; has no target parameter, since DEALER has
no API-level peer routing id to address).

**Completion result.** `Request()`'s builder resolves per the Messaging category's operation-
builder entry. `SetRoutingId`/`GetRoutingId` are synchronous.

**When to use.** Set `SetRoutingId` before connecting so peers observe it from the first message.
A DEALER cannot reply to an arbitrary token — it has no protocol envelope helper for that; replies
are answered from a received request context (`Received.Reply()`, Messaging category) or from an
explicit ROUTER/service reply surface when the target context requires it.

---

## `IRouterSocket`

A ROUTER socket: routes messages to peers addressed by routing id, and can reply to a specific
peer's request.

```csharp
using IRouterSocket router = context.CreateRouterSocket();
router.Send(peerRid).Message(Message.From("hello")).Submit();
router.OnCompletionControl((rid, parts) => { /* ... */ });
```

**Options.** Adds to `IRoutedMessageSocket` (`Send(RoutingId)`, `Recv(Received, RecvFlags)`,
`OnSendReady(Action)`) and `IConnectableSocket`: `Options` (`RouterSocketOptions`: `Mandatory`
`bool` — error instead of silent drop on an unknown route; `Handover` `bool` — shorthand over
`RoutingIdDuplicatePolicy`; `Probe` `bool`; `ConnectRoutingId` `RoutingId?` read-only plus
`SetConnectRoutingId(RoutingId)` to assign the next outbound connection's id instead of letting
the peer choose; `RequestTimeout` `TimeSpan?`; `PeerWeight` `int` 0-100), `SetRoutingId(RoutingId)`/
`GetRoutingId()`, `Request(RoutingId peerRid)` (Messaging category's `RequestOperation`),
`Reply(RoutingId rid, ulong requestSeq)` (Messaging category's `ReplyOperation`),
`TrySendCompletionControl(RoutingId peerRid, IReadOnlyList<Message> parts)`,
`OnCompletionControl(CompletionControlHandler handler)`.

**Completion result.** `TrySendCompletionControl` returns `bool` synchronously — it does not
consume `parts` (Core assigns no command meaning to the payload); `false` means completion-lane
back-pressure, other failures throw `ZlinkSubmitException`. `CompletionControlHandler` runs on a
background dispatch thread and owns every message in its `parts` — it must dispose each exactly
once.

**When to use.** Use `Request(peerRid)`/`Reply(rid, requestSeq)` for ROUTER-initiated or
ROUTER-answered request/reply, where DEALER cannot address a specific peer. Use
`TrySendCompletionControl`/`OnCompletionControl` for an opaque bounded control record on a peer's
existing completion connection, independent from application-level receive.

---

## `IPubSocket` / `IXPubSocket`

PUB publishes topic-filtered messages, dropping ones with no matching subscriber; XPUB additionally
surfaces subscriber subscription/unsubscription events.

```csharp
using IPubSocket pub = context.CreatePubSocket();
pub.Publish("prices").Message(Message.From(tick)).Submit();

using IXPubSocket xpub = context.CreateXPubSocket();
using SubscriptionEvent evt = new SubscriptionEvent();
if (xpub.ReceiveSubscriptionEvent(evt)) { /* ... */ }
```

**Options.** `IPublisherSocket` (shared by PUB/XPUB): `Publish(string topic)` (starts the shared
`SendOperation` builder), `OnSendReady(Action handler)`. `IPubSocket` adds `Options`
(`PubSocketOptions`, below), `SetRoutingId(RoutingId)`/`GetRoutingId()`. `IXPubSocket` adds
`Options` (also `PubSocketOptions` — the same facade type) and
`ReceiveSubscriptionEvent(SubscriptionEvent result, RecvFlags flags = RecvFlags.None)`; unlike
`IPubSocket`, **`IXPubSocket` has no `SetRoutingId`/`GetRoutingId` of its own** — it inherits only
`IPublisherSocket`. `PubSocketOptions`: `Verbose`/`Verboser` `bool` (deliver every (un)subscribe
message, including duplicates), `Manual` `bool` (subscriptions require `ApproveSubscribe`/
`RejectSubscribe` instead of auto-accept), `ManualLastValue` `bool` (manual mode that also replays
the last cached message per topic to a newly accepted subscriber), `NoDrop` `bool` (error instead
of silent drop on back-pressure), `WelcomeMessage` (`Message`, sent automatically to each newly
connected subscriber — the getter returns a caller-owned copy), `TopicsCount` (`int`, read-only),
`ApproveSubscribe(RoutingId)`/`RejectSubscribe(RoutingId)` (require `Manual`).

**Completion result.** `ReceiveSubscriptionEvent` returns `bool` — `false` only under
`RecvFlags.DontWait` with nothing available. `ApproveSubscribe`/`RejectSubscribe` are synchronous
with no return value.

**When to use.** Use `IXPubSocket` over `IPubSocket` specifically to observe subscriber churn via
`ReceiveSubscriptionEvent` (or manual admission via `Manual`/`ApproveSubscribe`/`RejectSubscribe`);
otherwise the two behave the same for publishing.

---

## `ISubSocket` / `IXSubSocket`

SUB subscribes to topics with subscriptions set as socket options; XSUB carries its subscriptions
as messages instead.

```csharp
using ISubSocket sub = context.CreateSubSocket();
sub.SetSubscription("prices.");
using TopicMessage msg = new TopicMessage();
if (sub.Subscribe(msg)) { /* ... */ }
```

**Options.** `ISubscriberSocket` (shared by SUB/XSUB): `SetSubscription(string topicOrPattern)`/
`UnsetSubscription(string topicOrPattern)` (subscriptions accumulate), `SubscriptionAt(int index)`
(`SubscriptionEntry?`, null when out of range), `Subscribe(TopicMessage result, RecvFlags flags =
RecvFlags.None)`. `ISubSocket` adds `Options` (`SubSocketOptions`: `TopicsCount` `int`,
read-only), `SetRoutingId(RoutingId)`/`GetRoutingId()`. **`IXSubSocket` adds nothing beyond
`ISubscriberSocket`** except its own `Options` (also typed `SubSocketOptions`) — it has no unique
member; every operation is the shared `ISubscriberSocket` surface.

**Completion result.** `Subscribe` returns `bool` — `false` only under `RecvFlags.DontWait` with
nothing available.

**When to use.** Use `ISubSocket` for the common case (subscriptions set as socket options); use
`IXSubSocket` specifically when subscriptions must be carried as ordinary messages instead (for
example, forwarding subscription state through a device that only relays messages).

---

## `IStreamSocket`

A STREAM socket: exchanges framed packets directly with raw TCP peers, outside the zlink wire
protocol used by every other socket type.

```csharp
using IStreamSocket stream = context.CreateStreamSocket();
stream.OnPacket((routingId, header, body) => { /* owns header/body; dispose each once */ });
```

**Options.** Extends `IRoutedMessageSocket`: `Options` (`StreamSocketOptions`: `Notify` `bool` —
deliver peer connect/disconnect as application messages), `OnPacket(StreamPacketHandler handler)`
(background-dispatch-thread callback; the handler owns and must dispose `header`/`body` exactly
once), `RecvPart(out RoutingId? sourceRoutingId, out Message? part, out bool hasMore, RecvFlags
flags = RecvFlags.None)` (the first receive call fixes this socket to receive mode — it cannot
combine with `OnPacket` registration), `DisconnectRid(RoutingId peerRid)`.

**Completion result.** `RecvPart` returns `bool` synchronously; the returned `part` is caller-owned
and must be disposed. `StreamPacketHandler` transfers message ownership to the callback, which must
dispose `header` and `body` exactly once.

**When to use.** Choose `OnPacket` for a callback-driven packet loop, or `RecvPart` for a
pull-based one — the two are mutually exclusive once the first receive call is made. Use `Notify`
when the application needs to observe raw peer connect/disconnect directly.

---

## Socket enums

Shared enums referenced across every entry above.

| Enum | Used by | Values |
|---|---|---|
| `SocketType` | Internal socket-kind identification | `Any`, `Pair`, `Pub`, `Sub`, `Dealer`, `Router`, `XPub`, `XSub`, `Stream` |
| `AutoHwmProfile` | `IContextOptions.AutoHwmProfile` (Core category) | `Compact`, `LowLatency`, `Balanced`, `Throughput` |
| `RidDuplicatePolicy` | `CommonSocketOptions.RoutingIdDuplicatePolicy`, `RouterSocketOptions.Handover` | `Reject`, `Handover` |
| `SubmitRetryMode` | `CommonSocketOptions.SubmitRetryMode` | `Off`, `LocalFailure` |
| `SendFlags` | Every send/request/reply builder's `.Flags(...)` stage (Messaging category) | `None`, `DontWait` |
| `RecvFlags` | Every `Recv`/`Subscribe`/`ReceiveSubscriptionEvent`/`RecvPart` | `None`, `DontWait` |

**When to use.** `DontWait` on either flags enum turns a blocking call into a non-blocking one that
reports `false`/back-pressure instead of blocking — see each entry above for exactly what `false`
means for that specific call.

---

See [`Contracts/Sockets/`](../../../../bindings/dotnet/src/Zlink/Contracts/Sockets/) and the
[.NET binding spec](../../spec/dotnet/README.en.md) for the full rationale.
