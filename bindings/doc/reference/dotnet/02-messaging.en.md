[한국어](02-messaging.ko.md) | English

[Reference index](README.en.md)

# 02. Messaging

This category covers message ownership, the receive envelope types (`Received`, `TopicMessage`,
`SubscriptionEvent`), and the shared send/request/reply operation-builder shape every socket-type
entry point in the Sockets category returns. The exact signatures are owned by
[`Contracts/Messaging/`](../../../../bindings/dotnet/src/Zlink/Contracts/Messaging/).
`Contracts/Messaging/MessageEnvelopeParts.cs` is `internal` and has no public contract entry.

---

## `Message`

Owns one zlink message payload — the unit every send, request, reply, and receive API moves.

```csharp
using Message empty = new Message();
using Message sized = new Message(4096);
using Message copy = Message.From("payload"u8);
using Message fromString = Message.From("hello", Encoding.UTF8);
```

**Options.** Constructors: `Message()` (empty), `Message(int size)` (writable storage,
`ArgumentOutOfRangeException` on negative size), `Message(ReadOnlySpan<byte>)`/
`Message(ReadOnlyMemory<byte>)` (snapshot copy). Static factories: `From(byte[])`,
`From(ReadOnlySpan<byte>)`, `From(ReadOnlyMemory<byte>)`, `From(ReadOnlySequence<byte>)`,
`From(Message)` (copies another message's payload), `From(string)`/`From(string, Encoding)`
(UTF-8 by default). Instance members: `Size`, `IsEmpty`, `RefCount`, `Allocate(int)` (static),
`AsSpan()`/`AsReadOnlySpan()` (writable/read-only views backed by this instance's storage),
`AsReadOnlyMemory()` (native-backed messages are copied into managed memory here), `ToArray()`,
`CopyTo(Span<byte>)`/`CopyTo(IBufferWriter<byte>)`/`TryCopyTo(Span<byte>, out int)`,
`GetString()`/`GetString(Encoding)`.

**Completion result.** Every member is synchronous. `Message` is `IDisposable`/`IAsyncDisposable` —
disposal releases payload storage. Span-returning members are valid only while the message remains
undisposed and unmoved; a submit that consumes this message (Sockets/Messaging builder category)
leaves the managed instance empty afterward — reading its payload then throws, but disposing it
stays safe and is still required to return pooled instances.

**When to use.** Use a sized or snapshot-copy constructor/factory to build an outbound payload; use
`AsSpan()`/`AsReadOnlySpan()` to read or write in place without an extra copy, and `ToArray()`/
`GetString()` when an independent managed copy is acceptable. `RefCount` reports the native
reference count only for a message that still owns its storage directly (not moved/disposed).

---

## `Received.Create()`

Creates a reusable receive envelope for the caller-provided-storage receive shape.

```csharp
using Received received = Received.Create();
bool ok = dealer.Recv(received);
```

**Options.** No parameters. `Received` has no public constructor — only `Create()`.

**Completion result.** Returns `Received` synchronously; the caller owns and must dispose it. A
receive API (Sockets category) overwrites its internal state on each successful call — reuse the
same instance across receives to avoid a per-recv allocation.

**When to use.** Create one `Received` per receive loop/thread and hand it to every subsequent
`Recv(Received, ...)` call on that loop, rather than allocating a fresh instance per message.

---

## `Received` members

Reads envelope metadata and message parts, or starts a reply/send addressed to the envelope's
source.

```csharp
if (received.RequestSeq is { } seq)
{
    received.Reply().Message(Message.From("ok")).Submit();
}
Message first = received.FirstPart();
```

**Options.** Read-only properties: `RoutingId` (`RoutingId?`, present when the receive path
provides one), `RequestSeq` (`ulong?`, present when replyable), `MessageType`
(`ReceivedMessageType`: `Raw`/`Request`/`Reply`/`ErrorReply`), `Parts` (`IReadOnlyList<Message>`),
`IsSinglePart`. Methods: `FirstPart()` (no ownership transfer), `SinglePartOrThrow()` (throws when
multipart), `Reply()` (valid only when `RequestSeq` has a value — see the shared builder shape
below), `Send()` (addressed to the envelope's source route).

**Completion result.** All members are synchronous. `Dispose()` releases the message parts owned
by this envelope unless another API has already transferred their ownership. `Reply()`/`Send()`
return a builder from the shared operation-builder shape documented below — submitting either
consumes this envelope's captured route the same way a `Message` part is consumed on submit.

**When to use.** Branch on `MessageType`/`RequestSeq` to decide whether an envelope is replyable.
Use `Reply()` to answer a request in place instead of looking up the source route separately; use
`Send()` for a non-reply message back to the same source.

---

## `TopicMessage`

Holds one received publish: its topic, source routing id, and message parts.

```csharp
using TopicMessage published = new TopicMessage();
bool ok = sub.Recv(published);
string topic = published.Topic;
```

**Options.** Public constructor `TopicMessage()` (unlike `Received`, this type is constructed
directly, not via a factory). Read-only members: `RoutingId` (`RoutingId?`), `Topic` (decoded
lazily from topic bytes), `Parts`, `IsSinglePart`, `FirstPart()`, `SinglePartOrThrow()`.

**Completion result.** Synchronous. `Dispose()` releases the parts this instance owns.

**When to use.** Reuse one instance across a subscribe-receive loop the same way `Received` is
reused, passing it to the SUB/XSUB receive API (Sockets category) on each call.

---

## `SubscriptionEvent`

Reports one subscriber's subscribe or unsubscribe, as observed by an XPUB socket.

```csharp
using SubscriptionEvent evt = new SubscriptionEvent();
bool ok = xpub.Recv(evt);
```

**Options.** Public constructor `SubscriptionEvent()`. Read-only members: `RoutingId`
(`RoutingId?`), `Topic` (`string`), `Subscribed` (`bool` — whether this event subscribed or
unsubscribed the topic). `SubscriptionEntry(string Filter, bool IsPattern)` is a related record
describing one active subscription (filter plus whether it is a pattern).

**Completion result.** Synchronous; no disposal — this type owns no native resources of its own.

**When to use.** Use on an XPUB socket's subscription-event receive path (Sockets category) to
observe subscriber churn instead of inferring it from application-level protocol messages.

---

## Send / request / reply operation-builder shape

The fluent builder every `Send`, routed send, `Publish`, `Request`, and `Reply` entry point (all
in the Sockets category) returns to accumulate parts, flags, and a terminal submit.

```csharp
dealer.Send(routingId).Message(Message.From("part-1")).Message(Message.From("part-2")).Submit();

IReadOnlyList<Message> reply = await dealer
    .Request(routingId)
    .Message(Message.From("payload"))
    .Timeout(TimeSpan.FromSeconds(5))
    .Async();

received.Reply().Message(Message.From("ok")).Submit();
```

**Options.** `SendOperation.Message(Message)` starts the chain; `SendSubmitOperation.Message(...)`
adds further parts, `.Flags(SendFlags)` sets submit-time flags, `.Submit()` is the terminal.
`RequestOperation`/`RequestSubmitOperation` mirror this shape plus `.Timeout(TimeSpan)`; calling
`.Flags(...)` on `RequestSubmitOperation` narrows the builder to
`RequestCallbackSubmitOperation`, dropping the awaitable `.Async()` path — only
`.Submit(RequestCallback)` remains reachable past that point. `ReplyOperation`/
`ReplySubmitOperation` mirror `SendOperation`/`SendSubmitOperation` but have no flags stage (the
core reply function takes no send-flag argument). The `Messages(IReadOnlyList<Message>)` extension
(`MessageOperations`) is a builder modifier available at every stage of all four families — it adds
several parts in one call in order and returns the same builder type; it is not an independent
entry point.

**Completion result.** `SendSubmitOperation.Submit()`/`ReplySubmitOperation.Submit()` are
synchronous; `Send`'s returns `bool` (`false` only when `SendFlags.DontWait` was set and the send
would have blocked — every other failure throws `ZlinkException`), `Reply`'s returns `void`.
`RequestSubmitOperation.Async(CancellationToken)` returns
`Task<IReadOnlyList<Message>>` — the caller owns and must dispose the reply messages.
`RequestSubmitOperation`/`RequestCallbackSubmitOperation.Submit(RequestCallback)` returns `bool`
(same `DontWait` convention as `Send`) and delivers the outcome later via the `RequestCallback`
delegate `(RequestResult result, IReadOnlyList<Message> parts)` — parts are populated only when
`result` is `RequestResult.Ok`. Every builder consumes its accumulated `Message` parts on a
successful submit only; on failure ownership is restored to the caller.

**When to use.** Use the awaitable `.Async()` path in async code; use `.Flags(...).Submit(callback)`
when a callback-completion surface is needed instead (for example, on a synchronous dispatch
thread that cannot await). Use `Reply()`/`Send()` from a `Received` envelope rather than
reconstructing the destination route by hand. Use the `Messages(...)` convenience when adding a
pre-built list of parts instead of chaining `.Message(...)` per part.

---

See [`Contracts/Messaging/`](../../../../bindings/dotnet/src/Zlink/Contracts/Messaging/) and the
[.NET binding spec](../../spec/dotnet/README.en.md) for the full rationale.
